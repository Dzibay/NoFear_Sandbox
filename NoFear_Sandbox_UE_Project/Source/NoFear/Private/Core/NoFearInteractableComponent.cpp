#include "Core/NoFearInteractableComponent.h"

#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/Pawn.h"

namespace
{
	/** Объекты взаимодействия всех миров; искать по миру редко и немного, отдельная подсистема не нужна. */
	TArray<TWeakObjectPtr<UNoFearInteractableComponent>> GInteractables;
}

UNoFearInteractableComponent::UNoFearInteractableComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	PromptText = NSLOCTEXT("NoFearInteraction", "DefaultPrompt", "Взаимодействовать");
}

void UNoFearInteractableComponent::BeginPlay()
{
	Super::BeginPlay();
	GInteractables.AddUnique(this);
}

void UNoFearInteractableComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	GInteractables.RemoveAll([this](const TWeakObjectPtr<UNoFearInteractableComponent>& Entry)
	{
		return !Entry.IsValid() || Entry.Get() == this;
	});
	Super::EndPlay(EndPlayReason);
}

void UNoFearInteractableComponent::GetInteractables(const UWorld* World, TArray<UNoFearInteractableComponent*>& OutInteractables)
{
	OutInteractables.Reset();
	for (const TWeakObjectPtr<UNoFearInteractableComponent>& Entry : GInteractables)
	{
		if (UNoFearInteractableComponent* Interactable = Entry.Get(); Interactable && Interactable->GetWorld() == World)
		{
			OutInteractables.Add(Interactable);
		}
	}
}

bool UNoFearInteractableComponent::CanInteract_Implementation(APawn* User) const
{
	return bInteractionEnabled && User && GetOwner() && User != GetOwner();
}

FText UNoFearInteractableComponent::GetInteractionPrompt_Implementation(APawn* User) const
{
	return PromptText;
}

bool UNoFearInteractableComponent::IsInRange(const APawn* User) const
{
	return User && GetOwner() && FVector::DistSquared(User->GetActorLocation(), GetOwner()->GetActorLocation()) <= FMath::Square(InteractionDistance);
}

FVector UNoFearInteractableComponent::GetPromptLocation() const
{
	const AActor* Owner = GetOwner();
	if (!Owner)
	{
		return FVector::ZeroVector;
	}
	if (const ACharacter* Character = Cast<ACharacter>(Owner); Character && Character->GetCapsuleComponent())
	{
		return Owner->GetActorLocation() + FVector(0.f, 0.f, Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 25.f);
	}
	return Owner->GetActorLocation();
}

bool UNoFearInteractableComponent::Interact(APawn* User)
{
	if (!IsInRange(User) || !CanInteract(User))
	{
		return false;
	}
	HandleInteract(User);
	OnInteracted.Broadcast(this, User);
	return true;
}

void UNoFearInteractableComponent::HandleInteract_Implementation(APawn* User)
{
}
