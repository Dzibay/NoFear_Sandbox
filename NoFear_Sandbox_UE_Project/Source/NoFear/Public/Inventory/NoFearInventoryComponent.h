#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "NoFearInventoryComponent.generated.h"

class ANoFearPickup;
class ANoFearWeapon;
class APlayerController;
class UInputComponent;
class UNoFearInventoryComponent;
class UNoFearItemData;
class UNoFearWeaponComponent;

/** Пункт меню ПКМ на экране кейса. */
UENUM(BlueprintType)
enum class ENoFearCaseAction : uint8
{
	/** Взять ствол в руки. */
	Equip,
	/** В первый свободный сектор колеса. */
	AssignWheel,
	ClearWheel,
	/** Половину стопки — на курсор. */
	Split,
	Drop,
};

/** Что будет, если положить предмет с курсора сейчас. */
enum class ENoFearCaseTarget : uint8
{
	/** Внутри панели, но не на клетке и не на секторе: ничего. */
	None,
	/** На клетки сетки. */
	Grid,
	/** Долить в такую же неполную стопку под курсором. */
	Merge,
	/** Назначить в сектор колеса; предмет остаётся на своём месте. */
	WheelSlot,
	/** За краем панели — выбросить. */
	Drop,
};

struct FNoFearCaseTarget
{
	ENoFearCaseTarget Kind = ENoFearCaseTarget::None;
	/** Левая верхняя клетка предмета (Grid). */
	FIntPoint Cell = FIntPoint::ZeroValue;
	int32 Slot = INDEX_NONE;
	int32 MergeId = 0;
	/** Сюда можно: клетки свободны, сектор принимает такой предмет. */
	bool bValid = false;
};

/** Разметка экрана кейса в пикселях вьюпорта: по ней рисует HUD и по ней курсор попадает в клетки и сектора. */
struct NOFEAR_API FNoFearCaseLayout
{
	float Scale = 1.f;
	FVector2D ViewportSize = FVector2D(1920.f, 1080.f);
	FIntPoint GridSize = FIntPoint(1, 1);
	/** Левый верхний угол сетки. */
	FVector2D GridOrigin = FVector2D::ZeroVector;
	float Cell = 56.f;
	FVector2D WheelCenter = FVector2D::ZeroVector;
	/** От центра колеса до центров секторов. */
	float WheelRadius = 150.f;
	float SlotSize = 84.f;
	/** Подложка: курсор за её краем — выбросить. */
	FVector2D PanelMin = FVector2D::ZeroVector;
	FVector2D PanelMax = FVector2D::ZeroVector;
	float MenuWidth = 240.f;
	float MenuItemHeight = 34.f;

	/** Сетка слева, колесо справа, всё по центру экрана. Нулевой размер вьюпорта (-nullrhi) — как 1920×1080. */
	static FNoFearCaseLayout Make(const FVector2D& InViewportSize, FIntPoint InGridSize);

	/** Точка в клетках сетки, дробная: (0, 0) — левый верхний угол сетки. */
	FVector2D ToGrid(const FVector2D& Point) const { return (Point - GridOrigin) / Cell; }
	FVector2D GetCellMin(FIntPoint InCell) const { return GridOrigin + FVector2D(InCell) * Cell; }
	bool IsOnGrid(const FVector2D& Point) const;
	bool IsInsidePanel(const FVector2D& Point) const;
	/** Сектор 0 сверху, дальше по часовой стрелке. */
	FVector2D GetSlotCenter(int32 Slot) const;
	int32 GetSlotAt(const FVector2D& Point) const;
};

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

	/** Скорость игры, пока открыт экран кейса. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Inventory|Screen", meta = (ClampMin = "0.05", ClampMax = "1"))
	float ScreenTimeDilation = 0.3f;

	/** Отошёл от предмета, который держит на курсоре с земли, дальше этого, см — предмет остаётся лежать. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Inventory|Screen", meta = (ClampMin = "0"))
	float PickupHoldDistance = 400.f;

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

	/** Сектор, в котором предмет, или INDEX_NONE. */
	UFUNCTION(BlueprintPure, Category = "NoFear|Inventory")
	int32 FindWheelSlotOf(int32 EntryId) const;

	/** В колесо можно только стволы, гранаты и медицину. */
	static bool IsWheelItem(const UNoFearItemData* Item);

	// --- Экран кейса (I) ---

	/** Ввод экрана: I на кейс и контекст самого экрана. Вызывает ANoFearCharacter::SetupPlayerInputComponent. */
	void SetupPlayerInput(UInputComponent* PlayerInputComponent);

	/** Открыть экран: время замедляется, появляется курсор, стрельба и обзор отключены, ходить можно. */
	UFUNCTION(BlueprintCallable, Category = "NoFear|Inventory|Screen")
	bool OpenScreen();

	/** Закрыть: предмет на курсоре возвращается на место, а взятый с земли остаётся лежать. */
	UFUNCTION(BlueprintCallable, Category = "NoFear|Inventory|Screen")
	void CloseScreen();

	UFUNCTION(BlueprintCallable, Category = "NoFear|Inventory|Screen")
	void ToggleScreen();

	UFUNCTION(BlueprintPure, Category = "NoFear|Inventory|Screen")
	bool IsScreenOpen() const { return bScreenOpen; }

	/** Открыть экран с предметом с земли на курсоре: в кейсе нет места. Положили — он исчезает с земли. */
	bool HoldPickup(ANoFearPickup* Pickup);

	/** Курсор в пикселях вьюпорта. Мышь его двигает сама; проверки ставят его так. */
	UFUNCTION(BlueprintCallable, Category = "NoFear|Inventory|Screen")
	void SetScreenCursor(FVector2D Position) { ScreenCursor = Position; }

	UFUNCTION(BlueprintPure, Category = "NoFear|Inventory|Screen")
	FVector2D GetScreenCursor() const { return ScreenCursor; }

	/** ЛКМ нажата: взять предмет под курсором, положить тот, что на курсоре, или выбрать пункт меню. */
	UFUNCTION(BlueprintCallable, Category = "NoFear|Inventory|Screen")
	void ScreenPress();

	/** ЛКМ отпущена: перетащили — положить; отпустили на том же месте — предмет остаётся на курсоре до следующего щелчка. */
	UFUNCTION(BlueprintCallable, Category = "NoFear|Inventory|Screen")
	void ScreenRelease();

	/** ПКМ: меню предмета под курсором, очистить сектор под курсором, повернуть предмет на курсоре. */
	UFUNCTION(BlueprintCallable, Category = "NoFear|Inventory|Screen")
	void ScreenContext();

	/** R: повернуть предмет на курсоре или под курсором. */
	UFUNCTION(BlueprintCallable, Category = "NoFear|Inventory|Screen")
	void ScreenRotate();

	/** Esc: закрыть меню, иначе вернуть предмет с курсора, иначе закрыть экран. */
	UFUNCTION(BlueprintCallable, Category = "NoFear|Inventory|Screen")
	void ScreenBack();

	/** 1–8 (Slot 0–7): назначить в сектор предмет под курсором или на курсоре; уже там — убрать. */
	UFUNCTION(BlueprintCallable, Category = "NoFear|Inventory|Screen")
	void ScreenSlotKey(int32 Slot);

	/** Разметка экрана для текущего вьюпорта и сетки. */
	FNoFearCaseLayout GetScreenLayout() const;

	UFUNCTION(BlueprintPure, Category = "NoFear|Inventory|Screen")
	FVector2D GetCellCenter(FIntPoint Cell) const;

	UFUNCTION(BlueprintPure, Category = "NoFear|Inventory|Screen")
	FVector2D GetWheelSlotCenter(int32 Slot) const;

	UFUNCTION(BlueprintPure, Category = "NoFear|Inventory|Screen")
	FVector2D GetMenuItemCenter(int32 Index) const;

	/** Точка далеко за краем панели — туда тащат, чтобы выбросить. */
	UFUNCTION(BlueprintPure, Category = "NoFear|Inventory|Screen")
	FVector2D GetOutsidePoint() const;

	UFUNCTION(BlueprintPure, Category = "NoFear|Inventory|Screen")
	bool IsHolding() const { return HeldSource != EHeldSource::None; }

	UFUNCTION(BlueprintPure, Category = "NoFear|Inventory|Screen")
	UNoFearItemData* GetHeldItem() const { return IsHolding() ? HeldItem.Get() : nullptr; }

	UFUNCTION(BlueprintPure, Category = "NoFear|Inventory|Screen")
	int32 GetHeldCount() const;

	/** Предмет кейса, который переносят (или от которого отделили стопку на курсоре), или 0. */
	UFUNCTION(BlueprintPure, Category = "NoFear|Inventory|Screen")
	int32 GetHeldEntryId() const { return HeldSource == EHeldSource::Entry || HeldSource == EHeldSource::Split ? HeldEntryId : 0; }

	/** На курсоре предмет с земли. */
	UFUNCTION(BlueprintPure, Category = "NoFear|Inventory|Screen")
	bool IsHoldingPickup() const { return HeldSource == EHeldSource::Pickup; }

	/** На курсоре часть стопки, ещё не положенная в кейс. */
	UFUNCTION(BlueprintPure, Category = "NoFear|Inventory|Screen")
	bool IsHoldingSplit() const { return HeldSource == EHeldSource::Split; }

	/** Сектор колеса, из которого взят предмет; INDEX_NONE — взят с сетки. Вынесли за край — сектор очищается, предмет остаётся в кейсе. */
	UFUNCTION(BlueprintPure, Category = "NoFear|Inventory|Screen")
	int32 GetHeldWheelSlot() const { return HeldSource == EHeldSource::Entry ? HeldWheelSlot : INDEX_NONE; }

	UFUNCTION(BlueprintPure, Category = "NoFear|Inventory|Screen")
	bool IsHeldRotated() const { return bHeldRotated; }

	/** Сколько клеток занимает предмет на курсоре с учётом поворота. */
	UFUNCTION(BlueprintPure, Category = "NoFear|Inventory|Screen")
	FIntPoint GetHeldSize() const;

	/** Где на предмете курсор, в клетках от его левого верхнего угла. */
	UFUNCTION(BlueprintPure, Category = "NoFear|Inventory|Screen")
	FVector2D GetHeldGrab() const { return HeldGrab; }

	/** Патроны в магазине ствола на курсоре; -1 — не ствол или полный. */
	UFUNCTION(BlueprintPure, Category = "NoFear|Inventory|Screen")
	int32 GetHeldMagazine() const;

	/** Что будет, если положить сейчас. */
	FNoFearCaseTarget GetHeldTarget() const;

	/** Предмет на клетке под курсором или 0. */
	UFUNCTION(BlueprintPure, Category = "NoFear|Inventory|Screen")
	int32 GetHoveredEntryId() const;

	/** Сектор колеса под курсором или INDEX_NONE. */
	UFUNCTION(BlueprintPure, Category = "NoFear|Inventory|Screen")
	int32 GetHoveredWheelSlot() const;

	UFUNCTION(BlueprintPure, Category = "NoFear|Inventory|Screen")
	bool IsMenuOpen() const { return bMenuOpen; }

	UFUNCTION(BlueprintPure, Category = "NoFear|Inventory|Screen")
	TArray<ENoFearCaseAction> GetMenuActions() const { return bMenuOpen ? MenuActions : TArray<ENoFearCaseAction>(); }

	UFUNCTION(BlueprintPure, Category = "NoFear|Inventory|Screen")
	int32 GetMenuEntryId() const { return bMenuOpen ? MenuEntryId : 0; }

	FVector2D GetMenuPosition() const { return MenuPosition; }

	/** Пункт меню под курсором или INDEX_NONE. */
	int32 GetHoveredMenuItem() const;

	static FText GetCaseActionName(ENoFearCaseAction Action);

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
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
	/** Откуда предмет на курсоре. */
	enum class EHeldSource : uint8
	{
		None,
		/** Предмет кейса: лежит на своём месте, пока его не положат в другое. */
		Entry,
		/** Отделённая часть стопки HeldEntryId: в кейсе её уже нет. */
		Split,
		/** Предмет с земли HeldPickup: лежит там, пока его не положат в кейс. */
		Pickup,
	};

	void TryActivate();
	void GrantStartingItems();

	APlayerController* GetOwnerPlayerController() const;
	/** Вернуть контекст ввода с I, если его сняли (персонаж GASP пересобирает контексты при вселении). */
	void EnsureCaseInput(APlayerController* PC) const;
	void UpdateScreenCursor(APlayerController* PC);

	/** Взять предмет кейса на курсор, курсор там, где за него взялись. */
	void HoldEntry(int32 Id, int32 FromWheelSlot = INDEX_NONE);
	/** Положить предмет с курсора туда, куда указывает GetHeldTarget. false — туда нельзя, он остаётся на курсоре. */
	bool PlaceHeld();
	/** Вернуть предмет с курсора: свой — на место, отделённую часть — в стопку, предмет с земли остаётся лежать. */
	void ReturnHeld();
	void ClearHeld();
	/** Курсор в клетках с учётом того, где за предмет взялись: левый верхний угол предмета. */
	FIntPoint GetHeldOrigin(const FNoFearCaseLayout& Layout) const;

	void OpenMenu(int32 EntryId);
	void CloseMenu();
	void RunMenuAction(ENoFearCaseAction Action);

	/** Предмет на землю перед героем с толчком вперёд. */
	ANoFearPickup* SpawnDropped(UNoFearItemData* Item, int32 Count, int32 MagazineAmmo) const;

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

	// --- Экран кейса ---

	bool bScreenOpen = false;
	float TimeDilationBeforeScreen = 1.f;
	bool bCursorShownBeforeScreen = false;
	/** Контроллер, у которого экран забрал курсор и обзор: ему и вернуть. */
	TWeakObjectPtr<APlayerController> ScreenController;

	FVector2D ScreenCursor = FVector2D::ZeroVector;
	/** Где была мышь в прошлом кадре: курсор берёт её позицию, только когда она сдвинулась. */
	FVector2D LastMousePosition = FVector2D(-1.f, -1.f);
	/** Где нажали ЛКМ: отпустили далеко — это перетаскивание. */
	FVector2D PressCursor = FVector2D::ZeroVector;
	/** Нажатие уже положило предмет или выбрало пункт меню — отпускание ничего не делает. */
	bool bPressConsumed = false;

	EHeldSource HeldSource = EHeldSource::None;
	int32 HeldEntryId = 0;
	UPROPERTY(Transient)
	TObjectPtr<UNoFearItemData> HeldItem;
	int32 HeldCount = 0;
	int32 HeldMagazine = -1;
	bool bHeldRotated = false;
	FVector2D HeldGrab = FVector2D::ZeroVector;
	/** Взяли из сектора колеса; INDEX_NONE — с сетки. */
	int32 HeldWheelSlot = INDEX_NONE;
	TWeakObjectPtr<ANoFearPickup> HeldPickup;

	bool bMenuOpen = false;
	int32 MenuEntryId = 0;
	FVector2D MenuPosition = FVector2D::ZeroVector;
	TArray<ENoFearCaseAction> MenuActions;
};
