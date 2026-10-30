#include "AI/NoFearRecruitableComponent.h"

#include "AI/NoFearCompanionAIController.h"
#include "Core/NoFearHealthComponent.h"
#include "Core/NoFearPlayerController.h"
#include "Core/NoFearTeam.h"
#include "GameFramework/Pawn.h"

UNoFearRecruitableComponent::UNoFearRecruitableComponent()
{
	InteractionDistance = 300.f;
	RecruitText = NSLOCTEXT("NoFearInteraction", "Recruit", "Завербовать");
	WaitText = NSLOCTEXT("NoFearInteraction", "CompanionWait", "Жди здесь");
	FollowText = NSLOCTEXT("NoFearInteraction", "CompanionFollow", "За мной");
	PromptText = RecruitText;
}

bool UNoFearRecruitableComponent::CanInteract_Implementation(APawn* User) const
{
	if (!Super::CanInteract_Implementation(User))
	{
		return false;
	}
	const APawn* OwnerPawn = Cast<APawn>(GetOwner());
	return OwnerPawn
		&& !UNoFearHealthComponent::IsActorDead(OwnerPawn)
		&& Cast<ANoFearCompanionAIController>(OwnerPawn->GetController())
		&& Cast<ANoFearPlayerController>(User->GetController())
		&& !UNoFearTeamLibrary::AreHostile(OwnerPawn, User);
}

FText UNoFearRecruitableComponent::GetInteractionPrompt_Implementation(APawn* User) const
{
	APawn* OwnerPawn = Cast<APawn>(GetOwner());
	const ANoFearPlayerController* PlayerController = User ? Cast<ANoFearPlayerController>(User->GetController()) : nullptr;
	if (PlayerController && PlayerController->IsCompanion(OwnerPawn))
	{
		const ANoFearCompanionAIController* Companion = Cast<ANoFearCompanionAIController>(OwnerPawn->GetController());
		return Companion && Companion->IsFollowing() ? WaitText : FollowText;
	}
	return RecruitText;
}

void UNoFearRecruitableComponent::HandleInteract_Implementation(APawn* User)
{
	APawn* OwnerPawn = Cast<APawn>(GetOwner());
	ANoFearPlayerController* PlayerController = User ? Cast<ANoFearPlayerController>(User->GetController()) : nullptr;
	if (!PlayerController || !OwnerPawn)
	{
		return;
	}

	if (PlayerController->IsCompanion(OwnerPawn))
	{
		PlayerController->ToggleCompanionFollowing(OwnerPawn);
	}
	else
	{
		PlayerController->RecruitCompanion(OwnerPawn);
	}
}
