#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimNodeBase.h"
#include "Animation/NoFearPoseTypes.h"
#include "AnimNode_NoFearPose.generated.h"

/**
 * Узел пост-процесс AnimBP: берёт входную позу (всё, что насчитал основной ABP_SandboxCharacter) и поворачивает
 * кости так, как велит UNoFearPoseComponent. Работает только в AnimBP с родителем UNoFearPoseAnimInstance.
 */
USTRUCT(BlueprintInternalUseOnly)
struct NOFEAR_API FAnimNode_NoFearPose : public FAnimNode_Base
{
	GENERATED_BODY()

	/** Входная поза. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Links)
	FPoseLink Source;

	// FAnimNode_Base interface
	virtual void Initialize_AnyThread(const FAnimationInitializeContext& Context) override;
	virtual void CacheBones_AnyThread(const FAnimationCacheBonesContext& Context) override;
	virtual void Update_AnyThread(const FAnimationUpdateContext& Context) override;
	virtual void Evaluate_AnyThread(FPoseContext& Output) override;
	virtual void GatherDebugData(FNodeDebugData& DebugData) override;

private:
	struct FCachedBone
	{
		ENoFearPoseBone Bone = ENoFearPoseBone::Pelvis;
		FCompactPoseBoneIndex Index = FCompactPoseBoneIndex(INDEX_NONE);
	};

	/** Кости, найденные в текущем скелете, от корня к листьям (родитель всегда раньше потомка). */
	TArray<FCachedBone, TInlineAllocator<static_cast<int32>(ENoFearPoseBone::Count)>> Bones;

	/** Анимация идёт под UNoFearPoseAnimInstance: прокси можно читать. */
	bool bValidProxy = false;
};
