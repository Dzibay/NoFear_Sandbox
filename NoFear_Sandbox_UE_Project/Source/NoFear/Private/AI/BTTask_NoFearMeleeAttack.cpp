#include "AI/BTTask_NoFearMeleeAttack.h"

#include "AIController.h"
#include "AI/NoFearMeleeComponent.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Object.h"
#include "Core/NoFearHealthComponent.h"

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

EBTNodeResult::Type UBTTask_NoFearMeleeAttack::ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	UNoFearMeleeComponent* Melee = FindMelee(OwnerComp);
	const UBlackboardComponent* Blackboard = OwnerComp.GetBlackboardComponent();
	AActor* Target = Blackboard ? Cast<AActor>(Blackboard->GetValueAsObject(GetSelectedBlackboardKey())) : nullptr;
	if (!Melee || !Target || UNoFearHealthComponent::IsActorDead(Target) || !Melee->IsInAttackRange(Target))
	{
		return EBTNodeResult::Failed;
	}

	FMeleeMemory& Memory = *CastInstanceNodeMemory<FMeleeMemory>(NodeMemory);
	Memory = FMeleeMemory();
	Memory.GiveUpTime = OwnerComp.GetWorld()->GetTimeSeconds() + MaxWaitTime;
	Memory.bAttackStarted = Melee->StartAttack(Target);
	return EBTNodeResult::InProgress;
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
	if (!Target || UNoFearHealthComponent::IsActorDead(Target) || !Melee->IsInAttackRange(Target)
		|| OwnerComp.GetWorld()->GetTimeSeconds() > Memory.GiveUpTime)
	{
		FinishLatentTask(OwnerComp, EBTNodeResult::Failed);
		return;
	}
	Memory.bAttackStarted = Melee->StartAttack(Target);
}

EBTNodeResult::Type UBTTask_NoFearMeleeAttack::AbortTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	// Начатый рывок не отменяем: тело уже в прыжке, компонент сам доведёт атаку до конца.
	return EBTNodeResult::Aborted;
}

FString UBTTask_NoFearMeleeAttack::GetStaticDescription() const
{
	return FString::Printf(TEXT("Melee attack %s (wait up to %.1f s)"), *GetSelectedBlackboardKey().ToString(), MaxWaitTime);
}
