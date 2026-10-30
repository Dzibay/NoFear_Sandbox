#include "Core/NoFearHUD.h"

#include "Camera/PlayerCameraManager.h"
#include "Core/NoFearPlayerController.h"
#include "Core/NoFearTeam.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "CanvasItem.h"
#include "Engine/Font.h"
#include "Math/RotationMatrix.h"
#include "RenderResource.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Weapons/NoFearWeapon.h"
#include "Weapons/NoFearWeaponComponent.h"
#include "Weapons/NoFearWeaponData.h"

namespace
{
	/** Скорость, с которой плавная полоска здоровья догоняет настоящую. */
	constexpr float HealthInterpSpeed = 12.f;
	/** Доля здоровья в секунду, на которую «хвост» догоняет полоску. */
	constexpr float GhostDrainSpeed = 0.45f;
	/** Сколько секунд затухает красная вспышка после удара. */
	constexpr float DamageFlashFadeTime = 0.8f;
	constexpr int32 MaxDamageIndicators = 8;

	/**
	 * Рисует треугольники с цветом и прозрачностью в вершинах.
	 * UCanvas::K2_DrawTriangle делает это в режиме Opaque и теряет альфу, поэтому элемент собираем сами.
	 */
	void DrawTranslucentTriangles(UCanvas* Canvas, const TArray<FCanvasUVTri>& Triangles)
	{
		FCanvasTriangleItem Item(FVector2D::ZeroVector, FVector2D::ZeroVector, FVector2D::ZeroVector, GWhiteTexture);
		Item.TriangleList = Triangles;
		Item.BlendMode = SE_BLEND_Translucent;
		Canvas->DrawItem(Item);
	}
}

ANoFearHUD::ANoFearHUD()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;
}

float ANoFearHUD::GetUIScale() const
{
	// Размеры заданы для 1080p; на других разрешениях HUD остаётся того же вида.
	return Canvas ? Canvas->SizeY / 1080.f : 1.f;
}

UFont* ANoFearHUD::GetFont() const
{
	if (HudFont)
	{
		return HudFont;
	}
	return GEngine ? GEngine->GetMediumFont() : nullptr;
}

// ---------------------------------------------------------------------------
// Состояние
// ---------------------------------------------------------------------------

bool ANoFearHUD::IsOwnerDead() const
{
	const ANoFearPlayerController* PlayerController = Cast<ANoFearPlayerController>(GetOwningPlayerController());
	if (PlayerController && PlayerController->IsWaitingForRespawn())
	{
		return true;
	}
	return UNoFearHealthComponent::IsActorDead(GetOwningPawn());
}

float ANoFearHUD::GetHitMarkerAlpha() const
{
	const float Duration = bHitKill ? KillMarkerDuration : HitMarkerDuration;
	return FMath::Clamp(1.f - HitMarkerAge / Duration, 0.f, 1.f);
}

float ANoFearHUD::GetDeathScreenAlpha() const
{
	if (!IsOwnerDead() || !GetWorld())
	{
		return 0.f;
	}
	return FMath::Clamp(static_cast<float>(GetWorld()->GetTimeSeconds() - DeathTime) / DeathScreenFadeTime, 0.f, 1.f);
}

float ANoFearHUD::GetLowHealthAlpha() const
{
	const UNoFearHealthComponent* Health = BoundHealth.Get();
	if (!Health || Health->IsDead() || LowHealthFraction <= 0.f)
	{
		return 0.f;
	}
	const float Fraction = Health->GetHealthFraction();
	if (Fraction >= LowHealthFraction)
	{
		return 0.f;
	}

	// Чем меньше здоровья, тем сильнее и чаще пульс.
	const float Severity = 1.f - Fraction / LowHealthFraction;
	const float Time = GetWorld() ? static_cast<float>(GetWorld()->GetTimeSeconds()) : 0.f;
	const float Pulse = 0.75f + 0.25f * FMath::Sin(Time * (4.f + 5.f * Severity));
	return 0.6f * Severity * Pulse;
}

void ANoFearHUD::EnsureBindings()
{
	APlayerController* PC = GetOwningPlayerController();

	ANoFearPlayerController* PlayerController = Cast<ANoFearPlayerController>(PC);
	if (PlayerController != BoundController.Get())
	{
		if (ANoFearPlayerController* Old = BoundController.Get())
		{
			Old->OnHitConfirmed.RemoveDynamic(this, &ANoFearHUD::HandleHitConfirmed);
			Old->OnPlayerDied.RemoveDynamic(this, &ANoFearHUD::HandlePlayerDied);
		}
		BoundController = PlayerController;
		if (PlayerController)
		{
			PlayerController->OnHitConfirmed.AddUniqueDynamic(this, &ANoFearHUD::HandleHitConfirmed);
			PlayerController->OnPlayerDied.AddUniqueDynamic(this, &ANoFearHUD::HandlePlayerDied);
		}
	}

	// Пешка меняется при возрождении: переподписываемся на здоровье новой.
	UNoFearHealthComponent* Health = UNoFearHealthComponent::FindHealth(PC ? PC->GetPawn() : nullptr);
	if (Health != BoundHealth.Get())
	{
		if (UNoFearHealthComponent* Old = BoundHealth.Get())
		{
			Old->OnDamaged.RemoveDynamic(this, &ANoFearHUD::HandleDamaged);
		}
		BoundHealth = Health;
		DamageIndicators.Reset();
		DamageFlash = 0.f;
		if (Health)
		{
			Health->OnDamaged.AddUniqueDynamic(this, &ANoFearHUD::HandleDamaged);
			DisplayedHealth = GhostHealth = Health->GetHealthFraction();
		}
	}
}

void ANoFearHUD::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	EnsureBindings();

	const UWorld* World = GetWorld();
	const double Now = World ? World->GetTimeSeconds() : 0.0;

	if (const UNoFearHealthComponent* Health = BoundHealth.Get())
	{
		const float Target = Health->GetHealthFraction();
		DisplayedHealth = FMath::FInterpTo(DisplayedHealth, Target, DeltaSeconds, HealthInterpSpeed);
		if (Target >= GhostHealth)
		{
			GhostHealth = Target;
		}
		else if (Now - LastDamageTime > GhostHoldTime)
		{
			GhostHealth = FMath::Max(Target, GhostHealth - GhostDrainSpeed * DeltaSeconds);
		}
	}

	HitMarkerAge += DeltaSeconds;
	DamageFlash = FMath::Max(0.f, DamageFlash - DeltaSeconds / DamageFlashFadeTime);

	for (FDamageIndicator& Indicator : DamageIndicators)
	{
		Indicator.Age += DeltaSeconds;
	}
	DamageIndicators.RemoveAll([this](const FDamageIndicator& Indicator) { return Indicator.Age >= DamageIndicatorDuration; });

	// Респаун не должен оставлять старое состояние смерти.
	const bool bDead = IsOwnerDead();
	if (bWasDead && !bDead)
	{
		DeathTime = -1.0e9;
	}
	bWasDead = bDead;
}

void ANoFearHUD::HandleHitConfirmed(AActor* Victim, float Damage, bool bHeadshot, bool bKilled)
{
	// Несколько попаданий подряд (дробь) складываются в один маркер: убийство и голова не пропадают.
	const bool bMarkerStillVisible = GetHitMarkerAlpha() > 0.f;
	bHitHeadshot = bHeadshot || (bMarkerStillVisible && bHitHeadshot);
	bHitKill = bKilled || (bMarkerStillVisible && bHitKill);
	HitMarkerAge = 0.f;
}

void ANoFearHUD::HandleDamaged(UNoFearHealthComponent* Component, const FNoFearDamageInfo& Info)
{
	LastDamageTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;

	const float MaxHealth = Component ? FMath::Max(Component->GetMaxHealth(), 1.f) : 100.f;
	DamageFlash = FMath::Clamp(DamageFlash + 0.25f + Info.Amount / MaxHealth, 0.f, 1.f);

	if (!Info.bHasSource)
	{
		return;
	}

	// Дробь и очередь от одного стрелка обновляют один указатель, а не плодят новые.
	for (FDamageIndicator& Indicator : DamageIndicators)
	{
		if (FVector::DistSquared(Indicator.SourceLocation, Info.SourceLocation) < FMath::Square(150.f))
		{
			Indicator.SourceLocation = Info.SourceLocation;
			Indicator.Age = 0.f;
			return;
		}
	}
	if (DamageIndicators.Num() >= MaxDamageIndicators)
	{
		DamageIndicators.RemoveAt(0);
	}
	FDamageIndicator& NewIndicator = DamageIndicators.AddDefaulted_GetRef();
	NewIndicator.SourceLocation = Info.SourceLocation;
}

void ANoFearHUD::HandlePlayerDied(const FNoFearDamageInfo& Info)
{
	DeathTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
	bWasDead = true;
	DamageFlash = 1.f;
}

// ---------------------------------------------------------------------------
// Отрисовка
// ---------------------------------------------------------------------------

void ANoFearHUD::DrawHUD()
{
	Super::DrawHUD();

	if (!Canvas)
	{
		return;
	}

	const bool bDead = IsOwnerDead();

	if (bShowVignette)
	{
		DrawVignette();
	}
	if (bShowDamageIndicators && !bDead)
	{
		DrawDamageIndicators();
	}

	if (!bDead)
	{
		const APawn* Pawn = GetOwningPawn();
		const UNoFearWeaponComponent* WeaponComponent = Pawn ? Pawn->FindComponentByClass<UNoFearWeaponComponent>() : nullptr;
		ANoFearWeapon* Weapon = WeaponComponent ? WeaponComponent->GetCurrentWeapon() : nullptr;

		// Оружие убрано (или ещё не выдано) — прицел и патроны не нужны.
		const bool bArmed = Weapon && Weapon->GetWeaponData() && Weapon->GetWeaponState() != ENoFearWeaponState::Holstered;
		if (bArmed && bShowCrosshair)
		{
			DrawCrosshair(Weapon);
		}
		else
		{
			CurrentGap = -1.f;
		}

		if (bShowHitMarker)
		{
			DrawHitMarker();
		}
		if (bArmed && bShowAmmo)
		{
			DrawAmmo(Weapon);
		}
		if (bShowWeaponSlots)
		{
			DrawWeaponSlots();
		}
	}

	if (bShowHealth)
	{
		DrawHealth();
	}
	if (bShowDeathScreen && bDead)
	{
		DrawDeathScreen();
	}
}

void ANoFearHUD::DrawLabel(const FString& Text, float X, float Y, const FLinearColor& Color, float Scale, ELabelAlign Align)
{
	UFont* Font = GetFont();
	float Width = 0.f;
	float Height = 0.f;
	GetTextSize(Text, Width, Height, Font, Scale);

	if (Align == ELabelAlign::Right)
	{
		X -= Width;
	}
	else if (Align == ELabelAlign::Center)
	{
		X -= Width * 0.5f;
	}
	X = FMath::RoundToFloat(X);
	Y = FMath::RoundToFloat(Y);

	const float Shadow = FMath::Max(1.f, FMath::RoundToFloat(GetUIScale()));
	DrawText(Text, FLinearColor(0.f, 0.f, 0.f, Color.A * 0.75f), X + Shadow, Y + Shadow, Font, Scale);
	DrawText(Text, Color, X, Y, Font, Scale);
}

void ANoFearHUD::DrawBar(float X, float Y, float Width, float Height, float Fraction, const FLinearColor& Fill, float GhostFraction)
{
	const float Border = FMath::Max(1.f, FMath::RoundToFloat(GetUIScale() * 2.f));
	Fraction = FMath::Clamp(Fraction, 0.f, 1.f);

	DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.6f), X - Border, Y - Border, Width + Border * 2.f, Height + Border * 2.f);
	DrawRect(FLinearColor(0.12f, 0.12f, 0.12f, 0.55f), X, Y, Width, Height);
	if (GhostFraction > Fraction)
	{
		DrawRect(FLinearColor(1.f, 1.f, 1.f, 0.55f), X, Y, Width * FMath::Clamp(GhostFraction, 0.f, 1.f), Height);
	}
	if (Fraction > 0.f)
	{
		DrawRect(Fill, X, Y, Width * Fraction, Height);
	}
}

void ANoFearHUD::DrawColoredQuad(const FVector2D& A, const FVector2D& B, const FVector2D& C, const FVector2D& D, const FLinearColor& ColorA, const FLinearColor& ColorB, const FLinearColor& ColorC, const FLinearColor& ColorD)
{
	TArray<FCanvasUVTri> Triangles;
	Triangles.SetNum(2);

	auto SetVertex = [](FCanvasUVTri& Tri, int32 Index, const FVector2D& Pos, const FLinearColor& Color)
	{
		switch (Index)
		{
		case 0: Tri.V0_Pos = Pos; Tri.V0_Color = Color; break;
		case 1: Tri.V1_Pos = Pos; Tri.V1_Color = Color; break;
		default: Tri.V2_Pos = Pos; Tri.V2_Color = Color; break;
		}
	};

	SetVertex(Triangles[0], 0, A, ColorA);
	SetVertex(Triangles[0], 1, B, ColorB);
	SetVertex(Triangles[0], 2, C, ColorC);
	SetVertex(Triangles[1], 0, A, ColorA);
	SetVertex(Triangles[1], 1, C, ColorC);
	SetVertex(Triangles[1], 2, D, ColorD);

	DrawTranslucentTriangles(Canvas, Triangles);
}

void ANoFearHUD::DrawHealth()
{
	const UNoFearHealthComponent* Health = BoundHealth.Get();
	if (!Health)
	{
		return;
	}

	const float S = GetUIScale();
	const float Fraction = DisplayedHealth;
	const bool bLow = Fraction < LowHealthFraction && !Health->IsDead();

	const float BarWidth = 320.f * S;
	const float BarHeight = 16.f * S;
	const float X = Margin * S;
	const float BarY = Canvas->SizeY - Margin * S - BarHeight;

	// Зелёный -> красный по мере потери здоровья.
	FLinearColor FillColor = FMath::Lerp(LowHealthColor, HealthColor, FMath::Clamp(Fraction / 0.6f, 0.f, 1.f));
	float Pulse = 1.f;
	if (bLow)
	{
		const float Time = GetWorld() ? static_cast<float>(GetWorld()->GetTimeSeconds()) : 0.f;
		Pulse = 0.7f + 0.3f * FMath::Sin(Time * 8.f);
	}
	FillColor.A *= Pulse;

	DrawBar(X, BarY, BarWidth, BarHeight, Fraction, FillColor, GhostHealth);

	const FString Number = FString::FromInt(FMath::CeilToInt(Health->GetHealth()));
	const FString MaxText = FString::Printf(TEXT("/ %d"), FMath::RoundToInt(Health->GetMaxHealth()));
	const float NumberScale = 2.2f * S;
	const float MaxScale = 1.1f * S;

	float NumberWidth = 0.f;
	float NumberHeight = 0.f;
	float MaxHeight = 0.f;
	float Unused = 0.f;
	GetTextSize(Number, NumberWidth, NumberHeight, GetFont(), NumberScale);
	GetTextSize(MaxText, Unused, MaxHeight, GetFont(), MaxScale);

	const float NumberY = BarY - 10.f * S - NumberHeight;
	FLinearColor NumberColor = bLow ? LowHealthColor : FLinearColor::White;
	NumberColor.A *= Pulse;
	DrawLabel(Number, X, NumberY, NumberColor, NumberScale, ELabelAlign::Left);
	DrawLabel(MaxText, X + NumberWidth + 8.f * S, NumberY + NumberHeight - MaxHeight - 2.f * S, FLinearColor(0.8f, 0.8f, 0.8f, 0.8f), MaxScale, ELabelAlign::Left);
}

void ANoFearHUD::DrawAmmo(ANoFearWeapon* Weapon)
{
	const UNoFearWeaponData* Data = Weapon->GetWeaponData();
	const float S = GetUIScale();
	const float Right = Canvas->SizeX - Margin * S;
	const float Bottom = Canvas->SizeY - Margin * S;

	const int32 Magazine = Weapon->GetMagazineAmmo();
	const int32 Reserve = Weapon->GetReserveAmmo();
	const ENoFearWeaponState State = Weapon->GetWeaponState();
	const bool bReloading = State == ENoFearWeaponState::Reloading;

	// Нижняя строка: полоска перезарядки или подсказка. Место под неё занято всегда, чтобы остальное не прыгало.
	const float StatusScale = 0.95f * S;
	float Unused = 0.f;
	float StatusHeight = 0.f;
	GetTextSize(TEXT("A"), Unused, StatusHeight, GetFont(), StatusScale);
	const float ReloadBarHeight = 5.f * S;
	const float StatusY = Bottom - ReloadBarHeight - 4.f * S - StatusHeight;

	if (bReloading)
	{
		DrawLabel(ReloadingText.ToString(), Right, StatusY, FLinearColor(1.f, 0.85f, 0.4f, 1.f), StatusScale, ELabelAlign::Right);
		const float BarWidth = 220.f * S;
		DrawBar(Right - BarWidth, Bottom - ReloadBarHeight, BarWidth, ReloadBarHeight, Weapon->GetReloadProgress(), FLinearColor(1.f, 0.85f, 0.4f, 1.f));
	}
	else if (State == ENoFearWeaponState::Idle && Magazine <= 0)
	{
		const bool bHasReserve = Reserve > 0;
		DrawLabel((bHasReserve ? ReloadHintText : NoAmmoText).ToString(), Right, StatusY, FLinearColor(1.f, 0.3f, 0.2f, 1.f), StatusScale, ELabelAlign::Right);
	}

	// Магазин крупно, запас мельче справа от него.
	const FString MagazineText = FString::FromInt(Magazine);
	const FString ReserveText = FString::Printf(TEXT("/ %d"), Reserve);
	const float MagazineScale = 3.f * S;
	const float ReserveScale = 1.5f * S;

	float MagazineWidth = 0.f;
	float MagazineHeight = 0.f;
	float ReserveWidth = 0.f;
	float ReserveHeight = 0.f;
	GetTextSize(MagazineText, MagazineWidth, MagazineHeight, GetFont(), MagazineScale);
	GetTextSize(ReserveText, ReserveWidth, ReserveHeight, GetFont(), ReserveScale);

	const float MagazineY = StatusY - 6.f * S - MagazineHeight;
	const bool bLowAmmo = Data->MagazineSize > 0 && Magazine <= FMath::Max(1, Data->MagazineSize / 4);
	const FLinearColor MagazineColor = Magazine <= 0 ? FLinearColor(1.f, 0.25f, 0.2f, 1.f) : (bLowAmmo ? FLinearColor(1.f, 0.7f, 0.25f, 1.f) : FLinearColor::White);

	DrawLabel(ReserveText, Right, MagazineY + MagazineHeight - ReserveHeight - 3.f * S, FLinearColor(0.8f, 0.8f, 0.8f, 0.85f), ReserveScale, ELabelAlign::Right);
	DrawLabel(MagazineText, Right - ReserveWidth - 12.f * S, MagazineY, MagazineColor, MagazineScale, ELabelAlign::Right);

	// Имя оружия над счётчиком.
	const FString Name = Data->DisplayName.IsEmpty() ? Data->GetName() : Data->DisplayName.ToString();
	const float NameScale = 1.2f * S;
	float NameHeight = 0.f;
	GetTextSize(Name, Unused, NameHeight, GetFont(), NameScale);
	DrawLabel(Name, Right, MagazineY - 4.f * S - NameHeight, FLinearColor(0.85f, 0.85f, 0.85f, 0.9f), NameScale, ELabelAlign::Right);
}

void ANoFearHUD::DrawWeaponSlots()
{
	const APawn* Pawn = GetOwningPawn();
	const UNoFearWeaponComponent* WeaponComponent = Pawn ? Pawn->FindComponentByClass<UNoFearWeaponComponent>() : nullptr;
	if (!WeaponComponent)
	{
		return;
	}

	const TArray<ANoFearWeapon*> Weapons = WeaponComponent->GetWeapons();
	if (Weapons.Num() == 0)
	{
		return;
	}

	const float S = GetUIScale();
	const float SlotWidth = 150.f * S;
	const float SlotHeight = 34.f * S;
	const float Gap = 8.f * S;
	const float TotalWidth = Weapons.Num() * SlotWidth + (Weapons.Num() - 1) * Gap;
	const float StartX = Canvas->SizeX * 0.5f - TotalWidth * 0.5f;
	const float Y = Canvas->SizeY - Margin * S - SlotHeight;
	const int32 Selected = WeaponComponent->GetTargetWeaponIndex();

	const float TextScale = 0.95f * S;
	float Unused = 0.f;
	float TextHeight = 0.f;
	GetTextSize(TEXT("A"), Unused, TextHeight, GetFont(), TextScale);

	for (int32 Index = 0; Index < Weapons.Num(); ++Index)
	{
		const ANoFearWeapon* Weapon = Weapons[Index];
		const UNoFearWeaponData* Data = Weapon ? Weapon->GetWeaponData() : nullptr;
		const bool bSelected = Index == Selected;
		const float X = StartX + Index * (SlotWidth + Gap);

		DrawRect(bSelected ? FLinearColor(1.f, 1.f, 1.f, 0.85f) : FLinearColor(0.f, 0.f, 0.f, 0.5f), X - S * 2.f, Y - S * 2.f, SlotWidth + S * 4.f, SlotHeight + S * 4.f);
		DrawRect(bSelected ? FLinearColor(0.1f, 0.1f, 0.1f, 0.8f) : FLinearColor(0.08f, 0.08f, 0.08f, 0.55f), X, Y, SlotWidth, SlotHeight);

		const FString Name = !Data ? FString() : (Data->DisplayName.IsEmpty() ? Data->GetName() : Data->DisplayName.ToString());
		const FString Label = FString::Printf(TEXT("%d  %s"), Index + 1, *Name);
		const float Alpha = bSelected ? 1.f : 0.6f;
		DrawLabel(Label, X + 10.f * S, Y + (SlotHeight - TextHeight) * 0.5f, FLinearColor(1.f, 1.f, 1.f, Alpha), TextScale, ELabelAlign::Left);
	}
}

void ANoFearHUD::DrawCrosshair(ANoFearWeapon* Weapon)
{
	const APlayerController* PC = GetOwningPlayerController();
	if (!PC || !PC->PlayerCameraManager)
	{
		return;
	}

	const float UIScale = GetUIScale();

	// Разброс в градусах -> пиксели: сколько пикселей занимает тангенс угла на этом FOV.
	const float HalfFov = FMath::DegreesToRadians(FMath::Clamp(PC->PlayerCameraManager->GetFOVAngle(), 10.f, 170.f) * 0.5f);
	const float PixelsPerTan = Canvas->SizeX * 0.5f / FMath::Tan(HalfFov);
	const float SpreadRad = FMath::DegreesToRadians(Weapon->GetCurrentSpread());
	const float TargetGap = FMath::Clamp(FMath::Tan(SpreadRad) * PixelsPerTan * SpreadScale, MinGap * UIScale, Canvas->SizeY * 0.25f);

	const float DeltaTime = GetWorld() ? GetWorld()->GetDeltaSeconds() : 0.f;
	CurrentGap = CurrentGap < 0.f ? TargetGap : FMath::FInterpTo(CurrentGap, TargetGap, DeltaTime, GapInterpSpeed);

	FLinearColor Color = IsEnemyUnderCrosshair(Weapon) ? EnemyColor : DefaultColor;
	const ENoFearWeaponState State = Weapon->GetWeaponState();
	if (State == ENoFearWeaponState::Reloading || State == ENoFearWeaponState::Equipping || State == ENoFearWeaponState::Holstering)
	{
		Color.A *= BusyOpacity;
	}
	const FLinearColor Outline(0.f, 0.f, 0.f, Color.A * 0.6f);

	const float CenterX = FMath::RoundToFloat(Canvas->SizeX * 0.5f);
	const float CenterY = FMath::RoundToFloat(Canvas->SizeY * 0.5f);
	const float Thickness = FMath::Max(FMath::RoundToFloat(BarThickness * UIScale), 1.f);
	const float Length = FMath::Max(FMath::RoundToFloat(BarLength * UIScale), 1.f);
	const float Gap = FMath::RoundToFloat(CurrentGap);
	const float Border = 1.f;

	// Чёрточка = обводка (чуть больше) + сама чёрточка, чтобы прицел читался на любом фоне.
	auto DrawCrosshairBar = [&](float X, float Y, float Width, float Height)
	{
		DrawRect(Outline, X - Border, Y - Border, Width + Border * 2.f, Height + Border * 2.f);
		DrawRect(Color, X, Y, Width, Height);
	};

	const float Half = Thickness * 0.5f;
	DrawCrosshairBar(CenterX - Half, CenterY - Gap - Length, Thickness, Length); // верх
	DrawCrosshairBar(CenterX - Half, CenterY + Gap, Thickness, Length);          // низ
	DrawCrosshairBar(CenterX - Gap - Length, CenterY - Half, Length, Thickness); // лево
	DrawCrosshairBar(CenterX + Gap, CenterY - Half, Length, Thickness);          // право
	DrawCrosshairBar(CenterX - Half, CenterY - Half, Thickness, Thickness);      // точка в центре
}

bool ANoFearHUD::IsEnemyUnderCrosshair(const ANoFearWeapon* Weapon) const
{
	const APlayerController* PC = GetOwningPlayerController();
	const UNoFearWeaponData* Data = Weapon->GetWeaponData();
	if (!PC || !Data || !GetWorld())
	{
		return false;
	}

	FVector CameraLocation;
	FRotator CameraRotation;
	PC->GetPlayerViewPoint(CameraLocation, CameraRotation);
	const FVector CameraForward = CameraRotation.Vector();

	// Тот же луч, что и у выстрела (ANoFearWeapon::ComputeAim): от уровня персонажа вперёд.
	const AActor* Pawn = PC->GetPawn();
	const FVector Pivot = Pawn ? Pawn->GetActorLocation() : CameraLocation;
	const FVector TraceStart = CameraLocation + CameraForward * FMath::Max(FVector::DotProduct(Pivot - CameraLocation, CameraForward), 0.f);
	const FVector TraceEnd = TraceStart + CameraForward * Data->Range;

	FCollisionQueryParams Params(SCENE_QUERY_STAT(NoFearCrosshair), true, Weapon);
	Params.AddIgnoredActor(Pawn);

	FHitResult Hit;
	if (!GetWorld()->LineTraceSingleByChannel(Hit, TraceStart, TraceEnd, Data->TraceChannel, Params))
	{
		return false;
	}
	// Труп врагом не считается.
	return UNoFearTeamLibrary::AreHostile(Pawn, Hit.GetActor()) && !UNoFearHealthComponent::IsActorDead(Hit.GetActor());
}

void ANoFearHUD::DrawHitMarker()
{
	const float Alpha = GetHitMarkerAlpha();
	if (Alpha <= 0.f)
	{
		return;
	}

	const float S = GetUIScale();
	FLinearColor Color = bHitKill ? KillMarkerColor : (bHitHeadshot ? HeadshotMarkerColor : HitMarkerColor);
	Color.A *= Alpha;
	const FLinearColor Outline(0.f, 0.f, 0.f, Color.A * 0.7f);

	// Убийство — крупнее и сначала «выпрыгивает».
	const float Pop = bHitKill ? 1.35f + 0.25f * Alpha : 1.f;
	const float Inner = 8.f * S * Pop;
	const float Outer = 17.f * S * Pop;
	const float Thickness = (bHitKill ? 3.f : 2.f) * S;

	const float CenterX = Canvas->SizeX * 0.5f;
	const float CenterY = Canvas->SizeY * 0.5f;
	const float Diagonal = 0.70710678f;

	for (int32 Sx = -1; Sx <= 1; Sx += 2)
	{
		for (int32 Sy = -1; Sy <= 1; Sy += 2)
		{
			const float X0 = CenterX + Sx * Inner * Diagonal;
			const float Y0 = CenterY + Sy * Inner * Diagonal;
			const float X1 = CenterX + Sx * Outer * Diagonal;
			const float Y1 = CenterY + Sy * Outer * Diagonal;
			DrawLine(X0, Y0, X1, Y1, Outline, Thickness + 2.f * S);
			DrawLine(X0, Y0, X1, Y1, Color, Thickness);
		}
	}
}

void ANoFearHUD::DrawDamageIndicators()
{
	const APlayerController* PC = GetOwningPlayerController();
	const APawn* Pawn = PC ? PC->GetPawn() : nullptr;
	if (!Pawn || DamageIndicators.Num() == 0)
	{
		return;
	}

	FVector CameraLocation;
	FRotator CameraRotation;
	PC->GetPlayerViewPoint(CameraLocation, CameraRotation);
	const FRotationMatrix YawMatrix(FRotator(0.f, CameraRotation.Yaw, 0.f));
	const FVector Forward = YawMatrix.GetScaledAxis(EAxis::X);
	const FVector Right = YawMatrix.GetScaledAxis(EAxis::Y);

	const float S = GetUIScale();
	const FVector2D Center(Canvas->SizeX * 0.5f, Canvas->SizeY * 0.5f);
	const float Radius = Canvas->SizeY * DamageIndicatorRadius;
	const float Length = 46.f * S;
	const float HalfWidth = 26.f * S;

	for (const FDamageIndicator& Indicator : DamageIndicators)
	{
		FVector ToSource = Indicator.SourceLocation - Pawn->GetActorLocation();
		ToSource.Z = 0.f;
		if (ToSource.IsNearlyZero(1.f))
		{
			continue;
		}

		// Угол от направления взгляда: 0 — спереди (вверх по экрану), вправо — по часовой.
		const float Angle = FMath::Atan2(FVector::DotProduct(ToSource, Right), FVector::DotProduct(ToSource, Forward));
		const FVector2D Direction(FMath::Sin(Angle), -FMath::Cos(Angle));
		const FVector2D Side(-Direction.Y, Direction.X);

		const float Fade = 1.f - Indicator.Age / DamageIndicatorDuration;
		const float Alpha = FMath::Clamp(FMath::Sqrt(FMath::Max(Fade, 0.f)), 0.f, 1.f);
		FLinearColor Base = DamageColor;
		Base.A = 0.85f * Alpha;
		FLinearColor Tip = DamageColor;
		Tip.A = 0.f;

		// Клин острием наружу, к стрелявшему; к острию прозрачнее.
		const FVector2D BaseCenter = Center + Direction * Radius;
		const FVector2D TipPoint = Center + Direction * (Radius + Length);
		const FVector2D BaseA = BaseCenter + Side * HalfWidth;
		const FVector2D BaseB = BaseCenter - Side * HalfWidth;

		TArray<FCanvasUVTri> Triangles;
		Triangles.SetNum(1);
		FCanvasUVTri& Tri = Triangles[0];
		Tri.V0_Pos = BaseA;
		Tri.V0_Color = Base;
		Tri.V1_Pos = BaseB;
		Tri.V1_Color = Base;
		Tri.V2_Pos = TipPoint;
		Tri.V2_Color = Tip;
		DrawTranslucentTriangles(Canvas, Triangles);
	}
}

void ANoFearHUD::DrawVignette()
{
	float Alpha = FMath::Max(GetLowHealthAlpha(), DamageFlash * 0.55f);
	if (IsOwnerDead())
	{
		Alpha = FMath::Max(Alpha, 0.6f * GetDeathScreenAlpha());
	}
	if (Alpha <= 0.01f)
	{
		return;
	}

	FLinearColor Outer = DamageColor * 0.75f;
	Outer.A = FMath::Clamp(Alpha, 0.f, 0.8f);
	FLinearColor Inner = Outer;
	Inner.A = 0.f;

	const float W = Canvas->SizeX;
	const float H = Canvas->SizeY;
	const float T = H * 0.24f;

	DrawColoredQuad(FVector2D(0.f, 0.f), FVector2D(W, 0.f), FVector2D(W, T), FVector2D(0.f, T), Outer, Outer, Inner, Inner);       // верх
	DrawColoredQuad(FVector2D(0.f, H), FVector2D(W, H), FVector2D(W, H - T), FVector2D(0.f, H - T), Outer, Outer, Inner, Inner);  // низ
	DrawColoredQuad(FVector2D(0.f, 0.f), FVector2D(0.f, H), FVector2D(T, H), FVector2D(T, 0.f), Outer, Outer, Inner, Inner);       // лево
	DrawColoredQuad(FVector2D(W, 0.f), FVector2D(W, H), FVector2D(W - T, H), FVector2D(W - T, 0.f), Outer, Outer, Inner, Inner);  // право
}

void ANoFearHUD::DrawDeathScreen()
{
	const float Alpha = GetDeathScreenAlpha();
	if (Alpha <= 0.f)
	{
		return;
	}

	const float S = GetUIScale();
	DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.55f * Alpha), 0.f, 0.f, Canvas->SizeX, Canvas->SizeY);

	const float TitleScale = 3.4f * S;
	float Unused = 0.f;
	float TitleHeight = 0.f;
	GetTextSize(DeadText.ToString(), Unused, TitleHeight, GetFont(), TitleScale);

	const float CenterX = Canvas->SizeX * 0.5f;
	const float TitleY = Canvas->SizeY * 0.38f;
	DrawLabel(DeadText.ToString(), CenterX, TitleY, FLinearColor(0.9f, 0.12f, 0.08f, Alpha), TitleScale, ELabelAlign::Center);

	const ANoFearPlayerController* PlayerController = Cast<ANoFearPlayerController>(GetOwningPlayerController());
	if (PlayerController && PlayerController->bAutoRespawn)
	{
		const int32 Seconds = FMath::CeilToInt(PlayerController->GetRespawnTimeRemaining());
		const FText Countdown = FText::Format(RespawnInText, FText::AsNumber(Seconds));
		DrawLabel(Countdown.ToString(), CenterX, TitleY + TitleHeight + 16.f * S, FLinearColor(1.f, 1.f, 1.f, Alpha), 1.5f * S, ELabelAlign::Center);
	}
}
