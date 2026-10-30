#include "AI/BTTask_NoFearShoot.h"

#include "AIController.h"
#include "AI/NoFearCombatComponent.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Object.h"
#include "Core/NoFearHealthComponent.h"
#include "Weapons/NoFearWeapon.h"
#include "Weapons/NoFearWeaponComponent.h"
#include "Weapons/NoFearWeaponData.h"

namespace
{
	UNoFearWeaponComponent* FindWeapons(const UBehaviorTreeComponent& OwnerComp)
	{
		const AAIController* Controller = OwnerComp.GetAIOwner();
		const APawn* Pawn = Controller ? Controller->GetPawn() : nullptr;
		return Pawn ? Pawn->FindComponentByClass<UNoFearWeaponComponent>() : nullptr;
	}
}

UBTTask_NoFearShoot::UBTTask_NoFearShoot(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	NodeName = TEXT("NoFear Shoot");
	INIT_TASK_NODE_NOTIFY_FLAGS();
	bNotifyTick = true;
	bNotifyTaskFinished = true;

	BlackboardKey.AddObjectFilter(this, GET_MEMBER_NAME_CHECKED(UBTTask_NoFearShoot, BlackboardKey), AActor::StaticClass());
	BlackboardKey.SelectedKeyName = TEXT("AttackTarget");
}

uint16 UBTTask_NoFearShoot::GetInstanceMemorySize() const
{
	return sizeof(FShootMemory);
}

void UBTTask_NoFearShoot::InitializeMemory(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, EBTMemoryInit::Type InitType) const
{
	InitializeNodeMemory<FShootMemory>(NodeMemory, InitType);
}

void UBTTask_NoFearShoot::CleanupMemory(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, EBTMemoryClear::Type CleanupType) const
{
	CleanupNodeMemory<FShootMemory>(NodeMemory, CleanupType);
}

EBTNodeResult::Type UBTTask_NoFearShoot::ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	const UNoFearWeaponComponent* Weapons = FindWeapons(OwnerComp);
	const UBlackboardComponent* Blackboard = OwnerComp.GetBlackboardComponent();
	if (!Weapons || Weapons->GetWeapons().Num() == 0 || !Blackboard || !Blackboard->GetValueAsObject(GetSelectedBlackboardKey()))
	{
		return EBTNodeResult::Failed;
	}

	FShootMemory& Memory = *CastInstanceNodeMemory<FShootMemory>(NodeMemory);
	Memory = FShootMemory();
	Memory.BurstsLeft = BurstsMax > 0 ? FMath::RandRange(FMath::Min(BurstsMin, BurstsMax), BurstsMax) : 0;
	Memory.bFiringPhase = true;
	Memory.PhaseEndTime = OwnerComp.GetWorld()->GetTimeSeconds() + FMath::FRandRange(BurstDurationMin, FMath::Max(BurstDurationMin, BurstDurationMax));
	return EBTNodeResult::InProgress;
}

void UBTTask_NoFearShoot::ReleaseTrigger(UBehaviorTreeComponent& OwnerComp, FShootMemory& Memory) const
{
	if (Memory.bTriggerHeld)
	{
		if (UNoFearWeaponComponent* Weapons = FindWeapons(OwnerComp))
		{
			Weapons->StopFire();
		}
		Memory.bTriggerHeld = false;
	}
}

void UBTTask_NoFearShoot::TickTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds)
{
	FShootMemory& Memory = *CastInstanceNodeMemory<FShootMemory>(NodeMemory);
	UNoFearWeaponComponent* Weapons = FindWeapons(OwnerComp);
	const UBlackboardComponent* Blackboard = OwnerComp.GetBlackboardComponent();
	const AActor* Target = Blackboard ? Cast<AActor>(Blackboard->GetValueAsObject(GetSelectedBlackboardKey())) : nullptr;
	if (!Weapons || Weapons->IsOwnerDead() || !Target || UNoFearHealthComponent::IsActorDead(Target))
	{
		FinishLatentTask(OwnerComp, EBTNodeResult::Succeeded);
		return;
	}

	// Оружие достаётся или перезаряжается — ждём.
	ANoFearWeapon* Weapon = Weapons->GetCurrentWeapon();
	if (!Weapon || Weapon->GetWeaponState() != ENoFearWeaponState::Idle || !Weapon->GetWeaponData())
	{
		ReleaseTrigger(OwnerComp, Memory);
		return;
	}
	if (Weapon->GetMagazineAmmo() <= 0)
	{
		ReleaseTrigger(OwnerComp, Memory);
		Weapons->Reload();
		return;
	}

	const double Now = OwnerComp.GetWorld()->GetTimeSeconds();
	if (Memory.bFiringPhase && Now >= Memory.PhaseEndTime)
	{
		ReleaseTrigger(OwnerComp, Memory);
		if (BurstsMax > 0 && --Memory.BurstsLeft <= 0)
		{
			FinishLatentTask(OwnerComp, EBTNodeResult::Succeeded);
			return;
		}
		Memory.bFiringPhase = false;
		Memory.PhaseEndTime = Now + FMath::FRandRange(PauseMin, FMath::Max(PauseMin, PauseMax));
		return;
	}
	if (!Memory.bFiringPhase)
	{
		if (Now < Memory.PhaseEndTime)
		{
			return;
		}
		Memory.bFiringPhase = true;
		Memory.PhaseEndTime = Now + FMath::FRandRange(BurstDurationMin, FMath::Max(BurstDurationMin, BurstDurationMax));
		Memory.NextTriggerTime = Now;
	}

	// Нет линии огня или не прошла реакция — не жмём спуск, но очередь по времени идёт.
	const AAIController* Controller = OwnerComp.GetAIOwner();
	const UNoFearCombatComponent* Combat = Controller && Controller->GetPawn() ? Controller->GetPawn()->FindComponentByClass<UNoFearCombatComponent>() : nullptr;
	if (Combat && !Combat->IsReadyToFire())
	{
		ReleaseTrigger(OwnerComp, Memory);
		return;
	}

	if (Weapon->GetWeaponData()->FireMode == ENoFearFireMode::Auto)
	{
		if (!Memory.bTriggerHeld)
		{
			Weapons->StartFire();
			Memory.bTriggerHeld = true;
		}
	}
	else if (Now >= Memory.NextTriggerTime)
	{
		// Одиночный выстрел или очередь Burst: нажать и отпустить, остальное оружие сделает само.
		Weapons->StartFire();
		Weapons->StopFire();
		Memory.NextTriggerTime = Now + FMath::FRandRange(TriggerIntervalMin, FMath::Max(TriggerIntervalMin, TriggerIntervalMax));
	}
}

EBTNodeResult::Type UBTTask_NoFearShoot::AbortTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	ReleaseTrigger(OwnerComp, *CastInstanceNodeMemory<FShootMemory>(NodeMemory));
	return EBTNodeResult::Aborted;
}

void UBTTask_NoFearShoot::OnTaskFinished(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, EBTNodeResult::Type TaskResult)
{
	ReleaseTrigger(OwnerComp, *CastInstanceNodeMemory<FShootMemory>(NodeMemory));
	Super::OnTaskFinished(OwnerComp, NodeMemory, TaskResult);
}

FString UBTTask_NoFearShoot::GetStaticDescription() const
{
	const FString Bursts = BurstsMax > 0 ? FString::Printf(TEXT("%d-%d bursts"), FMath::Min(BurstsMin, BurstsMax), BurstsMax) : TEXT("until aborted");
	return FString::Printf(TEXT("Shoot at %s: %s, %.2f-%.2f s"), *GetSelectedBlackboardKey().ToString(), *Bursts, BurstDurationMin, BurstDurationMax);
}
