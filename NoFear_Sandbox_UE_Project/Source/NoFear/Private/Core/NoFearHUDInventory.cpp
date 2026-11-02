// Кейс героя в HUD (Docs/Inventory_Design.md): сообщения о подборе, отладочная мини-сетка (NoFear.Inv.Debug)
// и экран кейса на I — с колесом или с сеткой открытого хранилища. Колесо предметов на Q — этап 3. Остальной HUD — NoFearHUD.cpp.

#include "Core/NoFearHUD.h"

#include "Engine/Canvas.h"
#include "GameFramework/Pawn.h"
#include "HAL/IConsoleManager.h"
#include "Inventory/NoFearInventoryComponent.h"
#include "Inventory/NoFearItemData.h"
#include "Weapons/NoFearWeapon.h"
#include "Weapons/NoFearWeaponComponent.h"
#include "Weapons/NoFearWeaponData.h"

namespace NoFearHUDInventory
{
	TAutoConsoleVariable<int32> CVarDebug(
		TEXT("NoFear.Inv.Debug"),
		0,
		TEXT("1 — показывать сетку кейса и сектора колеса справа на экране."),
		ECVF_Default);

	/** Короткая подпись предмета в клетке: первые буквы названия. */
	FString ShortName(const UNoFearItemData* Item, int32 MaxChars)
	{
		return Item ? Item->GetDisplayName().ToString().Left(MaxChars) : FString(TEXT("?"));
	}
}

void ANoFearHUD::DrawInventory()
{
	const UNoFearInventoryComponent* Case = UNoFearInventoryComponent::FindCase(GetOwningPawn());
	// Открыт экран кейса — сообщение и сетка рисуются на нём.
	if (!Case || Case->IsScreenOpen())
	{
		return;
	}
	const float S = GetUIScale();

	// Сообщение «Подобрано: …» / «Нет места в кейсе» — над слотами оружия.
	float Alpha = 0.f;
	const FText Notice = Case->GetNotice(Alpha);
	if (!Notice.IsEmpty() && Alpha > 0.f)
	{
		const float TextScale = 1.1f * S;
		float Width = 0.f;
		float Height = 0.f;
		GetTextSize(Notice.ToString(), Width, Height, GetFont(), TextScale);
		const float Padding = 8.f * S;
		const float Y = Canvas->SizeY - Margin * S - 34.f * S - 24.f * S - Height;
		const float X = Canvas->SizeX * 0.5f;
		DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.55f * Alpha), X - Width * 0.5f - Padding, Y - Padding * 0.5f, Width + Padding * 2.f, Height + Padding);
		DrawLabel(Notice.ToString(), X, Y, FLinearColor(1.f, 1.f, 1.f, Alpha), TextScale, ELabelAlign::Center);
	}

	if (NoFearHUDInventory::CVarDebug.GetValueOnGameThread() <= 0)
	{
		return;
	}

	// Сетка кейса: клетки, предметы цветом предмета, число в стопке и патроны в магазине ствола.
	const FIntPoint Grid = Case->GetGridSize();
	const float Cell = 24.f * S;
	const float Width = Grid.X * Cell;
	const float Height = Grid.Y * Cell;
	const float Left = Canvas->SizeX - Margin * S - Width;
	const float Top = Canvas->SizeY * 0.5f - Height * 0.5f;
	const float TextScale = 0.7f * S;

	DrawLabel(FString::Printf(TEXT("Кейс %dx%d, расширений %d/%d"), Grid.X, Grid.Y, Case->GetExpansionLevel(), Case->GetMaxExpansionLevel()),
		Left + Width, Top - 22.f * S, FLinearColor::White, 0.8f * S, ELabelAlign::Right);
	DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.6f), Left - 2.f * S, Top - 2.f * S, Width + 4.f * S, Height + 4.f * S);
	for (int32 Y = 0; Y < Grid.Y; ++Y)
	{
		for (int32 X = 0; X < Grid.X; ++X)
		{
			DrawRect(FLinearColor(0.15f, 0.15f, 0.15f, 0.7f), Left + X * Cell + 1.f, Top + Y * Cell + 1.f, Cell - 2.f, Cell - 2.f);
		}
	}

	for (const FNoFearInventoryEntry& Entry : Case->GetEntriesRef())
	{
		const FIntPoint Size = Entry.GetSize();
		const float X = Left + Entry.Cell.X * Cell;
		const float Y = Top + Entry.Cell.Y * Cell;
		FLinearColor Color = Entry.Item ? Entry.Item->Color : FLinearColor::Gray;
		Color.A = 0.85f;
		DrawRect(Color, X + 2.f, Y + 2.f, Size.X * Cell - 4.f, Size.Y * Cell - 4.f);

		FString Label = FString::Printf(TEXT("%d %s"), Entry.Id, *NoFearHUDInventory::ShortName(Entry.Item, FMath::Max(Size.X * 2, 3)));
		if (Entry.Weapon)
		{
			Label += FString::Printf(TEXT(" [%d]"), Entry.Weapon->GetMagazineAmmo());
		}
		else if (Entry.Count > 1)
		{
			Label += FString::Printf(TEXT(" ×%d"), Entry.Count);
		}
		DrawLabel(Label, X + 4.f * S, Y + 3.f * S, FLinearColor::White, TextScale, ELabelAlign::Left);
	}

	// Сектора колеса: номер и что в нём.
	float LineY = Top + Height + 10.f * S;
	for (int32 Slot = 0; Slot < UNoFearInventoryComponent::WheelSlotCount; ++Slot)
	{
		const int32 EntryId = Case->GetWheelSlot(Slot);
		FNoFearInventoryEntry Entry;
		const FString Text = EntryId > 0 && Case->GetEntry(EntryId, Entry)
			? FString::Printf(TEXT("%d: %s (#%d)"), Slot + 1, *NoFearHUDInventory::ShortName(Entry.Item, 24), EntryId)
			: FString::Printf(TEXT("%d: —"), Slot + 1);
		DrawLabel(Text, Left, LineY, FLinearColor(1.f, 1.f, 1.f, EntryId > 0 ? 0.9f : 0.45f), TextScale, ELabelAlign::Left);
		LineY += 16.f * S;
	}
}

void ANoFearHUD::DrawCaseScreen()
{
	const UNoFearInventoryComponent* Case = UNoFearInventoryComponent::FindCase(GetOwningPawn());
	if (!Case || !Case->IsScreenOpen())
	{
		return;
	}
	const FNoFearCaseLayout Layout = Case->GetScreenLayout();
	const float S = Layout.Scale;
	const float Line = FMath::Max(1.f, FMath::RoundToFloat(2.f * S));
	const FVector2D Cursor = Case->GetScreenCursor();
	const FLinearColor Good(0.25f, 0.9f, 0.4f, 1.f);
	const FLinearColor Bad(0.95f, 0.25f, 0.2f, 1.f);
	const FLinearColor Dim(1.f, 1.f, 1.f, 0.6f);

	const UNoFearWeaponComponent* Weapons = GetOwningPawn() ? GetOwningPawn()->FindComponentByClass<UNoFearWeaponComponent>() : nullptr;
	const ANoFearWeapon* InHands = Weapons ? Weapons->GetCurrentWeapon() : nullptr;

	auto Frame = [this](const FVector2D& Min, const FVector2D& Size, float Thickness, const FLinearColor& Color)
	{
		DrawRect(Color, Min.X, Min.Y, Size.X, Thickness);
		DrawRect(Color, Min.X, Min.Y + Size.Y - Thickness, Size.X, Thickness);
		DrawRect(Color, Min.X, Min.Y + Thickness, Thickness, Size.Y - Thickness * 2.f);
		DrawRect(Color, Min.X + Size.X - Thickness, Min.Y + Thickness, Thickness, Size.Y - Thickness * 2.f);
	};
	// Обрезать с многоточием, чтобы влезло в ширину.
	auto Fit = [this](const FString& Text, float MaxWidth, float Scale)
	{
		float Width = 0.f;
		float Height = 0.f;
		GetTextSize(Text, Width, Height, GetFont(), Scale);
		if (Width <= MaxWidth)
		{
			return Text;
		}
		for (int32 Chars = Text.Len() - 1; Chars > 0; --Chars)
		{
			const FString Short = Text.Left(Chars) + TEXT("…");
			GetTextSize(Short, Width, Height, GetFont(), Scale);
			if (Width <= MaxWidth)
			{
				return Short;
			}
		}
		return FString();
	};
	// Плашка предмета: тёмная заливка с оттенком предмета, рамка его цветом, название и число.
	auto DrawItem = [&](const UNoFearItemData* Item, const FVector2D& Min, const FVector2D& Size, float Alpha, bool bHighlight,
		const FString& Corner, int32 Slot, bool bInHands)
	{
		const FLinearColor ItemColor = Item ? Item->Color : FLinearColor::Gray;
		FLinearColor Fill = FMath::Lerp(FLinearColor(0.07f, 0.08f, 0.09f), ItemColor, 0.35f);
		Fill.A = 0.95f * Alpha;
		const float Inset = 2.f * S;
		const FVector2D BoxMin = Min + FVector2D(Inset);
		const FVector2D BoxSize = Size - FVector2D(Inset * 2.f);
		DrawRect(Fill, BoxMin.X, BoxMin.Y, BoxSize.X, BoxSize.Y);
		FLinearColor Border = bHighlight ? FLinearColor::White : ItemColor;
		Border.A = Alpha;
		Frame(BoxMin, BoxSize, bHighlight ? Line * 1.5f : Line, Border);

		const float TextScale = 0.78f * S;
		const float Pad = 5.f * S;
		const FLinearColor Text(1.f, 1.f, 1.f, Alpha);
		const FString Name = Item ? Item->GetDisplayName().ToString() : FString(TEXT("?"));
		const float NameWidth = BoxSize.X - Pad * 2.f - (Slot != INDEX_NONE ? 14.f * S : 0.f);
		DrawLabel(Fit(Name, NameWidth, TextScale), BoxMin.X + Pad, BoxMin.Y + Pad * 0.6f, Text, TextScale, ELabelAlign::Left);
		if (!Corner.IsEmpty())
		{
			DrawLabel(Corner, BoxMin.X + BoxSize.X - Pad, BoxMin.Y + BoxSize.Y - 20.f * S, Text, TextScale, ELabelAlign::Right);
		}
		if (Slot != INDEX_NONE)
		{
			// Номер сектора колеса — жёлтой цифрой в правом верхнем углу.
			DrawLabel(FString::FromInt(Slot + 1), BoxMin.X + BoxSize.X - Pad, BoxMin.Y + Pad * 0.6f, FLinearColor(1.f, 0.85f, 0.3f, Alpha), TextScale, ELabelAlign::Right);
		}
		if (bInHands && BoxSize.X > 120.f * S)
		{
			DrawLabel(TEXT("в руках"), BoxMin.X + Pad, BoxMin.Y + BoxSize.Y - 20.f * S, FLinearColor(0.6f, 0.9f, 1.f, Alpha), 0.7f * S, ELabelAlign::Left);
		}
	};
	auto CornerText = [](const UNoFearItemData* Item, int32 Count, int32 Magazine)
	{
		if (Item && Item->Kind == ENoFearItemKind::Weapon && Item->WeaponData)
		{
			const int32 Size = Item->WeaponData->MagazineSize;
			return FString::Printf(TEXT("%d/%d"), Magazine < 0 ? Size : Magazine, Size);
		}
		return Count > 1 ? FString::Printf(TEXT("×%d"), Count) : FString();
	};

	// Затемнение и подложка.
	DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.45f), 0.f, 0.f, Canvas->SizeX, Canvas->SizeY);
	const FVector2D PanelSize = Layout.PanelMax - Layout.PanelMin;
	DrawRect(FLinearColor(0.03f, 0.035f, 0.04f, 0.9f), Layout.PanelMin.X, Layout.PanelMin.Y, PanelSize.X, PanelSize.Y);
	Frame(Layout.PanelMin, PanelSize, FMath::Max(1.f, S), FLinearColor(1.f, 1.f, 1.f, 0.15f));

	const FIntPoint Grid = Layout.GridSize;
	const UNoFearItemContainerComponent* Container = Case->GetOpenContainer();
	const float TitleY = Layout.PanelMin.Y + 18.f * S;
	DrawLabel(FString::Printf(TEXT("Кейс %d×%d · расширений %d/%d"), Grid.X, Grid.Y, Case->GetExpansionLevel(), Case->GetMaxExpansionLevel()),
		Layout.GridOrigin.X, TitleY, FLinearColor::White, 1.f * S, ELabelAlign::Left);
	if (Layout.bShowWheel)
	{
		DrawLabel(TEXT("Колесо (1–8)"), Layout.WheelCenter.X, TitleY, Dim, 0.9f * S, ELabelAlign::Center);
	}
	else if (Container)
	{
		DrawLabel(FString::Printf(TEXT("%s %d×%d"), *Container->DisplayName.ToString(), Layout.ContainerSize.X, Layout.ContainerSize.Y),
			Layout.ContainerOrigin.X, TitleY, FLinearColor::White, 1.f * S, ELabelAlign::Left);
	}

	// Клетки.
	auto DrawCells = [&](bool bOfContainer)
	{
		const FIntPoint Size = Layout.GetSize(bOfContainer);
		for (int32 Y = 0; Y < Size.Y; ++Y)
		{
			for (int32 X = 0; X < Size.X; ++X)
			{
				const FVector2D Min = Layout.GetCellMin(FIntPoint(X, Y), bOfContainer);
				DrawRect(FLinearColor(0.12f, 0.13f, 0.14f, 0.95f), Min.X + 1.f, Min.Y + 1.f, Layout.Cell - 2.f, Layout.Cell - 2.f);
			}
		}
	};
	DrawCells(false);
	if (Container)
	{
		DrawCells(true);
	}

	const bool bHolding = Case->IsHolding();
	const bool bMenu = Case->IsMenuOpen();
	const bool bHeldFromContainer = Case->IsHoldingFromContainer();
	const int32 HeldId = bHolding && !Case->IsHoldingSplit() && !bHeldFromContainer ? Case->GetHeldEntryId() : 0;
	const int32 HeldContainerId = bHolding && !Case->IsHoldingSplit() && bHeldFromContainer ? Case->GetHeldEntryId() : 0;
	const int32 HoverId = !bHolding && !bMenu ? Case->GetHoveredEntryId() : 0;
	const int32 HoverContainerId = !bHolding && !bMenu ? Case->GetHoveredContainerEntryId() : 0;
	const int32 HoverSlot = Case->GetHoveredWheelSlot();
	const int32 MenuId = bMenu && !Case->IsMenuForContainer() ? Case->GetMenuEntryId() : 0;
	const int32 MenuContainerId = bMenu && Case->IsMenuForContainer() ? Case->GetMenuEntryId() : 0;
	const FNoFearCaseTarget Target = bHolding ? Case->GetHeldTarget() : FNoFearCaseTarget();

	// Предметы. Тот, что на курсоре, остаётся на месте полупрозрачным, пока его не положат.
	for (const FNoFearInventoryEntry& Entry : Case->GetEntriesRef())
	{
		const bool bHighlight = Entry.Id == HoverId || Entry.Id == MenuId;
		DrawItem(Entry.Item, Layout.GetCellMin(Entry.Cell), FVector2D(Entry.GetSize()) * Layout.Cell, Entry.Id == HeldId ? 0.3f : 1.f,
			bHighlight, CornerText(Entry.Item, Entry.Count, Entry.GetMagazineAmmo()), Case->FindWheelSlotOf(Entry.Id), Entry.Weapon && Entry.Weapon == InHands);
	}
	if (Container)
	{
		for (const FNoFearInventoryEntry& Entry : Container->GetEntriesRef())
		{
			const bool bHighlight = Entry.Id == HoverContainerId || Entry.Id == MenuContainerId;
			DrawItem(Entry.Item, Layout.GetCellMin(Entry.Cell, true), FVector2D(Entry.GetSize()) * Layout.Cell, Entry.Id == HeldContainerId ? 0.3f : 1.f,
				bHighlight, CornerText(Entry.Item, Entry.Count, Entry.GetMagazineAmmo()), INDEX_NONE, false);
		}
	}

	// Колесо: восемь секторов, первый сверху, дальше по часовой. С хранилищем на его месте сетка хранилища.
	const float Half = Layout.SlotSize * 0.5f;
	for (int32 Slot = 0; Layout.bShowWheel && Slot < UNoFearInventoryComponent::WheelSlotCount; ++Slot)
	{
		const FVector2D Min = Layout.GetSlotCenter(Slot) - FVector2D(Half);
		const FVector2D Size(Layout.SlotSize);
		FNoFearInventoryEntry Entry;
		if (Case->GetEntry(Case->GetWheelSlot(Slot), Entry))
		{
			const int32 Magazine = Entry.GetMagazineAmmo();
			// У гранат и медицины — сколько всего таких в кейсе: колесо берёт из любой стопки.
			const int32 Count = Entry.Item && Entry.Item->Kind != ENoFearItemKind::Weapon ? Case->CountItem(Entry.Item) : Entry.Count;
			DrawItem(Entry.Item, Min, Size, Entry.Id == HeldId ? 0.4f : 1.f, Slot == HoverSlot && !bHolding && !bMenu,
				CornerText(Entry.Item, Count, Magazine), INDEX_NONE, false);
		}
		else
		{
			DrawRect(FLinearColor(0.1f, 0.11f, 0.12f, 0.9f), Min.X + 2.f * S, Min.Y + 2.f * S, Size.X - 4.f * S, Size.Y - 4.f * S);
			Frame(Min + FVector2D(2.f * S), Size - FVector2D(4.f * S), FMath::Max(1.f, S), FLinearColor(1.f, 1.f, 1.f, Slot == HoverSlot ? 0.6f : 0.2f));
		}
		DrawLabel(FString::FromInt(Slot + 1), Min.X + 7.f * S, Min.Y + Size.Y - 22.f * S, FLinearColor(1.f, 0.85f, 0.3f, 0.9f), 0.8f * S, ELabelAlign::Left);
	}

	// Предмет на курсоре — за то место, где за него взялись.
	if (bHolding)
	{
		const FVector2D Min = Cursor - Case->GetHeldGrab() * Layout.Cell;
		DrawItem(Case->GetHeldItem(), Min, FVector2D(Case->GetHeldSize()) * Layout.Cell, 0.75f, false,
			CornerText(Case->GetHeldItem(), Case->GetHeldCount(), Case->GetHeldMagazine()), INDEX_NONE, false);
		// Куда ляжет — поверх предмета на курсоре: клетки зелёные или красные; доливка в стопку и сектор — рамка.
		if (Target.Kind == ENoFearCaseTarget::Grid)
		{
			const FIntPoint Size = Case->GetHeldSize();
			const FIntPoint TargetGrid = Layout.GetSize(Target.bContainer);
			FLinearColor Color = Target.bValid ? Good : Bad;
			Color.A = 0.35f;
			for (int32 Y = FMath::Max(Target.Cell.Y, 0); Y < FMath::Min(Target.Cell.Y + Size.Y, TargetGrid.Y); ++Y)
			{
				for (int32 X = FMath::Max(Target.Cell.X, 0); X < FMath::Min(Target.Cell.X + Size.X, TargetGrid.X); ++X)
				{
					const FVector2D CellMin = Layout.GetCellMin(FIntPoint(X, Y), Target.bContainer);
					DrawRect(Color, CellMin.X + 1.f, CellMin.Y + 1.f, Layout.Cell - 2.f, Layout.Cell - 2.f);
				}
			}
		}
		else if (Target.Kind == ENoFearCaseTarget::Merge)
		{
			const UNoFearItemContainerComponent* Storage = Target.bContainer ? Container : Case;
			if (const FNoFearInventoryEntry* Under = Storage ? Storage->FindEntry(Target.MergeId) : nullptr)
			{
				Frame(Layout.GetCellMin(Under->Cell, Target.bContainer), FVector2D(Under->GetSize()) * Layout.Cell, Line * 2.f, Good);
			}
		}
		else if (Target.Kind == ENoFearCaseTarget::WheelSlot)
		{
			Frame(Layout.GetSlotCenter(Target.Slot) - FVector2D(Half), FVector2D(Layout.SlotSize), Line * 2.f, Target.bValid ? Good : Bad);
		}
		else if (Target.Kind == ENoFearCaseTarget::Drop)
		{
			const TCHAR* DropText = Case->IsHoldingPickup() ? TEXT("Оставить на земле")
				: Case->GetHeldWheelSlot() != INDEX_NONE ? TEXT("Убрать из колеса") : TEXT("Выбросить");
			DrawLabel(DropText, Cursor.X, Cursor.Y + 26.f * S, Bad, 1.f * S, ELabelAlign::Center);
		}
	}

	// Карточка предмета под курсором: название, описание, подробности.
	int32 CardId = HoverId;
	const UNoFearItemContainerComponent* CardStorage = Case;
	if (!CardId && HoverContainerId)
	{
		CardId = HoverContainerId;
		CardStorage = Container;
	}
	if (!CardId && !bHolding && !bMenu && HoverSlot != INDEX_NONE)
	{
		CardId = Case->GetWheelSlot(HoverSlot);
	}
	FNoFearInventoryEntry Hovered;
	if (CardId && CardStorage && CardStorage->GetEntry(CardId, Hovered) && Hovered.Item)
	{
		const UNoFearItemData* Item = Hovered.Item;
		const bool bInCase = CardStorage == Case;
		const float Width = 320.f * S;
		const float Pad = 12.f * S;
		const float TextScale = 0.82f * S;
		const float LineHeight = 22.f * S;

		// Описание — по словам в ширину карточки.
		TArray<FString> Lines;
		TArray<FString> Words;
		Item->Description.ToString().ParseIntoArrayWS(Words);
		FString Current;
		for (const FString& Word : Words)
		{
			const FString Next = Current.IsEmpty() ? Word : Current + TEXT(" ") + Word;
			float W = 0.f;
			float H = 0.f;
			GetTextSize(Next, W, H, GetFont(), TextScale);
			if (W > Width - Pad * 2.f && !Current.IsEmpty())
			{
				Lines.Add(Current);
				Current = Word;
			}
			else
			{
				Current = Next;
			}
		}
		if (!Current.IsEmpty())
		{
			Lines.Add(Current);
		}

		TArray<FString> Details;
		if (Item->Kind == ENoFearItemKind::Weapon && Item->WeaponData)
		{
			const int32 Magazine = Hovered.GetMagazineAmmo();
			Details.Add(FString::Printf(TEXT("Магазин %d/%d"), Magazine < 0 ? Item->WeaponData->MagazineSize : Magazine, Item->WeaponData->MagazineSize));
			if (Item->AmmoItem)
			{
				Details.Add(FString::Printf(TEXT("%s в кейсе: %d"), *Item->AmmoItem->GetDisplayName().ToString(), Case->CountItem(Item->AmmoItem)));
			}
		}
		else if (Item->GetMaxStack() > 1)
		{
			Details.Add(FString::Printf(TEXT("В стопке %d/%d, всего %d"), Hovered.Count, Item->GetMaxStack(), CardStorage->CountItem(Item)));
		}
		if (Item->Kind == ENoFearItemKind::Medicine)
		{
			Details.Add(Item->bHealToFull
				? FString::Printf(TEXT("Лечит полностью за %.0f с"), Item->UseTime)
				: FString::Printf(TEXT("Лечит %.0f за %.0f с"), Item->HealAmount, Item->UseTime));
		}
		const int32 Slot = bInCase ? Case->FindWheelSlotOf(Hovered.Id) : INDEX_NONE;
		if (Slot != INDEX_NONE)
		{
			Details.Add(FString::Printf(TEXT("Колесо: сектор %d"), Slot + 1));
		}
		else if (bInCase && Layout.bShowWheel && UNoFearInventoryComponent::IsWheelItem(Item))
		{
			Details.Add(TEXT("Не в колесе: 1–8 или перетащить в сектор"));
		}
		else if (!bInCase)
		{
			Details.Add(FString::Printf(TEXT("Лежит: %s"), *CardStorage->DisplayName.ToString()));
		}

		const float Height = Pad * 2.f + 28.f * S + Lines.Num() * LineHeight + (Details.Num() > 0 ? 8.f * S + Details.Num() * LineHeight : 0.f);
		FVector2D Min = Cursor + FVector2D(20.f * S, 20.f * S);
		Min.X = FMath::Max(FMath::Min(Min.X, Canvas->SizeX - Width - 8.f * S), 0.f);
		Min.Y = FMath::Max(FMath::Min(Min.Y, Canvas->SizeY - Height - 8.f * S), 0.f);
		DrawRect(FLinearColor(0.02f, 0.02f, 0.025f, 0.95f), Min.X, Min.Y, Width, Height);
		Frame(Min, FVector2D(Width, Height), Line, Item->Color);
		float Y = Min.Y + Pad;
		DrawLabel(Item->GetDisplayName().ToString(), Min.X + Pad, Y, FLinearColor::White, 1.05f * S, ELabelAlign::Left);
		Y += 28.f * S;
		for (const FString& Text : Lines)
		{
			DrawLabel(Text, Min.X + Pad, Y, FLinearColor(0.8f, 0.8f, 0.8f, 1.f), TextScale, ELabelAlign::Left);
			Y += LineHeight;
		}
		Y += 8.f * S;
		for (const FString& Text : Details)
		{
			DrawLabel(Text, Min.X + Pad, Y, FLinearColor(0.6f, 0.85f, 1.f, 1.f), TextScale, ELabelAlign::Left);
			Y += LineHeight;
		}
	}

	// Меню ПКМ.
	if (bMenu)
	{
		const TArray<ENoFearCaseAction> Actions = Case->GetMenuActions();
		const FVector2D Min = Case->GetMenuPosition();
		const FVector2D Size(Layout.MenuWidth, Layout.MenuItemHeight * Actions.Num());
		const int32 Hover = Case->GetHoveredMenuItem();
		DrawRect(FLinearColor(0.02f, 0.02f, 0.025f, 0.97f), Min.X, Min.Y, Size.X, Size.Y);
		Frame(Min, Size, FMath::Max(1.f, S), FLinearColor(1.f, 1.f, 1.f, 0.35f));
		for (int32 Index = 0; Index < Actions.Num(); ++Index)
		{
			const float Y = Min.Y + Index * Layout.MenuItemHeight;
			if (Index == Hover)
			{
				DrawRect(FLinearColor(1.f, 1.f, 1.f, 0.14f), Min.X + 1.f, Y + 1.f, Size.X - 2.f, Layout.MenuItemHeight - 2.f);
			}
			const FLinearColor Color = Actions[Index] == ENoFearCaseAction::Drop ? FLinearColor(1.f, 0.6f, 0.5f, 1.f) : FLinearColor::White;
			DrawLabel(Fit(Case->GetMenuActionName(Actions[Index]).ToString(), Size.X - 20.f * S, 0.85f * S), Min.X + 12.f * S, Y + 7.f * S, Color, 0.85f * S, ELabelAlign::Left);
		}
	}

	// Сообщение и подсказка под подложкой.
	float Alpha = 0.f;
	const FText Notice = Case->GetNotice(Alpha);
	const float CenterX = (Layout.PanelMin.X + Layout.PanelMax.X) * 0.5f;
	if (!Notice.IsEmpty() && Alpha > 0.f)
	{
		DrawLabel(Notice.ToString(), CenterX, Layout.PanelMax.Y + 14.f * S, FLinearColor(1.f, 0.9f, 0.6f, Alpha), 1.05f * S, ELabelAlign::Center);
	}
	const TCHAR* Hint = Container
		? TEXT("ЛКМ — взять / положить, тащить между сетками · ПКМ — действия · R — повернуть · за край — выбросить · E / I / Esc — закрыть")
		: TEXT("ЛКМ — взять / положить · ПКМ — действия, повернуть · R — повернуть · 1–8 — в колесо · за край — выбросить · I / Esc — закрыть");
	DrawLabel(Hint, CenterX, Layout.PanelMax.Y + 46.f * S, Dim, 0.8f * S, ELabelAlign::Center);
}
