#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "NoFearItemData.generated.h"

class UNoFearGrenadeData;
class UNoFearWeaponData;
class UStaticMesh;
class UTexture2D;

UENUM(BlueprintType)
enum class ENoFearItemKind : uint8
{
	Weapon,
	Ammo,
	Grenade,
	Medicine,
	/** Не ложится в сетку: при подборе кейс сразу становится больше. */
	CaseExpansion
};

/**
 * Предмет кейса (Docs/Inventory_Design.md): что это, сколько клеток занимает, как складывается в стопку
 * и как выглядит на земле. Один ассет на вид предмета: DA_Item_Pistol, DA_Item_RifleAmmo, DA_Item_Bandage…
 */
UCLASS(BlueprintType)
class NOFEAR_API UNoFearItemData : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Item")
	FText DisplayName;

	/** Текст карточки предмета в экране кейса. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Item", meta = (MultiLine = true))
	FText Description;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Item")
	TObjectPtr<UTexture2D> Icon = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Item")
	ENoFearItemKind Kind = ENoFearItemKind::Ammo;

	/** Клеток по ширине и высоте без поворота. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Item|Grid")
	FIntPoint Size = FIntPoint(1, 1);

	/** Сколько штук в одной клетке-стопке. У оружия всегда 1. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Item|Grid", meta = (ClampMin = "1"))
	int32 MaxStack = 1;

	/** Цвет плашки в сетке кейса и заглушки на земле. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Item|Grid")
	FLinearColor Color = FLinearColor(0.5f, 0.5f, 0.5f);

	// --- На земле ---

	/** Меш предмета на земле. Пусто — у ствола его меш из WeaponData, у остального цветной брусок по размеру в клетках. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Item|World")
	TObjectPtr<UStaticMesh> WorldMesh = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Item|World")
	FVector WorldMeshScale = FVector::OneVector;

	/** Поворот меша на земле: ствол кладём на бок. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Item|World")
	FRotator WorldMeshRotation = FRotator::ZeroRotator;

	/** Сколько штук в предмете, поставленном на уровень без явного числа. 0 — полная стопка. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Item|World", meta = (ClampMin = "0"))
	int32 PickupCount = 0;

	// --- По виду ---

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Item|Weapon", meta = (EditCondition = "Kind == ENoFearItemKind::Weapon", EditConditionHides))
	TObjectPtr<UNoFearWeaponData> WeaponData = nullptr;

	/** Чем заряжается: предмет-патроны, из которых перезарядка берёт патроны. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Item|Weapon", meta = (EditCondition = "Kind == ENoFearItemKind::Weapon", EditConditionHides))
	TObjectPtr<UNoFearItemData> AmmoItem = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Item|Grenade", meta = (EditCondition = "Kind == ENoFearItemKind::Grenade", EditConditionHides))
	TObjectPtr<UNoFearGrenadeData> GrenadeData = nullptr;

	/** Сколько здоровья возвращает. Ноль при bHealToFull не важен. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Item|Medicine", meta = (EditCondition = "Kind == ENoFearItemKind::Medicine", EditConditionHides))
	float HealAmount = 40.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Item|Medicine", meta = (EditCondition = "Kind == ENoFearItemKind::Medicine", EditConditionHides))
	bool bHealToFull = false;

	/** Сколько секунд длится лечение. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Item|Medicine", meta = (EditCondition = "Kind == ENoFearItemKind::Medicine", EditConditionHides, ClampMin = "0"))
	float UseTime = 3.f;

	UFUNCTION(BlueprintPure, Category = "NoFear|Item")
	FText GetDisplayName() const;

	UFUNCTION(BlueprintPure, Category = "NoFear|Item")
	FIntPoint GetGridSize() const { return FIntPoint(FMath::Max(Size.X, 1), FMath::Max(Size.Y, 1)); }

	UFUNCTION(BlueprintPure, Category = "NoFear|Item")
	int32 GetMaxStack() const { return Kind == ENoFearItemKind::Weapon ? 1 : FMath::Max(MaxStack, 1); }

	/** Число штук для предмета на уровне: PickupCount или полная стопка. */
	int32 GetDefaultPickupCount() const { return PickupCount > 0 ? PickupCount : GetMaxStack(); }
};
