#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTTaskNode.h"
#include "BTTask_NoFearLookAround.generated.h"

/**
 * Постоять и осмотреться: каждые Look Interval секунд бот поворачивает голову и корпус
 * в случайную сторону (до Max Yaw от того, куда смотрел в начале). Успех через Duration ± Random Deviation.
 */
UCLASS(meta = (DisplayName = "NoFear Look Around"))
class NOFEAR_API UBTTask_NoFearLookAround : public UBTTaskNode
{
	GENERATED_UCLASS_BODY()

public:
	UPROPERTY(EditAnywhere, Category = "Look", meta = (ClampMin = "0"))
	float Duration = 4.f;

	UPROPERTY(EditAnywhere, Category = "Look", meta = (ClampMin = "0"))
	float RandomDeviation = 1.f;

	/** Как часто менять направление взгляда, сек. */
	UPROPERTY(EditAnywhere, Category = "Look", meta = (ClampMin = "0.1"))
	float LookInterval = 1.2f;

	/** Насколько далеко в сторону от исходного направления смотреть, градусов. */
	UPROPERTY(EditAnywhere, Category = "Look", meta = (ClampMin = "0", ClampMax = "180"))
	float MaxYaw = 110.f;

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
	struct FLookMemory
	{
		double EndTime = 0.0;
		double NextLookTime = 0.0;
		float BaseYaw = 0.f;
	};
};

/** Мирный добежал до укрытия: присесть и закрыть голову (ANoFearCivilianAIController::NotifyReachedSafety). */
UCLASS(meta = (DisplayName = "NoFear Civilian Shelter"))
class NOFEAR_API UBTTask_NoFearCivilianShelter : public UBTTaskNode
{
	GENERATED_UCLASS_BODY()

public:
	virtual EBTNodeResult::Type ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;
	virtual FString GetStaticDescription() const override;
};
