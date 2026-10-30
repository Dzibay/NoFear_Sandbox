#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/Tasks/BTTask_BlackboardBase.h"
#include "BTTask_NoFearMeleeAttack.generated.h"

/**
 * Удар UNoFearMeleeComponent по цели из ключа (по умолчанию AttackTarget).
 * Цель дальше Attack Range — провал (дерево снова подведёт бота ближе).
 * Удар на перезарядке — ждёт до Max Wait Time. Успех, когда атака закончилась.
 */
UCLASS(meta = (DisplayName = "NoFear Melee Attack"))
class NOFEAR_API UBTTask_NoFearMeleeAttack : public UBTTask_BlackboardBase
{
	GENERATED_UCLASS_BODY()

public:
	/** Сколько ждать перезарядки удара, сек. */
	UPROPERTY(EditAnywhere, Category = "Melee", meta = (ClampMin = "0"))
	float MaxWaitTime = 2.f;

	virtual EBTNodeResult::Type ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;
	virtual EBTNodeResult::Type AbortTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;
	virtual uint16 GetInstanceMemorySize() const override;
	virtual void InitializeMemory(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, EBTMemoryInit::Type InitType) const override;
	virtual void CleanupMemory(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, EBTMemoryClear::Type CleanupType) const override;
	virtual FString GetStaticDescription() const override;

protected:
	virtual void TickTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds) override;

private:
	struct FMeleeMemory
	{
		double GiveUpTime = 0.0;
		bool bAttackStarted = false;
	};
};
