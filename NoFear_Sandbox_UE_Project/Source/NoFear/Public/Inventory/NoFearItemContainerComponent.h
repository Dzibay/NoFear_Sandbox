#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "NoFearItemContainerComponent.generated.h"

class ANoFearWeapon;
class UNoFearItemContainerComponent;
class UNoFearItemData;

/** Сколько и чего: стартовое содержимое кейса или хранилища. */
USTRUCT(BlueprintType)
struct FNoFearItemStack
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory")
	TSoftObjectPtr<UNoFearItemData> Item;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory", meta = (ClampMin = "1"))
	int32 Count = 1;

	/** Патроны в магазине ствола, -1 — полный. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory")
	int32 MagazineAmmo = -1;
};

/** Предмет, лежащий в сетке: где лежит, повёрнут ли, сколько штук в стопке. */
USTRUCT(BlueprintType)
struct FNoFearInventoryEntry
{
	GENERATED_BODY()

	/** Номер предмета в этой сетке, не меняется, пока предмет в ней лежит. 0 — нет предмета. */
	UPROPERTY(BlueprintReadOnly, Category = "Inventory")
	int32 Id = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Inventory")
	TObjectPtr<UNoFearItemData> Item = nullptr;

	UPROPERTY(BlueprintReadOnly, Category = "Inventory")
	int32 Count = 0;

	/** Левая верхняя клетка. */
	UPROPERTY(BlueprintReadOnly, Category = "Inventory")
	FIntPoint Cell = FIntPoint::ZeroValue;

	/** Повёрнут на 90°: ширина и высота меняются местами. */
	UPROPERTY(BlueprintReadOnly, Category = "Inventory")
	bool bRotated = false;

	/** Ствол кейса героя у UNoFearWeaponComponent: он помнит магазин, пока лежит в кейсе. В хранилище стволов-акторов нет. */
	UPROPERTY(BlueprintReadOnly, Category = "Inventory")
	TObjectPtr<ANoFearWeapon> Weapon = nullptr;

	/** Патроны в магазине ствола без актора (в хранилище), -1 — полный. У ствола кейса магазин помнит Weapon. */
	UPROPERTY(BlueprintReadOnly, Category = "Inventory")
	int32 MagazineAmmo = -1;

	/** Сколько клеток занимает с учётом поворота. */
	FIntPoint GetSize() const;

	/** Патроны в магазине: у ствола кейса — из актора, в хранилище — MagazineAmmo. -1 — не ствол или полный. */
	int32 GetMagazineAmmo() const;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FNoFearInventoryChangedSignature, UNoFearItemContainerComponent*, Container);

/**
 * Сетка с предметами (Docs/Inventory_Design.md) — общая основа кейса героя и хранилищ в мире: сундуков, ящиков.
 * Предметы разного размера, их можно поворачивать; одинаковые патроны и бинты складываются в стопки.
 *
 * Сам по себе компонент — хранилище: на старте в нём StartingItems, ствол в нём — предмет и магазин, без актора.
 * Герой перекладывает предметы между кейсом и хранилищем на экране кейса: UNoFearInventoryComponent::OpenContainerScreen.
 */
UCLASS(ClassGroup = (NoFear), meta = (BlueprintSpawnableComponent))
class NOFEAR_API UNoFearItemContainerComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UNoFearItemContainerComponent();

	// --- Настройки ---

	/** Название над сеткой на экране кейса. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory")
	FText DisplayName;

	/** Размер сетки хранилища. У кейса героя не используется: там GridSizes по ступеням расширений. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Inventory")
	FIntPoint GridSize = FIntPoint(8, 6);

	/** Что лежит на старте. Не влезло в сетку — в лог предупреждение. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Inventory")
	TArray<FNoFearItemStack> StartingItems;

	// --- Что лежит ---

	UFUNCTION(BlueprintPure, Category = "NoFear|Inventory")
	virtual FIntPoint GetGridSize() const;

	UFUNCTION(BlueprintPure, Category = "NoFear|Inventory")
	TArray<FNoFearInventoryEntry> GetEntries() const { return Entries; }

	const TArray<FNoFearInventoryEntry>& GetEntriesRef() const { return Entries; }

	UFUNCTION(BlueprintPure, Category = "NoFear|Inventory")
	bool IsEmpty() const { return Entries.IsEmpty(); }

	UFUNCTION(BlueprintPure, Category = "NoFear|Inventory")
	bool GetEntry(int32 Id, FNoFearInventoryEntry& OutEntry) const;

	/** Запись с этим номером или nullptr. Указатель живёт до следующего изменения сетки. */
	const FNoFearInventoryEntry* FindEntry(int32 Id) const;

	/** Что лежит в клетке: номер предмета или 0. */
	UFUNCTION(BlueprintPure, Category = "NoFear|Inventory")
	int32 GetEntryAt(FIntPoint Cell) const;

	/** Сколько таких штук во всех стопках. */
	UFUNCTION(BlueprintPure, Category = "NoFear|Inventory")
	int32 CountItem(const UNoFearItemData* Item) const;

	/** Сколько из Count влезет, если положить автораскладкой. */
	UFUNCTION(BlueprintPure, Category = "NoFear|Inventory")
	int32 GetRoomFor(UNoFearItemData* Item, int32 Count) const;

	/** Свободно ли место под предмет (IgnoreId — предмет, который сейчас переносим). */
	UFUNCTION(BlueprintPure, Category = "NoFear|Inventory")
	bool CanPlaceAt(const UNoFearItemData* Item, FIntPoint Cell, bool bRotated, int32 IgnoreId = 0) const;

	// --- Изменения ---

	/**
	 * Положить автораскладкой: сначала долить неполные стопки, потом занять свободные клетки (с поворотом, если
	 * иначе не влезает). Возвращает, сколько штук легло. MagazineAmmo — патроны в магазине ствола, -1 — полный.
	 */
	UFUNCTION(BlueprintCallable, Category = "NoFear|Inventory")
	int32 AddItem(UNoFearItemData* Item, int32 Count = 1, int32 MagazineAmmo = -1);

	/** Положить новую стопку в эту клетку. Возвращает номер предмета или 0, если место занято. */
	UFUNCTION(BlueprintCallable, Category = "NoFear|Inventory")
	int32 PlaceItemAt(UNoFearItemData* Item, int32 Count, FIntPoint Cell, bool bRotated, int32 MagazineAmmo = -1);

	/** Переложить предмет. false — место занято или за краем сетки. */
	UFUNCTION(BlueprintCallable, Category = "NoFear|Inventory")
	bool MoveEntry(int32 Id, FIntPoint Cell, bool bRotated);

	/** Отделить Count штук в новую стопку на свободное место. Возвращает её номер или 0. */
	UFUNCTION(BlueprintCallable, Category = "NoFear|Inventory")
	int32 SplitStack(int32 Id, int32 Count);

	/** Долить в стопку до полной. Возвращает, сколько долито. */
	UFUNCTION(BlueprintCallable, Category = "NoFear|Inventory")
	int32 AddToEntry(int32 Id, int32 Count);

	/** Забрать Count штук из стопки: кончилась — предмет исчезает. Возвращает, сколько забрано. */
	UFUNCTION(BlueprintCallable, Category = "NoFear|Inventory")
	int32 TakeFromEntry(int32 Id, int32 Count);

	/** Убрать предмет совсем (ствол кейса исчезает из рук). */
	UFUNCTION(BlueprintCallable, Category = "NoFear|Inventory")
	bool RemoveEntry(int32 Id);

	/** Забрать Count штук из стопок этого предмета, начиная с самых маленьких. Возвращает, сколько забрано. */
	UFUNCTION(BlueprintCallable, Category = "NoFear|Inventory")
	int32 RemoveItem(const UNoFearItemData* Item, int32 Count);

	/** Убрать всё. */
	UFUNCTION(BlueprintCallable, Category = "NoFear|Inventory")
	void ClearItems();

	UPROPERTY(BlueprintAssignable, Category = "NoFear|Inventory")
	FNoFearInventoryChangedSignature OnInventoryChanged;

protected:
	virtual void BeginPlay() override;

	/** Можно ли сейчас класть: у кейса — только когда он включён. */
	virtual bool CanStoreItems() const { return true; }
	/** Ложится ли такой предмет в клетки: расширение кейса в кейс не кладётся, а в хранилище лежит. */
	virtual bool CanHoldItem(const UNoFearItemData* Item) const;
	/** Автораскладка. bApply false — только посчитать, сколько влезет. */
	virtual int32 AddInternal(UNoFearItemData* Item, int32 Count, int32 MagazineAmmo, bool bApply);
	/** Запись только что легла в Entries[Index]. bIncoming — пришла извне, а не отделена от своей же стопки. */
	virtual void HandleEntryAdded(int32 Index, bool bIncoming) {}
	/** Запись уже убрана из Entries. */
	virtual void HandleEntryRemoved(const FNoFearInventoryEntry& Entry) {}
	virtual void NotifyChanged();

	void GrantStartingItems();

	/** Карта клеток: номер предмета в каждой клетке или 0. */
	void BuildOccupancy(TArray<int32>& OutGrid, int32 IgnoreId = 0) const;
	bool IsAreaFree(const TArray<int32>& Grid, FIntPoint Cell, FIntPoint Size) const;
	bool FindFreeCell(const TArray<int32>& Grid, FIntPoint ItemSize, FIntPoint& OutCell, bool& bOutRotated) const;
	void MarkArea(TArray<int32>& Grid, FIntPoint Cell, FIntPoint Size, int32 Id) const;

	int32 CreateEntry(UNoFearItemData* Item, int32 Count, FIntPoint Cell, bool bRotated, int32 MagazineAmmo, bool bIncoming);
	/** Убрать запись с этим индексом. */
	void ReleaseEntryAt(int32 Index);
	int32 FindEntryIndex(int32 Id) const;

	UPROPERTY(Transient)
	TArray<FNoFearInventoryEntry> Entries;

	int32 NextId = 1;
};
