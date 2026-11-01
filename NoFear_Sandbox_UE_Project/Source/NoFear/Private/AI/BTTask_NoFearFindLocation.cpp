#include "AI/BTTask_NoFearFindLocation.h"
#include "Core/NoFearPerf.h"

#include "AIController.h"
#include "AI/NoFearAIController.h"
#include "AI/NoFearBotSubsystem.h"
#include "AI/NoFearMonsterAIController.h"
#include "Core/NoFearHealthComponent.h"
#include "Core/NoFearTeam.h"
#include "BehaviorTree/BehaviorTree.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Object.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Vector.h"
#include "NavigationSystem.h"

namespace
{
	/** Ближайшая к точке точка навмеша. false — навмеша рядом нет. */
	bool ProjectToNav(const UObject* WorldContext, const FVector& Point, FVector& OutPoint)
	{
		UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(WorldContext->GetWorld());
		FNavLocation NavLocation;
		if (NavSys && NavSys->ProjectPointToNavigation(Point, NavLocation, FVector(300.f, 300.f, 500.f)))
		{
			OutPoint = NavLocation.Location;
			return true;
		}
		return false;
	}
}

// ---------------------------------------------------------------------------
// Wander
// ---------------------------------------------------------------------------

UBTTask_NoFearFindWanderLocation::UBTTask_NoFearFindWanderLocation(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	NodeName = TEXT("NoFear Find Wander Location");

	BlackboardKey.AddVectorFilter(this, GET_MEMBER_NAME_CHECKED(UBTTask_NoFearFindWanderLocation, BlackboardKey));
	BlackboardKey.SelectedKeyName = FNoFearBotKeys::MoveGoal;

	OriginKey.AddVectorFilter(this, GET_MEMBER_NAME_CHECKED(UBTTask_NoFearFindWanderLocation, OriginKey));
	OriginKey.AllowNoneAsValue(true);
	OriginKey.SelectedKeyName = FNoFearBotKeys::HomeLocation;
}

void UBTTask_NoFearFindWanderLocation::InitializeFromAsset(UBehaviorTree& Asset)
{
	Super::InitializeFromAsset(Asset);
	if (const UBlackboardData* BBAsset = GetBlackboardAsset())
	{
		OriginKey.ResolveSelectedKey(*BBAsset);
	}
}

EBTNodeResult::Type UBTTask_NoFearFindWanderLocation::ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	NOFEAR_PERF_SCOPE(FindLocation);
	const AAIController* Controller = OwnerComp.GetAIOwner();
	const APawn* Pawn = Controller ? Controller->GetPawn() : nullptr;
	UBlackboardComponent* Blackboard = OwnerComp.GetBlackboardComponent();
	UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(OwnerComp.GetWorld());
	if (!Pawn || !Blackboard || !NavSys)
	{
		return EBTNodeResult::Failed;
	}

	FVector Origin = Pawn->GetActorLocation();
	if (OriginKey.IsSet())
	{
		const FVector KeyValue = Blackboard->GetValueAsVector(OriginKey.SelectedKeyName);
		if (FAISystem::IsValidLocation(KeyValue))
		{
			Origin = KeyValue;
		}
	}

	// Несколько попыток найти точку не под ногами.
	for (int32 Attempt = 0; Attempt < 6; ++Attempt)
	{
		FNavLocation Result;
		if (NavSys->GetRandomReachablePointInRadius(Origin, Radius, Result)
			&& FVector::DistSquared2D(Result.Location, Pawn->GetActorLocation()) >= FMath::Square(MinDistance))
		{
			Blackboard->SetValueAsVector(GetSelectedBlackboardKey(), Result.Location);
			return EBTNodeResult::Succeeded;
		}
	}
	return EBTNodeResult::Failed;
}

FString UBTTask_NoFearFindWanderLocation::GetStaticDescription() const
{
	return FString::Printf(TEXT("%s = random point within %.0f of %s"), *GetSelectedBlackboardKey().ToString(), Radius,
		OriginKey.IsSet() ? *OriginKey.SelectedKeyName.ToString() : TEXT("self"));
}

// ---------------------------------------------------------------------------
// Chase
// ---------------------------------------------------------------------------

UBTTask_NoFearFindChaseLocation::UBTTask_NoFearFindChaseLocation(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	NodeName = TEXT("NoFear Find Chase Location");

	BlackboardKey.AddVectorFilter(this, GET_MEMBER_NAME_CHECKED(UBTTask_NoFearFindChaseLocation, BlackboardKey));
	BlackboardKey.SelectedKeyName = FNoFearBotKeys::MoveGoal;

	TargetKey.AddObjectFilter(this, GET_MEMBER_NAME_CHECKED(UBTTask_NoFearFindChaseLocation, TargetKey), AActor::StaticClass());
	TargetKey.SelectedKeyName = FNoFearBotKeys::AttackTarget;
}

void UBTTask_NoFearFindChaseLocation::InitializeFromAsset(UBehaviorTree& Asset)
{
	Super::InitializeFromAsset(Asset);
	if (const UBlackboardData* BBAsset = GetBlackboardAsset())
	{
		TargetKey.ResolveSelectedKey(*BBAsset);
	}
}

EBTNodeResult::Type UBTTask_NoFearFindChaseLocation::ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	NOFEAR_PERF_SCOPE(FindLocation);
	AAIController* Controller = OwnerComp.GetAIOwner();
	const APawn* Pawn = Controller ? Controller->GetPawn() : nullptr;
	UBlackboardComponent* Blackboard = OwnerComp.GetBlackboardComponent();
	if (!Pawn || !Blackboard)
	{
		return EBTNodeResult::Failed;
	}

	const ANoFearAIController* Bot = Cast<ANoFearAIController>(Controller);
	FVector Goal;
	if (Bot && Bot->GetCurrentTarget())
	{
		Goal = Bot->GetLastKnownTargetLocation();
	}
	else if (const AActor* Target = Cast<AActor>(Blackboard->GetValueAsObject(TargetKey.SelectedKeyName)))
	{
		Goal = Target->GetActorLocation();
	}
	else
	{
		return EBTNodeResult::Failed;
	}

	// Не вплотную: остановиться чуть раньше, по линии от бота.
	const FVector PawnLocation = Pawn->GetActorLocation();
	const FVector FromGoal = PawnLocation - Goal;
	if (FromGoal.Size2D() > StopShortDistance)
	{
		Goal += FromGoal.GetSafeNormal2D() * StopShortDistance;
	}

	// Поводок: не дальше радиуса от дома (у напарника — от лидера).
	if (Bot && Bot->GetLeashRadius() > 0.f)
	{
		const FVector Anchor = Bot->GetLeashAnchor();
		FVector Offset = Goal - Anchor;
		Offset.Z = 0.f;
		if (Offset.Size() > Bot->GetLeashRadius())
		{
			Goal = Anchor + Offset.GetClampedToMaxSize(Bot->GetLeashRadius());
			Goal.Z = PawnLocation.Z;
		}
	}

	FVector NavGoal;
	if (!ProjectToNav(Pawn, Goal, NavGoal))
	{
		return EBTNodeResult::Failed;
	}
	Blackboard->SetValueAsVector(GetSelectedBlackboardKey(), NavGoal);
	return EBTNodeResult::Succeeded;
}

FString UBTTask_NoFearFindChaseLocation::GetStaticDescription() const
{
	return FString::Printf(TEXT("%s = last known location of %s, within leash"), *GetSelectedBlackboardKey().ToString(), *TargetKey.SelectedKeyName.ToString());
}

// ---------------------------------------------------------------------------
// Clear key
// ---------------------------------------------------------------------------

UBTTask_NoFearClearBlackboardKey::UBTTask_NoFearClearBlackboardKey(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	NodeName = TEXT("NoFear Clear Key");
}

EBTNodeResult::Type UBTTask_NoFearClearBlackboardKey::ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	if (UBlackboardComponent* Blackboard = OwnerComp.GetBlackboardComponent())
	{
		Blackboard->ClearValue(GetSelectedBlackboardKey());
	}
	return EBTNodeResult::Succeeded;
}

FString UBTTask_NoFearClearBlackboardKey::GetStaticDescription() const
{
	return FString::Printf(TEXT("Clear %s"), *GetSelectedBlackboardKey().ToString());
}

// ---------------------------------------------------------------------------
// Circle
// ---------------------------------------------------------------------------

UBTTask_NoFearFindCircleLocation::UBTTask_NoFearFindCircleLocation(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	NodeName = TEXT("NoFear Find Circle Location");

	BlackboardKey.AddVectorFilter(this, GET_MEMBER_NAME_CHECKED(UBTTask_NoFearFindCircleLocation, BlackboardKey));
	BlackboardKey.SelectedKeyName = FNoFearBotKeys::MoveGoal;

	TargetKey.AddObjectFilter(this, GET_MEMBER_NAME_CHECKED(UBTTask_NoFearFindCircleLocation, TargetKey), AActor::StaticClass());
	TargetKey.SelectedKeyName = FNoFearBotKeys::AttackTarget;
}

void UBTTask_NoFearFindCircleLocation::InitializeFromAsset(UBehaviorTree& Asset)
{
	Super::InitializeFromAsset(Asset);
	if (const UBlackboardData* BBAsset = GetBlackboardAsset())
	{
		TargetKey.ResolveSelectedKey(*BBAsset);
	}
}

EBTNodeResult::Type UBTTask_NoFearFindCircleLocation::ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	NOFEAR_PERF_SCOPE(FindLocation);
	AAIController* Controller = OwnerComp.GetAIOwner();
	const APawn* Pawn = Controller ? Controller->GetPawn() : nullptr;
	UBlackboardComponent* Blackboard = OwnerComp.GetBlackboardComponent();
	const AActor* Target = Blackboard ? Cast<AActor>(Blackboard->GetValueAsObject(TargetKey.SelectedKeyName)) : nullptr;
	if (!Pawn || !Target)
	{
		return EBTNodeResult::Failed;
	}

	const FVector Center = Target->GetActorLocation();
	const FVector PawnLocation = Pawn->GetActorLocation();
	FVector FromCenter = (PawnLocation - Center).GetSafeNormal2D();
	if (FromCenter.IsNearlyZero())
	{
		FromCenter = -Target->GetActorForwardVector().GetSafeNormal2D();
	}
	const float CurrentAngle = FMath::RadiansToDegrees(FMath::Atan2(FromCenter.Y, FromCenter.X));

	// Углы других монстров вокруг той же цели.
	TArray<float> OtherAngles;
	if (const UNoFearBotSubsystem* Subsystem = UNoFearBotSubsystem::Get(Pawn))
	{
		for (const ANoFearAIController* Bot : Subsystem->GetBots())
		{
			const APawn* OtherPawn = Bot ? Bot->GetPawn() : nullptr;
			if (!OtherPawn || OtherPawn == Pawn || UNoFearHealthComponent::IsActorDead(OtherPawn)
				|| FVector::DistSquared2D(OtherPawn->GetActorLocation(), Center) > FMath::Square(CrowdRadius)
				|| !UNoFearTeamLibrary::AreFriendly(Pawn, OtherPawn))
			{
				continue;
			}
			const FVector Dir = (OtherPawn->GetActorLocation() - Center).GetSafeNormal2D();
			OtherAngles.Add(FMath::RadiansToDegrees(FMath::Atan2(Dir.Y, Dir.X)));
		}
	}

	auto Separation = [&OtherAngles](float Angle)
	{
		float Best = 180.f;
		for (const float Other : OtherAngles)
		{
			Best = FMath::Min(Best, FMath::Abs(FMath::FindDeltaAngleDegrees(Angle, Other)));
		}
		return Best;
	};

	// Кандидаты по обе стороны; выбрать тот, что дальше всех от соседей (при равенстве — случайный).
	float BestAngle = CurrentAngle;
	float BestScore = -1.f;
	for (int32 Index = 0; Index < 6; ++Index)
	{
		const float Sign = Index % 2 == 0 ? 1.f : -1.f;
		const float Angle = CurrentAngle + Sign * FMath::FRandRange(MinStepAngle, FMath::Max(MinStepAngle, MaxStepAngle));
		const float Score = Separation(Angle) + FMath::FRandRange(0.f, 5.f);
		if (Score > BestScore)
		{
			BestScore = Score;
			BestAngle = Angle;
		}
	}

	// Кольцо — своё у каждого вида монстра (у прыгуна шире), у остальных — из задачи.
	float RingMin = MinRadius;
	float RingMax = MaxRadius;
	if (const ANoFearMonsterAIController* Monster = Cast<ANoFearMonsterAIController>(Controller))
	{
		RingMin = Monster->CircleMinRadius;
		RingMax = Monster->CircleMaxRadius;
	}
	const float Radius = FMath::FRandRange(RingMin, FMath::Max(RingMin, RingMax));
	const FVector Desired = Center + FRotator(0.f, BestAngle, 0.f).Vector() * Radius;
	FVector Goal;
	if (!ProjectToNav(Pawn, Desired, Goal))
	{
		return EBTNodeResult::Failed;
	}
	Blackboard->SetValueAsVector(GetSelectedBlackboardKey(), Goal);
	return EBTNodeResult::Succeeded;
}

FString UBTTask_NoFearFindCircleLocation::GetStaticDescription() const
{
	return FString::Printf(TEXT("%s = ring %.0f-%.0f around %s"), *GetSelectedBlackboardKey().ToString(), MinRadius, MaxRadius,
		*TargetKey.SelectedKeyName.ToString());
}

// ---------------------------------------------------------------------------

UBTTask_NoFearFindRetreatLocation::UBTTask_NoFearFindRetreatLocation(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	NodeName = TEXT("NoFear Find Retreat Location");

	BlackboardKey.AddVectorFilter(this, GET_MEMBER_NAME_CHECKED(UBTTask_NoFearFindRetreatLocation, BlackboardKey));
	BlackboardKey.SelectedKeyName = FNoFearBotKeys::MoveGoal;

	TargetKey.AddObjectFilter(this, GET_MEMBER_NAME_CHECKED(UBTTask_NoFearFindRetreatLocation, TargetKey), AActor::StaticClass());
	TargetKey.SelectedKeyName = FNoFearBotKeys::AttackTarget;
}

void UBTTask_NoFearFindRetreatLocation::InitializeFromAsset(UBehaviorTree& Asset)
{
	Super::InitializeFromAsset(Asset);
	if (const UBlackboardData* BBAsset = GetBlackboardAsset())
	{
		TargetKey.ResolveSelectedKey(*BBAsset);
	}
}

EBTNodeResult::Type UBTTask_NoFearFindRetreatLocation::ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	NOFEAR_PERF_SCOPE(FindLocation);
	AAIController* Controller = OwnerComp.GetAIOwner();
	const APawn* Pawn = Controller ? Controller->GetPawn() : nullptr;
	UBlackboardComponent* Blackboard = OwnerComp.GetBlackboardComponent();
	const AActor* Target = Blackboard ? Cast<AActor>(Blackboard->GetValueAsObject(TargetKey.SelectedKeyName)) : nullptr;
	UNavigationSystemV1* NavSys = Pawn ? FNavigationSystem::GetCurrent<UNavigationSystemV1>(Pawn->GetWorld()) : nullptr;
	if (!Pawn || !Target || !NavSys)
	{
		return EBTNodeResult::Failed;
	}

	const FVector Center = Target->GetActorLocation();
	const FVector PawnLocation = Pawn->GetActorLocation();
	const float CurrentDistance = FVector::Dist2D(PawnLocation, Center);
	FVector Away = (PawnLocation - Center).GetSafeNormal2D();
	if (Away.IsNearlyZero())
	{
		Away = -Pawn->GetActorForwardVector().GetSafeNormal2D();
	}
	const float AwayAngle = FMath::RadiansToDegrees(FMath::Atan2(Away.Y, Away.X));

	bool bFound = false;
	FVector Best = FVector::ZeroVector;
	float BestScore = -UE_BIG_NUMBER;
	for (int32 Index = 0; Index < NumSamples; ++Index)
	{
		// Первые варианты — ближе к «прямо от цели», дальше разброс шире.
		const float Spread = MaxAngle * FMath::Min(1.f, (Index + 1.f) / FMath::Max(NumSamples * 0.5f, 1.f));
		const float Angle = AwayAngle + FMath::FRandRange(-Spread, Spread);
		const float Distance = FMath::FRandRange(MinDistance, FMath::Max(MinDistance, MaxDistance));
		FVector Point;
		if (!ProjectToNav(Pawn, Center + FRotator(0.f, Angle, 0.f).Vector() * Distance, Point))
		{
			continue;
		}
		const float PointDistance = FVector::Dist2D(Point, Center);
		if (PointDistance <= CurrentDistance + 100.f)
		{
			continue;
		}
		// Путь не должен вести мимо цели: путь заметно длиннее прямой — значит, в обход через неё или тупик.
		FVector::FReal PathLength = 0.0;
		if (NavSys->GetPathLength(PawnLocation, Point, PathLength) != ENavigationQueryResult::Success
			|| PathLength > FVector::Dist2D(PawnLocation, Point) * 1.6f + 200.f)
		{
			continue;
		}
		const float Score = PointDistance - 0.3f * FMath::Abs(FMath::FindDeltaAngleDegrees(Angle, AwayAngle));
		if (Score > BestScore)
		{
			BestScore = Score;
			Best = Point;
			bFound = true;
		}
	}
	if (!bFound)
	{
		return EBTNodeResult::Failed;
	}
	Blackboard->SetValueAsVector(GetSelectedBlackboardKey(), Best);
	return EBTNodeResult::Succeeded;
}

FString UBTTask_NoFearFindRetreatLocation::GetStaticDescription() const
{
	return FString::Printf(TEXT("%s = %.0f-%.0f away from %s (±%.0f°)"), *GetSelectedBlackboardKey().ToString(), MinDistance, MaxDistance,
		*TargetKey.SelectedKeyName.ToString(), MaxAngle);
}
