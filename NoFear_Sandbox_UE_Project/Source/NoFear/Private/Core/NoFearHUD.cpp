#include "Core/NoFearHUD.h"
#include "Core/NoFearAISettings.h"
#include "Core/NoFearPerf.h"

#include "AI/NoFearAIController.h"
#include "AI/NoFearBotSubsystem.h"
#include "AI/NoFearCompanionAIController.h"
#include "AI/NoFearMonsterAIController.h"
#include "AI/NoFearShooterAIController.h"
#include "AI/NoFearSquadSubsystem.h"
#include "AI/NoFearRecruitableComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/CapsuleComponent.h"
#include "Core/NoFearCharacter.h"
#include "Core/NoFearFearComponent.h"
#include "Core/NoFearFearSubsystem.h"
#include "Core/NoFearInteractableComponent.h"
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
#include "HAL/IConsoleManager.h"
#include "Weapons/NoFearGrenade.h"
#include "Weapons/NoFearGrenadeComponent.h"
#include "Weapons/NoFearGrenadeData.h"
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
	/** Как часто пересчитывается прямая видимость панели над ботом. */
	constexpr double BotVisibilityInterval = 0.2;

	TAutoConsoleVariable<int32> CVarBotIndicators(
		TEXT("NoFear.HUD.BotIndicators"), 1,
		TEXT("Панель над ботами (здоровье, «?» и «!»): 0 — выключить, 1 — по правилам, 2 — показывать у всех, даже сквозь стены."),
		ECVF_Default);

	TAutoConsoleVariable<bool> CVarShowEnemyLoadout(
		TEXT("NoFear.HUD.ShowEnemyLoadout"), false,
		TEXT("Показывать запас гранат над врагами (как ANoFearHUD::bShowEnemyLoadout)."),
		ECVF_Default);

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
	DrawDownedScreen();
	if (bShowDamageIndicators && !bDead)
	{
		DrawDamageIndicators();
	}

	DrawnPortalLabels = DrawnFeederRings = DrawnBreakRings = 0;
	bPortalFreeDrawn = false;
	if (!bDead)
	{
		// Метки в мире — под прицелом и остальным HUD.
		if (bShowPortals)
		{
			DrawPortals();
		}
		if (bShowBotIndicators)
		{
			DrawBotIndicators();
		}
		if (bShowRecruitableMarkers)
		{
			DrawRecruitableMarkers();
		}
		if (bShowCompanionMarkers)
		{
			DrawCompanionMarkers();
		}
		if (bShowCompanions)
		{
			DrawCompanionPanel();
		}
		if (bShowInteractionPrompt)
		{
			DrawInteractionPrompt();
		}
		if (bShowGrenades)
		{
			DrawGrenadeDanger();
			DrawGrenadeAim();
		}

		const APawn* Pawn = GetOwningPawn();
		const UNoFearWeaponComponent* WeaponComponent = Pawn ? Pawn->FindComponentByClass<UNoFearWeaponComponent>() : nullptr;
		ANoFearWeapon* Weapon = WeaponComponent ? WeaponComponent->GetCurrentWeapon() : nullptr;
		const ANoFearCharacter* PawnCharacter = Cast<ANoFearCharacter>(Pawn);
		const UNoFearGrenadeComponent* Grenades = PawnCharacter ? PawnCharacter->GetGrenadeComponent() : nullptr;
		const bool bGrenadeAiming = Grenades && Grenades->IsAiming();

		// Оружие убрано (или ещё не выдано) — прицел и патроны не нужны. Целясь гранатой, прицел заменяет дуга.
		const bool bArmed = Weapon && Weapon->GetWeaponData() && Weapon->GetWeaponState() != ENoFearWeaponState::Holstered;
		if (bArmed && bShowCrosshair && !bGrenadeAiming)
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
		float AmmoTop = Canvas->SizeY - Margin * GetUIScale();
		if (bArmed && bShowAmmo)
		{
			AmmoTop = DrawAmmo(Weapon);
		}
		if (bShowGrenades)
		{
			DrawGrenadeCounter(AmmoTop);
		}
		if (bShowWeaponSlots)
		{
			DrawWeaponSlots();
		}
		DrawReviveProgress();
		DrawCalmProgress();
		if (bShowPortals)
		{
			DrawPortalFreeProgress();
		}
	}

	if (bShowHealth)
	{
		DrawHealth();
	}
	bBraceletDrawn = false;
	bRiftWarningDrawn = false;
	if (bShowBracelet && !bDead)
	{
		DrawBracelet();
	}
	if (bShowRadio)
	{
		DrawRadio();
	}
	if (bShowDeathScreen && bDead)
	{
		DrawDeathScreen();
	}
	if (!bDead)
	{
		// Колесо — поверх всего.
		DrawOrderWheel();
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

void ANoFearHUD::DrawBar(float X, float Y, float Width, float Height, float Fraction, const FLinearColor& Fill, float GhostFraction, float Opacity)
{
	const float Border = FMath::Max(1.f, FMath::RoundToFloat(GetUIScale() * 2.f));
	Fraction = FMath::Clamp(Fraction, 0.f, 1.f);

	DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.6f * Opacity), X - Border, Y - Border, Width + Border * 2.f, Height + Border * 2.f);
	DrawRect(FLinearColor(0.12f, 0.12f, 0.12f, 0.55f * Opacity), X, Y, Width, Height);
	if (GhostFraction > Fraction)
	{
		DrawRect(FLinearColor(1.f, 1.f, 1.f, 0.55f * Opacity), X, Y, Width * FMath::Clamp(GhostFraction, 0.f, 1.f), Height);
	}
	if (Fraction > 0.f)
	{
		FLinearColor FillColor = Fill;
		FillColor.A *= Opacity;
		DrawRect(FillColor, X, Y, Width * Fraction, Height);
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

float ANoFearHUD::DrawAmmo(ANoFearWeapon* Weapon)
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
	return MagazineY - 4.f * S - NameHeight;
}

void ANoFearHUD::DrawGrenadeCounter(float Bottom)
{
	const APawn* Pawn = GetOwningPawn();
	const ANoFearCharacter* Character = Cast<ANoFearCharacter>(Pawn);
	const UNoFearGrenadeComponent* Grenades = Character ? Character->GetGrenadeComponent() : nullptr;
	if (!Grenades)
	{
		return;
	}

	// «◆ N» справа внизу, над счётчиком патронов. Ромб рисуем сами: в шрифте движка нет такого знака.
	const float S = GetUIScale();
	const float Right = Canvas->SizeX - Margin * S;
	const int32 Count = Grenades->GetGrenadeCount();
	const FString Text = FString::FromInt(Count);
	const float TextScale = 1.5f * S;
	float TextWidth = 0.f;
	float TextHeight = 0.f;
	GetTextSize(Text, TextWidth, TextHeight, GetFont(), TextScale);
	const float Y = Bottom - 8.f * S - TextHeight;

	FLinearColor Color = Count > 0 ? GrenadeColor : FLinearColor(0.6f, 0.6f, 0.6f, 0.6f);
	if (Grenades->IsAiming())
	{
		Color = FLinearColor::White;
	}
	DrawLabel(Text, Right, Y, Color, TextScale, ELabelAlign::Right);
	DrawDiamond(FVector2D(Right - TextWidth - 14.f * S, Y + TextHeight * 0.5f), 7.f * S, Color);
}

void ANoFearHUD::DrawWorldCircle(const FVector& Center, float Radius, const FLinearColor& Color, float Thickness)
{
	const APlayerController* PC = GetOwningPlayerController();
	if (!PC || !Canvas)
	{
		return;
	}
	FVector CameraLocation;
	FRotator CameraRotation;
	PC->GetPlayerViewPoint(CameraLocation, CameraRotation);

	constexpr int32 Segments = 40;
	FVector Previous = Center + FVector(Radius, 0.f, 0.f);
	for (int32 Index = 1; Index <= Segments; ++Index)
	{
		const float Angle = UE_TWO_PI * Index / Segments;
		const FVector Next = Center + FVector(Radius * FMath::Cos(Angle), Radius * FMath::Sin(Angle), 0.f);
		// Отрезки за камерой проецируются криво — пропускаем.
		if (CameraRotation.UnrotateVector(Previous - CameraLocation).X > 10.f && CameraRotation.UnrotateVector(Next - CameraLocation).X > 10.f)
		{
			const FVector A = Canvas->Project(Previous, false);
			const FVector B = Canvas->Project(Next, false);
			DrawLine(A.X, A.Y, B.X, B.Y, Color, Thickness);
		}
		Previous = Next;
	}
}

void ANoFearHUD::DrawGrenadeAim()
{
	APawn* Pawn = GetOwningPawn();
	const ANoFearCharacter* Character = Cast<ANoFearCharacter>(Pawn);
	const UNoFearGrenadeComponent* Grenades = Character ? Character->GetGrenadeComponent() : nullptr;
	const APlayerController* PC = GetOwningPlayerController();
	if (!Grenades || !Grenades->IsAiming() || !PC)
	{
		return;
	}

	TArray<FVector> Points;
	FVector Landing;
	if (!Grenades->PredictArc(Points, Landing) || Points.Num() < 2)
	{
		return;
	}

	FVector CameraLocation;
	FRotator CameraRotation;
	PC->GetPlayerViewPoint(CameraLocation, CameraRotation);
	const float S = GetUIScale();
	const float Thickness = FMath::Max(1.5f, 2.5f * S);

	// Дуга: к концу ярче, чтобы читалось, куда летит.
	for (int32 Index = 1; Index < Points.Num(); ++Index)
	{
		const FVector& From = Points[Index - 1];
		const FVector& To = Points[Index];
		if (CameraRotation.UnrotateVector(From - CameraLocation).X <= 10.f || CameraRotation.UnrotateVector(To - CameraLocation).X <= 10.f)
		{
			continue;
		}
		FLinearColor Color = GrenadeArcColor;
		Color.A *= FMath::Lerp(0.35f, 1.f, static_cast<float>(Index) / (Points.Num() - 1));
		const FVector A = Canvas->Project(From, false);
		const FVector B = Canvas->Project(To, false);
		DrawLine(A.X, A.Y, B.X, B.Y, Color, Thickness);
	}

	// Круг поражения в точке падения и ромб в центре.
	const FVector Ground = Landing + FVector(0.f, 0.f, 4.f);
	FLinearColor Ring = GrenadeDangerColor;
	Ring.A = 0.8f;
	DrawWorldCircle(Ground, Grenades->GetRadius(), FLinearColor(0.f, 0.f, 0.f, 0.45f), Thickness + 2.f * S);
	DrawWorldCircle(Ground, Grenades->GetRadius(), Ring, Thickness);
	DrawWorldMarker(Ground, GrenadeArcColor, 5.f * S, /*bEdgeArrow=*/false, FString());
}

void ANoFearHUD::DrawGrenadeDanger()
{
	const APawn* PlayerPawn = GetOwningPawn();
	const UNoFearBotSubsystem* Subsystem = UNoFearBotSubsystem::Get(this);
	if (!PlayerPawn || !Subsystem)
	{
		return;
	}

	const float S = GetUIScale();
	const double Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
	for (const ANoFearGrenade* Grenade : Subsystem->GetLiveGrenades())
	{
		if (!Grenade || Grenade->HasExploded())
		{
			continue;
		}
		const float WarningRadius = Grenade->Data ? Grenade->Data->HUDWarningRadius : 1000.f;
		const float Distance = FVector::Dist(PlayerPawn->GetActorLocation(), Grenade->GetActorLocation());
		if (Distance > WarningRadius)
		{
			continue;
		}

		// Внутри радиуса взрыва — красный, и мигает тем чаще, чем ближе взрыв.
		const bool bInside = Distance < Grenade->GetRadius();
		FLinearColor Color = bInside ? GrenadeDangerColor : GrenadeColor;
		const float Rate = FMath::Lerp(14.f, 5.f, FMath::Clamp(Grenade->GetTimeToExplode() / 3.f, 0.f, 1.f));
		Color.A = 0.65f + 0.35f * FMath::Sin(static_cast<float>(Now) * Rate);
		const FString Caption = FText::Format(GrenadeWarningText, FText::FromString(FormatDistance(Distance))).ToString();
		DrawWorldMarker(Grenade->GetActorLocation() + FVector(0.f, 0.f, 25.f), Color, 9.f * S, /*bEdgeArrow=*/true, Caption);
	}
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

// ---------------------------------------------------------------------------
// Напарники и взаимодействие
// ---------------------------------------------------------------------------

FVector ANoFearHUD::GetOverheadLocation(const AActor* Actor)
{
	if (!Actor)
	{
		return FVector::ZeroVector;
	}
	float HalfHeight = 90.f;
	if (const ACharacter* Character = Cast<ACharacter>(Actor); Character && Character->GetCapsuleComponent())
	{
		HalfHeight = Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	}
	return Actor->GetActorLocation() + FVector(0.f, 0.f, HalfHeight + 35.f);
}

FString ANoFearHUD::FormatDistance(float Centimeters) const
{
	return FText::Format(DistanceText, FText::AsNumber(FMath::RoundToInt(Centimeters / 100.f))).ToString();
}

void ANoFearHUD::DrawDiamond(const FVector2D& Center, float Radius, const FLinearColor& Color)
{
	const float Outline = FMath::Max(1.f, GetUIScale() * 2.f);
	const FLinearColor Shadow(0.f, 0.f, 0.f, 0.6f * Color.A);
	const float R = Radius + Outline;
	DrawColoredQuad(Center + FVector2D(0.f, -R), Center + FVector2D(R, 0.f), Center + FVector2D(0.f, R), Center + FVector2D(-R, 0.f), Shadow, Shadow, Shadow, Shadow);
	DrawColoredQuad(Center + FVector2D(0.f, -Radius), Center + FVector2D(Radius, 0.f), Center + FVector2D(0.f, Radius), Center + FVector2D(-Radius, 0.f), Color, Color, Color, Color);
}

bool ANoFearHUD::DrawWorldMarker(const FVector& WorldLocation, const FLinearColor& Color, float Size, bool bEdgeArrow, const FString& Caption)
{
	const APlayerController* PC = GetOwningPlayerController();
	if (!PC || !Canvas)
	{
		return false;
	}

	FVector CameraLocation;
	FRotator CameraRotation;
	PC->GetPlayerViewPoint(CameraLocation, CameraRotation);
	// В осях камеры: X — вперёд, Y — вправо, Z — вверх.
	const FVector Local = CameraRotation.UnrotateVector(WorldLocation - CameraLocation);

	const float S = GetUIScale();
	const float Inset = 40.f * S;
	const float CaptionScale = 0.9f * S;
	float CaptionWidth = 0.f;
	float CaptionHeight = 0.f;
	if (!Caption.IsEmpty())
	{
		GetTextSize(Caption, CaptionWidth, CaptionHeight, GetFont(), CaptionScale);
	}

	if (Local.X > 10.f)
	{
		const FVector Projected = Canvas->Project(WorldLocation, false);
		const FVector2D Screen(Projected.X, Projected.Y);
		if (Screen.X >= Inset && Screen.X <= Canvas->SizeX - Inset && Screen.Y >= Inset && Screen.Y <= Canvas->SizeY - Inset)
		{
			DrawDiamond(Screen, Size, Color);
			if (!Caption.IsEmpty())
			{
				DrawLabel(Caption, Screen.X, Screen.Y - Size - 4.f * S - CaptionHeight, Color, CaptionScale, ELabelAlign::Center);
			}
			return true;
		}
	}
	if (!bEdgeArrow)
	{
		return false;
	}

	// Направление как на радаре (вид сверху): впереди — верхний край, позади — нижний, слева — левый.
	// Высоту и наклон камеры не учитываем: иначе стрелка прыгает, когда цель проходит рядом с камерой.
	const FVector Flat = FRotator(0.f, CameraRotation.Yaw, 0.f).UnrotateVector(WorldLocation - CameraLocation);
	FVector2D Direction = FVector2D(Flat.Y, -Flat.X).GetSafeNormal();
	if (Direction.IsNearlyZero())
	{
		Direction = FVector2D(0.f, 1.f);
	}

	// Стрелка острием к цели, с тёмной обводкой.
	const float Length = Size * 3.f;
	const float HalfWidth = Size * 1.8f;

	// Рамка для стрелки: нижний край выше полосок здоровья и оружия, чтобы стрелка на них не ложилась.
	const float FrameLeft = Inset + Length;
	const float FrameRight = Canvas->SizeX - Inset - Length;
	const float FrameTop = Inset + Length;
	const float FrameBottom = Canvas->SizeY - (Margin + 70.f) * S - Length;
	const FVector2D FrameCenter((FrameLeft + FrameRight) * 0.5f, (FrameTop + FrameBottom) * 0.5f);
	const float HalfWidthToEdge = (FrameRight - FrameLeft) * 0.5f;
	const float HalfHeightToEdge = (FrameBottom - FrameTop) * 0.5f;
	const float ToEdge = FMath::Min(
		FMath::Abs(Direction.X) > KINDA_SMALL_NUMBER ? HalfWidthToEdge / FMath::Abs(Direction.X) : UE_BIG_NUMBER,
		FMath::Abs(Direction.Y) > KINDA_SMALL_NUMBER ? HalfHeightToEdge / FMath::Abs(Direction.Y) : UE_BIG_NUMBER);
	const FVector2D Position = FrameCenter + Direction * ToEdge;
	const FVector2D Side(-Direction.Y, Direction.X);
	auto DrawArrow = [&](float Grow, const FLinearColor& ArrowColor)
	{
		TArray<FCanvasUVTri> Triangles;
		Triangles.SetNum(1);
		FCanvasUVTri& Tri = Triangles[0];
		Tri.V0_Pos = Position + Direction * (Length * 0.5f + Grow);
		Tri.V1_Pos = Position - Direction * (Length * 0.5f + Grow * 0.5f) + Side * (HalfWidth + Grow);
		Tri.V2_Pos = Position - Direction * (Length * 0.5f + Grow * 0.5f) - Side * (HalfWidth + Grow);
		Tri.V0_Color = ArrowColor;
		Tri.V1_Color = ArrowColor;
		Tri.V2_Color = ArrowColor;
		DrawTranslucentTriangles(Canvas, Triangles);
	};
	DrawArrow(2.f * S, FLinearColor(0.f, 0.f, 0.f, 0.6f * Color.A));
	DrawArrow(0.f, Color);

	if (!Caption.IsEmpty())
	{
		// Подпись — внутрь экрана от стрелки.
		const FVector2D LabelCenter = Position - Direction * (Length + FMath::Max(CaptionWidth, CaptionHeight) * 0.6f);
		DrawLabel(Caption, LabelCenter.X, LabelCenter.Y - CaptionHeight * 0.5f, Color, CaptionScale, ELabelAlign::Center);
	}
	return false;
}

void ANoFearHUD::DrawRecruitableMarkers()
{
	const ANoFearPlayerController* PlayerController = Cast<ANoFearPlayerController>(GetOwningPlayerController());
	APawn* PlayerPawn = GetOwningPawn();
	if (!PlayerController || !PlayerPawn)
	{
		return;
	}

	TArray<UNoFearInteractableComponent*> Interactables;
	UNoFearInteractableComponent::GetInteractables(GetWorld(), Interactables);
	const UNoFearInteractableComponent* Focused = PlayerController->GetFocusedInteractable();

	FLinearColor Color = CompanionColor;
	Color.A = 0.55f;
	const float S = GetUIScale();
	for (UNoFearInteractableComponent* Interactable : Interactables)
	{
		const APawn* Candidate = Cast<APawn>(Interactable->GetOwner());
		if (!Candidate || Interactable == Focused || !Interactable->IsA<UNoFearRecruitableComponent>() || PlayerController->IsCompanion(Candidate))
		{
			continue;
		}
		if (FVector::DistSquared(PlayerPawn->GetActorLocation(), Candidate->GetActorLocation()) > FMath::Square(RecruitableMarkerDistance)
			|| !Interactable->CanInteract(PlayerPawn))
		{
			continue;
		}
		DrawWorldMarker(GetOverheadLocation(Candidate), Color, 5.f * S, /*bEdgeArrow=*/false, FString());
	}
}

void ANoFearHUD::DrawRing(const FVector2D& Center, float Radius, float Fraction, const FLinearColor& Color, float Thickness)
{
	constexpr int32 Segments = 32;
	Fraction = FMath::Clamp(Fraction, 0.f, 1.f);
	const int32 Count = FMath::CeilToInt(Fraction * Segments);
	for (int32 Index = 0; Index < Count; ++Index)
	{
		const float From = static_cast<float>(Index) / Segments;
		const float To = FMath::Min(static_cast<float>(Index + 1) / Segments, Fraction);
		const float AngleFrom = -UE_HALF_PI + UE_TWO_PI * From;
		const float AngleTo = -UE_HALF_PI + UE_TWO_PI * To;
		DrawLine(
			Center.X + Radius * FMath::Cos(AngleFrom), Center.Y + Radius * FMath::Sin(AngleFrom),
			Center.X + Radius * FMath::Cos(AngleTo), Center.Y + Radius * FMath::Sin(AngleTo),
			Color, Thickness);
	}
}

void ANoFearHUD::DrawBotIndicators()
{
	NOFEAR_PERF_SCOPE(HUD);
	const int32 Mode = CVarBotIndicators.GetValueOnGameThread();
	const ANoFearPlayerController* PlayerController = Cast<ANoFearPlayerController>(GetOwningPlayerController());
	const APawn* PlayerPawn = GetOwningPawn();
	UWorld* World = GetWorld();
	const UNoFearBotSubsystem* Subsystem = UNoFearBotSubsystem::Get(this);
	if (Mode <= 0 || !PlayerController || !PlayerPawn || !World || !Subsystem || !Canvas)
	{
		BotPanels.Reset();
		DrawnSquadRings = DrawnSquadLines = DrawnOrderLines = DrawnOrderWords = DrawnTactics = DrawnFearBars = 0;
		return;
	}
	DrawnOrderWords = DrawnTactics = DrawnFearBars = 0;
	const FNoFearFearTuning& FearTuning = UNoFearFearSubsystem::Get(this) ? static_cast<const FNoFearFearTuning&>(*UNoFearFearSubsystem::Get(this)) : UNoFearAISettings::Get()->Fear;

	// Кольца под ногами — под панелями.
	if (bShowSquadRings)
	{
		DrawSquadRings();
	}
	else
	{
		DrawnSquadRings = DrawnSquadLines = DrawnOrderLines = 0;
	}
	const UNoFearSquadSubsystem* Squads = UNoFearSquadSubsystem::Get(this);

	FVector CameraLocation;
	FRotator CameraRotation;
	PlayerController->GetPlayerViewPoint(CameraLocation, CameraRotation);
	const double Now = World->GetTimeSeconds();
	const float S = GetUIScale();
	const float MaxDistance = FMath::Max(BotPanelMaxDistance, 1.f);

	// Кто сейчас под прицелом. Оружие в руках бота — часть бота.
	const AActor* UnderCrosshair = nullptr;
	{
		FCollisionQueryParams Params(SCENE_QUERY_STAT(NoFearBotPanelCrosshair), true, PlayerPawn);
		FHitResult Hit;
		if (World->LineTraceSingleByChannel(Hit, CameraLocation, CameraLocation + CameraRotation.Vector() * MaxDistance, ECC_Visibility, Params))
		{
			UnderCrosshair = Hit.GetActor();
			while (UnderCrosshair && !UnderCrosshair->IsA<APawn>() && UnderCrosshair->GetAttachParentActor())
			{
				UnderCrosshair = UnderCrosshair->GetAttachParentActor();
			}
		}
	}

	struct FPanelDraw
	{
		const APawn* Pawn = nullptr;
		const ANoFearAIController* Bot = nullptr;
		FVector2D Screen = FVector2D::ZeroVector;
		float Distance = 0.f;
		bool bHealth = false;
		bool bAlert = false;
		int32 Grenades = 0;
		FString Shout;
		bool bCompanion = false;
		bool bHostile = false;
		ENoFearTeam Team = ENoFearTeam::Neutral;
		/** Значок роли рядом с «◆ N». None — не в отряде. */
		ENoFearSquadRole Role = ENoFearSquadRole::None;
		FNoFearSquadMemberView SquadView;
		/** Слово приказа рядом со значком роли. */
		bool bOrder = false;
		/** Над командиром — тактика отряда и шкала морали. */
		bool bTactic = false;
		/** Полоска страха над полоской здоровья. */
		bool bFear = false;
	};
	TArray<FPanelDraw> Panels;
	TSet<const AActor*> Seen;

	for (const ANoFearAIController* Bot : Subsystem->GetBots())
	{
		const APawn* Pawn = Bot->GetPawn();
		const UNoFearHealthComponent* Health = Pawn ? UNoFearHealthComponent::FindHealth(Pawn) : nullptr;
		if (!Pawn || Pawn == PlayerPawn || !Health || Health->IsDead())
		{
			continue;
		}
		Seen.Add(Pawn);

		// Плавная полоска и «хвост» живут независимо от того, показана ли панель: появившись, она не дёргается.
		FBotPanelState& State = BotPanels.FindOrAdd(Pawn);
		const float Fraction = Health->GetHealthFraction();
		if (State.LastUpdate < 0.0)
		{
			State.DisplayedHealth = State.GhostHealth = State.LastHealth = Fraction;
		}
		const float DeltaTime = State.LastUpdate < 0.0 ? 0.f : static_cast<float>(FMath::Min(Now - State.LastUpdate, 0.25));
		State.LastUpdate = Now;
		if (Fraction < State.LastHealth - KINDA_SMALL_NUMBER)
		{
			State.LastDamageSeen = Now;
		}
		State.LastHealth = Fraction;
		State.DisplayedHealth = FMath::FInterpTo(State.DisplayedHealth, Fraction, DeltaTime, HealthInterpSpeed);
		if (Fraction >= State.GhostHealth)
		{
			State.GhostHealth = Fraction;
		}
		else if (Now - State.LastDamageSeen > GhostHoldTime)
		{
			State.GhostHealth = FMath::Max(Fraction, State.GhostHealth - GhostDrainSpeed * DeltaTime);
		}

		// Что показывать (правила из AI_Roadmap.md, этап 1).
		const float Distance = FVector::Dist(PlayerPawn->GetActorLocation(), Pawn->GetActorLocation());
		const ENoFearTeam Team = UNoFearTeamLibrary::GetTeam(Pawn);
		const bool bCompanion = PlayerController->IsCompanion(Pawn);
		const bool bHostile = UNoFearTeamLibrary::AreHostile(PlayerPawn, Pawn);
		const ENoFearAlertState Alert = Bot->GetAlertState();
		const bool bRecentlyDamaged = Health->GetTimeSinceLastDamage() < BotRecentDamageTime;
		const bool bAimed = Pawn == UnderCrosshair;

		bool bWantHealth = false;
		if (Mode >= 2 || bCompanion)
		{
			bWantHealth = true;
		}
		else
		{
			switch (Team)
			{
			case ENoFearTeam::Ally:
				bWantHealth = Distance <= AllyPanelDistance || bRecentlyDamaged || bAimed;
				break;
			case ENoFearTeam::Enemy:
				bWantHealth = bRecentlyDamaged || bAimed || Alert == ENoFearAlertState::Combat || Distance <= EnemyPanelDistance;
				break;
			case ENoFearTeam::Monster:
			case ENoFearTeam::AggroMonster:
				bWantHealth = bRecentlyDamaged || bAimed;
				break;
			default:
				// Мирные и нейтралы: только раненые.
				bWantHealth = Fraction < 0.999f && (bRecentlyDamaged || bAimed || Distance <= AllyPanelDistance);
				break;
			}
		}
		const bool bWantAlert = Alert != ENoFearAlertState::Calm && (bHostile || Mode >= 2);

		// Запас гранат: напарник и союзники — всегда, враги — по настройке, монстры и мирные — никогда.
		const ANoFearCharacter* BotCharacter = Cast<ANoFearCharacter>(Pawn);
		const UNoFearGrenadeComponent* BotGrenades = BotCharacter ? BotCharacter->GetGrenadeComponent() : nullptr;
		const int32 GrenadeCount = BotGrenades ? BotGrenades->GetGrenadeCount() : 0;
		const bool bLoadoutTeam = bCompanion || Team == ENoFearTeam::Ally
			|| (Team == ENoFearTeam::Enemy && (bShowEnemyLoadout || CVarShowEnemyLoadout.GetValueOnGameThread()));
		const bool bWantLoadout = GrenadeCount > 0 && bLoadoutTeam;
		const FString Shout = Bot->GetShoutText().ToString();

		// Отряд: приказ бойца и тактика у командира — тоже повод показать панель, чтобы было видно, что делает отряд.
		FNoFearSquadMemberView SquadView;
		const bool bSquad = !bCompanion && Squads && Squads->GetMemberView(Pawn, SquadView) && SquadView.SquadSize >= 2;
		const bool bWantOrder = bSquad && bShowSquadOrders && (SquadView.Order != ENoFearSquadOrder::None || SquadView.bGrenadeOrder);
		const bool bWantTactic = bSquad && bShowSquadOrders && Pawn == SquadView.Commander && SquadView.Tactic != ENoFearSquadTactic::None;

		// Страх (этап 11): у людей, не у монстров. В отладочном режиме — у всех, кто хоть немного боится.
		const float Fear = UNoFearFearComponent::GetActorFear(Pawn);
		State.DisplayedFear = FMath::FInterpTo(State.DisplayedFear, Fear, DeltaTime, HealthInterpSpeed);
		const bool bFrightening = UNoFearFearSubsystem::IsFrightening(Team);
		const bool bWantFear = bShowFearBars && !bFrightening
			&& (Mode >= 2 ? State.DisplayedFear >= 1.f : (Fear >= FearBarMinFear && Distance <= FearBarDistance));

		bool bWant = (bWantHealth || bWantAlert || bWantLoadout || bWantOrder || bWantTactic || bWantFear || !Shout.IsEmpty()) && Distance <= MaxDistance;

		// На экране ли голова бота.
		FVector2D Screen = FVector2D::ZeroVector;
		if (bWant)
		{
			const FVector Overhead = GetOverheadLocation(Pawn);
			bWant = CameraRotation.UnrotateVector(Overhead - CameraLocation).X > 10.f;
			if (bWant)
			{
				const FVector Projected = Canvas->Project(Overhead, false);
				Screen = FVector2D(Projected.X, Projected.Y);
				const float Inset = 20.f * S;
				bWant = Screen.X >= Inset && Screen.X <= Canvas->SizeX - Inset && Screen.Y >= Inset && Screen.Y <= Canvas->SizeY - Inset;
			}
		}

		// Прямая видимость от камеры; напарников видно всегда, как и их ромб.
		if (bWant && Mode < 2 && !bCompanion)
		{
			if (Now >= State.NextVisibilityCheck)
			{
				State.NextVisibilityCheck = Now + BotVisibilityInterval + FMath::FRand() * 0.05;
				FCollisionQueryParams Params(SCENE_QUERY_STAT(NoFearBotPanelSight), false, PlayerPawn);
				Params.AddIgnoredActor(Pawn);
				TArray<AActor*> Attached;
				Pawn->GetAttachedActors(Attached);
				Params.AddIgnoredActors(Attached);
				State.bLineOfSight = !World->LineTraceTestByChannel(CameraLocation, GetOverheadLocation(Pawn), ECC_Visibility, Params);
			}
			bWant = State.bLineOfSight;
		}
		else if (!bWant)
		{
			State.NextVisibilityCheck = 0.0;
		}

		State.Alpha = FMath::FInterpConstantTo(State.Alpha, bWant ? 1.f : 0.f, DeltaTime, 7.f);
		if (State.Alpha <= 0.01f)
		{
			continue;
		}

		FPanelDraw& Panel = Panels.AddDefaulted_GetRef();
		Panel.Pawn = Pawn;
		Panel.Bot = Bot;
		Panel.Screen = Screen;
		Panel.Distance = Distance;
		Panel.bHealth = bWantHealth;
		Panel.bAlert = bWantAlert;
		Panel.Grenades = bWantLoadout ? GrenadeCount : 0;
		Panel.Shout = Shout;
		Panel.bCompanion = bCompanion;
		Panel.bHostile = bHostile;
		Panel.Team = Team;
		Panel.bFear = bWantFear;
		if (bSquad)
		{
			Panel.Role = SquadView.Role;
			Panel.SquadView = SquadView;
			Panel.bOrder = bWantOrder;
			Panel.bTactic = bWantTactic;
		}
	}

	for (auto It = BotPanels.CreateIterator(); It; ++It)
	{
		if (!It.Key().IsValid() || !Seen.Contains(It.Key().Get()))
		{
			It.RemoveCurrent();
		}
	}

	// Ближайшие рисуются первыми и вытесняют дальние при переизбытке.
	Panels.Sort([](const FPanelDraw& A, const FPanelDraw& B) { return A.Distance < B.Distance; });
	// Сколько панелей рисуется за кадр (Project Settings → NoFear AI → HUD): ближайшие, остальные пропускаются.
	const int32 MaxBotPanels = UNoFearAISettings::Get()->Hud.MaxBotPanels;
	if (Panels.Num() > MaxBotPanels)
	{
		Panels.SetNum(MaxBotPanels);
	}

	// Дальние первыми, чтобы ближние ложились поверх.
	for (int32 Index = Panels.Num() - 1; Index >= 0; --Index)
	{
		const FPanelDraw& Panel = Panels[Index];
		const FBotPanelState* State = BotPanels.Find(Panel.Pawn);
		if (!State)
		{
			continue;
		}

		const float DistanceFade = 1.f - FMath::Clamp((Panel.Distance - MaxDistance * 0.8f) / (MaxDistance * 0.2f), 0.f, 1.f);
		const float Opacity = State->Alpha * DistanceFade;
		if (Opacity <= 0.01f)
		{
			continue;
		}

		const float Shrink = FMath::Lerp(1.f, 0.6f, FMath::Clamp((Panel.Distance - 800.f) / FMath::Max(MaxDistance - 800.f, 1.f), 0.f, 1.f));
		const float PanelScale = S * Shrink;
		const float CenterX = Panel.Screen.X;
		// Над ромбом напарника (и его подписью с расстоянием).
		float TopY = Panel.Screen.Y - (Panel.bCompanion ? 46.f * S : 4.f * S);

		// Нижняя строка — значок роли, слово приказа и запас гранат «◆ N», над ней полоска здоровья.
		const bool bRoleIcon = Panel.Role != ENoFearSquadRole::None;
		const FNoFearSquadMemberView& SquadView = Panel.SquadView;
		FString OrderWord;
		if (Panel.bOrder)
		{
			OrderWord = SquadView.bGrenadeOrder
				? NSLOCTEXT("NoFearHUD", "OrderGrenade", "граната").ToString()
				: UNoFearSquadSubsystem::GetOrderText(SquadView.Order).ToString();
		}
		if (Panel.Grenades > 0 || bRoleIcon)
		{
			FLinearColor Color = GrenadeColor;
			Color.A *= Opacity;
			const FString Count = Panel.Grenades > 0 ? FString::FromInt(Panel.Grenades) : FString();
			const float TextScale = 0.85f * PanelScale;
			float TextWidth = 0.f;
			float TextHeight = 0.f;
			GetTextSize(Panel.Grenades > 0 ? Count : FString(TEXT("0")), TextWidth, TextHeight, GetFont(), TextScale);
			const float DiamondRadius = 4.f * PanelScale;
			const float IconSize = 5.5f * PanelScale;
			const float IconWidth = bRoleIcon ? IconSize * 2.f : 0.f;
			const float GrenadeWidth = Panel.Grenades > 0 ? DiamondRadius * 2.f + 4.f * PanelScale + TextWidth : 0.f;

			// Новый приказ — слово вспыхивает белым и чуть крупнее.
			const float OrderFlash = 1.f - FMath::Clamp(static_cast<float>(Now - SquadView.OrderTime) / SquadOrderFlashTime, 0.f, 1.f);
			const float WordScale = 0.8f * PanelScale * (1.f + 0.2f * OrderFlash);
			float WordWidth = 0.f;
			float WordHeight = 0.f;
			if (!OrderWord.IsEmpty())
			{
				GetTextSize(OrderWord, WordWidth, WordHeight, GetFont(), WordScale);
			}

			const float GapWidth = 6.f * PanelScale;
			float RowWidth = 0.f;
			for (const float Part : { IconWidth, WordWidth, GrenadeWidth })
			{
				if (Part > 0.f)
				{
					RowWidth += (RowWidth > 0.f ? GapWidth : 0.f) + Part;
				}
			}
			const float RowLeft = CenterX - RowWidth * 0.5f;
			const float RowY = TopY - TextHeight;
			float Cursor = RowLeft;
			if (bRoleIcon)
			{
				FLinearColor IconColor = GetSquadRoleColor(Panel.Role);
				IconColor.A *= Opacity;
				DrawRoleIcon(FVector2D(Cursor + IconSize, RowY + TextHeight * 0.5f), IconSize, Panel.Role, IconColor);
				Cursor += IconWidth + GapWidth;
			}
			if (WordWidth > 0.f)
			{
				FLinearColor WordColor = SquadView.bGrenadeOrder ? GrenadeColor : GetSquadRoleColor(Panel.Role);
				WordColor = FMath::Lerp(WordColor, FLinearColor::White, OrderFlash * 0.7f);
				WordColor.A = Opacity;
				DrawLabel(OrderWord, Cursor, RowY + (TextHeight - WordHeight) * 0.5f, WordColor, WordScale, ELabelAlign::Left);
				Cursor += WordWidth + GapWidth;
				++DrawnOrderWords;
			}
			if (Panel.Grenades > 0)
			{
				DrawDiamond(FVector2D(Cursor + DiamondRadius, RowY + TextHeight * 0.5f), DiamondRadius, Color);
				DrawLabel(Count, RowLeft + RowWidth, RowY, Color, TextScale, ELabelAlign::Right);
			}
			TopY = RowY - 3.f * PanelScale;
		}

		if (Panel.bHealth)
		{
			const float BarWidth = FMath::RoundToFloat(76.f * PanelScale);
			const float BarHeight = FMath::Max(3.f, FMath::RoundToFloat(7.f * PanelScale));
			const float BarX = FMath::RoundToFloat(CenterX - BarWidth * 0.5f);
			const float BarY = FMath::RoundToFloat(TopY - BarHeight);

			FLinearColor Fill = Panel.bCompanion ? CompanionColor : (Panel.bHostile ? BotEnemyColor : (Panel.Team == ENoFearTeam::Ally ? BotAllyColor : BotNeutralColor));
			const float Fraction = State->LastHealth;
			if (Fraction < LowHealthFraction)
			{
				// Почти мёртв: к красному и мигает.
				Fill = FMath::Lerp(Fill, LowHealthColor, FMath::Clamp(1.f - Fraction / LowHealthFraction, 0.f, 1.f));
				Fill.A *= 0.7f + 0.3f * FMath::Sin(static_cast<float>(Now) * 9.f);
			}
			DrawBar(BarX, BarY, BarWidth, BarHeight, State->DisplayedHealth, Fill, State->GhostHealth, Opacity);
			TopY = BarY - 7.f * PanelScale;
		}

		// Страх — тонкая полоска прямо над здоровьем: жёлтая (тревога), оранжевая (паника), красная (ужас).
		if (Panel.bFear)
		{
			if (Panel.bHealth)
			{
				TopY += 4.f * PanelScale;
			}
			const float BarWidth = FMath::RoundToFloat(76.f * PanelScale);
			const float BarHeight = FMath::Max(2.f, FMath::RoundToFloat(4.f * PanelScale));
			const float BarX = FMath::RoundToFloat(CenterX - BarWidth * 0.5f);
			const float BarY = FMath::RoundToFloat(TopY - BarHeight);
			const float Fear = State->DisplayedFear;
			const FLinearColor& FearColor = Fear >= FearTuning.TerrorThreshold ? FearTerrorColor
				: (Fear >= FearTuning.PanicThreshold ? FearPanicColor : FearAnxiousColor);
			DrawBar(BarX, BarY, BarWidth, BarHeight, FMath::Clamp(Fear / 100.f, 0.f, 1.f), FearColor, -1.f, Opacity);
			TopY = BarY - 7.f * PanelScale;
			++DrawnFearBars;
		}

		if (Panel.bAlert)
		{
			const ENoFearAlertState Alert = Panel.Bot->GetAlertState();
			FLinearColor Color = AlertSuspiciousColor;
			FString Symbol = TEXT("?");
			float RingFill = FMath::Clamp(Panel.Bot->GetTopAwareness(), 0.f, 1.f);
			if (Alert == ENoFearAlertState::Combat)
			{
				Color = AlertCombatColor;
				Symbol = TEXT("!");
				RingFill = 1.f;
			}
			else if (Alert == ENoFearAlertState::Search)
			{
				Color = AlertSearchColor;
				RingFill = 1.f;
			}
			Color.A = Opacity;

			const float Radius = 13.f * PanelScale;
			const float Thickness = FMath::Max(2.f, 3.f * PanelScale);
			const FVector2D Center(CenterX, TopY - Radius - Thickness);

			DrawRing(Center, Radius, 1.f, FLinearColor(0.f, 0.f, 0.f, 0.55f * Opacity), Thickness + 2.f * S);
			FLinearColor Dim = Color;
			Dim.A *= 0.3f;
			DrawRing(Center, Radius, 1.f, Dim, Thickness);
			DrawRing(Center, Radius, RingFill, Color, Thickness);

			// Знак занимает примерно две трети внутреннего диаметра кольца, каким бы ни был шрифт.
			float TextWidth = 0.f;
			float TextHeight = 0.f;
			GetTextSize(Symbol, TextWidth, TextHeight, GetFont(), 1.f);
			const float TextScale = TextHeight > 1.f ? Radius * 2.2f / TextHeight : PanelScale;
			GetTextSize(Symbol, TextWidth, TextHeight, GetFont(), TextScale);
			DrawLabel(Symbol, Center.X, Center.Y - TextHeight * 0.5f, Color, TextScale, ELabelAlign::Center);
			TopY = Center.Y - Radius - Thickness - 4.f * PanelScale;
		}

		// Над командиром — тактика отряда (вспыхивает при смене) и шкала морали под ней.
		if (Panel.bTactic)
		{
			const float BarWidth = FMath::RoundToFloat(54.f * PanelScale);
			const float BarHeight = FMath::Max(2.f, FMath::RoundToFloat(4.f * PanelScale));
			const float BarX = FMath::RoundToFloat(CenterX - BarWidth * 0.5f);
			const float BarY = FMath::RoundToFloat(TopY - BarHeight);
			const float Morale = FMath::Clamp(SquadView.Morale, 0.f, 1.f);
			FLinearColor MoraleColor = FMath::Lerp(SquadMoraleLowColor, SquadMoraleHighColor, Morale);
			if (Morale < 0.25f)
			{
				// Отряд на грани бегства — шкала мигает.
				MoraleColor.A *= 0.6f + 0.4f * FMath::Sin(static_cast<float>(Now) * 8.f);
			}
			DrawBar(BarX, BarY, BarWidth, BarHeight, Morale, MoraleColor, -1.f, Opacity);

			FString Tactic = UNoFearSquadSubsystem::GetTacticText(SquadView.Tactic).ToString();
			if (SquadView.bHoldingLine)
			{
				Tactic += NSLOCTEXT("NoFearHUD", "TacticHoldingLine", " · рубеж").ToString();
			}
			if (!SquadView.AssistingSquad.IsNone())
			{
				Tactic += NSLOCTEXT("NoFearHUD", "TacticAssisting", " · к соседям").ToString();
			}
			const float TacticFlash = 1.f - FMath::Clamp(static_cast<float>(Now - SquadView.TacticChangeTime) / SquadOrderFlashTime, 0.f, 1.f);
			FLinearColor Color = FMath::Lerp(SquadCommanderColor, FLinearColor::White, TacticFlash * 0.7f);
			Color.A = Opacity;
			const float TextScale = 0.95f * PanelScale * (1.f + 0.2f * TacticFlash);
			float TextWidth = 0.f;
			float TextHeight = 0.f;
			GetTextSize(Tactic, TextWidth, TextHeight, GetFont(), TextScale);
			const float TextY = BarY - 3.f * PanelScale - TextHeight;
			DrawLabel(Tactic, CenterX, TextY, Color, TextScale, ELabelAlign::Center);
			TopY = TextY - 3.f * PanelScale;
			++DrawnTactics;
		}

		// Крик («Граната!») — над всем.
		if (!Panel.Shout.IsEmpty())
		{
			FLinearColor Color = ShoutColor;
			Color.A *= Opacity;
			const float TextScale = 1.1f * PanelScale;
			float TextWidth = 0.f;
			float TextHeight = 0.f;
			GetTextSize(Panel.Shout, TextWidth, TextHeight, GetFont(), TextScale);
			DrawLabel(Panel.Shout, CenterX, TopY - TextHeight, Color, TextScale, ELabelAlign::Center);
		}
	}
}

void ANoFearHUD::DrawInteractionPrompt()
{
	const ANoFearPlayerController* PlayerController = Cast<ANoFearPlayerController>(GetOwningPlayerController());
	APawn* PlayerPawn = GetOwningPawn();
	UNoFearInteractableComponent* Focused = PlayerController ? PlayerController->GetFocusedInteractable() : nullptr;
	if (!Focused || !PlayerPawn)
	{
		return;
	}

	const AActor* Target = Focused->GetOwner();
	const bool bRecruitable = Focused->IsA<UNoFearRecruitableComponent>();
	const float S = GetUIScale();

	// Над тем, с кем будем взаимодействовать, — белая метка (у своего напарника уже есть зелёная).
	if (!PlayerController->IsCompanion(Cast<APawn>(Target)))
	{
		DrawWorldMarker(GetOverheadLocation(Target), FLinearColor::White, 6.f * S, /*bEdgeArrow=*/false, FString());
	}

	const FString Key = InteractKeyText.ToString();
	const FString Action = Focused->GetInteractionPrompt(PlayerPawn).ToString();
	const float TextScale = 1.2f * S;
	float KeyWidth = 0.f;
	float KeyHeight = 0.f;
	float ActionWidth = 0.f;
	float ActionHeight = 0.f;
	GetTextSize(Key, KeyWidth, KeyHeight, GetFont(), TextScale);
	GetTextSize(Action, ActionWidth, ActionHeight, GetFont(), TextScale);

	// [E] Действие — по центру, под прицелом.
	const float Padding = 6.f * S;
	const float BoxHeight = KeyHeight + Padding;
	const float BoxWidth = FMath::Max(KeyWidth + Padding * 2.f, BoxHeight);
	const float Gap = 10.f * S;
	const float PromptLeft = FMath::RoundToFloat(Canvas->SizeX * 0.5f - (BoxWidth + Gap + ActionWidth) * 0.5f);
	const float PromptTop = FMath::RoundToFloat(Canvas->SizeY * 0.5f + 70.f * S);
	const float Border = FMath::Max(1.f, FMath::RoundToFloat(2.f * S));

	DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.6f), PromptLeft - Border, PromptTop - Border, BoxWidth + Border * 2.f, BoxHeight + Border * 2.f);
	DrawRect(FLinearColor(1.f, 1.f, 1.f, 0.92f), PromptLeft, PromptTop, BoxWidth, BoxHeight);
	DrawText(Key, FLinearColor(0.05f, 0.05f, 0.05f, 1.f), FMath::RoundToFloat(PromptLeft + (BoxWidth - KeyWidth) * 0.5f), FMath::RoundToFloat(PromptTop + (BoxHeight - KeyHeight) * 0.5f), GetFont(), TextScale);
	DrawLabel(Action, PromptLeft + BoxWidth + Gap, PromptTop + (BoxHeight - ActionHeight) * 0.5f, FLinearColor::White, TextScale, ELabelAlign::Left);

	// Под подсказкой — с кем.
	if (const ANoFearCharacter* Character = Cast<ANoFearCharacter>(Target))
	{
		const FLinearColor NameColor = bRecruitable ? CompanionColor : FLinearColor(0.85f, 0.85f, 0.85f, 0.9f);
		DrawLabel(Character->GetCharacterDisplayName().ToString(), Canvas->SizeX * 0.5f, PromptTop + BoxHeight + 8.f * S, NameColor, 0.9f * S, ELabelAlign::Center);
	}
}

FLinearColor ANoFearHUD::GetSquadRoleColor(ENoFearSquadRole SquadRole) const
{
	switch (SquadRole)
	{
	case ENoFearSquadRole::Commander:
		return SquadCommanderColor;
	case ENoFearSquadRole::Assault:
		return SquadAssaultColor;
	case ENoFearSquadRole::Support:
		return SquadSupportColor;
	default:
		return SquadRiflemanColor;
	}
}

void ANoFearHUD::DrawWorldLine(const FVector& From, const FVector& To, const FLinearColor& Color, float Thickness)
{
	const APlayerController* PC = GetOwningPlayerController();
	if (!PC || !Canvas)
	{
		return;
	}
	FVector CameraLocation;
	FRotator CameraRotation;
	PC->GetPlayerViewPoint(CameraLocation, CameraRotation);

	// Отрезок, уходящий за камеру, обрезаем по плоскости чуть впереди неё.
	constexpr float NearPlane = 10.f;
	FVector A = From;
	FVector B = To;
	const float DepthA = CameraRotation.UnrotateVector(A - CameraLocation).X;
	const float DepthB = CameraRotation.UnrotateVector(B - CameraLocation).X;
	if (DepthA <= NearPlane && DepthB <= NearPlane)
	{
		return;
	}
	if (DepthA <= NearPlane)
	{
		A = FMath::Lerp(A, B, (NearPlane - DepthA) / (DepthB - DepthA));
	}
	else if (DepthB <= NearPlane)
	{
		B = FMath::Lerp(B, A, (NearPlane - DepthB) / (DepthA - DepthB));
	}
	const FVector ScreenA = Canvas->Project(A, false);
	const FVector ScreenB = Canvas->Project(B, false);
	DrawLine(ScreenA.X, ScreenA.Y, ScreenB.X, ScreenB.Y, Color, Thickness);
}

void ANoFearHUD::DrawWorldDashedLine(const FVector& From, const FVector& To, const FLinearColor& Color, float Thickness, float Dash)
{
	const float Length = FVector::Dist(From, To);
	if (Length <= KINDA_SMALL_NUMBER)
	{
		return;
	}
	Dash = FMath::Max(Dash, 5.f);
	const FVector Direction = (To - From) / Length;
	for (float Start = 0.f; Start < Length; Start += Dash * 2.f)
	{
		DrawWorldLine(From + Direction * Start, From + Direction * FMath::Min(Start + Dash, Length), Color, Thickness);
	}
}

void ANoFearHUD::DrawGroundRing(const FVector& Center, float Radius, const FLinearColor& Color, float Thickness, bool bDashed)
{
	constexpr int32 Segments = 24;
	FVector Previous = Center + FVector(Radius, 0.f, 0.f);
	for (int32 Index = 1; Index <= Segments; ++Index)
	{
		const float Angle = UE_TWO_PI * Index / Segments;
		const FVector Next = Center + FVector(Radius * FMath::Cos(Angle), Radius * FMath::Sin(Angle), 0.f);
		if (!bDashed || Index % 2 == 1)
		{
			DrawWorldLine(Previous, Next, Color, Thickness);
		}
		Previous = Next;
	}
}

void ANoFearHUD::DrawRoleIcon(const FVector2D& Center, float Size, ENoFearSquadRole SquadRole, const FLinearColor& Color)
{
	const float Thickness = FMath::Max(1.5f, Size * 0.32f);
	const FLinearColor Shadow(0.f, 0.f, 0.f, 0.6f * Color.A);

	TArray<TArray<FVector2D>> Lines;
	switch (SquadRole)
	{
	case ENoFearSquadRole::Commander:
		// Корона: основание и три зубца.
		Lines.Add({ { -Size, Size * 0.55f }, { Size, Size * 0.55f }, { Size, -Size * 0.55f }, { Size * 0.5f, Size * 0.05f },
			{ 0.f, -Size * 0.75f }, { -Size * 0.5f, Size * 0.05f }, { -Size, -Size * 0.55f }, { -Size, Size * 0.55f } });
		break;
	case ENoFearSquadRole::Assault:
		// Шеврон вверх.
		Lines.Add({ { -Size, Size * 0.45f }, { 0.f, -Size * 0.45f }, { Size, Size * 0.45f } });
		break;
	case ENoFearSquadRole::Support:
		// Две черты.
		Lines.Add({ { -Size, -Size * 0.35f }, { Size, -Size * 0.35f } });
		Lines.Add({ { -Size, Size * 0.35f }, { Size, Size * 0.35f } });
		break;
	default:
		// Точка.
		DrawDiamond(Center, Size * 0.4f, Color);
		return;
	}

	// Сначала тень под всеми линиями, потом сами линии.
	for (const bool bShadow : { true, false })
	{
		for (const TArray<FVector2D>& Points : Lines)
		{
			for (int32 Index = 1; Index < Points.Num(); ++Index)
			{
				const FVector2D A = Center + Points[Index - 1];
				const FVector2D B = Center + Points[Index];
				DrawLine(A.X, A.Y, B.X, B.Y, bShadow ? Shadow : Color, bShadow ? Thickness + 2.f : Thickness);
			}
		}
	}
}

void ANoFearHUD::DrawSquadRings()
{
	NOFEAR_PERF_SCOPE(HUD);
	DrawnSquadRings = DrawnSquadLines = DrawnOrderLines = 0;
	const int32 Mode = CVarBotIndicators.GetValueOnGameThread();
	const ANoFearPlayerController* PlayerController = Cast<ANoFearPlayerController>(GetOwningPlayerController());
	const APawn* PlayerPawn = GetOwningPawn();
	UWorld* World = GetWorld();
	const UNoFearBotSubsystem* Bots = UNoFearBotSubsystem::Get(this);
	const UNoFearSquadSubsystem* Squads = UNoFearSquadSubsystem::Get(this);
	if (Mode <= 0 || !PlayerController || !PlayerPawn || !World || !Bots || !Canvas)
	{
		return;
	}

	FVector CameraLocation;
	FRotator CameraRotation;
	PlayerController->GetPlayerViewPoint(CameraLocation, CameraRotation);
	const double Now = World->GetTimeSeconds();
	const float DeltaTime = FMath::Min(World->GetDeltaSeconds(), 0.25f);
	const float S = GetUIScale();
	const float MaxDistance = FMath::Max(SquadRingMaxDistance, 1.f);

	struct FRingDraw
	{
		const APawn* Pawn = nullptr;
		FVector Center = FVector::ZeroVector;
		FNoFearSquadMemberView View;
		/** Вожак стаи монстров, а не боец отряда. */
		bool bPackLeader = false;
		bool bHostile = false;
		bool bDim = false;
		float Opacity = 0.f;
		float Distance = 0.f;
	};
	TArray<FRingDraw> Rings;

	for (const ANoFearAIController* Bot : Bots->GetBots())
	{
		const APawn* Pawn = Bot->GetPawn();
		if (!Pawn || Pawn == PlayerPawn || UNoFearHealthComponent::IsActorDead(Pawn) || PlayerController->IsCompanion(Pawn))
		{
			continue;
		}
		const float Distance = FVector::Dist(PlayerPawn->GetActorLocation(), Pawn->GetActorLocation());
		if (Distance > MaxDistance)
		{
			continue;
		}

		FRingDraw Ring;
		const bool bSquad = Squads && Squads->GetMemberView(Pawn, Ring.View) && Ring.View.SquadSize >= 2;
		if (!bSquad)
		{
			const ANoFearMonsterAIController* Monster = Cast<ANoFearMonsterAIController>(Bot);
			Ring.bPackLeader = Monster && Monster->IsPackLeader() && Monster->GetPackSize() >= 2;
			if (!Ring.bPackLeader)
			{
				continue;
			}
		}
		Ring.Pawn = Pawn;
		Ring.bHostile = UNoFearTeamLibrary::AreHostile(PlayerPawn, Pawn);

		// Враги и вожаки — только на виду у игрока (режим 2 — всегда).
		FBotPanelState& State = BotPanels.FindOrAdd(Pawn);
		bool bShow = true;
		if (Ring.bHostile && Mode < 2)
		{
			if (Now >= State.NextRingVisibilityCheck)
			{
				State.NextRingVisibilityCheck = Now + BotVisibilityInterval + FMath::FRand() * 0.05;
				FCollisionQueryParams Params(SCENE_QUERY_STAT(NoFearSquadRingSight), false, PlayerPawn);
				Params.AddIgnoredActor(Pawn);
				TArray<AActor*> Attached;
				Pawn->GetAttachedActors(Attached);
				Params.AddIgnoredActors(Attached);
				State.bRingLineOfSight = !World->LineTraceTestByChannel(CameraLocation, Pawn->GetActorLocation(), ECC_Visibility, Params);
			}
			bShow = State.bRingLineOfSight;
		}
		State.RingAlpha = FMath::FInterpConstantTo(State.RingAlpha, bShow ? 1.f : 0.f, DeltaTime, 5.f);

		const float DistanceFade = 1.f - FMath::Clamp((Distance - MaxDistance * 0.8f) / (MaxDistance * 0.2f), 0.f, 1.f);
		Ring.Opacity = State.RingAlpha * DistanceFade;
		if (bSquad)
		{
			// Плавно проявляется, когда боец встаёт в строй.
			Ring.Opacity *= FMath::Clamp(static_cast<float>(Now - Ring.View.JoinTime) / 0.4f, 0.f, 1.f);
			const ANoFearShooterAIController* Shooter = Cast<ANoFearShooterAIController>(Bot);
			const UNoFearHealthComponent* Health = UNoFearHealthComponent::FindHealth(Pawn);
			Ring.bDim = (Shooter && Shooter->IsRetreating()) || (Health && Health->GetHealthFraction() < LowHealthFraction);
		}
		if (Ring.Opacity <= 0.01f)
		{
			continue;
		}
		Ring.Center = Pawn->GetActorLocation() - FVector(0.f, 0.f, Pawn->GetSimpleCollisionHalfHeight() - 4.f);
		Ring.Distance = Distance;
		Rings.Add(Ring);
	}

	// Не больше MaxSquadRings колец (Project Settings → NoFear AI → HUD): ближние вытесняют дальние.
	Rings.Sort([](const FRingDraw& A, const FRingDraw& B) { return A.Distance < B.Distance; });
	const int32 MaxRings = UNoFearAISettings::Get()->Hud.MaxSquadRings;
	if (Rings.Num() > MaxRings)
	{
		Rings.SetNum(MaxRings);
	}

	const float Radius = SquadRingRadius;
	const float Outline = 2.f * S;

	// Линии к командиру: при сборе, вступлении бойца и смене командира. Под кольцами.
	for (const FRingDraw& Ring : Rings)
	{
		if (Ring.bPackLeader || !Ring.View.Commander || Ring.View.Commander == Ring.Pawn)
		{
			continue;
		}
		const double LineAge = FMath::Min(Now - Ring.View.JoinTime, Now - Ring.View.CommanderChangeTime);
		const FRingDraw* CommanderRing = Rings.FindByPredicate([&Ring](const FRingDraw& Other) { return Other.Pawn == Ring.View.Commander; });
		if (LineAge >= SquadLineTime || !CommanderRing)
		{
			continue;
		}
		const FVector Direction = (CommanderRing->Center - Ring.Center).GetSafeNormal2D();
		const FVector From = Ring.Center + Direction * Radius;
		const FVector To = CommanderRing->Center - Direction * Radius;
		if (FVector::DotProduct(To - From, Direction) <= 0.f)
		{
			continue;
		}
		FLinearColor Color = SquadCommanderColor;
		Color.A = FMath::Min(Ring.Opacity, CommanderRing->Opacity) * (1.f - static_cast<float>(LineAge / SquadLineTime));
		DrawWorldLine(From, To, FLinearColor(0.f, 0.f, 0.f, 0.5f * Color.A), 2.f * S + Outline);
		DrawWorldLine(From, To, Color, 2.f * S);
		++DrawnSquadLines;
	}

	// Линии приказов на земле, SquadOrderLineTime после приказа: сплошная — куда идти (рубеж, укрытие на фланге,
	// место сбора, свой, к кому послан), пунктир — по кому подавлять или куда бросить гранату. Тоже под кольцами.
	if (bShowSquadOrders && SquadOrderLineTime > 0.f)
	{
		// Точка приказа и враг — на пол под ними: линия лежит на земле, как и кольца.
		auto ToGround = [World](const FVector& Point, float FallbackZ)
		{
			FHitResult Hit;
			const FCollisionQueryParams Params(SCENE_QUERY_STAT(NoFearOrderLineGround), false);
			if (World->LineTraceSingleByObjectType(Hit, Point + FVector(0.f, 0.f, 60.f), Point - FVector(0.f, 0.f, 400.f), FCollisionObjectQueryParams(ECC_WorldStatic), Params))
			{
				return Hit.ImpactPoint + FVector(0.f, 0.f, 4.f);
			}
			return FVector(Point.X, Point.Y, FallbackZ);
		};

		for (const FRingDraw& Ring : Rings)
		{
			const FNoFearSquadMemberView& View = Ring.View;
			if (Ring.bPackLeader || (View.Order == ENoFearSquadOrder::None && !View.bGrenadeOrder))
			{
				continue;
			}
			const float Age = static_cast<float>(Now - View.OrderTime);
			if (Age < 0.f || Age >= SquadOrderLineTime)
			{
				continue;
			}
			// Быстро проявляется, вторую половину времени гаснет.
			const float Fade = FMath::Clamp(Age / 0.15f, 0.f, 1.f)
				* (1.f - FMath::Clamp((Age - SquadOrderLineTime * 0.5f) / (SquadOrderLineTime * 0.5f), 0.f, 1.f));
			const float Alpha = Ring.Opacity * Fade;
			if (Alpha <= 0.01f)
			{
				continue;
			}

			// Куда идти.
			bool bGoal = false;
			FVector Goal = FVector::ZeroVector;
			if (View.bHasOrderLocation)
			{
				Goal = ToGround(View.OrderLocation, Ring.Center.Z);
				bGoal = true;
			}
			else if (View.OrderTarget && View.OrderTarget != Ring.Pawn)
			{
				Goal = View.OrderTarget->GetActorLocation() - FVector(0.f, 0.f, View.OrderTarget->GetSimpleCollisionHalfHeight() - 4.f);
				bGoal = true;
			}
			if (bGoal && FVector::Dist2D(Ring.Center, Goal) > Radius * 2.f)
			{
				const FVector Direction = (Goal - Ring.Center).GetSafeNormal2D();
				const FVector From = Ring.Center + Direction * Radius;
				const FVector To = Goal - Direction * 20.f;
				FLinearColor Color = GetSquadRoleColor(View.Role);
				Color.A = Alpha;
				const FLinearColor Shadow(0.f, 0.f, 0.f, 0.45f * Alpha);
				DrawWorldLine(From, To, Shadow, 2.f * S + Outline);
				DrawWorldLine(From, To, Color, 2.f * S);
				// Наконечник у точки приказа и кружок на ней.
				const FVector Side = FVector(-Direction.Y, Direction.X, 0.f);
				for (const float Sign : { 1.f, -1.f })
				{
					const FVector Tip = To - Direction * 22.f + Side * (Sign * 12.f);
					DrawWorldLine(To, Tip, Shadow, 2.f * S + Outline);
					DrawWorldLine(To, Tip, Color, 2.f * S);
				}
				DrawGroundRing(Goal, 20.f, Color, 1.5f * S, true);
				++DrawnOrderLines;
			}

			// По кому стрелять или куда бросить гранату.
			const bool bFireOrder = View.Order == ENoFearSquadOrder::Suppress || View.Order == ENoFearSquadOrder::CoverFallback
				|| View.Order == ENoFearSquadOrder::CoveringFire;
			if ((bFireOrder || View.bGrenadeOrder) && View.bHasOrderEnemyLocation)
			{
				const FVector Enemy = ToGround(View.OrderEnemyLocation, Ring.Center.Z);
				if (FVector::Dist2D(Ring.Center, Enemy) > Radius * 2.f)
				{
					const FVector Direction = (Enemy - Ring.Center).GetSafeNormal2D();
					FLinearColor Color = View.bGrenadeOrder ? GrenadeColor : SquadFireLineColor;
					Color.A = Alpha * 0.85f;
					DrawWorldDashedLine(Ring.Center + Direction * Radius, Enemy, Color, 1.5f * S, 30.f);
					++DrawnOrderLines;
				}
			}
		}
	}

	for (const FRingDraw& Ring : Rings)
	{
		float Opacity = Ring.Opacity;
		if (Ring.bPackLeader)
		{
			FLinearColor Color = BotEnemyColor;
			Color.A *= Opacity;
			const FLinearColor Shadow(0.f, 0.f, 0.f, 0.45f * Opacity);
			for (const float R : { Radius + 6.f, Radius - 6.f })
			{
				DrawGroundRing(Ring.Center, R, Shadow, 2.5f * S + Outline);
				DrawGroundRing(Ring.Center, R, Color, 2.5f * S);
			}
			++DrawnSquadRings;
			continue;
		}

		const FNoFearSquadMemberView& View = Ring.View;
		if (View.bExecutingOrder)
		{
			// Выполняет приказ — кольцо пульсирует.
			Opacity *= 0.6f + 0.4f * (0.5f + 0.5f * FMath::Sin(static_cast<float>(Now) * 7.f));
		}
		if (Ring.bDim)
		{
			// Раненый отходит — вполсилы.
			Opacity *= 0.5f;
		}

		// Смена роли: перекраска со вспышкой; новый командир вспыхивает.
		FLinearColor Color = GetSquadRoleColor(View.Role);
		float Flash = 0.f;
		const float RoleT = static_cast<float>(Now - View.RoleChangeTime) / SquadRoleChangeTime;
		if (RoleT < 1.f)
		{
			Color = FMath::Lerp(GetSquadRoleColor(View.PreviousRole), Color, FMath::Clamp(RoleT, 0.f, 1.f));
			Flash = 1.f - FMath::Clamp(RoleT, 0.f, 1.f);
		}
		if (Ring.Pawn == View.Commander)
		{
			Flash = FMath::Max(Flash, 1.f - FMath::Clamp(static_cast<float>(Now - View.CommanderChangeTime) / 0.6f, 0.f, 1.f));
		}
		if (bShowSquadOrders && (View.Order != ENoFearSquadOrder::None || View.bGrenadeOrder))
		{
			// Новый приказ — кольцо вспыхивает.
			Flash = FMath::Max(Flash, 0.7f * (1.f - FMath::Clamp(static_cast<float>(Now - View.OrderTime) / SquadOrderFlashTime, 0.f, 1.f)));
		}
		Color = FMath::Lerp(Color, FLinearColor::White, Flash * 0.6f);
		Color.A = Opacity;
		const float Boost = 1.f + Flash;
		const FLinearColor Shadow(0.f, 0.f, 0.f, 0.45f * Opacity);

		// Сторона — тонкий внешний ободок.
		FLinearColor Border = Ring.bHostile ? BotEnemyColor : SquadAllyBorderColor;
		Border.A *= Opacity * 0.9f;
		DrawGroundRing(Ring.Center, Radius + 9.f, Border, 1.5f * S);

		switch (View.Role)
		{
		case ENoFearSquadRole::Commander:
			for (const float R : { Radius, Radius - 11.f })
			{
				DrawGroundRing(Ring.Center, R, Shadow, 2.5f * S * Boost + Outline);
				DrawGroundRing(Ring.Center, R, Color, 2.5f * S * Boost);
			}
			break;
		case ENoFearSquadRole::Assault:
			DrawGroundRing(Ring.Center, Radius, Shadow, 3.5f * S * Boost + Outline);
			DrawGroundRing(Ring.Center, Radius, Color, 3.5f * S * Boost);
			break;
		case ENoFearSquadRole::Support:
			DrawGroundRing(Ring.Center, Radius, Shadow, 3.f * S * Boost + Outline, true);
			DrawGroundRing(Ring.Center, Radius, Color, 3.f * S * Boost, true);
			break;
		default:
			DrawGroundRing(Ring.Center, Radius, Shadow, 1.5f * S * Boost + Outline);
			DrawGroundRing(Ring.Center, Radius, Color, 1.5f * S * Boost);
			break;
		}
		++DrawnSquadRings;
	}
}
