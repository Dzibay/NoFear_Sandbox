#include "AI/NoFearMeleeComponent.h"

#include "Animation/AnimInstance.h"
#include "Core/NoFearHealthComponent.h"
#include "Core/NoFearTeam.h"
#include "Engine/OverlapResult.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/DamageType.h"
#include "Kismet/GameplayStatics.h"
#include "TimerManager.h"

UNoFearMeleeComponent::UNoFearMeleeComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
	DamageType = UDamageType::StaticClass();
}

bool UNoFearMeleeComponent::IsOwnerAlive() const
{
	return GetOwner() && !UNoFearHealthComponent::IsActorDead(GetOwner());
}

bool UNoFearMeleeComponent::CanAttack() const
{
	if (Phase != EPhase::Idle || !IsOwnerAlive() || GetWorld()->GetTimeSeconds() < NextAttackTime)
	{
		return false;
	}
	const ACharacter* Character = Cast<ACharacter>(GetOwner());
	const UCharacterMovementComponent* Movement = Character ? Character->GetCharacterMovement() : nullptr;
	return !Movement || !Movement->IsFalling();
}

bool UNoFearMeleeComponent::IsInAttackRange(const AActor* Target) const
{
	return Target && GetOwner() && FVector::DistSquared(Target->GetActorLocation(), GetOwner()->GetActorLocation()) <= FMath::Square(AttackRange);
}

bool UNoFearMeleeComponent::StartAttack(AActor* Target)
{
	if (!Target || !CanAttack())
	{
		return false;
	}

	AttackTarget = Target;
	HitActors.Reset();
	NextAttackTime = GetWorld()->GetTimeSeconds() + Cooldown;
	Phase = EPhase::Windup;
	SetComponentTickEnabled(true);

	if (const ACharacter* Character = Cast<ACharacter>(GetOwner()); Montage && Character && Character->GetMesh())
	{
		if (UAnimInstance* Anim = Character->GetMesh()->GetAnimInstance())
		{
			Anim->Montage_Play(Montage, MontagePlayRate);
		}
	}

	OnAttackStarted.Broadcast(Target);

	if (WindupTime > 0.f)
	{
		GetWorld()->GetTimerManager().SetTimer(PhaseTimer, this, &UNoFearMeleeComponent::BeginLunge, WindupTime, false);
	}
	else
	{
		BeginLunge();
	}
	return true;
}

void UNoFearMeleeComponent::BeginLunge()
{
	if (!IsOwnerAlive())
	{
		FinishAttack();
		return;
	}

	Phase = EPhase::Lunge;
	FaceTarget(0.f);

	AActor* Owner = GetOwner();
	if (SwingSound)
	{
		UGameplayStatics::PlaySoundAtLocation(this, SwingSound, Owner->GetActorLocation());
	}
	if (ACharacter* Character = Cast<ACharacter>(Owner); Character && (LungeSpeed > 0.f || LungeUpSpeed > 0.f))
	{
		const FVector Forward = Character->GetActorForwardVector().GetSafeNormal2D();
		Character->LaunchCharacter(Forward * LungeSpeed + FVector::UpVector * LungeUpSpeed, true, true);
	}

	SweepHits();
	GetWorld()->GetTimerManager().SetTimer(PhaseTimer, this, &UNoFearMeleeComponent::EndHitWindow, HitWindow, false);
}

void UNoFearMeleeComponent::EndHitWindow()
{
	Phase = EPhase::Recovery;
	if (RecoveryTime > 0.f)
	{
		GetWorld()->GetTimerManager().SetTimer(PhaseTimer, this, &UNoFearMeleeComponent::FinishAttack, RecoveryTime, false);
	}
	else
	{
		FinishAttack();
	}
}

void UNoFearMeleeComponent::FinishAttack()
{
	Phase = EPhase::Idle;
	AttackTarget.Reset();
	HitActors.Reset();
	SetComponentTickEnabled(false);
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(PhaseTimer);
	}
}

void UNoFearMeleeComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	FinishAttack();
	Super::EndPlay(EndPlayReason);
}

void UNoFearMeleeComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (!IsOwnerAlive())
	{
		FinishAttack();
		return;
	}

	if (Phase == EPhase::Windup)
	{
		FaceTarget(DeltaTime);
	}
	else if (Phase == EPhase::Lunge)
	{
		SweepHits();
	}
}

void UNoFearMeleeComponent::FaceTarget(float DeltaTime)
{
	AActor* Owner = GetOwner();
	const AActor* Target = AttackTarget.Get();
	if (!Owner || !Target)
	{
		return;
	}

	const FVector ToTarget = (Target->GetActorLocation() - Owner->GetActorLocation()).GetSafeNormal2D();
	if (ToTarget.IsNearlyZero())
	{
		return;
	}

	const FRotator Current = Owner->GetActorRotation();
	const FRotator Desired(0.f, ToTarget.Rotation().Yaw, 0.f);
	// DeltaTime == 0 — развернуть сразу (перед рывком).
	const FRotator NewRotation = DeltaTime > 0.f && WindupTurnRate > 0.f
		? FMath::RInterpConstantTo(Current, Desired, DeltaTime, WindupTurnRate)
		: Desired;
	Owner->SetActorRotation(FRotator(Current.Pitch, NewRotation.Yaw, Current.Roll));
}

void UNoFearMeleeComponent::SweepHits()
{
	AActor* Owner = GetOwner();
	UWorld* World = GetWorld();
	if (!Owner || !World)
	{
		return;
	}

	const FVector Center = Owner->GetActorLocation() + Owner->GetActorForwardVector().GetSafeNormal2D() * HitReach;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(NoFearMelee), false, Owner);
	TArray<FOverlapResult> Overlaps;
	World->OverlapMultiByObjectType(Overlaps, Center, FQuat::Identity, FCollisionObjectQueryParams(ECC_Pawn), FCollisionShape::MakeSphere(HitRadius), Params);

	APawn* OwnerPawn = Cast<APawn>(Owner);
	AController* InstigatorController = OwnerPawn ? OwnerPawn->GetController() : nullptr;
	for (const FOverlapResult& Overlap : Overlaps)
	{
		AActor* Victim = Overlap.GetActor();
		if (!Victim || Victim == Owner || HitActors.Contains(Victim) || UNoFearHealthComponent::IsActorDead(Victim))
		{
			continue;
		}
		// Своих не задевает; врагов и нейтралов, попавших под удар, — задевает.
		if (UNoFearTeamLibrary::AreFriendly(Owner, Victim) || !UNoFearHealthComponent::FindHealth(Victim))
		{
			continue;
		}
		HitActors.Add(Victim);

		const FVector Direction = (Victim->GetActorLocation() - Owner->GetActorLocation()).GetSafeNormal();
		FHitResult Hit(Victim, Overlap.GetComponent(), Victim->GetActorLocation() - Direction * HitRadius * 0.5f, -Direction);
		UGameplayStatics::ApplyPointDamage(Victim, Damage, Direction, Hit, InstigatorController, Owner, DamageType);

		if (HitSound)
		{
			UGameplayStatics::PlaySoundAtLocation(this, HitSound, Hit.ImpactPoint);
		}
		OnAttackHit.Broadcast(Victim, Damage);
	}
}
