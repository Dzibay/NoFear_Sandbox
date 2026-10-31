#include "AI/NoFearMonsterKinds.h"

#include "AI/NoFearMeleeComponent.h"
#include "AI/NoFearMonsterAttack.h"
#include "Animation/NoFearPoseComponent.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "Core/NoFearCharacter.h"
#include "Core/NoFearHealthComponent.h"
#include "Core/NoFearTeam.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Weapons/NoFearWeaponComponent.h"

// --- Прыгун ---

ANoFearJumperAIController::ANoFearJumperAIController(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	CircleMinRadius = 600.f;
	CircleMaxRadius = 850.f;

	UNoFearMonsterAttack_Leap* Leap = CreateDefaultSubobject<UNoFearMonsterAttack_Leap>(TEXT("Leap"));
	AttackSet.Add(Leap);

	// Вплотную — быстрый слабый удар.
	UNoFearMonsterAttack_Swing* Swipe = CreateDefaultSubobject<UNoFearMonsterAttack_Swing>(TEXT("Swipe"));
	Swipe->AttackName = TEXT("Swipe");
	Swipe->Damage = 12.f;
	Swipe->Cooldown = 0.9f;
	Swipe->Weight = 0.5f;
	Swipe->WindupTime = 0.25f;
	Swipe->RecoveryTime = 0.25f;
	AttackSet.Add(Swipe);
}

void ANoFearJumperAIController::UpdateBot()
{
	Super::UpdateBot();

	FVector AimDirection;
	if (bDodgeWhenAimedAt && GetNow() >= NextDodgeTime && FindAimingThreat(AimDirection))
	{
		Dodge(AimDirection);
	}
}

bool ANoFearJumperAIController::FindAimingThreat(FVector& OutAimDirection) const
{
	const APawn* MyPawn = GetPawn();
	if (!MyPawn)
	{
		return false;
	}
	const FVector MyLocation = MyPawn->GetActorLocation();

	for (const TPair<TWeakObjectPtr<AActor>, FNoFearKnownActor>& Pair : KnownActors)
	{
		const APawn* Shooter = Cast<APawn>(Pair.Key.Get());
		if (!Shooter || !Pair.Value.bInView || UNoFearHealthComponent::IsActorDead(Shooter) || !UNoFearTeamLibrary::AreHostile(MyPawn, Shooter))
		{
			continue;
		}
		const ANoFearCharacter* ShooterCharacter = Cast<ANoFearCharacter>(Shooter);
		const UNoFearWeaponComponent* Weapons = ShooterCharacter ? ShooterCharacter->GetWeaponComponent() : nullptr;
		if (!Weapons || !Weapons->GetCurrentWeapon())
		{
			continue;
		}

		const FVector Eye = Shooter->GetPawnViewLocation();
		const FVector ToMe = MyLocation - Eye;
		const float Distance = ToMe.Size();
		if (Distance > DodgeMaxDistance || Distance < 1.f)
		{
			continue;
		}
		const FVector Aim = Shooter->GetBaseAimRotation().Vector();
		// Вблизи считается попаданием в тело (~50 см вбок), вдали — по углу.
		const float Threshold = FMath::Max(DodgeAimAngle, FMath::RadiansToDegrees(FMath::Atan2(50.f, Distance)));
		const float Angle = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(FVector::DotProduct(Aim, ToMe / Distance), -1.f, 1.f)));
		if (Angle <= Threshold)
		{
			OutAimDirection = Aim;
			return true;
		}
	}
	return false;
}

bool ANoFearJumperAIController::Dodge(FVector AimDirection)
{
	ACharacter* MyCharacter = Cast<ACharacter>(GetPawn());
	const UCharacterMovementComponent* Movement = MyCharacter ? MyCharacter->GetCharacterMovement() : nullptr;
	const UNoFearMeleeComponent* Melee = GetMeleeComponent();
	if (!Movement || Movement->IsFalling() || UNoFearHealthComponent::IsActorDead(MyCharacter) || (Melee && Melee->IsAttacking())
		|| UNoFearPoseComponent::IsActorStaggered(MyCharacter))
	{
		return false;
	}

	FVector Side = FVector::CrossProduct(FVector::UpVector, AimDirection.GetSafeNormal2D()).GetSafeNormal();
	if (Side.IsNearlyZero())
	{
		Side = MyCharacter->GetActorRightVector();
	}
	if (FMath::RandBool())
	{
		Side = -Side;
	}
	// Сбоку стена — прыгнуть в другую сторону.
	const FVector Start = MyCharacter->GetActorLocation();
	FCollisionQueryParams Params(SCENE_QUERY_STAT(NoFearDodge), false, MyCharacter);
	if (GetWorld()->LineTraceTestByChannel(Start, Start + Side * 350.f, ECC_WorldStatic, Params))
	{
		Side = -Side;
	}

	StopMovement();
	MyCharacter->LaunchCharacter(Side * DodgeSpeed + FVector::UpVector * DodgeUpSpeed, true, true);
	if (const ANoFearCharacter* NoFearCharacter = Cast<ANoFearCharacter>(MyCharacter))
	{
		if (UNoFearPoseComponent* Pose = NoFearCharacter->GetPoseComponent())
		{
			Pose->PlayPose(ENoFearPose::Lunge, 0.3f, 0.6f);
		}
	}
	++DodgeCount;
	NextDodgeTime = GetNow() + DodgeCooldown;
	return true;
}

void ANoFearJumperAIController::UpdateMovementStance()
{
	Super::UpdateMovementStance();
	// К цели — спринтом: в свою очередь или без стаи. На место в строю и в кольце бежит вместе со всеми.
	FVector PackGoal;
	if (CurrentTarget.IsValid() && !IsCircling() && !IsScattering() && (HasAttackToken() || !GetPackGoal(PackGoal)))
	{
		SetMovementFlags(false, false, false, true);
	}
}

// --- Плевун ---

ANoFearSpitterAIController::ANoFearSpitterAIController(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// Плюёт издалека и никому не мешает: очередь за жетоном ему не нужна.
	bUseAttackTokens = false;

	UNoFearMonsterAttack_Spit* Spit = CreateDefaultSubobject<UNoFearMonsterAttack_Spit>(TEXT("Spit"));
	AttackSet.Add(Spit);

	// Прижали — отбивается.
	UNoFearMonsterAttack_Swing* Swipe = CreateDefaultSubobject<UNoFearMonsterAttack_Swing>(TEXT("Swipe"));
	Swipe->AttackName = TEXT("Swipe");
	Swipe->MaxRange = 220.f;
	Swipe->Damage = 8.f;
	Swipe->Cooldown = 1.5f;
	AttackSet.Add(Swipe);
}

void ANoFearSpitterAIController::ConfigureMelee(UNoFearMeleeComponent* Melee)
{
	Super::ConfigureMelee(Melee);
	if (Melee)
	{
		Melee->bHoldPositionInRange = true;
	}
}

void ANoFearSpitterAIController::UpdateBot()
{
	Super::UpdateBot();

	const float Distance = GetTargetDistance2D();
	const bool bWasKeepingAway = bKeepingAway;
	if (!CurrentTarget.IsValid() || UNoFearHealthComponent::IsActorDead(GetPawn()))
	{
		bKeepingAway = false;
	}
	else if (!bKeepingAway && Distance < RetreatStartDistance)
	{
		bKeepingAway = true;
	}
	else if (bKeepingAway && Distance > RetreatStopDistance)
	{
		bKeepingAway = false;
	}
	if (bKeepingAway && !bWasKeepingAway)
	{
		++RetreatCount;
	}
	if (UBlackboardComponent* BlackboardComp = GetBlackboardComponent())
	{
		BlackboardComp->SetValueAsBool(FNoFearBotKeys::KeepAway, bKeepingAway);
	}
}

void ANoFearSpitterAIController::WriteBlackboard()
{
	Super::WriteBlackboard();
	if (UBlackboardComponent* BlackboardComp = GetBlackboardComponent())
	{
		BlackboardComp->SetValueAsBool(FNoFearBotKeys::KeepAway, bKeepingAway);
	}
}

// --- Танк ---

ANoFearTankAIController::ANoFearTankAIController(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	CircleMinRadius = 450.f;
	CircleMaxRadius = 650.f;

	// Широкий удар с размаху: задевает перед собой полукругом и отбрасывает.
	UNoFearMonsterAttack_Swing* Smash = CreateDefaultSubobject<UNoFearMonsterAttack_Swing>(TEXT("Smash"));
	Smash->AttackName = TEXT("Smash");
	Smash->MaxRange = 290.f;
	Smash->Cooldown = 2.f;
	Smash->Damage = 35.f;
	Smash->HitReach = 140.f;
	Smash->HitRadius = 140.f;
	Smash->KnockbackSpeed = 800.f;
	Smash->KnockbackUpSpeed = 300.f;
	Smash->WindupTime = 0.6f;
	Smash->HitWindow = 0.25f;
	Smash->RecoveryTime = 0.7f;
	Smash->WindupTurnRate = 360.f;
	Smash->LungeSpeed = 150.f;
	Smash->LungeStopDistance = 140.f;
	AttackSet.Add(Smash);

	UNoFearMonsterAttack_Charge* Charge = CreateDefaultSubobject<UNoFearMonsterAttack_Charge>(TEXT("Charge"));
	AttackSet.Add(Charge);
}
