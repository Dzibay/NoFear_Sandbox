#include "Core/NoFearPlayerController.h"

#include "Core/NoFearTeam.h"

FGenericTeamId ANoFearPlayerController::GetGenericTeamId() const
{
	return FGenericTeamId(static_cast<uint8>(ENoFearTeam::Player));
}
