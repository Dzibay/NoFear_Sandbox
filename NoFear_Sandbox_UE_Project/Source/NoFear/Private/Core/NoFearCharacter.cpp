#include "Core/NoFearCharacter.h"

#include "Camera/CameraComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/SpringArmComponent.h"
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

void ANoFearCharacter::BeginPlay()
{
	Super::BeginPlay();

	if (bDisableLegacySpringArmCamera)
	{
		DisableLegacyCamera();
	}
}

void ANoFearCharacter::DisableLegacyCamera()
{
	// GameplayCameraComponent из плагина GameplayCameras — ищем по имени, чтобы не тянуть модуль в Build.cs.
	UClass* GameplayCameraClass = FindObject<UClass>(nullptr, TEXT("/Script/GameplayCameras.GameplayCameraComponent"));
	if (!GameplayCameraClass || !GetComponentByClass(TSubclassOf<UActorComponent>(GameplayCameraClass)))
	{
		return;
	}

	TInlineComponentArray<USpringArmComponent*> SpringArms(this);
	for (USpringArmComponent* SpringArm : SpringArms)
	{
		TArray<USceneComponent*> ArmChildren;
		SpringArm->GetChildrenComponents(/*bIncludeAllDescendants=*/true, ArmChildren);
		for (USceneComponent* Child : ArmChildren)
		{
			if (UCameraComponent* LegacyCamera = Cast<UCameraComponent>(Child))
			{
				LegacyCamera->Deactivate();
			}
		}
	}
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
