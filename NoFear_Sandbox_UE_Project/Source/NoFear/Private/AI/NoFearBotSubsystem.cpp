#include "AI/NoFearBotSubsystem.h"

#include "AI/NoFearAIController.h"
#include "Core/NoFearCharacter.h"
#include "Core/NoFearHealthComponent.h"
#include "Core/NoFearTeam.h"
#include "Engine/Engine.h"
#include "Engine/World.h"

namespace
{
	/** Ближе этого расстояния стена между источником шума и слушателем не проверяется. */
	constexpr float WallCheckMinDistance = 200.f;
}

UNoFearBotSubsystem* UNoFearBotSubsystem::Get(const UObject* WorldContextObject)
{
	const UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull) : nullptr;
	return World ? World->GetSubsystem<UNoFearBotSubsystem>() : nullptr;
}

void UNoFearBotSubsystem::RegisterBot(ANoFearAIController* Bot)
{
	if (Bot)
	{
		Bots.RemoveAll([](const TWeakObjectPtr<ANoFearAIController>& Entry) { return !Entry.IsValid(); });
		Bots.AddUnique(Bot);
	}
}

void UNoFearBotSubsystem::UnregisterBot(ANoFearAIController* Bot)
{
	Bots.RemoveAll([Bot](const TWeakObjectPtr<ANoFearAIController>& Entry) { return !Entry.IsValid() || Entry.Get() == Bot; });
}

TArray<ANoFearAIController*> UNoFearBotSubsystem::GetBots() const
{
	TArray<ANoFearAIController*> Result;
	Result.Reserve(Bots.Num());
	for (const TWeakObjectPtr<ANoFearAIController>& Entry : Bots)
	{
		ANoFearAIController* Bot = Entry.Get();
		if (Bot && Bot->GetPawn())
		{
			Result.Add(Bot);
		}
	}
	return Result;
}

void UNoFearBotSubsystem::RegisterCorpse(ANoFearCharacter* Character)
{
	if (Character)
	{
		Corpses.RemoveAll([](const TWeakObjectPtr<ANoFearCharacter>& Entry) { return !Entry.IsValid(); });
		Corpses.AddUnique(Character);
	}
}

TArray<ANoFearCharacter*> UNoFearBotSubsystem::GetCorpses() const
{
	TArray<ANoFearCharacter*> Result;
	Result.Reserve(Corpses.Num());
	for (const TWeakObjectPtr<ANoFearCharacter>& Entry : Corpses)
	{
		if (ANoFearCharacter* Corpse = Entry.Get())
		{
			Result.Add(Corpse);
		}
	}
	return Result;
}

void UNoFearBotSubsystem::ReportNoise(const UObject* WorldContextObject, AActor* Source, FVector Location, float Range, ENoFearNoiseType Type)
{
	UNoFearBotSubsystem* Subsystem = Get(WorldContextObject);
	UWorld* World = Subsystem ? Subsystem->GetWorld() : nullptr;
	if (!World || !Source || Range <= 0.f)
	{
		return;
	}

	// Стена глушит шаги и приземление, но не выстрелы.
	const bool bMuffledByWalls = Type == ENoFearNoiseType::Footstep || Type == ENoFearNoiseType::Landing;

	for (ANoFearAIController* Bot : Subsystem->GetBots())
	{
		const APawn* Listener = Bot->GetPawn();
		if (!Listener || Listener == Source || UNoFearHealthComponent::IsActorDead(Listener) || !UNoFearTeamLibrary::AreHostile(Listener, Source))
		{
			continue;
		}

		const float Distance = FVector::Dist(Listener->GetActorLocation(), Location);
		if (Distance > Range)
		{
			continue;
		}
		if (bMuffledByWalls && Distance > WallCheckMinDistance)
		{
			// Оружие на теле — отдельные акторы; они стену не изображают.
			FCollisionQueryParams Params(SCENE_QUERY_STAT(NoFearNoiseWall), false, Source);
			Params.AddIgnoredActor(Listener);
			TArray<AActor*> Attached;
			Source->GetAttachedActors(Attached);
			Listener->GetAttachedActors(Attached, /*bResetArray=*/false);
			Params.AddIgnoredActors(Attached);
			if (World->LineTraceTestByChannel(Location, Listener->GetActorLocation(), ECC_Visibility, Params) && Distance > Range * WallNoiseFactor)
			{
				continue;
			}
		}
		Bot->HearNoise(Source, Location, Type);
	}
}

void UNoFearBotSubsystem::BroadcastAlarm(const ANoFearAIController* Origin, AActor* Threat, const FVector& ThreatLocation)
{
	const APawn* OriginPawn = Origin ? Origin->GetPawn() : nullptr;
	if (!OriginPawn || !Threat)
	{
		return;
	}

	const float RadiusSquared = FMath::Square(Origin->GetAlertShareRadius());
	for (ANoFearAIController* Bot : GetBots())
	{
		const APawn* Receiver = Bot->GetPawn();
		if (Bot == Origin || !Receiver || !UNoFearTeamLibrary::AreFriendly(OriginPawn, Receiver))
		{
			continue;
		}
		if (FVector::DistSquared(OriginPawn->GetActorLocation(), Receiver->GetActorLocation()) <= RadiusSquared)
		{
			Bot->ReceiveAlarm(Threat, ThreatLocation);
		}
	}
}

void UNoFearBotSubsystem::NotifyAttack(AActor* Victim, AActor* Attacker)
{
	if (!Victim || !Attacker || Victim == Attacker)
	{
		return;
	}

	for (ANoFearAIController* Bot : GetBots())
	{
		const APawn* Witness = Bot->GetPawn();
		const float Radius = Bot->GetAlertShareRadius();
		if (!Witness || Witness == Victim || Witness == Attacker || Radius <= 0.f
			|| FVector::DistSquared(Witness->GetActorLocation(), Victim->GetActorLocation()) > FMath::Square(Radius))
		{
			continue;
		}

		if (UNoFearTeamLibrary::AreFriendly(Witness, Victim) && UNoFearTeamLibrary::AreHostile(Witness, Attacker))
		{
			// Напали на своего: нападавший — враг.
			Bot->ReceiveAlarm(Attacker, Attacker->GetActorLocation());
		}
		else if (UNoFearTeamLibrary::AreFriendly(Witness, Attacker) && UNoFearTeamLibrary::AreHostile(Witness, Victim))
		{
			// Свой напал на врага: помогаем.
			Bot->ReceiveAlarm(Victim, Victim->GetActorLocation());
		}
	}
}
