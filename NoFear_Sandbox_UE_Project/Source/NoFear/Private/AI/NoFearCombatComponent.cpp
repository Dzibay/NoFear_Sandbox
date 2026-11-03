#include "AI/NoFearCombatComponent.h"
#include "Core/NoFearPerf.h"

#include "AI/NoFearCombatSubsystem.h"
#include "AI/NoFearAIController.h"
#include "AI/NoFearMonsterKinds.h"
#include "AI/NoFearOpportunitySubsystem.h"
#include "AIController.h"
#include "Animation/NoFearPoseComponent.h"
#include "BrainComponent.h"
#include "Core/NoFearCharacter.h"
#include "Weapons/NoFearWeapon.h"
#include "Weapons/NoFearWeaponComponent.h"
#include "Weapons/NoFearWeaponData.h"
#include "Core/NoFearFearComponent.h"
#include "Core/NoFearFearSubsystem.h"
#include "Core/NoFearHealthComponent.h"
#include "Core/NoFearTeam.h"
#include "EngineUtils.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "Blueprint/AIBlueprintHelperLibrary.h"
#include "CollisionQueryParams.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"

UNoFearCombatComponent::UNoFearCombatComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
}

void UNoFearCombatComponent::BeginPlay()
{
	Super::BeginPlay();

	SetComponentTickInterval(UpdateInterval);

	if (bUseAccelerationForPaths)
	{
		if (const ACharacter* Character = Cast<ACharacter>(GetOwner()))
		{
			if (UCharacterMovementComponent* Movement = Character->GetCharacterMovement())
			{
				Movement->GetNavMovementProperties()->bUseAccelerationForPaths = true;
			}
		}
	}

	if (UNoFearCombatSubsystem* Subsystem = GetWorld()->GetSubsystem<UNoFearCombatSubsystem>())
	{
		Subsystem->RegisterCombatant(this);
	}

	if (UNoFearHealthComponent* Health = UNoFearHealthComponent::FindHealth(GetOwner()))
	{
		Health->OnDeath.AddDynamic(this, &UNoFearCombatComponent::HandleOwnerDeath);
	}
}

void UNoFearCombatComponent::HandleOwnerDeath(UNoFearHealthComponent* Component, const FNoFearDamageInfo& Info)
{
	SetComponentTickEnabled(false);
	if (UNoFearCombatSubsystem* Subsystem = GetWorld()->GetSubsystem<UNoFearCombatSubsystem>())
	{
		Subsystem->UnregisterCombatant(this);
	}

	bManualTarget = false;
	if (CurrentTarget.IsValid())
	{
		ChangeTarget(nullptr);
	}
	Suppression = 0.f;
	RefreshSuppressedState();
	WriteBlackboard();
}

void UNoFearCombatComponent::HandleActorDied(AActor* DeadActor)
{
	if (LastShooter.Get() == DeadActor)
	{
		LastShooter.Reset();
	}
	if (CurrentTarget.Get() != DeadActor)
	{
		return;
	}

	bManualTarget = false;
	ChangeTarget(nullptr);
	if (UBlackboardComponent* Blackboard = GetBlackboard(); Blackboard && !TargetKeyName.IsNone())
	{
		Blackboard->ClearValue(TargetKeyName);
	}
	WriteBlackboard();
}

void UNoFearCombatComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* World = GetWorld())
	{
		if (UNoFearCombatSubsystem* Subsystem = World->GetSubsystem<UNoFearCombatSubsystem>())
		{
			Subsystem->UnregisterCombatant(this);
		}
	}

	Super::EndPlay(EndPlayReason);
}

void UNoFearCombatComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	NOFEAR_PERF_SCOPE(Combat);
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (!bManualTarget && !TargetKeyName.IsNone())
	{
		if (const UBlackboardComponent* Blackboard = GetBlackboard())
		{
			AActor* BlackboardTarget = Cast<AActor>(Blackboard->GetValueAsObject(TargetKeyName));
			if (BlackboardTarget != CurrentTarget.Get())
			{
				ChangeTarget(BlackboardTarget);
			}
		}
	}
	else if (bManualTarget && !CurrentTarget.IsValid())
	{
		// Цель, заданная вручную, уничтожена.
		bManualTarget = false;
		ChangeTarget(nullptr);
	}

	UpdateLineOfSight();
	UpdateSuppression(DeltaTime);
	UpdatePhantom();
	UpdatePinRescue();
	WriteBlackboard();
}

// --- Видения ---

void UNoFearCombatComponent::SeePhantom(FVector Point, float Duration)
{
	APawn* Pawn = Cast<APawn>(GetOwner());
	AAIController* Controller = Pawn ? Cast<AAIController>(Pawn->GetController()) : nullptr;
	if (!Controller || Duration <= 0.f || UNoFearHealthComponent::IsActorIncapacitated(Pawn))
	{
		return;
	}
	// Дерево уже держат пошатывание, граната или портал — им не до видений.
	UBrainComponent* Brain = Controller->BrainComponent;
	if (!IsSeeingPhantom())
	{
		if (Brain && Brain->IsPaused())
		{
			return;
		}
		if (Brain)
		{
			Brain->PauseLogic(TEXT("Phantom"));
		}
	}
	PhantomPoint = Point;
	PhantomUntil = GetNow() + Duration;
	NextPhantomTrigger = 0.0;
	++PhantomCount;
	Controller->StopMovement();
	Controller->SetFocalPoint(Point, EAIFocusPriority::Gameplay);
}

void UNoFearCombatComponent::ClearPhantom()
{
	if (!IsSeeingPhantom())
	{
		return;
	}
	PhantomUntil = 0.0;
	APawn* Pawn = Cast<APawn>(GetOwner());
	if (const ANoFearCharacter* Character = Cast<ANoFearCharacter>(Pawn))
	{
		if (UNoFearWeaponComponent* Weapons = Character->GetWeaponComponent())
		{
			Weapons->StopFire();
		}
	}
	bPhantomTriggerHeld = false;
	AAIController* Controller = Pawn ? Cast<AAIController>(Pawn->GetController()) : nullptr;
	if (!Controller)
	{
		return;
	}
	// Паузу перехватили граната или портал — фокус и дерево теперь их.
	const ANoFearAIController* Bot = Cast<ANoFearAIController>(Controller);
	if (Bot && (Bot->IsEvadingGrenade() || Bot->IsPortalControlled()))
	{
		return;
	}
	Controller->ClearFocus(EAIFocusPriority::Gameplay);
	if (Controller->BrainComponent && Controller->BrainComponent->IsPaused() && !UNoFearHealthComponent::IsActorIncapacitated(Pawn))
	{
		Controller->BrainComponent->ResumeLogic(TEXT("Phantom over"));
	}
}

void UNoFearCombatComponent::UpdatePhantom()
{
	if (!IsSeeingPhantom())
	{
		return;
	}
	const ANoFearCharacter* Character = Cast<ANoFearCharacter>(GetOwner());
	const double Now = GetNow();
	const ANoFearAIController* Bot = Character ? Cast<ANoFearAIController>(Character->GetController()) : nullptr;
	if (!Character || Now >= PhantomUntil || UNoFearHealthComponent::IsActorIncapacitated(Character)
		|| (Bot && (Bot->IsEvadingGrenade() || Bot->IsPortalControlled())))
	{
		ClearPhantom();
		return;
	}
	UNoFearWeaponComponent* Weapons = Character->GetWeaponComponent();
	ANoFearWeapon* Weapon = Weapons ? Weapons->GetCurrentWeapon() : nullptr;
	if (!Weapon || !Weapon->GetWeaponData() || Weapon->GetWeaponState() != ENoFearWeaponState::Idle)
	{
		return;
	}
	if (Weapon->GetMagazineAmmo() <= 0)
	{
		Weapons->StopFire();
		bPhantomTriggerHeld = false;
		Weapons->Reload();
		return;
	}
	// Ствол сначала доворачивается на пустое место.
	const FVector ToPoint = (PhantomPoint - Character->GetActorLocation()).GetSafeNormal2D();
	if (FVector::DotProduct(Character->GetBaseAimRotation().Vector().GetSafeNormal2D(), ToPoint) < 0.9f)
	{
		return;
	}
	if (Weapon->GetWeaponData()->FireMode == ENoFearFireMode::Auto)
	{
		if (!bPhantomTriggerHeld)
		{
			Weapons->StartFire();
			bPhantomTriggerHeld = true;
		}
	}
	else if (Now >= NextPhantomTrigger)
	{
		Weapons->StartFire();
		Weapons->StopFire();
		NextPhantomTrigger = Now + FMath::FRandRange(0.2f, 0.4f);
	}
}

// --- Спасение из-под ловчего ---

APawn* UNoFearCombatComponent::FindPinnedAllyCatcher() const
{
	const APawn* Pawn = Cast<APawn>(GetOwner());
	const ANoFearAIController* Bot = Pawn ? Cast<ANoFearAIController>(Pawn->GetController()) : nullptr;
	UWorld* World = GetWorld();
	if (!Bot || !World || Bot->PinRescueRadius <= 0.f)
	{
		return nullptr;
	}
	TArray<ANoFearJumperAIController*> Pinners;
	ANoFearJumperAIController::GetActivePinners(World, Pinners);
	const FVector Eyes = GetEyesLocation();
	APawn* Best = nullptr;
	float BestDistance = Bot->PinRescueRadius;
	for (const ANoFearJumperAIController* Pinner : Pinners)
	{
		APawn* Catcher = Pinner ? Pinner->GetPawn() : nullptr;
		const AActor* Victim = Pinner ? Pinner->GetPinVictim() : nullptr;
		if (!Catcher || !Victim || Victim == Pawn || UNoFearHealthComponent::IsActorDead(Catcher)
			|| UNoFearTeamLibrary::AreHostile(Pawn, Victim) || !UNoFearTeamLibrary::AreHostile(Pawn, Catcher))
		{
			continue;
		}
		const float Distance = FVector::Dist(Pawn->GetActorLocation(), Catcher->GetActorLocation());
		if (Distance > BestDistance)
		{
			continue;
		}
		FCollisionQueryParams Params(SCENE_QUERY_STAT(NoFearPinRescueSight), false, Pawn);
		Params.AddIgnoredActor(Victim);
		Params.AddIgnoredActor(Catcher);
		if (World->LineTraceTestByChannel(Eyes, Catcher->GetActorLocation(), ECC_Visibility, Params))
		{
			continue;
		}
		Best = Catcher;
		BestDistance = Distance;
	}
	return Best;
}

void UNoFearCombatComponent::UpdatePinRescue()
{
	ANoFearCharacter* Character = Cast<ANoFearCharacter>(GetOwner());
	ANoFearAIController* Bot = Character ? Cast<ANoFearAIController>(Character->GetController()) : nullptr;
	UNoFearWeaponComponent* Weapons = Character ? Character->GetWeaponComponent() : nullptr;
	ANoFearWeapon* Weapon = Weapons ? Weapons->GetCurrentWeapon() : nullptr;
	const double Now = GetNow();
	if (!Bot || !Weapon || UNoFearHealthComponent::IsActorIncapacitated(Character) || IsSeeingPhantom()
		|| Bot->IsEvadingGrenade() || Bot->IsPortalControlled())
	{
		ClearPinRescue();
		return;
	}

	APawn* Catcher = RescueCatcher.Get();
	if (!Catcher)
	{
		// Берётся спасать, только если дерево свободно: пошатывание и прочие паузы важнее.
		UBrainComponent* Brain = Bot->BrainComponent;
		if (Brain && Brain->IsPaused())
		{
			return;
		}
		Catcher = FindPinnedAllyCatcher();
		if (!Catcher)
		{
			return;
		}
		if (Brain)
		{
			Brain->PauseLogic(TEXT("PinRescue"));
		}
		RescueCatcher = Catcher;
		RescueLastSeenTime = Now;
		NextRescueTrigger = 0.0;
		bRescueTriggerHeld = false;
		++PinRescueCount;
		Bot->StopMovement();
		Bot->SetFocus(Catcher, EAIFocusPriority::Gameplay);
		SetTarget(Catcher);
	}

	// Ловчий отпустил (сбили, умер) или пропал из виду дольше секунды — назад к дереву.
	const ANoFearJumperAIController* Pinner = Cast<ANoFearJumperAIController>(Catcher->GetController());
	if (!Pinner || !Pinner->IsPinning() || UNoFearHealthComponent::IsActorDead(Catcher))
	{
		ClearPinRescue();
		return;
	}
	if (FindPinnedAllyCatcher() == Catcher)
	{
		RescueLastSeenTime = Now;
	}
	else if (Now - RescueLastSeenTime > 1.0)
	{
		ClearPinRescue();
		return;
	}

	// Вплотную — прикладом: ловчий слетает с жертвы от удара.
	const float Distance = FVector::Dist(Character->GetActorLocation(), Catcher->GetActorLocation());
	if (Distance <= ButtStrikeRange && Now >= NextButtStrikeTime)
	{
		NextButtStrikeTime = Now + ButtStrikeCooldown;
		Weapons->StopFire();
		bRescueTriggerHeld = false;
		++ButtStrikeCount;
		if (UNoFearPoseComponent* Pose = Character->GetPoseComponent())
		{
			Pose->PlayPose(ENoFearPose::Swing, 0.25f, 0.6f);
		}
		UGameplayStatics::ApplyDamage(Catcher, ButtStrikeDamage, Bot, Character, UDamageType::StaticClass());
		return;
	}

	if (!Weapon->GetWeaponData() || Weapon->GetWeaponState() != ENoFearWeaponState::Idle)
	{
		return;
	}
	if (Weapon->GetMagazineAmmo() <= 0)
	{
		Weapons->StopFire();
		bRescueTriggerHeld = false;
		Weapons->Reload();
		return;
	}
	// Ствол сначала доворачивается на ловчего.
	const FVector ToCatcher = (Catcher->GetActorLocation() - Character->GetActorLocation()).GetSafeNormal2D();
	if (FVector::DotProduct(Character->GetBaseAimRotation().Vector().GetSafeNormal2D(), ToCatcher) < 0.9f)
	{
		return;
	}
	if (Weapon->GetWeaponData()->FireMode == ENoFearFireMode::Auto)
	{
		if (!bRescueTriggerHeld)
		{
			Weapons->StartFire();
			bRescueTriggerHeld = true;
		}
	}
	else if (Now >= NextRescueTrigger)
	{
		Weapons->StartFire();
		Weapons->StopFire();
		NextRescueTrigger = Now + FMath::FRandRange(0.2f, 0.4f);
	}
}

void UNoFearCombatComponent::ClearPinRescue()
{
	if (!RescueCatcher.IsValid() && !bRescueTriggerHeld)
	{
		RescueCatcher.Reset();
		return;
	}
	const bool bWasRescuing = RescueCatcher.IsValid() || bRescueTriggerHeld;
	RescueCatcher.Reset();
	bRescueTriggerHeld = false;
	APawn* Pawn = Cast<APawn>(GetOwner());
	if (const ANoFearCharacter* Character = Cast<ANoFearCharacter>(Pawn))
	{
		if (UNoFearWeaponComponent* Weapons = Character->GetWeaponComponent())
		{
			Weapons->StopFire();
		}
	}
	AAIController* Controller = Pawn ? Cast<AAIController>(Pawn->GetController()) : nullptr;
	if (!Controller || !bWasRescuing)
	{
		return;
	}
	if (bManualTarget)
	{
		SetTarget(nullptr);
	}
	// Паузу перехватили видение, граната или портал — фокус и дерево теперь их.
	const ANoFearAIController* Bot = Cast<ANoFearAIController>(Controller);
	if (IsSeeingPhantom() || (Bot && (Bot->IsEvadingGrenade() || Bot->IsPortalControlled())))
	{
		return;
	}
	Controller->ClearFocus(EAIFocusPriority::Gameplay);
	if (Controller->BrainComponent && Controller->BrainComponent->IsPaused() && !UNoFearHealthComponent::IsActorIncapacitated(Pawn))
	{
		Controller->BrainComponent->ResumeLogic(TEXT("PinRescue over"));
	}
}

AActor* UNoFearCombatComponent::GetThreatActor() const
{
	if (AActor* Target = CurrentTarget.Get())
	{
		return Target;
	}
	return LastShooter.Get();
}

void UNoFearCombatComponent::SetTarget(AActor* NewTarget)
{
	bManualTarget = NewTarget != nullptr;
	if (NewTarget != CurrentTarget.Get())
	{
		ChangeTarget(NewTarget);
	}
}

float UNoFearCombatComponent::GetTimeSinceTargetSeen() const
{
	return static_cast<float>(GetNow() - LastSeenTime);
}

void UNoFearCombatComponent::SetBlindAimPoint(FVector AimPoint)
{
	if (!bHasLineOfSight)
	{
		LastKnownAimPoint = AimPoint;
	}
}

bool UNoFearCombatComponent::IsReadyToFire() const
{
	// Пошатнувшийся не стреляет (UNoFearPoseComponent::Stagger).
	return CurrentTarget.IsValid() && bHasLineOfSight && GetNow() >= ReadyToFireTime && !BlockingFriend.IsValid()
		&& !UNoFearPoseComponent::IsActorStaggered(GetOwner());
}

bool UNoFearCombatComponent::CanBlindFire() const
{
	const AActor* Target = CurrentTarget.Get();
	if (!Target || bHasLineOfSight || UNoFearPoseComponent::IsActorStaggered(GetOwner()))
	{
		return false;
	}
	return !FindFriendInLine(GetOwner(), Target, GetEyesLocation(), LastKnownAimPoint, FMath::Max(FriendlyFireClearance, 40.f));
}

AActor* UNoFearCombatComponent::FindFriendInLine(const AActor* Shooter, const AActor* Target, const FVector& Start, const FVector& End, float Clearance)
{
	UWorld* World = Shooter ? Shooter->GetWorld() : nullptr;
	if (!World || Clearance <= 0.f)
	{
		return nullptr;
	}
	for (TActorIterator<ACharacter> It(World); It; ++It)
	{
		ACharacter* Other = *It;
		if (Other == Shooter || Other == Target || UNoFearHealthComponent::IsActorDead(Other) || UNoFearTeamLibrary::AreHostile(Shooter, Other))
		{
			continue;
		}
		// Лежит под ловчим, в которого и целимся, — стреляем поверх.
		if (const ANoFearJumperAIController* Pinner = ANoFearJumperAIController::FindPinner(Other); Pinner && Target && Pinner->GetPawn() == Target)
		{
			continue;
		}
		const UCapsuleComponent* Capsule = Other->GetCapsuleComponent();
		if (!Capsule)
		{
			continue;
		}
		// Ось капсулы против линии огня.
		const float Radius = Capsule->GetScaledCapsuleRadius();
		const float AxisHalf = FMath::Max(Capsule->GetScaledCapsuleHalfHeight() - Radius, 0.f);
		const FVector Center = Capsule->GetComponentLocation();
		FVector OnLine, OnAxis;
		FMath::SegmentDistToSegmentSafe(Start, End, Center - FVector(0.f, 0.f, AxisHalf), Center + FVector(0.f, 0.f, AxisHalf), OnLine, OnAxis);
		if (FVector::Dist(OnLine, OnAxis) < Radius + Clearance)
		{
			return Other;
		}
	}
	return nullptr;
}

FVector UNoFearCombatComponent::ComputeShotDirection(FVector MuzzleLocation)
{
	AActor* Owner = GetOwner();
	AActor* Target = CurrentTarget.Get();

	FVector AimPoint;
	if (IsSeeingPhantom())
	{
		// Видение: огонь по пустому месту.
		AimPoint = PhantomPoint;
	}
	else if (const APawn* Catcher = RescueCatcher.Get())
	{
		// Спасает прижатого: в тело ловчего, без упреждения — он сидит на жертве.
		AimPoint = GetTargetAimPoint(Catcher, false);
	}
	else if (Target && bHasLineOfSight)
	{
		const bool bHead = FMath::FRand() < HeadshotChance;
		AimPoint = GetTargetAimPoint(Target, bHead) - Target->GetVelocity() * TrackingLag;
	}
	else if (Target)
	{
		// Цель за укрытием: огонь по последней известной позиции.
		AimPoint = LastKnownAimPoint;
	}
	else
	{
		FVector EyesLocation;
		FRotator EyesRotation;
		Owner->GetActorEyesViewPoint(EyesLocation, EyesRotation);
		AimPoint = MuzzleLocation + EyesRotation.Vector() * 1000.f;
	}

	FVector Direction = AimPoint - MuzzleLocation;
	const float Distance = static_cast<float>(Direction.Size());
	Direction = Direction.GetSafeNormal();
	if (Direction.IsNearlyZero())
	{
		Direction = Owner->GetActorForwardVector();
	}

	// Нормальное распределение (Бокс — Мюллер): большинство пуль ложится близко к точке прицеливания.
	const float Sigma = GetCurrentSpreadDegrees(Distance);
	const float U1 = FMath::Max(FMath::FRand(), UE_KINDA_SMALL_NUMBER);
	const float U2 = FMath::FRand();
	const float OffsetDegrees = FMath::Min(Sigma * FMath::Sqrt(-2.f * FMath::Loge(U1)), Sigma * 3.f);
	const float OffsetAngle = 2.f * UE_PI * U2;

	FRotator ShotRotation = Direction.Rotation();
	ShotRotation.Pitch += OffsetDegrees * FMath::Sin(OffsetAngle);
	ShotRotation.Yaw += OffsetDegrees * FMath::Cos(OffsetAngle);
	return ShotRotation.Vector();
}

FVector UNoFearCombatComponent::ComputeShotEnd(FVector MuzzleLocation, float Range)
{
	return MuzzleLocation + ComputeShotDirection(MuzzleLocation) * Range;
}

float UNoFearCombatComponent::GetCurrentSpreadDegrees(float Distance) const
{
	const double Now = GetNow();
	float Multiplier = 1.f;

	const float SettleAlpha = AimSettleTime > 0.f ? FMath::Clamp(static_cast<float>(Now - AimStartTime) / AimSettleTime, 0.f, 1.f) : 1.f;
	Multiplier *= FMath::Lerp(AcquireSpreadMultiplier, 1.f, SettleAlpha);

	if (const AActor* Target = CurrentTarget.Get())
	{
		const float TargetSpeed = static_cast<float>(Target->GetVelocity().Size());
		Multiplier *= 1.f + TargetMovingSpread * FMath::Clamp(TargetSpeed / 600.f, 0.f, 1.5f);
	}

	const float OwnSpeed = static_cast<float>(GetOwner()->GetVelocity().Size());
	Multiplier *= 1.f + SelfMovingSpread * FMath::Clamp(OwnSpeed / 600.f, 0.f, 1.5f);

	if (EffectiveRange > 0.f && Distance > EffectiveRange)
	{
		Multiplier *= 1.f + (Distance - EffectiveRange) / EffectiveRange;
	}

	Multiplier *= FMath::Lerp(1.f, SuppressedSpreadMultiplier, Suppression);

	if (!bHasLineOfSight)
	{
		Multiplier *= BlindFireSpreadMultiplier;
	}

	// Ослеплён плевком: точность ×0.3.
	if (UNoFearOpportunitySubsystem::HasTag(GetOwner(), ENoFearOpportunity::Blinded))
	{
		Multiplier *= BlindedSpreadMultiplier;
	}

	// Паника (этап 11): руки трясутся — от порога паники до страха 100 разброс растёт до PanicSpreadMultiplier.
	if (const UNoFearFearSubsystem* FearSubsystem = UNoFearFearSubsystem::Get(this))
	{
		const float Fear = UNoFearFearComponent::GetActorFear(GetOwner());
		if (Fear >= FearSubsystem->PanicThreshold)
		{
			const float PanicAlpha = FMath::Clamp((Fear - FearSubsystem->PanicThreshold) / FMath::Max(1.f, 100.f - FearSubsystem->PanicThreshold), 0.f, 1.f);
			Multiplier *= FMath::Lerp(1.f, FearSubsystem->PanicSpreadMultiplier, PanicAlpha);
		}
	}

	return FMath::Min(BaseSpreadDegrees * Multiplier, MaxSpreadDegrees);
}

void UNoFearCombatComponent::AddSuppression(float Amount, AActor* Shooter, FVector ShotOrigin)
{
	if (Amount <= 0.f)
	{
		return;
	}

	const float Added = Amount * SuppressionSensitivity;
	Suppression = FMath::Clamp(Suppression + Added, 0.f, 1.f);
	LastSuppressionTime = GetNow();
	LastShotOrigin = ShotOrigin;
	if (Shooter)
	{
		LastShooter = Shooter;
	}

	// Пуля рядом сбивает прицел: часть набранного времени прицеливания теряется.
	const double AimElapsed = GetNow() - AimStartTime;
	AimStartTime = GetNow() - AimElapsed * (1.0 - FMath::Clamp(Added, 0.f, 1.f));

	OnUnderFire.Broadcast(Shooter, ShotOrigin, Added);
	RefreshSuppressedState();
	WriteBlackboard();
}

void UNoFearCombatComponent::ChangeTarget(AActor* NewTarget)
{
	CurrentTarget = NewTarget;
	bHasLineOfSight = false;
	LastSeenTime = -1.0e9;
	AimStartTime = GetNow();

	if (NewTarget)
	{
		// Цель пришла от восприятия, так что примерное положение известно сразу.
		LastKnownTargetLocation = NewTarget->GetActorLocation();
		LastKnownAimPoint = GetTargetAimPoint(NewTarget, false);
	}

	OnTargetChanged.Broadcast(NewTarget);
}

void UNoFearCombatComponent::StartAcquisition()
{
	const double Now = GetNow();
	AimStartTime = Now;
	// Подавленный бот реагирует медленнее.
	const float Reaction = FMath::FRandRange(ReactionTimeMin, FMath::Max(ReactionTimeMin, ReactionTimeMax));
	ReadyToFireTime = Now + Reaction * FMath::Lerp(1.f, 1.5f, Suppression);
}

void UNoFearCombatComponent::UpdateLineOfSight()
{
	AActor* Target = CurrentTarget.Get();
	if (!Target)
	{
		bHasLineOfSight = false;
		BlockingFriend = nullptr;
		return;
	}

	AActor* Owner = GetOwner();
	UWorld* World = GetWorld();

	FCollisionQueryParams Params(SCENE_QUERY_STAT(NoFearLineOfSight), true, Owner);
	Params.AddIgnoredActor(Target);

	TArray<AActor*> AttachedActors;
	Owner->GetAttachedActors(AttachedActors, false, true);
	Target->GetAttachedActors(AttachedActors, false, true);
	Params.AddIgnoredActors(AttachedActors);

	const FVector Eyes = GetEyesLocation();
	const FVector Chest = GetTargetAimPoint(Target, false);
	const FVector Head = GetTargetAimPoint(Target, true);

	const bool bVisible = !World->LineTraceTestByChannel(Eyes, Chest, SightChannel, Params)
		|| !World->LineTraceTestByChannel(Eyes, Head, SightChannel, Params);

	if (bVisible)
	{
		const double Now = GetNow();
		if (!bHasLineOfSight && Now - LastSeenTime > TargetMemoryTime)
		{
			StartAcquisition();
		}
		LastSeenTime = Now;
		LastKnownTargetLocation = Target->GetActorLocation();
		LastKnownAimPoint = Chest;
	}

	bHasLineOfSight = bVisible;

	// Свои на линии огня: не стрелять, пока не отойдут (или пока бот не сместится).
	BlockingFriend = bVisible && FriendlyFireClearance > 0.f ? FindFriendInLine(Owner, Target, Eyes, Chest, FriendlyFireClearance) : nullptr;
}

void UNoFearCombatComponent::UpdateSuppression(float DeltaTime)
{
	if (Suppression > 0.f && GetNow() - LastSuppressionTime > SuppressionDecayDelay)
	{
		Suppression = FMath::Max(0.f, Suppression - SuppressionDecayRate * DeltaTime);
	}
	RefreshSuppressedState();
}

void UNoFearCombatComponent::RefreshSuppressedState()
{
	const bool bNewSuppressed = bIsSuppressed ? Suppression > RecoverThreshold : Suppression >= SuppressedThreshold;
	if (bNewSuppressed != bIsSuppressed)
	{
		bIsSuppressed = bNewSuppressed;
		OnSuppressedChanged.Broadcast(bIsSuppressed);
	}
}

void UNoFearCombatComponent::WriteBlackboard()
{
	UBlackboardComponent* Blackboard = GetBlackboard();
	if (!Blackboard)
	{
		return;
	}

	// Ключей может не быть в блэкборде — тогда запись молча пропускается.
	if (!HasLineOfSightKeyName.IsNone())
	{
		Blackboard->SetValueAsBool(HasLineOfSightKeyName, bHasLineOfSight);
	}
	if (!CanFireKeyName.IsNone())
	{
		Blackboard->SetValueAsBool(CanFireKeyName, IsReadyToFire());
	}
	if (!IsSuppressedKeyName.IsNone())
	{
		Blackboard->SetValueAsBool(IsSuppressedKeyName, bIsSuppressed);
	}
	if (!SuppressionKeyName.IsNone())
	{
		Blackboard->SetValueAsFloat(SuppressionKeyName, Suppression);
	}
	if (!LastKnownLocationKeyName.IsNone() && CurrentTarget.IsValid())
	{
		Blackboard->SetValueAsVector(LastKnownLocationKeyName, LastKnownTargetLocation);
	}
}

UBlackboardComponent* UNoFearCombatComponent::GetBlackboard() const
{
	return UAIBlueprintHelperLibrary::GetBlackboard(GetOwner());
}

FVector UNoFearCombatComponent::GetEyesLocation() const
{
	FVector EyesLocation;
	FRotator EyesRotation;
	GetOwner()->GetActorEyesViewPoint(EyesLocation, EyesRotation);
	return EyesLocation;
}

FVector UNoFearCombatComponent::GetTargetAimPoint(const AActor* Target, bool bHead) const
{
	if (const ACharacter* Character = Cast<ACharacter>(Target))
	{
		const FName BoneName = bHead ? HeadBoneName : ChestBoneName;
		const USkeletalMeshComponent* Mesh = Character->GetMesh();
		if (Mesh && !BoneName.IsNone() && Mesh->DoesSocketExist(BoneName))
		{
			return Mesh->GetSocketLocation(BoneName);
		}

		const float HalfHeight = Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
		return Character->GetActorLocation() + FVector(0.f, 0.f, HalfHeight * (bHead ? 0.8f : 0.35f));
	}

	FVector Origin;
	FVector Extent;
	Target->GetActorBounds(true, Origin, Extent);
	return bHead ? Origin + FVector(0.f, 0.f, Extent.Z * 0.6f) : Origin;
}

double UNoFearCombatComponent::GetNow() const
{
	const UWorld* World = GetWorld();
	return World ? World->GetTimeSeconds() : 0.0;
}
