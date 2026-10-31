#include "AI/NoFearSquadSubsystem.h"

#include "AI/NoFearBotSubsystem.h"
#include "AI/NoFearCompanionAIController.h"
#include "AI/NoFearShooterAIController.h"
#include "Core/NoFearCharacter.h"
#include "Core/NoFearHealthComponent.h"
#include "Engine/World.h"

namespace
{
	/** Роль с тела: пусто — стрелок. */
	ENoFearSquadRole GetBodyRole(const APawn* Pawn)
	{
		const ANoFearCharacter* Character = Cast<ANoFearCharacter>(Pawn);
		const ENoFearSquadRole Role = Character ? Character->SquadRole : ENoFearSquadRole::None;
		return Role == ENoFearSquadRole::None ? ENoFearSquadRole::Rifleman : Role;
	}
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

void UNoFearSquadSubsystem::Tick(float DeltaTime)
{
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

	// Выбывшие: погибли, сменили отряд или команду, ушли из-под контроллера.
	for (FSquad& Squad : Squads)
	{
		Squad.Members.RemoveAll([&Squad](const FMember& Member)
		{
			const ANoFearShooterAIController* Bot = Member.Bot.Get();
			const ANoFearCharacter* Character = Cast<ANoFearCharacter>(Member.Pawn.Get());
			return !Bot || !Character || Bot->GetPawn() != Character || Character->IsDead()
				|| Character->Squad != Squad.Name || Character->Team != Squad.Team;
		});
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
			for (FMember& Member : Squad.Members)
			{
				Member.JoinTime = Now;
			}
		}
		AssignRoles(Squad, Now);
		ShareIntel(Squad, Now);
	}
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
	// Кого видят бойцы.
	for (const FMember& Member : Squad.Members)
	{
		const ANoFearShooterAIController* Bot = Member.Bot.Get();
		if (!Bot)
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
		if (!Enemy || UNoFearHealthComponent::IsActorDead(Enemy) || Now - It.Value().LastSeenTime > EnemyMemoryTime)
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
			if (!Bot)
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
	const FMember* Member = nullptr;
	if (FindSquadOf(Pawn, &Member))
	{
		const_cast<FMember*>(Member)->bExecutingOrder = bExecuting;
	}
}

bool UNoFearSquadSubsystem::GetMemberView(const APawn* Pawn, FNoFearSquadMemberView& OutView) const
{
	const FMember* Member = nullptr;
	const FSquad* Squad = FindSquadOf(Pawn, &Member);
	if (!Squad)
	{
		return false;
	}
	OutView.Role = Member->Role;
	OutView.PreviousRole = Member->PreviousRole;
	OutView.JoinTime = Member->JoinTime;
	OutView.RoleChangeTime = Member->RoleChangeTime;
	OutView.CommanderChangeTime = Squad->CommanderChangeTime;
	OutView.Commander = Squad->Commander.Get();
	OutView.SquadSize = Squad->Members.Num();
	OutView.bExecutingOrder = Member->bExecutingOrder;
	return true;
}
