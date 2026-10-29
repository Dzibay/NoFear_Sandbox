#include "Core/NoFearCharacter.h"

#include "GameFramework/PlayerController.h"

ANoFearCharacter::ANoFearCharacter()
{
}

void ANoFearCharacter::SetTeam(ENoFearTeam NewTeam)
{
	Team = NewTeam;
}

void ANoFearCharacter::SetGenericTeamId(const FGenericTeamId& NewTeamId)
{
	SetTeam(static_cast<ENoFearTeam>(NewTeamId.GetId()));
}

FGenericTeamId ANoFearCharacter::GetGenericTeamId() const
{
	return FGenericTeamId(static_cast<uint8>(Team));
}

void ANoFearCharacter::PossessedBy(AController* NewController)
{
	Super::PossessedBy(NewController);

	if (NewController && NewController->IsPlayerController())
	{
		SetTeam(ENoFearTeam::Player);
	}
}
