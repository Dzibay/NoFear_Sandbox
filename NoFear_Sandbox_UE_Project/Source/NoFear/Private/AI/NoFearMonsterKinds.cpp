#include "AI/NoFearMonsterKinds.h"

#include "AI/NoFearMeleeComponent.h"
#include "AI/NoFearMonsterAttack.h"
#include "AI/NoFearOpportunitySubsystem.h"
#include "AI/NoFearBotSubsystem.h"
#include "AI/NoFearCivilianAIController.h"
#include "AI/NoFearCombatComponent.h"
#include "AI/NoFearTankShake.h"
#include "AI/NoFearVulnerabilityComponent.h"
#include "Animation/NoFearPoseComponent.h"
#include "Camera/CameraShakeBase.h"
#include "Components/AudioComponent.h"
#include "Core/NoFearBraceletComponent.h"
#include "Core/NoFearFearComponent.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "Core/NoFearCharacter.h"
#include "Core/NoFearHealthComponent.h"
#include "Core/NoFearTeam.h"
#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#include "NavigationSystem.h"
#include "TimerManager.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Weapons/NoFearWeapon.h"
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

	// Ловчий: голова ×1.5 (от загонщика); любит облитых слизью, отставших и сбитых.
	KindName = TEXT("Jumper");
	TargetWeights.Hero = 0.8f;
	TargetWeights.Companion = 0.8f;
	TargetWeights.Soldier = 0.8f;
	TargetWeights.Civilian = 0.4f;
	OpportunityBonuses.Reset();
	OpportunityBonuses.Add(ENoFearOpportunity::KnockedDown, 1.f);
	OpportunityBonuses.Add(ENoFearOpportunity::Slimed, 1.f);
	OpportunityBonuses.Add(ENoFearOpportunity::Isolated, 1.f);
	OpportunityBonuses.Add(ENoFearOpportunity::Marked, 1.5f);
}

void ANoFearTankAIController::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);

	bRaging = false;
	RageUntil = 0.0;
	bShielding = false;
	Shots.Reset();
	ShieldShooters.Reset();
	bHasLastStepLocation = false;
	StepTravel = 0.f;

	if (!InPawn)
	{
		return;
	}
	if (UNoFearVulnerabilityComponent* Vulnerability = InPawn->FindComponentByClass<UNoFearVulnerabilityComponent>())
	{
		Vulnerability->Plates.Reset();
		for (const TPair<FName, float>& Plate : { TPair<FName, float>(TEXT("PlateChest"), 0.f),
				TPair<FName, float>(TEXT("PlateShoulderL"), -35.f), TPair<FName, float>(TEXT("PlateShoulderR"), 35.f) })
		{
			FNoFearArmorPlate NewPlate;
			NewPlate.PlateZone = Plate.Key;
			NewPlate.AngleDeg = Plate.Value;
			Vulnerability->Plates.Add(NewPlate);
		}
		Vulnerability->AllStrippedZone = TEXT("FrontBare");
		Vulnerability->StunnedWeakSpotScale = 1.2f;
	}
	if (ANoFearCharacter* Body = Cast<ANoFearCharacter>(InPawn))
	{
		// Колосса слышно до того, как его видно.
		Body->WalkFootstepRange = StepFootstepRange;
		Body->RunFootstepRange = StepFootstepRange;
		Body->SprintFootstepRange = StepFootstepRange;
		if (UCharacterMovementComponent* Movement = Body->GetCharacterMovement())
		{
			BaseMaxWalkSpeed = Movement->MaxWalkSpeed;
		}
	}
}

void ANoFearTankAIController::HandleDanger(ENoFearDangerType Type, AActor* Source, FVector ThreatLocation)
{
	Super::HandleDanger(Type, Source, ThreatLocation);

	// Запоминает стрелков, чей ствол смотрит на колосса.
	APawn* MyPawn = GetPawn();
	const APawn* Shooter = Cast<APawn>(Source);
	if ((Type != ENoFearDangerType::Gunshot && Type != ENoFearDangerType::NearMiss) || !MyPawn || !Shooter || Shooter == MyPawn
		|| UNoFearTeamLibrary::AreFriendly(MyPawn, Shooter) || UNoFearHealthComponent::IsActorDead(Shooter))
	{
		return;
	}
	const FVector ToMe = MyPawn->GetActorLocation() - Shooter->GetActorLocation();
	if (ToMe.Size2D() > ShieldShooterRange)
	{
		return;
	}
	const float Angle = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(
		FVector::DotProduct(Shooter->GetBaseAimRotation().Vector().GetSafeNormal2D(), ToMe.GetSafeNormal2D()), -1.f, 1.f)));
	if (Angle > ShieldAimAngle)
	{
		return;
	}
	FShot& Shot = Shots.AddDefaulted_GetRef();
	Shot.Shooter = Source;
	Shot.Location = Shooter->GetActorLocation();
	Shot.Time = GetNow();
}

float ANoFearTankAIController::ScoreTarget(const AActor* Candidate, const FNoFearKnownActor& Knowledge) const
{
	float Score = Super::ScoreTarget(Candidate, Knowledge);
	// Щит: идёт на стрелков.
	if (bShielding && ShieldShooters.Contains(Candidate))
	{
		Score += ShieldTargetBonus;
	}
	return Score;
}

void ANoFearTankAIController::UpdateBot()
{
	Super::UpdateBot();

	APawn* MyPawn = GetPawn();
	if (!MyPawn || UNoFearHealthComponent::IsActorDead(MyPawn))
	{
		return;
	}
	UpdateRage();
	UpdateShield();
	UpdateSteps();

	// Рёв раз в RoarInterval, если цель близко и колосс не занят.
	const AActor* Target = CurrentTarget.Get();
	const UNoFearMeleeComponent* Melee = GetMeleeComponent();
	if (Target && !IsRoaring() && IsRoarReady() && !(Melee && Melee->IsAttacking())
		&& FVector::Dist(MyPawn->GetActorLocation(), Target->GetActorLocation()) <= RoarTargetRange)
	{
		Roar();
	}
}

void ANoFearTankAIController::Roar()
{
	Super::Roar();

	APawn* MyPawn = GetPawn();
	UWorld* World = GetWorld();
	if (!MyPawn || !World || UNoFearHealthComponent::IsActorDead(MyPawn))
	{
		return;
	}
	++TankRoarCount;
	const FVector Location = MyPawn->GetActorLocation();

	for (ANoFearCharacter* Human : TActorRange<ANoFearCharacter>(World))
	{
		if (!Human || Human == MyPawn || UNoFearHealthComponent::IsActorDead(Human) || UNoFearTeamLibrary::AreFriendly(MyPawn, Human))
		{
			continue;
		}
		if (FVector::Dist(Human->GetActorLocation(), Location) > RoarFearRadius)
		{
			continue;
		}
		// Герою — через браслет, остальным — напрямую.
		const APlayerController* PlayerController = Cast<APlayerController>(Human->GetController());
		if (UNoFearBraceletComponent* Bracelet = PlayerController ? PlayerController->FindComponentByClass<UNoFearBraceletComponent>() : nullptr)
		{
			Bracelet->ReceiveMonsterRoar(RoarFearAmount, RoarFearCap, Location, MyPawn);
		}
		else if (UNoFearFearComponent* Fear = UNoFearFearComponent::FindFear(Human))
		{
			Fear->AddFearFrom(RoarFearAmount, RoarFearCap, Location, MyPawn, true);
		}
	}
	// Мирные рядом срываются в бег.
	UNoFearBotSubsystem::ReportDanger(this, ENoFearDangerType::Fight, MyPawn, Location, RoarPanicRadius, Location);
	ShakeHeroCamera(RoarShake, RoarFearRadius * 2.f, 1.f);
}

void ANoFearTankAIController::ShakeHeroCamera(TSubclassOf<UCameraShakeBase> Shake, float Radius, float Scale)
{
	const APawn* MyPawn = GetPawn();
	const UWorld* World = GetWorld();
	if (!Shake || !MyPawn || !World)
	{
		return;
	}
	for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
	{
		APlayerController* PlayerController = It->Get();
		const APawn* Hero = PlayerController ? PlayerController->GetPawn() : nullptr;
		if (!Hero)
		{
			continue;
		}
		const float Distance = FVector::Dist(Hero->GetActorLocation(), MyPawn->GetActorLocation());
		if (Distance <= Radius)
		{
			PlayerController->ClientStartCameraShake(Shake, Scale * (1.f - 0.6f * Distance / FMath::Max(Radius, 1.f)));
		}
	}
}

void ANoFearTankAIController::UpdateSteps()
{
	const APawn* MyPawn = GetPawn();
	if (!MyPawn)
	{
		return;
	}
	const FVector Location = MyPawn->GetActorLocation();
	if (bHasLastStepLocation)
	{
		const float Moved = FVector::Dist2D(Location, LastStepLocation);
		// Рывок на сотни см за кадр — телепорт, а не шаги.
		if (Moved < 400.f && MyPawn->GetVelocity().Size2D() > 30.f)
		{
			StepTravel += Moved;
		}
	}
	LastStepLocation = Location;
	bHasLastStepLocation = true;
	if (StepTravel >= StepShakeDistance)
	{
		StepTravel = FMath::Fmod(StepTravel, StepShakeDistance);
		++StepShakeCount;
		ShakeHeroCamera(StepShake, StepShakeRadius, 1.f);
	}
}

void ANoFearTankAIController::UpdateShield()
{
	APawn* MyPawn = GetPawn();
	const double Now = GetNow();
	Shots.RemoveAll([this, Now](const FShot& Shot) { return Now - Shot.Time > ShieldMemory || !Shot.Shooter.IsValid(); });

	// Разные стрелки, последний выстрел каждого.
	TMap<AActor*, FVector> Latest;
	for (const FShot& Shot : Shots)
	{
		Latest.Add(Shot.Shooter.Get(), Shot.Location);
	}
	bShielding = false;
	ShieldShooters.Reset();
	ShieldedAllyCount = 0;
	if (Latest.Num() >= ShieldMinShooters)
	{
		// Со своей стороны: направления на стрелков не расходятся с их средним больше ShieldSideAngle.
		FVector Mean = FVector::ZeroVector;
		FVector Center = FVector::ZeroVector;
		for (const TPair<AActor*, FVector>& Entry : Latest)
		{
			Mean += (Entry.Value - MyPawn->GetActorLocation()).GetSafeNormal2D();
			Center += Entry.Value;
		}
		Mean = Mean.GetSafeNormal2D();
		Center /= Latest.Num();
		int32 SameSide = 0;
		for (const TPair<AActor*, FVector>& Entry : Latest)
		{
			const FVector Direction = (Entry.Value - MyPawn->GetActorLocation()).GetSafeNormal2D();
			if (!Mean.IsNearlyZero()
				&& FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(FVector::DotProduct(Mean, Direction), -1.f, 1.f))) <= ShieldSideAngle)
			{
				++SameSide;
				ShieldShooters.Add(Entry.Key);
			}
		}
		if (SameSide >= ShieldMinShooters)
		{
			bShielding = true;
			ShieldFireCenter = Center;
		}
		else
		{
			ShieldShooters.Reset();
		}
	}

	if (!bShielding)
	{
		return;
	}

	// Союзники в ShieldAllyRadius занимают места позади колосса относительно огня.
	UWorld* World = GetWorld();
	const FVector TankLocation = MyPawn->GetActorLocation();
	const FVector Away = (TankLocation - ShieldFireCenter).GetSafeNormal2D();
	const FVector Side = FVector::CrossProduct(FVector::UpVector, Away).GetSafeNormal();
	TArray<ANoFearMonsterAIController*> Allies;
	for (ANoFearCharacter* Candidate : TActorRange<ANoFearCharacter>(World))
	{
		ANoFearMonsterAIController* Ally = Candidate && Candidate != MyPawn ? Cast<ANoFearMonsterAIController>(Candidate->GetController()) : nullptr;
		if (Ally && !Ally->IsA<ANoFearTankAIController>() && !UNoFearHealthComponent::IsActorDead(Candidate)
			&& UNoFearTeamLibrary::AreFriendly(MyPawn, Candidate)
			&& FVector::Dist2D(Candidate->GetActorLocation(), TankLocation) <= ShieldAllyRadius)
		{
			Allies.Add(Ally);
		}
	}
	for (int32 Index = 0; Index < Allies.Num(); ++Index)
	{
		const float Lateral = (Index - 0.5f * (Allies.Num() - 1)) * ShieldShadeSpacing;
		Allies[Index]->SetShadeGoal(TankLocation + Away * ShieldShadeDistance + Side * Lateral, 0.6f);
	}
	ShieldedAllyCount = Allies.Num();
}

void ANoFearTankAIController::StartRage(float Duration)
{
	if (bRaging)
	{
		// Уже в ярости: временная продлевается, постоянная остаётся постоянной.
		if (RageUntil > 0.0)
		{
			RageUntil = Duration > 0.f ? FMath::Max(RageUntil, GetNow() + Duration) : 0.0;
		}
		return;
	}
	bRaging = true;
	RageUntil = Duration > 0.f ? GetNow() + Duration : 0.0;
	// Начинает с рёва.
	Roar();
	if (UNoFearMeleeComponent* Melee = GetMeleeComponent())
	{
		if (UNoFearMonsterAttack* Charge = Melee->FindAttack(TEXT("Charge")))
		{
			BaseChargeCooldown = Charge->Cooldown;
			BaseChargeMinRange = Charge->MinRange;
			Charge->Cooldown = BaseChargeCooldown * RageChargeCooldownScale;
			Charge->MinRange = FMath::Min(Charge->MinRange, RageChargeMinRange);
			// Не ждёт старой перезарядки.
			Charge->NextUseTime = 0.0;
		}
	}
}

void ANoFearTankAIController::UpdateRage()
{
	const APawn* MyPawn = GetPawn();
	UNoFearHealthComponent* Health = MyPawn ? UNoFearHealthComponent::FindHealth(MyPawn) : nullptr;
	if (!bRaging && Health && Health->GetHealthFraction() <= RageHealthFraction)
	{
		StartRage();
	}
	if (!bRaging)
	{
		return;
	}
	// Временная ярость кончилась (здоровье ещё не на пороге — иначе продолжится как постоянная).
	if (RageUntil > 0.0 && GetNow() >= RageUntil)
	{
		if (Health && Health->GetHealthFraction() <= RageHealthFraction)
		{
			RageUntil = 0.0;
		}
		else
		{
			bRaging = false;
			RageUntil = 0.0;
			if (UNoFearMeleeComponent* Melee = GetMeleeComponent())
			{
				if (UNoFearMonsterAttack* Charge = Melee->FindAttack(TEXT("Charge")))
				{
					Charge->Cooldown = BaseChargeCooldown;
					Charge->MinRange = BaseChargeMinRange;
				}
			}
			return;
		}
	}
	if (UNoFearMeleeComponent* Melee = GetMeleeComponent())
	{
		Melee->CooldownScale *= RageCooldownScale;
	}
}

void ANoFearTankAIController::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	APawn* MyPawn = GetPawn();
	if (!MyPawn)
	{
		return;
	}
	// Скорость и походка задаются Blueprint тела, поэтому ярость ускоряет само время тела, пока он идёт:
	// шаг 200 → RageSpeed. В ударе, рывке и рёве время обычное.
	float Dilation = 1.f;
	const UNoFearMeleeComponent* Melee = GetMeleeComponent();
	const ACharacter* Body = Cast<ACharacter>(MyPawn);
	const float WalkSpeed = Body && Body->GetCharacterMovement() ? Body->GetCharacterMovement()->MaxWalkSpeed : 0.f;
	if (bRaging && WalkSpeed > 1.f && !IsRoaring() && !(Melee && Melee->IsAttacking()))
	{
		Dilation = FMath::Max(1.f, RageSpeed / WalkSpeed);
	}
	// Бафф шептуна поверх (время тела колосс выставляет сам каждый кадр).
	if (IsWhisperBuffed() && !(Melee && Melee->IsAttacking()))
	{
		Dilation *= WhisperBuffSpeedScale;
	}
	MyPawn->CustomTimeDilation = Dilation;
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

bool ANoFearJumperAIController::GetStandoffPoint(const FVector& TargetLocation, const FVector& PackSide, FVector& OutPoint) const
{
	const APawn* MyPawn = GetPawn();
	const AActor* Target = CurrentTarget.Get();
	if (!MyPawn || !Target)
	{
		return false;
	}
	// Заходит с той стороны взгляда цели, где уже стоит: во фланг и за спину, не пересекая линию огня.
	const APawn* TargetPawn = Cast<APawn>(Target);
	FVector Facing = (TargetPawn ? TargetPawn->GetBaseAimRotation().Vector() : Target->GetActorForwardVector()).GetSafeNormal2D();
	if (Facing.IsNearlyZero())
	{
		Facing = -PackSide;
	}
	const FVector ToMe = (MyPawn->GetActorLocation() - TargetLocation).GetSafeNormal2D();
	const float Side = FVector::CrossProduct(Facing, ToMe).Z >= 0.f ? 1.f : -1.f;
	OutPoint = TargetLocation + Facing.RotateAngleAxis(Side * StalkAngle, FVector::UpVector) * StalkDistance;
	return true;
}

bool ANoFearJumperAIController::IsTargetDistracted(const AActor* Target) const
{
	const APawn* MyPawn = GetPawn();
	if (!MyPawn || !Target)
	{
		return false;
	}
	for (const ENoFearOpportunity Tag : { ENoFearOpportunity::KnockedDown, ENoFearOpportunity::Slimed, ENoFearOpportunity::Pinned,
		ENoFearOpportunity::Blinded })
	{
		if (UNoFearOpportunitySubsystem::HasTag(Target, Tag))
		{
			return true;
		}
	}

	// Без оружия в руках (мирный) или перезаряжается — открыт.
	const ANoFearCharacter* TargetCharacter = Cast<ANoFearCharacter>(Target);
	const UNoFearWeaponComponent* Weapons = TargetCharacter ? TargetCharacter->GetWeaponComponent() : nullptr;
	const ANoFearWeapon* Weapon = Weapons ? Weapons->GetCurrentWeapon() : nullptr;
	if (!Weapon || Weapon->GetWeaponState() != ENoFearWeaponState::Idle)
	{
		return true;
	}

	// Стоит спиной или стреляет в другую сторону.
	const APawn* TargetPawn = Cast<APawn>(Target);
	const FVector Facing = (TargetPawn ? TargetPawn->GetBaseAimRotation().Vector() : Target->GetActorForwardVector()).GetSafeNormal2D();
	const FVector ToMe = (MyPawn->GetActorLocation() - Target->GetActorLocation()).GetSafeNormal2D();
	const float Angle = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(static_cast<float>(FVector::DotProduct(Facing, ToMe)), -1.f, 1.f)));
	if (Angle >= BackTurnedAngle)
	{
		return true;
	}
	const UWorld* World = GetWorld();
	const bool bFiring = World && World->GetTimeSeconds() - Weapon->GetLastFireTime() <= BusyFireWindow;
	return bFiring && Angle >= BusyAngle;
}

bool ANoFearJumperAIController::WantsFreeAttack(AActor* Target)
{
	const double Now = GetNow();
	if (Target != WaitTarget.Get())
	{
		WaitTarget = Target;
		WaitStartTime = Now;
		bLeapCommitted = false;
	}
	if (bLeapCommitted)
	{
		return true;
	}
	if (Now < NextLeapTime)
	{
		return false;
	}
	if (IsTargetDistracted(Target))
	{
		++OpportunityLeaps;
		bLeapCommitted = true;
	}
	else if (Now - WaitStartTime >= LeapPatience)
	{
		++ImpatientLeaps;
		bLeapCommitted = true;
	}
	return bLeapCommitted;
}

void ANoFearJumperAIController::OnMeleeAttackFinished(AActor* Target)
{
	// Прыгнул — отскочил и снова выжидает.
	bLeapCommitted = false;
	NextLeapTime = GetNow() + LeapRest;
	WaitStartTime = NextLeapTime;
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

	// Плевун: кислотный мешок на горле ×2, в замахе плевка ×3 и светится; голова ×1.5.
	KindName = TEXT("Spitter");
	FNoFearWeakZone SacOpen;
	SacOpen.Name = TEXT("SacOpen");
	SacOpen.Bones = { TEXT("neck_01") };
	SacOpen.DamageMultiplier = 3.f;
	SacOpen.Condition = ENoFearWeakCondition::Windup;
	SacOpen.AttackName = TEXT("Spit");
	SacOpen.bGlow = true;
	SacOpen.GlowColor = FLinearColor(0.3f, 1.f, 0.1f);
	SacOpen.GlowRadius = 11.f;
	FNoFearWeakZone Sac;
	Sac.Name = TEXT("Sac");
	Sac.Bones = { TEXT("neck_01") };
	Sac.DamageMultiplier = 2.f;
	// Порядок важен: открытый мешок проверяется раньше обычного.
	WeakZones.Insert(Sac, 0);
	WeakZones.Insert(SacOpen, 0);

	TargetWeights.Hero = 0.6f;
	TargetWeights.Companion = 0.6f;
	TargetWeights.Soldier = 0.8f;
	TargetWeights.Civilian = 0.5f;
	OpportunityBonuses.Reset();
	OpportunityBonuses.Add(ENoFearOpportunity::KnockedDown, 1.f);
	OpportunityBonuses.Add(ENoFearOpportunity::Pinned, 0.6f);
	OpportunityBonuses.Add(ENoFearOpportunity::Marked, 1.5f);
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

bool ANoFearSpitterAIController::GetStandoffPoint(const FVector& TargetLocation, const FVector& PackSide, FVector& OutPoint) const
{
	const APawn* MyPawn = GetPawn();
	if (!MyPawn)
	{
		return false;
	}
	// За кольцом со стороны стаи; несколько плевунов расходятся веером (±25°), каждый в свою сторону.
	const float Spread = (static_cast<int32>(MyPawn->GetUniqueID() % 3) - 1) * 25.f;
	OutPoint = TargetLocation + PackSide.RotateAngleAxis(Spread, FVector::UpVector) * StandDistance;
	return true;
}

bool ANoFearSpitterAIController::WantsFreeAttack(AActor* Target)
{
	// Только что плюнул и стоит не на своём месте — сначала перебраться, потом плевать снова.
	FVector Goal;
	const APawn* MyPawn = GetPawn();
	if (MyPawn && GetNow() - LastAttackTime < RelocateTime && GetPackGoal(Goal)
		&& FVector::DistSquared2D(MyPawn->GetActorLocation(), Goal) > FMath::Square(StandTolerance))
	{
		return false;
	}
	return true;
}

void ANoFearSpitterAIController::OnMeleeAttackFinished(AActor* Target)
{
	LastAttackTime = GetNow();
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

	// Ярость ускоряет время тела (см. Tick).
	PrimaryActorTick.bCanEverTick = true;

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

	Smash->bBreaksCover = true;

	UNoFearMonsterAttack_Charge* Charge = CreateDefaultSubobject<UNoFearMonsterAttack_Charge>(TEXT("Charge"));
	AttackSet.Add(Charge);

	UNoFearMonsterAttack_Grab* Grab = CreateDefaultSubobject<UNoFearMonsterAttack_Grab>(TEXT("Grab"));
	AttackSet.Add(Grab);

	// Рёв: колосс стоит 1 с и ревёт не чаще раза в 18 с.
	RoarDuration = 1.f;
	RoarCooldown = RoarInterval;
	RoarShake = UNoFearTankRoarShake::StaticClass();
	StepShake = UNoFearTankStepShake::StaticClass();

	// Колосс: броня спереди, бока, спина, нарост и сорванные пластины (раздел 4.5). Любит солдат и тех, кто в кислоте.
	KindName = TEXT("Tank");
	WeakZones.Reset();

	FNoFearWeakZone Growth;
	Growth.Name = TEXT("Growth");
	Growth.Bones = { TEXT("spine_03") };
	Growth.Side = ENoFearWeakSide::Back;
	Growth.DamageMultiplier = 2.5f;
	Growth.bGlow = true;
	Growth.GlowColor = FLinearColor(1.f, 0.3f, 0.05f);
	Growth.GlowRadius = 14.f;
	WeakZones.Add(Growth);

	// Сорванные пластины: свой ×0.8 поверх брони (зоны по стороне на это попадание не действуют).
	auto AddPlateZone = [this](FName ZoneName, FName Bone)
	{
		FNoFearWeakZone Plate;
		Plate.Name = ZoneName;
		Plate.Bones = { Bone };
		Plate.Side = ENoFearWeakSide::Front;
		Plate.DamageMultiplier = 0.8f;
		Plate.Condition = ENoFearWeakCondition::Manual;
		Plate.bIgnoresSideZones = true;
		Plate.bGlow = true;
		Plate.GlowColor = FLinearColor(1.f, 0.85f, 0.1f);
		Plate.GlowRadius = 16.f;
		WeakZones.Add(Plate);
	};
	AddPlateZone(TEXT("PlateChest"), TEXT("spine_03"));
	AddPlateZone(TEXT("PlateShoulderL"), TEXT("clavicle_l"));
	AddPlateZone(TEXT("PlateShoulderR"), TEXT("clavicle_r"));

	// Все пластины сорваны: весь перед пробивается ×0.6.
	FNoFearWeakZone FrontBare;
	FrontBare.Name = TEXT("FrontBare");
	FrontBare.Side = ENoFearWeakSide::Front;
	FrontBare.DamageMultiplier = 0.6f;
	FrontBare.Condition = ENoFearWeakCondition::Manual;
	WeakZones.Add(FrontBare);

	// Передняя броня ×0, пока работает: не работает после тарана в стену и без пластин.
	FNoFearWeakZone FrontArmor;
	FrontArmor.Name = TEXT("FrontArmor");
	FrontArmor.Side = ENoFearWeakSide::Front;
	FrontArmor.DamageMultiplier = 0.f;
	FrontArmor.Condition = ENoFearWeakCondition::ArmorBroken;
	FrontArmor.bInvert = true;
	WeakZones.Add(FrontArmor);

	FNoFearWeakZone Flank;
	Flank.Name = TEXT("Flank");
	Flank.Side = ENoFearWeakSide::Flank;
	Flank.DamageMultiplier = 0.75f;
	WeakZones.Add(Flank);

	TargetWeights.Hero = 0.8f;
	TargetWeights.Companion = 0.6f;
	TargetWeights.Soldier = 1.f;
	TargetWeights.Civilian = 0.2f;
	bFinishKnockedDown = false;
	OpportunityBonuses.Reset();
	OpportunityBonuses.Add(ENoFearOpportunity::InAcid, 0.5f);
	OpportunityBonuses.Add(ENoFearOpportunity::Marked, 1.5f);
}

// --- Шептун ---

namespace NoFearWhisperer
{
	float GetHumanFear(const AActor* Human)
	{
		const UNoFearFearComponent* Fear = UNoFearFearComponent::FindFear(Human);
		return Fear ? Fear->GetFear() : 0.f;
	}

	UNoFearBraceletComponent* FindBracelet(const APawn* Human)
	{
		const APlayerController* PlayerController = Human ? Cast<APlayerController>(Human->GetController()) : nullptr;
		return PlayerController ? PlayerController->FindComponentByClass<UNoFearBraceletComponent>() : nullptr;
	}

	/** Страх человеку от шептуна: герою — через браслет (источник «Шёпот»), остальным — напрямую. */
	void AddWhisperFear(APawn* Human, float Amount, float Cap, const FVector& Location, AActor* Source, bool bDirect)
	{
		if (Amount <= 0.f)
		{
			return;
		}
		if (UNoFearBraceletComponent* Bracelet = FindBracelet(Human))
		{
			Bracelet->ReceiveWhisper(Amount, Cap, Location, Source);
		}
		else if (UNoFearFearComponent* Fear = UNoFearFearComponent::FindFear(Human))
		{
			Fear->AddFearFrom(Amount, Cap, Location, Source, bDirect);
		}
	}

	bool IsArmed(const ANoFearCharacter* Human)
	{
		const UNoFearWeaponComponent* Weapons = Human ? Human->GetWeaponComponent() : nullptr;
		return Weapons && Weapons->GetCurrentWeapon();
	}

	bool ProjectToNav(const UWorld* World, const FVector& Point, FVector& OutPoint)
	{
		const UNavigationSystemV1* Nav = World ? FNavigationSystem::GetCurrent<UNavigationSystemV1>(World) : nullptr;
		FNavLocation Projected;
		if (Nav && Nav->ProjectPointToNavigation(Point, Projected, FVector(250.f, 250.f, 300.f)))
		{
			OutPoint = Projected.Location;
			return true;
		}
		return false;
	}
}

ANoFearWhispererAIController::ANoFearWhispererAIController(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// Сам не дерётся: жетон не берёт, бьёт только вплотную.
	bUseAttackTokens = false;
	bFinishKnockedDown = false;

	UNoFearMonsterAttack_Swing* Swipe = CreateDefaultSubobject<UNoFearMonsterAttack_Swing>(TEXT("Swipe"));
	Swipe->AttackName = TEXT("Swipe");
	Swipe->MaxRange = 180.f;
	Swipe->Damage = 5.f;
	Swipe->Cooldown = 1.5f;
	AttackSet.Add(Swipe);

	// Рык — это замах крика (см. Roar); BT стоит на месте, пока IsRoaring.
	RoarDuration = ScreamWindup;
	RoarCooldown = ScreamInterval;

	WhisperSound = TSoftObjectPtr<USoundBase>(FSoftObjectPath(TEXT("/Game/Audio/Ambient/Starter_Wind05.Starter_Wind05")));

	// Шептун: голова на замахе крика ×3 и светится; голова ×1.5 (от загонщика).
	KindName = TEXT("Whisperer");
	FNoFearWeakZone ScreamHead;
	ScreamHead.Name = TEXT("ScreamHead");
	ScreamHead.Bones = { TEXT("head") };
	ScreamHead.DamageMultiplier = 3.f;
	ScreamHead.Condition = ENoFearWeakCondition::Scream;
	ScreamHead.bGlow = true;
	ScreamHead.GlowColor = FLinearColor(0.6f, 0.1f, 1.f);
	ScreamHead.GlowRadius = 14.f;
	WeakZones.Insert(ScreamHead, 0);

	TargetWeights.Hero = 0.6f;
	TargetWeights.Companion = 0.6f;
	TargetWeights.Soldier = 0.6f;
	TargetWeights.Civilian = 1.f;
	OpportunityBonuses.Reset();
	OpportunityBonuses.Add(ENoFearOpportunity::Marked, 1.5f);
}

void ANoFearWhispererAIController::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);

	bScreamWindup = false;
	bChanneling = false;
	MarkVictim.Reset();
	ChannelVictim.Reset();
	VisionVictims.Reset();
	LastUpdateTime = -1.0;
	const double Now = GetNow();
	// Первая метка и видение — не сразу после появления.
	NextMarkTime = Now + MarkInterval * 0.5f;
	NextVisionTime = Now + FMath::FRandRange(VisionInterval.X, VisionInterval.Y);
	NextScreamTime = 0.0;
	NextGuardTime = 0.0;
	NextPhaseStepTime = 0.0;

	if (!InPawn)
	{
		return;
	}
	if (UNoFearHealthComponent* Health = UNoFearHealthComponent::FindHealth(InPawn))
	{
		Health->OnDamaged.AddUniqueDynamic(this, &ANoFearWhispererAIController::HandleDamaged);
		Health->OnDeath.AddUniqueDynamic(this, &ANoFearWhispererAIController::HandleDeath);
		BoundHealth = Health;
	}
	// Шёпот: зациклен, слышно на полтора радиуса ауры, громче ближе.
	if (USoundBase* Sound = WhisperSound.LoadSynchronous())
	{
		UAudioComponent* Audio = NewObject<UAudioComponent>(InPawn, TEXT("WhisperAudio"));
		Audio->SetSound(Sound);
		Audio->bAutoDestroy = false;
		Audio->bOverrideAttenuation = true;
		Audio->AttenuationOverrides.bAttenuate = true;
		Audio->AttenuationOverrides.AttenuationShape = EAttenuationShape::Sphere;
		Audio->AttenuationOverrides.AttenuationShapeExtents = FVector(200.f);
		Audio->AttenuationOverrides.FalloffDistance = AuraRadius * 1.5f;
		Audio->SetPitchMultiplier(0.55f);
		Audio->SetupAttachment(InPawn->GetRootComponent());
		Audio->RegisterComponent();
		Audio->OnAudioFinished.AddUniqueDynamic(this, &ANoFearWhispererAIController::HandleWhisperFinished);
		Audio->Play();
		WhisperAudio = Audio;
	}
}

void ANoFearWhispererAIController::OnUnPossess()
{
	ClearEffects();
	if (UNoFearHealthComponent* Health = BoundHealth.Get())
	{
		Health->OnDamaged.RemoveDynamic(this, &ANoFearWhispererAIController::HandleDamaged);
		Health->OnDeath.RemoveDynamic(this, &ANoFearWhispererAIController::HandleDeath);
	}
	BoundHealth.Reset();
	Super::OnUnPossess();
}

void ANoFearWhispererAIController::HandleWhisperFinished()
{
	UAudioComponent* Audio = WhisperAudio.Get();
	if (Audio && GetPawn() && !UNoFearHealthComponent::IsActorDead(GetPawn()))
	{
		Audio->Play();
	}
}

AActor* ANoFearWhispererAIController::GetMarkTarget() const
{
	AActor* Victim = MarkVictim.Get();
	return Victim && GetNow() < MarkUntil && !UNoFearHealthComponent::IsActorDead(Victim) ? Victim : nullptr;
}

bool ANoFearWhispererAIController::WantsFreeAttack(AActor* Target)
{
	// Бьёт только вплотную и не во время крика и канала.
	return !bScreamWindup && !bChanneling && GetTargetDistance2D() <= 220.f;
}

void ANoFearWhispererAIController::UpdateBot()
{
	Super::UpdateBot();

	APawn* MyPawn = GetPawn();
	if (!MyPawn || UNoFearHealthComponent::IsActorDead(MyPawn))
	{
		return;
	}
	const double Now = GetNow();
	const float DeltaTime = LastUpdateTime < 0.0 ? 0.f : static_cast<float>(FMath::Min(Now - LastUpdateTime, 0.5));
	LastUpdateTime = Now;

	UpdateAura(DeltaTime);
	UpdateBuff();
	UpdateVisions();
	UpdateMark(DeltaTime);
	UpdateGuardTrigger();
	UpdatePosition();

	// Крик раз в ScreamInterval, если цель близко и шептун не занят.
	const AActor* Target = CurrentTarget.Get();
	const UNoFearMeleeComponent* Melee = GetMeleeComponent();
	if (Target && !bScreamWindup && !bChanneling && Now >= NextScreamTime && !IsRoaring() && !IsScattering()
		&& !(Melee && Melee->IsAttacking()) && !UNoFearPoseComponent::IsActorStaggered(MyPawn)
		&& FVector::Dist(MyPawn->GetActorLocation(), Target->GetActorLocation()) <= ScreamTargetRange)
	{
		Roar();
	}
}

void ANoFearWhispererAIController::UpdateAura(float DeltaTime)
{
	APawn* MyPawn = GetPawn();
	UWorld* World = GetWorld();
	if (!MyPawn || !World || DeltaTime <= 0.f)
	{
		return;
	}
	const FVector Location = MyPawn->GetActorLocation();
	for (ANoFearCharacter* Human : TActorRange<ANoFearCharacter>(World))
	{
		if (!Human || Human == MyPawn || UNoFearHealthComponent::IsActorDead(Human) || UNoFearTeamLibrary::AreFriendly(MyPawn, Human)
			|| Cast<ANoFearMonsterAIController>(Human->GetController()))
		{
			continue;
		}
		// Сквозь стены: только расстояние.
		if (FVector::Dist(Human->GetActorLocation(), Location) <= AuraRadius)
		{
			NoFearWhisperer::AddWhisperFear(Human, AuraFearPerSecond * DeltaTime, AuraFearCap, Location, MyPawn, false);
		}
	}

	// Метка: страх помеченного не спадает и растёт.
	if (APawn* Victim = Cast<APawn>(GetMarkTarget()))
	{
		const float Fear = NoFearWhisperer::GetHumanFear(Victim);
		MarkFearFloor = FMath::Min(100.f, FMath::Max(MarkFearFloor, Fear) + MarkFearPerSecond * DeltaTime);
		NoFearWhisperer::AddWhisperFear(Victim, MarkFearFloor - Fear, 100.f, Location, MyPawn, false);
	}
}

void ANoFearWhispererAIController::UpdateBuff()
{
	APawn* MyPawn = GetPawn();
	UWorld* World = GetWorld();
	if (!MyPawn || !World)
	{
		return;
	}
	BuffedCount = 0;
	for (ANoFearCharacter* Candidate : TActorRange<ANoFearCharacter>(World))
	{
		ANoFearMonsterAIController* Ally = Candidate && Candidate != MyPawn ? Cast<ANoFearMonsterAIController>(Candidate->GetController()) : nullptr;
		if (Ally && !Ally->IsA<ANoFearWhispererAIController>() && !UNoFearHealthComponent::IsActorDead(Candidate)
			&& UNoFearTeamLibrary::AreFriendly(MyPawn, Candidate)
			&& FVector::Dist(Candidate->GetActorLocation(), MyPawn->GetActorLocation()) <= BuffRadius)
		{
			// Держится, пока шептун рядом и жив; обновляется каждый пересчёт.
			Ally->ApplyWhisperBuff(0.6f);
			++BuffedCount;
		}
	}
}

bool ANoFearWhispererAIController::SendVision(APawn* Victim)
{
	APawn* MyPawn = GetPawn();
	ANoFearCharacter* Human = Cast<ANoFearCharacter>(Victim);
	if (!MyPawn || !Human || UNoFearHealthComponent::IsActorIncapacitated(Human))
	{
		return false;
	}
	const FVector Origin = Human->GetActorLocation();
	const FVector Direction = FVector::ForwardVector.RotateAngleAxis(FMath::FRandRange(0.f, 360.f), FVector::UpVector);
	const FVector Point = Origin + Direction * FMath::FRandRange(VisionDistance.X, VisionDistance.Y) + FVector(0.f, 0.f, 40.f);

	// Мирный не стреляет — бежит от ложного монстра.
	if (ANoFearCivilianAIController* Civilian = Cast<ANoFearCivilianAIController>(Human->GetController()))
	{
		Civilian->Panic(Point, nullptr, true);
	}
	else
	{
		UNoFearCombatComponent* Combat = Human->FindComponentByClass<UNoFearCombatComponent>();
		if (!Combat || !NoFearWhisperer::IsArmed(Human))
		{
			return false;
		}
		const bool bWasSeeing = Combat->IsSeeingPhantom();
		const int32 CountBefore = Combat->GetPhantomCount();
		Combat->SeePhantom(Point, FMath::FRandRange(VisionDuration.X, VisionDuration.Y));
		if (Combat->GetPhantomCount() == CountBefore && !bWasSeeing)
		{
			return false;
		}
		VisionVictims.AddUnique(Human);
	}
	++VisionCount;
	return true;
}

void ANoFearWhispererAIController::UpdateVisions()
{
	APawn* MyPawn = GetPawn();
	UWorld* World = GetWorld();
	const double Now = GetNow();
	VisionVictims.RemoveAll([](const TWeakObjectPtr<APawn>& Victim) { return !Victim.IsValid(); });
	if (!MyPawn || !World || Now < NextVisionTime)
	{
		return;
	}
	// Один бот из подходящих: в ауре, страх от VisionMinFear, не герой.
	TArray<APawn*> Candidates;
	for (ANoFearCharacter* Human : TActorRange<ANoFearCharacter>(World))
	{
		if (!Human || Human == MyPawn || Human->IsPlayerControlled() || UNoFearHealthComponent::IsActorIncapacitated(Human)
			|| UNoFearTeamLibrary::AreFriendly(MyPawn, Human) || Cast<ANoFearMonsterAIController>(Human->GetController())
			|| FVector::Dist(Human->GetActorLocation(), MyPawn->GetActorLocation()) > AuraRadius
			|| NoFearWhisperer::GetHumanFear(Human) < VisionMinFear)
		{
			continue;
		}
		const UNoFearCombatComponent* Combat = Human->FindComponentByClass<UNoFearCombatComponent>();
		if (Combat && Combat->IsSeeingPhantom())
		{
			continue;
		}
		Candidates.Add(Human);
	}
	if (Candidates.IsEmpty())
	{
		return;
	}
	if (SendVision(Candidates[FMath::RandRange(0, Candidates.Num() - 1)]))
	{
		NextVisionTime = Now + FMath::FRandRange(VisionInterval.X, VisionInterval.Y);
	}
}

bool ANoFearWhispererAIController::CanSee(const AActor* Other) const
{
	const APawn* MyPawn = GetPawn();
	const UWorld* World = GetWorld();
	if (!MyPawn || !Other || !World)
	{
		return false;
	}
	FCollisionQueryParams Params(SCENE_QUERY_STAT(WhispererSight), false, MyPawn);
	Params.AddIgnoredActor(Other);
	const FVector Start = MyPawn->GetPawnViewLocation();
	const APawn* OtherPawn = Cast<APawn>(Other);
	const FVector End = OtherPawn ? OtherPawn->GetPawnViewLocation() : Other->GetActorLocation();
	FHitResult Hit;
	return !World->LineTraceSingleByChannel(Hit, Start, End, ECC_Visibility, Params);
}

bool ANoFearWhispererAIController::StartMark(AActor* Victim)
{
	APawn* MyPawn = GetPawn();
	if (!MyPawn || !Victim || UNoFearHealthComponent::IsActorDead(MyPawn) || UNoFearHealthComponent::IsActorDead(Victim) || bScreamWindup)
	{
		return false;
	}
	bChanneling = true;
	ChannelVictim = Victim;
	ChannelEndTime = GetNow() + MarkChannelTime;
	ChannelDamage = 0.f;
	StopMovement();
	SetFocus(Victim, EAIFocusPriority::Gameplay);
	return true;
}

void ANoFearWhispererAIController::CancelMark()
{
	if (!bChanneling)
	{
		return;
	}
	bChanneling = false;
	ChannelVictim.Reset();
	ClearFocus(EAIFocusPriority::Gameplay);
	++MarkInterruptCount;
	// Сорванный канал повторяет не сразу.
	NextMarkTime = GetNow() + MarkInterval * 0.5f;
}

void ANoFearWhispererAIController::UpdateMark(float DeltaTime)
{
	APawn* MyPawn = GetPawn();
	UWorld* World = GetWorld();
	const double Now = GetNow();
	if (!MyPawn || !World)
	{
		return;
	}
	// Метка кончилась.
	if (MarkVictim.IsValid() && !GetMarkTarget())
	{
		UNoFearOpportunitySubsystem::RemoveTag(MarkVictim.Get(), ENoFearOpportunity::Marked);
		MarkVictim.Reset();
	}

	if (bChanneling)
	{
		AActor* Victim = ChannelVictim.Get();
		// Потерял из виду или жертва погибла — канал сорван.
		if (!Victim || UNoFearHealthComponent::IsActorDead(Victim) || !CanSee(Victim))
		{
			CancelMark();
			return;
		}
		StopMovement();
		SetShadeGoal(MyPawn->GetActorLocation(), 0.6f);
		const APawn* VictimPawn = Cast<APawn>(Victim);
		DrawDebugLine(World, MyPawn->GetPawnViewLocation(), VictimPawn ? VictimPawn->GetPawnViewLocation() : Victim->GetActorLocation(),
			FColor(170, 40, 255), false, 0.3f, 0, 3.f);
		if (Now < ChannelEndTime)
		{
			return;
		}
		// Канал закончен: метка.
		bChanneling = false;
		ChannelVictim.Reset();
		ClearFocus(EAIFocusPriority::Gameplay);
		if (AActor* OldVictim = MarkVictim.Get(); OldVictim && OldVictim != Victim)
		{
			UNoFearOpportunitySubsystem::RemoveTag(OldVictim, ENoFearOpportunity::Marked);
		}
		MarkVictim = Victim;
		MarkUntil = Now + MarkDuration;
		MarkFearFloor = NoFearWhisperer::GetHumanFear(Victim);
		UNoFearOpportunitySubsystem::AddTag(Victim, ENoFearOpportunity::Marked, MarkDuration);
		++MarkCount;
		NextMarkTime = Now + MarkInterval;
		return;
	}

	if (Now < NextMarkTime || bScreamWindup || IsRoaring() || UNoFearPoseComponent::IsActorStaggered(MyPawn))
	{
		return;
	}
	// Самый напуганный человек в MarkRange, которого видно.
	AActor* Best = nullptr;
	float BestFear = -1.f;
	for (ANoFearCharacter* Human : TActorRange<ANoFearCharacter>(World))
	{
		if (!Human || Human == MyPawn || UNoFearHealthComponent::IsActorDead(Human) || UNoFearTeamLibrary::AreFriendly(MyPawn, Human)
			|| Cast<ANoFearMonsterAIController>(Human->GetController())
			|| FVector::Dist(Human->GetActorLocation(), MyPawn->GetActorLocation()) > MarkRange)
		{
			continue;
		}
		const float Fear = NoFearWhisperer::GetHumanFear(Human);
		if (Fear > BestFear && CanSee(Human))
		{
			BestFear = Fear;
			Best = Human;
		}
	}
	if (Best)
	{
		StartMark(Best);
	}
	else
	{
		NextMarkTime = Now + 1.f;
	}
}

// --- Крик ---

void ANoFearWhispererAIController::Roar()
{
	APawn* MyPawn = GetPawn();
	if (!MyPawn || UNoFearHealthComponent::IsActorDead(MyPawn) || bScreamWindup)
	{
		return;
	}
	if (bChanneling)
	{
		return;
	}
	RoarDuration = ScreamWindup;
	RoarCooldown = ScreamInterval;
	Super::Roar();
	bScreamWindup = true;
	ScreamDamage = 0.f;
	NextScreamTime = GetNow() + ScreamInterval;
	if (UNoFearVulnerabilityComponent* Vulnerability = MyPawn->FindComponentByClass<UNoFearVulnerabilityComponent>())
	{
		Vulnerability->OpenWindow(ENoFearWeakCondition::Scream, ScreamWindup);
	}
	GetWorldTimerManager().SetTimer(ScreamTimer, this, &ANoFearWhispererAIController::FinishScream, ScreamWindup, false);
}

void ANoFearWhispererAIController::FinishScream()
{
	APawn* MyPawn = GetPawn();
	UWorld* World = GetWorld();
	if (!bScreamWindup || !MyPawn || !World || UNoFearHealthComponent::IsActorDead(MyPawn))
	{
		bScreamWindup = false;
		return;
	}
	bScreamWindup = false;
	if (UNoFearVulnerabilityComponent* Vulnerability = MyPawn->FindComponentByClass<UNoFearVulnerabilityComponent>())
	{
		Vulnerability->CloseWindow(ENoFearWeakCondition::Scream);
	}
	++ScreamCount;
	const FVector Location = MyPawn->GetActorLocation();
	for (ANoFearCharacter* Human : TActorRange<ANoFearCharacter>(World))
	{
		if (!Human || Human == MyPawn || UNoFearHealthComponent::IsActorDead(Human) || UNoFearTeamLibrary::AreFriendly(MyPawn, Human)
			|| Cast<ANoFearMonsterAIController>(Human->GetController())
			|| FVector::Dist(Human->GetActorLocation(), Location) > ScreamRadius)
		{
			continue;
		}
		NoFearWhisperer::AddWhisperFear(Human, ScreamFear, ScreamFearCap, Location, MyPawn, true);
	}
	// Мирные в радиусе срываются в бег.
	UNoFearBotSubsystem::ReportDanger(this, ENoFearDangerType::Fight, MyPawn, Location, ScreamRadius, Location);
	UNoFearBotSubsystem::ReportNoise(this, MyPawn, Location, ScreamRadius * 1.5f, ENoFearNoiseType::Other);
	DrawDebugSphere(World, Location, ScreamRadius, 32, FColor(170, 40, 255), false, 0.5f);
}

void ANoFearWhispererAIController::CancelScream()
{
	if (!bScreamWindup)
	{
		return;
	}
	bScreamWindup = false;
	GetWorldTimerManager().ClearTimer(ScreamTimer);
	if (APawn* MyPawn = GetPawn())
	{
		if (UNoFearVulnerabilityComponent* Vulnerability = MyPawn->FindComponentByClass<UNoFearVulnerabilityComponent>())
		{
			Vulnerability->CloseWindow(ENoFearWeakCondition::Scream);
		}
	}
}

void ANoFearWhispererAIController::Stun(const FVector& From)
{
	const ANoFearCharacter* MyCharacter = Cast<ANoFearCharacter>(GetPawn());
	UNoFearPoseComponent* Pose = MyCharacter ? MyCharacter->GetPoseComponent() : nullptr;
	if (!Pose)
	{
		return;
	}
	Pose->Stagger((MyCharacter->GetActorLocation() - From).GetSafeNormal2D(), ScreamStunTime);
	Pose->OnHitReaction.Broadcast(Pose, true);
}

// --- Урон и охрана ---

void ANoFearWhispererAIController::HandleDamaged(UNoFearHealthComponent* Component, const FNoFearDamageInfo& Info)
{
	APawn* MyPawn = GetPawn();
	if (!MyPawn || !Component || Component->IsDead())
	{
		return;
	}
	AActor* Offender = Info.InstigatorController ? Info.InstigatorController->GetPawn() : Info.DamageCauser.Get();
	const FVector From = Offender ? Offender->GetActorLocation() : MyPawn->GetActorLocation() - MyPawn->GetActorForwardVector() * 100.f;

	if (bScreamWindup)
	{
		ScreamDamage += Info.Amount;
		if (ScreamDamage >= ScreamInterruptDamage)
		{
			CancelScream();
			++ScreamInterruptCount;
			// Крик сорван: оглушён, рык закончится вместе с ним.
			Stun(From);
		}
	}
	if (bChanneling)
	{
		ChannelDamage += Info.Amount;
		if (ChannelDamage >= MarkInterruptFraction * Component->GetMaxHealth())
		{
			CancelMark();
		}
	}
	if (Offender && !UNoFearTeamLibrary::AreFriendly(MyPawn, Offender))
	{
		RaiseGuard(Offender);
	}
}

void ANoFearWhispererAIController::UpdateGuardTrigger()
{
	float Distance = 0.f;
	if (APawn* Intruder = FindNearestHuman(false, Distance); Intruder && Distance <= GuardTriggerRadius)
	{
		RaiseGuard(Intruder);
	}
}

void ANoFearWhispererAIController::RaiseGuard(AActor* Offender)
{
	APawn* MyPawn = GetPawn();
	UWorld* World = GetWorld();
	const double Now = GetNow();
	if (!MyPawn || !World || !Offender || UNoFearHealthComponent::IsActorDead(MyPawn) || UNoFearHealthComponent::IsActorDead(Offender))
	{
		return;
	}
	// Сам уходит фазовым шагом (своя перезарядка).
	if (Now >= NextPhaseStepTime && !bScreamWindup)
	{
		PhaseStep(Offender->GetActorLocation());
	}
	if (Now < NextGuardTime)
	{
		return;
	}
	NextGuardTime = Now + GuardCooldown;
	++GuardCount;

	// Два ближайших загонщика или ловчих идут на обидчика; колосс закрывает линию огня.
	const FVector MyLocation = MyPawn->GetActorLocation();
	TArray<TPair<float, ANoFearMonsterAIController*>> Defenders;
	for (ANoFearCharacter* Candidate : TActorRange<ANoFearCharacter>(World))
	{
		ANoFearMonsterAIController* Ally = Candidate && Candidate != MyPawn ? Cast<ANoFearMonsterAIController>(Candidate->GetController()) : nullptr;
		if (!Ally || UNoFearHealthComponent::IsActorDead(Candidate) || !UNoFearTeamLibrary::AreFriendly(MyPawn, Candidate))
		{
			continue;
		}
		const float Distance = FVector::Dist(Candidate->GetActorLocation(), MyLocation);
		if (ANoFearTankAIController* Tank = Cast<ANoFearTankAIController>(Ally))
		{
			if (Distance <= TankCoverRange)
			{
				const FVector ToOffender = (Offender->GetActorLocation() - MyLocation).GetSafeNormal2D();
				Tank->SetShadeGoal(MyLocation + ToOffender * 300.f, 3.f);
			}
			continue;
		}
		if (Ally->GetClass() == ANoFearMonsterAIController::StaticClass() || Ally->IsA<ANoFearJumperAIController>())
		{
			Defenders.Emplace(Distance, Ally);
		}
	}
	Defenders.Sort([](const TPair<float, ANoFearMonsterAIController*>& A, const TPair<float, ANoFearMonsterAIController*>& B) { return A.Key < B.Key; });
	for (int32 Index = 0; Index < FMath::Min(GuardDefenders, Defenders.Num()); ++Index)
	{
		Defenders[Index].Value->Defend(Offender, GuardTime);
	}
}

bool ANoFearWhispererAIController::PhaseStep(FVector From)
{
	APawn* MyPawn = GetPawn();
	UWorld* World = GetWorld();
	if (!MyPawn || !World || UNoFearHealthComponent::IsActorDead(MyPawn))
	{
		return false;
	}
	const FVector Location = MyPawn->GetActorLocation();
	FVector Away = (Location - From).GetSafeNormal2D();
	if (Away.IsNearlyZero())
	{
		Away = -MyPawn->GetActorForwardVector().GetSafeNormal2D();
	}
	// Назад от угрозы, если там нет навигации — в стороны.
	FVector Destination = Location;
	bool bFound = false;
	for (const float Angle : { 0.f, 35.f, -35.f, 70.f, -70.f })
	{
		const FVector Candidate = Location + Away.RotateAngleAxis(Angle, FVector::UpVector) * PhaseStepDistance;
		if (NoFearWhisperer::ProjectToNav(World, Candidate, Destination))
		{
			bFound = true;
			break;
		}
	}
	if (!bFound)
	{
		return false;
	}
	const float HalfHeight = MyPawn->GetDefaultHalfHeight();
	Destination.Z += HalfHeight;
	if (!MyPawn->TeleportTo(Destination, MyPawn->GetActorRotation()))
	{
		return false;
	}
	StopMovement();
	NextPhaseStepTime = GetNow() + PhaseStepCooldown;
	++PhaseStepCount;
	// Мерцание: пропадает на миг.
	MyPawn->SetActorHiddenInGame(true);
	TWeakObjectPtr<APawn> WeakPawn = MyPawn;
	FTimerHandle FlickerTimer;
	GetWorldTimerManager().SetTimer(FlickerTimer, FTimerDelegate::CreateLambda([WeakPawn]()
	{
		if (APawn* Pawn = WeakPawn.Get())
		{
			Pawn->SetActorHiddenInGame(false);
		}
	}), 0.2f, false);
	DrawDebugLine(World, Location, Destination, FColor(170, 40, 255), false, 0.5f, 0, 2.f);
	return true;
}

// --- Место ---

APawn* ANoFearWhispererAIController::FindNearestHuman(bool bArmed, float& OutDistance) const
{
	const APawn* MyPawn = GetPawn();
	UWorld* World = GetWorld();
	OutDistance = UE_BIG_NUMBER;
	APawn* Best = nullptr;
	if (!MyPawn || !World)
	{
		return nullptr;
	}
	for (ANoFearCharacter* Human : TActorRange<ANoFearCharacter>(World))
	{
		if (!Human || Human == MyPawn || UNoFearHealthComponent::IsActorIncapacitated(Human) || UNoFearTeamLibrary::AreFriendly(MyPawn, Human)
			|| Cast<ANoFearMonsterAIController>(Human->GetController()) || (bArmed && !NoFearWhisperer::IsArmed(Human)))
		{
			continue;
		}
		const float Distance = FVector::Dist2D(Human->GetActorLocation(), MyPawn->GetActorLocation());
		if (Distance < OutDistance)
		{
			OutDistance = Distance;
			Best = Human;
		}
	}
	return Best;
}

void ANoFearWhispererAIController::UpdatePosition()
{
	APawn* MyPawn = GetPawn();
	UWorld* World = GetWorld();
	if (!MyPawn || !World || bChanneling || IsScattering())
	{
		return;
	}
	const FVector MyLocation = MyPawn->GetActorLocation();
	float ShooterDistance = 0.f;
	const APawn* Shooter = FindNearestHuman(true, ShooterDistance);
	const AActor* Target = CurrentTarget.Get();
	if (!Shooter && !Target)
	{
		return;
	}

	FVector Goal = MyLocation;
	bool bHasGoal = false;
	// В тени колосса: позади него от ближайшего стрелка.
	if (Shooter)
	{
		const ANoFearCharacter* BestTank = nullptr;
		float BestTankDistance = TankShadeRange;
		for (ANoFearCharacter* Candidate : TActorRange<ANoFearCharacter>(World))
		{
			if (Candidate && Cast<ANoFearTankAIController>(Candidate->GetController()) && !UNoFearHealthComponent::IsActorDead(Candidate)
				&& UNoFearTeamLibrary::AreFriendly(MyPawn, Candidate))
			{
				const float Distance = FVector::Dist2D(Candidate->GetActorLocation(), MyLocation);
				if (Distance <= BestTankDistance)
				{
					BestTankDistance = Distance;
					BestTank = Candidate;
				}
			}
		}
		if (BestTank)
		{
			const FVector Away = (BestTank->GetActorLocation() - Shooter->GetActorLocation()).GetSafeNormal2D();
			Goal = BestTank->GetActorLocation() + Away * TankShadeDistance;
			bHasGoal = true;
		}
		else if (ShooterDistance < KeepMinDistance || ShooterDistance > KeepMaxDistance)
		{
			// Коридор KeepMin–KeepMax от стрелка: со своей стороны, к середине коридора.
			const FVector Away = (MyLocation - Shooter->GetActorLocation()).GetSafeNormal2D();
			Goal = Shooter->GetActorLocation() + Away * (0.5f * (KeepMinDistance + KeepMaxDistance));
			bHasGoal = true;
		}
		else
		{
			// В коридоре — стоит.
			Goal = MyLocation;
			bHasGoal = true;
		}
	}
	else if (Target)
	{
		// Только безоружные: подходит на край ауры.
		const FVector Away = (MyLocation - Target->GetActorLocation()).GetSafeNormal2D();
		Goal = Target->GetActorLocation() + Away * (AuraRadius * 0.65f);
		bHasGoal = true;
	}
	if (!bHasGoal)
	{
		return;
	}
	FVector Projected;
	if (NoFearWhisperer::ProjectToNav(World, Goal, Projected))
	{
		Goal = Projected;
	}
	SetShadeGoal(Goal, 0.6f);
}

// --- Смерть ---

void ANoFearWhispererAIController::HandleDeath(UNoFearHealthComponent* Component, const FNoFearDamageInfo& Info)
{
	APawn* MyPawn = GetPawn();
	UWorld* World = GetWorld();
	ClearEffects();
	if (!MyPawn || !World)
	{
		return;
	}
	// Колоссы рядом впадают в ярость.
	for (ANoFearCharacter* Candidate : TActorRange<ANoFearCharacter>(World))
	{
		ANoFearTankAIController* Tank = Candidate ? Cast<ANoFearTankAIController>(Candidate->GetController()) : nullptr;
		if (Tank && !UNoFearHealthComponent::IsActorDead(Candidate) && UNoFearTeamLibrary::AreFriendly(MyPawn, Candidate)
			&& FVector::Dist(Candidate->GetActorLocation(), MyPawn->GetActorLocation()) <= DeathRageRange)
		{
			Tank->StartRage(DeathRageTime);
		}
	}
}

void ANoFearWhispererAIController::ClearEffects()
{
	CancelScream();
	if (bChanneling)
	{
		bChanneling = false;
		ChannelVictim.Reset();
		ClearFocus(EAIFocusPriority::Gameplay);
	}
	if (AActor* Victim = MarkVictim.Get())
	{
		UNoFearOpportunitySubsystem::RemoveTag(Victim, ENoFearOpportunity::Marked);
	}
	MarkVictim.Reset();
	MarkUntil = 0.0;
	for (const TWeakObjectPtr<APawn>& Victim : VisionVictims)
	{
		if (UNoFearCombatComponent* Combat = Victim.IsValid() ? Victim->FindComponentByClass<UNoFearCombatComponent>() : nullptr)
		{
			Combat->ClearPhantom();
		}
	}
	VisionVictims.Reset();
	if (UAudioComponent* Audio = WhisperAudio.Get())
	{
		Audio->OnAudioFinished.RemoveDynamic(this, &ANoFearWhispererAIController::HandleWhisperFinished);
		Audio->Stop();
		Audio->DestroyComponent();
	}
	WhisperAudio.Reset();
	// Бафф держится 0.6 с после последнего пересчёта и спадёт сам.
}
