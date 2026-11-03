#include "AI/NoFearCompanionAIController.h"

#include "AI/NoFearBotSubsystem.h"
#include "AI/NoFearRecruitableComponent.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "BrainComponent.h"
#include "Core/NoFearCharacter.h"
#include "Core/NoFearHealthComponent.h"
#include "Core/NoFearPlayerController.h"
#include "Core/NoFearTeam.h"
#include "Engine/World.h"
#include "GameFramework/Controller.h"
#include "GameFramework/Pawn.h"
#include "NavigationSystem.h"
#include "Weapons/NoFearGrenadeComponent.h"
#include "Weapons/NoFearWeapon.h"
#include "Weapons/NoFearWeaponComponent.h"
#include "Weapons/NoFearWeaponData.h"

#define LOCTEXT_NAMESPACE "NoFearCompanion"

namespace
{
	/** Столько секунд после выстрела по напарнику или игроку можно отвечать, даже если «не стрелять». */
	constexpr double ProvokedTime = 6.0;
	/** Крадёмся, а игрок выстрелил не раньше стольких секунд назад — тишина кончилась. */
	constexpr double LeaderFireProvokeTime = 3.0;
	/** Тот, в кого попал игрок, важнее столько секунд. */
	constexpr double LeaderTargetMemory = 5.0;
	/** Врага, которого не видно столько секунд, при новой встрече объявляют снова. */
	constexpr double CalloutRepeatTime = 25.0;
	/** Поле зрения игрока для отметок (от центра экрана), градусы. */
	constexpr float LeaderSightAngle = 55.f;
	/** Ближе этого к точке приказа «Иди туда» — пришёл. */
	constexpr float OrderArriveDistance = 120.f;
	/** «Ко мне»: ближе этого к игроку — дошёл, дальше просто идёт следом. */
	constexpr float ComeHereDistance = 300.f;

	bool ProjectToNavigation(UWorld* World, const FVector& Point, FVector& OutLocation)
	{
		UNavigationSystemV1* Nav = World ? FNavigationSystem::GetCurrent<UNavigationSystemV1>(World) : nullptr;
		FNavLocation Projected;
		if (!Nav || !Nav->ProjectPointToNavigation(Point, Projected, FVector(150.f, 150.f, 300.f)))
		{
			return false;
		}
		OutLocation = Projected.Location;
		return true;
	}

	bool IsMonster(const AActor* Actor)
	{
		const ENoFearTeam Team = UNoFearTeamLibrary::GetTeam(Actor);
		return Team == ENoFearTeam::Monster || Team == ENoFearTeam::AggroMonster;
	}

	ANoFearWeapon* GetPawnWeapon(const APawn* Pawn)
	{
		const ANoFearCharacter* Character = Cast<ANoFearCharacter>(Pawn);
		const UNoFearWeaponComponent* Weapons = Character ? Character->GetWeaponComponent() : nullptr;
		return Weapons ? Weapons->GetCurrentWeapon() : nullptr;
	}
}

ANoFearCompanionAIController::ANoFearCompanionAIController(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// Без лидера — обычный союзник со своим поводком (LeashRadius стрелка).
	// Тела не отвлекают напарника от лидера и приказа «жди здесь».
	bReactToCorpses = false;
}

void ANoFearCompanionAIController::OnPossess(APawn* InPawn)
{
	if (InPawn && bAddRecruitableComponentIfMissing && !InPawn->FindComponentByClass<UNoFearRecruitableComponent>())
	{
		UNoFearRecruitableComponent* Recruitable = NewObject<UNoFearRecruitableComponent>(InPawn, TEXT("RecruitableComponent"));
		Recruitable->RegisterComponent();
	}

	Super::OnPossess(InPawn);

	bDefaultThrowGrenades = bThrowGrenades;
	SupplyMagazines = MaxSupplyMagazines;
	SupplyBandages = MaxSupplyBandages;
	NextMagazineRefill = 0.0;
	NextBandageRefill = 0.0;
	bDowned = false;
	LastCompanionUpdate = GetNow();

	// Упал или поднялся — сразу, не дожидаясь обновления: дерево не должно успеть побежать.
	if (UNoFearHealthComponent* Health = UNoFearHealthComponent::FindHealth(InPawn))
	{
		Health->OnDowned.AddUniqueDynamic(this, &ThisClass::HandleSelfDowned);
		Health->OnRevived.AddUniqueDynamic(this, &ThisClass::HandleSelfRevived);
		BoundSelfHealth = Health;
	}
}

void ANoFearCompanionAIController::OnUnPossess()
{
	if (UNoFearHealthComponent* Health = BoundSelfHealth.Get())
	{
		Health->OnDowned.RemoveDynamic(this, &ThisClass::HandleSelfDowned);
		Health->OnRevived.RemoveDynamic(this, &ThisClass::HandleSelfRevived);
	}
	BoundSelfHealth = nullptr;
	ReleaseHelperReservations();

	Super::OnUnPossess();
}

void ANoFearCompanionAIController::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	ReleaseHelperReservations();
	UnbindLeader();

	Super::EndPlay(EndPlayReason);
}

APawn* ANoFearCompanionAIController::GetLeaderPawn() const
{
	APawn* LeaderPawn = Leader.IsValid() ? Leader->GetPawn() : nullptr;
	return LeaderPawn && !UNoFearHealthComponent::IsActorDead(LeaderPawn) ? LeaderPawn : nullptr;
}

ANoFearPlayerController* ANoFearCompanionAIController::GetLeaderController() const
{
	return Cast<ANoFearPlayerController>(Leader.Get());
}

void ANoFearCompanionAIController::SetLeader(AController* NewLeader)
{
	if (Leader.Get() == NewLeader)
	{
		return;
	}

	Leader = NewLeader;
	bFollowing = NewLeader != nullptr;
	if (!NewLeader)
	{
		// Отпущенный напарник остаётся союзником там, где стоит, по обычным правилам.
		if (GetPawn())
		{
			HomeLocation = GetPawn()->GetActorLocation();
		}
		ClearOrder();
		StopReviving();
		bHoldFire = false;
		SupplyKind = 0;
		bHasCompanionGoal = false;
		if (bCrouchedForStealth)
		{
			SetCrouching(false);
			bCrouchedForStealth = false;
		}
		bSneaking = false;
		bThrowGrenades = bDefaultThrowGrenades;
		Marks.Reset();
		ReleaseHelperReservations();
		UNoFearHealthComponent* Health = BoundSelfHealth.Get();
		if (Health && !Health->IsDowned())
		{
			Health->bCanBeDowned = false;
		}
	}
	BindLeader();
	WriteBlackboard();
	UpdateMovementStance();
	OnCompanionStateChanged.Broadcast(this);
}

void ANoFearCompanionAIController::SetFollowing(bool bNewFollowing)
{
	bNewFollowing = bNewFollowing && Leader.IsValid();
	if (bFollowing == bNewFollowing)
	{
		return;
	}

	bFollowing = bNewFollowing;
	if (!bFollowing && GetPawn())
	{
		HomeLocation = GetPawn()->GetActorLocation();
	}
	WriteBlackboard();
	UpdateMovementStance();
	OnCompanionStateChanged.Broadcast(this);
}

FVector ANoFearCompanionAIController::GetLeashAnchor() const
{
	if (const APawn* LeaderPawn = IsFollowing() ? GetLeaderPawn() : nullptr)
	{
		return LeaderPawn->GetActorLocation();
	}
	return Super::GetLeashAnchor();
}

float ANoFearCompanionAIController::GetLeashRadius() const
{
	return IsFollowing() && GetLeaderPawn() ? FollowLeashRadius : Super::GetLeashRadius();
}

float ANoFearCompanionAIController::GetDistanceToLeader() const
{
	const APawn* LeaderPawn = GetLeaderPawn();
	return LeaderPawn && GetPawn() ? FVector::Dist(LeaderPawn->GetActorLocation(), GetPawn()->GetActorLocation()) : 0.f;
}

// --- Обновление ---

void ANoFearCompanionAIController::UpdateBot()
{
	APawn* MyPawn = GetPawn();
	if (!MyPawn || UNoFearHealthComponent::IsActorDead(MyPawn))
	{
		if (bHasCompanionGoal || ReservedLeaderPawn.IsValid() || LaneReservationUntil > 0.0)
		{
			bHasCompanionGoal = false;
			ReleaseHelperReservations();
		}
		Super::UpdateBot();
		return;
	}

	const double Now = GetNow();
	const float DeltaTime = FMath::Clamp(static_cast<float>(Now - LastCompanionUpdate), 0.f, 0.5f);
	LastCompanionUpdate = Now;
	BindLeader();

	UNoFearHealthComponent* MyHealth = UNoFearHealthComponent::FindHealth(MyPawn);
	if (MyHealth && MyHealth->IsDowned())
	{
		UpdateDowned();
		return;
	}
	if (bDowned)
	{
		RecoverFromDowned();
	}

	APawn* LeaderPawn = GetLeaderPawn();
	if (MyHealth)
	{
		// Падает раненым, только пока лидер на ногах: иначе поднимать некому.
		MyHealth->bCanBeDowned = LeaderPawn && !UNoFearHealthComponent::IsActorDowned(LeaderPawn);
	}

	// Пока идёт следом, «дом» — где стоит сам: если лидер погибнет или скажет ждать, напарник останется здесь.
	if (IsFollowing())
	{
		HomeLocation = MyPawn->GetActorLocation();
	}
	for (auto It = LeaderAttackers.CreateIterator(); It; ++It)
	{
		if (!It.Key().IsValid() || Now - It.Value() > ProvokedTime)
		{
			It.RemoveCurrent();
		}
	}

	UpdateSupplyStock(Now);
	UpdateStealth();

	FVector Goal = FVector::ZeroVector;
	bHasCompanionGoal = UpdateReviveLeader(DeltaTime, Goal) || UpdateOrderMove(Goal) || UpdateSupply(Goal) || UpdateFireLane(Goal);
	CompanionGoal = Goal;

	// Тихо, «не стрелять» или занят своим делом — гранат сам не бросает (по приказу — бросает).
	bThrowGrenades = bDefaultThrowGrenades && !bHoldFire && !bSneaking && !bHasCompanionGoal;

	Super::UpdateBot();

	if (bReviving && LeaderPawn)
	{
		SetFocus(LeaderPawn);
	}
	if (bSneaking && !IsCrouching())
	{
		SetCrouching(true);
	}

	if (UNoFearBotSubsystem* Bots = UNoFearBotSubsystem::Get(this))
	{
		if (LaneReservationUntil > 0.0 && Now >= LaneReservationUntil)
		{
			Bots->ReleaseCover(this);
			LaneReservationUntil = 0.0;
		}
		// В бою место игрока занято: напарник не встанет в укрытие у него на голове.
		APawn* Reserve = IsFollowing() && LeaderPawn && GetCurrentTarget() ? LeaderPawn : nullptr;
		if (ReservedLeaderPawn.IsValid() && ReservedLeaderPawn.Get() != Reserve)
		{
			Bots->ReleaseCover(ReservedLeaderPawn.Get());
			ReservedLeaderPawn = nullptr;
		}
		if (Reserve)
		{
			Bots->ReserveCover(Reserve, Reserve->GetActorLocation());
			ReservedLeaderPawn = Reserve;
		}
	}

	UpdateMarksAndCallouts();
	UpdateCombatBarks();
}

bool ANoFearCompanionAIController::IsValidTarget(const AActor* Candidate) const
{
	if (!Super::IsValidTarget(Candidate))
	{
		return false;
	}
	if (Candidate == OrderTarget.Get())
	{
		return true;
	}
	// Слишком отстал — сначала догнать лидера.
	if (IsFollowing() && GetLeaderPawn() && GetDistanceToLeader() > BreakOffDistance)
	{
		return false;
	}
	// «Не стрелять» и скрытность: первым не начинает.
	return !(bHoldFire || bSneaking) || IsProvoked(Candidate);
}

bool ANoFearCompanionAIController::IsProvoked(const AActor* Candidate) const
{
	const double Now = GetNow();
	AActor* Key = const_cast<AActor*>(Candidate);
	const FNoFearKnownActor* Knowledge = KnownActors.Find(Key);
	if (Knowledge && Now - Knowledge->LastAttackedTime <= ProvokedTime)
	{
		return true;
	}
	const double* AttackedLeader = LeaderAttackers.Find(Key);
	if (AttackedLeader && Now - *AttackedLeader <= ProvokedTime)
	{
		return true;
	}
	return bSneaking && !bHoldFire && Now - GetLeaderLastFireTime() <= LeaderFireProvokeTime;
}

float ANoFearCompanionAIController::ScoreTarget(const AActor* Candidate, const FNoFearKnownActor& Knowledge) const
{
	float Score = Super::ScoreTarget(Candidate, Knowledge);
	if (!IsRecruited())
	{
		return Score;
	}

	const double Now = GetNow();
	if (Candidate == OrderTarget.Get())
	{
		Score += OrderedTargetBonus;
	}
	// Помогает игроку: в кого он попал и кто стреляет в него.
	if (Candidate == LeaderHitTarget.Get() && Now - LeaderHitTime <= LeaderTargetMemory)
	{
		Score += LeaderTargetBonus;
	}
	const double* AttackedLeader = LeaderAttackers.Find(const_cast<AActor*>(Candidate));
	if (AttackedLeader && Now - *AttackedLeader <= ProvokedTime)
	{
		Score += LeaderAttackerBonus;
	}
	const APawn* LeaderPawn = GetLeaderPawn();
	const APawn* CandidatePawn = Cast<APawn>(Candidate);
	const ANoFearAIController* CandidateBot = CandidatePawn ? Cast<ANoFearAIController>(CandidatePawn->GetController()) : nullptr;
	if (LeaderPawn && CandidateBot && CandidateBot->GetCurrentTarget() == LeaderPawn)
	{
		Score += 0.5f;
	}
	return Score;
}

bool ANoFearCompanionAIController::ShouldWalk() const
{
	if (bSneaking)
	{
		return true;
	}
	if (bHasCompanionGoal)
	{
		// По делу — бегом.
		return false;
	}
	if (const APawn* LeaderPawn = IsFollowing() && !GetCurrentTarget() ? GetLeaderPawn() : nullptr)
	{
		return GetDistanceToLeader() < RunToLeaderDistance && LeaderPawn->GetVelocity().Size2D() < LeaderRunSpeed;
	}
	return Super::ShouldWalk();
}

bool ANoFearCompanionAIController::CanInvestigateLocation(const FVector& Location) const
{
	// Завербованный не уходит проверять шум: он с игроком или ждёт, где сказали.
	return !IsRecruited() && Super::CanInvestigateLocation(Location);
}

void ANoFearCompanionAIController::WriteBlackboard()
{
	UBlackboardComponent* BlackboardComp = GetBlackboardComponent();
	if (!BlackboardComp)
	{
		Super::WriteBlackboard();
		return;
	}

	// Пачкой: стрелок очищает RallyLocation, а мы сразу пишем снова — без паузы декоратор успел бы прервать ветку.
	BlackboardComp->PauseObserverNotifications();
	Super::WriteBlackboard();

	APawn* LeaderPawn = IsFollowing() && !bDowned ? GetLeaderPawn() : nullptr;
	BlackboardComp->SetValueAsObject(FNoFearBotKeys::Leader, LeaderPawn);
	BlackboardComp->SetValueAsBool(FNoFearBotKeys::IsFollowing, LeaderPawn != nullptr);
	if (bHasCompanionGoal && !bDowned)
	{
		BlackboardComp->SetValueAsVector(FNoFearBotKeys::RallyLocation, CompanionGoal);
	}
	BlackboardComp->ResumeObserverNotifications(true);
}

void ANoFearCompanionAIController::MakeCoverSearch(bool bRetreat, AActor* Threat, const FVector& ThreatLocation, FNoFearCoverSearch& OutSearch) const
{
	Super::MakeCoverSearch(bRetreat, Threat, ThreatLocation, OutSearch);

	const APawn* MyPawn = GetPawn();
	const APawn* LeaderPawn = IsFollowing() ? GetLeaderPawn() : nullptr;
	if (!MyPawn || !LeaderPawn || bRetreat)
	{
		return;
	}

	// Укрытие рядом с игроком: сбоку (с той стороны, где напарник уже стоит) и чуть позади относительно врага.
	const FVector LeaderLocation = LeaderPawn->GetActorLocation();
	const FVector ToThreat = (ThreatLocation - LeaderLocation).GetSafeNormal2D();
	const FVector Side = FVector::CrossProduct(FVector::UpVector, ToThreat);
	const float Sign = FVector::DotProduct(MyPawn->GetActorLocation() - LeaderLocation, Side) >= 0.f ? 1.f : -1.f;
	OutSearch.bUseSearchOrigin = true;
	OutSearch.SearchOrigin = LeaderLocation;
	OutSearch.SearchRadius = FMath::Min(OutSearch.SearchRadius, NearLeaderCoverRadius);
	OutSearch.GoalLocation = LeaderLocation + Side * Sign * 400.f - ToThreat * 150.f;
	OutSearch.GoalWeight = 2.f;
}

void ANoFearCompanionAIController::HandleDanger(ENoFearDangerType Type, AActor* Source, FVector ThreatLocation)
{
	Super::HandleDanger(Type, Source, ThreatLocation);

	// Чужая граната рядом — предупредить игрока (свою и его не объявляет).
	if (Type == ENoFearDangerType::Grenade && IsRecruited() && !bDowned && Source)
	{
		const AActor* Thrower = Source->GetInstigator() ? static_cast<AActor*>(Source->GetInstigator()) : Source->GetOwner();
		if (!Thrower || UNoFearTeamLibrary::AreHostile(GetPawn(), Thrower))
		{
			Say(TEXT("Grenade"), LOCTEXT("GrenadeWarning", "Граната! В сторону!"), 6.f, true);
		}
	}
}

// --- Ранение ---

void ANoFearCompanionAIController::HandleSelfDowned(UNoFearHealthComponent* Component, const FNoFearDamageInfo& Info)
{
	UpdateDowned();
}

void ANoFearCompanionAIController::HandleSelfRevived(UNoFearHealthComponent* Component, AActor* Reviver)
{
	RecoverFromDowned();
}

void ANoFearCompanionAIController::UpdateDowned()
{
	APawn* MyPawn = GetPawn();
	const UNoFearHealthComponent* Health = UNoFearHealthComponent::FindHealth(MyPawn);
	if (!MyPawn || !Health || !Health->IsDowned())
	{
		return;
	}

	const double Now = GetNow();
	if (!bDowned)
	{
		bDowned = true;
		// Поднимать игрока теперь некому: упадёт тем же кадром — значит, погибнет.
		if (UNoFearHealthComponent* LeaderHealth = BoundLeaderHealth.Get(); LeaderHealth && !LeaderHealth->IsDowned())
		{
			LeaderHealth->bCanBeDowned = false;
		}
		EndGrenadeEvade();
		ReleaseStagger();
		StopMovement();
		if (const ANoFearCharacter* BotCharacter = GetNoFearCharacter())
		{
			if (UNoFearWeaponComponent* Weapons = BotCharacter->GetWeaponComponent())
			{
				Weapons->StopFire();
				Weapons->SetAiming(false);
			}
		}
		ReleaseCover();
		ReleaseHelperReservations();
		SetTarget(nullptr);
		ClearFocus(EAIFocusPriority::Gameplay);
		ClearOrder();
		StopReviving();
		SupplyKind = 0;
		bHasCompanionGoal = false;
		// Присел он уже сам (ANoFearCharacter держит упавшего пригнувшимся); встанет при подъёме.
		bSneaking = false;
		bCrouchedForStealth = false;
		if (UBrainComponent* Brain = GetBrainComponent())
		{
			Brain->PauseLogic(TEXT("Downed"));
		}
		WriteBlackboard();
		Say(TEXT("Downed"), LOCTEXT("Downed", "Я ранен! Помоги!"), 0.f, true);
		NextDownedBarkTime = Now + 8.0;
		OnCompanionStateChanged.Broadcast(this);
		return;
	}

	if (Now >= NextDownedBarkTime)
	{
		NextDownedBarkTime = Now + 8.0;
		if (!Health->IsBeingRevived())
		{
			const bool bBleedingOut = Health->GetDownedTimeRemaining() < 10.f;
			Say(TEXT("DownedCall"), bBleedingOut ? LOCTEXT("BleedingOut", "Я истекаю кровью!") : LOCTEXT("PickMeUp", "Подними меня!"), 0.f, true);
		}
	}
}

void ANoFearCompanionAIController::RecoverFromDowned()
{
	if (!bDowned)
	{
		return;
	}
	bDowned = false;
	LastCompanionUpdate = GetNow();
	if (UBrainComponent* Brain = GetBrainComponent(); Brain && !IsPortalControlled())
	{
		Brain->ResumeLogic(TEXT("Revived"));
	}
	UpdateMovementStance();
	Say(TEXT("Revived"), LOCTEXT("Revived", "Спасибо, я в строю."), 0.f, true);
	OnCompanionStateChanged.Broadcast(this);
}

void ANoFearCompanionAIController::StopReviving()
{
	ReviveProgress = 0.f;
	if (!bReviving)
	{
		return;
	}
	bReviving = false;
	ClearFocus(EAIFocusPriority::Gameplay);
	if (!bSneaking)
	{
		SetCrouching(false);
	}
}

bool ANoFearCompanionAIController::UpdateReviveLeader(float DeltaTime, FVector& OutGoal)
{
	APawn* MyPawn = GetPawn();
	APawn* LeaderPawn = GetLeaderPawn();
	UNoFearHealthComponent* LeaderHealth = UNoFearHealthComponent::FindHealth(LeaderPawn);
	if (!MyPawn || !LeaderHealth || !LeaderHealth->IsDowned())
	{
		StopReviving();
		bLeaderDownNoticed = false;
		return false;
	}

	// Игрок упал: бросить всё, бежать к нему даже под огнём.
	if (!bLeaderDownNoticed)
	{
		bLeaderDownNoticed = true;
		ClearOrder();
		SupplyKind = 0;
		Say(TEXT("LeaderDown"), LOCTEXT("LeaderDown", "Держись! Иду к тебе!"), 0.f, true);
	}

	const FVector LeaderLocation = LeaderPawn->GetActorLocation();
	if (FVector::Dist2D(MyPawn->GetActorLocation(), LeaderLocation) > ReviveDistance)
	{
		StopReviving();
		// Встать сбоку от игрока, а не за спиной: камера сзади, напарника должно быть видно.
		const FVector Right = LeaderPawn->GetActorRightVector().GetSafeNormal2D();
		const float Side = FVector::DotProduct(MyPawn->GetActorLocation() - LeaderLocation, Right) >= 0.f ? 1.f : -1.f;
		if (!ProjectToNavigation(GetWorld(), LeaderLocation + Right * Side * ReviveDistance * 0.6f, OutGoal))
		{
			OutGoal = LeaderLocation;
		}
		return true;
	}

	if (!bReviving)
	{
		bReviving = true;
		ReviveProgress = 0.f;
		StopMovement();
		SetCrouching(true);
		Say(TEXT("Reviving"), LOCTEXT("Reviving", "Поднимаю, держись!"), 0.f, true);
	}
	OutGoal = MyPawn->GetActorLocation();

	// Ранили во время помощи — начинать заново.
	const UNoFearHealthComponent* MyHealth = UNoFearHealthComponent::FindHealth(MyPawn);
	if (MyHealth && MyHealth->GetTimeSinceLastDamage() < DeltaTime + 0.05f)
	{
		ReviveProgress = 0.f;
	}
	else
	{
		ReviveProgress += DeltaTime;
	}
	LeaderHealth->NotifyReviveProgress(FMath::Clamp(ReviveProgress / FMath::Max(ReviveTime, 0.1f), 0.f, 1.f));

	if (ReviveProgress >= ReviveTime && LeaderHealth->Revive(MyPawn))
	{
		StopReviving();
		bLeaderDownNoticed = false;
		Say(TEXT("LeaderUp"), LOCTEXT("LeaderUp", "Вставай! Ты нужен."), 0.f, true);
		return false;
	}
	return true;
}

// --- Приказы ---

bool ANoFearCompanionAIController::IssueOrder(ENoFearCompanionOrder Order, FVector Point, AActor* Target)
{
	APawn* MyPawn = GetPawn();
	if (!MyPawn || !IsRecruited() || UNoFearHealthComponent::IsActorDead(MyPawn))
	{
		return false;
	}
	if (bDowned)
	{
		Say(TEXT("Order"), LOCTEXT("OrderDowned", "Не могу, я ранен!"), 0.f, true);
		return false;
	}

	const double Now = GetNow();
	APawn* LeaderPawn = GetLeaderPawn();
	switch (Order)
	{
	case ENoFearCompanionOrder::Follow:
		ClearOrder();
		SetFollowing(true);
		Say(TEXT("Order"), LOCTEXT("OrderFollow", "Иду за тобой."), 0.f, true);
		break;

	case ENoFearCompanionOrder::Hold:
		ClearOrder();
		SetFollowing(false);
		HomeLocation = MyPawn->GetActorLocation();
		Say(TEXT("Order"), LOCTEXT("OrderHold", "Жду здесь."), 0.f, true);
		break;

	case ENoFearCompanionOrder::MoveTo:
	{
		FVector Goal;
		if (!ProjectToNavigation(GetWorld(), Point, Goal))
		{
			Say(TEXT("Order"), LOCTEXT("OrderNoPath", "Туда не пройти."), 0.f, true);
			return false;
		}
		ClearOrder();
		SetFollowing(false);
		HomeLocation = Goal;
		ActiveOrder = ENoFearCompanionOrder::MoveTo;
		OrderPoint = Goal;
		OrderUntil = Now + 40.0;
		Say(TEXT("Order"), LOCTEXT("OrderMove", "Иду туда."), 0.f, true);
		break;
	}

	case ENoFearCompanionOrder::ComeHere:
		if (!LeaderPawn)
		{
			return false;
		}
		ClearOrder();
		SetFollowing(true);
		ActiveOrder = ENoFearCompanionOrder::ComeHere;
		OrderUntil = Now + 20.0;
		Say(TEXT("Order"), LOCTEXT("OrderComeHere", "Иду к тебе."), 0.f, true);
		break;

	case ENoFearCompanionOrder::Attack:
	{
		const bool bValid = Target && Target != MyPawn && !UNoFearHealthComponent::IsActorIncapacitated(Target)
			&& UNoFearTeamLibrary::AreHostile(MyPawn, Target);
		if (!bValid)
		{
			Say(TEXT("Order"), LOCTEXT("OrderNoTarget", "Не вижу цель."), 0.f, true);
			return false;
		}
		ClearOrder();
		ActiveOrder = ENoFearCompanionOrder::Attack;
		OrderTarget = Target;
		OrderUntil = Now + 30.0;
		// Игрок показал — значит, напарник знает, где враг.
		FNoFearKnownActor& Knowledge = KnownActors.FindOrAdd(Target);
		Knowledge.Awareness = 1.f;
		Knowledge.LastHeardTime = Now;
		if (!Knowledge.bInView)
		{
			Knowledge.LastKnownLocation = Target->GetActorLocation();
		}
		AddMark(Target, true);
		Say(TEXT("Order"), LOCTEXT("OrderAttack", "Атакую!"), 0.f, true);
		break;
	}

	case ENoFearCompanionOrder::Grenade:
	{
		const ANoFearCharacter* BotCharacter = GetNoFearCharacter();
		const UNoFearGrenadeComponent* Grenades = BotCharacter ? BotCharacter->GetGrenadeComponent() : nullptr;
		if (!Grenades || Grenades->GetGrenadeCount() <= 0)
		{
			Say(TEXT("Order"), LOCTEXT("OrderNoGrenades", "Гранат нет."), 0.f, true);
			return false;
		}
		if (LeaderPawn && FVector::Dist(LeaderPawn->GetActorLocation(), Point) < Grenades->GetRadius() + 150.f)
		{
			Say(TEXT("Order"), LOCTEXT("OrderGrenadeTooClose", "Слишком близко к тебе!"), 0.f, true);
			return false;
		}
		ClearOrder();
		ActiveOrder = ENoFearCompanionOrder::Grenade;
		OrderPoint = Point;
		OrderUntil = Now + 12.0;
		Say(TEXT("Order"), LOCTEXT("OrderGrenade", "Понял, гранату туда."), 0.f, true);
		break;
	}

	case ENoFearCompanionOrder::ToggleFire:
		bHoldFire = !bHoldFire;
		Say(TEXT("Order"), bHoldFire ? LOCTEXT("OrderHoldFire", "Не стреляю.") : LOCTEXT("OrderFireAtWill", "Огонь по готовности."), 0.f, true);
		break;

	case ENoFearCompanionOrder::Supply:
	{
		const int32 Kind = PickSupply(true);
		if (Kind == 0)
		{
			Say(TEXT("Order"), SupplyMagazines + SupplyBandages == 0 ? LOCTEXT("SupplyEmpty", "Сам пустой.") : LOCTEXT("SupplyNotNeeded", "Тебе пока хватает."), 0.f, true);
			return false;
		}
		SupplyKind = Kind;
		SupplyUntil = Now + 15.0;
		Say(TEXT("Order"), Kind == 1 ? LOCTEXT("SupplyAmmoGo", "Несу патроны!") : LOCTEXT("SupplyBandageGo", "Несу бинт!"), 0.f, true);
		break;
	}

	default:
		return false;
	}

	OnCompanionStateChanged.Broadcast(this);
	// Отозваться сразу, а не через четверть секунды.
	UpdateBot();
	return true;
}

void ANoFearCompanionAIController::ClearOrder()
{
	if (ActiveOrder == ENoFearCompanionOrder::None && !OrderTarget.IsValid())
	{
		return;
	}
	ActiveOrder = ENoFearCompanionOrder::None;
	OrderTarget = nullptr;
	OrderUntil = 0.0;
	for (FNoFearCompanionMark& Mark : Marks)
	{
		Mark.bOrdered = false;
	}
	OnCompanionStateChanged.Broadcast(this);
}

bool ANoFearCompanionAIController::GetOrderPoint(FVector& OutPoint) const
{
	switch (ActiveOrder)
	{
	case ENoFearCompanionOrder::MoveTo:
	case ENoFearCompanionOrder::Grenade:
		OutPoint = OrderPoint;
		return true;
	case ENoFearCompanionOrder::Attack:
		if (const AActor* Target = OrderTarget.Get())
		{
			OutPoint = Target->GetActorLocation();
			return true;
		}
		return false;
	default:
		break;
	}
	// Ждёт — показать, где.
	if (IsRecruited() && !IsFollowing())
	{
		OutPoint = HomeLocation;
		return true;
	}
	return false;
}

bool ANoFearCompanionAIController::UpdateOrderMove(FVector& OutGoal)
{
	APawn* MyPawn = GetPawn();
	if (!MyPawn || ActiveOrder == ENoFearCompanionOrder::None)
	{
		return false;
	}

	const double Now = GetNow();
	const FVector MyLocation = MyPawn->GetActorLocation();
	if (Now > OrderUntil)
	{
		switch (ActiveOrder)
		{
		case ENoFearCompanionOrder::MoveTo:
			Say(TEXT("OrderDone"), LOCTEXT("OrderMoveFailed", "Не могу туда пройти."), 0.f, true);
			break;
		case ENoFearCompanionOrder::Grenade:
			Say(TEXT("OrderDone"), LOCTEXT("OrderGrenadeFailed", "Не могу бросить."), 0.f, true);
			break;
		case ENoFearCompanionOrder::Attack:
			Say(TEXT("OrderDone"), LOCTEXT("OrderAttackLost", "Цель потерял."), 0.f, true);
			break;
		default:
			break;
		}
		ClearOrder();
		return false;
	}

	switch (ActiveOrder)
	{
	case ENoFearCompanionOrder::MoveTo:
		if (FVector::Dist2D(MyLocation, OrderPoint) <= OrderArriveDistance)
		{
			Say(TEXT("OrderDone"), LOCTEXT("OrderArrived", "На месте."), 0.f, true);
			HomeLocation = OrderPoint;
			ClearOrder();
			return false;
		}
		OutGoal = OrderPoint;
		return true;

	case ENoFearCompanionOrder::ComeHere:
	{
		const APawn* LeaderPawn = GetLeaderPawn();
		if (!LeaderPawn)
		{
			ClearOrder();
			return false;
		}
		if (FVector::Dist2D(MyLocation, LeaderPawn->GetActorLocation()) <= ComeHereDistance)
		{
			Say(TEXT("OrderDone"), LOCTEXT("OrderCameBack", "Я рядом."), 0.f, true);
			ClearOrder();
			return false;
		}
		OutGoal = LeaderPawn->GetActorLocation();
		return true;
	}

	case ENoFearCompanionOrder::Attack:
	{
		AActor* Target = OrderTarget.Get();
		if (!Target || UNoFearHealthComponent::IsActorIncapacitated(Target))
		{
			if (Target)
			{
				Say(TEXT("OrderDone"), LOCTEXT("OrderTargetDown", "Цель уничтожена."), 0.f, true);
			}
			ClearOrder();
			return false;
		}
		AddMark(Target, true);
		// Идёт обычным боем: цель приказа важнее остальных (ScoreTarget).
		return false;
	}

	case ENoFearCompanionOrder::Grenade:
	{
		const ANoFearCharacter* BotCharacter = GetNoFearCharacter();
		UNoFearGrenadeComponent* Grenades = BotCharacter ? BotCharacter->GetGrenadeComponent() : nullptr;
		if (!Grenades || Grenades->GetGrenadeCount() <= 0)
		{
			Say(TEXT("OrderDone"), LOCTEXT("OrderNoGrenadesLeft", "Гранат нет."), 0.f, true);
			ClearOrder();
			return false;
		}
		const float MinDistance = Grenades->GetRadius() + 150.f;
		const float Distance = FVector::Dist(MyLocation, OrderPoint);
		if (Distance >= MinDistance && Distance <= MaxGrenadeDistance && Grenades->CanReach(OrderPoint))
		{
			if (ThrowGrenadeAtSpot(OrderPoint))
			{
				ClearOrder();
				return false;
			}
			// Свои у точки или руки заняты: стоять и пробовать до конца приказа.
			OutGoal = MyLocation;
			return true;
		}
		if (Distance < MinDistance)
		{
			// Слишком близко — отойти от точки.
			const FVector Away = OrderPoint + (MyLocation - OrderPoint).GetSafeNormal2D() * (MinDistance + 150.f);
			if (!ProjectToNavigation(GetWorld(), Away, OutGoal))
			{
				OutGoal = MyLocation;
			}
			return true;
		}
		// Далеко или не докинуть — подойти.
		OutGoal = OrderPoint;
		return true;
	}

	default:
		return false;
	}
}

// --- Снабжение ---

void ANoFearCompanionAIController::UpdateSupplyStock(double Now)
{
	auto Refill = [Now](int32& Count, int32 Max, double& NextTime, float RefillTime)
	{
		if (Count >= Max)
		{
			NextTime = 0.0;
			return;
		}
		if (NextTime <= 0.0)
		{
			NextTime = Now + RefillTime;
			return;
		}
		if (Now >= NextTime)
		{
			++Count;
			NextTime = Count < Max ? Now + RefillTime : 0.0;
		}
	};
	Refill(SupplyMagazines, MaxSupplyMagazines, NextMagazineRefill, MagazineRefillTime);
	Refill(SupplyBandages, MaxSupplyBandages, NextBandageRefill, BandageRefillTime);
}

int32 ANoFearCompanionAIController::PickSupply(bool bOrdered) const
{
	const APawn* LeaderPawn = GetLeaderPawn();
	const UNoFearHealthComponent* Health = UNoFearHealthComponent::FindHealth(LeaderPawn);
	if (!LeaderPawn || !Health || Health->IsDowned())
	{
		return 0;
	}

	const float HealthFraction = Health->GetHealthFraction();
	bool bNeedBandage = false;
	if (SupplyBandages > 0)
	{
		// Сам несёт бинт, когда игрок плох и уже не под огнём; по приказу — при любой ране.
		bNeedBandage = bOrdered ? HealthFraction < 0.95f : HealthFraction < SupplyHealthFraction && Health->GetTimeSinceLastDamage() > 2.f;
	}

	bool bNeedAmmo = false;
	bool bLowAmmo = false;
	const ANoFearWeapon* Weapon = GetPawnWeapon(LeaderPawn);
	const UNoFearWeaponData* Data = Weapon ? Weapon->GetWeaponData() : nullptr;
	if (SupplyMagazines > 0 && Data && Data->MagazineSize > 0)
	{
		// Сам несёт, когда патронов меньше магазина; по приказу — если магазин влезет в запас.
		const int32 TotalAmmo = Weapon->GetMagazineAmmo() + Weapon->GetReserveAmmo();
		bLowAmmo = TotalAmmo < Data->MagazineSize * 2;
		bNeedAmmo = bOrdered ? Weapon->GetReserveAmmo() + Data->MagazineSize <= Data->MaxReserveAmmo
			: TotalAmmo < Data->MagazineSize;
	}

	// Совсем мало здоровья — сначала бинт. По приказу раненому патроны нужнее, только если их почти нет.
	if (bNeedBandage && (HealthFraction < 0.25f || !bNeedAmmo || (bOrdered && !bLowAmmo)))
	{
		return 2;
	}
	return bNeedAmmo ? 1 : (bNeedBandage ? 2 : 0);
}

bool ANoFearCompanionAIController::GiveSupply(int32 Kind)
{
	APawn* LeaderPawn = GetLeaderPawn();
	if (!LeaderPawn)
	{
		return false;
	}

	if (Kind == 1 && SupplyMagazines > 0)
	{
		ANoFearWeapon* Weapon = GetPawnWeapon(LeaderPawn);
		const UNoFearWeaponData* Data = Weapon ? Weapon->GetWeaponData() : nullptr;
		if (Data && Weapon->AddReserveAmmo(Data->MagazineSize) > 0)
		{
			--SupplyMagazines;
			Say(TEXT("SupplyGive"), LOCTEXT("SupplyAmmoGive", "Держи магазин!"), 0.f, true);
			return true;
		}
	}
	else if (Kind == 2 && SupplyBandages > 0)
	{
		UNoFearHealthComponent* Health = UNoFearHealthComponent::FindHealth(LeaderPawn);
		if (Health && Health->Heal(BandageHeal) > 0.f)
		{
			--SupplyBandages;
			Say(TEXT("SupplyGive"), LOCTEXT("SupplyBandageGive", "Держи бинт!"), 0.f, true);
			return true;
		}
	}
	Say(TEXT("SupplyGive"), LOCTEXT("SupplyNotNeededNow", "Тебе пока хватает."), 0.f, true);
	return false;
}

bool ANoFearCompanionAIController::UpdateSupply(FVector& OutGoal)
{
	APawn* MyPawn = GetPawn();
	APawn* LeaderPawn = GetLeaderPawn();
	const double Now = GetNow();
	if (!MyPawn || !LeaderPawn || UNoFearHealthComponent::IsActorIncapacitated(LeaderPawn))
	{
		SupplyKind = 0;
		return false;
	}

	if (SupplyKind == 0)
	{
		// Сам — только идя следом, не под огнём и не чаще SupplyCooldown.
		if (!IsFollowing() || Now < NextSupplyTime)
		{
			return false;
		}
		const UNoFearHealthComponent* MyHealth = UNoFearHealthComponent::FindHealth(MyPawn);
		if (MyHealth && MyHealth->GetTimeSinceLastDamage() < 3.f)
		{
			return false;
		}
		SupplyKind = PickSupply(false);
		if (SupplyKind == 0)
		{
			return false;
		}
		SupplyUntil = Now + 15.0;
		Say(TEXT("SupplyGo"), SupplyKind == 1 ? LOCTEXT("SupplyAmmoAuto", "Несу патроны!") : LOCTEXT("SupplyBandageAuto", "Несу бинт!"), 0.f, true);
	}
	else if (Now > SupplyUntil)
	{
		SupplyKind = 0;
		NextSupplyTime = Now + SupplyCooldown;
		return false;
	}

	if (FVector::Dist(MyPawn->GetActorLocation(), LeaderPawn->GetActorLocation()) <= SupplyGiveDistance)
	{
		GiveSupply(SupplyKind);
		SupplyKind = 0;
		NextSupplyTime = Now + SupplyCooldown;
		return false;
	}
	OutGoal = LeaderPawn->GetActorLocation();
	return true;
}

// --- Линия огня, скрытность ---

bool ANoFearCompanionAIController::UpdateFireLane(FVector& OutGoal)
{
	APawn* MyPawn = GetPawn();
	const double Now = GetNow();
	if (!MyPawn)
	{
		return false;
	}
	if (Now < LaneUntil)
	{
		OutGoal = LaneGoal;
		return true;
	}

	const ANoFearCharacter* LeaderCharacter = Cast<ANoFearCharacter>(GetLeaderPawn());
	const ANoFearPlayerController* PC = GetLeaderController();
	if (!LeaderCharacter || !PC)
	{
		return false;
	}
	const UNoFearWeaponComponent* Weapons = LeaderCharacter->GetWeaponComponent();
	const bool bAiming = Weapons && Weapons->IsAiming();
	const bool bFiring = Now - GetLeaderLastFireTime() < 1.0;
	if (!bAiming && !bFiring)
	{
		return false;
	}

	FVector ViewLocation;
	FRotator ViewRotation;
	PC->GetPlayerViewPoint(ViewLocation, ViewRotation);
	const FVector Direction = ViewRotation.Vector();
	const FVector Chest = MyPawn->GetActorLocation() + FVector(0.f, 0.f, 40.f);
	const FVector ToChest = Chest - ViewLocation;
	const float Along = FVector::DotProduct(ToChest, Direction);
	if (Along < 100.f || Along > FireLaneMaxDistance)
	{
		return false;
	}
	const FVector Offset = ToChest - Direction * Along;
	if (Offset.Size() > FireLaneRadius)
	{
		return false;
	}
	// Между игроком и напарником стена — пуля всё равно не долетит.
	FCollisionQueryParams Params(SCENE_QUERY_STAT(NoFearCompanionFireLane), false, LeaderCharacter);
	Params.AddIgnoredActor(MyPawn);
	if (GetWorld()->LineTraceTestByChannel(ViewLocation, Chest, ECC_Visibility, Params))
	{
		return false;
	}

	// Шаг вбок от линии: в ту сторону, где уже стоит, иначе в другую.
	const FVector Side = FVector::CrossProduct(FVector::UpVector, Direction).GetSafeNormal2D();
	float Sign = FVector::DotProduct(Offset, Side) >= 0.f ? 1.f : -1.f;
	for (int32 Attempt = 0; Attempt < 2; ++Attempt, Sign = -Sign)
	{
		FVector Goal;
		if (!ProjectToNavigation(GetWorld(), MyPawn->GetActorLocation() + Side * Sign * (FireLaneRadius + 170.f), Goal))
		{
			continue;
		}
		LaneGoal = Goal;
		LaneUntil = Now + 1.5;
		if (IsInCover())
		{
			// Укрытие на линии огня игрока: бросить и 6 с не брать снова.
			if (UNoFearBotSubsystem* Bots = UNoFearBotSubsystem::Get(this))
			{
				Bots->ReserveCover(this, Cover.Location);
				LaneReservationUntil = Now + 6.0;
			}
			ReleaseCover();
		}
		Say(TEXT("FireLane"), LOCTEXT("FireLane", "Ухожу с линии огня!"), 10.f);
		OutGoal = LaneGoal;
		return true;
	}
	return false;
}

void ANoFearCompanionAIController::UpdateStealth()
{
	const ACharacter* LeaderCharacter = Cast<ACharacter>(GetLeaderPawn());
	const bool bWantsStealth = IsFollowing() && LeaderCharacter && LeaderCharacter->IsCrouched()
		&& !UNoFearHealthComponent::IsActorIncapacitated(LeaderCharacter) && !GetCurrentTarget()
		&& GetNow() - GetLeaderLastFireTime() > LeaderFireProvokeTime;
	if (bWantsStealth == bSneaking)
	{
		return;
	}

	bSneaking = bWantsStealth;
	if (bSneaking)
	{
		bCrouchedForStealth = !IsCrouching();
		if (bCrouchedForStealth)
		{
			SetCrouching(true);
		}
		Say(TEXT("Stealth"), LOCTEXT("Stealth", "Понял, тихо."), 30.f, true);
	}
	else if (bCrouchedForStealth)
	{
		bCrouchedForStealth = false;
		SetCrouching(false);
	}
	UpdateMovementStance();
	OnCompanionStateChanged.Broadcast(this);
}

// --- Помощь с целями игрока ---

void ANoFearCompanionAIController::BindLeader()
{
	ANoFearPlayerController* PC = GetLeaderController();
	if (BoundLeaderController.Get() != PC)
	{
		if (ANoFearPlayerController* Old = BoundLeaderController.Get())
		{
			Old->OnHitConfirmed.RemoveDynamic(this, &ThisClass::HandleLeaderHitConfirmed);
		}
		BoundLeaderController = PC;
		if (PC)
		{
			PC->OnHitConfirmed.AddUniqueDynamic(this, &ThisClass::HandleLeaderHitConfirmed);
		}
	}

	// Игрок возродился — новая пешка, новое здоровье.
	UNoFearHealthComponent* LeaderHealth = UNoFearHealthComponent::FindHealth(GetLeaderPawn());
	if (BoundLeaderHealth.Get() != LeaderHealth)
	{
		if (UNoFearHealthComponent* Old = BoundLeaderHealth.Get())
		{
			Old->OnDamaged.RemoveDynamic(this, &ThisClass::HandleLeaderDamaged);
		}
		BoundLeaderHealth = LeaderHealth;
		if (LeaderHealth)
		{
			LeaderHealth->OnDamaged.AddUniqueDynamic(this, &ThisClass::HandleLeaderDamaged);
		}
	}
}

void ANoFearCompanionAIController::UnbindLeader()
{
	if (ANoFearPlayerController* PC = BoundLeaderController.Get())
	{
		PC->OnHitConfirmed.RemoveDynamic(this, &ThisClass::HandleLeaderHitConfirmed);
	}
	if (UNoFearHealthComponent* LeaderHealth = BoundLeaderHealth.Get())
	{
		LeaderHealth->OnDamaged.RemoveDynamic(this, &ThisClass::HandleLeaderDamaged);
	}
	BoundLeaderController = nullptr;
	BoundLeaderHealth = nullptr;
}

void ANoFearCompanionAIController::ReleaseHelperReservations()
{
	if (UNoFearBotSubsystem* Bots = UNoFearBotSubsystem::Get(this))
	{
		Bots->ReleaseCover(this);
		if (APawn* LeaderPawn = ReservedLeaderPawn.Get())
		{
			Bots->ReleaseCover(LeaderPawn);
		}
	}
	ReservedLeaderPawn = nullptr;
	LaneReservationUntil = 0.0;
}

void ANoFearCompanionAIController::HandleLeaderDamaged(UNoFearHealthComponent* Component, const FNoFearDamageInfo& Info)
{
	APawn* MyPawn = GetPawn();
	if (!MyPawn)
	{
		return;
	}
	if (Info.bDowned)
	{
		// Лидер упал: самому падать уже нельзя, поднимать будет некому.
		if (UNoFearHealthComponent* MyHealth = BoundSelfHealth.Get())
		{
			MyHealth->bCanBeDowned = false;
		}
	}

	AActor* Attacker = Info.InstigatorController ? Info.InstigatorController->GetPawn() : nullptr;
	if (!Attacker && Info.DamageCauser)
	{
		Attacker = Info.DamageCauser->GetInstigator() ? static_cast<AActor*>(Info.DamageCauser->GetInstigator()) : Info.DamageCauser.Get();
	}
	if (!Attacker || !UNoFearTeamLibrary::AreHostile(MyPawn, Attacker))
	{
		return;
	}
	LeaderAttackers.Add(Attacker, GetNow());
	if (!bDowned)
	{
		ReportThreat(Attacker);
	}
}

void ANoFearCompanionAIController::HandleLeaderHitConfirmed(AActor* Victim, float Damage, bool bHeadshot, bool bKilled)
{
	APawn* MyPawn = GetPawn();
	if (!Victim || !MyPawn || bDowned || !UNoFearTeamLibrary::AreHostile(MyPawn, Victim))
	{
		return;
	}

	const double Now = GetNow();
	if (bKilled)
	{
		LeaderKilledActor = Victim;
		LeaderKillTime = Now;
		if (FMath::FRand() < 0.35f)
		{
			Say(TEXT("NiceShot"), bHeadshot ? LOCTEXT("NiceHeadshot", "Отличный выстрел!") : LOCTEXT("NiceShot", "Хороший выстрел!"), 12.f);
		}
		return;
	}

	// В кого стреляет игрок — туда и напарник; где враг, понятно по выстрелам.
	LeaderHitTarget = Victim;
	LeaderHitTime = Now;
	FNoFearKnownActor& Knowledge = KnownActors.FindOrAdd(Victim);
	Knowledge.Awareness = 1.f;
	Knowledge.LastHeardTime = Now;
	if (!Knowledge.bInView)
	{
		Knowledge.LastKnownLocation = Victim->GetActorLocation();
	}
}

double ANoFearCompanionAIController::GetLeaderLastFireTime() const
{
	const ANoFearWeapon* Weapon = GetPawnWeapon(GetLeaderPawn());
	return Weapon ? Weapon->GetLastFireTime() : -1.0e9;
}

// --- Отметки и реплики ---

bool ANoFearCompanionAIController::DoesLeaderSee(const AActor* Actor) const
{
	const ANoFearPlayerController* PC = GetLeaderController();
	const APawn* LeaderPawn = GetLeaderPawn();
	if (!PC || !LeaderPawn || !Actor)
	{
		return false;
	}

	FVector ViewLocation;
	FRotator ViewRotation;
	PC->GetPlayerViewPoint(ViewLocation, ViewRotation);
	const FVector TargetLocation = Actor->GetActorLocation() + FVector(0.f, 0.f, 40.f);
	const FVector ToTarget = (TargetLocation - ViewLocation).GetSafeNormal();
	if (FVector::DotProduct(ToTarget, ViewRotation.Vector()) < FMath::Cos(FMath::DegreesToRadians(LeaderSightAngle)))
	{
		return false;
	}
	FCollisionQueryParams Params(SCENE_QUERY_STAT(NoFearCompanionLeaderSight), false, LeaderPawn);
	Params.AddIgnoredActor(Actor);
	Params.AddIgnoredActor(GetPawn());
	return !GetWorld()->LineTraceTestByChannel(ViewLocation, TargetLocation, ECC_Visibility, Params);
}

FString ANoFearCompanionAIController::DescribeFromLeader(const FVector& Location) const
{
	const APawn* LeaderPawn = GetLeaderPawn();
	const APawn* Origin = LeaderPawn ? LeaderPawn : GetPawn();
	if (!Origin)
	{
		return FString();
	}

	FRotator View = Origin->GetActorRotation();
	if (const ANoFearPlayerController* PC = GetLeaderController())
	{
		FVector ViewLocation;
		PC->GetPlayerViewPoint(ViewLocation, View);
	}
	const FVector ToLocation = Location - Origin->GetActorLocation();
	const float Yaw = FRotator::NormalizeAxis(ToLocation.Rotation().Yaw - View.Yaw);
	const float AbsYaw = FMath::Abs(Yaw);
	const TCHAR* Direction = AbsYaw <= 45.f ? TEXT("впереди") : (AbsYaw >= 135.f ? TEXT("сзади") : (Yaw > 0.f ? TEXT("справа") : TEXT("слева")));
	int32 Meters = FMath::Max(1, FMath::RoundToInt(ToLocation.Size2D() / 100.f));
	if (Meters > 20)
	{
		Meters = FMath::RoundToInt(Meters / 5.f) * 5;
	}
	return FString::Printf(TEXT("%s, %d м"), Direction, Meters);
}

void ANoFearCompanionAIController::AddMark(AActor* Actor, bool bOrdered)
{
	if (!Actor)
	{
		return;
	}
	const double Until = GetNow() + MarkTime;
	for (FNoFearCompanionMark& Mark : Marks)
	{
		if (Mark.Actor.Get() == Actor)
		{
			Mark.Location = Actor->GetActorLocation();
			Mark.Until = FMath::Max(Mark.Until, Until);
			Mark.bOrdered = Mark.bOrdered || bOrdered;
			return;
		}
	}
	FNoFearCompanionMark& Mark = Marks.AddDefaulted_GetRef();
	Mark.Actor = Actor;
	Mark.Location = Actor->GetActorLocation();
	Mark.Until = Until;
	Mark.bOrdered = bOrdered;
}

void ANoFearCompanionAIController::UpdateMarksAndCallouts()
{
	const double Now = GetNow();
	APawn* MyPawn = GetPawn();
	Marks.RemoveAll([Now](const FNoFearCompanionMark& Mark)
	{
		const AActor* Actor = Mark.Actor.Get();
		return !Actor || Now > Mark.Until || UNoFearHealthComponent::IsActorDead(Actor);
	});
	if (!IsRecruited() || !MyPawn)
	{
		Marks.Reset();
		return;
	}

	// Видит врага, которого не видит игрок, — отметить. Новый контакт (не видели CalloutRepeatTime) — сказать, где.
	TArray<AActor*, TInlineAllocator<4>> NewContacts;
	for (const TPair<TWeakObjectPtr<AActor>, FNoFearKnownActor>& Pair : KnownActors)
	{
		AActor* Actor = Pair.Key.Get();
		const FNoFearKnownActor& Knowledge = Pair.Value;
		if (!Actor || Knowledge.Awareness < 1.f || !Knowledge.bVisible || UNoFearHealthComponent::IsActorIncapacitated(Actor)
			|| !UNoFearTeamLibrary::AreHostile(MyPawn, Actor))
		{
			continue;
		}
		if (!DoesLeaderSee(Actor))
		{
			AddMark(Actor, false);
		}
		else if (FNoFearCompanionMark* Existing = Marks.FindByPredicate([Actor](const FNoFearCompanionMark& Mark) { return Mark.Actor.Get() == Actor; }))
		{
			// Игрок уже видит сам — отметка просто идёт за врагом, пока не погаснет.
			Existing->Location = Actor->GetActorLocation();
		}
		double* Called = CalledOut.Find(Actor);
		if (Called && Now - *Called <= CalloutRepeatTime)
		{
			*Called = Now;
		}
		else
		{
			NewContacts.Add(Actor);
		}
	}
	for (auto It = CalledOut.CreateIterator(); It; ++It)
	{
		if (!It.Key().IsValid() || Now - It.Value() > CalloutRepeatTime)
		{
			It.RemoveCurrent();
		}
	}

	if (NewContacts.Num() > 0)
	{
		NewContacts.Sort([MyPawn](const AActor& A, const AActor& B)
		{
			return FVector::DistSquared(A.GetActorLocation(), MyPawn->GetActorLocation()) < FVector::DistSquared(B.GetActorLocation(), MyPawn->GetActorLocation());
		});
		const AActor* Nearest = NewContacts[0];
		const bool bMonster = IsMonster(Nearest);
		const FText Where = FText::FromString(DescribeFromLeader(Nearest->GetActorLocation()));
		FText Text;
		if (NewContacts.Num() == 1)
		{
			Text = FText::Format(bMonster ? LOCTEXT("ContactMonster", "Монстр {0}!") : LOCTEXT("ContactEnemy", "Контакт {0}!"), Where);
		}
		else
		{
			Text = FText::Format(bMonster ? LOCTEXT("ContactMonsters", "Монстры {0}!") : LOCTEXT("ContactEnemies", "Несколько контактов {0}!"), Where);
		}
		if (Say(TEXT("Contact"), Text, 3.f))
		{
			for (AActor* Actor : NewContacts)
			{
				CalledOut.Add(Actor, Now);
			}
		}
	}
}

void ANoFearCompanionAIController::UpdateCombatBarks()
{
	if (!IsRecruited())
	{
		return;
	}

	const double Now = GetNow();
	AActor* Target = GetCurrentTarget();

	const ANoFearWeapon* Weapon = GetPawnWeapon(GetPawn());
	const bool bReloading = Weapon && Weapon->GetWeaponState() == ENoFearWeaponState::Reloading;
	if (bReloading && !bWasReloading && Target)
	{
		Say(TEXT("Reload"), LOCTEXT("Reloading", "Перезаряжаюсь!"), 8.f);
	}
	bWasReloading = bReloading;

	// Прошлая цель погибла, и добил её не игрок.
	AActor* LastTarget = LastFightTarget.Get();
	if (LastTarget && LastTarget != Target && UNoFearHealthComponent::IsActorDead(LastTarget))
	{
		const bool bLeaderKill = LastTarget == LeaderKilledActor.Get() && Now - LeaderKillTime < 2.0;
		if (!bLeaderKill)
		{
			Say(TEXT("Kill"), FMath::RandBool() ? LOCTEXT("KillOne", "Готов!") : LOCTEXT("KillTwo", "Минус один."), 4.f);
		}
	}
	LastFightTarget = Target;

	const ENoFearAlertState State = GetAlertState();
	if (State == ENoFearAlertState::Calm && (LastAlertState == ENoFearAlertState::Combat || LastAlertState == ENoFearAlertState::Search))
	{
		Say(TEXT("Clear"), LOCTEXT("Clear", "Чисто."), 15.f);
	}
	LastAlertState = State;

	const UNoFearHealthComponent* LeaderHealth = BoundLeaderHealth.Get();
	if (LeaderHealth && !LeaderHealth->IsDead() && !LeaderHealth->IsDowned() && LeaderHealth->GetHealthFraction() < 0.3f
		&& LeaderHealth->GetTimeSinceLastDamage() < 0.5f)
	{
		Say(TEXT("LeaderHurt"), LOCTEXT("LeaderHurt", "Тебя зацепило! Укройся!"), 20.f);
	}
}

void ANoFearCompanionAIController::ReactToPhantomShot()
{
	Say(TEXT("Phantom"), LOCTEXT("PhantomNobody", "Там никого нет."), 3.f, true);
}

bool ANoFearCompanionAIController::Say(FName Key, const FText& Text, float Cooldown, bool bForce)
{
	const double Now = GetNow();
	const double* Next = NextBarkTimes.Find(Key);
	if ((Next && Now < *Next) || (!bForce && Now - LastBarkTime < BarkGap))
	{
		return false;
	}
	NextBarkTimes.Add(Key, Now + Cooldown);
	LastBarkTime = Now;

	// Крадёмся — только шёпотом по рации, без реплики над головой.
	if (!bSneaking)
	{
		Shout(Text, 2.f);
	}
	if (ANoFearPlayerController* PC = GetLeaderController())
	{
		PC->AddRadioLine(GetPawn(), Text, bSneaking);
	}
	return true;
}

ENoFearCompanionActivity ANoFearCompanionAIController::GetActivity() const
{
	if (bDowned || UNoFearHealthComponent::IsActorDowned(GetPawn()))
	{
		return ENoFearCompanionActivity::Downed;
	}
	if (!IsRecruited())
	{
		return GetCurrentTarget() ? ENoFearCompanionActivity::Fighting : ENoFearCompanionActivity::Idle;
	}
	if (bReviving || UNoFearHealthComponent::IsActorDowned(GetLeaderPawn()))
	{
		return ENoFearCompanionActivity::Reviving;
	}
	switch (ActiveOrder)
	{
	case ENoFearCompanionOrder::MoveTo:
		return ENoFearCompanionActivity::Moving;
	case ENoFearCompanionOrder::ComeHere:
		return ENoFearCompanionActivity::ComingBack;
	case ENoFearCompanionOrder::Grenade:
		return ENoFearCompanionActivity::ThrowingGrenade;
	case ENoFearCompanionOrder::Attack:
		return ENoFearCompanionActivity::Attacking;
	default:
		break;
	}
	if (SupplyKind != 0)
	{
		return ENoFearCompanionActivity::Supplying;
	}
	if (GetNow() < LaneUntil)
	{
		return ENoFearCompanionActivity::Dodging;
	}
	if (GetCurrentTarget())
	{
		return ENoFearCompanionActivity::Fighting;
	}
	if (bSneaking)
	{
		return ENoFearCompanionActivity::Sneaking;
	}
	return IsFollowing() ? ENoFearCompanionActivity::Following : ENoFearCompanionActivity::Waiting;
}

#undef LOCTEXT_NAMESPACE
