#pragma once

#include "CoreMinimal.h"
#include "EnvironmentQuery/EnvQueryContext.h"
#include "EnvQueryContext_NoFearThreat.generated.h"

/**
 * Угроза для бота, выполняющего запрос: цель из NoFear Combat (или ключа AttackTarget), а если цели нет — последний стрелявший.
 * Если цель давно не видна, отдаётся точка, где её видели последний раз, а не её настоящее положение.
 */
UCLASS(Blueprintable, meta = (DisplayName = "NoFear Threat"))
class NOFEAR_API UEnvQueryContext_NoFearThreat : public UEnvQueryContext
{
	GENERATED_BODY()

public:
	virtual void ProvideContext(FEnvQueryInstance& QueryInstance, FEnvQueryContextData& ContextData) const override;

	/** Сколько секунд после потери цели бот ещё «чувствует», где она. */
	UPROPERTY(EditDefaultsOnly, Category = Context)
	float TrackHiddenTargetTime = 2.f;
};
