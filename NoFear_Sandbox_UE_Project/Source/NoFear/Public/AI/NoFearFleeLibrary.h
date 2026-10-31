#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "NoFearFleeLibrary.generated.h"

class APawn;

/**
 * Куда бежать от угроз. Повторяет этапы EQS-запроса (кольцо точек → фильтры → оценка → проверка пути),
 * но в C++: запрос нужен раз в секунду одному боту, а ассет EQS пришлось бы генерировать и поддерживать.
 */
UCLASS()
class NOFEAR_API UNoFearFleeLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Точка навигации в MinDistance..MaxDistance от пешки, подальше от угроз и вне их видимости,
	 * путь к которой не проходит ближе SafePathDistance к угрозе. false — бежать некуда (прятаться на месте).
	 */
	UFUNCTION(BlueprintCallable, Category = "NoFear|AI")
	static bool FindFleeLocation(APawn* Pawn, const TArray<FVector>& Threats, FVector& OutLocation,
		float MinDistance = 1500.f, float MaxDistance = 3000.f, float SafePathDistance = 500.f, int32 NumSamples = 32);

	/** Точка безопасна: дальше MinThreatDistance от всех угроз. */
	UFUNCTION(BlueprintPure, Category = "NoFear|AI")
	static bool IsLocationSafe(const FVector& Location, const TArray<FVector>& Threats, float MinThreatDistance);
};
