#include "Core/NoFearPlayerController.h"

#include "Core/NoFearTeam.h"
#include "Engine/World.h"
#include "GameFramework/DamageType.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/PlayerStart.h"
#include "Kismet/GameplayStatics.h"
#include "TimerManager.h"

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
