#include "AI/NoFearCivilianAIController.h"

#include "BehaviorTree/BehaviorTree.h"
#include "UObject/ConstructorHelpers.h"

ANoFearCivilianAIController::ANoFearCivilianAIController(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	bCanAttack = false;
	bInvestigateLostTarget = false;
	bInvestigateNoises = false;
	bStrafeInCombat = false;

	static ConstructorHelpers::FObjectFinderOptional<UBehaviorTree> Tree(TEXT("/Game/AI/BT_NoFear_Civilian.BT_NoFear_Civilian"));
	BehaviorTree = Tree.Get();
}
