#include "AI/NoFearBotSubsystem.h"

#include "AI/NoFearAIController.h"
#include "Core/NoFearCharacter.h"
#include "Core/NoFearHealthComponent.h"
#include "Core/NoFearTeam.h"
#include "Weapons/NoFearGrenade.h"
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

void UNoFearBotSubsystem::RegisterGrenade(ANoFearGrenade* Grenade)
{
	if (Grenade)
	{
		Grenades.RemoveAll([](const TWeakObjectPtr<ANoFearGrenade>& Entry) { return !Entry.IsValid(); });
		Grenades.AddUnique(Grenade);
	}
}

void UNoFearBotSubsystem::UnregisterGrenade(ANoFearGrenade* Grenade)
{
	Grenades.RemoveAll([Grenade](const TWeakObjectPtr<ANoFearGrenade>& Entry) { return !Entry.IsValid() || Entry.Get() == Grenade; });
}

TArray<ANoFearGrenade*> UNoFearBotSubsystem::GetLiveGrenades() const
{
	TArray<ANoFearGrenade*> Result;
	for (const TWeakObjectPtr<ANoFearGrenade>& Entry : Grenades)
	{
		ANoFearGrenade* Grenade = Entry.Get();
		if (Grenade && !Grenade->HasExploded())
		{
			Result.Add(Grenade);
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

		// У монстров слух острее: тот же шум они слышат дальше.
		const float HeardRange = Range * Bot->GetHearingRangeScale();
		const float Distance = FVector::Dist(Listener->GetActorLocation(), Location);
		if (Distance > HeardRange)
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
			if (World->LineTraceTestByChannel(Location, Listener->GetActorLocation(), ECC_Visibility, Params) && Distance > HeardRange * WallNoiseFactor)
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

// --- Жетоны атаки ---

void UNoFearBotSubsystem::PruneTokens(FAttackTokens& Tokens, double Now) const
{
	Tokens.Holders.RemoveAll([](const TWeakObjectPtr<AActor>& Holder)
	{
		return !Holder.IsValid() || UNoFearHealthComponent::IsActorDead(Holder.Get());
	});
	for (auto It = Tokens.Waiting.CreateIterator(); It; ++It)
	{
		// Ожидание без повторной просьбы больше секунды — монстр, видимо, уже занят другим.
		if (!It.Key().IsValid() || Now - It.Value() > 1.0)
		{
			It.RemoveCurrent();
		}
	}
	// Долго держит и не бьёт, а другие ждут — жетон отбирается; вернуть его сразу тот не сможет.
	if (!Tokens.Waiting.IsEmpty())
	{
		for (int32 Index = Tokens.Holders.Num() - 1; Index >= 0; --Index)
		{
			const TWeakObjectPtr<AActor> Holder = Tokens.Holders[Index];
			const double* Since = Tokens.HeldSince.Find(Holder);
			if (Since && Now - *Since > TokenHoldTimeout)
			{
				Tokens.Holders.RemoveAt(Index);
				Tokens.PassedUntil.Add(Holder, Now + TokenPassDelay);
			}
		}
	}
	for (auto It = Tokens.HeldSince.CreateIterator(); It; ++It)
	{
		if (!Tokens.Holders.Contains(It.Key()))
		{
			It.RemoveCurrent();
		}
	}
	for (auto It = Tokens.PassedUntil.CreateIterator(); It; ++It)
	{
		if (!It.Key().IsValid() || Now >= It.Value())
		{
			It.RemoveCurrent();
		}
	}
}

bool UNoFearBotSubsystem::RequestAttackToken(AActor* Attacker, AActor* Target)
{
	if (!Attacker || !Target)
	{
		return false;
	}
	const double Now = GetWorld()->GetTimeSeconds();
	FAttackTokens& Tokens = AttackTokens.FindOrAdd(Target);
	PruneTokens(Tokens, Now);

	if (Tokens.Holders.Contains(Attacker))
	{
		return true;
	}
	const bool bPassed = Tokens.PassedUntil.Contains(Attacker);
	if (!bPassed && Tokens.Holders.Num() < MaxAttackersPerTarget)
	{
		Tokens.Holders.Add(Attacker);
		Tokens.HeldSince.Add(Attacker, Now);
		Tokens.Waiting.Remove(Attacker);
		return true;
	}
	Tokens.Waiting.Add(Attacker, Now);
	return false;
}

void UNoFearBotSubsystem::ReleaseAttackToken(AActor* Attacker, AActor* Target, bool bPassOn)
{
	FAttackTokens* Tokens = Target ? AttackTokens.Find(Target) : nullptr;
	if (!Attacker || !Tokens || Tokens->Holders.Remove(Attacker) == 0)
	{
		return;
	}
	const double Now = GetWorld()->GetTimeSeconds();
	PruneTokens(*Tokens, Now);
	// Уступаем, только если жетона действительно кто-то ждёт: одинокий монстр бьёт без пауз.
	if (bPassOn && Tokens->Waiting.Num() > 0)
	{
		Tokens->PassedUntil.Add(Attacker, Now + TokenPassDelay);
	}
}

void UNoFearBotSubsystem::ReleaseAllAttackTokens(AActor* Attacker)
{
	for (auto It = AttackTokens.CreateIterator(); It; ++It)
	{
		It.Value().Holders.Remove(Attacker);
		It.Value().Waiting.Remove(Attacker);
		if (!It.Key().IsValid() || (It.Value().Holders.IsEmpty() && It.Value().Waiting.IsEmpty() && It.Value().PassedUntil.IsEmpty()))
		{
			It.RemoveCurrent();
		}
	}
}

bool UNoFearBotSubsystem::HasAttackToken(const AActor* Attacker, const AActor* Target) const
{
	const FAttackTokens* Tokens = Target ? AttackTokens.Find(Target) : nullptr;
	return Tokens && Tokens->Holders.Contains(Attacker);
}

int32 UNoFearBotSubsystem::GetAttackTokenCount(const AActor* Target) const
{
	const FAttackTokens* Tokens = Target ? AttackTokens.Find(Target) : nullptr;
	if (!Tokens)
	{
		return 0;
	}
	int32 Count = 0;
	for (const TWeakObjectPtr<AActor>& Holder : Tokens->Holders)
	{
		Count += Holder.IsValid() && !UNoFearHealthComponent::IsActorDead(Holder.Get()) ? 1 : 0;
	}
	return Count;
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

	// Драку видят и посторонние (мирные пугаются).
	ReportDanger(Victim, ENoFearDangerType::Fight, Attacker, Victim->GetActorLocation(), FightNoticeRadius, Attacker->GetActorLocation());
}

void UNoFearBotSubsystem::ReportDanger(const UObject* WorldContextObject, ENoFearDangerType Type, AActor* Source, FVector Origin, float Radius, FVector ThreatLocation)
{
	UNoFearBotSubsystem* Subsystem = Get(WorldContextObject);
	if (!Subsystem || Radius <= 0.f)
	{
		return;
	}
	const float RadiusSquared = FMath::Square(Radius);
	for (ANoFearAIController* Bot : Subsystem->GetBots())
	{
		const APawn* Listener = Bot->GetPawn();
		if (!Listener || Listener == Source || UNoFearHealthComponent::IsActorDead(Listener)
			|| FVector::DistSquared(Listener->GetActorLocation(), Origin) > RadiusSquared)
		{
			continue;
		}
		Bot->HandleDanger(Type, Source, ThreatLocation);
	}
}

void UNoFearBotSubsystem::ReportBulletPath(const UObject* WorldContextObject, AActor* Shooter, const FVector& Start, const FVector& End)
{
	UNoFearBotSubsystem* Subsystem = Get(WorldContextObject);
	if (!Subsystem)
	{
		return;
	}
	const FVector ShooterLocation = Shooter ? Shooter->GetActorLocation() : Start;
	for (ANoFearAIController* Bot : Subsystem->GetBots())
	{
		const APawn* Listener = Bot->GetPawn();
		if (!Listener || Listener == Shooter || UNoFearHealthComponent::IsActorDead(Listener))
		{
			continue;
		}
		const FVector Closest = FMath::ClosestPointOnSegment(Listener->GetActorLocation(), Start, End);
		if (FVector::DistSquared(Closest, Listener->GetActorLocation()) <= FMath::Square(NearMissRadius))
		{
			Bot->HandleDanger(ENoFearDangerType::NearMiss, Shooter, ShooterLocation);
		}
	}
}

void UNoFearBotSubsystem::ReserveCover(AActor* Owner, const FVector& Location)
{
	if (Owner)
	{
		CoverReservations.Add(Owner, Location);
	}
}

void UNoFearBotSubsystem::ReleaseCover(AActor* Owner)
{
	CoverReservations.Remove(Owner);
}

bool UNoFearBotSubsystem::IsCoverReserved(const FVector& Location, float Radius, const AActor* Except) const
{
	const float RadiusSquared = FMath::Square(Radius);
	for (const TPair<TWeakObjectPtr<AActor>, FVector>& Pair : CoverReservations)
	{
		const AActor* Owner = Pair.Key.Get();
		// Бронь погибшего или исчезнувшего не держит точку (снимется при следующей брони или уходе).
		if (!Owner || Owner == Except || UNoFearHealthComponent::IsActorDead(Owner))
		{
			continue;
		}
		if (FVector::DistSquared2D(Pair.Value, Location) < RadiusSquared)
		{
			return true;
		}
	}
	return false;
}

bool UNoFearBotSubsystem::GetReservedCover(const AActor* Owner, FVector& OutLocation) const
{
	if (const FVector* Location = CoverReservations.Find(TWeakObjectPtr<AActor>(const_cast<AActor*>(Owner))))
	{
		OutLocation = *Location;
		return true;
	}
	return false;
}

int32 UNoFearBotSubsystem::GetReservedCoverCount() const
{
	int32 Count = 0;
	for (const TPair<TWeakObjectPtr<AActor>, FVector>& Pair : CoverReservations)
	{
		if (Pair.Key.IsValid() && !UNoFearHealthComponent::IsActorDead(Pair.Key.Get()))
		{
			++Count;
		}
	}
	return Count;
}
