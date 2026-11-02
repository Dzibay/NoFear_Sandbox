#include "AI/NoFearMonsterAIController.h"

#include "AI/NoFearBotSubsystem.h"
#include "AI/NoFearMeleeComponent.h"
#include "AI/NoFearMonsterAttack.h"
#include "AI/NoFearMonsterKinds.h"
#include "EngineUtils.h"
#include "AI/NoFearOpportunitySubsystem.h"
#include "AI/NoFearTankShake.h"
#include "AI/NoFearVulnerabilityComponent.h"
#include "Animation/NoFearPoseComponent.h"
#include "BehaviorTree/BehaviorTree.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "Components/PointLightComponent.h"
#include "Core/NoFearCharacter.h"
#include "Core/NoFearFearComponent.h"
#include "Core/NoFearHealthComponent.h"
#include "Core/NoFearTeam.h"
#include "NavigationSystem.h"
#include "Portal/NoFearPortalSubsystem.h"
#include "Perception/AISenseConfig_Hearing.h"
#include "Perception/AISenseConfig_Sight.h"
#include "TimerManager.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Sound/SoundBase.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	/** Ближе этого к цели монстр без жетона кружит шагом боком, а не бежит. */
	constexpr float CircleStanceDistance = 900.f;
	/** Ближе этого — только что ударил и отходит на кольцо: пятится бегом, не сводя глаз с цели. */
	constexpr float CircleBackOffDistance = 330.f;
	/** Монстр ближе этого к своему месту в кольце стаи — стоит на нём и кружит, а не бежит к нему, см. */
	constexpr float PackSlotStanceDistance = 350.f;
	/** Точка стаи дальше этого — догоняет бегом, а не шагом, см. */
	constexpr float PackGoalRunDistance = 600.f;
	/** Самооборона не кончается, пока цель не отошла на столько дальше SelfDefenseDistance, см. */
	constexpr float SelfDefenseHysteresis = 200.f;

	/** Звуки попадания загонщика (заглушки: удар по телу из Lyra). */
	const TCHAR* HunterHitSounds[] = {
		TEXT("/Game/External/LyraContent/Audio/Sounds/Impacts/Lyra_Plyr_BulletImpact_01.Lyra_Plyr_BulletImpact_01"),
		TEXT("/Game/External/LyraContent/Audio/Sounds/Impacts/Lyra_Plyr_BulletImpact_02.Lyra_Plyr_BulletImpact_02"),
		TEXT("/Game/External/LyraContent/Audio/Sounds/Impacts/Lyra_Plyr_BulletImpact_03.Lyra_Plyr_BulletImpact_03"),
		TEXT("/Game/External/LyraContent/Audio/Sounds/Impacts/Lyra_Plyr_BulletImpact_04.Lyra_Plyr_BulletImpact_04"),
		TEXT("/Game/External/LyraContent/Audio/Sounds/Impacts/Lyra_Plyr_BulletImpact_05.Lyra_Plyr_BulletImpact_05"),
		TEXT("/Game/External/LyraContent/Audio/Sounds/Impacts/Lyra_Plyr_BulletImpact_06.Lyra_Plyr_BulletImpact_06"),
	};
	/** Ложный выпад: только если цель на таком расстоянии, см; бросок длится столько, сек. */
	constexpr float FakeLungeMinDistance = 250.f;
	constexpr float FakeLungeMaxDistance = 750.f;
	constexpr float FakeLungeDuration = 0.35f;

	/** Курс вожака гуляет на ±столько градусов раз в 6–10 секунд. */
	constexpr float RoamTurnAngle = 50.f;
	constexpr float RoamTurnMinInterval = 6.f;
	constexpr float RoamTurnMaxInterval = 10.f;
	/** Как часто пересчитывать точку, вокруг которой бродить, сек. */
	constexpr double RoamCenterInterval = 1.0;
	const FVector RoamProjectExtent(300.f, 300.f, 500.f);

	/** Страж ближе этого к посту — стоит на нём и смотрит наружу, см. */
	constexpr float GuardPostTolerance = 150.f;
	/** Страж бьёт обидчика, отошедшего от портала не дальше Radius + столько, см. */
	constexpr float GuardGrudgeSlack = 600.f;
	/** Прибавка к оценке цели стража: тот, кто освобождает проводника. */
	constexpr float GuardFreerBonus = 3.f;
}

ANoFearMonsterAIController::ANoFearMonsterAIController(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// Бежит к цели лицом по ходу; прицела и стрейфа у монстра нет.
	bStrafeInCombat = false;
	bWalkWhenCalm = true;
	// Потеряв цель, заглядывает и в шкафы рядом.
	bSearchHideSpots = true;
	LeashRadius = 0.f;
	// Дома нет: цель, о которой знает стая, гонит далеко и помнит долго.
	MaxTargetDistance = 8000.f;
	TargetMemoryTime = 15.f;

	// Зовёт своих, когда на него напали (NotifyAttack). Стае о цели сообщает UNoFearPackSubsystem.
	AlertShareRadius = 3000.f;

	// Видит хуже стрелков, зато слышит дальше и острее.
	SightConfig->SightRadius = 1800.f;
	SightConfig->LoseSightRadius = 2300.f;
	SightConfig->PeripheralVisionAngleDegrees = 70.f;
	HearingConfig->HearingRange = 5000.f;
	HearingRangeScale = 1.75f;
	FootstepAwarenessGain = 0.45f;
	FootstepAwarenessCap = 0.95f;

	static ConstructorHelpers::FObjectFinderOptional<UBehaviorTree> Tree(TEXT("/Game/AI/BT_NoFear_Monster.BT_NoFear_Monster"));
	BehaviorTree = Tree.Get();

	// Загонщик (и бешеный): голова ×1.5; окно после удара — у компонента уязвимости.
	FNoFearWeakZone Head;
	Head.Name = TEXT("Head");
	Head.Bones = { TEXT("head") };
	Head.DamageMultiplier = 1.5f;
	WeakZones.Add(Head);

	OpportunityBonuses.Add(ENoFearOpportunity::KnockedDown, 1.f);
	OpportunityBonuses.Add(ENoFearOpportunity::Blinded, 0.8f);
	OpportunityBonuses.Add(ENoFearOpportunity::Marked, 1.5f);

	AggroTargetWeights.Hero = 0.5f;
	AggroTargetWeights.Companion = 0.5f;
	AggroTargetWeights.Soldier = 0.5f;
	AggroTargetWeights.Civilian = 0.5f;
	AggroTargetWeights.Monster = 0.5f;
}

FName ANoFearMonsterAIController::GetKindName() const
{
	return IsAggro() ? FName(TEXT("Rabid")) : KindName;
}

bool ANoFearMonsterAIController::IsExhaling() const
{
	const UNoFearVulnerabilityComponent* Vulnerability = UNoFearVulnerabilityComponent::Find(GetPawn());
	return Vulnerability && Vulnerability->IsWindowOpen(ENoFearWeakCondition::Exhale);
}

float ANoFearMonsterAIController::GetTargetScore(AActor* Candidate) const
{
	if (!Candidate || !GetPawn())
	{
		return 0.f;
	}
	const FNoFearKnownActor* Knowledge = KnownActors.Find(Candidate);
	return ScoreTarget(Candidate, Knowledge ? *Knowledge : FNoFearKnownActor());
}

UNoFearMeleeComponent* ANoFearMonsterAIController::GetMeleeComponent() const
{
	return GetPawn() ? GetPawn()->FindComponentByClass<UNoFearMeleeComponent>() : nullptr;
}

bool ANoFearMonsterAIController::IsAggro() const
{
	return GetPawn() && UNoFearTeamLibrary::GetTeam(GetPawn()) == ENoFearTeam::AggroMonster;
}

void ANoFearMonsterAIController::OnPossess(APawn* InPawn)
{
	if (InPawn && bAddMeleeComponentIfMissing && !InPawn->FindComponentByClass<UNoFearMeleeComponent>())
	{
		UNoFearMeleeComponent* Melee = NewObject<UNoFearMeleeComponent>(InPawn, TEXT("MeleeComponent"));
		Melee->RegisterComponent();
	}

	Grudges.Reset();
	DefendTarget.Reset();
	DefendUntil = 0.0;
	RageUntil = 0.0;
	WhisperBuffUntil = 0.0;
	bBuffDilation = false;
	bHasAttackToken = false;
	TokenTarget.Reset();
	bSelfDefense = false;
	bRoaring = false;
	RoamHeading = InPawn ? InPawn->GetActorForwardVector().GetSafeNormal2D() : FVector::ForwardVector;
	NextRoamTurnTime = 0.0;
	NextRoamCenterTime = 0.0;

	ConfiguredMelee.Reset();
	NextFakeLungeTime = 0.0;
	GetWorldTimerManager().ClearTimer(FakeLungeTimer);

	Super::OnPossess(InPawn);
	ConfigureHunterBody(InPawn);

	if (UNoFearMeleeComponent* Melee = GetMeleeComponent())
	{
		Melee->OnAttackStarted.AddUniqueDynamic(this, &ANoFearMonsterAIController::HandleAttackStarted);
		Melee->OnAttackFinished.AddUniqueDynamic(this, &ANoFearMonsterAIController::HandleAttackFinished);
		ConfigureMelee(Melee);
		ConfiguredMelee = Melee;
	}
	if (UNoFearHealthComponent* Health = UNoFearHealthComponent::FindHealth(InPawn))
	{
		Health->OnDamaged.AddUniqueDynamic(this, &ANoFearMonsterAIController::HandleBodyDamaged);
		BoundBodyHealth = Health;
	}

	// Слабые места и окна вида.
	ChainedAttacks = 0;
	LastAttackFinishTime = -1.0e9;
	if (InPawn)
	{
		UNoFearVulnerabilityComponent* Vulnerability = InPawn->FindComponentByClass<UNoFearVulnerabilityComponent>();
		if (!Vulnerability && bAddVulnerabilityIfMissing)
		{
			Vulnerability = NewObject<UNoFearVulnerabilityComponent>(InPawn, TEXT("VulnerabilityComponent"));
			Vulnerability->Zones = WeakZones;
			Vulnerability->RegisterComponent();
		}
		else if (Vulnerability)
		{
			Vulnerability->SetZones(WeakZones);
		}
	}
}

void ANoFearMonsterAIController::ConfigureMelee(UNoFearMeleeComponent* Melee)
{
	if (Melee && !AttackSet.IsEmpty())
	{
		Melee->SetAttacks(ObjectPtrDecay(AttackSet));
	}
	ConfigureHunterMelee(Melee);
}

bool ANoFearMonsterAIController::IsHunterKind() const
{
	return GetKindName() == FName(TEXT("Hunter"));
}

void ANoFearMonsterAIController::ConfigureHunterBody(APawn* InPawn)
{
	if (!InPawn || !IsHunterKind() || HunterMaxHealth <= 0.f)
	{
		return;
	}
	if (UNoFearHealthComponent* Health = UNoFearHealthComponent::FindHealth(InPawn); Health && !Health->IsDead())
	{
		// Целое тело остаётся целым при новом максимуме; раненое сохраняет долю здоровья.
		const float Fraction = Health->GetMaxHealth() > 0.f ? Health->GetHealthFraction() : 1.f;
		Health->MaxHealth = HunterMaxHealth;
		Health->SetHealth(HunterMaxHealth * Fraction);
	}
}

void ANoFearMonsterAIController::ConfigureHunterMelee(UNoFearMeleeComponent* Melee)
{
	if (!Melee || !IsHunterKind() || !AttackSet.IsEmpty())
	{
		return;
	}
	Melee->AddAttack(GetDefault<UNoFearMonsterAttack_Combo>());
	if (UNoFearMonsterAttack* Swing = Melee->FindAttack(TEXT("Swing")))
	{
		Swing->FeintChance = HunterFeintChance;
		if (Swing->KnockbackSpeed <= 0.f)
		{
			Swing->KnockbackSpeed = 250.f;
		}
	}
	for (UNoFearMonsterAttack* Attack : Melee->Attacks)
	{
		if (!Attack)
		{
			continue;
		}
		if (!Attack->HitSound)
		{
			Attack->HitSound = LoadObject<USoundBase>(nullptr, HunterHitSounds[FMath::RandHelper(UE_ARRAY_COUNT(HunterHitSounds))]);
		}
		if (!Attack->HitShake)
		{
			Attack->HitShake = UNoFearHunterHitShake::StaticClass();
		}
	}
}

void ANoFearMonsterAIController::UpdateNearWhisperer()
{
	const APawn* MyPawn = GetPawn();
	if (!MyPawn || !IsHunterKind())
	{
		NearWhisperer.Reset();
		return;
	}
	const double Now = GetNow();
	if (Now < NextWhispererCheckTime)
	{
		return;
	}
	NextWhispererCheckTime = Now + 1.0;
	NearWhisperer.Reset();
	const float WhispererSearchRadius = FMath::Max(HerdToWhispererRadius, HypnosisCoverRadius);
	float BestDistSq = FMath::Square(WhispererSearchRadius);
	for (TActorIterator<ANoFearWhispererAIController> It(GetWorld()); It; ++It)
	{
		const APawn* WhispererPawn = It->GetPawn();
		if (!WhispererPawn || UNoFearHealthComponent::IsActorDead(WhispererPawn))
		{
			continue;
		}
		const float DistSq = FVector::DistSquared(MyPawn->GetActorLocation(), WhispererPawn->GetActorLocation());
		if (DistSq <= BestDistSq)
		{
			BestDistSq = DistSq;
			NearWhisperer = *It;
		}
	}
}

bool ANoFearMonsterAIController::IsHypnosisThreat(const AActor* Candidate) const
{
	const ANoFearWhispererAIController* Whisperer = NearWhisperer.Get();
	const APawn* MyPawn = GetPawn();
	const APawn* WhispererPawn = Whisperer ? Whisperer->GetPawn() : nullptr;
	if (!Candidate || !MyPawn || !WhispererPawn || !Whisperer->IsHypnotizing() || HypnosisCoverBonus <= 0.f
		|| FVector::DistSquared(MyPawn->GetActorLocation(), WhispererPawn->GetActorLocation()) > FMath::Square(HypnosisCoverRadius))
	{
		return false;
	}
	const APawn* Victim = Whisperer->GetHuntVictim();
	if (Candidate == Victim || Candidate == WhispererPawn)
	{
		return false;
	}
	if (Whisperer->HasGrudgeAgainst(Candidate) || Whisperer->GetDefendTarget() == Candidate)
	{
		return true;
	}
	return Victim && FVector::DistSquared(Candidate->GetActorLocation(), Victim->GetActorLocation()) <= FMath::Square(HypnosisCoverVictimDistance);
}

void ANoFearMonsterAIController::UpdateFakeLunge()
{
	ACharacter* Body = Cast<ACharacter>(GetPawn());
	const AActor* Target = CurrentTarget.Get();
	UNoFearMeleeComponent* Melee = GetMeleeComponent();
	if (!Body || !Target || !Melee || !IsHunterKind() || FakeLungeMaxInterval <= 0.f || bHasAttackToken || IsPackRaging()
		|| GetWorldTimerManager().IsTimerActive(FakeLungeTimer))
	{
		return;
	}
	const double Now = GetNow();
	if (NextFakeLungeTime <= 0.0)
	{
		NextFakeLungeTime = Now + FMath::FRandRange(FakeLungeMinInterval, FakeLungeMaxInterval);
		return;
	}
	if (Now < NextFakeLungeTime || !IsCircling() || !Melee->CanAttack())
	{
		return;
	}
	const float Distance = FVector::Dist2D(Body->GetActorLocation(), Target->GetActorLocation());
	if (Distance < FakeLungeMinDistance || Distance > FakeLungeMaxDistance)
	{
		return;
	}
	// Бросок к цели без удара: игрок дёргается, а монстр отскакивает обратно в кольцо.
	NextFakeLungeTime = Now + FMath::FRandRange(FakeLungeMinInterval, FakeLungeMaxInterval);
	++FakeLungeCount;
	const FVector Dir = (Target->GetActorLocation() - Body->GetActorLocation()).GetSafeNormal2D();
	Body->SetActorRotation(Dir.Rotation());
	Body->LaunchCharacter(Dir * FakeLungeSpeed + FVector::UpVector * 120.f, true, true);
	if (UNoFearPoseComponent* Pose = Body->FindComponentByClass<UNoFearPoseComponent>())
	{
		Pose->PlayPose(ENoFearPose::Lunge, FakeLungeDuration, 0.8f);
	}
	Melee->BlockAttacksFor(FakeLungeDuration + 0.2f);
	GetWorldTimerManager().SetTimer(FakeLungeTimer, this, &ANoFearMonsterAIController::EndFakeLunge, FakeLungeDuration, false);
}

void ANoFearMonsterAIController::EndFakeLunge()
{
	ACharacter* Body = Cast<ACharacter>(GetPawn());
	if (!Body || UNoFearHealthComponent::IsActorDead(Body))
	{
		return;
	}
	FVector Away = -Body->GetActorForwardVector().GetSafeNormal2D();
	if (const AActor* Target = CurrentTarget.Get())
	{
		Away = (Body->GetActorLocation() - Target->GetActorLocation()).GetSafeNormal2D();
	}
	Body->LaunchCharacter(Away * FakeLungeBackSpeed + FVector::UpVector * 100.f, true, true);
}

float ANoFearMonsterAIController::GetTargetDistance2D() const
{
	const APawn* MyPawn = GetPawn();
	const AActor* Target = CurrentTarget.Get();
	return MyPawn && Target ? FVector::Dist2D(MyPawn->GetActorLocation(), Target->GetActorLocation()) : UE_BIG_NUMBER;
}

void ANoFearMonsterAIController::OnUnPossess()
{
	if (UNoFearBotSubsystem* Subsystem = UNoFearBotSubsystem::Get(this); Subsystem && GetPawn())
	{
		Subsystem->ReleaseAllAttackTokens(GetPawn());
	}
	TokenTarget.Reset();
	if (UNoFearMeleeComponent* Melee = GetMeleeComponent())
	{
		Melee->OnAttackStarted.RemoveDynamic(this, &ANoFearMonsterAIController::HandleAttackStarted);
		Melee->OnAttackFinished.RemoveDynamic(this, &ANoFearMonsterAIController::HandleAttackFinished);
	}
	if (UNoFearHealthComponent* Health = BoundBodyHealth.Get())
	{
		Health->OnDamaged.RemoveDynamic(this, &ANoFearMonsterAIController::HandleBodyDamaged);
	}
	BoundBodyHealth.Reset();
	GetWorldTimerManager().ClearTimer(RoarTimer);
	GetWorldTimerManager().ClearTimer(FakeLungeTimer);
	bRoaring = false;
	Super::OnUnPossess();
}

// --- Обновление ---

void ANoFearMonsterAIController::UpdateBot()
{
	APawn* MyPawn = GetPawn();
	if (!MyPawn || UNoFearHealthComponent::IsActorDead(MyPawn))
	{
		if (MyPawn && bHasAttackToken)
		{
			if (UNoFearBotSubsystem* Subsystem = UNoFearBotSubsystem::Get(this))
			{
				Subsystem->ReleaseAllAttackTokens(MyPawn);
			}
			bHasAttackToken = false;
		}
		if (UPointLightComponent* Glow = BuffGlow.Get())
		{
			Glow->SetVisibility(false);
		}
		return;
	}

	UpdateGrudges();

	if (UNoFearMeleeComponent* Melee = GetMeleeComponent())
	{
		Melee->CooldownScale = IsAggro() ? AggroCooldownScale : 1.f;
		const bool bBuffed = IsWhisperBuffed();
		Melee->CooldownScale *= bBuffed ? WhisperBuffCooldownScale : 1.f;
		Melee->DamageScale = bBuffed ? WhisperBuffDamageScale : 1.f;
		// Бафф и ярость стаи ускоряют тело вне атак (колосс сам пишет время тела каждый кадр, см. его Tick).
		const bool bRaging = IsPackRaging();
		float Dilation = 1.f;
		if (!Melee->IsAttacking())
		{
			Dilation *= bBuffed ? WhisperBuffSpeedScale : 1.f;
			Dilation *= bRaging ? RageSpeedScale : 1.f;
		}
		if (bBuffed || bRaging || bBuffDilation)
		{
			MyPawn->CustomTimeDilation = Dilation;
			bBuffDilation = bBuffed || bRaging;
		}
		// Фиолетовый отсвет баффа.
		UPointLightComponent* Glow = BuffGlow.Get();
		if (bBuffed && !Glow)
		{
			Glow = NewObject<UPointLightComponent>(MyPawn, NAME_None, RF_Transient);
			Glow->SetLightColor(FLinearColor(0.55f, 0.1f, 1.f));
			Glow->SetIntensity(2500.f);
			Glow->SetAttenuationRadius(260.f);
			Glow->SetCastShadows(false);
			Glow->SetupAttachment(MyPawn->GetRootComponent());
			Glow->SetRelativeLocation(FVector(0.f, 0.f, 40.f));
			Glow->RegisterComponent();
			BuffGlow = Glow;
		}
		if (Glow && Glow->IsVisible() != bBuffed)
		{
			Glow->SetVisibility(bBuffed);
		}
		// Компонент мог появиться (или смениться) уже после OnPossess.
		if (!Melee->OnAttackFinished.IsAlreadyBound(this, &ANoFearMonsterAIController::HandleAttackFinished))
		{
			Melee->OnAttackFinished.AddDynamic(this, &ANoFearMonsterAIController::HandleAttackFinished);
		}
		if (!Melee->OnAttackStarted.IsAlreadyBound(this, &ANoFearMonsterAIController::HandleAttackStarted))
		{
			Melee->OnAttackStarted.AddDynamic(this, &ANoFearMonsterAIController::HandleAttackStarted);
		}
		if (ConfiguredMelee.Get() != Melee)
		{
			ConfigureMelee(Melee);
			ConfiguredMelee = Melee;
		}
	}

	Super::UpdateBot();

	UpdateScare();
	UpdateAttackToken();
	UpdateFakeLunge();
	// Жетон и точка испуга могли смениться после того, как база записала блэкборд.
	SetBlackboardBool(FNoFearBotKeys::HasAttackToken, bHasAttackToken);
	if (bScaring)
	{
		WriteBlackboard();
	}
	UpdateGuardFacing();
}

void ANoFearMonsterAIController::UpdateScare()
{
	APawn* MyPawn = GetPawn();
	AActor* Target = CurrentTarget.Get();
	UpdateNearWhisperer();
	const bool bCivilianTarget = MyPawn && Target && !IsAggro() && !HasGrudgeAgainst(Target)
		&& UNoFearOpportunitySubsystem::GetTargetCategory(Target) == ENoFearTargetCategory::Civilian;
	// Загон к шептуну: мирного рядом с шептуном (и его жертву) не бьёт, а гонит к нему с дальней стороны.
	const ANoFearWhispererAIController* Whisperer = NearWhisperer.Get();
	const APawn* WhispererPawn = Whisperer ? Whisperer->GetPawn() : nullptr;
	bHerding = bCivilianTarget && WhispererPawn && HerdToWhispererRadius > 0.f
		&& FVector::DistSquared(MyPawn->GetActorLocation(), WhispererPawn->GetActorLocation()) <= FMath::Square(HerdToWhispererRadius);
	bScaring = bCivilianTarget && (bScareCivilians || bHerding);
	if (!bScaring)
	{
		return;
	}
	// Наскок: рык в лицо, человек пугается напрямую (такой испуг ломает и передаётся соседям).
	const double Now = GetNow();
	if (Now >= NextScareTime && FVector::DistSquared2D(MyPawn->GetActorLocation(), Target->GetActorLocation()) <= FMath::Square(ScareReach))
	{
		if (UNoFearFearComponent* Fear = UNoFearFearComponent::FindFear(Target))
		{
			Fear->AddFearFrom(ScareFear, 100.f, MyPawn->GetActorLocation(), MyPawn, true);
		}
		if (const ANoFearCharacter* MyCharacter = Cast<ANoFearCharacter>(MyPawn))
		{
			if (UNoFearPoseComponent* Pose = MyCharacter->GetPoseComponent())
			{
				Pose->PlayPose(ENoFearPose::Lunge, 0.25f, 0.4f);
			}
		}
		NextScareTime = Now + ScareInterval;
		++ScareCount;
	}
}

void ANoFearMonsterAIController::UpdateGuardFacing()
{
	const APawn* MyPawn = GetPawn();
	if (!bPortalGuard || !MyPawn || CurrentTarget.IsValid()
		|| FVector::DistSquared2D(MyPawn->GetActorLocation(), GuardPost) > FMath::Square(GuardPostTolerance))
	{
		return;
	}
	const FVector Outward = (GuardPost - GuardCenter).GetSafeNormal2D();
	SetFocalPoint(GuardPost + Outward * 1000.f + FVector(0.f, 0.f, 60.f));
}

bool ANoFearMonsterAIController::CanJoinPack() const
{
	return bJoinsPack && !IsAggro() && !bPortalGuard;
}

void ANoFearMonsterAIController::SetPortalGuard(const FVector& Center, const FVector& Post, float Radius)
{
	bPortalGuard = true;
	GuardCenter = Center;
	GuardPost = Post;
	GuardRadius = Radius;
}

void ANoFearMonsterAIController::ClearPortalGuard()
{
	if (bPortalGuard)
	{
		bPortalGuard = false;
		ClearFocus(EAIFocusPriority::Gameplay);
	}
}

bool ANoFearMonsterAIController::AcquireToken(AActor* Target)
{
	UNoFearBotSubsystem* Subsystem = UNoFearBotSubsystem::Get(this);
	if (!Subsystem)
	{
		return true;
	}
	if (!Subsystem->RequestAttackToken(GetPawn(), Target))
	{
		return false;
	}
	TokenTarget = Target;
	return true;
}

void ANoFearMonsterAIController::UpdateGrudges()
{
	const double Now = GetNow();
	for (auto It = Grudges.CreateIterator(); It; ++It)
	{
		if (!It.Key().IsValid() || UNoFearHealthComponent::IsActorDead(It.Key().Get()) || Now - It.Value() > GrudgeTime)
		{
			It.RemoveCurrent();
		}
	}
	if (IsAggro())
	{
		return;
	}

	for (TPair<TWeakObjectPtr<AActor>, FNoFearKnownActor>& Pair : KnownActors)
	{
		AActor* Actor = Pair.Key.Get();
		if (!Actor)
		{
			continue;
		}
		// Кто нападал — тот обидчик.
		if (Now - Pair.Value.LastAttackedTime <= 1.0)
		{
			Grudges.Add(Pair.Key, Now);
		}
		// Обидчика узнаёт с первого взгляда.
		if (Pair.Value.bInView && Grudges.Contains(Pair.Key))
		{
			Pair.Value.Awareness = 1.f;
		}
	}
}

void ANoFearMonsterAIController::ReleaseToken()
{
	if (TokenTarget.IsValid())
	{
		if (UNoFearBotSubsystem* Subsystem = UNoFearBotSubsystem::Get(this))
		{
			Subsystem->ReleaseAttackToken(GetPawn(), TokenTarget.Get());
		}
	}
	TokenTarget.Reset();
}

void ANoFearMonsterAIController::UpdateAttackToken()
{
	AActor* Target = CurrentTarget.Get();

	if (TokenTarget.IsValid() && TokenTarget.Get() != Target)
	{
		ReleaseToken();
	}
	if (!Target)
	{
		bHasAttackToken = false;
		bSelfDefense = false;
		return;
	}
	if (IsAggro())
	{
		ReleaseToken();
		bHasAttackToken = true;
		return;
	}
	// Человека гонит и пугает, а не бьёт.
	if (bScaring)
	{
		ReleaseToken();
		bHasAttackToken = false;
		bSelfDefense = false;
		return;
	}

	// Добивание: сбитого с ног рядом бьёт без очереди и без волны стаи.
	if (bFinishKnockedDown && GetTargetDistance2D() <= FinishDistance
		&& UNoFearOpportunitySubsystem::HasTag(Target, ENoFearOpportunity::KnockedDown))
	{
		bHasAttackToken = true;
		return;
	}

	// В стае очередь решает стая: бьют только окружив цель, своей волной. Кого цель бьёт рядом — отвечает без очереди.
	// Ближний бой и в волне, и в самообороне — по жетону: на одну цель вплотную не больше MaxAttackersPerTarget.
	FNoFearPackMemberView View;
	if (GetPackView(View) && View.PackSize >= 2 && (View.Target == Target || View.State == ENoFearPackState::Rage))
	{
		// Ярость: убийцу вожака бьют все разом, без очереди и жетона.
		if (View.State == ENoFearPackState::Rage && View.Target == Target)
		{
			ReleaseToken();
			bSelfDefense = false;
			bHasAttackToken = true;
			return;
		}
		const FNoFearKnownActor* Knowledge = KnownActors.Find(Target);
		const bool bHitByTarget = Knowledge && GetNow() - Knowledge->LastAttackedTime <= SelfDefenseTime;
		const float DefenseDistance = SelfDefenseDistance + (bSelfDefense ? SelfDefenseHysteresis : 0.f);
		bSelfDefense = bHitByTarget && GetTargetDistance2D() <= DefenseDistance;
		bool bWantsMelee = false;
		switch (View.State)
		{
		case ENoFearPackState::Encircle:
		case ENoFearPackState::Attack:
			if (!IsRingFighter())
			{
				// Прыгун и плевун в кольцо не встают и жетон не занимают: бьют, когда сами решат.
				ReleaseToken();
				bHasAttackToken = bSelfDefense || WantsFreeAttack(Target);
				return;
			}
			bWantsMelee = View.bAttackTurn || bSelfDefense;
			break;
		default:
			bWantsMelee = bSelfDefense;
			break;
		}
		bHasAttackToken = bWantsMelee && AcquireToken(Target);
		if (!bHasAttackToken)
		{
			ReleaseToken();
		}
		return;
	}

	// Одиночка (или бьёт не цель стаи): боец кольца — по жетону, прыгун и плевун — сами по себе.
	bSelfDefense = false;
	if (!IsRingFighter())
	{
		ReleaseToken();
		bHasAttackToken = WantsFreeAttack(Target);
		return;
	}
	bHasAttackToken = AcquireToken(Target);
}

void ANoFearMonsterAIController::HandleAttackStarted(AActor* Target)
{
	// Тело GASP поворачивается только к фокусу и только боком: на замахе смотрим на цель.
	if (Target && !IsPortalControlled())
	{
		SetFocus(Target);
	}
	UpdateMovementStance();
}

bool ANoFearMonsterAIController::IsFacingAttack() const
{
	const UNoFearMeleeComponent* Melee = GetMeleeComponent();
	if (!Melee || !Melee->IsAttacking())
	{
		return false;
	}
	const UNoFearMonsterAttack* Attack = Melee->GetCurrentAttack();
	return Melee->IsInWindup()
		|| !(Attack && (Attack->IsA<UNoFearMonsterAttack_Charge>() || Attack->IsA<UNoFearMonsterAttack_Leap>() || Attack->IsA<UNoFearMonsterAttack_Lunge>()));
}

void ANoFearMonsterAIController::HandleAttackFinished(AActor* Target)
{
	OnMeleeAttackFinished(Target);
	UpdateMovementStance();

	if (IsAggro())
	{
		// Выдох: после серии ударов подряд стоит и тяжело дышит — окно уязвимости, атак нет.
		const double Now = GetNow();
		ChainedAttacks = Now - LastAttackFinishTime <= ExhaleChainGap ? ChainedAttacks + 1 : 1;
		LastAttackFinishTime = Now;
		if (ExhaleAfterAttacks > 0 && ChainedAttacks >= ExhaleAfterAttacks)
		{
			ChainedAttacks = 0;
			if (UNoFearVulnerabilityComponent* Vulnerability = UNoFearVulnerabilityComponent::Find(GetPawn()))
			{
				Vulnerability->OpenWindow(ENoFearWeakCondition::Exhale, ExhaleTime);
			}
			if (UNoFearMeleeComponent* Melee = GetMeleeComponent())
			{
				Melee->BlockAttacksFor(ExhaleTime);
			}
			StopMovement();
		}
		return;
	}

	FNoFearPackMemberView View;
	if (GetPackView(View) && View.PackSize >= 2)
	{
		if (UNoFearPackSubsystem* PackSubsystem = UNoFearPackSubsystem::Get(this))
		{
			PackSubsystem->NotifyAttackFinished(this);
		}
		// Ударил в свою очередь — назад на место в кольце, жетон — тому, кто ждёт; защищается — бьёт дальше.
		if (View.State == ENoFearPackState::Attack && IsRingFighter() && !bSelfDefense)
		{
			if (UNoFearBotSubsystem* Subsystem = TokenTarget.IsValid() ? UNoFearBotSubsystem::Get(this) : nullptr)
			{
				Subsystem->ReleaseAttackToken(GetPawn(), TokenTarget.Get(), true);
			}
			TokenTarget.Reset();
			bHasAttackToken = false;
			SetBlackboardBool(FNoFearBotKeys::HasAttackToken, false);
		}
		return;
	}

	if (!IsRingFighter() || !TokenTarget.IsValid())
	{
		return;
	}
	UNoFearBotSubsystem* Subsystem = UNoFearBotSubsystem::Get(this);
	if (!Subsystem)
	{
		return;
	}
	// Ударил — уступи жетон тому, кто ждёт. Никто не ждёт — жетон сразу вернётся.
	AActor* Held = TokenTarget.Get();
	Subsystem->ReleaseAttackToken(GetPawn(), Held, true);
	bHasAttackToken = Subsystem->RequestAttackToken(GetPawn(), Held);
	if (!bHasAttackToken)
	{
		TokenTarget.Reset();
	}
	SetBlackboardBool(FNoFearBotKeys::HasAttackToken, bHasAttackToken);
}

// --- Цели ---

bool ANoFearMonsterAIController::HasGrudgeAgainst(const AActor* Actor) const
{
	return Actor && Grudges.Contains(Actor);
}

bool ANoFearMonsterAIController::IsValidTarget(const AActor* Candidate) const
{
	// Проводник питает портал — его монстры не трогают.
	if (UNoFearPortalSubsystem::IsActorFeeder(Candidate))
	{
		return false;
	}
	// Страж портала не уходит с поста: бьёт тех, кто подошёл к порталу, и обидчика, пока тот рядом.
	if (bPortalGuard && Candidate)
	{
		const float Distance = FVector::Dist2D(Candidate->GetActorLocation(), GuardCenter);
		if (Distance > GuardRadius + (HasGrudgeAgainst(Candidate) ? GuardGrudgeSlack : 0.f))
		{
			return false;
		}
	}
	// Охотник портала гонит мирных только в своих угодьях — за поводком отпускает (обидчика гонит дальше).
	if (bScareCivilians && Candidate && LeashRadius > 0.f && !HasGrudgeAgainst(Candidate)
		&& UNoFearOpportunitySubsystem::GetTargetCategory(Candidate) == ENoFearTargetCategory::Civilian
		&& FVector::DistSquared2D(Candidate->GetActorLocation(), GetHomeLocation()) > FMath::Square(LeashRadius))
	{
		return false;
	}
	if (Super::IsValidTarget(Candidate))
	{
		return true;
	}
	const APawn* MyPawn = GetPawn();
	if (IsAggro() || !Candidate || !MyPawn || Candidate == MyPawn || UNoFearHealthComponent::IsActorIncapacitated(Candidate)
		|| !UNoFearTeamLibrary::AreHostile(MyPawn, Candidate))
	{
		return false;
	}
	// Дальше MaxTargetDistance: обидчика и цель стаи гонит хоть через всю карту.
	FNoFearPackMemberView View;
	return HasGrudgeAgainst(Candidate) || (GetPackView(View) && View.Target == Candidate);
}

float ANoFearMonsterAIController::ScoreTarget(const AActor* Candidate, const FNoFearKnownActor& Knowledge) const
{
	if (IsAggro())
	{
		// Ближайший, кто бы он ни был; текущую цель держит слабо.
		const float Distance = FVector::Dist(GetPawn()->GetActorLocation(), Candidate->GetActorLocation());
		float Score = 2.f * (1.f - FMath::Clamp(Distance / FMath::Max(MaxTargetDistance, 1.f), 0.f, 1.f));
		if (Knowledge.bVisible)
		{
			Score += 0.3f;
		}
		if (Candidate == CurrentTarget.Get())
		{
			Score += AggroTargetStickiness;
		}
		Score += AggroTargetWeights.Get(UNoFearOpportunitySubsystem::GetTargetCategory(Candidate));
		return Score;
	}

	float Score = Super::ScoreTarget(Candidate, Knowledge);
	// Вкусы вида и метки возможностей (сбит с ног, слизь, помечен...).
	Score += TargetWeights.Get(UNoFearOpportunitySubsystem::GetTargetCategory(Candidate));
	Score += UNoFearOpportunitySubsystem::GetOpportunityBonus(Candidate, OpportunityBonuses);
	if (HasGrudgeAgainst(Candidate))
	{
		Score += GrudgeScoreBonus;
	}
	FNoFearPackMemberView View;
	if (GetPackView(View) && View.Target == Candidate)
	{
		Score += PackTargetBonus;
	}
	// Охрана шептуна: идёт на обидчика.
	if (Candidate && Candidate == GetDefendTarget())
	{
		Score += DefendScoreBonus;
	}
	// Прикрытие гипноза: загонщик рядом с шептуном бьёт того, кто мешает.
	if (IsHunterKind() && IsHypnosisThreat(Candidate))
	{
		Score += HypnosisCoverBonus;
	}
	// Страж первым бьёт того, кто освобождает проводника.
	if (bPortalGuard)
	{
		const UNoFearPortalSubsystem* Portals = UNoFearPortalSubsystem::Get(this);
		if (Portals && Portals->IsFreeingFeeder(Candidate))
		{
			Score += GuardFreerBonus;
		}
	}
	return Score;
}

void ANoFearMonsterAIController::HandleTargetChanged(AActor* NewTarget, AActor* OldTarget)
{
	Super::HandleTargetChanged(NewTarget, OldTarget);

	// Заметил добычу из спокойствия — рык, стая поднимается.
	if (NewTarget && !OldTarget && GetNow() >= NextRoarTime)
	{
		Roar();
	}
}

// --- Рык и стая ---

void ANoFearMonsterAIController::Roar()
{
	APawn* MyPawn = GetPawn();
	if (!MyPawn || UNoFearHealthComponent::IsActorDead(MyPawn))
	{
		return;
	}
	bRoaring = true;
	++RoarCount;
	NextRoarTime = GetNow() + RoarCooldown;
	StopMovement();
	SetBlackboardBool(FNoFearBotKeys::IsRoaring, true);
	GetWorldTimerManager().SetTimer(RoarTimer, this, &ANoFearMonsterAIController::EndRoar, FMath::Max(RoarDuration, 0.05f), false);

	if (const ANoFearCharacter* MyCharacter = Cast<ANoFearCharacter>(MyPawn))
	{
		if (UNoFearPoseComponent* Pose = MyCharacter->GetPoseComponent())
		{
			Pose->PlayPose(ENoFearPose::Roar, RoarDuration * 0.75f);
		}
	}

	UNoFearBotSubsystem::ReportNoise(this, MyPawn, MyPawn->GetActorLocation(), RoarNoiseRange, ENoFearNoiseType::Other);
	if (AActor* Target = CurrentTarget.Get())
	{
		AlarmPack(Target);
	}
}

void ANoFearMonsterAIController::EndRoar()
{
	bRoaring = false;
	SetBlackboardBool(FNoFearBotKeys::IsRoaring, false);
}

void ANoFearMonsterAIController::AlarmPack(AActor* Threat)
{
	const APawn* MyPawn = GetPawn();
	const UNoFearPackSubsystem* PackSubsystem = UNoFearPackSubsystem::Get(this);
	if (!MyPawn || !Threat || !PackSubsystem)
	{
		return;
	}
	// Рык слышит вся стая, где бы ни была.
	for (APawn* MemberPawn : PackSubsystem->GetPackMembers(MyPawn))
	{
		ANoFearMonsterAIController* Member = MemberPawn != MyPawn ? Cast<ANoFearMonsterAIController>(MemberPawn->GetController()) : nullptr;
		if (Member)
		{
			Member->ReceivePackAlarm(Threat, Threat->GetActorLocation());
		}
	}
}

void ANoFearMonsterAIController::ReceivePackAlarm(AActor* Threat, const FVector& ThreatLocation)
{
	NextRoarTime = FMath::Max(NextRoarTime, GetNow() + RoarCooldown);
	ReceiveAlarm(Threat, ThreatLocation);
}

void ANoFearMonsterAIController::ShareAlarm()
{
	// Монстры не поднимают всех вокруг: стае о цели сообщает UNoFearPackSubsystem, чужие стаи сами разберутся.
}

bool ANoFearMonsterAIController::GetPackView(FNoFearPackMemberView& OutView) const
{
	const UNoFearPackSubsystem* PackSubsystem = UNoFearPackSubsystem::Get(this);
	return PackSubsystem && PackSubsystem->GetMemberView(this, OutView);
}

void ANoFearMonsterAIController::SetShadeGoal(FVector Goal, float Duration)
{
	ShadeGoal = Goal;
	ShadeUntil = Duration > 0.f ? GetNow() + Duration : 0.0;
}

bool ANoFearMonsterAIController::HasShadeGoal() const
{
	return GetNow() < ShadeUntil;
}

void ANoFearMonsterAIController::Defend(AActor* Offender, float Duration)
{
	if (!Offender || Duration <= 0.f || !GetPawn() || IsAggro() || UNoFearHealthComponent::IsActorDead(Offender))
	{
		return;
	}
	const double Now = GetNow();
	DefendTarget = Offender;
	DefendUntil = Now + Duration;
	// Обида на Duration: запись живёт GrudgeTime с отметки, поэтому отметка сдвинута назад (дольше уже имеющейся не укорачиваем).
	const double Stamp = Now - FMath::Max(0.f, GrudgeTime - Duration);
	double& Grudge = Grudges.FindOrAdd(Offender, Stamp);
	Grudge = FMath::Max(Grudge, Stamp);
	ReceiveAlarm(Offender, Offender->GetActorLocation());
}

AActor* ANoFearMonsterAIController::GetDefendTarget() const
{
	AActor* Target = DefendTarget.Get();
	return Target && GetNow() < DefendUntil && !UNoFearHealthComponent::IsActorDead(Target) ? Target : nullptr;
}

void ANoFearMonsterAIController::ApplyWhisperBuff(float Duration)
{
	WhisperBuffUntil = FMath::Max(WhisperBuffUntil, GetNow() + Duration);
}

bool ANoFearMonsterAIController::IsWhisperBuffed() const
{
	return GetNow() < WhisperBuffUntil;
}

bool ANoFearMonsterAIController::IsRoarReady() const
{
	return GetNow() >= NextRoarTime;
}

bool ANoFearMonsterAIController::GetPackGoal(FVector& OutGoal) const
{
	// Колосс прикрывает: место в его тени важнее места в строю.
	if (HasShadeGoal() && !IsPackRaging())
	{
		OutGoal = ShadeGoal;
		return true;
	}
	// Пугает человека — подходит к нему вплотную.
	const APawn* MyPawn = GetPawn();
	const AActor* Target = CurrentTarget.Get();
	if (bScaring && MyPawn && Target)
	{
		FVector Away = (MyPawn->GetActorLocation() - Target->GetActorLocation()).GetSafeNormal2D();
		// Загон: заходит с дальней от шептуна стороны, человек бежит от него — к шептуну.
		const ANoFearWhispererAIController* Whisperer = NearWhisperer.Get();
		if (bHerding && Whisperer && Whisperer->GetPawn())
		{
			const FVector FromWhisperer = (Target->GetActorLocation() - Whisperer->GetPawn()->GetActorLocation()).GetSafeNormal2D();
			if (!FromWhisperer.IsNearlyZero())
			{
				Away = FromWhisperer;
			}
		}
		OutGoal = Target->GetActorLocation() + Away * ScareDistance;
		return true;
	}
	// Страж без цели — на посту.
	if (bPortalGuard && !Target)
	{
		OutGoal = GuardPost;
		return true;
	}
	FNoFearPackMemberView View;
	if (!GetPackView(View) || !View.bHasGoal)
	{
		return false;
	}
	OutGoal = View.Goal;
	return true;
}

APawn* ANoFearMonsterAIController::GetPackLeader() const
{
	FNoFearPackMemberView View;
	return GetPackView(View) && View.Leader && View.Leader->GetPawn() ? View.Leader->GetPawn() : GetPawn();
}

bool ANoFearMonsterAIController::IsPackLeader() const
{
	FNoFearPackMemberView View;
	return !GetPackView(View) || !View.Leader || View.Leader == this;
}

int32 ANoFearMonsterAIController::GetPackSize() const
{
	FNoFearPackMemberView View;
	return GetPackView(View) ? FMath::Max(View.PackSize, 1) : 1;
}

ENoFearPackState ANoFearMonsterAIController::GetPackState() const
{
	FNoFearPackMemberView View;
	return GetPackView(View) ? View.State : ENoFearPackState::Roam;
}

bool ANoFearMonsterAIController::IsPackRaging() const
{
	// Своё время ярости: шептун в неё не впадает, хоть стая и в ярости.
	return GetNow() < RageUntil && GetPackState() == ENoFearPackState::Rage;
}

void ANoFearMonsterAIController::BeginPackRage(AActor* Killer, float Duration)
{
	// Как охрана шептуна: бросить цель и идти на убийцу, пока ярость не пройдёт.
	RageUntil = GetNow() + Duration;
	Defend(Killer, Duration);
}

void ANoFearMonsterAIController::HandleBodyDamaged(UNoFearHealthComponent* Component, const FNoFearDamageInfo& Info)
{
	if (!Info.bFatal)
	{
		// Вздрагивание: сильное попадание на замахе срывает удар загонщика.
		UNoFearMeleeComponent* Melee = GetMeleeComponent();
		if (IsHunterKind() && Melee && Melee->IsInWindup() && (Info.bHeadshot || Info.Amount >= FlinchCancelDamage))
		{
			Melee->CancelAttack();
			Melee->BlockAttacksFor(FlinchBlockTime);
			++FlinchCancelCount;
			if (UNoFearPoseComponent* Pose = GetPawn() ? GetPawn()->FindComponentByClass<UNoFearPoseComponent>() : nullptr)
			{
				Pose->PlayPose(ENoFearPose::Flinch, 0.35f, 1.f);
			}
		}
		return;
	}
	AActor* Killer = Info.InstigatorController ? Info.InstigatorController->GetPawn() : nullptr;
	if (!Killer && Info.DamageCauser)
	{
		Killer = Info.DamageCauser->GetInstigator() ? Info.DamageCauser->GetInstigator() : Info.DamageCauser.Get();
	}
	if (UNoFearPackSubsystem* PackSubsystem = UNoFearPackSubsystem::Get(this); PackSubsystem && Killer)
	{
		PackSubsystem->NotifyMemberKilled(this, Killer);
	}
}

// --- Походка и блэкборд ---

bool ANoFearMonsterAIController::IsCircling() const
{
	const APawn* MyPawn = GetPawn();
	const AActor* Target = CurrentTarget.Get();
	if (!MyPawn || !Target || bHasAttackToken
		|| FVector::DistSquared2D(MyPawn->GetActorLocation(), Target->GetActorLocation()) > FMath::Square(CircleStanceDistance))
	{
		return false;
	}
	// Бежит на своё место в кольце — не кружит, пока не добежал.
	FVector Goal;
	return !GetPackGoal(Goal) || FVector::DistSquared2D(MyPawn->GetActorLocation(), Goal) <= FMath::Square(PackSlotStanceDistance);
}

bool ANoFearMonsterAIController::ShouldWalk() const
{
	if (IsPackRaging())
	{
		return false;
	}
	// В строю идёт со стаей: отстал от своего места — догоняет бегом, иначе шагает с вожаком.
	FVector Goal;
	if (GetPawn() && GetPackGoal(Goal))
	{
		return FVector::DistSquared2D(GetPawn()->GetActorLocation(), Goal) <= FMath::Square(PackGoalRunDistance) && Super::ShouldWalk();
	}
	// Шум проверяет бегом, спокойно бродит шагом.
	const UBlackboardComponent* BlackboardComp = GetBlackboardComponent();
	if (BlackboardComp && BlackboardComp->IsVectorValueSet(FNoFearBotKeys::InvestigateLocation))
	{
		return false;
	}
	return Super::ShouldWalk();
}

void ANoFearMonsterAIController::UpdateMovementStance()
{
	if (IsFacingAttack())
	{
		// Атакует стоя: боком к фокусу (цели), шагом — так тело доворачивается к цели.
		SetMovementFlags(true, false, true, false);
		return;
	}
	if (bScaring)
	{
		// Гонит человека бегом, рядом — наступает шагом.
		const bool bClose = GetTargetDistance2D() <= ScareDistance + 150.f;
		SetMovementFlags(bClose, false, bClose, !bClose);
		return;
	}
	if (IsPackRaging() && bHasAttackToken)
	{
		// На убийцу вожака — со всех ног.
		SetMovementFlags(false, false, false, true);
		return;
	}
	if (IsCircling())
	{
		// Без жетона кружит шагом боком, не сводя глаз с цели; слишком близко — отходит бегом.
		SetMovementFlags(true, false, GetTargetDistance2D() >= CircleBackOffDistance);
		return;
	}
	Super::UpdateMovementStance();
}

FVector ANoFearMonsterAIController::GetRoamCenter()
{
	const APawn* MyPawn = GetPawn();
	const double Now = GetNow();
	if (!MyPawn || Now < NextRoamCenterTime)
	{
		return RoamCenter;
	}
	NextRoamCenterTime = Now + RoamCenterInterval;

	if (Now >= NextRoamTurnTime)
	{
		RoamHeading = RoamHeading.RotateAngleAxis(FMath::FRandRange(-RoamTurnAngle, RoamTurnAngle), FVector::UpVector);
		NextRoamTurnTime = Now + FMath::FRandRange(RoamTurnMinInterval, RoamTurnMaxInterval);
	}

	// Шаг вперёд по курсу; впереди нет навмеша (край карты, стена) — свернуть или развернуться.
	const FVector Location = MyPawn->GetActorLocation();
	RoamCenter = Location;
	const UNavigationSystemV1* NavSystem = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
	if (!NavSystem)
	{
		return RoamCenter;
	}
	const float Side = FMath::RandBool() ? 1.f : -1.f;
	for (const float Turn : { 0.f, 90.f * Side, -90.f * Side, 180.f })
	{
		const FVector Direction = RoamHeading.RotateAngleAxis(Turn, FVector::UpVector);
		FNavLocation Projected;
		if (NavSystem->ProjectPointToNavigation(Location + Direction * RoamStride, Projected, RoamProjectExtent))
		{
			RoamHeading = Direction;
			RoamCenter = Projected.Location;
			break;
		}
	}
	return RoamCenter;
}

void ANoFearMonsterAIController::WriteBlackboard()
{
	Super::WriteBlackboard();
	UBlackboardComponent* BlackboardComp = GetBlackboardComponent();
	if (!BlackboardComp)
	{
		return;
	}
	BlackboardComp->SetValueAsBool(FNoFearBotKeys::HasAttackToken, bHasAttackToken);
	BlackboardComp->SetValueAsBool(FNoFearBotKeys::IsRoaring, bRoaring);

	FNoFearPackMemberView View;
	const bool bInPack = GetPackView(View) && View.PackSize >= 2;
	// Монстры не бегут: точка от стаи — только место в строю и кольце (PackGoal).
	BlackboardComp->ClearValue(FNoFearBotKeys::FleeLocation);
	FVector PackGoal;
	if (GetPackGoal(PackGoal))
	{
		BlackboardComp->SetValueAsVector(FNoFearBotKeys::PackGoal, PackGoal);
	}
	else
	{
		BlackboardComp->ClearValue(FNoFearBotKeys::PackGoal);
	}

	// Вожак и одиночка бродят по карте вперёд по своему курсу, остальные — вокруг вожака.
	const APawn* LeaderPawn = bInPack && View.Leader && View.Leader != this ? View.Leader->GetPawn() : nullptr;
	BlackboardComp->SetValueAsVector(FNoFearBotKeys::WanderCenter, LeaderPawn ? LeaderPawn->GetActorLocation() : GetRoamCenter());
}

void ANoFearMonsterAIController::SetBlackboardBool(FName Key, bool bValue)
{
	if (UBlackboardComponent* BlackboardComp = GetBlackboardComponent())
	{
		BlackboardComp->SetValueAsBool(Key, bValue);
	}
}
