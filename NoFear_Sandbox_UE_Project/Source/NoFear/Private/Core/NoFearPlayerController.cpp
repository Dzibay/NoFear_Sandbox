#include "Core/NoFearPlayerController.h"

#include "AI/NoFearCompanionAIController.h"
#include "Core/NoFearInteractableComponent.h"
#include "Core/NoFearTeam.h"
#include "EnhancedInputComponent.h"
#include "Engine/World.h"
#include "GameFramework/DamageType.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/PlayerStart.h"
#include "InputAction.h"
#include "Kismet/GameplayStatics.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	ANoFearCompanionAIController* GetCompanionController(const APawn* Pawn)
	{
		return Pawn ? Cast<ANoFearCompanionAIController>(Pawn->GetController()) : nullptr;
	}
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
		ClientMessage(FString::Printf(TEXT("NoFearSpawnBot: class %s not found. Types: Civilian, Monster, AggroMonster, Enemy, Ally, Companion"), *ClassPath));
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

void ANoFearPlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();

	if (UEnhancedInputComponent* EnhancedInput = Cast<UEnhancedInputComponent>(InputComponent); EnhancedInput && InteractAction)
	{
		// Started — один раз на нажатие, даже если у действия нет триггера Pressed.
		EnhancedInput->BindAction(InteractAction, ETriggerEvent::Started, this, &ANoFearPlayerController::OnInteractInput);
	}
}

void ANoFearPlayerController::PlayerTick(float DeltaTime)
{
	Super::PlayerTick(DeltaTime);

	if (PruneCompanions())
	{
		OnCompanionsChanged.Broadcast();
	}
	UpdateFocusedInteractable();
}

// --- Взаимодействие ---

void ANoFearPlayerController::OnInteractInput()
{
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
	if (!User || bWaitingForRespawn || UNoFearHealthComponent::IsActorDead(User))
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
	return Companions.RemoveAll([this](const TWeakObjectPtr<APawn>& Entry)
	{
		const APawn* CompanionPawn = Entry.Get();
		const ANoFearCompanionAIController* Companion = GetCompanionController(CompanionPawn);
		return !CompanionPawn || UNoFearHealthComponent::IsActorDead(CompanionPawn) || !Companion || Companion->GetLeader() != this;
	}) > 0;
}
