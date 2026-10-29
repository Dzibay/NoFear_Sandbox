#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "NoFearGameMode.generated.h"

/**
 * Режим игры по умолчанию (DefaultEngine.ini → GlobalDefaultGameMode).
 * Игрок — CBP_SandboxCharacter, контроллер — ANoFearPlayerController.
 */
UCLASS()
class NOFEAR_API ANoFearGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	ANoFearGameMode();

	virtual void InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage) override;
};
