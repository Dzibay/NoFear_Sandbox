#include "Core/NoFearTeam.h"

#include "GameFramework/Controller.h"
#include "GameFramework/Pawn.h"

ETeamAttitude::Type UNoFearTeamLibrary::GetAttitude(FGenericTeamId A, FGenericTeamId B)
{
	if (A == FGenericTeamId::NoTeam || B == FGenericTeamId::NoTeam)
	{
		return ETeamAttitude::Neutral;
	}
	return A == B ? ETeamAttitude::Friendly : ETeamAttitude::Hostile;
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
	return GetAttitude(FGenericTeamId(static_cast<uint8>(GetTeam(A))), FGenericTeamId(static_cast<uint8>(GetTeam(B)))) == ETeamAttitude::Hostile;
}

bool UNoFearTeamLibrary::AreFriendly(const AActor* A, const AActor* B)
{
	return GetAttitude(FGenericTeamId(static_cast<uint8>(GetTeam(A))), FGenericTeamId(static_cast<uint8>(GetTeam(B)))) == ETeamAttitude::Friendly;
}
