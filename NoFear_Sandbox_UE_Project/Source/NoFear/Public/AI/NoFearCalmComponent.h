#pragma once

#include "CoreMinimal.h"
#include "Core/NoFearInteractableComponent.h"
#include "NoFearCalmComponent.generated.h"

/**
 * Успокоить испуганного мирного, удерживая E (этап 11). Мирный контроллер сам ставит этот компонент на тело.
 * Доступно, пока страх не ниже порога тревоги. Пока E держат и герой рядом, страх снимается
 * со скоростью CalmRate (ANoFearPlayerController::StartCalming). Удобнее всего — тех, кто прячется:
 * бегущего в панике не догнать.
 */
UCLASS(ClassGroup = (NoFear), meta = (BlueprintSpawnableComponent))
class NOFEAR_API UNoFearCalmComponent : public UNoFearInteractableComponent
{
	GENERATED_BODY()

public:
	UNoFearCalmComponent();

	virtual bool CanInteract_Implementation(APawn* User) const override;

protected:
	virtual void HandleInteract_Implementation(APawn* User) override;
};
