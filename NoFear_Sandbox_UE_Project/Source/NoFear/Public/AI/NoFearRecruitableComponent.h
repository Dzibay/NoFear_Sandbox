#pragma once

#include "CoreMinimal.h"
#include "Core/NoFearInteractableComponent.h"
#include "NoFearRecruitableComponent.generated.h"

/**
 * Вербовка напарника клавишей E. Ставится на тело с ANoFearCompanionAIController (CBP_Bot_Companion).
 * Чужой напарник — «Завербовать» (ANoFearPlayerController::RecruitCompanion), свой — «Жди здесь» / «За мной».
 * Враждебного, мёртвого или бота без контроллера напарника завербовать нельзя.
 */
UCLASS(ClassGroup = (NoFear), meta = (BlueprintSpawnableComponent))
class NOFEAR_API UNoFearRecruitableComponent : public UNoFearInteractableComponent
{
	GENERATED_BODY()

public:
	UNoFearRecruitableComponent();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interaction")
	FText RecruitText;

	/** Подсказка своему напарнику, который идёт следом. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interaction")
	FText WaitText;

	/** Подсказка своему напарнику, который ждёт. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interaction")
	FText FollowText;

	virtual bool CanInteract_Implementation(APawn* User) const override;
	virtual FText GetInteractionPrompt_Implementation(APawn* User) const override;

protected:
	virtual void HandleInteract_Implementation(APawn* User) override;
};
