#include "AI/BTTask_NoFearLookAround.h"

#include "AI/NoFearAIController.h"
#include "AI/NoFearCivilianAIController.h"
#include "BehaviorTree/BehaviorTreeComponent.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"

// ---------------------------------------------------------------------------
// Look Around
// ---------------------------------------------------------------------------

UBTTask_NoFearLookAround::UBTTask_NoFearLookAround(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	NodeName = TEXT("NoFear Look Around");
	bNotifyTick = true;
	bNotifyTaskFinished = true;
}

uint16 UBTTask_NoFearLookAround::GetInstanceMemorySize() const
{
	return sizeof(FLookMemory);
}

void UBTTask_NoFearLookAround::InitializeMemory(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, EBTMemoryInit::Type InitType) const
{
	InitializeNodeMemory<FLookMemory>(NodeMemory, InitType);
}

void UBTTask_NoFearLookAround::CleanupMemory(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, EBTMemoryClear::Type CleanupType) const
{
	CleanupNodeMemory<FLookMemory>(NodeMemory, CleanupType);
}

EBTNodeResult::Type UBTTask_NoFearLookAround::ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	AAIController* Controller = OwnerComp.GetAIOwner();
	const APawn* Pawn = Controller ? Controller->GetPawn() : nullptr;
	if (!Pawn)
	{
		return EBTNodeResult::Failed;
	}

	FLookMemory* Memory = CastInstanceNodeMemory<FLookMemory>(NodeMemory);
	const double Now = OwnerComp.GetWorld()->GetTimeSeconds();
	Memory->EndTime = Now + FMath::Max(0.f, FMath::FRandRange(Duration - RandomDeviation, Duration + RandomDeviation));
	Memory->NextLookTime = Now + LookInterval * FMath::FRandRange(0.3f, 0.7f);
	Memory->BaseYaw = Pawn->GetActorRotation().Yaw;

	if (ANoFearAIController* Bot = Cast<ANoFearAIController>(Controller))
	{
		Bot->SetLookAroundStance(true);
	}
	return EBTNodeResult::InProgress;
}

void UBTTask_NoFearLookAround::TickTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds)
{
	AAIController* Controller = OwnerComp.GetAIOwner();
	const APawn* Pawn = Controller ? Controller->GetPawn() : nullptr;
	FLookMemory* Memory = CastInstanceNodeMemory<FLookMemory>(NodeMemory);
	const double Now = OwnerComp.GetWorld()->GetTimeSeconds();
	if (!Pawn || Now >= Memory->EndTime)
	{
		FinishLatentTask(OwnerComp, EBTNodeResult::Succeeded);
		return;
	}

	if (Now >= Memory->NextLookTime)
	{
		Memory->NextLookTime = Now + LookInterval * FMath::FRandRange(0.8f, 1.3f);
		const float Yaw = Memory->BaseYaw + FMath::FRandRange(-MaxYaw, MaxYaw);
		const FVector Direction = FRotator(0.f, Yaw, 0.f).Vector();
		Controller->SetFocalPoint(Pawn->GetActorLocation() + Direction * 500.f + FVector(0.f, 0.f, 60.f));
	}
}

EBTNodeResult::Type UBTTask_NoFearLookAround::AbortTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	return EBTNodeResult::Aborted;
}

void UBTTask_NoFearLookAround::OnTaskFinished(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, EBTNodeResult::Type TaskResult)
{
	if (AAIController* Controller = OwnerComp.GetAIOwner())
	{
		Controller->ClearFocus(EAIFocusPriority::Gameplay);
		if (ANoFearAIController* Bot = Cast<ANoFearAIController>(Controller))
		{
			Bot->SetLookAroundStance(false);
		}
	}
	Super::OnTaskFinished(OwnerComp, NodeMemory, TaskResult);
}

FString UBTTask_NoFearLookAround::GetStaticDescription() const
{
	return FString::Printf(TEXT("Осмотреться %.1f ± %.1f с, каждые %.1f с, до %.0f°"), Duration, RandomDeviation, LookInterval, MaxYaw);
}

// ---------------------------------------------------------------------------
// Civilian Shelter
// ---------------------------------------------------------------------------

UBTTask_NoFearCivilianShelter::UBTTask_NoFearCivilianShelter(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	NodeName = TEXT("NoFear Civilian Shelter");
}

EBTNodeResult::Type UBTTask_NoFearCivilianShelter::ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	ANoFearCivilianAIController* Civilian = Cast<ANoFearCivilianAIController>(OwnerComp.GetAIOwner());
	if (!Civilian)
	{
		return EBTNodeResult::Failed;
	}
	Civilian->NotifyReachedSafety();
	return EBTNodeResult::Succeeded;
}

FString UBTTask_NoFearCivilianShelter::GetStaticDescription() const
{
	return TEXT("Спрятаться: присесть и закрыть голову");
}
