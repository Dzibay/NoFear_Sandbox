#include "Core/NoFearPlayerController.h"

#include "AI/NoFearBotSubsystem.h"
#include "AI/NoFearCompanionAIController.h"
#include "Core/NoFearCharacter.h"
#include "Core/NoFearInteractableComponent.h"
#include "Core/NoFearPerf.h"
#include "Core/NoFearTeam.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/DamageType.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/PlayerStart.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "Kismet/GameplayStatics.h"
#include "NavigationSystem.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"
#include "Weapons/NoFearGrenadeComponent.h"
#include "Weapons/NoFearWeaponComponent.h"

#define LOCTEXT_NAMESPACE "NoFearPlayerController"

namespace
{
	ANoFearCompanionAIController* GetCompanionController(const APawn* Pawn)
	{
		return Pawn ? Cast<ANoFearCompanionAIController>(Pawn->GetController()) : nullptr;
	}

	/** Враг для приказа «Атакуй»: живой, на ногах, враждебный игроку. */
	bool IsOrderEnemy(const APawn* Player, const AActor* Actor)
	{
		return Player && Actor && Actor->IsA<APawn>() && !UNoFearHealthComponent::IsActorIncapacitated(Actor)
			&& UNoFearTeamLibrary::AreHostile(Player, Actor);
	}

	void IgnoreWithAttached(FCollisionQueryParams& Params, const AActor* Actor)
	{
		if (!Actor)
		{
			return;
		}
		Params.AddIgnoredActor(Actor);
		TArray<AActor*> Attached;
		Actor->GetAttachedActors(Attached);
		Params.AddIgnoredActors(Attached);
	}

	/** Больше строк рации на экране не держим. */
	constexpr int32 MaxRadioLines = 5;
	/** Последняя цель игрока годится для «Атакуй» столько секунд. */
	constexpr double LastHitEnemyMemory = 5.0;
	/** Насколько враг может быть в стороне от прицела, чтобы «Атакуй» взял его, градусы. */
	constexpr float OrderAimConeAngle = 5.f;
}

ANoFearPlayerController::ANoFearPlayerController()
{
	static ConstructorHelpers::FObjectFinderOptional<UInputAction> InteractActionFinder(TEXT("/Game/Input/IA_Interact.IA_Interact"));
	InteractAction = InteractActionFinder.Get();
}

FGenericTeamId ANoFearPlayerController::GetGenericTeamId() const
{
	return FGenericTeamId(static_cast<uint8>(ENoFearTeam::Player));
}

float ANoFearPlayerController::GetRespawnTimeRemaining() const
{
	const UWorld* World = GetWorld();
	if (!bWaitingForRespawn || !bAutoRespawn || !World)
	{
		return 0.f;
	}
	return FMath::Max(0.f, static_cast<float>(RespawnTime - World->GetTimeSeconds()));
}

void ANoFearPlayerController::OnUnPossess()
{
	CloseOrderWheel(false);
	StopRevive();

	Super::OnUnPossess();
}

void ANoFearPlayerController::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	CloseOrderWheel(false);
	StopRevive();

	Super::EndPlay(EndPlayReason);
}

void ANoFearPlayerController::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);

	if (!InPawn)
	{
		return;
	}
	if (!bHasRespawnTransform)
	{
		RespawnTransform = InPawn->GetActorTransform();
		RespawnTransform.SetScale3D(FVector::OneVector);
		bHasRespawnTransform = true;
	}

	if (bWaitingForRespawn)
	{
		bWaitingForRespawn = false;
		GetWorldTimerManager().ClearTimer(RespawnTimer);
		ResetIgnoreMoveInput();
		OnPlayerRespawned.Broadcast();
	}
}

void ANoFearPlayerController::NotifyPawnDied(const FNoFearDamageInfo& Info)
{
	if (bWaitingForRespawn)
	{
		return;
	}

	CloseOrderWheel(false);
	StopRevive();
	bWaitingForRespawn = true;
	RespawnTime = GetWorld()->GetTimeSeconds() + RespawnDelay;
	// Камера остаётся на теле, можно оглядеться; двигаться мёртвый не должен.
	SetIgnoreMoveInput(true);

	OnPlayerDied.Broadcast(Info);

	if (bAutoRespawn)
	{
		GetWorldTimerManager().SetTimer(RespawnTimer, this, &ANoFearPlayerController::RespawnPlayer, FMath::Max(RespawnDelay, 0.01f), false);
	}
}

void ANoFearPlayerController::NotifyHitConfirmed(AActor* Victim, float Damage, bool bHeadshot, bool bKilled)
{
	// Свой собственный выстрел по себе маркер не показывает.
	if (Victim && Victim == GetPawn())
	{
		return;
	}
	if (!bKilled && IsOrderEnemy(GetPawn(), Victim))
	{
		LastHitEnemy = Victim;
		LastHitEnemyTime = GetWorld()->GetTimeSeconds();
	}
	OnHitConfirmed.Broadcast(Victim, Damage, bHeadshot, bKilled);
}

void ANoFearPlayerController::RespawnPlayer()
{
	GetWorldTimerManager().ClearTimer(RespawnTimer);

	AGameModeBase* GameMode = GetWorld() ? GetWorld()->GetAuthGameMode() : nullptr;
	APawn* DeadPawn = GetPawn();
	if (!GameMode || (DeadPawn && !UNoFearHealthComponent::IsActorDead(DeadPawn)))
	{
		return;
	}

	if (DeadPawn)
	{
		// Тело остаётся лежать до своего исчезновения (ANoFearCharacter::CorpseLifeSpan).
		UnPossess();
	}

	// Без PlayerStart FindPlayerStart возвращает WorldSettings (начало координат) — тогда берём запомненную точку.
	AActor* Start = GameMode->FindPlayerStart(this);
	if (bHasRespawnTransform && !(Start && Start->IsA<APlayerStart>()))
	{
		GameMode->RestartPlayerAtTransform(this, RespawnTransform);
	}
	else
	{
		GameMode->RestartPlayer(this);
	}
}

void ANoFearPlayerController::NoFearHurt(float Amount)
{
	if (APawn* ControlledPawn = GetPawn())
	{
		UGameplayStatics::ApplyDamage(ControlledPawn, Amount, nullptr, nullptr, UDamageType::StaticClass());
	}
}

void ANoFearPlayerController::NoFearKill()
{
	if (UNoFearHealthComponent* Health = UNoFearHealthComponent::FindHealth(GetPawn()))
	{
		Health->Kill();
	}
}

void ANoFearPlayerController::NoFearSpawnBot(const FString& Type, int32 Count)
{
	UWorld* World = GetWorld();
	const APawn* ControlledPawn = GetPawn();
	if (!World || !ControlledPawn)
	{
		return;
	}

	const FString ClassPath = FString::Printf(TEXT("/Game/Blueprints/Bots/CBP_Bot_%s.CBP_Bot_%s_C"), *Type, *Type);
	UClass* BotClass = LoadClass<APawn>(nullptr, *ClassPath);
	if (!BotClass)
	{
		ClientMessage(FString::Printf(TEXT("NoFearSpawnBot: class %s not found. Types: Civilian, Monster, AggroMonster, Jumper, Spitter, Tank, Enemy, Ally, Companion"), *ClassPath));
		return;
	}

	// Ставим в ряд перед игроком, лицом к нему.
	FVector ViewLocation;
	FRotator ViewRotation;
	GetPlayerViewPoint(ViewLocation, ViewRotation);
	const FRotator Facing(0.f, ViewRotation.Yaw, 0.f);
	const FVector Forward = Facing.Vector();
	const FVector Right = FRotationMatrix(Facing).GetUnitAxis(EAxis::Y);
	const FRotator BotRotation(0.f, ViewRotation.Yaw + 180.f, 0.f);

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;

	Count = FMath::Clamp(Count, 1, 20);
	for (int32 Index = 0; Index < Count; ++Index)
	{
		const float SideOffset = (Index - (Count - 1) * 0.5f) * 150.f;
		const FVector Location = ControlledPawn->GetActorLocation() + Forward * 400.f + Right * SideOffset + FVector(0.f, 0.f, 10.f);
		World->SpawnActor<APawn>(BotClass, Location, BotRotation, Params);
	}
}

void ANoFearPlayerController::NoFearStressTest(int32 Count, const FString& Mode)
{
	UWorld* World = GetWorld();
	const APawn* ControlledPawn = GetPawn();
	UNavigationSystemV1* NavSystem = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
	if (!World || !ControlledPawn || !NavSystem)
	{
		return;
	}

	// Старые боты (кроме напарников) мешают замеру.
	if (const UNoFearBotSubsystem* Bots = UNoFearBotSubsystem::Get(this))
	{
		for (ANoFearAIController* Bot : Bots->GetBots())
		{
			APawn* BotPawn = Bot ? Bot->GetPawn() : nullptr;
			if (BotPawn && !Bot->IsA<ANoFearCompanionAIController>())
			{
				BotPawn->Destroy();
			}
		}
	}

	Count = FMath::Clamp(Count, 1, 120);
	const float DistanceScale = Mode.Equals(TEXT("calm"), ESearchCase::IgnoreCase) ? 2.5f : 1.f;
	const FVector Origin = ControlledPawn->GetActorLocation();
	const FRotator Facing(0.f, GetControlRotation().Yaw, 0.f);

	struct FGroup
	{
		TArray<FString> Types;
		FName Squad;
		FName Pack;
		/** Направление от игрока (градусы от взгляда) и дальность, см. */
		float Angle;
		float Distance;
		/** Разброс бойцов вокруг центра группы, см. */
		float Spread;
	};
	TArray<FGroup> Groups;
	const TArray<FString> SquadTypes = {TEXT("Enemy"), TEXT("Enemy"), TEXT("Enemy"), TEXT("Enemy"), TEXT("Enemy")};
	Groups.Add({{TEXT("Ally"), TEXT("Ally"), TEXT("Ally"), TEXT("Ally"), TEXT("Ally")}, TEXT("StressAllies"), NAME_None, 160.f, 800.f, 400.f});
	Groups.Add({SquadTypes, TEXT("StressEnemyA"), NAME_None, 0.f, 3000.f, 500.f});
	Groups.Add({SquadTypes, TEXT("StressEnemyB"), NAME_None, -40.f, 3000.f, 500.f});
	Groups.Add({SquadTypes, TEXT("StressEnemyC"), NAME_None, 20.f, 6000.f, 500.f});
	Groups.Add({{TEXT("Monster"), TEXT("Monster"), TEXT("Monster"), TEXT("Jumper"), TEXT("Spitter")}, NAME_None, TEXT("StressPack1"), 80.f, 4000.f, 500.f});
	Groups.Add({{TEXT("Monster"), TEXT("Monster"), TEXT("Monster"), TEXT("Tank"), TEXT("Jumper")}, NAME_None, TEXT("StressPack2"), -130.f, 7000.f, 500.f});
	TArray<FString> Civilians;
	Civilians.Init(TEXT("Civilian"), 10);
	Groups.Add({Civilians, NAME_None, NAME_None, 0.f, 0.f, 0.f});
	// Сверх 40 — ещё вражеские отряды по кругу на 40–80 м.
	for (int32 Extra = 0, Total = 40; Total < Count; ++Extra, Total += 5)
	{
		Groups.Add({SquadTypes, FName(*FString::Printf(TEXT("StressEnemyX%d"), Extra)), NAME_None, Extra * 67.f + 30.f, 4000.f + (Extra % 3) * 2000.f, 500.f});
	}

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
	const ENoFearSquadRole Roles[] = {ENoFearSquadRole::Commander, ENoFearSquadRole::Assault, ENoFearSquadRole::Support, ENoFearSquadRole::Rifleman, ENoFearSquadRole::Rifleman};

	int32 Spawned = 0;
	for (const FGroup& Group : Groups)
	{
		const bool bCivilians = Group.Distance <= 0.f;
		FNavLocation Center;
		const FVector Wanted = Origin + Facing.RotateVector(FRotator(0.f, Group.Angle, 0.f).Vector()) * Group.Distance * DistanceScale;
		if (!bCivilians && !NavSystem->ProjectPointToNavigation(Wanted, Center, FVector(2000.f, 2000.f, 2000.f))
			&& !NavSystem->GetRandomReachablePointInRadius(Origin, Group.Distance * DistanceScale, Center))
		{
			continue;
		}

		for (int32 Index = 0; Index < Group.Types.Num() && Spawned < Count; ++Index)
		{
			const FString& Type = Group.Types[Index];
			UClass* BotClass = LoadClass<APawn>(nullptr, *FString::Printf(TEXT("/Game/Blueprints/Bots/CBP_Bot_%s.CBP_Bot_%s_C"), *Type, *Type));
			if (!BotClass)
			{
				continue;
			}
			FNavLocation Point;
			const bool bFound = bCivilians
				? NavSystem->GetRandomReachablePointInRadius(Origin, 5000.f * DistanceScale, Point) && FVector::Dist2D(Point.Location, Origin) > 1000.f
				: NavSystem->GetRandomReachablePointInRadius(Center.Location, Group.Spread, Point);
			if (!bFound)
			{
				Point.Location = bCivilians ? Origin + FMath::VRand().GetSafeNormal2D() * 1500.f : Center.Location;
			}

			const FVector Location = Point.Location + FVector(0.f, 0.f, 100.f);
			const FRotator Rotation(0.f, (Origin - Location).Rotation().Yaw, 0.f);
			APawn* NewPawn = World->SpawnActorDeferred<APawn>(BotClass, FTransform(Rotation, Location), nullptr, nullptr, Params.SpawnCollisionHandlingOverride);
			if (!NewPawn)
			{
				continue;
			}
			if (ANoFearCharacter* BotCharacter = Cast<ANoFearCharacter>(NewPawn))
			{
				if (!Group.Squad.IsNone())
				{
					BotCharacter->Squad = Group.Squad;
					BotCharacter->SquadRole = Roles[Index % UE_ARRAY_COUNT(Roles)];
				}
				BotCharacter->PackName = Group.Pack;
			}
			NewPawn->FinishSpawning(FTransform(Rotation, Location));
			++Spawned;
		}
	}

	const FString Message = FString::Printf(TEXT("NoFearStressTest: %d bots (%s)"), Spawned, *Mode);
	UE_LOG(LogTemp, Display, TEXT("NOFEAR_STRESS %s"), *Message);
	ClientMessage(Message);
}

void ANoFearPlayerController::NoFearPerf(float Seconds)
{
	if (UNoFearPerfSubsystem* Perf = UNoFearPerfSubsystem::Get(this))
	{
		Perf->StartSample(Seconds);
	}
}

void ANoFearPlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();

	// Колесо приказов: своё действие и контекст, ассеты ввода GASP не трогаем.
	if (!OrderWheelAction)
	{
		OrderWheelAction = NewObject<UInputAction>(this, TEXT("IA_NoFearOrderWheel"));
		OrderWheelAction->ValueType = EInputActionValueType::Boolean;
	}
	if (!CompanionInputContext)
	{
		CompanionInputContext = NewObject<UInputMappingContext>(this, TEXT("IMC_NoFearCompanion"));
		CompanionInputContext->MapKey(OrderWheelAction, EKeys::Tab);
		CompanionInputContext->MapKey(OrderWheelAction, EKeys::Gamepad_DPad_Up);
	}

	if (UEnhancedInputComponent* EnhancedInput = Cast<UEnhancedInputComponent>(InputComponent))
	{
		if (InteractAction)
		{
			// Started — один раз на нажатие, даже если у действия нет триггера Pressed.
			EnhancedInput->BindAction(InteractAction, ETriggerEvent::Started, this, &ANoFearPlayerController::OnInteractInput);
		}
		EnhancedInput->BindAction(OrderWheelAction, ETriggerEvent::Started, this, &ANoFearPlayerController::OnOrderWheelPressed);
		EnhancedInput->BindAction(OrderWheelAction, ETriggerEvent::Completed, this, &ANoFearPlayerController::OnOrderWheelReleased);
		EnhancedInput->BindAction(OrderWheelAction, ETriggerEvent::Canceled, this, &ANoFearPlayerController::OnOrderWheelReleased);
	}
}

void ANoFearPlayerController::PlayerTick(float DeltaTime)
{
	Super::PlayerTick(DeltaTime);

	// Персонаж GASP может пересобрать контексты при вселении — вернуть свой.
	if (UEnhancedInputLocalPlayerSubsystem* Input = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(GetLocalPlayer()))
	{
		if (CompanionInputContext && !Input->HasMappingContext(CompanionInputContext))
		{
			Input->AddMappingContext(CompanionInputContext, 10);
		}
	}

	if (PruneCompanions())
	{
		OnCompanionsChanged.Broadcast();
	}
	UpdateCanBeDowned();
	UpdateFocusedInteractable();
	UpdateRevive(DeltaTime);
	if (bOrderWheelOpen)
	{
		UpdateOrderWheel();
	}
}

// --- Взаимодействие ---

void ANoFearPlayerController::OnInteractInput()
{
	TGuardValue<bool> Guard(bHandlingInteractInput, true);
	InteractWithFocused();
}

bool ANoFearPlayerController::InteractWithFocused()
{
	UpdateFocusedInteractable();
	UNoFearInteractableComponent* Focused = FocusedInteractable.Get();
	return Focused && Focused->Interact(GetPawn());
}

UNoFearInteractableComponent* ANoFearPlayerController::GetFocusedInteractable() const
{
	return FocusedInteractable.Get();
}

void ANoFearPlayerController::UpdateFocusedInteractable()
{
	FocusedInteractable.Reset();

	APawn* User = GetPawn();
	if (!User || bWaitingForRespawn || bOrderWheelOpen || UNoFearHealthComponent::IsActorIncapacitated(User))
	{
		return;
	}

	TArray<UNoFearInteractableComponent*> Interactables;
	UNoFearInteractableComponent::GetInteractables(GetWorld(), Interactables);
	if (Interactables.IsEmpty())
	{
		return;
	}

	FVector ViewLocation;
	FRotator ViewRotation;
	GetPlayerViewPoint(ViewLocation, ViewRotation);
	const FVector ViewDirection = ViewRotation.Vector();
	const float MinDot = FMath::Cos(FMath::DegreesToRadians(InteractionMaxAngle));

	float BestScore = -UE_BIG_NUMBER;
	for (UNoFearInteractableComponent* Interactable : Interactables)
	{
		const AActor* Target = Interactable->GetOwner();
		if (!Target || !Interactable->IsInRange(User) || !Interactable->CanInteract(User))
		{
			continue;
		}

		const float Dot = FVector::DotProduct(ViewDirection, (Target->GetActorLocation() - ViewLocation).GetSafeNormal());
		const float Distance = FVector::Dist(User->GetActorLocation(), Target->GetActorLocation());
		if (Dot < MinDot && !(Distance <= InteractionCloseDistance && Dot > 0.f))
		{
			continue;
		}

		// Важнее то, на что смотрим; среди равных — что ближе.
		const float Score = Dot - 0.25f * Distance / FMath::Max(Interactable->InteractionDistance, 1.f);
		if (Score > BestScore)
		{
			BestScore = Score;
			FocusedInteractable = Interactable;
		}
	}
}

// --- Напарники ---

TArray<APawn*> ANoFearPlayerController::GetCompanions() const
{
	TArray<APawn*> Result;
	for (const TWeakObjectPtr<APawn>& Companion : Companions)
	{
		if (APawn* CompanionPawn = Companion.Get(); CompanionPawn && !UNoFearHealthComponent::IsActorDead(CompanionPawn))
		{
			Result.Add(CompanionPawn);
		}
	}
	return Result;
}

bool ANoFearPlayerController::IsCompanion(const APawn* CompanionPawn) const
{
	return CompanionPawn && Companions.ContainsByPredicate([CompanionPawn](const TWeakObjectPtr<APawn>& Entry)
	{
		return Entry.Get() == CompanionPawn;
	});
}

bool ANoFearPlayerController::RecruitCompanion(APawn* CompanionPawn)
{
	ANoFearCompanionAIController* Companion = GetCompanionController(CompanionPawn);
	if (!Companion || UNoFearHealthComponent::IsActorDead(CompanionPawn) || UNoFearTeamLibrary::AreHostile(CompanionPawn, GetPawn()))
	{
		return false;
	}

	PruneCompanions();
	if (IsCompanion(CompanionPawn))
	{
		SetCompanionFollowing(CompanionPawn, true);
		return true;
	}

	// Лимит занят — самый давний напарник остаётся союзником там, где стоит.
	while (Companions.Num() >= FMath::Max(MaxCompanions, 1))
	{
		ReleaseCompanion(Companions[0].Get());
	}

	Companions.Add(CompanionPawn);
	Companion->SetLeader(this);
	OnCompanionsChanged.Broadcast();
	return true;
}

void ANoFearPlayerController::DismissCompanion(APawn* CompanionPawn)
{
	if (IsCompanion(CompanionPawn))
	{
		ReleaseCompanion(CompanionPawn);
		OnCompanionsChanged.Broadcast();
	}
}

void ANoFearPlayerController::ReleaseCompanion(APawn* CompanionPawn)
{
	Companions.RemoveAll([CompanionPawn](const TWeakObjectPtr<APawn>& Entry)
	{
		return !Entry.IsValid() || Entry.Get() == CompanionPawn;
	});
	if (ANoFearCompanionAIController* Companion = GetCompanionController(CompanionPawn); Companion && Companion->GetLeader() == this)
	{
		Companion->SetLeader(nullptr);
	}
}

void ANoFearPlayerController::SetCompanionFollowing(APawn* CompanionPawn, bool bFollow)
{
	ANoFearCompanionAIController* Companion = IsCompanion(CompanionPawn) ? GetCompanionController(CompanionPawn) : nullptr;
	if (Companion && Companion->IsFollowing() != bFollow)
	{
		Companion->SetFollowing(bFollow);
		OnCompanionsChanged.Broadcast();
	}
}

void ANoFearPlayerController::ToggleCompanionFollowing(APawn* CompanionPawn)
{
	if (const ANoFearCompanionAIController* Companion = IsCompanion(CompanionPawn) ? GetCompanionController(CompanionPawn) : nullptr)
	{
		SetCompanionFollowing(CompanionPawn, !Companion->IsFollowing());
	}
}

bool ANoFearPlayerController::PruneCompanions()
{
	// Погибший напарник не возвращается: его контроллер уничтожается вместе со смертью, слот освобождается.
	for (const TWeakObjectPtr<APawn>& Entry : Companions)
	{
		const ANoFearCharacter* Fallen = Cast<ANoFearCharacter>(Entry.Get());
		if (Fallen && UNoFearHealthComponent::IsActorDead(Fallen))
		{
			AddRadioLine(nullptr, FText::Format(LOCTEXT("CompanionDied", "{0} погиб."), Fallen->GetCharacterDisplayName()), false);
		}
	}
	return Companions.RemoveAll([this](const TWeakObjectPtr<APawn>& Entry)
	{
		const APawn* CompanionPawn = Entry.Get();
		const ANoFearCompanionAIController* Companion = GetCompanionController(CompanionPawn);
		return !CompanionPawn || UNoFearHealthComponent::IsActorDead(CompanionPawn) || !Companion || Companion->GetLeader() != this;
	}) > 0;
}

ANoFearCompanionAIController* ANoFearPlayerController::GetMainCompanion() const
{
	for (const TWeakObjectPtr<APawn>& Entry : Companions)
	{
		APawn* CompanionPawn = Entry.Get();
		ANoFearCompanionAIController* Companion = GetCompanionController(CompanionPawn);
		if (Companion && !UNoFearHealthComponent::IsActorDead(CompanionPawn) && Companion->GetLeader() == this)
		{
			return Companion;
		}
	}
	return nullptr;
}

void ANoFearPlayerController::AddRadioLine(APawn* Speaker, const FText& Text, bool bWhisper)
{
	const double Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
	RadioLines.RemoveAll([this, Now](const FNoFearRadioLine& Line)
	{
		return Now - Line.Time > RadioLineTime;
	});
	while (RadioLines.Num() >= MaxRadioLines)
	{
		RadioLines.RemoveAt(0);
	}

	FNoFearRadioLine& Line = RadioLines.AddDefaulted_GetRef();
	Line.Speaker = Speaker;
	if (const ANoFearCharacter* SpeakerCharacter = Cast<ANoFearCharacter>(Speaker))
	{
		Line.SpeakerName = SpeakerCharacter->GetCharacterDisplayName();
	}
	Line.Text = Text;
	Line.Time = Now;
	Line.bWhisper = bWhisper;
}

void ANoFearPlayerController::UpdateCanBeDowned()
{
	UNoFearHealthComponent* Health = UNoFearHealthComponent::FindHealth(GetPawn());
	if (!Health || Health->IsDead() || Health->IsDowned())
	{
		return;
	}
	// Без напарника на ногах поднимать некому — смерть как раньше.
	const ANoFearCompanionAIController* Companion = GetMainCompanion();
	Health->bCanBeDowned = Companion && !UNoFearHealthComponent::IsActorIncapacitated(Companion->GetPawn());
}

// --- Колесо приказов ---

void ANoFearPlayerController::OnOrderWheelPressed()
{
	OpenOrderWheel();
}

void ANoFearPlayerController::OnOrderWheelReleased()
{
	CloseOrderWheel(true);
}

void ANoFearPlayerController::OpenOrderWheel()
{
	APawn* MyPawn = GetPawn();
	if (bOrderWheelOpen || !MyPawn || bWaitingForRespawn || UNoFearHealthComponent::IsActorIncapacitated(MyPawn))
	{
		return;
	}
	if (!GetMainCompanion())
	{
		AddRadioLine(nullptr, LOCTEXT("NoCompanion", "Нет напарника: подойди к союзнику и нажми E."), false);
		return;
	}

	StopRevive();
	bOrderWheelOpen = true;
	OrderWheelCursor = FVector2D::ZeroVector;
	OrderWheelSelection = INDEX_NONE;
	TimeDilationBeforeWheel = UGameplayStatics::GetGlobalTimeDilation(this);
	UGameplayStatics::SetGlobalTimeDilation(this, OrderWheelTimeDilation);
	// Мышь выбирает сектор, а не крутит камеру.
	SetIgnoreLookInput(true);
	if (const ANoFearCharacter* PlayerCharacter = Cast<ANoFearCharacter>(MyPawn))
	{
		if (UNoFearWeaponComponent* Weapons = PlayerCharacter->GetWeaponComponent())
		{
			Weapons->StopFire();
		}
	}
	UpdateOrderAim();
	BuildOrderWheelEntries();
}

void ANoFearPlayerController::CloseOrderWheel(bool bIssue)
{
	if (!bOrderWheelOpen)
	{
		return;
	}
	bOrderWheelOpen = false;
	UGameplayStatics::SetGlobalTimeDilation(this, TimeDilationBeforeWheel);
	SetIgnoreLookInput(false);

	const int32 Selection = OrderWheelSelection;
	OrderWheelSelection = INDEX_NONE;
	if (bIssue && OrderWheelEntries.IsValidIndex(Selection) && OrderWheelEntries[Selection].bEnabled)
	{
		IssueCompanionOrder(OrderWheelEntries[Selection].Order);
	}
}

void ANoFearPlayerController::UpdateOrderWheel()
{
	const APawn* MyPawn = GetPawn();
	if (!MyPawn || UNoFearHealthComponent::IsActorIncapacitated(MyPawn) || !GetMainCompanion())
	{
		CloseOrderWheel(false);
		return;
	}

	const float MaxLength = OrderWheelDeadZone * 3.f;
	float DeltaX = 0.f;
	float DeltaY = 0.f;
	GetInputMouseDelta(DeltaX, DeltaY);
	OrderWheelCursor += FVector2D(DeltaX, DeltaY);
	float StickX = 0.f;
	float StickY = 0.f;
	GetInputAnalogStickState(EControllerAnalogStick::CAS_RightStick, StickX, StickY);
	if (FVector2D(StickX, StickY).Size() > 0.5f)
	{
		OrderWheelCursor = FVector2D(StickX, StickY).GetSafeNormal() * MaxLength;
	}
	if (OrderWheelCursor.Size() > MaxLength)
	{
		OrderWheelCursor = OrderWheelCursor.GetSafeNormal() * MaxLength;
	}

	UpdateOrderAim();
	BuildOrderWheelEntries();

	const int32 Count = OrderWheelEntries.Num();
	if (Count > 0 && OrderWheelCursor.Size() >= OrderWheelDeadZone)
	{
		// 0° — вверх, по часовой стрелке.
		const float Angle = FMath::RadiansToDegrees(FMath::Atan2(OrderWheelCursor.X, OrderWheelCursor.Y));
		OrderWheelSelection = ((FMath::RoundToInt(Angle / (360.f / Count)) % Count) + Count) % Count;
	}
	else
	{
		OrderWheelSelection = INDEX_NONE;
	}
}

FVector2D ANoFearPlayerController::GetOrderWheelCursor() const
{
	return OrderWheelCursor.Size() >= OrderWheelDeadZone ? OrderWheelCursor.GetSafeNormal() : FVector2D::ZeroVector;
}

bool ANoFearPlayerController::GetOrderAimPoint(FVector& OutPoint) const
{
	OutPoint = OrderAimPoint;
	return bOrderAimValid;
}

void ANoFearPlayerController::UpdateOrderAim()
{
	bOrderAimValid = false;
	OrderAimEnemy = nullptr;
	APawn* MyPawn = GetPawn();
	UWorld* World = GetWorld();
	if (!MyPawn || !World)
	{
		return;
	}

	FVector ViewLocation;
	FRotator ViewRotation;
	GetPlayerViewPoint(ViewLocation, ViewRotation);
	const FVector Direction = ViewRotation.Vector();

	FCollisionQueryParams Params(SCENE_QUERY_STAT(NoFearOrderAim), false);
	IgnoreWithAttached(Params, MyPawn);
	for (const TWeakObjectPtr<APawn>& Companion : Companions)
	{
		IgnoreWithAttached(Params, Companion.Get());
	}

	FHitResult Hit;
	if (World->LineTraceSingleByChannel(Hit, ViewLocation, ViewLocation + Direction * OrderTraceDistance, ECC_Visibility, Params))
	{
		bOrderAimValid = true;
		OrderAimPoint = Hit.ImpactPoint;
		// Попали в оружие в руках — значит, в его владельца.
		AActor* HitActor = Hit.GetActor();
		if (HitActor && !HitActor->IsA<APawn>() && HitActor->GetAttachParentActor())
		{
			HitActor = HitActor->GetAttachParentActor();
		}
		if (IsOrderEnemy(MyPawn, HitActor))
		{
			OrderAimEnemy = HitActor;
		}
	}

	// Капсулы не всегда ловят луч: ближайший к прицелу видимый враг в узком конусе.
	if (!OrderAimEnemy.IsValid())
	{
		const UNoFearBotSubsystem* Bots = UNoFearBotSubsystem::Get(this);
		const float MinDot = FMath::Cos(FMath::DegreesToRadians(OrderAimConeAngle));
		float BestDot = MinDot;
		for (const ANoFearAIController* Bot : Bots ? Bots->GetBots() : TArray<ANoFearAIController*>())
		{
			APawn* BotPawn = Bot ? Bot->GetPawn() : nullptr;
			if (!IsOrderEnemy(MyPawn, BotPawn))
			{
				continue;
			}
			const FVector Target = BotPawn->GetActorLocation() + FVector(0.f, 0.f, 40.f);
			const FVector ToTarget = Target - ViewLocation;
			const float Dot = FVector::DotProduct(ToTarget.GetSafeNormal(), Direction);
			if (Dot <= BestDot || ToTarget.Size() > OrderTraceDistance)
			{
				continue;
			}
			FCollisionQueryParams SightParams = Params;
			IgnoreWithAttached(SightParams, BotPawn);
			if (World->LineTraceTestByChannel(ViewLocation, Target, ECC_Visibility, SightParams))
			{
				continue;
			}
			BestDot = Dot;
			OrderAimEnemy = BotPawn;
		}
	}

	// Никого под прицелом — тот, в кого игрок только что попал.
	if (!OrderAimEnemy.IsValid() && World->GetTimeSeconds() - LastHitEnemyTime <= LastHitEnemyMemory && IsOrderEnemy(MyPawn, LastHitEnemy.Get()))
	{
		OrderAimEnemy = LastHitEnemy;
	}
}

void ANoFearPlayerController::BuildOrderWheelEntries()
{
	const ANoFearCompanionAIController* Companion = GetMainCompanion();
	const ANoFearCharacter* CompanionCharacter = Companion ? Cast<ANoFearCharacter>(Companion->GetPawn()) : nullptr;
	const UNoFearGrenadeComponent* Grenades = CompanionCharacter ? CompanionCharacter->GetGrenadeComponent() : nullptr;
	const int32 GrenadeCount = Grenades ? Grenades->GetGrenadeCount() : 0;
	const APawn* MyPawn = GetPawn();

	OrderWheelEntries.Reset();
	auto Add = [this](ENoFearCompanionOrder Order, const FText& Label, const FText& Detail, bool bEnabled)
	{
		FNoFearOrderWheelEntry& Entry = OrderWheelEntries.AddDefaulted_GetRef();
		Entry.Order = Order;
		Entry.Label = Label;
		Entry.Detail = Detail;
		Entry.bEnabled = bEnabled;
	};

	// По часовой стрелке сверху; противоположные приказы — друг напротив друга.
	Add(ENoFearCompanionOrder::Follow, LOCTEXT("WheelFollow", "За мной"),
		Companion && Companion->IsFollowing() && Companion->GetActiveOrder() == ENoFearCompanionOrder::None ? LOCTEXT("WheelFollowing", "уже идёт") : FText::GetEmpty(), true);

	FText EnemyDetail = LOCTEXT("WheelNoEnemy", "наведи на врага");
	if (const ANoFearCharacter* Enemy = Cast<ANoFearCharacter>(OrderAimEnemy.Get()))
	{
		EnemyDetail = Enemy->GetCharacterDisplayName();
	}
	Add(ENoFearCompanionOrder::Attack, LOCTEXT("WheelAttack", "Атакуй цель"), EnemyDetail, OrderAimEnemy.IsValid());

	FText PointDetail = LOCTEXT("WheelNoPoint", "наведи на место");
	if (bOrderAimValid && MyPawn)
	{
		PointDetail = FText::Format(LOCTEXT("WheelMeters", "{0} м"), FText::AsNumber(FMath::RoundToInt(FVector::Dist(MyPawn->GetActorLocation(), OrderAimPoint) / 100.f)));
	}
	Add(ENoFearCompanionOrder::MoveTo, LOCTEXT("WheelMove", "Иди туда"), PointDetail, bOrderAimValid);

	Add(ENoFearCompanionOrder::Grenade, LOCTEXT("WheelGrenade", "Граната туда"),
		GrenadeCount > 0 ? FText::Format(LOCTEXT("WheelGrenades", "гранат: {0}"), FText::AsNumber(GrenadeCount)) : LOCTEXT("WheelNoGrenades", "гранат нет"),
		GrenadeCount > 0 && bOrderAimValid);

	Add(ENoFearCompanionOrder::Hold, LOCTEXT("WheelHold", "Жди здесь"),
		Companion && !Companion->IsFollowing() ? LOCTEXT("WheelWaiting", "уже ждёт") : FText::GetEmpty(), true);

	const int32 Magazines = Companion ? Companion->GetSupplyMagazines() : 0;
	const int32 Bandages = Companion ? Companion->GetSupplyBandages() : 0;
	Add(ENoFearCompanionOrder::Supply, LOCTEXT("WheelSupply", "Патроны / бинт"),
		FText::Format(LOCTEXT("WheelSupplyDetail", "магазинов {0}, бинтов {1}"), FText::AsNumber(Magazines), FText::AsNumber(Bandages)),
		Magazines + Bandages > 0);

	Add(ENoFearCompanionOrder::ComeHere, LOCTEXT("WheelComeHere", "Ко мне"), FText::GetEmpty(), true);

	const bool bHoldingFire = Companion && Companion->IsHoldingFire();
	Add(ENoFearCompanionOrder::ToggleFire, bHoldingFire ? LOCTEXT("WheelFireAtWill", "Огонь по готовности") : LOCTEXT("WheelHoldFire", "Не стрелять"),
		bHoldingFire ? LOCTEXT("WheelHoldingFire", "сейчас молчит") : FText::GetEmpty(), true);
}

bool ANoFearPlayerController::IssueCompanionOrder(ENoFearCompanionOrder Order)
{
	ANoFearCompanionAIController* Companion = GetMainCompanion();
	const APawn* MyPawn = GetPawn();
	if (!Companion || !MyPawn)
	{
		return false;
	}

	UpdateOrderAim();
	if ((Order == ENoFearCompanionOrder::MoveTo || Order == ENoFearCompanionOrder::Grenade) && !bOrderAimValid)
	{
		AddRadioLine(Companion->GetPawn(), LOCTEXT("OrderNoPoint", "Не вижу, куда."), false);
		return false;
	}
	const FVector Point = bOrderAimValid ? OrderAimPoint : MyPawn->GetActorLocation();
	const bool bAccepted = Companion->IssueOrder(Order, Point, OrderAimEnemy.Get());
	if (bAccepted)
	{
		OnCompanionsChanged.Broadcast();
	}
	return bAccepted;
}

// --- Подъём упавшего ---

bool ANoFearPlayerController::StartRevive(APawn* DownedPawn)
{
	APawn* MyPawn = GetPawn();
	if (!MyPawn || !DownedPawn || DownedPawn == MyPawn || !UNoFearHealthComponent::IsActorDowned(DownedPawn)
		|| UNoFearHealthComponent::IsActorIncapacitated(MyPawn) || UNoFearTeamLibrary::AreHostile(MyPawn, DownedPawn))
	{
		return false;
	}
	if (ReviveTarget.Get() == DownedPawn)
	{
		return true;
	}

	StopRevive();
	ReviveTarget = DownedPawn;
	ReviveElapsed = 0.f;
	bReviveHeldByInput = bHandlingInteractInput;
	ReviveKeys.Reset();
	if (bReviveHeldByInput)
	{
		if (const UEnhancedInputLocalPlayerSubsystem* Input = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(GetLocalPlayer()); Input && InteractAction)
		{
			ReviveKeys = Input->QueryKeysMappedToAction(InteractAction);
		}
		if (ReviveKeys.IsEmpty())
		{
			ReviveKeys.Add(EKeys::E);
		}
	}
	// Поднимает на месте и не стреляет.
	SetIgnoreMoveInput(true);
	bReviveBlocksMove = true;
	if (const ANoFearCharacter* PlayerCharacter = Cast<ANoFearCharacter>(MyPawn))
	{
		if (UNoFearWeaponComponent* Weapons = PlayerCharacter->GetWeaponComponent())
		{
			Weapons->StopFire();
		}
	}
	return true;
}

void ANoFearPlayerController::StopRevive()
{
	ReviveTarget = nullptr;
	ReviveElapsed = 0.f;
	bReviveHeldByInput = false;
	if (bReviveBlocksMove)
	{
		bReviveBlocksMove = false;
		SetIgnoreMoveInput(false);
	}
}

void ANoFearPlayerController::UpdateRevive(float DeltaTime)
{
	if (!ReviveTarget.IsValid())
	{
		if (bReviveBlocksMove)
		{
			StopRevive();
		}
		return;
	}

	APawn* MyPawn = GetPawn();
	APawn* Target = ReviveTarget.Get();
	UNoFearHealthComponent* TargetHealth = UNoFearHealthComponent::FindHealth(Target);
	const UNoFearHealthComponent* MyHealth = UNoFearHealthComponent::FindHealth(MyPawn);
	bool bHeld = !bReviveHeldByInput;
	for (const FKey& Key : ReviveKeys)
	{
		bHeld = bHeld || IsInputKeyDown(Key);
	}
	const bool bInRange = MyPawn && FVector::Dist(MyPawn->GetActorLocation(), Target->GetActorLocation()) <= 350.f;
	if (!bHeld || !bInRange || !TargetHealth || !TargetHealth->IsDowned() || !MyHealth || MyHealth->IsDead() || MyHealth->IsDowned())
	{
		StopRevive();
		return;
	}

	// Ранили во время подъёма — начинать заново.
	if (MyHealth->GetTimeSinceLastDamage() < DeltaTime + 0.05f)
	{
		ReviveElapsed = 0.f;
	}
	else
	{
		ReviveElapsed += DeltaTime;
	}
	TargetHealth->NotifyReviveProgress(GetReviveProgress());
	if (ReviveElapsed >= ReviveHoldTime)
	{
		TargetHealth->Revive(MyPawn);
		StopRevive();
	}
}

// --- Консоль ---

void ANoFearPlayerController::NoFearDownCompanion()
{
	const ANoFearCompanionAIController* Companion = GetMainCompanion();
	APawn* CompanionPawn = Companion ? Companion->GetPawn() : nullptr;
	UNoFearHealthComponent* Health = UNoFearHealthComponent::FindHealth(CompanionPawn);
	if (!Health || Health->IsDowned())
	{
		ClientMessage(TEXT("NoFearDownCompanion: no standing companion"));
		return;
	}
	Health->bCanBeDowned = true;
	UGameplayStatics::ApplyDamage(CompanionPawn, Health->GetHealth() + 10.f, nullptr, nullptr, UDamageType::StaticClass());
}

void ANoFearPlayerController::NoFearDownSelf()
{
	UpdateCanBeDowned();
	const UNoFearHealthComponent* Health = UNoFearHealthComponent::FindHealth(GetPawn());
	if (Health && !Health->IsDowned())
	{
		UGameplayStatics::ApplyDamage(GetPawn(), Health->GetHealth() + 10.f, nullptr, nullptr, UDamageType::StaticClass());
	}
}

void ANoFearPlayerController::NoFearOrder(const FString& Order)
{
	static const TMap<FString, ENoFearCompanionOrder> Orders = {
		{TEXT("Follow"), ENoFearCompanionOrder::Follow},
		{TEXT("Hold"), ENoFearCompanionOrder::Hold},
		{TEXT("Move"), ENoFearCompanionOrder::MoveTo},
		{TEXT("Come"), ENoFearCompanionOrder::ComeHere},
		{TEXT("Attack"), ENoFearCompanionOrder::Attack},
		{TEXT("Grenade"), ENoFearCompanionOrder::Grenade},
		{TEXT("Fire"), ENoFearCompanionOrder::ToggleFire},
		{TEXT("Supply"), ENoFearCompanionOrder::Supply},
	};
	for (const TPair<FString, ENoFearCompanionOrder>& Pair : Orders)
	{
		if (Pair.Key.Equals(Order, ESearchCase::IgnoreCase))
		{
			IssueCompanionOrder(Pair.Value);
			return;
		}
	}
	ClientMessage(TEXT("NoFearOrder: Follow, Hold, Move, Come, Attack, Grenade, Fire, Supply"));
}

void ANoFearPlayerController::NoFearOrderWheel(int32 Open, int32 Sector)
{
	if (Open == 0)
	{
		CloseOrderWheel(false);
		return;
	}
	OpenOrderWheel();
	const int32 Count = OrderWheelEntries.Num();
	if (bOrderWheelOpen && Sector >= 0 && Count > 0)
	{
		// Курсор в середину сектора: дальше UpdateOrderWheel выберет его сам.
		const float Angle = FMath::DegreesToRadians(360.f / Count * (Sector % Count));
		OrderWheelCursor = FVector2D(FMath::Sin(Angle), FMath::Cos(Angle)) * OrderWheelDeadZone * 3.f;
		OrderWheelSelection = Sector % Count;
	}
}

#undef LOCTEXT_NAMESPACE
