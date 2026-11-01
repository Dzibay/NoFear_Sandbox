// Порталы страха в HUD (этап 13): аура и сердце портала на земле, ступень и размер над провалом,
// кольца у ног проводников и тех, кто вот-вот сломается, кольцо «Освобождаю» у прицела.
// Остальной HUD — NoFearHUD.cpp, NoFearHUDCompanion.cpp и NoFearHUDBracelet.cpp.

#include "Core/NoFearHUD.h"

#include "AI/NoFearAIController.h"
#include "AI/NoFearBotSubsystem.h"
#include "Components/CapsuleComponent.h"
#include "Engine/Canvas.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/PlayerController.h"
#include "Portal/NoFearPortal.h"
#include "Portal/NoFearPortalSubsystem.h"

#define LOCTEXT_NAMESPACE "NoFearHUDPortal"

namespace NoFearHUDPortal
{
	/** Кольцо у ног проводника и дуга слома, см. */
	constexpr float FeederRingRadius = 55.f;
	constexpr float FreeArcRadius = 42.f;
	constexpr float BreakArcRadius = 50.f;
	/** Слом меньше этого не показываем: страх скачет у порога. */
	constexpr float BreakShowMin = 0.05f;

	FLinearColor Faded(FLinearColor Color, float Alpha)
	{
		Color.A *= Alpha;
		return Color;
	}

	/** Точка у ног персонажа, чуть над землёй. */
	FVector GetFeet(const AActor* Actor)
	{
		float HalfHeight = 90.f;
		if (const ACharacter* Character = Cast<ACharacter>(Actor); Character && Character->GetCapsuleComponent())
		{
			HalfHeight = Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
		}
		return Actor->GetActorLocation() - FVector(0.f, 0.f, HalfHeight - 4.f);
	}

	FText GetStageName(ENoFearPortalStage Stage)
	{
		switch (Stage)
		{
		case ENoFearPortalStage::Small:
			return LOCTEXT("StageSmall", "малый");
		case ENoFearPortalStage::Medium:
			return LOCTEXT("StageMedium", "средний");
		case ENoFearPortalStage::Large:
			return LOCTEXT("StageLarge", "большой");
		default:
			return LOCTEXT("StageRift", "трещина");
		}
	}
}

void ANoFearHUD::DrawPortals()
{
	using namespace NoFearHUDPortal;

	const UNoFearPortalSubsystem* Portals = UNoFearPortalSubsystem::Get(this);
	const APlayerController* PC = GetOwningPlayerController();
	const UWorld* World = GetWorld();
	if (!Portals || !PC || !Canvas || !World)
	{
		return;
	}
	const TArray<ANoFearPortal*> OpenPortals = Portals->GetPortals();

	FVector CameraLocation;
	FRotator CameraRotation;
	PC->GetPlayerViewPoint(CameraLocation, CameraRotation);
	const float S = GetUIScale();
	const float Time = static_cast<float>(World->GetTimeSeconds());
	const FNoFearPortalTuning& Tuning = *Portals;

	auto DistanceFade = [](float Distance, float MaxDistance)
	{
		return 1.f - FMath::Clamp((Distance - MaxDistance * 0.8f) / FMath::Max(MaxDistance * 0.2f, 1.f), 0.f, 1.f);
	};

	// Дуга на земле: Fraction круга по часовой стрелке (вид сверху) от дальней от камеры точки — на экране сверху.
	auto DrawGroundArc = [this, &CameraRotation](const FVector& Center, float Radius, float Fraction, const FLinearColor& Color, float Thickness)
	{
		Fraction = FMath::Clamp(Fraction, 0.f, 1.f);
		if (Fraction <= 0.f)
		{
			return;
		}
		const int32 Segments = FMath::Max(2, FMath::CeilToInt(28.f * Fraction));
		const float Start = FMath::DegreesToRadians(CameraRotation.Yaw);
		const float Sweep = UE_TWO_PI * Fraction;
		FVector Previous = Center + FVector(Radius * FMath::Cos(Start), Radius * FMath::Sin(Start), 0.f);
		for (int32 Index = 1; Index <= Segments; ++Index)
		{
			const float Angle = Start + Sweep * Index / Segments;
			const FVector Next = Center + FVector(Radius * FMath::Cos(Angle), Radius * FMath::Sin(Angle), 0.f);
			DrawWorldLine(Previous, Next, Color, Thickness);
			Previous = Next;
		}
	};

	for (const ANoFearPortal* Portal : OpenPortals)
	{
		const FVector Ground = Portal->GetActorLocation() + FVector(0.f, 0.f, 3.f);
		const float Fade = DistanceFade(FVector::Dist(CameraLocation, Ground), PortalMarkDistance) * (Portal->IsClosing() ? 0.5f : 1.f);
		if (Fade <= 0.01f)
		{
			continue;
		}

		// Аура: пунктир по краю, сплошное кольцо — сердце, где страх доводит до слома. Дышит с порталом.
		const float Breath = 0.75f + 0.25f * FMath::Sin(Time * (2.f + Portal->GetFeederCount()));
		const float Aura = Portals->GetAuraRadius(Portal);
		DrawGroundRing(Ground, Aura, Faded(PortalColor, 0.35f * Fade * Breath), 1.5f * S, true);
		DrawGroundRing(Ground, Aura * Tuning.AuraCoreFraction, Faded(PortalColor, 0.6f * Fade * Breath), 2.f * S, false);

		// Подпись над провалом: ступень, размер с отметками ступеней, проводники и монстры.
		const FVector Top = Portal->GetCoreLocation() + FVector(0.f, 0.f, Portal->GetCoreRadius() + 30.f);
		if (CameraRotation.UnrotateVector(Top - CameraLocation).X <= 10.f)
		{
			continue;
		}
		const FVector Projected = Canvas->Project(Top, false);
		const float Inset = 20.f * S;
		if (Projected.X < Inset || Projected.X > Canvas->SizeX - Inset || Projected.Y < Inset || Projected.Y > Canvas->SizeY - Inset)
		{
			continue;
		}

		const FString Title = FText::Format(LOCTEXT("PortalTitle", "ПОРТАЛ · {0}"),
			Portal->IsClosing() ? LOCTEXT("PortalClosing", "затягивается") : GetStageName(Portal->GetStage())).ToString();
		const int32 FeederCount = Portal->GetFeederCount();
		const int32 MonsterCount = Portal->GetMonsterCount();
		FString Info;
		if (FeederCount > 0 || MonsterCount > 0)
		{
			Info = FText::Format(LOCTEXT("PortalInfo", "проводники {0} · монстры {1}"), FText::AsNumber(FeederCount), FText::AsNumber(MonsterCount)).ToString();
		}

		const float TitleScale = 0.95f * S;
		const float InfoScale = 0.8f * S;
		float TitleWidth = 0.f;
		float TitleHeight = 0.f;
		GetTextSize(Title, TitleWidth, TitleHeight, GetFont(), TitleScale);
		float InfoWidth = 0.f;
		float InfoHeight = 0.f;
		if (!Info.IsEmpty())
		{
			GetTextSize(Info, InfoWidth, InfoHeight, GetFont(), InfoScale);
		}

		const float BarWidth = 90.f * S;
		const float BarHeight = 5.f * S;
		const float Gap = 4.f * S;
		float Y = Projected.Y;
		if (!Info.IsEmpty())
		{
			Y -= InfoHeight;
			DrawLabel(Info, Projected.X, Y, Faded(FLinearColor::White, 0.85f * Fade), InfoScale, ELabelAlign::Center);
			Y -= Gap;
		}
		Y -= BarHeight;
		const float BarX = Projected.X - BarWidth * 0.5f;
		DrawBar(BarX, Y, BarWidth, BarHeight, Portal->GetSize() / 100.f, PortalColor, -1.f, Fade);
		for (const float Threshold : { Tuning.SmallSize, Tuning.MediumSize, Tuning.LargeSize })
		{
			const float X = BarX + BarWidth * FMath::Clamp(Threshold / 100.f, 0.f, 1.f);
			DrawLine(X, Y - 2.f * S, X, Y + BarHeight + 2.f * S, Faded(FLinearColor::White, 0.7f * Fade), FMath::Max(1.f, S));
		}
		Y -= Gap + TitleHeight;
		DrawLabel(Title, Projected.X, Y, Faded(PortalColor, Fade), TitleScale, ELabelAlign::Center);
		++DrawnPortalLabels;
	}

	// Проводники: кольцо у ног пульсирует с порталом, внутри дуга — сколько уже освобождено.
	for (const ANoFearPortal* Portal : OpenPortals)
	{
		const float Pulse = 0.7f + 0.3f * FMath::Sin(Time * (2.f + 1.5f * FMath::Min(Portal->GetFeederCount(), 4)));
		for (const APawn* Feeder : Portal->GetFeeders())
		{
			const FVector Feet = GetFeet(Feeder);
			const float Fade = DistanceFade(FVector::Dist(CameraLocation, Feet), PortalPeopleDistance);
			if (Fade <= 0.01f)
			{
				continue;
			}
			DrawGroundRing(Feet, FeederRingRadius, Faded(PortalColor, 0.85f * Fade * Pulse), 2.f * S, false);
			DrawGroundArc(Feet, FreeArcRadius, Portals->GetFreeProgress(Feeder), Faded(CalmColor, Fade), 3.f * S);
			++DrawnFeederRings;
		}
	}

	// Кто вот-вот сломается: дуга слома у ног, мигает всё чаще. Ещё можно успеть успокоить.
	if (const UNoFearBotSubsystem* Bots = UNoFearBotSubsystem::Get(this))
	{
		for (const ANoFearAIController* Bot : Bots->GetBots())
		{
			const APawn* Pawn = Bot ? Bot->GetPawn() : nullptr;
			const float Break = Pawn ? Portals->GetBreakProgress(Pawn) : 0.f;
			if (Break < BreakShowMin || Portals->IsFeeder(Pawn))
			{
				continue;
			}
			const FVector Feet = GetFeet(Pawn);
			const float Fade = DistanceFade(FVector::Dist(CameraLocation, Feet), PortalPeopleDistance);
			if (Fade <= 0.01f)
			{
				continue;
			}
			const float Flicker = 0.65f + 0.35f * FMath::Sin(Time * (6.f + 14.f * Break));
			DrawGroundArc(Feet, BreakArcRadius, Break, Faded(FearTerrorColor, Fade * Flicker), 2.5f * S);
			++DrawnBreakRings;
		}
	}
}

void ANoFearHUD::DrawPortalFreeProgress()
{
	const UNoFearPortalSubsystem* Portals = UNoFearPortalSubsystem::Get(this);
	APawn* Feeder = nullptr;
	float Progress = 0.f;
	if (!Portals || !Canvas || !Portals->GetHeroFreeing(GetOwningPlayerController(), Feeder, Progress))
	{
		return;
	}

	const float S = GetUIScale();
	const FVector2D Center(Canvas->SizeX * 0.5f, Canvas->SizeY * 0.5f);
	const float Radius = 30.f * S;
	DrawRing(Center, Radius, 1.f, FLinearColor(0.f, 0.f, 0.f, 0.5f), 7.f * S);
	DrawRing(Center, Radius, 1.f, NoFearHUDPortal::Faded(PortalColor, 0.45f), 4.f * S);
	DrawRing(Center, Radius, Progress, CalmColor, 4.f * S);

	const FString Text = LOCTEXT("Freeing", "Освобождаю").ToString();
	float TextWidth = 0.f;
	float TextHeight = 0.f;
	GetTextSize(Text, TextWidth, TextHeight, GetFont(), 1.f * S);
	DrawLabel(Text, Center.X, Center.Y - Radius - 12.f * S - TextHeight, CalmColor, 1.f * S, ELabelAlign::Center);
	bPortalFreeDrawn = true;
}

#undef LOCTEXT_NAMESPACE
