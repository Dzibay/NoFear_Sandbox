#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "GenericTeamAgentInterface.h"
#include "Core/NoFearTeam.h"
#include "NoFearCharacter.generated.h"

class UNoFearWeaponComponent;

/**
 * База для всех персонажей: игрока, ботов и монстров.
 * CBP_SandboxCharacter наследуется от неё (Class Settings → Parent Class).
 * Команда и оружие; здоровье добавится следующим слоем.
 */
UCLASS()
class NOFEAR_API ANoFearCharacter : public ACharacter, public IGenericTeamAgentInterface
{
	GENERATED_BODY()

public:
	ANoFearCharacter();

	/** Команда. Меняется у каждого экземпляра на уровне. Игрок получает Player при вселении. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|Team", meta = (ExposeOnSpawn = true))
	ENoFearTeam Team = ENoFearTeam::NPC;

	UFUNCTION(BlueprintCallable, Category = "NoFear|Team")
	void SetTeam(ENoFearTeam NewTeam);

	UFUNCTION(BlueprintPure, Category = "NoFear|Weapon")
	UNoFearWeaponComponent* GetWeaponComponent() const { return WeaponComponent; }

	// IGenericTeamAgentInterface
	virtual void SetGenericTeamId(const FGenericTeamId& NewTeamId) override;
	virtual FGenericTeamId GetGenericTeamId() const override;

protected:
	virtual void PossessedBy(AController* NewController) override;
	virtual void NotifyControllerChanged() override;
	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "NoFear|Weapon")
	TObjectPtr<UNoFearWeaponComponent> WeaponComponent;
};
