#include "AI/EnvQueryTest_NoFearCover.h"

#include "AI/EnvQueryContext_NoFearThreat.h"
#include "AI/NoFearCombatComponent.h"
#include "AI/NoFearBotSubsystem.h"
#include "AI/NoFearCombatSubsystem.h"
#include "AI/NoFearCoverLibrary.h"
#include "AIController.h"
#include "CollisionQueryParams.h"
#include "Engine/HitResult.h"
#include "Engine/World.h"
#include "EnvironmentQuery/Items/EnvQueryItemType_VectorBase.h"
#include "GameFramework/Pawn.h"
#include "Navigation/PathFollowingComponent.h"

#define LOCTEXT_NAMESPACE "NoFearEQS"

UEnvQueryTest_NoFearCover::UEnvQueryTest_NoFearCover(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	Cost = EEnvTestCost::High;
	ValidItemType = UEnvQueryItemType_VectorBase::StaticClass();
	SetWorkOnFloatValues(true);

	ThreatContext = UEnvQueryContext_NoFearThreat::StaticClass();

	TestPurpose = EEnvTestPurpose::FilterAndScore;
	FilterType = EEnvTestFilterType::Minimum;
	FloatValueMin.DefaultValue = 0.1f;
}

void UEnvQueryTest_NoFearCover::RunTest(FEnvQueryInstance& QueryInstance) const
{
	UObject* QueryOwner = QueryInstance.Owner.Get();
	UWorld* World = QueryInstance.World;
	if (!QueryOwner || !World)
	{
		return;
	}

	FloatValueMin.BindData(QueryOwner, QueryInstance.QueryID);
	const float MinThreshold = FloatValueMin.GetValue();
	FloatValueMax.BindData(QueryOwner, QueryInstance.QueryID);
	const float MaxThreshold = FloatValueMax.GetValue();

	TArray<FVector> ThreatLocations;
	if (!QueryInstance.PrepareContext(ThreatContext, ThreatLocations) || ThreatLocations.Num() == 0)
	{
		return;
	}
	for (FVector& ThreatLocation : ThreatLocations)
	{
		ThreatLocation.Z += ThreatEyeHeight;
	}

	// Пешки (боты, угроза) и их оружие укрытием не считаются.
	FCollisionQueryParams TraceParams(SCENE_QUERY_STAT(NoFearCoverTest), false);
	TArray<AActor*> IgnoredActors;
	QueryInstance.PrepareContext(ThreatContext, IgnoredActors);
	if (AActor* QuerierActor = Cast<AActor>(QueryOwner))
	{
		IgnoredActors.Add(QuerierActor);
	}

	const UNoFearCombatSubsystem* Subsystem = World->GetSubsystem<UNoFearCombatSubsystem>();
	const UNoFearCombatComponent* QuerierCombat = UNoFearCombatSubsystem::FindCombatComponent(Cast<AActor>(QueryOwner));

	TArray<FVector> OccupiedLocations;
	if (Subsystem)
	{
		for (const UNoFearCombatComponent* Combatant : Subsystem->GetCombatants())
		{
			AActor* Owner = Combatant->GetOwner();
			if (!Owner)
			{
				continue;
			}
			IgnoredActors.Add(Owner);

			if (Combatant == QuerierCombat || OccupiedRadius <= 0.f)
			{
				continue;
			}
			OccupiedLocations.Add(Owner->GetActorLocation());

			const APawn* Pawn = Cast<APawn>(Owner);
			const AAIController* Controller = Pawn ? Cast<AAIController>(Pawn->GetController()) : nullptr;
			const UPathFollowingComponent* PathFollowing = Controller ? Controller->GetPathFollowingComponent() : nullptr;
			if (PathFollowing && PathFollowing->GetStatus() != EPathFollowingStatus::Idle)
			{
				OccupiedLocations.Add(PathFollowing->GetPathDestination());
			}
		}
	}

	TArray<AActor*> AttachedActors;
	for (const AActor* Actor : IgnoredActors)
	{
		if (Actor)
		{
			Actor->GetAttachedActors(AttachedActors, false, true);
		}
	}
	TraceParams.AddIgnoredActors(IgnoredActors);
	TraceParams.AddIgnoredActors(AttachedActors);

	const float OccupiedRadiusSq = FMath::Square(OccupiedRadius);
	const UNoFearBotSubsystem* Bots = World->GetSubsystem<UNoFearBotSubsystem>();
	const AActor* QuerierActor = Cast<AActor>(QueryOwner);

	for (FEnvQueryInstance::ItemIterator It(this, QueryInstance); It; ++It)
	{
		const FVector ItemLocation = GetItemLocation(QueryInstance, It.GetIndex());

		// Укрытие, забронированное другим стрелком, занято.
		bool bOccupied = OccupiedRadius > 0.f && Bots && Bots->IsCoverReserved(ItemLocation, OccupiedRadius, QuerierActor);
		for (const FVector& Occupied : OccupiedLocations)
		{
			if (FVector::DistSquared2D(ItemLocation, Occupied) < OccupiedRadiusSq)
			{
				bOccupied = true;
				break;
			}
		}

		float Score = 0.f;
		if (!bOccupied)
		{
			// Точка должна быть укрыта от всех угроз — берём худшую оценку.
			Score = 1.f;
			for (const FVector& ThreatEyes : ThreatLocations)
			{
				float ObstacleDistance = 0.f;
				const ENoFearCoverType Cover = UNoFearCoverLibrary::EvaluateCover(World, ItemLocation, ThreatEyes, TraceParams, TraceChannel,
					&ObstacleDistance, LowCoverHeight, HighCoverHeight);
				if (Cover == ENoFearCoverType::None)
				{
					Score = 0.f;
					break;
				}

				float ThreatScore = Cover == ENoFearCoverType::High ? FullCoverScore : LowCoverScore;
				if (MaxCoverDistance > 0.f && ObstacleDistance > MaxCoverDistance)
				{
					ThreatScore *= DistantCoverMultiplier;
				}
				Score = FMath::Min(Score, ThreatScore);
			}
		}

		It.SetScore(TestPurpose, FilterType, Score, MinThreshold, MaxThreshold);
	}
}

FText UEnvQueryTest_NoFearCover::GetDescriptionTitle() const
{
	return FText::Format(LOCTEXT("CoverTitle", "{0}: cover from {1}"),
		Super::GetDescriptionTitle(), UEnvQueryTypes::DescribeContext(ThreatContext));
}

FText UEnvQueryTest_NoFearCover::GetDescriptionDetails() const
{
	return FText::Format(LOCTEXT("CoverDetails", "low {0} / high {1} cm, max cover distance {2}\n{3}"),
		FText::AsNumber(LowCoverHeight), FText::AsNumber(HighCoverHeight), FText::AsNumber(MaxCoverDistance), DescribeFloatTestParams());
}

#undef LOCTEXT_NAMESPACE
