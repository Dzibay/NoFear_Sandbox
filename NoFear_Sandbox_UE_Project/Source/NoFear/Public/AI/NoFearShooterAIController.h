#pragma once

#include "CoreMinimal.h"
#include "AI/NoFearAIController.h"
#include "NoFearShooterAIController.generated.h"

class UNoFearCombatComponent;
class UNoFearWeaponComponent;

/**
 * Стрелок: враги (команда Enemy) и союзники (Ally). Держит точку («дом»), заметив врага — целится и
 * стреляет очередями, теряя из виду — подходит ближе, но не дальше поводка от дома.
 *
 * Точность, линия огня и подавление — UNoFearCombatComponent на теле (добавляется сам, если его нет).
 * Оружие — UNoFearWeaponComponent тела (StartingWeapons). Дерево: BT_NoFear_Soldier.
 */
UCLASS(Blueprintable)
class NOFEAR_API ANoFearShooterAIController : public ANoFearAIController
{
	GENERATED_BODY()

public:
	ANoFearShooterAIController(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	UFUNCTION(BlueprintPure, Category = "NoFear|AI")
	UNoFearWeaponComponent* GetWeaponComponent() const;

	UFUNCTION(BlueprintPure, Category = "NoFear|AI")
	UNoFearCombatComponent* GetCombatComponent() const;

	virtual bool IsTargetVisible() const override;

	/** Запас патронов не кончается (магазин всё равно перезаряжается). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|AI|Weapon")
	bool bInfiniteReserveAmmo = true;

	/** С целью держать оружие в прицеле (поза прицеливания у тела GASP). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|AI|Weapon")
	bool bAimInCombat = true;

	/** Если у тела нет UNoFearCombatComponent, добавить его с настройками по умолчанию. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|AI|Weapon")
	bool bAddCombatComponentIfMissing = true;

protected:
	virtual void OnPossess(APawn* InPawn) override;
	virtual void OnUnPossess() override;
	virtual void UpdateBot() override;
	virtual void HandleTargetChanged(AActor* NewTarget, AActor* OldTarget) override;

	/** Достать оружие, если убрано, и пополнить запас. */
	void MaintainWeapon();
};
