#pragma once

#include "CoreMinimal.h"
#include "Camera/CameraShakeBase.h"
#include "NoFearTankShake.generated.h"

/** Дрожь камеры на рёв колосса: короткая и сильная. */
UCLASS()
class NOFEAR_API UNoFearTankRoarShake : public UCameraShakeBase
{
	GENERATED_BODY()

public:
	UNoFearTankRoarShake(const FObjectInitializer& ObjectInitializer);
};

/** Резкий толчок камеры, когда загонщик задел игрока. */
UCLASS()
class NOFEAR_API UNoFearHunterHitShake : public UCameraShakeBase
{
	GENERATED_BODY()

public:
	UNoFearHunterHitShake(const FObjectInitializer& ObjectInitializer);
};

/** Слабая дрожь камеры на шаг колосса. */
UCLASS()
class NOFEAR_API UNoFearTankStepShake : public UCameraShakeBase
{
	GENERATED_BODY()

public:
	UNoFearTankStepShake(const FObjectInitializer& ObjectInitializer);
};
