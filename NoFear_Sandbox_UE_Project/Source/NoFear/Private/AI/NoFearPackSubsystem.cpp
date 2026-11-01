#include "AI/NoFearPackSubsystem.h"
#include "Core/NoFearAISettings.h"
#include "Core/NoFearPerf.h"

#include "AI/NoFearBotSubsystem.h"
#include "AI/NoFearMonsterAIController.h"
#include "Core/NoFearCharacter.h"
#include "Core/NoFearHealthComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "NavigationSystem.h"

namespace
{
	/** Как далеко от точки искать навмеш под место в строю или в кольце, см. */
	const FVector GoalProjectExtent(250.f, 250.f, 400.f);

	/** Клин за вожаком: на каждый ряд — шаг назад и шире в стороны, см. */
	constexpr float WedgeRowBack = 220.f;
	constexpr float WedgeSideBase = 180.f;
	constexpr float WedgeSideStep = 60.f;
	/** Места в клине чуть гуляют, чтобы строй не выглядел расчерченным, см; раз в столько секунд. */
	constexpr float WedgeJitter = 100.f;
	constexpr double WedgeJitterPeriod = 5.0;

	/** Цепь при сборе: на шаг впереди вожака, крайние — чуть позади, см. */
	constexpr float GatherLead = 150.f;
	constexpr float GatherStepBack = 80.f;

	/** Места в строю ставятся с упреждением по ходу вожака, сек. */
	constexpr float LeaderLeadTime = 0.4f;
	/** Медленнее этого вожак стоит: строй не разворачивается, см/с. */
	constexpr float HeadingMinSpeed = 50.f;

	/** Разбегаются в пределах ±этого угла от направления «прочь от угрозы». */
	constexpr float ScatterSpreadAngle = 60.f;

	bool IsAlive(const APawn* Pawn)
	{
		return Pawn && !UNoFearHealthComponent::IsActorDead(Pawn);
	}

	FName GetBodyPackName(const APawn* Pawn)
	{
		const ANoFearCharacter* Character = Cast<ANoFearCharacter>(Pawn);
		return Character ? Character->PackName : NAME_None;
	}

	/** Направление по углу в плоскости XY, градусы. */
	FVector AngleToDirection(float Degrees)
	{
		const float Radians = FMath::DegreesToRadians(Degrees);
		return FVector(FMath::Cos(Radians), FMath::Sin(Radians), 0.f);
	}

	float DirectionToAngle(const FVector& Direction)
	{
		return static_cast<float>(FMath::RadiansToDegrees(FMath::Atan2(Direction.Y, Direction.X)));
	}

	bool IsDebugEnabled()
	{
		const IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(TEXT("NoFear.AI.Debug"));
		return CVar && CVar->GetInt() > 0;
	}
}

UNoFearPackSubsystem* UNoFearPackSubsystem::Get(const UObject* WorldContextObject)
{
	const UWorld* World = WorldContextObject ? WorldContextObject->GetWorld() : nullptr;
	return World ? World->GetSubsystem<UNoFearPackSubsystem>() : nullptr;
}

bool UNoFearPackSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

TStatId UNoFearPackSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UNoFearPackSubsystem, STATGROUP_Tickables);
}

void UNoFearPackSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	static_cast<FNoFearPackTuning&>(*this) = UNoFearAISettings::Get()->Pack;
}

void UNoFearPackSubsystem::Tick(float DeltaTime)
{
	NOFEAR_PERF_SCOPE(Pack);
	Super::Tick(DeltaTime);
	TimeSinceUpdate += DeltaTime;
	if (TimeSinceUpdate >= UpdateInterval)
	{
		TimeSinceUpdate = 0.f;
		UpdatePacks();
	}
}

void UNoFearPackSubsystem::UpdatePacks()
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	const double Now = World->GetTimeSeconds();

	UpdateMembership(Now);
	// Индекс нужен уже во время обновления: рык нового вожака спрашивает состав стаи.
	RebuildIndex();
	for (FPack& Pack : Packs)
	{
		UpdatePack(Pack, Now);
	}
	DrawDebug();
}

// --- Состав ---

void UNoFearPackSubsystem::UpdateMembership(double Now)
{
	const UNoFearBotSubsystem* BotSubsystem = UNoFearBotSubsystem::Get(this);
	if (!BotSubsystem)
	{
		return;
	}

	for (FPack& Pack : Packs)
	{
		// Погиб вожак — стая разбегается (кто остался). Вожак, просто ушедший из стаи, — не повод.
		const int32 OldCount = Pack.Members.Num();
		const bool bLeaderDied = !Pack.LeaderPawn.IsExplicitlyNull() && !IsAlive(Pack.LeaderPawn.Get());

		// Выбывшие: погибли, ушли из-под контроллера, стали агрессивными, сменили имя стаи или команду.
		Pack.Members.RemoveAll([&Pack](const FMember& Member)
		{
			const ANoFearMonsterAIController* Bot = Member.Bot.Get();
			const APawn* Pawn = Member.Pawn.Get();
			return !Bot || !IsAlive(Pawn) || Bot->GetPawn() != Pawn || Bot->IsAggro()
				|| GetBodyPackName(Pawn) != Pack.Name || UNoFearTeamLibrary::GetTeam(Pawn) != Pack.Team;
		});

		// Безымянный, отставший от спокойного вожака дальше LeaveDistance, уходит: найдёт стаю ближе или будет один.
		const APawn* LeaderPawn = Pack.LeaderPawn.Get();
		if (Pack.Name.IsNone() && Pack.State == ENoFearPackState::Roam && IsAlive(LeaderPawn))
		{
			const FVector LeaderLocation = LeaderPawn->GetActorLocation();
			const float LeaveSquared = FMath::Square(LeaveDistance);
			Pack.Members.RemoveAll([LeaderPawn, &LeaderLocation, LeaveSquared](const FMember& Member)
			{
				return Member.Pawn.Get() != LeaderPawn && FVector::DistSquared2D(Member.Pawn->GetActorLocation(), LeaderLocation) > LeaveSquared;
			});
		}

		if (bLeaderDied && OldCount >= 2 && !Pack.Members.IsEmpty())
		{
			Pack.LeaderPawn.Reset();
			Pack.Leader.Reset();
			BeginScatter(Pack, Pack.Target.IsValid() ? Pack.TargetLocation : Pack.LeaderLocation, Now);
		}
	}
	Packs.RemoveAll([](const FPack& Pack) { return Pack.Members.IsEmpty(); });

	// Новички — в порядке регистрации ботов.
	TSet<const ANoFearMonsterAIController*> Assigned;
	for (const FPack& Pack : Packs)
	{
		for (const FMember& Member : Pack.Members)
		{
			Assigned.Add(Member.Bot.Get());
		}
	}
	for (ANoFearAIController* AnyBot : BotSubsystem->GetBots())
	{
		ANoFearMonsterAIController* Bot = Cast<ANoFearMonsterAIController>(AnyBot);
		APawn* Pawn = Bot ? Bot->GetPawn() : nullptr;
		if (!IsAlive(Pawn) || Bot->IsAggro() || Assigned.Contains(Bot))
		{
			continue;
		}
		const FName Name = GetBodyPackName(Pawn);
		const ENoFearTeam Team = UNoFearTeamLibrary::GetTeam(Pawn);
		const FVector Location = Pawn->GetActorLocation();

		FPack* Best = nullptr;
		float BestDistanceSquared = FMath::Square(PackFormRadius);
		for (FPack& Pack : Packs)
		{
			if (Pack.Name != Name || Pack.Team != Team || Pack.Members.Num() >= MaxPackSize)
			{
				continue;
			}
			if (!Name.IsNone())
			{
				// Именованная стая — где бы ни стояли.
				Best = &Pack;
				break;
			}
			if (Pack.State == ENoFearPackState::Scatter)
			{
				continue;
			}
			for (const FMember& Member : Pack.Members)
			{
				const float DistanceSquared = FVector::DistSquared2D(Member.Pawn->GetActorLocation(), Location);
				if (DistanceSquared <= BestDistanceSquared)
				{
					BestDistanceSquared = DistanceSquared;
					Best = &Pack;
				}
			}
		}
		if (!Best)
		{
			Best = &Packs.AddDefaulted_GetRef();
			Best->Id = NextPackId++;
			Best->Name = Name;
			Best->Team = Team;
			Best->StateStartTime = Now;
			Best->LeaderLocation = Location;
			Best->Heading = Pawn->GetActorForwardVector().GetSafeNormal2D();
		}
		FMember& Member = Best->Members.AddDefaulted_GetRef();
		Member.Bot = Bot;
		Member.Pawn = Pawn;
		Assigned.Add(Bot);
	}

	// Безымянные стаи рядом сливаются: обе спокойны или гонят одну цель, и вместе не больше MaxPackSize.
	const float FormSquared = FMath::Square(PackFormRadius);
	for (int32 A = 0; A < Packs.Num(); ++A)
	{
		for (int32 B = Packs.Num() - 1; B > A; --B)
		{
			FPack& Into = Packs[A];
			FPack& From = Packs[B];
			if (!Into.Name.IsNone() || !From.Name.IsNone() || Into.Team != From.Team
				|| Into.State == ENoFearPackState::Scatter || From.State == ENoFearPackState::Scatter
				|| Into.Members.Num() + From.Members.Num() > MaxPackSize)
			{
				continue;
			}
			const bool bBothCalm = !Into.Target.IsValid() && !From.Target.IsValid();
			const bool bSameTarget = Into.Target.IsValid() && Into.Target == From.Target;
			if (!bBothCalm && !bSameTarget)
			{
				continue;
			}
			bool bClose = false;
			for (const FMember& First : Into.Members)
			{
				for (const FMember& Second : From.Members)
				{
					bClose |= FVector::DistSquared2D(First.Pawn->GetActorLocation(), Second.Pawn->GetActorLocation()) <= FormSquared;
				}
			}
			if (bClose)
			{
				Into.Members.Append(From.Members);
				Into.SlotCount = 0;
				Packs.RemoveAt(B);
			}
		}
	}

	// Вожак — старейший (появился раньше всех): у всех стай одно правило, вожак не прыгает.
	for (FPack& Pack : Packs)
	{
		const FMember* Oldest = nullptr;
		for (const FMember& Member : Pack.Members)
		{
			if (!Oldest || Member.Pawn->GetUniqueID() < Oldest->Pawn->GetUniqueID())
			{
				Oldest = &Member;
			}
		}
		Pack.Leader = Oldest->Bot;
		Pack.LeaderPawn = Oldest->Pawn;
	}
}

// --- Решения ---

void UNoFearPackSubsystem::UpdatePack(FPack& Pack, double Now)
{
	APawn* LeaderPawn = Pack.LeaderPawn.Get();
	if (!LeaderPawn)
	{
		return;
	}
	Pack.LeaderLocation = LeaderPawn->GetActorLocation();
	const FVector LeaderVelocity = LeaderPawn->GetVelocity();
	if (LeaderVelocity.Size2D() > HeadingMinSpeed)
	{
		Pack.Heading = LeaderVelocity.GetSafeNormal2D();
	}

	if (Pack.State == ENoFearPackState::Scatter)
	{
		if (Now < Pack.ScatterUntil)
		{
			ComputeGoals(Pack, Now);
			return;
		}
		// Разбежались — новый вожак рыком собирает стаю.
		SetState(Pack, ENoFearPackState::Roam, Now);
		if (ANoFearMonsterAIController* Leader = Pack.Leader.Get(); Leader && Pack.Members.Num() >= 2)
		{
			Leader->Roar();
		}
	}

	PickTarget(Pack, Now);
	ShareIntel(Pack, Now);

	const AActor* Target = Pack.Target.Get();
	if (!Target)
	{
		SetState(Pack, ENoFearPackState::Roam, Now);
	}
	else
	{
		const FVector Center = Pack.TargetLocation;
		const float LeaderDistance = FVector::Dist2D(Pack.LeaderLocation, Center);
		bool bAnyInContact = false;
		bool bAllFar = true;
		int32 RingCount = 0;
		for (const FMember& Member : Pack.Members)
		{
			const float Distance = FVector::Dist2D(Member.Pawn->GetActorLocation(), Center);
			bAnyInContact |= Distance <= ContactDistance;
			bAllFar &= Distance > DisengageDistance;
			RingCount += IsRingMember(Pack, Member) ? 1 : 0;
		}

		switch (Pack.State)
		{
		case ENoFearPackState::Roam:
			SetState(Pack, ENoFearPackState::Gather, Now);
			break;

		case ENoFearPackState::Gather:
			// Окружают только видимую цель: вожак подвёл стаю или кто-то уже сцепился с ней.
			if (Pack.bTargetSeen && (LeaderDistance <= EngageDistance || bAnyInContact))
			{
				SetState(Pack, RingCount <= 1 ? ENoFearPackState::Attack : ENoFearPackState::Encircle, Now);
			}
			break;

		case ENoFearPackState::Encircle:
			if (bAllFar)
			{
				SetState(Pack, ENoFearPackState::Gather, Now);
				break;
			}
			if (NeedsSlots(Pack))
			{
				AssignSlots(Pack);
			}
			{
				int32 Reached = 0;
				for (const FMember& Member : Pack.Members)
				{
					if (Member.Slot != INDEX_NONE
						&& FVector::DistSquared2D(Member.Pawn->GetActorLocation(), GetSlotPoint(Pack, Member)) <= FMath::Square(SlotReachedDistance))
					{
						++Reached;
					}
				}
				const int32 Needed = FMath::Max(1, FMath::CeilToInt(EncircleReadyFraction * Pack.SlotCount));
				if (Reached >= Needed || Now - Pack.StateStartTime >= EncircleMaxTime)
				{
					SetState(Pack, ENoFearPackState::Attack, Now);
				}
			}
			break;

		case ENoFearPackState::Attack:
			if (bAllFar)
			{
				SetState(Pack, ENoFearPackState::Gather, Now);
				break;
			}
			if (NeedsSlots(Pack))
			{
				AssignSlots(Pack);
			}
			break;

		default:
			break;
		}
	}

	UpdateWaves(Pack, Now);
	ComputeGoals(Pack, Now);
}

void UNoFearPackSubsystem::SetState(FPack& Pack, ENoFearPackState NewState, double Now)
{
	if (Pack.State == NewState)
	{
		return;
	}
	Pack.State = NewState;
	Pack.StateStartTime = Now;
	Pack.HoldStartTime = -1.0;

	for (FMember& Member : Pack.Members)
	{
		Member.bAttackTurn = false;
		Member.bAttackedThisWave = false;
		if (NewState != ENoFearPackState::Scatter)
		{
			Member.bHasFleeGoal = false;
		}
	}

	if (NewState == ENoFearPackState::Encircle || NewState == ENoFearPackState::Attack)
	{
		AssignSlots(Pack);
		Pack.Wave = 0;
		Pack.WaveStartTime = Now;
	}
	else
	{
		Pack.SlotCount = 0;
		for (FMember& Member : Pack.Members)
		{
			Member.Slot = INDEX_NONE;
		}
	}
}

void UNoFearPackSubsystem::PickTarget(FPack& Pack, double Now)
{
	AActor* NewTarget = nullptr;

	// Цель вожака; у вожака нет — та, что чаще всего у остальных.
	if (const ANoFearMonsterAIController* Leader = Pack.Leader.Get())
	{
		AActor* LeaderTarget = Leader->GetCurrentTarget();
		// Упавшего раненым стая не выбирает: добивать его пойдёт разве что тот, в кого он стреляет.
		NewTarget = LeaderTarget && !UNoFearHealthComponent::IsActorIncapacitated(LeaderTarget) ? LeaderTarget : nullptr;
	}
	if (!NewTarget)
	{
		TMap<AActor*, int32> Votes;
		int32 BestVotes = 0;
		for (const FMember& Member : Pack.Members)
		{
			AActor* MemberTarget = Member.Bot.IsValid() ? Member.Bot->GetCurrentTarget() : nullptr;
			if (!MemberTarget || UNoFearHealthComponent::IsActorIncapacitated(MemberTarget))
			{
				continue;
			}
			const int32 Count = ++Votes.FindOrAdd(MemberTarget);
			if (Count > BestVotes)
			{
				BestVotes = Count;
				NewTarget = MemberTarget;
			}
		}
	}

	if (NewTarget != Pack.Target.Get())
	{
		Pack.Target = NewTarget;
		Pack.TargetLocation = NewTarget ? NewTarget->GetActorLocation() : Pack.TargetLocation;
		// Новая цель — кольцо и волны заново.
		Pack.SlotCount = 0;
		Pack.Wave = 0;
		Pack.WaveStartTime = Now;
		for (FMember& Member : Pack.Members)
		{
			Member.Slot = INDEX_NONE;
			Member.bAttackTurn = false;
			Member.bAttackedThisWave = false;
		}
	}
}

void UNoFearPackSubsystem::ShareIntel(FPack& Pack, double Now)
{
	Pack.bTargetSeen = false;
	AActor* Target = Pack.Target.Get();
	if (!Target)
	{
		return;
	}

	// Свежее знание — своё: видит сейчас или цель только что в него попала. Тревога от стаи (LastHeardTime) не в счёт,
	// иначе стая вечно подтверждала бы сама себя.
	bool bFresh = false;
	double LatestContact = -UE_BIG_NUMBER;
	FVector LatestLocation = Pack.TargetLocation;
	for (const FMember& Member : Pack.Members)
	{
		const FNoFearKnownActor* Knowledge = Member.Bot.IsValid() ? Member.Bot->GetKnownActors().Find(Target) : nullptr;
		if (!Knowledge)
		{
			continue;
		}
		Pack.bTargetSeen |= Knowledge->bVisible;
		bFresh |= Knowledge->bVisible || Now - Knowledge->LastAttackedTime <= PackIntelTime;
		const double Contact = FMath::Max3(Knowledge->LastSeenTime, Knowledge->LastAttackedTime, Knowledge->LastHeardTime);
		if (Contact > LatestContact)
		{
			LatestContact = Contact;
			LatestLocation = Knowledge->LastKnownLocation;
		}
	}

	if (!bFresh)
	{
		// Никто не видел давно: стая идёт туда, где цель засекли последней.
		Pack.TargetLocation = LatestLocation;
		return;
	}

	Pack.TargetLocation = Target->GetActorLocation();
	for (const FMember& Member : Pack.Members)
	{
		ANoFearMonsterAIController* Bot = Member.Bot.Get();
		const FNoFearKnownActor* Knowledge = Bot ? Bot->GetKnownActors().Find(Target) : nullptr;
		if (Bot && !(Knowledge && Knowledge->bVisible))
		{
			Bot->ReceivePackAlarm(Target, Pack.TargetLocation);
		}
	}
}

void UNoFearPackSubsystem::BeginScatter(FPack& Pack, const FVector& From, double Now)
{
	SetState(Pack, ENoFearPackState::Scatter, Now);
	Pack.ScatterUntil = Now + ScatterTime;

	UNavigationSystemV1* NavSystem = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
	for (FMember& Member : Pack.Members)
	{
		Member.bHasFleeGoal = false;
		const APawn* Pawn = Member.Pawn.Get();
		if (!Pawn)
		{
			continue;
		}
		const FVector Location = Pawn->GetActorLocation();
		FVector Away = (Location - From).GetSafeNormal2D();
		if (Away.IsNearlyZero())
		{
			Away = AngleToDirection(FMath::FRandRange(-180.f, 180.f));
		}

		// Прочь от угрозы с разбросом; не вышло — ближе; совсем не вышло — куда угодно в пределах ScatterMaxDistance.
		for (int32 Attempt = 0; Attempt < 4 && !Member.bHasFleeGoal; ++Attempt)
		{
			const float Angle = FMath::FRandRange(-ScatterSpreadAngle, ScatterSpreadAngle);
			const float Distance = FMath::FRandRange(ScatterMinDistance, ScatterMaxDistance) * (Attempt >= 2 ? 0.5f : 1.f);
			const FVector Point = Location + Away.RotateAngleAxis(Angle, FVector::UpVector) * Distance;
			Member.bHasFleeGoal = ProjectGoal(Point, Member.FleeGoal);
		}
		FNavLocation Random;
		if (!Member.bHasFleeGoal && NavSystem && NavSystem->GetRandomReachablePointInRadius(Location, ScatterMaxDistance, Random))
		{
			Member.FleeGoal = Random.Location;
			Member.bHasFleeGoal = true;
		}
	}
}

// --- Кольцо и волны ---

bool UNoFearPackSubsystem::IsRingMember(const FPack& Pack, const FMember& Member) const
{
	const ANoFearMonsterAIController* Bot = Member.Bot.Get();
	if (!Bot || !Bot->bUseAttackTokens)
	{
		// Плевун бьёт издалека и в кольцо не встаёт.
		return false;
	}
	const AActor* BotTarget = Bot->GetCurrentTarget();
	return !BotTarget || BotTarget == Pack.Target.Get();
}

bool UNoFearPackSubsystem::NeedsSlots(const FPack& Pack) const
{
	int32 RingCount = 0;
	for (const FMember& Member : Pack.Members)
	{
		const bool bRing = IsRingMember(Pack, Member);
		if (bRing != (Member.Slot != INDEX_NONE))
		{
			return true;
		}
		RingCount += bRing ? 1 : 0;
	}
	return RingCount != Pack.SlotCount;
}

void UNoFearPackSubsystem::AssignSlots(FPack& Pack)
{
	TArray<int32> Ring;
	FVector Centroid = FVector::ZeroVector;
	for (int32 MemberIndex = 0; MemberIndex < Pack.Members.Num(); ++MemberIndex)
	{
		FMember& Member = Pack.Members[MemberIndex];
		Member.Slot = INDEX_NONE;
		if (IsRingMember(Pack, Member))
		{
			Ring.Add(MemberIndex);
			Centroid += Member.Pawn->GetActorLocation();
		}
	}
	Pack.SlotCount = Ring.Num();
	if (Ring.IsEmpty())
	{
		Pack.WaveCount = 1;
		return;
	}

	// Место 0 смотрит туда, откуда пришла стая; остальные — через равные углы по кругу.
	const FVector Center = Pack.TargetLocation;
	Centroid /= Ring.Num();
	const FVector ToPack = (Centroid - Center).GetSafeNormal2D();
	Pack.SlotBaseAngle = ToPack.IsNearlyZero() ? 0.f : DirectionToAngle(ToPack);
	const float Step = 360.f / Ring.Num();

	// Монстры по их углу от цели (от места 0, по кругу) — так соседи получают соседние места и никто не бежит через цель.
	struct FBearing
	{
		int32 MemberIndex;
		float Angle;
	};
	TArray<FBearing> Bearings;
	for (const int32 MemberIndex : Ring)
	{
		const FVector ToMember = Pack.Members[MemberIndex].Pawn->GetActorLocation() - Center;
		Bearings.Add({ MemberIndex, static_cast<float>(FRotator::ClampAxis(DirectionToAngle(ToMember) - Pack.SlotBaseAngle)) });
	}
	Bearings.Sort([](const FBearing& A, const FBearing& B) { return A.Angle < B.Angle; });

	// Из поворотов «по порядку» — тот, где в сумме идти меньше всего.
	int32 BestShift = 0;
	float BestCost = UE_BIG_NUMBER;
	for (int32 Shift = 0; Shift < Bearings.Num(); ++Shift)
	{
		float Cost = 0.f;
		for (int32 Rank = 0; Rank < Bearings.Num(); ++Rank)
		{
			const float SlotAngle = ((Rank + Shift) % Bearings.Num()) * Step;
			Cost += FMath::Abs(FMath::FindDeltaAngleDegrees(Bearings[Rank].Angle, SlotAngle));
		}
		if (Cost < BestCost)
		{
			BestCost = Cost;
			BestShift = Shift;
		}
	}
	for (int32 Rank = 0; Rank < Bearings.Num(); ++Rank)
	{
		Pack.Members[Bearings[Rank].MemberIndex].Slot = (Rank + BestShift) % Bearings.Num();
	}

	// Волны по 2 (с пятерых — по 3); места одной волны разнесены по кругу: бьют с разных сторон.
	const int32 WaveSize = Ring.Num() >= 5 ? 3 : 2;
	Pack.WaveCount = FMath::Max(1, FMath::DivideAndRoundUp(Ring.Num(), WaveSize));
	Pack.Wave %= Pack.WaveCount;
}

void UNoFearPackSubsystem::UpdateWaves(FPack& Pack, double Now)
{
	for (FMember& Member : Pack.Members)
	{
		Member.bAttackTurn = false;
	}
	if (Pack.State != ENoFearPackState::Attack || Pack.WaveCount <= 0)
	{
		return;
	}

	// Волна отработала (все ударили) или застряла — очередь следующей.
	bool bAnyInWave = false;
	bool bAllDone = true;
	for (const FMember& Member : Pack.Members)
	{
		if (Member.Slot != INDEX_NONE && Member.Slot % Pack.WaveCount == Pack.Wave)
		{
			bAnyInWave = true;
			bAllDone &= Member.bAttackedThisWave;
		}
	}
	if (!bAnyInWave || bAllDone || Now - Pack.WaveStartTime >= WaveMaxTime)
	{
		Pack.Wave = (Pack.Wave + 1) % Pack.WaveCount;
		Pack.WaveStartTime = Now;
		for (FMember& Member : Pack.Members)
		{
			if (Member.Slot != INDEX_NONE && Member.Slot % Pack.WaveCount == Pack.Wave)
			{
				Member.bAttackedThisWave = false;
			}
		}
	}

	for (FMember& Member : Pack.Members)
	{
		Member.bAttackTurn = Member.Slot != INDEX_NONE && Member.Slot % Pack.WaveCount == Pack.Wave && !Member.bAttackedThisWave;
	}
}

void UNoFearPackSubsystem::NotifyAttackFinished(const ANoFearMonsterAIController* Bot)
{
	FMember* Member = nullptr;
	if (FindPack(Bot, &Member) && Member)
	{
		Member->bAttackedThisWave = true;
		Member->bAttackTurn = false;
	}
}

FVector UNoFearPackSubsystem::GetSlotPoint(const FPack& Pack, const FMember& Member) const
{
	const ANoFearMonsterAIController* Bot = Member.Bot.Get();
	const float Radius = Bot ? Bot->GetRingRadius() : 500.f;
	const float Step = 360.f / FMath::Max(Pack.SlotCount, 1);
	return Pack.TargetLocation + AngleToDirection(Pack.SlotBaseAngle + Member.Slot * Step) * Radius;
}

// --- Точки ---

void UNoFearPackSubsystem::ComputeGoals(FPack& Pack, double Now)
{
	for (FMember& Member : Pack.Members)
	{
		Member.bHasGoal = false;
	}

	if (Pack.State == ENoFearPackState::Scatter)
	{
		for (FMember& Member : Pack.Members)
		{
			Member.bHasGoal = Member.bHasFleeGoal;
			Member.Goal = Member.FleeGoal;
		}
		return;
	}

	const APawn* LeaderPawn = Pack.LeaderPawn.Get();
	if (!LeaderPawn)
	{
		return;
	}
	const FVector LeaderLocation = Pack.LeaderLocation;
	const FVector Anchor = LeaderLocation + LeaderPawn->GetVelocity().GetSafeNormal2D() * FMath::Min(LeaderPawn->GetVelocity().Size2D() * LeaderLeadTime, 300.0);

	// Ждать отставших: кто-то дальше StragglerDistance — вожак стоит, но не дольше MaxHoldTime за раз.
	bool bStragglers = false;
	for (const FMember& Member : Pack.Members)
	{
		bStragglers |= Member.Pawn.Get() != LeaderPawn
			&& FVector::DistSquared2D(Member.Pawn->GetActorLocation(), LeaderLocation) > FMath::Square(StragglerDistance);
	}
	const bool bTargetClose = Pack.Target.IsValid() && FVector::Dist2D(LeaderLocation, Pack.TargetLocation) <= EngageDistance;
	bool bHold = false;
	if (bStragglers && !bTargetClose && (Pack.State == ENoFearPackState::Roam || Pack.State == ENoFearPackState::Gather))
	{
		if (Pack.HoldStartTime < 0.0)
		{
			Pack.HoldStartTime = Now;
		}
		bHold = Now - Pack.HoldStartTime <= MaxHoldTime;
	}
	else if (!bStragglers)
	{
		Pack.HoldStartTime = -1.0;
	}

	if (Pack.State == ENoFearPackState::Roam || Pack.State == ENoFearPackState::Gather)
	{
		const bool bGather = Pack.State == ENoFearPackState::Gather;
		// Спокойно — клином за вожаком по его ходу; на сборе — цепью поперёк пути к цели.
		FVector Forward = bGather ? (Pack.TargetLocation - LeaderLocation).GetSafeNormal2D() : Pack.Heading;
		if (Forward.IsNearlyZero())
		{
			Forward = Pack.Heading;
		}
		const FVector Right(-Forward.Y, Forward.X, 0.f);

		int32 Rank = 0;
		for (FMember& Member : Pack.Members)
		{
			if (Member.Pawn.Get() == LeaderPawn)
			{
				if (bHold)
				{
					Member.bHasGoal = ProjectGoal(LeaderLocation, Member.Goal);
				}
				else if (bGather)
				{
					Member.bHasGoal = ProjectGoal(Pack.TargetLocation, Member.Goal);
				}
				// Спокойный вожак без ожидания точки не получает: бродит сам (WanderCenter).
				continue;
			}

			const int32 Row = Rank / 2 + 1;
			const float Side = Rank % 2 == 0 ? -1.f : 1.f;
			++Rank;

			FVector Point;
			if (bGather)
			{
				Point = Anchor + Forward * (GatherLead - GatherStepBack * Row) + Right * Side * Row * GatherSpacing;
			}
			else
			{
				Point = Anchor - Forward * WedgeRowBack * Row + Right * Side * (WedgeSideBase + WedgeSideStep * Row);
				FRandomStream Jitter(HashCombine(Member.Pawn->GetUniqueID(), static_cast<uint32>(Now / WedgeJitterPeriod)));
				Point += FVector(Jitter.FRandRange(-WedgeJitter, WedgeJitter), Jitter.FRandRange(-WedgeJitter, WedgeJitter), 0.f);
			}
			Member.bHasGoal = ProjectGoal(Point, Member.Goal);
		}
		return;
	}

	// Окружение и атака: каждый — на своё место в кольце; место дальше FlankStepAngle — в обход, по дуге снаружи.
	const FVector Center = Pack.TargetLocation;
	const float Step = 360.f / FMath::Max(Pack.SlotCount, 1);
	for (FMember& Member : Pack.Members)
	{
		const ANoFearMonsterAIController* Bot = Member.Bot.Get();
		if (!Bot || Member.Slot == INDEX_NONE)
		{
			continue;
		}
		const float Radius = Bot->GetRingRadius();
		const FVector ToMember = Member.Pawn->GetActorLocation() - Center;
		const float MemberAngle = DirectionToAngle(ToMember);
		const float SlotAngle = Pack.SlotBaseAngle + Member.Slot * Step;
		const float Delta = FMath::FindDeltaAngleDegrees(MemberAngle, SlotAngle);

		FVector Point = GetSlotPoint(Pack, Member);
		if (FMath::Abs(Delta) > FlankStepAngle)
		{
			const float FlankRadius = FMath::Clamp(static_cast<float>(ToMember.Size2D()), Radius * 1.5f, Radius * 2.f);
			Point = Center + AngleToDirection(MemberAngle + FMath::Sign(Delta) * FlankStepAngle) * FlankRadius;
			Member.bHasGoal = ProjectGoal(Point, Member.Goal);
			if (Member.bHasGoal)
			{
				continue;
			}
			Point = GetSlotPoint(Pack, Member);
		}
		Member.bHasGoal = ProjectGoal(Point, Member.Goal);
	}
}

bool UNoFearPackSubsystem::ProjectGoal(const FVector& Point, FVector& OutPoint) const
{
	const UNavigationSystemV1* NavSystem = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
	FNavLocation Projected;
	if (!NavSystem || !NavSystem->ProjectPointToNavigation(Point, Projected, GoalProjectExtent))
	{
		return false;
	}
	OutPoint = Projected.Location;
	return true;
}

// --- Индекс и запросы ---

void UNoFearPackSubsystem::RebuildIndex()
{
	Index.Reset();
	for (int32 PackIndex = 0; PackIndex < Packs.Num(); ++PackIndex)
	{
		const FPack& Pack = Packs[PackIndex];
		for (int32 MemberIndex = 0; MemberIndex < Pack.Members.Num(); ++MemberIndex)
		{
			const FMember& Member = Pack.Members[MemberIndex];
			if (const UObject* Bot = Member.Bot.Get())
			{
				Index.Add(Bot, FIntPoint(PackIndex, MemberIndex));
			}
			if (const UObject* Pawn = Member.Pawn.Get())
			{
				Index.Add(Pawn, FIntPoint(PackIndex, MemberIndex));
			}
		}
	}
}

const UNoFearPackSubsystem::FPack* UNoFearPackSubsystem::FindPack(const UObject* Key, const FMember** OutMember) const
{
	const FIntPoint* Entry = Key ? Index.Find(Key) : nullptr;
	if (!Entry || !Packs.IsValidIndex(Entry->X) || !Packs[Entry->X].Members.IsValidIndex(Entry->Y))
	{
		return nullptr;
	}
	const FPack& Pack = Packs[Entry->X];
	const FMember& Member = Pack.Members[Entry->Y];
	// Индекс старше состава на одно обновление: проверить, что место всё ещё его.
	if (static_cast<const UObject*>(Member.Bot.Get()) != Key && static_cast<const UObject*>(Member.Pawn.Get()) != Key)
	{
		return nullptr;
	}
	if (OutMember)
	{
		*OutMember = &Member;
	}
	return &Pack;
}

UNoFearPackSubsystem::FPack* UNoFearPackSubsystem::FindPack(const UObject* Key, FMember** OutMember)
{
	const FMember* Member = nullptr;
	FPack* Pack = const_cast<FPack*>(static_cast<const UNoFearPackSubsystem*>(this)->FindPack(Key, &Member));
	if (OutMember)
	{
		*OutMember = const_cast<FMember*>(Member);
	}
	return Pack;
}

bool UNoFearPackSubsystem::GetMemberView(const ANoFearMonsterAIController* Bot, FNoFearPackMemberView& OutView) const
{
	const FMember* Member = nullptr;
	const FPack* Pack = FindPack(Bot, &Member);
	if (!Pack || !Member)
	{
		return false;
	}
	OutView.State = Pack->State;
	OutView.Leader = Pack->Leader.Get();
	OutView.PackSize = Pack->Members.Num();
	OutView.Target = Pack->Target.Get();
	OutView.bHasGoal = Member->bHasGoal;
	OutView.Goal = Member->Goal;
	OutView.bAttackTurn = Member->bAttackTurn;
	OutView.Slot = Member->Slot;
	return true;
}

ENoFearPackState UNoFearPackSubsystem::GetPackState(const APawn* Pawn) const
{
	const FPack* Pack = FindPack(Pawn);
	return Pack ? Pack->State : ENoFearPackState::Roam;
}

APawn* UNoFearPackSubsystem::GetPackLeader(const APawn* Pawn) const
{
	const FPack* Pack = FindPack(Pawn);
	return Pack ? Pack->LeaderPawn.Get() : nullptr;
}

TArray<APawn*> UNoFearPackSubsystem::GetPackMembers(const APawn* Pawn) const
{
	TArray<APawn*> Result;
	if (const FPack* Pack = FindPack(Pawn))
	{
		for (const FMember& Member : Pack->Members)
		{
			if (APawn* MemberPawn = Member.Pawn.Get(); IsAlive(MemberPawn))
			{
				Result.Add(MemberPawn);
			}
		}
	}
	return Result;
}

AActor* UNoFearPackSubsystem::GetPackTarget(const APawn* Pawn) const
{
	const FPack* Pack = FindPack(Pawn);
	return Pack ? Pack->Target.Get() : nullptr;
}

bool UNoFearPackSubsystem::GetPackGoal(const APawn* Pawn, FVector& OutGoal) const
{
	const FMember* Member = nullptr;
	if (!FindPack(Pawn, &Member) || !Member || !Member->bHasGoal)
	{
		return false;
	}
	OutGoal = Member->Goal;
	return true;
}

int32 UNoFearPackSubsystem::GetPackSlot(const APawn* Pawn) const
{
	const FMember* Member = nullptr;
	return FindPack(Pawn, &Member) && Member ? Member->Slot : INDEX_NONE;
}

void UNoFearPackSubsystem::GetPackWave(const APawn* Pawn, int32& OutWave, int32& OutWaveCount) const
{
	const FPack* Pack = FindPack(Pawn);
	OutWave = Pack ? Pack->Wave : 0;
	OutWaveCount = Pack ? Pack->WaveCount : 0;
}

bool UNoFearPackSubsystem::IsAttackTurn(const APawn* Pawn) const
{
	const FMember* Member = nullptr;
	return FindPack(Pawn, &Member) && Member && Member->bAttackTurn;
}

// --- Отладка ---

void UNoFearPackSubsystem::DrawDebug() const
{
#if ENABLE_DRAW_DEBUG
	UWorld* World = GetWorld();
	if (!World || !IsDebugEnabled())
	{
		return;
	}
	const float Duration = UpdateInterval + 0.05f;
	for (const FPack& Pack : Packs)
	{
		const APawn* LeaderPawn = Pack.LeaderPawn.Get();
		if (!LeaderPawn || Pack.Members.Num() < 2)
		{
			continue;
		}
		FColor Color = FColor::Green;
		switch (Pack.State)
		{
		case ENoFearPackState::Gather: Color = FColor::Yellow; break;
		case ENoFearPackState::Encircle: Color = FColor::Orange; break;
		case ENoFearPackState::Attack: Color = FColor::Red; break;
		case ENoFearPackState::Scatter: Color = FColor::Purple; break;
		default: break;
		}

		FString Text = FString::Printf(TEXT("Pack %d  %s  x%d"), Pack.Id, *StaticEnum<ENoFearPackState>()->GetNameStringByValue(static_cast<int64>(Pack.State)), Pack.Members.Num());
		if (Pack.State == ENoFearPackState::Attack)
		{
			Text += FString::Printf(TEXT("  wave %d/%d"), Pack.Wave + 1, Pack.WaveCount);
		}
		DrawDebugString(World, LeaderPawn->GetActorLocation() + FVector(0.f, 0.f, 150.f), Text, nullptr, Color, Duration, true);

		for (const FMember& Member : Pack.Members)
		{
			const APawn* Pawn = Member.Pawn.Get();
			if (!Pawn)
			{
				continue;
			}
			if (Member.bHasGoal)
			{
				DrawDebugLine(World, Pawn->GetActorLocation(), Member.Goal, Color, false, Duration, 0, 2.f);
				DrawDebugSphere(World, Member.Goal, 20.f, 6, Color, false, Duration);
			}
			if (Member.Slot != INDEX_NONE)
			{
				DrawDebugPoint(World, GetSlotPoint(Pack, Member) + FVector(0.f, 0.f, 20.f), 14.f, Member.bAttackTurn ? FColor::Red : FColor::White, false, Duration);
			}
		}
	}
#endif
}
