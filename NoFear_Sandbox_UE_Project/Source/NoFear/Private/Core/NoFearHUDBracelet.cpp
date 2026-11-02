// Браслет героя в HUD (этап 12): дуга страха над здоровьем, способности Q и Alt, ступор, стрелка на разлом.
// Остальной HUD — NoFearHUD.cpp и NoFearHUDCompanion.cpp.

#include "Core/NoFearHUD.h"

#include "AI/NoFearMonsterKinds.h"
#include "AI/NoFearOpportunitySubsystem.h"
#include "Core/NoFearAISettings.h"
#include "Core/NoFearBraceletComponent.h"
#include "Core/NoFearFearComponent.h"
#include "Core/NoFearFearSubsystem.h"
#include "Core/NoFearPlayerController.h"
#include "Engine/Canvas.h"
#include "GameFramework/Pawn.h"

#define LOCTEXT_NAMESPACE "NoFearHUDBracelet"

namespace NoFearHUDBracelet
{
	/** 0° — вверх, по часовой стрелке (как DrawWedge). */
	FVector2D Point(const FVector2D& Center, float Degrees, float Radius)
	{
		const float Radians = FMath::DegreesToRadians(Degrees);
		return Center + FVector2D(FMath::Sin(Radians), -FMath::Cos(Radians)) * Radius;
	}

	FLinearColor Faded(FLinearColor Color, float Alpha)
	{
		Color.A *= Alpha;
		return Color;
	}

	/** Дуга шкалы: от -90° (слева) через верх до +90° (справа). */
	float FearToAngle(float Fear)
	{
		return -90.f + 180.f * FMath::Clamp(Fear / 100.f, 0.f, 1.f);
	}
}

void ANoFearHUD::DrawArc(const FVector2D& Center, float Inner, float Outer, float From, float To, const FLinearColor& Color)
{
	const int32 Chunks = FMath::Max(1, FMath::CeilToInt(FMath::Abs(To - From) / 15.f));
	for (int32 Index = 0; Index < Chunks; ++Index)
	{
		DrawWedge(Center, Inner, Outer,
			FMath::Lerp(From, To, static_cast<float>(Index) / Chunks),
			FMath::Lerp(From, To, static_cast<float>(Index + 1) / Chunks), Color);
	}
}

void ANoFearHUD::DrawBracelet()
{
	using namespace NoFearHUDBracelet;

	const ANoFearPlayerController* PlayerController = Cast<ANoFearPlayerController>(GetOwningPlayerController());
	const UNoFearBraceletComponent* Bracelet = PlayerController ? PlayerController->GetBracelet() : nullptr;
	if (!Bracelet || !Bracelet->GetHeroFear() || !Canvas)
	{
		return;
	}

	const UNoFearFearSubsystem* FearSubsystem = UNoFearFearSubsystem::Get(this);
	const FNoFearFearTuning& FearTuning = FearSubsystem ? static_cast<const FNoFearFearTuning&>(*FearSubsystem) : UNoFearAISettings::Get()->Fear;

	const float S = GetUIScale();
	const float Fear = Bracelet->GetFear();
	const float Pulse = Bracelet->GetHeartPulse();
	const bool bStupor = Bracelet->IsInStupor();
	const float Time = GetWorld() ? static_cast<float>(GetWorld()->GetTimeSeconds()) : 0.f;

	// Над числом здоровья: та же раскладка, что в DrawHealth.
	float Unused = 0.f;
	float HealthNumberHeight = 0.f;
	GetTextSize(TEXT("100"), Unused, HealthNumberHeight, GetFont(), 2.2f * S);
	const float HealthTop = Canvas->SizeY - Margin * S - 16.f * S - 10.f * S - HealthNumberHeight;

	const float Radius = 62.f * S;
	const float Thickness = 9.f * S;
	const FVector2D Center(Margin * S + Radius, HealthTop - 12.f * S);

	FLinearColor FillColor = BraceletCalmColor;
	if (Fear >= FearTuning.TerrorThreshold)
	{
		FillColor = FearTerrorColor;
	}
	else if (Fear >= FearTuning.PanicThreshold)
	{
		FillColor = FearPanicColor;
	}
	else if (Fear >= FearTuning.AnxiousThreshold)
	{
		FillColor = FearAnxiousColor;
	}

	// Подложка, заполнение и отблеск удара сердца по внешнему краю.
	DrawArc(Center, Radius - Thickness - 2.f * S, Radius + 2.f * S, -90.f, 90.f, FLinearColor(0.f, 0.f, 0.f, 0.45f));
	DrawArc(Center, Radius - Thickness, Radius, -90.f, 90.f, FLinearColor(0.15f, 0.15f, 0.15f, 0.6f));
	if (Fear > 0.5f)
	{
		DrawArc(Center, Radius - Thickness, Radius, -90.f, FearToAngle(Fear), FillColor);
	}
	if (Pulse > 0.01f && (Fear >= Bracelet->GetTuning().HeartbeatStartFear || bStupor))
	{
		DrawArc(Center, Radius + 2.f * S, Radius + 2.f * S + 4.f * S * Pulse, -90.f, FearToAngle(FMath::Max(Fear, 2.f)), Faded(FillColor, 0.6f * Pulse));
	}

	// Риски порогов.
	for (const float Threshold : { FearTuning.AnxiousThreshold, FearTuning.PanicThreshold, FearTuning.TerrorThreshold })
	{
		const float Angle = FearToAngle(Threshold);
		DrawWedge(Center, Radius - Thickness - 3.f * S, Radius + 3.f * S, Angle - 0.7f, Angle + 0.7f, FLinearColor(0.f, 0.f, 0.f, 0.85f));
	}

	// Число страха и подпись внутри дуги; число вздрагивает с ударом сердца.
	const FString Number = FString::FromInt(FMath::RoundToInt(Fear));
	const float NumberScale = 1.7f * S * (1.f + 0.08f * Pulse);
	const float LabelScale = 0.75f * S;
	float NumberHeight = 0.f;
	float LabelHeight = 0.f;
	GetTextSize(Number, Unused, NumberHeight, GetFont(), NumberScale);
	GetTextSize(BraceletLabel.ToString(), Unused, LabelHeight, GetFont(), LabelScale);
	const float NumberY = Center.Y - NumberHeight + 2.f * S;
	DrawLabel(Number, Center.X, NumberY, Fear >= FearTuning.AnxiousThreshold ? FillColor : FLinearColor::White, NumberScale, ELabelAlign::Center);
	DrawLabel(BraceletLabel.ToString(), Center.X, NumberY - LabelHeight + 2.f * S, FLinearColor(0.8f, 0.8f, 0.8f, 0.8f), LabelScale, ELabelAlign::Center);

	// Справа: Q — стабилизация (кольцо готовности), Alt — запас дыхания.
	const float SideX = Center.X + Radius + 18.f * S;
	const float KeyScale = 0.85f * S;
	const float HintScale = 0.75f * S;
	{
		const float RingRadius = 13.f * S;
		const FVector2D RingCenter(SideX + RingRadius, Center.Y - 44.f * S);
		const float Ready = Bracelet->GetStabilizeReadyFraction();
		const bool bReady = Ready >= 1.f;
		const bool bActive = Bracelet->IsStabilizing();
		const FLinearColor RingColor = bActive ? Faded(CalmColor, 0.7f + 0.3f * FMath::Sin(Time * 10.f))
			: (bReady ? CalmColor : FLinearColor(0.6f, 0.6f, 0.6f, 0.8f));
		DrawRing(RingCenter, RingRadius, 1.f, FLinearColor(0.f, 0.f, 0.f, 0.5f), 6.f * S);
		DrawRing(RingCenter, RingRadius, bActive ? 1.f : Ready, RingColor, 3.f * S);

		float KeyHeight = 0.f;
		GetTextSize(TEXT("Q"), Unused, KeyHeight, GetFont(), KeyScale);
		DrawLabel(TEXT("Q"), RingCenter.X, RingCenter.Y - KeyHeight * 0.5f, bReady || bActive ? FLinearColor::White : FLinearColor(0.7f, 0.7f, 0.7f, 0.8f), KeyScale, ELabelAlign::Center);

		const FString Hint = bReady || bActive ? LOCTEXT("Stabilize", "Стабилизация").ToString()
			: FString::Printf(TEXT("%d с"), FMath::CeilToInt(Bracelet->GetStabilizeCooldownRemaining()));
		float HintHeight = 0.f;
		GetTextSize(Hint, Unused, HintHeight, GetFont(), HintScale);
		DrawLabel(Hint, RingCenter.X + RingRadius + 8.f * S, RingCenter.Y - HintHeight * 0.5f, FLinearColor(0.85f, 0.85f, 0.85f, bReady || bActive ? 0.9f : 0.6f), HintScale, ELabelAlign::Left);
	}
	{
		float KeyWidth = 0.f;
		float KeyHeight = 0.f;
		GetTextSize(TEXT("Alt"), KeyWidth, KeyHeight, GetFont(), KeyScale);
		const float BarY = Center.Y - 12.f * S;
		const bool bHolding = Bracelet->IsHoldingBreath();
		const bool bLocked = Bracelet->IsBreathLocked();
		const FLinearColor BreathColor = bLocked ? FLinearColor(0.8f, 0.25f, 0.2f, 0.7f)
			: (bHolding ? CalmColor : FLinearColor(0.85f, 0.85f, 0.85f, 0.75f));
		DrawLabel(TEXT("Alt"), SideX, BarY + 3.f * S - KeyHeight * 0.5f, bLocked ? FLinearColor(0.7f, 0.7f, 0.7f, 0.6f) : FLinearColor::White, KeyScale, ELabelAlign::Left);
		DrawBar(SideX + KeyWidth + 8.f * S, BarY, 80.f * S, 6.f * S, Bracelet->GetBreath(), BreathColor, 0.f, bHolding ? 1.f : 0.75f);
	}

	// Ступор: мигающая надпись над дугой.
	if (bStupor)
	{
		const float Flash = 0.55f + 0.45f * FMath::Sin(Time * 14.f);
		const float StuporScale = 1.3f * S;
		float StuporHeight = 0.f;
		GetTextSize(StuporText.ToString(), Unused, StuporHeight, GetFont(), StuporScale);
		DrawLabel(StuporText.ToString(), Center.X, Center.Y - Radius - 10.f * S - StuporHeight, Faded(FearTerrorColor, Flash), StuporScale, ELabelAlign::Center);
	}

	bBraceletDrawn = true;

	DrawPinEscape(SideX, Center.Y - 80.f * S);

	if (Bracelet->HasRiftWarning())
	{
		DrawRiftWarning(Bracelet);
	}
}

void ANoFearHUD::DrawPinEscape(float X, float Y)
{
	using namespace NoFearHUDBracelet;

	const ANoFearJumperAIController* Pinner = ANoFearJumperAIController::FindPinner(GetOwningPawn());
	if (!Pinner || !Canvas)
	{
		return;
	}
	const float S = GetUIScale();
	const float Time = GetWorld() ? static_cast<float>(GetWorld()->GetTimeSeconds()) : 0.f;
	const float Flash = 0.6f + 0.4f * FMath::Sin(Time * 16.f);
	const FString Key = NSLOCTEXT("NoFearHUD", "SpaceKey", "Пробел").ToString();
	const float KeyScale = 0.95f * S;
	float KeyWidth = 0.f;
	float KeyHeight = 0.f;
	GetTextSize(Key, KeyWidth, KeyHeight, GetFont(), KeyScale);

	// Пробел мигает; полоска — сколько нажатий набрано за окно.
	DrawLabel(Key, X, Y - KeyHeight * 0.5f, Faded(FLinearColor::White, Flash), KeyScale, ELabelAlign::Left);
	const float BarX = X + KeyWidth + 8.f * S;
	DrawBar(BarX, Y - 4.f * S, 90.f * S, 8.f * S, Pinner->GetEscapeProgress(), Faded(FearPanicColor, 0.6f + 0.4f * Flash), 0.f, 1.f);
	const float HintScale = 0.75f * S;
	float HintHeight = 0.f;
	float Unused = 0.f;
	GetTextSize(BreakFreeText.ToString(), Unused, HintHeight, GetFont(), HintScale);
	DrawLabel(BreakFreeText.ToString(), BarX + 98.f * S, Y - HintHeight * 0.5f, FLinearColor(0.9f, 0.9f, 0.9f, 0.9f), HintScale, ELabelAlign::Left);
	bPinEscapeDrawn = true;
}

void ANoFearHUD::DrawSpitterOverlays()
{
	using namespace NoFearHUDBracelet;

	const APawn* Pawn = GetOwningPawn();
	if (!Pawn || !Canvas)
	{
		return;
	}
	const float W = Canvas->SizeX;
	const float H = Canvas->SizeY;
	const float Time = GetWorld() ? static_cast<float>(GetWorld()->GetTimeSeconds()) : 0.f;

	// Слизь: зелёные подтёки сверху по краям, сползают и бледнеют к концу.
	const float SlimeLeft = UNoFearOpportunitySubsystem::GetTimeRemaining(Pawn, ENoFearOpportunity::Slimed);
	if (SlimeLeft > 0.f || UNoFearOpportunitySubsystem::HasTag(Pawn, ENoFearOpportunity::Slimed))
	{
		const float Alpha = SlimeLeft > 0.f ? FMath::Clamp(SlimeLeft / 1.5f, 0.f, 1.f) : 1.f;
		for (int32 Index = 0; Index < 14; ++Index)
		{
			// Половина у левого края, половина у правого; длина и ширина «случайные», но стабильные.
			const float Seed = FMath::Frac(FMath::Sin(Index * 12.9898f) * 43758.5453f);
			const bool bLeft = Index % 2 == 0;
			const float Offset = (0.01f + 0.16f * Seed) * W;
			const float StreakX = bLeft ? Offset : W - Offset;
			const float Width = (10.f + 26.f * FMath::Frac(Seed * 7.1f)) * GetUIScale();
			const float Length = H * (0.15f + 0.45f * FMath::Frac(Seed * 3.3f)) + 20.f * FMath::Sin(Time * 0.7f + Index);
			const FLinearColor Top = Faded(SlimeColor, Alpha);
			const FLinearColor Bottom = Faded(SlimeColor, 0.f);
			DrawColoredQuad(FVector2D(StreakX - Width * 0.5f, 0.f), FVector2D(StreakX + Width * 0.5f, 0.f),
				FVector2D(StreakX + Width * 0.3f, Length), FVector2D(StreakX - Width * 0.3f, Length), Top, Top, Bottom, Bottom);
			// Капля на конце подтёка.
			DrawArc(FVector2D(StreakX, Length), 0.f, Width * 0.45f, -180.f, 180.f, Faded(SlimeColor, Alpha * 0.7f));
		}
		bSlimeOverlayDrawn = true;
	}

	// Ослепление: тёмные пятна по центру, закрывают прицел и редеют к концу.
	const float BlindLeft = UNoFearOpportunitySubsystem::GetTimeRemaining(Pawn, ENoFearOpportunity::Blinded);
	if (BlindLeft > 0.f || UNoFearOpportunitySubsystem::HasTag(Pawn, ENoFearOpportunity::Blinded))
	{
		const float Alpha = BlindLeft > 0.f ? FMath::Clamp(BlindLeft / 0.8f, 0.f, 1.f) : 1.f;
		const FVector2D Center(W * 0.5f, H * 0.5f);
		for (int32 Index = 0; Index < 7; ++Index)
		{
			const float Seed = FMath::Frac(FMath::Sin(Index * 78.233f) * 12345.678f);
			const FVector2D Spot = Center + FVector2D((Seed - 0.5f) * W * 0.45f, (FMath::Frac(Seed * 5.7f) - 0.5f) * H * 0.4f);
			const float Radius = H * (0.12f + 0.12f * FMath::Frac(Seed * 9.1f));
			DrawArc(Spot, 0.f, Radius * 0.7f, -180.f, 180.f, Faded(BlindColor, Alpha));
			DrawArc(Spot, Radius * 0.7f, Radius, -180.f, 180.f, Faded(BlindColor, Alpha * 0.5f));
		}
		bBlindOverlayDrawn = true;
	}
}

void ANoFearHUD::DrawRiftWarning(const UNoFearBraceletComponent* Bracelet)
{
	using namespace NoFearHUDBracelet;

	APlayerController* PlayerController = GetOwningPlayerController();
	const APawn* Pawn = GetOwningPawn();
	if (!PlayerController || !Pawn || !Canvas)
	{
		return;
	}

	FVector ViewLocation;
	FRotator ViewRotation;
	PlayerController->GetPlayerViewPoint(ViewLocation, ViewRotation);
	const FVector ToRift = Bracelet->GetRiftLocation() - Pawn->GetActorLocation();
	const float Angle = FRotator::NormalizeAxis(ToRift.Rotation().Yaw - ViewRotation.Yaw);

	const float S = GetUIScale();
	const float Alpha = FMath::Clamp(Bracelet->GetRiftWarningAlpha() * 1.5f, 0.f, 1.f);
	const FLinearColor Color = Faded(RiftColor, Alpha);
	const FVector2D Center(Canvas->SizeX * 0.5f, Canvas->SizeY * 0.2f);

	// Стрелка-шеврон по направлению на разлом (вверх — прямо по взгляду).
	const FVector2D Tip = Point(Center, Angle, 26.f * S);
	const FVector2D Left = Point(Center, Angle - 140.f, 18.f * S);
	const FVector2D Notch = Point(Center, Angle + 180.f, 5.f * S);
	const FVector2D Right = Point(Center, Angle + 140.f, 18.f * S);
	DrawArc(Center, 30.f * S, 33.f * S, -180.f, 180.f, Faded(FLinearColor(0.f, 0.f, 0.f, 0.5f), Alpha));
	DrawColoredQuad(Tip, Left, Notch, Right, Color, Color, Color, Color);

	const FString Text = FText::Format(RiftWarningText, FText::AsNumber(FMath::RoundToInt(ToRift.Size2D() / 100.f))).ToString();
	DrawLabel(Text, Center.X, Center.Y + 40.f * S, Color, 1.1f * S, ELabelAlign::Center);

	bRiftWarningDrawn = true;
}

#undef LOCTEXT_NAMESPACE
