#include "AI/NoFearCalmComponent.h"

#include "Core/NoFearFearComponent.h"
#include "Core/NoFearFearSubsystem.h"
#include "Core/NoFearHealthComponent.h"
#include "Core/NoFearPlayerController.h"
#include "Core/NoFearTeam.h"
#include "GameFramework/Pawn.h"
#include "Portal/NoFearPortalSubsystem.h"

UNoFearCalmComponent::UNoFearCalmComponent()
{
	InteractionDistance = 300.f;
	PromptText = NSLOCTEXT("NoFearInteraction", "Calm", "Успокоить (держать E)");
}

bool UNoFearCalmComponent::CanInteract_Implementation(APawn* User) const
{
	if (!Super::CanInteract_Implementation(User) || !User)
	{
		return false;
	}
	const APawn* OwnerPawn = Cast<APawn>(GetOwner());
	const UNoFearFearComponent* Fear = UNoFearFearComponent::FindFear(OwnerPawn);
	const UNoFearFearSubsystem* Subsystem = UNoFearFearSubsystem::Get(this);
	return OwnerPawn && Fear && Subsystem
		&& !UNoFearHealthComponent::IsActorDead(OwnerPawn)
		&& !UNoFearPortalSubsystem::IsActorFeeder(OwnerPawn)
		&& Fear->GetFear() >= Subsystem->AnxiousThreshold
		&& Cast<ANoFearPlayerController>(User->GetController())
		&& !UNoFearTeamLibrary::AreHostile(OwnerPawn, User);
}

void UNoFearCalmComponent::HandleInteract_Implementation(APawn* User)
{
	if (ANoFearPlayerController* PlayerController = User ? Cast<ANoFearPlayerController>(User->GetController()) : nullptr)
	{
		PlayerController->StartCalming(Cast<APawn>(GetOwner()));
	}
}
