// Экран кейса (I): курсор, перетаскивание и поворот предметов, меню ПКМ, назначение в колесо, выброс за край
// и предмет с земли на курсоре, когда в кейсе нет места. Данные кейса — в NoFearInventoryComponent.cpp.

#include "Inventory/NoFearInventoryComponent.h"

#include "Core/NoFearHealthComponent.h"
#include "Core/NoFearPlayerController.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "InputAction.h"
#include "InputKeyEventArgs.h"
#include "InputMappingContext.h"
#include "Inventory/NoFearItemData.h"
#include "Inventory/NoFearPickup.h"
#include "Kismet/GameplayStatics.h"
#include "Weapons/NoFearGrenadeComponent.h"
#include "Weapons/NoFearWeapon.h"
#include "Weapons/NoFearWeaponComponent.h"

#define LOCTEXT_NAMESPACE "NoFearInventoryScreen"

namespace
{
	constexpr int32 CaseMappingPriority = 3;
	/** Выше всех контекстов героя: клавиши, которые экран забирает себе, до них не доходят. */
	constexpr int32 ScreenMappingPriority = 100;
	/** Отпустили ЛКМ дальше этого от места нажатия (пикселей при 1080p) — перетащили. */
	constexpr float DragThreshold = 8.f;

	/**
	 * Действия и контексты экрана — одни на всю игру. Свои у каждого тела оставались бы в подсистеме после
	 * смерти тела и перехватывали бы I у нового.
	 */
	struct FCaseInput
	{
		UInputAction* Toggle = nullptr;
		UInputAction* Click = nullptr;
		UInputAction* Menu = nullptr;
		UInputAction* Rotate = nullptr;
		UInputAction* Back = nullptr;
		UInputAction* Slots[UNoFearInventoryComponent::WheelSlotCount] = {};
		/** Заглушки: клавиши героя, которые на экране ничего не должны делать. */
		UInputAction* Block = nullptr;
		UInputAction* BlockLook = nullptr;
		/** I — пока кейс работает. */
		UInputMappingContext* CaseMapping = nullptr;
		/** Пока экран открыт. */
		UInputMappingContext* ScreenMapping = nullptr;
	};

	UInputAction* MakeAction(const TCHAR* Name, EInputActionValueType Type)
	{
		UPackage* Package = GetTransientPackage();
		UInputAction* Action = NewObject<UInputAction>(Package, MakeUniqueObjectName(Package, UInputAction::StaticClass(), FName(Name)));
		Action->ValueType = Type;
		Action->AddToRoot();
		return Action;
	}

	UInputMappingContext* MakeMapping(const TCHAR* Name)
	{
		UPackage* Package = GetTransientPackage();
		UInputMappingContext* Mapping = NewObject<UInputMappingContext>(Package, MakeUniqueObjectName(Package, UInputMappingContext::StaticClass(), FName(Name)));
		Mapping->AddToRoot();
		return Mapping;
	}

	const FCaseInput& GetCaseInput()
	{
		static FCaseInput Input;
		if (Input.CaseMapping)
		{
			return Input;
		}

		Input.Toggle = MakeAction(TEXT("IA_NoFearCaseScreen"), EInputActionValueType::Boolean);
		Input.Click = MakeAction(TEXT("IA_NoFearCaseClick"), EInputActionValueType::Boolean);
		Input.Menu = MakeAction(TEXT("IA_NoFearCaseMenu"), EInputActionValueType::Boolean);
		Input.Rotate = MakeAction(TEXT("IA_NoFearCaseRotate"), EInputActionValueType::Boolean);
		Input.Back = MakeAction(TEXT("IA_NoFearCaseBack"), EInputActionValueType::Boolean);
		for (int32 Slot = 0; Slot < UNoFearInventoryComponent::WheelSlotCount; ++Slot)
		{
			Input.Slots[Slot] = MakeAction(*FString::Printf(TEXT("IA_NoFearCaseSlot%d"), Slot + 1), EInputActionValueType::Boolean);
		}
		Input.Block = MakeAction(TEXT("IA_NoFearCaseBlock"), EInputActionValueType::Axis1D);
		Input.BlockLook = MakeAction(TEXT("IA_NoFearCaseBlockLook"), EInputActionValueType::Axis2D);

		Input.CaseMapping = MakeMapping(TEXT("IMC_NoFearCase"));
		Input.CaseMapping->MapKey(Input.Toggle, EKeys::I);

		UInputMappingContext* Screen = MakeMapping(TEXT("IMC_NoFearCaseScreen"));
		Screen->MapKey(Input.Click, EKeys::LeftMouseButton);
		Screen->MapKey(Input.Menu, EKeys::RightMouseButton);
		Screen->MapKey(Input.Rotate, EKeys::R);
		Screen->MapKey(Input.Back, EKeys::Escape);
		const FKey SlotKeys[] = { EKeys::One, EKeys::Two, EKeys::Three, EKeys::Four, EKeys::Five, EKeys::Six, EKeys::Seven, EKeys::Eight };
		for (int32 Slot = 0; Slot < UNoFearInventoryComponent::WheelSlotCount; ++Slot)
		{
			Screen->MapKey(Input.Slots[Slot], SlotKeys[Slot]);
		}
		// Граната, кобура и рэгдолл, смена ствола колёсиком, колесо приказов, стабилизация, E, средняя кнопка, N.
		const FKey Blocked[] = {
			EKeys::G, EKeys::X, EKeys::MouseWheelAxis, EKeys::MouseScrollUp, EKeys::MouseScrollDown,
			EKeys::Tab, EKeys::Q, EKeys::E, EKeys::MiddleMouseButton, EKeys::N,
		};
		for (const FKey& Key : Blocked)
		{
			Screen->MapKey(Input.Block, Key);
		}
		Screen->MapKey(Input.BlockLook, EKeys::Mouse2D);
		Input.ScreenMapping = Screen;
		return Input;
	}

	UEnhancedInputLocalPlayerSubsystem* GetInputSubsystem(const APlayerController* PC)
	{
		return PC ? ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PC->GetLocalPlayer()) : nullptr;
	}

	/** Курсор берётся за середину предмета, а у чётных сторон — за клетку правее и ниже середины. */
	FVector2D GetCenterGrab(FIntPoint Size)
	{
		return FVector2D(Size.X / 2 + 0.5f, Size.Y / 2 + 0.5f);
	}

	FText GetNameWithCount(const UNoFearItemData* Item, int32 Count)
	{
		return Count > 1 ? FText::Format(LOCTEXT("NameCount", "{0} ×{1}"), Item->GetDisplayName(), FText::AsNumber(Count)) : Item->GetDisplayName();
	}

	/** Забрать Count штук с лежащего предмета: кончились — он исчезает. */
	void TakeFromPickup(ANoFearPickup* Pickup, int32 Count)
	{
		if (!Pickup)
		{
			return;
		}
		const int32 Left = Pickup->GetCount() - Count;
		if (Left <= 0)
		{
			Pickup->Destroy();
		}
		else
		{
			Pickup->SetItem(Pickup->Item, Left, Pickup->MagazineAmmo);
		}
	}
}

// ---------------------------------------------------------------------------
// Разметка
// ---------------------------------------------------------------------------

FNoFearCaseLayout FNoFearCaseLayout::Make(const FVector2D& InViewportSize, FIntPoint InGridSize)
{
	FNoFearCaseLayout Layout;
	Layout.ViewportSize = InViewportSize.X > 1.f && InViewportSize.Y > 1.f ? InViewportSize : FVector2D(1920.f, 1080.f);
	Layout.GridSize = FIntPoint(FMath::Max(InGridSize.X, 1), FMath::Max(InGridSize.Y, 1));
	// Как GetUIScale у HUD: всё в пикселях 1080p.
	const float S = Layout.ViewportSize.Y / 1080.f;
	Layout.Scale = S;
	Layout.Cell = 56.f * S;
	Layout.WheelRadius = 150.f * S;
	Layout.SlotSize = 84.f * S;
	Layout.MenuWidth = 240.f * S;
	Layout.MenuItemHeight = 34.f * S;

	const float Gap = 72.f * S;
	const FVector2D GridExtent = FVector2D(Layout.GridSize) * Layout.Cell;
	const float WheelSpan = 2.f * Layout.WheelRadius + Layout.SlotSize;
	const float TotalWidth = GridExtent.X + Gap + WheelSpan;
	const float Left = (Layout.ViewportSize.X - TotalWidth) * 0.5f;
	const float CenterY = Layout.ViewportSize.Y * 0.5f;
	Layout.GridOrigin = FVector2D(Left, CenterY - GridExtent.Y * 0.5f);
	Layout.WheelCenter = FVector2D(Left + GridExtent.X + Gap + WheelSpan * 0.5f, CenterY);

	// Подложка с полями и строкой заголовка сверху.
	const float Pad = 28.f * S;
	const float Title = 36.f * S;
	const float Top = FMath::Min(Layout.GridOrigin.Y, CenterY - WheelSpan * 0.5f);
	const float Bottom = FMath::Max(Layout.GridOrigin.Y + GridExtent.Y, CenterY + WheelSpan * 0.5f);
	Layout.PanelMin = FVector2D(Left - Pad, Top - Pad - Title);
	Layout.PanelMax = FVector2D(Left + TotalWidth + Pad, Bottom + Pad);
	return Layout;
}

bool FNoFearCaseLayout::IsOnGrid(const FVector2D& Point) const
{
	const FVector2D Local = ToGrid(Point);
	return Local.X >= 0.f && Local.Y >= 0.f && Local.X < GridSize.X && Local.Y < GridSize.Y;
}

bool FNoFearCaseLayout::IsInsidePanel(const FVector2D& Point) const
{
	return Point.X >= PanelMin.X && Point.Y >= PanelMin.Y && Point.X <= PanelMax.X && Point.Y <= PanelMax.Y;
}

FVector2D FNoFearCaseLayout::GetSlotCenter(int32 Slot) const
{
	const float Angle = FMath::DegreesToRadians(-90.f + 45.f * Slot);
	return WheelCenter + FVector2D(FMath::Cos(Angle), FMath::Sin(Angle)) * WheelRadius;
}

int32 FNoFearCaseLayout::GetSlotAt(const FVector2D& Point) const
{
	for (int32 Slot = 0; Slot < UNoFearInventoryComponent::WheelSlotCount; ++Slot)
	{
		const FVector2D Offset = Point - GetSlotCenter(Slot);
		if (FMath::Abs(Offset.X) <= SlotSize * 0.5f && FMath::Abs(Offset.Y) <= SlotSize * 0.5f)
		{
			return Slot;
		}
	}
	return INDEX_NONE;
}

FNoFearCaseLayout UNoFearInventoryComponent::GetScreenLayout() const
{
	int32 Width = 0;
	int32 Height = 0;
	const APlayerController* PC = ScreenController.IsValid() ? ScreenController.Get() : GetOwnerPlayerController();
	if (PC)
	{
		PC->GetViewportSize(Width, Height);
	}
	return FNoFearCaseLayout::Make(FVector2D(Width, Height), GetGridSize());
}

FVector2D UNoFearInventoryComponent::GetCellCenter(FIntPoint Cell) const
{
	const FNoFearCaseLayout Layout = GetScreenLayout();
	return Layout.GetCellMin(Cell) + FVector2D(Layout.Cell * 0.5f);
}

FVector2D UNoFearInventoryComponent::GetWheelSlotCenter(int32 Slot) const
{
	return GetScreenLayout().GetSlotCenter(Slot);
}

FVector2D UNoFearInventoryComponent::GetMenuItemCenter(int32 Index) const
{
	const FNoFearCaseLayout Layout = GetScreenLayout();
	return MenuPosition + FVector2D(Layout.MenuWidth * 0.5f, Layout.MenuItemHeight * (Index + 0.5f));
}

FVector2D UNoFearInventoryComponent::GetOutsidePoint() const
{
	const FNoFearCaseLayout Layout = GetScreenLayout();
	const float Y = Layout.ViewportSize.Y * 0.5f;
	// Посередине между краем экрана и подложкой; на узком экране — под подложкой.
	if (Layout.PanelMin.X > 16.f * Layout.Scale)
	{
		return FVector2D(Layout.PanelMin.X * 0.5f, Y);
	}
	return FVector2D(Layout.ViewportSize.X * 0.5f, (Layout.PanelMax.Y + Layout.ViewportSize.Y) * 0.5f);
}

// ---------------------------------------------------------------------------
// Ввод, открытие и закрытие
// ---------------------------------------------------------------------------

void UNoFearInventoryComponent::SetupPlayerInput(UInputComponent* PlayerInputComponent)
{
	UEnhancedInputComponent* Input = Cast<UEnhancedInputComponent>(PlayerInputComponent);
	if (!Input)
	{
		return;
	}
	const FCaseInput& Actions = GetCaseInput();
	Input->BindAction(Actions.Toggle, ETriggerEvent::Started, this, &UNoFearInventoryComponent::ToggleScreen);
	Input->BindAction(Actions.Click, ETriggerEvent::Started, this, &UNoFearInventoryComponent::ScreenPress);
	Input->BindAction(Actions.Click, ETriggerEvent::Completed, this, &UNoFearInventoryComponent::ScreenRelease);
	Input->BindAction(Actions.Click, ETriggerEvent::Canceled, this, &UNoFearInventoryComponent::ScreenRelease);
	Input->BindAction(Actions.Menu, ETriggerEvent::Started, this, &UNoFearInventoryComponent::ScreenContext);
	Input->BindAction(Actions.Rotate, ETriggerEvent::Started, this, &UNoFearInventoryComponent::ScreenRotate);
	Input->BindAction(Actions.Back, ETriggerEvent::Started, this, &UNoFearInventoryComponent::ScreenBack);
	const TWeakObjectPtr<UNoFearInventoryComponent> WeakThis(this);
	for (int32 Slot = 0; Slot < WheelSlotCount; ++Slot)
	{
		Input->BindActionInstanceLambda(Actions.Slots[Slot], ETriggerEvent::Started, [WeakThis, Slot](const FInputActionInstance&)
		{
			if (UNoFearInventoryComponent* Case = WeakThis.Get())
			{
				Case->ScreenSlotKey(Slot);
			}
		});
	}
}

APlayerController* UNoFearInventoryComponent::GetOwnerPlayerController() const
{
	const APawn* Pawn = Cast<APawn>(GetOwner());
	APlayerController* PC = Pawn ? Cast<APlayerController>(Pawn->GetController()) : nullptr;
	return PC && PC->IsLocalController() ? PC : nullptr;
}

void UNoFearInventoryComponent::EnsureCaseInput(APlayerController* PC) const
{
	UEnhancedInputLocalPlayerSubsystem* Input = GetInputSubsystem(PC);
	if (!Input)
	{
		return;
	}
	const FCaseInput& Actions = GetCaseInput();
	if (!Input->HasMappingContext(Actions.CaseMapping))
	{
		Input->AddMappingContext(Actions.CaseMapping, CaseMappingPriority);
	}
	if (bScreenOpen && !Input->HasMappingContext(Actions.ScreenMapping))
	{
		Input->AddMappingContext(Actions.ScreenMapping, ScreenMappingPriority);
	}
}

bool UNoFearInventoryComponent::OpenScreen()
{
	if (bScreenOpen)
	{
		return true;
	}
	APlayerController* PC = GetOwnerPlayerController();
	if (!bCaseActive || !PC || UNoFearHealthComponent::IsActorIncapacitated(GetOwner()))
	{
		return false;
	}
	if (const ANoFearPlayerController* NoFearPC = Cast<ANoFearPlayerController>(PC); NoFearPC && NoFearPC->IsOrderWheelOpen())
	{
		return false;
	}

	bScreenOpen = true;
	ScreenController = PC;
	TimeDilationBeforeScreen = UGameplayStatics::GetGlobalTimeDilation(this);
	UGameplayStatics::SetGlobalTimeDilation(this, ScreenTimeDilation);

	// Очередь, прицел и замах гранатой обрываются: руки заняты кейсом.
	if (UNoFearWeaponComponent* Weapons = GetWeaponComponent())
	{
		Weapons->StopFire();
		Weapons->SetAiming(false);
	}
	if (UNoFearGrenadeComponent* Grenades = GetOwner()->FindComponentByClass<UNoFearGrenadeComponent>(); Grenades && Grenades->IsAiming())
	{
		Grenades->CancelAim();
	}

	// Курсор виден и не прячется при нажатии; камера стоит, ходить можно.
	bCursorShownBeforeScreen = PC->ShouldShowMouseCursor();
	PC->SetShowMouseCursor(true);
	FInputModeGameAndUI Mode;
	Mode.SetHideCursorDuringCapture(false);
	Mode.SetLockMouseToViewportBehavior(EMouseLockMode::LockAlways);
	PC->SetInputMode(Mode);
	PC->SetIgnoreLookInput(true);
	EnsureCaseInput(PC);

	const FNoFearCaseLayout Layout = GetScreenLayout();
	ScreenCursor = Layout.GridOrigin + FVector2D(Layout.GridSize) * Layout.Cell * 0.5f;
	PC->SetMouseLocation(FMath::RoundToInt(ScreenCursor.X), FMath::RoundToInt(ScreenCursor.Y));
	LastMousePosition = ScreenCursor;
	PressCursor = ScreenCursor;
	bPressConsumed = false;
	CloseMenu();
	return true;
}

void UNoFearInventoryComponent::CloseScreen()
{
	if (!bScreenOpen)
	{
		return;
	}
	ReturnHeld();
	CloseMenu();
	bScreenOpen = false;
	bPressConsumed = false;
	UGameplayStatics::SetGlobalTimeDilation(this, TimeDilationBeforeScreen);

	if (APlayerController* PC = ScreenController.Get())
	{
		PC->SetShowMouseCursor(bCursorShownBeforeScreen);
		PC->SetInputMode(FInputModeGameOnly());
		PC->SetIgnoreLookInput(false);
		if (UEnhancedInputLocalPlayerSubsystem* Input = GetInputSubsystem(PC))
		{
			Input->RemoveMappingContext(GetCaseInput().ScreenMapping);
		}
	}
	ScreenController.Reset();
}

void UNoFearInventoryComponent::ToggleScreen()
{
	if (bScreenOpen)
	{
		CloseScreen();
	}
	else
	{
		OpenScreen();
	}
}

bool UNoFearInventoryComponent::HoldPickup(ANoFearPickup* Pickup)
{
	// Расширение в сетку не кладётся — ему экран не поможет.
	if (!Pickup || !Pickup->Item || Pickup->Item->Kind == ENoFearItemKind::CaseExpansion || !OpenScreen())
	{
		return false;
	}
	ReturnHeld();
	CloseMenu();
	HeldSource = EHeldSource::Pickup;
	HeldPickup = Pickup;
	HeldItem = Pickup->Item;
	HeldCount = FMath::Min(Pickup->GetCount(), Pickup->Item->GetMaxStack());
	HeldMagazine = Pickup->MagazineAmmo;
	bHeldRotated = false;
	HeldGrab = GetCenterGrab(Pickup->Item->GetGridSize());
	PushNotice(LOCTEXT("HoldPickup", "Нет места: освободите место или выбросьте лишнее"));
	return true;
}

void UNoFearInventoryComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	CloseScreen();
	Super::EndPlay(EndPlayReason);
}

void UNoFearInventoryComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	APlayerController* PC = GetOwnerPlayerController();
	if (bScreenOpen)
	{
		// Упал, сменился контроллер, открылось колесо приказов — экран закрывается.
		const ANoFearPlayerController* NoFearPC = Cast<ANoFearPlayerController>(PC);
		if (!PC || PC != ScreenController.Get() || UNoFearHealthComponent::IsActorIncapacitated(GetOwner()) || (NoFearPC && NoFearPC->IsOrderWheelOpen()))
		{
			CloseScreen();
		}
		else
		{
			UpdateScreenCursor(PC);

			if (HeldSource == EHeldSource::Entry && FindEntryIndex(HeldEntryId) == INDEX_NONE)
			{
				ClearHeld();
			}
			if (HeldSource == EHeldSource::Pickup)
			{
				const ANoFearPickup* Pickup = HeldPickup.Get();
				if (!Pickup || FVector::Dist(Pickup->GetActorLocation(), GetOwner()->GetActorLocation()) > PickupHoldDistance)
				{
					ClearHeld();
					PushNotice(LOCTEXT("PickupLeft", "Предмет остался лежать"));
				}
			}
			if (bMenuOpen && FindEntryIndex(MenuEntryId) == INDEX_NONE)
			{
				CloseMenu();
			}
		}
	}
	EnsureCaseInput(PC);
}

void UNoFearInventoryComponent::UpdateScreenCursor(APlayerController* PC)
{
	float X = 0.f;
	float Y = 0.f;
	if (!PC || !PC->GetMousePosition(X, Y))
	{
		return;
	}
	// Мышь двигает курсор, только когда сдвинулась: проверки ставят курсор сами, а мышь при этом стоит.
	const FVector2D Mouse(X, Y);
	if (FVector2D::DistSquared(Mouse, LastMousePosition) > 0.25f)
	{
		ScreenCursor = Mouse;
	}
	LastMousePosition = Mouse;
}

// ---------------------------------------------------------------------------
// Предмет на курсоре
// ---------------------------------------------------------------------------

int32 UNoFearInventoryComponent::GetHeldCount() const
{
	if (HeldSource == EHeldSource::Entry)
	{
		const int32 Index = FindEntryIndex(HeldEntryId);
		return Index != INDEX_NONE ? Entries[Index].Count : 0;
	}
	return IsHolding() ? HeldCount : 0;
}

FIntPoint UNoFearInventoryComponent::GetHeldSize() const
{
	const FIntPoint Size = HeldItem ? HeldItem->GetGridSize() : FIntPoint(1, 1);
	return bHeldRotated ? FIntPoint(Size.Y, Size.X) : Size;
}

int32 UNoFearInventoryComponent::GetHeldMagazine() const
{
	if (HeldSource == EHeldSource::Entry)
	{
		const int32 Index = FindEntryIndex(HeldEntryId);
		return Index != INDEX_NONE && Entries[Index].Weapon ? Entries[Index].Weapon->GetMagazineAmmo() : -1;
	}
	return HeldSource == EHeldSource::Pickup ? HeldMagazine : -1;
}

FIntPoint UNoFearInventoryComponent::GetHeldOrigin(const FNoFearCaseLayout& Layout) const
{
	const FVector2D Origin = Layout.ToGrid(ScreenCursor) - HeldGrab;
	return FIntPoint(FMath::RoundToInt(Origin.X), FMath::RoundToInt(Origin.Y));
}

FNoFearCaseTarget UNoFearInventoryComponent::GetHeldTarget() const
{
	FNoFearCaseTarget Target;
	if (!IsHolding() || !HeldItem)
	{
		return Target;
	}
	const FNoFearCaseLayout Layout = GetScreenLayout();

	const int32 Slot = Layout.GetSlotAt(ScreenCursor);
	if (Slot != INDEX_NONE)
	{
		// В колесо — только то, что уже в кейсе.
		Target.Kind = ENoFearCaseTarget::WheelSlot;
		Target.Slot = Slot;
		Target.bValid = HeldSource != EHeldSource::Pickup && IsWheelItem(HeldItem);
		return Target;
	}
	if (!Layout.IsInsidePanel(ScreenCursor))
	{
		Target.Kind = ENoFearCaseTarget::Drop;
		Target.bValid = true;
		return Target;
	}

	if (Layout.IsOnGrid(ScreenCursor) && HeldItem->GetMaxStack() > 1)
	{
		const FVector2D Local = Layout.ToGrid(ScreenCursor);
		const int32 UnderId = GetEntryAt(FIntPoint(FMath::FloorToInt(Local.X), FMath::FloorToInt(Local.Y)));
		const int32 UnderIndex = FindEntryIndex(UnderId);
		// Часть стопки можно вернуть в её же стопку; целую стопку — только в другую.
		const bool bSelf = HeldSource == EHeldSource::Entry && UnderId == HeldEntryId;
		if (UnderIndex != INDEX_NONE && !bSelf && Entries[UnderIndex].Item == HeldItem
			&& Entries[UnderIndex].Count < HeldItem->GetMaxStack())
		{
			Target.Kind = ENoFearCaseTarget::Merge;
			Target.MergeId = UnderId;
			Target.Cell = Entries[UnderIndex].Cell;
			Target.bValid = true;
			return Target;
		}
	}

	// Хоть одной клеткой над сеткой — показать, ляжет ли.
	const FIntPoint Origin = GetHeldOrigin(Layout);
	const FIntPoint Size = GetHeldSize();
	const bool bOverGrid = Origin.X < Layout.GridSize.X && Origin.Y < Layout.GridSize.Y && Origin.X + Size.X > 0 && Origin.Y + Size.Y > 0;
	if (bOverGrid)
	{
		Target.Kind = ENoFearCaseTarget::Grid;
		Target.Cell = Origin;
		Target.bValid = CanPlaceAt(HeldItem, Origin, bHeldRotated, HeldSource == EHeldSource::Entry ? HeldEntryId : 0);
	}
	return Target;
}

void UNoFearInventoryComponent::HoldEntry(int32 Id, int32 FromWheelSlot)
{
	const int32 Index = FindEntryIndex(Id);
	if (Index == INDEX_NONE)
	{
		return;
	}
	const FNoFearInventoryEntry& Entry = Entries[Index];
	const FNoFearCaseLayout Layout = GetScreenLayout();
	ClearHeld();
	HeldSource = EHeldSource::Entry;
	HeldEntryId = Id;
	HeldWheelSlot = FromWheelSlot;
	HeldItem = Entry.Item;
	HeldCount = Entry.Count;
	bHeldRotated = Entry.bRotated;
	// Взяли с клетки — курсор там, где взялись; из сектора колеса — за середину.
	const FVector2D Grab = Layout.ToGrid(ScreenCursor) - FVector2D(Entry.Cell);
	const FVector2D Size(Entry.GetSize());
	const bool bOnItem = Grab.X >= 0.f && Grab.Y >= 0.f && Grab.X <= Size.X && Grab.Y <= Size.Y;
	HeldGrab = bOnItem ? Grab : GetCenterGrab(Entry.GetSize());
}

bool UNoFearInventoryComponent::PlaceHeld()
{
	const FNoFearCaseTarget Target = GetHeldTarget();
	if (!Target.bValid || !HeldItem)
	{
		return false;
	}
	UNoFearItemData* Item = HeldItem;
	ANoFearPickup* Pickup = HeldSource == EHeldSource::Pickup ? HeldPickup.Get() : nullptr;

	switch (Target.Kind)
	{
	case ENoFearCaseTarget::Grid:
	{
		if (HeldSource == EHeldSource::Entry)
		{
			if (!MoveEntry(HeldEntryId, Target.Cell, bHeldRotated))
			{
				return false;
			}
			ClearHeld();
			return true;
		}
		const int32 Count = Pickup ? FMath::Min(HeldCount, Pickup->GetCount()) : HeldCount;
		if (Count <= 0 || PlaceItemAt(Item, Count, Target.Cell, bHeldRotated, Pickup ? HeldMagazine : -1) == 0)
		{
			return false;
		}
		if (Pickup)
		{
			TakeFromPickup(Pickup, Count);
			PushNotice(FText::Format(LOCTEXT("Picked", "Подобрано: {0}"), GetNameWithCount(Item, Count)));
		}
		ClearHeld();
		return true;
	}

	case ENoFearCaseTarget::Merge:
	{
		const int32 Index = FindEntryIndex(Target.MergeId);
		const int32 Moved = Index != INDEX_NONE ? FMath::Min(Item->GetMaxStack() - Entries[Index].Count, GetHeldCount()) : 0;
		if (Moved <= 0)
		{
			return false;
		}
		Entries[Index].Count += Moved;
		if (HeldSource == EHeldSource::Entry)
		{
			// Своя стопка: убывает на месте, кончилась — исчезает; остаток так и остаётся на курсоре.
			const int32 SourceIndex = FindEntryIndex(HeldEntryId);
			Entries[SourceIndex].Count -= Moved;
			if (Entries[SourceIndex].Count <= 0)
			{
				ReleaseEntryAt(SourceIndex);
				ClearHeld();
			}
		}
		else
		{
			if (Pickup)
			{
				TakeFromPickup(Pickup, Moved);
				PushNotice(FText::Format(LOCTEXT("Picked", "Подобрано: {0}"), GetNameWithCount(Item, Moved)));
			}
			HeldCount -= Moved;
			if (HeldCount <= 0)
			{
				ClearHeld();
			}
		}
		NotifyChanged();
		return true;
	}

	case ENoFearCaseTarget::WheelSlot:
		if (HeldSource == EHeldSource::Entry)
		{
			AssignWheelSlot(Target.Slot, HeldEntryId);
			ClearHeld();
			return true;
		}
		if (HeldSource == EHeldSource::Split)
		{
			// Отделённая часть возвращается в стопку, сектор получает саму стопку.
			if (FindEntryIndex(HeldEntryId) != INDEX_NONE)
			{
				AssignWheelSlot(Target.Slot, HeldEntryId);
			}
			ReturnHeld();
			return true;
		}
		return false;

	case ENoFearCaseTarget::Drop:
	{
		const int32 Count = GetHeldCount();
		if (HeldSource == EHeldSource::Entry && HeldWheelSlot != INDEX_NONE)
		{
			// Из колеса за край — убрать из сектора: сектор лишь ссылка на предмет в кейсе.
			ClearWheelSlot(HeldWheelSlot);
		}
		else if (HeldSource == EHeldSource::Entry)
		{
			DropEntry(HeldEntryId, 0);
			PushNotice(FText::Format(LOCTEXT("Dropped", "Выброшено: {0}"), GetNameWithCount(Item, Count)));
		}
		else if (HeldSource == EHeldSource::Split)
		{
			SpawnDropped(Item, Count, -1);
			PushNotice(FText::Format(LOCTEXT("Dropped", "Выброшено: {0}"), GetNameWithCount(Item, Count)));
		}
		// С земли — просто отпустить: он там и лежит.
		ClearHeld();
		return true;
	}

	default:
		return false;
	}
}

void UNoFearInventoryComponent::ReturnHeld()
{
	if (HeldSource != EHeldSource::Split || !HeldItem || HeldCount <= 0)
	{
		ClearHeld();
		return;
	}
	UNoFearItemData* Item = HeldItem;
	int32 Left = HeldCount;
	const int32 Index = FindEntryIndex(HeldEntryId);
	ClearHeld();
	if (Index != INDEX_NONE && Entries[Index].Item == Item)
	{
		const int32 Back = FMath::Min(Left, Item->GetMaxStack() - Entries[Index].Count);
		Entries[Index].Count += Back;
		Left -= Back;
	}
	// Стопку за это время доливали или выбросили — что не влезло, раскладывается заново или падает под ноги.
	if (Left > 0)
	{
		AddItemOrDrop(Item, Left, -1);
	}
	NotifyChanged();
}

void UNoFearInventoryComponent::ClearHeld()
{
	HeldSource = EHeldSource::None;
	HeldEntryId = 0;
	HeldItem = nullptr;
	HeldCount = 0;
	HeldMagazine = -1;
	bHeldRotated = false;
	HeldGrab = FVector2D::ZeroVector;
	HeldWheelSlot = INDEX_NONE;
	HeldPickup.Reset();
}

// ---------------------------------------------------------------------------
// Мышь и клавиши экрана
// ---------------------------------------------------------------------------

int32 UNoFearInventoryComponent::GetHoveredEntryId() const
{
	const FNoFearCaseLayout Layout = GetScreenLayout();
	if (!Layout.IsOnGrid(ScreenCursor))
	{
		return 0;
	}
	const FVector2D Local = Layout.ToGrid(ScreenCursor);
	return GetEntryAt(FIntPoint(FMath::FloorToInt(Local.X), FMath::FloorToInt(Local.Y)));
}

int32 UNoFearInventoryComponent::GetHoveredWheelSlot() const
{
	return GetScreenLayout().GetSlotAt(ScreenCursor);
}

void UNoFearInventoryComponent::ScreenPress()
{
	if (!bScreenOpen)
	{
		return;
	}
	UpdateScreenCursor(ScreenController.Get());
	PressCursor = ScreenCursor;
	bPressConsumed = false;

	if (bMenuOpen)
	{
		// Щелчок мимо меню просто закрывает его.
		const int32 Item = GetHoveredMenuItem();
		if (MenuActions.IsValidIndex(Item))
		{
			RunMenuAction(MenuActions[Item]);
		}
		CloseMenu();
		bPressConsumed = true;
		return;
	}
	if (IsHolding())
	{
		PlaceHeld();
		bPressConsumed = true;
		return;
	}
	if (const int32 Id = GetHoveredEntryId())
	{
		HoldEntry(Id);
		return;
	}
	// Из сектора колеса тоже можно взять — и перетащить в другой сектор.
	const int32 Slot = GetHoveredWheelSlot();
	if (const int32 SlotId = GetWheelSlot(Slot))
	{
		HoldEntry(SlotId, Slot);
	}
}

void UNoFearInventoryComponent::ScreenRelease()
{
	if (!bScreenOpen)
	{
		return;
	}
	UpdateScreenCursor(ScreenController.Get());
	if (bPressConsumed)
	{
		bPressConsumed = false;
		return;
	}
	// Перетащили — положить. Отпустили на месте — предмет остаётся на курсоре до следующего щелчка.
	if (IsHolding() && FVector2D::Distance(ScreenCursor, PressCursor) > DragThreshold * GetScreenLayout().Scale)
	{
		PlaceHeld();
	}
}

void UNoFearInventoryComponent::ScreenContext()
{
	if (!bScreenOpen)
	{
		return;
	}
	UpdateScreenCursor(ScreenController.Get());
	if (bMenuOpen)
	{
		CloseMenu();
		return;
	}
	if (IsHolding())
	{
		ScreenRotate();
		return;
	}
	const int32 Slot = GetHoveredWheelSlot();
	if (Slot != INDEX_NONE)
	{
		ClearWheelSlot(Slot);
		return;
	}
	if (const int32 Id = GetHoveredEntryId())
	{
		OpenMenu(Id);
	}
}

void UNoFearInventoryComponent::ScreenRotate()
{
	if (!bScreenOpen)
	{
		return;
	}
	UpdateScreenCursor(ScreenController.Get());
	if (IsHolding())
	{
		// Поворот по часовой: точка (x, y) предмета высотой h уходит в (h − y, x), курсор держит ту же точку.
		const FIntPoint Size = GetHeldSize();
		if (Size.X != Size.Y)
		{
			HeldGrab = FVector2D(Size.Y - HeldGrab.Y, HeldGrab.X);
			bHeldRotated = !bHeldRotated;
		}
		return;
	}
	if (bMenuOpen)
	{
		return;
	}
	const int32 Id = GetHoveredEntryId();
	const int32 Index = FindEntryIndex(Id);
	if (Index == INDEX_NONE)
	{
		return;
	}
	const FNoFearInventoryEntry& Entry = Entries[Index];
	const FIntPoint Size = Entry.GetSize();
	if (Size.X != Size.Y && !MoveEntry(Id, Entry.Cell, !Entry.bRotated))
	{
		PushNotice(LOCTEXT("NoRoomRotate", "Не повернуть: мешают соседи или край"));
	}
}

void UNoFearInventoryComponent::ScreenBack()
{
	if (!bScreenOpen)
	{
		return;
	}
	if (bMenuOpen)
	{
		CloseMenu();
	}
	else if (IsHolding())
	{
		ReturnHeld();
	}
	else
	{
		CloseScreen();
	}
}

void UNoFearInventoryComponent::ScreenSlotKey(int32 Slot)
{
	if (!bScreenOpen || !WheelSlots.IsValidIndex(Slot))
	{
		return;
	}
	UpdateScreenCursor(ScreenController.Get());
	if (IsHoldingPickup())
	{
		PushNotice(LOCTEXT("WheelPickup", "Сначала положите предмет в кейс"));
		return;
	}
	const int32 Id = IsHolding() ? GetHeldEntryId() : GetHoveredEntryId();
	const int32 Index = FindEntryIndex(Id);
	if (Index == INDEX_NONE)
	{
		return;
	}
	if (!IsWheelItem(Entries[Index].Item))
	{
		PushNotice(LOCTEXT("WheelKind", "В колесо — только оружие, гранаты и медицина"));
		return;
	}
	if (GetWheelSlot(Slot) == Id)
	{
		ClearWheelSlot(Slot);
	}
	else
	{
		AssignWheelSlot(Slot, Id);
	}
}

// ---------------------------------------------------------------------------
// Меню ПКМ
// ---------------------------------------------------------------------------

void UNoFearInventoryComponent::OpenMenu(int32 EntryId)
{
	const int32 Index = FindEntryIndex(EntryId);
	if (Index == INDEX_NONE)
	{
		return;
	}
	const FNoFearInventoryEntry& Entry = Entries[Index];
	const UNoFearWeaponComponent* Weapons = GetWeaponComponent();

	MenuActions.Reset();
	if (Entry.Weapon && Weapons && Weapons->GetCurrentWeapon() != Entry.Weapon)
	{
		MenuActions.Add(ENoFearCaseAction::Equip);
	}
	if (FindWheelSlotOf(EntryId) != INDEX_NONE)
	{
		MenuActions.Add(ENoFearCaseAction::ClearWheel);
	}
	else if (IsWheelItem(Entry.Item) && WheelSlots.Contains(0))
	{
		MenuActions.Add(ENoFearCaseAction::AssignWheel);
	}
	if (Entry.Count > 1)
	{
		MenuActions.Add(ENoFearCaseAction::Split);
	}
	MenuActions.Add(ENoFearCaseAction::Drop);

	// Справа ниже курсора, не вылезая за экран.
	const FNoFearCaseLayout Layout = GetScreenLayout();
	const FVector2D Extent(Layout.MenuWidth, Layout.MenuItemHeight * MenuActions.Num());
	MenuPosition = ScreenCursor + FVector2D(6.f * Layout.Scale);
	MenuPosition.X = FMath::Clamp(MenuPosition.X, 0.f, FMath::Max(Layout.ViewportSize.X - Extent.X, 0.f));
	MenuPosition.Y = FMath::Clamp(MenuPosition.Y, 0.f, FMath::Max(Layout.ViewportSize.Y - Extent.Y, 0.f));
	MenuEntryId = EntryId;
	bMenuOpen = true;
}

void UNoFearInventoryComponent::CloseMenu()
{
	bMenuOpen = false;
	MenuEntryId = 0;
	MenuActions.Reset();
}

int32 UNoFearInventoryComponent::GetHoveredMenuItem() const
{
	if (!bMenuOpen)
	{
		return INDEX_NONE;
	}
	const FNoFearCaseLayout Layout = GetScreenLayout();
	const FVector2D Local = ScreenCursor - MenuPosition;
	if (Local.X < 0.f || Local.Y < 0.f || Local.X >= Layout.MenuWidth)
	{
		return INDEX_NONE;
	}
	const int32 Index = FMath::FloorToInt(Local.Y / Layout.MenuItemHeight);
	return MenuActions.IsValidIndex(Index) ? Index : INDEX_NONE;
}

void UNoFearInventoryComponent::RunMenuAction(ENoFearCaseAction Action)
{
	const int32 Id = MenuEntryId;
	CloseMenu();
	const int32 Index = FindEntryIndex(Id);
	if (Index == INDEX_NONE)
	{
		return;
	}

	switch (Action)
	{
	case ENoFearCaseAction::Equip:
		if (UNoFearWeaponComponent* Weapons = GetWeaponComponent(); Weapons && Entries[Index].Weapon)
		{
			Weapons->EquipWeapon(Weapons->GetWeapons().IndexOfByKey(Entries[Index].Weapon.Get()));
		}
		break;

	case ENoFearCaseAction::AssignWheel:
		if (const int32 Free = WheelSlots.IndexOfByKey(0); Free != INDEX_NONE)
		{
			AssignWheelSlot(Free, Id);
		}
		break;

	case ENoFearCaseAction::ClearWheel:
		ClearWheelSlot(FindWheelSlotOf(Id));
		break;

	case ENoFearCaseAction::Split:
	{
		// Половина (меньшая) уходит на курсор; положить — новой стопкой, Esc — обратно.
		FNoFearInventoryEntry& Entry = Entries[Index];
		const int32 Half = Entry.Count / 2;
		if (Half <= 0)
		{
			break;
		}
		Entry.Count -= Half;
		ClearHeld();
		HeldSource = EHeldSource::Split;
		HeldEntryId = Id;
		HeldItem = Entry.Item;
		HeldCount = Half;
		bHeldRotated = false;
		HeldGrab = GetCenterGrab(Entry.Item->GetGridSize());
		NotifyChanged();
		break;
	}

	case ENoFearCaseAction::Drop:
	{
		UNoFearItemData* Item = Entries[Index].Item;
		const int32 Count = Entries[Index].Count;
		DropEntry(Id, 0);
		PushNotice(FText::Format(LOCTEXT("Dropped", "Выброшено: {0}"), GetNameWithCount(Item, Count)));
		break;
	}
	}
}

FText UNoFearInventoryComponent::GetCaseActionName(ENoFearCaseAction Action)
{
	switch (Action)
	{
	case ENoFearCaseAction::Equip: return LOCTEXT("ActionEquip", "Взять в руки");
	case ENoFearCaseAction::AssignWheel: return LOCTEXT("ActionAssign", "В колесо");
	case ENoFearCaseAction::ClearWheel: return LOCTEXT("ActionClear", "Убрать из колеса");
	case ENoFearCaseAction::Split: return LOCTEXT("ActionSplit", "Разделить");
	case ENoFearCaseAction::Drop: return LOCTEXT("ActionDrop", "Выбросить");
	}
	return FText::GetEmpty();
}

// ---------------------------------------------------------------------------
// Консоль
// ---------------------------------------------------------------------------

namespace
{
	FAutoConsoleCommandWithWorldAndArgs CaseScreenCommand(
		TEXT("NoFear.Inv.Screen"),
		TEXT("Экран кейса героя: NoFear.Inv.Screen [0|1], без числа — переключить."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			UNoFearInventoryComponent* Case = UNoFearInventoryComponent::FindCase(UGameplayStatics::GetPlayerPawn(World, 0));
			if (!Case)
			{
				UE_LOG(LogTemp, Warning, TEXT("NoFear.Inv.Screen: у героя нет кейса"));
				return;
			}
			if (Args.Num() == 0)
			{
				Case->ToggleScreen();
			}
			else if (FCString::Atoi(*Args[0]) != 0)
			{
				Case->OpenScreen();
			}
			else
			{
				Case->CloseScreen();
			}
		}));

	FAutoConsoleCommandWithWorldAndArgs CaseKeyCommand(
		TEXT("NoFear.Inv.Key"),
		TEXT("Нажать или отпустить клавишу у игрока, как с клавиатуры (для проверок): NoFear.Inv.Key <Key> [press|release], например I, LeftMouseButton, R, One."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
			const FKey Key = Args.Num() > 0 ? FKey(*Args[0]) : FKey();
			if (!PC || !Key.IsValid())
			{
				UE_LOG(LogTemp, Warning, TEXT("NoFear.Inv.Key: нет игрока или клавиши %s"), Args.Num() > 0 ? *Args[0] : TEXT(""));
				return;
			}
			const bool bRelease = Args.Num() > 1 && Args[1].Equals(TEXT("release"), ESearchCase::IgnoreCase);
			PC->InputKey(FInputKeyEventArgs::CreateSimulated(Key, bRelease ? IE_Released : IE_Pressed, bRelease ? 0.f : 1.f));
		}));
}

#undef LOCTEXT_NAMESPACE
