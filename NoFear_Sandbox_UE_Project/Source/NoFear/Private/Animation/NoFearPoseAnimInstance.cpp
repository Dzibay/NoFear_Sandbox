#include "Animation/NoFearPoseAnimInstance.h"

#include "Animation/NoFearPoseComponent.h"

void FNoFearPoseAnimProxy::PreUpdate(UAnimInstance* InAnimInstance, float DeltaSeconds)
{
	FAnimInstanceProxy::PreUpdate(InAnimInstance, DeltaSeconds);

	const AActor* Owner = InAnimInstance ? InAnimInstance->GetOwningActor() : nullptr;
	const UNoFearPoseComponent* Poses = Owner ? Owner->FindComponentByClass<UNoFearPoseComponent>() : nullptr;
	Frame = Poses ? Poses->BuildFrame() : FNoFearPoseFrame();
}

FAnimInstanceProxy* UNoFearPoseAnimInstance::CreateAnimInstanceProxy()
{
	return new FNoFearPoseAnimProxy(this);
}

void UNoFearPoseAnimInstance::DestroyAnimInstanceProxy(FAnimInstanceProxy* InProxy)
{
	delete static_cast<FNoFearPoseAnimProxy*>(InProxy);
}
