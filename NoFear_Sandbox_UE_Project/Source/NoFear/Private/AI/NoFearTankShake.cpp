#include "AI/NoFearTankShake.h"

#include "Shakes/PerlinNoiseCameraShakePattern.h"

UNoFearTankRoarShake::UNoFearTankRoarShake(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	UPerlinNoiseCameraShakePattern* Pattern = CreateDefaultSubobject<UPerlinNoiseCameraShakePattern>(TEXT("RootShakePattern"));
	Pattern->Duration = 1.1f;
	Pattern->BlendInTime = 0.1f;
	Pattern->BlendOutTime = 0.4f;
	Pattern->X.Amplitude = 1.5f;
	Pattern->X.Frequency = 28.f;
	Pattern->Y.Amplitude = 1.5f;
	Pattern->Y.Frequency = 24.f;
	Pattern->Z.Amplitude = 2.5f;
	Pattern->Z.Frequency = 30.f;
	Pattern->Pitch.Amplitude = 0.9f;
	Pattern->Pitch.Frequency = 26.f;
	Pattern->Yaw.Amplitude = 0.9f;
	Pattern->Yaw.Frequency = 22.f;
	Pattern->Roll.Amplitude = 0.6f;
	Pattern->Roll.Frequency = 20.f;
	SetRootShakePattern(Pattern);
}

UNoFearTankStepShake::UNoFearTankStepShake(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	UPerlinNoiseCameraShakePattern* Pattern = CreateDefaultSubobject<UPerlinNoiseCameraShakePattern>(TEXT("RootShakePattern"));
	Pattern->Duration = 0.3f;
	Pattern->BlendInTime = 0.04f;
	Pattern->BlendOutTime = 0.2f;
	Pattern->Z.Amplitude = 0.9f;
	Pattern->Z.Frequency = 18.f;
	Pattern->Pitch.Amplitude = 0.25f;
	Pattern->Pitch.Frequency = 16.f;
	Pattern->Roll.Amplitude = 0.2f;
	Pattern->Roll.Frequency = 14.f;
	SetRootShakePattern(Pattern);
}
