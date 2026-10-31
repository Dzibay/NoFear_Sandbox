#include "AI/NoFearMonsterAttack.h"

#include "AI/NoFearMeleeComponent.h"
#include "Core/NoFearHealthComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/DamageType.h"
#include "Kismet/GameplayStatics.h"

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
	}
	if (HitSound)
	{
		UGameplayStatics::PlaySoundAtLocation(Melee, HitSound, Hit.ImpactPoint);
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
