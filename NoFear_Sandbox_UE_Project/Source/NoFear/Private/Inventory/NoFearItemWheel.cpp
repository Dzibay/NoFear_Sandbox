// Колесо предметов (Q): пока Q зажата, время замедлено, мышь выбирает один из восьми секторов колеса кейса,
// отпущенная Q применяет его — ствол в руки, граната на G, медицина лечит (NoFearHealing.cpp), ловушка на пол. Мышь в центре — отмена.
// Здесь же колёсико мыши, листающее стволы колеса, и выбор гранаты, которую бросает G.
// Секторы колеса и сетка — в NoFearInventoryComponent.cpp, ввод Q — в NoFearInventoryScreen.cpp, рисует ANoFearHUD::DrawItemWheel.

#include "Inventory/NoFearInventoryComponent.h"

#include "Core/NoFearHealthComponent.h"
#include "Core/NoFearPlayerController.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Inventory/NoFearItemData.h"
#include "Kismet/GameplayStatics.h"
#include "Weapons/NoFearGrenadeComponent.h"
#include "Weapons/NoFearWeapon.h"
#include "Weapons/NoFearWeaponComponent.h"

#define LOCTEXT_NAMESPACE "NoFearItemWheel"

bool UNoFearInventoryComponent::OpenItemWheel()
{
	if (bItemWheelOpen)
	{
		return true;
	}
	APlayerController* PC = GetOwnerPlayerController();
	if (!bCaseActive || !PC || bScreenOpen || UNoFearHealthComponent::IsActorIncapacitated(GetOwner()))
	{
		return false;
	}
	if (const ANoFearPlayerController* NoFearPC = Cast<ANoFearPlayerController>(PC); NoFearPC && NoFearPC->IsOrderWheelOpen())
	{
		return false;
	}

	// Колесо обрывает лечение, предмет остаётся.
	CancelHealing(true);
	bItemWheelOpen = true;
	WheelController = PC;
	ItemWheelCursor = FVector2D::ZeroVector;
	ItemWheelSelection = INDEX_NONE;
	TimeDilationBeforeWheel = UGameplayStatics::GetGlobalTimeDilation(this);
	UGameplayStatics::SetGlobalTimeDilation(this, WheelTimeDilation);

	// Мышь выбирает сектор, а не крутит камеру; очередь, прицел и замах гранатой обрываются.
	PC->SetIgnoreLookInput(true);
	if (UNoFearWeaponComponent* Weapons = GetWeaponComponent())
	{
		Weapons->StopFire();
		Weapons->SetAiming(false);
	}
	if (UNoFearGrenadeComponent* Grenades = GetOwner()->FindComponentByClass<UNoFearGrenadeComponent>(); Grenades && Grenades->IsAiming())
	{
		Grenades->CancelAim();
	}
	EnsureCaseInput(PC);
	return true;
}

void UNoFearInventoryComponent::CloseItemWheel(bool bUse)
{
	if (!bItemWheelOpen)
	{
		return;
	}
	const int32 Selection = ItemWheelSelection;
	bItemWheelOpen = false;
	ItemWheelSelection = INDEX_NONE;
	ItemWheelCursor = FVector2D::ZeroVector;
	UGameplayStatics::SetGlobalTimeDilation(this, TimeDilationBeforeWheel);

	if (APlayerController* PC = WheelController.Get())
	{
		PC->SetIgnoreLookInput(false);
		RemoveItemWheelInput(PC);
	}
	WheelController.Reset();

	if (bUse && Selection != INDEX_NONE)
	{
		UseWheelSlot(Selection);
	}
}

void UNoFearInventoryComponent::UpdateItemWheel(APlayerController* PC)
{
	const ANoFearPlayerController* NoFearPC = Cast<ANoFearPlayerController>(PC);
	if (!PC || PC != WheelController.Get() || bScreenOpen || UNoFearHealthComponent::IsActorIncapacitated(GetOwner())
		|| (NoFearPC && NoFearPC->IsOrderWheelOpen()))
	{
		CloseItemWheel(false);
		return;
	}

	float DeltaX = 0.f;
	float DeltaY = 0.f;
	PC->GetInputMouseDelta(DeltaX, DeltaY);
	FVector2D Cursor = ItemWheelCursor + FVector2D(DeltaX, DeltaY);
	float StickX = 0.f;
	float StickY = 0.f;
	PC->GetInputAnalogStickState(EControllerAnalogStick::CAS_RightStick, StickX, StickY);
	if (FVector2D(StickX, StickY).Size() > 0.5f)
	{
		Cursor = FVector2D(StickX, StickY).GetSafeNormal() * WheelDeadZone * 3.f;
	}
	SetItemWheelCursor(Cursor);
}

void UNoFearInventoryComponent::SetItemWheelCursor(FVector2D Cursor)
{
	const float MaxLength = WheelDeadZone * 3.f;
	ItemWheelCursor = Cursor.Size() > MaxLength ? Cursor.GetSafeNormal() * MaxLength : Cursor;
	if (ItemWheelCursor.Size() >= WheelDeadZone)
	{
		// 0° — вверх, по часовой стрелке, как у колеса приказов; сектор 0 — верхний.
		const float Angle = FMath::RadiansToDegrees(FMath::Atan2(ItemWheelCursor.X, ItemWheelCursor.Y));
		ItemWheelSelection = ((FMath::RoundToInt(Angle / (360.f / WheelSlotCount)) % WheelSlotCount) + WheelSlotCount) % WheelSlotCount;
	}
	else
	{
		ItemWheelSelection = INDEX_NONE;
	}
}

FVector2D UNoFearInventoryComponent::GetItemWheelCursor() const
{
	return ItemWheelCursor.Size() >= WheelDeadZone ? ItemWheelCursor.GetSafeNormal() : FVector2D::ZeroVector;
}

bool UNoFearInventoryComponent::UseWheelSlot(int32 Slot)
{
	const FNoFearInventoryEntry* Entry = bCaseActive ? FindEntry(GetWheelSlot(Slot)) : nullptr;
	UNoFearItemData* Item = Entry ? Entry->Item.Get() : nullptr;
	if (!Item || UNoFearHealthComponent::IsActorIncapacitated(GetOwner()))
	{
		return false;
	}

	switch (Item->Kind)
	{
	case ENoFearItemKind::Weapon:
	{
		UNoFearWeaponComponent* Weapons = GetWeaponComponent();
		const int32 Index = Weapons ? Weapons->GetWeapons().IndexOfByKey(Entry->Weapon.Get()) : INDEX_NONE;
		if (Index == INDEX_NONE)
		{
			return false;
		}
		if (Weapons->GetTargetWeaponIndex() != Index)
		{
			Weapons->EquipWeapon(Index);
		}
		return true;
	}
	case ENoFearItemKind::Grenade:
	{
		UNoFearGrenadeComponent* Grenades = GetOwner()->FindComponentByClass<UNoFearGrenadeComponent>();
		if (!Grenades)
		{
			return false;
		}
		Grenades->SetGrenadeItem(Item);
		PushNotice(FText::Format(LOCTEXT("GrenadeChosen", "Граната: {0} (G)"), Item->DisplayName));
		return true;
	}
	case ENoFearItemKind::Medicine:
		return StartHealing(Item);
	case ENoFearItemKind::Placeable:
		return PlaceItem(Item) != nullptr;
	default:
		return false;
	}
}

bool UNoFearInventoryComponent::CycleWheelWeapon(int32 Direction)
{
	UNoFearWeaponComponent* Weapons = GetWeaponComponent();
	if (!bCaseActive || !Weapons || Direction == 0 || UNoFearHealthComponent::IsActorIncapacitated(GetOwner()))
	{
		return false;
	}

	// Стволы колеса по порядку секторов: индексы у UNoFearWeaponComponent.
	const TArray<ANoFearWeapon*> Owned = Weapons->GetWeapons();
	TArray<int32> Order;
	for (int32 Slot = 0; Slot < WheelSlotCount; ++Slot)
	{
		const FNoFearInventoryEntry* Entry = FindEntry(GetWheelSlot(Slot));
		if (Entry && Entry->Item && Entry->Item->Kind == ENoFearItemKind::Weapon)
		{
			if (const int32 Index = Owned.IndexOfByKey(Entry->Weapon.Get()); Index != INDEX_NONE)
			{
				Order.AddUnique(Index);
			}
		}
	}
	if (Order.Num() == 0)
	{
		return false;
	}

	const int32 Current = Order.IndexOfByKey(Weapons->GetTargetWeaponIndex());
	int32 Next;
	if (Current == INDEX_NONE)
	{
		Next = Direction > 0 ? 0 : Order.Num() - 1;
	}
	else
	{
		Next = (Current + (Direction > 0 ? 1 : -1) + Order.Num()) % Order.Num();
	}
	if (Order[Next] == Weapons->GetTargetWeaponIndex())
	{
		return false;
	}
	Weapons->EquipWeapon(Order[Next]);
	return true;
}

void UNoFearInventoryComponent::RefreshGrenadeChoice()
{
	UNoFearGrenadeComponent* Grenades = bCaseActive ? GetOwner()->FindComponentByClass<UNoFearGrenadeComponent>() : nullptr;
	if (!Grenades || CountItem(Grenades->GetGrenadeItem()) > 0)
	{
		return;
	}
	// Выбранные кончились: сначала гранаты из секторов колеса по порядку, потом любые из кейса.
	UNoFearItemData* Next = nullptr;
	for (int32 Slot = 0; Slot < WheelSlotCount && !Next; ++Slot)
	{
		const FNoFearInventoryEntry* Entry = FindEntry(GetWheelSlot(Slot));
		if (Entry && Entry->Item && Entry->Item->Kind == ENoFearItemKind::Grenade)
		{
			Next = Entry->Item;
		}
	}
	for (int32 i = 0; i < Entries.Num() && !Next; ++i)
	{
		if (Entries[i].Item && Entries[i].Item->Kind == ENoFearItemKind::Grenade)
		{
			Next = Entries[i].Item;
		}
	}
	if (Next)
	{
		Grenades->SetGrenadeItem(Next);
	}
}

// ---------------------------------------------------------------------------
// Консоль
// ---------------------------------------------------------------------------

namespace
{
	FAutoConsoleCommandWithWorldAndArgs ItemWheelCommand(
		TEXT("NoFear.Inv.Wheel"),
		TEXT("Применить сектор колеса предметов героя, как отпущенная на нём Q: NoFear.Inv.Wheel <1-8>."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			UNoFearInventoryComponent* Case = UNoFearInventoryComponent::FindCase(UGameplayStatics::GetPlayerPawn(World, 0));
			if (!Case || Args.Num() == 0)
			{
				UE_LOG(LogTemp, Warning, TEXT("NoFear.Inv.Wheel: нужен номер сектора 1-8 и герой с кейсом"));
				return;
			}
			const int32 Slot = FCString::Atoi(*Args[0]) - 1;
			const bool bUsed = Case->UseWheelSlot(Slot);
			UE_LOG(LogTemp, Display, TEXT("NoFear.Inv.Wheel %d: %s"), Slot + 1, bUsed ? TEXT("применён") : TEXT("ничего"));
		}));
}

#undef LOCTEXT_NAMESPACE
