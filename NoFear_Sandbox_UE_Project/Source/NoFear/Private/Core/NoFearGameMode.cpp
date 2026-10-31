#include "Core/NoFearGameMode.h"

#include "Core/NoFearHUD.h"
#include "Core/NoFearPlayerController.h"
#include "Core/NoFearTeam.h"
#include "UObject/ConstructorHelpers.h"

ANoFearGameMode::ANoFearGameMode()
{
	PlayerControllerClass = ANoFearPlayerController::StaticClass();
	HUDClass = ANoFearHUD::StaticClass();

	static ConstructorHelpers::FClassFinder<APawn> PlayerPawnClass(TEXT("/Game/Blueprints/CBP_Player"));
	if (PlayerPawnClass.Succeeded())
	{
		DefaultPawnClass = PlayerPawnClass.Class;
	}
}

void ANoFearGameMode::InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage)
{
	Super::InitGame(MapName, Options, ErrorMessage);

	// AI Perception различает друзей и врагов через этот решатель.
	FGenericTeamId::SetAttitudeSolver(&UNoFearTeamLibrary::GetAttitude);
}
