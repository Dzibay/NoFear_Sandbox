#include "AI/NoFearMonsterAIController.h"

#include "AI/NoFearBotSubsystem.h"
#include "AI/NoFearMeleeComponent.h"
#include "AI/NoFearMonsterAttack.h"
#include "Animation/NoFearPoseComponent.h"
#include "BehaviorTree/BehaviorTree.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "Core/NoFearCharacter.h"
#include "Core/NoFearHealthComponent.h"
#include "Core/NoFearTeam.h"
#include "NavigationSystem.h"
#include "Portal/NoFearPortalSubsystem.h"
#include "Perception/AISenseConfig_Hearing.h"
#include "Perception/AISenseConfig_Sight.h"
#include "TimerManager.h"
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

	/** Курс вожака гуляет на ±столько градусов раз в 6–10 секунд. */
	constexpr float RoamTurnAngle = 50.f;
	constexpr float RoamTurnMinInterval = 6.f;
	constexpr float RoamTurnMaxInterval = 10.f;
	/** Как часто пересчитывать точку, вокруг которой бродить, сек. */
	constexpr double RoamCenterInterval = 1.0;
	const FVector RoamProjectExtent(300.f, 300.f, 500.f);
}

ANoFearMonsterAIController::ANoFearMonsterAIController(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// Бежит к цели лицом по ходу; прицела и стрейфа у монстра нет.
	bStrafeInCombat = false;
	bWalkWhenCalm = true;
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
	bHasAttackToken = false;
	TokenTarget.Reset();
	bSelfDefense = false;
	bRoaring = false;
	RoamHeading = InPawn ? InPawn->GetActorForwardVector().GetSafeNormal2D() : FVector::ForwardVector;
	NextRoamTurnTime = 0.0;
	NextRoamCenterTime = 0.0;

	ConfiguredMelee.Reset();

	Super::OnPossess(InPawn);

	if (UNoFearMeleeComponent* Melee = GetMeleeComponent())
	{
		Melee->OnAttackFinished.AddUniqueDynamic(this, &ANoFearMonsterAIController::HandleAttackFinished);
		ConfigureMelee(Melee);
		ConfiguredMelee = Melee;
	}
}

void ANoFearMonsterAIController::ConfigureMelee(UNoFearMeleeComponent* Melee)
{
	if (Melee && !AttackSet.IsEmpty())
	{
		Melee->SetAttacks(ObjectPtrDecay(AttackSet));
	}
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
		Melee->OnAttackFinished.RemoveDynamic(this, &ANoFearMonsterAIController::HandleAttackFinished);
	}
	GetWorldTimerManager().ClearTimer(RoarTimer);
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
		return;
	}

	UpdateGrudges();

	if (UNoFearMeleeComponent* Melee = GetMeleeComponent())
	{
		Melee->CooldownScale = IsAggro() ? AggroCooldownScale : 1.f;
		// Компонент мог появиться (или смениться) уже после OnPossess.
		if (!Melee->OnAttackFinished.IsAlreadyBound(this, &ANoFearMonsterAIController::HandleAttackFinished))
		{
			Melee->OnAttackFinished.AddDynamic(this, &ANoFearMonsterAIController::HandleAttackFinished);
		}
		if (ConfiguredMelee.Get() != Melee)
		{
			ConfigureMelee(Melee);
			ConfiguredMelee = Melee;
		}
	}

	Super::UpdateBot();

	UpdateAttackToken();
	// Жетон мог смениться после того, как база записала блэкборд.
	SetBlackboardBool(FNoFearBotKeys::HasAttackToken, bHasAttackToken);
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
	APawn* MyPawn = GetPawn();
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

	// В стае очередь решает стая: бьют только окружив цель, своей волной. Кого цель бьёт рядом — отвечает без очереди.
	FNoFearPackMemberView View;
	if (GetPackView(View) && View.PackSize >= 2 && (View.Target == Target || View.State == ENoFearPackState::Scatter))
	{
		ReleaseToken();
		const FNoFearKnownActor* Knowledge = KnownActors.Find(Target);
		const bool bHitByTarget = Knowledge && GetNow() - Knowledge->LastAttackedTime <= SelfDefenseTime;
		const float DefenseDistance = SelfDefenseDistance + (bSelfDefense ? SelfDefenseHysteresis : 0.f);
		bSelfDefense = bHitByTarget && GetTargetDistance2D() <= DefenseDistance;
		switch (View.State)
		{
		case ENoFearPackState::Scatter:
			bHasAttackToken = false;
			break;
		case ENoFearPackState::Encircle:
		case ENoFearPackState::Attack:
			// Плевун в кольцо не встаёт: окружают — плюётся.
			bHasAttackToken = !bUseAttackTokens || View.bAttackTurn || bSelfDefense;
			break;
		default:
			bHasAttackToken = bSelfDefense;
			break;
		}
		return;
	}

	// Одиночка (или бьёт не цель стаи): жетоны, не больше двоих на цель.
	bSelfDefense = false;
	UNoFearBotSubsystem* Subsystem = UNoFearBotSubsystem::Get(this);
	if (!bUseAttackTokens || !Subsystem)
	{
		bHasAttackToken = true;
		return;
	}
	bHasAttackToken = Subsystem->RequestAttackToken(MyPawn, Target);
	if (bHasAttackToken)
	{
		TokenTarget = Target;
	}
}

void ANoFearMonsterAIController::HandleAttackFinished(AActor* Target)
{
	if (IsAggro())
	{
		return;
	}

	FNoFearPackMemberView View;
	if (GetPackView(View) && View.PackSize >= 2)
	{
		if (UNoFearPackSubsystem* PackSubsystem = UNoFearPackSubsystem::Get(this))
		{
			PackSubsystem->NotifyAttackFinished(this);
		}
		// Ударил в свою очередь — назад на место в кольце; защищается — бьёт дальше.
		if (View.State == ENoFearPackState::Attack && bUseAttackTokens && !bSelfDefense)
		{
			bHasAttackToken = false;
			SetBlackboardBool(FNoFearBotKeys::HasAttackToken, false);
		}
		return;
	}

	if (!bUseAttackTokens || !TokenTarget.IsValid())
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
		return Score;
	}

	float Score = Super::ScoreTarget(Candidate, Knowledge);
	if (HasGrudgeAgainst(Candidate))
	{
		Score += GrudgeScoreBonus;
	}
	FNoFearPackMemberView View;
	if (GetPackView(View) && View.Target == Candidate)
	{
		Score += PackTargetBonus;
	}
	return Score;
}

void ANoFearMonsterAIController::HandleTargetChanged(AActor* NewTarget, AActor* OldTarget)
{
	Super::HandleTargetChanged(NewTarget, OldTarget);

	// Заметил добычу из спокойствия — рык, стая поднимается.
	if (NewTarget && !OldTarget && GetNow() >= NextRoarTime && !IsScattering())
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

bool ANoFearMonsterAIController::GetPackGoal(FVector& OutGoal) const
{
	FNoFearPackMemberView View;
	if (!GetPackView(View) || !View.bHasGoal || View.State == ENoFearPackState::Scatter)
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

bool ANoFearMonsterAIController::IsScattering() const
{
	return GetPackState() == ENoFearPackState::Scatter;
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
	if (IsScattering())
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
	if (IsScattering())
	{
		// Разбегается со всех ног, не оглядываясь.
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
	const bool bScattering = bInPack && View.State == ENoFearPackState::Scatter;
	BlackboardComp->SetValueAsBool(FNoFearBotKeys::IsScattering, bScattering);

	// Точка от стаи: бегство (FleeLocation) или место в строю и кольце (PackGoal).
	if (bScattering && View.bHasGoal)
	{
		BlackboardComp->SetValueAsVector(FNoFearBotKeys::FleeLocation, View.Goal);
	}
	else
	{
		BlackboardComp->ClearValue(FNoFearBotKeys::FleeLocation);
	}
	if (bInPack && !bScattering && View.bHasGoal)
	{
		BlackboardComp->SetValueAsVector(FNoFearBotKeys::PackGoal, View.Goal);
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
