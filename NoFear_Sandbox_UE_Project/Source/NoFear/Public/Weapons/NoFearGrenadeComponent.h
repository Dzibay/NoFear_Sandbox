#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "NoFearGrenadeComponent.generated.h"

class ANoFearGrenade;
class UInputAction;
class UInputComponent;
class UNoFearGrenadeData;
class UStaticMeshComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FNoFearGrenadeThrownSignature, UNoFearGrenadeComponent*, Component, ANoFearGrenade*, Grenade);

/**
 * Запас гранат и бросок. Есть у каждого ANoFearCharacter.
 *
 * Игрок: зажал G (GrenadeAction) — оружие прячется, поза Grenade из GASP и замах (поза Throw), HUD рисует дугу и
 * круг поражения; отпустил — бросок по взгляду. Боты бросают через ThrowGrenadeAt. Запас задаётся на BeginPlay
 * (StartingGrenades), поэтому новое тело после респауна снова с полным запасом.
 */
UCLASS(ClassGroup = (NoFear), meta = (BlueprintSpawnableComponent))
class NOFEAR_API UNoFearGrenadeComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UNoFearGrenadeComponent();

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grenade")
	TObjectPtr<UNoFearGrenadeData> GrenadeData;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grenade")
	TSubclassOf<ANoFearGrenade> GrenadeClass;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grenade", meta = (ClampMin = "0"))
	int32 MaxGrenades = 3;

	/** Сколько гранат на старте. Боты получают свой запас от контроллера (ANoFearAIController::BotGrenades). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grenade", meta = (ClampMin = "0"))
	int32 StartingGrenades = 3;

	/** Зажать — прицелиться, отпустить — бросить. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grenade|Input")
	TObjectPtr<UInputAction> GrenadeAction;

	/** Поза GASP (OverlayState) с гранатой в руке на время прицеливания. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grenade|Pose")
	FName AimOverlayState = TEXT("Grenade");

	/** Кость, к которой крепится граната в руке. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grenade|Pose")
	FName HandBone = TEXT("hand_r");

	/** Сколько после отпускания до вылета гранаты (рука идёт вперёд), с. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grenade|Pose", meta = (ClampMin = "0"))
	float ReleaseDelay = 0.1f;

	/** Сколько после броска руки ещё заняты, потом оружие возвращается, с. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grenade|Pose", meta = (ClampMin = "0"))
	float RecoverTime = 0.45f;

	/** Бросок бота: замах до вылета гранаты, с. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grenade|Bot", meta = (ClampMin = "0"))
	float BotWindupTime = 0.4f;

	/** Бросок бота: высота дуги (0 — настильно, 1 — свечой). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grenade|Bot", meta = (ClampMin = "0.05", ClampMax = "0.95"))
	float BotArcParam = 0.5f;

	/** Бросок бота: насколько далеко от цели может лечь граната по расчёту пути, см. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grenade|Bot", meta = (ClampMin = "0"))
	float BotLandingTolerance = 220.f;

	UFUNCTION(BlueprintPure, Category = "NoFear|Grenade")
	int32 GetGrenadeCount() const { return GrenadeCount; }

	UFUNCTION(BlueprintCallable, Category = "NoFear|Grenade")
	void SetGrenadeCount(int32 NewCount);

	UFUNCTION(BlueprintCallable, Category = "NoFear|Grenade")
	void AddGrenades(int32 Amount);

	/** Игрок держит G: рисовать дугу. */
	UFUNCTION(BlueprintPure, Category = "NoFear|Grenade")
	bool IsAiming() const { return bAiming; }

	/** Замах, бросок или восстановление после него: руки заняты. */
	UFUNCTION(BlueprintPure, Category = "NoFear|Grenade")
	bool IsBusy() const { return bAiming || bThrowing; }

	/** Можно ли начать бросок сейчас (жив, есть гранаты, руки свободны). */
	UFUNCTION(BlueprintPure, Category = "NoFear|Grenade")
	bool CanThrow() const;

	UFUNCTION(BlueprintCallable, Category = "NoFear|Grenade")
	void BeginAim();

	UFUNCTION(BlueprintCallable, Category = "NoFear|Grenade")
	void ReleaseThrow();

	UFUNCTION(BlueprintCallable, Category = "NoFear|Grenade")
	void CancelAim();

	/** Откуда вылетает граната и с какой скоростью при броске по взгляду. */
	UFUNCTION(BlueprintPure, Category = "NoFear|Grenade")
	void GetAimedThrow(FVector& OutStart, FVector& OutVelocity) const;

	/** Дуга броска по взгляду (для HUD). false — нет данных или некуда. */
	UFUNCTION(BlueprintCallable, Category = "NoFear|Grenade")
	bool PredictArc(TArray<FVector>& OutPoints, FVector& OutLanding) const;

	/**
	 * Бросок бота в точку: подбирает дугу, проверяет, что граната ляжет рядом (стены, потолок). false — не вышло,
	 * гранат нет или руки заняты.
	 */
	UFUNCTION(BlueprintCallable, Category = "NoFear|Grenade")
	bool ThrowGrenadeAt(FVector Target);

	/** Можно ли докинуть до точки (без броска). */
	UFUNCTION(BlueprintPure, Category = "NoFear|Grenade")
	bool CanReach(FVector Target) const;

	UFUNCTION(BlueprintPure, Category = "NoFear|Grenade")
	float GetRadius() const;

	/** Последняя брошенная граната (для отладки и тестов). */
	UFUNCTION(BlueprintPure, Category = "NoFear|Grenade")
	ANoFearGrenade* GetLastGrenade() const { return LastGrenade.Get(); }

	UPROPERTY(BlueprintAssignable, Category = "NoFear|Grenade")
	FNoFearGrenadeThrownSignature OnGrenadeThrown;

	/** Привязать ввод. Вызывается из ANoFearCharacter::SetupPlayerInputComponent. */
	void SetupPlayerInput(UInputComponent* PlayerInputComponent);

	/** Владелец умер: отменить бросок. */
	void HandleOwnerDeath();

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	void OnGrenadeStarted();
	void OnGrenadeCompleted();
	void OnGrenadeCanceled();

	FVector GetThrowStart(const FRotator& Facing) const;
	bool FindVelocityTo(const FVector& Start, const FVector& Target, FVector& OutVelocity) const;
	void SetHeldVisible(bool bVisible);
	void SpawnGrenade(FVector Start, FVector Velocity);
	void SpawnAimedGrenade();
	void FinishThrow();
	void EndBusy();

	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> HeldMesh;

	TWeakObjectPtr<ANoFearGrenade> LastGrenade;
	FTimerHandle ReleaseTimer;
	FTimerHandle RecoverTimer;
	FVector BotStart = FVector::ZeroVector;
	FVector BotVelocity = FVector::ZeroVector;
	int32 GrenadeCount = 0;
	/** Запас уже задан (SetGrenadeCount до BeginPlay — контроллер бота при захвате). */
	bool bCountSet = false;
	bool bAiming = false;
	bool bThrowing = false;
	bool bOwnerDead = false;
};
