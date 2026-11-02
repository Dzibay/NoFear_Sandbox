#pragma once

#include "CoreMinimal.h"
#include "Core/NoFearHealthComponent.h"
#include "Inventory/NoFearItemContainerComponent.h"
#include "NoFearInventoryComponent.generated.h"

class ANoFearPickup;
class ANoFearWeapon;
class APlayerController;
class UInputComponent;
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
	/** Из открытого хранилища в кейс автораскладкой. */
	Take,
	/** Из кейса в открытое хранилище автораскладкой. */
	Store,
	/** Бинт, аптечка: экран закрывается, начинается лечение. */
	Use,
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
	/** Grid и Merge — в сетке открытого хранилища, а не кейса. */
	bool bContainer = false;
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
	/** Сетка открытого хранилища справа от кейса; нулевая — хранилища нет. */
	FIntPoint ContainerSize = FIntPoint::ZeroValue;
	FVector2D ContainerOrigin = FVector2D::ZeroVector;
	/** Колесо есть только без хранилища: на его месте сетка хранилища. */
	bool bShowWheel = true;
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

	/**
	 * Сетка кейса слева, справа колесо или сетка хранилища, всё по центру экрана.
	 * Нулевой размер вьюпорта (-nullrhi) — как 1920×1080.
	 */
	static FNoFearCaseLayout Make(const FVector2D& InViewportSize, FIntPoint InGridSize, FIntPoint InContainerSize = FIntPoint::ZeroValue);

	bool HasContainer() const { return ContainerSize.X > 0 && ContainerSize.Y > 0; }
	FVector2D GetOrigin(bool bContainer) const { return bContainer ? ContainerOrigin : GridOrigin; }
	FIntPoint GetSize(bool bContainer) const { return bContainer ? ContainerSize : GridSize; }

	/** Точка в клетках сетки (кейса или хранилища), дробная: (0, 0) — левый верхний угол сетки. */
	FVector2D ToGrid(const FVector2D& Point, bool bContainer = false) const { return (Point - GetOrigin(bContainer)) / Cell; }
	FVector2D GetCellMin(FIntPoint InCell, bool bContainer = false) const { return GetOrigin(bContainer) + FVector2D(InCell) * Cell; }
	bool IsOnGrid(const FVector2D& Point, bool bContainer = false) const;
	bool IsInsidePanel(const FVector2D& Point) const;
	/** Сектор 0 сверху, дальше по часовой стрелке. */
	FVector2D GetSlotCenter(int32 Slot) const;
	int32 GetSlotAt(const FVector2D& Point) const;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FNoFearContainerScreenSignature, UNoFearItemContainerComponent*, Container);

/**
 * Кейс героя (Docs/Inventory_Design.md): сетка клеток, в которой лежат стволы, патроны, гранаты и медицина.
 * Сетка, стопки и поворот — от UNoFearItemContainerComponent; расширения увеличивают её по GridSizes.
 * На экране кейса рядом может быть открыто хранилище (OpenContainerScreen): предметы перетаскивают между сетками.
 * Колесо предметов на Q (NoFearItemWheel.cpp) применяет то, что лежит в восьми секторах; колёсико мыши листает стволы колеса.
 *
 * Стоит на каждом ANoFearCharacter, но работает только у того, кем управляет игрок: включается при вселении
 * и забирает себе стволы UNoFearWeaponComponent. Каждый ствол кейса — актор ANoFearWeapon, который берёт
 * патроны на перезарядку из кейса; гранаты UNoFearGrenadeComponent тоже лежат здесь. У ботов кейс выключен,
 * и оружие с гранатами работают по-старому, со своим запасом.
 */
UCLASS(ClassGroup = (NoFear), meta = (BlueprintSpawnableComponent))
class NOFEAR_API UNoFearInventoryComponent : public UNoFearItemContainerComponent
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

	/** Взять в руки первый ствол стартового набора. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Inventory")
	bool bEquipOnStart = true;

	/** Новые стволы, гранаты, медицина и то, что ставится в мир, сами занимают свободные сектора колеса. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Inventory")
	bool bAutoAssignWheel = true;

	/** На каком расстоянии перед героем появляется выброшенный предмет, см. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Inventory", meta = (ClampMin = "0"))
	float DropDistance = 70.f;

	/** На каком расстоянии перед героем встаёт предмет из колеса (капкан, растяжка), см. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Inventory", meta = (ClampMin = "0"))
	float PlaceDistance = 120.f;

	/** Сколько секунд видна строка «Подобрано…» / «Нет места». */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Inventory", meta = (ClampMin = "0"))
	float NoticeDuration = 2.5f;

	/** Скорость игры, пока открыт экран кейса. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Inventory|Screen", meta = (ClampMin = "0.05", ClampMax = "1"))
	float ScreenTimeDilation = 0.3f;

	/**
	 * Отошёл от предмета, который держит на курсоре с земли, дальше этого, см — предмет остаётся лежать.
	 * Отошёл так от открытого хранилища — экран закрывается.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Inventory|Screen", meta = (ClampMin = "0"))
	float PickupHoldDistance = 400.f;

	/** Скорость игры, пока зажата Q и открыто колесо предметов. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Inventory|Wheel", meta = (ClampMin = "0.05", ClampMax = "1"))
	float WheelTimeDilation = 0.3f;

	/** Насколько сдвинуть мышь от центра колеса, чтобы выбрать сектор (сырые единицы мыши). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Inventory|Wheel", meta = (ClampMin = "1"))
	float WheelDeadZone = 12.f;

	/** Колёсико мыши листает только стволы из колеса, по порядку секторов. Выключено — все стволы кейса. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Inventory|Wheel")
	bool bScrollWheelWeaponsOnly = true;

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

	/** Размер на текущей ступени расширений. */
	virtual FIntPoint GetGridSize() const override;

	UFUNCTION(BlueprintPure, Category = "NoFear|Inventory")
	int32 GetExpansionLevel() const { return ExpansionLevel; }

	UFUNCTION(BlueprintPure, Category = "NoFear|Inventory")
	int32 GetMaxExpansionLevel() const { return FMath::Max(GridSizes.Num() - 1, 0); }

	UFUNCTION(BlueprintPure, Category = "NoFear|Inventory")
	bool CanExpand() const { return ExpansionLevel < GetMaxExpansionLevel(); }

	/** Увеличить сетку на ступень. Предметы остаются на своих клетках. */
	UFUNCTION(BlueprintCallable, Category = "NoFear|Inventory")
	bool AddExpansion();

	/** Номер предмета, которому принадлежит ствол, или 0. */
	UFUNCTION(BlueprintPure, Category = "NoFear|Inventory")
	int32 FindEntryForWeapon(const ANoFearWeapon* Weapon) const;

	// --- Изменения ---

	/** Как AddItem, но то, что не влезло, падает под ноги. Возвращает, сколько легло в кейс. */
	UFUNCTION(BlueprintCallable, Category = "NoFear|Inventory")
	int32 AddItemOrDrop(UNoFearItemData* Item, int32 Count = 1, int32 MagazineAmmo = -1);

	/** Выбросить предмет перед героем (Count 0 — всю стопку). Ствол уходит с тем же магазином. */
	UFUNCTION(BlueprintCallable, Category = "NoFear|Inventory")
	ANoFearPickup* DropEntry(int32 Id, int32 Count = 0);

	/** Опустошить кейс и вернуть сетку к начальному размеру. */
	UFUNCTION(BlueprintCallable, Category = "NoFear|Inventory")
	void ClearCase();

	/**
	 * Поставить предмет вида Placeable: на полу в PlaceDistance перед героем появляется актор PlaceClass с его поворотом,
	 * владелец и инстигатор — герой. Предмет тратится, только если актор пережил FinishSpawning.
	 * nullptr — предмета нет в кейсе, открыт экран, впереди стена или нет пола, актор уничтожил себя в BeginPlay.
	 */
	UFUNCTION(BlueprintCallable, Category = "NoFear|Inventory")
	AActor* PlaceItem(UNoFearItemData* Item);

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

	/** В колесо можно только стволы, гранаты, медицину и то, что ставится в мир. */
	static bool IsWheelItem(const UNoFearItemData* Item);

	/**
	 * Применить сектор колеса: ствол — в руки, граната — её бросает G, медицина — StartHealing,
	 * капкан и растяжка — PlaceItem. false — сектор пуст или предмет сейчас не применить.
	 */
	UFUNCTION(BlueprintCallable, Category = "NoFear|Inventory")
	bool UseWheelSlot(int32 Slot);

	/**
	 * Колёсико мыши: следующий (Direction > 0) или предыдущий ствол из колеса по порядку секторов.
	 * Ствол в руках не из колеса или руки пусты — вперёд первый, назад последний. false — листать нечего.
	 */
	UFUNCTION(BlueprintCallable, Category = "NoFear|Inventory")
	bool CycleWheelWeapon(int32 Direction);

	// --- Колесо предметов (Q) ---

	/** Открыть колесо: время замедляется, мышь выбирает сектор, обзор стоит, ходить можно. */
	UFUNCTION(BlueprintCallable, Category = "NoFear|Inventory|Wheel")
	bool OpenItemWheel();

	/** Закрыть колесо; bUse — применить выбранный сектор (Q отпущена). Мышь в центре — ничего не выбрано. */
	UFUNCTION(BlueprintCallable, Category = "NoFear|Inventory|Wheel")
	void CloseItemWheel(bool bUse = true);

	UFUNCTION(BlueprintPure, Category = "NoFear|Inventory|Wheel")
	bool IsItemWheelOpen() const { return bItemWheelOpen; }

	/** Сектор под курсором колеса или INDEX_NONE (мышь в центре). */
	UFUNCTION(BlueprintPure, Category = "NoFear|Inventory|Wheel")
	int32 GetItemWheelSelection() const { return bItemWheelOpen ? ItemWheelSelection : INDEX_NONE; }

	/** Куда смотрит курсор колеса: единичный вектор, Y вверх; ноль — в центре. */
	UFUNCTION(BlueprintPure, Category = "NoFear|Inventory|Wheel")
	FVector2D GetItemWheelCursor() const;

	/** Курсор колеса в сырых единицах мыши, Y вверх. Мышь двигает его сама; проверки ставят его так. */
	UFUNCTION(BlueprintCallable, Category = "NoFear|Inventory|Wheel")
	void SetItemWheelCursor(FVector2D Cursor);

	// --- Лечение (бинт, аптечка) ---

	/**
	 * Начать лечение предметом из кейса: UseTime секунд герой идёт шагом и не стреляет, потом получает HealAmount
	 * здоровья (bHealToFull — до полного), и предмет тратится. Урон, спринт, ЛКМ, ПКМ, R, G, X, колесо предметов
	 * или экран кейса прерывают лечение, предмет тогда остаётся. false — предмета нет, здоровье полное, герой упал,
	 * бежит или уже лечится.
	 */
	UFUNCTION(BlueprintCallable, Category = "NoFear|Inventory|Healing")
	bool StartHealing(UNoFearItemData* Item);

	/** Прервать лечение, предмет не тратится. bNotice — строка «Лечение прервано». */
	UFUNCTION(BlueprintCallable, Category = "NoFear|Inventory|Healing")
	void CancelHealing(bool bNotice = true);

	UFUNCTION(BlueprintPure, Category = "NoFear|Inventory|Healing")
	bool IsHealing() const { return HealingItem != nullptr; }

	/** Чем лечится герой или nullptr. */
	UFUNCTION(BlueprintPure, Category = "NoFear|Inventory|Healing")
	UNoFearItemData* GetHealingItem() const { return HealingItem; }

	/** Сколько лечения прошло, 0–1. */
	UFUNCTION(BlueprintPure, Category = "NoFear|Inventory|Healing")
	float GetHealingProgress() const;

	/** Сколько секунд лечения осталось. */
	UFUNCTION(BlueprintPure, Category = "NoFear|Inventory|Healing")
	float GetHealingTimeRemaining() const;

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

	/**
	 * Открыть экран кейса с сеткой хранилища рядом (сундук, ящик): предметы перетаскивают между сетками,
	 * колеса на экране нет. Закрывается теми же Esc и I, а ещё E и когда герой отошёл от хранилища.
	 * Экран уже открыт — true, только если с этим же хранилищем.
	 */
	UFUNCTION(BlueprintCallable, Category = "NoFear|Inventory|Screen")
	bool OpenContainerScreen(UNoFearItemContainerComponent* Container);

	/** Хранилище, открытое на экране кейса, или nullptr. */
	UFUNCTION(BlueprintPure, Category = "NoFear|Inventory|Screen")
	UNoFearItemContainerComponent* GetOpenContainer() const;

	/** Экран с хранилищем закрылся. Уничтоженное хранилище приходит как nullptr. */
	UPROPERTY(BlueprintAssignable, Category = "NoFear|Inventory|Screen")
	FNoFearContainerScreenSignature OnContainerScreenClosed;

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

	/** E: закрыть экран с хранилищем. На обычном экране кейса ничего не делает. */
	UFUNCTION(BlueprintCallable, Category = "NoFear|Inventory|Screen")
	void ScreenInteract();

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

	/** Центр клетки сетки открытого хранилища. */
	UFUNCTION(BlueprintPure, Category = "NoFear|Inventory|Screen")
	FVector2D GetContainerCellCenter(FIntPoint Cell) const;

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

	/** Предмет, который переносят (или от которого отделили стопку на курсоре), или 0. Чей он — IsHoldingFromContainer. */
	UFUNCTION(BlueprintPure, Category = "NoFear|Inventory|Screen")
	int32 GetHeldEntryId() const { return HeldSource == EHeldSource::Entry || HeldSource == EHeldSource::Split ? HeldEntryId : 0; }

	/** Предмет на курсоре — из открытого хранилища (или часть его стопки). */
	UFUNCTION(BlueprintPure, Category = "NoFear|Inventory|Screen")
	bool IsHoldingFromContainer() const;

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

	/** Предмет кейса на клетке под курсором или 0. */
	UFUNCTION(BlueprintPure, Category = "NoFear|Inventory|Screen")
	int32 GetHoveredEntryId() const;

	/** Предмет открытого хранилища на клетке под курсором или 0. */
	UFUNCTION(BlueprintPure, Category = "NoFear|Inventory|Screen")
	int32 GetHoveredContainerEntryId() const;

	/** Сектор колеса под курсором или INDEX_NONE. */
	UFUNCTION(BlueprintPure, Category = "NoFear|Inventory|Screen")
	int32 GetHoveredWheelSlot() const;

	UFUNCTION(BlueprintPure, Category = "NoFear|Inventory|Screen")
	bool IsMenuOpen() const { return bMenuOpen; }

	UFUNCTION(BlueprintPure, Category = "NoFear|Inventory|Screen")
	TArray<ENoFearCaseAction> GetMenuActions() const { return bMenuOpen ? MenuActions : TArray<ENoFearCaseAction>(); }

	/** Предмет, для которого открыто меню, или 0. Чей он — IsMenuForContainer. */
	UFUNCTION(BlueprintPure, Category = "NoFear|Inventory|Screen")
	int32 GetMenuEntryId() const { return bMenuOpen ? MenuEntryId : 0; }

	/** Меню открыто для предмета хранилища. */
	UFUNCTION(BlueprintPure, Category = "NoFear|Inventory|Screen")
	bool IsMenuForContainer() const;

	FVector2D GetMenuPosition() const { return MenuPosition; }

	/** Пункт меню под курсором или INDEX_NONE. */
	int32 GetHoveredMenuItem() const;

	static FText GetCaseActionName(ENoFearCaseAction Action);

	/** Как GetCaseActionName, но «Переложить» называет открытое хранилище. */
	FText GetMenuActionName(ENoFearCaseAction Action) const;

	// --- Сообщения HUD ---

	void PushNotice(const FText& Text);

	/** Текущая строка и её прозрачность (затухает к концу). Пусто — ничего не показывать. */
	FText GetNotice(float& OutAlpha) const;

	/** Сменился контроллер владельца: игрок вселился — включить кейс. Вызывает ANoFearCharacter. */
	void HandleControllerChanged();

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	virtual bool CanStoreItems() const override { return bCaseActive; }
	virtual bool CanHoldItem(const UNoFearItemData* Item) const override;
	virtual int32 AddInternal(UNoFearItemData* Item, int32 Count, int32 MagazineAmmo, bool bApply) override;
	/** Ствол получает актор у UNoFearWeaponComponent, новые предметы занимают свободные сектора колеса. */
	virtual void HandleEntryAdded(int32 Index, bool bIncoming) override;
	/** Ствол уничтожается, сектора колеса переходят на такую же стопку или пустеют. */
	virtual void HandleEntryRemoved(const FNoFearInventoryEntry& Entry) override;
	virtual void NotifyChanged() override;

private:
	/** Откуда предмет на курсоре. */
	enum class EHeldSource : uint8
	{
		None,
		/** Предмет HeldStorage (кейса или хранилища): лежит на своём месте, пока его не положат в другое. */
		Entry,
		/** Отделённая часть стопки HeldEntryId из HeldStorage: там её уже нет. */
		Split,
		/** Предмет с земли HeldPickup: лежит там, пока его не положат в кейс. */
		Pickup,
	};

	void TryActivate();

	APlayerController* GetOwnerPlayerController() const;
	/** Вернуть контексты ввода с I и Q, если их сняли (персонаж GASP пересобирает контексты при вселении). */
	void EnsureCaseInput(APlayerController* PC) const;
	/** Снять заглушки колеса предметов. */
	void RemoveItemWheelInput(APlayerController* PC) const;
	/** Снять заглушки лечения. */
	void RemoveHealingInput(APlayerController* PC) const;
	void UpdateScreenCursor(APlayerController* PC);

	/** Взять предмет кейса или хранилища на курсор, курсор там, где за него взялись. */
	void HoldEntry(UNoFearItemContainerComponent* Storage, int32 Id, int32 FromWheelSlot = INDEX_NONE);
	/** Положить предмет с курсора туда, куда указывает GetHeldTarget. false — туда нельзя, он остаётся на курсоре. */
	bool PlaceHeld();
	/** Вернуть предмет с курсора: свой — на место, отделённую часть — в стопку, предмет с земли остаётся лежать. */
	void ReturnHeld();
	void ClearHeld();
	/** Курсор в клетках сетки с учётом того, где за предмет взялись: левый верхний угол предмета. */
	FIntPoint GetHeldOrigin(const FNoFearCaseLayout& Layout, bool bContainer) const;
	/** Куда ляжет предмет с курсора в этой сетке. false — ни курсор, ни предмет не над ней. */
	bool FindGridTarget(const FNoFearCaseLayout& Layout, bool bContainer, FNoFearCaseTarget& OutTarget) const;
	/** Чей предмет на курсоре (Entry и Split) или nullptr. */
	UNoFearItemContainerComponent* GetHeldStorage() const;
	/** Кейс или хранилище с предметом под курсором; OutId — его номер. nullptr — под курсором пусто. */
	UNoFearItemContainerComponent* GetHoveredStorage(int32& OutId) const;
	bool IsContainerInReach(const UNoFearItemContainerComponent* Container) const;
	/** «Взято: …» — в кейс из хранилища, «Убрано: …» — из кейса в хранилище. */
	void PushTransferNotice(const UNoFearItemData* Item, int32 Count, bool bToCase);

	void OpenMenu(UNoFearItemContainerComponent* Storage, int32 EntryId);
	void CloseMenu();
	void RunMenuAction(ENoFearCaseAction Action);

	/** Предмет на землю перед героем с толчком вперёд. */
	ANoFearPickup* SpawnDropped(UNoFearItemData* Item, int32 Count, int32 MagazineAmmo) const;

	void AutoAssignWheel(const FNoFearInventoryEntry& Entry);

	UNoFearWeaponComponent* GetWeaponComponent() const;

	/** Колесо открыто: мышь двигает курсор; упал, сменился контроллер, открылся экран или колесо приказов — закрыть. */
	void UpdateItemWheel(APlayerController* PC);
	/** Гранаты, которые бросает G, кончились — G бросает другие гранаты из кейса, по порядку секторов колеса. */
	void RefreshGrenadeChoice();
	/** Идёт лечение: шагом, без спринта; упал, сменился контроллер, предмет пропал — прервать; время вышло — FinishHealing. */
	void UpdateHealing(APlayerController* PC);
	/** Прибавить здоровье и потратить предмет. */
	void FinishHealing();
	/** Сбросить лечение: вернуть шаг и снять заглушки. */
	void EndHealing();
	UFUNCTION()
	void HandleHealingDamaged(UNoFearHealthComponent* Component, const FNoFearDamageInfo& Info);
	/** Флаг CharacterInputState у тела GASP (WantsToWalk, WantsToSprint). false — у тела такого поля нет. */
	bool GetInputStateFlag(const TCHAR* Prefix, bool& bOutValue) const;
	void SetInputStateFlag(const TCHAR* Prefix, bool bValue) const;

	TArray<int32> WheelSlots;
	int32 ExpansionLevel = 0;
	bool bCaseActive = false;

	FText NoticeText;
	double NoticeTime = -100.0;

	// --- Колесо предметов ---

	bool bItemWheelOpen = false;
	int32 ItemWheelSelection = INDEX_NONE;
	FVector2D ItemWheelCursor = FVector2D::ZeroVector;
	float TimeDilationBeforeWheel = 1.f;
	/** Контроллер, у которого колесо забрало обзор: ему и вернуть. */
	TWeakObjectPtr<APlayerController> WheelController;

	// --- Лечение ---

	UPROPERTY(Transient)
	TObjectPtr<UNoFearItemData> HealingItem;
	double HealingStartTime = 0.0;
	float HealingDuration = 0.f;
	/** Шагом ли шёл герой до лечения: вернуть после. bHealingWalkSaved — флаг у тела нашёлся. */
	bool bWalkBeforeHealing = false;
	bool bHealingWalkSaved = false;
	TWeakObjectPtr<APlayerController> HealingController;

	// --- Экран кейса ---

	bool bScreenOpen = false;
	float TimeDilationBeforeScreen = 1.f;
	bool bCursorShownBeforeScreen = false;
	/** Контроллер, у которого экран забрал курсор и обзор: ему и вернуть. */
	TWeakObjectPtr<APlayerController> ScreenController;
	/** Экран открыт с хранилищем (OpenContainerScreen). Хранилище исчезло — экран закрывается. */
	bool bContainerScreen = false;
	TWeakObjectPtr<UNoFearItemContainerComponent> OpenContainer;

	FVector2D ScreenCursor = FVector2D::ZeroVector;
	/** Где была мышь в прошлом кадре: курсор берёт её позицию, только когда она сдвинулась. */
	FVector2D LastMousePosition = FVector2D(-1.f, -1.f);
	/** Где нажали ЛКМ: отпустили далеко — это перетаскивание. */
	FVector2D PressCursor = FVector2D::ZeroVector;
	/** Нажатие уже положило предмет или выбрало пункт меню — отпускание ничего не делает. */
	bool bPressConsumed = false;

	EHeldSource HeldSource = EHeldSource::None;
	TWeakObjectPtr<UNoFearItemContainerComponent> HeldStorage;
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
	TWeakObjectPtr<UNoFearItemContainerComponent> MenuStorage;
	int32 MenuEntryId = 0;
	FVector2D MenuPosition = FVector2D::ZeroVector;
	TArray<ENoFearCaseAction> MenuActions;
};
