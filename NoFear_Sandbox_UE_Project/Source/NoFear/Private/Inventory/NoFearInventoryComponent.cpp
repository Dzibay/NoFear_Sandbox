#include "Inventory/NoFearInventoryComponent.h"

#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformTime.h"
#include "Inventory/NoFearItemData.h"
#include "Inventory/NoFearPickup.h"
#include "Kismet/GameplayStatics.h"
#include "Weapons/NoFearWeapon.h"
#include "Weapons/NoFearWeaponComponent.h"
#include "Weapons/NoFearWeaponData.h"

#define LOCTEXT_NAMESPACE "NoFearInventory"

FIntPoint FNoFearInventoryEntry::GetSize() const
{
	const FIntPoint Size = Item ? Item->GetGridSize() : FIntPoint(1, 1);
	return bRotated ? FIntPoint(Size.Y, Size.X) : Size;
}

namespace
{
	FNoFearItemStack MakeStack(const TCHAR* Path, int32 Count)
	{
		FNoFearItemStack Stack;
		Stack.Item = TSoftObjectPtr<UNoFearItemData>(FSoftObjectPath(Path));
		Stack.Count = Count;
		return Stack;
	}

	bool IsWheelKind(const UNoFearItemData* Item)
	{
		return Item && (Item->Kind == ENoFearItemKind::Weapon || Item->Kind == ENoFearItemKind::Grenade || Item->Kind == ENoFearItemKind::Medicine);
	}
}

UNoFearInventoryComponent::UNoFearInventoryComponent()
{
	// Тикает только у героя игрока: курсор экрана кейса и контекст ввода с I.
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;

	GridSizes = { FIntPoint(8, 5), FIntPoint(9, 6), FIntPoint(10, 7) };
	StartingItems = {
		MakeStack(TEXT("/Game/Items/DA_Item_Pistol.DA_Item_Pistol"), 1),
		MakeStack(TEXT("/Game/Items/DA_Item_PistolAmmo.DA_Item_PistolAmmo"), 24),
		MakeStack(TEXT("/Game/Items/DA_Item_Bandage.DA_Item_Bandage"), 1),
	};
	WheelSlots.Init(0, WheelSlotCount);
}

// ---------------------------------------------------------------------------
// Включение
// ---------------------------------------------------------------------------

UNoFearInventoryComponent* UNoFearInventoryComponent::FindCase(const AActor* Actor)
{
	UNoFearInventoryComponent* Case = Actor ? Actor->FindComponentByClass<UNoFearInventoryComponent>() : nullptr;
	return Case && Case->bCaseActive ? Case : nullptr;
}

bool UNoFearInventoryComponent::WillManageWeapons(const AActor* Owner)
{
	const UNoFearInventoryComponent* Case = Owner ? Owner->FindComponentByClass<UNoFearInventoryComponent>() : nullptr;
	if (!Case)
	{
		return false;
	}
	const APawn* Pawn = Cast<APawn>(Owner);
	return Case->bCaseActive || (Case->bEnabled && Pawn && Pawn->IsPlayerControlled());
}

void UNoFearInventoryComponent::BeginPlay()
{
	Super::BeginPlay();
	TryActivate();
}

void UNoFearInventoryComponent::HandleControllerChanged()
{
	const APawn* Pawn = Cast<APawn>(GetOwner());
	if (bScreenOpen && (!Pawn || !Pawn->IsPlayerControlled()))
	{
		CloseScreen();
	}
	TryActivate();
}

void UNoFearInventoryComponent::TryActivate()
{
	const APawn* Pawn = Cast<APawn>(GetOwner());
	if (bCaseActive || !bEnabled || !HasBegunPlay() || !Pawn || !Pawn->IsPlayerControlled())
	{
		return;
	}
	bCaseActive = true;
	ExpansionLevel = 0;
	WheelSlots.Init(0, WheelSlotCount);
	SetComponentTickEnabled(true);

	// Новое тело начинает игру раньше, чем в него вселяется игрок, и успевает получить StartingWeapons: их заменяет кейс.
	if (UNoFearWeaponComponent* Weapons = GetWeaponComponent())
	{
		Weapons->RemoveAllWeapons();
	}
	GrantStartingItems();

	if (UNoFearWeaponComponent* Weapons = GetWeaponComponent(); Weapons && bEquipOnStart)
	{
		for (const FNoFearInventoryEntry& Entry : Entries)
		{
			if (Entry.Weapon)
			{
				Weapons->EquipWeapon(Weapons->GetWeapons().IndexOfByKey(Entry.Weapon.Get()));
				break;
			}
		}
	}
}

void UNoFearInventoryComponent::GrantStartingItems()
{
	for (const FNoFearItemStack& Stack : StartingItems)
	{
		UNoFearItemData* Item = Stack.Item.LoadSynchronous();
		if (!Item)
		{
			if (!Stack.Item.IsNull())
			{
				UE_LOG(LogTemp, Warning, TEXT("NoFearInventory: стартовый предмет %s не найден"), *Stack.Item.ToString());
			}
			continue;
		}
		const int32 Added = AddItem(Item, Stack.Count);
		if (Added < Stack.Count)
		{
			UE_LOG(LogTemp, Warning, TEXT("NoFearInventory: стартовый предмет %s не влез в кейс (%d из %d)"), *Item->GetName(), Added, Stack.Count);
		}
	}
}

UNoFearWeaponComponent* UNoFearInventoryComponent::GetWeaponComponent() const
{
	return GetOwner() ? GetOwner()->FindComponentByClass<UNoFearWeaponComponent>() : nullptr;
}

// ---------------------------------------------------------------------------
// Сетка
// ---------------------------------------------------------------------------

FIntPoint UNoFearInventoryComponent::GetGridSize() const
{
	FIntPoint Size(8, 5);
	if (GridSizes.Num() > 0)
	{
		Size = GridSizes[FMath::Clamp(ExpansionLevel, 0, GridSizes.Num() - 1)];
	}
	return FIntPoint(FMath::Max(Size.X, 1), FMath::Max(Size.Y, 1));
}

bool UNoFearInventoryComponent::AddExpansion()
{
	if (!bCaseActive || !CanExpand())
	{
		return false;
	}
	++ExpansionLevel;
	NotifyChanged();
	return true;
}

void UNoFearInventoryComponent::BuildOccupancy(TArray<int32>& OutGrid, int32 IgnoreId) const
{
	const FIntPoint GridSize = GetGridSize();
	OutGrid.Init(0, GridSize.X * GridSize.Y);
	for (const FNoFearInventoryEntry& Entry : Entries)
	{
		if (Entry.Id != IgnoreId)
		{
			MarkArea(OutGrid, Entry.Cell, Entry.GetSize(), Entry.Id);
		}
	}
}

void UNoFearInventoryComponent::MarkArea(TArray<int32>& Grid, FIntPoint Cell, FIntPoint Size, int32 Id) const
{
	const FIntPoint GridSize = GetGridSize();
	for (int32 Y = FMath::Max(Cell.Y, 0); Y < FMath::Min(Cell.Y + Size.Y, GridSize.Y); ++Y)
	{
		for (int32 X = FMath::Max(Cell.X, 0); X < FMath::Min(Cell.X + Size.X, GridSize.X); ++X)
		{
			Grid[Y * GridSize.X + X] = Id;
		}
	}
}

bool UNoFearInventoryComponent::IsAreaFree(const TArray<int32>& Grid, FIntPoint Cell, FIntPoint Size) const
{
	const FIntPoint GridSize = GetGridSize();
	if (Cell.X < 0 || Cell.Y < 0 || Cell.X + Size.X > GridSize.X || Cell.Y + Size.Y > GridSize.Y)
	{
		return false;
	}
	for (int32 Y = Cell.Y; Y < Cell.Y + Size.Y; ++Y)
	{
		for (int32 X = Cell.X; X < Cell.X + Size.X; ++X)
		{
			if (Grid[Y * GridSize.X + X] != 0)
			{
				return false;
			}
		}
	}
	return true;
}

bool UNoFearInventoryComponent::FindFreeCell(const TArray<int32>& Grid, FIntPoint ItemSize, FIntPoint& OutCell, bool& bOutRotated) const
{
	const FIntPoint GridSize = GetGridSize();
	// Сначала как есть, потом повёрнутым: сверху вниз, слева направо.
	for (int32 Pass = 0; Pass < 2; ++Pass)
	{
		const bool bRotated = Pass == 1;
		if (bRotated && ItemSize.X == ItemSize.Y)
		{
			break;
		}
		const FIntPoint Size = bRotated ? FIntPoint(ItemSize.Y, ItemSize.X) : ItemSize;
		for (int32 Y = 0; Y + Size.Y <= GridSize.Y; ++Y)
		{
			for (int32 X = 0; X + Size.X <= GridSize.X; ++X)
			{
				if (IsAreaFree(Grid, FIntPoint(X, Y), Size))
				{
					OutCell = FIntPoint(X, Y);
					bOutRotated = bRotated;
					return true;
				}
			}
		}
	}
	return false;
}

bool UNoFearInventoryComponent::CanPlaceAt(const UNoFearItemData* Item, FIntPoint Cell, bool bRotated, int32 IgnoreId) const
{
	if (!Item || Item->Kind == ENoFearItemKind::CaseExpansion)
	{
		return false;
	}
	TArray<int32> Grid;
	BuildOccupancy(Grid, IgnoreId);
	const FIntPoint Size = Item->GetGridSize();
	return IsAreaFree(Grid, Cell, bRotated ? FIntPoint(Size.Y, Size.X) : Size);
}

// ---------------------------------------------------------------------------
// Что лежит
// ---------------------------------------------------------------------------

int32 UNoFearInventoryComponent::FindEntryIndex(int32 Id) const
{
	if (Id == 0)
	{
		return INDEX_NONE;
	}
	return Entries.IndexOfByPredicate([Id](const FNoFearInventoryEntry& Entry) { return Entry.Id == Id; });
}

bool UNoFearInventoryComponent::GetEntry(int32 Id, FNoFearInventoryEntry& OutEntry) const
{
	const int32 Index = FindEntryIndex(Id);
	if (Index == INDEX_NONE)
	{
		return false;
	}
	OutEntry = Entries[Index];
	return true;
}

int32 UNoFearInventoryComponent::GetEntryAt(FIntPoint Cell) const
{
	for (const FNoFearInventoryEntry& Entry : Entries)
	{
		const FIntPoint Size = Entry.GetSize();
		if (Cell.X >= Entry.Cell.X && Cell.X < Entry.Cell.X + Size.X && Cell.Y >= Entry.Cell.Y && Cell.Y < Entry.Cell.Y + Size.Y)
		{
			return Entry.Id;
		}
	}
	return 0;
}

int32 UNoFearInventoryComponent::CountItem(const UNoFearItemData* Item) const
{
	int32 Total = 0;
	for (const FNoFearInventoryEntry& Entry : Entries)
	{
		if (Item && Entry.Item == Item)
		{
			Total += Entry.Count;
		}
	}
	return Total;
}

int32 UNoFearInventoryComponent::FindEntryForWeapon(const ANoFearWeapon* Weapon) const
{
	for (const FNoFearInventoryEntry& Entry : Entries)
	{
		if (Weapon && Entry.Weapon == Weapon)
		{
			return Entry.Id;
		}
	}
	return 0;
}

int32 UNoFearInventoryComponent::GetRoomFor(UNoFearItemData* Item, int32 Count) const
{
	// Без bApply AddInternal ничего не меняет, только считает.
	return const_cast<UNoFearInventoryComponent*>(this)->AddInternal(Item, Count, -1, false);
}

// ---------------------------------------------------------------------------
// Изменения
// ---------------------------------------------------------------------------

int32 UNoFearInventoryComponent::AddItem(UNoFearItemData* Item, int32 Count, int32 MagazineAmmo)
{
	return AddInternal(Item, Count, MagazineAmmo, true);
}

int32 UNoFearInventoryComponent::AddInternal(UNoFearItemData* Item, int32 Count, int32 MagazineAmmo, bool bApply)
{
	if (!bCaseActive || !Item || Count <= 0)
	{
		return 0;
	}

	if (Item->Kind == ENoFearItemKind::CaseExpansion)
	{
		const int32 Steps = FMath::Min(Count, GetMaxExpansionLevel() - ExpansionLevel);
		for (int32 Step = 0; bApply && Step < Steps; ++Step)
		{
			AddExpansion();
		}
		return FMath::Max(Steps, 0);
	}

	const int32 MaxStack = Item->GetMaxStack();
	int32 Left = Count;

	// Сначала доливаем неполные стопки.
	for (FNoFearInventoryEntry& Entry : Entries)
	{
		if (Left <= 0)
		{
			break;
		}
		if (Entry.Item == Item && Entry.Count < MaxStack)
		{
			const int32 Added = FMath::Min(Left, MaxStack - Entry.Count);
			if (bApply)
			{
				Entry.Count += Added;
			}
			Left -= Added;
		}
	}

	// Остаток — новыми стопками на свободные клетки.
	TArray<int32> Grid;
	BuildOccupancy(Grid);
	while (Left > 0)
	{
		FIntPoint Cell;
		bool bRotated = false;
		if (!FindFreeCell(Grid, Item->GetGridSize(), Cell, bRotated))
		{
			break;
		}
		const int32 Added = FMath::Min(Left, MaxStack);
		const int32 Id = bApply ? CreateEntry(Item, Added, Cell, bRotated, MagazineAmmo, bAutoAssignWheel) : INDEX_NONE;
		const FIntPoint Size = Item->GetGridSize();
		MarkArea(Grid, Cell, bRotated ? FIntPoint(Size.Y, Size.X) : Size, Id);
		Left -= Added;
	}

	const int32 Total = Count - Left;
	if (bApply && Total > 0)
	{
		NotifyChanged();
	}
	return Total;
}

int32 UNoFearInventoryComponent::AddItemOrDrop(UNoFearItemData* Item, int32 Count, int32 MagazineAmmo)
{
	const int32 Added = AddItem(Item, Count, MagazineAmmo);
	const int32 Left = Count - Added;
	AActor* Owner = GetOwner();
	if (Item && Left > 0 && Owner)
	{
		// Под ноги, чуть впереди, чтобы не провалилось в капсулу.
		const FVector Location = Owner->GetActorLocation() + Owner->GetActorForwardVector() * 40.f;
		ANoFearPickup::SpawnPickup(GetWorld(), Item, Left, MagazineAmmo, FTransform(Owner->GetActorRotation(), Location), FVector::ZeroVector);
		PushNotice(LOCTEXT("NoRoomDropped", "Нет места в кейсе"));
	}
	return Added;
}

int32 UNoFearInventoryComponent::PlaceItemAt(UNoFearItemData* Item, int32 Count, FIntPoint Cell, bool bRotated, int32 MagazineAmmo)
{
	if (!bCaseActive || !Item || Count <= 0 || !CanPlaceAt(Item, Cell, bRotated))
	{
		return 0;
	}
	const int32 Id = CreateEntry(Item, FMath::Min(Count, Item->GetMaxStack()), Cell, bRotated, MagazineAmmo, bAutoAssignWheel);
	NotifyChanged();
	return Id;
}

int32 UNoFearInventoryComponent::CreateEntry(UNoFearItemData* Item, int32 Count, FIntPoint Cell, bool bRotated, int32 MagazineAmmo, bool bAssignWheel)
{
	FNoFearInventoryEntry Entry;
	Entry.Id = NextId++;
	Entry.Item = Item;
	Entry.Count = Count;
	Entry.Cell = Cell;
	Entry.bRotated = bRotated;

	if (Item->Kind == ENoFearItemKind::Weapon)
	{
		UNoFearWeaponComponent* Weapons = GetWeaponComponent();
		if (Weapons && Item->WeaponData)
		{
			Entry.Weapon = Weapons->AddWeaponInstance(Item->WeaponData, MagazineAmmo);
			if (Entry.Weapon)
			{
				Entry.Weapon->SetAmmoSource(this, Item->AmmoItem);
			}
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("NoFearInventory: у %s нет WeaponData — ствол в руки не возьмётся"), *Item->GetName());
		}
	}

	Entries.Add(Entry);
	if (bAssignWheel)
	{
		AutoAssignWheel(Entry);
	}
	return Entry.Id;
}

void UNoFearInventoryComponent::AutoAssignWheel(const FNoFearInventoryEntry& Entry)
{
	if (!IsWheelKind(Entry.Item))
	{
		return;
	}
	// Стопки одного предмета делят сектор: вторая пачка бинтов не занимает новый. Стволы — каждый свой.
	if (Entry.Item->Kind != ENoFearItemKind::Weapon)
	{
		for (const int32 SlotId : WheelSlots)
		{
			const int32 Index = FindEntryIndex(SlotId);
			if (Index != INDEX_NONE && Entries[Index].Item == Entry.Item)
			{
				return;
			}
		}
	}
	for (int32& SlotId : WheelSlots)
	{
		if (SlotId == 0)
		{
			SlotId = Entry.Id;
			return;
		}
	}
}

void UNoFearInventoryComponent::ReleaseEntryAt(int32 Index)
{
	if (!Entries.IsValidIndex(Index))
	{
		return;
	}
	const FNoFearInventoryEntry Entry = Entries[Index];
	Entries.RemoveAt(Index);

	for (int32& SlotId : WheelSlots)
	{
		if (SlotId != Entry.Id)
		{
			continue;
		}
		// Кончилась стопка — сектор переходит на другую стопку того же предмета, если она ещё ни в каком секторе.
		SlotId = 0;
		for (const FNoFearInventoryEntry& Other : Entries)
		{
			if (Other.Item == Entry.Item && !WheelSlots.Contains(Other.Id))
			{
				SlotId = Other.Id;
				break;
			}
		}
	}

	if (Entry.Weapon)
	{
		if (UNoFearWeaponComponent* Weapons = GetWeaponComponent())
		{
			Weapons->RemoveWeapon(Entry.Weapon);
		}
		else
		{
			Entry.Weapon->Destroy();
		}
	}
}

bool UNoFearInventoryComponent::MoveEntry(int32 Id, FIntPoint Cell, bool bRotated)
{
	const int32 Index = FindEntryIndex(Id);
	if (Index == INDEX_NONE || !CanPlaceAt(Entries[Index].Item, Cell, bRotated, Id))
	{
		return false;
	}
	Entries[Index].Cell = Cell;
	Entries[Index].bRotated = bRotated;
	NotifyChanged();
	return true;
}

int32 UNoFearInventoryComponent::SplitStack(int32 Id, int32 Count)
{
	const int32 Index = FindEntryIndex(Id);
	if (Index == INDEX_NONE || Count <= 0 || Count >= Entries[Index].Count)
	{
		return 0;
	}
	UNoFearItemData* Item = Entries[Index].Item;
	TArray<int32> Grid;
	BuildOccupancy(Grid);
	FIntPoint Cell;
	bool bRotated = false;
	if (!FindFreeCell(Grid, Item->GetGridSize(), Cell, bRotated))
	{
		return 0;
	}
	Entries[Index].Count -= Count;
	const int32 NewId = CreateEntry(Item, Count, Cell, bRotated, -1, false);
	NotifyChanged();
	return NewId;
}

bool UNoFearInventoryComponent::RemoveEntry(int32 Id)
{
	const int32 Index = FindEntryIndex(Id);
	if (Index == INDEX_NONE)
	{
		return false;
	}
	ReleaseEntryAt(Index);
	NotifyChanged();
	return true;
}

int32 UNoFearInventoryComponent::RemoveItem(const UNoFearItemData* Item, int32 Count)
{
	if (!Item || Count <= 0)
	{
		return 0;
	}

	// Сначала из самых маленьких стопок: неполные пачки уходят первыми и освобождают клетки.
	TArray<int32> Ids;
	for (const FNoFearInventoryEntry& Entry : Entries)
	{
		if (Entry.Item == Item)
		{
			Ids.Add(Entry.Id);
		}
	}
	Ids.Sort([this](int32 A, int32 B) { return Entries[FindEntryIndex(A)].Count < Entries[FindEntryIndex(B)].Count; });

	int32 Left = Count;
	for (const int32 Id : Ids)
	{
		if (Left <= 0)
		{
			break;
		}
		const int32 Index = FindEntryIndex(Id);
		FNoFearInventoryEntry& Entry = Entries[Index];
		const int32 Taken = FMath::Min(Left, Entry.Count);
		Entry.Count -= Taken;
		Left -= Taken;
		if (Entry.Count <= 0)
		{
			ReleaseEntryAt(Index);
		}
	}

	const int32 Removed = Count - Left;
	if (Removed > 0)
	{
		NotifyChanged();
	}
	return Removed;
}

ANoFearPickup* UNoFearInventoryComponent::DropEntry(int32 Id, int32 Count)
{
	const int32 Index = FindEntryIndex(Id);
	AActor* Owner = GetOwner();
	if (Index == INDEX_NONE || !Owner)
	{
		return nullptr;
	}

	FNoFearInventoryEntry& Entry = Entries[Index];
	UNoFearItemData* Item = Entry.Item;
	const int32 DropCount = Count <= 0 ? Entry.Count : FMath::Min(Count, Entry.Count);
	const int32 Magazine = Entry.Weapon ? Entry.Weapon->GetMagazineAmmo() : -1;
	if (DropCount >= Entry.Count)
	{
		ReleaseEntryAt(Index);
	}
	else
	{
		Entry.Count -= DropCount;
	}
	NotifyChanged();
	return SpawnDropped(Item, DropCount, Magazine);
}

ANoFearPickup* UNoFearInventoryComponent::SpawnDropped(UNoFearItemData* Item, int32 Count, int32 MagazineAmmo) const
{
	const AActor* Owner = GetOwner();
	if (!Owner || !Item || Count <= 0)
	{
		return nullptr;
	}
	// Перед героем на уровне пояса, с толчком вперёд: предмет падает и катится.
	const FVector Forward = Owner->GetActorForwardVector();
	const FVector Location = Owner->GetActorLocation() + Forward * DropDistance + FVector(0.f, 0.f, 10.f);
	const FVector Velocity = Owner->GetVelocity() + Forward * 150.f + FVector(0.f, 0.f, 120.f);
	return ANoFearPickup::SpawnPickup(GetWorld(), Item, Count, MagazineAmmo, FTransform(Owner->GetActorRotation(), Location), Velocity);
}

void UNoFearInventoryComponent::ClearCase()
{
	while (Entries.Num() > 0)
	{
		ReleaseEntryAt(Entries.Num() - 1);
	}
	ExpansionLevel = 0;
	WheelSlots.Init(0, WheelSlotCount);
	NotifyChanged();
}

void UNoFearInventoryComponent::NotifyChanged()
{
	// Запас в HUD патронов — это патроны в кейсе: обновить у всех стволов.
	for (const FNoFearInventoryEntry& Entry : Entries)
	{
		if (Entry.Weapon)
		{
			Entry.Weapon->RefreshAmmo();
		}
	}
	OnInventoryChanged.Broadcast(this);
}

// ---------------------------------------------------------------------------
// Колесо
// ---------------------------------------------------------------------------

int32 UNoFearInventoryComponent::GetWheelSlot(int32 Slot) const
{
	return WheelSlots.IsValidIndex(Slot) ? WheelSlots[Slot] : 0;
}

bool UNoFearInventoryComponent::AssignWheelSlot(int32 Slot, int32 EntryId)
{
	const int32 Index = FindEntryIndex(EntryId);
	if (!WheelSlots.IsValidIndex(Slot) || Index == INDEX_NONE || !IsWheelKind(Entries[Index].Item))
	{
		return false;
	}
	for (int32& SlotId : WheelSlots)
	{
		if (SlotId == EntryId)
		{
			SlotId = 0;
		}
	}
	WheelSlots[Slot] = EntryId;
	NotifyChanged();
	return true;
}

void UNoFearInventoryComponent::ClearWheelSlot(int32 Slot)
{
	if (WheelSlots.IsValidIndex(Slot) && WheelSlots[Slot] != 0)
	{
		WheelSlots[Slot] = 0;
		NotifyChanged();
	}
}

int32 UNoFearInventoryComponent::FindWheelSlotOf(int32 EntryId) const
{
	return EntryId != 0 ? WheelSlots.IndexOfByKey(EntryId) : INDEX_NONE;
}

bool UNoFearInventoryComponent::IsWheelItem(const UNoFearItemData* Item)
{
	return IsWheelKind(Item);
}

// ---------------------------------------------------------------------------
// Сообщения
// ---------------------------------------------------------------------------

void UNoFearInventoryComponent::PushNotice(const FText& Text)
{
	NoticeText = Text;
	// Реальное время: экран кейса замедляет игру, а строка должна гаснуть с обычной скоростью.
	NoticeTime = FPlatformTime::Seconds();
}

FText UNoFearInventoryComponent::GetNotice(float& OutAlpha) const
{
	const double Elapsed = FPlatformTime::Seconds() - NoticeTime;
	if (NoticeText.IsEmpty() || Elapsed > NoticeDuration)
	{
		OutAlpha = 0.f;
		return FText::GetEmpty();
	}
	OutAlpha = FMath::Clamp(static_cast<float>((NoticeDuration - Elapsed) / 0.5), 0.f, 1.f);
	return NoticeText;
}

// ---------------------------------------------------------------------------
// Консоль
// ---------------------------------------------------------------------------

namespace
{
	UNoFearInventoryComponent* GetPlayerCase(UWorld* World)
	{
		UNoFearInventoryComponent* Case = UNoFearInventoryComponent::FindCase(UGameplayStatics::GetPlayerPawn(World, 0));
		if (!Case)
		{
			UE_LOG(LogTemp, Warning, TEXT("NoFear.Inv: у героя нет кейса"));
		}
		return Case;
	}

	/** «Pistol» → /Game/Items/DA_Item_Pistol, полный путь — как есть. */
	UNoFearItemData* LoadItem(const FString& Name)
	{
		FString Path = Name;
		if (!Path.StartsWith(TEXT("/")))
		{
			const FString Asset = Path.StartsWith(TEXT("DA_Item_")) ? Path : TEXT("DA_Item_") + Path;
			Path = FString::Printf(TEXT("/Game/Items/%s.%s"), *Asset, *Asset);
		}
		UNoFearItemData* Item = LoadObject<UNoFearItemData>(nullptr, *Path);
		if (!Item)
		{
			UE_LOG(LogTemp, Warning, TEXT("NoFear.Inv: нет предмета %s"), *Path);
		}
		return Item;
	}

	void GiveCommand(const TArray<FString>& Args, UWorld* World)
	{
		UNoFearInventoryComponent* Case = GetPlayerCase(World);
		UNoFearItemData* Item = Case && Args.Num() > 0 ? LoadItem(Args[0]) : nullptr;
		if (!Item)
		{
			return;
		}
		const int32 Count = Args.Num() > 1 ? FMath::Max(FCString::Atoi(*Args[1]), 1) : Item->GetDefaultPickupCount();
		const int32 Added = Case->AddItemOrDrop(Item, Count);
		UE_LOG(LogTemp, Log, TEXT("NoFear.Inv.Give: %s ×%d, в кейс легло %d"), *Item->GetName(), Count, Added);
	}

	void KitCommand(const TArray<FString>& Args, UWorld* World)
	{
		UNoFearInventoryComponent* Case = GetPlayerCase(World);
		if (!Case)
		{
			return;
		}
		Case->ClearCase();
		while (Case->AddExpansion())
		{
		}
		// Прежний набор героя: три ствола с их стартовым запасом и три гранаты. Первый ствол — в руки.
		for (const TCHAR* Name : { TEXT("Rifle"), TEXT("Pistol"), TEXT("Shotgun") })
		{
			UNoFearItemData* Item = LoadItem(Name);
			if (!Item)
			{
				continue;
			}
			Case->AddItem(Item, 1);
			if (Item->WeaponData && Item->AmmoItem)
			{
				Case->AddItem(Item->AmmoItem, Item->WeaponData->StartingReserveAmmo);
			}
		}
		if (UNoFearItemData* Grenade = LoadItem(TEXT("Grenade")))
		{
			Case->AddItem(Grenade, 3);
		}
		if (UNoFearItemData* Bandage = LoadItem(TEXT("Bandage")))
		{
			Case->AddItem(Bandage, 1);
		}

		const APawn* Pawn = Cast<APawn>(Case->GetOwner());
		UNoFearWeaponComponent* Weapons = Pawn ? Pawn->FindComponentByClass<UNoFearWeaponComponent>() : nullptr;
		if (Weapons && Weapons->GetWeapons().Num() > 0)
		{
			Weapons->EquipWeapon(0);
		}
	}

	void ClearCommand(const TArray<FString>& Args, UWorld* World)
	{
		if (UNoFearInventoryComponent* Case = GetPlayerCase(World))
		{
			Case->ClearCase();
		}
	}

	void SpawnCommand(const TArray<FString>& Args, UWorld* World)
	{
		const APawn* Pawn = UGameplayStatics::GetPlayerPawn(World, 0);
		UNoFearItemData* Item = Pawn && Args.Num() > 0 ? LoadItem(Args[0]) : nullptr;
		if (!Item)
		{
			return;
		}
		const int32 Count = Args.Num() > 1 ? FMath::Max(FCString::Atoi(*Args[1]), 1) : 0;
		const float Distance = Args.Num() > 2 ? FCString::Atof(*Args[2]) : 150.f;
		const FRotator View(0.f, Pawn->GetControlRotation().Yaw, 0.f);
		const FVector Location = Pawn->GetActorLocation() + View.Vector() * Distance;
		ANoFearPickup::SpawnPickup(World, Item, Count, -1, FTransform(View, Location), FVector::ZeroVector);
	}

	void DropCommand(const TArray<FString>& Args, UWorld* World)
	{
		UNoFearInventoryComponent* Case = GetPlayerCase(World);
		if (Case && Args.Num() > 0)
		{
			Case->DropEntry(FCString::Atoi(*Args[0]), Args.Num() > 1 ? FCString::Atoi(*Args[1]) : 0);
		}
	}

	void ListCommand(const TArray<FString>& Args, UWorld* World)
	{
		const UNoFearInventoryComponent* Case = GetPlayerCase(World);
		if (!Case)
		{
			return;
		}
		const FIntPoint Size = Case->GetGridSize();
		UE_LOG(LogTemp, Log, TEXT("NoFear.Inv: кейс %dx%d, расширений %d"), Size.X, Size.Y, Case->GetExpansionLevel());
		for (const FNoFearInventoryEntry& Entry : Case->GetEntriesRef())
		{
			UE_LOG(LogTemp, Log, TEXT("  #%d %s ×%d at (%d,%d)%s%s"), Entry.Id, Entry.Item ? *Entry.Item->GetName() : TEXT("?"), Entry.Count,
				Entry.Cell.X, Entry.Cell.Y, Entry.bRotated ? TEXT(" rot") : TEXT(""),
				Entry.Weapon ? *FString::Printf(TEXT(" mag %d"), Entry.Weapon->GetMagazineAmmo()) : TEXT(""));
		}
		for (int32 Slot = 0; Slot < UNoFearInventoryComponent::WheelSlotCount; ++Slot)
		{
			UE_LOG(LogTemp, Log, TEXT("  wheel %d: #%d"), Slot + 1, Case->GetWheelSlot(Slot));
		}
	}

	FAutoConsoleCommandWithWorldAndArgs InvGiveCommand(
		TEXT("NoFear.Inv.Give"),
		TEXT("Положить предмет в кейс героя: NoFear.Inv.Give Rifle | RifleAmmo 30 | /Game/Items/DA_Item_X. Не влезло — падает под ноги."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&GiveCommand));

	FAutoConsoleCommandWithWorldAndArgs InvKitCommand(
		TEXT("NoFear.Inv.Kit"),
		TEXT("Полный набор для проверок: кейс до максимума, автомат, пистолет, дробовик с запасом, три гранаты, бинт."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&KitCommand));

	FAutoConsoleCommandWithWorldAndArgs InvClearCommand(
		TEXT("NoFear.Inv.Clear"),
		TEXT("Опустошить кейс героя и вернуть его к начальному размеру."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&ClearCommand));

	FAutoConsoleCommandWithWorldAndArgs InvSpawnCommand(
		TEXT("NoFear.Inv.Spawn"),
		TEXT("Положить предмет на землю перед героем: NoFear.Inv.Spawn Shells [число=полная стопка] [расстояние=150]."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&SpawnCommand));

	FAutoConsoleCommandWithWorldAndArgs InvDropCommand(
		TEXT("NoFear.Inv.Drop"),
		TEXT("Выбросить предмет кейса по номеру (NoFear.Inv.List): NoFear.Inv.Drop 3 [число=вся стопка]."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&DropCommand));

	FAutoConsoleCommandWithWorldAndArgs InvListCommand(
		TEXT("NoFear.Inv.List"),
		TEXT("Вывести в лог, что лежит в кейсе и в секторах колеса."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&ListCommand));
}

#undef LOCTEXT_NAMESPACE
