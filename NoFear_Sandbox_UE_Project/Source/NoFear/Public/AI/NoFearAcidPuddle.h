#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "NoFearAcidPuddle.generated.h"

class UDamageType;
class UStaticMeshComponent;

/**
 * Лужа кислоты: плоский круг на земле, раз в TickInterval наносит DamagePerTick всем живым не-союзникам
 * стрелявшего (Instigator) в радиусе Radius. Несколько луж не складываются: стоящий в них горит как в одной. Исчезает через Lifetime сек, последнюю секунду сжимается.
 */
UCLASS(Blueprintable)
class NOFEAR_API ANoFearAcidPuddle : public AActor
{
	GENERATED_BODY()

public:
	ANoFearAcidPuddle();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Puddle", meta = (ClampMin = "1"))
	float Radius = 150.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Puddle", meta = (ClampMin = "0.1"))
	float Lifetime = 6.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Puddle", meta = (ClampMin = "0.05"))
	float TickInterval = 0.5f;

	/** Урон за такт (8 урона в секунду при 4 и 0.5 с). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Puddle", meta = (ClampMin = "0"))
	float DamagePerTick = 4.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Puddle")
	TSubclassOf<UDamageType> DamageType;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Puddle")
	FLinearColor Color = FLinearColor(0.2f, 0.7f, 0.02f);

	/** Сколько всего урона нанесла (для тестов). */
	UFUNCTION(BlueprintPure, Category = "NoFear|Puddle")
	float GetTotalDamageDealt() const { return TotalDamageDealt; }

	virtual void Tick(float DeltaSeconds) override;

protected:
	virtual void BeginPlay() override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Puddle")
	TObjectPtr<UStaticMeshComponent> Mesh;

private:
	void DealDamage();

	FTimerHandle DamageTimer;
	double SpawnTime = 0.0;
	float TotalDamageDealt = 0.f;
};
