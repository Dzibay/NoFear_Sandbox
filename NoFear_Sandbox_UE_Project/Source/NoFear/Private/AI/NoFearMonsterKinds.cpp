#include "AI/NoFearMonsterKinds.h"

#include "AI/NoFearMeleeComponent.h"
#include "AI/NoFearMonsterAttack.h"
#include "AI/NoFearOpportunitySubsystem.h"
#include "AI/NoFearBotSubsystem.h"
#include "AI/NoFearTankShake.h"
#include "AI/NoFearVulnerabilityComponent.h"
#include "Animation/NoFearPoseComponent.h"
#include "Camera/CameraShakeBase.h"
#include "Core/NoFearBraceletComponent.h"
#include "Core/NoFearFearComponent.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "Core/NoFearCharacter.h"
#include "Core/NoFearHealthComponent.h"
#include "Core/NoFearTeam.h"
#include "Engine/World.h"
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

void ANoFearTankAIController::StartRage()
{
	if (bRaging)
	{
		return;
	}
	bRaging = true;
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
