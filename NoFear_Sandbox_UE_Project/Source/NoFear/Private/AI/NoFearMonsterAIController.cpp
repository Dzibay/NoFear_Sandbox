#include "AI/NoFearMonsterAIController.h"

#include "AI/NoFearMeleeComponent.h"
#include "BehaviorTree/BehaviorTree.h"
#include "Perception/AISenseConfig_Sight.h"
#include "UObject/ConstructorHelpers.h"

ANoFearMonsterAIController::ANoFearMonsterAIController(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// Бежит к цели лицом по ходу; прицела и стрейфа у монстра нет.
	bStrafeInCombat = false;
	bWalkWhenCalm = true;
	LeashRadius = 0.f;
	MaxTargetDistance = 4000.f;

	// Рык стаи слышно дальше, чем крик стрелка.
	AlertShareRadius = 3000.f;

	SightConfig->SightRadius = 2500.f;
	SightConfig->LoseSightRadius = 3000.f;
	SightConfig->PeripheralVisionAngleDegrees = 80.f;

	static ConstructorHelpers::FObjectFinderOptional<UBehaviorTree> Tree(TEXT("/Game/AI/BT_NoFear_Monster.BT_NoFear_Monster"));
	BehaviorTree = Tree.Get();
}

UNoFearMeleeComponent* ANoFearMonsterAIController::GetMeleeComponent() const
{
	return GetPawn() ? GetPawn()->FindComponentByClass<UNoFearMeleeComponent>() : nullptr;
}

void ANoFearMonsterAIController::OnPossess(APawn* InPawn)
{
	if (InPawn && bAddMeleeComponentIfMissing && !InPawn->FindComponentByClass<UNoFearMeleeComponent>())
	{
		UNoFearMeleeComponent* Melee = NewObject<UNoFearMeleeComponent>(InPawn, TEXT("MeleeComponent"));
		Melee->RegisterComponent();
	}

	Super::OnPossess(InPawn);
}
