#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/Tasks/BTTask_BlackboardBase.h"
#include "BTTask_NoFearShoot.generated.h"

/**
 * Стрельба очередями по цели из ключа (по умолчанию AttackTarget) оружием тела (UNoFearWeaponComponent).
 * Автомат держит спуск очередь, одиночный и Burst жмут спуск с паузами. Пустой магазин — перезарядка.
 * Пока UNoFearCombatComponent не готов стрелять (нет линии огня, реакция), спуск отпущен
 * (кроме подавления по приказу отряда, см. bBlindFireWhenSuppressing).
 * Завершается успехом после нескольких очередей или если цель пропала/погибла — дерево перепроверит ситуацию.
 */
UCLASS(meta = (DisplayName = "NoFear Shoot"))
class NOFEAR_API UBTTask_NoFearShoot : public UBTTask_BlackboardBase
{
	GENERATED_UCLASS_BODY()

public:
	/** Длительность очереди, сек. */
	UPROPERTY(EditAnywhere, Category = "Shoot", meta = (ClampMin = "0.05"))
	float BurstDurationMin = 0.35f;

	UPROPERTY(EditAnywhere, Category = "Shoot", meta = (ClampMin = "0.05"))
	float BurstDurationMax = 1.f;

	/** Пауза между очередями, сек. */
	UPROPERTY(EditAnywhere, Category = "Shoot", meta = (ClampMin = "0"))
	float PauseMin = 0.3f;

	UPROPERTY(EditAnywhere, Category = "Shoot", meta = (ClampMin = "0"))
	float PauseMax = 0.8f;

	/** Для одиночного и Burst-режима: интервал между нажатиями спуска внутри очереди. */
	UPROPERTY(EditAnywhere, Category = "Shoot", meta = (ClampMin = "0.05"))
	float TriggerIntervalMin = 0.25f;

	UPROPERTY(EditAnywhere, Category = "Shoot", meta = (ClampMin = "0.05"))
	float TriggerIntervalMax = 0.5f;

	/** Сколько очередей сделать до выхода из задачи. 0 — стрелять, пока не прервут. */
	UPROPERTY(EditAnywhere, Category = "Shoot", meta = (ClampMin = "0"))
	int32 BurstsMin = 2;

	UPROPERTY(EditAnywhere, Category = "Shoot", meta = (ClampMin = "0"))
	int32 BurstsMax = 4;

	/**
	 * Подавление: пока стрелок выполняет приказ отряда на огонь (ANoFearShooterAIController::IsSuppressing),
	 * стрелять и по невидимой цели — туда, где её видели свои (UNoFearCombatComponent::CanBlindFire).
	 */
	UPROPERTY(EditAnywhere, Category = "Shoot")
	bool bBlindFireWhenSuppressing = false;

	virtual EBTNodeResult::Type ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;
	virtual EBTNodeResult::Type AbortTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;
	virtual void OnTaskFinished(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, EBTNodeResult::Type TaskResult) override;
	virtual uint16 GetInstanceMemorySize() const override;
	virtual void InitializeMemory(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, EBTMemoryInit::Type InitType) const override;
	virtual void CleanupMemory(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, EBTMemoryClear::Type CleanupType) const override;
	virtual FString GetStaticDescription() const override;

protected:
	virtual void TickTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds) override;

private:
	struct FShootMemory
	{
		double PhaseEndTime = 0.0;
		double NextTriggerTime = 0.0;
		int32 BurstsLeft = 0;
		bool bFiringPhase = false;
		bool bTriggerHeld = false;
	};

	void ReleaseTrigger(UBehaviorTreeComponent& OwnerComp, FShootMemory& Memory) const;
};
