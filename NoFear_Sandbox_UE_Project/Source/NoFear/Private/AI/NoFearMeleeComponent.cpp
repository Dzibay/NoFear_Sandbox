#include "AI/NoFearMeleeComponent.h"

#include "AI/NoFearBotSubsystem.h"
#include "AI/NoFearMonsterAttack.h"
#include "Animation/AnimInstance.h"
#include "Animation/NoFearPoseComponent.h"
#include "Core/NoFearHealthComponent.h"
#include "Core/NoFearTeam.h"
#include "Engine/OverlapResult.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "TimerManager.h"

UNoFearMeleeComponent::UNoFearMeleeComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;

	Attacks.Add(CreateDefaultSubobject<UNoFearMonsterAttack_Swing>(TEXT("Swing")));
	Attacks.Add(CreateDefaultSubobject<UNoFearMonsterAttack_Lunge>(TEXT("Lunge")));
}

bool UNoFearMeleeComponent::IsOwnerAlive() const
{
	return GetOwner() && !UNoFearHealthComponent::IsActorDead(GetOwner());
}

bool UNoFearMeleeComponent::CanAttack() const
{
	if (Phase != EPhase::Idle || !IsOwnerAlive() || GetWorld()->GetTimeSeconds() < NextAttackTime || UNoFearPoseComponent::IsActorStaggered(GetOwner()))
	{
		return false;
	}
	const ACharacter* Character = Cast<ACharacter>(GetOwner());
	const UCharacterMovementComponent* Movement = Character ? Character->GetCharacterMovement() : nullptr;
	return !Movement || !Movement->IsFalling();
}

bool UNoFearMeleeComponent::IsReady(const UNoFearMonsterAttack* Attack) const
{
	return Attack && GetWorld()->GetTimeSeconds() >= Attack->NextUseTime;
}

bool UNoFearMeleeComponent::CanAttackTarget(const AActor* Target) const
{
	return CanAttack() && ChooseAttack(Target) != nullptr;
}

bool UNoFearMeleeComponent::IsInAttackRange(const AActor* Target) const
{
	for (const UNoFearMonsterAttack* Attack : Attacks)
	{
		if (Attack && Attack->CanUse(this, Target))
		{
			return true;
		}
	}
	return false;
}

float UNoFearMeleeComponent::GetMaxAttackRange() const
{
	float Range = 0.f;
	for (const UNoFearMonsterAttack* Attack : Attacks)
	{
		if (Attack)
		{
			Range = FMath::Max(Range, Attack->MaxRange);
		}
	}
	return Range;
}

UNoFearMonsterAttack* UNoFearMeleeComponent::FindAttack(FName Name) const
{
	for (UNoFearMonsterAttack* Attack : Attacks)
	{
		if (Attack && Attack->AttackName == Name)
		{
			return Attack;
		}
	}
	return nullptr;
}

void UNoFearMeleeComponent::SetAttacks(const TArray<UNoFearMonsterAttack*>& Templates)
{
	FinishAttack();
	Attacks.Reset();
	for (const UNoFearMonsterAttack* Template : Templates)
	{
		if (Template)
		{
			Attacks.Add(DuplicateObject<UNoFearMonsterAttack>(Template, this));
		}
	}
}

UNoFearMonsterAttack* UNoFearMeleeComponent::ChooseAttack(const AActor* Target) const
{
	if (!Target)
	{
		return nullptr;
	}
	TArray<UNoFearMonsterAttack*, TInlineAllocator<8>> Candidates;
	float TotalWeight = 0.f;
	for (UNoFearMonsterAttack* Attack : Attacks)
	{
		if (Attack && Attack->Weight > 0.f && IsReady(Attack) && Attack->CanUse(this, Target))
		{
			Candidates.Add(Attack);
			TotalWeight += Attack->Weight;
		}
	}
	if (Candidates.IsEmpty())
	{
		return nullptr;
	}
	float Roll = FMath::FRand() * TotalWeight;
	for (UNoFearMonsterAttack* Attack : Candidates)
	{
		Roll -= Attack->Weight;
		if (Roll <= 0.f)
		{
			return Attack;
		}
	}
	return Candidates.Last();
}

bool UNoFearMeleeComponent::StartAttack(AActor* Target)
{
	if (!Target || !CanAttack())
	{
		return false;
	}
	UNoFearMonsterAttack* Attack = ChooseAttack(Target);
	return Attack && BeginAttack(Attack, Target);
}

bool UNoFearMeleeComponent::StartAttackByName(AActor* Target, FName Name, bool bIgnoreRange)
{
	UNoFearMonsterAttack* Attack = FindAttack(Name);
	if (!Attack || !Target || !CanAttack())
	{
		return false;
	}
	if (!bIgnoreRange && (!IsReady(Attack) || !Attack->CanUse(this, Target)))
	{
		return false;
	}
	return BeginAttack(Attack, Target);
}

bool UNoFearMeleeComponent::BeginAttack(UNoFearMonsterAttack* Attack, AActor* Target)
{
	UWorld* World = GetWorld();
	CurrentAttack = Attack;
	AttackTarget = Target;
	HitActors.Reset();
	LastAttackName = Attack->AttackName;
	Attack->bHitThisAttack = false;
	++AttackCount;
	Attack->NextUseTime = World->GetTimeSeconds() + Attack->Cooldown * CooldownScale;
	Phase = EPhase::Windup;
	SetComponentTickEnabled(true);

	ACharacter* Character = Cast<ACharacter>(GetOwner());
	if (Character && Attack->Montage && Character->GetMesh())
	{
		if (UAnimInstance* Anim = Character->GetMesh()->GetAnimInstance())
		{
			Anim->Montage_Play(Attack->Montage, Attack->MontagePlayRate);
		}
	}
	if (UNoFearPoseComponent* Pose = GetOwner()->FindComponentByClass<UNoFearPoseComponent>())
	{
		// Поза держится весь замах и начало удара, потом плавно уходит — выход из позы и читается как удар.
		Pose->PlayPose(Attack->Pose, Attack->WindupTime + Attack->HitWindow * 0.5f, Attack->PoseIntensity);
	}
	Attack->BeginWindup(this, Character, Target);

	OnAttackStarted.Broadcast(Target);

	// Драка рядом пугает мирных.
	const FVector OwnerLocation = GetOwner()->GetActorLocation();
	UNoFearBotSubsystem::ReportDanger(this, ENoFearDangerType::Fight, GetOwner(), OwnerLocation, UNoFearBotSubsystem::FightNoticeRadius, OwnerLocation);

	if (Attack->WindupTime > 0.f)
	{
		World->GetTimerManager().SetTimer(PhaseTimer, this, &UNoFearMeleeComponent::BeginStrike, Attack->WindupTime, false);
	}
	else
	{
		BeginStrike();
	}
	return true;
}

void UNoFearMeleeComponent::BeginStrike()
{
	UNoFearMonsterAttack* Attack = CurrentAttack;
	if (!IsOwnerAlive() || !Attack)
	{
		FinishAttack();
		return;
	}

	Phase = EPhase::Strike;
	FaceTarget(0.f);

	AActor* Owner = GetOwner();
	if (Attack->SwingSound)
	{
		UGameplayStatics::PlaySoundAtLocation(this, Attack->SwingSound, Owner->GetActorLocation());
	}
	Attack->StrikeStartTime = GetWorld()->GetTimeSeconds();
	Attack->BeginStrike(this, Cast<ACharacter>(Owner), AttackTarget.Get());

	SweepHits();
	GetWorld()->GetTimerManager().SetTimer(PhaseTimer, this, &UNoFearMeleeComponent::EndHitWindow, Attack->HitWindow, false);
}

void UNoFearMeleeComponent::EndHitWindow()
{
	if (Phase != EPhase::Strike)
	{
		return;
	}
	Phase = EPhase::Recovery;
	if (CurrentAttack)
	{
		CurrentAttack->BeginRecovery(this, Cast<ACharacter>(GetOwner()), AttackTarget.Get());
	}
	const float RecoveryTime = CurrentAttack ? CurrentAttack->RecoveryTime : 0.f;
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
	if (Phase == EPhase::Idle)
	{
		return;
	}
	UNoFearMonsterAttack* Attack = CurrentAttack;
	AActor* Target = AttackTarget.Get();
	Phase = EPhase::Idle;
	CurrentAttack = nullptr;
	AttackTarget.Reset();
	HitActors.Reset();
	SetComponentTickEnabled(false);
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(PhaseTimer);
		NextAttackTime = World->GetTimeSeconds() + MinTimeBetweenAttacks;
	}
	if (Attack)
	{
		Attack->EndAttack(this, Cast<ACharacter>(GetOwner()));
	}
	OnAttackFinished.Broadcast(Target);
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
		return;
	}

	if (Phase == EPhase::Strike)
	{
		SweepHits();
		if (CurrentAttack && !CurrentAttack->TickStrike(this, Cast<ACharacter>(GetOwner()), AttackTarget.Get(), DeltaTime))
		{
			GetWorld()->GetTimerManager().ClearTimer(PhaseTimer);
			EndHitWindow();
		}
	}
	// После TickStrike: атака могла сама задать скорость (таран).
	KeepOffTarget();
}

void UNoFearMeleeComponent::KeepOffTarget()
{
	// Капсулы персонажей друг друга не блокируют: без этого рывок проносит монстра сквозь цель.
	const ACharacter* Owner = Cast<ACharacter>(GetOwner());
	UCharacterMovementComponent* Movement = Owner ? Owner->GetCharacterMovement() : nullptr;
	const AActor* Target = AttackTarget.Get();
	if (!Movement || !Target || !CurrentAttack)
	{
		return;
	}
	const FVector ToTarget = Target->GetActorLocation() - Owner->GetActorLocation();
	if (ToTarget.Size2D() >= CurrentAttack->LungeStopDistance)
	{
		return;
	}
	const FVector Dir = ToTarget.GetSafeNormal2D();
	const float Closing = FVector::DotProduct(Movement->Velocity, Dir);
	if (Closing > 0.f)
	{
		Movement->Velocity -= Dir * Closing;
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
	// DeltaTime == 0 — развернуть сразу (перед ударом).
	const float TurnRate = CurrentAttack ? CurrentAttack->WindupTurnRate : 0.f;
	const FRotator NewRotation = DeltaTime > 0.f && TurnRate > 0.f
		? FMath::RInterpConstantTo(Current, Desired, DeltaTime, TurnRate)
		: Desired;
	Owner->SetActorRotation(FRotator(Current.Pitch, NewRotation.Yaw, Current.Roll));
}

void UNoFearMeleeComponent::SweepHits()
{
	AActor* Owner = GetOwner();
	UWorld* World = GetWorld();
	UNoFearMonsterAttack* Attack = CurrentAttack;
	if (!Owner || !World || !Attack || !Attack->bSweepHits)
	{
		return;
	}

	const FVector Center = Owner->GetActorLocation() + Owner->GetActorForwardVector().GetSafeNormal2D() * Attack->HitReach;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(NoFearMelee), false, Owner);
	TArray<FOverlapResult> Overlaps;
	World->OverlapMultiByObjectType(Overlaps, Center, FQuat::Identity, FCollisionObjectQueryParams(ECC_Pawn), FCollisionShape::MakeSphere(Attack->HitRadius), Params);

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
		Attack->bHitThisAttack = true;

		const FVector Direction = (Victim->GetActorLocation() - Owner->GetActorLocation()).GetSafeNormal();
		const FHitResult Hit(Victim, Overlap.GetComponent(), Victim->GetActorLocation() - Direction * Attack->HitRadius * 0.5f, -Direction);
		Attack->ApplyHit(this, Victim, Hit);
		OnAttackHit.Broadcast(Victim, Attack->Damage * DamageScale);
	}
}
