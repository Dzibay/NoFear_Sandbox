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

	// --- Гранаты ---

	/**
	 * Бросать гранаты (запас — BotGrenades): в цель, засевшую в укрытии дольше HiddenTargetTime, или в кучку
	 * из ClusterCount врагов. Не бросает, если рядом с точкой падения свои или нейтральные.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|AI|Grenade")
	bool bThrowGrenades = true;

	/** Пауза между бросками, с (случайно в пределах). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|AI|Grenade", meta = (ClampMin = "0"))
	FVector2D GrenadeCooldown = FVector2D(10.f, 15.f);

	/** Первый бросок не раньше, чем через столько после того, как появилась цель, с. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|AI|Grenade", meta = (ClampMin = "0"))
	FVector2D FirstGrenadeDelay = FVector2D(5.f, 8.f);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|AI|Grenade", meta = (ClampMin = "0"))
	float MinGrenadeDistance = 600.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|AI|Grenade", meta = (ClampMin = "0"))
	float MaxGrenadeDistance = 2000.f;

	/** Цель не видна столько секунд — выкуривать гранатой. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|AI|Grenade", meta = (ClampMin = "0"))
	float HiddenTargetTime = 4.f;

	/** Дольше этого цель не видна — уже неизвестно, где она, не бросать, с. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|AI|Grenade", meta = (ClampMin = "0"))
	float MaxUnseenTime = 12.f;

	/** Враги в этом радиусе от цели считаются кучкой, см. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|AI|Grenade", meta = (ClampMin = "0"))
	float ClusterRadius = 400.f;

	/** Сколько врагов (с целью) в кучке — повод бросить. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|AI|Grenade", meta = (ClampMin = "2"))
	int32 ClusterCount = 2;

	/** Свои и нейтральные ближе Radius + этого к точке падения — не бросать, см. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|AI|Grenade", meta = (ClampMin = "0"))
	float FriendlyGrenadeMargin = 150.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|AI|Grenade")
	FText GrenadeShout = NSLOCTEXT("NoFear", "GrenadeShout", "Граната!");

	/** Почему бот не бросил гранату в прошлый раз (для отладки и тестов). Пусто — бросил. */
	UFUNCTION(BlueprintPure, Category = "NoFear|AI")
	FString GetGrenadeDebugReason() const { return GrenadeDebugReason; }

	/** Сколько гранат бот бросил. */
	UFUNCTION(BlueprintPure, Category = "NoFear|AI")
	int32 GetGrenadesThrown() const { return GrenadesThrown; }

protected:
	virtual void OnPossess(APawn* InPawn) override;
	virtual void OnUnPossess() override;
	virtual void UpdateBot() override;
	virtual void HandleTargetChanged(AActor* NewTarget, AActor* OldTarget) override;

	/** Достать оружие, если убрано, и пополнить запас. */
	void MaintainWeapon();

	/** Раз в обновление: пора ли бросить гранату, и если да — бросить. */
	void TryThrowGrenade();
	/** Куда лечь гранате у цели: земля под последним известным местом. */
	bool GetGrenadeSpot(const AActor* Target, FVector& OutSpot) const;

	double NextGrenadeTime = 0.0;
	FString GrenadeDebugReason;
	int32 GrenadesThrown = 0;
};
