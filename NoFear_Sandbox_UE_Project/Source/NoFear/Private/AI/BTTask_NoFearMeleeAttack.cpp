#include "AI/BTTask_NoFearMeleeAttack.h"

#include "AIController.h"
#include "AI/NoFearMeleeComponent.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Object.h"
#include "Core/NoFearHealthComponent.h"
#include "Navigation/PathFollowingComponent.h"

namespace
{
	UNoFearMeleeComponent* FindMelee(const UBehaviorTreeComponent& OwnerComp)
	{
		const AAIController* Controller = OwnerComp.GetAIOwner();
		const APawn* Pawn = Controller ? Controller->GetPawn() : nullptr;
		return Pawn ? Pawn->FindComponentByClass<UNoFearMeleeComponent>() : nullptr;
	}
}

UBTTask_NoFearMeleeAttack::UBTTask_NoFearMeleeAttack(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	NodeName = TEXT("NoFear Melee Attack");
	INIT_TASK_NODE_NOTIFY_FLAGS();
	bNotifyTick = true;

	BlackboardKey.AddObjectFilter(this, GET_MEMBER_NAME_CHECKED(UBTTask_NoFearMeleeAttack, BlackboardKey), AActor::StaticClass());
	BlackboardKey.SelectedKeyName = TEXT("AttackTarget");
}

uint16 UBTTask_NoFearMeleeAttack::GetInstanceMemorySize() const
{
	return sizeof(FMeleeMemory);
}

void UBTTask_NoFearMeleeAttack::InitializeMemory(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, EBTMemoryInit::Type InitType) const
{
	InitializeNodeMemory<FMeleeMemory>(NodeMemory, InitType);
}

void UBTTask_NoFearMeleeAttack::CleanupMemory(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, EBTMemoryClear::Type CleanupType) const
{
	CleanupNodeMemory<FMeleeMemory>(NodeMemory, CleanupType);
}

bool UBTTask_NoFearMeleeAttack::AttackOrChase(UBehaviorTreeComponent& OwnerComp, FMeleeMemory& Memory, AActor* Target) const
{
	UNoFearMeleeComponent* Melee = FindMelee(OwnerComp);
	AAIController* Controller = OwnerComp.GetAIOwner();
	if (!Melee || !Controller)
	{
		return false;
	}

	if (Melee->CanAttackTarget(Target))
	{
		if (Memory.bChasing)
		{
			Controller->StopMovement();
			Memory.bChasing = false;
		}
		Memory.bAttackStarted = Melee->StartAttack(Target);
		return true;
	}

	if (!bChase)
	{
		// Без погони ждём перезарядки только на дистанции удара.
		return Melee->IsInAttackRange(Target);
	}

	// Подбегаем, пока не упёрлись в цель; если уже вплотную — стоим и ждём перезарядки.
	const APawn* Pawn = Controller->GetPawn();
	const bool bClose = Pawn && FVector::Dist2D(Pawn->GetActorLocation(), Target->GetActorLocation()) <= ChaseAcceptanceRadius + 80.f;
	if (!bClose && Controller->GetMoveStatus() == EPathFollowingStatus::Idle)
	{
		Controller->MoveToActor(Target, ChaseAcceptanceRadius, true, true, true, nullptr, true);
		Memory.bChasing = true;
	}
	return true;
}

EBTNodeResult::Type UBTTask_NoFearMeleeAttack::ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	const UBlackboardComponent* Blackboard = OwnerComp.GetBlackboardComponent();
	AActor* Target = Blackboard ? Cast<AActor>(Blackboard->GetValueAsObject(GetSelectedBlackboardKey())) : nullptr;
	if (!FindMelee(OwnerComp) || !Target || UNoFearHealthComponent::IsActorDead(Target))
	{
		return EBTNodeResult::Failed;
	}

	FMeleeMemory& Memory = *CastInstanceNodeMemory<FMeleeMemory>(NodeMemory);
	Memory = FMeleeMemory();
	Memory.GiveUpTime = OwnerComp.GetWorld()->GetTimeSeconds() + MaxWaitTime;
	return AttackOrChase(OwnerComp, Memory, Target) ? EBTNodeResult::InProgress : EBTNodeResult::Failed;
}

void UBTTask_NoFearMeleeAttack::TickTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds)
{
	FMeleeMemory& Memory = *CastInstanceNodeMemory<FMeleeMemory>(NodeMemory);
	UNoFearMeleeComponent* Melee = FindMelee(OwnerComp);
	if (!Melee)
	{
		FinishLatentTask(OwnerComp, EBTNodeResult::Failed);
		return;
	}

	if (Memory.bAttackStarted)
	{
		if (!Melee->IsAttacking())
		{
			FinishLatentTask(OwnerComp, EBTNodeResult::Succeeded);
		}
		return;
	}

	const UBlackboardComponent* Blackboard = OwnerComp.GetBlackboardComponent();
	AActor* Target = Blackboard ? Cast<AActor>(Blackboard->GetValueAsObject(GetSelectedBlackboardKey())) : nullptr;
	if (!Target || UNoFearHealthComponent::IsActorDead(Target) || OwnerComp.GetWorld()->GetTimeSeconds() > Memory.GiveUpTime
		|| !AttackOrChase(OwnerComp, Memory, Target))
	{
		StopChase(OwnerComp, Memory);
		FinishLatentTask(OwnerComp, EBTNodeResult::Failed);
	}
}

void UBTTask_NoFearMeleeAttack::StopChase(UBehaviorTreeComponent& OwnerComp, FMeleeMemory& Memory) const
{
	if (Memory.bChasing)
	{
		if (AAIController* Controller = OwnerComp.GetAIOwner())
		{
			Controller->StopMovement();
		}
		Memory.bChasing = false;
	}
}

EBTNodeResult::Type UBTTask_NoFearMeleeAttack::AbortTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	// Погоню прекращаем, а начатый рывок — нет: тело уже в прыжке, компонент сам доведёт атаку до конца.
	StopChase(OwnerComp, *CastInstanceNodeMemory<FMeleeMemory>(NodeMemory));
	return EBTNodeResult::Aborted;
}

FString UBTTask_NoFearMeleeAttack::GetStaticDescription() const
{
	return FString::Printf(TEXT("Melee attack %s (%s, give up after %.1f s)"), *GetSelectedBlackboardKey().ToString(), bChase ? TEXT("chase") : TEXT("no chase"), MaxWaitTime);
}
