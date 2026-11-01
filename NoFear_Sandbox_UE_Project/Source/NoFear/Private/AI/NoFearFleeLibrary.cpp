#include "AI/NoFearFleeLibrary.h"
#include "Core/NoFearPerf.h"

#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "NavigationPath.h"
#include "NavigationSystem.h"

namespace
{
	struct FFleeCandidate
	{
		FVector Location;
		float Score;
	};

	/** Расстояние в плоскости от точки до отрезка. */
	float DistToSegment2D(const FVector& Point, const FVector& A, const FVector& B)
	{
		const FVector P(Point.X, Point.Y, 0.f);
		return FVector::Dist(P, FMath::ClosestPointOnSegment(P, FVector(A.X, A.Y, 0.f), FVector(B.X, B.Y, 0.f)));
	}
}

bool UNoFearFleeLibrary::IsLocationSafe(const FVector& Location, const TArray<FVector>& Threats, float MinThreatDistance)
{
	for (const FVector& Threat : Threats)
	{
		if (FVector::DistSquared2D(Location, Threat) < FMath::Square(MinThreatDistance))
		{
			return false;
		}
	}
	return true;
}

bool UNoFearFleeLibrary::FindFleeLocation(APawn* Pawn, const TArray<FVector>& Threats, FVector& OutLocation,
	float MinDistance, float MaxDistance, float SafePathDistance, int32 NumSamples)
{
	NOFEAR_PERF_SCOPE(FindLocation);
	FNoFearSearchBudget::Consume(ENoFearSearchKind::Location);
	UWorld* World = Pawn ? Pawn->GetWorld() : nullptr;
	UNavigationSystemV1* NavSys = World ? FNavigationSystem::GetCurrent<UNavigationSystemV1>(World) : nullptr;
	if (!NavSys)
	{
		return false;
	}

	const FVector Origin = Pawn->GetActorLocation();

	// Направление «прочь»: сумма направлений от угроз, ближние весят больше.
	FVector Away = FVector::ZeroVector;
	for (const FVector& Threat : Threats)
	{
		const FVector FromThreat = (Origin - Threat) * FVector(1.f, 1.f, 0.f);
		const float Dist = FMath::Max(FromThreat.Size(), 50.f);
		Away += FromThreat / Dist / Dist;
	}
	Away = Away.GetSafeNormal();

	// 1. Кольцо точек вокруг пешки, спроецированных на навмеш.
	TArray<FFleeCandidate> Candidates;
	const int32 Samples = FMath::Max(NumSamples, 8);
	const float AngleOffset = FMath::FRand() * 360.f / Samples;
	for (int32 Index = 0; Index < Samples; ++Index)
	{
		const float Angle = FMath::DegreesToRadians(AngleOffset + 360.f * Index / Samples);
		const FVector Dir(FMath::Cos(Angle), FMath::Sin(Angle), 0.f);
		const FVector Raw = Origin + Dir * FMath::FRandRange(MinDistance, MaxDistance);

		FNavLocation NavLocation;
		if (!NavSys->ProjectPointToNavigation(Raw, NavLocation, FVector(150.f, 150.f, 400.f)))
		{
			continue;
		}
		const FVector Point = NavLocation.Location;
		const FVector ToPoint = (Point - Origin) * FVector(1.f, 1.f, 0.f);
		if (ToPoint.Size() < MinDistance * 0.6f)
		{
			continue;
		}
		const FVector ToPointDir = ToPoint.GetSafeNormal();

		// 2. Фильтр: не бежать в сторону угрозы.
		bool bTowardThreat = false;
		float NearestThreat = TNumericLimits<float>::Max();
		bool bHidden = true;
		for (const FVector& Threat : Threats)
		{
			const FVector ToThreat = (Threat - Origin) * FVector(1.f, 1.f, 0.f);
			if (FVector::DotProduct(ToThreat.GetSafeNormal(), ToPointDir) > 0.6f)
			{
				bTowardThreat = true;
				break;
			}
			NearestThreat = FMath::Min(NearestThreat, FVector::Dist2D(Point, Threat));
			// Видит ли угроза точку: луч по статике уровня.
			FCollisionQueryParams Params(SCENE_QUERY_STAT(NoFearFleeSight), false);
			if (!World->LineTraceTestByObjectType(Threat + FVector(0.f, 0.f, 60.f), Point + FVector(0.f, 0.f, 60.f),
				FCollisionObjectQueryParams(ECC_WorldStatic), Params))
			{
				bHidden = false;
			}
		}
		if (bTowardThreat)
		{
			continue;
		}

		// 3. Оценка: дальше от угроз, по направлению «прочь», за укрытием.
		float Score = FMath::Min(NearestThreat / 1000.f, 3.f);
		if (!Away.IsNearlyZero())
		{
			Score += 1.5f * FVector::DotProduct(ToPointDir, Away);
		}
		if (bHidden && Threats.Num() > 0)
		{
			Score += 1.f;
		}
		Candidates.Add({ Point, Score });
	}

	Candidates.Sort([](const FFleeCandidate& A, const FFleeCandidate& B) { return A.Score > B.Score; });

	// 4. Путь к лучшим точкам не должен проходить мимо угрозы.
	const int32 MaxPathChecks = FMath::Min(Candidates.Num(), 8);
	for (int32 Index = 0; Index < MaxPathChecks; ++Index)
	{
		const FVector Point = Candidates[Index].Location;
		UNavigationPath* Path = UNavigationSystemV1::FindPathToLocationSynchronously(World, Origin, Point, Pawn);
		if (!Path || !Path->IsValid() || Path->IsPartial() || Path->PathPoints.Num() < 2)
		{
			continue;
		}

		bool bPathSafe = true;
		for (const FVector& Threat : Threats)
		{
			// Если угроза уже вплотную, требование мягче: важно хотя бы не бежать сквозь неё.
			const float Clearance = FMath::Min(SafePathDistance, 0.8f * FVector::Dist2D(Origin, Threat));
			for (int32 Seg = 0; Seg + 1 < Path->PathPoints.Num() && bPathSafe; ++Seg)
			{
				bPathSafe = DistToSegment2D(Threat, Path->PathPoints[Seg], Path->PathPoints[Seg + 1]) >= Clearance;
			}
			if (!bPathSafe)
			{
				break;
			}
		}
		if (bPathSafe)
		{
			OutLocation = Point;
			return true;
		}
	}
	return false;
}
