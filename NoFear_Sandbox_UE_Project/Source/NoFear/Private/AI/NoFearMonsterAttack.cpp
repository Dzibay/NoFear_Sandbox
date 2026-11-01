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

bool UNoFearMonsterAttack_Leap::TickStrike(UNoFearMeleeComponent* Melee, ACharacter* Owner, AActor* Target, float DeltaTime)
{
	// Прыжок начинается со следующего шага движения; приземлился — удар закончен.
	const UCharacterMovementComponent* Movement = Owner ? Owner->GetCharacterMovement() : nullptr;
	const double Elapsed = Melee->GetWorld()->GetTimeSeconds() - StrikeStartTime;
	return !Movement || Elapsed < 0.15 || Movement->IsFalling();
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

// --- Плевок ---

UNoFearMonsterAttack_Spit::UNoFearMonsterAttack_Spit()
{
	AttackName = TEXT("Spit");
	MinRange = 500.f;
	MaxRange = 1600.f;
	Cooldown = 2.2f;
	Weight = 1.f;
	MaxHeightDifference = 400.f;
	Damage = 10.f;
	bSweepHits = false;
	WindupTime = 0.45f;
	HitWindow = 0.1f;
	RecoveryTime = 0.45f;
	Pose = ENoFearPose::Spit;
	ProjectileClass = ANoFearSpitProjectile::StaticClass();
}

bool UNoFearMonsterAttack_Spit::CanUse(const UNoFearMeleeComponent* Melee, const AActor* Target) const
{
	if (!Super::CanUse(Melee, Target))
	{
		return false;
	}
	// Плюёт, только если видит цель: иначе снаряд уйдёт в стену.
	const AActor* Owner = Melee->GetOwner();
	FCollisionQueryParams Params(SCENE_QUERY_STAT(NoFearSpitSight), false, Owner);
	Params.AddIgnoredActor(Target);
	const FVector Start = Owner->GetActorLocation() + FVector(0.f, 0.f, MuzzleUp);
	return !Melee->GetWorld()->LineTraceTestByChannel(Start, Target->GetActorLocation(), ECC_Visibility, Params);
}

void UNoFearMonsterAttack_Spit::BeginStrike(UNoFearMeleeComponent* Melee, ACharacter* Owner, AActor* Target)
{
	UWorld* World = Melee->GetWorld();
	if (!Owner || !Target || !ProjectileClass || !World)
	{
		return;
	}

	const FVector Start = Owner->GetActorLocation() + Owner->GetActorForwardVector().GetSafeNormal2D() * MuzzleForward + FVector(0.f, 0.f, MuzzleUp);
	FVector Aim = Target->GetActorLocation();
	FVector Velocity;
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

	FActorSpawnParameters SpawnParams;
	SpawnParams.Owner = Owner;
	SpawnParams.Instigator = Owner;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AActor* Projectile = World->SpawnActor<AActor>(ProjectileClass, Start, Velocity.Rotation(), SpawnParams);
	if (ANoFearSpitProjectile* Spit = Cast<ANoFearSpitProjectile>(Projectile))
	{
		Spit->DirectDamage = Damage * Melee->DamageScale;
		Spit->Launch(Velocity);
	}
	if (Projectile)
	{
		++ShotCount;
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
	// Направление выбирается один раз в конце замаха: от тарана можно уйти в сторону.
	ChargeDirection = Owner->GetActorForwardVector().GetSafeNormal2D();
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
