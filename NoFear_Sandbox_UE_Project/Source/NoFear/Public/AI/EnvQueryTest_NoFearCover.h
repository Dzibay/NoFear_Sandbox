#pragma once

#include "CoreMinimal.h"
#include "Engine/EngineTypes.h"
#include "EnvironmentQuery/EnvQueryTest.h"
#include "EnvQueryTest_NoFearCover.generated.h"

class UEnvQueryContext;

/**
 * Оценка укрытия в точке. Из точки на двух высотах (сидя и стоя) пускаются лучи к глазам угрозы.
 *  - закрыты оба — полное укрытие (FullCoverScore);
 *  - закрыт только нижний — низкое укрытие, из-за которого можно стрелять (LowCoverScore);
 *  - нижний открыт — точка простреливается (0).
 * Укрытие вплотную (ближе MaxCoverDistance) ценится выше, чем далёкая стена.
 * Точки рядом с другими ботами, их целями движения и чужой бронью укрытий (UNoFearBotSubsystem) считаются занятыми.
 * Лучи те же, что у поиска укрытий стрелков (UNoFearCoverLibrary::EvaluateCover).
 * По умолчанию — фильтр (минимум 0.1) и оценка.
 */
UCLASS(meta = (DisplayName = "NoFear: Cover"))
class NOFEAR_API UEnvQueryTest_NoFearCover : public UEnvQueryTest
{
	GENERATED_BODY()

public:
	UEnvQueryTest_NoFearCover(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	/** От кого прячемся. */
	UPROPERTY(EditDefaultsOnly, Category = Cover)
	TSubclassOf<UEnvQueryContext> ThreatContext;

	/** Высота глаз угрозы над её позицией (у персонажа позиция — центр капсулы). */
	UPROPERTY(EditDefaultsOnly, Category = Cover)
	float ThreatEyeHeight = 70.f;

	/** Высота проверки «сидя» над точкой навмеша. */
	UPROPERTY(EditDefaultsOnly, Category = Cover)
	float LowCoverHeight = 80.f;

	/** Высота проверки «стоя» над точкой навмеша. */
	UPROPERTY(EditDefaultsOnly, Category = Cover)
	float HighCoverHeight = 160.f;

	/** Укрытие должно быть не дальше этого расстояния от точки. 0 — без ограничения. */
	UPROPERTY(EditDefaultsOnly, Category = Cover, meta = (ClampMin = "0"))
	float MaxCoverDistance = 200.f;

	UPROPERTY(EditDefaultsOnly, Category = Cover, meta = (ClampMin = "0", ClampMax = "1"))
	float FullCoverScore = 1.f;

	UPROPERTY(EditDefaultsOnly, Category = Cover, meta = (ClampMin = "0", ClampMax = "1"))
	float LowCoverScore = 0.75f;

	/** Множитель оценки, если препятствие дальше MaxCoverDistance: бот скрыт, но не прижат к укрытию. */
	UPROPERTY(EditDefaultsOnly, Category = Cover, meta = (ClampMin = "0", ClampMax = "1"))
	float DistantCoverMultiplier = 0.4f;

	/** Радиус «занятости» вокруг других ботов и их целей движения. 0 — не проверять. */
	UPROPERTY(EditDefaultsOnly, Category = Cover, meta = (ClampMin = "0"))
	float OccupiedRadius = 150.f;

	UPROPERTY(EditDefaultsOnly, Category = Cover)
	TEnumAsByte<ECollisionChannel> TraceChannel = ECC_Visibility;

	virtual void RunTest(FEnvQueryInstance& QueryInstance) const override;
	virtual FText GetDescriptionTitle() const override;
	virtual FText GetDescriptionDetails() const override;
};
