#include "AI/BTTask_NoFearCoverFight.h"

#include "AI/NoFearCombatComponent.h"
#include "AI/NoFearShooterAIController.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "Core/NoFearHealthComponent.h"
#include "Navigation/PathFollowingComponent.h"
#include "Weapons/NoFearWeapon.h"
#include "Weapons/NoFearWeaponComponent.h"
#include "Weapons/NoFearWeaponData.h"

namespace
{
	ANoFearShooterAIController* GetShooter(const UBehaviorTreeComponent& OwnerComp)
	{
		return Cast<ANoFearShooterAIController>(OwnerComp.GetAIOwner());
	}

	UNoFearWeaponComponent* GetWeapons(const ANoFearShooterAIController* Shooter)
	{
		return Shooter ? Shooter->GetWeaponComponent() : nullptr;
	}

	/** Ход начат и ещё идёт. */
	bool IsStillMoving(const ANoFearShooterAIController* Shooter, bool bMoving)
	{
		return bMoving && Shooter && Shooter->GetMoveStatus() != EPathFollowingStatus::Idle;
	}

	bool StartMove(ANoFearShooterAIController* Shooter, const FVector& Location)
	{
		const EPathFollowingRequestResult::Type Result = Shooter->MoveToLocation(Location, 15.f, false, true, false, true);
		return Result == EPathFollowingRequestResult::RequestSuccessful;
	}
}

UBTTask_NoFearCoverFight::UBTTask_NoFearCoverFight(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	NodeName = TEXT("NoFear Cover Fight");
	INIT_TASK_NODE_NOTIFY_FLAGS();
	bNotifyTick = true;
	bNotifyTaskFinished = true;
}

uint16 UBTTask_NoFearCoverFight::GetInstanceMemorySize() const
{
	return sizeof(FCoverFightMemory);
}

void UBTTask_NoFearCoverFight::InitializeMemory(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, EBTMemoryInit::Type InitType) const
{
	InitializeNodeMemory<FCoverFightMemory>(NodeMemory, InitType);
}

void UBTTask_NoFearCoverFight::CleanupMemory(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, EBTMemoryClear::Type CleanupType) const
{
	CleanupNodeMemory<FCoverFightMemory>(NodeMemory, CleanupType);
}

EBTNodeResult::Type UBTTask_NoFearCoverFight::ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	ANoFearShooterAIController* Shooter = GetShooter(OwnerComp);
	const UNoFearWeaponComponent* Weapons = GetWeapons(Shooter);
	if (!Shooter || !Shooter->HasCover() || !Shooter->GetCurrentTarget() || !Weapons || Weapons->GetWeapons().Num() == 0)
	{
		return EBTNodeResult::Failed;
	}

	FCoverFightMemory& Memory = *CastInstanceNodeMemory<FCoverFightMemory>(NodeMemory);
	Memory = FCoverFightMemory();
	Memory.CoverLocation = Shooter->GetCoverLocation();
	Memory.CyclesLeft = FMath::RandRange(FMath::Min(CyclesMin, CyclesMax), FMath::Max(CyclesMin, CyclesMax));
	EnterPhase(OwnerComp, Memory, EPhase::Hide);
	return EBTNodeResult::InProgress;
}

void UBTTask_NoFearCoverFight::EnterPhase(UBehaviorTreeComponent& OwnerComp, FCoverFightMemory& Memory, EPhase Phase) const
{
	ANoFearShooterAIController* Shooter = GetShooter(OwnerComp);
	const APawn* Pawn = Shooter ? Shooter->GetPawn() : nullptr;
	if (!Pawn)
	{
		return;
	}
	const double Now = OwnerComp.GetWorld()->GetTimeSeconds();
	Memory.Phase = Phase;
	Memory.PhaseStartTime = Now;
	Memory.bMoving = false;
	const bool bLow = Shooter->IsCoverLow();

	switch (Phase)
	{
	case EPhase::Hide:
	{
		ReleaseTrigger(OwnerComp, Memory);
		Shooter->SetCrouching(bLow);
		float HideTime = FMath::FRandRange(Shooter->CoverHideTime.X, FMath::Max(Shooter->CoverHideTime.X, Shooter->CoverHideTime.Y));
		if (Shooter->IsCoveringFire())
		{
			HideTime *= 0.4f;
		}
		const UNoFearCombatComponent* Combat = Shooter->GetCombatComponent();
		if (Combat && Combat->IsSuppressed())
		{
			HideTime *= 1.5f;
		}
		Memory.PhaseEndTime = Now + HideTime;
		if (FVector::Dist2D(Pawn->GetActorLocation(), Memory.CoverLocation) > 40.f)
		{
			Memory.bMoving = StartMove(Shooter, Memory.CoverLocation);
		}
		Shooter->SetCoverPhase(TEXT("Hide"));
		break;
	}
	case EPhase::Peek:
		if (bLow)
		{
			Shooter->SetCrouching(false);
			Memory.PhaseEndTime = Now + StandUpTime;
		}
		else
		{
			Memory.bMoving = Shooter->CanPeekFromCover() && StartMove(Shooter, Shooter->GetCoverPeekLocation());
			Memory.PhaseEndTime = Now + MoveTimeout;
		}
		Shooter->SetCoverPhase(TEXT("Peek"));
		break;

	case EPhase::Fire:
		Memory.BurstsLeft = FMath::RandRange(FMath::Min(BurstsMin, BurstsMax), FMath::Max(BurstsMin, BurstsMax)) + (Shooter->IsCoveringFire() ? 1 : 0);
		Memory.bBurstActive = true;
		Memory.PhaseEndTime = Now + FMath::FRandRange(BurstDurationMin, FMath::Max(BurstDurationMin, BurstDurationMax));
		Memory.NextTriggerTime = Now;
		Memory.NotReadySince = -1.0;
		Shooter->SetCoverPhase(TEXT("Fire"));
		break;

	case EPhase::Return:
		ReleaseTrigger(OwnerComp, Memory);
		if (bLow)
		{
			Shooter->SetCrouching(true);
			Memory.PhaseEndTime = Now + 0.2;
		}
		else
		{
			Memory.bMoving = FVector::Dist2D(Pawn->GetActorLocation(), Memory.CoverLocation) > 20.f && StartMove(Shooter, Memory.CoverLocation);
			Memory.PhaseEndTime = Now + MoveTimeout;
		}
		Shooter->SetCoverPhase(TEXT("Return"));
		break;
	}
}

void UBTTask_NoFearCoverFight::ReleaseTrigger(UBehaviorTreeComponent& OwnerComp, FCoverFightMemory& Memory) const
{
	if (Memory.bTriggerHeld)
	{
		if (UNoFearWeaponComponent* Weapons = GetWeapons(GetShooter(OwnerComp)))
		{
			Weapons->StopFire();
		}
		Memory.bTriggerHeld = false;
	}
}

void UBTTask_NoFearCoverFight::TickTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds)
{
	FCoverFightMemory& Memory = *CastInstanceNodeMemory<FCoverFightMemory>(NodeMemory);
	ANoFearShooterAIController* Shooter = GetShooter(OwnerComp);
	UNoFearWeaponComponent* Weapons = GetWeapons(Shooter);
	const AActor* Target = Shooter ? Shooter->GetCurrentTarget() : nullptr;
	if (!Shooter || !Weapons || Weapons->IsOwnerDead() || !Target || UNoFearHealthComponent::IsActorDead(Target))
	{
		FinishLatentTask(OwnerComp, EBTNodeResult::Succeeded);
		return;
	}
	// Укрытие сменилось или пропало: дерево отведёт к новому.
	if (!Shooter->HasCover() || FVector::Dist2D(Shooter->GetCoverLocation(), Memory.CoverLocation) > 10.f)
	{
		FinishLatentTask(OwnerComp, EBTNodeResult::Succeeded);
		return;
	}

	const double Now = OwnerComp.GetWorld()->GetTimeSeconds();
	ANoFearWeapon* Weapon = Weapons->GetCurrentWeapon();
	const UNoFearWeaponData* Data = Weapon ? Weapon->GetWeaponData() : nullptr;
	const bool bWeaponIdle = Data && Weapon->GetWeaponState() == ENoFearWeaponState::Idle;
	// Санитар рядом: спрятаться и не высовываться, пока перевязывают.
	const bool bAwaitingMedic = Shooter->IsAwaitingMedic();
	if (bAwaitingMedic && (Memory.Phase == EPhase::Peek || Memory.Phase == EPhase::Fire))
	{
		EnterPhase(OwnerComp, Memory, EPhase::Return);
		return;
	}

	switch (Memory.Phase)
	{
	case EPhase::Hide:
		// Перезарядка за укрытием: магазин неполон — сейчас, пока не видно.
		if (bWeaponIdle && Weapon->GetReserveAmmo() > 0
			&& Weapon->GetMagazineAmmo() < FMath::CeilToInt(Data->MagazineSize * Shooter->CoverReloadFraction))
		{
			Weapons->Reload();
		}
		if (!bWeaponIdle || bAwaitingMedic || IsStillMoving(Shooter, Memory.bMoving))
		{
			Memory.PhaseEndTime = FMath::Max(Memory.PhaseEndTime, Now + 0.1);
			return;
		}
		if (Now >= Memory.PhaseEndTime)
		{
			EnterPhase(OwnerComp, Memory, EPhase::Peek);
		}
		return;

	case EPhase::Peek:
		if (Now >= Memory.PhaseEndTime || (!Shooter->IsCoverLow() && !IsStillMoving(Shooter, Memory.bMoving)))
		{
			EnterPhase(OwnerComp, Memory, EPhase::Fire);
		}
		return;

	case EPhase::Return:
		if (Now >= Memory.PhaseEndTime || (!Shooter->IsCoverLow() && !IsStillMoving(Shooter, Memory.bMoving)))
		{
			// Ожидание санитара — не цикл боя: иначе задача кончится и укрытие перепроверят посреди перевязки.
			if (!bAwaitingMedic && --Memory.CyclesLeft <= 0)
			{
				Shooter->RequestCoverRefresh();
				FinishLatentTask(OwnerComp, EBTNodeResult::Succeeded);
				return;
			}
			EnterPhase(OwnerComp, Memory, EPhase::Hide);
		}
		return;

	case EPhase::Fire:
		break;
	}

	// Fire: пустой магазин или оружие занято — назад за укрытие.
	if (!bWeaponIdle || Weapon->GetMagazineAmmo() <= 0)
	{
		EnterPhase(OwnerComp, Memory, EPhase::Return);
		return;
	}
	const UNoFearCombatComponent* Combat = Shooter->GetCombatComponent();
	if (Combat && Combat->IsFriendlyInLineOfFire())
	{
		// Свой на линии огня: не стрелять, спрятаться, укрытие перепроверить.
		Shooter->RequestCoverRefresh();
		EnterPhase(OwnerComp, Memory, EPhase::Return);
		return;
	}
	// Подавление по приказу отряда: цель не видна — огонь туда, где её видели свои.
	const bool bBlind = Combat && Shooter->IsSuppressing() && Combat->CanBlindFire();
	if (Combat && !Combat->IsReadyToFire() && !bBlind)
	{
		ReleaseTrigger(OwnerComp, Memory);
		if (Memory.NotReadySince < 0.0)
		{
			Memory.NotReadySince = Now;
		}
		else if (Now - Memory.NotReadySince > PeekWaitTime)
		{
			EnterPhase(OwnerComp, Memory, EPhase::Return);
		}
		return;
	}
	Memory.NotReadySince = -1.0;

	if (Memory.bBurstActive && Now >= Memory.PhaseEndTime)
	{
		ReleaseTrigger(OwnerComp, Memory);
		if (--Memory.BurstsLeft <= 0)
		{
			EnterPhase(OwnerComp, Memory, EPhase::Return);
			return;
		}
		Memory.bBurstActive = false;
		Memory.PhaseEndTime = Now + FMath::FRandRange(BurstPauseMin, FMath::Max(BurstPauseMin, BurstPauseMax));
		return;
	}
	if (!Memory.bBurstActive)
	{
		if (Now < Memory.PhaseEndTime)
		{
			return;
		}
		Memory.bBurstActive = true;
		Memory.PhaseEndTime = Now + FMath::FRandRange(BurstDurationMin, FMath::Max(BurstDurationMin, BurstDurationMax));
		Memory.NextTriggerTime = Now;
	}

	if (Data->FireMode == ENoFearFireMode::Auto)
	{
		if (!Memory.bTriggerHeld)
		{
			Weapons->StartFire();
			Memory.bTriggerHeld = true;
		}
	}
	else if (Now >= Memory.NextTriggerTime)
	{
		Weapons->StartFire();
		Weapons->StopFire();
		Memory.NextTriggerTime = Now + FMath::FRandRange(TriggerIntervalMin, FMath::Max(TriggerIntervalMin, TriggerIntervalMax));
	}
}

void UBTTask_NoFearCoverFight::Cleanup(UBehaviorTreeComponent& OwnerComp, FCoverFightMemory& Memory) const
{
	ReleaseTrigger(OwnerComp, Memory);
	ANoFearShooterAIController* Shooter = GetShooter(OwnerComp);
	if (!Shooter)
	{
		return;
	}
	if (Memory.bMoving)
	{
		Shooter->StopMovement();
		Memory.bMoving = false;
	}
	// Остаётся сидеть, только если снова будет драться из этого же низкого укрытия.
	if (!(Shooter->IsInCover() && Shooter->IsCoverLow() && FVector::Dist2D(Shooter->GetCoverLocation(), Memory.CoverLocation) <= 10.f))
	{
		Shooter->SetCrouching(false);
	}
	Shooter->SetCoverPhase(FString());
}

EBTNodeResult::Type UBTTask_NoFearCoverFight::AbortTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	FCoverFightMemory& Memory = *CastInstanceNodeMemory<FCoverFightMemory>(NodeMemory);
	Cleanup(OwnerComp, Memory);
	if (ANoFearShooterAIController* Shooter = GetShooter(OwnerComp))
	{
		Shooter->SetCrouching(false);
	}
	return EBTNodeResult::Aborted;
}

void UBTTask_NoFearCoverFight::OnTaskFinished(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, EBTNodeResult::Type TaskResult)
{
	Cleanup(OwnerComp, *CastInstanceNodeMemory<FCoverFightMemory>(NodeMemory));
	Super::OnTaskFinished(OwnerComp, NodeMemory, TaskResult);
}

FString UBTTask_NoFearCoverFight::GetStaticDescription() const
{
	return FString::Printf(TEXT("Hide / peek / %d-%d bursts, %d-%d cycles"), BurstsMin, BurstsMax, CyclesMin, CyclesMax);
}

// ---------------------------------------------------------------------------
// Find combat position
// ---------------------------------------------------------------------------

UBTTask_NoFearFindCombatPosition::UBTTask_NoFearFindCombatPosition(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	NodeName = TEXT("NoFear Find Combat Position");
	BlackboardKey.AddVectorFilter(this, GET_MEMBER_NAME_CHECKED(UBTTask_NoFearFindCombatPosition, BlackboardKey));
	BlackboardKey.SelectedKeyName = FNoFearBotKeys::MoveGoal;
}

EBTNodeResult::Type UBTTask_NoFearFindCombatPosition::ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	const ANoFearShooterAIController* Shooter = GetShooter(OwnerComp);
	UBlackboardComponent* Blackboard = OwnerComp.GetBlackboardComponent();
	FVector Location;
	if (!Shooter || !Blackboard || !Shooter->FindCombatPosition(Location))
	{
		return EBTNodeResult::Failed;
	}
	Blackboard->SetValueAsVector(GetSelectedBlackboardKey(), Location);
	return EBTNodeResult::Succeeded;
}

FString UBTTask_NoFearFindCombatPosition::GetStaticDescription() const
{
	return FString::Printf(TEXT("%s = combat position at weapon range"), *GetSelectedBlackboardKey().ToString());
}
