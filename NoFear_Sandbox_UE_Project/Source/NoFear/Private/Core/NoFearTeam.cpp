#include "Core/NoFearTeam.h"

#include "GameFramework/Controller.h"
#include "GameFramework/Pawn.h"

UNoFearTeamSettings::UNoFearTeamSettings()
{
	using enum ENoFearTeam;
	const ETeamAttitude::Type Hostile = ETeamAttitude::Hostile;

	Relations = {
		FNoFearTeamRelation(Player, Ally, ETeamAttitude::Friendly),

		FNoFearTeamRelation(Player, Enemy, Hostile),
		FNoFearTeamRelation(Player, Monster, Hostile),
		FNoFearTeamRelation(Player, AggroMonster, Hostile),
		FNoFearTeamRelation(Ally, Enemy, Hostile),
		FNoFearTeamRelation(Ally, Monster, Hostile),
		FNoFearTeamRelation(Ally, AggroMonster, Hostile),

		FNoFearTeamRelation(Enemy, Monster, Hostile),
		FNoFearTeamRelation(Enemy, AggroMonster, Hostile),
		FNoFearTeamRelation(Monster, AggroMonster, Hostile),

		FNoFearTeamRelation(Monster, Civilian, Hostile),
		FNoFearTeamRelation(AggroMonster, Civilian, Hostile),
	};
	SelfHostileTeams = { AggroMonster };
}

ETeamAttitude::Type UNoFearTeamSettings::GetAttitude(ENoFearTeam A, ENoFearTeam B) const
{
	if (A == ENoFearTeam::Neutral || B == ENoFearTeam::Neutral)
	{
		return ETeamAttitude::Neutral;
	}
	if (A == B)
	{
		return SelfHostileTeams.Contains(A) ? ETeamAttitude::Hostile : ETeamAttitude::Friendly;
	}
	// Записей десяток, а спрашивают редко (восприятие, выбор цели) — линейного поиска хватает.
	for (const FNoFearTeamRelation& Relation : Relations)
	{
		if ((Relation.TeamA == A && Relation.TeamB == B) || (Relation.TeamA == B && Relation.TeamB == A))
		{
			return Relation.Attitude;
		}
	}
	return DefaultAttitude;
}

ETeamAttitude::Type UNoFearTeamLibrary::GetAttitude(FGenericTeamId A, FGenericTeamId B)
{
	if (A == FGenericTeamId::NoTeam || B == FGenericTeamId::NoTeam)
	{
		return ETeamAttitude::Neutral;
	}
	return GetDefault<UNoFearTeamSettings>()->GetAttitude(static_cast<ENoFearTeam>(A.GetId()), static_cast<ENoFearTeam>(B.GetId()));
}

TEnumAsByte<ETeamAttitude::Type> UNoFearTeamLibrary::GetTeamAttitude(ENoFearTeam A, ENoFearTeam B)
{
	return GetDefault<UNoFearTeamSettings>()->GetAttitude(A, B);
}

TEnumAsByte<ETeamAttitude::Type> UNoFearTeamLibrary::GetActorAttitude(const AActor* A, const AActor* B)
{
	if (!A || !B)
	{
		return ETeamAttitude::Neutral;
	}
	return GetTeamAttitude(GetTeam(A), GetTeam(B));
}

ENoFearTeam UNoFearTeamLibrary::GetTeam(const AActor* Actor)
{
	if (!Actor)
	{
		return ENoFearTeam::Neutral;
	}

	if (const IGenericTeamAgentInterface* Agent = Cast<IGenericTeamAgentInterface>(Actor))
	{
		return static_cast<ENoFearTeam>(Agent->GetGenericTeamId().GetId());
	}

	// Оружие, снаряды: берём команду того, кто ими владеет.
	if (const APawn* InstigatorPawn = Actor->GetInstigator(); InstigatorPawn && InstigatorPawn != Actor)
	{
		return GetTeam(InstigatorPawn);
	}
	if (const AActor* Owner = Actor->GetOwner(); Owner && Owner != Actor)
	{
		return GetTeam(Owner);
	}
	return ENoFearTeam::Neutral;
}

bool UNoFearTeamLibrary::AreHostile(const AActor* A, const AActor* B)
{
	return A && B && GetActorAttitude(A, B) == ETeamAttitude::Hostile;
}

bool UNoFearTeamLibrary::AreFriendly(const AActor* A, const AActor* B)
{
	return A && B && GetActorAttitude(A, B) == ETeamAttitude::Friendly;
}
