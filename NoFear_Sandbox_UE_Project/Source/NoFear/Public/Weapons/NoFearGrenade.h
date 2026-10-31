#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "NoFearGrenade.generated.h"

class UNoFearGrenadeData;
class UPointLightComponent;
class UProjectileMovementComponent;
class USphereComponent;
class UStaticMeshComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FNoFearGrenadeExplodedSignature, ANoFearGrenade*, Grenade);

/**
 * Брошенная граната: летит по дуге, отскакивает от стен и пола, через FuseTime после броска взрывается.
 *
 * Упав (первый отскок) и потом каждые DangerNotifyInterval секунд, сообщает об опасности ботам, которые её видят
 * или слышат рядом: они убегают (ANoFearAIController::HandleDanger). Взрыв ранит только тех, до кого есть прямая
 * линия от центра (стены и укрытия защищают), толкает рэгдоллы, шумит и подавляет ботов поблизости.
 * Instigator — кто бросил; Data — настройки (UNoFearGrenadeData).
 */
UCLASS(Blueprintable)
class NOFEAR_API ANoFearGrenade : public AActor
{
	GENERATED_BODY()

public:
	ANoFearGrenade();

	/** Настройки. Задаются до BeginPlay (SpawnActorDeferred) или до Launch. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grenade", meta = (ExposeOnSpawn = true))
	TObjectPtr<UNoFearGrenadeData> Data;

	/** Бросить со скоростью Velocity (см/с): запускается полёт и запал. */
	UFUNCTION(BlueprintCallable, Category = "NoFear|Grenade")
	void Launch(FVector Velocity);

	/** Взорвать сейчас (запал истёк). */
	UFUNCTION(BlueprintCallable, Category = "NoFear|Grenade")
	void Explode();

	UFUNCTION(BlueprintPure, Category = "NoFear|Grenade")
	bool HasExploded() const { return bExploded; }

	/** Уже упала (был хотя бы один отскок). */
	UFUNCTION(BlueprintPure, Category = "NoFear|Grenade")
	bool HasLanded() const { return bLanded; }

	/** Сколько секунд до взрыва (0 — взорвалась или ещё не брошена). */
	UFUNCTION(BlueprintPure, Category = "NoFear|Grenade")
	float GetTimeToExplode() const;

	UFUNCTION(BlueprintPure, Category = "NoFear|Grenade")
	float GetRadius() const;

	UFUNCTION(BlueprintPure, Category = "NoFear|Grenade")
	UProjectileMovementComponent* GetProjectileMovement() const { return ProjectileMovement; }

	/** Сколько урона нанёс взрыв (для отладки и тестов). */
	UFUNCTION(BlueprintPure, Category = "NoFear|Grenade")
	float GetTotalDamageDealt() const { return TotalDamageDealt; }

	/** Сколько персонажей ранил взрыв. */
	UFUNCTION(BlueprintPure, Category = "NoFear|Grenade")
	int32 GetVictimCount() const { return VictimCount; }

	/** Сколько персонажей в радиусе укрылись от взрыва за препятствием. */
	UFUNCTION(BlueprintPure, Category = "NoFear|Grenade")
	int32 GetShieldedCount() const { return ShieldedCount; }

	UPROPERTY(BlueprintAssignable, Category = "NoFear|Grenade")
	FNoFearGrenadeExplodedSignature OnExploded;

	/** Как часто лежащая граната напоминает ботам об опасности, с. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grenade|Danger", meta = (ClampMin = "0.1"))
	float DangerNotifyInterval = 0.4f;

	/** Видит гранату только бот, у которого она в пределах этого угла от взгляда, градусы. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grenade|Danger", meta = (ClampMin = "0", ClampMax = "180"))
	float DangerViewAngle = 80.f;

	/** Сколько живёт вспышка после взрыва, с. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grenade|FX", meta = (ClampMin = "0.05"))
	float FlashTime = 0.45f;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaSeconds) override;

	UFUNCTION()
	void HandleBounce(const FHitResult& ImpactResult, const FVector& ImpactVelocity);

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Grenade")
	TObjectPtr<USphereComponent> Collision;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Grenade")
	TObjectPtr<UStaticMeshComponent> Mesh;

	/** Шар взрыва: расширяется и гаснет за FlashTime. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Grenade")
	TObjectPtr<UStaticMeshComponent> FlashMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Grenade")
	TObjectPtr<UPointLightComponent> FlashLight;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Grenade")
	TObjectPtr<UProjectileMovementComponent> ProjectileMovement;

private:
	/** Боты рядом, которые видят или слышат гранату, узнают об опасности. */
	void NotifyDanger();
	/**
	 * Есть ли прямая линия от From до To: стены и укрытия (WorldStatic и WorldDynamic — блоки уровня GASP подвижные)
	 * закрывают, персонажи, их оружие и другие гранаты — нет.
	 */
	bool HasClearLine(const FVector& From, const FVector& To, const AActor* Target) const;
	void ApplyBlast(const FVector& Center);

	FTimerHandle FuseTimer;
	FTimerHandle DangerTimer;
	double LaunchTime = -1.0;
	double ExplodeTime = -1.0;
	bool bExploded = false;
	bool bLanded = false;
	float TotalDamageDealt = 0.f;
	int32 VictimCount = 0;
	int32 ShieldedCount = 0;
	double LastBounceSoundTime = -1.0;
};
