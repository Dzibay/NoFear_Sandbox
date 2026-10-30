#pragma once

#include "CoreMinimal.h"
#include "AI/NoFearAIController.h"
#include "NoFearCivilianAIController.generated.h"

/**
 * Мирный житель: не выбирает целей и не стреляет. Пока просто стоит (BT_NoFear_Civilian).
 * Сюда же потом ляжет бегство и паника: урон и выстрелы уже приходят в базовый контроллер.
 */
UCLASS(Blueprintable)
class NOFEAR_API ANoFearCivilianAIController : public ANoFearAIController
{
	GENERATED_BODY()

public:
	ANoFearCivilianAIController(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());
};
