#include "Portal/NoFearFreeComponent.h"

#include "Core/NoFearHealthComponent.h"
#include "Core/NoFearPlayerController.h"
#include "GameFramework/Pawn.h"
#include "Portal/NoFearPortalSubsystem.h"

UNoFearFreeComponent::UNoFearFreeComponent()
{
	InteractionDistance = 260.f;
	PromptText = NSLOCTEXT("NoFearInteraction", "Free", "Освободить (держать E)");
}

bool UNoFearFreeComponent::CanInteract_Implementation(APawn* User) const
{
	if (!Super::CanInteract_Implementation(User) || !User)
	{
		return false;
	}
	const APawn* OwnerPawn = Cast<APawn>(GetOwner());
	const UNoFearPortalSubsystem* Portals = UNoFearPortalSubsystem::Get(this);
	return OwnerPawn && Portals && Portals->IsFeeder(OwnerPawn)
		&& Cast<ANoFearPlayerController>(User->GetController())
		&& !UNoFearHealthComponent::IsActorIncapacitated(User);
}

void UNoFearFreeComponent::HandleInteract_Implementation(APawn* User)
{
	APlayerController* PlayerController = User ? Cast<APlayerController>(User->GetController()) : nullptr;
	UNoFearPortalSubsystem* Portals = UNoFearPortalSubsystem::Get(this);
	if (!PlayerController || !Portals)
	{
		return;
	}
	// Нажали E — держим, пока клавишу не отпустят; вызвали из Blueprint без клавиши — идёт до StopHeroFreeing.
	TArray<FKey> Keys;
	UNoFearPortalSubsystem::GetInteractKeys(PlayerController, Keys);
	bool bHeld = false;
	for (const FKey& Key : Keys)
	{
		bHeld = bHeld || PlayerController->IsInputKeyDown(Key);
	}
	Portals->StartHeroFreeing(PlayerController, Cast<APawn>(GetOwner()), bHeld);
}
