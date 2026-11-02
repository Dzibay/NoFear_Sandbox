#include "AI/NoFearMonsterKinds.h"

#include "AI/NoFearAcidPuddle.h"
#include "AI/NoFearMeleeComponent.h"
#include "AI/NoFearSpitProjectile.h"
#include "AI/NoFearMonsterAttack.h"
#include "AI/NoFearOpportunitySubsystem.h"
#include "AI/NoFearBotSubsystem.h"
#include "AI/NoFearCivilianAIController.h"
#include "AI/NoFearCombatComponent.h"
#include "AI/NoFearTankShake.h"
#include "AI/NoFearVulnerabilityComponent.h"
#include "AI/NoFearSquadSubsystem.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Animation/NoFearPoseComponent.h"
#include "BrainComponent.h"
#include "Camera/CameraShakeBase.h"
#include "Components/AudioComponent.h"
#include "Components/CapsuleComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Navigation/PathFollowingComponent.h"
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
#include "Weapons/NoFearWeaponData.h"
#include "Portal/NoFearPortal.h"
#include "Portal/NoFearPortalSubsystem.h"

// --- Прыгун ---

TArray<TWeakObjectPtr<ANoFearJumperAIController>> ANoFearJumperAIController::ActivePinners;

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

	// С насеста — прыжок сверху. Сам не выбирается: запускает контроллер по имени.
	UNoFearMonsterAttack_Pounce* Pounce = CreateDefaultSubobject<UNoFearMonsterAttack_Pounce>(TEXT("Pounce"));
	AttackSet.Add(Pounce);

	// Ловчий: голова ×1.5 (от загонщика), в полёте спина ×1.5; любит облитых слизью, отставших и сбитых.
	FNoFearWeakZone Flight;
	Flight.Name = TEXT("Flight");
	Flight.Bones = { TEXT("spine_01"), TEXT("spine_02"), TEXT("spine_03") };
	Flight.DamageMultiplier = 1.5f;
	Flight.Condition = ENoFearWeakCondition::Flight;
	WeakZones.Insert(Flight, 0);
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

void ANoFearJumperAIController::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);

	PerchState = EPerchState::None;
	bPouncing = false;
	bKnockedFromAir = false;
	bLogicPausedByCatcher = false;
	NextPerchSearchTime = GetNow() + 1.0;
	if (UNoFearHealthComponent* Health = InPawn ? UNoFearHealthComponent::FindHealth(InPawn) : nullptr)
	{
		Health->OnDamaged.AddUniqueDynamic(this, &ANoFearJumperAIController::HandleDamaged);
		BoundHealth = Health;
	}
}

void ANoFearJumperAIController::OnUnPossess()
{
	// Дерево снимет с паузы база; здесь только отпустить жертву.
	bLogicPausedByCatcher = false;
	ReleasePin();
	PerchState = EPerchState::None;
	bPouncing = false;
	GetWorldTimerManager().ClearTimer(CatcherTimer);
	if (UNoFearMeleeComponent* Melee = GetMeleeComponent())
	{
		Melee->OnAttackHit.RemoveDynamic(this, &ANoFearJumperAIController::HandleAttackHit);
	}
	if (UNoFearHealthComponent* Health = BoundHealth.Get())
	{
		Health->OnDamaged.RemoveDynamic(this, &ANoFearJumperAIController::HandleDamaged);
	}
	BoundHealth.Reset();
	Super::OnUnPossess();
}

void ANoFearJumperAIController::ConfigureMelee(UNoFearMeleeComponent* Melee)
{
	Super::ConfigureMelee(Melee);
	if (Melee)
	{
		Melee->OnAttackHit.AddUniqueDynamic(this, &ANoFearJumperAIController::HandleAttackHit);
	}
}

void ANoFearJumperAIController::UpdateBot()
{
	Super::UpdateBot();

	const bool bBusy = IsPinning() || PerchState != EPerchState::None || bPouncing;
	// Уклоняется, только если держат на мушке DodgeAimTime подряд: ствол, скользнувший мимо, не в счёт.
	FVector AimDirection;
	const double Now = GetNow();
	if (!bBusy && bDodgeWhenAimedAt && FindAimingThreat(AimDirection))
	{
		AimedSince = AimedSince < 0.0 ? Now : AimedSince;
		if (Now >= NextDodgeTime && Now - AimedSince >= DodgeAimTime)
		{
			Dodge(AimDirection);
		}
	}
	else
	{
		AimedSince = -1.0;
	}

	// Цель далеко, прыжок сверху готов — поискать уступ над ней.
	if (bUsePerches && !bBusy && CurrentTarget.IsValid() && GetNow() >= NextPerchSearchTime && IsPounceReady())
	{
		const float Distance = GetTargetDistance2D();
		if (Distance > PerchMinTargetDistance && Distance <= PerchSearchRadius + 500.f)
		{
			NextPerchSearchTime = GetNow() + 2.0;
			TryPerch();
		}
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
	// Сначала в сторону своего бока подкрадывания (не мечется туда-сюда), без бока — наугад.
	// Бок считается от взгляда цели (прицела) так же, как в GetStandoffPoint.
	const float PreferredSide = StalkSide != 0.f ? StalkSide : (FMath::RandBool() ? 1.f : -1.f);
	if (FVector::CrossProduct(AimDirection.GetSafeNormal2D(), Side).Z * PreferredSide < 0.f)
	{
		Side = -Side;
	}
	// Капсулой на всю высоту: низкий ящик сбоку тоже помеха. Обе стороны заняты — не прыгает.
	const UCapsuleComponent* Capsule = MyCharacter->GetCapsuleComponent();
	const FCollisionShape Shape = FCollisionShape::MakeCapsule(Capsule->GetScaledCapsuleRadius(), Capsule->GetScaledCapsuleHalfHeight() - 5.f);
	const FVector Start = MyCharacter->GetActorLocation() + FVector(0.f, 0.f, 10.f);
	FCollisionQueryParams Params(SCENE_QUERY_STAT(NoFearDodge), false, MyCharacter);
	const auto IsBlocked = [&](const FVector& Dir)
	{
		return GetWorld()->SweepTestByChannel(Start, Start + Dir * 350.f, FQuat::Identity, ECC_Pawn, Shape, Params);
	};
	if (IsBlocked(Side))
	{
		Side = -Side;
		if (IsBlocked(Side))
		{
			NextDodgeTime = GetNow() + 0.5;
			return false;
		}
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
	if (IsFacingAttack())
	{
		return;
	}
	// К цели — спринтом: в свою очередь или без стаи. На место в строю и в кольце бежит вместе со всеми.
	// К подножию насеста — тоже спринтом.
	FVector PackGoal;
	if (PerchState == EPerchState::Approach
		|| (CurrentTarget.IsValid() && !IsCircling() && !IsScattering() && (HasAttackToken() || !GetPackGoal(PackGoal))))
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
	// Бок держит StalkSideHoldTime и меняет, только уйдя за линию взгляда с запасом StalkSideMargin: иначе у
	// цели, смотрящей прямо на него, бок прыгает каждый кадр и ловчий бегает из стороны в сторону.
	const float Cross = FVector::CrossProduct(Facing, ToMe).Z;
	const float Desired = Cross >= 0.f ? 1.f : -1.f;
	const double Now = GetNow();
	if (StalkSide == 0.f
		|| (Desired != StalkSide && FMath::Abs(Cross) >= FMath::Sin(FMath::DegreesToRadians(StalkSideMargin)) && Now - StalkSideTime >= StalkSideHoldTime))
	{
		StalkSide = Desired;
		StalkSideTime = Now;
	}
	OutPoint = TargetLocation + Facing.RotateAngleAxis(StalkSide * StalkAngle, FVector::UpVector) * StalkDistance;
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
	// Цель только что спряталась за укрытие — прыгнуть к ней сразу, пока не устроилась.
	const FNoFearKnownActor* Knowledge = KnownActors.Find(Target);
	const UNoFearMeleeComponent* Melee = GetMeleeComponent();
	const UNoFearMonsterAttack* Leap = Melee ? Melee->FindAttack(TEXT("Leap")) : nullptr;
	const bool bTargetHidden = Knowledge && !Knowledge->bVisible && Now - Knowledge->LastSeenTime <= 3.0
		&& Leap && GetTargetDistance2D() <= Leap->MaxRange + 150.f;
	if (IsTargetDistracted(Target))
	{
		++OpportunityLeaps;
		bLeapCommitted = true;
	}
	else if (bTargetHidden)
	{
		++OpportunityLeaps;
		++CoverLeaps;
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
	bKnockedFromAir = false;
	if (bPouncing)
	{
		// Прыгнул сверху: прижал — держит, промахнулся — снова думает сам.
		bPouncing = false;
		if (!IsPinning())
		{
			ResumeBrain();
		}
	}
}

// --- Ловчий: насест ---

bool ANoFearJumperAIController::IsPounceReady() const
{
	const UNoFearMeleeComponent* Melee = GetMeleeComponent();
	const UNoFearMonsterAttack* Pounce = Melee ? Melee->FindAttack(TEXT("Pounce")) : nullptr;
	return Pounce && GetWorld() && Pounce->NextUseTime <= GetWorld()->GetTimeSeconds();
}

void ANoFearJumperAIController::PauseBrain()
{
	UBrainComponent* Brain = GetBrainComponent();
	if (Brain && !Brain->IsPaused())
	{
		Brain->PauseLogic(TEXT("Catcher"));
		bLogicPausedByCatcher = true;
	}
}

void ANoFearJumperAIController::ResumeBrain()
{
	if (!bLogicPausedByCatcher)
	{
		return;
	}
	bLogicPausedByCatcher = false;
	APawn* MyPawn = GetPawn();
	UBrainComponent* Brain = GetBrainComponent();
	if (Brain && Brain->IsPaused() && !IsPortalControlled() && !UNoFearHealthComponent::IsActorIncapacitated(MyPawn))
	{
		Brain->ResumeLogic(TEXT("Catcher over"));
		// Пошатнулся, пока дерево стояло на паузе ловчего, — дальше паузу держит пошатывание.
		if (UNoFearPoseComponent::IsActorStaggered(MyPawn))
		{
			BeginStagger();
		}
	}
	UpdateMovementStance();
}

void ANoFearJumperAIController::StartCatcherTimer()
{
	if (!GetWorldTimerManager().IsTimerActive(CatcherTimer))
	{
		GetWorldTimerManager().SetTimer(CatcherTimer, this, &ANoFearJumperAIController::TickCatcher, 0.05f, true);
	}
}

void ANoFearJumperAIController::Stun(const FVector& From, float Time)
{
	const ANoFearCharacter* MyCharacter = Cast<ANoFearCharacter>(GetPawn());
	UNoFearPoseComponent* Pose = MyCharacter ? MyCharacter->GetPoseComponent() : nullptr;
	if (!Pose || Time <= 0.f)
	{
		return;
	}
	Pose->Stagger((MyCharacter->GetActorLocation() - From).GetSafeNormal2D(), Time);
	Pose->OnHitReaction.Broadcast(Pose, true);
}

bool ANoFearJumperAIController::TryPerch()
{
	ACharacter* MyCharacter = Cast<ACharacter>(GetPawn());
	const AActor* Target = CurrentTarget.Get();
	const UNoFearMeleeComponent* Melee = GetMeleeComponent();
	const UBrainComponent* Brain = GetBrainComponent();
	const UCharacterMovementComponent* Movement = MyCharacter ? MyCharacter->GetCharacterMovement() : nullptr;
	if (!Movement || !Target || Movement->IsFalling() || IsPinning() || bPouncing || PerchState != EPerchState::None
		|| (Brain && Brain->IsPaused()) || IsPortalControlled() || (Melee && Melee->IsAttacking())
		|| UNoFearPoseComponent::IsActorStaggered(MyCharacter) || UNoFearHealthComponent::IsActorDead(MyCharacter))
	{
		return false;
	}
	FVector Top;
	FVector Foot;
	if (!FindPerch(Target, Top, Foot))
	{
		NextPerchSearchTime = GetNow() + 4.0;
		return false;
	}
	PerchTop = Top;
	PerchFoot = Foot;
	PerchState = EPerchState::Approach;
	PerchStateTime = GetNow();
	NoSightSince = -1.0;
	PauseBrain();
	StopMovement();
	MoveToLocation(PerchFoot, 40.f, false, true, true, false);
	UpdateMovementStance();
	StartCatcherTimer();
	return true;
}

bool ANoFearJumperAIController::FindPerch(const AActor* Target, FVector& OutTop, FVector& OutFoot) const
{
	const ACharacter* MyCharacter = Cast<ACharacter>(GetPawn());
	const UWorld* World = GetWorld();
	if (!MyCharacter || !Target || !World)
	{
		return false;
	}
	const float MyRadius = MyCharacter->GetCapsuleComponent()->GetScaledCapsuleRadius();
	const float MyHalf = MyCharacter->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	const ACharacter* TargetCharacter = Cast<ACharacter>(Target);
	const float TargetHalf = TargetCharacter ? TargetCharacter->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() : 90.f;
	const FVector TargetLocation = Target->GetActorLocation();
	const FVector TargetFeet = TargetLocation - FVector(0.f, 0.f, TargetHalf);
	const FVector MyLocation = MyCharacter->GetActorLocation();

	FCollisionQueryParams Params(SCENE_QUERY_STAT(NoFearPerch), false, MyCharacter);
	Params.AddIgnoredActor(Target);
	const FCollisionObjectQueryParams Statics(ECC_WorldStatic);
	// Земля у подножия бывает и подвижной геометрией (пол из мешей).
	FCollisionObjectQueryParams GroundObjects(ECC_WorldStatic);
	GroundObjects.AddObjectTypesToQuery(ECC_WorldDynamic);
	const UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);

	float BestDistance = UE_BIG_NUMBER;
	bool bFound = false;
	// Кольца вокруг цели: сверху вниз трассой ищем ровный верх на нужной высоте.
	for (float Ring = 400.f; Ring <= PerchSearchRadius; Ring += 300.f)
	{
		for (int32 Step = 0; Step < 16; ++Step)
		{
			const FVector Dir = FVector::ForwardVector.RotateAngleAxis(Step * 22.5f, FVector::UpVector);
			const FVector Column = TargetFeet + Dir * Ring;
			FHitResult Hit;
			const FVector TraceStart(Column.X, Column.Y, TargetFeet.Z + PerchMaxHeight + 150.f);
			const FVector TraceEnd(Column.X, Column.Y, TargetFeet.Z + PerchMinHeight - 20.f);
			if (!World->LineTraceSingleByObjectType(Hit, TraceStart, TraceEnd, Statics, Params) || Hit.bStartPenetrating
				|| Hit.ImpactNormal.Z < 0.85f)
			{
				continue;
			}
			const FVector Top = Hit.ImpactPoint;
			const float Height = Top.Z - TargetFeet.Z;
			if (Height < PerchMinHeight || Height > PerchMaxHeight)
			{
				continue;
			}
			// Недавно не забрался сюда — не пробует снова.
			const double Now = GetNow();
			if (FailedPerches.ContainsByPredicate([&](const TPair<FVector, double>& Failed)
				{
					return Now - Failed.Value < PerchRetryTime && FVector::DistSquared(Failed.Key, Top) < FMath::Square(250.f);
				}))
			{
				continue;
			}
			// Встанет наверху и увидит оттуда цель.
			const FVector Stand = Top + FVector(0.f, 0.f, MyHalf + 5.f);
			if (World->OverlapBlockingTestByChannel(Stand, FQuat::Identity, ECC_Pawn, FCollisionShape::MakeCapsule(MyRadius, MyHalf), Params)
				|| World->LineTraceTestByChannel(Stand + FVector(0.f, 0.f, MyHalf * 0.5f), TargetLocation, ECC_Visibility, Params))
			{
				continue;
			}
			// Подножие: от верха к ловчему, пока земля не уйдёт вниз на 150+ — там стена.
			const FVector ToMe = (MyLocation - Top).GetSafeNormal2D();
			if (ToMe.IsNearlyZero())
			{
				continue;
			}
			bool bHasFoot = false;
			FVector Foot = FVector::ZeroVector;
			for (float Along = 50.f; Along <= 700.f; Along += 50.f)
			{
				const FVector Probe = Top + ToMe * Along;
				FHitResult Ground;
				if (!World->LineTraceSingleByObjectType(Ground, Probe + FVector(0.f, 0.f, 50.f), Probe - FVector(0.f, 0.f, PerchMaxHeight + 400.f), GroundObjects, Params))
				{
					break;
				}
				if (Top.Z - Ground.ImpactPoint.Z >= 150.f)
				{
					Foot = Ground.ImpactPoint + ToMe * (MyRadius + 10.f);
					bHasFoot = true;
					break;
				}
			}
			if (!bHasFoot)
			{
				continue;
			}
			if (NavSys)
			{
				FNavLocation NavFoot;
				if (!NavSys->ProjectPointToNavigation(Foot, NavFoot, FVector(150.f, 150.f, 250.f)))
				{
					continue;
				}
				Foot = NavFoot.Location;
			}
			const float Distance = FVector::Dist2D(MyLocation, Foot);
			if (Distance < BestDistance)
			{
				BestDistance = Distance;
				OutTop = Top;
				OutFoot = Foot;
				bFound = true;
			}
		}
	}
	return bFound;
}

void ANoFearJumperAIController::EndPerch(bool bFailed)
{
	if (bFailed)
	{
		const double Now = GetNow();
		FailedPerches.RemoveAll([&](const TPair<FVector, double>& Failed) { return Now - Failed.Value >= PerchRetryTime; });
		FailedPerches.Emplace(PerchTop, Now);
	}
	PerchState = EPerchState::None;
	StopMovement();
	NextPerchSearchTime = GetNow() + (bFailed ? 4.0 : 2.0);
	ResumeBrain();
}

void ANoFearJumperAIController::TickCatcher()
{
	APawn* MyPawn = GetPawn();
	const double Now = GetNow();
	if (!MyPawn || UNoFearHealthComponent::IsActorDead(MyPawn))
	{
		bLogicPausedByCatcher = false;
		ReleasePin();
		PerchState = EPerchState::None;
		bPouncing = false;
		GetWorldTimerManager().ClearTimer(CatcherTimer);
		return;
	}
	// Пошатнулся (сбили, оглушили) — бросает всё, дальше ведёт база.
	if (UNoFearPoseComponent::IsActorStaggered(MyPawn) && (IsPinning() || PerchState != EPerchState::None))
	{
		ReleasePin();
		PerchState = EPerchState::None;
		ResumeBrain();
	}

	if (IsPinning())
	{
		HoldPin(Now);
	}
	switch (PerchState)
	{
	case EPerchState::Approach:
		TickApproach(Now);
		break;
	case EPerchState::Climb:
		TickClimb(Now);
		break;
	case EPerchState::Perched:
		TickPerched(Now);
		break;
	default:
		break;
	}
	if (!IsPinning() && PerchState == EPerchState::None && !bPouncing)
	{
		GetWorldTimerManager().ClearTimer(CatcherTimer);
	}
}

void ANoFearJumperAIController::TickApproach(double Now)
{
	const APawn* MyPawn = GetPawn();
	if (!CurrentTarget.IsValid() || Now - PerchStateTime > 6.0)
	{
		EndPerch(true);
		return;
	}
	const float Distance = FVector::Dist2D(MyPawn->GetActorLocation(), PerchFoot);
	const bool bIdle = GetMoveStatus() == EPathFollowingStatus::Idle;
	if (Distance <= 90.f || (bIdle && Distance <= 200.f))
	{
		StopMovement();
		PerchState = EPerchState::Climb;
		PerchStateTime = Now;
		NextHopTime = Now;
		bOverHopped = false;
		return;
	}
	if (bIdle)
	{
		MoveToLocation(PerchFoot, 40.f, false, true, true, false);
	}
}

void ANoFearJumperAIController::TickClimb(double Now)
{
	ACharacter* MyCharacter = Cast<ACharacter>(GetPawn());
	UCharacterMovementComponent* Movement = MyCharacter ? MyCharacter->GetCharacterMovement() : nullptr;
	if (!Movement || !CurrentTarget.IsValid() || Now - PerchStateTime > 5.0)
	{
		EndPerch(true);
		return;
	}
	const FVector MyLocation = MyCharacter->GetActorLocation();
	const float Remaining = PerchTop.Z - (MyLocation.Z - MyCharacter->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
	if (Movement->IsMovingOnGround() && Remaining <= 30.f)
	{
		// Наверху.
		Movement->StopMovementImmediately();
		PerchState = EPerchState::Perched;
		PerchStateTime = Now;
		NoSightSince = -1.0;
		++PerchCount;
		return;
	}
	// Подпрыгивает с земли или на вершине прошлого прыжка; ноги выше края — один толчок вперёд, дальше ждёт приземления.
	if (Now < NextHopTime || (Movement->IsFalling() && (Movement->Velocity.Z > 0.f || (Remaining <= 30.f && bOverHopped))))
	{
		return;
	}
	const FVector ToTop = (PerchTop - MyLocation).GetSafeNormal2D();
	if (Remaining > 30.f)
	{
		const float Gravity = FMath::Max(-Movement->GetGravityZ(), 1.f);
		const float Hop = FMath::Min(ClimbHopHeight, Remaining + 40.f);
		MyCharacter->LaunchCharacter(ToTop * 120.f + FVector::UpVector * FMath::Sqrt(2.f * Gravity * Hop), true, true);
		NextHopTime = Now + 0.2;
		bOverHopped = false;
	}
	else
	{
		// Ноги выше края — перескочить на верх, не перелетев уступ.
		const float Forward = FMath::Clamp(FVector::Dist2D(PerchTop, MyLocation) * 2.5f, 150.f, 450.f);
		MyCharacter->LaunchCharacter(ToTop * Forward + FVector::UpVector * 250.f, true, true);
		NextHopTime = Now + 0.5;
		bOverHopped = true;
	}
	if (const ANoFearCharacter* NoFearCharacter = Cast<ANoFearCharacter>(MyCharacter))
	{
		if (UNoFearPoseComponent* Pose = NoFearCharacter->GetPoseComponent())
		{
			Pose->PlayPose(ENoFearPose::Lunge, 0.25f, 0.5f);
		}
	}
}

void ANoFearJumperAIController::TickPerched(double Now)
{
	const ACharacter* MyCharacter = Cast<ACharacter>(GetPawn());
	AActor* Target = CurrentTarget.Get();
	if (!MyCharacter || !Target || UNoFearHealthComponent::IsActorIncapacitated(Target))
	{
		EndPerch(false);
		return;
	}
	// Сбросили с уступа.
	const float Feet = MyCharacter->GetActorLocation().Z - MyCharacter->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	if (Feet < PerchTop.Z - 80.f)
	{
		EndPerch(false);
		return;
	}
	SetFocus(Target);
	FCollisionQueryParams Params(SCENE_QUERY_STAT(NoFearPerchSight), false, MyCharacter);
	Params.AddIgnoredActor(Target);
	const bool bSees = !GetWorld()->LineTraceTestByChannel(MyCharacter->GetPawnViewLocation(), Target->GetActorLocation(), ECC_Visibility, Params);
	const float Distance = FVector::Dist(MyCharacter->GetActorLocation(), Target->GetActorLocation());
	if (bSees)
	{
		NoSightSince = -1.0;
	}
	else if (NoSightSince < 0.0)
	{
		NoSightSince = Now;
	}
	// Цель ушла далеко или скрылась надолго — слезает.
	if (Distance > PerchSearchRadius + 300.f || (NoSightSince >= 0.0 && Now - NoSightSince > 3.0))
	{
		EndPerch(false);
		return;
	}
	const UNoFearMeleeComponent* Melee = GetMeleeComponent();
	const UNoFearMonsterAttack* Pounce = Melee ? Melee->FindAttack(TEXT("Pounce")) : nullptr;
	if (!bSees || !Pounce || !IsPounceReady() || Distance > Pounce->MaxRange)
	{
		return;
	}
	if (IsTargetDistracted(Target) || UNoFearOpportunitySubsystem::HasTag(Target, ENoFearOpportunity::Isolated)
		|| Now - PerchStateTime >= PerchPatience)
	{
		StartPounce();
	}
}

bool ANoFearJumperAIController::StartPounce()
{
	UNoFearMeleeComponent* Melee = GetMeleeComponent();
	AActor* Target = CurrentTarget.Get();
	if (!Melee || !Target || IsPinning() || !Melee->StartAttackByName(Target, TEXT("Pounce"), true))
	{
		return false;
	}
	PerchState = EPerchState::None;
	bPouncing = true;
	bKnockedFromAir = false;
	++PounceCount;
	StopMovement();
	PauseBrain();
	StartCatcherTimer();
	return true;
}

// --- Ловчий: прижатие ---

ANoFearJumperAIController* ANoFearJumperAIController::FindPinner(const AActor* Victim)
{
	if (!Victim)
	{
		return nullptr;
	}
	for (const TWeakObjectPtr<ANoFearJumperAIController>& Weak : ActivePinners)
	{
		ANoFearJumperAIController* Pinner = Weak.Get();
		if (Pinner && Pinner->PinVictim.Get() == Victim)
		{
			return Pinner;
		}
	}
	return nullptr;
}

void ANoFearJumperAIController::GetActivePinners(const UWorld* World, TArray<ANoFearJumperAIController*>& OutPinners)
{
	OutPinners.Reset();
	for (const TWeakObjectPtr<ANoFearJumperAIController>& Weak : ActivePinners)
	{
		ANoFearJumperAIController* Pinner = Weak.Get();
		if (Pinner && Pinner->GetWorld() == World && Pinner->IsPinning())
		{
			OutPinners.Add(Pinner);
		}
	}
}

void ANoFearJumperAIController::HandleAttackHit(AActor* Victim, float Damage)
{
	const UNoFearMeleeComponent* Melee = GetMeleeComponent();
	const UNoFearMonsterAttack* Attack = Melee ? Melee->GetCurrentAttack() : nullptr;
	ACharacter* VictimCharacter = Cast<ACharacter>(Victim);
	if (!Attack || Attack->AttackName != TEXT("Pounce") || bKnockedFromAir || IsPinning() || !VictimCharacter
		|| !UNoFearHealthComponent::FindHealth(VictimCharacter) || UNoFearHealthComponent::IsActorIncapacitated(VictimCharacter)
		|| FindPinner(VictimCharacter))
	{
		return;
	}
	BeginPin(VictimCharacter);
}

void ANoFearJumperAIController::BeginPin(ACharacter* Victim)
{
	ACharacter* MyCharacter = Cast<ACharacter>(GetPawn());
	if (!MyCharacter || !Victim)
	{
		return;
	}
	const double Now = GetNow();
	PinVictim = Victim;
	++PinCount;
	PinDamageTaken = 0.f;
	EscapePresses.Reset();
	NextPinDamageTime = Now + PinDamageInterval;
	PinSide = (MyCharacter->GetActorLocation() - Victim->GetActorLocation()).GetSafeNormal2D();
	if (PinSide.IsNearlyZero())
	{
		PinSide = -Victim->GetActorForwardVector().GetSafeNormal2D();
	}
	ActivePinners.RemoveAll([](const TWeakObjectPtr<ANoFearJumperAIController>& Weak) { return !Weak.IsValid(); });
	ActivePinners.AddUnique(this);

	UNoFearOpportunitySubsystem::AddTag(Victim, ENoFearOpportunity::Pinned, 0.f);
	UNoFearOpportunitySubsystem::AddTag(Victim, ENoFearOpportunity::KnockedDown, 1.2f);

	// Жертва на земле: присела (капсула ниже), не ходит и не стреляет.
	if (UCharacterMovementComponent* VictimMovement = Victim->GetCharacterMovement())
	{
		bCrouchedByPin = !Victim->bIsCrouched;
		if (bCrouchedByPin)
		{
			Victim->Crouch();
			VictimMovement->Crouch(false);
		}
		VictimMovement->StopMovementImmediately();
		VictimMovement->DisableMovement();
	}
	if (AAIController* VictimAI = Cast<AAIController>(Victim->GetController()))
	{
		VictimAI->StopMovement();
	}
	if (const ANoFearCharacter* VictimNoFear = Cast<ANoFearCharacter>(Victim))
	{
		if (UNoFearWeaponComponent* Weapons = VictimNoFear->GetWeaponComponent())
		{
			bVictimFireWasBlocked = Weapons->IsFireBlocked();
			Weapons->StopFire();
			Weapons->SetFireBlocked(true);
		}
	}
	// Мирный под ловчим — в ужасе.
	if (Cast<ANoFearCivilianAIController>(Victim->GetController()))
	{
		if (UNoFearFearComponent* Fear = UNoFearFearComponent::FindFear(Victim))
		{
			Fear->AddFearFrom(100.f, 100.f, MyCharacter->GetActorLocation(), MyCharacter, true);
		}
	}
	UNoFearBotSubsystem::ReportDanger(this, ENoFearDangerType::Fight, MyCharacter, Victim->GetActorLocation(),
		UNoFearBotSubsystem::FightNoticeRadius, MyCharacter->GetActorLocation());

	if (UCharacterMovementComponent* Movement = MyCharacter->GetCharacterMovement())
	{
		Movement->StopMovementImmediately();
		Movement->DisableMovement();
	}
	StopMovement();
	PauseBrain();
	StartCatcherTimer();
	HoldPin(Now);
}

void ANoFearJumperAIController::HoldPin(double Now)
{
	ACharacter* Victim = PinVictim.Get();
	ACharacter* MyCharacter = Cast<ACharacter>(GetPawn());
	if (!Victim || !MyCharacter || UNoFearHealthComponent::IsActorIncapacitated(Victim) || UNoFearHealthComponent::IsActorDead(MyCharacter))
	{
		ReleasePin();
		ResumeBrain();
		return;
	}
	if (UCharacterMovementComponent* VictimMovement = Victim->GetCharacterMovement(); VictimMovement && VictimMovement->MovementMode != MOVE_None)
	{
		VictimMovement->StopMovementImmediately();
		VictimMovement->DisableMovement();
	}
	if (const ANoFearCharacter* VictimNoFear = Cast<ANoFearCharacter>(Victim))
	{
		if (UNoFearWeaponComponent* Weapons = VictimNoFear->GetWeaponComponent(); Weapons && !Weapons->IsFireBlocked())
		{
			Weapons->StopFire();
			Weapons->SetFireBlocked(true);
		}
	}

	// Сидит на жертве лицом к ней.
	const float VictimHalf = Victim->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	const float MyHalf = MyCharacter->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	const FVector Seat = Victim->GetActorLocation() + PinSide * 35.f + FVector::UpVector * (VictimHalf + MyHalf * 0.5f);
	MyCharacter->SetActorLocationAndRotation(Seat, (-PinSide).Rotation(), false, nullptr, ETeleportType::TeleportPhysics);
	if (UCharacterMovementComponent* Movement = MyCharacter->GetCharacterMovement(); Movement && Movement->MovementMode != MOVE_None)
	{
		Movement->StopMovementImmediately();
		Movement->DisableMovement();
	}
	SetFocus(Victim);

	if (Now >= NextPinDamageTime)
	{
		NextPinDamageTime = Now + PinDamageInterval;
		UGameplayStatics::ApplyDamage(Victim, PinDamage, this, MyCharacter, UDamageType::StaticClass());
		if (const ANoFearCharacter* NoFearCharacter = Cast<ANoFearCharacter>(MyCharacter))
		{
			if (UNoFearPoseComponent* Pose = NoFearCharacter->GetPoseComponent())
			{
				Pose->PlayPose(ENoFearPose::Swing, 0.3f, 0.8f);
			}
		}
	}
}

void ANoFearJumperAIController::ReleasePin()
{
	ACharacter* Victim = PinVictim.Get();
	PinVictim.Reset();
	ActivePinners.RemoveAll([this](const TWeakObjectPtr<ANoFearJumperAIController>& Weak) { return !Weak.IsValid() || Weak.Get() == this; });
	EscapePresses.Reset();
	if (Victim)
	{
		UNoFearOpportunitySubsystem::RemoveTag(Victim, ENoFearOpportunity::Pinned);
		if (!UNoFearHealthComponent::IsActorDead(Victim))
		{
			if (UCharacterMovementComponent* VictimMovement = Victim->GetCharacterMovement(); VictimMovement && VictimMovement->MovementMode == MOVE_None)
			{
				VictimMovement->SetMovementMode(MOVE_Walking);
			}
			if (bCrouchedByPin && !UNoFearHealthComponent::IsActorIncapacitated(Victim))
			{
				Victim->UnCrouch();
			}
			const ANoFearCharacter* VictimNoFear = Cast<ANoFearCharacter>(Victim);
			UNoFearWeaponComponent* Weapons = VictimNoFear ? VictimNoFear->GetWeaponComponent() : nullptr;
			const UNoFearBraceletComponent* Bracelet = UNoFearBraceletComponent::FindBracelet(Victim);
			if (Weapons && !bVictimFireWasBlocked && !(Bracelet && Bracelet->IsInStupor()))
			{
				Weapons->SetFireBlocked(false);
			}
		}
	}
	bCrouchedByPin = false;
	ACharacter* MyCharacter = Cast<ACharacter>(GetPawn());
	UCharacterMovementComponent* Movement = MyCharacter ? MyCharacter->GetCharacterMovement() : nullptr;
	if (Movement && Movement->MovementMode == MOVE_None && !UNoFearHealthComponent::IsActorDead(MyCharacter))
	{
		Movement->SetMovementMode(MOVE_Falling);
	}
}

int32 ANoFearJumperAIController::GetEscapeRequired() const
{
	const AActor* Victim = PinVictim.Get();
	float Fear = 0.f;
	if (const UNoFearBraceletComponent* Bracelet = Victim ? UNoFearBraceletComponent::FindBracelet(Victim) : nullptr)
	{
		Fear = Bracelet->GetFear();
	}
	else if (const UNoFearFearComponent* FearComponent = Victim ? UNoFearFearComponent::FindFear(Victim) : nullptr)
	{
		Fear = FearComponent->GetFear();
	}
	return Fear >= PinAfraidFear ? PinEscapePressesAfraid : PinEscapePresses;
}

float ANoFearJumperAIController::GetEscapeProgress() const
{
	if (!IsPinning())
	{
		return 0.f;
	}
	const double Now = GetNow();
	int32 Count = 0;
	for (const double Time : EscapePresses)
	{
		Count += Now - Time <= PinEscapeWindow ? 1 : 0;
	}
	return FMath::Clamp(static_cast<float>(Count) / FMath::Max(GetEscapeRequired(), 1), 0.f, 1.f);
}

void ANoFearJumperAIController::AddEscapePress()
{
	const ACharacter* Victim = PinVictim.Get();
	if (!Victim)
	{
		return;
	}
	const double Now = GetNow();
	EscapePresses.Add(Now);
	EscapePresses.RemoveAll([this, Now](double Time) { return Now - Time > PinEscapeWindow; });
	if (EscapePresses.Num() < GetEscapeRequired())
	{
		return;
	}
	// Вырвался: сбросил ловчего с себя.
	++EscapeCount;
	const FVector From = Victim->GetActorLocation();
	ReleasePin();
	ResumeBrain();
	if (ACharacter* MyCharacter = Cast<ACharacter>(GetPawn()))
	{
		MyCharacter->LaunchCharacter(PinSide * 450.f + FVector::UpVector * 300.f, true, true);
	}
	Stun(From, 1.f);
}

void ANoFearJumperAIController::ThrowOff(AActor* From)
{
	ACharacter* MyCharacter = Cast<ACharacter>(GetPawn());
	const ACharacter* Victim = PinVictim.Get();
	if (!MyCharacter || !Victim)
	{
		return;
	}
	const FVector FromLocation = From && From != Victim ? From->GetActorLocation() : Victim->GetActorLocation();
	FVector Away = (MyCharacter->GetActorLocation() - FromLocation).GetSafeNormal2D();
	if (Away.IsNearlyZero())
	{
		Away = PinSide;
	}
	++KnockOffCount;
	ReleasePin();
	ResumeBrain();
	MyCharacter->LaunchCharacter(Away * 600.f + FVector::UpVector * 350.f, true, true);
	Stun(FromLocation, KnockOffStunTime);
}

void ANoFearJumperAIController::HandleDamaged(UNoFearHealthComponent* Component, const FNoFearDamageInfo& Info)
{
	ACharacter* MyCharacter = Cast<ACharacter>(GetPawn());
	if (!MyCharacter || !Component || Component->IsDead())
	{
		return;
	}
	AActor* Offender = Info.InstigatorController ? Info.InstigatorController->GetPawn() : Info.DamageCauser.Get();
	const FVector From = Offender ? Offender->GetActorLocation()
		: (Info.bHasSource ? Info.SourceLocation : MyCharacter->GetActorLocation() - MyCharacter->GetActorForwardVector() * 100.f);

	// Сидит на жертве: снимает накопленный урон, дробовик или удар вплотную.
	if (IsPinning())
	{
		PinDamageTaken += Info.Amount;
		const ANoFearWeapon* Weapon = Cast<ANoFearWeapon>(Info.DamageCauser.Get());
		const bool bShotgun = Weapon && Weapon->GetWeaponData() && Weapon->GetWeaponData()->WeaponType == ENoFearWeaponType::Shotgun;
		const bool bMelee = Offender && !Weapon && FVector::Dist(Offender->GetActorLocation(), MyCharacter->GetActorLocation()) <= KnockOffMeleeDistance;
		if (PinDamageTaken >= KnockOffDamage || bShotgun || bMelee)
		{
			ThrowOff(Offender);
		}
		return;
	}

	// Сбит в полёте: падает камнем и оглушён.
	UCharacterMovementComponent* Movement = MyCharacter->GetCharacterMovement();
	const UNoFearMeleeComponent* Melee = GetMeleeComponent();
	const UNoFearMonsterAttack* Attack = Melee ? Melee->GetCurrentAttack() : nullptr;
	if (Attack && Attack->IsA<UNoFearMonsterAttack_Leap>() && Movement && Movement->IsFalling() && !bKnockedFromAir
		&& Info.Amount >= FlightKnockDamage)
	{
		bKnockedFromAir = true;
		++FlightKnockCount;
		Movement->Velocity = FVector(0.f, 0.f, -800.f);
		Stun(From, FlightStunTime);
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

void ANoFearSpitterAIController::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);
	bPoppingSac = false;
	if (UNoFearHealthComponent* Health = InPawn ? UNoFearHealthComponent::FindHealth(InPawn) : nullptr)
	{
		Health->OnDamaged.AddUniqueDynamic(this, &ANoFearSpitterAIController::HandleDamaged);
		BoundHealth = Health;
	}
}

void ANoFearSpitterAIController::OnUnPossess()
{
	if (UNoFearHealthComponent* Health = BoundHealth.Get())
	{
		Health->OnDamaged.RemoveDynamic(this, &ANoFearSpitterAIController::HandleDamaged);
	}
	BoundHealth.Reset();
	Super::OnUnPossess();
}

void ANoFearSpitterAIController::HandleDamaged(UNoFearHealthComponent* Component, const FNoFearDamageInfo& Info)
{
	// Открытый мешок (зона SacOpen есть только на замахе плевка) пробит — лопается.
	const UNoFearMeleeComponent* Melee = GetMeleeComponent();
	if (!bPoppingSac && Info.WeakZone == TEXT("SacOpen") && Melee && Melee->IsAttacking() && !UNoFearHealthComponent::IsActorDead(GetPawn()))
	{
		PopSac(Info);
	}
}

void ANoFearSpitterAIController::PopSac(const FNoFearDamageInfo& Info)
{
	ACharacter* MyCharacter = Cast<ACharacter>(GetPawn());
	UWorld* World = GetWorld();
	if (!MyCharacter || !World)
	{
		return;
	}
	TGuardValue<bool> Guard(bPoppingSac, true);
	++SacPopCount;

	if (UNoFearMeleeComponent* Melee = GetMeleeComponent())
	{
		Melee->CancelAttack();
	}

	// Лужа под самим плевуном: жжёт людей, что подошли вплотную (своих лужа не трогает).
	TSubclassOf<ANoFearAcidPuddle> PuddleClass = SacPuddleClass;
	if (!PuddleClass)
	{
		for (const UNoFearMonsterAttack* Attack : AttackSet)
		{
			const UNoFearMonsterAttack_Spit* Spit = Cast<UNoFearMonsterAttack_Spit>(Attack);
			const ANoFearSpitProjectile* Projectile = Spit && Spit->ProjectileClass ? Spit->ProjectileClass->GetDefaultObject<ANoFearSpitProjectile>() : nullptr;
			if (Projectile && Projectile->PuddleClass)
			{
				PuddleClass = Projectile->PuddleClass;
				break;
			}
		}
	}
	if (PuddleClass)
	{
		FHitResult Ground;
		FCollisionQueryParams Params(SCENE_QUERY_STAT(NoFearSacPuddle), false, MyCharacter);
		const FVector Start = MyCharacter->GetActorLocation();
		const FVector End = Start - FVector(0.f, 0.f, 400.f);
		FCollisionObjectQueryParams GroundObjects;
		GroundObjects.AddObjectTypesToQuery(ECC_WorldStatic);
		GroundObjects.AddObjectTypesToQuery(ECC_WorldDynamic);
		const FVector Location = World->LineTraceSingleByObjectType(Ground, Start, End, GroundObjects, Params)
			? Ground.ImpactPoint
			: Start - FVector(0.f, 0.f, MyCharacter->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
		FActorSpawnParameters SpawnParams;
		SpawnParams.Owner = MyCharacter;
		SpawnParams.Instigator = MyCharacter;
		SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		World->SpawnActor<ANoFearAcidPuddle>(PuddleClass, Location + FVector(0.f, 0.f, 1.f), FRotator::ZeroRotator, SpawnParams);
	}

	if (SacPopDamage > 0.f)
	{
		UGameplayStatics::ApplyDamage(MyCharacter, SacPopDamage, Info.InstigatorController, Info.DamageCauser, UDamageType::StaticClass());
	}

	if (!UNoFearHealthComponent::IsActorDead(MyCharacter) && SacPopStunTime > 0.f)
	{
		const ANoFearCharacter* NoFearCharacter = Cast<ANoFearCharacter>(MyCharacter);
		if (UNoFearPoseComponent* Pose = NoFearCharacter ? NoFearCharacter->GetPoseComponent() : nullptr)
		{
			const FVector From = Info.bHasSource ? Info.SourceLocation : MyCharacter->GetActorLocation() + MyCharacter->GetActorForwardVector() * 100.f;
			Pose->Stagger((MyCharacter->GetActorLocation() - From).GetSafeNormal2D(), SacPopStunTime);
			Pose->OnHitReaction.Broadcast(Pose, true);
		}
	}
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
	// Союзник ближе к цели — встать за его спину (колосс — в первую очередь, иначе ближайший к цели).
	ScreenAlly.Reset();
	const FVector MyLocation = MyPawn->GetActorLocation();
	APawn* BestAlly = nullptr;
	float BestScore = TNumericLimits<float>::Max();
	for (ANoFearCharacter* Candidate : TActorRange<ANoFearCharacter>(GetWorld()))
	{
		const ANoFearMonsterAIController* Ally = Candidate && Candidate != MyPawn ? Cast<ANoFearMonsterAIController>(Candidate->GetController()) : nullptr;
		if (!Ally || Ally->IsA<ANoFearSpitterAIController>() || UNoFearHealthComponent::IsActorDead(Candidate)
			|| !UNoFearTeamLibrary::AreFriendly(MyPawn, Candidate))
		{
			continue;
		}
		const float AllyToTarget = FVector::Dist2D(Candidate->GetActorLocation(), TargetLocation);
		if (AllyToTarget > StandDistance || AllyToTarget < 100.f)
		{
			continue;
		}
		const float Score = AllyToTarget - (Ally->IsA<ANoFearTankAIController>() ? 100000.f : 0.f);
		if (Score < BestScore)
		{
			BestScore = Score;
			BestAlly = Candidate;
		}
	}
	if (BestAlly)
	{
		const FVector Away = (BestAlly->GetActorLocation() - TargetLocation).GetSafeNormal2D();
		const float Distance = FMath::Max(FVector::Dist2D(BestAlly->GetActorLocation(), TargetLocation) + ScreenOffset, RetreatStopDistance + 50.f);
		OutPoint = TargetLocation + Away * Distance;
		OutPoint.Z = MyLocation.Z;
		ScreenAlly = BestAlly;
		return true;
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
	/** Звук в точке, обрезанный затуханием через Duration: стартовые ветры зациклены и иначе играют вечно. */
	void PlayLimitedSound(UObject* WorldContext, USoundBase* Sound, const FVector& Location, float Pitch, float Duration)
	{
		UAudioComponent* Audio = Sound ? UGameplayStatics::SpawnSoundAtLocation(WorldContext, Sound, Location, FRotator::ZeroRotator, 1.f, Pitch, 0.f) : nullptr;
		UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
		if (!Audio || !World)
		{
			return;
		}
		const float FadeTime = FMath::Min(0.25f, Duration * 0.5f);
		FTimerHandle Handle;
		World->GetTimerManager().SetTimer(Handle, FTimerDelegate::CreateWeakLambda(Audio, [Audio, FadeTime]()
		{
			Audio->FadeOut(FadeTime, 0.f);
		}), FMath::Max(0.05f, Duration - FadeTime), false);
	}

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
	PrimaryActorTick.bCanEverTick = true;
	// Сам не дерётся: жетон не берёт, бьёт только вплотную.
	bUseAttackTokens = false;
	bFinishKnockedDown = false;

	UNoFearMonsterAttack_Swing* Swipe = CreateDefaultSubobject<UNoFearMonsterAttack_Swing>(TEXT("Swipe"));
	Swipe->AttackName = TEXT("Swipe");
	Swipe->MaxRange = 180.f;
	Swipe->Damage = 5.f;
	Swipe->Cooldown = 1.5f;
	AttackSet.Add(Swipe);

	WhisperSound = TSoftObjectPtr<USoundBase>(FSoftObjectPath(TEXT("/Game/Audio/Ambient/Starter_Wind05.Starter_Wind05")));

	// Шептун: голова во время гипноза ×3 и светится (окно Scream); голова ×1.5 (от загонщика).
	KindName = TEXT("Whisperer");
	FNoFearWeakZone HypnoHead;
	HypnoHead.Name = TEXT("HypnoHead");
	HypnoHead.Bones = { TEXT("head") };
	HypnoHead.DamageMultiplier = 3.f;
	HypnoHead.Condition = ENoFearWeakCondition::Scream;
	HypnoHead.bGlow = true;
	HypnoHead.GlowColor = FLinearColor(0.6f, 0.1f, 1.f);
	HypnoHead.GlowRadius = 14.f;
	WeakZones.Insert(HypnoHead, 0);

	TargetWeights.Hero = 0.6f;
	TargetWeights.Companion = 0.6f;
	TargetWeights.Soldier = 0.6f;
	TargetWeights.Civilian = 1.f;
	OpportunityBonuses.Reset();
}

void ANoFearWhispererAIController::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);

	Mode = EMode::Keep;
	ModeStartTime = GetNow();
	Victim.Reset();
	TendPortal.Reset();
	RetryUntil.Reset();
	VisionVictims.Reset();
	LastUpdateTime = -1.0;
	const double Now = GetNow();
	// Первое видение — не сразу после появления.
	NextVisionTime = Now + FMath::FRandRange(VisionInterval.X, VisionInterval.Y);
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

AActor* ANoFearWhispererAIController::GetTendedPortal() const
{
	return Mode == EMode::Tend ? TendPortal.Get() : nullptr;
}

bool ANoFearWhispererAIController::WantsFreeAttack(AActor* Target)
{
	// Бьёт только вплотную и не во время гипноза.
	return Mode != EMode::Hypnotize && GetTargetDistance2D() <= 220.f;
}

void ANoFearWhispererAIController::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	APawn* MyPawn = GetPawn();
	if (!MyPawn)
	{
		return;
	}
	// Скорость задаёт Blueprint тела: бег к жертве до HuntRunSpeed — временем тела, в ударе время обычное.
	float Dilation = 1.f;
	const UNoFearMeleeComponent* Melee = GetMeleeComponent();
	const ACharacter* Body = Cast<ACharacter>(MyPawn);
	const UCharacterMovementComponent* Movement = Body ? Body->GetCharacterMovement() : nullptr;
	if (Mode == EMode::Hunt && Movement && Movement->MaxWalkSpeed > 1.f && !(Melee && Melee->IsAttacking())
		&& !UNoFearHealthComponent::IsActorDead(MyPawn))
	{
		Dilation = FMath::Max(1.f, HuntRunSpeed / Movement->MaxWalkSpeed);
	}
	MyPawn->CustomTimeDilation = Dilation;
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
	UpdateGuardTrigger();
	if (Mode == EMode::Hypnotize)
	{
		UpdateHypnosis(DeltaTime);
	}
	else
	{
		UpdateMode();
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

bool ANoFearWhispererAIController::SendVision(APawn* VisionVictim)
{
	APawn* MyPawn = GetPawn();
	ANoFearCharacter* Human = Cast<ANoFearCharacter>(VisionVictim);
	if (!MyPawn || !Human || UNoFearHealthComponent::IsActorIncapacitated(Human))
	{
		return false;
	}
	const FVector Origin = Human->GetActorLocation();
	const FVector Direction = FVector::ForwardVector.RotateAngleAxis(FMath::FRandRange(0.f, 360.f), FVector::UpVector);
	const FVector Point = Origin + Direction * FMath::FRandRange(VisionDistance.X, VisionDistance.Y) + FVector(0.f, 0.f, 40.f);

	// Мирный не стреляет — бежит от ложного монстра (жертву гипноза не трогает).
	if (ANoFearCivilianAIController* Civilian = Cast<ANoFearCivilianAIController>(Human->GetController()))
	{
		if (Civilian->IsPortalControlled())
		{
			return false;
		}
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
	VisionVictims.RemoveAll([](const TWeakObjectPtr<APawn>& Item) { return !Item.IsValid(); });
	if (!MyPawn || !World || Now < NextVisionTime)
	{
		return;
	}
	// Один бот из подходящих: в ауре, страх от VisionMinFear, не герой, не в трансе.
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
		const ANoFearAIController* Bot = Cast<ANoFearAIController>(Human->GetController());
		if (Bot && Bot->IsPortalControlled())
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

// --- Охота и гипноз ---

void ANoFearWhispererAIController::SetMode(EMode NewMode)
{
	if (Mode == NewMode)
	{
		return;
	}
	Mode = NewMode;
	ModeStartTime = GetNow();
	if (NewMode != EMode::Hunt && NewMode != EMode::Hypnotize)
	{
		Victim.Reset();
	}
	if (NewMode != EMode::Tend)
	{
		TendPortal.Reset();
	}
}

bool ANoFearWhispererAIController::IsValidVictim(const APawn* Candidate) const
{
	const APawn* MyPawn = GetPawn();
	if (!MyPawn || !Candidate || UNoFearHealthComponent::IsActorIncapacitated(Candidate) || UNoFearTeamLibrary::AreFriendly(MyPawn, Candidate))
	{
		return false;
	}
	// Только мирные, ещё не проводники и не в чужом трансе.
	const ANoFearCivilianAIController* Civilian = Cast<ANoFearCivilianAIController>(Candidate->GetController());
	if (!Civilian || Civilian->IsPortalControlled() || UNoFearPortalSubsystem::IsActorFeeder(Candidate))
	{
		return false;
	}
	const UNoFearFearComponent* Fear = UNoFearFearComponent::FindFear(Candidate);
	if (!Fear || !Fear->CanBeAfraid())
	{
		return false;
	}
	const double* Until = RetryUntil.Find(TWeakObjectPtr<APawn>(const_cast<APawn*>(Candidate)));
	return !Until || GetNow() >= *Until;
}

APawn* ANoFearWhispererAIController::FindHuntVictim() const
{
	const APawn* MyPawn = GetPawn();
	UWorld* World = GetWorld();
	if (!MyPawn || !World)
	{
		return nullptr;
	}
	APawn* Best = nullptr;
	float BestDistance = HuntRange;
	for (ANoFearCharacter* Human : TActorRange<ANoFearCharacter>(World))
	{
		if (Human == MyPawn || !IsValidVictim(Human))
		{
			continue;
		}
		const float Distance = FVector::Dist(Human->GetActorLocation(), MyPawn->GetActorLocation());
		if (Distance <= BestDistance)
		{
			BestDistance = Distance;
			Best = Human;
		}
	}
	return Best;
}

AActor* ANoFearWhispererAIController::FindTendPortal() const
{
	const APawn* MyPawn = GetPawn();
	const UNoFearPortalSubsystem* Portals = UNoFearPortalSubsystem::Get(this);
	if (!MyPawn || !Portals)
	{
		return nullptr;
	}
	AActor* Best = nullptr;
	float BestDistance = TendSearchRange;
	for (ANoFearPortal* Portal : Portals->GetPortals())
	{
		if (!Portal || Portal->IsClosing() || Portal->GetFeederCount() <= 0)
		{
			continue;
		}
		const float Distance = FVector::Dist(Portal->GetActorLocation(), MyPawn->GetActorLocation());
		if (Distance <= BestDistance)
		{
			BestDistance = Distance;
			Best = Portal;
		}
	}
	return Best;
}

void ANoFearWhispererAIController::UpdateMode()
{
	APawn* MyPawn = GetPawn();
	if (!MyPawn || IsScattering() || UNoFearPoseComponent::IsActorStaggered(MyPawn))
	{
		return;
	}
	const double Now = GetNow();
	for (auto It = RetryUntil.CreateIterator(); It; ++It)
	{
		if (!It.Key().IsValid() || Now >= It.Value())
		{
			It.RemoveCurrent();
		}
	}

	if (Mode == EMode::Hunt)
	{
		UpdateHunt();
		if (Mode == EMode::Hunt || Mode == EMode::Hypnotize)
		{
			return;
		}
	}
	// Мирные в округе — первым делом они.
	if (APawn* NewVictim = FindHuntVictim())
	{
		SetMode(EMode::Hunt);
		Victim = NewVictim;
		UpdateHunt();
		return;
	}
	// Мирные кончились (уже качают портал) — к порталу ускорять рост.
	if (AActor* Portal = FindTendPortal())
	{
		SetMode(EMode::Tend);
		TendPortal = Portal;
		UpdateTend();
		return;
	}
	SetMode(EMode::Keep);
	UpdatePosition();
}

void ANoFearWhispererAIController::UpdateHunt()
{
	APawn* MyPawn = GetPawn();
	UWorld* World = GetWorld();
	APawn* Target = Victim.Get();
	const double Now = GetNow();
	if (!MyPawn || !World)
	{
		return;
	}
	if (!IsValidVictim(Target) || Now - ModeStartTime > HuntTimeout)
	{
		// Не добежал или жертву перехватили — на время её не трогает.
		if (Target)
		{
			RetryUntil.Add(Target, Now + HypnoRetryTime);
		}
		SetMode(EMode::Keep);
		return;
	}
	const FVector MyLocation = MyPawn->GetActorLocation();
	const FVector VictimLocation = Target->GetActorLocation();
	const float Distance = FVector::Dist(MyLocation, VictimLocation);
	if (Distance <= HypnoStartRange && CanSee(Target))
	{
		StartHypnosis(Target);
		return;
	}
	// Бегом к жертве, на HuntApproachDistance от неё со своей стороны.
	FVector Away = (MyLocation - VictimLocation).GetSafeNormal2D();
	if (Away.IsNearlyZero())
	{
		Away = -MyPawn->GetActorForwardVector().GetSafeNormal2D();
	}
	FVector Goal = VictimLocation + Away * FMath::Min(HuntApproachDistance, HypnoStartRange * 0.8f);
	FVector Projected;
	if (NoFearWhisperer::ProjectToNav(World, Goal, Projected) || NoFearWhisperer::ProjectToNav(World, VictimLocation, Projected))
	{
		Goal = Projected;
	}
	SetShadeGoal(Goal, 0.6f);
}

bool ANoFearWhispererAIController::StartHypnosis(APawn* NewVictim)
{
	APawn* MyPawn = GetPawn();
	ANoFearAIController* VictimBot = NewVictim ? Cast<ANoFearAIController>(NewVictim->GetController()) : nullptr;
	if (!MyPawn || !VictimBot || UNoFearHealthComponent::IsActorDead(MyPawn) || UNoFearHealthComponent::IsActorIncapacitated(NewVictim)
		|| VictimBot->IsPortalControlled() || UNoFearPortalSubsystem::IsActorFeeder(NewVictim))
	{
		return false;
	}
	if (Mode == EMode::Hypnotize)
	{
		EndHypnosis(false);
	}
	// Жертва застывает лицом к шептуну, как проводник перед порталом.
	VictimBot->SetPortalTrance(true, MyPawn);
	SetMode(EMode::Hypnotize);
	Victim = NewVictim;
	StopMovement();
	SetFocus(NewVictim, EAIFocusPriority::Gameplay);
	if (UNoFearVulnerabilityComponent* Vulnerability = MyPawn->FindComponentByClass<UNoFearVulnerabilityComponent>())
	{
		Vulnerability->OpenWindow(ENoFearWeakCondition::Scream, 3600.f);
	}
	const UNoFearHealthComponent* VictimHealth = UNoFearHealthComponent::FindHealth(NewVictim);
	VictimStartHealth = VictimHealth ? VictimHealth->GetHealth() : 0.f;
	HypnoDamage = 0.f;
	LastSeenVictimTime = GetNow();
	++HypnoCount;
	return true;
}

void ANoFearWhispererAIController::UpdateHypnosis(float DeltaTime)
{
	APawn* MyPawn = GetPawn();
	UWorld* World = GetWorld();
	APawn* Target = Victim.Get();
	const double Now = GetNow();
	if (!MyPawn || !World)
	{
		return;
	}
	if (!Target || UNoFearHealthComponent::IsActorIncapacitated(Target))
	{
		EndHypnosis(false);
		return;
	}
	// Сломалась — уже проводник, портал забрал её себе.
	if (UNoFearPortalSubsystem::IsActorFeeder(Target))
	{
		EndHypnosis(true);
		return;
	}
	const ANoFearAIController* VictimBot = Cast<ANoFearAIController>(Target->GetController());
	const UNoFearHealthComponent* VictimHealth = UNoFearHealthComponent::FindHealth(Target);
	const float Fear = NoFearWhisperer::GetHumanFear(Target);
	if (CanSee(Target))
	{
		LastSeenVictimTime = Now;
	}
	if (!VictimBot || !VictimBot->IsInPortalTrance()
		|| Now - LastSeenVictimTime > HypnoLostSightTime
		|| FVector::Dist(Target->GetActorLocation(), MyPawn->GetActorLocation()) > HypnoBreakRange
		|| (VictimHealth && VictimHealth->GetHealth() < VictimStartHealth - 0.5f)
		|| (Now - ModeStartTime > 1.0 && Fear < HypnoCalmFear))
	{
		EndHypnosis(false);
		return;
	}
	SetShadeGoal(MyPawn->GetActorLocation(), 0.6f);
	SetFocus(Target, EAIFocusPriority::Gameplay);
	DrawDebugLine(World, MyPawn->GetPawnViewLocation(), Target->GetPawnViewLocation(), FColor(170, 40, 255), false, 0.3f, 0, 3.f);
	// Прямой испуг: на 100 страх ломает жертву, портал делает её проводником.
	NoFearWhisperer::AddWhisperFear(Target, HypnoFearPerSecond * DeltaTime, 100.f, MyPawn->GetActorLocation(), MyPawn, true);
}

void ANoFearWhispererAIController::EndHypnosis(bool bBroken)
{
	if (Mode != EMode::Hypnotize)
	{
		return;
	}
	APawn* Target = Victim.Get();
	if (bBroken)
	{
		++BrokenCount;
	}
	else
	{
		++HypnoInterruptCount;
		if (Target)
		{
			RetryUntil.Add(Target, GetNow() + HypnoRetryTime);
			// Очнулась: убегает от шептуна (если её не забрал портал).
			ANoFearAIController* VictimBot = Cast<ANoFearAIController>(Target->GetController());
			if (VictimBot && VictimBot->IsInPortalTrance() && !UNoFearPortalSubsystem::IsActorFeeder(Target))
			{
				VictimBot->SetPortalTrance(false);
			}
		}
	}
	if (APawn* MyPawn = GetPawn())
	{
		if (UNoFearVulnerabilityComponent* Vulnerability = MyPawn->FindComponentByClass<UNoFearVulnerabilityComponent>())
		{
			Vulnerability->CloseWindow(ENoFearWeakCondition::Scream);
		}
	}
	ClearFocus(EAIFocusPriority::Gameplay);
	SetMode(EMode::Keep);
}

// --- У портала ---

void ANoFearWhispererAIController::UpdateTend()
{
	APawn* MyPawn = GetPawn();
	UWorld* World = GetWorld();
	ANoFearPortal* Portal = Cast<ANoFearPortal>(TendPortal.Get());
	if (!MyPawn || !World)
	{
		return;
	}
	if (!Portal || Portal->IsClosing() || Portal->GetFeederCount() <= 0)
	{
		SetMode(EMode::Keep);
		return;
	}
	const FVector PortalLocation = Portal->GetActorLocation();
	const FVector MyLocation = MyPawn->GetActorLocation();
	// Со стороны от ближайшего стрелка, иначе — со своей.
	float ShooterDistance = 0.f;
	const APawn* Shooter = FindNearestHuman(true, ShooterDistance);
	FVector Side = Shooter ? (PortalLocation - Shooter->GetActorLocation()).GetSafeNormal2D() : (MyLocation - PortalLocation).GetSafeNormal2D();
	if (Side.IsNearlyZero())
	{
		Side = FVector::ForwardVector;
	}
	FVector Goal = PortalLocation + Side * TendDistance;
	FVector Projected;
	if (NoFearWhisperer::ProjectToNav(World, Goal, Projected))
	{
		Goal = Projected;
	}
	SetShadeGoal(Goal, 0.6f);

	if (FVector::Dist2D(MyLocation, PortalLocation) <= TendRadius)
	{
		Portal->ApplyWhisperBoost(PortalGrowthScale, PortalFreeScale, 0.6f);
		DrawDebugLine(World, MyPawn->GetPawnViewLocation(), PortalLocation, FColor(120, 20, 200), false, 0.3f, 0, 1.5f);
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
	Pose->Stagger((MyCharacter->GetActorLocation() - From).GetSafeNormal2D(), HypnoStunTime);
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

	if (Mode == EMode::Hypnotize)
	{
		HypnoDamage += Info.Amount;
		if (HypnoDamage >= HypnoInterruptDamage)
		{
			// Гипноз сорван: оглушён, жертва очнулась.
			EndHypnosis(false);
			Stun(From);
		}
	}
	if (Offender && !UNoFearTeamLibrary::AreFriendly(MyPawn, Offender))
	{
		RaiseGuard(Offender);
	}
}

void ANoFearWhispererAIController::UpdateGuardTrigger()
{
	// Только вооружённые: безоружный мирный рядом — это жертва, а не угроза.
	float Distance = 0.f;
	if (APawn* Intruder = FindNearestHuman(true, Distance); Intruder && Distance <= GuardTriggerRadius)
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
	// Сам уходит фазовым шагом (своя перезарядка). Гипноз не бросает: его срывает только урон от HypnoInterruptDamage.
	if (Now >= NextPhaseStepTime && Mode != EMode::Hypnotize)
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
	// Шаг рвёт гипноз: жертва очнулась.
	EndHypnosis(false);
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
	if (!MyPawn || !World || IsScattering())
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
		}
		else if (ShooterDistance < KeepMinDistance || ShooterDistance > KeepMaxDistance)
		{
			// Коридор KeepMin–KeepMax от стрелка: со своей стороны, к середине коридора.
			const FVector Away = (MyLocation - Shooter->GetActorLocation()).GetSafeNormal2D();
			Goal = Shooter->GetActorLocation() + Away * (0.5f * (KeepMinDistance + KeepMaxDistance));
		}
	}
	else
	{
		// Только безоружные (не годные в жертвы): подходит на край ауры.
		const FVector Away = (MyLocation - Target->GetActorLocation()).GetSafeNormal2D();
		Goal = Target->GetActorLocation() + Away * (AuraRadius * 0.65f);
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
	MyPawn->CustomTimeDilation = 1.f;
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
	// Жертва гипноза приходит в себя.
	EndHypnosis(false);
	SetMode(EMode::Keep);
	for (const TWeakObjectPtr<APawn>& Item : VisionVictims)
	{
		if (UNoFearCombatComponent* Combat = Item.IsValid() ? Item->FindComponentByClass<UNoFearCombatComponent>() : nullptr)
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
	// Бафф держится 0.6 с после последнего пересчёта и спадёт сам, буст портала — тоже.
}

// --- Тень ---

ANoFearShadeAIController::ANoFearShadeAIController(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// Охотится одна: в стаю не входит, жетон не берёт, удар запускает сама (BeginStrike).
	bUseAttackTokens = false;
	bJoinsPack = false;
	bFinishKnockedDown = false;

	AttackSet.Reset();
	UNoFearMonsterAttack_Swing* Strike = CreateDefaultSubobject<UNoFearMonsterAttack_Swing>(TEXT("Strike"));
	Strike->AttackName = TEXT("Strike");
	Strike->MaxRange = 260.f;
	Strike->Damage = StrikeDamage;
	Strike->Cooldown = 1.f;
	Strike->WindupTime = StrikeWindup;
	Strike->RecoveryTime = 0.3f;
	Strike->HitReach = 110.f;
	Strike->LungeSpeed = 500.f;
	Strike->LungeStopDistance = 90.f;
	AttackSet.Add(Strike);

	CloakMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/Materials/M_NoFear_ShadeCloak.M_NoFear_ShadeCloak")));
	HissSound = TSoftObjectPtr<USoundBase>(FSoftObjectPath(TEXT("/Game/Audio/Ambient/Starter_Wind06.Starter_Wind06")));
	CrySound = TSoftObjectPtr<USoundBase>(FSoftObjectPath(TEXT("/Game/Audio/Ambient/Starter_Wind05.Starter_Wind05")));
	StepsSound = TSoftObjectPtr<USoundBase>(FSoftObjectPath(TEXT("/Game/Audio/Foley/MetaSounds/MSS_FoleySound_Run.MSS_FoleySound_Run")));
	ShotSound = TSoftObjectPtr<USoundBase>(FSoftObjectPath(TEXT("/Game/Audio/Effects/SC_GunShot_Cue.SC_GunShot_Cue")));

	// Тень: голова ×2, пока проявлена; спина ×1.5 на отходе. Обе зоны включает контроллер.
	KindName = TEXT("Shade");
	WeakZones.Reset();
	FNoFearWeakZone Head;
	Head.Name = TEXT("RevealedHead");
	Head.Bones = { TEXT("head") };
	Head.DamageMultiplier = 2.f;
	Head.Condition = ENoFearWeakCondition::Manual;
	WeakZones.Add(Head);
	FNoFearWeakZone Back;
	Back.Name = TEXT("RetreatBack");
	Back.Side = ENoFearWeakSide::Back;
	Back.DamageMultiplier = 1.5f;
	Back.Condition = ENoFearWeakCondition::Manual;
	WeakZones.Add(Back);

	TargetWeights.Hero = 1.f;
	TargetWeights.Companion = 1.f;
	TargetWeights.Soldier = 1.f;
	TargetWeights.Civilian = 0.6f;
	OpportunityBonuses.Reset();
	OpportunityBonuses.Add(ENoFearOpportunity::Isolated, 2.f);
}

void ANoFearShadeAIController::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);

	State = ENoFearShadeState::Stalk;
	Victim.Reset();
	RecentDamage.Reset();
	CloakInstances.Reset();
	bHasOrbit = false;
	LastUpdateTime = -1.0;
	const double Now = GetNow();
	StateStartTime = Now;
	RevealUntil = 0.0;
	HideUntil = 0.0;
	NextStrikeTime = Now + 1.f;
	NextFakeSoundTime = Now + FMath::FRandRange(FakeSoundInterval.X, FakeSoundInterval.Y);
	Opacity = StandingOpacity;

	ANoFearCharacter* Body = Cast<ANoFearCharacter>(InPawn);
	if (!Body)
	{
		return;
	}
	if (UNoFearHealthComponent* Health = UNoFearHealthComponent::FindHealth(Body))
	{
		Health->OnDamaged.AddUniqueDynamic(this, &ANoFearShadeAIController::HandleDamaged);
		BoundHealth = Health;
	}
	// Шаги почти не слышны.
	const ANoFearCharacter* Defaults = GetDefault<ANoFearCharacter>();
	Body->WalkFootstepRange = Defaults->WalkFootstepRange * FootstepScale;
	Body->RunFootstepRange = Defaults->RunFootstepRange * FootstepScale;
	Body->SprintFootstepRange = Defaults->SprintFootstepRange * FootstepScale;

	// Плащ: прозрачный материал на всех слотах; окраска тела в BeginPlay его не перекрывает.
	USkeletalMeshComponent* Mesh = Body->GetMesh();
	UMaterialInterface* Cloak = CloakMaterial.LoadSynchronous();
	if (Mesh && Cloak)
	{
		Body->bApplyBodyColor = false;
		for (int32 Slot = 0; Slot < Mesh->GetNumMaterials(); ++Slot)
		{
			UMaterialInstanceDynamic* Instance = UMaterialInstanceDynamic::Create(Cloak, Body);
			Mesh->SetMaterial(Slot, Instance);
			CloakInstances.Add(Instance);
		}
	}
	ApplyOpacity();
}

void ANoFearShadeAIController::OnUnPossess()
{
	if (UNoFearHealthComponent* Health = BoundHealth.Get())
	{
		Health->OnDamaged.RemoveDynamic(this, &ANoFearShadeAIController::HandleDamaged);
	}
	BoundHealth.Reset();
	if (UNoFearMeleeComponent* Melee = GetMeleeComponent())
	{
		Melee->OnAttackHit.RemoveDynamic(this, &ANoFearShadeAIController::HandleAttackHit);
	}
	if (APawn* MyPawn = GetPawn())
	{
		MyPawn->CustomTimeDilation = 1.f;
	}
	Super::OnUnPossess();
}

void ANoFearShadeAIController::ConfigureMelee(UNoFearMeleeComponent* Melee)
{
	Super::ConfigureMelee(Melee);
	if (Melee)
	{
		Melee->OnAttackHit.AddUniqueDynamic(this, &ANoFearShadeAIController::HandleAttackHit);
	}
}

float ANoFearShadeAIController::GetSightLimit(const AActor* Actor)
{
	const APawn* Pawn = Cast<APawn>(Actor);
	const ANoFearShadeAIController* Shade = Pawn ? Cast<ANoFearShadeAIController>(Pawn->GetController()) : nullptr;
	if (!Shade || UNoFearHealthComponent::IsActorDead(Pawn))
	{
		return -1.f;
	}
	if (Shade->IsVanished())
	{
		return 0.f;
	}
	return Shade->IsRevealed() ? -1.f : Shade->CloakedSightRange;
}

bool ANoFearShadeAIController::IsVanished() const
{
	return (State == ENoFearShadeState::Retreat || State == ENoFearShadeState::Flee) && GetNow() < HideUntil;
}

bool ANoFearShadeAIController::IsRevealed() const
{
	const APawn* MyPawn = GetPawn();
	if (!MyPawn || IsVanished())
	{
		return false;
	}
	if (State == ENoFearShadeState::Strike || GetNow() < RevealUntil)
	{
		return true;
	}
	// Человек вплотную.
	for (const ANoFearCharacter* Human : TActorRange<ANoFearCharacter>(GetWorld()))
	{
		if (Human && Human != MyPawn && !Cast<ANoFearMonsterAIController>(Human->GetController())
			&& !UNoFearHealthComponent::IsActorDead(Human)
			&& FVector::DistSquared(Human->GetActorLocation(), MyPawn->GetActorLocation()) <= FMath::Square(RevealHumanDistance))
		{
			return true;
		}
	}
	return false;
}

void ANoFearShadeAIController::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	APawn* MyPawn = GetPawn();
	if (!MyPawn)
	{
		return;
	}
	// Скорость задаёт Blueprint тела: бег до RunSpeed — временем тела, в ударе время обычное.
	float Dilation = 1.f;
	const UNoFearMeleeComponent* Melee = GetMeleeComponent();
	const ACharacter* Body = Cast<ACharacter>(MyPawn);
	const UCharacterMovementComponent* Movement = Body ? Body->GetCharacterMovement() : nullptr;
	const bool bRunning = State == ENoFearShadeState::Approach || State == ENoFearShadeState::Retreat || State == ENoFearShadeState::Flee;
	if (bRunning && Movement && Movement->MaxWalkSpeed > 1.f && !(Melee && Melee->IsAttacking()))
	{
		Dilation = FMath::Max(1.f, RunSpeed / Movement->MaxWalkSpeed);
	}
	MyPawn->CustomTimeDilation = Dilation;
	UpdateCloak(DeltaSeconds);
}

void ANoFearShadeAIController::UpdateMovementStance()
{
	// Выслеживает шагом, заходит, отходит и бежит бегом; лицом по ходу.
	const bool bRunning = State == ENoFearShadeState::Approach || State == ENoFearShadeState::Retreat || State == ENoFearShadeState::Flee;
	SetMovementFlags(false, false, !bRunning, false);
}

void ANoFearShadeAIController::UpdateBot()
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

	switch (State)
	{
	case ENoFearShadeState::Stalk:
		UpdateStalk(DeltaTime);
		break;
	case ENoFearShadeState::Approach:
		UpdateApproach();
		break;
	case ENoFearShadeState::Strike:
		UpdateStrike();
		break;
	default:
		UpdateHide();
		break;
	}

	if (UNoFearVulnerabilityComponent* Vulnerability = UNoFearVulnerabilityComponent::Find(MyPawn))
	{
		Vulnerability->SetZoneOpen(TEXT("RevealedHead"), IsRevealed());
		Vulnerability->SetZoneOpen(TEXT("RetreatBack"), State == ENoFearShadeState::Retreat || State == ENoFearShadeState::Flee);
	}
}

void ANoFearShadeAIController::SetState(ENoFearShadeState NewState)
{
	State = NewState;
	StateStartTime = GetNow();
	bHasOrbit = false;
}

void ANoFearShadeAIController::MoveToward(const FVector& Goal)
{
	FVector Projected = Goal;
	NoFearWhisperer::ProjectToNav(GetWorld(), Goal, Projected);
	SetShadeGoal(Projected, 0.6f);
}

// --- Выслеживание ---

void ANoFearShadeAIController::GatherHumans(TArray<APawn*>& OutHumans) const
{
	const APawn* MyPawn = GetPawn();
	if (!MyPawn)
	{
		return;
	}
	for (ANoFearCharacter* Human : TActorRange<ANoFearCharacter>(GetWorld()))
	{
		if (Human && Human != MyPawn && !Cast<ANoFearMonsterAIController>(Human->GetController())
			&& !UNoFearHealthComponent::IsActorIncapacitated(Human) && UNoFearTeamLibrary::AreHostile(MyPawn, Human)
			&& FVector::DistSquared(Human->GetActorLocation(), MyPawn->GetActorLocation()) <= FMath::Square(HuntRange))
		{
			OutHumans.Add(Human);
		}
	}
}

float ANoFearShadeAIController::GetNearestAllyDistance(const APawn* Human) const
{
	float Best = TNumericLimits<float>::Max();
	for (const ANoFearCharacter* Other : TActorRange<ANoFearCharacter>(GetWorld()))
	{
		if (Other && Other != Human && !Cast<ANoFearMonsterAIController>(Other->GetController())
			&& !UNoFearHealthComponent::IsActorIncapacitated(Other) && UNoFearTeamLibrary::AreFriendly(Human, Other))
		{
			Best = FMath::Min(Best, FVector::Dist(Other->GetActorLocation(), Human->GetActorLocation()));
		}
	}
	return Best;
}

APawn* ANoFearShadeAIController::FindIsolatedVictim() const
{
	TArray<APawn*> Humans;
	GatherHumans(Humans);
	APawn* Best = nullptr;
	float BestScore = -1.f;
	for (APawn* Human : Humans)
	{
		const float AllyDistance = GetNearestAllyDistance(Human);
		if (AllyDistance <= IsolatedDistance)
		{
			continue;
		}
		// Дальше от своих лучше, вооружённый интереснее; совсем одинокий — как отставший на HuntRange.
		float Score = FMath::Min(AllyDistance, HuntRange);
		if (NoFearWhisperer::IsArmed(Cast<ANoFearCharacter>(Human)))
		{
			Score += ArmedBonus;
		}
		if (Score > BestScore)
		{
			BestScore = Score;
			Best = Human;
		}
	}
	return Best;
}

bool ANoFearShadeAIController::GetGroupCenter(FVector& OutCenter) const
{
	const APawn* MyPawn = GetPawn();
	TArray<APawn*> Humans;
	GatherHumans(Humans);
	if (!MyPawn || Humans.IsEmpty())
	{
		return false;
	}
	const APawn* Nearest = nullptr;
	float NearestDistance = TNumericLimits<float>::Max();
	for (const APawn* Human : Humans)
	{
		const float Distance = FVector::DistSquared(Human->GetActorLocation(), MyPawn->GetActorLocation());
		if (Distance < NearestDistance)
		{
			NearestDistance = Distance;
			Nearest = Human;
		}
	}
	FVector Sum = FVector::ZeroVector;
	int32 Count = 0;
	for (const APawn* Human : Humans)
	{
		if (FVector::Dist(Human->GetActorLocation(), Nearest->GetActorLocation()) <= GroupRadius)
		{
			Sum += Human->GetActorLocation();
			++Count;
		}
	}
	OutCenter = Sum / FMath::Max(Count, 1);
	return true;
}

void ANoFearShadeAIController::UpdateStalk(float DeltaTime)
{
	APawn* MyPawn = GetPawn();
	const double Now = GetNow();
	if (Now >= NextStrikeTime)
	{
		if (APawn* Isolated = FindIsolatedVictim())
		{
			Victim = Isolated;
			const FVector ToMe = MyPawn->GetActorLocation() - Isolated->GetActorLocation();
			// Обходит с той стороны, с которой ближе к спине.
			ArcSign = FVector::DotProduct(ToMe, FVector::CrossProduct(FVector::UpVector, Isolated->GetActorForwardVector())) >= 0.f ? 1.f : -1.f;
			SetState(ENoFearShadeState::Approach);
			UpdateApproach();
			return;
		}
	}
	Victim.Reset();

	// Все вместе — кружит вокруг группы в коридоре StalkMin–StalkMax и ждёт.
	FVector Center;
	if (!GetGroupCenter(Center))
	{
		return;
	}
	const FVector FromCenter = MyPawn->GetActorLocation() - Center;
	if (!bHasOrbit)
	{
		OrbitAngle = FMath::RadiansToDegrees(FMath::Atan2(FromCenter.Y, FromCenter.X));
		bHasOrbit = true;
	}
	OrbitAngle = FMath::UnwindDegrees(OrbitAngle + StalkOrbitSpeed * DeltaTime);
	const float Radius = FMath::Clamp(static_cast<float>(FromCenter.Size2D()), StalkMinDistance, StalkMaxDistance);
	const FVector Goal = Center + FRotator(0.f, OrbitAngle, 0.f).Vector() * Radius;
	MoveToward(FVector(Goal.X, Goal.Y, MyPawn->GetActorLocation().Z));

	UpdateFakeSounds();
}

// --- Подход и удар ---

void ANoFearShadeAIController::UpdateApproach()
{
	APawn* MyPawn = GetPawn();
	AActor* Target = Victim.Get();
	const double Now = GetNow();
	if (!Target || UNoFearHealthComponent::IsActorIncapacitated(Target) || GetNearestAllyDistance(Cast<APawn>(Target)) < RejoinedDistance)
	{
		// Жертва вернулась к своим — снова ждать.
		Victim.Reset();
		SetState(ENoFearShadeState::Stalk);
		return;
	}
	const FVector TargetLocation = Target->GetActorLocation();
	const FVector Forward = Target->GetActorForwardVector().GetSafeNormal2D();
	const FVector ToMe = MyPawn->GetActorLocation() - TargetLocation;
	const float Distance = ToMe.Size2D();
	const bool bBehind = FVector::DotProduct(ToMe.GetSafeNormal2D(), -Forward) >= FMath::Cos(FMath::DegreesToRadians(BackHalfAngle));
	const bool bImpatient = Now - StateStartTime > ApproachPatience;
	if (Distance <= StrikeRange && (bBehind || bImpatient))
	{
		BeginStrike(Target);
		return;
	}
	FVector Goal;
	if (bBehind || bImpatient)
	{
		// За спиной: прямо к ней.
		Goal = TargetLocation - Forward * BehindDistance;
	}
	else
	{
		// Спереди или сбоку: по дуге в сторону спины — точка на круге ArcRadius, повёрнутая к спине.
		const FVector Dir = Distance > 1.f ? ToMe / Distance : -Forward;
		const float Radius = FMath::Min(ArcRadius, FMath::Max(Distance, StrikeRange));
		const FVector Next = Dir.RotateAngleAxis(50.f * ArcSign, FVector::UpVector);
		Goal = TargetLocation + Next.GetSafeNormal2D() * Radius;
		// Дошли до спины — дальше прямо.
		if (FVector::DotProduct(Next.GetSafeNormal2D(), -Forward) > 0.7f)
		{
			Goal = TargetLocation - Forward * FMath::Min(Radius, BehindDistance + 200.f);
		}
	}
	MoveToward(FVector(Goal.X, Goal.Y, TargetLocation.Z));
}

void ANoFearShadeAIController::BeginStrike(AActor* Target)
{
	UNoFearMeleeComponent* Melee = GetMeleeComponent();
	APawn* MyPawn = GetPawn();
	if (!Melee || !MyPawn || !Melee->StartAttackByName(Target, TEXT("Strike"), true))
	{
		return;
	}
	StopMovement();
	SetShadeGoal(MyPawn->GetActorLocation(), 0.6f);
	SetState(ENoFearShadeState::Strike);
	++StrikeCount;
	// Проявляется на замахе с шипением.
	NoFearWhisperer::PlayLimitedSound(this, HissSound.LoadSynchronous(), MyPawn->GetActorLocation(), 2.4f, HissDuration);
}

void ANoFearShadeAIController::HandleAttackHit(AActor* HitVictim, float Damage)
{
	const UNoFearMeleeComponent* Melee = GetMeleeComponent();
	const UNoFearMonsterAttack* Attack = Melee ? Melee->GetCurrentAttack() : nullptr;
	const APawn* MyPawn = GetPawn();
	ACharacter* VictimCharacter = Cast<ACharacter>(HitVictim);
	if (!Attack || Attack->AttackName != TEXT("Strike") || !MyPawn || !VictimCharacter || UNoFearHealthComponent::IsActorDead(VictimCharacter))
	{
		return;
	}
	// В спину — сбивает с ног.
	const FVector FromVictim = (MyPawn->GetActorLocation() - VictimCharacter->GetActorLocation()).GetSafeNormal2D();
	if (FVector::DotProduct(FromVictim, -VictimCharacter->GetActorForwardVector().GetSafeNormal2D()) < FMath::Cos(FMath::DegreesToRadians(BackHalfAngle)))
	{
		return;
	}
	++KnockDownCount;
	UNoFearOpportunitySubsystem::AddTag(VictimCharacter, ENoFearOpportunity::KnockedDown, KnockDownTime);
	VictimCharacter->LaunchCharacter(-FromVictim * 350.f + FVector::UpVector * 120.f, true, true);
	if (const ANoFearCharacter* VictimNoFear = Cast<ANoFearCharacter>(VictimCharacter))
	{
		if (UNoFearPoseComponent* Pose = VictimNoFear->GetPoseComponent())
		{
			Pose->Stagger(-FromVictim, KnockDownTime);
			Pose->OnHitReaction.Broadcast(Pose, true);
		}
	}
	if (AAIController* VictimAI = Cast<AAIController>(VictimCharacter->GetController()))
	{
		VictimAI->StopMovement();
	}
}

void ANoFearShadeAIController::UpdateStrike()
{
	const UNoFearMeleeComponent* Melee = GetMeleeComponent();
	if (Melee && Melee->IsAttacking() && GetNow() - StateStartTime < 3.f)
	{
		return;
	}
	// Ударила — уходит и исчезает.
	const AActor* Target = Victim.Get();
	const APawn* MyPawn = GetPawn();
	const FVector From = Target ? Target->GetActorLocation() : MyPawn->GetActorLocation() - MyPawn->GetActorForwardVector() * 100.f;
	BeginHide(ENoFearShadeState::Retreat, From, RetreatDistance, VanishTime);
}

// --- Отход и бегство ---

void ANoFearShadeAIController::BeginHide(ENoFearShadeState NewState, const FVector& From, float Distance, float Duration)
{
	const APawn* MyPawn = GetPawn();
	if (UNoFearMeleeComponent* Melee = GetMeleeComponent())
	{
		Melee->CancelAttack();
	}
	FVector Away = (MyPawn->GetActorLocation() - From).GetSafeNormal2D();
	if (Away.IsNearlyZero())
	{
		Away = -MyPawn->GetActorForwardVector().GetSafeNormal2D();
	}
	// Прочь от обидчика; не вышло на навигации — пробует со сдвигом вбок.
	HideGoal = MyPawn->GetActorLocation() + Away * Distance;
	for (const float Turn : { 0.f, 35.f, -35.f, 70.f, -70.f })
	{
		FVector Projected;
		if (NoFearWhisperer::ProjectToNav(GetWorld(), MyPawn->GetActorLocation() + Away.RotateAngleAxis(Turn, FVector::UpVector) * Distance, Projected))
		{
			HideGoal = Projected;
			break;
		}
	}
	SetState(NewState);
	HideUntil = GetNow() + Duration;
	RevealUntil = 0.0;
	Victim.Reset();
	ClearFocus(EAIFocusPriority::Gameplay);
	SetShadeGoal(HideGoal, Duration);
}

void ANoFearShadeAIController::UpdateHide()
{
	const double Now = GetNow();
	if (Now >= HideUntil)
	{
		NextStrikeTime = Now + StrikeCooldown;
		SetState(ENoFearShadeState::Stalk);
		return;
	}
	SetShadeGoal(HideGoal, 0.6f);
}

void ANoFearShadeAIController::HandleDamaged(UNoFearHealthComponent* Component, const FNoFearDamageInfo& Info)
{
	APawn* MyPawn = GetPawn();
	if (!MyPawn || !Component || Component->IsDead())
	{
		return;
	}
	const double Now = GetNow();
	RevealUntil = Now + RevealOnHurtTime;
	RecentDamage.Add(TPair<double, float>(Now, Info.Amount));
	RecentDamage.RemoveAll([this, Now](const TPair<double, float>& Entry) { return Now - Entry.Key > FleeWindow; });
	float Lost = 0.f;
	for (const TPair<double, float>& Entry : RecentDamage)
	{
		Lost += Entry.Value;
	}
	if (State != ENoFearShadeState::Flee && Lost > FleeHealthFraction * Component->GetMaxHealth())
	{
		// Много урона подряд — бежит прочь от обидчика и прячется.
		++FleeCount;
		RecentDamage.Reset();
		const AActor* Offender = Info.InstigatorController ? Info.InstigatorController->GetPawn() : Info.DamageCauser.Get();
		const FVector From = Offender ? Offender->GetActorLocation() : MyPawn->GetActorLocation() - MyPawn->GetActorForwardVector() * 100.f;
		BeginHide(ENoFearShadeState::Flee, From, FleeDistance, FleeHideTime);
	}
}

// --- Плащ ---

void ANoFearShadeAIController::UpdateCloak(float DeltaTime)
{
	const APawn* MyPawn = GetPawn();
	if (!MyPawn)
	{
		return;
	}
	float Wanted;
	if (UNoFearHealthComponent::IsActorDead(MyPawn))
	{
		Wanted = RevealedOpacity;
	}
	else if (IsVanished())
	{
		Wanted = 0.f;
	}
	else if (IsRevealed())
	{
		Wanted = RevealedOpacity;
	}
	else
	{
		Wanted = MyPawn->GetVelocity().Size2D() > 20.f ? MovingOpacity : StandingOpacity;
	}
	// Проявление на замахе — сразу, остальное плавно.
	Opacity = State == ENoFearShadeState::Strike ? Wanted : FMath::FInterpConstantTo(Opacity, Wanted, DeltaTime, OpacityFadeSpeed);
	ApplyOpacity();
}

void ANoFearShadeAIController::ApplyOpacity()
{
	const ACharacter* Body = Cast<ACharacter>(GetPawn());
	USkeletalMeshComponent* Mesh = Body ? Body->GetMesh() : nullptr;
	for (const TWeakObjectPtr<UMaterialInstanceDynamic>& Instance : CloakInstances)
	{
		if (UMaterialInstanceDynamic* Material = Instance.Get())
		{
			Material->SetScalarParameterValue(TEXT("Opacity"), Opacity);
		}
	}
	// Тень от тела выдаёт её — только проявленная отбрасывает тень.
	const bool bShadow = Opacity > 0.5f;
	if (Mesh && Mesh->CastShadow != bShadow)
	{
		Mesh->SetCastShadow(bShadow);
	}
}

// --- Ложные звуки ---

void ANoFearShadeAIController::UpdateFakeSounds()
{
	if (GetNow() >= NextFakeSoundTime)
	{
		PlayFakeSoundNow();
	}
}

bool ANoFearShadeAIController::PlayFakeSoundNow()
{
	const APawn* MyPawn = GetPawn();
	NextFakeSoundTime = GetNow() + FMath::FRandRange(FakeSoundInterval.X, FakeSoundInterval.Y);
	FVector Center;
	if (!MyPawn || !GetGroupCenter(Center))
	{
		return false;
	}
	// В стороне от тени: от направления на неё повернуть на 90–150° в случайную сторону.
	FVector ToShade = (MyPawn->GetActorLocation() - Center).GetSafeNormal2D();
	if (ToShade.IsNearlyZero())
	{
		ToShade = FVector::ForwardVector;
	}
	const float Sign = FMath::RandBool() ? 1.f : -1.f;
	FVector Location = Center;
	for (int32 Attempt = 0; Attempt < 6; ++Attempt)
	{
		const FVector Dir = ToShade.RotateAngleAxis(Sign * FMath::FRandRange(90.f, 150.f), FVector::UpVector);
		FVector Projected;
		if (NoFearWhisperer::ProjectToNav(GetWorld(), Center + Dir * FMath::FRandRange(FakeSoundDistance.X, FakeSoundDistance.Y), Projected))
		{
			Location = Projected;
			break;
		}
	}
	PlayFakeSound(Location, FMath::RandRange(0, 2));
	return true;
}

void ANoFearShadeAIController::PlayFakeSound(FVector Location, int32 Kind)
{
	APawn* MyPawn = GetPawn();
	UWorld* World = GetWorld();
	if (!MyPawn || !World)
	{
		return;
	}
	++FakeSoundCount;
	LastFakeSoundLocation = Location;

	// Герой слышит звук на месте, без меток.
	const TSoftObjectPtr<USoundBase>& SoundRef = Kind == 0 ? CrySound : Kind == 1 ? StepsSound : ShotSound;
	NoFearWhisperer::PlayLimitedSound(this, SoundRef.LoadSynchronous(), Location, Kind == 0 ? CryPitch : 1.f, CryDuration);

	// Боты-люди идут проверить; из собранного отряда — один боец.
	LastFakeSoundCheckers = 0;
	const UNoFearSquadSubsystem* Squads = UNoFearSquadSubsystem::Get(this);
	for (ANoFearCharacter* Human : TActorRange<ANoFearCharacter>(World))
	{
		ANoFearAIController* Bot = Human ? Cast<ANoFearAIController>(Human->GetController()) : nullptr;
		if (!Bot || Cast<ANoFearMonsterAIController>(Bot) || UNoFearHealthComponent::IsActorIncapacitated(Human)
			|| UNoFearTeamLibrary::AreFriendly(MyPawn, Human)
			|| FVector::DistSquared(Human->GetActorLocation(), Location) > FMath::Square(FakeSoundHearing))
		{
			continue;
		}
		if (Squads && Squads->IsInFormedSquad(Human) && Squads->PickNoiseChecker(Human, Location) != Human)
		{
			continue;
		}
		if (Bot->CheckNoise(Location, FakeSoundCheckTime))
		{
			++LastFakeSoundCheckers;
		}
	}
}
