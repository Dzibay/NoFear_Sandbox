#include "AI/NoFearCompanionAIController.h"

#include "AI/NoFearRecruitableComponent.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "Core/NoFearHealthComponent.h"
#include "GameFramework/Controller.h"
#include "GameFramework/Pawn.h"

ANoFearCompanionAIController::ANoFearCompanionAIController(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// Без лидера — обычный союзник со своим поводком (LeashRadius стрелка).
}

void ANoFearCompanionAIController::OnPossess(APawn* InPawn)
{
	if (InPawn && bAddRecruitableComponentIfMissing && !InPawn->FindComponentByClass<UNoFearRecruitableComponent>())
	{
		UNoFearRecruitableComponent* Recruitable = NewObject<UNoFearRecruitableComponent>(InPawn, TEXT("RecruitableComponent"));
		Recruitable->RegisterComponent();
	}

	Super::OnPossess(InPawn);
}

APawn* ANoFearCompanionAIController::GetLeaderPawn() const
{
	APawn* LeaderPawn = Leader.IsValid() ? Leader->GetPawn() : nullptr;
	return LeaderPawn && !UNoFearHealthComponent::IsActorDead(LeaderPawn) ? LeaderPawn : nullptr;
}

void ANoFearCompanionAIController::SetLeader(AController* NewLeader)
{
	if (Leader.Get() == NewLeader)
	{
		return;
	}

	Leader = NewLeader;
	bFollowing = NewLeader != nullptr;
	if (!NewLeader && GetPawn())
	{
		// Отпущенный напарник остаётся союзником там, где стоит.
		HomeLocation = GetPawn()->GetActorLocation();
	}
	WriteBlackboard();
	UpdateMovementStance();
	OnCompanionStateChanged.Broadcast(this);
}

void ANoFearCompanionAIController::SetFollowing(bool bNewFollowing)
{
	bNewFollowing = bNewFollowing && Leader.IsValid();
	if (bFollowing == bNewFollowing)
	{
		return;
	}

	bFollowing = bNewFollowing;
	if (!bFollowing && GetPawn())
	{
		HomeLocation = GetPawn()->GetActorLocation();
	}
	WriteBlackboard();
	UpdateMovementStance();
	OnCompanionStateChanged.Broadcast(this);
}

FVector ANoFearCompanionAIController::GetLeashAnchor() const
{
	if (const APawn* LeaderPawn = IsFollowing() ? GetLeaderPawn() : nullptr)
	{
		return LeaderPawn->GetActorLocation();
	}
	return Super::GetLeashAnchor();
}

float ANoFearCompanionAIController::GetLeashRadius() const
{
	return IsFollowing() && GetLeaderPawn() ? FollowLeashRadius : Super::GetLeashRadius();
}

float ANoFearCompanionAIController::GetDistanceToLeader() const
{
	const APawn* LeaderPawn = GetLeaderPawn();
	return LeaderPawn && GetPawn() ? FVector::Dist(LeaderPawn->GetActorLocation(), GetPawn()->GetActorLocation()) : 0.f;
}

void ANoFearCompanionAIController::UpdateBot()
{
	// Пока идёт следом, «дом» — где стоит сам: если лидер погибнет или скажет ждать, напарник останется здесь.
	if (IsFollowing() && GetPawn())
	{
		HomeLocation = GetPawn()->GetActorLocation();
	}

	Super::UpdateBot();
}

bool ANoFearCompanionAIController::IsValidTarget(const AActor* Candidate) const
{
	if (!Super::IsValidTarget(Candidate))
	{
		return false;
	}
	// Слишком отстал — сначала догнать лидера.
	return !IsFollowing() || !GetLeaderPawn() || GetDistanceToLeader() <= BreakOffDistance;
}

bool ANoFearCompanionAIController::ShouldWalk() const
{
	if (const APawn* LeaderPawn = IsFollowing() && !GetCurrentTarget() ? GetLeaderPawn() : nullptr)
	{
		return GetDistanceToLeader() < RunToLeaderDistance && LeaderPawn->GetVelocity().Size2D() < LeaderRunSpeed;
	}
	return Super::ShouldWalk();
}

void ANoFearCompanionAIController::WriteBlackboard()
{
	Super::WriteBlackboard();

	if (UBlackboardComponent* BlackboardComp = GetBlackboardComponent())
	{
		APawn* LeaderPawn = IsFollowing() ? GetLeaderPawn() : nullptr;
		BlackboardComp->SetValueAsObject(FNoFearBotKeys::Leader, LeaderPawn);
		BlackboardComp->SetValueAsBool(FNoFearBotKeys::IsFollowing, LeaderPawn != nullptr);
	}
}
