#include "AI/EnvQueryContext_NoFearThreat.h"

#include "AI/NoFearCombatComponent.h"
#include "AI/NoFearCombatSubsystem.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "Blueprint/AIBlueprintHelperLibrary.h"
#include "EnvironmentQuery/EnvQueryTypes.h"
#include "EnvironmentQuery/Items/EnvQueryItemType_Actor.h"
#include "EnvironmentQuery/Items/EnvQueryItemType_Point.h"

void UEnvQueryContext_NoFearThreat::ProvideContext(FEnvQueryInstance& QueryInstance, FEnvQueryContextData& ContextData) const
{
	AActor* Querier = Cast<AActor>(QueryInstance.Owner.Get());
	if (!Querier)
	{
		return;
	}

	if (const UNoFearCombatComponent* Combat = UNoFearCombatSubsystem::FindCombatComponent(Querier))
	{
		AActor* Target = Combat->GetTarget();
		if (Target && !Combat->HasLineOfSight() && Combat->GetTimeSinceTargetSeen() > TrackHiddenTargetTime)
		{
			UEnvQueryItemType_Point::SetContextHelper(ContextData, Combat->GetLastKnownTargetLocation());
			return;
		}
		if (AActor* Threat = Combat->GetThreatActor())
		{
			UEnvQueryItemType_Actor::SetContextHelper(ContextData, Threat);
			return;
		}
		if (!Combat->GetLastShotOrigin().IsZero())
		{
			UEnvQueryItemType_Point::SetContextHelper(ContextData, Combat->GetLastShotOrigin());
		}
		return;
	}

	// Бот без NoFear Combat: берём цель из блэкборда.
	if (const UBlackboardComponent* Blackboard = UAIBlueprintHelperLibrary::GetBlackboard(Querier))
	{
		if (const AActor* Target = Cast<AActor>(Blackboard->GetValueAsObject(TEXT("AttackTarget"))))
		{
			UEnvQueryItemType_Actor::SetContextHelper(ContextData, Target);
		}
	}
}
