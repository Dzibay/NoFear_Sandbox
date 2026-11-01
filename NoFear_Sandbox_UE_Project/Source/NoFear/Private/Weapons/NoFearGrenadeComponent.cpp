#include "Weapons/NoFearGrenadeComponent.h"

#include "Animation/NoFearPoseComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Core/NoFearCharacter.h"
#include "EnhancedInputComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"
#include "Weapons/NoFearGrenade.h"
#include "Weapons/NoFearGrenadeData.h"
#include "Weapons/NoFearWeaponComponent.h"

namespace
{
	/** Откуда вылетает граната относительно центра капсулы: вперёд, вправо, вверх. */
	const FVector ThrowOffset(40.f, 20.f, 60.f);
	constexpr float GrenadeTraceRadius = 6.f;
	constexpr float ArcSimTime = 4.f;
	/** Бот не бросает сильнее, чем ThrowSpeed × это. */
	constexpr float BotMaxSpeedScale = 1.4f;

	void FillPathParams(FPredictProjectilePathParams& Params, const AActor* Owner)
	{
		Params.bTraceWithCollision = true;
		Params.bTraceWithChannel = false;
		Params.ObjectTypes = { UEngineTypes::ConvertToObjectType(ECC_WorldStatic), UEngineTypes::ConvertToObjectType(ECC_WorldDynamic) };
		Params.ProjectileRadius = GrenadeTraceRadius;
		Params.MaxSimTime = ArcSimTime;
		Params.SimFrequency = 20.f;
		Params.ActorsToIgnore.Add(const_cast<AActor*>(Owner));
	}
}

UNoFearGrenadeComponent::UNoFearGrenadeComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	GrenadeClass = ANoFearGrenade::StaticClass();

	static ConstructorHelpers::FObjectFinderOptional<UNoFearGrenadeData> DefaultData(TEXT("/Game/Weapons/Data/DA_Grenade.DA_Grenade"));
	GrenadeData = DefaultData.Get();
	static ConstructorHelpers::FObjectFinderOptional<UInputAction> DefaultAction(TEXT("/Game/Input/IA_Grenade.IA_Grenade"));
	GrenadeAction = DefaultAction.Get();
}

void UNoFearGrenadeComponent::BeginPlay()
{
	Super::BeginPlay();
	if (!bCountSet)
	{
		GrenadeCount = FMath::Clamp(StartingGrenades, 0, MaxGrenades);
	}
}

void UNoFearGrenadeComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(ReleaseTimer);
		World->GetTimerManager().ClearTimer(RecoverTimer);
	}
	if (HeldMesh)
	{
		HeldMesh->DestroyComponent();
		HeldMesh = nullptr;
	}
	Super::EndPlay(EndPlayReason);
}

void UNoFearGrenadeComponent::SetGrenadeCount(int32 NewCount)
{
	bCountSet = true;
	GrenadeCount = FMath::Clamp(NewCount, 0, MaxGrenades);
}

void UNoFearGrenadeComponent::AddGrenades(int32 Amount)
{
	SetGrenadeCount(GrenadeCount + Amount);
}

float UNoFearGrenadeComponent::GetRadius() const
{
	return GrenadeData ? GrenadeData->Radius : 0.f;
}

bool UNoFearGrenadeComponent::CanThrow() const
{
	// Раненый лёжа гранату не бросает.
	if (bOwnerDead || bAiming || bThrowing || GrenadeCount <= 0 || !GrenadeData || !GrenadeClass || UNoFearHealthComponent::IsActorIncapacitated(GetOwner()))
	{
		return false;
	}
	const ANoFearCharacter* Character = Cast<ANoFearCharacter>(GetOwner());
	const UNoFearWeaponComponent* Weapons = Character ? Character->GetWeaponComponent() : nullptr;
	return !Weapons || (!Weapons->IsWeaponBusy() && !Weapons->AreHandsBusy() && !Weapons->IsFireBlocked());
}

// ---------------------------------------------------------------------------
// Игрок
// ---------------------------------------------------------------------------

void UNoFearGrenadeComponent::SetupPlayerInput(UInputComponent* PlayerInputComponent)
{
	UEnhancedInputComponent* Input = Cast<UEnhancedInputComponent>(PlayerInputComponent);
	if (!Input || !GrenadeAction)
	{
		return;
	}
	Input->BindAction(GrenadeAction, ETriggerEvent::Started, this, &UNoFearGrenadeComponent::OnGrenadeStarted);
	Input->BindAction(GrenadeAction, ETriggerEvent::Completed, this, &UNoFearGrenadeComponent::OnGrenadeCompleted);
	// Потеря фокуса окна или смена контекста — не бросок, а отмена: граната остаётся в запасе.
	Input->BindAction(GrenadeAction, ETriggerEvent::Canceled, this, &UNoFearGrenadeComponent::OnGrenadeCanceled);
}

void UNoFearGrenadeComponent::OnGrenadeStarted()
{
	BeginAim();
}

void UNoFearGrenadeComponent::OnGrenadeCompleted()
{
	ReleaseThrow();
}

void UNoFearGrenadeComponent::OnGrenadeCanceled()
{
	CancelAim();
}

void UNoFearGrenadeComponent::BeginAim()
{
	if (!CanThrow())
	{
		return;
	}
	ANoFearCharacter* Character = Cast<ANoFearCharacter>(GetOwner());
	bAiming = true;
	if (Character)
	{
		if (UNoFearWeaponComponent* Weapons = Character->GetWeaponComponent())
		{
			Weapons->SetHandsBusy(true);
			Weapons->SetOverlayState(AimOverlayState);
		}
		if (UNoFearPoseComponent* Pose = Character->GetPoseComponent())
		{
			Pose->SetPoseHeld(ENoFearPose::Throw, true);
		}
	}
	SetHeldVisible(true);
}

void UNoFearGrenadeComponent::ReleaseThrow()
{
	if (!bAiming)
	{
		return;
	}
	bAiming = false;
	bThrowing = true;

	if (const ANoFearCharacter* Character = Cast<ANoFearCharacter>(GetOwner()))
	{
		if (UNoFearPoseComponent* Pose = Character->GetPoseComponent())
		{
			Pose->SetPoseHeld(ENoFearPose::Throw, false);
		}
	}
	if (ReleaseDelay > 0.f)
	{
		GetWorld()->GetTimerManager().SetTimer(ReleaseTimer, this, &UNoFearGrenadeComponent::SpawnAimedGrenade, ReleaseDelay, false);
	}
	else
	{
		SpawnAimedGrenade();
	}
}

void UNoFearGrenadeComponent::SpawnAimedGrenade()
{
	FVector Start, Velocity;
	GetAimedThrow(Start, Velocity);
	SpawnGrenade(Start, Velocity);
	FinishThrow();
}

void UNoFearGrenadeComponent::CancelAim()
{
	if (!bAiming)
	{
		return;
	}
	bAiming = false;
	if (const ANoFearCharacter* Character = Cast<ANoFearCharacter>(GetOwner()))
	{
		if (UNoFearPoseComponent* Pose = Character->GetPoseComponent())
		{
			Pose->SetPoseHeld(ENoFearPose::Throw, false);
		}
	}
	EndBusy();
}

FVector UNoFearGrenadeComponent::GetThrowStart(const FRotator& Facing) const
{
	const AActor* Owner = GetOwner();
	if (!Owner)
	{
		return FVector::ZeroVector;
	}
	const FRotator Yaw(0.f, Facing.Yaw, 0.f);
	return Owner->GetActorLocation() + Yaw.RotateVector(ThrowOffset);
}

void UNoFearGrenadeComponent::GetAimedThrow(FVector& OutStart, FVector& OutVelocity) const
{
	const APawn* Pawn = Cast<APawn>(GetOwner());
	if (!Pawn || !GrenadeData)
	{
		OutStart = OutVelocity = FVector::ZeroVector;
		return;
	}
	FRotator Aim = Pawn->GetControlRotation();
	Aim.Pitch = FMath::ClampAngle(FRotator::NormalizeAxis(Aim.Pitch) + GrenadeData->ThrowPitchOffset, -80.f, 80.f);
	OutStart = GetThrowStart(Aim);
	OutVelocity = Aim.Vector() * GrenadeData->ThrowSpeed;
}

bool UNoFearGrenadeComponent::PredictArc(TArray<FVector>& OutPoints, FVector& OutLanding) const
{
	OutPoints.Reset();
	OutLanding = FVector::ZeroVector;
	if (!GrenadeData || !GetOwner())
	{
		return false;
	}
	FVector Start, Velocity;
	GetAimedThrow(Start, Velocity);

	FPredictProjectilePathParams Params;
	Params.StartLocation = Start;
	Params.LaunchVelocity = Velocity;
	FillPathParams(Params, GetOwner());
	FPredictProjectilePathResult Result;
	const bool bHit = UGameplayStatics::PredictProjectilePath(this, Params, Result);
	for (const FPredictProjectilePathPointData& Point : Result.PathData)
	{
		OutPoints.Add(Point.Location);
	}
	OutLanding = bHit ? Result.HitResult.ImpactPoint : Result.LastTraceDestination.Location;
	return bHit;
}

// ---------------------------------------------------------------------------
// Бот
// ---------------------------------------------------------------------------

bool UNoFearGrenadeComponent::FindVelocityTo(const FVector& Start, const FVector& Target, FVector& OutVelocity) const
{
	if (!GrenadeData)
	{
		return false;
	}
	// Сначала навесом, потом выше: под низким потолком или над укрытием подходит другая дуга.
	for (const float Arc : { BotArcParam, FMath::Min(BotArcParam + 0.2f, 0.9f), FMath::Max(BotArcParam - 0.2f, 0.15f) })
	{
		FVector Velocity;
		if (!UGameplayStatics::SuggestProjectileVelocity_CustomArc(this, Velocity, Start, Target, 0.f, Arc))
		{
			continue;
		}
		if (Velocity.Size() > GrenadeData->ThrowSpeed * BotMaxSpeedScale)
		{
			continue;
		}
		FPredictProjectilePathParams Params;
		Params.StartLocation = Start;
		Params.LaunchVelocity = Velocity;
		FillPathParams(Params, GetOwner());
		FPredictProjectilePathResult Result;
		if (!UGameplayStatics::PredictProjectilePath(this, Params, Result))
		{
			continue;
		}
		if (FVector::Dist(Result.HitResult.ImpactPoint, Target) <= BotLandingTolerance)
		{
			OutVelocity = Velocity;
			return true;
		}
	}
	return false;
}

bool UNoFearGrenadeComponent::CanReach(FVector Target) const
{
	const AActor* Owner = GetOwner();
	if (!Owner)
	{
		return false;
	}
	const FVector Start = GetThrowStart((Target - Owner->GetActorLocation()).Rotation());
	FVector Velocity;
	return FindVelocityTo(Start, Target, Velocity);
}

bool UNoFearGrenadeComponent::ThrowGrenadeAt(FVector Target)
{
	const AActor* Owner = GetOwner();
	if (!CanThrow() || !Owner)
	{
		return false;
	}
	const FVector Start = GetThrowStart((Target - Owner->GetActorLocation()).Rotation());
	FVector Velocity;
	if (!FindVelocityTo(Start, Target, Velocity))
	{
		return false;
	}

	BotStart = Start;
	BotVelocity = Velocity;
	bThrowing = true;
	if (const ANoFearCharacter* Character = Cast<ANoFearCharacter>(Owner))
	{
		if (UNoFearWeaponComponent* Weapons = Character->GetWeaponComponent())
		{
			Weapons->SetHandsBusy(true);
		}
		if (UNoFearPoseComponent* Pose = Character->GetPoseComponent())
		{
			Pose->PlayPose(ENoFearPose::Throw, BotWindupTime + 0.1f);
		}
	}
	SetHeldVisible(true);

	FTimerDelegate Release = FTimerDelegate::CreateWeakLambda(this, [this]()
	{
		if (bOwnerDead)
		{
			return;
		}
		// За время замаха бот мог сместиться: точка вылета едет вместе с ним.
		const AActor* Thrower = GetOwner();
		const FVector Start = Thrower ? GetThrowStart(BotVelocity.Rotation()) : BotStart;
		SpawnGrenade(Start, BotVelocity);
		FinishThrow();
	});
	GetWorld()->GetTimerManager().SetTimer(ReleaseTimer, Release, FMath::Max(BotWindupTime, 0.01f), false);
	return true;
}

// ---------------------------------------------------------------------------
// Общее
// ---------------------------------------------------------------------------

void UNoFearGrenadeComponent::SpawnGrenade(FVector Start, FVector Velocity)
{
	SetHeldVisible(false);
	APawn* Pawn = Cast<APawn>(GetOwner());
	UWorld* World = GetWorld();
	if (!Pawn || !World || !GrenadeClass || GrenadeCount <= 0)
	{
		return;
	}
	const FTransform SpawnTransform(Velocity.Rotation(), Start);
	ANoFearGrenade* Grenade = World->SpawnActorDeferred<ANoFearGrenade>(GrenadeClass, SpawnTransform, Pawn, Pawn, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!Grenade)
	{
		return;
	}
	Grenade->Data = GrenadeData;
	UGameplayStatics::FinishSpawningActor(Grenade, SpawnTransform);
	Grenade->Launch(Velocity);
	--GrenadeCount;
	LastGrenade = Grenade;
	OnGrenadeThrown.Broadcast(this, Grenade);
}

void UNoFearGrenadeComponent::FinishThrow()
{
	if (RecoverTime > 0.f)
	{
		GetWorld()->GetTimerManager().SetTimer(RecoverTimer, this, &UNoFearGrenadeComponent::EndBusy, RecoverTime, false);
	}
	else
	{
		EndBusy();
	}
}

void UNoFearGrenadeComponent::EndBusy()
{
	bThrowing = false;
	SetHeldVisible(false);
	const ANoFearCharacter* Character = Cast<ANoFearCharacter>(GetOwner());
	if (UNoFearWeaponComponent* Weapons = Character ? Character->GetWeaponComponent() : nullptr)
	{
		Weapons->SetHandsBusy(false);
	}
}

void UNoFearGrenadeComponent::SetHeldVisible(bool bVisible)
{
	if (!bVisible)
	{
		if (HeldMesh)
		{
			HeldMesh->SetVisibility(false);
		}
		return;
	}
	const ACharacter* Character = Cast<ACharacter>(GetOwner());
	USkeletalMeshComponent* CharacterMesh = Character ? Character->GetMesh() : nullptr;
	if (!CharacterMesh)
	{
		return;
	}
	if (!HeldMesh)
	{
		UStaticMesh* Sphere = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere"));
		HeldMesh = NewObject<UStaticMeshComponent>(GetOwner(), TEXT("HeldGrenade"));
		HeldMesh->SetStaticMesh(Sphere);
		HeldMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		HeldMesh->SetCastShadow(false);
		HeldMesh->SetRelativeScale3D(FVector(0.16f, 0.16f, 0.2f));
		HeldMesh->SetupAttachment(CharacterMesh, HandBone);
		HeldMesh->RegisterComponent();
		if (UMaterialInterface* ShapeMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial")))
		{
			HeldMesh->SetMaterial(0, ShapeMaterial);
			if (UMaterialInstanceDynamic* Material = HeldMesh->CreateAndSetMaterialInstanceDynamic(0))
			{
				Material->SetVectorParameterValue(TEXT("Color"), GrenadeData ? GrenadeData->Color : FLinearColor::Green);
			}
		}
		// Чуть в ладонь, а не в центр кисти.
		HeldMesh->SetRelativeLocation(FVector(8.f, 3.f, 0.f));
	}
	HeldMesh->SetVisibility(true);
}

void UNoFearGrenadeComponent::HandleOwnerDeath()
{
	if (bOwnerDead)
	{
		return;
	}
	bOwnerDead = true;
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(ReleaseTimer);
		World->GetTimerManager().ClearTimer(RecoverTimer);
	}
	bAiming = false;
	bThrowing = false;
	SetHeldVisible(false);
}
