#pragma once

#include "CoreMinimal.h"
#include "Core/NoFearInteractableComponent.h"
#include "NoFearFreeComponent.generated.h"

/**
 * Вывести проводника из транса, удерживая E (этап 13). Подсистема порталов ставит компонент на проводника
 * и снимает, когда тот очнулся или погиб. Пока E держат и герой рядом, освобождение растёт за HeroFreeTime;
 * отпустил — откатывается (UNoFearPortalSubsystem::StartHeroFreeing).
 */
UCLASS(ClassGroup = (NoFear), meta = (BlueprintSpawnableComponent))
class NOFEAR_API UNoFearFreeComponent : public UNoFearInteractableComponent
{
	GENERATED_BODY()

public:
	UNoFearFreeComponent();

	virtual bool CanInteract_Implementation(APawn* User) const override;

protected:
	virtual void HandleInteract_Implementation(APawn* User) override;
};
