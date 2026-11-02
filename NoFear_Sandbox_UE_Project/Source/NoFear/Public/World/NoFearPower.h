#pragma once

#include "CoreMinimal.h"
#include "Core/NoFearInteractableComponent.h"
#include "GameFramework/Actor.h"
#include "World/NoFearDestructible.h"
#include "NoFearPower.generated.h"

class UMaterialInterface;
class UPointLightComponent;
class USoundBase;
class UStaticMesh;
class UStaticMeshComponent;
class UNoFearItemData;

class ANoFearGenerator;
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FNoFearGeneratorChanged, ANoFearGenerator*, Generator, bool, bRunning);

/**
 * Лампа (Docs/Interactables.md, «Генератор и свет»): точечный свет, который учитывает UNoFearLightSubsystem.
 * Горит, если включена (выключателем), её линия Circuit под питанием и плафон цел. Попадание бьёт плафон.
 * Точка актора — место плафона.
 */
UCLASS(Blueprintable)
class NOFEAR_API ANoFearLamp : public AActor
{
	GENERATED_BODY()

public:
	ANoFearLamp();

	/** Линия питания. None — горит без генератора. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Lamp")
	FName Circuit;

	/** Включена выключателем. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Lamp")
	bool bSwitchedOn = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Lamp", meta = (ClampMin = "0"))
	float Intensity = 4000.f;

	/** Радиус света (см). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Lamp", meta = (ClampMin = "50"))
	float Radius = 700.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Lamp")
	FLinearColor LightColor = FLinearColor(1.f, 0.85f, 0.6f);

	/** Сколько освещённости даёт лампа у себя под плафоном (1 — как днём). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Lamp", meta = (ClampMin = "0", ClampMax = "1"))
	float LightWeight = 1.f;

	/** Мигает, как старая проводка. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Lamp")
	bool bFlicker = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Lamp|Sound")
	TObjectPtr<USoundBase> BreakSound;

	UFUNCTION(BlueprintCallable, Category = "NoFear|Lamp")
	void SetSwitchedOn(bool bOn);

	/** Горит прямо сейчас. */
	UFUNCTION(BlueprintPure, Category = "NoFear|Lamp")
	bool IsLit() const { return bLit; }

	UFUNCTION(BlueprintPure, Category = "NoFear|Lamp")
	bool IsBroken() const { return bBroken; }

	/** Есть питание на линии лампы. */
	UFUNCTION(BlueprintPure, Category = "NoFear|Lamp")
	bool IsPowered() const;

	/** Разбить плафон: звон, лампа гаснет насовсем. */
	UFUNCTION(BlueprintCallable, Category = "NoFear|Lamp")
	void BreakBulb(AActor* Breaker);

	virtual void OnConstruction(const FTransform& Transform) override;
	virtual float TakeDamage(float DamageAmount, const FDamageEvent& DamageEvent, AController* EventInstigator, AActor* DamageCauser) override;
	virtual void Tick(float DeltaSeconds) override;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Lamp")
	TObjectPtr<USceneComponent> Root;

	/** Плафон: ловит пули (Visibility), пешек не держит. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Lamp")
	TObjectPtr<UStaticMeshComponent> Bulb;

	/** Провод вверх от плафона. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Lamp")
	TObjectPtr<UStaticMeshComponent> Cord;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Lamp")
	TObjectPtr<UPointLightComponent> Light;

	UPROPERTY()
	TObjectPtr<UMaterialInterface> PlaceholderMaterial;

private:
	void HandleCircuitChanged(FName ChangedCircuit, bool bPowered);
	void Refresh();

	bool bLit = false;
	bool bBroken = false;
	float FlickerTime = 0.f;
	FDelegateHandle CircuitHandle;
};

/** Подсказка и действие берутся у выключателя. */
UCLASS(ClassGroup = (NoFear))
class NOFEAR_API UNoFearLightSwitchInteraction : public UNoFearInteractableComponent
{
	GENERATED_BODY()

public:
	UNoFearLightSwitchInteraction();

	virtual FText GetInteractionPrompt_Implementation(APawn* User) const override;

protected:
	virtual void HandleInteract_Implementation(APawn* User) override;
};

/**
 * Выключатель на стене: E включает и выключает лампы Lamps (или все лампы линии Circuit, если список пуст).
 * Без питания лампы не загорятся — герою об этом скажут. Точка актора — центр пластины, ось X — от стены.
 */
UCLASS(Blueprintable)
class NOFEAR_API ANoFearLightSwitch : public AActor
{
	GENERATED_BODY()

public:
	ANoFearLightSwitch();

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Switch")
	TArray<TObjectPtr<ANoFearLamp>> Lamps;

	/** Если Lamps пуст — все лампы этой линии. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Switch")
	FName Circuit;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Switch")
	bool bOn = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Switch|Sound")
	TObjectPtr<USoundBase> ClickSound;

	UFUNCTION(BlueprintCallable, Category = "NoFear|Switch")
	void Toggle(APawn* User);

	UFUNCTION(BlueprintCallable, Category = "NoFear|Switch")
	void SetOn(bool bNewOn, APawn* User = nullptr);

	UFUNCTION(BlueprintPure, Category = "NoFear|Switch")
	bool IsOn() const { return bOn; }

	/** Лампы под выключателем. */
	UFUNCTION(BlueprintPure, Category = "NoFear|Switch")
	TArray<ANoFearLamp*> GetLamps() const;

	virtual void OnConstruction(const FTransform& Transform) override;

protected:
	virtual void BeginPlay() override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Switch")
	TObjectPtr<USceneComponent> Root;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Switch")
	TObjectPtr<UStaticMeshComponent> Plate;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Switch")
	TObjectPtr<UStaticMeshComponent> Lever;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Switch")
	TObjectPtr<UNoFearLightSwitchInteraction> Interaction;

	UPROPERTY()
	TObjectPtr<UMaterialInterface> PlaceholderMaterial;

private:
	void RefreshLever();
};

/**
 * Генератор: питает линию Circuit, пока работает. Заводится рывками шнура (держать E StartTime; заводится с шансом
 * StartChance, иначе чихает), жжёт топливо и громко тарахтит — монстры идут на шум. E на работающем глушит.
 * Пустой бак заправляется предметом FuelItem из кейса. Сломанный уроном глохнет насовсем.
 */
UCLASS(Blueprintable)
class NOFEAR_API ANoFearGenerator : public ANoFearDestructible
{
	GENERATED_BODY()

public:
	ANoFearGenerator();

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Generator")
	FName Circuit = TEXT("Main");

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Generator")
	FVector Size = FVector(110.f, 70.f, 80.f);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Generator")
	bool bStartRunning = false;

	/** Полный бак, секунды работы. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Generator|Fuel", meta = (ClampMin = "1"))
	float MaxFuel = 180.f;

	/** Топлива в начале игры, доля бака. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Generator|Fuel", meta = (ClampMin = "0", ClampMax = "1"))
	float StartFuelFraction = 1.f;

	/** Предмет-канистра: одна штука — полный бак. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Generator|Fuel")
	TSoftObjectPtr<UNoFearItemData> FuelItem;

	/** Сколько держать E на рывок шнура. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Generator", meta = (ClampMin = "0.1"))
	float StartTime = 1.2f;

	/** Шанс завестись с рывка. Неудача — чих и шум. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Generator", meta = (ClampMin = "0", ClampMax = "1"))
	float StartChance = 0.6f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Generator|Sound")
	TObjectPtr<USoundBase> PullSound;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Generator|Sound")
	TObjectPtr<USoundBase> RunSound;

	/** Шум работы для слуха ботов: громкость и дальность, раз в RunNoiseInterval. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Generator|Sound", meta = (ClampMin = "0"))
	float RunNoiseLoudness = 0.8f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Generator|Sound", meta = (ClampMin = "0"))
	float RunNoiseRange = 3000.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Generator|Sound", meta = (ClampMin = "0.2"))
	float RunNoiseInterval = 2.f;

	UPROPERTY(BlueprintAssignable, Category = "NoFear|Generator")
	FNoFearGeneratorChanged OnRunningChanged;

	UFUNCTION(BlueprintPure, Category = "NoFear|Generator")
	bool IsRunning() const { return bRunning; }

	UFUNCTION(BlueprintPure, Category = "NoFear|Generator")
	float GetFuel() const { return Fuel; }

	UFUNCTION(BlueprintPure, Category = "NoFear|Generator")
	float GetFuelFraction() const { return Fuel / FMath::Max(MaxFuel, 1.f); }

	UFUNCTION(BlueprintCallable, Category = "NoFear|Generator")
	void SetFuel(float Seconds);

	/** Один рывок шнура от User: заводится с шансом StartChance. true — завёлся. */
	UFUNCTION(BlueprintCallable, Category = "NoFear|Generator")
	bool PullStart(APawn* User);

	UFUNCTION(BlueprintCallable, Category = "NoFear|Generator")
	void Stop();

	/** Залить канистру из кейса User. */
	UFUNCTION(BlueprintCallable, Category = "NoFear|Generator")
	bool Refuel(APawn* User);

	/** Прогресс удержания E, 0..1 (для HUD и тестов). */
	UFUNCTION(BlueprintPure, Category = "NoFear|Generator")
	float GetWorkProgress() const;

	virtual bool CanInteractBy(const APawn* User) const override;
	virtual FText GetPrompt(const APawn* User) const override;
	virtual void InteractBy(APawn* User) override;
	virtual void Tick(float DeltaSeconds) override;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual FVector GetBodySize() const override { return Size; }
	virtual void HandleBroken(AActor* Breaker) override;

	/** Огонёк на корпусе: зелёный — работает. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Generator")
	TObjectPtr<UPointLightComponent> StatusLight;

private:
	void SetRunning(bool bNewRunning);
	bool HasFuelItem(const APawn* User) const;
	void FinishWork(APawn* User);

	bool bRunning = false;
	float Fuel = 0.f;
	float NoiseTime = 0.f;
	float ShakeTime = 0.f;
	TWeakObjectPtr<APawn> Worker;
	float WorkTime = 0.f;
};
