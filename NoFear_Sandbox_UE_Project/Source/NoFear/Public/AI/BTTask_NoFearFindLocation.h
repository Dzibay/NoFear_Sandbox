#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/Tasks/BTTask_BlackboardBase.h"
#include "BTTask_NoFearFindLocation.generated.h"

/**
 * Случайная достижимая точка навигации в радиусе от Origin Key (по умолчанию HomeLocation).
 * Результат пишется в ключ задачи (по умолчанию MoveGoal). Для блуждания монстров и патрулей.
 */
UCLASS(meta = (DisplayName = "NoFear Find Wander Location"))
class NOFEAR_API UBTTask_NoFearFindWanderLocation : public UBTTask_BlackboardBase
{
	GENERATED_UCLASS_BODY()

public:
	/** Вокруг какой точки искать. Пустой ключ — вокруг бота. */
	UPROPERTY(EditAnywhere, Category = "Wander")
	FBlackboardKeySelector OriginKey;

	UPROPERTY(EditAnywhere, Category = "Wander", meta = (ClampMin = "0"))
	float Radius = 800.f;

	/** Точка ближе этого к боту не годится (иначе он «бродит» на месте). */
	UPROPERTY(EditAnywhere, Category = "Wander", meta = (ClampMin = "0"))
	float MinDistance = 200.f;

	virtual EBTNodeResult::Type ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;
	virtual void InitializeFromAsset(UBehaviorTree& Asset) override;
	virtual FString GetStaticDescription() const override;
};

/**
 * Куда идти за целью, которую сейчас не видно (или не достать выстрелом): последнее известное место цели
 * (ANoFearAIController::GetLastKnownTargetLocation), подтянутое к поводку бота — дому или лидеру напарника.
 * Результат пишется в ключ задачи (по умолчанию MoveGoal).
 */
UCLASS(meta = (DisplayName = "NoFear Find Chase Location"))
class NOFEAR_API UBTTask_NoFearFindChaseLocation : public UBTTask_BlackboardBase
{
	GENERATED_UCLASS_BODY()

public:
	/** Цель. Нужен, если контроллер не ANoFearAIController. */
	UPROPERTY(EditAnywhere, Category = "Chase")
	FBlackboardKeySelector TargetKey;

	/** Остановиться, не доходя столько см до точки цели. */
	UPROPERTY(EditAnywhere, Category = "Chase", meta = (ClampMin = "0"))
	float StopShortDistance = 150.f;

	virtual EBTNodeResult::Type ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;
	virtual void InitializeFromAsset(UBehaviorTree& Asset) override;
	virtual FString GetStaticDescription() const override;
};

/** Очистить ключ блэкборда (например, InvestigateLocation после проверки). */
UCLASS(meta = (DisplayName = "NoFear Clear Blackboard Key"))
class NOFEAR_API UBTTask_NoFearClearBlackboardKey : public UBTTask_BlackboardBase
{
	GENERATED_UCLASS_BODY()

public:
	virtual EBTNodeResult::Type ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;
	virtual FString GetStaticDescription() const override;
};

/**
 * Точка на кольце MinRadius..MaxRadius вокруг цели для монстра без жетона атаки: шаг по кругу от текущего угла
 * в сторону, где меньше других монстров (они расходятся вокруг цели, а не толпятся). Результат — в ключ задачи.
 */
UCLASS(meta = (DisplayName = "NoFear Find Circle Location"))
class NOFEAR_API UBTTask_NoFearFindCircleLocation : public UBTTask_BlackboardBase
{
	GENERATED_UCLASS_BODY()

public:
	UPROPERTY(EditAnywhere, Category = "Circle")
	FBlackboardKeySelector TargetKey;

	UPROPERTY(EditAnywhere, Category = "Circle", meta = (ClampMin = "0"))
	float MinRadius = 400.f;

	UPROPERTY(EditAnywhere, Category = "Circle", meta = (ClampMin = "0"))
	float MaxRadius = 600.f;

	/** Шаг по кругу за раз, градусы. */
	UPROPERTY(EditAnywhere, Category = "Circle", meta = (ClampMin = "0"))
	float MinStepAngle = 15.f;

	UPROPERTY(EditAnywhere, Category = "Circle", meta = (ClampMin = "0"))
	float MaxStepAngle = 50.f;

	/** Учитывать других монстров ближе этого к цели. */
	UPROPERTY(EditAnywhere, Category = "Circle", meta = (ClampMin = "0"))
	float CrowdRadius = 1200.f;

	virtual EBTNodeResult::Type ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;
	virtual void InitializeFromAsset(UBehaviorTree& Asset) override;
	virtual FString GetStaticDescription() const override;
};
