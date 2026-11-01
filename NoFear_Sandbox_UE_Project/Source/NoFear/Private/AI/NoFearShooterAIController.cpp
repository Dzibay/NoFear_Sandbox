#include "AI/NoFearShooterAIController.h"

#include "AI/NoFearBotSubsystem.h"
#include "AI/NoFearCombatComponent.h"
#include "AI/NoFearSquadSubsystem.h"
#include "BehaviorTree/BehaviorTree.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "Core/NoFearHealthComponent.h"
#include "Core/NoFearPerf.h"
#include "Core/NoFearAISettings.h"
#include "AI/NoFearAILodSubsystem.h"
#include "NavigationSystem.h"
#include "Animation/NoFearPoseComponent.h"
#include "Core/NoFearCharacter.h"
#include "Core/NoFearTeam.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Weapons/NoFearGrenadeComponent.h"
#include "UObject/ConstructorHelpers.h"
#include "Weapons/NoFearWeapon.h"
#include "Weapons/NoFearWeaponComponent.h"
#include "Weapons/NoFearWeaponData.h"

ANoFearShooterAIController::ANoFearShooterAIController(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	LeashRadius = 1500.f;

	// Стрелки зовут своих в бою и замечают тела погибших своих.
	AlertShareRadius = 2500.f;
	bReactToCorpses = true;
	BotGrenades = 2;

	static ConstructorHelpers::FObjectFinderOptional<UBehaviorTree> Tree(TEXT("/Game/AI/BT_NoFear_Soldier.BT_NoFear_Soldier"));
	BehaviorTree = Tree.Get();

	static ConstructorHelpers::FObjectFinderOptional<UNoFearWeaponData> Shotgun(TEXT("/Game/Weapons/Data/DA_Shotgun.DA_Shotgun"));
	if (Shotgun.Get())
	{
		RoleWeapons.Add(ENoFearSquadRole::Assault, Shotgun.Get());
	}
}

ENoFearSquadRole ANoFearShooterAIController::GetSquadRole() const
{
	const UNoFearSquadSubsystem* Squads = UNoFearSquadSubsystem::Get(this);
	return Squads ? Squads->GetSquadRole(GetPawn()) : ENoFearSquadRole::None;
}

float ANoFearShooterAIController::ScoreTarget(const AActor* Candidate, const FNoFearKnownActor& Knowledge) const
{
	float Score = Super::ScoreTarget(Candidate, Knowledge);
	if (SquadTargetSharePenalty > 0.f)
	{
		if (const UNoFearSquadSubsystem* Squads = UNoFearSquadSubsystem::Get(this))
		{
			// Делением, а не вычитанием: оценка остаётся положительной, и единственную цель (её бьёт весь отряд) боец не теряет.
			Score /= 1.f + SquadTargetSharePenalty * Squads->CountSquadmatesTargeting(GetPawn(), Candidate);
		}
	}
	if (Candidate && Candidate == SquadOrder.Enemy)
	{
		// Враг из приказа (подавить, обойти, прикрыть от него): по нему, если видимый не мешает.
		Score += 0.5f;
	}
	return Score;
}

FVector ANoFearShooterAIController::GetLeashAnchor() const
{
	FVector Anchor;
	float Radius = 0.f;
	const UNoFearSquadSubsystem* Squads = UNoFearSquadSubsystem::Get(this);
	return Squads && Squads->GetSquadLeash(GetPawn(), Anchor, Radius) ? Anchor : Super::GetLeashAnchor();
}

float ANoFearShooterAIController::GetLeashRadius() const
{
	FVector Anchor;
	float Radius = 0.f;
	const UNoFearSquadSubsystem* Squads = UNoFearSquadSubsystem::Get(this);
	return Squads && Squads->GetSquadLeash(GetPawn(), Anchor, Radius) ? Radius : Super::GetLeashRadius();
}

// --- Приказы отряда ---

namespace
{
	/** Приказ, по которому боец ищет новое укрытие и идёт туда. */
	bool IsCoverMoveOrder(ENoFearSquadOrder Order)
	{
		switch (Order)
		{
		case ENoFearSquadOrder::TakeCover:
		case ENoFearSquadOrder::Bound:
		case ENoFearSquadOrder::Flank:
		case ENoFearSquadOrder::Rush:
		case ENoFearSquadOrder::FallBack:
		case ENoFearSquadOrder::Reinforce:
		case ENoFearSquadOrder::CoverRescue:
			return true;
		default:
			return false;
		}
	}
}

void ANoFearShooterAIController::UpdateSquadOrder()
{
	const UNoFearSquadSubsystem* Squads = UNoFearSquadSubsystem::Get(this);
	SquadOrder = Squads ? Squads->GetSquadOrderInfo(GetPawn()) : FNoFearSquadOrderInfo();
	if (SquadOrder.Serial == HandledOrderSerial)
	{
		return;
	}
	if (IsCoverMoveOrder(SquadOrder.Order) && SquadOrder.Order != ENoFearSquadOrder::FallBack && !GetCurrentTarget()
		&& GetNow() - SquadOrder.Time < 1.5)
	{
		// Цель ещё не выбрана (сведения отряда дойдут при ближайшем обновлении): принять приказ чуть позже,
		// иначе укрытие по нему снимется сразу же — без цели укрытий не держат.
		return;
	}
	HandledOrderSerial = SquadOrder.Serial;
	bOrderStuck = !ExecuteSquadOrder();
}

bool ANoFearShooterAIController::ExecuteSquadOrder()
{
	APawn* MyPawn = GetPawn();
	const ENoFearSquadOrder Order = SquadOrder.Order;
	if (!MyPawn)
	{
		return false;
	}
	if (Order != ENoFearSquadOrder::None)
	{
		// По приказу не уходят вперёд сами.
		AdvanceUntil = -1.0;
	}
	if (!IsCoverMoveOrder(Order))
	{
		// Огонь, оборона, перевязка, сбор: укрытие не меняется, остальное — дело дерева (IsSuppressing, HealTarget, RallyLocation).
		return true;
	}
	if (bRetreating && Order != ENoFearSquadOrder::FallBack)
	{
		// Раненый отходит сам; отход отряда ему по пути.
		return false;
	}
	if (Order == ENoFearSquadOrder::Flank)
	{
		// Укрытие на фланге командир уже занял за бойцом (SearchFlankCover).
		return SquadOrder.bHasLocation && HasCover() && FVector::Dist2D(Cover.Location, SquadOrder.Location) < 150.f;
	}
	if (!bUseCover)
	{
		return Order == ENoFearSquadOrder::FallBack || Order == ENoFearSquadOrder::Rush || Order == ENoFearSquadOrder::Reinforce
			|| Order == ENoFearSquadOrder::CoverRescue;
	}

	AActor* Threat = nullptr;
	FVector ThreatLocation;
	if (!GetOrderThreat(Threat, ThreatLocation))
	{
		// Не от кого прятаться: отход, подмога и прикрытие спасения — в открытую (GetOrderMoveGoal), остальное выполнить нельзя.
		if (Order == ENoFearSquadOrder::FallBack || Order == ENoFearSquadOrder::CoverRescue)
		{
			ReleaseCover();
			return true;
		}
		return false;
	}
	const float Current = FVector::Dist2D(MyPawn->GetActorLocation(), ThreatLocation);
	FNoFearCoverSearch Search;
	MakeCoverSearch(Order == ENoFearSquadOrder::FallBack, Threat, ThreatLocation, Search);

	switch (Order)
	{
	case ENoFearSquadOrder::TakeCover:
	{
		const bool bFar = Current > Search.MaxThreatDistance - 200.f;
		if (HasCover() && (!bFar || FVector::Dist2D(Cover.Location, ThreatLocation) >= Current - ContactCoverAdvance))
		{
			return true;
		}
		if (bFar)
		{
			// Контакт издалека: залечь рядом. Укрытие далеко впереди (выбранное самим до приказа) бросить —
			// бежать к нему в открытую нельзя, сближаться отряд будет перебежками.
			if (!ApplyContactCoverSearch(Search, Current))
			{
				ReleaseCover();
			}
			return true;
		}
		return HasCover() || ApplyCoverSearch(Search);
	}

	case ENoFearSquadOrder::Bound:
		// Перебежка: укрытие ближе к врагу хотя бы на BoundGain.
		Search.MaxThreatDistance = FMath::Min(Search.MaxThreatDistance, FMath::Max(Current - BoundGain, MinCoverThreatDistance + 50.f));
		return ApplyCoverSearch(Search);

	case ENoFearSquadOrder::Rush:
		// Натиск: укрытие заметно ближе; нет — сблизиться в открытую (FindCombatPosition).
		Search.MaxThreatDistance = FMath::Min(Search.MaxThreatDistance, FMath::Max(Current - RushGain, MinCoverThreatDistance + 50.f));
		if (!ApplyCoverSearch(Search))
		{
			ReleaseCover();
		}
		return true;

	case ENoFearSquadOrder::FallBack:
		// Отход перекатом: дальше от врага и к рубежу; нет укрытия — к рубежу в открытую.
		Search.RetreatGain = 200.f;
		Search.LeashRadius = 0.f;
		if (SquadOrder.bHasLocation)
		{
			// Каждый перекат — ближе к рубежу (а не просто прочь от врага: отошедший вбок уходил бы от рубежа).
			Search.GoalLocation = SquadOrder.Location;
			Search.GoalWeight = 1.f;
			Search.GoalGain = Search.RetreatGain;
		}
		if (!ApplyCoverSearch(Search))
		{
			ReleaseCover();
		}
		return true;

	case ENoFearSquadOrder::Reinforce:
	{
		// Подмога: укрытие рядом с прижатым своим; нет — к нему в открытую.
		const AActor* Mate = SquadOrder.Target;
		if (!Mate)
		{
			return false;
		}
		const FVector MateLocation = Mate->GetActorLocation();
		Search.bUseSearchOrigin = true;
		Search.SearchOrigin = MateLocation;
		Search.SearchRadius = ReinforceSearchRadius;
		Search.MaxThreatDistance = FMath::Max(Search.MaxThreatDistance, FVector::Dist2D(MateLocation, ThreatLocation) + 500.f);
		Search.GoalLocation = MateLocation;
		Search.GoalWeight = 0.5f;
		if (!ApplyCoverSearch(Search))
		{
			ReleaseCover();
		}
		return true;
	}

	case ENoFearSquadOrder::CoverRescue:
	{
		// Прикрытие спасения: укрытие у своего места рядом с целью; нет — на место в открытую.
		if (!SquadOrder.bHasLocation)
		{
			return false;
		}
		Search.bUseSearchOrigin = true;
		Search.SearchOrigin = SquadOrder.Location;
		Search.SearchRadius = ReinforceSearchRadius;
		Search.MaxThreatDistance = FMath::Max(Search.MaxThreatDistance, FVector::Dist2D(SquadOrder.Location, ThreatLocation) + 500.f);
		Search.GoalLocation = SquadOrder.Location;
		Search.GoalWeight = 0.5f;
		if (!ApplyCoverSearch(Search))
		{
			ReleaseCover();
		}
		return true;
	}

	default:
		return true;
	}
}

bool ANoFearShooterAIController::IsMovingToOrderCover() const
{
	return IsCoverMoveOrder(SquadOrder.Order) && HandledOrderSerial == SquadOrder.Serial && !bOrderStuck && HasCover() && !IsInCover();
}

bool ANoFearShooterAIController::GetOrderThreat(AActor*& OutThreat, FVector& OutLocation) const
{
	if (AActor* Target = GetCurrentTarget())
	{
		OutThreat = Target;
		OutLocation = GetLastKnownTargetLocation();
		return true;
	}
	if (SquadOrder.Enemy && SquadOrder.bHasEnemyLocation)
	{
		OutThreat = SquadOrder.Enemy.Get();
		OutLocation = SquadOrder.EnemyLocation;
		return true;
	}
	return false;
}

bool ANoFearShooterAIController::GetOrderMoveGoal(FVector& OutGoal) const
{
	switch (SquadOrder.Order)
	{
	case ENoFearSquadOrder::Regroup:
	case ENoFearSquadOrder::Assist:
		// Сбор и помощь чужому отряду — пока свой враг не попался на глаза.
		OutGoal = SquadOrder.Location;
		return SquadOrder.bHasLocation && !GetCurrentTarget();
	case ENoFearSquadOrder::FallBack:
	{
		// Дошёл до рубежа — дальше бой как обычно.
		const APawn* MyPawn = GetPawn();
		OutGoal = SquadOrder.Location;
		return SquadOrder.bHasLocation && !HasCover() && MyPawn && FVector::Dist2D(MyPawn->GetActorLocation(), OutGoal) > 150.f;
	}
	case ENoFearSquadOrder::Reinforce:
	{
		const AActor* Mate = SquadOrder.Target;
		const APawn* MyPawn = GetPawn();
		if (!Mate || !MyPawn || HasCover())
		{
			return false;
		}
		// Встать рядом со своим, со своей стороны; уже рядом — драться.
		const FVector MateLocation = Mate->GetActorLocation();
		const FVector FromMate = MyPawn->GetActorLocation() - MateLocation;
		if (FromMate.Size2D() <= 450.f)
		{
			return false;
		}
		OutGoal = MateLocation + FromMate.GetSafeNormal2D() * 350.f;
		return true;
	}
	case ENoFearSquadOrder::CoverRescue:
	{
		// На своё место у цели спасения, пока не в бою и без укрытия.
		const APawn* MyPawn = GetPawn();
		OutGoal = SquadOrder.Location;
		return SquadOrder.bHasLocation && !GetCurrentTarget() && !HasCover() && MyPawn && FVector::Dist2D(MyPawn->GetActorLocation(), OutGoal) > 120.f;
	}
	default:
		return false;
	}
}

bool ANoFearShooterAIController::IsSuppressing() const
{
	switch (SquadOrder.Order)
	{
	case ENoFearSquadOrder::Suppress:
	case ENoFearSquadOrder::CoverFallback:
	case ENoFearSquadOrder::CoveringFire:
		break;
	default:
		return false;
	}
	// По цели, о которой есть свежие сведения (свои глаза или отряд): по забытому месту не стреляют.
	const AActor* Target = GetCurrentTarget();
	const FNoFearKnownActor* Known = Target ? KnownActors.Find(Target) : nullptr;
	return Known && GetNow() - FMath::Max(Known->LastSeenTime, Known->LastHeardTime) <= MaxUnseenTime;
}

APawn* ANoFearShooterAIController::GetHealTarget() const
{
	APawn* Patient = SquadOrder.Order == ENoFearSquadOrder::Heal ? Cast<APawn>(SquadOrder.Target.Get()) : nullptr;
	return Patient && !UNoFearHealthComponent::IsActorDead(Patient) ? Patient : nullptr;
}

bool ANoFearShooterAIController::IsAwaitingMedic() const
{
	const APawn* MyPawn = GetPawn();
	const UNoFearSquadSubsystem* Squads = UNoFearSquadSubsystem::Get(this);
	const APawn* Medic = Squads && MyPawn ? Squads->GetMedicFor(MyPawn) : nullptr;
	return Medic && FVector::Dist2D(Medic->GetActorLocation(), MyPawn->GetActorLocation()) <= MedicWaitRadius;
}

bool ANoFearShooterAIController::SearchFlankCover(AActor* Threat, const FVector& ThreatLocation, const FVector& FlankFrom, float MinAngle, FVector& OutCover)
{
	// Без своей цели укрытие не удержится (UpdateCover снимет его), раненому не до обхода.
	if (!GetPawn() || !bUseCover || !Threat || !GetCurrentTarget() || bRetreating)
	{
		FlankSearchStats = FString::Printf(TEXT("skip: target=%d retreat=%d"), GetCurrentTarget() != nullptr, bRetreating);
		return false;
	}
	FNoFearCoverSearch Search;
	MakeCoverSearch(false, Threat, ThreatLocation, Search);
	Search.SearchRadius = FMath::Max(CoverSearchRadius, 2500.f);
	// Укрытия за препятствиями узкие: на дальних кольцах точки чаще (шаг 180 см до радиуса ~2500).
	Search.MaxRingSamples = 90;
	// Фланг — ради перекрёстного огня, а не дистанции оружия: дробовик тоже может обойти издалека (оценка всё равно тянет ближе).
	if (Search.MaxThreatDistance > 0.f)
	{
		Search.MaxThreatDistance = FMath::Max(Search.MaxThreatDistance, FlankMaxThreatDistance);
	}
	if (Search.LeashRadius > 0.f)
	{
		Search.LeashRadius *= 1.5f;
	}
	Search.MinFlankAngle = MinAngle;
	Search.FlankFrom = FlankFrom;
	Search.bHiddenPath = true;
	// Обход длиннее прямой дороги.
	Search.MaxPathLengthFactor = 2.2f;
	// Лучшие по оценке точки часто с открытым подходом: проверить побольше (обход ищут редко, раз в FlankCooldown).
	Search.MaxPathChecks = 16;
	const bool bFound = ApplyCoverSearch(Search);
	FlankSearchStats = UNoFearCoverLibrary::GetLastSearchStats();
	if (!bFound)
	{
		return false;
	}
	OutCover = Cover.Location;
	// Приказ дойдёт при ближайшем обновлении бойца: до тех пор укрытие не перепроверять.
	NextCoverCheckTime = GetNow() + 1.0;
	bCoverRefreshRequested = false;
	CoverLostChecks = 0;
	return true;
}

UNoFearWeaponComponent* ANoFearShooterAIController::GetWeaponComponent() const
{
	return GetPawn() ? GetPawn()->FindComponentByClass<UNoFearWeaponComponent>() : nullptr;
}

UNoFearCombatComponent* ANoFearShooterAIController::GetCombatComponent() const
{
	return GetPawn() ? GetPawn()->FindComponentByClass<UNoFearCombatComponent>() : nullptr;
}

bool ANoFearShooterAIController::IsTargetVisible() const
{
	// Зрение восприятия видит тело целиком; компонент боя проверяет линию огня до груди и головы.
	if (const UNoFearCombatComponent* Combat = GetCombatComponent(); Combat && Combat->GetTarget() == GetCurrentTarget())
	{
		return Combat->HasLineOfSight();
	}
	return Super::IsTargetVisible();
}

void ANoFearShooterAIController::OnPossess(APawn* InPawn)
{
	// Компонент нужен до старта дерева: он читает цель из блэкборда и пишет туда CanFire.
	if (InPawn && bAddCombatComponentIfMissing && !InPawn->FindComponentByClass<UNoFearCombatComponent>())
	{
		UNoFearCombatComponent* Combat = NewObject<UNoFearCombatComponent>(InPawn, TEXT("CombatComponent"));
		Combat->RegisterComponent();
	}

	Super::OnPossess(InPawn);
}

void ANoFearShooterAIController::OnUnPossess()
{
	ReleaseCover();
	if (UNoFearWeaponComponent* Weapons = GetWeaponComponent())
	{
		Weapons->StopFire();
		Weapons->SetAiming(false);
	}
	Super::OnUnPossess();
}

void ANoFearShooterAIController::UpdateBot()
{
	Super::UpdateBot();

	if (GetPawn() && !UNoFearHealthComponent::IsActorDead(GetPawn()))
	{
		MaintainWeapon();
		UpdateSquadOrder();
		UpdateRescueFacing();
		UpdateCover();
		if (IsSuppressing())
		{
			// Подавление: цель не видна — стрелять туда, где её видели свои.
			if (UNoFearCombatComponent* Combat = GetCombatComponent(); Combat && !Combat->HasLineOfSight())
			{
				Combat->SetBlindAimPoint(GetLastKnownTargetLocation() + FVector(0.f, 0.f, 30.f));
			}
		}
		TryThrowGrenade();
		WriteBlackboard();
	}
	else
	{
		if (HasCover())
		{
			ReleaseCover();
		}
		SquadOrder = FNoFearSquadOrderInfo();
	}
}

void ANoFearShooterAIController::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	ReleaseCover();
	Super::EndPlay(EndPlayReason);
}

void ANoFearShooterAIController::WriteBlackboard()
{
	Super::WriteBlackboard();
	UBlackboardComponent* BlackboardComp = GetBlackboardComponent();
	if (!BlackboardComp)
	{
		return;
	}
	if (HasCover())
	{
		BlackboardComp->SetValueAsVector(FNoFearBotKeys::CoverLocation, Cover.Location);
	}
	else
	{
		BlackboardComp->ClearValue(FNoFearBotKeys::CoverLocation);
	}
	BlackboardComp->SetValueAsBool(FNoFearBotKeys::InCover, IsInCover());
	BlackboardComp->SetValueAsBool(FNoFearBotKeys::CoverIsLow, IsCoverLow());
	BlackboardComp->SetValueAsBool(FNoFearBotKeys::IsRetreating, bRetreating);
	BlackboardComp->SetValueAsBool(FNoFearBotKeys::InWeaponRange, IsTargetInWeaponRange());

	// Приказ отряда: перевязка, сбор и подавление ведут свои ветки дерева.
	if (APawn* Patient = GetHealTarget())
	{
		BlackboardComp->SetValueAsObject(FNoFearBotKeys::HealTarget, Patient);
	}
	else
	{
		BlackboardComp->ClearValue(FNoFearBotKeys::HealTarget);
	}
	// Санитар рядом, а укрытия, где пересидеть, нет: стоять, где застал санитар.
	const bool bHoldForMedic = IsAwaitingMedic() && !(HasCover() && GetCurrentTarget());
	if (bHoldForMedic && !bHoldingForMedic && GetPawn())
	{
		MedicWaitLocation = GetPawn()->GetActorLocation();
	}
	bHoldingForMedic = bHoldForMedic;
	FVector Rally;
	if (GetOrderMoveGoal(Rally))
	{
		BlackboardComp->SetValueAsVector(FNoFearBotKeys::RallyLocation, Rally);
	}
	else if (bHoldingForMedic)
	{
		BlackboardComp->SetValueAsVector(FNoFearBotKeys::RallyLocation, MedicWaitLocation);
	}
	else
	{
		BlackboardComp->ClearValue(FNoFearBotKeys::RallyLocation);
	}
	const UNoFearCombatComponent* Combat = GetCombatComponent();
	BlackboardComp->SetValueAsBool(FNoFearBotKeys::IsSuppressing, IsSuppressing() && !HasCover() && Combat && !Combat->HasLineOfSight());
	BlackboardComp->SetValueAsInt(FNoFearBotKeys::SquadOrder, static_cast<int32>(SquadOrder.Order));
}

bool ANoFearShooterAIController::IsInCover() const
{
	const APawn* MyPawn = GetPawn();
	return HasCover() && MyPawn && FVector::Dist2D(MyPawn->GetActorLocation(), Cover.Location) < 60.f;
}

bool ANoFearShooterAIController::IsCoveringFire() const
{
	return GetNow() < CoveringFireUntil || SquadOrder.Order == ENoFearSquadOrder::CoveringFire || SquadOrder.Order == ENoFearSquadOrder::CoverFallback;
}

FVector2D ANoFearShooterAIController::GetPreferredRange() const
{
	const UNoFearWeaponComponent* Weapons = GetWeaponComponent();
	const ANoFearWeapon* Weapon = Weapons ? Weapons->GetCurrentWeapon() : nullptr;
	const UNoFearWeaponData* Data = Weapon ? Weapon->GetWeaponData() : nullptr;
	return Data ? Data->GetBotPreferredRange() : FVector2D(800.f, 2000.f);
}

bool ANoFearShooterAIController::IsTargetInWeaponRange() const
{
	const APawn* MyPawn = GetPawn();
	if (!MyPawn || !GetCurrentTarget())
	{
		return false;
	}
	// Чуть дальше удобной дистанции ещё стреляют, чтобы не бегать туда-сюда на границе.
	return FVector::Dist2D(MyPawn->GetActorLocation(), GetLastKnownTargetLocation()) <= GetPreferredRange().Y * 1.25f + 100.f;
}

void ANoFearShooterAIController::RequestCoverRefresh()
{
	bCoverRefreshRequested = true;
	NextCoverCheckTime = 0.0;
}

void ANoFearShooterAIController::ReleaseCover()
{
	if (UNoFearBotSubsystem* Bots = UNoFearBotSubsystem::Get(this))
	{
		if (AActor* Reserved = CoverOwner.Get())
		{
			Bots->ReleaseCover(Reserved);
		}
	}
	Cover = FNoFearCoverPoint();
	CoverOwner.Reset();
	CoverLostChecks = 0;
	CoverPhase.Reset();
}

bool ANoFearShooterAIController::SearchCover(bool bRetreat)
{
	AActor* Target = GetCurrentTarget();
	if (!GetPawn() || !Target)
	{
		return false;
	}
	FNoFearCoverSearch Search;
	MakeCoverSearch(bRetreat, Target, GetLastKnownTargetLocation(), Search);
	if (!bRetreat)
	{
		const float Current = FVector::Dist2D(GetPawn()->GetActorLocation(), Search.ThreatLocation);
		switch (SquadOrder.Order)
		{
		case ENoFearSquadOrder::TakeCover:
			if (Current > Search.MaxThreatDistance - 200.f)
			{
				// Контакт издалека: только укрытия рядом, к дальним отряд пойдёт перебежками.
				return ApplyContactCoverSearch(Search, Current);
			}
			break;
		case ENoFearSquadOrder::Rush:
			// Натиск отряда: укрытие только ближе к врагу.
			Search.MaxThreatDistance = FMath::Min(Search.MaxThreatDistance, FMath::Max(Current - 300.f, MinCoverThreatDistance + 50.f));
			break;
		case ENoFearSquadOrder::Bound:
		case ENoFearSquadOrder::Flank:
		case ENoFearSquadOrder::Hold:
			// Отряд продвинулся или держит рубеж: сменить укрытие можно, отступить назад — нет.
			Search.MaxThreatDistance = FMath::Min(Search.MaxThreatDistance, FMath::Max(Current + 150.f, MinCoverThreatDistance + 50.f));
			break;
		case ENoFearSquadOrder::Suppress:
		case ENoFearSquadOrder::CoveringFire:
		case ENoFearSquadOrder::CoverFallback:
			// Прикрывает огнём: укрытие сменить можно, лезть к врагу — нет.
			if (Current - 200.f >= Search.MaxThreatDistance)
			{
				// Не достаёт оттуда, где стоит: только укрытие рядом — ближе к врагу отряд идёт перебежками,
				// а прикрывающий в открытую не бежит.
				return ApplyContactCoverSearch(Search, Current);
			}
			Search.MinThreatDistance = FMath::Max(Search.MinThreatDistance, Current - 200.f);
			break;
		case ENoFearSquadOrder::FallBack:
			// Отходит к рубежу (укрытия по пути не нашлось или старое пропало): сменить укрытие можно,
			// но не навстречу врагу и не дальше от рубежа — иначе обычный поиск без поводка уводил назад к врагу.
			if (Current - 200.f < Search.MaxThreatDistance)
			{
				Search.MinThreatDistance = FMath::Max(Search.MinThreatDistance, Current - 200.f);
			}
			if (SquadOrder.bHasLocation)
			{
				Search.LeashAnchor = SquadOrder.Location;
				Search.LeashRadius = FMath::Max(FVector::Dist2D(GetPawn()->GetActorLocation(), SquadOrder.Location), 500.f);
			}
			break;
		default:
			break;
		}
	}
	return ApplyCoverSearch(Search);
}

void ANoFearShooterAIController::MakeCoverSearch(bool bRetreat, AActor* Threat, const FVector& ThreatLocation, FNoFearCoverSearch& OutSearch) const
{
	APawn* MyPawn = GetPawn();
	const FVector2D Range = GetPreferredRange();
	OutSearch = FNoFearCoverSearch();
	OutSearch.Pawn = MyPawn;
	OutSearch.ThreatLocation = ThreatLocation;
	OutSearch.ThreatActor = Threat;
	OutSearch.SearchRadius = bRetreat ? RetreatSearchRadius : CoverSearchRadius;
	OutSearch.MinThreatDistance = MinCoverThreatDistance;
	OutSearch.PreferredRange = Range;
	OutSearch.bRetreat = bRetreat;
	OutSearch.RetreatGain = RetreatGain;
	OutSearch.MaxObstacleDistance = CoverMaxObstacleDistance;
	OutSearch.ReserveRadius = CoverReserveRadius;
	OutSearch.LeashAnchor = GetLeashAnchor();
	OutSearch.LeashRadius = GetLeashRadius() > 0.f && bRetreat ? GetLeashRadius() * 1.5f : GetLeashRadius();
	if (const UNoFearAILodSubsystem* Lod = UNoFearAILodSubsystem::Get(this))
	{
		// Далёкий бот: меньше точек и проверок пути — игрок разницы не увидит.
		const ENoFearAILod Level = Lod->GetBotLod(this);
		const FNoFearAISearchTuning& Tuning = UNoFearAISettings::Get()->Search;
		const float Scale = Level == ENoFearAILod::Far ? Tuning.FarSampleScale : Level == ENoFearAILod::Mid ? Tuning.MidSampleScale : 1.f;
		OutSearch.MaxRingSamples = FMath::Max(6, FMath::RoundToInt(OutSearch.MaxRingSamples * Scale));
		OutSearch.MaxPathChecks = FMath::Max(2, FMath::RoundToInt(OutSearch.MaxPathChecks * Scale));
	}
	if (!bRetreat)
	{
		// Не дальше, чем бьёт оружие: дробовик ищет укрытие рядом с целью.
		OutSearch.MaxThreatDistance = Range.Y * 1.25f + 200.f;
		if (MyPawn && GetNow() < AdvanceUntil)
		{
			// Цель давно не видна: только укрытия ближе к ней.
			const float Current = FVector::Dist2D(MyPawn->GetActorLocation(), ThreatLocation);
			OutSearch.MaxThreatDistance = FMath::Min(OutSearch.MaxThreatDistance, FMath::Max(Current - 300.f, MinCoverThreatDistance + 50.f));
		}
	}
}

bool ANoFearShooterAIController::ApplyCoverSearch(const FNoFearCoverSearch& Search)
{
	APawn* MyPawn = GetPawn();
	FNoFearCoverPoint Found;
	if (!MyPawn || !UNoFearCoverLibrary::FindCover(Search, Found))
	{
		return false;
	}
	ReleaseCover();
	Cover = Found;
	CoverOwner = MyPawn;
	if (UNoFearBotSubsystem* Bots = UNoFearBotSubsystem::Get(this))
	{
		Bots->ReserveCover(MyPawn, Found.Location);
	}
	return true;
}

bool ANoFearShooterAIController::ApplyContactCoverSearch(const FNoFearCoverSearch& Search, float Current)
{
	if (Current <= Search.MaxThreatDistance - 200.f)
	{
		return false;
	}
	// Залечь рядом, а не бежать в открытую к дистанции оружия: сближаться отряд будет перебежками.
	FNoFearCoverSearch Near = Search;
	Near.MaxThreatDistance = Current + 150.f;
	Near.MinThreatDistance = FMath::Max(Search.MinThreatDistance, Current - ContactCoverAdvance);
	return ApplyCoverSearch(Near);
}

void ANoFearShooterAIController::UpdateRescueFacing()
{
	// Прикрывающий спасение на своём месте без цели смотрит на портал, откуда полезут монстры.
	const APawn* MyPawn = GetPawn();
	const bool bFace = MyPawn && !IsPortalControlled() && !GetCurrentTarget() && SquadOrder.Order == ENoFearSquadOrder::CoverRescue
		&& SquadOrder.bHasFacing && SquadOrder.bHasLocation && FVector::Dist2D(MyPawn->GetActorLocation(), SquadOrder.Location) <= 200.f;
	if (bFace)
	{
		SetFocalPoint(SquadOrder.FacingLocation, EAIFocusPriority::Gameplay);
		bRescueFacing = true;
	}
	else if (bRescueFacing)
	{
		bRescueFacing = false;
		if (!IsPortalControlled() && !GetCurrentTarget())
		{
			ClearFocus(EAIFocusPriority::Gameplay);
		}
	}
}

void ANoFearShooterAIController::UpdateCover()
{
	APawn* MyPawn = GetPawn();
	AActor* Target = GetCurrentTarget();
	const double Now = GetNow();
	if (bUseCover && MyPawn && !IsEvadingGrenade() && IsMovingToOrderCover())
	{
		// Идёт в укрытие по приказу отряда: до прихода не перепроверять, снимает только угроза вплотную.
		AActor* Threat = nullptr;
		FVector ThreatLocation;
		if (!GetOrderThreat(Threat, ThreatLocation) || FVector::Dist2D(Cover.Location, ThreatLocation) >= 0.8f * MinCoverThreatDistance)
		{
			return;
		}
		ReleaseCover();
		bOrderStuck = true;
		NextCoverCheckTime = 0.0;
	}
	if (!bUseCover || !MyPawn || !Target || GetAlertState() != ENoFearAlertState::Combat || IsEvadingGrenade())
	{
		if (HasCover())
		{
			ReleaseCover();
		}
		if (!Target)
		{
			bRetreating = false;
			AdvanceUntil = -1.0;
		}
		return;
	}

	// Раненый: в дальнее укрытие, своих — прикрыть.
	const UNoFearHealthComponent* Health = UNoFearHealthComponent::FindHealth(MyPawn);
	const bool bWounded = RetreatHealthFraction > 0.f && Health && Health->GetHealthFraction() < RetreatHealthFraction;
	if (bRetreating && Health && Health->GetHealthFraction() >= RetreatHealthFraction + 0.1f)
	{
		// Перевязали: снова в бой, ближнее укрытие найдётся при следующей проверке.
		bRetreating = false;
		RequestCoverRefresh();
	}
	if (bWounded && !bRetreating && Now >= NextRetreatTime && FNoFearSearchBudget::HasBudget(ENoFearSearchKind::Cover))
	{
		NextRetreatTime = Now + 5.0;
		if (SearchCover(true))
		{
			bRetreating = true;
			Shout(RetreatShout, 2.f);
			RequestCoveringFire(Target);
			NextCoverCheckTime = Now + FMath::FRandRange(CoverSearchInterval.X, CoverSearchInterval.Y);
			return;
		}
	}

	// Лимит поисков этого кадра исчерпан — проверка на следующем размышлении (без укрытия — ищем первыми, у кого есть — подождут).
	if (Now < NextCoverCheckTime || !FNoFearSearchBudget::HasBudget(ENoFearSearchKind::Cover))
	{
		return;
	}
	NextCoverCheckTime = Now + FMath::FRandRange(CoverSearchInterval.X, FMath::Max(CoverSearchInterval.X, CoverSearchInterval.Y));

	const FVector Threat = GetLastKnownTargetLocation();
	if (HasCover())
	{
		bool bKeep = true;
		const UNoFearCombatComponent* Combat = GetCombatComponent();
		const UWorld* World = GetWorld();
		if (FVector::Dist2D(Cover.Location, Threat) < 0.8f * MinCoverThreatDistance)
		{
			// Угроза подошла вплотную.
			bKeep = false;
		}
		else if (UNoFearCoverLibrary::EvaluateCover(World, Cover.Location, Threat + FVector(0.f, 0.f, UNoFearCoverLibrary::ThreatEyeHeight),
			UNoFearCoverLibrary::MakeCoverTraceParams(World), ECC_Visibility) != Cover.Type)
		{
			// Угроза обошла (или укрытие стало другим): две проверки подряд — искать заново.
			bKeep = ++CoverLostChecks < 2;
		}
		else
		{
			CoverLostChecks = 0;
		}
		if (bKeep && !bRetreating && SquadOrder.Order == ENoFearSquadOrder::None && IsInCover() && Combat
			&& Combat->GetTimeSinceTargetSeen() > AdvanceAfterUnseenTime)
		{
			// Цель давно не видна: подойти ближе (в отряде вперёд ведёт командир).
			bKeep = false;
			AdvanceUntil = Now + 5.0;
		}
		if (bKeep && !bCoverRefreshRequested)
		{
			return;
		}
		bCoverRefreshRequested = false;
		if (bKeep)
		{
			// Задача боя отработала циклы: поискать лучше, иначе остаться.
			const FNoFearCoverPoint Old = Cover;
			if (!SearchCover(false))
			{
				Cover = Old;
				CoverOwner = MyPawn;
				if (UNoFearBotSubsystem* Bots = UNoFearBotSubsystem::Get(this))
				{
					Bots->ReserveCover(MyPawn, Old.Location);
				}
			}
			return;
		}
		ReleaseCover();
	}

	SearchCover(bRetreating);
}

void ANoFearShooterAIController::RequestCoveringFire(AActor* Threat)
{
	APawn* MyPawn = GetPawn();
	UNoFearBotSubsystem* Bots = UNoFearBotSubsystem::Get(this);
	if (!MyPawn || !Bots)
	{
		return;
	}
	const FVector ThreatLocation = GetLastKnownTargetLocation();
	if (UNoFearSquadSubsystem* Squads = UNoFearSquadSubsystem::Get(this); Squads && Squads->RequestCoveringFire(MyPawn, Threat, ThreatLocation))
	{
		// В отряде прикрывают по приказу командира.
		return;
	}
	for (ANoFearAIController* Bot : Bots->GetBots())
	{
		ANoFearShooterAIController* Shooter = Cast<ANoFearShooterAIController>(Bot);
		APawn* OtherPawn = Shooter ? Shooter->GetPawn() : nullptr;
		if (!OtherPawn || Shooter == this || UNoFearHealthComponent::IsActorDead(OtherPawn) || UNoFearTeamLibrary::AreHostile(MyPawn, OtherPawn)
			|| FVector::Dist(MyPawn->GetActorLocation(), OtherPawn->GetActorLocation()) > CoverFireRequestRadius)
		{
			continue;
		}
		Shooter->ReceiveCoveringFireRequest(MyPawn, Threat, ThreatLocation);
	}
}

void ANoFearShooterAIController::ReceiveCoveringFireRequest(AActor* Requester, AActor* Threat, const FVector& ThreatLocation)
{
	if (!GetPawn() || UNoFearHealthComponent::IsActorDead(GetPawn()))
	{
		return;
	}
	if (!IsCoveringFire())
	{
		Shout(CoveringFireShout, 1.5f);
	}
	CoveringFireUntil = GetNow() + CoveringFireTime;
	if (!GetCurrentTarget() && Threat)
	{
		ReceiveAlarm(Threat, ThreatLocation);
	}
}

bool ANoFearShooterAIController::FindCombatPosition(FVector& OutLocation) const
{
	const APawn* MyPawn = GetPawn();
	const AActor* Target = GetCurrentTarget();
	UWorld* World = GetWorld();
	UNavigationSystemV1* NavSys = World ? FNavigationSystem::GetCurrent<UNavigationSystemV1>(World) : nullptr;
	if (!MyPawn || !Target || !NavSys)
	{
		return false;
	}
	const FVector Location = MyPawn->GetActorLocation();
	const FVector TargetLocation = GetLastKnownTargetLocation();
	const float LeashRadiusNow = GetLeashRadius();
	const FVector Anchor = GetLeashAnchor();
	const bool bRush = SquadOrder.Order == ENoFearSquadOrder::Rush;
	if (SquadOrder.Order == ENoFearSquadOrder::TakeCover && !IsTargetInWeaponRange())
	{
		// Контакт издалека, укрытия рядом нет: не бежать к врагу в открытую — сближаться отряд будет перебежками.
		return false;
	}

	if (!IsTargetVisible())
	{
		if (SquadOrder.Order == ENoFearSquadOrder::Hold || SquadOrder.Order == ENoFearSquadOrder::Suppress
			|| SquadOrder.Order == ENoFearSquadOrder::TakeCover)
		{
			// Оборона, подавление, контакт: место не покидать, искать врага не ходить.
			return false;
		}
		// Не видно — туда, где видели, но не вплотную и в пределах поводка.
		FVector Goal = TargetLocation;
		const FVector FromGoal = Location - Goal;
		if (FromGoal.Size2D() > 150.f)
		{
			Goal += FromGoal.GetSafeNormal2D() * 150.f;
		}
		if (LeashRadiusNow > 0.f)
		{
			FVector Offset = Goal - Anchor;
			Offset.Z = 0.f;
			if (Offset.Size() > LeashRadiusNow)
			{
				Goal = Anchor + Offset.GetClampedToMaxSize(LeashRadiusNow);
				Goal.Z = Location.Z;
			}
		}
		FNavLocation NavGoal;
		if (!NavSys->ProjectPointToNavigation(Goal, NavGoal, FVector(300.f, 300.f, 500.f)))
		{
			return false;
		}
		OutLocation = NavGoal.Location;
		return true;
	}

	// Видно — на удобной дистанции оружия, шагом вбок, чтобы не стоять на месте.
	const FVector2D Range = GetPreferredRange();
	const float Distance = FVector::Dist2D(Location, TargetLocation);
	float Desired = Distance;
	if (Distance > Range.Y)
	{
		Desired = FMath::Max(Range.Y * 0.85f, Range.X);
	}
	else if (Distance < Range.X)
	{
		Desired = Range.X * 1.1f;
	}
	if (bRush)
	{
		// Натиск: каждый шаг заметно ближе, пока не упрёшься в нижнюю границу дистанции оружия.
		Desired = FMath::Max(FMath::Min(Desired, Distance - 600.f), FMath::Max(Range.X, 300.f));
	}
	else if (SquadOrder.Order == ENoFearSquadOrder::Hold || SquadOrder.Order == ENoFearSquadOrder::Suppress
		|| SquadOrder.Order == ENoFearSquadOrder::CoveringFire || SquadOrder.Order == ENoFearSquadOrder::CoverFallback)
	{
		// Оборона и огонь с места (пока другие идут или отходят): шаг вбок или назад, но не к врагу.
		Desired = FMath::Max(Desired, Distance);
	}
	Desired = FMath::Max(Desired, 250.f);

	const FCollisionQueryParams Params = UNoFearCoverLibrary::MakeCoverTraceParams(World);
	const FVector TargetEyes = TargetLocation + FVector(0.f, 0.f, UNoFearCoverLibrary::ThreatEyeHeight);
	const FVector FromTarget = (Location - TargetLocation).GetSafeNormal2D();
	const UNoFearBotSubsystem* Bots = UNoFearBotSubsystem::Get(this);

	float BestScore = -FLT_MAX;
	bool bFound = false;
	static const float Angles[] = { 12.f, 25.f, 40.f, 55.f };
	const float FirstSign = FMath::RandBool() ? 1.f : -1.f;
	for (const float Angle : Angles)
	{
		for (const float Sign : { FirstSign, -FirstSign })
		{
			const FVector Dir = FromTarget.RotateAngleAxis(Angle * Sign + FMath::FRandRange(-5.f, 5.f), FVector::UpVector);
			FNavLocation Nav;
			if (!NavSys->ProjectPointToNavigation(TargetLocation + Dir * Desired, Nav, FVector(100.f, 100.f, 300.f)))
			{
				continue;
			}
			const FVector Point = Nav.Location;
			const float Travel = FVector::Dist2D(Point, Location);
			if (Travel < 150.f || (LeashRadiusNow > 0.f && FVector::Dist2D(Point, Anchor) > LeashRadiusNow))
			{
				continue;
			}
			if (Bots && Bots->IsCoverReserved(Point, CoverReserveRadius, MyPawn))
			{
				continue;
			}
			const FVector Eyes = Point + FVector(0.f, 0.f, UNoFearCoverLibrary::HighCoverHeight - 10.f);
			if (UNoFearCombatComponent::FindFriendInLine(MyPawn, Target, Eyes, TargetEyes, 40.f))
			{
				continue;
			}
			float Score = World->LineTraceTestByChannel(Eyes, TargetEyes, ECC_Visibility, Params) ? 0.f : 1.f;
			Score -= FMath::Abs(FVector::Dist2D(Point, TargetLocation) - Desired) / 1000.f;
			Score -= Travel / 2000.f;
			if (Score > BestScore)
			{
				BestScore = Score;
				OutLocation = Point;
				bFound = true;
			}
		}
	}
	return bFound;
}

void ANoFearShooterAIController::MaintainWeapon()
{
	UNoFearWeaponComponent* Weapons = GetWeaponComponent();
	if (!Weapons || Weapons->IsOwnerDead() || Weapons->GetWeapons().Num() == 0 || Weapons->AreHandsBusy())
	{
		return;
	}

	// Роль даёт оружие; роль сменилась на ту, что без своего оружия, — обратно к стартовому.
	const ENoFearSquadRole SquadRole = GetSquadRole();
	UNoFearWeaponData* RoleWeapon = SquadRole != ENoFearSquadRole::None ? RoleWeapons.FindRef(SquadRole).Get() : nullptr;
	int32 WantedIndex = INDEX_NONE;
	if (RoleWeapon)
	{
		const TArray<ANoFearWeapon*> Owned = Weapons->GetWeapons();
		WantedIndex = Owned.IndexOfByPredicate([RoleWeapon](const ANoFearWeapon* Weapon) { return Weapon && Weapon->GetWeaponData() == RoleWeapon; });
		if (WantedIndex == INDEX_NONE && Weapons->AddWeapon(RoleWeapon))
		{
			WantedIndex = Weapons->GetWeapons().Num() - 1;
		}
		AppliedRoleWeapon = RoleWeapon;
	}
	else if (AppliedRoleWeapon.IsValid())
	{
		WantedIndex = 0;
		AppliedRoleWeapon.Reset();
	}

	if (WantedIndex != INDEX_NONE && Weapons->GetTargetWeaponIndex() != WantedIndex)
	{
		Weapons->EquipWeapon(WantedIndex);
	}
	else if (Weapons->GetTargetWeaponIndex() == INDEX_NONE)
	{
		Weapons->EquipWeapon(0);
	}

	const bool bWantsAim = bAimInCombat && GetCurrentTarget() != nullptr && !IsEvadingGrenade();
	if (Weapons->IsAiming() != bWantsAim)
	{
		Weapons->SetAiming(bWantsAim);
	}

	if (bInfiniteReserveAmmo)
	{
		for (ANoFearWeapon* Weapon : Weapons->GetWeapons())
		{
			const UNoFearWeaponData* Data = Weapon ? Weapon->GetWeaponData() : nullptr;
			if (Data && Weapon->GetReserveAmmo() < Data->MagazineSize)
			{
				Weapon->AddReserveAmmo(FMath::Max(Data->MaxReserveAmmo, Data->MagazineSize));
			}
		}
	}

	// Без цели — перезарядиться, если магазин почти пуст (в бою перезаряжаются в укрытии).
	const ANoFearWeapon* Current = Weapons->GetCurrentWeapon();
	const UNoFearWeaponData* CurrentData = Current ? Current->GetWeaponData() : nullptr;
	if (!GetCurrentTarget() && CurrentData && Current->GetWeaponState() == ENoFearWeaponState::Idle && Current->GetReserveAmmo() > 0
		&& Current->GetMagazineAmmo() < FMath::CeilToInt(CurrentData->MagazineSize * IdleReloadFraction))
	{
		Weapons->Reload();
	}
}

void ANoFearShooterAIController::HandleTargetChanged(AActor* NewTarget, AActor* OldTarget)
{
	Super::HandleTargetChanged(NewTarget, OldTarget);

	// Только что заметил врага: гранату не сразу.
	if (NewTarget && !OldTarget)
	{
		NextGrenadeTime = FMath::Max(NextGrenadeTime, GetNow() + FMath::FRandRange(FirstGrenadeDelay.X, FirstGrenadeDelay.Y));
	}

	if (!NewTarget)
	{
		if (UNoFearWeaponComponent* Weapons = GetWeaponComponent())
		{
			Weapons->StopFire();
		}
	}
	if (GetPawn())
	{
		MaintainWeapon();
	}
}

bool ANoFearShooterAIController::GetGrenadeSpot(const AActor* Target, const FVector& Known, FVector& OutSpot) const
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}
	FHitResult Hit;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(NoFearGrenadeSpot), false);
	Params.AddIgnoredActor(Target);
	if (World->LineTraceSingleByObjectType(Hit, Known + FVector(0.f, 0.f, 50.f), Known - FVector(0.f, 0.f, 300.f), FCollisionObjectQueryParams(ECC_WorldStatic), Params))
	{
		OutSpot = Hit.ImpactPoint + FVector(0.f, 0.f, 5.f);
		return true;
	}
	OutSpot = Known;
	return true;
}

void ANoFearShooterAIController::TryThrowGrenade()
{
	const double Now = GetNow();
	if (!bThrowGrenades || Now < NextGrenadeTime)
	{
		return;
	}
	APawn* MyPawn = GetPawn();
	const ANoFearCharacter* BotCharacter = GetNoFearCharacter();
	UNoFearGrenadeComponent* Grenades = BotCharacter ? BotCharacter->GetGrenadeComponent() : nullptr;
	AActor* Target = GetCurrentTarget();
	if (!MyPawn || !Grenades || Grenades->GetGrenadeCount() <= 0)
	{
		GrenadeDebugReason = TEXT("no grenades");
		return;
	}
	if (!Target || GetAlertState() != ENoFearAlertState::Combat)
	{
		GrenadeDebugReason = TEXT("no target");
		return;
	}
	if (IsEvadingGrenade() || UNoFearPoseComponent::IsActorStaggered(MyPawn) || !Grenades->CanThrow())
	{
		GrenadeDebugReason = TEXT("busy");
		return;
	}
	if (const UNoFearSquadSubsystem* Squads = UNoFearSquadSubsystem::Get(this);
		Squads && Squads->IsInFormedSquad(MyPawn) && Squads->GetSquadMembers(MyPawn).Num() >= 2)
	{
		// В отряде гранату бросают по приказу командира (OrderGrenade): одна на всех, не каждый свою.
		GrenadeDebugReason = TEXT("waiting for order");
		return;
	}
	const FNoFearKnownActor* Knowledge = KnownActors.Find(Target);
	FVector Spot;
	if (!Knowledge || !GetGrenadeSpot(Target, Knowledge->LastKnownLocation, Spot))
	{
		GrenadeDebugReason = TEXT("no spot");
		return;
	}
	const float Distance = FVector::Dist2D(MyPawn->GetActorLocation(), Spot);
	if (Distance < MinGrenadeDistance || Distance > MaxGrenadeDistance)
	{
		GrenadeDebugReason = FString::Printf(TEXT("distance %.0f"), Distance);
		return;
	}

	// Повод: цель засела в укрытии или враги стоят кучкой.
	const double Unseen = Knowledge->bVisible ? 0.0 : Now - Knowledge->LastSeenTime;
	const bool bTargetHidden = Unseen >= HiddenTargetTime && Unseen <= MaxUnseenTime;
	int32 Cluster = 0;
	for (const TPair<TWeakObjectPtr<AActor>, FNoFearKnownActor>& Pair : KnownActors)
	{
		const AActor* Other = Pair.Key.Get();
		if (Other && Pair.Value.Awareness >= 1.f && Now - Pair.Value.LastSeenTime <= 3.0 && IsValidTarget(Other)
			&& FVector::Dist(Pair.Value.LastKnownLocation, Knowledge->LastKnownLocation) <= ClusterRadius)
		{
			++Cluster;
		}
	}
	if (!bTargetHidden && Cluster < ClusterCount)
	{
		GrenadeDebugReason = FString::Printf(TEXT("no reason (unseen %.1f, cluster %d)"), Unseen, Cluster);
		return;
	}

	ThrowGrenadeAtSpot(Spot);
}

bool ANoFearShooterAIController::ThrowGrenadeAtSpot(const FVector& Spot)
{
	APawn* MyPawn = GetPawn();
	const ANoFearCharacter* BotCharacter = GetNoFearCharacter();
	UNoFearGrenadeComponent* Grenades = BotCharacter ? BotCharacter->GetGrenadeComponent() : nullptr;
	const double Now = GetNow();
	if (!MyPawn || !Grenades)
	{
		return false;
	}

	// Своих и мирных рядом с точкой не задеть.
	const float SafeDistance = Grenades->GetRadius() + FriendlyGrenadeMargin;
	for (TActorIterator<ANoFearCharacter> It(GetWorld()); It; ++It)
	{
		const ANoFearCharacter* Other = *It;
		if (Other == MyPawn || Other->IsDead() || UNoFearTeamLibrary::AreHostile(MyPawn, Other))
		{
			continue;
		}
		if (FVector::Dist(Other->GetActorLocation(), Spot) < SafeDistance)
		{
			GrenadeDebugReason = FString::Printf(TEXT("friend near %s"), *Other->GetName());
			NextGrenadeTime = Now + 1.0;
			return false;
		}
	}

	if (!Grenades->ThrowGrenadeAt(Spot))
	{
		GrenadeDebugReason = TEXT("cannot reach");
		NextGrenadeTime = Now + 1.5;
		return false;
	}
	SetFocalPoint(Spot);
	Shout(GrenadeShout, 1.5f);
	++GrenadesThrown;
	GrenadeDebugReason.Reset();
	NextGrenadeTime = Now + FMath::FRandRange(GrenadeCooldown.X, GrenadeCooldown.Y);
	return true;
}

bool ANoFearShooterAIController::OrderGrenade(AActor* Enemy, const FVector& KnownLocation)
{
	APawn* MyPawn = GetPawn();
	const ANoFearCharacter* BotCharacter = GetNoFearCharacter();
	UNoFearGrenadeComponent* Grenades = BotCharacter ? BotCharacter->GetGrenadeComponent() : nullptr;
	if (!bThrowGrenades || !MyPawn || UNoFearHealthComponent::IsActorDead(MyPawn) || !Grenades || Grenades->GetGrenadeCount() <= 0
		|| !Grenades->CanThrow() || IsEvadingGrenade() || UNoFearPoseComponent::IsActorStaggered(MyPawn))
	{
		return false;
	}
	FVector Spot;
	if (!GetGrenadeSpot(Enemy, KnownLocation, Spot))
	{
		return false;
	}
	const float Distance = FVector::Dist2D(MyPawn->GetActorLocation(), Spot);
	if (Distance < MinGrenadeDistance || Distance > MaxGrenadeDistance)
	{
		return false;
	}
	return ThrowGrenadeAtSpot(Spot);
}
