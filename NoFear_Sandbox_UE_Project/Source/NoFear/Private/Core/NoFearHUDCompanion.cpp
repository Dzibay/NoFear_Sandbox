// Напарник в HUD: панель, метки, точка приказа, рация, колесо приказов, экран ранения, подъём упавшего.
// Остальной HUD — NoFearHUD.cpp.

#include "Core/NoFearHUD.h"

#include "AI/NoFearCompanionAIController.h"
#include "CanvasItem.h"
#include "Core/NoFearCharacter.h"
#include "Core/NoFearPlayerController.h"
#include "Engine/Canvas.h"
#include "GameFramework/Pawn.h"

#define LOCTEXT_NAMESPACE "NoFearHUDCompanion"

namespace
{
	/** Как в NoFearHUD.cpp: UCanvas::K2_DrawTriangle рисует непрозрачно, поэтому элемент собираем сами. */
	void DrawTranslucent(UCanvas* Canvas, const TArray<FCanvasUVTri>& Triangles)
	{
		if (!Canvas || Triangles.IsEmpty())
		{
			return;
		}
		FCanvasTriangleItem Item(FVector2D::ZeroVector, FVector2D::ZeroVector, FVector2D::ZeroVector, GWhiteTexture);
		Item.TriangleList = Triangles;
		Item.BlendMode = SE_BLEND_Translucent;
		Canvas->DrawItem(Item);
	}

	/** Точка на окружности: 0° — вверх, по часовой стрелке (ось Y экрана смотрит вниз). */
	FVector2D PointOnCircle(const FVector2D& Center, float Degrees, float Radius)
	{
		const float Radians = FMath::DegreesToRadians(Degrees);
		return Center + FVector2D(FMath::Sin(Radians), -FMath::Cos(Radians)) * Radius;
	}

	FLinearColor WithAlpha(FLinearColor Color, float Alpha)
	{
		Color.A *= Alpha;
		return Color;
	}

	FString GetCompanionName(const APawn* Pawn)
	{
		if (const ANoFearCharacter* Character = Cast<ANoFearCharacter>(Pawn))
		{
			return Character->GetCharacterDisplayName().ToString();
		}
		return Pawn ? Pawn->GetName() : FString();
	}

	/** Последние столько секунд строка рации гаснет. */
	constexpr float RadioFadeTime = 0.8f;
	/** Последние столько секунд гаснет отметка врага. */
	constexpr float MarkFadeTime = 1.5f;
}

// ---------------------------------------------------------------------------
// Панель и метки напарника
// ---------------------------------------------------------------------------

void ANoFearHUD::DrawCompanionPanel()
{
	const ANoFearPlayerController* PlayerController = Cast<ANoFearPlayerController>(GetOwningPlayerController());
	if (!PlayerController)
	{
		return;
	}
	const TArray<APawn*> Companions = PlayerController->GetCompanions();
	if (Companions.IsEmpty())
	{
		return;
	}

	const float S = GetUIScale();
	const float X = Margin * S;
	const float ContentX = X + 20.f * S;
	float Y = Margin * S;
	const APawn* PlayerPawn = GetOwningPawn();
	const FLinearColor Dim(0.8f, 0.8f, 0.8f, 0.85f);
	const float Time = GetWorld() ? static_cast<float>(GetWorld()->GetTimeSeconds()) : 0.f;

	DrawLabel(CompanionTitleText.ToString(), X, Y, Dim, 0.85f * S, ELabelAlign::Left);
	Y += 22.f * S;

	for (const APawn* Companion : Companions)
	{
		const ANoFearCompanionAIController* Controller = Cast<ANoFearCompanionAIController>(Companion->GetController());
		const UNoFearHealthComponent* Health = UNoFearHealthComponent::FindHealth(Companion);
		const bool bDowned = Health && Health->IsDowned();
		const FString Name = GetCompanionName(Companion);
		const FLinearColor NameColor = bDowned ? WithAlpha(DownedColor, 0.7f + 0.3f * FMath::Sin(Time * 6.f)) : CompanionColor;

		// Строка 1: значок, имя, расстояние.
		const float NameScale = 1.15f * S;
		float NameWidth = 0.f;
		float NameHeight = 0.f;
		GetTextSize(Name, NameWidth, NameHeight, GetFont(), NameScale);
		DrawDiamond(FVector2D(X + 7.f * S, Y + NameHeight * 0.5f), 6.f * S, NameColor);
		DrawLabel(Name, ContentX, Y, NameColor, NameScale, ELabelAlign::Left);
		if (PlayerPawn)
		{
			const FString Distance = FormatDistance(FVector::Dist(PlayerPawn->GetActorLocation(), Companion->GetActorLocation()));
			float DistanceWidth = 0.f;
			float DistanceHeight = 0.f;
			GetTextSize(Distance, DistanceWidth, DistanceHeight, GetFont(), 0.85f * S);
			DrawLabel(Distance, ContentX + NameWidth + 12.f * S, Y + NameHeight - DistanceHeight - 1.f * S, Dim, 0.85f * S, ELabelAlign::Left);
		}
		Y += NameHeight + 4.f * S;

		// Строка 2: здоровье (у упавшего — сколько осталось жить) и чем занят.
		const float BarWidth = 200.f * S;
		const float BarHeight = 8.f * S;
		FString Status;
		FLinearColor StatusColor(1.f, 1.f, 1.f, 0.9f);
		if (bDowned)
		{
			DrawBar(ContentX, Y, BarWidth, BarHeight, Health->GetDownedFraction(), DownedColor);
			if (Health->IsBeingRevived())
			{
				Status = FText::Format(LOCTEXT("PanelReviving", "поднимаете: {0}%"), FText::AsNumber(FMath::RoundToInt(Health->GetReviveProgress() * 100.f))).ToString();
				StatusColor = CompanionColor;
			}
			else
			{
				Status = FText::Format(LOCTEXT("PanelDowned", "РАНЕН, {0} с — держите E рядом"), FText::AsNumber(FMath::CeilToInt(Health->GetDownedTimeRemaining()))).ToString();
				StatusColor = DownedColor;
			}
		}
		else
		{
			const float Fraction = Health ? Health->GetHealthFraction() : 1.f;
			DrawBar(ContentX, Y, BarWidth, BarHeight, Fraction, FMath::Lerp(LowHealthColor, CompanionColor, FMath::Clamp(Fraction / 0.6f, 0.f, 1.f)));
			switch (Controller ? Controller->GetActivity() : ENoFearCompanionActivity::Idle)
			{
			case ENoFearCompanionActivity::Following:       Status = CompanionFollowingText.ToString(); break;
			case ENoFearCompanionActivity::Waiting:         Status = CompanionWaitingText.ToString(); break;
			case ENoFearCompanionActivity::Fighting:        Status = CompanionFightingText.ToString(); break;
			case ENoFearCompanionActivity::Sneaking:        Status = LOCTEXT("ActSneaking", "крадётся следом").ToString(); break;
			case ENoFearCompanionActivity::Moving:          Status = LOCTEXT("ActMoving", "идёт на точку").ToString(); break;
			case ENoFearCompanionActivity::ComingBack:      Status = LOCTEXT("ActComing", "бежит к вам").ToString(); break;
			case ENoFearCompanionActivity::Attacking:       Status = LOCTEXT("ActAttacking", "атакует цель").ToString(); break;
			case ENoFearCompanionActivity::ThrowingGrenade: Status = LOCTEXT("ActGrenade", "бросает гранату").ToString(); break;
			case ENoFearCompanionActivity::Supplying:       Status = LOCTEXT("ActSupplying", "несёт припасы").ToString(); break;
			case ENoFearCompanionActivity::Reviving:
				Status = Controller->GetLeaderReviveProgress() > 0.f ? LOCTEXT("ActReviving", "поднимает вас").ToString()
					: LOCTEXT("ActRunningToRevive", "бежит к вам").ToString();
				break;
			case ENoFearCompanionActivity::Dodging:         Status = LOCTEXT("ActDodging", "уходит от гранаты").ToString(); break;
			default:                                        Status = LOCTEXT("ActIdle", "на месте").ToString(); break;
			}
		}
		float StatusWidth = 0.f;
		float StatusHeight = 0.f;
		GetTextSize(Status, StatusWidth, StatusHeight, GetFont(), 0.85f * S);
		DrawLabel(Status, ContentX + BarWidth + 12.f * S, Y + BarHeight * 0.5f - StatusHeight * 0.5f, StatusColor, 0.85f * S, ELabelAlign::Left);
		Y += BarHeight + 8.f * S;

		// Строка 3: запас для игрока и режим огня.
		if (Controller && !bDowned)
		{
			FString Supply = FText::Format(LOCTEXT("PanelSupply", "магазинов {0}   бинтов {1}"),
				FText::AsNumber(Controller->GetSupplyMagazines()), FText::AsNumber(Controller->GetSupplyBandages())).ToString();
			if (Controller->IsHoldingFire())
			{
				Supply += LOCTEXT("PanelHoldFire", "   не стреляет").ToString();
			}
			float SupplyWidth = 0.f;
			float SupplyHeight = 0.f;
			GetTextSize(Supply, SupplyWidth, SupplyHeight, GetFont(), 0.8f * S);
			DrawLabel(Supply, ContentX, Y, Dim, 0.8f * S, ELabelAlign::Left);
			Y += SupplyHeight;
		}
		Y += 12.f * S;
	}
}

void ANoFearHUD::DrawCompanionMarkers()
{
	const ANoFearPlayerController* PlayerController = Cast<ANoFearPlayerController>(GetOwningPlayerController());
	if (!PlayerController || !Canvas)
	{
		return;
	}

	const APawn* PlayerPawn = GetOwningPawn();
	const float S = GetUIScale();
	const float Time = GetWorld() ? static_cast<float>(GetWorld()->GetTimeSeconds()) : 0.f;
	for (const APawn* Companion : PlayerController->GetCompanions())
	{
		const float Distance = PlayerPawn ? FVector::Dist(PlayerPawn->GetActorLocation(), Companion->GetActorLocation()) : 0.f;
		const UNoFearHealthComponent* Health = UNoFearHealthComponent::FindHealth(Companion);
		if (!Health || !Health->IsDowned())
		{
			const FString Caption = Distance > CompanionCaptionDistance ? FormatDistance(Distance) : FString();
			DrawWorldMarker(GetOverheadLocation(Companion), CompanionColor, 7.f * S, /*bEdgeArrow=*/true, Caption);
			continue;
		}

		// Упал: крупная пульсирующая красная метка, стрелка у края и таймер.
		const FVector Overhead = GetOverheadLocation(Companion);
		FString Caption = FText::Format(LOCTEXT("MarkerDowned", "РАНЕН  {0} с"), FText::AsNumber(FMath::CeilToInt(Health->GetDownedTimeRemaining()))).ToString();
		if (Distance > CompanionCaptionDistance)
		{
			Caption += TEXT("  ") + FormatDistance(Distance);
		}
		const FLinearColor Color = WithAlpha(DownedColor, 0.7f + 0.3f * FMath::Sin(Time * 6.f));
		if (DrawWorldMarker(Overhead, Color, 10.f * S, /*bEdgeArrow=*/true, Caption) && Health->IsBeingRevived())
		{
			const FVector Projected = Canvas->Project(Overhead, false);
			const FVector2D Center(Projected.X, Projected.Y);
			DrawRing(Center, 18.f * S, 1.f, FLinearColor(0.f, 0.f, 0.f, 0.5f), 5.f * S);
			DrawRing(Center, 18.f * S, Health->GetReviveProgress(), CompanionColor, 3.f * S);
		}
	}

	DrawCompanionMarks();
}

void ANoFearHUD::DrawCompanionMarks()
{
	const ANoFearPlayerController* PlayerController = Cast<ANoFearPlayerController>(GetOwningPlayerController());
	const ANoFearCompanionAIController* Companion = PlayerController ? PlayerController->GetMainCompanion() : nullptr;
	const UWorld* World = GetWorld();
	if (!Companion || !World || UNoFearHealthComponent::IsActorIncapacitated(Companion->GetPawn()))
	{
		return;
	}

	const double Now = World->GetTimeSeconds();
	const float S = GetUIScale();

	// Враги, которых видит напарник: где он их видел последний раз.
	for (const FNoFearCompanionMark& Mark : Companion->GetMarks())
	{
		const AActor* Actor = Mark.Actor.Get();
		if (!Actor || Now > Mark.Until || UNoFearHealthComponent::IsActorIncapacitated(Actor))
		{
			continue;
		}
		const float Alpha = FMath::Clamp(static_cast<float>(Mark.Until - Now) / MarkFadeTime, 0.f, 1.f);
		const FVector Location = Mark.Location + (GetOverheadLocation(Actor) - Actor->GetActorLocation());
		const FLinearColor Color = WithAlpha(Mark.bOrdered ? DownedColor : CompanionMarkColor, Alpha);
		const FString Caption = Mark.bOrdered ? LOCTEXT("MarkTarget", "ЦЕЛЬ").ToString() : FString();
		DrawWorldMarker(Location, Color, (Mark.bOrdered ? 8.f : 6.f) * S, /*bEdgeArrow=*/true, Caption);
	}

	// Точка приказа: куда идёт, куда бросит гранату.
	const ENoFearCompanionOrder Order = Companion->GetActiveOrder();
	FVector Point;
	if ((Order == ENoFearCompanionOrder::MoveTo || Order == ENoFearCompanionOrder::Grenade) && Companion->GetOrderPoint(Point))
	{
		const bool bGrenade = Order == ENoFearCompanionOrder::Grenade;
		const FLinearColor Color = bGrenade ? CompanionMarkColor : CompanionColor;
		DrawGroundRing(Point + FVector(0.f, 0.f, 5.f), bGrenade ? 300.f : 60.f, WithAlpha(Color, 0.85f), 2.f * S, /*bDashed=*/bGrenade);
		const FString Caption = bGrenade ? LOCTEXT("OrderGrenade", "граната").ToString() : LOCTEXT("OrderMove", "сюда").ToString();
		DrawWorldMarker(Point + FVector(0.f, 0.f, 60.f), Color, 5.f * S, /*bEdgeArrow=*/false, Caption);
	}
}

// ---------------------------------------------------------------------------
// Рация
// ---------------------------------------------------------------------------

void ANoFearHUD::DrawRadio()
{
	const ANoFearPlayerController* PlayerController = Cast<ANoFearPlayerController>(GetOwningPlayerController());
	const UWorld* World = GetWorld();
	if (!PlayerController || !World || PlayerController->GetRadioLines().IsEmpty())
	{
		return;
	}

	const double Now = World->GetTimeSeconds();
	const float S = GetUIScale();
	const float Scale = 1.f * S;
	const float X = Margin * S;
	// Слева посередине: над здоровьем, под панелью напарника.
	float Y = Canvas->SizeY * 0.56f;
	for (const FNoFearRadioLine& Line : PlayerController->GetRadioLines())
	{
		const float Age = static_cast<float>(Now - Line.Time);
		if (Age > PlayerController->RadioLineTime)
		{
			continue;
		}
		const float Alpha = FMath::Clamp((PlayerController->RadioLineTime - Age) / RadioFadeTime, 0.f, 1.f);

		const FString Name = Line.SpeakerName.IsEmpty() ? FString() : Line.SpeakerName.ToString() + TEXT(": ");
		FString Text = Line.Text.ToString();
		if (Line.bWhisper)
		{
			Text = LOCTEXT("Whisper", "(шёпотом) ").ToString() + Text;
		}
		float NameWidth = 0.f;
		float NameHeight = 0.f;
		float TextWidth = 0.f;
		float TextHeight = 0.f;
		if (!Name.IsEmpty())
		{
			GetTextSize(Name, NameWidth, NameHeight, GetFont(), Scale);
		}
		GetTextSize(Text, TextWidth, TextHeight, GetFont(), Scale);

		const float Pad = 6.f * S;
		DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.4f * Alpha), X - Pad, Y - Pad * 0.5f, NameWidth + TextWidth + Pad * 2.f, TextHeight + Pad);
		const bool bFromCompanion = Line.Speaker.IsValid() || !Line.SpeakerName.IsEmpty();
		if (!Name.IsEmpty())
		{
			DrawLabel(Name, X, Y, WithAlpha(CompanionColor, Alpha), Scale, ELabelAlign::Left);
		}
		const FLinearColor TextColor = !bFromCompanion ? FLinearColor(1.f, 0.85f, 0.55f, 1.f)
			: (Line.bWhisper ? FLinearColor(0.8f, 0.8f, 0.8f, 0.85f) : FLinearColor::White);
		DrawLabel(Text, X + NameWidth, Y, WithAlpha(TextColor, Alpha), Scale, ELabelAlign::Left);
		Y += TextHeight + Pad + 4.f * S;
	}
}

// ---------------------------------------------------------------------------
// Ранение и подъём
// ---------------------------------------------------------------------------

void ANoFearHUD::DrawDownedScreen()
{
	const UNoFearHealthComponent* Health = BoundHealth.Get();
	if (!Health || !Health->IsDowned() || !Canvas)
	{
		return;
	}

	const float S = GetUIScale();
	const float W = Canvas->SizeX;
	const float H = Canvas->SizeY;
	const float Fraction = FMath::Clamp(Health->GetDownedFraction(), 0.f, 1.f);

	// Края темнеют и сходятся к центру, пока уходит время.
	const FLinearColor Outer(0.f, 0.f, 0.f, FMath::Lerp(0.6f, 0.92f, 1.f - Fraction));
	const FLinearColor Inner(0.f, 0.f, 0.f, 0.f);
	const float T = H * FMath::Lerp(0.26f, 0.42f, 1.f - Fraction);
	DrawColoredQuad(FVector2D(0.f, 0.f), FVector2D(W, 0.f), FVector2D(W, T), FVector2D(0.f, T), Outer, Outer, Inner, Inner);
	DrawColoredQuad(FVector2D(0.f, H), FVector2D(W, H), FVector2D(W, H - T), FVector2D(0.f, H - T), Outer, Outer, Inner, Inner);
	DrawColoredQuad(FVector2D(0.f, 0.f), FVector2D(0.f, H), FVector2D(T, H), FVector2D(T, 0.f), Outer, Outer, Inner, Inner);
	DrawColoredQuad(FVector2D(W, 0.f), FVector2D(W, H), FVector2D(W - T, H), FVector2D(W - T, 0.f), Outer, Outer, Inner, Inner);

	// Заголовок, таймер, кто поднимает.
	const float CenterX = W * 0.5f;
	float Y = H * 0.14f;
	const FString Title = LOCTEXT("DownedTitle", "ВЫ РАНЕНЫ").ToString();
	float TitleWidth = 0.f;
	float TitleHeight = 0.f;
	GetTextSize(Title, TitleWidth, TitleHeight, GetFont(), 2.2f * S);
	DrawLabel(Title, CenterX, Y, DownedColor, 2.2f * S, ELabelAlign::Center);
	Y += TitleHeight + 10.f * S;

	const float BarWidth = 320.f * S;
	const float BarHeight = 8.f * S;
	DrawBar(CenterX - BarWidth * 0.5f, Y, BarWidth, BarHeight, Fraction, DownedColor);
	const FString Seconds = FText::Format(LOCTEXT("DownedSeconds", "{0} с"), FText::AsNumber(FMath::CeilToInt(Health->GetDownedTimeRemaining()))).ToString();
	float SecondsWidth = 0.f;
	float SecondsHeight = 0.f;
	GetTextSize(Seconds, SecondsWidth, SecondsHeight, GetFont(), 0.9f * S);
	DrawLabel(Seconds, CenterX + BarWidth * 0.5f + 10.f * S, Y + BarHeight * 0.5f - SecondsHeight * 0.5f, FLinearColor::White, 0.9f * S, ELabelAlign::Left);
	Y += BarHeight + 14.f * S;

	const ANoFearPlayerController* PlayerController = Cast<ANoFearPlayerController>(GetOwningPlayerController());
	const ANoFearCompanionAIController* Companion = PlayerController ? PlayerController->GetMainCompanion() : nullptr;
	const APawn* CompanionPawn = Companion ? Companion->GetPawn() : nullptr;
	const APawn* PlayerPawn = GetOwningPawn();
	FString Help;
	FLinearColor HelpColor = FLinearColor::White;
	if (Health->IsBeingRevived())
	{
		Help = FText::Format(LOCTEXT("DownedReviving", "{0} поднимает вас"), FText::FromString(GetCompanionName(CompanionPawn))).ToString();
		HelpColor = CompanionColor;
		const FVector2D Center(CenterX, H * 0.5f);
		DrawRing(Center, 34.f * S, 1.f, FLinearColor(0.f, 0.f, 0.f, 0.5f), 7.f * S);
		DrawRing(Center, 34.f * S, Health->GetReviveProgress(), CompanionColor, 4.f * S);
	}
	else if (CompanionPawn && PlayerPawn && !UNoFearHealthComponent::IsActorIncapacitated(CompanionPawn))
	{
		Help = FText::Format(LOCTEXT("DownedHelpComing", "{0} идёт на помощь — {1}"), FText::FromString(GetCompanionName(CompanionPawn)),
			FText::FromString(FormatDistance(FVector::Dist(PlayerPawn->GetActorLocation(), CompanionPawn->GetActorLocation())))).ToString();
		HelpColor = CompanionColor;
	}
	else
	{
		Help = LOCTEXT("DownedNoHelp", "Помощи нет").ToString();
		HelpColor = DownedColor;
	}
	float HelpWidth = 0.f;
	float HelpHeight = 0.f;
	GetTextSize(Help, HelpWidth, HelpHeight, GetFont(), 1.1f * S);
	DrawLabel(Help, CenterX, Y, HelpColor, 1.1f * S, ELabelAlign::Center);
	Y += HelpHeight + 6.f * S;
	DrawLabel(LOCTEXT("DownedHint", "Можно отстреливаться и ползти. Перезарядки и гранат нет.").ToString(), CenterX, Y, FLinearColor(0.85f, 0.85f, 0.85f, 0.8f), 0.85f * S, ELabelAlign::Center);
}

void ANoFearHUD::DrawReviveProgress()
{
	const ANoFearPlayerController* PlayerController = Cast<ANoFearPlayerController>(GetOwningPlayerController());
	const APawn* Target = PlayerController ? PlayerController->GetReviveTarget() : nullptr;
	if (!Target || !Canvas)
	{
		return;
	}

	const float S = GetUIScale();
	const FVector2D Center(Canvas->SizeX * 0.5f, Canvas->SizeY * 0.5f);
	const float Radius = 30.f * S;
	DrawRing(Center, Radius, 1.f, FLinearColor(0.f, 0.f, 0.f, 0.5f), 7.f * S);
	DrawRing(Center, Radius, PlayerController->GetReviveProgress(), CompanionColor, 4.f * S);

	const FString Text = FText::Format(LOCTEXT("Reviving", "Поднимаю: {0}"), FText::FromString(GetCompanionName(Target))).ToString();
	float TextWidth = 0.f;
	float TextHeight = 0.f;
	GetTextSize(Text, TextWidth, TextHeight, GetFont(), 1.f * S);
	DrawLabel(Text, Center.X, Center.Y - Radius - 12.f * S - TextHeight, CompanionColor, 1.f * S, ELabelAlign::Center);
}

void ANoFearHUD::DrawCalmProgress()
{
	const ANoFearPlayerController* PlayerController = Cast<ANoFearPlayerController>(GetOwningPlayerController());
	const APawn* Target = PlayerController ? PlayerController->GetCalmTarget() : nullptr;
	if (!Target || !Canvas)
	{
		return;
	}

	const float S = GetUIScale();
	const FVector2D Center(Canvas->SizeX * 0.5f, Canvas->SizeY * 0.5f);
	const float Radius = 30.f * S;
	DrawRing(Center, Radius, 1.f, FLinearColor(0.f, 0.f, 0.f, 0.5f), 7.f * S);
	DrawRing(Center, Radius, PlayerController->GetCalmProgress(), CalmColor, 4.f * S);

	const FString Text = LOCTEXT("Calming", "Успокаиваю").ToString();
	float TextWidth = 0.f;
	float TextHeight = 0.f;
	GetTextSize(Text, TextWidth, TextHeight, GetFont(), 1.f * S);
	DrawLabel(Text, Center.X, Center.Y - Radius - 12.f * S - TextHeight, CalmColor, 1.f * S, ELabelAlign::Center);
}

// ---------------------------------------------------------------------------
// Колесо приказов
// ---------------------------------------------------------------------------

void ANoFearHUD::DrawWedge(const FVector2D& Center, float Inner, float Outer, float From, float To, const FLinearColor& Color)
{
	constexpr int32 Segments = 8;
	TArray<FCanvasUVTri> Triangles;
	Triangles.Reserve(Segments * 2);
	auto AddTriangle = [&Triangles, &Color](const FVector2D& A, const FVector2D& B, const FVector2D& C)
	{
		FCanvasUVTri Tri;
		Tri.V0_Pos = A;
		Tri.V1_Pos = B;
		Tri.V2_Pos = C;
		Tri.V0_Color = Color;
		Tri.V1_Color = Color;
		Tri.V2_Color = Color;
		Triangles.Add(Tri);
	};
	for (int32 Index = 0; Index < Segments; ++Index)
	{
		const float A = FMath::Lerp(From, To, static_cast<float>(Index) / Segments);
		const float B = FMath::Lerp(From, To, static_cast<float>(Index + 1) / Segments);
		const FVector2D InnerA = PointOnCircle(Center, A, Inner);
		const FVector2D OuterA = PointOnCircle(Center, A, Outer);
		const FVector2D OuterB = PointOnCircle(Center, B, Outer);
		const FVector2D InnerB = PointOnCircle(Center, B, Inner);
		AddTriangle(InnerA, OuterA, OuterB);
		AddTriangle(InnerA, OuterB, InnerB);
	}
	DrawTranslucent(Canvas, Triangles);
}

void ANoFearHUD::DrawOrderWheel()
{
	const ANoFearPlayerController* PlayerController = Cast<ANoFearPlayerController>(GetOwningPlayerController());
	if (!PlayerController || !PlayerController->IsOrderWheelOpen() || !Canvas)
	{
		return;
	}
	const TArray<FNoFearOrderWheelEntry>& Entries = PlayerController->GetOrderWheelEntries();
	const int32 Count = Entries.Num();
	if (Count == 0)
	{
		return;
	}

	const float S = GetUIScale();
	const FVector2D Center(Canvas->SizeX * 0.5f, Canvas->SizeY * 0.5f);
	const float Inner = 80.f * S;
	const float Outer = 250.f * S;
	const float SectorAngle = 360.f / Count;
	const float Gap = 1.2f;
	const int32 Selection = PlayerController->GetOrderWheelSelection();

	// Время замедлено — экран чуть темнее.
	DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.25f), 0.f, 0.f, Canvas->SizeX, Canvas->SizeY);

	// Куда смотрит прицел: точка для «Иди туда» / «Граната», враг для «Атакуй».
	FVector AimPoint;
	if (PlayerController->GetOrderAimPoint(AimPoint))
	{
		DrawGroundRing(AimPoint + FVector(0.f, 0.f, 5.f), 60.f, FLinearColor(1.f, 1.f, 1.f, 0.85f), 2.f * S);
	}
	if (const AActor* AimEnemy = PlayerController->GetOrderAimEnemy())
	{
		const ANoFearCharacter* EnemyCharacter = Cast<ANoFearCharacter>(AimEnemy);
		DrawWorldMarker(GetOverheadLocation(AimEnemy), DownedColor, 8.f * S, /*bEdgeArrow=*/false,
			EnemyCharacter ? EnemyCharacter->GetCharacterDisplayName().ToString() : FString());
	}

	for (int32 Index = 0; Index < Count; ++Index)
	{
		const FNoFearOrderWheelEntry& Entry = Entries[Index];
		const bool bSelected = Index == Selection;
		const float Angle = Index * SectorAngle;
		FLinearColor Fill(0.f, 0.f, 0.f, 0.6f);
		if (bSelected)
		{
			Fill = Entry.bEnabled ? WithAlpha(CompanionColor, 0.55f) : FLinearColor(0.4f, 0.4f, 0.4f, 0.55f);
		}
		DrawWedge(Center, Inner, bSelected ? Outer + 12.f * S : Outer, Angle - SectorAngle * 0.5f + Gap, Angle + SectorAngle * 0.5f - Gap, Fill);

		const FLinearColor LabelColor = !Entry.bEnabled ? FLinearColor(0.6f, 0.6f, 0.6f, 0.6f) : FLinearColor::White;
		const FVector2D LabelCenter = PointOnCircle(Center, Angle, (Inner + Outer) * 0.5f + 8.f * S);
		const FString Label = Entry.Label.ToString();
		const FString Detail = Entry.Detail.ToString();
		float LabelWidth = 0.f;
		float LabelHeight = 0.f;
		float DetailWidth = 0.f;
		float DetailHeight = 0.f;
		GetTextSize(Label, LabelWidth, LabelHeight, GetFont(), 1.05f * S);
		if (!Detail.IsEmpty())
		{
			GetTextSize(Detail, DetailWidth, DetailHeight, GetFont(), 0.75f * S);
		}
		const float Top = LabelCenter.Y - (LabelHeight + DetailHeight) * 0.5f;
		DrawLabel(Label, LabelCenter.X, Top, LabelColor, 1.05f * S, ELabelAlign::Center);
		if (!Detail.IsEmpty())
		{
			DrawLabel(Detail, LabelCenter.X, Top + LabelHeight, WithAlpha(LabelColor, 0.75f), 0.75f * S, ELabelAlign::Center);
		}
	}

	// Указатель в сторону выбора — у внутреннего края, центр остаётся прозрачным для прицела.
	const FVector2D Cursor = PlayerController->GetOrderWheelCursor();
	if (!Cursor.IsNearlyZero())
	{
		const FVector2D Direction = FVector2D(Cursor.X, -Cursor.Y).GetSafeNormal();
		DrawDiamond(Center + Direction * (Inner - 14.f * S), 6.f * S, CompanionColor);
	}
	DrawRing(Center, Inner, 1.f, FLinearColor(1.f, 1.f, 1.f, 0.25f), 1.5f * S);

	// Под колесом — кому приказ и как отменить.
	const ANoFearCompanionAIController* Companion = PlayerController->GetMainCompanion();
	float Y = Center.Y + Outer + 24.f * S;
	const FString Name = GetCompanionName(Companion ? Companion->GetPawn() : nullptr);
	if (!Name.IsEmpty())
	{
		float NameWidth = 0.f;
		float NameHeight = 0.f;
		GetTextSize(Name, NameWidth, NameHeight, GetFont(), 1.1f * S);
		DrawLabel(Name, Center.X, Y, CompanionColor, 1.1f * S, ELabelAlign::Center);
		Y += NameHeight + 2.f * S;
	}
	const FText Hint = Selection == INDEX_NONE ? LOCTEXT("WheelHintPick", "Мышь — выбрать приказ, отпустить F — отдать")
		: LOCTEXT("WheelHintCancel", "Вернуть мышь в центр — отменить");
	DrawLabel(Hint.ToString(), Center.X, Y, FLinearColor(0.85f, 0.85f, 0.85f, 0.85f), 0.85f * S, ELabelAlign::Center);
}

#undef LOCTEXT_NAMESPACE
