#pragma once

#include "CoreMinimal.h"
#include "AnimGraphNode_Base.h"
#include "Animation/AnimNode_NoFearPose.h"
#include "AnimGraphNode_NoFearPose.generated.h"

/** Узел «NoFear Pose» в графе пост-процесс AnimBP: накладывает позы UNoFearPoseComponent на входную позу. */
UCLASS()
class NOFEAREDITOR_API UAnimGraphNode_NoFearPose : public UAnimGraphNode_Base
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, Category = Settings)
	FAnimNode_NoFearPose Node;

	// UEdGraphNode interface
	virtual FText GetTooltipText() const override;
	virtual FText GetNodeTitle(ENodeTitleType::Type TitleType) const override;
	virtual FLinearColor GetNodeTitleColor() const override;

	// UAnimGraphNode_Base interface
	virtual FString GetNodeCategory() const override;
	virtual void ValidateAnimNodeDuringCompilation(USkeleton* ForSkeleton, FCompilerResultsLog& MessageLog) override;
};
