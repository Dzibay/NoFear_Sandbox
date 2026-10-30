#include "AnimGraphNode_NoFearPose.h"

#include "Animation/AnimBlueprint.h"
#include "Animation/NoFearPoseAnimInstance.h"
#include "Kismet2/CompilerResultsLog.h"

#define LOCTEXT_NAMESPACE "NoFearPoseNode"

FText UAnimGraphNode_NoFearPose::GetTooltipText() const
{
	return LOCTEXT("NoFearPoseTooltip", "Накладывает позы персонажа NoFear (UNoFearPoseComponent) на входную позу");
}

FText UAnimGraphNode_NoFearPose::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
	return LOCTEXT("NoFearPoseTitle", "NoFear Pose");
}

FLinearColor UAnimGraphNode_NoFearPose::GetNodeTitleColor() const
{
	return FLinearColor(0.85f, 0.4f, 0.25f);
}

FString UAnimGraphNode_NoFearPose::GetNodeCategory() const
{
	return TEXT("NoFear");
}

void UAnimGraphNode_NoFearPose::ValidateAnimNodeDuringCompilation(USkeleton* ForSkeleton, FCompilerResultsLog& MessageLog)
{
	Super::ValidateAnimNodeDuringCompilation(ForSkeleton, MessageLog);

	const UAnimBlueprint* AnimBlueprint = GetAnimBlueprint();
	if (AnimBlueprint && AnimBlueprint->ParentClass && !AnimBlueprint->ParentClass->IsChildOf(UNoFearPoseAnimInstance::StaticClass()))
	{
		MessageLog.Error(*LOCTEXT("NoFearPoseWrongParent", "@@ работает только в AnimBP с родителем NoFearPoseAnimInstance").ToString(), this);
	}
}

#undef LOCTEXT_NAMESPACE
