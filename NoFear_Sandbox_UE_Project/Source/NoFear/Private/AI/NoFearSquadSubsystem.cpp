#include "AI/NoFearSquadSubsystem.h"
#include "Core/NoFearAISettings.h"
#include "Core/NoFearPerf.h"

#include "AI/NoFearBotSubsystem.h"
#include "AI/NoFearCombatComponent.h"
#include "AI/NoFearCompanionAIController.h"
#include "AI/NoFearShooterAIController.h"
#include "Core/NoFearCharacter.h"
#include "Core/NoFearFearComponent.h"
#include "Core/NoFearFearSubsystem.h"
#include "Core/NoFearHealthComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "NavigationSystem.h"
#include "Weapons/NoFearGrenadeComponent.h"
#include "Weapons/NoFearWeapon.h"
#include "Weapons/NoFearWeaponComponent.h"

namespace
{
	/** Роль с тела: пусто — стрелок. */
	ENoFearSquadRole GetBodyRole(const APawn* Pawn)
	{
		const ANoFearCharacter* Character = Cast<ANoFearCharacter>(Pawn);
		const ENoFearSquadRole Role = Character ? Character->SquadRole : ENoFearSquadRole::None;
		return Role == ENoFearSquadRole::None ? ENoFearSquadRole::Rifleman : Role;
	}

	bool IsSquadDebugEnabled()
	{
		const IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(TEXT("NoFear.AI.Debug"));
		return CVar && CVar->GetInt() > 0;
	}

	float GetHealthFraction(const AActor* Actor)
	{
		const UNoFearHealthComponent* Health = UNoFearHealthComponent::FindHealth(Actor);
		return Health ? Health->GetHealthFraction() : 1.f;
	}

	/** Приказ ведёт бойца куда-то: такой не отвлекают на помощь и гранаты. Сбор — не в счёт. */
	bool IsMovingOrder(ENoFearSquadOrder Order)
	{
		switch (Order)
		{
		case ENoFearSquadOrder::Bound:
		case ENoFearSquadOrder::Flank:
		case ENoFearSquadOrder::Rush:
		case ENoFearSquadOrder::FallBack:
		case ENoFearSquadOrder::Reinforce:
		case ENoFearSquadOrder::Heal:
		case ENoFearSquadOrder::Assist:
			return true;
		default:
			return false;
		}
	}

	bool ProjectToNav(const UWorld* World, const FVector& Point, const FVector& Extent, FVector& OutPoint)
	{
		const UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
		FNavLocation NavLocation;
		if (NavSys && NavSys->ProjectPointToNavigation(Point, NavLocation, Extent))
		{
			OutPoint = NavLocation.Location;
			return true;
		}
		return false;
	}

	const FText ShoutLostMan = NSLOCTEXT("NoFear", "SquadLostMan", "Потеряли бойца!");
	const FText ShoutKill = NSLOCTEXT("NoFear", "SquadKill", "Минус один!");
	const FText ShoutContact = NSLOCTEXT("NoFear", "SquadContact", "Контакт!");
	const FText ShoutToBattle = NSLOCTEXT("NoFear", "SquadToBattle", "К бою!");
	const FText ShoutContactToBattle = NSLOCTEXT("NoFear", "SquadContactToBattle", "Контакт! К бою!");
	const FText ShoutCommanderDown = NSLOCTEXT("NoFear", "SquadCommanderDown", "Командир убит! Отходим!");
	const FText ShoutAllWounded = NSLOCTEXT("NoFear", "SquadAllWounded", "Все ранены! Отходим!");
	const FText ShoutFallback = NSLOCTEXT("NoFear", "SquadFallback", "Отходим! Перекатами!");
	const FText ShoutHoldLine = NSLOCTEXT("NoFear", "SquadHoldLine", "Держим рубеж!");
	const FText ShoutToPosts = NSLOCTEXT("NoFear", "SquadToPosts", "По местам!");
	const FText ShoutBounding = NSLOCTEXT("NoFear", "SquadBounding", "Перебежками, вперёд!");
	const FText ShoutDefend = NSLOCTEXT("NoFear", "SquadDefend", "Держим позиции!");
	const FText ShoutRegroup = NSLOCTEXT("NoFear", "SquadRegroup", "Все ко мне!");
	const FText ShoutMoving = NSLOCTEXT("NoFear", "SquadMoving", "Двигаюсь!");
	const FText ShoutFallingBack = NSLOCTEXT("NoFear", "SquadFallingBack", "Отхожу!");
	const FText ShoutFlankRight = NSLOCTEXT("NoFear", "SquadFlankRight", "Обходи справа!");
	const FText ShoutFlankLeft = NSLOCTEXT("NoFear", "SquadFlankLeft", "Обходи слева!");
	const FText ShoutFlanking = NSLOCTEXT("NoFear", "SquadFlanking", "Обхожу!");
	const FText ShoutInPosition = NSLOCTEXT("NoFear", "SquadInPosition", "На позиции!");
	const FText ShoutComing = NSLOCTEXT("NoFear", "SquadComing", "Держись, иду!");
	const FText ShoutPinned = NSLOCTEXT("NoFear", "SquadPinned", "Прижали!");
	const FText ShoutWounded = NSLOCTEXT("NoFear", "SquadWounded", "Ранен!");
	const FText ShoutBandage = NSLOCTEXT("NoFear", "SquadBandage", "Перевяжу, держись!");
	const FText ShoutBandageDone = NSLOCTEXT("NoFear", "SquadBandageDone", "Готово!");
	const FText ShoutGrenadeOrder = NSLOCTEXT("NoFear", "SquadGrenadeOrder", "Гранату туда!");
	const FText ShoutNeedHelp = NSLOCTEXT("NoFear", "SquadNeedHelp", "Нужна помощь!");
	const FText ShoutHelpComing = NSLOCTEXT("NoFear", "SquadHelpComing", "Идём на помощь!");
	const FText ShoutHelpArrived = NSLOCTEXT("NoFear", "SquadHelpArrived", "Мы здесь!");
	const FText ShoutHelpReturn = NSLOCTEXT("NoFear", "SquadHelpReturn", "Возвращаемся!");
}

UNoFearSquadSubsystem* UNoFearSquadSubsystem::Get(const UObject* WorldContextObject)
{
	const UWorld* World = WorldContextObject ? WorldContextObject->GetWorld() : nullptr;
	return World ? World->GetSubsystem<UNoFearSquadSubsystem>() : nullptr;
}

bool UNoFearSquadSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

TStatId UNoFearSquadSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UNoFearSquadSubsystem, STATGROUP_Tickables);
}

void UNoFearSquadSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	static_cast<FNoFearSquadTuning&>(*this) = UNoFearAISettings::Get()->Squad;
}

void UNoFearSquadSubsystem::Tick(float DeltaTime)
{
	NOFEAR_PERF_SCOPE(Squad);
	Super::Tick(DeltaTime);
	TimeSinceUpdate += DeltaTime;
	if (TimeSinceUpdate >= UpdateInterval)
	{
		TimeSinceUpdate = 0.f;
		UpdateSquads();
	}
}

void UNoFearSquadSubsystem::UpdateSquads()
{
	const UWorld* World = GetWorld();
	const UNoFearBotSubsystem* BotSubsystem = UNoFearBotSubsystem::Get(this);
	if (!World || !BotSubsystem)
	{
		return;
	}
	const double Now = World->GetTimeSeconds();
	const float DeltaTime = LastUpdateTime < 0.0 ? 0.f : static_cast<float>(FMath::Max(0.0, Now - LastUpdateTime));
	LastUpdateTime = Now;

	// Выбывшие: погибли, сменили отряд или команду, ушли из-под контроллера. Гибель бьёт по морали.
	for (FSquad& Squad : Squads)
	{
		for (int32 Index = Squad.Members.Num() - 1; Index >= 0; --Index)
		{
			const FMember& Member = Squad.Members[Index];
			const ANoFearShooterAIController* Bot = Member.Bot.Get();
			const ANoFearCharacter* Character = Cast<ANoFearCharacter>(Member.Pawn.Get());
			const bool bDead = !Character || Character->IsDead();
			if (!bDead && Bot && Bot->GetPawn() == Character && Character->Squad == Squad.Name && Character->Team == Squad.Team)
			{
				continue;
			}
			if (bDead && Squad.bFormed)
			{
				HandleMemberDeath(Squad, Member, Now);
			}
			Squad.Members.RemoveAt(Index);
		}
	}
	Squads.RemoveAll([](const FSquad& Squad) { return Squad.Members.IsEmpty(); });

	// Новички — в порядке регистрации ботов, чтобы «первый боец» не прыгал от обновления к обновлению.
	for (ANoFearAIController* AnyBot : BotSubsystem->GetBots())
	{
		ANoFearShooterAIController* Bot = Cast<ANoFearShooterAIController>(AnyBot);
		ANoFearCharacter* Character = Bot ? Cast<ANoFearCharacter>(Bot->GetPawn()) : nullptr;
		if (!Character || Character->Squad.IsNone() || Character->IsDead() || Bot->IsA<ANoFearCompanionAIController>())
		{
			continue;
		}

		FSquad* Squad = Squads.FindByPredicate([Character](const FSquad& Candidate)
		{
			return Candidate.Name == Character->Squad && Candidate.Team == Character->Team;
		});
		if (!Squad)
		{
			Squad = &Squads.AddDefaulted_GetRef();
			Squad->Name = Character->Squad;
			Squad->Team = Character->Team;
		}
		if (Squad->Members.ContainsByPredicate([Character](const FMember& Member) { return Member.Pawn.Get() == Character; })
			|| Squad->Members.Num() >= MaxSquadSize)
		{
			continue;
		}

		FMember& Member = Squad->Members.AddDefaulted_GetRef();
		Member.Bot = Bot;
		Member.Pawn = Character;
		Member.JoinTime = Now;
	}

	for (FSquad& Squad : Squads)
	{
		// Отряд собран, когда в нём впервые двое: все встают в строй разом (кольца и линии к командиру).
		if (!Squad.bFormed && Squad.Members.Num() >= 2)
		{
			Squad.bFormed = true;
			Squad.Morale = StartMorale;
			for (FMember& Member : Squad.Members)
			{
				Member.JoinTime = Now;
			}
		}
		Squad.PeakSize = FMath::Max(Squad.PeakSize, Squad.Members.Num());
		AssignRoles(Squad, Now);
		ShareIntel(Squad, Now);
		if (!Squad.bFormed)
		{
			continue;
		}

		UpdateMembers(Squad, Now, DeltaTime);
		UpdateEngagement(Squad, Now);
		UpdateMorale(Squad, DeltaTime);
		UpdateTactic(Squad, Now);
		UpdateHelp(Squad, Now);
		IssueOrders(Squad, Now);
		UpdateGrenades(Squad, Now);
	}

	UpdateAssists(Now);
	DrawDebug(Now);
}

void UNoFearSquadSubsystem::AssignRoles(FSquad& Squad, double Now)
{
	APawn* OldCommander = Squad.Commander.Get();
	const bool bOldCommanderHere = OldCommander && Squad.Members.ContainsByPredicate([OldCommander](const FMember& Member)
	{
		return Member.Pawn.Get() == OldCommander;
	});

	// Командир с тела главнее назначенного; иначе прежний; иначе первый стрелок, поддержка, штурмовик.
	APawn* NewCommander = nullptr;
	for (const FMember& Member : Squad.Members)
	{
		if (GetBodyRole(Member.Pawn.Get()) == ENoFearSquadRole::Commander)
		{
			NewCommander = Member.Pawn.Get();
			break;
		}
	}
	if (!NewCommander && bOldCommanderHere)
	{
		NewCommander = OldCommander;
	}
	for (const ENoFearSquadRole Preferred : { ENoFearSquadRole::Rifleman, ENoFearSquadRole::Support, ENoFearSquadRole::Assault })
	{
		if (NewCommander)
		{
			break;
		}
		for (const FMember& Member : Squad.Members)
		{
			if (GetBodyRole(Member.Pawn.Get()) == Preferred)
			{
				NewCommander = Member.Pawn.Get();
				break;
			}
		}
	}

	if (NewCommander != OldCommander)
	{
		// Первое назначение — не смена. Смена в отряде из одного никому не видна.
		if (OldCommander && Squad.Members.Num() >= 2)
		{
			Squad.CommanderChangeTime = Now;
		}
		Squad.Commander = NewCommander;
	}

	for (FMember& Member : Squad.Members)
	{
		const ENoFearSquadRole BodyRole = GetBodyRole(Member.Pawn.Get());
		ENoFearSquadRole Role = BodyRole;
		if (Member.Pawn.Get() == NewCommander)
		{
			Role = ENoFearSquadRole::Commander;
		}
		else if (BodyRole == ENoFearSquadRole::Commander)
		{
			// Второй командир с тела — рядовой стрелок.
			Role = ENoFearSquadRole::Rifleman;
		}

		if (!Member.bRoleAssigned)
		{
			Member.Role = Member.PreviousRole = Role;
			Member.bRoleAssigned = true;
		}
		else if (Member.Role != Role)
		{
			Member.PreviousRole = Member.Role;
			Member.Role = Role;
			Member.RoleChangeTime = Now;
		}
	}
}

void UNoFearSquadSubsystem::ShareIntel(FSquad& Squad, double Now)
{
	// Кого видят бойцы. Ушедшие на помощь соседям делятся с ними (UpdateAssists), а не со своими.
	for (const FMember& Member : Squad.Members)
	{
		const ANoFearShooterAIController* Bot = Member.Bot.Get();
		if (!Bot || !Member.AssistSquad.IsNone())
		{
			continue;
		}
		for (const TPair<TWeakObjectPtr<AActor>, FNoFearKnownActor>& Pair : Bot->GetKnownActors())
		{
			const AActor* Enemy = Pair.Key.Get();
			if (Enemy && Pair.Value.bVisible && !UNoFearHealthComponent::IsActorDead(Enemy))
			{
				FEnemy& Known = Squad.Enemies.FindOrAdd(Pair.Key);
				Known.Location = Enemy->GetActorLocation();
				Known.LastSeenTime = Now;
			}
		}
	}

	for (auto It = Squad.Enemies.CreateIterator(); It; ++It)
	{
		const AActor* Enemy = It.Key().Get();
		const bool bDead = Enemy && UNoFearHealthComponent::IsActorDead(Enemy);
		if (bDead && Squad.bFormed && Now - It.Value().LastSeenTime <= 5.0)
		{
			// Враг, с которым только что дрались, убит — отряд воспрял.
			AddMorale(Squad, KillMoraleGain);
			const APawn* Nearest = nullptr;
			float NearestDistSq = TNumericLimits<float>::Max();
			for (const FMember& Member : Squad.Members)
			{
				const APawn* Pawn = Member.Pawn.Get();
				const float DistSq = Pawn ? FVector::DistSquared(Pawn->GetActorLocation(), Enemy->GetActorLocation()) : TNumericLimits<float>::Max();
				if (Pawn && Member.AssistSquad.IsNone() && DistSq < NearestDistSq)
				{
					Nearest = Pawn;
					NearestDistSq = DistSq;
				}
			}
			SquadShout(Squad, Nearest, ShoutKill);
		}
		if (!Enemy || bDead || Now - It.Value().LastSeenTime > EnemyMemoryTime)
		{
			It.RemoveCurrent();
		}
	}

	// Свежее — всем, кто сам не видит.
	for (const TPair<TWeakObjectPtr<AActor>, FEnemy>& Pair : Squad.Enemies)
	{
		AActor* Enemy = Pair.Key.Get();
		if (!Enemy || Now - Pair.Value.LastSeenTime > SquadIntelTime)
		{
			continue;
		}
		for (const FMember& Member : Squad.Members)
		{
			ANoFearShooterAIController* Bot = Member.Bot.Get();
			if (!Bot || !Member.AssistSquad.IsNone())
			{
				continue;
			}
			const FNoFearKnownActor* Own = Bot->GetKnownActors().Find(Pair.Key);
			if (!Own || !Own->bVisible)
			{
				Bot->ReceiveAlarm(Enemy, Pair.Value.Location);
			}
		}
	}
}

void UNoFearSquadSubsystem::HandleMemberDeath(FSquad& Squad, const FMember& Dead, double Now)
{
	AddMorale(Squad, -DeathMoraleLoss);
	const APawn* DeadPawn = Dead.Pawn.Get();
	if (DeadPawn && Squad.Flanker.Get() == DeadPawn)
	{
		Squad.Flanker = nullptr;
	}
	if (Dead.Role == ENoFearSquadRole::Commander)
	{
		// Командир убит: отряд отходит (UpdateTactic), крик — у нового командира.
		AddMorale(Squad, -CommanderDeathMoraleLoss);
		Squad.CommanderDeathTime = Now;
		return;
	}

	const APawn* Nearest = nullptr;
	float NearestDistSq = TNumericLimits<float>::Max();
	for (const FMember& Member : Squad.Members)
	{
		const APawn* Pawn = Member.Pawn.Get();
		if (!Pawn || Pawn == DeadPawn || !Member.AssistSquad.IsNone())
		{
			continue;
		}
		const float DistSq = DeadPawn ? FVector::DistSquared(Pawn->GetActorLocation(), DeadPawn->GetActorLocation()) : 0.f;
		if (DistSq < NearestDistSq)
		{
			Nearest = Pawn;
			NearestDistSq = DistSq;
		}
	}
	SquadShout(Squad, Nearest, ShoutLostMan);
}

void UNoFearSquadSubsystem::UpdateMembers(FSquad& Squad, double Now, float DeltaTime)
{
	for (FMember& Member : Squad.Members)
	{
		const APawn* Pawn = Member.Pawn.Get();
		const ANoFearShooterAIController* Bot = Member.Bot.Get();
		if (!Pawn || !Bot)
		{
			continue;
		}
		// Ушедшие на помощь соседям воюют за них: их раны не бьют по морали своего отряда.
		const bool bCounts = Member.AssistSquad.IsNone();

		if (const UNoFearHealthComponent* Health = UNoFearHealthComponent::FindHealth(Pawn))
		{
			const float Current = Health->GetHealth();
			if (Member.LastHealth >= 0.f && Current < Member.LastHealth - KINDA_SMALL_NUMBER)
			{
				Member.LastDamageTime = Now;
				if (bCounts)
				{
					AddMorale(Squad, -DamageMoraleLoss * (Member.LastHealth - Current) / FMath::Max(1.f, Health->GetMaxHealth()));
				}
			}
			Member.LastHealth = Current;
		}

		const UNoFearCombatComponent* Combat = Bot->GetCombatComponent();
		if (Combat && Combat->IsSuppressed())
		{
			if (Member.SuppressedSince < 0.0)
			{
				Member.SuppressedSince = Now;
			}
			if (bCounts)
			{
				AddMorale(Squad, -SuppressionMoraleLoss * DeltaTime);
			}
		}
		else
		{
			Member.SuppressedSince = -1.0;
		}

		const bool bEvading = Bot->IsEvadingGrenade();
		if (bEvading && !Member.bWasEvading && Now - Member.LastGrenadeScareTime >= 3.0)
		{
			Member.LastGrenadeScareTime = Now;
			if (bCounts)
			{
				AddMorale(Squad, -GrenadeMoraleLoss);
			}
		}
		Member.bWasEvading = bEvading;

		// Дошёл по приказу — запомнить: выглянув из укрытия, он всё равно «дошёл».
		if (Member.SettledSerial != Member.OrderSerial && HasSettled(Member, Now))
		{
			Member.SettledSerial = Member.OrderSerial;
		}
	}
}

void UNoFearSquadSubsystem::UpdateEngagement(FSquad& Squad, double Now)
{
	bool bEngaged = false;
	bool bAlerted = false;

	// Сколько бойцов бьют каждую цель, и где цель по их сведениям (цель могли только слышать).
	struct FTargetInfo
	{
		int32 Count = 0;
		FVector Location = FVector::ZeroVector;
		double LastSeen = -1.0e9;
	};
	TMap<AActor*, FTargetInfo> Targets;
	for (const FMember& Member : Squad.Members)
	{
		const ANoFearShooterAIController* Bot = Member.Bot.Get();
		if (!Bot || !Member.AssistSquad.IsNone())
		{
			continue;
		}
		const ENoFearAlertState Alert = Bot->GetAlertState();
		if (Alert == ENoFearAlertState::Combat || Alert == ENoFearAlertState::Search)
		{
			bAlerted = true;
		}
		AActor* Target = Bot->GetCurrentTarget();
		if (!Target || UNoFearHealthComponent::IsActorDead(Target))
		{
			continue;
		}
		bEngaged = true;
		FTargetInfo& Info = Targets.FindOrAdd(Target);
		++Info.Count;
		if (const FNoFearKnownActor* Known = Bot->GetKnownActors().Find(Target))
		{
			if (Known->LastSeenTime >= Info.LastSeen || Info.Count == 1)
			{
				Info.Location = Known->LastKnownLocation;
				Info.LastSeen = FMath::Max(Info.LastSeen, Known->LastSeenTime);
			}
		}
	}
	for (const TPair<TWeakObjectPtr<AActor>, FEnemy>& Pair : Squad.Enemies)
	{
		AActor* Enemy = Pair.Key.Get();
		if (!Enemy)
		{
			continue;
		}
		if (Now - Pair.Value.LastSeenTime <= CombatMemoryTime)
		{
			bEngaged = true;
			FTargetInfo& Info = Targets.FindOrAdd(Enemy);
			Info.Location = Pair.Value.Location;
			Info.LastSeen = FMath::Max(Info.LastSeen, Pair.Value.LastSeenTime);
		}
		else if (FTargetInfo* Info = Targets.Find(Enemy))
		{
			// Цель бойца, которую отряд видел давно: место — по сведениям отряда, если они свежее.
			if (Pair.Value.LastSeenTime > Info->LastSeen)
			{
				Info->Location = Pair.Value.Location;
				Info->LastSeen = Pair.Value.LastSeenTime;
			}
		}
	}
	Squad.bEngaged = bEngaged;
	Squad.bAlerted = bAlerted || bEngaged;

	// Главная угроза: та, в которую целится больше бойцов; поровну — которую видели последней.
	AActor* Best = nullptr;
	FTargetInfo BestInfo;
	for (const TPair<AActor*, FTargetInfo>& Pair : Targets)
	{
		if (!Best || Pair.Value.Count > BestInfo.Count || (Pair.Value.Count == BestInfo.Count && Pair.Value.LastSeen > BestInfo.LastSeen))
		{
			Best = Pair.Key;
			BestInfo = Pair.Value;
		}
	}

	// Сидит ли она на месте: сменилась или ушла дальше 300 см — отсчёт заново.
	if (Best != Squad.MainEnemy.Get() || !Best || FVector::Dist(BestInfo.Location, Squad.DugInAnchor) > 300.f)
	{
		Squad.DugInAnchor = BestInfo.Location;
		Squad.DugInSince = Now;
	}
	Squad.MainEnemy = Best;
	Squad.MainEnemyLocation = BestInfo.Location;
	Squad.MainEnemyLastSeen = Best ? BestInfo.LastSeen : -1.0e9;
}

void UNoFearSquadSubsystem::UpdateMorale(FSquad& Squad, float DeltaTime)
{
	const UWorld* World = GetWorld();
	const double Now = World ? World->GetTimeSeconds() : 0.0;
	const int32 Alive = CountActive(Squad);
	int32 EnemyCount = 0;
	for (const TPair<TWeakObjectPtr<AActor>, FEnemy>& Pair : Squad.Enemies)
	{
		if (Pair.Key.IsValid() && Now - Pair.Value.LastSeenTime <= CombatMemoryTime)
		{
			++EnemyCount;
		}
	}

	// «Спокойная» мораль: своих больше, чем врагов, — выше; отряд поредел — ниже.
	const float Losses = 1.f - static_cast<float>(Alive) / FMath::Max(1, Squad.PeakSize);
	float Baseline = FMath::Clamp(StartMorale + 0.08f * (Alive - EnemyCount) - 0.25f * Losses, 0.3f, 0.9f);

	// Страх (этап 11): средний страх бойцов выше порога тревоги тянет мораль вниз, до MoralePenalty при страхе 100.
	if (const UNoFearFearSubsystem* Fear = UNoFearFearSubsystem::Get(this); Fear && Alive > 0)
	{
		float FearSum = 0.f;
		int32 FearCount = 0;
		for (const FMember& Member : Squad.Members)
		{
			if (Member.Pawn.IsValid() && Member.Bot.IsValid() && Member.AssistSquad.IsNone())
			{
				FearSum += UNoFearFearComponent::GetActorFear(Member.Pawn.Get());
				++FearCount;
			}
		}
		const float AverageFear = FearCount > 0 ? FearSum / FearCount : 0.f;
		const float Excess = FMath::Max(0.f, AverageFear - Fear->AnxiousThreshold) / FMath::Max(1.f, 100.f - Fear->AnxiousThreshold);
		Baseline = FMath::Max(0.f, Baseline - Fear->MoralePenalty * Excess);
	}
	const float Rate = Squad.bEngaged ? MoraleRecoveryRate : MoraleRestRate;
	Squad.Morale = FMath::FInterpConstantTo(Squad.Morale, Baseline, DeltaTime, Rate);
}

void UNoFearSquadSubsystem::UpdateTactic(FSquad& Squad, double Now)
{
	const double PreviousEngagedTime = Squad.LastEngagedTime;
	if (Squad.bEngaged)
	{
		Squad.LastEngagedTime = Now;
	}

	// Боя нет. Отход доводится до конца и без врага на виду.
	if (!Squad.bEngaged && Squad.Tactic != ENoFearSquadTactic::Fallback)
	{
		if (Squad.bAlerted)
		{
			// Кто-то обыскивает — каждый сам по себе, сбор после.
			if (Squad.Tactic != ENoFearSquadTactic::None)
			{
				SetTactic(Squad, ENoFearSquadTactic::None, Now);
			}
			return;
		}
		if (Squad.bWasEngaged && Squad.Tactic != ENoFearSquadTactic::Regroup)
		{
			BeginRegroup(Squad, Now);
			return;
		}
		if (Squad.Tactic == ENoFearSquadTactic::Regroup)
		{
			if (IsRegroupDone(Squad, Now))
			{
				Squad.bWasEngaged = false;
				Squad.bHoldingLine = false;
				SetTactic(Squad, ENoFearSquadTactic::None, Now);
				SquadShout(Squad, Squad.Commander.Get(), ShoutToPosts);
			}
			return;
		}
		if (Squad.Tactic != ENoFearSquadTactic::None)
		{
			SetTactic(Squad, ENoFearSquadTactic::None, Now);
		}
		return;
	}

	const int32 Alive = CountActive(Squad);
	if (Alive == 0)
	{
		return;
	}

	// Бой начался. После долгого затишья — «Контакт»: все в укрытие лицом к угрозе.
	if (Squad.bEngaged && (Squad.Tactic == ENoFearSquadTactic::None || Squad.Tactic == ENoFearSquadTactic::Regroup))
	{
		Squad.bWasEngaged = true;
		if (Now - PreviousEngagedTime > 10.0)
		{
			SetTactic(Squad, ENoFearSquadTactic::Contact, Now);
			Squad.ContactTime = Now;
			Squad.NextGrenadeTime = FMath::Max(Squad.NextGrenadeTime, Now + FirstSquadGrenadeDelay);
			Squad.NextFlankTime = FMath::Max(Squad.NextFlankTime, Now + FlankDugInTime);
			Squad.NextRushTime = FMath::Max(Squad.NextRushTime, Now + 3.0);

			// Кричит заметивший (видит главного врага), командир командует.
			const APawn* Commander = Squad.Commander.Get();
			const APawn* Spotter = nullptr;
			for (const FMember& Member : Squad.Members)
			{
				const ANoFearShooterAIController* Bot = Member.Bot.Get();
				if (Bot && Member.AssistSquad.IsNone() && Bot->GetCurrentTarget() && Bot->IsTargetVisible())
				{
					Spotter = Member.Pawn.Get();
					break;
				}
			}
			if (!Spotter || Spotter == Commander)
			{
				SquadShout(Squad, Commander, ShoutContactToBattle);
			}
			else
			{
				SquadShout(Squad, Spotter, ShoutContact);
				SquadShout(Squad, Commander, ShoutToBattle);
			}
			return;
		}
		SetTactic(Squad, ENoFearSquadTactic::Engage, Now);
	}

	// Срочный отход перекатами: погиб командир, ранены все, мораль сломлена.
	bool bAllWounded = true;
	for (const FMember& Member : Squad.Members)
	{
		if (Member.Pawn.IsValid() && Member.AssistSquad.IsNone() && GetHealthFraction(Member.Pawn.Get()) >= WoundedFraction)
		{
			bAllWounded = false;
			break;
		}
	}
	const bool bCommanderLost = Squad.CommanderDeathTime > 0.0 && Now - Squad.CommanderDeathTime < 15.0;
	if (Squad.Tactic == ENoFearSquadTactic::Fallback)
	{
		// Уже отходят: гибель командира учтена.
		Squad.CommanderDeathTime = -1.0e9;
	}
	else if (Squad.bEngaged)
	{
		if (bCommanderLost)
		{
			BeginFallback(Squad, Now, ShoutCommanderDown);
			return;
		}
		if (bAllWounded && !Squad.bHoldingLine && Now >= Squad.NextFallbackTime)
		{
			BeginFallback(Squad, Now, ShoutAllWounded);
			return;
		}
		if (Squad.Morale < FallbackMorale && Now >= Squad.NextFallbackTime)
		{
			BeginFallback(Squad, Now, ShoutFallback);
			return;
		}
	}

	if (Squad.Tactic == ENoFearSquadTactic::Fallback)
	{
		UpdateLeapfrog(Squad, Now);
		bool bAllArrived = true;
		for (const FMember& Member : Squad.Members)
		{
			const APawn* Pawn = Member.Pawn.Get();
			if (Pawn && Member.AssistSquad.IsNone() && FVector::Dist2D(Pawn->GetActorLocation(), Squad.FallbackAnchor) > FallbackArriveRadius)
			{
				bAllArrived = false;
				break;
			}
		}
		if (bAllArrived || Squad.Swaps >= MaxFallbackSwaps || Now - Squad.TacticStartTime > 30.0)
		{
			// Дошли до рубежа: держать его, поводок — вокруг рубежа.
			Squad.bFallbackDone = true;
			Squad.bHoldingLine = true;
			Squad.NextFallbackTime = Now + FallbackCooldown;
			SetTactic(Squad, ENoFearSquadTactic::Defend, Now);
			Squad.TacticReason = TEXT("рубеж");
			SquadShout(Squad, Squad.Commander.Get(), ShoutHoldLine);
		}
		return;
	}

	if (Squad.Tactic == ENoFearSquadTactic::Contact && Now - Squad.TacticStartTime < ContactDuration)
	{
		return;
	}

	// Текущие манёвры: перебежки — смена групп; фланг и натиск — не закончились ли.
	const double InTactic = Now - Squad.TacticStartTime;
	bool bTacticEnded = false;
	switch (Squad.Tactic)
	{
	case ENoFearSquadTactic::Bounding:
		UpdateLeapfrog(Squad, Now);
		if (Squad.Swaps >= MaxBoundSwaps || Squad.StuckPhases >= 2 || (InTactic > 2.0 && !NeedsToClose(Squad, Now, true)))
		{
			bTacticEnded = true;
			Squad.NextBoundTime = Now + 6.0;
		}
		break;
	case ENoFearSquadTactic::Flank:
		bTacticEnded = !IsFlankActive(Squad, Now);
		break;
	case ENoFearSquadTactic::Rush:
		bTacticEnded = IsRushDone(Squad, Now);
		break;
	default:
		break;
	}
	// Закончившийся манёвр сразу сдаётся в обычный бой: так срабатывают его перезарядки,
	// а новый манёвр того же рода начнётся с нуля (SetTactic на ту же тактику ничего не делает).
	if (bTacticEnded)
	{
		SetTactic(Squad, ENoFearSquadTactic::Engage, Now);
	}

	if (!bTacticEnded && Now < Squad.NextThinkTime)
	{
		return;
	}
	Squad.NextThinkTime = Now + TacticThinkInterval;

	// Оборона: на рубеже после отхода или мораль низкая (выход из обороны — выше DefendExitMorale).
	if (Squad.bHoldingLine)
	{
		if (Squad.Tactic != ENoFearSquadTactic::Defend)
		{
			SetTactic(Squad, ENoFearSquadTactic::Defend, Now);
			Squad.TacticReason = TEXT("рубеж");
		}
		return;
	}
	const bool bInDefend = Squad.Tactic == ENoFearSquadTactic::Defend;
	if (Squad.Morale < DefendMorale || (bInDefend && Squad.Morale < DefendExitMorale))
	{
		if (!bInDefend)
		{
			SetTactic(Squad, ENoFearSquadTactic::Defend, Now);
			Squad.TacticReason = TEXT("мораль");
		}
		return;
	}

	// Начатый манёвр доводится до конца.
	if (!bTacticEnded && (Squad.Tactic == ENoFearSquadTactic::Bounding || Squad.Tactic == ENoFearSquadTactic::Flank
		|| Squad.Tactic == ENoFearSquadTactic::Rush))
	{
		return;
	}
	if (!bTacticEnded && Squad.Tactic != ENoFearSquadTactic::Contact && InTactic < MinTacticTime)
	{
		return;
	}

	// Натиск: враг перезаряжается, ранен или прижат — и он близко.
	if (Squad.Morale >= RushMorale && Alive >= 2 && Now >= Squad.NextRushTime)
	{
		const FString Reason = GetRushReason(Squad, Now);
		if (!Reason.IsEmpty())
		{
			SetTactic(Squad, ENoFearSquadTactic::Rush, Now);
			Squad.TacticReason = FString::Printf(TEXT("враг %s"), *Reason);
			const FText Text = FText::Format(NSLOCTEXT("NoFear", "SquadRush", "{0}! Дави!"),
				FText::FromString(Reason.Left(1).ToUpper() + Reason.Mid(1)));
			SquadShout(Squad, Squad.Commander.Get(), Text);
			return;
		}
	}

	// Фланг: враг засел и сидит — штурмовик обходит, остальные прижимают.
	if (Alive >= 3 && Squad.Morale >= 0.5f && Now >= Squad.NextFlankTime && Squad.MainEnemy.IsValid()
		&& Now - Squad.DugInSince >= FlankDugInTime && Now - Squad.MainEnemyLastSeen <= 8.0 && TryBeginFlank(Squad, Now))
	{
		return;
	}

	// Перебежки: врага не достать оттуда, где стоим.
	if (Alive >= 2 && Now >= Squad.NextBoundTime && NeedsToClose(Squad, Now))
	{
		BeginBounding(Squad, Now);
		return;
	}

	if (Squad.Tactic != ENoFearSquadTactic::Engage)
	{
		SetTactic(Squad, ENoFearSquadTactic::Engage, Now);
	}
}

void UNoFearSquadSubsystem::SetTactic(FSquad& Squad, ENoFearSquadTactic Tactic, double Now)
{
	if (Squad.Tactic == Tactic)
	{
		return;
	}
	const ENoFearSquadTactic Old = Squad.Tactic;
	if (Old == ENoFearSquadTactic::Flank)
	{
		Squad.Flanker = nullptr;
		Squad.FlankArriveTime = -1.0;
		Squad.NextFlankTime = FMath::Max(Squad.NextFlankTime, Now + FlankCooldown);
	}
	else if (Old == ENoFearSquadTactic::Rush)
	{
		Squad.NextRushTime = Now + RushCooldown;
	}

	Squad.Tactic = Tactic;
	Squad.TacticStartTime = Now;
	Squad.NextThinkTime = Now + TacticThinkInterval;
	Squad.TacticReason.Reset();

	FText Text;
	switch (Tactic)
	{
	case ENoFearSquadTactic::Bounding:
		Text = ShoutBounding;
		break;
	case ENoFearSquadTactic::Defend:
		Text = ShoutDefend;
		break;
	case ENoFearSquadTactic::Regroup:
		Text = ShoutRegroup;
		break;
	default:
		break;
	}
	if (!Text.IsEmpty())
	{
		SquadShout(Squad, Squad.Commander.Get(), Text);
	}
}

void UNoFearSquadSubsystem::BeginBounding(FSquad& Squad, double Now)
{
	// Командир — в группе A, остальные через одного; первой идёт группа B.
	bool bTeamB = true;
	for (FMember& Member : Squad.Members)
	{
		if (Member.Pawn.Get() == Squad.Commander.Get())
		{
			Member.bTeamB = false;
			continue;
		}
		Member.bTeamB = bTeamB;
		bTeamB = !bTeamB;
	}
	Squad.bTeamBMoving = true;
	Squad.PhaseStartTime = Now;
	Squad.Swaps = 0;
	Squad.StuckPhases = 0;
	SetTactic(Squad, ENoFearSquadTactic::Bounding, Now);
	Squad.TacticReason = Now - Squad.MainEnemyLastSeen >= BoundHiddenTime ? TEXT("враг не виден") : TEXT("далеко");
}

void UNoFearSquadSubsystem::BeginFallback(FSquad& Squad, double Now, const FText& ShoutText)
{
	const UWorld* World = GetWorld();
	const FVector Centroid = GetCentroid(Squad);
	const FVector Home = GetHomeCentroid(Squad);

	// Откуда угроза: главный враг, иначе середина всех известных.
	FVector Threat = Squad.MainEnemyLocation;
	bool bHasThreat = Squad.MainEnemy.IsValid();
	if (!bHasThreat)
	{
		FVector Sum = FVector::ZeroVector;
		int32 Count = 0;
		for (const TPair<TWeakObjectPtr<AActor>, FEnemy>& Pair : Squad.Enemies)
		{
			if (Pair.Key.IsValid())
			{
				Sum += Pair.Value.Location;
				++Count;
			}
		}
		if (Count > 0)
		{
			Threat = Sum / Count;
			bHasThreat = true;
		}
	}

	// Рубеж — посты отряда, если они заметно дальше от врага; иначе на FallbackDistance прочь от врага.
	FVector Anchor = Home;
	if (bHasThreat && FVector::Dist2D(Home, Threat) < FVector::Dist2D(Centroid, Threat) + 500.f)
	{
		FVector Away = (Centroid - Threat).GetSafeNormal2D();
		if (Away.IsNearlyZero())
		{
			Away = (Home - Threat).GetSafeNormal2D();
		}
		if (Away.IsNearlyZero())
		{
			Away = FVector::ForwardVector;
		}
		bool bFound = false;
		const UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
		for (const float Fraction : { 1.f, 0.66f, 0.33f })
		{
			const FVector Wanted = Centroid + Away * FallbackDistance * Fraction;
			FVector Point;
			if (!ProjectToNav(World, Wanted, FVector(500.f, 500.f, 500.f), Point))
			{
				continue;
			}
			// До рубежа должен быть путь, а не стена между.
			FVector::FReal PathLength = 0.0;
			if (NavSys && NavSys->GetPathLength(Centroid, Point, PathLength) == ENavigationQueryResult::Success
				&& PathLength <= FVector::Dist(Centroid, Point) * 2.5 + 300.0)
			{
				Anchor = Point;
				bFound = true;
				break;
			}
		}
		if (!bFound)
		{
			Anchor = Home;
		}
	}

	// Группы по удалённости от врага через одного; первой отходит та, где ближний к врагу.
	TArray<FMember*> Ordered;
	for (FMember& Member : Squad.Members)
	{
		if (Member.Pawn.IsValid() && Member.AssistSquad.IsNone())
		{
			Ordered.Add(&Member);
		}
	}
	if (bHasThreat)
	{
		Ordered.Sort([&Threat](const FMember& A, const FMember& B)
		{
			return FVector::DistSquared(A.Pawn->GetActorLocation(), Threat) < FVector::DistSquared(B.Pawn->GetActorLocation(), Threat);
		});
	}
	for (int32 Index = 0; Index < Ordered.Num(); ++Index)
	{
		Ordered[Index]->bTeamB = Index % 2 == 0;
	}

	Squad.FallbackAnchor = Anchor;
	Squad.bFallbackDone = false;
	Squad.bHoldingLine = false;
	Squad.bTeamBMoving = true;
	Squad.PhaseStartTime = Now;
	Squad.Swaps = 0;
	Squad.StuckPhases = 0;
	Squad.CommanderDeathTime = -1.0e9;
	SetTactic(Squad, ENoFearSquadTactic::Fallback, Now);
	Squad.TacticReason = ShoutText.ToString();
	SquadShout(Squad, Squad.Commander.Get(), ShoutText, 2.5f);
}

void UNoFearSquadSubsystem::BeginRegroup(FSquad& Squad, double Now)
{
	// Сбор у командира (после отхода — на рубеже).
	const APawn* Commander = Squad.Commander.Get();
	FVector Center = Squad.bHoldingLine ? Squad.FallbackAnchor : (Commander ? Commander->GetActorLocation() : GetCentroid(Squad));
	FVector Projected;
	if (ProjectToNav(GetWorld(), Center, FVector(300.f, 300.f, 500.f), Projected))
	{
		Center = Projected;
	}
	Squad.RallyCenter = Center;
	SetTactic(Squad, ENoFearSquadTactic::Regroup, Now);
}

bool UNoFearSquadSubsystem::TryBeginFlank(FSquad& Squad, double Now)
{
	AActor* Enemy = Squad.MainEnemy.Get();
	if (!Enemy)
	{
		return false;
	}
	const FVector Centroid = GetCentroid(Squad);

	// Обходит штурмовик, нет его — стрелок. Раненый, отходящий и занятый помощью не годятся.
	TArray<FMember*> Candidates;
	for (FMember& Member : Squad.Members)
	{
		const ANoFearShooterAIController* Bot = Member.Bot.Get();
		const APawn* Pawn = Member.Pawn.Get();
		if (!Bot || !Pawn || !Member.AssistSquad.IsNone() || Member.HelpOrder != ENoFearSquadOrder::None || Bot->IsRetreating()
			|| GetHealthFraction(Pawn) < WoundedFraction)
		{
			continue;
		}
		if (Member.Role == ENoFearSquadRole::Assault || Member.Role == ENoFearSquadRole::Rifleman)
		{
			Candidates.Add(&Member);
		}
	}
	Candidates.StableSort([](const FMember& A, const FMember& B)
	{
		return A.Role == ENoFearSquadRole::Assault && B.Role != ENoFearSquadRole::Assault;
	});

	for (int32 Index = 0; Index < Candidates.Num() && Index < 2; ++Index)
	{
		FMember& Member = *Candidates[Index];
		FVector Point;
		if (!Member.Bot->SearchFlankCover(Enemy, Squad.MainEnemyLocation, Centroid, FlankMinAngle, Point))
		{
			continue;
		}
		SetTactic(Squad, ENoFearSquadTactic::Flank, Now);
		Squad.Flanker = Member.Pawn;
		Squad.FlankPoint = Point;
		Squad.FlankArriveTime = -1.0;
		Squad.TacticReason = FString::Printf(TEXT("засел %.0f с"), Now - Squad.DugInSince);

		if (FVector::Dist2D(Point, Member.Pawn->GetActorLocation()) < 300.f)
		{
			// Он уже на фланге: обходить некуда — сразу перекрёстный огонь, пока отряд прижимает.
			Squad.FlankArriveTime = Now;
			SquadShout(Squad, Member.Pawn.Get(), ShoutInPosition);
			return true;
		}

		// С какой стороны — глазами отряда, смотрящего на врага.
		const FVector ToSquad = Centroid - Squad.MainEnemyLocation;
		const FVector ToFlank = Point - Squad.MainEnemyLocation;
		const bool bRight = FVector::CrossProduct(ToSquad, ToFlank).Z < 0.f;
		SquadShout(Squad, Squad.Commander.Get(), bRight ? ShoutFlankRight : ShoutFlankLeft);
		SquadShout(Squad, Member.Pawn.Get(), ShoutFlanking);
		return true;
	}
	Squad.NextFlankTime = Now + FlankCooldown;
	return false;
}

FString UNoFearSquadSubsystem::GetRushReason(const FSquad& Squad, double Now) const
{
	const AActor* Enemy = Squad.MainEnemy.Get();
	if (!Enemy || Now - Squad.MainEnemyLastSeen > 3.0 || FVector::Dist(GetCentroid(Squad), Squad.MainEnemyLocation) > RushMaxDistance)
	{
		return FString();
	}

	const UNoFearWeaponComponent* EnemyWeapons = Enemy->FindComponentByClass<UNoFearWeaponComponent>();
	const ANoFearWeapon* EnemyWeapon = EnemyWeapons ? EnemyWeapons->GetCurrentWeapon() : nullptr;
	if (EnemyWeapon && EnemyWeapon->GetWeaponState() == ENoFearWeaponState::Reloading)
	{
		return TEXT("перезаряжается");
	}
	if (GetHealthFraction(Enemy) < 0.4f)
	{
		return TEXT("ранен");
	}
	const UNoFearCombatComponent* EnemyCombat = Enemy->FindComponentByClass<UNoFearCombatComponent>();
	if (EnemyCombat && EnemyCombat->IsSuppressed())
	{
		return TEXT("прижат");
	}

	// Прижат огнём отряда: по нему стреляет большинство, а он в ответ не попадает.
	int32 Alive = 0;
	int32 Firing = 0;
	bool bRecentlyHit = false;
	for (const FMember& Member : Squad.Members)
	{
		const ANoFearShooterAIController* Bot = Member.Bot.Get();
		if (!Bot || !Member.AssistSquad.IsNone())
		{
			continue;
		}
		++Alive;
		bRecentlyHit |= Now - Member.LastDamageTime < 2.0;
		const UNoFearWeaponComponent* Weapons = Bot->GetWeaponComponent();
		const ANoFearWeapon* Weapon = Weapons ? Weapons->GetCurrentWeapon() : nullptr;
		if (Bot->GetCurrentTarget() == Enemy && Weapon && Now - Weapon->GetLastFireTime() <= 1.0)
		{
			++Firing;
		}
	}
	if (!bRecentlyHit && Firing >= FMath::Max(2, FMath::CeilToInt(Alive * 0.6f)))
	{
		return TEXT("прижат");
	}
	return FString();
}

bool UNoFearSquadSubsystem::NeedsToClose(const FSquad& Squad, double Now, bool bKeepClosing) const
{
	const AActor* Enemy = Squad.MainEnemy.Get();
	if (!Enemy)
	{
		return false;
	}
	if (Now - Squad.MainEnemyLastSeen >= BoundHiddenTime)
	{
		return true;
	}
	// Большинство бьющих по нему — вне удобной дистанции своего оружия.
	int32 Targeting = 0;
	int32 OutOfRange = 0;
	for (const FMember& Member : Squad.Members)
	{
		const ANoFearShooterAIController* Bot = Member.Bot.Get();
		if (Bot && Member.AssistSquad.IsNone() && Bot->GetCurrentTarget() == Enemy)
		{
			++Targeting;
			if (!Bot->IsTargetInWeaponRange())
			{
				++OutOfRange;
			}
		}
	}
	return Targeting > 0 && OutOfRange > 0 && (bKeepClosing ? OutOfRange * 2 >= Targeting : OutOfRange * 2 > Targeting);
}

void UNoFearSquadSubsystem::UpdateLeapfrog(FSquad& Squad, double Now)
{
	const double PhaseTime = Now - Squad.PhaseStartTime;
	if (PhaseTime < 1.5)
	{
		return;
	}
	const bool bFallback = Squad.Tactic == ENoFearSquadTactic::Fallback;
	const ENoFearSquadOrder MoveOrder = bFallback ? ENoFearSquadOrder::FallBack : ENoFearSquadOrder::Bound;

	int32 Movers = 0;
	int32 Settled = 0;
	int32 Stuck = 0;
	for (const FMember& Member : Squad.Members)
	{
		const ANoFearShooterAIController* Bot = Member.Bot.Get();
		if (!Bot || Member.bTeamB != Squad.bTeamBMoving || Member.Order != MoveOrder)
		{
			continue;
		}
		++Movers;
		if (IsSettled(Member))
		{
			++Settled;
		}
		if (Bot->GetHandledOrderSerial() == Member.OrderSerial && Bot->IsOrderStuck())
		{
			++Stuck;
		}
	}
	// Идущие ещё в пути — ждать (но не дольше BoundPhaseMaxTime).
	if (Movers > 0 && Settled < Movers && PhaseTime < BoundPhaseMaxTime)
	{
		return;
	}

	Squad.StuckPhases = Movers > 0 && Stuck == Movers ? Squad.StuckPhases + 1 : 0;
	Squad.bTeamBMoving = !Squad.bTeamBMoving;
	Squad.PhaseStartTime = Now;
	++Squad.Swaps;

	for (const FMember& Member : Squad.Members)
	{
		if (Member.Pawn.IsValid() && Member.AssistSquad.IsNone() && Member.bTeamB == Squad.bTeamBMoving
			&& GetHealthFraction(Member.Pawn.Get()) >= WoundedFraction)
		{
			SquadShout(Squad, Member.Pawn.Get(), bFallback ? ShoutFallingBack : ShoutMoving);
			break;
		}
	}
}

bool UNoFearSquadSubsystem::IsRegroupDone(const FSquad& Squad, double Now) const
{
	const double InTactic = Now - Squad.TacticStartTime;
	if (InTactic >= 30.0)
	{
		return true;
	}
	if (InTactic < 4.0)
	{
		return false;
	}
	// Все на местах вокруг командира и перевязаны (кому было кому перевязать).
	for (const FMember& Member : Squad.Members)
	{
		const APawn* Pawn = Member.Pawn.Get();
		if (!Pawn || !Member.AssistSquad.IsNone())
		{
			continue;
		}
		if (Member.HelpOrder == ENoFearSquadOrder::Heal)
		{
			return false;
		}
		if (Member.Order == ENoFearSquadOrder::Regroup && Member.bHasOrderLocation
			&& FVector::Dist2D(Pawn->GetActorLocation(), Member.OrderLocation) > 350.f)
		{
			return false;
		}
		if (CountActive(Squad) >= 2 && GetHealthFraction(Pawn) < HealRestFraction && Now >= Member.NextHealTime)
		{
			return false;
		}
	}
	return true;
}

bool UNoFearSquadSubsystem::IsFlankActive(FSquad& Squad, double Now)
{
	const APawn* FlankerPawn = Squad.Flanker.Get();
	const FMember* Member = FlankerPawn ? Squad.Members.FindByPredicate([FlankerPawn](const FMember& Candidate)
	{
		return Candidate.Pawn.Get() == FlankerPawn;
	}) : nullptr;
	const ANoFearShooterAIController* Bot = Member ? Member->Bot.Get() : nullptr;
	if (!Bot || !Member->AssistSquad.IsNone() || Bot->IsRetreating())
	{
		return false;
	}
	// Дошёл: держит фланг и бьёт перекрёстным огнём, отряд прижимает — потом обычный бой.
	if (Squad.FlankArriveTime >= 0.0)
	{
		return Now - Squad.FlankArriveTime < FlankFireTime;
	}
	if (Now - Squad.TacticStartTime > FlankMaxTime)
	{
		return false;
	}
	if (Member->Order != ENoFearSquadOrder::Flank || Bot->GetHandledOrderSerial() != Member->OrderSerial)
	{
		return true;
	}
	if (Bot->IsOrderStuck())
	{
		return false;
	}
	if (Bot->IsInCover() && FVector::Dist2D(Bot->GetCoverLocation(), Squad.FlankPoint) < 150.f)
	{
		Squad.FlankArriveTime = Now;
		SquadShout(Squad, FlankerPawn, ShoutInPosition);
		return FlankFireTime > 0.f;
	}
	return true;
}

bool UNoFearSquadSubsystem::IsRushDone(const FSquad& Squad, double Now) const
{
	const double InTactic = Now - Squad.TacticStartTime;
	const AActor* Enemy = Squad.MainEnemy.Get();
	if (InTactic > RushMaxTime || !Enemy || UNoFearHealthComponent::IsActorDead(Enemy))
	{
		return true;
	}
	if (InTactic < 2.0)
	{
		return false;
	}
	int32 Rushers = 0;
	int32 Settled = 0;
	for (const FMember& Member : Squad.Members)
	{
		if (Member.Order == ENoFearSquadOrder::Rush)
		{
			++Rushers;
			Settled += IsSettled(Member) ? 1 : 0;
		}
	}
	return Rushers == 0 || Settled == Rushers;
}

void UNoFearSquadSubsystem::IssueOrders(FSquad& Squad, double Now)
{
	AActor* MainEnemy = Squad.MainEnemy.Get();
	const APawn* Commander = Squad.Commander.Get();
	const APawn* Flanker = Squad.Flanker.Get();
	const int32 Alive = CountActive(Squad);
	const bool bHasSupport = Squad.Members.ContainsByPredicate([](const FMember& Member)
	{
		return Member.Role == ENoFearSquadRole::Support && Member.Pawn.IsValid() && Member.AssistSquad.IsNone();
	});
	const bool bCombatTactic = Squad.Tactic != ENoFearSquadTactic::None && Squad.Tactic != ENoFearSquadTactic::Regroup;

	// Кого сейчас перевязывают: в бою он держит позицию, чтобы санитар его догнал.
	TArray<const AActor*, TInlineAllocator<2>> Patients;
	for (const FMember& Member : Squad.Members)
	{
		if (Member.HelpOrder == ENoFearSquadOrder::Heal && Member.HelpTarget.IsValid())
		{
			Patients.Add(Member.HelpTarget.Get());
		}
	}

	// Места в сборе: командир — в центре, остальные — кольцом в 200 см.
	const int32 RingCount = FMath::Max(1, Alive - 1);
	int32 RingIndex = 0;
	const FSquad* AssistCaller = nullptr;
	FVector AssistPoint = FVector::ZeroVector;

	for (FMember& Member : Squad.Members)
	{
		const APawn* Pawn = Member.Pawn.Get();
		if (!Pawn || !Member.Bot.IsValid())
		{
			continue;
		}

		// Помощь соседнему отряду — поверх всего.
		if (!Member.AssistSquad.IsNone())
		{
			if (const FSquad* Caller = FindSquadByName(Member.AssistSquad, Squad.Team))
			{
				// Точка с проверкой пути — одна на всех идущих к этому отряду.
				if (Caller != AssistCaller)
				{
					AssistCaller = Caller;
					AssistPoint = GetAssistPoint(*Caller);
				}
				SetOrder(Member, ENoFearSquadOrder::Assist, Now, &AssistPoint, nullptr, Caller->MainEnemy.Get());
				continue;
			}
		}

		// Помощь своим — поверх тактики.
		if (Member.HelpOrder != ENoFearSquadOrder::None && Member.HelpTarget.IsValid())
		{
			AActor* Enemy = Member.HelpEnemy.IsValid() ? Member.HelpEnemy.Get() : MainEnemy;
			SetOrder(Member, Member.HelpOrder, Now, nullptr, Member.HelpTarget.Get(), Enemy);
			continue;
		}

		const bool bWounded = GetHealthFraction(Pawn) < WoundedFraction;
		ENoFearSquadOrder Order = ENoFearSquadOrder::None;
		FVector Location = FVector::ZeroVector;
		bool bHasLocation = false;
		switch (Squad.Tactic)
		{
		case ENoFearSquadTactic::Contact:
			Order = ENoFearSquadOrder::TakeCover;
			break;
		case ENoFearSquadTactic::Bounding:
			// Раненый не бегает: прикрывает огнём.
			Order = Member.bTeamB == Squad.bTeamBMoving && !bWounded ? ENoFearSquadOrder::Bound : ENoFearSquadOrder::Suppress;
			break;
		case ENoFearSquadTactic::Flank:
			if (Pawn == Flanker)
			{
				Order = ENoFearSquadOrder::Flank;
				Location = Squad.FlankPoint;
				bHasLocation = true;
			}
			else
			{
				Order = ENoFearSquadOrder::Suppress;
			}
			break;
		case ENoFearSquadTactic::Rush:
		{
			// Поддержка прижимает огнём (нет её — командир, если есть кому бежать).
			const bool bCovers = Member.Role == ENoFearSquadRole::Support
				|| (!bHasSupport && Member.Role == ENoFearSquadRole::Commander && Alive >= 3);
			Order = bCovers || bWounded ? ENoFearSquadOrder::Suppress : ENoFearSquadOrder::Rush;
			break;
		}
		case ENoFearSquadTactic::Defend:
			Order = ENoFearSquadOrder::Hold;
			if (Squad.bHoldingLine && FVector::Dist2D(Pawn->GetActorLocation(), Squad.FallbackAnchor) > FallbackLeashRadius)
			{
				// Отстал при отходе: подтянуться к рубежу перекатами (за поводком укрытий ему не найдут, стоять в открытую нельзя).
				Order = ENoFearSquadOrder::FallBack;
				Location = Squad.FallbackAnchor;
				bHasLocation = true;
			}
			break;
		case ENoFearSquadTactic::Fallback:
			Location = Squad.FallbackAnchor;
			if (Alive <= 1 || Member.bTeamB == Squad.bTeamBMoving)
			{
				Order = ENoFearSquadOrder::FallBack;
				bHasLocation = true;
			}
			else
			{
				Order = ENoFearSquadOrder::CoverFallback;
			}
			break;
		case ENoFearSquadTactic::Regroup:
			Order = ENoFearSquadOrder::Regroup;
			Location = Squad.RallyCenter;
			if (Pawn != Commander)
			{
				const float Angle = 2.f * PI * RingIndex / RingCount;
				++RingIndex;
				const FVector Slot = Squad.RallyCenter + FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0.f) * 200.f;
				if (!ProjectToNav(GetWorld(), Slot, FVector(100.f, 100.f, 300.f), Location))
				{
					Location = Squad.RallyCenter;
				}
			}
			bHasLocation = true;
			break;
		default:
			break;
		}

		if (bCombatTactic && Order != ENoFearSquadOrder::FallBack && Patients.Contains(Pawn))
		{
			Order = ENoFearSquadOrder::Hold;
			bHasLocation = false;
		}
		const bool bNeedsEnemy = Order != ENoFearSquadOrder::None && Order != ENoFearSquadOrder::Regroup;
		SetOrder(Member, Order, Now, bHasLocation ? &Location : nullptr, nullptr, bNeedsEnemy ? MainEnemy : nullptr);
		if (Order == ENoFearSquadOrder::FallBack && Squad.Tactic == ENoFearSquadTactic::Defend && IsSettled(Member) && Now - Member.OrderTime > 3.0)
		{
			// Отставший дошёл до укрытия на пути к рубежу и отдышался — следующий перекат (новый номер приказа).
			Member.OrderTime = Now;
			Member.OrderSerial = ++OrderSerialCounter;
		}
	}
}

void UNoFearSquadSubsystem::SetOrder(FMember& Member, ENoFearSquadOrder Order, double Now, const FVector* Location, AActor* Target, AActor* Enemy)
{
	// Новый номер — только если приказ правда другой: иначе боец будет заново искать укрытие каждые 0.2 с.
	const bool bHasLocation = Location != nullptr;
	const bool bChanged = Member.Order != Order || Member.OrderTarget.Get() != Target || Member.bHasOrderLocation != bHasLocation
		|| (bHasLocation && FVector::DistSquared(Member.OrderLocation, *Location) > FMath::Square(300.f));
	Member.OrderEnemy = Enemy;
	if (!bChanged)
	{
		return;
	}
	Member.Order = Order;
	Member.OrderLocation = bHasLocation ? *Location : FVector::ZeroVector;
	Member.bHasOrderLocation = bHasLocation;
	Member.OrderTarget = Target;
	Member.OrderTime = Now;
	Member.OrderSerial = ++OrderSerialCounter;
}

void UNoFearSquadSubsystem::ClearHelp(FMember& Member)
{
	Member.HelpOrder = ENoFearSquadOrder::None;
	Member.HelpTarget = nullptr;
	Member.HelpEnemy = nullptr;
	Member.HelpUntil = 0.0;
}

void UNoFearSquadSubsystem::UpdateHelp(FSquad& Squad, double Now)
{
	const int32 Alive = CountActive(Squad);
	auto IsInSquad = [&Squad](const AActor* Actor)
	{
		return Actor && Squad.Members.ContainsByPredicate([Actor](const FMember& Member) { return Member.Pawn.Get() == Actor; });
	};

	// Отработанная помощь снимается.
	for (FMember& Member : Squad.Members)
	{
		if (Member.HelpOrder == ENoFearSquadOrder::None)
		{
			continue;
		}
		const APawn* Pawn = Member.Pawn.Get();
		AActor* Target = Member.HelpTarget.Get();
		bool bExpire = !Pawn || !Target || UNoFearHealthComponent::IsActorDead(Target) || !IsInSquad(Target)
			|| !Member.AssistSquad.IsNone() || GetHealthFraction(Pawn) < WoundedFraction;
		if (!bExpire && Now > Member.HelpUntil)
		{
			bExpire = true;
			if (Member.HelpOrder == ENoFearSquadOrder::Heal)
			{
				// Не дошёл до раненого — к нему попробуют снова чуть позже.
				FMember* Patient = Squad.Members.FindByPredicate([Target](const FMember& Candidate) { return Candidate.Pawn.Get() == Target; });
				if (Patient)
				{
					Patient->NextHealTime = Now + 5.0;
				}
			}
		}
		if (!bExpire && Squad.Tactic == ENoFearSquadTactic::Fallback
			&& (Member.HelpOrder == ENoFearSquadOrder::Reinforce || Member.HelpOrder == ENoFearSquadOrder::Heal))
		{
			bExpire = true;
		}
		if (!bExpire && Member.HelpOrder == ENoFearSquadOrder::Reinforce
			&& FVector::Dist2D(Pawn->GetActorLocation(), Target->GetActorLocation()) <= ReinforceArriveRadius)
		{
			// Подмога пришла.
			bExpire = true;
			AddMorale(Squad, ReinforceMoraleGain);
		}
		if (bExpire)
		{
			ClearHelp(Member);
		}
	}

	// Подмога прижатому (давно под огнём) или только что раненому.
	if (Squad.bEngaged && Squad.Tactic != ENoFearSquadTactic::Fallback && Squad.Tactic != ENoFearSquadTactic::Contact)
	{
		for (FMember& Patient : Squad.Members)
		{
			APawn* PatientPawn = Patient.Pawn.Get();
			const ANoFearShooterAIController* PatientBot = Patient.Bot.Get();
			if (!PatientPawn || !PatientBot || !Patient.AssistSquad.IsNone() || Now < Patient.NextReinforceTime)
			{
				continue;
			}
			const bool bPinned = Patient.SuppressedSince >= 0.0 && Now - Patient.SuppressedSince > ReinforcePinnedTime;
			const bool bHurt = Now - Patient.LastDamageTime < 2.0 && GetHealthFraction(PatientPawn) < ReinforceHealthFraction;
			if ((!bPinned && !bHurt) || PatientBot->IsRetreating() || IsMovingOrder(Patient.Order))
			{
				continue;
			}
			const bool bAlreadyHelped = Squad.Members.ContainsByPredicate([PatientPawn](const FMember& Member)
			{
				return Member.HelpOrder == ENoFearSquadOrder::Reinforce && Member.HelpTarget.Get() == PatientPawn;
			});
			if (bAlreadyHelped)
			{
				continue;
			}

			TArray<TPair<float, FMember*>> Helpers;
			for (FMember& Helper : Squad.Members)
			{
				const APawn* HelperPawn = Helper.Pawn.Get();
				if (&Helper == &Patient || !HelperPawn || !IsFreeForHelp(Helper) || HelperPawn == Squad.Flanker.Get()
					|| GetHealthFraction(HelperPawn) < WoundedFraction || (Helper.Role == ENoFearSquadRole::Commander && Alive > 2))
				{
					continue;
				}
				const float Distance = FVector::Dist(HelperPawn->GetActorLocation(), PatientPawn->GetActorLocation());
				if (Distance <= ReinforceRadius && Distance > ReinforceArriveRadius)
				{
					Helpers.Add({ Distance, &Helper });
				}
			}
			if (Helpers.IsEmpty())
			{
				Patient.NextReinforceTime = Now + 3.0;
				continue;
			}
			Helpers.Sort([](const TPair<float, FMember*>& A, const TPair<float, FMember*>& B) { return A.Key < B.Key; });
			const int32 Count = FMath::Min(Helpers.Num(), Alive >= 4 ? 2 : 1);
			AActor* Enemy = PatientBot->GetCurrentTarget() ? PatientBot->GetCurrentTarget() : Squad.MainEnemy.Get();
			for (int32 Index = 0; Index < Count; ++Index)
			{
				FMember& Helper = *Helpers[Index].Value;
				Helper.HelpOrder = ENoFearSquadOrder::Reinforce;
				Helper.HelpTarget = PatientPawn;
				Helper.HelpEnemy = Enemy;
				Helper.HelpUntil = Now + ReinforceMaxTime;
			}
			SquadShout(Squad, PatientPawn, bPinned ? ShoutPinned : ShoutWounded);
			SquadShout(Squad, Helpers[0].Value->Pawn.Get(), ShoutComing);
			Patient.NextReinforceTime = Now + ReinforceCooldown;
			break;
		}
	}

	// Перевязка: один санитар за раз. В бою — тяжёлых и до HealCombatTarget, после боя — всех до полного.
	if (Squad.Tactic == ENoFearSquadTactic::Fallback || Alive < 2
		|| Squad.Members.ContainsByPredicate([](const FMember& Member) { return Member.HelpOrder == ENoFearSquadOrder::Heal; }))
	{
		return;
	}
	const bool bRest = !Squad.bAlerted && (Squad.Tactic == ENoFearSquadTactic::None || Squad.Tactic == ENoFearSquadTactic::Regroup);
	const bool bCombat = Squad.bEngaged && Squad.Tactic != ENoFearSquadTactic::Contact && Squad.Tactic != ENoFearSquadTactic::Rush;
	if (!bRest && !bCombat)
	{
		return;
	}

	FMember* Patient = nullptr;
	float Lowest = bRest ? HealRestFraction : HealCombatFraction;
	for (FMember& Member : Squad.Members)
	{
		const APawn* Pawn = Member.Pawn.Get();
		if (!Pawn || !Member.AssistSquad.IsNone() || Now < Member.NextHealTime || Now - Member.LastDamageTime < HealSafeTime
			|| IsMovingOrder(Member.Order) || Pawn->GetVelocity().Size2D() > 50.f)
		{
			continue;
		}
		const float Fraction = GetHealthFraction(Pawn);
		if (Fraction < Lowest && !UNoFearHealthComponent::IsActorDead(Pawn))
		{
			Patient = &Member;
			Lowest = Fraction;
		}
	}
	if (!Patient)
	{
		return;
	}

	// Санитар — поддержка, иначе ближайший здоровый (командир — только если их двое).
	APawn* PatientPawn = Patient->Pawn.Get();
	FMember* Medic = nullptr;
	float BestScore = TNumericLimits<float>::Max();
	for (FMember& Member : Squad.Members)
	{
		const APawn* Pawn = Member.Pawn.Get();
		if (&Member == Patient || !Pawn || !IsFreeForHelp(Member) || Pawn == Squad.Flanker.Get()
			|| GetHealthFraction(Pawn) < WoundedFraction || (Member.Role == ENoFearSquadRole::Commander && Alive > 2))
		{
			continue;
		}
		const float Distance = FVector::Dist(Pawn->GetActorLocation(), PatientPawn->GetActorLocation());
		if (!bRest && Distance > HealMaxDistance)
		{
			continue;
		}
		const float Score = Distance - (Member.Role == ENoFearSquadRole::Support ? 100000.f : 0.f);
		if (Score < BestScore)
		{
			Medic = &Member;
			BestScore = Score;
		}
	}
	if (!Medic)
	{
		return;
	}
	Medic->HelpOrder = ENoFearSquadOrder::Heal;
	Medic->HelpTarget = PatientPawn;
	Medic->HelpEnemy = nullptr;
	Medic->HelpUntil = Now + HealOrderTimeout;
	Medic->HealTargetFraction = bRest ? 1.f : HealCombatTarget;
	Patient->NextHealTime = Now + 3.0;
	SquadShout(Squad, Medic->Pawn.Get(), ShoutBandage);
}

bool UNoFearSquadSubsystem::CompleteBandage(APawn* Medic, APawn* Patient)
{
	FMember* MedicMember = nullptr;
	FMember* PatientMember = nullptr;
	FSquad* Squad = FindSquadOf(Medic, &MedicMember);
	if (!Squad || !Patient || Medic == Patient || FindSquadOf(Patient, &PatientMember) != Squad)
	{
		return false;
	}
	UNoFearHealthComponent* Health = UNoFearHealthComponent::FindHealth(Patient);
	if (!Health || Health->IsDead())
	{
		return false;
	}
	const UWorld* World = GetWorld();
	const double Now = World ? World->GetTimeSeconds() : 0.0;

	const bool bOrdered = MedicMember->HelpOrder == ENoFearSquadOrder::Heal && MedicMember->HelpTarget.Get() == Patient;
	const float TargetFraction = bOrdered ? MedicMember->HealTargetFraction : (Squad->bEngaged ? HealCombatTarget : 1.f);
	const float Amount = Health->GetMaxHealth() * TargetFraction - Health->GetHealth();
	if (Amount > 0.f)
	{
		Health->Heal(Amount);
	}
	PatientMember->NextHealTime = Now + HealCooldown;
	PatientMember->LastHealth = Health->GetHealth();
	AddMorale(*Squad, BandageMoraleGain);
	if (bOrdered)
	{
		// Снять приказ сразу, чтобы дерево не начало перевязку заново до следующего обновления отрядов.
		ClearHelp(*MedicMember);
		SetOrder(*MedicMember, ENoFearSquadOrder::None, Now, nullptr, nullptr, nullptr);
	}
	SquadShout(*Squad, Medic, ShoutBandageDone);
	return true;
}

void UNoFearSquadSubsystem::AbortBandage(APawn* Medic, APawn* Patient)
{
	FMember* MedicMember = nullptr;
	FSquad* Squad = FindSquadOf(Medic, &MedicMember);
	if (!Squad || MedicMember->HelpOrder != ENoFearSquadOrder::Heal || MedicMember->HelpTarget.Get() != Patient)
	{
		return;
	}
	const UWorld* World = GetWorld();
	const double Now = World ? World->GetTimeSeconds() : 0.0;
	if (FMember* PatientMember = Squad->Members.FindByPredicate([Patient](const FMember& Member) { return Member.Pawn.Get() == Patient; }))
	{
		PatientMember->NextHealTime = Now + 5.0;
	}
	// Как и после перевязки: снять сразу, иначе дерево санитара застрянет в бою с ключом HealTarget.
	ClearHelp(*MedicMember);
	SetOrder(*MedicMember, ENoFearSquadOrder::None, Now, nullptr, nullptr, nullptr);
}

APawn* UNoFearSquadSubsystem::GetMedicFor(const APawn* Patient) const
{
	const FSquad* Squad = Patient ? FindSquadOf(Patient) : nullptr;
	if (!Squad)
	{
		return nullptr;
	}
	for (const FMember& Member : Squad->Members)
	{
		if (Member.HelpOrder == ENoFearSquadOrder::Heal && Member.HelpTarget.Get() == Patient)
		{
			return Member.Pawn.Get();
		}
	}
	return nullptr;
}

bool UNoFearSquadSubsystem::RequestCoveringFire(APawn* Requester, AActor* Threat, FVector ThreatLocation)
{
	FMember* RequesterMember = nullptr;
	FSquad* Squad = FindSquadOf(Requester, &RequesterMember);
	const UWorld* World = GetWorld();
	if (!Squad || !World || !Squad->bFormed || Squad->Members.Num() < 2)
	{
		return false;
	}
	const double Now = World->GetTimeSeconds();

	// Прикрывают ближайшие свободные, поддержка — первой.
	TArray<TPair<float, FMember*>> Candidates;
	for (FMember& Member : Squad->Members)
	{
		const APawn* Pawn = Member.Pawn.Get();
		if (&Member == RequesterMember || !Pawn || !Member.Bot.IsValid() || !Member.AssistSquad.IsNone()
			|| GetHealthFraction(Pawn) < WoundedFraction)
		{
			continue;
		}
		if (Member.HelpOrder != ENoFearSquadOrder::CoveringFire && !IsFreeForHelp(Member))
		{
			continue;
		}
		const float Distance = FVector::Dist(Pawn->GetActorLocation(), Requester->GetActorLocation());
		if (Distance <= 3000.f)
		{
			Candidates.Add({ Distance - (Member.Role == ENoFearSquadRole::Support ? 5000.f : 0.f), &Member });
		}
	}
	if (Candidates.IsEmpty())
	{
		return false;
	}
	Candidates.Sort([](const TPair<float, FMember*>& A, const TPair<float, FMember*>& B) { return A.Key < B.Key; });
	for (int32 Index = 0; Index < Candidates.Num() && Index < CoveringFireHelpers; ++Index)
	{
		FMember& Helper = *Candidates[Index].Value;
		Helper.HelpOrder = ENoFearSquadOrder::CoveringFire;
		Helper.HelpTarget = Requester;
		Helper.HelpEnemy = Threat;
		Helper.HelpUntil = Now + CoveringFireTime;
		Helper.Bot->ReceiveCoveringFireRequest(Requester, Threat, ThreatLocation);
	}
	return true;
}

void UNoFearSquadSubsystem::UpdateGrenades(FSquad& Squad, double Now)
{
	if (!Squad.bEngaged || Squad.Tactic == ENoFearSquadTactic::Fallback || Squad.Tactic == ENoFearSquadTactic::Regroup
		|| Squad.Tactic == ENoFearSquadTactic::None || Now < Squad.NextGrenadeTime || Now - Squad.ContactTime < FirstSquadGrenadeDelay)
	{
		return;
	}
	AActor* Enemy = Squad.MainEnemy.Get();
	if (!Enemy)
	{
		return;
	}

	// Повод: враг засел (сидит на месте) или враги кучкой.
	FString Reason;
	if (Now - Squad.DugInSince >= GrenadeDugInTime && Now - Squad.MainEnemyLastSeen <= GrenadeMaxUnseenTime)
	{
		Reason = TEXT("засел");
	}
	else
	{
		int32 Cluster = 0;
		for (const TPair<TWeakObjectPtr<AActor>, FEnemy>& Pair : Squad.Enemies)
		{
			if (Pair.Key.IsValid() && Now - Pair.Value.LastSeenTime <= 3.0
				&& FVector::Dist(Pair.Value.Location, Squad.MainEnemyLocation) <= GrenadeClusterRadius)
			{
				++Cluster;
			}
		}
		if (Cluster >= GrenadeClusterCount)
		{
			Reason = TEXT("кучка");
		}
	}
	if (Reason.IsEmpty())
	{
		Squad.NextGrenadeTime = Now + 1.0;
		return;
	}

	// Бросает не командир, кто на удобной дистанции и не занят перебежкой.
	TArray<TPair<float, FMember*>> Candidates;
	for (FMember& Member : Squad.Members)
	{
		ANoFearShooterAIController* Bot = Member.Bot.Get();
		const ANoFearCharacter* Character = Cast<ANoFearCharacter>(Member.Pawn.Get());
		const UNoFearGrenadeComponent* Grenades = Character ? Character->GetGrenadeComponent() : nullptr;
		if (!Bot || !Grenades || !Grenades->CanThrow() || Grenades->GetGrenadeCount() <= 0 || !Member.AssistSquad.IsNone()
			|| IsMovingOrder(Member.Order) || Member.SuppressedSince >= 0.0 || Bot->IsEvadingGrenade() || !Bot->bThrowGrenades)
		{
			continue;
		}
		const float Distance = FVector::Dist(Character->GetActorLocation(), Squad.MainEnemyLocation);
		if (Distance < Bot->MinGrenadeDistance || Distance > Bot->MaxGrenadeDistance)
		{
			continue;
		}
		const float Score = (Member.Role != ENoFearSquadRole::Commander ? 1.f : 0.f) - FMath::Abs(Distance - 1200.f) / 1000.f;
		Candidates.Add({ Score, &Member });
	}
	Candidates.Sort([](const TPair<float, FMember*>& A, const TPair<float, FMember*>& B) { return A.Key > B.Key; });
	for (const TPair<float, FMember*>& Candidate : Candidates)
	{
		FMember& Member = *Candidate.Value;
		if (Member.Bot->OrderGrenade(Enemy, Squad.MainEnemyLocation))
		{
			Member.GrenadeOrderTime = Now;
			if (Member.Pawn.Get() != Squad.Commander.Get())
			{
				SquadShout(Squad, Squad.Commander.Get(), ShoutGrenadeOrder);
			}
			Squad.NextGrenadeTime = Now + FMath::FRandRange(SquadGrenadeCooldown.X, FMath::Max(SquadGrenadeCooldown.X, SquadGrenadeCooldown.Y));
			return;
		}
	}
	Squad.NextGrenadeTime = Now + 2.0;
}

void UNoFearSquadSubsystem::UpdateAssists(double Now)
{
	// Помогающие тому, кто их уже не ждёт (отряд пропал или отпустил), — к своим.
	for (FSquad& Squad : Squads)
	{
		for (FMember& Member : Squad.Members)
		{
			if (Member.AssistSquad.IsNone())
			{
				continue;
			}
			const FSquad* Caller = FindSquadByName(Member.AssistSquad, Squad.Team);
			if (!Caller || Caller->HelperSquad != Squad.Name)
			{
				Member.AssistSquad = NAME_None;
			}
		}
	}

	for (FSquad& Caller : Squads)
	{
		if (!Caller.bFormed)
		{
			continue;
		}

		if (!Caller.HelperSquad.IsNone())
		{
			FSquad* Helper = FindSquadByName(Caller.HelperSquad, Caller.Team);
			TArray<FMember*> Helpers;
			if (Helper)
			{
				for (FMember& Member : Helper->Members)
				{
					if (Member.AssistSquad == Caller.Name && Member.Bot.IsValid() && Member.Pawn.IsValid())
					{
						Helpers.Add(&Member);
					}
				}
			}
			if (Helpers.IsEmpty() || Now - Caller.LastEngagedTime > 5.0 || Now - Caller.AssistStartTime > AssistMaxTime)
			{
				ReleaseHelpers(Caller);
				continue;
			}

			const FVector CallerCenter = GetCentroid(Caller);
			if (!Caller.bAssistArrived)
			{
				for (const FMember* Member : Helpers)
				{
					if (FVector::Dist2D(Member->Pawn->GetActorLocation(), CallerCenter) <= AssistArriveRadius)
					{
						Caller.bAssistArrived = true;
						AddMorale(Caller, HelpArrivedMoraleGain);
						SquadShout(*Helper, Member->Pawn.Get(), ShoutHelpArrived);
						break;
					}
				}
			}

			// Сведения о врагах: что видят помогающие — отряду, свежее отряда — помогающим.
			for (const FMember* Member : Helpers)
			{
				for (const TPair<TWeakObjectPtr<AActor>, FNoFearKnownActor>& Pair : Member->Bot->GetKnownActors())
				{
					const AActor* Enemy = Pair.Key.Get();
					if (Enemy && Pair.Value.bVisible && !UNoFearHealthComponent::IsActorDead(Enemy))
					{
						FEnemy& Known = Caller.Enemies.FindOrAdd(Pair.Key);
						Known.Location = Enemy->GetActorLocation();
						Known.LastSeenTime = Now;
					}
				}
			}
			for (const TPair<TWeakObjectPtr<AActor>, FEnemy>& Pair : Caller.Enemies)
			{
				AActor* Enemy = Pair.Key.Get();
				if (!Enemy || Now - Pair.Value.LastSeenTime > SquadIntelTime)
				{
					continue;
				}
				for (const FMember* Member : Helpers)
				{
					const FNoFearKnownActor* Own = Member->Bot->GetKnownActors().Find(Pair.Key);
					if (!Own || !Own->bVisible)
					{
						Member->Bot->ReceiveAlarm(Enemy, Pair.Value.Location);
					}
				}
			}
			continue;
		}

		// Звать соседей: оборона, отход или мораль низкая.
		if (!Caller.bEngaged || Now < Caller.NextHelpCallTime || Caller.Tactic == ENoFearSquadTactic::Contact)
		{
			continue;
		}
		const bool bNeedHelp = Caller.Tactic == ENoFearSquadTactic::Defend || Caller.Tactic == ENoFearSquadTactic::Fallback
			|| Caller.Morale < AssistMorale;
		if (!bNeedHelp)
		{
			continue;
		}

		// Ближайший свободный отряд своей команды: не в бою, никому не помогает, ему никто не помогает.
		const FVector CallerCenter = GetCentroid(Caller);
		FSquad* Best = nullptr;
		float BestDistance = AssistRadius;
		for (FSquad& Other : Squads)
		{
			if (&Other == &Caller || Other.Team != Caller.Team || !Other.bFormed || Other.Members.Num() < 2 || Other.bEngaged
				|| Other.bAlerted || !Other.HelperSquad.IsNone()
				|| Other.Members.ContainsByPredicate([](const FMember& Member) { return !Member.AssistSquad.IsNone(); }))
			{
				continue;
			}
			const float Distance = FVector::Dist2D(GetCentroid(Other), CallerCenter);
			if (Distance < BestDistance)
			{
				Best = &Other;
				BestDistance = Distance;
			}
		}
		if (!Best)
		{
			Caller.NextHelpCallTime = Now + 5.0;
			continue;
		}

		// Идёт половина (не меньше одного), ближайшие к просящим; командир остаётся.
		TArray<TPair<float, FMember*>> Candidates;
		for (FMember& Member : Best->Members)
		{
			const APawn* Pawn = Member.Pawn.Get();
			if (Pawn && Member.Bot.IsValid() && Member.Role != ENoFearSquadRole::Commander && GetHealthFraction(Pawn) >= WoundedFraction)
			{
				Candidates.Add({ static_cast<float>(FVector::Dist2D(Pawn->GetActorLocation(), CallerCenter)), &Member });
			}
		}
		if (Candidates.IsEmpty())
		{
			Caller.NextHelpCallTime = Now + 5.0;
			continue;
		}
		Candidates.Sort([](const TPair<float, FMember*>& A, const TPair<float, FMember*>& B) { return A.Key < B.Key; });
		const int32 Count = FMath::Min(Candidates.Num(), FMath::Max(1, Best->Members.Num() / 2));
		for (int32 Index = 0; Index < Count; ++Index)
		{
			FMember& Member = *Candidates[Index].Value;
			Member.AssistSquad = Caller.Name;
			ClearHelp(Member);
			if (AActor* Enemy = Caller.MainEnemy.Get())
			{
				Member.Bot->ReceiveAlarm(Enemy, Caller.MainEnemyLocation);
			}
		}
		Caller.HelperSquad = Best->Name;
		Caller.AssistStartTime = Now;
		Caller.bAssistArrived = false;
		Caller.NextHelpCallTime = Now + AssistCallCooldown;
		SquadShout(Caller, Caller.Commander.Get(), ShoutNeedHelp);
		SquadShout(*Best, Candidates[0].Value->Pawn.Get(), ShoutHelpComing);
	}
}

void UNoFearSquadSubsystem::ReleaseHelpers(FSquad& Caller)
{
	if (FSquad* Helper = FindSquadByName(Caller.HelperSquad, Caller.Team))
	{
		bool bShouted = false;
		for (FMember& Member : Helper->Members)
		{
			if (Member.AssistSquad != Caller.Name)
			{
				continue;
			}
			Member.AssistSquad = NAME_None;
			if (!bShouted)
			{
				SquadShout(*Helper, Member.Pawn.Get(), ShoutHelpReturn);
				bShouted = true;
			}
		}
	}
	Caller.HelperSquad = NAME_None;
	Caller.bAssistArrived = false;
}

// --- Мелочи ---

int32 UNoFearSquadSubsystem::CountActive(const FSquad& Squad) const
{
	int32 Count = 0;
	for (const FMember& Member : Squad.Members)
	{
		Count += Member.Pawn.IsValid() && Member.Bot.IsValid() && Member.AssistSquad.IsNone() ? 1 : 0;
	}
	return Count;
}

void UNoFearSquadSubsystem::AddMorale(FSquad& Squad, float Delta)
{
	Squad.Morale = FMath::Clamp(Squad.Morale + Delta, 0.f, 1.f);
}

void UNoFearSquadSubsystem::SquadShout(const FSquad& Squad, const APawn* Who, const FText& Text, float Duration) const
{
	if (!Who)
	{
		return;
	}
	for (const FMember& Member : Squad.Members)
	{
		if (Member.Pawn.Get() == Who)
		{
			if (ANoFearShooterAIController* Bot = Member.Bot.Get())
			{
				Bot->Shout(Text, Duration);
			}
			return;
		}
	}
}

FVector UNoFearSquadSubsystem::GetCentroid(const FSquad& Squad) const
{
	// Без ушедших на помощь соседям; ушли все — по всем.
	for (const bool bOnlyActive : { true, false })
	{
		FVector Sum = FVector::ZeroVector;
		int32 Count = 0;
		for (const FMember& Member : Squad.Members)
		{
			if (const APawn* Pawn = Member.Pawn.Get(); Pawn && (!bOnlyActive || Member.AssistSquad.IsNone()))
			{
				Sum += Pawn->GetActorLocation();
				++Count;
			}
		}
		if (Count > 0)
		{
			return Sum / Count;
		}
	}
	return FVector::ZeroVector;
}

FVector UNoFearSquadSubsystem::GetAssistPoint(const FSquad& Caller) const
{
	const FVector Centroid = GetCentroid(Caller);
	const APawn* Nearest = nullptr;
	double NearestDistance = TNumericLimits<double>::Max();
	for (const FMember& Member : Caller.Members)
	{
		const APawn* Pawn = Member.Pawn.Get();
		if (!Pawn || !Member.AssistSquad.IsNone())
		{
			continue;
		}
		const double Distance = FVector::DistSquared2D(Pawn->GetActorLocation(), Centroid);
		if (Distance < NearestDistance)
		{
			Nearest = Pawn;
			NearestDistance = Distance;
		}
	}
	if (!Nearest)
	{
		return Centroid;
	}
	// Центр отряда может прийтись в ящик: проекция ляжет на его крышу, куда пути нет, и MoveTo сразу провалится.
	// Поэтому — только если от бойца до точки есть полный путь; нет — идти к самому бойцу.
	const FVector From = Nearest->GetActorLocation();
	const UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
	FVector Point;
	FVector::FReal PathLength = 0.0;
	if (NavSys && ProjectToNav(GetWorld(), Centroid, FVector(150.f, 150.f, 300.f), Point)
		&& NavSys->GetPathLength(From, Point, PathLength) == ENavigationQueryResult::Success
		&& PathLength <= FVector::Dist(From, Point) * 2.5 + 300.0)
	{
		return Point;
	}
	return From;
}

FVector UNoFearSquadSubsystem::GetHomeCentroid(const FSquad& Squad) const
{
	for (const bool bOnlyActive : { true, false })
	{
		FVector Sum = FVector::ZeroVector;
		int32 Count = 0;
		for (const FMember& Member : Squad.Members)
		{
			if (const ANoFearShooterAIController* Bot = Member.Bot.Get(); Bot && (!bOnlyActive || Member.AssistSquad.IsNone()))
			{
				Sum += Bot->GetHomeLocation();
				++Count;
			}
		}
		if (Count > 0)
		{
			return Sum / Count;
		}
	}
	return FVector::ZeroVector;
}

bool UNoFearSquadSubsystem::IsFreeForHelp(const FMember& Member) const
{
	const ANoFearShooterAIController* Bot = Member.Bot.Get();
	return Bot && Member.AssistSquad.IsNone() && Member.HelpOrder == ENoFearSquadOrder::None && !IsMovingOrder(Member.Order)
		&& !Bot->IsRetreating() && !Bot->IsEvadingGrenade();
}

bool UNoFearSquadSubsystem::HasSettled(const FMember& Member, double Now) const
{
	const ANoFearShooterAIController* Bot = Member.Bot.Get();
	const APawn* Pawn = Member.Pawn.Get();
	if (!Bot || !Pawn)
	{
		return true;
	}
	if (Bot->GetHandledOrderSerial() != Member.OrderSerial)
	{
		return false;
	}
	if (Bot->IsOrderStuck())
	{
		return true;
	}
	if (Bot->HasCover())
	{
		return Bot->IsInCover();
	}
	if (Member.bHasOrderLocation && FVector::Dist2D(Pawn->GetActorLocation(), Member.OrderLocation) < 150.f)
	{
		return true;
	}
	return Pawn->GetVelocity().Size2D() < 50.f && Now - Member.OrderTime > 1.0;
}

void UNoFearSquadSubsystem::DrawDebug(double Now) const
{
	const UWorld* World = GetWorld();
	if (!World || !IsSquadDebugEnabled())
	{
		return;
	}
	const float Duration = UpdateInterval + 0.05f;
	for (const FSquad& Squad : Squads)
	{
		if (!Squad.bFormed)
		{
			continue;
		}
		if (const APawn* Commander = Squad.Commander.Get())
		{
			FString Text = FString::Printf(TEXT("%s: %s, мораль %.2f"), *Squad.Name.ToString(),
				Squad.Tactic == ENoFearSquadTactic::None ? TEXT("—") : *GetTacticText(Squad.Tactic).ToString(), Squad.Morale);
			if (!Squad.TacticReason.IsEmpty())
			{
				Text += FString::Printf(TEXT(" (%s)"), *Squad.TacticReason);
			}
			if (!Squad.HelperSquad.IsNone())
			{
				Text += FString::Printf(TEXT(", помогает %s"), *Squad.HelperSquad.ToString());
			}
			DrawDebugString(World, Commander->GetActorLocation() + FVector(0.f, 0.f, 150.f), Text, nullptr, FColor::Cyan, Duration, true);
		}
		if (Squad.Tactic == ENoFearSquadTactic::Fallback || Squad.bHoldingLine)
		{
			DrawDebugSphere(World, Squad.FallbackAnchor, Squad.bHoldingLine ? FallbackLeashRadius : FallbackArriveRadius, 16, FColor::Orange, false, Duration);
		}
		for (const FMember& Member : Squad.Members)
		{
			const APawn* Pawn = Member.Pawn.Get();
			if (!Pawn || Member.Order == ENoFearSquadOrder::None)
			{
				continue;
			}
			FVector End;
			if (Member.bHasOrderLocation)
			{
				End = Member.OrderLocation;
			}
			else if (const AActor* Target = Member.OrderTarget.Get())
			{
				End = Target->GetActorLocation();
			}
			else
			{
				continue;
			}
			DrawDebugLine(World, Pawn->GetActorLocation(), End, IsSettled(Member) ? FColor::Green : FColor::Yellow, false, Duration, 0, 2.f);
		}
	}
}

// --- Поиск ---

const UNoFearSquadSubsystem::FSquad* UNoFearSquadSubsystem::FindSquadOf(const APawn* Pawn, const FMember** OutMember) const
{
	if (!Pawn)
	{
		return nullptr;
	}
	for (const FSquad& Squad : Squads)
	{
		for (const FMember& Member : Squad.Members)
		{
			if (Member.Pawn.Get() == Pawn)
			{
				if (OutMember)
				{
					*OutMember = &Member;
				}
				return &Squad;
			}
		}
	}
	return nullptr;
}

UNoFearSquadSubsystem::FSquad* UNoFearSquadSubsystem::FindSquadOf(const APawn* Pawn, FMember** OutMember)
{
	const FMember* ConstMember = nullptr;
	const FSquad* Squad = static_cast<const UNoFearSquadSubsystem*>(this)->FindSquadOf(Pawn, &ConstMember);
	if (OutMember)
	{
		*OutMember = const_cast<FMember*>(ConstMember);
	}
	return const_cast<FSquad*>(Squad);
}

const UNoFearSquadSubsystem::FSquad* UNoFearSquadSubsystem::FindSquadByName(FName Name, ENoFearTeam Team) const
{
	return Name.IsNone() ? nullptr : Squads.FindByPredicate([Name, Team](const FSquad& Squad) { return Squad.Name == Name && Squad.Team == Team; });
}

UNoFearSquadSubsystem::FSquad* UNoFearSquadSubsystem::FindSquadByName(FName Name, ENoFearTeam Team)
{
	return const_cast<FSquad*>(static_cast<const UNoFearSquadSubsystem*>(this)->FindSquadByName(Name, Team));
}

// --- Запросы ---

ENoFearSquadRole UNoFearSquadSubsystem::GetSquadRole(const APawn* Pawn) const
{
	const FMember* Member = nullptr;
	return FindSquadOf(Pawn, &Member) ? Member->Role : ENoFearSquadRole::None;
}

APawn* UNoFearSquadSubsystem::GetSquadCommander(const APawn* Pawn) const
{
	const FSquad* Squad = FindSquadOf(Pawn);
	return Squad ? Squad->Commander.Get() : nullptr;
}

TArray<APawn*> UNoFearSquadSubsystem::GetSquadMembers(const APawn* Pawn) const
{
	TArray<APawn*> Result;
	if (const FSquad* Squad = FindSquadOf(Pawn))
	{
		for (const FMember& Member : Squad->Members)
		{
			if (APawn* MemberPawn = Member.Pawn.Get())
			{
				Result.Add(MemberPawn);
			}
		}
	}
	return Result;
}

bool UNoFearSquadSubsystem::IsInFormedSquad(const APawn* Pawn) const
{
	const FSquad* Squad = FindSquadOf(Pawn);
	return Squad && Squad->bFormed;
}

int32 UNoFearSquadSubsystem::GetSquadKnownEnemyCount(const APawn* Pawn) const
{
	const FSquad* Squad = FindSquadOf(Pawn);
	const UWorld* World = GetWorld();
	if (!Squad || !World)
	{
		return 0;
	}
	int32 Count = 0;
	for (const TPair<TWeakObjectPtr<AActor>, FEnemy>& Pair : Squad->Enemies)
	{
		if (Pair.Key.IsValid() && World->GetTimeSeconds() - Pair.Value.LastSeenTime <= SquadIntelTime)
		{
			++Count;
		}
	}
	return Count;
}

int32 UNoFearSquadSubsystem::CountSquadmatesTargeting(const APawn* Pawn, const AActor* Target) const
{
	const FSquad* Squad = Target ? FindSquadOf(Pawn) : nullptr;
	if (!Squad)
	{
		return 0;
	}
	int32 Count = 0;
	for (const FMember& Member : Squad->Members)
	{
		const ANoFearShooterAIController* Bot = Member.Bot.Get();
		if (Bot && Member.Pawn.Get() != Pawn && Bot->GetCurrentTarget() == Target)
		{
			++Count;
		}
	}
	return Count;
}

void UNoFearSquadSubsystem::SetExecutingOrder(const APawn* Pawn, bool bExecuting)
{
	FMember* Member = nullptr;
	if (FindSquadOf(Pawn, &Member))
	{
		Member->bExecutingOrder = bExecuting;
	}
}

ENoFearSquadOrder UNoFearSquadSubsystem::GetSquadOrder(const APawn* Pawn) const
{
	const FMember* Member = nullptr;
	const FSquad* Squad = FindSquadOf(Pawn, &Member);
	return Squad && Squad->bFormed ? Member->Order : ENoFearSquadOrder::None;
}

FNoFearSquadOrderInfo UNoFearSquadSubsystem::GetSquadOrderInfo(const APawn* Pawn) const
{
	FNoFearSquadOrderInfo Info;
	const FMember* Member = nullptr;
	const FSquad* Squad = FindSquadOf(Pawn, &Member);
	if (!Squad || !Squad->bFormed)
	{
		return Info;
	}
	Info.Order = Member->Order;
	Info.Location = Member->OrderLocation;
	Info.bHasLocation = Member->bHasOrderLocation;
	Info.Target = Member->OrderTarget.Get();
	Info.Enemy = Member->OrderEnemy.Get();
	Info.Time = Member->OrderTime;
	Info.Serial = Member->OrderSerial;
	if (Info.Enemy)
	{
		Info.bHasEnemyLocation = GetSquadEnemyLocation(Pawn, Info.Enemy, Info.EnemyLocation);
	}
	return Info;
}

ENoFearSquadTactic UNoFearSquadSubsystem::GetSquadTactic(const APawn* Pawn) const
{
	const FSquad* Squad = FindSquadOf(Pawn);
	return Squad ? Squad->Tactic : ENoFearSquadTactic::None;
}

float UNoFearSquadSubsystem::GetSquadMorale(const APawn* Pawn) const
{
	const FSquad* Squad = FindSquadOf(Pawn);
	return Squad ? Squad->Morale : -1.f;
}

void UNoFearSquadSubsystem::SetSquadMorale(const APawn* Pawn, float NewMorale)
{
	if (FSquad* Squad = FindSquadOf(Pawn, nullptr))
	{
		Squad->Morale = FMath::Clamp(NewMorale, 0.f, 1.f);
		// Решение — при ближайшем обновлении, а не через TacticThinkInterval.
		Squad->NextThinkTime = 0.0;
	}
}

FName UNoFearSquadSubsystem::GetAssistingSquad(const APawn* Pawn) const
{
	const FMember* Member = nullptr;
	return FindSquadOf(Pawn, &Member) ? Member->AssistSquad : NAME_None;
}

FName UNoFearSquadSubsystem::GetHelperSquad(const APawn* Pawn) const
{
	const FSquad* Squad = FindSquadOf(Pawn);
	return Squad ? Squad->HelperSquad : NAME_None;
}

bool UNoFearSquadSubsystem::GetSquadLeash(const APawn* Pawn, FVector& OutAnchor, float& OutRadius) const
{
	const FMember* Member = nullptr;
	const FSquad* Squad = FindSquadOf(Pawn, &Member);
	if (!Squad || !Squad->bFormed)
	{
		return false;
	}
	if (!Member->AssistSquad.IsNone())
	{
		if (const FSquad* Caller = FindSquadByName(Member->AssistSquad, Squad->Team))
		{
			OutAnchor = GetCentroid(*Caller);
			OutRadius = AssistLeashRadius;
			return true;
		}
	}
	if (Squad->Tactic == ENoFearSquadTactic::Fallback)
	{
		OutAnchor = Squad->FallbackAnchor;
		OutRadius = 0.f;
		return true;
	}
	if (Squad->bHoldingLine)
	{
		OutAnchor = Squad->FallbackAnchor;
		OutRadius = FallbackLeashRadius;
		return true;
	}
	OutAnchor = GetHomeCentroid(*Squad);
	OutRadius = SquadLeashRadius;
	return true;
}

bool UNoFearSquadSubsystem::GetSquadEnemyLocation(const APawn* Pawn, const AActor* Enemy, FVector& OutLocation) const
{
	const FMember* Member = nullptr;
	const FSquad* Squad = FindSquadOf(Pawn, &Member);
	if (!Squad || !Enemy)
	{
		return false;
	}
	// Помогающий знает и то, что видит отряд, которому он помогает.
	const FSquad* Sources[2] = { Squad, Member->AssistSquad.IsNone() ? nullptr : FindSquadByName(Member->AssistSquad, Squad->Team) };
	double BestTime = -1.0e9;
	bool bFound = false;
	for (const FSquad* Source : Sources)
	{
		if (!Source)
		{
			continue;
		}
		for (const TPair<TWeakObjectPtr<AActor>, FEnemy>& Pair : Source->Enemies)
		{
			if (Pair.Key.Get() == Enemy && Pair.Value.LastSeenTime > BestTime)
			{
				OutLocation = Pair.Value.Location;
				BestTime = Pair.Value.LastSeenTime;
				bFound = true;
			}
		}
		if (!bFound && Source->MainEnemy.Get() == Enemy)
		{
			OutLocation = Source->MainEnemyLocation;
			bFound = true;
		}
	}
	return bFound;
}

bool UNoFearSquadSubsystem::GetMemberView(const APawn* Pawn, FNoFearSquadMemberView& OutView) const
{
	const FMember* Member = nullptr;
	const FSquad* Squad = FindSquadOf(Pawn, &Member);
	if (!Squad)
	{
		return false;
	}
	const UWorld* World = GetWorld();
	const double Now = World ? World->GetTimeSeconds() : 0.0;
	OutView.Role = Member->Role;
	OutView.PreviousRole = Member->PreviousRole;
	OutView.JoinTime = Member->JoinTime;
	OutView.RoleChangeTime = Member->RoleChangeTime;
	OutView.CommanderChangeTime = Squad->CommanderChangeTime;
	OutView.Commander = Squad->Commander.Get();
	OutView.SquadSize = Squad->Members.Num();
	OutView.Tactic = Squad->Tactic;
	OutView.TacticChangeTime = Squad->TacticStartTime;
	OutView.Morale = Squad->Morale;
	OutView.bHoldingLine = Squad->bHoldingLine;
	OutView.AssistingSquad = Member->AssistSquad;

	const bool bOrders = Squad->bFormed;
	OutView.Order = bOrders ? Member->Order : ENoFearSquadOrder::None;
	OutView.OrderLocation = Member->OrderLocation;
	OutView.bHasOrderLocation = bOrders && Member->bHasOrderLocation;
	OutView.OrderTarget = bOrders ? Member->OrderTarget.Get() : nullptr;
	OutView.OrderTime = Member->OrderTime;
	OutView.bHasOrderEnemyLocation = false;
	if (const AActor* Enemy = bOrders ? Member->OrderEnemy.Get() : nullptr)
	{
		OutView.bHasOrderEnemyLocation = GetSquadEnemyLocation(Pawn, Enemy, OutView.OrderEnemyLocation);
	}
	OutView.bGrenadeOrder = Now - Member->GrenadeOrderTime < 2.5;
	OutView.bExecutingOrder = Member->bExecutingOrder
		|| (OutView.Order != ENoFearSquadOrder::None && OutView.Order != ENoFearSquadOrder::Hold);
	return true;
}

FText UNoFearSquadSubsystem::GetOrderText(ENoFearSquadOrder Order)
{
	switch (Order)
	{
	case ENoFearSquadOrder::TakeCover: return NSLOCTEXT("NoFear", "OrderTakeCover", "в укрытие");
	case ENoFearSquadOrder::Suppress: return NSLOCTEXT("NoFear", "OrderSuppress", "подавляет");
	case ENoFearSquadOrder::Bound: return NSLOCTEXT("NoFear", "OrderBound", "перебегает");
	case ENoFearSquadOrder::Flank: return NSLOCTEXT("NoFear", "OrderFlank", "обходит");
	case ENoFearSquadOrder::Rush: return NSLOCTEXT("NoFear", "OrderRush", "натиск");
	case ENoFearSquadOrder::Hold: return NSLOCTEXT("NoFear", "OrderHold", "держит");
	case ENoFearSquadOrder::FallBack: return NSLOCTEXT("NoFear", "OrderFallBack", "отходит");
	case ENoFearSquadOrder::CoverFallback: return NSLOCTEXT("NoFear", "OrderCoverFallback", "прикрывает");
	case ENoFearSquadOrder::CoveringFire: return NSLOCTEXT("NoFear", "OrderCoveringFire", "прикрывает");
	case ENoFearSquadOrder::Reinforce: return NSLOCTEXT("NoFear", "OrderReinforce", "на помощь");
	case ENoFearSquadOrder::Heal: return NSLOCTEXT("NoFear", "OrderHeal", "перевязка");
	case ENoFearSquadOrder::Regroup: return NSLOCTEXT("NoFear", "OrderRegroup", "сбор");
	case ENoFearSquadOrder::Assist: return NSLOCTEXT("NoFear", "OrderAssist", "к соседям");
	default: return FText::GetEmpty();
	}
}

FText UNoFearSquadSubsystem::GetTacticText(ENoFearSquadTactic Tactic)
{
	switch (Tactic)
	{
	case ENoFearSquadTactic::Contact: return NSLOCTEXT("NoFear", "TacticContact", "Контакт");
	case ENoFearSquadTactic::Engage: return NSLOCTEXT("NoFear", "TacticEngage", "Бой");
	case ENoFearSquadTactic::Bounding: return NSLOCTEXT("NoFear", "TacticBounding", "Перебежки");
	case ENoFearSquadTactic::Flank: return NSLOCTEXT("NoFear", "TacticFlank", "Фланг");
	case ENoFearSquadTactic::Rush: return NSLOCTEXT("NoFear", "TacticRush", "Натиск");
	case ENoFearSquadTactic::Defend: return NSLOCTEXT("NoFear", "TacticDefend", "Оборона");
	case ENoFearSquadTactic::Fallback: return NSLOCTEXT("NoFear", "TacticFallback", "Отход");
	case ENoFearSquadTactic::Regroup: return NSLOCTEXT("NoFear", "TacticRegroup", "Сбор");
	default: return FText::GetEmpty();
	}
}

FString UNoFearSquadSubsystem::GetSquadDebugText(const APawn* Pawn) const
{
	const FSquad* Squad = FindSquadOf(Pawn);
	const UWorld* World = GetWorld();
	if (!Squad || !World)
	{
		return FString();
	}
	const double Now = World->GetTimeSeconds();
	FString Text = FString::Printf(TEXT("%s %s"), *Squad->Name.ToString(), *GetTacticText(Squad->Tactic).ToString());
	if (!Squad->TacticReason.IsEmpty())
	{
		Text += FString::Printf(TEXT("(%s)"), *Squad->TacticReason);
	}
	Text += FString::Printf(TEXT(" %.0fs eng=%d al=%d"), Now - Squad->TacticStartTime, Squad->bEngaged, Squad->bAlerted);
	if (const AActor* Enemy = Squad->MainEnemy.Get())
	{
		Text += FString::Printf(TEXT(" main=%s seen=%.1f dug=%.1f d=%.0f"), *Enemy->GetName(), Now - Squad->MainEnemyLastSeen,
			Now - Squad->DugInSince, FVector::Dist(GetCentroid(*Squad), Squad->MainEnemyLocation));
	}
	Text += FString::Printf(TEXT(" flank_in=%.1f gren_in=%.1f rush_in=%.1f"), Squad->NextFlankTime - Now, Squad->NextGrenadeTime - Now,
		Squad->NextRushTime - Now);
	if (Squad->Tactic == ENoFearSquadTactic::Bounding || Squad->Tactic == ENoFearSquadTactic::Fallback)
	{
		Text += FString::Printf(TEXT(" swaps=%d moving=%s stuck=%d"), Squad->Swaps, Squad->bTeamBMoving ? TEXT("B") : TEXT("A"), Squad->StuckPhases);
	}
	if (Squad->Tactic == ENoFearSquadTactic::Fallback || Squad->bHoldingLine)
	{
		Text += FString::Printf(TEXT(" anchor=%.0f%s"), FVector::Dist2D(GetCentroid(*Squad), Squad->FallbackAnchor), Squad->bHoldingLine ? TEXT(" line") : TEXT(""));
	}
	if (const APawn* Flanker = Squad->Flanker.Get())
	{
		Text += FString::Printf(TEXT(" flanker=%s"), *Flanker->GetName());
	}
	return Text;
}
