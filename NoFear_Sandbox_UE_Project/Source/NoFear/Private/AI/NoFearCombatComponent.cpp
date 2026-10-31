#include "AI/NoFearCombatComponent.h"

#include "AI/NoFearCombatSubsystem.h"
#include "Animation/NoFearPoseComponent.h"
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
	WriteBlackboard();
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

bool UNoFearCombatComponent::IsReadyToFire() const
{
	// Пошатнувшийся не стреляет (UNoFearPoseComponent::Stagger).
	return CurrentTarget.IsValid() && bHasLineOfSight && GetNow() >= ReadyToFireTime && !BlockingFriend.IsValid()
		&& !UNoFearPoseComponent::IsActorStaggered(GetOwner());
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
	if (Target && bHasLineOfSight)
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
