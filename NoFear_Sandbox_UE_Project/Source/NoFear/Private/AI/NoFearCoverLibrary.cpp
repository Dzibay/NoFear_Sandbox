#include "AI/NoFearCoverLibrary.h"
#include "Core/NoFearPerf.h"

#include "AI/NoFearAIController.h"
#include "AI/NoFearBotSubsystem.h"
#include "CollisionQueryParams.h"
#include "Engine/HitResult.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Character.h"
#include "NavigationPath.h"
#include "NavigationSystem.h"

namespace
{
	/** Расстояние в плоскости от точки до отрезка. */
	float CoverDistToSegment2D(const FVector& Point, const FVector& A, const FVector& B)
	{
		const FVector P(Point.X, Point.Y, 0.f);
		return FVector::Dist(P, FMath::ClosestPointOnSegment(P, FVector(A.X, A.Y, 0.f), FVector(B.X, B.Y, 0.f)));
	}

	float PathLength(const TArray<FVector>& Points)
	{
		float Length = 0.f;
		for (int32 Index = 0; Index + 1 < Points.Num(); ++Index)
		{
			Length += FVector::Dist(Points[Index], Points[Index + 1]);
		}
		return Length;
	}

	/** Угол у угрозы между направлениями на A и на B в плоскости, градусы. */
	float AngleAtThreat(const FVector& Threat, const FVector& A, const FVector& B)
	{
		const FVector ToA = (A - Threat).GetSafeNormal2D();
		const FVector ToB = (B - Threat).GetSafeNormal2D();
		return FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(static_cast<float>(FVector::DotProduct(ToA, ToB)), -1.f, 1.f)));
	}

	/**
	 * Замеры пути (каждые Step см, кроме первых Skip см), видные с глаз угрозы на уровне груди: всего замеров,
	 * сколько видно и самый длинный открытый участок подряд.
	 */
	int32 CountVisiblePathSamples(const UWorld* World, const TArray<FVector>& Points, const FVector& ThreatEyes,
		const FCollisionQueryParams& Params, float Step, float Skip, int32* OutSamples = nullptr, int32* OutLongestRun = nullptr)
	{
		int32 Visible = 0;
		int32 Samples = 0;
		int32 Run = 0;
		int32 LongestRun = 0;
		float Travelled = 0.f;
		float NextSample = Skip;
		for (int32 Index = 0; Index + 1 < Points.Num(); ++Index)
		{
			const FVector A = Points[Index];
			const FVector B = Points[Index + 1];
			const float Length = FVector::Dist(A, B);
			while (NextSample <= Travelled + Length)
			{
				const FVector Sample = FMath::Lerp(A, B, Length > 0.f ? (NextSample - Travelled) / Length : 0.f);
				++Samples;
				if (!World->LineTraceTestByChannel(Sample + FVector(0.f, 0.f, 110.f), ThreatEyes, ECC_Visibility, Params))
				{
					++Visible;
					LongestRun = FMath::Max(LongestRun, ++Run);
				}
				else
				{
					Run = 0;
				}
				NextSample += Step;
			}
			Travelled += Length;
		}
		if (OutSamples)
		{
			*OutSamples = Samples;
		}
		if (OutLongestRun)
		{
			*OutLongestRun = LongestRun;
		}
		return Visible;
	}
}

ENoFearCoverType UNoFearCoverLibrary::EvaluateCover(const UWorld* World, const FVector& GroundPoint, const FVector& ThreatEyes,
	const FCollisionQueryParams& Params, ECollisionChannel Channel, float* OutObstacleDistance, float LowHeight, float HighHeight)
{
	if (OutObstacleDistance)
	{
		*OutObstacleDistance = 0.f;
	}
	if (!World)
	{
		return ENoFearCoverType::None;
	}

	FHitResult LowHit;
	if (!World->LineTraceSingleByChannel(LowHit, GroundPoint + FVector(0.f, 0.f, LowHeight), ThreatEyes, Channel, Params))
	{
		return ENoFearCoverType::None;
	}
	if (OutObstacleDistance)
	{
		*OutObstacleDistance = LowHit.Distance;
	}
	const bool bHighBlocked = World->LineTraceTestByChannel(GroundPoint + FVector(0.f, 0.f, HighHeight), ThreatEyes, Channel, Params);
	return bHighBlocked ? ENoFearCoverType::High : ENoFearCoverType::Low;
}

FCollisionQueryParams UNoFearCoverLibrary::MakeCoverTraceParams(const UWorld* World)
{
	FCollisionQueryParams Params(SCENE_QUERY_STAT(NoFearCover), false);
	if (!World)
	{
		return Params;
	}
	TArray<AActor*> Ignored;
	for (TActorIterator<ACharacter> It(const_cast<UWorld*>(World)); It; ++It)
	{
		Ignored.Add(*It);
		It->GetAttachedActors(Ignored, false, true);
	}
	Params.AddIgnoredActors(Ignored);
	return Params;
}

bool UNoFearCoverLibrary::FindPeekLocation(const UWorld* World, const FVector& CoverLocation, const FVector& ThreatEyes,
	const FCollisionQueryParams& Params, FVector& OutPeek)
{
	UNavigationSystemV1* NavSys = World ? FNavigationSystem::GetCurrent<UNavigationSystemV1>(const_cast<UWorld*>(World)) : nullptr;
	if (!NavSys)
	{
		return false;
	}
	const FVector ToThreat = (ThreatEyes - CoverLocation).GetSafeNormal2D();
	const FVector Side(-ToThreat.Y, ToThreat.X, 0.f);
	static const float Offsets[] = { 60.f, 100.f, 140.f, 180.f };
	for (const float Offset : Offsets)
	{
		for (const float Sign : { 1.f, -1.f })
		{
			const FVector Wanted = CoverLocation + Side * (Offset * Sign);
			FNavLocation NavLocation;
			if (!NavSys->ProjectPointToNavigation(Wanted, NavLocation, FVector(40.f, 40.f, 150.f))
				|| FVector::Dist2D(NavLocation.Location, Wanted) > 40.f)
			{
				continue;
			}
			const FVector Peek = NavLocation.Location;
			// Между укрытием и точкой выглядывания не должно быть стены, а оттуда стоя должно быть видно угрозу.
			if (World->LineTraceTestByChannel(CoverLocation + FVector(0.f, 0.f, 50.f), Peek + FVector(0.f, 0.f, 50.f), ECC_Visibility, Params)
				|| World->LineTraceTestByChannel(Peek + FVector(0.f, 0.f, HighCoverHeight), ThreatEyes, ECC_Visibility, Params))
			{
				continue;
			}
			OutPeek = Peek;
			return true;
		}
	}
	return false;
}

FString UNoFearCoverLibrary::LastSearchStats;

bool UNoFearCoverLibrary::FindCover(const FNoFearCoverSearch& Search, FNoFearCoverPoint& OutCover)
{
	NOFEAR_PERF_SCOPE(Cover);
	FNoFearSearchBudget::Consume(ENoFearSearchKind::Cover);
	APawn* Pawn = Search.Pawn;
	UWorld* World = Pawn ? Pawn->GetWorld() : nullptr;
	UNavigationSystemV1* NavSys = World ? FNavigationSystem::GetCurrent<UNavigationSystemV1>(const_cast<UWorld*>(World)) : nullptr;
	LastSearchStats = TEXT("no nav");
	if (!NavSys)
	{
		return false;
	}
	// Отладка (GetLastSearchStats): сколько точек отсеял каждый фильтр.
	struct FStats
	{
		int32 Points = 0, Range = 0, Leash = 0, Retreat = 0, Angle = 0, Taken = 0, NoCover = 0, Far = 0, NoPeek = 0;
		int32 NoPath = 0, Long = 0, Close = 0, Seen = 0;
	} Stats;

	const FVector Origin = Pawn->GetActorLocation();
	// Кольца — вокруг заданной точки (подкрепление) или вокруг себя; расстояние пути и отхода — всё равно от пешки.
	const FVector RingCenter = Search.bUseSearchOrigin ? Search.SearchOrigin : Origin;
	const FVector Threat = Search.ThreatLocation;
	const FVector ThreatEyes = Threat + FVector(0.f, 0.f, ThreatEyeHeight);
	const float CurrentThreatDistance = FVector::Dist2D(Origin, Threat);
	const float CurrentGoalDistance = FVector::Dist2D(Origin, Search.GoalLocation);
	const float GoalThreatDistance = FVector::Dist2D(Search.GoalLocation, Threat);
	const FCollisionQueryParams Params = MakeCoverTraceParams(World);

	const UNoFearBotSubsystem* Bots = World->GetSubsystem<UNoFearBotSubsystem>();
	TArray<FVector> OtherBots;
	if (Bots)
	{
		for (const ANoFearAIController* Bot : Bots->GetBots())
		{
			if (Bot && Bot->GetPawn() && Bot->GetPawn() != Pawn)
			{
				OtherBots.Add(Bot->GetPawn()->GetActorLocation());
			}
		}
	}

	TArray<FNoFearCoverPoint> Candidates;
	// Проверить точку; препятствие далеко — один раз подвинуться к нему (bAllowSnap).
	TFunction<void(const FVector&, bool)> TryPoint;
	TryPoint = [&](const FVector& Point, bool bAllowSnap)
	{
		// 2. Фильтры без лучей: дистанция до угрозы, поводок, отход, бронь и другие боты.
		const float ThreatDistance = FVector::Dist2D(Point, Threat);
		if (ThreatDistance < Search.MinThreatDistance
			|| (Search.MaxThreatDistance > 0.f && ThreatDistance > Search.MaxThreatDistance))
		{
			++Stats.Range;
			return;
		}
		if (Search.LeashRadius > 0.f && FVector::Dist2D(Point, Search.LeashAnchor) > Search.LeashRadius)
		{
			++Stats.Leash;
			return;
		}
		if (Search.bRetreat && Search.GoalGain > 0.f)
		{
			// К рубежу: каждый перекат ближе к нему и не навстречу врагу (иначе отошедший вбок уходил бы от рубежа всё дальше).
			if (FVector::Dist2D(Point, Search.GoalLocation) > CurrentGoalDistance - Search.GoalGain
				|| ThreatDistance < FMath::Min(CurrentThreatDistance, GoalThreatDistance) - 200.f)
			{
				++Stats.Retreat;
				return;
			}
		}
		else if (Search.bRetreat && ThreatDistance < CurrentThreatDistance + Search.RetreatGain)
		{
			++Stats.Retreat;
			return;
		}
		const float FlankAngle = Search.MinFlankAngle > 0.f ? AngleAtThreat(Threat, Search.FlankFrom, Point) : 0.f;
		if (FlankAngle < Search.MinFlankAngle)
		{
			++Stats.Angle;
			return;
		}
		if (Bots && Bots->IsCoverReserved(Point, Search.ReserveRadius, Pawn))
		{
			++Stats.Taken;
			return;
		}
		bool bCrowded = false;
		for (const FVector& Other : OtherBots)
		{
			if (FVector::DistSquared2D(Point, Other) < FMath::Square(Search.ReserveRadius))
			{
				bCrowded = true;
				break;
			}
		}
		if (bCrowded)
		{
			++Stats.Taken;
			return;
		}

		// 3. Лучи к глазам угрозы: укрытие вплотную, низкое или высокое.
		float ObstacleDistance = 0.f;
		const ENoFearCoverType Type = EvaluateCover(World, Point, ThreatEyes, Params, ECC_Visibility, &ObstacleDistance);
		if (Type == ENoFearCoverType::None)
		{
			++Stats.NoCover;
			return;
		}
		if (Search.MaxObstacleDistance > 0.f && ObstacleDistance > Search.MaxObstacleDistance)
		{
			++Stats.Far;
			// Угроза закрыта, но препятствие далеко: подойти к нему вплотную и проверить ещё раз.
			const FVector RayStart = Point + FVector(0.f, 0.f, LowCoverHeight);
			const FVector Ray = ThreatEyes - RayStart;
			const float Horizontal = ObstacleDistance * Ray.Size2D() / FMath::Max(Ray.Size(), 1.f) - 50.f;
			FNavLocation Snapped;
			const FVector Wanted = Point + Ray.GetSafeNormal2D() * Horizontal;
			if (bAllowSnap && Horizontal > 0.f && NavSys->ProjectPointToNavigation(Wanted, Snapped, FVector(40.f, 40.f, 200.f))
				&& FVector::Dist2D(Snapped.Location, Wanted) < 40.f)
			{
				TryPoint(Snapped.Location, false);
			}
			return;
		}

		FNoFearCoverPoint Cover;
		Cover.Location = Point;
		Cover.Type = Type;
		if (Type == ENoFearCoverType::Low)
		{
			// Из-за низкого укрытия стреляют стоя, если стоя действительно видно угрозу.
			Cover.PeekLocation = Point;
			Cover.bCanPeek = !World->LineTraceTestByChannel(Point + FVector(0.f, 0.f, HighCoverHeight), ThreatEyes, ECC_Visibility, Params);
		}
		else
		{
			Cover.bCanPeek = FindPeekLocation(World, Point, ThreatEyes, Params, Cover.PeekLocation);
		}
		if (Search.MinFlankAngle > 0.f && !Cover.bCanPeek)
		{
			// С фланга нужно стрелять, а не пережидать.
			++Stats.NoPeek;
			return;
		}

		// 4. Оценка.
		float Score;
		if (Search.bRetreat)
		{
			Score = Type == ENoFearCoverType::High ? 1.f : 0.8f;
			// Чем больше выигрыш (к рубежу или прочь от врага), тем лучше.
			const float Gain = Search.GoalGain > 0.f ? CurrentGoalDistance - FVector::Dist2D(Point, Search.GoalLocation)
				: ThreatDistance - CurrentThreatDistance;
			Score += 0.6f * FMath::Min(Gain / 1000.f, 1.5f);
		}
		else
		{
			Score = Cover.bCanPeek ? (Type == ENoFearCoverType::High ? 1.f : 0.95f) : 0.45f;
			if (ThreatDistance < Search.PreferredRange.X)
			{
				Score -= 0.6f * (Search.PreferredRange.X - ThreatDistance) / FMath::Max(Search.PreferredRange.X, 1.f);
			}
			else if (Search.PreferredRange.Y > 0.f && ThreatDistance > Search.PreferredRange.Y)
			{
				Score -= 0.6f * (ThreatDistance - Search.PreferredRange.Y) / Search.PreferredRange.Y;
			}
		}
		Score -= 0.5f * FVector::Dist2D(RingCenter, Point) / FMath::Max(Search.SearchRadius, 1.f);
		if (Search.bUseSearchOrigin)
		{
			// Из равных у цели — та, что ближе к себе (не обегать прижатого своего).
			Score -= 0.2f * FVector::Dist2D(Origin, Point) / 2000.f;
		}
		if (Search.GoalWeight > 0.f)
		{
			Score -= Search.GoalWeight * FVector::Dist2D(Point, Search.GoalLocation) / 1000.f;
		}
		if (Search.MinFlankAngle > 0.f)
		{
			// Чем круче фланг, тем лучше: перекрёстный огонь.
			Score += 0.4f * FMath::Min((FlankAngle - Search.MinFlankAngle) / 60.f, 1.f);
		}
		Score += FMath::FRand() * 0.05f;
		Cover.Score = Score;
		Candidates.Add(Cover);
	};

	// 1. Кольца точек вокруг пешки (и сама её точка: если она уже в укрытии, можно остаться).
	constexpr float RingStep = 150.f;
	const int32 RingCount = FMath::Max(1, FMath::FloorToInt(Search.SearchRadius / RingStep));
	const float AngleOffset = FMath::FRand() * 360.f;
	for (int32 Ring = 0; Ring <= RingCount; ++Ring)
	{
		const float Radius = Ring * RingStep;
		const int32 Samples = Ring == 0 ? 1 : FMath::Clamp(FMath::RoundToInt(2.f * PI * Radius / 180.f), 6, FMath::Max(Search.MaxRingSamples, 6));
		for (int32 Index = 0; Index < Samples; ++Index)
		{
			const float Angle = FMath::DegreesToRadians(AngleOffset + Ring * 17.f + 360.f * Index / Samples);
			const FVector Raw = RingCenter + FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0.f) * Radius;

			FNavLocation NavLocation;
			if (NavSys->ProjectPointToNavigation(Raw, NavLocation, FVector(60.f, 60.f, 300.f)))
			{
				++Stats.Points;
				TryPoint(NavLocation.Location, true);
			}
		}
	}

	Candidates.Sort([](const FNoFearCoverPoint& A, const FNoFearCoverPoint& B) { return A.Score > B.Score; });

	// 5. Путь к лучшим: есть, не слишком кружной и не мимо угрозы.
	const float Clearance = FMath::Min(Search.MinThreatDistance * 0.6f, 0.8f * CurrentThreatDistance);
	const int32 MaxPathChecks = FMath::Min(Candidates.Num(), FMath::Max(Search.MaxPathChecks, 1));
	// Путь — от точки навмеша под ногами, а не от центра капсулы: иначе запрос иногда не находит старт.
	FVector PathStart = Origin;
	FNavLocation OriginNav;
	if (NavSys->ProjectPointToNavigation(Origin, OriginNav, FVector(100.f, 100.f, 250.f)))
	{
		PathStart = OriginNav.Location;
	}
	const auto WriteStats = [&Stats, &Candidates]()
	{
		LastSearchStats = FString::Printf(TEXT("pts=%d range=%d leash=%d retreat=%d angle=%d taken=%d nocover=%d far=%d nopeek=%d cand=%d | nopath=%d long=%d close=%d seen=%d"),
			Stats.Points, Stats.Range, Stats.Leash, Stats.Retreat, Stats.Angle, Stats.Taken, Stats.NoCover, Stats.Far, Stats.NoPeek,
			Candidates.Num(), Stats.NoPath, Stats.Long, Stats.Close, Stats.Seen);
	};
	for (int32 Index = 0; Index < MaxPathChecks; ++Index)
	{
		const FNoFearCoverPoint& Cover = Candidates[Index];
		if (FVector::Dist2D(Origin, Cover.Location) < 60.f)
		{
			OutCover = Cover;
			WriteStats();
			return true;
		}
		UNavigationPath* Path = UNavigationSystemV1::FindPathToLocationSynchronously(World, PathStart, Cover.Location, Pawn);
		if (!Path || !Path->IsValid() || Path->IsPartial() || Path->PathPoints.Num() < 2)
		{
			++Stats.NoPath;
			continue;
		}
		if (PathLength(Path->PathPoints) > Search.MaxPathLengthFactor * FVector::Dist(Origin, Cover.Location) + 300.f)
		{
			++Stats.Long;
			continue;
		}
		bool bPathSafe = true;
		for (int32 Seg = 0; Seg + 1 < Path->PathPoints.Num() && bPathSafe; ++Seg)
		{
			bPathSafe = CoverDistToSegment2D(Threat, Path->PathPoints[Seg], Path->PathPoints[Seg + 1]) >= Clearance;
		}
		if (!bPathSafe)
		{
			++Stats.Close;
			continue;
		}
		if (Search.bHiddenPath)
		{
			// Скрытный обход: угроза видит идущего лишь урывками (один промельк допустим всегда), без долгой открытой полосы.
			int32 Samples = 0;
			int32 LongestRun = 0;
			const int32 Visible = CountVisiblePathSamples(World, Path->PathPoints, ThreatEyes, Params, 200.f, 300.f, &Samples, &LongestRun);
			if (Visible > FMath::Max(1, FMath::FloorToInt(Samples * Search.HiddenPathMaxVisible))
				|| LongestRun > FMath::Max(1, Search.HiddenPathMaxRun))
			{
				++Stats.Seen;
				continue;
			}
		}
		OutCover = Cover;
		WriteStats();
		return true;
	}
	WriteStats();
	return false;
}

ENoFearCoverType UNoFearCoverLibrary::TestCoverAt(const UObject* WorldContextObject, FVector Location, FVector ThreatLocation)
{
	const UWorld* World = WorldContextObject ? WorldContextObject->GetWorld() : nullptr;
	if (!World)
	{
		return ENoFearCoverType::None;
	}
	return EvaluateCover(World, Location, ThreatLocation + FVector(0.f, 0.f, ThreatEyeHeight), MakeCoverTraceParams(World), ECC_Visibility);
}
