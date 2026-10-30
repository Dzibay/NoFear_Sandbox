#include "Animation/AnimNode_NoFearPose.h"

#include "Animation/AnimInstanceProxy.h"
#include "Animation/NoFearPoseAnimInstance.h"
#include "Animation/NoFearPoseComponent.h"
#include "BoneContainer.h"
#include "BonePose.h"

void FAnimNode_NoFearPose::Initialize_AnyThread(const FAnimationInitializeContext& Context)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_ANIMNODE(Initialize_AnyThread)
	FAnimNode_Base::Initialize_AnyThread(Context);

	Source.Initialize(Context);

	const UObject* AnimObject = Context.AnimInstanceProxy ? Context.AnimInstanceProxy->GetAnimInstanceObject() : nullptr;
	bValidProxy = AnimObject && AnimObject->IsA<UNoFearPoseAnimInstance>();
}

void FAnimNode_NoFearPose::CacheBones_AnyThread(const FAnimationCacheBonesContext& Context)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_ANIMNODE(CacheBones_AnyThread)
	Source.CacheBones(Context);

	Bones.Reset();
	const FBoneContainer& BoneContainer = Context.AnimInstanceProxy->GetRequiredBones();
	for (int32 BoneIndex = 0; BoneIndex < static_cast<int32>(ENoFearPoseBone::Count); ++BoneIndex)
	{
		const ENoFearPoseBone Bone = static_cast<ENoFearPoseBone>(BoneIndex);
		const int32 MeshIndex = BoneContainer.GetPoseBoneIndexForBoneName(UNoFearPoseComponent::GetBoneName(Bone));
		if (MeshIndex == INDEX_NONE)
		{
			continue;
		}

		const FCompactPoseBoneIndex CompactIndex = BoneContainer.MakeCompactPoseIndex(FMeshPoseBoneIndex(MeshIndex));
		if (CompactIndex.GetInt() != INDEX_NONE)
		{
			Bones.Add({Bone, CompactIndex});
		}
	}

	// Родитель всегда раньше потомка: поворот ребёнка считается от уже повёрнутой цепочки.
	Bones.Sort([](const FCachedBone& A, const FCachedBone& B) { return A.Index.GetInt() < B.Index.GetInt(); });
}

void FAnimNode_NoFearPose::Update_AnyThread(const FAnimationUpdateContext& Context)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_ANIMNODE(Update_AnyThread)
	Source.Update(Context);
}

void FAnimNode_NoFearPose::Evaluate_AnyThread(FPoseContext& Output)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_ANIMNODE(Evaluate_AnyThread)
	Source.Evaluate(Output);

	if (!bValidProxy || Bones.IsEmpty())
	{
		return;
	}

	const FNoFearPoseFrame& Frame = static_cast<const FNoFearPoseAnimProxy*>(Output.AnimInstanceProxy)->Frame;
	if (Frame.IsEmpty())
	{
		return;
	}

	FCompactPose& Pose = Output.Pose;
	const FBoneContainer& BoneContainer = Pose.GetBoneContainer();
	for (const FCachedBone& Cached : Bones)
	{
		if (!Frame.IsBoneActive(Cached.Bone))
		{
			continue;
		}

		// Поворот задан в пространстве меша вокруг самой кости. Переводим в локальный: P^-1 * R * P * Local,
		// где P — уже повёрнутый поворот родителя в пространстве меша.
		FQuat ParentRotation = FQuat::Identity;
		for (FCompactPoseBoneIndex Parent = BoneContainer.GetParentBoneIndex(Cached.Index);
			Parent.GetInt() != INDEX_NONE;
			Parent = BoneContainer.GetParentBoneIndex(Parent))
		{
			ParentRotation = Pose[Parent].GetRotation() * ParentRotation;
		}

		FTransform& Local = Pose[Cached.Index];
		const FQuat& Rotation = Frame.Rotations[static_cast<int32>(Cached.Bone)];
		Local.SetRotation((ParentRotation.Inverse() * Rotation * ParentRotation * Local.GetRotation()).GetNormalized());
	}
}

void FAnimNode_NoFearPose::GatherDebugData(FNodeDebugData& DebugData)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_ANIMNODE(GatherDebugData)
	DebugData.AddDebugItem(DebugData.GetNodeName(this));
	Source.GatherDebugData(DebugData);
}
