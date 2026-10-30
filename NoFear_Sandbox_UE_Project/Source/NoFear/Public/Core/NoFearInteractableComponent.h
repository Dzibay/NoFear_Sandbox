#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "NoFearInteractableComponent.generated.h"

class UNoFearInteractableComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FNoFearInteractedSignature, UNoFearInteractableComponent*, Interactable, APawn*, User);

/**
 * То, с чем игрок взаимодействует клавишей E (IA_Interact). ANoFearPlayerController каждый кадр выбирает
 * доступный объект в радиусе Interaction Distance, ближайший к центру экрана, а HUD пишет над ним «[E] …».
 * Наследники переопределяют CanInteract, GetInteractionPrompt и HandleInteract (в C++ или Blueprint).
 */
UCLASS(Blueprintable, ClassGroup = (NoFear), meta = (BlueprintSpawnableComponent))
class NOFEAR_API UNoFearInteractableComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UNoFearInteractableComponent();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interaction")
	bool bInteractionEnabled = true;

	/** С какого расстояния до владельца можно взаимодействовать. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interaction", meta = (ClampMin = "0"))
	float InteractionDistance = 300.f;

	/** Подсказка по умолчанию («Открыть», «Поговорить»). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interaction")
	FText PromptText;

	/** Можно ли сейчас взаимодействовать (без учёта расстояния). */
	UFUNCTION(BlueprintNativeEvent, BlueprintPure, Category = "NoFear|Interaction")
	bool CanInteract(APawn* User) const;

	/** Что написать после «[E]». */
	UFUNCTION(BlueprintNativeEvent, BlueprintPure, Category = "NoFear|Interaction")
	FText GetInteractionPrompt(APawn* User) const;

	/** Пользователь достаточно близко. */
	UFUNCTION(BlueprintPure, Category = "NoFear|Interaction")
	bool IsInRange(const APawn* User) const;

	/** Взаимодействовать. false — сейчас нельзя (далеко или CanInteract вернул false). */
	UFUNCTION(BlueprintCallable, Category = "NoFear|Interaction")
	bool Interact(APawn* User);

	/** Точка, над которой HUD рисует подсказку: верх капсулы персонажа или центр актора. */
	UFUNCTION(BlueprintPure, Category = "NoFear|Interaction")
	FVector GetPromptLocation() const;

	UPROPERTY(BlueprintAssignable, Category = "NoFear|Interaction")
	FNoFearInteractedSignature OnInteracted;

	/** Все объекты взаимодействия, начавшие игру в этом мире. */
	static void GetInteractables(const UWorld* World, TArray<UNoFearInteractableComponent*>& OutInteractables);

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** Само действие. База ничего не делает: подписывайся на OnInteracted или переопредели. */
	UFUNCTION(BlueprintNativeEvent, Category = "NoFear|Interaction")
	void HandleInteract(APawn* User);
};
