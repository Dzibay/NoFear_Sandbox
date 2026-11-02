#pragma once

#include "CoreMinimal.h"
#include "World/NoFearDestructible.h"
#include "NoFearSignals.generated.h"

class UAudioComponent;
class UPointLightComponent;
class USoundBase;
class UStaticMeshComponent;

class ANoFearRadio;
class ANoFearSiren;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FNoFearRadioChanged, ANoFearRadio*, Radio, bool, bOn);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FNoFearSirenChanged, ANoFearSiren*, Siren, bool, bSounding);

/**
 * Радио на батарейках (Docs/Interactables.md): E включает и выключает. Пока играет, шумит раз в NoiseInterval —
 * монстры идут на звук, так что включённое радио — приманка. Батарея садится за BatteryTime секунд работы.
 * При первом включении на экран выводится Broadcast. Ломается от урона.
 */
UCLASS(Blueprintable)
class NOFEAR_API ANoFearRadio : public ANoFearDestructible
{
	GENERATED_BODY()

public:
	ANoFearRadio();

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Radio")
	FVector Size = FVector(34.f, 14.f, 20.f);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Radio")
	bool bStartOn = false;

	/** Секунд работы от полной батареи. 0 — без батареи (вечно). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Radio", meta = (ClampMin = "0"))
	float BatteryTime = 60.f;

	/** Что говорит эфир при первом включении (пусто — ничего). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Radio", meta = (MultiLine = true))
	FText Broadcast;

	/** Петля эфира (музыка, шипение). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Radio|Sound")
	TObjectPtr<USoundBase> PlaySound;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Radio|Sound")
	TObjectPtr<USoundBase> ClickSound;

	/** Шум эфира для слуха ботов: громкость, дальность, раз в PlayNoiseInterval. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Radio|Sound", meta = (ClampMin = "0"))
	float PlayNoiseLoudness = 0.7f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Radio|Sound", meta = (ClampMin = "0"))
	float PlayNoiseRange = 2500.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Radio|Sound", meta = (ClampMin = "0.2"))
	float PlayNoiseInterval = 1.5f;

	UPROPERTY(BlueprintAssignable, Category = "NoFear|Radio")
	FNoFearRadioChanged OnRadioChanged;

	UFUNCTION(BlueprintPure, Category = "NoFear|Radio")
	bool IsOn() const { return bOn; }

	/** Сколько секунд работы осталось. */
	UFUNCTION(BlueprintPure, Category = "NoFear|Radio")
	float GetBattery() const { return Battery; }

	UFUNCTION(BlueprintCallable, Category = "NoFear|Radio")
	void SetBattery(float Seconds);

	/** Включить или выключить от User. false — сломано или батарея села. */
	UFUNCTION(BlueprintCallable, Category = "NoFear|Radio")
	bool SetOn(bool bNewOn, APawn* User = nullptr);

	virtual bool CanInteractBy(const APawn* User) const override;
	virtual FText GetPrompt(const APawn* User) const override;
	virtual void InteractBy(APawn* User) override;
	virtual void Tick(float DeltaSeconds) override;

protected:
	virtual void BeginPlay() override;
	virtual FVector GetBodySize() const override { return Size; }
	virtual void HandleBroken(AActor* Breaker) override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Radio")
	TObjectPtr<UAudioComponent> Audio;

	/** Огонёк шкалы: горит, пока играет. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Radio")
	TObjectPtr<UPointLightComponent> DialLight;

private:
	bool bOn = false;
	bool bBroadcastShown = false;
	float Battery = 0.f;
	float NoiseTime = 0.f;
};

/**
 * Сирена (Docs/Interactables.md): шкаф с рупором и мигалкой. Держать E ActivateTime — воет Duration секунд, очень
 * громко: все монстры в NoiseRange идут на звук (отвлечь от укрытия или собрать в одно место). E на воющей —
 * выключить. Нужна линия питания Circuit (пусто — работает всегда). После воя Cooldown не включается.
 * Сломанная уроном замолкает насовсем.
 */
UCLASS(Blueprintable)
class NOFEAR_API ANoFearSiren : public ANoFearDestructible
{
	GENERATED_BODY()

public:
	ANoFearSiren();

	/** Линия питания (UNoFearLightSubsystem). Пусто — питание не нужно. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Siren")
	FName Circuit;

	/** Шкаф управления; рупор с мигалкой — над ним. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Siren")
	FVector Size = FVector(40.f, 30.f, 120.f);

	/** Сколько держать E на включение. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Siren", meta = (ClampMin = "0"))
	float ActivateTime = 1.f;

	/** Сколько воет. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Siren", meta = (ClampMin = "1"))
	float Duration = 15.f;

	/** Сколько ждать после воя до следующего включения. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Siren", meta = (ClampMin = "0"))
	float Cooldown = 20.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Siren|Sound")
	TObjectPtr<USoundBase> WailSound;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Siren|Sound")
	TObjectPtr<USoundBase> ClickSound;

	/** Шум воя для слуха ботов: громкость, дальность, раз в WailNoiseInterval. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Siren|Sound", meta = (ClampMin = "0"))
	float WailNoiseLoudness = 1.5f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Siren|Sound", meta = (ClampMin = "0"))
	float WailNoiseRange = 8000.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Siren|Sound", meta = (ClampMin = "0.2"))
	float WailNoiseInterval = 1.f;

	UPROPERTY(BlueprintAssignable, Category = "NoFear|Siren")
	FNoFearSirenChanged OnSirenChanged;

	UFUNCTION(BlueprintPure, Category = "NoFear|Siren")
	bool IsSounding() const { return bSounding; }

	UFUNCTION(BlueprintPure, Category = "NoFear|Siren")
	bool IsPowered() const;

	/** Сколько секунд до готовности после воя (0 — готова). */
	UFUNCTION(BlueprintPure, Category = "NoFear|Siren")
	float GetCooldownLeft() const { return CooldownLeft; }

	/** Прогресс удержания E, 0..1. */
	UFUNCTION(BlueprintPure, Category = "NoFear|Siren")
	float GetWorkProgress() const;

	/** Включить вой от User. false — сломана, нет питания, остывает или уже воет. */
	UFUNCTION(BlueprintCallable, Category = "NoFear|Siren")
	bool Activate(APawn* User);

	UFUNCTION(BlueprintCallable, Category = "NoFear|Siren")
	void Silence();

	virtual bool CanInteractBy(const APawn* User) const override;
	virtual FText GetPrompt(const APawn* User) const override;
	virtual void InteractBy(APawn* User) override;
	virtual void Tick(float DeltaSeconds) override;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual FVector GetBodySize() const override { return Size; }
	virtual void HandleBroken(AActor* Breaker) override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Siren")
	TObjectPtr<UStaticMeshComponent> Horn;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Siren")
	TObjectPtr<UStaticMeshComponent> Beacon;

	/** Мигалка: красный свет вращается, пока воет. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Siren")
	TObjectPtr<UPointLightComponent> BeaconLight;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Siren")
	TObjectPtr<UAudioComponent> Audio;

private:
	void HandleCircuitChanged(FName ChangedCircuit, bool bPowered);
	void SetSounding(bool bNewSounding, APawn* User);
	void PlaceParts();

	bool bSounding = false;
	float TimeLeft = 0.f;
	float CooldownLeft = 0.f;
	float NoiseTime = 0.f;
	float SpinTime = 0.f;
	TWeakObjectPtr<APawn> Worker;
	float WorkTime = 0.f;
	FDelegateHandle CircuitHandle;
};
