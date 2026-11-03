#include "Portal/NoFearPortalSubsystem.h"
#include "Core/NoFearAISettings.h"
#include "Core/NoFearPerf.h"

#include "AI/NoFearAIController.h"
#include "AI/NoFearBotSubsystem.h"
#include "AI/NoFearMonsterAIController.h"
#include "AI/NoFearSquadSubsystem.h"
#include "Core/NoFearBraceletComponent.h"
#include "Core/NoFearCharacter.h"
#include "Core/NoFearFearComponent.h"
#include "Core/NoFearFearSubsystem.h"
#include "Core/NoFearHealthComponent.h"
#include "Core/NoFearPlayerController.h"
#include "Core/NoFearTeam.h"
#include "DrawDebugHelpers.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "EnhancedInputSubsystems.h"
#include "HAL/IConsoleManager.h"
#include "NavigationSystem.h"
#include "Navigation/PathFollowingComponent.h"
#include "NoFear.h"
#include "Portal/NoFearFreeComponent.h"
#include "Portal/NoFearPortal.h"
#include "Weapons/NoFearWeaponComponent.h"

namespace
{
	TAutoConsoleVariable<int32> CVarPortalDebug(
		TEXT("NoFear.Portal.Debug"),
		0,
		TEXT("1 — порталы: аура и сердце, размер и ступень, проводники, слом и освобождение над людьми."),
		ECVF_Default);

	/** Как далеко от точки искать навмеш под разлом или монстра, см. */
	const FVector NavExtent(300.f, 300.f, 500.f);
	/** Монстр выходит на таком расстоянии от края провала, см. */
	constexpr float SpawnRingOffset = 80.f;
	/** Монстров выпускаем на такой высоте над навмешем (полвысоты капсулы с запасом), см. */
	constexpr float SpawnLift = 100.f;
	/** Освобождение спадает, если его не подпитывали дольше этого, сек. */
	constexpr double FreeHoldGrace = 0.35;
	/** Крик сломавшегося слышен на столько, см. */
	constexpr float BreakScreamRadius = 1500.f;
	/** Стражей назначаем не чаще, сек. */
	constexpr double RescueAssignInterval = 0.5;
	/** Путь к проводнику перезапрашиваем не чаще, сек. */
	constexpr double RescueRepathInterval = 1.0;
	/** Страж в досягаемости, пока ближе GuardianReach × это (чтобы не дёргался на границе). */
	constexpr float RescueReachHysteresis = 1.3f;
	/** Герой освобождает чуть дальше HeroFreeDistance, чтобы держание не рвалось от толчка, см. */
	constexpr float HeroFreeSlack = 60.f;
	/** Проводник шепчет раз в столько секунд (±30%). */
	constexpr float WhisperInterval = 7.f;
	/** Возвращающийся монстр перезапрашивает путь не чаще, сек. */
	constexpr double RecallRepathInterval = 1.0;
	/** Браслет героя чует открытие и рост портала на таком расстоянии, см. */
	constexpr float RiftSenseRadius = 6000.f;

	enum EMonsterSlot : int32
	{
		SlotMonster,
		SlotJumper,
		SlotSpitter,
		SlotTank,
		SlotCount
	};

	bool IsAlive(const APawn* Pawn)
	{
		return Pawn && !UNoFearHealthComponent::IsActorDead(Pawn);
	}

	ANoFearAIController* GetBot(const APawn* Pawn)
	{
		return Pawn ? Cast<ANoFearAIController>(Pawn->GetController()) : nullptr;
	}

	TWeakObjectPtr<APawn> PawnKey(const APawn* Pawn)
	{
		return TWeakObjectPtr<APawn>(const_cast<APawn*>(Pawn));
	}

	/** Браслеты героев рядом почуяли разлом: стрелка и сигнал (этап 12). */
	void ReportRiftToHeroes(const UWorld* World, const FVector& Location)
	{
		if (!World)
		{
			return;
		}
		for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
		{
			const ANoFearPlayerController* PlayerController = Cast<ANoFearPlayerController>(It->Get());
			const APawn* Hero = PlayerController ? PlayerController->GetPawn() : nullptr;
			UNoFearBraceletComponent* Bracelet = PlayerController ? PlayerController->GetBracelet() : nullptr;
			if (Bracelet && Hero && FVector::DistSquared(Hero->GetActorLocation(), Location) <= FMath::Square(RiftSenseRadius))
			{
				Bracelet->ReportRift(Location);
			}
		}
	}

	const FText& PickLine(const TArray<FText>& Lines)
	{
		return Lines[FMath::RandHelper(Lines.Num())];
	}

	const TArray<FText>& BreakLines()
	{
		static const TArray<FText> Lines = {
			NSLOCTEXT("NoFearPortal", "Break1", "Оно смотрит на меня..."),
			NSLOCTEXT("NoFearPortal", "Break2", "Я слышу... оно зовёт"),
			NSLOCTEXT("NoFearPortal", "Break3", "Не могу отвести глаз..."),
			NSLOCTEXT("NoFearPortal", "Break4", "Хватит! Пусть оно заберёт!"),
		};
		return Lines;
	}

	const TArray<FText>& WhisperLines()
	{
		static const TArray<FText> Lines = {
			NSLOCTEXT("NoFearPortal", "Whisper1", "...ещё... ещё..."),
			NSLOCTEXT("NoFearPortal", "Whisper2", "...оно растёт..."),
			NSLOCTEXT("NoFearPortal", "Whisper3", "...открой... открой..."),
			NSLOCTEXT("NoFearPortal", "Whisper4", "...не трогайте меня..."),
		};
		return Lines;
	}

	const TArray<FText>& WakeLines()
	{
		static const TArray<FText> Lines = {
			NSLOCTEXT("NoFearPortal", "Wake1", "Что... где я?!"),
			NSLOCTEXT("NoFearPortal", "Wake2", "Оно было у меня в голове!"),
			NSLOCTEXT("NoFearPortal", "Wake3", "Бежим отсюда!"),
		};
		return Lines;
	}

	const TArray<FText>& RescueLines()
	{
		static const TArray<FText> Lines = {
			NSLOCTEXT("NoFearPortal", "Rescue1", "Держись, я тебя вытащу!"),
			NSLOCTEXT("NoFearPortal", "Rescue2", "Эй! Смотри на меня!"),
			NSLOCTEXT("NoFearPortal", "Rescue3", "Прикройте, вытаскиваю его!"),
		};
		return Lines;
	}

	const TArray<FText>& RescuedLines()
	{
		static const TArray<FText> Lines = {
			NSLOCTEXT("NoFearPortal", "Rescued1", "Очнись! Уходим!"),
			NSLOCTEXT("NoFearPortal", "Rescued2", "Есть! Он с нами!"),
		};
		return Lines;
	}

	const TArray<FText>& CalmLines()
	{
		static const TArray<FText> Lines = {
			NSLOCTEXT("NoFearPortal", "Calm1", "Тихо, тихо, я рядом!"),
			NSLOCTEXT("NoFearPortal", "Calm2", "Не смотри туда! Смотри на меня!"),
			NSLOCTEXT("NoFearPortal", "Calm3", "Дыши. Мы тебя прикроем."),
		};
		return Lines;
	}

	const TArray<FText>& CalmedLines()
	{
		static const TArray<FText> Lines = {
			NSLOCTEXT("NoFearPortal", "Calmed1", "Вот так. Держись за нами."),
			NSLOCTEXT("NoFearPortal", "Calmed2", "Всё, он в порядке!"),
		};
		return Lines;
	}

	/** Опустить точку на землю (трасса вниз), если под ней что-то есть. */
	FVector SnapToGround(const UWorld* World, const FVector& Location, const AActor* Ignore)
	{
		FHitResult Hit;
		FCollisionQueryParams Params(SCENE_QUERY_STAT(NoFearPortalGround), false, Ignore);
		if (World && World->LineTraceSingleByChannel(Hit, Location + FVector(0.f, 0.f, 200.f), Location - FVector(0.f, 0.f, 500.f), ECC_Visibility, Params))
		{
			return Hit.ImpactPoint;
		}
		return Location;
	}

	/** Цель стража рядом: сначала бой, освобождение потом. */
	bool HasCombatTarget(const ANoFearAIController* Bot, float Distance)
	{
		const AActor* Target = Bot ? Bot->GetCurrentTarget() : nullptr;
		const APawn* Guardian = Bot ? Bot->GetPawn() : nullptr;
		return Target && Guardian && !UNoFearHealthComponent::IsActorDead(Target)
			&& FVector::Dist(Target->GetActorLocation(), Guardian->GetActorLocation()) <= Distance;
	}

	// --- Консоль ---

	APawn* GetPlayerPawn(const UWorld* World)
	{
		const APlayerController* PlayerController = World ? World->GetFirstPlayerController() : nullptr;
		return PlayerController ? PlayerController->GetPawn() : nullptr;
	}

	void BreakCommand(const TArray<FString>& Args, UWorld* World)
	{
		UNoFearPortalSubsystem* Portals = UNoFearPortalSubsystem::Get(World);
		const UNoFearBotSubsystem* Bots = UNoFearBotSubsystem::Get(World);
		const APawn* Player = GetPlayerPawn(World);
		if (!Portals || !Bots || !Player)
		{
			return;
		}
		// Без числа — ближайшего к игроку; с числом — всех в этом радиусе.
		const float Radius = Args.Num() > 0 ? FCString::Atof(*Args[0]) : 0.f;
		TArray<APawn*> Candidates;
		for (const ANoFearAIController* Bot : Bots->GetBots())
		{
			if (APawn* Pawn = Bot ? Bot->GetPawn() : nullptr)
			{
				Candidates.Add(Pawn);
			}
		}
		const FVector From = Player->GetActorLocation();
		Candidates.Sort([&From](const APawn& A, const APawn& B)
		{
			return FVector::DistSquared(A.GetActorLocation(), From) < FVector::DistSquared(B.GetActorLocation(), From);
		});
		for (APawn* Pawn : Candidates)
		{
			if (Radius > 0.f && FVector::Dist(Pawn->GetActorLocation(), From) > Radius)
			{
				break;
			}
			if (Portals->BreakPerson(Pawn) && Radius <= 0.f)
			{
				break;
			}
		}
	}

	void OpenCommand(const TArray<FString>& Args, UWorld* World)
	{
		UNoFearPortalSubsystem* Portals = UNoFearPortalSubsystem::Get(World);
		const APawn* Player = GetPlayerPawn(World);
		if (!Portals || !Player)
		{
			return;
		}
		const float Distance = Args.Num() > 0 ? FCString::Atof(*Args[0]) : 600.f;
		const float Size = Args.Num() > 1 ? FCString::Atof(*Args[1]) : 0.f;
		const FRotator View = Player->GetControlRotation();
		FVector Location = Player->GetActorLocation() + FRotator(0.f, View.Yaw, 0.f).Vector() * Distance;
		if (const UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World))
		{
			FNavLocation NavLocation;
			if (Nav->ProjectPointToNavigation(Location, NavLocation, NavExtent))
			{
				Location = NavLocation.Location;
			}
		}
		Portals->OpenPortal(SnapToGround(World, Location, Player), Size);
	}

	void CloseAllCommand(const TArray<FString>& Args, UWorld* World)
	{
		if (UNoFearPortalSubsystem* Portals = UNoFearPortalSubsystem::Get(World))
		{
			for (ANoFearPortal* Portal : Portals->GetPortals())
			{
				Portals->ClosePortal(Portal);
			}
		}
	}

	void SetSizeCommand(const TArray<FString>& Args, UWorld* World)
	{
		UNoFearPortalSubsystem* Portals = UNoFearPortalSubsystem::Get(World);
		const APawn* Player = GetPlayerPawn(World);
		if (Portals && Player && Args.Num() > 0)
		{
			Portals->SetPortalSize(Portals->FindNearestPortal(Player->GetActorLocation()), FCString::Atof(*Args[0]));
		}
	}

	void FreeAllCommand(const TArray<FString>& Args, UWorld* World)
	{
		if (UNoFearPortalSubsystem* Portals = UNoFearPortalSubsystem::Get(World))
		{
			for (ANoFearPortal* Portal : Portals->GetPortals())
			{
				for (APawn* Feeder : Portal->GetFeeders())
				{
					Portals->FreeFeeder(Feeder);
				}
			}
		}
	}

	FAutoConsoleCommandWithWorldAndArgs PortalBreakCommand(
		TEXT("NoFear.Portal.Break"),
		TEXT("Сломать ближайшего к игроку человека (с числом — всех в радиусе, см): транс и портал."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&BreakCommand));

	FAutoConsoleCommandWithWorldAndArgs PortalOpenCommand(
		TEXT("NoFear.Portal.Open"),
		TEXT("Открыть портал перед игроком: NoFear.Portal.Open [расстояние=600] [размер=StartSize]."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&OpenCommand));

	FAutoConsoleCommandWithWorldAndArgs PortalCloseAllCommand(
		TEXT("NoFear.Portal.CloseAll"),
		TEXT("Затянуть все порталы: проводники очнутся, монстры побегут обратно."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&CloseAllCommand));

	FAutoConsoleCommandWithWorldAndArgs PortalSetSizeCommand(
		TEXT("NoFear.Portal.SetSize"),
		TEXT("Размер ближайшего к игроку портала: NoFear.Portal.SetSize 50."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&SetSizeCommand));

	FAutoConsoleCommandWithWorldAndArgs PortalFreeAllCommand(
		TEXT("NoFear.Portal.FreeAll"),
		TEXT("Вывести из транса всех проводников."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&FreeAllCommand));
}

// --- Жизнь подсистемы ---

UNoFearPortalSubsystem* UNoFearPortalSubsystem::Get(const UObject* WorldContextObject)
{
	const UWorld* World = WorldContextObject ? WorldContextObject->GetWorld() : nullptr;
	return World ? World->GetSubsystem<UNoFearPortalSubsystem>() : nullptr;
}

void UNoFearPortalSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	static_cast<FNoFearPortalTuning&>(*this) = UNoFearAISettings::Get()->Portal;
}

void UNoFearPortalSubsystem::Deinitialize()
{
	BreakStates.Reset();
	Feeders.Reset();
	Rescues.Reset();
	RescueCooldowns.Reset();
	HeroFreeing.Reset();
	Portals.Reset();
	RecallMoveTimes.Reset();
	LoadedMonsterClasses.Reset();
	Super::Deinitialize();
}

bool UNoFearPortalSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

TStatId UNoFearPortalSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UNoFearPortalSubsystem, STATGROUP_Tickables);
}

void UNoFearPortalSubsystem::Tick(float DeltaTime)
{
	NOFEAR_PERF_SCOPE(Fear);
	Super::Tick(DeltaTime);
	TimeSinceUpdate += DeltaTime;
	if (TimeSinceUpdate >= UpdateInterval)
	{
		UpdatePortals(TimeSinceUpdate);
		TimeSinceUpdate = 0.f;
	}
	if (CVarPortalDebug.GetValueOnGameThread() > 0)
	{
		DrawDebug();
	}
}

void UNoFearPortalSubsystem::UpdatePortals(float DeltaTime)
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	const double Now = World->GetTimeSeconds();
	Portals.RemoveAll([](const TWeakObjectPtr<ANoFearPortal>& Portal) { return !Portal.IsValid(); });

	UpdateFeeders(DeltaTime, Now);
	UpdateBreaking(DeltaTime, Now);
	UpdateGrowth(DeltaTime, Now);
	UpdateAura(DeltaTime);
	UpdateMonsters(Now);
	UpdateClosing(Now);
	UpdateHeroFreeing(DeltaTime, Now);
	AssignRescuers(Now);
	UpdateRescues(DeltaTime, Now);
}

// --- Запросы ---

bool UNoFearPortalSubsystem::IsFeeder(const APawn* Pawn) const
{
	return Pawn && Feeders.Contains(PawnKey(Pawn));
}

bool UNoFearPortalSubsystem::IsActorFeeder(const AActor* Actor)
{
	const APawn* Pawn = Cast<APawn>(Actor);
	if (const AController* Controller = Cast<AController>(Actor))
	{
		Pawn = Controller->GetPawn();
	}
	const UNoFearPortalSubsystem* Subsystem = Get(Actor);
	return Subsystem && Subsystem->IsFeeder(Pawn);
}

ANoFearPortal* UNoFearPortalSubsystem::GetFeederPortal(const APawn* Pawn) const
{
	const FFeederState* State = Pawn ? Feeders.Find(PawnKey(Pawn)) : nullptr;
	return State ? State->Portal.Get() : nullptr;
}

TArray<ANoFearPortal*> UNoFearPortalSubsystem::GetPortals() const
{
	TArray<ANoFearPortal*> Result;
	for (const TWeakObjectPtr<ANoFearPortal>& Portal : Portals)
	{
		if (ANoFearPortal* Valid = Portal.Get())
		{
			Result.Add(Valid);
		}
	}
	return Result;
}

float UNoFearPortalSubsystem::GetBreakProgress(const APawn* Pawn) const
{
	const FBreakState* State = Pawn ? BreakStates.Find(PawnKey(Pawn)) : nullptr;
	return State ? FMath::Clamp(State->Progress, 0.f, 1.f) : 0.f;
}

float UNoFearPortalSubsystem::GetFreeProgress(const APawn* Pawn) const
{
	const FFeederState* State = Pawn ? Feeders.Find(PawnKey(Pawn)) : nullptr;
	return State ? FMath::Clamp(State->FreeProgress, 0.f, 1.f) : 0.f;
}

bool UNoFearPortalSubsystem::GetHeroFreeing(const APlayerController* PlayerController, APawn*& OutFeeder, float& OutProgress) const
{
	OutFeeder = nullptr;
	OutProgress = 0.f;
	const FHeroFreeState* State = PlayerController ? HeroFreeing.Find(TWeakObjectPtr<APlayerController>(const_cast<APlayerController*>(PlayerController))) : nullptr;
	if (!State || !State->Feeder.IsValid())
	{
		return false;
	}
	OutFeeder = State->Feeder.Get();
	OutProgress = GetFreeProgress(OutFeeder);
	return true;
}

APawn* UNoFearPortalSubsystem::GetRescueTarget(const ANoFearAIController* Guardian) const
{
	const FRescueState* State = Guardian ? Rescues.Find(TWeakObjectPtr<ANoFearAIController>(const_cast<ANoFearAIController*>(Guardian))) : nullptr;
	return State ? State->Feeder.Get() : nullptr;
}

bool UNoFearPortalSubsystem::StartSquadRescue(ANoFearAIController* Bot, APawn* Target, bool bCalm, float AbortDistance)
{
	APawn* Rescuer = Bot ? Bot->GetPawn() : nullptr;
	const UWorld* World = GetWorld();
	if (!Rescuer || !Target || !World || UNoFearHealthComponent::IsActorIncapacitated(Rescuer) || IsFeeder(Rescuer)
		|| IsRescueTaken(Target, Bot) || (bCalm ? IsFeeder(Target) : !IsFeeder(Target)))
	{
		return false;
	}
	if (FRescueState* Existing = Rescues.Find(Bot))
	{
		if (Existing->Feeder.Get() == Target && Existing->bCalm == bCalm)
		{
			Existing->AbortDistance = AbortDistance;
			return true;
		}
		EndRescue(Bot, false);
	}
	if (Bot->IsPortalControlled())
	{
		return false;
	}
	RescueCooldowns.Remove(Bot);
	FRescueState& State = Rescues.Add(Bot);
	State.Feeder = Target;
	State.StartTime = World->GetTimeSeconds();
	State.bSquad = true;
	State.bCalm = bCalm;
	State.AbortDistance = AbortDistance;
	Bot->SetPortalControl(true);
	Bot->Shout(PickLine(bCalm ? CalmLines() : RescueLines()), 2.f);
	UE_LOG(LogNoFearAI, Log, TEXT("Portal: отряд посылает %s %s %s"), *Rescuer->GetName(), bCalm ? TEXT("успокоить") : TEXT("вывести"), *Target->GetName());
	return true;
}

void UNoFearPortalSubsystem::StopSquadRescue(ANoFearAIController* Bot)
{
	const FRescueState* State = Bot ? Rescues.Find(Bot) : nullptr;
	if (State && State->bSquad)
	{
		EndRescue(Bot, false);
	}
}

bool UNoFearPortalSubsystem::IsRescueTaken(const APawn* Target, const ANoFearAIController* Except) const
{
	if (!Target)
	{
		return false;
	}
	for (const TPair<TWeakObjectPtr<ANoFearAIController>, FRescueState>& Pair : Rescues)
	{
		if (Pair.Key.IsValid() && Pair.Key.Get() != Except && Pair.Value.Feeder.Get() == Target)
		{
			return true;
		}
	}
	for (const TPair<TWeakObjectPtr<APlayerController>, FHeroFreeState>& Pair : HeroFreeing)
	{
		if (Pair.Key.IsValid() && Pair.Value.Feeder.Get() == Target)
		{
			return true;
		}
	}
	return false;
}

TArray<APawn*> UNoFearPortalSubsystem::GetFeeders() const
{
	TArray<APawn*> Result;
	for (const TPair<TWeakObjectPtr<APawn>, FFeederState>& Pair : Feeders)
	{
		if (APawn* Feeder = Pair.Key.Get())
		{
			Result.Add(Feeder);
		}
	}
	return Result;
}

bool UNoFearPortalSubsystem::IsFreeingFeeder(const AActor* Actor) const
{
	if (!Actor)
	{
		return false;
	}
	for (const TPair<TWeakObjectPtr<APlayerController>, FHeroFreeState>& Pair : HeroFreeing)
	{
		if (Pair.Key.IsValid() && Pair.Key->GetPawn() == Actor && Pair.Value.Feeder.IsValid())
		{
			return true;
		}
	}
	for (const TPair<TWeakObjectPtr<ANoFearAIController>, FRescueState>& Pair : Rescues)
	{
		if (Pair.Key.IsValid() && Pair.Key->GetPawn() == Actor && Pair.Value.Feeder.IsValid())
		{
			return true;
		}
	}
	return false;
}

ANoFearPortal* UNoFearPortalSubsystem::GetMonsterPortal(const APawn* Monster) const
{
	if (!Monster)
	{
		return nullptr;
	}
	for (const TWeakObjectPtr<ANoFearPortal>& Weak : Portals)
	{
		ANoFearPortal* Portal = Weak.Get();
		if (Portal && Portal->Monsters.Contains(PawnKey(Monster)))
		{
			return Portal;
		}
	}
	return nullptr;
}

ANoFearPortal* UNoFearPortalSubsystem::FindNearestPortal(FVector Location, bool bIncludeClosing) const
{
	ANoFearPortal* Best = nullptr;
	double BestDistance = TNumericLimits<double>::Max();
	for (const TWeakObjectPtr<ANoFearPortal>& Weak : Portals)
	{
		ANoFearPortal* Portal = Weak.Get();
		if (!Portal || (!bIncludeClosing && Portal->bClosing))
		{
			continue;
		}
		const double Distance = FVector::DistSquared(Portal->GetActorLocation(), Location);
		if (Distance < BestDistance)
		{
			Best = Portal;
			BestDistance = Distance;
		}
	}
	return Best;
}

ENoFearPortalStage UNoFearPortalSubsystem::GetStageForSize(float Size) const
{
	if (Size >= LargeSize)
	{
		return ENoFearPortalStage::Large;
	}
	if (Size >= MediumSize)
	{
		return ENoFearPortalStage::Medium;
	}
	return Size >= SmallSize ? ENoFearPortalStage::Small : ENoFearPortalStage::Rift;
}

float UNoFearPortalSubsystem::GetAuraRadius(const ANoFearPortal* Portal) const
{
	return Portal ? AuraRadius + AuraRadiusPerSize * Portal->Size : 0.f;
}

float UNoFearPortalSubsystem::GetBreakTimeFor(const APawn* Pawn) const
{
	const bool bShooter = UNoFearFearSubsystem::IsShooterTeam(UNoFearTeamLibrary::GetTeam(Pawn));
	return FMath::Max(bShooter ? ShooterBreakTime : BreakTime, 0.1f);
}

void UNoFearPortalSubsystem::GetInteractKeys(const APlayerController* PlayerController, TArray<FKey>& OutKeys)
{
	OutKeys.Reset();
	const ANoFearPlayerController* NoFearController = Cast<ANoFearPlayerController>(PlayerController);
	const UEnhancedInputLocalPlayerSubsystem* Input = PlayerController && PlayerController->GetLocalPlayer()
		? ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PlayerController->GetLocalPlayer()) : nullptr;
	if (Input && NoFearController && NoFearController->InteractAction)
	{
		OutKeys = Input->QueryKeysMappedToAction(NoFearController->InteractAction);
	}
	if (OutKeys.IsEmpty())
	{
		OutKeys.Add(EKeys::E);
	}
}

// --- Слом ---

bool UNoFearPortalSubsystem::CanBreak(const APawn* Pawn) const
{
	if (!Pawn || Pawn->IsPlayerControlled() || IsFeeder(Pawn) || !GetBot(Pawn) || UNoFearHealthComponent::IsActorIncapacitated(Pawn))
	{
		return false;
	}
	const ENoFearTeam Team = UNoFearTeamLibrary::GetTeam(Pawn);
	if (Team == ENoFearTeam::Monster || Team == ENoFearTeam::AggroMonster || Team == ENoFearTeam::Player)
	{
		return false;
	}
	const UNoFearFearComponent* Fear = UNoFearFearComponent::FindFear(Pawn);
	return Fear && Fear->CanBeAfraid();
}

void UNoFearPortalSubsystem::UpdateBreaking(float DeltaTime, double Now)
{
	const UNoFearBotSubsystem* Bots = UNoFearBotSubsystem::Get(this);
	if (!Bots)
	{
		return;
	}

	TArray<APawn*> ToBreak;
	for (const ANoFearAIController* Bot : Bots->GetBots())
	{
		APawn* Pawn = Bot ? Bot->GetPawn() : nullptr;
		if (!CanBreak(Pawn))
		{
			continue;
		}
		FBreakState* State = BreakStates.Find(Pawn);
		if (State && State->ImmuneUntil > Now)
		{
			continue;
		}
		// Ужас непрерывен: страх у потолка и прямой испуг совсем недавно. Сбежавший от ужаса копит слом назад.
		const UNoFearFearComponent* Fear = UNoFearFearComponent::FindFear(Pawn);
		const bool bTerror = Fear->GetFear() >= BreakFear && Now - Fear->GetLastDirectGainTime() <= BreakFreshTime;
		if (!bTerror && (!State || State->Progress <= 0.f))
		{
			continue;
		}
		if (!State)
		{
			State = &BreakStates.Add(Pawn);
		}
		const float Time = GetBreakTimeFor(Pawn);
		State->Progress = bTerror
			? State->Progress + DeltaTime / Time
			: FMath::Max(State->Progress - DeltaTime * BreakDrainScale / Time, 0.f);
		if (State->Progress >= 1.f)
		{
			ToBreak.Add(Pawn);
		}
	}

	for (APawn* Pawn : ToBreak)
	{
		BreakPerson(Pawn);
	}
	for (auto It = BreakStates.CreateIterator(); It; ++It)
	{
		if (!It.Key().IsValid() || (It.Value().Progress <= 0.f && It.Value().ImmuneUntil <= Now))
		{
			It.RemoveCurrent();
		}
	}
}

void UNoFearPortalSubsystem::HoldBreak(APawn* Pawn, float Seconds)
{
	const UWorld* World = GetWorld();
	if (!World || !Pawn)
	{
		return;
	}
	FBreakState& State = BreakStates.FindOrAdd(Pawn);
	State.Progress = 0.f;
	State.ImmuneUntil = FMath::Max(State.ImmuneUntil, World->GetTimeSeconds() + FMath::Max(Seconds, 0.f));
}

bool UNoFearPortalSubsystem::BreakPerson(APawn* Pawn)
{
	const UWorld* World = GetWorld();
	if (!World || !CanBreak(Pawn))
	{
		return false;
	}
	const double Now = World->GetTimeSeconds();
	ANoFearPortal* Portal = ChoosePortalFor(Pawn, Now);
	if (!Portal)
	{
		return false;
	}

	BreakStates.Remove(Pawn);
	AddFeeder(Portal, Pawn);
	if (ANoFearAIController* Bot = GetBot(Pawn))
	{
		Bot->Shout(PickLine(BreakLines()), 2.5f);
	}
	// Крик сломавшегося пугает соседей и показывает, откуда беда.
	UNoFearBotSubsystem::ReportDanger(this, ENoFearDangerType::Scream, Pawn, Pawn->GetActorLocation(), BreakScreamRadius, Portal->GetActorLocation());
	UE_LOG(LogNoFearAI, Log, TEXT("Portal: %s сломался и питает %s (размер %.1f, проводников %d)"),
		*Pawn->GetName(), *Portal->GetName(), Portal->Size, Portal->GetFeederCount());
	return true;
}

ANoFearPortal* UNoFearPortalSubsystem::ChoosePortalFor(const APawn* Pawn, double Now)
{
	const FVector Location = Pawn->GetActorLocation();
	ANoFearPortal* Nearest = FindNearestPortal(Location, true);
	const bool bNearEnough = Nearest && FVector::Dist2D(Nearest->GetActorLocation(), Location) <= JoinRadius;
	if (!bNearEnough)
	{
		// Рядом портала нет — открывается новый, если их ещё не слишком много и прошлый открылся не только что.
		const bool bCanOpen = Portals.Num() < MaxPortals && (Portals.IsEmpty() || Now - LastOpenTime >= OpenCooldown);
		FVector OpenLocation;
		if (bCanOpen && FindOpenLocation(Pawn, OpenLocation))
		{
			return OpenPortal(OpenLocation);
		}
	}
	if (Nearest && Nearest->bClosing)
	{
		ReopenPortal(Nearest);
	}
	return Nearest;
}

bool UNoFearPortalSubsystem::FindOpenLocation(const APawn* Pawn, FVector& OutLocation) const
{
	const UWorld* World = GetWorld();
	if (!World || !Pawn)
	{
		return false;
	}
	const FVector Origin = Pawn->GetActorLocation();
	const UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);

	// Разлом открывается там, куда человек смотрит в ужасе: со стороны того, что его напугало, иначе — перед ним.
	FVector Facing = Pawn->GetActorForwardVector().GetSafeNormal2D();
	if (const UNoFearFearComponent* Fear = UNoFearFearComponent::FindFear(Pawn))
	{
		const FVector ToSource = (Fear->GetSourceLocation() - Origin).GetSafeNormal2D();
		if (!ToSource.IsNearlyZero())
		{
			Facing = ToSource;
		}
	}
	if (Facing.IsNearlyZero())
	{
		Facing = FVector::ForwardVector;
	}

	const float BaseYaw = Facing.Rotation().Yaw;
	const float Distance = FMath::FRandRange(OpenDistanceMin, FMath::Max(OpenDistanceMin, OpenDistanceMax));
	const FVector Eye = Pawn->GetPawnViewLocation();
	FCollisionQueryParams Params(SCENE_QUERY_STAT(NoFearPortalOpen), false, Pawn);
	static const float Offsets[] = {0.f, 45.f, -45.f, 90.f, -90.f, 135.f, -135.f, 180.f};

	bool bHasFallback = false;
	FVector Fallback = FVector::ZeroVector;
	for (const float Offset : Offsets)
	{
		const FVector Wanted = Origin + FRotator(0.f, BaseYaw + Offset, 0.f).Vector() * Distance;
		FVector Candidate = Wanted;
		if (Nav)
		{
			FNavLocation NavLocation;
			if (!Nav->ProjectPointToNavigation(Wanted, NavLocation, NavExtent))
			{
				continue;
			}
			Candidate = NavLocation.Location;
		}
		Candidate = SnapToGround(World, Candidate, Pawn);
		if (FVector::Dist2D(Candidate, Origin) < OpenDistanceMin * 0.5f)
		{
			continue;
		}
		if (!bHasFallback)
		{
			Fallback = Candidate;
			bHasFallback = true;
		}
		// Лучше всего — на виду: человек смотрит в провал, луч к нему не уходит в стену.
		if (!World->LineTraceTestByChannel(Eye, Candidate + FVector(0.f, 0.f, 100.f), ECC_Visibility, Params))
		{
			OutLocation = Candidate;
			return true;
		}
	}
	OutLocation = bHasFallback ? Fallback : SnapToGround(World, Origin + Facing * OpenDistanceMin, Pawn);
	return true;
}

// --- Порталы ---

ANoFearPortal* UNoFearPortalSubsystem::OpenPortal(FVector Location, float InSize)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return nullptr;
	}
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ANoFearPortal* Portal = World->SpawnActor<ANoFearPortal>(ANoFearPortal::StaticClass(), Location, FRotator::ZeroRotator, Params);
	if (!Portal)
	{
		return nullptr;
	}

	const double Now = World->GetTimeSeconds();
	// Провал на экране растёт из точки (DisplaySize = 0 при BeginPlay) до настоящего размера.
	Portal->Size = FMath::Clamp(InSize > 0.f ? InSize : StartSize, 0.f, 100.f);
	Portal->Stage = GetStageForSize(Portal->Size);
	Portal->OpenTime = Now;
	Portal->NextSpawnTime = Portal->Stage != ENoFearPortalStage::Rift ? Now + FirstSpawnDelay : 0.0;
	Portals.Add(Portal);
	LastOpenTime = Now;
	PreloadMonsterClasses();
	ReportRiftToHeroes(World, Location);
	UE_LOG(LogNoFearAI, Log, TEXT("Portal: открыт %s в %s, размер %.1f"), *Portal->GetName(), *Location.ToCompactString(), Portal->Size);
	return Portal;
}

void UNoFearPortalSubsystem::ClosePortal(ANoFearPortal* Portal)
{
	if (const UWorld* World = GetWorld())
	{
		BeginClosing(Portal, World->GetTimeSeconds());
	}
}

void UNoFearPortalSubsystem::SetPortalSize(ANoFearPortal* Portal, float NewSize)
{
	if (!Portal)
	{
		return;
	}
	Portal->Size = FMath::Clamp(NewSize, 0.f, 100.f);
	if (Portal->bClosing && Portal->Size > 0.f)
	{
		ReopenPortal(Portal);
	}
	// Ступень и первая волна пересчитаются при обновлении.
}

void UNoFearPortalSubsystem::UpdateGrowth(float DeltaTime, double Now)
{
	TArray<ANoFearPortal*> ToClose;
	for (const TWeakObjectPtr<ANoFearPortal>& Weak : Portals)
	{
		ANoFearPortal* Portal = Weak.Get();
		if (!Portal || Portal->bClosing)
		{
			continue;
		}
		Portal->Feeders.RemoveAll([this, Portal](const TWeakObjectPtr<APawn>& Feeder)
		{
			const FFeederState* State = Feeders.Find(Feeder);
			return !Feeder.IsValid() || !State || State->Portal.Get() != Portal;
		});

		// Проводники складываются с убылью: двое растят быстрее одного, но не вдвое.
		const int32 Count = Portal->Feeders.Num();
		if (Count > 0)
		{
			// Шептун у портала ускоряет рост (ApplyWhisperBoost).
			const float Growth = FeedRate * FMath::Pow(static_cast<float>(Count), FeedExponent) * Portal->GetGrowthScale();
			Portal->Size = FMath::Min(Portal->Size + Growth * DeltaTime, 100.f);
		}
		else
		{
			Portal->Size = FMath::Max(Portal->Size - StarveRate * DeltaTime, 0.f);
		}

		const ENoFearPortalStage NewStage = GetStageForSize(Portal->Size);
		if (NewStage != Portal->Stage)
		{
			UE_LOG(LogNoFearAI, Log, TEXT("Portal: %s ступень %d -> %d (размер %.1f, проводников %d)"),
				*Portal->GetName(), static_cast<int32>(Portal->Stage), static_cast<int32>(NewStage), Portal->Size, Count);
			if (NewStage > Portal->Stage)
			{
				// Портал дорос до новой ступени — новая волна выходит почти сразу, а не ждёт старого интервала.
				ReportRiftToHeroes(GetWorld(), Portal->GetActorLocation());
				if (Portal->NextSpawnTime > 0.0)
				{
					Portal->NextSpawnTime = FMath::Min(Portal->NextSpawnTime, Now + FirstSpawnDelay);
				}
			}
			Portal->Stage = NewStage;
		}
		// Трещина монстров не выпускает; доросла до малого — первый выходит через FirstSpawnDelay.
		if (Portal->Stage == ENoFearPortalStage::Rift)
		{
			Portal->NextSpawnTime = 0.0;
		}
		else if (Portal->NextSpawnTime <= 0.0)
		{
			Portal->NextSpawnTime = Now + FirstSpawnDelay;
		}

		if (Count == 0 && Portal->Size <= 0.f)
		{
			ToClose.Add(Portal);
		}
	}
	for (ANoFearPortal* Portal : ToClose)
	{
		BeginClosing(Portal, Now);
	}
}

void UNoFearPortalSubsystem::GatherPeople(TArray<APawn*>& OutPeople) const
{
	OutPeople.Reset();
	if (const UNoFearBotSubsystem* Bots = UNoFearBotSubsystem::Get(this))
	{
		for (const ANoFearAIController* Bot : Bots->GetBots())
		{
			APawn* Pawn = Bot ? Bot->GetPawn() : nullptr;
			if (IsAlive(Pawn))
			{
				OutPeople.Add(Pawn);
			}
		}
	}
	if (const UWorld* World = GetWorld())
	{
		for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
		{
			APawn* Pawn = It->Get() ? It->Get()->GetPawn() : nullptr;
			if (IsAlive(Pawn))
			{
				OutPeople.AddUnique(Pawn);
			}
		}
	}
}

void UNoFearPortalSubsystem::UpdateAura(float DeltaTime)
{
	if (Portals.IsEmpty() && Feeders.IsEmpty())
	{
		return;
	}

	TArray<APawn*> People;
	GatherPeople(People);
	TArray<const APawn*> FeederPawns;
	for (const TPair<TWeakObjectPtr<APawn>, FFeederState>& Pair : Feeders)
	{
		if (const APawn* Feeder = Pair.Key.Get())
		{
			FeederPawns.Add(Feeder);
		}
	}

	for (APawn* Person : People)
	{
		UNoFearFearComponent* Fear = UNoFearFearComponent::FindFear(Person);
		if (!Fear || !Fear->CanBeAfraid() || IsFeeder(Person))
		{
			continue;
		}
		const FVector Location = Person->GetActorLocation();

		// Аура портала: у сердца — прямой испуг до 100 (стоящего там сломает), дальше — давит, но не ломает.
		for (const TWeakObjectPtr<ANoFearPortal>& Weak : Portals)
		{
			const ANoFearPortal* Portal = Weak.Get();
			if (!Portal || Portal->bClosing)
			{
				continue;
			}
			const float Radius = GetAuraRadius(Portal);
			const float Distance = FVector::Dist(Location, Portal->GetActorLocation());
			if (Distance > Radius)
			{
				continue;
			}
			const float CoreRadius = Radius * AuraCoreFraction;
			if (Distance <= CoreRadius)
			{
				Fear->AddFear(AuraFearRate * DeltaTime, AuraCoreCap, Portal->GetCoreLocation(), true);
			}
			else
			{
				const float Alpha = FMath::Clamp((Distance - CoreRadius) / FMath::Max(Radius - CoreRadius, 1.f), 0.f, 1.f);
				Fear->AddFear(AuraFearRate * FMath::Lerp(1.f, 0.5f, Alpha) * DeltaTime, FMath::Lerp(AuraCoreCap, AuraEdgeCap, Alpha), Portal->GetCoreLocation(), false);
			}
		}

		// Проводник в трансе жуток сам по себе — как паника соседа.
		for (const APawn* Feeder : FeederPawns)
		{
			if (Feeder != Person && FVector::Dist(Location, Feeder->GetActorLocation()) <= FeederFearRadius)
			{
				Fear->AddFear(FeederFearRate * DeltaTime, FeederFearCap, Feeder->GetActorLocation(), false);
			}
		}
	}
}

// --- Проводники ---

void UNoFearPortalSubsystem::AddFeeder(ANoFearPortal* Portal, APawn* Pawn)
{
	UWorld* World = GetWorld();
	if (!World || !Portal || !Pawn)
	{
		return;
	}
	ANoFearAIController* Bot = GetBot(Pawn);
	// Страж, сломавшийся по дороге к проводнику, сам становится проводником.
	EndRescue(Bot, false);

	FFeederState& State = Feeders.FindOrAdd(Pawn);
	State = FFeederState();
	State.Portal = Portal;
	State.NextWhisperTime = World->GetTimeSeconds() + FMath::FRandRange(3.f, WhisperInterval);
	if (UNoFearFearComponent* Fear = UNoFearFearComponent::FindFear(Pawn))
	{
		// Проводник сам не успокаивается: спада нет, страх держится на 100, пока его не освободят.
		State.SavedDecayMultiplier = Fear->DecayMultiplier;
		Fear->DecayMultiplier = 0.f;
		Fear->SetFear(100.f);
	}
	Portal->Feeders.AddUnique(Pawn);

	// Успокаивать его бесполезно — герой бросает и может освобождать.
	for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
	{
		ANoFearPlayerController* PlayerController = Cast<ANoFearPlayerController>(It->Get());
		if (PlayerController && PlayerController->GetCalmTarget() == Pawn)
		{
			PlayerController->StopCalming();
		}
	}

	if (Bot)
	{
		Bot->SetPortalTrance(true, Portal);
	}
	UNoFearFreeComponent* FreeComponent = NewObject<UNoFearFreeComponent>(Pawn);
	FreeComponent->InteractionDistance = HeroFreeDistance;
	FreeComponent->RegisterComponent();
	// Ссылка на State могла устареть: компонент мог что-то добавить в карту только через подсистему, но перестрахуемся.
	if (FFeederState* Fresh = Feeders.Find(Pawn))
	{
		Fresh->FreeComponent = FreeComponent;
	}
}

void UNoFearPortalSubsystem::RemoveFeeder(APawn* Pawn, bool bWake, AActor* Freer)
{
	FFeederState State;
	if (!Pawn || !Feeders.RemoveAndCopyValue(Pawn, State))
	{
		return;
	}
	const UWorld* World = GetWorld();
	const double Now = World ? World->GetTimeSeconds() : 0.0;
	ANoFearPortal* Portal = State.Portal.Get();
	if (Portal)
	{
		Portal->Feeders.Remove(Pawn);
	}
	if (UNoFearFreeComponent* FreeComponent = State.FreeComponent.Get())
	{
		FreeComponent->DestroyComponent();
	}
	EndRescuesOf(Pawn);
	TArray<APlayerController*> Heroes;
	for (const TPair<TWeakObjectPtr<APlayerController>, FHeroFreeState>& Pair : HeroFreeing)
	{
		if (Pair.Value.Feeder.Get() == Pawn)
		{
			Heroes.Add(Pair.Key.Get());
		}
	}
	for (APlayerController* Hero : Heroes)
	{
		StopHeroFreeing(Hero);
	}

	UNoFearFearComponent* Fear = UNoFearFearComponent::FindFear(Pawn);
	if (Fear)
	{
		Fear->DecayMultiplier = State.SavedDecayMultiplier;
	}
	ANoFearAIController* Bot = GetBot(Pawn);
	if (!bWake)
	{
		// Погиб или упал раненым: дерево не продолжится, пока он не на ногах.
		if (Bot)
		{
			Bot->SetPortalTrance(false);
		}
		return;
	}

	// Очнулся: всё ещё в панике, бежит прочь от портала и какое-то время не сломается снова.
	if (Fear)
	{
		Fear->SetFear(FMath::FRandRange(FreedFearMin, FMath::Max(FreedFearMin, FreedFearMax)));
		if (Portal)
		{
			Fear->AddFear(KINDA_SMALL_NUMBER * 10.f, 100.f, Portal->GetActorLocation(), false);
		}
	}
	FBreakState& Break = BreakStates.FindOrAdd(Pawn);
	Break.Progress = 0.f;
	Break.ImmuneUntil = Now + BreakImmunityTime;
	if (Bot)
	{
		Bot->SetPortalTrance(false);
		Bot->Shout(PickLine(WakeLines()), 2.f);
	}

	// Вытащил человека — герою самому легче (браслет, этап 12).
	const APawn* FreerPawn = Cast<APawn>(Freer);
	if (const ANoFearPlayerController* PlayerController = FreerPawn ? Cast<ANoFearPlayerController>(FreerPawn->GetController()) : nullptr)
	{
		if (UNoFearBraceletComponent* Bracelet = PlayerController->GetBracelet())
		{
			Bracelet->NotifyCalmedCivilian();
		}
	}
	UE_LOG(LogNoFearAI, Log, TEXT("Portal: %s освобождён (%s)"), *Pawn->GetName(), Freer ? *Freer->GetName() : TEXT("портал затянулся"));
}

bool UNoFearPortalSubsystem::FreeFeeder(APawn* Feeder, AActor* Freer)
{
	if (!IsFeeder(Feeder))
	{
		return false;
	}
	RemoveFeeder(Feeder, true, Freer);
	return true;
}

bool UNoFearPortalSubsystem::ApplyFreeing(APawn* Feeder, float Amount, AActor* Freer)
{
	FFeederState* State = Feeder ? Feeders.Find(Feeder) : nullptr;
	const UWorld* World = GetWorld();
	if (!State || !World || Amount <= 0.f)
	{
		return false;
	}
	// Шептун у портала замедляет освобождение.
	if (const ANoFearPortal* Portal = State->Portal.Get())
	{
		Amount *= Portal->GetFreeScale();
	}
	State->FreeProgress = FMath::Min(State->FreeProgress + Amount, 1.f);
	State->LastFreeTime = World->GetTimeSeconds();
	return State->FreeProgress >= 1.f && FreeFeeder(Feeder, Freer);
}

void UNoFearPortalSubsystem::UpdateFeeders(float DeltaTime, double Now)
{
	TArray<TWeakObjectPtr<APawn>> Stale;
	TArray<APawn*> Fallen;
	TArray<APawn*> Orphans;
	for (TPair<TWeakObjectPtr<APawn>, FFeederState>& Pair : Feeders)
	{
		APawn* Pawn = Pair.Key.Get();
		FFeederState& State = Pair.Value;
		if (!Pawn)
		{
			Stale.Add(Pair.Key);
			continue;
		}
		if (UNoFearHealthComponent::IsActorIncapacitated(Pawn))
		{
			Fallen.Add(Pawn);
			continue;
		}
		const ANoFearPortal* Portal = State.Portal.Get();
		if (!Portal || Portal->bClosing)
		{
			Orphans.Add(Pawn);
			continue;
		}

		if (UNoFearFearComponent* Fear = UNoFearFearComponent::FindFear(Pawn); Fear && Fear->GetFear() < 100.f)
		{
			Fear->SetFear(100.f);
		}
		// Без рук освобождение откатывается: бросил на полпути — начинай почти заново.
		if (State.FreeProgress > 0.f && Now - State.LastFreeTime > FreeHoldGrace)
		{
			State.FreeProgress = FMath::Max(State.FreeProgress - FreeProgressDecay * DeltaTime, 0.f);
		}
		if (Now >= State.NextWhisperTime)
		{
			if (ANoFearAIController* Bot = GetBot(Pawn))
			{
				Bot->Shout(PickLine(WhisperLines()), 2.f);
			}
			State.NextWhisperTime = Now + WhisperInterval * FMath::FRandRange(0.7f, 1.3f);
		}
	}

	for (const TWeakObjectPtr<APawn>& Key : Stale)
	{
		Feeders.Remove(Key);
	}
	for (APawn* Pawn : Fallen)
	{
		const bool bDowned = !UNoFearHealthComponent::IsActorDead(Pawn);
		RemoveFeeder(Pawn, false, nullptr);
		if (bDowned)
		{
			// Поднятый раненый не сломается сразу снова.
			FBreakState& Break = BreakStates.FindOrAdd(Pawn);
			Break.Progress = 0.f;
			Break.ImmuneUntil = Now + BreakImmunityTime;
		}
	}
	for (APawn* Pawn : Orphans)
	{
		RemoveFeeder(Pawn, true, nullptr);
	}
}

// --- Монстры ---

void UNoFearPortalSubsystem::PreloadMonsterClasses()
{
	if (LoadedMonsterClasses.Num() == SlotCount)
	{
		return;
	}
	LoadedMonsterClasses.Reset();
	LoadedMonsterClasses.Add(MonsterClass.LoadSynchronous());
	LoadedMonsterClasses.Add(JumperClass.LoadSynchronous());
	LoadedMonsterClasses.Add(SpitterClass.LoadSynchronous());
	LoadedMonsterClasses.Add(TankClass.LoadSynchronous());
}

int32 UNoFearPortalSubsystem::CountMonsters(const ANoFearPortal* Portal, UClass* OfClass) const
{
	int32 Count = 0;
	if (Portal)
	{
		for (const TWeakObjectPtr<APawn>& Monster : Portal->Monsters)
		{
			const APawn* Pawn = Monster.Get();
			Count += IsAlive(Pawn) && (!OfClass || Pawn->IsA(OfClass)) ? 1 : 0;
		}
	}
	return Count;
}

int32 UNoFearPortalSubsystem::CountAllMonsters() const
{
	int32 Count = 0;
	for (const TWeakObjectPtr<ANoFearPortal>& Portal : Portals)
	{
		Count += CountMonsters(Portal.Get(), nullptr);
	}
	return Count;
}

void UNoFearPortalSubsystem::UpdateMonsters(double Now)
{
	// Убитые больше не числятся за порталом; часть живых стережёт проводников, остальные охотятся.
	for (const TWeakObjectPtr<ANoFearPortal>& Weak : Portals)
	{
		ANoFearPortal* Portal = Weak.Get();
		if (!Portal)
		{
			continue;
		}
		Portal->Monsters.RemoveAll([](const TWeakObjectPtr<APawn>& Monster) { return !IsAlive(Monster.Get()); });
		if (!Portal->bClosing)
		{
			UpdateGuards(Portal);
		}
	}

	int32 Budget = MaxMonsters - CountAllMonsters();
	for (const TWeakObjectPtr<ANoFearPortal>& Weak : Portals)
	{
		ANoFearPortal* Portal = Weak.Get();
		// Монстры идут, только пока портал питают: без проводников он лишь сжимается.
		if (!Portal || Portal->bClosing || Portal->Stage == ENoFearPortalStage::Rift || Portal->Feeders.IsEmpty()
			|| Portal->NextSpawnTime <= 0.0 || Now < Portal->NextSpawnTime)
		{
			continue;
		}

		int32 WaveSize = 1;
		int32 MaxAlive = SmallMaxAlive;
		float Interval = SmallSpawnInterval;
		if (Portal->Stage == ENoFearPortalStage::Medium)
		{
			WaveSize = MediumWaveSize;
			MaxAlive = MediumMaxAlive;
			Interval = MediumSpawnInterval;
		}
		else if (Portal->Stage == ENoFearPortalStage::Large)
		{
			WaveSize = LargeWaveSize;
			MaxAlive = LargeMaxAlive;
			Interval = LargeSpawnInterval;
		}

		const int32 Count = FMath::Min3(WaveSize, MaxAlive - Portal->Monsters.Num(), Budget);
		if (Count > 0)
		{
			Budget -= Count;
			SpawnWave(Portal, Count, Now);
		}
		Portal->NextSpawnTime = Now + Interval * FMath::FRandRange(0.85f, 1.15f);
	}
}

void UNoFearPortalSubsystem::UpdateGuards(ANoFearPortal* Portal)
{
	const FVector Center = Portal->GetActorLocation();
	const float HunterLeash = MonsterLeashRadius + MonsterLeashPerSize * Portal->Size;

	TArray<ANoFearMonsterAIController*> Guards;
	TArray<ANoFearMonsterAIController*> Candidates;
	for (const TWeakObjectPtr<APawn>& Monster : Portal->Monsters)
	{
		ANoFearMonsterAIController* Bot = Monster.IsValid() ? Cast<ANoFearMonsterAIController>(Monster->GetController()) : nullptr;
		if (!Bot)
		{
			continue;
		}
		if (Bot->IsPortalGuard())
		{
			Guards.Add(Bot);
		}
		else if (Bot->IsRingFighter())
		{
			// Стражем становится боец ближнего боя (не прыгун и не плевун), ближайший к порталу.
			Candidates.Add(Bot);
		}
	}

	// Хотя бы один монстр всегда охотится; без проводников стеречь некого.
	int32 Wanted = Portal->Stage == ENoFearPortalStage::Large ? LargeGuards
		: (Portal->Stage == ENoFearPortalStage::Medium ? MediumGuards : SmallGuards);
	Wanted = Portal->Feeders.IsEmpty() ? 0 : FMath::Clamp(Wanted, 0, Portal->Monsters.Num() - 1);
	while (Guards.Num() > Wanted)
	{
		Guards.Pop()->ClearPortalGuard();
	}
	if (Guards.Num() < Wanted && !Candidates.IsEmpty())
	{
		Candidates.Sort([&Center](const ANoFearMonsterAIController& A, const ANoFearMonsterAIController& B)
		{
			return FVector::DistSquared2D(A.GetPawn()->GetActorLocation(), Center) < FVector::DistSquared2D(B.GetPawn()->GetActorLocation(), Center);
		});
		for (int32 Index = 0; Index < Candidates.Num() && Guards.Num() < Wanted; ++Index)
		{
			Guards.Add(Candidates[Index]);
		}
	}

	// Посты — на линиях от портала к проводникам, чуть дальше них: страж стоит спиной к проводнику.
	TArray<float> FeederYaws;
	float FeederDistance = Portal->GetCoreRadius();
	for (const TWeakObjectPtr<APawn>& Feeder : Portal->Feeders)
	{
		if (Feeder.IsValid())
		{
			const FVector Offset = Feeder->GetActorLocation() - Center;
			FeederYaws.Add(Offset.Rotation().Yaw);
			FeederDistance = FMath::Max(FeederDistance, static_cast<float>(Offset.Size2D()));
		}
	}
	// Проводник, который ещё бредёт к порталу издалека, не уводит стражу за собой.
	const float PostRadius = FMath::Min(FeederDistance, Portal->GetCoreRadius() + GuardMaxFeederDistance) + GuardStandOff;
	const UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
	for (int32 Index = 0; Index < Guards.Num(); ++Index)
	{
		float Yaw = 360.f * Index / Guards.Num();
		if (!FeederYaws.IsEmpty())
		{
			// Стражей больше, чем проводников, — лишние встают веером рядом.
			const int32 Round = Index / FeederYaws.Num();
			Yaw = FeederYaws[Index % FeederYaws.Num()] + (Round % 2 ? -30.f : 30.f) * ((Round + 1) / 2);
		}
		FVector Post = Center + FRotator(0.f, Yaw, 0.f).Vector() * PostRadius;
		FNavLocation NavLocation;
		if (Nav && Nav->ProjectPointToNavigation(Post, NavLocation, NavExtent))
		{
			Post = NavLocation.Location;
		}
		Guards[Index]->SetPortalGuard(Center, Post, PostRadius + GuardReach);
		Guards[Index]->LeashRadius = PostRadius + GuardReach;
	}

	// Охотники уходят далеко и гонят людей к порталу: свободный охотник сам ищет того, кого можно сломать,
	// а не ждёт, пока услышит. Двое на одного — только когда людей меньше, чем охотников.
	TArray<APawn*> Prey;
	GatherPeople(Prey);
	Prey.RemoveAll([this, &Center, HunterLeash](const APawn* Person)
	{
		return !CanBreak(Person) || FVector::DistSquared2D(Person->GetActorLocation(), Center) > FMath::Square(HunterLeash);
	});
	TMap<const APawn*, int32> Hunted;
	for (const TWeakObjectPtr<APawn>& Monster : Portal->Monsters)
	{
		ANoFearAIController* Bot = GetBot(Monster.Get());
		const ANoFearMonsterAIController* MonsterBot = Cast<ANoFearMonsterAIController>(Bot);
		if (!Bot || (MonsterBot && MonsterBot->IsPortalGuard()))
		{
			continue;
		}
		Bot->LeashRadius = HunterLeash;
		if (const APawn* Target = Cast<APawn>(Bot->GetCurrentTarget()))
		{
			++Hunted.FindOrAdd(Target);
		}
	}
	for (const TWeakObjectPtr<APawn>& Monster : Portal->Monsters)
	{
		ANoFearMonsterAIController* MonsterBot = Monster.IsValid() ? Cast<ANoFearMonsterAIController>(Monster->GetController()) : nullptr;
		if (!MonsterBot || MonsterBot->IsPortalGuard() || !MonsterBot->bScareCivilians || MonsterBot->GetCurrentTarget()
			|| MonsterBot->IsPortalControlled())
		{
			continue;
		}
		APawn* Best = nullptr;
		float BestScore = TNumericLimits<float>::Max();
		for (APawn* Person : Prey)
		{
			const float Score = FVector::Dist2D(Person->GetActorLocation(), Monster->GetActorLocation()) + 2000.f * Hunted.FindRef(Person);
			if (Score < BestScore)
			{
				BestScore = Score;
				Best = Person;
			}
		}
		if (Best)
		{
			++Hunted.FindOrAdd(Best);
			MonsterBot->ReceiveAlarm(Best, Best->GetActorLocation());
		}
	}

	// Кто освобождает проводника этого портала — на того страж и бросается.
	TArray<APawn*> Freers;
	for (const TPair<TWeakObjectPtr<APlayerController>, FHeroFreeState>& Pair : HeroFreeing)
	{
		if (Pair.Key.IsValid() && Pair.Key->GetPawn() && Portal->Feeders.Contains(Pair.Value.Feeder))
		{
			Freers.Add(Pair.Key->GetPawn());
		}
	}
	for (const TPair<TWeakObjectPtr<ANoFearAIController>, FRescueState>& Pair : Rescues)
	{
		if (Pair.Key.IsValid() && Pair.Key->GetPawn() && Portal->Feeders.Contains(Pair.Value.Feeder))
		{
			Freers.Add(Pair.Key->GetPawn());
		}
	}
	for (APawn* Freer : Freers)
	{
		for (ANoFearMonsterAIController* Guard : Guards)
		{
			if (!Guard->GetCurrentTarget())
			{
				Guard->ReceiveAlarm(Freer, Freer->GetActorLocation());
			}
		}
	}
}

void UNoFearPortalSubsystem::SpawnWave(ANoFearPortal* Portal, int32 Count, double Now)
{
	PreloadMonsterClasses();
	UClass* Basic = LoadedMonsterClasses[SlotMonster];
	UClass* Jumper = LoadedMonsterClasses[SlotJumper];
	UClass* Spitter = LoadedMonsterClasses[SlotSpitter];
	UClass* Tank = LoadedMonsterClasses[SlotTank];

	// Малый — одиночки; средний — монстр со стайкой прыгунов и плевунов; большой — танк впереди волны.
	TArray<UClass*> Wave;
	switch (Portal->Stage)
	{
	case ENoFearPortalStage::Small:
		Wave.Add(Basic);
		break;
	case ENoFearPortalStage::Medium:
		Wave.Add(Basic);
		while (Wave.Num() < Count)
		{
			Wave.Add(FMath::RandBool() ? Jumper : Spitter);
		}
		break;
	case ENoFearPortalStage::Large:
		if (Tank && CountMonsters(Portal, Tank) < MaxTanksPerPortal)
		{
			Wave.Add(Tank);
		}
		while (Wave.Num() < Count)
		{
			const int32 Roll = FMath::RandRange(0, 2);
			Wave.Add(Roll == 0 ? Basic : (Roll == 1 ? Jumper : Spitter));
		}
		break;
	default:
		break;
	}
	Wave.SetNum(FMath::Min(Wave.Num(), Count));

	int32 Spawned = 0;
	for (UClass* Class : Wave)
	{
		Spawned += SpawnMonster(Portal, Class ? Class : Basic) ? 1 : 0;
	}
	UE_LOG(LogNoFearAI, Log, TEXT("Portal: %s выпустил %d (ступень %d, живых %d)"),
		*Portal->GetName(), Spawned, static_cast<int32>(Portal->Stage), CountMonsters(Portal, nullptr));
}

APawn* UNoFearPortalSubsystem::SpawnMonster(ANoFearPortal* Portal, UClass* InMonsterClass)
{
	UWorld* World = GetWorld();
	if (!World || !Portal || !InMonsterClass)
	{
		return nullptr;
	}

	// Выходит из-под края провала в случайную сторону и смотрит наружу.
	const FVector Center = Portal->GetActorLocation();
	FVector Location = Center + FRotator(0.f, FMath::FRandRange(0.f, 360.f), 0.f).Vector() * (Portal->GetCoreRadius() + SpawnRingOffset);
	if (const UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World))
	{
		FNavLocation NavLocation;
		if (Nav->ProjectPointToNavigation(Location, NavLocation, NavExtent) || Nav->ProjectPointToNavigation(Center, NavLocation, NavExtent))
		{
			Location = NavLocation.Location;
		}
	}
	Location.Z += SpawnLift;
	const FVector Outward = (Location - Center).GetSafeNormal2D();
	const FTransform Transform(FRotator(0.f, Outward.IsNearlyZero() ? FMath::FRandRange(0.f, 360.f) : Outward.Rotation().Yaw, 0.f), Location);

	APawn* Monster = World->SpawnActorDeferred<APawn>(InMonsterClass, Transform, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn);
	if (!Monster)
	{
		return nullptr;
	}
	Monster->FinishSpawning(Transform);
	if (!Monster->GetController())
	{
		Monster->SpawnDefaultController();
	}
	if (ANoFearAIController* Bot = GetBot(Monster))
	{
		// Дом — портал: охотится вокруг него, гонит и пугает людей, чтобы те ломались и питали его.
		Bot->SetHomeLocation(Center);
		Bot->LeashRadius = MonsterLeashRadius + MonsterLeashPerSize * Portal->Size;
		if (ANoFearMonsterAIController* MonsterBot = Cast<ANoFearMonsterAIController>(Bot))
		{
			MonsterBot->bScareCivilians = true;
		}
	}
	Portal->Monsters.Add(Monster);
	return Monster;
}

// --- Закрытие ---

void UNoFearPortalSubsystem::BeginClosing(ANoFearPortal* Portal, double Now)
{
	if (!Portal || Portal->bClosing)
	{
		return;
	}
	Portal->bClosing = true;
	Portal->CloseTime = Now;
	Portal->NextSpawnTime = 0.0;
	for (APawn* Feeder : Portal->GetFeeders())
	{
		RemoveFeeder(Feeder, true, nullptr);
	}
	// Затягивает обратно: монстры бросают всё и бегут к порталу.
	const FVector Center = Portal->GetActorLocation();
	for (APawn* Monster : Portal->GetMonsters())
	{
		ANoFearAIController* Bot = GetBot(Monster);
		if (Bot && IsAlive(Monster))
		{
			if (ANoFearMonsterAIController* MonsterBot = Cast<ANoFearMonsterAIController>(Bot))
			{
				MonsterBot->ClearPortalGuard();
			}
			Bot->SetPortalControl(true, true);
			Bot->MoveToLocation(Center, RecallReach * 0.5f);
		}
	}
	UE_LOG(LogNoFearAI, Log, TEXT("Portal: %s затягивается, монстров %d"), *Portal->GetName(), CountMonsters(Portal, nullptr));
}

void UNoFearPortalSubsystem::ReopenPortal(ANoFearPortal* Portal)
{
	if (!Portal || !Portal->bClosing)
	{
		return;
	}
	Portal->bClosing = false;
	Portal->Size = FMath::Max(Portal->Size, StartSize);
	Portal->NextSpawnTime = 0.0;
	for (APawn* Monster : Portal->GetMonsters())
	{
		if (ANoFearAIController* Bot = GetBot(Monster))
		{
			Bot->SetPortalControl(false);
		}
	}
	UE_LOG(LogNoFearAI, Log, TEXT("Portal: %s снова открыт (размер %.1f)"), *Portal->GetName(), Portal->Size);
}

void UNoFearPortalSubsystem::UpdateClosing(double Now)
{
	TArray<ANoFearPortal*> Done;
	for (const TWeakObjectPtr<ANoFearPortal>& Weak : Portals)
	{
		ANoFearPortal* Portal = Weak.Get();
		if (!Portal || !Portal->bClosing)
		{
			continue;
		}
		const FVector Center = Portal->GetActorLocation();
		const bool bTimeUp = Now - Portal->CloseTime >= RecallTime;
		for (int32 Index = Portal->Monsters.Num() - 1; Index >= 0; --Index)
		{
			APawn* Monster = Portal->Monsters[Index].Get();
			if (!IsAlive(Monster))
			{
				Portal->Monsters.RemoveAt(Index);
				continue;
			}
			// Добежал — ушёл в провал; не успел — растворяется, когда портал схлопнется.
			if (bTimeUp || FVector::Dist2D(Monster->GetActorLocation(), Center) <= RecallReach)
			{
				Portal->Monsters.RemoveAt(Index);
				Monster->Destroy();
				continue;
			}
			ANoFearAIController* Bot = GetBot(Monster);
			if (!Bot)
			{
				continue;
			}
			if (!Bot->IsPortalControlled())
			{
				Bot->SetPortalControl(true, true);
			}
			double& LastMove = RecallMoveTimes.FindOrAdd(Monster, Portal->CloseTime);
			if (Bot->GetMoveStatus() == EPathFollowingStatus::Idle && Now - LastMove >= RecallRepathInterval)
			{
				Bot->MoveToLocation(Center, RecallReach * 0.5f);
				LastMove = Now;
			}
		}
		if (bTimeUp)
		{
			Done.Add(Portal);
		}
	}
	for (ANoFearPortal* Portal : Done)
	{
		FinishPortal(Portal);
	}
}

void UNoFearPortalSubsystem::FinishPortal(ANoFearPortal* Portal)
{
	if (!Portal)
	{
		return;
	}
	// Отпустило: у всех, кто был в ауре, страх спадает.
	const FVector Center = Portal->GetActorLocation();
	const float Radius = GetAuraRadius(Portal);
	TArray<APawn*> People;
	GatherPeople(People);
	for (APawn* Person : People)
	{
		UNoFearFearComponent* Fear = UNoFearFearComponent::FindFear(Person);
		if (Fear && !IsFeeder(Person) && FVector::Dist(Person->GetActorLocation(), Center) <= Radius)
		{
			Fear->Soothe(CloseRelief);
		}
	}
	for (APawn* Feeder : Portal->GetFeeders())
	{
		RemoveFeeder(Feeder, true, nullptr);
	}
	for (APawn* Monster : Portal->GetMonsters())
	{
		if (IsAlive(Monster))
		{
			Monster->Destroy();
		}
	}
	Portal->Monsters.Reset();
	Portals.Remove(Portal);
	for (auto It = RecallMoveTimes.CreateIterator(); It; ++It)
	{
		if (!It.Key().IsValid())
		{
			It.RemoveCurrent();
		}
	}
	UE_LOG(LogNoFearAI, Log, TEXT("Portal: %s закрылся"), *Portal->GetName());
	Portal->Destroy();
}

// --- Освобождение героем ---

bool UNoFearPortalSubsystem::StartHeroFreeing(APlayerController* PlayerController, APawn* Feeder, bool bHeldByInput)
{
	APawn* Hero = PlayerController ? PlayerController->GetPawn() : nullptr;
	if (!Hero || !IsFeeder(Feeder) || UNoFearHealthComponent::IsActorIncapacitated(Hero)
		|| FVector::Dist(Hero->GetActorLocation(), Feeder->GetActorLocation()) > HeroFreeDistance + HeroFreeSlack)
	{
		return false;
	}
	if (const FHeroFreeState* Existing = HeroFreeing.Find(PlayerController); Existing && Existing->Feeder.Get() == Feeder)
	{
		return true;
	}

	StopHeroFreeing(PlayerController);
	if (ANoFearPlayerController* NoFearController = Cast<ANoFearPlayerController>(PlayerController))
	{
		NoFearController->StopCalming();
		NoFearController->StopRevive();
	}
	FHeroFreeState& State = HeroFreeing.Add(PlayerController);
	State.Feeder = Feeder;
	State.bHeldByInput = bHeldByInput;
	if (bHeldByInput)
	{
		GetInteractKeys(PlayerController, State.Keys);
	}
	// Держит человека за плечи и зовёт: стоит на месте и не стреляет.
	PlayerController->SetIgnoreMoveInput(true);
	State.bMoveLocked = true;
	if (const ANoFearCharacter* HeroCharacter = Cast<ANoFearCharacter>(Hero))
	{
		if (UNoFearWeaponComponent* Weapons = HeroCharacter->GetWeaponComponent())
		{
			Weapons->StopFire();
		}
	}
	return true;
}

void UNoFearPortalSubsystem::StopHeroFreeing(APlayerController* PlayerController)
{
	FHeroFreeState State;
	if (!PlayerController || !HeroFreeing.RemoveAndCopyValue(PlayerController, State))
	{
		return;
	}
	if (State.bMoveLocked)
	{
		PlayerController->SetIgnoreMoveInput(false);
	}
}

void UNoFearPortalSubsystem::UpdateHeroFreeing(float DeltaTime, double Now)
{
	struct FFreeing
	{
		APlayerController* Controller;
		APawn* Feeder;
		APawn* Hero;
	};
	TArray<FFreeing> Active;
	TArray<APlayerController*> ToStop;
	TArray<TWeakObjectPtr<APlayerController>> Stale;
	for (const TPair<TWeakObjectPtr<APlayerController>, FHeroFreeState>& Pair : HeroFreeing)
	{
		APlayerController* PlayerController = Pair.Key.Get();
		if (!PlayerController)
		{
			Stale.Add(Pair.Key);
			continue;
		}
		const FHeroFreeState& State = Pair.Value;
		APawn* Feeder = State.Feeder.Get();
		APawn* Hero = PlayerController->GetPawn();
		bool bHeld = !State.bHeldByInput;
		for (const FKey& Key : State.Keys)
		{
			bHeld = bHeld || PlayerController->IsInputKeyDown(Key);
		}
		const bool bInRange = Hero && Feeder && FVector::Dist(Hero->GetActorLocation(), Feeder->GetActorLocation()) <= HeroFreeDistance + HeroFreeSlack;
		if (!bHeld || !bInRange || !IsFeeder(Feeder) || UNoFearHealthComponent::IsActorIncapacitated(Hero))
		{
			ToStop.Add(PlayerController);
			continue;
		}
		Active.Add({PlayerController, Feeder, Hero});
	}

	for (const TWeakObjectPtr<APlayerController>& Key : Stale)
	{
		HeroFreeing.Remove(Key);
	}
	for (APlayerController* PlayerController : ToStop)
	{
		StopHeroFreeing(PlayerController);
	}
	for (const FFreeing& Freeing : Active)
	{
		if (ApplyFreeing(Freeing.Feeder, DeltaTime / FMath::Max(HeroFreeTime, 0.1f), Freeing.Hero))
		{
			StopHeroFreeing(Freeing.Controller);
		}
	}
}

// --- Стражи ---

namespace
{
	/** Боец отряда, которого отряд ещё не подхватил (только появился): сам не бежит — им распорядится командир. */
	bool IsAwaitingSquad(const APawn* Pawn, const UNoFearSquadSubsystem* Squads)
	{
		const ANoFearCharacter* Character = Cast<ANoFearCharacter>(Pawn);
		return Squads && Character && !Character->Squad.IsNone() && Squads->GetSquadMembers(Pawn).IsEmpty();
	}
}

void UNoFearPortalSubsystem::AssignRescuers(double Now)
{
	if (Now < NextRescueAssignTime)
	{
		return;
	}
	NextRescueAssignTime = Now + RescueAssignInterval;
	for (auto It = RescueCooldowns.CreateIterator(); It; ++It)
	{
		if (!It.Key().IsValid() || It.Value() <= Now)
		{
			It.RemoveCurrent();
		}
	}
	const UNoFearBotSubsystem* Bots = UNoFearBotSubsystem::Get(this);
	if (!Bots || Feeders.IsEmpty())
	{
		return;
	}
	// Бойцы собранных отрядов в одиночку не ходят: их посылает командир, когда у проводника зачищено.
	const UNoFearSquadSubsystem* Squads = UNoFearSquadSubsystem::Get(this);

	// Одного проводника выводит один страж.
	TSet<const APawn*> Taken;
	for (const TPair<TWeakObjectPtr<ANoFearAIController>, FRescueState>& Pair : Rescues)
	{
		if (const APawn* Feeder = Pair.Value.Feeder.Get())
		{
			Taken.Add(Feeder);
		}
	}
	for (const TPair<TWeakObjectPtr<APlayerController>, FHeroFreeState>& Pair : HeroFreeing)
	{
		if (const APawn* Feeder = Pair.Value.Feeder.Get())
		{
			Taken.Add(Feeder);
		}
	}

	for (ANoFearAIController* Bot : Bots->GetBots())
	{
		APawn* Guardian = Bot ? Bot->GetPawn() : nullptr;
		if (!Guardian || UNoFearTeamLibrary::GetTeam(Guardian) != ENoFearTeam::Ally || Rescues.Contains(Bot) || RescueCooldowns.Contains(Bot)
			|| (Squads && Squads->IsInFormedSquad(Guardian)) || IsAwaitingSquad(Guardian, Squads)
			|| IsFeeder(Guardian) || Bot->IsPortalControlled() || UNoFearHealthComponent::IsActorIncapacitated(Guardian)
			|| HasCombatTarget(Bot, GuardianCombatDistance))
		{
			continue;
		}

		APawn* Best = nullptr;
		float BestDistance = GuardianSearchRadius;
		for (const TPair<TWeakObjectPtr<APawn>, FFeederState>& Pair : Feeders)
		{
			APawn* Feeder = Pair.Key.Get();
			if (!Feeder || Taken.Contains(Feeder) || UNoFearTeamLibrary::AreHostile(Guardian, Feeder))
			{
				continue;
			}
			const float Distance = FVector::Dist(Guardian->GetActorLocation(), Feeder->GetActorLocation());
			if (Distance < BestDistance)
			{
				Best = Feeder;
				BestDistance = Distance;
			}
		}
		if (!Best)
		{
			continue;
		}

		FRescueState& State = Rescues.Add(Bot);
		State.Feeder = Best;
		State.StartTime = Now;
		Taken.Add(Best);
		Bot->SetPortalControl(true);
		Bot->Shout(PickLine(RescueLines()), 2.f);
		UE_LOG(LogNoFearAI, Log, TEXT("Portal: %s идёт выводить %s"), *Guardian->GetName(), *Best->GetName());
	}
}

void UNoFearPortalSubsystem::UpdateRescues(float DeltaTime, double Now)
{
	const UNoFearFearSubsystem* FearSubsystem = UNoFearFearSubsystem::Get(this);
	TArray<TPair<ANoFearAIController*, bool>> ToEnd;
	TArray<TPair<APawn*, ANoFearAIController*>> Working;
	TArray<TPair<APawn*, ANoFearAIController*>> Calming;
	TArray<TWeakObjectPtr<ANoFearAIController>> Stale;
	for (TPair<TWeakObjectPtr<ANoFearAIController>, FRescueState>& Pair : Rescues)
	{
		ANoFearAIController* Bot = Pair.Key.Get();
		if (!Bot)
		{
			Stale.Add(Pair.Key);
			continue;
		}
		FRescueState& State = Pair.Value;
		APawn* Guardian = Bot->GetPawn();
		APawn* Feeder = State.Feeder.Get();
		// Успокаивают живого паникующего (не проводника), пока страх не спал; выводят — того, кто ещё в трансе.
		const bool bTargetOk = State.bCalm
			? Feeder && !IsFeeder(Feeder) && !UNoFearHealthComponent::IsActorIncapacitated(Feeder)
				&& UNoFearFearComponent::GetActorFear(Feeder) > (FearSubsystem ? FearSubsystem->CalmTargetFear : 15.f)
			: IsFeeder(Feeder);
		if (!Guardian || !bTargetOk || UNoFearHealthComponent::IsActorIncapacitated(Guardian))
		{
			ToEnd.Add({Bot, false});
			continue;
		}
		// Враг подошёл — сначала отбиться; вернётся позже (бойца отряда вернёт командир, когда снова чисто).
		if (HasCombatTarget(Bot, State.bSquad ? State.AbortDistance : GuardianCombatDistance))
		{
			ToEnd.Add({Bot, !State.bSquad});
			continue;
		}

		const float Distance = FVector::Dist2D(Guardian->GetActorLocation(), Feeder->GetActorLocation());
		const float BaseReach = State.bCalm && FearSubsystem ? FMath::Min(GuardianReach * 1.5f, FearSubsystem->CalmDistance * 0.8f) : GuardianReach;
		const float Reach = State.bInReach ? BaseReach * RescueReachHysteresis : BaseReach;
		if (Distance <= Reach)
		{
			if (!State.bInReach)
			{
				Bot->StopMovement();
				State.bInReach = true;
			}
			Bot->SetFocus(Feeder);
			(State.bCalm ? Calming : Working).Add({Feeder, Bot});
			continue;
		}
		State.bInReach = false;
		if (Now - State.StartTime > GuardianApproachTime)
		{
			ToEnd.Add({Bot, true});
			continue;
		}
		if (!Bot->IsEvadingGrenade() && Now >= State.NextMoveTime && Bot->GetMoveStatus() == EPathFollowingStatus::Idle)
		{
			Bot->MoveToActor(Feeder, BaseReach * 0.6f);
			State.NextMoveTime = Now + RescueRepathInterval;
		}
	}

	for (const TWeakObjectPtr<ANoFearAIController>& Key : Stale)
	{
		Rescues.Remove(Key);
	}
	for (const TPair<ANoFearAIController*, bool>& End : ToEnd)
	{
		EndRescue(End.Key, End.Value);
	}
	for (const TPair<APawn*, ANoFearAIController*>& Work : Working)
	{
		if (ApplyFreeing(Work.Key, DeltaTime / FMath::Max(GuardianFreeTime, 0.1f), Work.Value->GetPawn()))
		{
			Work.Value->Shout(PickLine(RescuedLines()), 2.f);
		}
	}
	// Успокаивают как герой: страх спадает со скоростью CalmRate до CalmTargetFear, срыв в транс откатывается.
	for (const TPair<APawn*, ANoFearAIController*>& Work : Calming)
	{
		UNoFearFearComponent* Fear = UNoFearFearComponent::FindFear(Work.Key);
		if (!Fear || !FearSubsystem)
		{
			EndRescue(Work.Value, false);
			continue;
		}
		Fear->Comfort(FearSubsystem->CalmRate * DeltaTime);
		if (FBreakState* Break = BreakStates.Find(Work.Key))
		{
			Break->Progress = FMath::Max(0.f, Break->Progress - DeltaTime / FMath::Max(GuardianFreeTime, 0.1f));
		}
		if (Fear->GetFear() <= FearSubsystem->CalmTargetFear)
		{
			BreakStates.Remove(Work.Key);
			Work.Value->Shout(PickLine(CalmedLines()), 2.f);
			EndRescue(Work.Value, false);
		}
	}
}

void UNoFearPortalSubsystem::EndRescue(ANoFearAIController* Guardian, bool bCooldown)
{
	if (!Guardian || !Rescues.Remove(Guardian))
	{
		return;
	}
	Guardian->ClearFocus(EAIFocusPriority::Gameplay);
	Guardian->SetPortalControl(false);
	if (bCooldown)
	{
		if (const UWorld* World = GetWorld())
		{
			RescueCooldowns.Add(Guardian, World->GetTimeSeconds() + GuardianRetryTime);
		}
	}
}

void UNoFearPortalSubsystem::EndRescuesOf(const APawn* Feeder)
{
	TArray<ANoFearAIController*> Guardians;
	for (const TPair<TWeakObjectPtr<ANoFearAIController>, FRescueState>& Pair : Rescues)
	{
		if (Pair.Value.Feeder.Get() == Feeder && Pair.Key.IsValid())
		{
			Guardians.Add(Pair.Key.Get());
		}
	}
	for (ANoFearAIController* Guardian : Guardians)
	{
		EndRescue(Guardian, false);
	}
}

// --- Отладка ---

void UNoFearPortalSubsystem::DrawDebug() const
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	static const TCHAR* StageNames[] = {TEXT("трещина"), TEXT("малый"), TEXT("средний"), TEXT("большой")};
	for (const TWeakObjectPtr<ANoFearPortal>& Weak : Portals)
	{
		const ANoFearPortal* Portal = Weak.Get();
		if (!Portal)
		{
			continue;
		}
		const FVector Center = Portal->GetActorLocation() + FVector(0.f, 0.f, 10.f);
		const float Radius = GetAuraRadius(Portal);
		const FColor Color = Portal->bClosing ? FColor(120, 120, 255) : FColor(190, 90, 255);
		DrawDebugCircle(World, Center, Radius, 48, Color, false, -1.f, 0, 3.f, FVector(1.f, 0.f, 0.f), FVector(0.f, 1.f, 0.f), false);
		DrawDebugCircle(World, Center, Radius * AuraCoreFraction, 32, FColor::Red, false, -1.f, 0, 3.f, FVector(1.f, 0.f, 0.f), FVector(0.f, 1.f, 0.f), false);
		const int32 StageIndex = FMath::Clamp(static_cast<int32>(Portal->Stage), 0, 3);
		DrawDebugString(World, Portal->GetCoreLocation() + FVector(0.f, 0.f, Portal->GetCoreRadius() + 40.f),
			FString::Printf(TEXT("%s %.0f%s  проводников %d  монстров %d"), StageNames[StageIndex], Portal->Size,
				Portal->bClosing ? TEXT(" (затягивается)") : TEXT(""), Portal->GetFeederCount(), CountMonsters(Portal, nullptr)),
			nullptr, Color, 0.f, true);
	}
	for (const TPair<TWeakObjectPtr<APawn>, FFeederState>& Pair : Feeders)
	{
		const APawn* Feeder = Pair.Key.Get();
		if (Feeder)
		{
			DrawDebugString(World, Feeder->GetActorLocation() + FVector(0.f, 0.f, 120.f),
				FString::Printf(TEXT("транс, освобождён %.0f%%"), Pair.Value.FreeProgress * 100.f), nullptr, FColor(190, 90, 255), 0.f, true);
		}
	}
	for (const TPair<TWeakObjectPtr<APawn>, FBreakState>& Pair : BreakStates)
	{
		const APawn* Pawn = Pair.Key.Get();
		if (Pawn && Pair.Value.Progress > 0.f)
		{
			DrawDebugString(World, Pawn->GetActorLocation() + FVector(0.f, 0.f, 120.f),
				FString::Printf(TEXT("слом %.0f%%"), Pair.Value.Progress * 100.f), nullptr, FColor::Orange, 0.f, true);
		}
	}
	for (const TPair<TWeakObjectPtr<ANoFearAIController>, FRescueState>& Pair : Rescues)
	{
		const APawn* Guardian = Pair.Key.IsValid() ? Pair.Key->GetPawn() : nullptr;
		const APawn* Feeder = Pair.Value.Feeder.Get();
		if (Guardian && Feeder)
		{
			DrawDebugLine(World, Guardian->GetActorLocation(), Feeder->GetActorLocation(), FColor::Green, false, -1.f, 0, 2.f);
		}
	}
}
