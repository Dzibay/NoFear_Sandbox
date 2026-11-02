#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Subsystems/WorldSubsystem.h"
#include "NoFearLightSubsystem.generated.h"

class UBoxComponent;
class ULocalLightComponent;

/**
 * Тёмная зона (подвал, дом без света): внутри коробки фоновая освещённость AmbientLight вместо дневной 1.
 * Свет добавляют лампы (ANoFearLamp и любые ULocalLightComponent, зарегистрированные в UNoFearLightSubsystem).
 */
UCLASS(Blueprintable)
class NOFEAR_API ANoFearDarkVolume : public AActor
{
	GENERATED_BODY()

public:
	ANoFearDarkVolume();

	/** Полуразмеры зоны (см). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Darkness")
	FVector Extent = FVector(500.f, 500.f, 200.f);

	/** Освещённость без ламп: 0 — кромешная тьма, 1 — день. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Darkness", meta = (ClampMin = "0", ClampMax = "1"))
	float AmbientLight = 0.08f;

	UFUNCTION(BlueprintPure, Category = "NoFear|Light")
	bool IsInside(FVector Location) const;

	virtual void OnConstruction(const FTransform& Transform) override;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UPROPERTY(VisibleAnywhere, Category = "Darkness")
	TObjectPtr<UBoxComponent> Bounds;
};

/**
 * Освещённость для игровой логики (Docs/Interactables.md, «Свет и темнота»): 0 — тьма, 1 — светло.
 * Вне тёмных зон всегда 1. В тёмной зоне — её AmbientLight плюс вклад включённых ламп в радиусе.
 * ИИ берёт отсюда множители зрения и страха.
 */
UCLASS()
class NOFEAR_API UNoFearLightSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	/** Множитель скорости обнаружения в полной темноте. */
	static constexpr float DarkDetectFactor = 0.35f;

	/** Множитель роста страха у людей в полной темноте. */
	static constexpr float DarkFearFactor = 1.5f;

	/** Освещённость в точке, 0..1. */
	UFUNCTION(BlueprintPure, Category = "NoFear|Light", meta = (WorldContext = "WorldContextObject"))
	static float GetLightLevel(const UObject* WorldContextObject, FVector Location);

	/** Во сколько раз медленнее замечают того, кто стоит в Location: 1 на свету, DarkDetectFactor в темноте. */
	UFUNCTION(BlueprintPure, Category = "NoFear|Light", meta = (WorldContext = "WorldContextObject"))
	static float GetDetectionFactor(const UObject* WorldContextObject, FVector Location);

	/** Во сколько раз быстрее растёт страх в Location: 1 на свету, DarkFearFactor в темноте. */
	UFUNCTION(BlueprintPure, Category = "NoFear|Light", meta = (WorldContext = "WorldContextObject"))
	static float GetDarknessFearFactor(const UObject* WorldContextObject, FVector Location);

	/** Учитывать свет лампы. Weight — сколько освещённости она даёт в центре (1 — как день). */
	void RegisterLight(ULocalLightComponent* Light, float Weight = 1.f);
	void UnregisterLight(ULocalLightComponent* Light);

	/**
	 * Линии питания (Docs/Interactables.md, «Генератор и свет»): лампа с непустым Circuit горит, только пока линию
	 * питает хотя бы один источник (генератор). Линия None питается всегда.
	 */
	UFUNCTION(BlueprintPure, Category = "NoFear|Light")
	bool IsCircuitPowered(FName Circuit) const;

	/** Источник Source начал (bPowered) или перестал питать линию Circuit. */
	void SetCircuitPowered(FName Circuit, const UObject* Source, bool bPowered);

	/** Линия включилась или погасла. */
	DECLARE_MULTICAST_DELEGATE_TwoParams(FOnCircuitChanged, FName /*Circuit*/, bool /*bPowered*/);
	FOnCircuitChanged OnCircuitChanged;

	void RegisterDarkVolume(ANoFearDarkVolume* Volume);
	void UnregisterDarkVolume(ANoFearDarkVolume* Volume);

	float ComputeLightLevel(const FVector& Location) const;

private:
	struct FLightEntry
	{
		TWeakObjectPtr<ULocalLightComponent> Light;
		float Weight = 1.f;
	};
	TArray<FLightEntry> Lights;
	TArray<TWeakObjectPtr<ANoFearDarkVolume>> DarkVolumes;
	TMap<FName, TArray<TWeakObjectPtr<const UObject>>> CircuitSources;
};
