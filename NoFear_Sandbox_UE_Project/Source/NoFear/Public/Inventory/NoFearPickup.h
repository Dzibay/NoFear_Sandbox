#pragma once

#include "CoreMinimal.h"
#include "Core/NoFearInteractableComponent.h"
#include "GameFramework/Actor.h"
#include "NoFearPickup.generated.h"

class UBoxComponent;
class UMaterialInterface;
class UNoFearItemData;
class USkeletalMeshComponent;
class UStaticMesh;
class UStaticMeshComponent;

/** «[E] Подобрать: …» у предмета на земле. Работает, только если у того, кто жмёт E, есть кейс. */
UCLASS(ClassGroup = (NoFear))
class NOFEAR_API UNoFearPickupInteraction : public UNoFearInteractableComponent
{
	GENERATED_BODY()

public:
	UNoFearPickupInteraction();

	virtual bool CanInteract_Implementation(APawn* User) const override;
	virtual FText GetInteractionPrompt_Implementation(APawn* User) const override;

protected:
	virtual void HandleInteract_Implementation(APawn* User) override;
};

/**
 * Предмет на земле (Docs/Inventory_Design.md): ставится на уровень или падает из кейса при выбросе.
 * E кладёт его в кейс автораскладкой; не влезло всё — остаток лежит дальше. Ствол помнит патроны в магазине.
 * Без WorldMesh у предмета ствол показывается своим мешем из WeaponData, остальное — цветным бруском по размеру в клетках.
 */
UCLASS(Blueprintable)
class NOFEAR_API ANoFearPickup : public AActor
{
	GENERATED_BODY()

public:
	ANoFearPickup();

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Pickup")
	TObjectPtr<UNoFearItemData> Item;

	/** Сколько штук. 0 — сколько задано у предмета (PickupCount или полная стопка). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Pickup", meta = (ClampMin = "0"))
	int32 Count = 0;

	/** Патроны в магазине ствола. -1 — полный. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Pickup", meta = (ClampMin = "-1"))
	int32 MagazineAmmo = -1;

	/** Падать и катиться. Выключено — висит там, где поставлен. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Pickup")
	bool bSimulatePhysics = true;

	/** Сторона клетки кейса в сантиметрах для бруска-заглушки. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Pickup", meta = (ClampMin = "1"))
	float PlaceholderCellSize = 10.f;

	UFUNCTION(BlueprintCallable, Category = "NoFear|Pickup")
	void SetItem(UNoFearItemData* NewItem, int32 NewCount = 0, int32 NewMagazineAmmo = -1);

	/** Сколько штук лежит (Count 0 уже заменён числом по умолчанию). */
	UFUNCTION(BlueprintPure, Category = "NoFear|Pickup")
	int32 GetCount() const;

	/** «Пистолетные патроны ×30». */
	UFUNCTION(BlueprintPure, Category = "NoFear|Pickup")
	FText GetPickupName() const;

	/** Положить в кейс того, кто подбирает. false — нет кейса или ни одна штука не влезла. */
	UFUNCTION(BlueprintCallable, Category = "NoFear|Pickup")
	bool TryPickUp(APawn* User);

	/** Создать предмет на земле с толчком Velocity (выброс из кейса, остаток, который не влез). */
	static ANoFearPickup* SpawnPickup(UWorld* World, UNoFearItemData* Item, int32 Count, int32 MagazineAmmo, const FTransform& Transform, const FVector& Velocity);

	UBoxComponent* GetBox() const { return Box; }

	virtual void OnConstruction(const FTransform& Transform) override;

protected:
	virtual void BeginPlay() override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Pickup")
	TObjectPtr<UBoxComponent> Box;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Pickup")
	TObjectPtr<UStaticMeshComponent> Mesh;

	/** Меш ствола (у оружия скелетный). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Pickup")
	TObjectPtr<USkeletalMeshComponent> WeaponMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Pickup")
	TObjectPtr<UNoFearPickupInteraction> Interaction;

private:
	/** Меш по предмету и коробка столкновений по мешу. */
	void RefreshAppearance();

	UPROPERTY()
	TObjectPtr<UStaticMesh> PlaceholderMesh;

	UPROPERTY()
	TObjectPtr<UMaterialInterface> PlaceholderMaterial;
};
