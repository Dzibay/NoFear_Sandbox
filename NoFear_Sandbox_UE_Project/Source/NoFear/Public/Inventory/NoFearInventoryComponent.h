#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "NoFearInventoryComponent.generated.h"

class ANoFearPickup;
class ANoFearWeapon;
class UNoFearInventoryComponent;
class UNoFearItemData;
class UNoFearWeaponComponent;

/** Сколько и чего: стартовый набор кейса. */
USTRUCT(BlueprintType)
struct FNoFearItemStack
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory")
	TSoftObjectPtr<UNoFearItemData> Item;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory", meta = (ClampMin = "1"))
	int32 Count = 1;
};

/** Предмет, лежащий в кейсе: где лежит, повёрнут ли, сколько штук в стопке. */
USTRUCT(BlueprintType)
struct FNoFearInventoryEntry
{
	GENERATED_BODY()

	/** Номер предмета в этом кейсе, не меняется, пока предмет в нём лежит. 0 — нет предмета. */
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

	/** Ствол этого предмета у UNoFearWeaponComponent: он помнит магазин, пока лежит в кейсе. */
	UPROPERTY(BlueprintReadOnly, Category = "Inventory")
	TObjectPtr<ANoFearWeapon> Weapon = nullptr;

	/** Сколько клеток занимает с учётом поворота. */
	FIntPoint GetSize() const;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FNoFearInventoryChangedSignature, UNoFearInventoryComponent*, Inventory);

/**
 * Кейс героя (Docs/Inventory_Design.md): сетка клеток, в которой лежат стволы, патроны, гранаты и медицина.
 * Предметы разного размера, их можно поворачивать; одинаковые патроны и бинты складываются в стопки.
 * Расширения увеличивают сетку по GridSizes.
 *
 * Стоит на каждом ANoFearCharacter, но работает только у того, кем управляет игрок: включается при вселении
 * и забирает себе стволы UNoFearWeaponComponent. Каждый ствол кейса — актор ANoFearWeapon, который берёт
 * патроны на перезарядку из кейса; гранаты UNoFearGrenadeComponent тоже лежат здесь. У ботов кейс выключен,
 * и оружие с гранатами работают по-старому, со своим запасом.
 */
UCLASS(ClassGroup = (NoFear), meta = (BlueprintSpawnableComponent))
class NOFEAR_API UNoFearInventoryComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UNoFearInventoryComponent();

	static constexpr int32 WheelSlotCount = 8;

	// --- Настройки ---

	/** Выключено — кейса нет и у игрока: оружие и гранаты по-старому. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Inventory")
	bool bEnabled = true;

	/** Размер сетки на каждой ступени: без расширений, после первого, после второго… */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Inventory")
	TArray<FIntPoint> GridSizes;

	/** Что лежит в кейсе на старте. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Inventory")
	TArray<FNoFearItemStack> StartingItems;

	/** Взять в руки первый ствол стартового набора. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Inventory")
	bool bEquipOnStart = true;

	/** Новые стволы, гранаты и медицина сами занимают свободные сектора колеса. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Inventory")
	bool bAutoAssignWheel = true;

	/** На каком расстоянии перед героем появляется выброшенный предмет, см. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Inventory", meta = (ClampMin = "0"))
	float DropDistance = 70.f;

	/** Сколько секунд видна строка «Подобрано…» / «Нет места». */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Inventory", meta = (ClampMin = "0"))
	float NoticeDuration = 2.5f;

	// --- Кейс ---

	/** Работающий кейс актора (только у героя игрока) или nullptr. */
	static UNoFearInventoryComponent* FindCase(const AActor* Actor);

	/**
	 * Будет ли кейс вести стволы владельца: уже включён или включится в его BeginPlay (владелец под игроком).
	 * UNoFearWeaponComponent тогда не выдаёт StartingWeapons.
	 */
	static bool WillManageWeapons(const AActor* Owner);

	UFUNCTION(BlueprintPure, Category = "NoFear|Inventory")
	bool IsCaseActive() const { return bCaseActive; }

	UFUNCTION(BlueprintPure, Category = "NoFear|Inventory")
	FIntPoint GetGridSize() const;

	UFUNCTION(BlueprintPure, Category = "NoFear|Inventory")
	int32 GetExpansionLevel() const { return ExpansionLevel; }

	UFUNCTION(BlueprintPure, Category = "NoFear|Inventory")
	int32 GetMaxExpansionLevel() const { return FMath::Max(GridSizes.Num() - 1, 0); }

	UFUNCTION(BlueprintPure, Category = "NoFear|Inventory")
	bool CanExpand() const { return ExpansionLevel < GetMaxExpansionLevel(); }

	/** Увеличить сетку на ступень. Предметы остаются на своих клетках. */
	UFUNCTION(BlueprintCallable, Category = "NoFear|Inventory")
	bool AddExpansion();

	UFUNCTION(BlueprintPure, Category = "NoFear|Inventory")
	TArray<FNoFearInventoryEntry> GetEntries() const { return Entries; }

	const TArray<FNoFearInventoryEntry>& GetEntriesRef() const { return Entries; }

	UFUNCTION(BlueprintPure, Category = "NoFear|Inventory")
	bool GetEntry(int32 Id, FNoFearInventoryEntry& OutEntry) const;

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

	/** Номер предмета, которому принадлежит ствол, или 0. */
	UFUNCTION(BlueprintPure, Category = "NoFear|Inventory")
	int32 FindEntryForWeapon(const ANoFearWeapon* Weapon) const;

	// --- Изменения ---

	/**
	 * Положить автораскладкой: сначала долить неполные стопки, потом занять свободные клетки (с поворотом, если
	 * иначе не влезает). Возвращает, сколько штук легло. MagazineAmmo — патроны в магазине ствола, -1 — полный.
	 */
	UFUNCTION(BlueprintCallable, Category = "NoFear|Inventory")
	int32 AddItem(UNoFearItemData* Item, int32 Count = 1, int32 MagazineAmmo = -1);

	/** Как AddItem, но то, что не влезло, падает под ноги. Возвращает, сколько легло в кейс. */
	UFUNCTION(BlueprintCallable, Category = "NoFear|Inventory")
	int32 AddItemOrDrop(UNoFearItemData* Item, int32 Count = 1, int32 MagazineAmmo = -1);

	/** Положить новую стопку в эту клетку. Возвращает номер предмета или 0, если место занято. */
	UFUNCTION(BlueprintCallable, Category = "NoFear|Inventory")
	int32 PlaceItemAt(UNoFearItemData* Item, int32 Count, FIntPoint Cell, bool bRotated, int32 MagazineAmmo = -1);

	/** Переложить предмет. false — место занято или за краем сетки. */
	UFUNCTION(BlueprintCallable, Category = "NoFear|Inventory")
	bool MoveEntry(int32 Id, FIntPoint Cell, bool bRotated);

	/** Отделить Count штук в новую стопку на свободное место. Возвращает её номер или 0. */
	UFUNCTION(BlueprintCallable, Category = "NoFear|Inventory")
	int32 SplitStack(int32 Id, int32 Count);

	/** Убрать предмет из кейса совсем (ствол исчезает из рук). */
	UFUNCTION(BlueprintCallable, Category = "NoFear|Inventory")
	bool RemoveEntry(int32 Id);

	/** Забрать Count штук из стопок этого предмета, начиная с самых маленьких. Возвращает, сколько забрано. */
	UFUNCTION(BlueprintCallable, Category = "NoFear|Inventory")
	int32 RemoveItem(const UNoFearItemData* Item, int32 Count);

	/** Выбросить предмет перед героем (Count 0 — всю стопку). Ствол уходит с тем же магазином. */
	UFUNCTION(BlueprintCallable, Category = "NoFear|Inventory")
	ANoFearPickup* DropEntry(int32 Id, int32 Count = 0);

	/** Опустошить кейс и вернуть сетку к начальному размеру. */
	UFUNCTION(BlueprintCallable, Category = "NoFear|Inventory")
	void ClearCase();

	// --- Колесо ---

	/** Номер предмета в секторе колеса или 0. */
	UFUNCTION(BlueprintPure, Category = "NoFear|Inventory")
	int32 GetWheelSlot(int32 Slot) const;

	/** Назначить предмет в сектор; из прежнего сектора он уходит. */
	UFUNCTION(BlueprintCallable, Category = "NoFear|Inventory")
	bool AssignWheelSlot(int32 Slot, int32 EntryId);

	UFUNCTION(BlueprintCallable, Category = "NoFear|Inventory")
	void ClearWheelSlot(int32 Slot);

	// --- Сообщения HUD ---

	void PushNotice(const FText& Text);

	/** Текущая строка и её прозрачность (затухает к концу). Пусто — ничего не показывать. */
	FText GetNotice(float& OutAlpha) const;

	UPROPERTY(BlueprintAssignable, Category = "NoFear|Inventory")
	FNoFearInventoryChangedSignature OnInventoryChanged;

	/** Сменился контроллер владельца: игрок вселился — включить кейс. Вызывает ANoFearCharacter. */
	void HandleControllerChanged();

protected:
	virtual void BeginPlay() override;

private:
	void TryActivate();
	void GrantStartingItems();

	/** Автораскладка. bApply false — только посчитать, сколько влезет. */
	int32 AddInternal(UNoFearItemData* Item, int32 Count, int32 MagazineAmmo, bool bApply);

	/** Карта клеток: номер предмета в каждой клетке или 0. */
	void BuildOccupancy(TArray<int32>& OutGrid, int32 IgnoreId = 0) const;
	bool IsAreaFree(const TArray<int32>& Grid, FIntPoint Cell, FIntPoint Size) const;
	bool FindFreeCell(const TArray<int32>& Grid, FIntPoint ItemSize, FIntPoint& OutCell, bool& bOutRotated) const;
	void MarkArea(TArray<int32>& Grid, FIntPoint Cell, FIntPoint Size, int32 Id) const;

	int32 CreateEntry(UNoFearItemData* Item, int32 Count, FIntPoint Cell, bool bRotated, int32 MagazineAmmo, bool bAssignWheel);
	/** Убрать запись с этим индексом: ствол уничтожается, сектора колеса переходят на такую же стопку или пустеют. */
	void ReleaseEntryAt(int32 Index);
	int32 FindEntryIndex(int32 Id) const;
	void AutoAssignWheel(const FNoFearInventoryEntry& Entry);
	void NotifyChanged();

	UNoFearWeaponComponent* GetWeaponComponent() const;

	UPROPERTY(Transient)
	TArray<FNoFearInventoryEntry> Entries;

	TArray<int32> WheelSlots;
	int32 NextId = 1;
	int32 ExpansionLevel = 0;
	bool bCaseActive = false;

	FText NoticeText;
	double NoticeTime = -100.0;
};
