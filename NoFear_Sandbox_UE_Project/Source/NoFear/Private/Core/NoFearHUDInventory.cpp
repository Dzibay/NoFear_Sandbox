// Кейс героя в HUD (Docs/Inventory_Design.md): сообщения о подборе и отладочная мини-сетка (NoFear.Inv.Debug).
// Экран кейса и колесо предметов — этапы 2 и 3. Остальной HUD — NoFearHUD.cpp.

#include "Core/NoFearHUD.h"

#include "Engine/Canvas.h"
#include "GameFramework/Pawn.h"
#include "HAL/IConsoleManager.h"
#include "Inventory/NoFearInventoryComponent.h"
#include "Inventory/NoFearItemData.h"
#include "Weapons/NoFearWeapon.h"

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
	if (!Case)
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
