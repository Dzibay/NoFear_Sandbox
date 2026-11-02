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
		return Item && (Item->Kind == ENoFearItemKind::Weapon || Item->Kind == ENoFearItemKind::Grenade || Item->Kind == ENoFearItemKind::Medicine
			|| Item->Kind == ENoFearItemKind::Placeable);
	}
}

UNoFearInventoryComponent::UNoFearInventoryComponent()
{
	// Тикает только у героя игрока: курсор экрана кейса и контекст ввода с I.
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;

	DisplayName = LOCTEXT("CaseName", "Кейс");
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

bool UNoFearInventoryComponent::CanHoldItem(const UNoFearItemData* Item) const
{
	// Расширение не лежит в клетках кейса — оно его увеличивает (AddInternal).
	return Super::CanHoldItem(Item) && Item->Kind != ENoFearItemKind::CaseExpansion;
}

// ---------------------------------------------------------------------------
// Что лежит
// ---------------------------------------------------------------------------

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

// ---------------------------------------------------------------------------
// Изменения
// ---------------------------------------------------------------------------

int32 UNoFearInventoryComponent::AddInternal(UNoFearItemData* Item, int32 Count, int32 MagazineAmmo, bool bApply)
{
	if (bCaseActive && Item && Count > 0 && Item->Kind == ENoFearItemKind::CaseExpansion)
	{
		const int32 Steps = FMath::Max(FMath::Min(Count, GetMaxExpansionLevel() - ExpansionLevel), 0);
		for (int32 Step = 0; bApply && Step < Steps; ++Step)
		{
			AddExpansion();
		}
		return Steps;
	}
	return Super::AddInternal(Item, Count, MagazineAmmo, bApply);
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

void UNoFearInventoryComponent::HandleEntryAdded(int32 Index, bool bIncoming)
{
	UNoFearItemData* Item = Entries[Index].Item;
	if (Item->Kind == ENoFearItemKind::Weapon)
	{
		UNoFearWeaponComponent* Weapons = GetWeaponComponent();
		if (Weapons && Item->WeaponData)
		{
			ANoFearWeapon* Weapon = Weapons->AddWeaponInstance(Item->WeaponData, Entries[Index].MagazineAmmo);
			Entries[Index].Weapon = Weapon;
			if (Weapon)
			{
				Weapon->SetAmmoSource(this, Item->AmmoItem);
			}
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("NoFearInventory: у %s нет WeaponData — ствол в руки не возьмётся"), *Item->GetName());
		}
	}
	if (bIncoming && bAutoAssignWheel)
	{
		AutoAssignWheel(Entries[Index]);
	}
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

void UNoFearInventoryComponent::HandleEntryRemoved(const FNoFearInventoryEntry& Entry)
{
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
	const int32 Magazine = Entry.GetMagazineAmmo();
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
	Super::NotifyChanged();
}

// ---------------------------------------------------------------------------
// Предметы в мир
// ---------------------------------------------------------------------------

AActor* UNoFearInventoryComponent::PlaceItem(UNoFearItemData* Item)
{
	APawn* Pawn = Cast<APawn>(GetOwner());
	UWorld* World = GetWorld();
	if (!Pawn || !World || !Item || Item->Kind != ENoFearItemKind::Placeable || bScreenOpen || CountItem(Item) <= 0)
	{
		return nullptr;
	}
	UClass* Class = Item->PlaceClass.LoadSynchronous();
	if (!Class)
	{
		UE_LOG(LogTemp, Warning, TEXT("NoFearInventory: у %s нет PlaceClass — ставить нечего"), *Item->GetName());
		return nullptr;
	}

	// Место — PlaceDistance перед героем: путь туда не перекрыт стеной, внизу пол, а не обрыв или крутой склон.
	const FRotator Facing(0.f, Pawn->GetControlRotation().Yaw, 0.f);
	const FVector Center = Pawn->GetActorLocation();
	const FVector Spot = Center + Facing.Vector() * PlaceDistance;
	const float FeetDepth = Pawn->GetSimpleCollisionHalfHeight();
	FCollisionQueryParams Params(SCENE_QUERY_STAT(NoFearPlaceItem), false, Pawn);
	TArray<AActor*> Attached;
	Pawn->GetAttachedActors(Attached, true, true);
	Params.AddIgnoredActors(Attached);
	FCollisionObjectQueryParams Solid;
	Solid.AddObjectTypesToQuery(ECC_WorldStatic);
	Solid.AddObjectTypesToQuery(ECC_WorldDynamic);
	FHitResult Floor;
	const bool bBlocked = World->LineTraceTestByObjectType(Center, Spot, Solid, Params);
	const bool bFloor = !bBlocked && World->LineTraceSingleByObjectType(Floor, Spot, Spot - FVector(0.f, 0.f, FeetDepth + 60.f), Solid, Params)
		&& Floor.ImpactNormal.Z > 0.7f;
	if (!bFloor)
	{
		PushNotice(LOCTEXT("PlaceNoRoom", "Здесь не поставить"));
		return nullptr;
	}

	const FTransform Transform(Facing, Floor.ImpactPoint);
	AActor* Placed = World->SpawnActorDeferred<AActor>(Class, Transform, Pawn, Pawn, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (Placed)
	{
		Placed->FinishSpawning(Transform);
	}
	// Актор сам проверяет место в BeginPlay и уничтожает себя, если оно не годится: тогда предмет остаётся в кейсе.
	if (!IsValid(Placed) || Placed->IsActorBeingDestroyed())
	{
		PushNotice(LOCTEXT("PlaceNoRoom", "Здесь не поставить"));
		return nullptr;
	}
	RemoveItem(Item, 1);
	PushNotice(FText::Format(LOCTEXT("Placed", "Поставлено: {0}"), Item->GetDisplayName()));
	return Placed;
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

	void PlaceCommand(const TArray<FString>& Args, UWorld* World)
	{
		UNoFearInventoryComponent* Case = GetPlayerCase(World);
		UNoFearItemData* Item = Case && Args.Num() > 0 ? LoadItem(Args[0]) : nullptr;
		if (Item)
		{
			const AActor* Placed = Case->PlaceItem(Item);
			UE_LOG(LogTemp, Log, TEXT("NoFear.Inv.Place: %s -> %s"), *Item->GetName(), Placed ? *Placed->GetName() : TEXT("не поставлено"));
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

	FAutoConsoleCommandWithWorldAndArgs InvPlaceCommand(
		TEXT("NoFear.Inv.Place"),
		TEXT("Поставить предмет из кейса перед героем, как с колеса: NoFear.Inv.Place BearTrap."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&PlaceCommand));

	FAutoConsoleCommandWithWorldAndArgs InvListCommand(
		TEXT("NoFear.Inv.List"),
		TEXT("Вывести в лог, что лежит в кейсе и в секторах колеса."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&ListCommand));
}

#undef LOCTEXT_NAMESPACE
