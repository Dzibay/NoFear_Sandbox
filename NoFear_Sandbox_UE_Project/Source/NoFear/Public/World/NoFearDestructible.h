#pragma once

#include "CoreMinimal.h"
#include "Core/NoFearInteractableComponent.h"
#include "GameFramework/Actor.h"
#include "World/NoFearChest.h"
#include "NoFearDestructible.generated.h"

class UMaterialInterface;
class UNavModifierComponent;
class UNoFearGrenadeData;
class UNoFearHealthComponent;
class UPointLightComponent;
class USoundBase;
class UStaticMesh;
class UStaticMeshComponent;
struct FNoFearDamageInfo;

class ANoFearDestructible;
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FNoFearDestructibleBroken, ANoFearDestructible*, Destructible, AActor*, Breaker);

/** Подсказка и действие берутся у объекта (ANoFearDestructible::GetPrompt / InteractBy). */
UCLASS(ClassGroup = (NoFear))
class NOFEAR_API UNoFearDestructibleInteraction : public UNoFearInteractableComponent
{
	GENERATED_BODY()

public:
	UNoFearDestructibleInteraction();

	virtual bool CanInteract_Implementation(APawn* User) const override;
	virtual FText GetInteractionPrompt_Implementation(APawn* User) const override;

protected:
	virtual void HandleInteract_Implementation(APawn* User) override;
};

/**
 * Разрушаемый предмет (Docs/Interactables.md): корпус-заглушка, прочность (UNoFearHealthComponent — пули, взрывы,
 * удары), вырез в навмеше, пока цел. Сломанный разлетается обломками, шумит и пропадает.
 */
UCLASS(Abstract)
class NOFEAR_API ANoFearDestructible : public AActor
{
	GENERATED_BODY()

public:
	ANoFearDestructible();

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Destructible")
	FLinearColor Color = FLinearColor(0.5f, 0.5f, 0.5f);

	/** Меш корпуса: растягивается на размер. Пусто — заглушка. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Destructible")
	TObjectPtr<UStaticMesh> BodyMesh;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Destructible|Sound")
	TObjectPtr<USoundBase> BreakSound;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Destructible|Sound", meta = (ClampMin = "0"))
	float NoiseLoudness = 0.6f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Destructible|Sound", meta = (ClampMin = "0"))
	float NoiseRange = 1000.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Destructible", meta = (ClampMin = "0"))
	int32 DebrisCount = 8;

	UPROPERTY(BlueprintAssignable, Category = "NoFear|Destructible")
	FNoFearDestructibleBroken OnBroken;

	UFUNCTION(BlueprintPure, Category = "NoFear|Destructible")
	bool IsBroken() const { return bBroken; }

	UFUNCTION(BlueprintPure, Category = "NoFear|Destructible")
	float GetHealth() const;

	/** Сломать сразу. */
	UFUNCTION(BlueprintCallable, Category = "NoFear|Destructible")
	void Break(AActor* Breaker);

	virtual bool CanInteractBy(const APawn* User) const { return false; }
	virtual FText GetPrompt(const APawn* User) const { return FText::GetEmpty(); }
	virtual void InteractBy(APawn* User) {}

	virtual void OnConstruction(const FTransform& Transform) override;
	virtual float TakeDamage(float DamageAmount, const FDamageEvent& DamageEvent, AController* EventInstigator, AActor* DamageCauser) override;

protected:
	virtual void BeginPlay() override;

	/** Размер корпуса (см) и его центр в осях актора. */
	virtual FVector GetBodySize() const { return FVector(60.f); }
	virtual FVector GetBodyCenter() const { return FVector(0.f, 0.f, GetBodySize().Z * 0.5f); }
	virtual UStaticMesh* GetPlaceholderMesh() const { return CubeMesh; }

	/** Урон прошёл (Applied > 0), объект ещё цел. */
	virtual void OnDamaged(float Applied, AActor* Breaker) {}

	/** Объект только что сломан: корпус уже скрыт, навмеш открыт. */
	virtual void HandleBroken(AActor* Breaker);

	void RefreshShape();
	static AActor* GetBreaker(AController* EventInstigator, AActor* DamageCauser);

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Destructible")
	TObjectPtr<USceneComponent> Root;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Destructible")
	TObjectPtr<UStaticMeshComponent> Body;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Destructible")
	TObjectPtr<UNoFearHealthComponent> Health;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Destructible")
	TObjectPtr<UNavModifierComponent> NavBlocker;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Destructible")
	TObjectPtr<UNoFearDestructibleInteraction> Interaction;

	UPROPERTY()
	TObjectPtr<UStaticMesh> CubeMesh;

	UPROPERTY()
	TObjectPtr<UStaticMesh> CylinderMesh;

	UPROPERTY()
	TObjectPtr<UMaterialInterface> PlaceholderMaterial;

private:
	UFUNCTION()
	void HandleDeath(UNoFearHealthComponent* Component, const FNoFearDamageInfo& Info);

	bool bBroken = false;
};

/**
 * Взрывоопасная бочка. Ниже BurnHealthFraction прочности загорается: огонь, боты разбегаются, через BurnTime взрыв.
 * Прочность кончилась — взрыв через короткий запал (соседние бочки рвутся цепочкой).
 */
UCLASS(Blueprintable)
class NOFEAR_API ANoFearExplosiveBarrel : public ANoFearDestructible
{
	GENERATED_BODY()

public:
	ANoFearExplosiveBarrel();

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Barrel", meta = (ClampMin = "10"))
	float Diameter = 60.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Barrel", meta = (ClampMin = "20"))
	float BarrelHeight = 90.f;

	/** Сила взрыва. Пусто — граната по умолчанию. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Barrel")
	TObjectPtr<UNoFearGrenadeData> Explosion;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Barrel", meta = (ClampMin = "0", ClampMax = "1"))
	float BurnHealthFraction = 0.5f;

	/** Сколько горит до взрыва. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Barrel", meta = (ClampMin = "0.1"))
	float BurnTime = 4.f;

	/** Запал после того, как прочность кончилась: случайно из диапазона, чтобы цепочка рвалась по очереди. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Barrel", meta = (ClampMin = "0"))
	FVector2D FuseRange = FVector2D(0.15f, 0.4f);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Barrel")
	TObjectPtr<USoundBase> IgniteSound;

	UFUNCTION(BlueprintPure, Category = "NoFear|Barrel")
	bool IsBurning() const { return bBurning; }

	UFUNCTION(BlueprintPure, Category = "NoFear|Barrel")
	bool HasExploded() const { return bExploded; }

	/** Поджечь (огонь, через BurnTime взрыв). */
	UFUNCTION(BlueprintCallable, Category = "NoFear|Barrel")
	void Ignite(AActor* Igniter);

	virtual void Tick(float DeltaSeconds) override;

protected:
	virtual FVector GetBodySize() const override { return FVector(Diameter, Diameter, BarrelHeight); }
	virtual UStaticMesh* GetPlaceholderMesh() const override { return CylinderMesh; }
	virtual void OnDamaged(float Applied, AActor* Breaker) override;
	virtual void HandleBroken(AActor* Breaker) override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Barrel")
	TObjectPtr<UPointLightComponent> Fire;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Barrel")
	TObjectPtr<UStaticMeshComponent> Flame;

private:
	void Detonate();
	void ReportDanger();

	bool bBurning = false;
	bool bExploded = false;
	float BurnLeft = 0.f;
	float DangerTimer = 0.f;
	TWeakObjectPtr<APawn> LastInstigator;
	FTimerHandle FuseTimer;
};

/**
 * Ломаемый ящик: ломается уроном или героем (зажать E OpenTime секунд). Содержимое высыпается на пол подбираемыми
 * предметами.
 */
UCLASS(Blueprintable)
class NOFEAR_API ANoFearBreakableCrate : public ANoFearDestructible
{
	GENERATED_BODY()

public:
	ANoFearBreakableCrate();

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Crate")
	FVector Size = FVector(70.f, 70.f, 60.f);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Crate")
	TArray<FNoFearChestItem> Contents;

	/** Сколько держать E, чтобы разбить. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Crate", meta = (ClampMin = "0.1"))
	float OpenTime = 1.f;

	/** Прогресс удержания E, 0..1 (для HUD и тестов). */
	UFUNCTION(BlueprintPure, Category = "NoFear|Crate")
	float GetWorkProgress() const;

	virtual bool CanInteractBy(const APawn* User) const override;
	virtual FText GetPrompt(const APawn* User) const override;
	virtual void InteractBy(APawn* User) override;
	virtual void Tick(float DeltaSeconds) override;

protected:
	virtual FVector GetBodySize() const override { return Size; }
	virtual void HandleBroken(AActor* Breaker) override;

private:
	TWeakObjectPtr<APawn> Worker;
	float WorkTime = 0.f;
};

/**
 * Окно в проёме стены: мутное стекло не пропускает ни взгляд, ни пули, ни пешек. Первый же урон или E — стекло
 * бьётся с громким звоном; проём остаётся (подоконник высотой SillHeight). Точка актора — середина проёма на полу,
 * ось X — нормаль. Стену под окном и над ним строит уровень.
 */
UCLASS(Blueprintable)
class NOFEAR_API ANoFearWindow : public ANoFearDestructible
{
	GENERATED_BODY()

public:
	ANoFearWindow();

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Window", meta = (ClampMin = "40"))
	float Width = 120.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Window", meta = (ClampMin = "0"))
	float SillHeight = 90.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Window", meta = (ClampMin = "30"))
	float WindowHeight = 110.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Window")
	FLinearColor FrameColor = FLinearColor(0.3f, 0.22f, 0.15f);

	virtual bool CanInteractBy(const APawn* User) const override;
	virtual FText GetPrompt(const APawn* User) const override;
	virtual void InteractBy(APawn* User) override;
	virtual void OnConstruction(const FTransform& Transform) override;

protected:
	virtual FVector GetBodySize() const override { return FVector(4.f, Width, WindowHeight); }
	virtual FVector GetBodyCenter() const override { return FVector(0.f, 0.f, SillHeight + WindowHeight * 0.5f); }
	virtual void HandleBroken(AActor* Breaker) override;

	/** Рама: подоконник, перекладина сверху и стойки. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Window")
	TArray<TObjectPtr<UStaticMeshComponent>> FrameParts;

private:
	void RefreshFrame();
};
