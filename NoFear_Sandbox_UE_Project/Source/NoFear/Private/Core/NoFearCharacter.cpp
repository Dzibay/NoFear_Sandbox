#include "Core/NoFearCharacter.h"

#include "GameFramework/PlayerController.h"
#include "Weapons/NoFearWeaponComponent.h"

ANoFearCharacter::ANoFearCharacter()
{
	WeaponComponent = CreateDefaultSubobject<UNoFearWeaponComponent>(TEXT("WeaponComponent"));
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

void ANoFearCharacter::NotifyControllerChanged()
{
	Super::NotifyControllerChanged();

	if (WeaponComponent)
	{
		WeaponComponent->HandleControllerChanged();
	}
}

void ANoFearCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);

	if (WeaponComponent)
	{
		WeaponComponent->SetupPlayerInput(PlayerInputComponent);
	}
}
