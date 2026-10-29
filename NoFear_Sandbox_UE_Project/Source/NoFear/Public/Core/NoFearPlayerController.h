#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "GenericTeamAgentInterface.h"
#include "NoFearPlayerController.generated.h"

/** Контроллер игрока. Всегда в команде Player. */
UCLASS()
class NOFEAR_API ANoFearPlayerController : public APlayerController, public IGenericTeamAgentInterface
{
	GENERATED_BODY()

public:
	// IGenericTeamAgentInterface
	virtual FGenericTeamId GetGenericTeamId() const override;
};
