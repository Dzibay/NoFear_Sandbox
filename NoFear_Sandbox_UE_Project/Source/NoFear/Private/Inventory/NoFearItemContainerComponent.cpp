// Сетка с предметами — общая основа кейса героя и хранилищ (Docs/Inventory_Design.md). Кейс — NoFearInventoryComponent.cpp.

#include "Inventory/NoFearItemContainerComponent.h"

#include "GameFramework/Actor.h"
#include "Inventory/NoFearItemData.h"
#include "Weapons/NoFearWeapon.h"

#define LOCTEXT_NAMESPACE "NoFearItemContainer"

FIntPoint FNoFearInventoryEntry::GetSize() const
{
	const FIntPoint Size = Item ? Item->GetGridSize() : FIntPoint(1, 1);
	return bRotated ? FIntPoint(Size.Y, Size.X) : Size;
}

int32 FNoFearInventoryEntry::GetMagazineAmmo() const
{
	return Weapon ? Weapon->GetMagazineAmmo() : MagazineAmmo;
}

UNoFearItemContainerComponent::UNoFearItemContainerComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	DisplayName = LOCTEXT("StorageName", "Хранилище");
}

void UNoFearItemContainerComponent::BeginPlay()
{
	Super::BeginPlay();
	// Кейс героя получает стартовый набор, когда включится под игроком.
	if (CanStoreItems())
	{
		GrantStartingItems();
	}
}

void UNoFearItemContainerComponent::GrantStartingItems()
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
		const int32 Added = AddItem(Item, Stack.Count, Stack.MagazineAmmo);
		if (Added < Stack.Count)
		{
			UE_LOG(LogTemp, Warning, TEXT("NoFearInventory: стартовый предмет %s не влез в %s (%d из %d)"), *Item->GetName(),
				*GetNameSafe(GetOwner()), Added, Stack.Count);
		}
	}
}

// ---------------------------------------------------------------------------
// Сетка
// ---------------------------------------------------------------------------

FIntPoint UNoFearItemContainerComponent::GetGridSize() const
{
	return FIntPoint(FMath::Max(GridSize.X, 1), FMath::Max(GridSize.Y, 1));
}

void UNoFearItemContainerComponent::BuildOccupancy(TArray<int32>& OutGrid, int32 IgnoreId) const
{
	const FIntPoint Size = GetGridSize();
	OutGrid.Init(0, Size.X * Size.Y);
	for (const FNoFearInventoryEntry& Entry : Entries)
	{
		if (Entry.Id != IgnoreId)
		{
			MarkArea(OutGrid, Entry.Cell, Entry.GetSize(), Entry.Id);
		}
	}
}

void UNoFearItemContainerComponent::MarkArea(TArray<int32>& Grid, FIntPoint Cell, FIntPoint Size, int32 Id) const
{
	const FIntPoint Extent = GetGridSize();
	for (int32 Y = FMath::Max(Cell.Y, 0); Y < FMath::Min(Cell.Y + Size.Y, Extent.Y); ++Y)
	{
		for (int32 X = FMath::Max(Cell.X, 0); X < FMath::Min(Cell.X + Size.X, Extent.X); ++X)
		{
			Grid[Y * Extent.X + X] = Id;
		}
	}
}

bool UNoFearItemContainerComponent::IsAreaFree(const TArray<int32>& Grid, FIntPoint Cell, FIntPoint Size) const
{
	const FIntPoint Extent = GetGridSize();
	if (Cell.X < 0 || Cell.Y < 0 || Cell.X + Size.X > Extent.X || Cell.Y + Size.Y > Extent.Y)
	{
		return false;
	}
	for (int32 Y = Cell.Y; Y < Cell.Y + Size.Y; ++Y)
	{
		for (int32 X = Cell.X; X < Cell.X + Size.X; ++X)
		{
			if (Grid[Y * Extent.X + X] != 0)
			{
				return false;
			}
		}
	}
	return true;
}

bool UNoFearItemContainerComponent::FindFreeCell(const TArray<int32>& Grid, FIntPoint ItemSize, FIntPoint& OutCell, bool& bOutRotated) const
{
	const FIntPoint Extent = GetGridSize();
	// Сначала как есть, потом повёрнутым: сверху вниз, слева направо.
	for (int32 Pass = 0; Pass < 2; ++Pass)
	{
		const bool bRotated = Pass == 1;
		if (bRotated && ItemSize.X == ItemSize.Y)
		{
			break;
		}
		const FIntPoint Size = bRotated ? FIntPoint(ItemSize.Y, ItemSize.X) : ItemSize;
		for (int32 Y = 0; Y + Size.Y <= Extent.Y; ++Y)
		{
			for (int32 X = 0; X + Size.X <= Extent.X; ++X)
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

bool UNoFearItemContainerComponent::CanHoldItem(const UNoFearItemData* Item) const
{
	return Item != nullptr;
}

bool UNoFearItemContainerComponent::CanPlaceAt(const UNoFearItemData* Item, FIntPoint Cell, bool bRotated, int32 IgnoreId) const
{
	if (!CanHoldItem(Item))
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

int32 UNoFearItemContainerComponent::FindEntryIndex(int32 Id) const
{
	if (Id == 0)
	{
		return INDEX_NONE;
	}
	return Entries.IndexOfByPredicate([Id](const FNoFearInventoryEntry& Entry) { return Entry.Id == Id; });
}

bool UNoFearItemContainerComponent::GetEntry(int32 Id, FNoFearInventoryEntry& OutEntry) const
{
	const int32 Index = FindEntryIndex(Id);
	if (Index == INDEX_NONE)
	{
		return false;
	}
	OutEntry = Entries[Index];
	return true;
}

const FNoFearInventoryEntry* UNoFearItemContainerComponent::FindEntry(int32 Id) const
{
	const int32 Index = FindEntryIndex(Id);
	return Index != INDEX_NONE ? &Entries[Index] : nullptr;
}

int32 UNoFearItemContainerComponent::GetEntryAt(FIntPoint Cell) const
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

int32 UNoFearItemContainerComponent::CountItem(const UNoFearItemData* Item) const
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

int32 UNoFearItemContainerComponent::GetRoomFor(UNoFearItemData* Item, int32 Count) const
{
	// Без bApply AddInternal ничего не меняет, только считает.
	return const_cast<UNoFearItemContainerComponent*>(this)->AddInternal(Item, Count, -1, false);
}

// ---------------------------------------------------------------------------
// Изменения
// ---------------------------------------------------------------------------

int32 UNoFearItemContainerComponent::AddItem(UNoFearItemData* Item, int32 Count, int32 MagazineAmmo)
{
	return AddInternal(Item, Count, MagazineAmmo, true);
}

int32 UNoFearItemContainerComponent::AddInternal(UNoFearItemData* Item, int32 Count, int32 MagazineAmmo, bool bApply)
{
	if (!CanStoreItems() || !CanHoldItem(Item) || Count <= 0)
	{
		return 0;
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
		const int32 Id = bApply ? CreateEntry(Item, Added, Cell, bRotated, MagazineAmmo, true) : INDEX_NONE;
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

int32 UNoFearItemContainerComponent::PlaceItemAt(UNoFearItemData* Item, int32 Count, FIntPoint Cell, bool bRotated, int32 MagazineAmmo)
{
	if (!CanStoreItems() || !Item || Count <= 0 || !CanPlaceAt(Item, Cell, bRotated))
	{
		return 0;
	}
	const int32 Id = CreateEntry(Item, FMath::Min(Count, Item->GetMaxStack()), Cell, bRotated, MagazineAmmo, true);
	NotifyChanged();
	return Id;
}

int32 UNoFearItemContainerComponent::CreateEntry(UNoFearItemData* Item, int32 Count, FIntPoint Cell, bool bRotated, int32 MagazineAmmo, bool bIncoming)
{
	FNoFearInventoryEntry Entry;
	Entry.Id = NextId++;
	Entry.Item = Item;
	Entry.Count = Count;
	Entry.Cell = Cell;
	Entry.bRotated = bRotated;
	Entry.MagazineAmmo = Item->Kind == ENoFearItemKind::Weapon ? MagazineAmmo : -1;
	const int32 Index = Entries.Add(Entry);
	HandleEntryAdded(Index, bIncoming);
	return Entry.Id;
}

void UNoFearItemContainerComponent::ReleaseEntryAt(int32 Index)
{
	if (!Entries.IsValidIndex(Index))
	{
		return;
	}
	const FNoFearInventoryEntry Entry = Entries[Index];
	Entries.RemoveAt(Index);
	HandleEntryRemoved(Entry);
}

bool UNoFearItemContainerComponent::MoveEntry(int32 Id, FIntPoint Cell, bool bRotated)
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

int32 UNoFearItemContainerComponent::SplitStack(int32 Id, int32 Count)
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

int32 UNoFearItemContainerComponent::AddToEntry(int32 Id, int32 Count)
{
	const int32 Index = FindEntryIndex(Id);
	if (Index == INDEX_NONE || Count <= 0 || !Entries[Index].Item)
	{
		return 0;
	}
	const int32 Added = FMath::Min(Count, Entries[Index].Item->GetMaxStack() - Entries[Index].Count);
	if (Added <= 0)
	{
		return 0;
	}
	Entries[Index].Count += Added;
	NotifyChanged();
	return Added;
}

int32 UNoFearItemContainerComponent::TakeFromEntry(int32 Id, int32 Count)
{
	const int32 Index = FindEntryIndex(Id);
	if (Index == INDEX_NONE || Count <= 0)
	{
		return 0;
	}
	const int32 Taken = FMath::Min(Count, Entries[Index].Count);
	Entries[Index].Count -= Taken;
	if (Entries[Index].Count <= 0)
	{
		ReleaseEntryAt(Index);
	}
	NotifyChanged();
	return Taken;
}

bool UNoFearItemContainerComponent::RemoveEntry(int32 Id)
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

int32 UNoFearItemContainerComponent::RemoveItem(const UNoFearItemData* Item, int32 Count)
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

void UNoFearItemContainerComponent::ClearItems()
{
	while (Entries.Num() > 0)
	{
		ReleaseEntryAt(Entries.Num() - 1);
	}
	NotifyChanged();
}

void UNoFearItemContainerComponent::NotifyChanged()
{
	OnInventoryChanged.Broadcast(this);
}

#undef LOCTEXT_NAMESPACE
