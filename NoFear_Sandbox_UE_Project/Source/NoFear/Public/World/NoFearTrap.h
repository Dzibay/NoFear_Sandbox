#pragma once

#include "CoreMinimal.h"
#include "Core/NoFearInteractableComponent.h"
#include "GameFramework/Actor.h"
#include "NoFearTrap.generated.h"

class UBoxComponent;
class UMaterialInterface;
class UNoFearGrenadeData;
class UNoFearItemData;
class UPrimitiveComponent;
class USoundBase;
class UStaticMesh;
class UStaticMeshComponent;

class ANoFearTrap;
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FNoFearTrapSprung, ANoFearTrap*, Trap, APawn*, Victim);

/** Подсказка и действие берутся у ловушки (ANoFearTrap::GetPrompt / InteractBy). */
UCLASS(ClassGroup = (NoFear))
class NOFEAR_API UNoFearTrapInteraction : public UNoFearInteractableComponent
{
	GENERATED_BODY()

public:
	UNoFearTrapInteraction();

	virtual bool CanInteract_Implementation(APawn* User) const override;
	virtual FText GetInteractionPrompt_Implementation(APawn* User) const override;

protected:
	virtual void HandleInteract_Implementation(APawn* User) override;
};

/**
 * Ловушка (Docs/Interactables.md). Поставленная героем (Instigator — игрок; ставится из колеса предметов)
 * срабатывает только на врагов поставившего; стоящая на карте без Instigator — на любого.
 * Свою взведённую ловушку можно забрать обратно в кейс (ItemAsset).
 */
UCLASS(Abstract)
class NOFEAR_API ANoFearTrap : public AActor
{
	GENERATED_BODY()

public:
	ANoFearTrap();

	/** Предмет кейса, который возвращается, когда ловушку забирают. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Trap")
	TSoftObjectPtr<UNoFearItemData> ItemAsset;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Trap")
	FLinearColor Color = FLinearColor(0.35f, 0.35f, 0.38f);

	UPROPERTY(BlueprintAssignable, Category = "NoFear|Trap")
	FNoFearTrapSprung OnSprung;

	UFUNCTION(BlueprintPure, Category = "NoFear|Trap")
	bool IsArmed() const { return bArmed; }

	/** Сработает ли на эту пешку. */
	UFUNCTION(BlueprintPure, Category = "NoFear|Trap")
	virtual bool IsValidVictim(const APawn* Pawn) const;

	/** Забрать в кейс: только взведённую и только свою (или ничью). */
	UFUNCTION(BlueprintPure, Category = "NoFear|Trap")
	virtual bool CanPickUp(const APawn* User) const;

	UFUNCTION(BlueprintCallable, Category = "NoFear|Trap")
	bool PickUp(APawn* User);

	/** Сколько раз срабатывала (для тестов). */
	UFUNCTION(BlueprintPure, Category = "NoFear|Trap")
	int32 GetSprungCount() const { return SprungCount; }

	virtual bool CanInteractBy(const APawn* User) const;
	virtual FText GetPrompt(const APawn* User) const;
	virtual void InteractBy(APawn* User);

	static void GetTraps(const UWorld* World, TArray<ANoFearTrap*>& OutTraps);

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UFUNCTION()
	void HandleTriggerOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);

	/** Пешка вошла в зону срабатывания взведённой ловушки. */
	virtual void Spring(APawn* Victim) PURE_VIRTUAL(ANoFearTrap::Spring, );

	UStaticMeshComponent* MakePart(const TCHAR* Name, USceneComponent* Parent);

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Trap")
	TObjectPtr<USceneComponent> Root;

	/** Зона срабатывания: перекрытия с пешками. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Trap")
	TObjectPtr<UBoxComponent> Trigger;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Trap")
	TObjectPtr<UNoFearTrapInteraction> Interaction;

	UPROPERTY()
	TObjectPtr<UStaticMesh> PlaceholderMesh;

	UPROPERTY()
	TObjectPtr<UMaterialInterface> PlaceholderMaterial;

	bool bArmed = true;
	int32 SprungCount = 0;
};

/**
 * Капкан: наступил — Damage урона и пешка не может сдвинуться HoldTime секунд. Сработавший без жертвы
 * взводится заново: зажать E RearmTime секунд.
 */
UCLASS(Blueprintable)
class NOFEAR_API ANoFearBearTrap : public ANoFearTrap
{
	GENERATED_BODY()

public:
	ANoFearBearTrap();

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Trap|Bear", meta = (ClampMin = "0"))
	float Damage = 35.f;

	/** Сколько секунд держит пойманного. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Trap|Bear", meta = (ClampMin = "0"))
	float HoldTime = 4.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Trap|Bear", meta = (ClampMin = "0.1"))
	float RearmTime = 1.2f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Trap|Bear")
	TObjectPtr<USoundBase> SnapSound;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Trap|Bear")
	TObjectPtr<USoundBase> ArmSound;

	/** Кто сейчас в капкане. */
	UFUNCTION(BlueprintPure, Category = "NoFear|Trap")
	APawn* GetHeldPawn() const { return Held.Get(); }

	/** Взвести сразу (без удержания E). */
	UFUNCTION(BlueprintCallable, Category = "NoFear|Trap")
	bool Rearm(APawn* User);

	virtual bool CanInteractBy(const APawn* User) const override;
	virtual FText GetPrompt(const APawn* User) const override;
	virtual void InteractBy(APawn* User) override;
	virtual void Tick(float DeltaSeconds) override;

protected:
	virtual void BeginPlay() override;
	virtual void Spring(APawn* Victim) override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Trap")
	TObjectPtr<UStaticMeshComponent> Plate;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Trap")
	TObjectPtr<USceneComponent> LeftHinge;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Trap")
	TObjectPtr<USceneComponent> RightHinge;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Trap")
	TObjectPtr<UStaticMeshComponent> LeftJaw;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Trap")
	TObjectPtr<UStaticMeshComponent> RightJaw;

private:
	void Release();
	void RefreshJaws();

	TWeakObjectPtr<APawn> Held;
	double ReleaseTime = 0.0;
	/** 0 — раскрыт, 1 — захлопнут. */
	float Closed = 0.f;
	TWeakObjectPtr<APawn> Worker;
	float WorkTime = 0.f;
};

/**
 * Растяжка: проволока поперёк прохода между ближайшими стенами слева и справа (до MaxHalfLength в каждую сторону).
 * Задел проволоку — через FuseDelay взрыв как от гранаты Explosion.
 */
UCLASS(Blueprintable)
class NOFEAR_API ANoFearTripwire : public ANoFearTrap
{
	GENERATED_BODY()

public:
	ANoFearTripwire();

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Trap|Tripwire", meta = (ClampMin = "30"))
	float MaxHalfLength = 250.f;

	/** На какой высоте натянута проволока. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Trap|Tripwire", meta = (ClampMin = "2"))
	float WireHeight = 14.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Trap|Tripwire", meta = (ClampMin = "0"))
	float FuseDelay = 0.25f;

	/** Сила взрыва. Пусто — граната по умолчанию. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Trap|Tripwire")
	TObjectPtr<UNoFearGrenadeData> Explosion;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Trap|Tripwire")
	TObjectPtr<USoundBase> TriggerSound;

	/** Длина проволоки после натяжки. */
	UFUNCTION(BlueprintPure, Category = "NoFear|Trap")
	float GetWireLength() const { return WireEnd.Y - WireStart.Y; }

	/** Натянуть заново (ищет стены от текущей точки). */
	UFUNCTION(BlueprintCallable, Category = "NoFear|Trap")
	void Stretch();

	virtual FText GetPrompt(const APawn* User) const override;
	virtual void OnConstruction(const FTransform& Transform) override;

protected:
	virtual void BeginPlay() override;
	virtual void Spring(APawn* Victim) override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Trap")
	TObjectPtr<UStaticMeshComponent> Wire;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Trap")
	TObjectPtr<UStaticMeshComponent> LeftPost;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Trap")
	TObjectPtr<UStaticMeshComponent> RightPost;

	/** Заряд у левого конца. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Trap")
	TObjectPtr<UStaticMeshComponent> Charge;

private:
	void Detonate();
	void RefreshShape();

	/** Концы проволоки в осях актора (X = 0). */
	FVector WireStart = FVector(0.f, -100.f, 0.f);
	FVector WireEnd = FVector(0.f, 100.f, 0.f);
	TWeakObjectPtr<APawn> TriggeredBy;
	FTimerHandle FuseTimer;
};
