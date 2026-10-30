#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimInstanceProxy.h"
#include "Animation/NoFearPoseTypes.h"
#include "NoFearPoseAnimInstance.generated.h"

/**
 * Прокси пост-процесс AnimBP: каждый кадр на игровом потоке забирает у UNoFearPoseComponent повороты костей,
 * а узел FAnimNode_NoFearPose читает их из анимационных потоков.
 */
USTRUCT()
struct NOFEAR_API FNoFearPoseAnimProxy : public FAnimInstanceProxy
{
	GENERATED_BODY()

	FNoFearPoseAnimProxy() {}
	explicit FNoFearPoseAnimProxy(UAnimInstance* InAnimInstance)
		: FAnimInstanceProxy(InAnimInstance)
	{
	}

	virtual void PreUpdate(UAnimInstance* InAnimInstance, float DeltaSeconds) override;

	/** Повороты костей на текущий кадр (пространство меша). */
	FNoFearPoseFrame Frame;
};

/**
 * Родитель ABP_NoFearPose — пост-процесс AnimBP поверх GASP. Сам по себе ничего не считает:
 * в графе ABP_NoFearPose стоит узел «NoFear Pose», который накладывает на входную позу повороты из прокси.
 * Ассет создаётся командой редактора NoFear.GeneratePoseAnimBP (см. Docs/AI_Setup.md).
 */
UCLASS(Transient)
class NOFEAR_API UNoFearPoseAnimInstance : public UAnimInstance
{
	GENERATED_BODY()

protected:
	virtual FAnimInstanceProxy* CreateAnimInstanceProxy() override;
	virtual void DestroyAnimInstanceProxy(FAnimInstanceProxy* InProxy) override;
};
