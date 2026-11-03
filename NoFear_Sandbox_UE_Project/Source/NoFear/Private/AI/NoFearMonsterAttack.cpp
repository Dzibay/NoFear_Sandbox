#include "AI/NoFearMonsterAttack.h"

#include "AI/NoFearMeleeComponent.h"
#include "Engine/OverlapResult.h"
#include "AI/NoFearOpportunitySubsystem.h"
#include "AI/NoFearSpitProjectile.h"
#include "AIController.h"
#include "Animation/NoFearPoseComponent.h"
#include "Components/CapsuleComponent.h"
#include "Core/NoFearHealthComponent.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "AI/NoFearVulnerabilityComponent.h"
#include "Core/NoFearCharacter.h"
#include "Core/NoFearTeam.h"
#include "EngineUtils.h"
#include "GameFramework/DamageType.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/PlayerController.h"

const FName UNoFearMonsterAttack::BreakableTag = TEXT("NoFearBreakable");

UNoFearMonsterAttack::UNoFearMonsterAttack()
{
	DamageType = UDamageType::StaticClass();
}

bool UNoFearMonsterAttack::CanUse(const UNoFearMeleeComponent* Melee, const AActor* Target) const
{
	const AActor* Owner = Melee ? Melee->GetOwner() : nullptr;
	if (!Owner || !Target)
	{
		return false;
	}
	const FVector Delta = Target->GetActorLocation() - Owner->GetActorLocation();
	if (FMath::Abs(Delta.Z) > MaxHeightDifference)
	{
		return false;
	}
	const float Distance = Delta.Size();
	return Distance >= MinRange && Distance <= MaxRange;
}

void UNoFearMonsterAttack::BeginWindup(UNoFearMeleeComponent* Melee, ACharacter* Owner, AActor* Target)
{
}

void UNoFearMonsterAttack::BeginStrike(UNoFearMeleeComponent* Melee, ACharacter* Owner, AActor* Target)
{
	if (Owner && (LungeSpeed > 0.f || LungeUpSpeed > 0.f))
	{
		const FVector Forward = Owner->GetActorForwardVector().GetSafeNormal2D();
		float Speed = LungeSpeed;
		if (Target && LungeUpSpeed > 0.f)
		{
			// Время в воздухе ≈ 2·Vz/g: столько, чтобы долететь до цели без LungeStopDistance, не больше.
			const float Gravity = FMath::Max(FMath::Abs(Owner->GetWorld()->GetGravityZ()), 1.f);
			const float AirTime = 2.f * LungeUpSpeed / Gravity;
			const float Gap = FVector::Dist2D(Owner->GetActorLocation(), Target->GetActorLocation()) - LungeStopDistance;
			Speed = FMath::Clamp(Gap / AirTime, 0.f, LungeSpeed);
		}
		Owner->LaunchCharacter(Forward * Speed + FVector::UpVector * LungeUpSpeed, true, true);
	}
}

void UNoFearMonsterAttack::ApplyHit(UNoFearMeleeComponent* Melee, AActor* Victim, const FHitResult& Hit)
{
	AActor* Owner = Melee->GetOwner();
	const APawn* OwnerPawn = Cast<APawn>(Owner);
	AController* InstigatorController = OwnerPawn ? OwnerPawn->GetController() : nullptr;
	const FVector Direction = (Victim->GetActorLocation() - Owner->GetActorLocation()).GetSafeNormal();

	UGameplayStatics::ApplyPointDamage(Victim, Damage * Melee->DamageScale, Direction, Hit, InstigatorController, Owner, DamageType);

	if ((KnockbackSpeed > 0.f || KnockbackUpSpeed > 0.f) && !UNoFearHealthComponent::IsActorDead(Victim))
	{
		if (ACharacter* VictimCharacter = Cast<ACharacter>(Victim))
		{
			VictimCharacter->LaunchCharacter(Direction.GetSafeNormal2D() * KnockbackSpeed + FVector::UpVector * KnockbackUpSpeed, true, true);
		}
		// Сильный отброс сбивает с ног: метка для добивания (загонщики, ловчий, плевун).
		if (KnockbackSpeed >= KnockDownMinSpeed)
		{
			UNoFearOpportunitySubsystem::AddTag(Victim, ENoFearOpportunity::KnockedDown, KnockDownTime);
		}
	}
	if (HitSound)
	{
		UGameplayStatics::PlaySoundAtLocation(Melee, HitSound, Hit.ImpactPoint);
	}
	if (HitShake && HitShakeScale > 0.f)
	{
		const APawn* VictimPawn = Cast<APawn>(Victim);
		if (APlayerController* PC = VictimPawn ? Cast<APlayerController>(VictimPawn->GetController()) : nullptr)
		{
			PC->ClientStartCameraShake(HitShake, HitShakeScale);
		}
	}
}

void UNoFearMonsterAttack::BreakCover(UNoFearMeleeComponent* Melee, AActor* Prop, UPrimitiveComponent* Component)
{
	AActor* Owner = Melee ? Melee->GetOwner() : nullptr;
	if (!Owner || !Prop)
	{
		return;
	}
	const FVector Direction = (Prop->GetActorLocation() - Owner->GetActorLocation()).GetSafeNormal();
	if (Prop->ActorHasTag(BreakableTag))
	{
		// Временное укрытие: ломается и исчезает.
		++BrokenCoverCount;
		Prop->Destroy();
	}
	else if (Component && Component->IsSimulatingPhysics())
	{
		++BrokenCoverCount;
		Component->AddImpulse(Direction * CoverImpulse * Component->GetMass(), NAME_None, false);
	}
}

UNoFearMonsterAttack_Swing::UNoFearMonsterAttack_Swing()
{
	AttackName = TEXT("Swing");
	MinRange = 0.f;
	MaxRange = 260.f;
	Cooldown = 1.2f;
	Weight = 1.f;
	Damage = 20.f;
	HitReach = 100.f;
	HitRadius = 90.f;
	WindupTime = 0.35f;
	HitWindow = 0.25f;
	RecoveryTime = 0.35f;
	// Шаг вперёд вместе с ударом, чтобы не махать по воздуху, если цель чуть отступила.
	LungeSpeed = 250.f;
	Pose = ENoFearPose::Swing;
}

UNoFearMonsterAttack_Combo::UNoFearMonsterAttack_Combo()
{
	AttackName = TEXT("Combo");
	MinRange = 0.f;
	MaxRange = 240.f;
	Cooldown = 4.f;
	Weight = 1.f;
	Damage = 14.f;
	HitReach = 100.f;
	HitRadius = 90.f;
	WindupTime = 0.4f;
	HitWindow = 0.52f;
	RecoveryTime = 0.9f;
	LungeSpeed = 250.f;
	Pose = ENoFearPose::Swing;
}

bool UNoFearMonsterAttack_Combo::CanHitNow(float Elapsed) const
{
	return Elapsed < HitActiveTime || (Elapsed >= SecondHitDelay && Elapsed < SecondHitDelay + HitActiveTime);
}

void UNoFearMonsterAttack_Combo::BeginStrike(UNoFearMeleeComponent* Melee, ACharacter* Owner, AActor* Target)
{
	bSecondSwing = false;
	StrikeElapsed = 0.f;
	Super::BeginStrike(Melee, Owner, Target);
}

bool UNoFearMonsterAttack_Combo::TickStrike(UNoFearMeleeComponent* Melee, ACharacter* Owner, AActor* Target, float DeltaTime)
{
	StrikeElapsed += DeltaTime;
	if (!bSecondSwing && StrikeElapsed >= SecondHitDelay)
	{
		// Второй взмах: та же цель может получить ещё раз, поза и звук повторяются.
		bSecondSwing = true;
		++SecondSwingCount;
		Melee->ResetHitActors();
		if (UNoFearPoseComponent* PoseComp = Owner->FindComponentByClass<UNoFearPoseComponent>())
		{
			PoseComp->PlayPose(Pose, HitActiveTime + 0.15f, PoseIntensity * 1.15f);
		}
		if (SwingSound)
		{
			UGameplayStatics::PlaySoundAtLocation(Melee, SwingSound, Owner->GetActorLocation());
		}
		if (SecondLungeSpeed > 0.f)
		{
			FVector Dir = Owner->GetActorForwardVector();
			if (Target)
			{
				Dir = (Target->GetActorLocation() - Owner->GetActorLocation()).GetSafeNormal2D();
			}
			Owner->LaunchCharacter(Dir * SecondLungeSpeed, true, false);
		}
	}
	return true;
}

UNoFearMonsterAttack_Lunge::UNoFearMonsterAttack_Lunge()
{
	AttackName = TEXT("Lunge");
	MinRange = 200.f;
	MaxRange = 480.f;
	Cooldown = 3.5f;
	Weight = 1.5f;
	Damage = 30.f;
	HitReach = 90.f;
	HitRadius = 80.f;
	WindupTime = 0.45f;
	HitWindow = 0.45f;
	RecoveryTime = 0.5f;
	LungeSpeed = 950.f;
	LungeUpSpeed = 250.f;
	Pose = ENoFearPose::Lunge;
}

// --- Прыжок ---

UNoFearMonsterAttack_Leap::UNoFearMonsterAttack_Leap()
{
	AttackName = TEXT("Leap");
	MinRange = 550.f;
	MaxRange = 950.f;
	Cooldown = 3.f;
	Weight = 2.f;
	MaxHeightDifference = 250.f;
	Damage = 22.f;
	HitReach = 80.f;
	HitRadius = 85.f;
	WindupTime = 0.4f;
	// Окно удара — до приземления, это только предел.
	HitWindow = 1.8f;
	RecoveryTime = 0.6f;
	LungeSpeed = 1300.f;
	LungeUpSpeed = 650.f;
	Pose = ENoFearPose::Lunge;
}

bool UNoFearMonsterAttack_Leap::FindClearArc(const ACharacter* Owner, const FVector& Land, TConstArrayView<float> ArcParams, float MaxSpeed,
	const AActor* IgnoreActor, FVector& OutVelocity)
{
	UWorld* World = Owner ? Owner->GetWorld() : nullptr;
	const UCharacterMovementComponent* Movement = Owner ? Owner->GetCharacterMovement() : nullptr;
	if (!World || !Movement)
	{
		return false;
	}
	const UCapsuleComponent* Capsule = Owner->GetCapsuleComponent();
	// Капсула чуть меньше своей: касание стены краем и пол под ногами — не помеха.
	const FCollisionShape Shape = FCollisionShape::MakeCapsule(FMath::Max(Capsule->GetScaledCapsuleRadius() - 6.f, 10.f),
		FMath::Max(Capsule->GetScaledCapsuleHalfHeight() - 12.f, 20.f));
	FCollisionQueryParams Params(SCENE_QUERY_STAT(NoFearLeapArc), false, Owner);
	if (IgnoreActor)
	{
		// И то, что у цели в руках (оружие — WorldDynamic): в конце дуги ловчий на ней.
		Params.AddIgnoredActor(IgnoreActor);
		TArray<AActor*> Attached;
		IgnoreActor->GetAttachedActors(Attached, true, true);
		Params.AddIgnoredActors(Attached);
	}
	FCollisionObjectQueryParams Objects(ECC_WorldStatic);
	Objects.AddObjectTypesToQuery(ECC_WorldDynamic);
	const float GravityZ = Movement->GetGravityZ();
	const FVector Start = Owner->GetActorLocation() + FVector(0.f, 0.f, 8.f);
	const FVector End = Land + FVector(0.f, 0.f, 14.f);

	for (const float Arc : ArcParams)
	{
		FVector Velocity;
		if (!UGameplayStatics::SuggestProjectileVelocity_CustomArc(Owner, Velocity, Start, End, GravityZ, Arc) || Velocity.Size() > MaxSpeed)
		{
			continue;
		}
		const float Speed2D = Velocity.Size2D();
		if (Speed2D < 1.f)
		{
			continue;
		}
		const float FlightTime = FVector::Dist2D(Start, End) / Speed2D;
		constexpr int32 Steps = 12;
		bool bClear = true;
		FVector Previous = Start;
		for (int32 Step = 1; Step <= Steps && bClear; ++Step)
		{
			const float Time = FlightTime * Step / Steps;
			const FVector Point = Step == Steps ? End : Start + Velocity * Time + FVector(0.f, 0.f, 0.5f * GravityZ * Time * Time);
			bClear = !World->SweepTestByObjectType(Previous, Point, FQuat::Identity, Objects, Shape, Params);
			Previous = Point;
		}
		if (bClear)
		{
			OutVelocity = Velocity;
			return true;
		}
	}
	return false;
}

bool UNoFearMonsterAttack_Leap::FindLandingNear(const ACharacter* Owner, const AActor* Target, float StopDistance, FVector& OutLand)
{
	UWorld* World = Owner ? Owner->GetWorld() : nullptr;
	if (!World || !Target)
	{
		return false;
	}
	const float MyHalf = Owner->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	const float MyRadius = Owner->GetCapsuleComponent()->GetScaledCapsuleRadius();
	const ACharacter* TargetCharacter = Cast<ACharacter>(Target);
	const float TargetHalf = TargetCharacter ? TargetCharacter->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() : 90.f;
	const FVector TargetFeet = Target->GetActorLocation() - FVector(0.f, 0.f, TargetHalf);
	FVector ToMe = (Owner->GetActorLocation() - TargetFeet).GetSafeNormal2D();
	if (ToMe.IsNearlyZero())
	{
		ToMe = -Owner->GetActorForwardVector().GetSafeNormal2D();
	}
	FCollisionQueryParams Params(SCENE_QUERY_STAT(NoFearLeapLand), false, Owner);
	Params.AddIgnoredActor(Target);
	FCollisionObjectQueryParams Objects(ECC_WorldStatic);
	Objects.AddObjectTypesToQuery(ECC_WorldDynamic);
	// Перед целью: на полу или на низком блоке (цель за ним присела) — сверху и бьёт.
	const float Stops[] = { StopDistance, StopDistance + 60.f, FMath::Max(StopDistance - 50.f, 40.f) };
	for (const float Stop : Stops)
	{
		const FVector Column = TargetFeet + ToMe * Stop;
		FHitResult Ground;
		if (!World->LineTraceSingleByObjectType(Ground, Column + FVector(0.f, 0.f, 260.f), Column - FVector(0.f, 0.f, 200.f), Objects, Params)
			|| Ground.bStartPenetrating || Ground.ImpactNormal.Z < 0.7f)
		{
			continue;
		}
		const FVector Land = Ground.ImpactPoint + FVector(0.f, 0.f, MyHalf + 4.f);
		if (World->OverlapAnyTestByObjectType(Land, FQuat::Identity, Objects, FCollisionShape::MakeCapsule(MyRadius - 4.f, MyHalf - 4.f), Params))
		{
			continue;
		}
		OutLand = Land;
		return true;
	}
	return false;
}

bool UNoFearMonsterAttack_Leap::ComputeArc(const ACharacter* Owner, const AActor* Target, FVector& OutVelocity) const
{
	FVector Land;
	if (!FindLandingNear(Owner, Target, LungeStopDistance, Land))
	{
		return false;
	}
	// Сперва привычная дуга, помеха на пути — выше.
	const float Arcs[] = { 0.45f, 0.6f, 0.72f, 0.82f };
	return FindClearArc(Owner, Land, Arcs, MaxLaunchSpeed, Target, OutVelocity);
}

bool UNoFearMonsterAttack_Leap::HasClearArc(const ACharacter* Owner, const AActor* Target) const
{
	const UWorld* World = Owner ? Owner->GetWorld() : nullptr;
	if (!World || !Target)
	{
		return false;
	}
	const double Now = World->GetTimeSeconds();
	if (CachedArcTarget.Get() != Target || Now - CachedArcTime > 0.3)
	{
		CachedArcTarget = Target;
		CachedArcTime = Now;
		bCachedArcClear = ComputeArc(Owner, Target, CachedArcVelocity);
	}
	return bCachedArcClear;
}

bool UNoFearMonsterAttack_Leap::CanUse(const UNoFearMeleeComponent* Melee, const AActor* Target) const
{
	return Super::CanUse(Melee, Target) && HasClearArc(Cast<ACharacter>(Melee->GetOwner()), Target);
}

void UNoFearMonsterAttack_Leap::BeginStrike(UNoFearMeleeComponent* Melee, ACharacter* Owner, AActor* Target)
{
	bLanded = false;
	if (!Owner || !Target)
	{
		return;
	}
	// За замах цель сдвинулась: дуга заново; закрылась — прыгает по той, что была свободна при выборе атаки.
	FVector Velocity;
	if (ComputeArc(Owner, Target, Velocity))
	{
		++ArcLaunchCount;
		Owner->LaunchCharacter(Velocity, true, true);
	}
	else if (bCachedArcClear && CachedArcTarget.Get() == Target)
	{
		++ArcLaunchCount;
		Owner->LaunchCharacter(CachedArcVelocity, true, true);
	}
	else
	{
		// Дуги нет — бьёт с места, не врезаясь в стену.
		++BlockedArcCount;
	}
	CachedArcTime = -1.0;
}

bool UNoFearMonsterAttack_Leap::TickStrike(UNoFearMeleeComponent* Melee, ACharacter* Owner, AActor* Target, float DeltaTime)
{
	// Прыжок начинается со следующего шага движения; приземлился — удар закончен.
	const UCharacterMovementComponent* Movement = Owner ? Owner->GetCharacterMovement() : nullptr;
	const double Elapsed = Melee->GetWorld()->GetTimeSeconds() - StrikeStartTime;
	const bool bFlying = !Movement || Elapsed < 0.15 || Movement->IsFalling();
	if (!bFlying && !bLanded && Owner)
	{
		bLanded = true;
		if (LandSound)
		{
			UGameplayStatics::PlaySoundAtLocation(Melee, LandSound, Owner->GetActorLocation());
		}
	}
	return bFlying;
}

void UNoFearMonsterAttack_Leap::BeginRecovery(UNoFearMeleeComponent* Melee, ACharacter* Owner, AActor* Target)
{
	if (!Owner || (BounceSpeed <= 0.f && BounceUpSpeed <= 0.f))
	{
		return;
	}
	FVector Away = Target ? (Owner->GetActorLocation() - Target->GetActorLocation()).GetSafeNormal2D() : -Owner->GetActorForwardVector().GetSafeNormal2D();
	if (Away.IsNearlyZero())
	{
		Away = -Owner->GetActorForwardVector().GetSafeNormal2D();
	}
	Owner->LaunchCharacter(Away * BounceSpeed + FVector::UpVector * BounceUpSpeed, true, true);
}

// --- Прыжок сверху ---

UNoFearMonsterAttack_Pounce::UNoFearMonsterAttack_Pounce()
{
	AttackName = TEXT("Pounce");
	MinRange = 0.f;
	MaxRange = 1500.f;
	Cooldown = 8.f;
	Weight = 0.f;
	MaxHeightDifference = 700.f;
	Damage = 15.f;
	HitReach = 40.f;
	HitRadius = 90.f;
	WindupTime = 0.35f;
	HitWindow = 2.5f;
	RecoveryTime = 0.4f;
	LungeSpeed = 1500.f;
	LungeUpSpeed = 500.f;
	// Летит на саму цель, а не останавливается перед ней.
	LungeStopDistance = 0.f;
	BounceSpeed = 0.f;
	BounceUpSpeed = 0.f;
	Pose = ENoFearPose::Lunge;
	PoseIntensity = 1.2f;
}

void UNoFearMonsterAttack_Pounce::BeginStrike(UNoFearMeleeComponent* Melee, ACharacter* Owner, AActor* Target)
{
	if (!Owner || !Target)
	{
		return;
	}
	bLanded = false;
	// Дуга в точку чуть ниже центра цели: ловчий падает ей на плечи. Край уступа мешает — дуга выше.
	const FVector Start = Owner->GetActorLocation();
	const FVector Aim = Target->GetActorLocation() - FVector(0.f, 0.f, 20.f);
	FVector Velocity;
	const float Arcs[] = { ArcParam, 0.45f, 0.6f };
	const float GravityZ = Owner->GetCharacterMovement() ? Owner->GetCharacterMovement()->GetGravityZ() : 0.f;
	if (FindClearArc(Owner, Aim, Arcs, 2600.f, Target, Velocity)
		|| UGameplayStatics::SuggestProjectileVelocity_CustomArc(Melee, Velocity, Start, Aim, GravityZ, ArcParam))
	{
		++ArcLaunchCount;
		Owner->LaunchCharacter(Velocity, true, true);
	}
	else
	{
		UNoFearMonsterAttack::BeginStrike(Melee, Owner, Target);
	}
}

bool UNoFearMonsterAttack_Pounce::CanPounceFrom(const ACharacter* Owner, const AActor* Target) const
{
	if (!Owner || !Target)
	{
		return false;
	}
	FVector Velocity;
	const float Arcs[] = { ArcParam, 0.45f, 0.6f };
	return FindClearArc(Owner, Target->GetActorLocation() - FVector(0.f, 0.f, 20.f), Arcs, 2600.f, Target, Velocity);
}

bool UNoFearMonsterAttack_Pounce::TickStrike(UNoFearMeleeComponent* Melee, ACharacter* Owner, AActor* Target, float DeltaTime)
{
	return !bHitThisAttack && Super::TickStrike(Melee, Owner, Target, DeltaTime);
}

// --- Плевок ---

UNoFearMonsterAttack_Spit::UNoFearMonsterAttack_Spit()
{
	AttackName = TEXT("Spit");
	MinRange = 500.f;
	MaxRange = 1600.f;
	Cooldown = 1.8f;
	Weight = 1.f;
	MaxHeightDifference = 400.f;
	Damage = 10.f;
	bSweepHits = false;
	WindupTime = 0.6f;
	HitWindow = 0.1f;
	RecoveryTime = 0.45f;
	Pose = ENoFearPose::Spit;
	ProjectileClass = ANoFearSpitProjectile::StaticClass();
}

FVector UNoFearMonsterAttack_Spit::GetMuzzle(const AActor* Owner) const
{
	return Owner->GetActorLocation() + Owner->GetActorForwardVector().GetSafeNormal2D() * MuzzleForward + FVector(0.f, 0.f, MuzzleUp);
}

bool UNoFearMonsterAttack_Spit::HasLineOfSight(const UNoFearMeleeComponent* Melee, const AActor* Owner, const AActor* Target) const
{
	FCollisionQueryParams Params(SCENE_QUERY_STAT(NoFearSpitSight), false, Owner);
	Params.AddIgnoredActor(Target);
	const FVector Start = Owner->GetActorLocation() + FVector(0.f, 0.f, MuzzleUp);
	return !Melee->GetWorld()->LineTraceTestByChannel(Start, Target->GetActorLocation(), ECC_Visibility, Params);
}

bool UNoFearMonsterAttack_Spit::CanLob(const UNoFearMeleeComponent* Melee, const AActor* Owner, const AActor* Target, FVector& OutVelocity) const
{
	const FVector Start = GetMuzzle(Owner);
	const FVector Aim = Target->GetActorLocation();
	if (!UGameplayStatics::SuggestProjectileVelocity_CustomArc(Melee, OutVelocity, Start, Aim, 0.f, LobArcParam))
	{
		return false;
	}
	FPredictProjectilePathParams PathParams(12.f, Start, OutVelocity, 4.f, ECC_Visibility, const_cast<AActor*>(Owner));
	PathParams.ActorsToIgnore.Add(const_cast<AActor*>(Target));
	PathParams.SimFrequency = 10.f;
	FPredictProjectilePathResult PathResult;
	if (!UGameplayStatics::PredictProjectilePath(Melee, PathParams, PathResult))
	{
		return false;
	}
	return FVector::Dist2D(PathResult.HitResult.ImpactPoint, Aim) <= LobLandTolerance;
}

bool UNoFearMonsterAttack_Spit::CanUse(const UNoFearMeleeComponent* Melee, const AActor* Target) const
{
	if (!Super::CanUse(Melee, Target))
	{
		return false;
	}
	// Видит цель — плюёт прямо; не видит — только навесом, если дуга ложится рядом (иначе снаряд уйдёт в стену).
	const AActor* Owner = Melee->GetOwner();
	if (HasLineOfSight(Melee, Owner, Target))
	{
		return true;
	}
	FVector Velocity;
	return bLobOverCover && CanLob(Melee, Owner, Target, Velocity);
}

void UNoFearMonsterAttack_Spit::BeginStrike(UNoFearMeleeComponent* Melee, ACharacter* Owner, AActor* Target)
{
	UWorld* World = Melee->GetWorld();
	if (!Owner || !Target || !ProjectileClass || !World)
	{
		return;
	}

	const FVector Start = GetMuzzle(Owner);
	FVector Aim = Target->GetActorLocation();
	FVector Velocity;
	const bool bLob = bLobOverCover && !HasLineOfSight(Melee, Owner, Target) && CanLob(Melee, Owner, Target, Velocity);
	if (!bLob)
	{
		if (!UGameplayStatics::SuggestProjectileVelocity_CustomArc(Melee, Velocity, Start, Aim, 0.f, ArcParam))
		{
			return;
		}
		// Упреждение: куда цель успеет дойти за время полёта.
		const float HorizontalSpeed = FMath::Max(Velocity.Size2D(), 1.f);
		const float FlightTime = FVector::Dist2D(Start, Aim) / HorizontalSpeed;
		const FVector TargetVelocity = Target->GetVelocity() * FVector(1.f, 1.f, 0.f);
		if (LeadFactor > 0.f && !TargetVelocity.IsNearlyZero())
		{
			Aim += TargetVelocity * FlightTime * LeadFactor;
			UGameplayStatics::SuggestProjectileVelocity_CustomArc(Melee, Velocity, Start, Aim, 0.f, ArcParam);
		}
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.Owner = Owner;
	SpawnParams.Instigator = Owner;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AActor* Projectile = World->SpawnActor<AActor>(ProjectileClass, Start, Velocity.Rotation(), SpawnParams);
	if (ANoFearSpitProjectile* Spit = Cast<ANoFearSpitProjectile>(Projectile))
	{
		Spit->DirectDamage = Damage * Melee->DamageScale;
		Spit->bLobbed = bLob;
		Spit->Launch(Velocity);
	}
	if (Projectile)
	{
		++ShotCount;
		LobCount += bLob ? 1 : 0;
	}
}

// --- Таран ---

UNoFearMonsterAttack_Charge::UNoFearMonsterAttack_Charge()
{
	AttackName = TEXT("Charge");
	MinRange = 450.f;
	MaxRange = 1400.f;
	Cooldown = 7.f;
	Weight = 3.f;
	MaxHeightDifference = 120.f;
	Damage = 30.f;
	HitReach = 110.f;
	HitRadius = 110.f;
	KnockbackSpeed = 1000.f;
	KnockbackUpSpeed = 400.f;
	WindupTime = 0.7f;
	HitWindow = 1.5f;
	RecoveryTime = 0.6f;
	LungeStopDistance = 140.f;
	Pose = ENoFearPose::Lunge;
	PoseIntensity = 0.7f;
	bBreaksCover = true;
	CoverImpulse = 1200.f;
}

void UNoFearMonsterAttack_Charge::BeginStrike(UNoFearMeleeComponent* Melee, ACharacter* Owner, AActor* Target)
{
	if (!Owner)
	{
		return;
	}
	// Направление выбирается один раз в конце замаха: от тарана можно уйти в сторону. На цель, а не по лицу:
	// тело GASP доворачивается с запаздыванием.
	ChargeDirection = Target ? (Target->GetActorLocation() - Owner->GetActorLocation()).GetSafeNormal2D() : FVector::ZeroVector;
	if (ChargeDirection.IsNearlyZero())
	{
		ChargeDirection = Owner->GetActorForwardVector().GetSafeNormal2D();
	}
	Owner->SetActorRotation(ChargeDirection.Rotation());
	bWallHitThisCharge = false;
	if (AController* Controller = Owner->GetController())
	{
		Controller->StopMovement();
	}
}

bool UNoFearMonsterAttack_Charge::TickStrike(UNoFearMeleeComponent* Melee, ACharacter* Owner, AActor* Target, float DeltaTime)
{
	UCharacterMovementComponent* Movement = Owner ? Owner->GetCharacterMovement() : nullptr;
	if (!Movement || bHitThisAttack)
	{
		return false;
	}

	// Стена впереди — удар, оглушение.
	const UCapsuleComponent* Capsule = Owner->GetCapsuleComponent();
	const float CapsuleRadius = Capsule ? Capsule->GetScaledCapsuleRadius() : 40.f;
	const FVector Start = Owner->GetActorLocation();
	const FVector End = Start + ChargeDirection * (CapsuleRadius + WallCheckDistance);
	FCollisionQueryParams Params(SCENE_QUERY_STAT(NoFearCharge), false, Owner);
	FCollisionObjectQueryParams Objects;
	Objects.AddObjectTypesToQuery(ECC_WorldStatic);
	Objects.AddObjectTypesToQuery(ECC_WorldDynamic);
	// Ломаемые укрытия и физические предметы не останавливают: их сносит сам таран.
	TArray<FHitResult> Sweep;
	Melee->GetWorld()->SweepMultiByObjectType(Sweep, Start, End, FQuat::Identity, Objects, FCollisionShape::MakeSphere(CapsuleRadius * 0.5f), Params);
	const FHitResult* Wall = Sweep.FindByPredicate([](const FHitResult& Hit)
	{
		const AActor* HitActor = Hit.GetActor();
		// Многоцелевой запрос по типам объектов отдаёт касания (bBlockingHit ложно), поэтому смотрим на компонент и нормаль.
		return Hit.GetComponent() && Hit.ImpactNormal.Z < 0.6f && Hit.ImpactNormal.Z > -0.6f
			&& !(HitActor && HitActor->ActorHasTag(UNoFearMonsterAttack::BreakableTag))
			&& !(Hit.GetComponent() && Hit.GetComponent()->IsSimulatingPhysics());
	});
	if (Wall && FMath::Abs(Wall->ImpactNormal.Z) < 0.6f)
	{
		++WallHitCount;
		bWallHitThisCharge = true;
		Movement->Velocity = FVector::ZeroVector;
		if (UNoFearPoseComponent* OwnerPose = Owner->FindComponentByClass<UNoFearPoseComponent>(); OwnerPose && StunTime > 0.f)
		{
			OwnerPose->Stagger(-ChargeDirection, StunTime);
			if (UNoFearVulnerabilityComponent* Vulnerability = Owner->FindComponentByClass<UNoFearVulnerabilityComponent>())
			{
				Vulnerability->OpenWindow(ENoFearWeakCondition::ArmorBroken, StunTime);
			}
			// Контроллер ставит дерево на паузу по реакции на удар.
			OwnerPose->OnHitReaction.Broadcast(OwnerPose, true);
		}
		return false;
	}

	Movement->Velocity = FVector(ChargeDirection.X * ChargeSpeed, ChargeDirection.Y * ChargeSpeed, Movement->Velocity.Z);
	// Скорость задана напрямую, без ускорения: тело само не повернётся по ходу.
	Owner->SetActorRotation(ChargeDirection.Rotation());
	return true;
}

void UNoFearMonsterAttack_Charge::BeginRecovery(UNoFearMeleeComponent* Melee, ACharacter* Owner, AActor* Target)
{
	// Промах без стены: разворот чуть дольше, спина открыта.
	const bool bMissed = !bWallHitThisCharge && !bHitThisAttack;
	RecoveryTime = bMissed ? MissRecoveryTime : HitRecoveryTime;
	MissCount += bMissed ? 1 : 0;
}

// --- Хват и бросок ---

UNoFearMonsterAttack_Grab::UNoFearMonsterAttack_Grab()
{
	AttackName = TEXT("Grab");
	MinRange = 0.f;
	MaxRange = 220.f;
	Cooldown = 10.f;
	Weight = 1.5f;
	Damage = 25.f;
	WindupTime = 0.5f;
	// Держит HoldTime, потом летит до приземления: окно закрывается раньше, когда бросок закончился.
	HitWindow = 3.f;
	RecoveryTime = 0.5f;
	WindupTurnRate = 540.f;
	KnockDownTime = 1.5f;
	bSweepHits = false;
	Pose = ENoFearPose::Swing;
}

bool UNoFearMonsterAttack_Grab::CanUse(const UNoFearMeleeComponent* Melee, const AActor* Target) const
{
	const ACharacter* Character = Cast<ACharacter>(Target);
	return Character && !UNoFearHealthComponent::IsActorDead(Character) && UNoFearHealthComponent::FindHealth(Character)
		&& Super::CanUse(Melee, Target);
}

void UNoFearMonsterAttack_Grab::BeginStrike(UNoFearMeleeComponent* Melee, ACharacter* Owner, AActor* Target)
{
	bThrown = false;
	ACharacter* Victim = Cast<ACharacter>(Target);
	if (!Owner || !Victim || UNoFearHealthComponent::IsActorDead(Victim)
		|| FVector::Dist2D(Owner->GetActorLocation(), Victim->GetActorLocation()) > MaxRange + 80.f)
	{
		Held.Reset();
		return;
	}
	Held = Victim;
	bHitThisAttack = true;
	if (UCharacterMovementComponent* VictimMovement = Victim->GetCharacterMovement())
	{
		VictimMovement->StopMovementImmediately();
		VictimMovement->DisableMovement();
	}
}

void UNoFearMonsterAttack_Grab::Release(ACharacter* Victim)
{
	if (Victim)
	{
		if (UCharacterMovementComponent* VictimMovement = Victim->GetCharacterMovement())
		{
			VictimMovement->SetMovementMode(MOVE_Falling);
		}
	}
	Held.Reset();
}

bool UNoFearMonsterAttack_Grab::TickStrike(UNoFearMeleeComponent* Melee, ACharacter* Owner, AActor* Target, float DeltaTime)
{
	ACharacter* Victim = Held.Get();
	UWorld* World = Melee ? Melee->GetWorld() : nullptr;
	if (!Owner || !Victim || !World || UNoFearHealthComponent::IsActorDead(Victim))
	{
		Release(Victim);
		return false;
	}
	const double Now = World->GetTimeSeconds();

	if (!bThrown)
	{
		// Держит перед собой.
		FVector HoldPoint = Owner->GetActorLocation() + Owner->GetActorForwardVector().GetSafeNormal2D() * HoldDistance;
		HoldPoint.Z = Owner->GetActorLocation().Z;
		Victim->SetActorLocation(HoldPoint, false, nullptr, ETeleportType::TeleportPhysics);
		if (UCharacterMovementComponent* VictimMovement = Victim->GetCharacterMovement())
		{
			VictimMovement->Velocity = FVector::ZeroVector;
		}
		if (Now - StrikeStartTime >= HoldTime)
		{
			Throw(Melee, Owner);
		}
		return true;
	}

	// Полёт: удар о человека или стену, иначе до приземления.
	const UCharacterMovementComponent* VictimMovement = Victim->GetCharacterMovement();
	if (Now - ThrowTime < 0.12)
	{
		return true;
	}
	FCollisionQueryParams Params(SCENE_QUERY_STAT(NoFearGrab), false, Owner);
	Params.AddIgnoredActor(Victim);
	TArray<FOverlapResult> Overlaps;
	World->OverlapMultiByObjectType(Overlaps, Victim->GetActorLocation(), FQuat::Identity, FCollisionObjectQueryParams(ECC_Pawn),
		FCollisionShape::MakeSphere(95.f), Params);
	for (const FOverlapResult& Overlap : Overlaps)
	{
		AActor* Other = Overlap.GetActor();
		if (Other && Other != Owner && !UNoFearHealthComponent::IsActorDead(Other) && UNoFearHealthComponent::FindHealth(Other)
			&& !UNoFearTeamLibrary::AreFriendly(Owner, Other))
		{
			ImpactVictim(Melee, Owner, Other);
			return false;
		}
	}
	const float Speed2D = VictimMovement ? VictimMovement->Velocity.Size2D() : 0.f;
	if (VictimMovement && VictimMovement->IsFalling() && Speed2D < ThrownSpeed * 0.3f)
	{
		// Скорость пропала в воздухе — врезался в стену.
		ImpactVictim(Melee, Owner, nullptr);
		return false;
	}
	return VictimMovement && VictimMovement->IsFalling();
}

void UNoFearMonsterAttack_Grab::Throw(UNoFearMeleeComponent* Melee, ACharacter* Owner)
{
	ACharacter* Victim = Held.Get();
	UWorld* World = Melee->GetWorld();
	if (!Victim || !World)
	{
		return;
	}
	const FVector OwnerLocation = Owner->GetActorLocation();
	const FVector Forward = Owner->GetActorForwardVector().GetSafeNormal2D();

	// В другого человека впереди, иначе в ближайшую стену.
	FVector Direction = Forward;
	float Distance = ThrowRange;
	bLastThrowAtPerson = false;
	float BestDistance = TNumericLimits<float>::Max();
	for (ANoFearCharacter* Candidate : TActorRange<ANoFearCharacter>(World))
	{
		if (!Candidate || Candidate == Owner || Candidate == Victim || UNoFearHealthComponent::IsActorDead(Candidate)
			|| UNoFearTeamLibrary::AreFriendly(Owner, Candidate))
		{
			continue;
		}
		const FVector Delta = Candidate->GetActorLocation() - OwnerLocation;
		const float Dist = Delta.Size2D();
		const float Angle = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(FVector::DotProduct(Forward, Delta.GetSafeNormal2D()), -1.f, 1.f)));
		if (Dist <= ThrowPersonRange && Angle <= ThrowPersonAngle && Dist < BestDistance)
		{
			BestDistance = Dist;
			Direction = Delta.GetSafeNormal2D();
			Distance = Dist;
			bLastThrowAtPerson = true;
		}
	}
	if (!bLastThrowAtPerson)
	{
		float NearestWall = TNumericLimits<float>::Max();
		FCollisionQueryParams Params(SCENE_QUERY_STAT(NoFearGrabWall), false, Owner);
		Params.AddIgnoredActor(Victim);
		for (int32 Index = 0; Index < 8; ++Index)
		{
			const FVector Ray = Forward.RotateAngleAxis(45.f * Index, FVector::UpVector);
			FHitResult Hit;
			if (World->LineTraceSingleByObjectType(Hit, OwnerLocation, OwnerLocation + Ray * ThrowRange,
					FCollisionObjectQueryParams(ECC_WorldStatic), Params) && Hit.Distance < NearestWall)
			{
				NearestWall = Hit.Distance;
				Direction = Ray;
				Distance = Hit.Distance;
			}
		}
	}

	// Время в воздухе ≈ 2·Vz/g; скорость такая, чтобы долететь до цели, не больше ThrowMaxSpeed.
	const float Gravity = FMath::Max(FMath::Abs(World->GetGravityZ()), 1.f);
	const float AirTime = 2.f * ThrowUpSpeed / Gravity;
	ThrownSpeed = FMath::Clamp((Distance - HoldDistance) / FMath::Max(AirTime, 0.1f), 600.f, ThrowMaxSpeed);

	Release(Victim);
	bThrown = true;
	ThrowTime = World->GetTimeSeconds();
	++ThrowCount;
	bLastThrowImpacted = false;
	LastImpactVictim.Reset();

	Victim->LaunchCharacter(Direction * ThrownSpeed + FVector::UpVector * ThrowUpSpeed, true, true);
	Held = Victim;

	const FHitResult Hit(Victim, nullptr, Victim->GetActorLocation(), -Direction);
	ApplyHit(Melee, Victim, Hit);
	Melee->OnAttackHit.Broadcast(Victim, Damage * Melee->DamageScale);
	UNoFearOpportunitySubsystem::AddTag(Victim, ENoFearOpportunity::KnockedDown, KnockDownTime);
}

void UNoFearMonsterAttack_Grab::ImpactVictim(UNoFearMeleeComponent* Melee, ACharacter* Owner, AActor* Other)
{
	ACharacter* Victim = Held.Get();
	bLastThrowImpacted = true;
	const APawn* OwnerPawn = Cast<APawn>(Owner);
	AController* InstigatorController = OwnerPawn ? OwnerPawn->GetController() : nullptr;
	if (Victim && !UNoFearHealthComponent::IsActorDead(Victim))
	{
		const FHitResult Hit(Victim, nullptr, Victim->GetActorLocation(), FVector::UpVector);
		UGameplayStatics::ApplyPointDamage(Victim, ImpactDamage * Melee->DamageScale, FVector::ForwardVector, Hit, InstigatorController, Owner, DamageType);
	}
	if (Other && !UNoFearHealthComponent::IsActorDead(Other))
	{
		LastImpactVictim = Other;
		const FVector Direction = Victim ? (Other->GetActorLocation() - Victim->GetActorLocation()).GetSafeNormal() : FVector::ForwardVector;
		const FHitResult Hit(Other, nullptr, Other->GetActorLocation(), -Direction);
		UGameplayStatics::ApplyPointDamage(Other, ImpactDamage * Melee->DamageScale, Direction, Hit, InstigatorController, Owner, DamageType);
		UNoFearOpportunitySubsystem::AddTag(Other, ENoFearOpportunity::KnockedDown, KnockDownTime);
		if (ACharacter* OtherCharacter = Cast<ACharacter>(Other))
		{
			OtherCharacter->LaunchCharacter(Direction.GetSafeNormal2D() * 400.f + FVector::UpVector * 150.f, true, true);
		}
	}
	if (Victim)
	{
		UNoFearOpportunitySubsystem::AddTag(Victim, ENoFearOpportunity::KnockedDown, KnockDownTime);
	}
	Held.Reset();
}

void UNoFearMonsterAttack_Grab::EndAttack(UNoFearMeleeComponent* Melee, ACharacter* Owner)
{
	// Прервали (убили колосса, оглушили): отпускает на месте.
	if (ACharacter* Victim = Held.Get())
	{
		Release(Victim);
	}
	bThrown = false;
}
