#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/Tasks/BTTask_BlackboardBase.h"
#include "BTTask_NoFearMeleeAttack.generated.h"

/**
 * Атака UNoFearMeleeComponent по цели из ключа (по умолчанию AttackTarget).
 * Бежит к цели (Chase), а как только какая-нибудь атака подходит по дистанции и перезарядке — останавливается
 * и бьёт: издалека это рывок, вплотную — удар с места. Успех, когда атака закончилась;
 * провал, если за Max Wait Time так и не ударил или цель пропала.
 */
UCLASS(meta = (DisplayName = "NoFear Melee Attack"))
class NOFEAR_API UBTTask_NoFearMeleeAttack : public UBTTask_BlackboardBase
{
	GENERATED_UCLASS_BODY()

public:
	/** Сколько гнаться и ждать перезарядки, прежде чем сдаться, сек. */
	UPROPERTY(EditAnywhere, Category = "Melee", meta = (ClampMin = "0"))
	float MaxWaitTime = 5.f;

	/** Бежать к цели, пока ни одна атака не достаёт. false — сразу провал, если цель вне дистанции. */
	UPROPERTY(EditAnywhere, Category = "Melee")
	bool bChase = true;

	/** До какого расстояния подбегать, см. */
	UPROPERTY(EditAnywhere, Category = "Melee", meta = (ClampMin = "0", EditCondition = "bChase"))
	float ChaseAcceptanceRadius = 100.f;

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
		bool bChasing = false;
	};

	/** Ударить, если можно; иначе (если разрешено) бежать к цели. false — ни то ни другое. */
	bool AttackOrChase(UBehaviorTreeComponent& OwnerComp, FMeleeMemory& Memory, AActor* Target) const;
	void StopChase(UBehaviorTreeComponent& OwnerComp, FMeleeMemory& Memory) const;
};
