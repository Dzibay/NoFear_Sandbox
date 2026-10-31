#include "AI/BTTask_NoFearBandage.h"

#include "AI/NoFearAIController.h"
#include "AI/NoFearSquadSubsystem.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "Core/NoFearHealthComponent.h"
#include "Navigation/PathFollowingComponent.h"
#include "NoFear.h"
#include "Weapons/NoFearWeaponComponent.h"

UBTTask_NoFearBandage::UBTTask_NoFearBandage(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	NodeName = TEXT("NoFear Bandage");
	INIT_TASK_NODE_NOTIFY_FLAGS();
	bNotifyTick = true;
	bNotifyTaskFinished = true;

	// Ключи-объекты в блэкборде — AActor: фильтр по APawn не пропустил бы HealTarget, и редактор сбросил бы ключ в None.
	BlackboardKey.AddObjectFilter(this, GET_MEMBER_NAME_CHECKED(UBTTask_NoFearBandage, BlackboardKey), AActor::StaticClass());
	BlackboardKey.SelectedKeyName = FNoFearBotKeys::HealTarget;
}

uint16 UBTTask_NoFearBandage::GetInstanceMemorySize() const
{
	return sizeof(FBandageMemory);
}

void UBTTask_NoFearBandage::InitializeMemory(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, EBTMemoryInit::Type InitType) const
{
	InitializeNodeMemory<FBandageMemory>(NodeMemory, InitType);
}

void UBTTask_NoFearBandage::CleanupMemory(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, EBTMemoryClear::Type CleanupType) const
{
	CleanupNodeMemory<FBandageMemory>(NodeMemory, CleanupType);
}

EBTNodeResult::Type UBTTask_NoFearBandage::ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	ANoFearAIController* Controller = Cast<ANoFearAIController>(OwnerComp.GetAIOwner());
	APawn* Medic = Controller ? Controller->GetPawn() : nullptr;
	const UBlackboardComponent* Blackboard = OwnerComp.GetBlackboardComponent();
	APawn* Patient = Blackboard ? Cast<APawn>(Blackboard->GetValueAsObject(GetSelectedBlackboardKey())) : nullptr;
	if (!Medic || !Patient || Patient == Medic || UNoFearHealthComponent::IsActorDead(Patient))
	{
		return Fail(OwnerComp, Medic, Patient);
	}

	FBandageMemory& Memory = *CastInstanceNodeMemory<FBandageMemory>(NodeMemory);
	Memory = FBandageMemory();
	Memory.Patient = Patient;
	Memory.TimeLeft = BandageTime;
	if (UNoFearWeaponComponent* Weapons = Medic->FindComponentByClass<UNoFearWeaponComponent>())
	{
		Weapons->StopFire();
	}
	const bool bClose = FVector::Dist2D(Medic->GetActorLocation(), Patient->GetActorLocation()) <= MaxPatientDistance;
	SetClose(OwnerComp, Memory, bClose, OwnerComp.GetWorld()->GetTimeSeconds());
	return EBTNodeResult::InProgress;
}

void UBTTask_NoFearBandage::TickTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds)
{
	FBandageMemory& Memory = *CastInstanceNodeMemory<FBandageMemory>(NodeMemory);
	const AAIController* Controller = OwnerComp.GetAIOwner();
	APawn* Medic = Controller ? Controller->GetPawn() : nullptr;
	APawn* Patient = Memory.Patient.Get();
	if (!Medic || UNoFearHealthComponent::IsActorDead(Medic) || !Patient || UNoFearHealthComponent::IsActorDead(Patient))
	{
		FinishLatentTask(OwnerComp, Fail(OwnerComp, Medic, Patient));
		return;
	}

	const double Now = OwnerComp.GetWorld()->GetTimeSeconds();
	if (FVector::Dist2D(Medic->GetActorLocation(), Patient->GetActorLocation()) > MaxPatientDistance)
	{
		// Ещё бежит к раненому или тот сдвинулся (выглянул, отшатнулся): догнать, время перевязки стоит.
		if (Memory.LostSince < 0.0)
		{
			SetClose(OwnerComp, Memory, false, Now);
		}
		else if (Now > Memory.Deadline)
		{
			FinishLatentTask(OwnerComp, Fail(OwnerComp, Medic, Patient));
		}
		else if (Controller->GetMoveStatus() == EPathFollowingStatus::Idle && Now >= Memory.NextMoveTime)
		{
			// Бег сбили (оглушение, уклонение от гранаты) или раненый ушёл с конца пути: идти снова.
			MoveToPatient(OwnerComp, Memory, Now);
		}
		return;
	}
	if (Memory.LostSince >= 0.0)
	{
		SetClose(OwnerComp, Memory, true, Now);
	}
	Memory.TimeLeft -= DeltaSeconds;
	if (Memory.TimeLeft > 0.f)
	{
		return;
	}
	UNoFearSquadSubsystem* Squads = UNoFearSquadSubsystem::Get(Medic);
	if (Squads && Squads->CompleteBandage(Medic, Patient))
	{
		FinishLatentTask(OwnerComp, EBTNodeResult::Succeeded);
		return;
	}
	FinishLatentTask(OwnerComp, Fail(OwnerComp, Medic, Patient));
}

void UBTTask_NoFearBandage::SetClose(UBehaviorTreeComponent& OwnerComp, FBandageMemory& Memory, bool bClose, double Now) const
{
	ANoFearAIController* Controller = Cast<ANoFearAIController>(OwnerComp.GetAIOwner());
	APawn* Patient = Memory.Patient.Get();
	if (!Controller || !Patient)
	{
		return;
	}
	if (bClose)
	{
		Memory.LostSince = -1.0;
		Memory.bReached = true;
		Controller->StopMovement();
		Controller->SetCrouching(true);
		Controller->SetFocus(Patient, EAIFocusPriority::Gameplay);
		Memory.bCrouched = true;
		return;
	}
	Memory.LostSince = Now;
	Memory.Deadline = Now + (Memory.bReached ? PatientLostTime : ApproachTime);
	if (Memory.bCrouched)
	{
		Controller->SetCrouching(false);
		Memory.bCrouched = false;
	}
	MoveToPatient(OwnerComp, Memory, Now);
}

void UBTTask_NoFearBandage::MoveToPatient(UBehaviorTreeComponent& OwnerComp, FBandageMemory& Memory, double Now) const
{
	AAIController* Controller = OwnerComp.GetAIOwner();
	APawn* Patient = Memory.Patient.Get();
	if (!Controller || !Patient)
	{
		return;
	}
	Memory.NextMoveTime = Now + 0.5;
	const EPathFollowingRequestResult::Type Result = Controller->MoveToActor(Patient, MaxPatientDistance * 0.4f,
		/*bStopOnOverlap=*/true, /*bUsePathfinding=*/true, /*bCanStrafe=*/true);
	if (Result == EPathFollowingRequestResult::Failed && !Memory.bReportedNoPath)
	{
		Memory.bReportedNoPath = true;
		UE_LOG(LogNoFearAI, Log, TEXT("Bandage: %s has no path to %s"), *GetNameSafe(Controller->GetPawn()), *GetNameSafe(Patient));
	}
}

EBTNodeResult::Type UBTTask_NoFearBandage::Fail(UBehaviorTreeComponent& OwnerComp, APawn* Medic, APawn* Patient) const
{
	// Снять приказ сразу: иначе ключ HealTarget останется, а дерево уйдёт в бой и к перевязке не вернётся до таймаута.
	if (UNoFearSquadSubsystem* Squads = Medic && Patient ? UNoFearSquadSubsystem::Get(Medic) : nullptr)
	{
		Squads->AbortBandage(Medic, Patient);
	}
	return EBTNodeResult::Failed;
}

void UBTTask_NoFearBandage::StopBandage(UBehaviorTreeComponent& OwnerComp, FBandageMemory& Memory) const
{
	ANoFearAIController* Controller = Cast<ANoFearAIController>(OwnerComp.GetAIOwner());
	if (Controller && Memory.Patient.IsValid())
	{
		if (Memory.LostSince >= 0.0)
		{
			Controller->StopMovement();
		}
		if (Memory.bCrouched)
		{
			Controller->SetCrouching(false);
		}
		Controller->ClearFocus(EAIFocusPriority::Gameplay);
	}
	Memory.bCrouched = false;
	Memory.LostSince = -1.0;
	Memory.Patient.Reset();
}

EBTNodeResult::Type UBTTask_NoFearBandage::AbortTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	StopBandage(OwnerComp, *CastInstanceNodeMemory<FBandageMemory>(NodeMemory));
	return EBTNodeResult::Aborted;
}

void UBTTask_NoFearBandage::OnTaskFinished(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, EBTNodeResult::Type TaskResult)
{
	StopBandage(OwnerComp, *CastInstanceNodeMemory<FBandageMemory>(NodeMemory));
	Super::OnTaskFinished(OwnerComp, NodeMemory, TaskResult);
}

FString UBTTask_NoFearBandage::GetStaticDescription() const
{
	return FString::Printf(TEXT("Bandage %s: %.1f s, within %.0f"), *GetSelectedBlackboardKey().ToString(), BandageTime, MaxPatientDistance);
}
