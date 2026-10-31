#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTTaskNode.h"
#include "BehaviorTree/Tasks/BTTask_BlackboardBase.h"
#include "BTTask_NoFearCoverFight.generated.h"

/**
 * Бой из укрытия стрелка (ANoFearShooterAIController, укрытие уже занято и бот в нём):
 *   Hide — сидит за низким укрытием или стоит за высоким, перезаряжается, если магазин неполон;
 *   Peek — встаёт из-за низкого или шагает вбок из-за высокого;
 *   Fire — 1–2 очереди, пока можно стрелять (свои на линии огня или пустой магазин — назад);
 *   Return — снова присесть или вернуться за укрытие.
 * После нескольких циклов просит контроллер перепроверить укрытие и завершается успехом.
 * Пока прикрывает отходящего своего, прячется меньше и стреляет дольше.
 * По приказу отряда на подавление стреляет и по невидимой цели — туда, где её видели свои.
 */
UCLASS(meta = (DisplayName = "NoFear Cover Fight"))
class NOFEAR_API UBTTask_NoFearCoverFight : public UBTTaskNode
{
	GENERATED_UCLASS_BODY()

public:
	/** Сколько циклов «спрятаться — выглянуть — стрелять» до перепроверки укрытия. */
	UPROPERTY(EditAnywhere, Category = "Cover", meta = (ClampMin = "1"))
	int32 CyclesMin = 3;

	UPROPERTY(EditAnywhere, Category = "Cover", meta = (ClampMin = "1"))
	int32 CyclesMax = 5;

	/** Очередей за одно выглядывание. */
	UPROPERTY(EditAnywhere, Category = "Cover", meta = (ClampMin = "1"))
	int32 BurstsMin = 1;

	UPROPERTY(EditAnywhere, Category = "Cover", meta = (ClampMin = "1"))
	int32 BurstsMax = 2;

	UPROPERTY(EditAnywhere, Category = "Cover", meta = (ClampMin = "0.05"))
	float BurstDurationMin = 0.35f;

	UPROPERTY(EditAnywhere, Category = "Cover", meta = (ClampMin = "0.05"))
	float BurstDurationMax = 0.9f;

	UPROPERTY(EditAnywhere, Category = "Cover", meta = (ClampMin = "0"))
	float BurstPauseMin = 0.25f;

	UPROPERTY(EditAnywhere, Category = "Cover", meta = (ClampMin = "0"))
	float BurstPauseMax = 0.5f;

	/** Одиночный и Burst: интервал между нажатиями спуска. */
	UPROPERTY(EditAnywhere, Category = "Cover", meta = (ClampMin = "0.05"))
	float TriggerIntervalMin = 0.25f;

	UPROPERTY(EditAnywhere, Category = "Cover", meta = (ClampMin = "0.05"))
	float TriggerIntervalMax = 0.5f;

	/** Встать из-за низкого укрытия, с. */
	UPROPERTY(EditAnywhere, Category = "Cover", meta = (ClampMin = "0"))
	float StandUpTime = 0.35f;

	/** Выглянул, а стрелять нельзя столько секунд — назад. */
	UPROPERTY(EditAnywhere, Category = "Cover", meta = (ClampMin = "0"))
	float PeekWaitTime = 1.2f;

	/** Шаг вбок или обратно дольше этого — считать, что дошёл (или не дойдёт), с. */
	UPROPERTY(EditAnywhere, Category = "Cover", meta = (ClampMin = "0.1"))
	float MoveTimeout = 2.5f;

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
	enum class EPhase : uint8
	{
		Hide,
		Peek,
		Fire,
		Return
	};

	struct FCoverFightMemory
	{
		FVector CoverLocation = FVector::ZeroVector;
		double PhaseStartTime = 0.0;
		double PhaseEndTime = 0.0;
		double NextTriggerTime = 0.0;
		double NotReadySince = -1.0;
		int32 CyclesLeft = 0;
		int32 BurstsLeft = 0;
		EPhase Phase = EPhase::Hide;
		bool bBurstActive = false;
		bool bTriggerHeld = false;
		bool bMoving = false;
	};

	void EnterPhase(UBehaviorTreeComponent& OwnerComp, FCoverFightMemory& Memory, EPhase Phase) const;
	void ReleaseTrigger(UBehaviorTreeComponent& OwnerComp, FCoverFightMemory& Memory) const;
	void Cleanup(UBehaviorTreeComponent& OwnerComp, FCoverFightMemory& Memory) const;
};

/**
 * Точка для открытого боя стрелка (ANoFearShooterAIController::FindCombatPosition) в ключ (MoveGoal):
 * цель не видна — к месту, где её видели; видна — шаг вбок на удобной дистанции оружия, не загораживая своих.
 */
UCLASS(meta = (DisplayName = "NoFear Find Combat Position"))
class NOFEAR_API UBTTask_NoFearFindCombatPosition : public UBTTask_BlackboardBase
{
	GENERATED_UCLASS_BODY()

public:
	virtual EBTNodeResult::Type ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;
	virtual FString GetStaticDescription() const override;
};
