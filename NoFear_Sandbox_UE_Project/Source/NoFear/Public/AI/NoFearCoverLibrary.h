#pragma once

#include "CoreMinimal.h"
#include "Engine/EngineTypes.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "NoFearCoverLibrary.generated.h"

class APawn;
struct FCollisionQueryParams;

/** Что даёт укрытие в точке от угрозы. */
UENUM(BlueprintType)
enum class ENoFearCoverType : uint8
{
	/** Точка простреливается. */
	None,
	/** Закрыт только присевший: стрелять — встать. */
	Low,
	/** Закрыт и стоящий: стрелять — шагнуть в сторону. */
	High
};

/** Параметры поиска укрытия (ANoFearShooterAIController заполняет из своих настроек). */
struct FNoFearCoverSearch
{
	/** Кто ищет. */
	APawn* Pawn = nullptr;
	/** От кого прятаться: центр капсулы угрозы (глаза — на ThreatEyeHeight выше). */
	FVector ThreatLocation = FVector::ZeroVector;
	/** Угроза-актор: в лучах не учитывается. */
	const AActor* ThreatActor = nullptr;
	/** Где искать: кольца вокруг пешки до этого радиуса. */
	float SearchRadius = 1500.f;
	/** Точек на кольце не больше этого (шаг по кольцу — 180 см): дальние кольца реже. Редкие поиски (фланг) берут больше. */
	int32 MaxRingSamples = 20;
	/** Ближе к угрозе — не укрытие. */
	float MinThreatDistance = 500.f;
	/** Дальше от угрозы — не годится. 0 — без ограничения. */
	float MaxThreatDistance = 0.f;
	/** Удобная дистанция до угрозы (по оружию); вне её оценка ниже. */
	FVector2D PreferredRange = FVector2D(800.f, 2000.f);
	/** Поводок: точки не дальше LeashRadius от LeashAnchor. 0 — без поводка. */
	FVector LeashAnchor = FVector::ZeroVector;
	float LeashRadius = 0.f;
	/** Отход: точка дальше от угрозы, чем пешка сейчас, хотя бы на RetreatGain, и чем дальше — тем лучше. */
	bool bRetreat = false;
	float RetreatGain = 300.f;
	/** Препятствие должно быть не дальше этого от точки, см. */
	float MaxObstacleDistance = 150.f;
	/** Чужая бронь или другой бот ближе этого — точка занята. */
	float ReserveRadius = 150.f;

	// --- Приказы отряда ---

	/** Искать вокруг SearchOrigin, а не вокруг пешки (подкрепление — укрытие рядом с прижатым своим). */
	bool bUseSearchOrigin = false;
	FVector SearchOrigin = FVector::ZeroVector;
	/** Тянуться к GoalLocation: минус GoalWeight за каждые 1000 см от неё (отход — к рубежу). 0 — без цели. */
	FVector GoalLocation = FVector::ZeroVector;
	float GoalWeight = 0.f;
	/**
	 * Отход к цели: точка ближе к GoalLocation, чем пешка, хотя бы на GoalGain (вместо «дальше от угрозы на RetreatGain»)
	 * и к угрозе не ближе, чем сама пешка или цель (без 200 см). 0 — отход по угрозе.
	 */
	float GoalGain = 0.f;
	/**
	 * Фланг: угол у угрозы между направлениями на FlankFrom (где отряд) и на точку — не меньше этого, градусы.
	 * Точки с большим углом лучше. 0 — без фланга.
	 */
	float MinFlankAngle = 0.f;
	FVector FlankFrom = FVector::ZeroVector;
	/**
	 * Путь к точке скрытный (замеры каждые 200 см, первые 300 см не в счёт): угроза видит не больше HiddenPathMaxVisible
	 * от всех замеров (но один промельк допустим всегда) и ни одного открытого участка длиннее HiddenPathMaxRun замеров подряд.
	 */
	bool bHiddenPath = false;
	float HiddenPathMaxVisible = 0.3f;
	int32 HiddenPathMaxRun = 3;
	/** Путь не длиннее этого × расстояние по прямой + 300 см. */
	float MaxPathLengthFactor = 1.7f;
	/** Сколько лучших точек проверять путём. */
	int32 MaxPathChecks = 6;
};

/** Найденное укрытие. */
struct FNoFearCoverPoint
{
	FVector Location = FVector::ZeroVector;
	ENoFearCoverType Type = ENoFearCoverType::None;
	/** Откуда стрелять: у низкого — та же точка (встать), у высокого — шаг в сторону. */
	FVector PeekLocation = FVector::ZeroVector;
	/** Есть ли откуда стрелять. Высокое укрытие без выглядывания годится только переждать. */
	bool bCanPeek = false;
	float Score = 0.f;
};

/**
 * Укрытия для стрелков. Те же лучи, что у теста EQS «NoFear: Cover» (тест сам вызывает EvaluateCover):
 * из точки на высоте сидя и стоя к глазам угрозы. Поиск — в C++, как у бегства мирных (UNoFearFleeLibrary):
 * кольца точек → фильтры (дистанция, поводок, бронь) → лучи → оценка → проверка пути у лучших.
 */
UCLASS()
class NOFEAR_API UNoFearCoverLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** Высота глаз угрозы над центром капсулы. */
	static constexpr float ThreatEyeHeight = 70.f;
	/** Высота проверки «сидя» и «стоя» над точкой навмеша. */
	static constexpr float LowCoverHeight = 80.f;
	static constexpr float HighCoverHeight = 160.f;

	/**
	 * Укрытие в точке навмеша GroundPoint от глаз угрозы. OutObstacleDistance — расстояние до препятствия по нижнему лучу.
	 * Лучи игнорируют то, что в Params (пешки и их оружие — MakeCoverTraceParams).
	 */
	static ENoFearCoverType EvaluateCover(const UWorld* World, const FVector& GroundPoint, const FVector& ThreatEyes,
		const FCollisionQueryParams& Params, ECollisionChannel Channel, float* OutObstacleDistance = nullptr,
		float LowHeight = LowCoverHeight, float HighHeight = HighCoverHeight);

	/** Параметры лучей укрытия: все персонажи и прикреплённое к ним (оружие) прозрачны. */
	static FCollisionQueryParams MakeCoverTraceParams(const UWorld* World);

	/** Лучшее укрытие по правилам Search. false — укрытий нет. */
	static bool FindCover(const FNoFearCoverSearch& Search, FNoFearCoverPoint& OutCover);

	/** Отладка: сколько точек отсеял каждый фильтр при последнем FindCover (почему укрытие не нашлось). */
	static const FString& GetLastSearchStats() { return LastSearchStats; }

	/** Откуда стрелять из высокого укрытия: ближайший шаг вбок (60–160 см), откуда стоя видно угрозу. */
	static bool FindPeekLocation(const UWorld* World, const FVector& CoverLocation, const FVector& ThreatEyes,
		const FCollisionQueryParams& Params, FVector& OutPeek);

	/** Укрытие в точке от угрозы (для тестов и Blueprint). Location — точка на полу или навмеше. */
	UFUNCTION(BlueprintCallable, Category = "NoFear|AI", meta = (WorldContext = "WorldContextObject"))
	static ENoFearCoverType TestCoverAt(const UObject* WorldContextObject, FVector Location, FVector ThreatLocation);

private:
	static FString LastSearchStats;
};
