#include "Animation/NoFearPoseComponent.h"

#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/Controller.h"
#include "Weapons/NoFearWeaponComponent.h"

namespace
{
constexpr int32 PoseCount = static_cast<int32>(ENoFearPose::Count);

FNoFearPoseBoneEntry MakeEntry(ENoFearPoseBone Bone, float Forward, float Side = 0.f, float Twist = 0.f)
{
	FNoFearPoseBoneEntry Entry;
	Entry.Bone = Bone;
	Entry.Angles.Forward = Forward;
	Entry.Angles.Side = Side;
	Entry.Angles.Twist = Twist;
	return Entry;
}

float SmoothWeight(float Weight)
{
	return FMath::SmoothStep(0.f, 1.f, Weight);
}
}

// ---------------------------------------------------------------------------
// Углы
// ---------------------------------------------------------------------------

FQuat FNoFearBoneAngles::ToQuat(float Scale) const
{
	// Оси персонажа: X вперёд, Y вправо, Z вверх. Плюс по Forward: вершина кости идёт вперёд (поворот вокруг +Y),
	// плюс по Side: вершина идёт вправо (вокруг -X), плюс по Twist: нос вправо (вокруг +Z).
	const FQuat Pitch(FVector::YAxisVector, FMath::DegreesToRadians(Forward * Scale));
	const FQuat Roll(FVector::XAxisVector, FMath::DegreesToRadians(-Side * Scale));
	const FQuat Yaw(FVector::ZAxisVector, FMath::DegreesToRadians(Twist * Scale));
	return Yaw * Pitch * Roll;
}

// ---------------------------------------------------------------------------
// Компонент
// ---------------------------------------------------------------------------

UNoFearPoseComponent::UNoFearPoseComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PrePhysics;

	FillDefaultPoses();
}

void UNoFearPoseComponent::FillDefaultPoses()
{
	using B = ENoFearPoseBone;

	// Вздрагивание: корпус и голова отдёргиваются от выстрела (направление — по пуле, bDirectional).
	FNoFearPoseSettings FlinchPose;
	FlinchPose.bDirectional = true;
	FlinchPose.BlendInTime = 0.06f;
	FlinchPose.BlendOutTime = 0.3f;
	FlinchPose.Bones = {
		MakeEntry(B::Pelvis, 3.f),
		MakeEntry(B::Spine02, 5.f),
		MakeEntry(B::Spine03, 6.f),
		MakeEntry(B::Spine04, 5.f),
		MakeEntry(B::Spine05, 3.f),
		MakeEntry(B::Neck01, 4.f),
		MakeEntry(B::Head, 7.f),
	};
	Poses.Add(ENoFearPose::Flinch, FlinchPose);

	// Пошатывание: сильно откинут назад (от выстрела), колени подогнуты, голова мотнулась.
	FNoFearPoseSettings StaggerPose;
	StaggerPose.bDirectional = true;
	StaggerPose.BlendInTime = 0.12f;
	StaggerPose.BlendOutTime = 0.4f;
	StaggerPose.Bones = {
		MakeEntry(B::Pelvis, -4.f),
		MakeEntry(B::Spine01, 5.f),
		MakeEntry(B::Spine02, 7.f),
		MakeEntry(B::Spine03, 8.f),
		MakeEntry(B::Spine04, 6.f),
		MakeEntry(B::Spine05, 4.f),
		MakeEntry(B::Neck01, 6.f),
		MakeEntry(B::Head, 8.f),
		MakeEntry(B::ThighL, -14.f),
		MakeEntry(B::ThighR, -10.f),
		MakeEntry(B::CalfL, 20.f),
		MakeEntry(B::CalfR, 15.f),
	};
	Poses.Add(ENoFearPose::Stagger, StaggerPose);

	// Ранен: сутулая спина, опущенная голова, чуть завален влево. Хромота добавляется отдельно (BuildFrame).
	FNoFearPoseSettings WoundedPose;
	WoundedPose.BlendInTime = 0.4f;
	WoundedPose.BlendOutTime = 0.6f;
	WoundedPose.Bones = {
		MakeEntry(B::Pelvis, -2.f),
		MakeEntry(B::Spine01, 3.f),
		MakeEntry(B::Spine02, 4.f, -2.f),
		MakeEntry(B::Spine03, 5.f, -3.f),
		MakeEntry(B::Spine04, 3.f, -2.f),
		MakeEntry(B::Neck01, 4.f),
		MakeEntry(B::Head, 5.f),
	};
	Poses.Add(ENoFearPose::Wounded, WoundedPose);

	// Закрыть голову руками: спина согнута, голова вниз, локти вперёд-вверх, кисти на затылке.
	// Углы рук — в осях персонажа и наследуются от родителя: предплечье добавляет к плечу.
	// Скрутка плеча сводит кисти к голове (подобрано замером кистей относительно кости head).
	FNoFearPoseSettings CoverHeadPose;
	CoverHeadPose.BlendInTime = 0.35f;
	CoverHeadPose.BlendOutTime = 0.5f;
	CoverHeadPose.Bones = {
		MakeEntry(B::Spine02, 10.f),
		MakeEntry(B::Spine03, 10.f),
		MakeEntry(B::Spine04, 8.f),
		MakeEntry(B::Neck01, 10.f),
		MakeEntry(B::Head, 20.f),
		MakeEntry(B::UpperArmL, -140.f, 0.f, 40.f),
		MakeEntry(B::UpperArmR, -140.f, 0.f, -40.f),
		MakeEntry(B::LowerArmL, -110.f),
		MakeEntry(B::LowerArmR, -110.f),
	};
	Poses.Add(ENoFearPose::CoverHead, CoverHeadPose);

	// Замах: плечи развёрнуты вправо, правая рука отведена в сторону и вверх, локоть согнут — удар идёт на выходе из позы.
	FNoFearPoseSettings SwingPose;
	SwingPose.BlendInTime = 0.15f;
	SwingPose.BlendOutTime = 0.2f;
	SwingPose.Bones = {
		MakeEntry(B::Spine02, 6.f, 0.f, 10.f),
		MakeEntry(B::Spine03, 6.f, 0.f, 12.f),
		MakeEntry(B::Spine04, 4.f, 0.f, 8.f),
		MakeEntry(B::Head, -6.f, 0.f, -18.f),
		MakeEntry(B::UpperArmR, -50.f, -80.f),
		MakeEntry(B::LowerArmR, -70.f),
		MakeEntry(B::UpperArmL, -30.f, 20.f),
		MakeEntry(B::LowerArmL, -40.f),
	};
	Poses.Add(ENoFearPose::Swing, SwingPose);

	// Рывок: низко пригнулся, обе руки вперёд когтями, колени согнуты, голова поднята к цели.
	FNoFearPoseSettings LungePose;
	LungePose.BlendInTime = 0.15f;
	LungePose.BlendOutTime = 0.3f;
	LungePose.Bones = {
		MakeEntry(B::Pelvis, 8.f),
		MakeEntry(B::Spine01, 8.f),
		MakeEntry(B::Spine02, 10.f),
		MakeEntry(B::Spine03, 10.f),
		MakeEntry(B::Spine04, 6.f),
		MakeEntry(B::Neck01, -8.f),
		MakeEntry(B::Head, -18.f),
		MakeEntry(B::UpperArmL, -75.f, 15.f),
		MakeEntry(B::UpperArmR, -75.f, -15.f),
		MakeEntry(B::LowerArmL, -25.f),
		MakeEntry(B::LowerArmR, -25.f),
		MakeEntry(B::ThighL, -25.f),
		MakeEntry(B::ThighR, -25.f),
		MakeEntry(B::CalfL, 40.f),
		MakeEntry(B::CalfR, 40.f),
	};
	Poses.Add(ENoFearPose::Lunge, LungePose);

	// Рык: грудь вперёд, голова запрокинута, руки разведены в стороны и согнуты.
	FNoFearPoseSettings RoarPose;
	RoarPose.BlendInTime = 0.2f;
	RoarPose.BlendOutTime = 0.4f;
	RoarPose.Bones = {
		MakeEntry(B::Spine02, -4.f),
		MakeEntry(B::Spine03, -8.f),
		MakeEntry(B::Spine04, -8.f),
		MakeEntry(B::Neck01, -10.f),
		MakeEntry(B::Head, -25.f),
		MakeEntry(B::UpperArmL, -20.f, 55.f),
		MakeEntry(B::UpperArmR, -20.f, -55.f),
		MakeEntry(B::LowerArmL, -50.f),
		MakeEntry(B::LowerArmR, -50.f),
	};
	Poses.Add(ENoFearPose::Roar, RoarPose);

	// Плевок: грудь подана назад, шея и голова вытянуты вперёд к цели, руки прижаты — выход из позы и есть плевок.
	FNoFearPoseSettings SpitPose;
	SpitPose.BlendInTime = 0.2f;
	SpitPose.BlendOutTime = 0.15f;
	SpitPose.Bones = {
		MakeEntry(B::Spine02, -6.f),
		MakeEntry(B::Spine03, -10.f),
		MakeEntry(B::Spine04, -6.f),
		MakeEntry(B::Neck01, 18.f),
		MakeEntry(B::Neck02, 12.f),
		MakeEntry(B::Head, -20.f),
		MakeEntry(B::UpperArmL, -15.f, 25.f),
		MakeEntry(B::UpperArmR, -15.f, -25.f),
		MakeEntry(B::LowerArmL, -60.f),
		MakeEntry(B::LowerArmR, -60.f),
	};
	Poses.Add(ENoFearPose::Spit, SpitPose);

	// Бросок гранаты: держится, пока игрок целится; выход из позы — сам бросок (рука идёт вперёд).
	FNoFearPoseSettings ThrowPose;
	ThrowPose.BlendInTime = 0.25f;
	ThrowPose.BlendOutTime = 0.12f;
	ThrowPose.Bones = {
		MakeEntry(B::Spine02, -4.f, 0.f, 10.f),
		MakeEntry(B::Spine03, -6.f, 0.f, 14.f),
		MakeEntry(B::Spine04, -4.f, 0.f, 8.f),
		MakeEntry(B::Head, 4.f, 0.f, -24.f),
		MakeEntry(B::UpperArmR, -40.f, -95.f, 20.f),
		MakeEntry(B::LowerArmR, -95.f),
		MakeEntry(B::UpperArmL, -70.f, 10.f),
		MakeEntry(B::LowerArmL, -15.f),
	};
	Poses.Add(ENoFearPose::Throw, ThrowPose);
}

void UNoFearPoseComponent::BeginPlay()
{
	Super::BeginPlay();

	// Позы считаются до того, как меш начнёт считать анимацию в этом кадре.
	if (const ACharacter* Character = Cast<ACharacter>(GetOwner()); Character && Character->GetMesh())
	{
		Character->GetMesh()->PrimaryComponentTick.AddPrerequisite(this, PrimaryComponentTick);
	}

	if (UNoFearHealthComponent* Health = UNoFearHealthComponent::FindHealth(GetOwner()))
	{
		BoundHealth = Health;
		Health->OnDamaged.AddDynamic(this, &UNoFearPoseComponent::HandleDamaged);
		Health->OnHealthChanged.AddDynamic(this, &UNoFearPoseComponent::HandleHealthChanged);
		Health->OnDeath.AddDynamic(this, &UNoFearPoseComponent::HandleDeath);
		bDead = Health->IsDead();
	}
	else
	{
		SetComponentTickEnabled(false);
	}
}

void UNoFearPoseComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UNoFearHealthComponent* Health = BoundHealth.Get())
	{
		Health->OnDamaged.RemoveDynamic(this, &UNoFearPoseComponent::HandleDamaged);
		Health->OnHealthChanged.RemoveDynamic(this, &UNoFearPoseComponent::HandleHealthChanged);
		Health->OnDeath.RemoveDynamic(this, &UNoFearPoseComponent::HandleDeath);
	}
	BoundHealth.Reset();

	Super::EndPlay(EndPlayReason);
}

USkeletalMeshComponent* UNoFearPoseComponent::GetMesh() const
{
	const ACharacter* Character = Cast<ACharacter>(GetOwner());
	return Character ? Character->GetMesh() : nullptr;
}

double UNoFearPoseComponent::GetNow() const
{
	const UWorld* World = GetWorld();
	return World ? World->GetTimeSeconds() : 0.0;
}

// ---------------------------------------------------------------------------
// Позы
// ---------------------------------------------------------------------------

void UNoFearPoseComponent::StartPose(ENoFearPose Pose, float Duration, bool bHeld, float Intensity, float DirectionYaw)
{
	const int32 Index = static_cast<int32>(Pose);
	if (Index < 0 || Index >= PoseCount || bDead)
	{
		return;
	}

	FPoseState& State = States[Index];
	State.Intensity = FMath::Clamp(Intensity, 0.f, 1.f);
	State.DirectionYaw = DirectionYaw;
	State.bHeld = bHeld;
	State.HoldUntil = GetNow() + FMath::Max(Duration, 0.f);

	// Готовая анимация вместо поворотов костей: играем монтаж на основном AnimBP.
	const FNoFearPoseSettings* Settings = Poses.Find(Pose);
	UAnimMontage* Montage = Settings ? Settings->ReplacementMontage.Get() : nullptr;
	State.bMontage = Montage != nullptr;
	if (Montage)
	{
		USkeletalMeshComponent* Mesh = GetMesh();
		UAnimInstance* AnimInstance = Mesh ? Mesh->GetAnimInstance() : nullptr;
		if (AnimInstance && !AnimInstance->Montage_IsPlaying(Montage))
		{
			AnimInstance->Montage_Play(Montage);
		}
	}
}

void UNoFearPoseComponent::PlayPose(ENoFearPose Pose, float Duration, float Intensity, float DirectionYaw)
{
	StartPose(Pose, Duration, false, Intensity, DirectionYaw);
}

void UNoFearPoseComponent::SetPoseHeld(ENoFearPose Pose, bool bHeld, float Intensity)
{
	const int32 Index = static_cast<int32>(Pose);
	if (Index < 0 || Index >= PoseCount)
	{
		return;
	}

	if (bHeld)
	{
		StartPose(Pose, 0.f, true, Intensity, States[Index].DirectionYaw);
		return;
	}

	FPoseState& State = States[Index];
	State.bHeld = false;
	State.HoldUntil = 0.0;
	if (State.bMontage)
	{
		const FNoFearPoseSettings* Settings = Poses.Find(Pose);
		USkeletalMeshComponent* Mesh = GetMesh();
		UAnimInstance* AnimInstance = Mesh ? Mesh->GetAnimInstance() : nullptr;
		if (Settings && Settings->ReplacementMontage && AnimInstance && AnimInstance->Montage_IsPlaying(Settings->ReplacementMontage))
		{
			AnimInstance->Montage_Stop(Settings->BlendOutTime, Settings->ReplacementMontage);
		}
	}
}

float UNoFearPoseComponent::GetPoseWeight(ENoFearPose Pose) const
{
	const int32 Index = static_cast<int32>(Pose);
	if (Index < 0 || Index >= PoseCount)
	{
		return 0.f;
	}
	return States[Index].Intensity * SmoothWeight(States[Index].Weight);
}

void UNoFearPoseComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	const double Now = GetNow();

	// Вес каждой позы плавно идёт к 1, пока поза включена, и к 0 после.
	for (int32 Index = 0; Index < PoseCount; ++Index)
	{
		FPoseState& State = States[Index];
		const FNoFearPoseSettings* Settings = Poses.Find(static_cast<ENoFearPose>(Index));
		const bool bActive = !bDead && (State.bHeld || Now < State.HoldUntil);
		const float BlendTime = Settings ? (bActive ? Settings->BlendInTime : Settings->BlendOutTime) : 0.f;
		const float Goal = bActive ? 1.f : 0.f;
		State.Weight = BlendTime > KINDA_SMALL_NUMBER
			? FMath::FInterpConstantTo(State.Weight, Goal, DeltaTime, 1.f / BlendTime)
			: Goal;
	}

	UpdateWounded();

	// Фаза хромоты идёт по пройденному пути, а не по времени: стоит — не качается.
	const AActor* Owner = GetOwner();
	const float Speed = Owner ? Owner->GetVelocity().Size2D() : 0.f;
	LimpSpeedAlpha = FMath::FInterpTo(LimpSpeedAlpha, FMath::Clamp(Speed / 300.f, 0.f, 1.f), DeltaTime, 6.f);
	LimpPhase = FMath::Fmod(LimpPhase + DeltaTime * PI * LimpStepRate * (Speed / 300.f), 2.f * PI);

	// Если безоружный раненый успел взять оружие, поза Injured уже заменена позой оружия.
	if (bInjuredOverlayApplied)
	{
		const ACharacter* Character = Cast<ACharacter>(GetOwner());
		const UNoFearWeaponComponent* Weapons = Character ? Character->FindComponentByClass<UNoFearWeaponComponent>() : nullptr;
		if (Weapons && Weapons->GetCurrentWeapon())
		{
			bInjuredOverlayApplied = false;
		}
	}
}

FNoFearPoseFrame UNoFearPoseComponent::BuildFrame() const
{
	FNoFearPoseFrame Frame;
	const USkeletalMeshComponent* Mesh = GetMesh();
	const AActor* Owner = GetOwner();
	if (!Mesh || !Owner || bDead)
	{
		return Frame;
	}

	constexpr int32 BoneCount = static_cast<int32>(ENoFearPoseBone::Count);
	FQuat ActorRotations[BoneCount];
	for (FQuat& Rotation : ActorRotations)
	{
		Rotation = FQuat::Identity;
	}
	uint32 Mask = 0;

	const auto AddRotation = [&](ENoFearPoseBone Bone, const FQuat& Rotation)
	{
		const int32 BoneIndex = static_cast<int32>(Bone);
		ActorRotations[BoneIndex] = Rotation * ActorRotations[BoneIndex];
		Mask |= 1u << BoneIndex;
	};

	for (int32 Index = 0; Index < PoseCount; ++Index)
	{
		const FPoseState& State = States[Index];
		const FNoFearPoseSettings* Settings = Poses.Find(static_cast<ENoFearPose>(Index));
		const float Scale = State.Intensity * SmoothWeight(State.Weight);
		if (!Settings || State.bMontage || Scale <= KINDA_SMALL_NUMBER)
		{
			continue;
		}

		float Sin = 0.f;
		float Cos = 1.f;
		if (Settings->bDirectional)
		{
			FMath::SinCos(&Sin, &Cos, FMath::DegreesToRadians(State.DirectionYaw));
		}

		for (const FNoFearPoseBoneEntry& Entry : Settings->Bones)
		{
			FNoFearBoneAngles Angles = Entry.Angles;
			if (Settings->bDirectional)
			{
				// «Вперёд» позы — это направление толчка: поворачиваем пару наклонов на угол толчка.
				Angles.Forward = Entry.Angles.Forward * Cos - Entry.Angles.Side * Sin;
				Angles.Side = Entry.Angles.Forward * Sin + Entry.Angles.Side * Cos;
			}
			AddRotation(Entry.Bone, Angles.ToQuat(Scale));
		}

		// Хромота: таз качается, больная (правая) нога подгибается на каждом шаге.
		if (Index == static_cast<int32>(ENoFearPose::Wounded) && LimpSpeedAlpha > 0.01f)
		{
			const float Alpha = Scale * LimpSpeedAlpha;
			const float Wave = FMath::Sin(LimpPhase);
			FNoFearBoneAngles Pelvis;
			Pelvis.Side = Wave * LimpPelvisRoll * Alpha;
			AddRotation(ENoFearPoseBone::Pelvis, Pelvis.ToQuat());

			FNoFearBoneAngles Knee;
			Knee.Forward = FMath::Max(0.f, Wave) * LimpKneeBend * Alpha;
			AddRotation(ENoFearPoseBone::CalfR, Knee.ToQuat());
		}
	}

	if (Mask == 0)
	{
		return Frame;
	}

	// Из осей персонажа в пространство меша (у манекена меш повёрнут на -90°).
	const FQuat ActorToMesh = Mesh->GetComponentQuat().Inverse() * Owner->GetActorQuat();
	const FQuat MeshToActor = ActorToMesh.Inverse();
	for (int32 BoneIndex = 0; BoneIndex < BoneCount; ++BoneIndex)
	{
		if (Mask & (1u << BoneIndex))
		{
			Frame.Rotations[BoneIndex] = (ActorToMesh * ActorRotations[BoneIndex] * MeshToActor).GetNormalized();
		}
	}
	Frame.ActiveMask = Mask;
	return Frame;
}

FName UNoFearPoseComponent::GetBoneName(ENoFearPoseBone Bone)
{
	static const FName Names[] = {
		TEXT("pelvis"),
		TEXT("spine_01"), TEXT("spine_02"), TEXT("spine_03"), TEXT("spine_04"), TEXT("spine_05"),
		TEXT("neck_01"), TEXT("neck_02"), TEXT("head"),
		TEXT("clavicle_l"), TEXT("clavicle_r"),
		TEXT("upperarm_l"), TEXT("upperarm_r"),
		TEXT("lowerarm_l"), TEXT("lowerarm_r"),
		TEXT("hand_l"), TEXT("hand_r"),
		TEXT("thigh_l"), TEXT("thigh_r"),
		TEXT("calf_l"), TEXT("calf_r"),
		TEXT("foot_l"), TEXT("foot_r"),
	};
	static_assert(UE_ARRAY_COUNT(Names) == static_cast<int32>(ENoFearPoseBone::Count), "Имена костей должны совпадать с ENoFearPoseBone");

	const int32 Index = static_cast<int32>(Bone);
	return Index >= 0 && Index < UE_ARRAY_COUNT(Names) ? Names[Index] : NAME_None;
}

FString UNoFearPoseComponent::DescribeState() const
{
	FString Text;
	const UEnum* Enum = StaticEnum<ENoFearPose>();
	for (int32 Index = 0; Index < PoseCount; ++Index)
	{
		const float Weight = GetPoseWeight(static_cast<ENoFearPose>(Index));
		if (Weight > 0.02f)
		{
			Text += FString::Printf(TEXT("%s%s %.1f"), Text.IsEmpty() ? TEXT("") : TEXT(", "),
				*Enum->GetNameStringByIndex(Index), Weight);
		}
	}
	if (IsStaggered())
	{
		Text = TEXT("СТАН ") + Text;
	}
	return Text;
}

// ---------------------------------------------------------------------------
// Реакции на урон
// ---------------------------------------------------------------------------

bool UNoFearPoseComponent::CanReact() const
{
	if (!bHitReactions || bDead)
	{
		return false;
	}
	const APawn* Pawn = Cast<APawn>(GetOwner());
	return bReactWhenPlayerControlled || !Pawn || !Pawn->IsPlayerControlled();
}

float UNoFearPoseComponent::ComputePushYaw(const FNoFearDamageInfo& Info) const
{
	const AActor* Owner = GetOwner();
	if (!Owner)
	{
		return 180.f;
	}

	// Корпус толкает по полёту пули; если направления нет — от стрелка к цели.
	FVector Push = Info.Direction;
	if (Push.IsNearlyZero())
	{
		if (Info.bHasSource)
		{
			Push = Owner->GetActorLocation() - Info.SourceLocation;
		}
		else if (Info.DamageCauser)
		{
			Push = Owner->GetActorLocation() - Info.DamageCauser->GetActorLocation();
		}
	}
	Push.Z = 0.f;
	if (Push.IsNearlyZero())
	{
		return 180.f;
	}

	const FVector Local = Owner->GetActorQuat().UnrotateVector(Push.GetSafeNormal());
	return FMath::RadiansToDegrees(FMath::Atan2(Local.Y, Local.X));
}

void UNoFearPoseComponent::ApplyHitReaction(const FNoFearDamageInfo& Info)
{
	if (!CanReact() || Info.Amount < FlinchMinDamage)
	{
		return;
	}

	const double Now = GetNow();
	const float Yaw = ComputePushYaw(Info);
	const float Intensity = FMath::Clamp(Info.Amount / FMath::Max(FlinchFullDamage, 1.f), 0.35f, 1.f);
	PlayPose(ENoFearPose::Flinch, FlinchHoldTime, Intensity, Yaw);

	if (HitSlowTime > 0.f)
	{
		SlowUntil = FMath::Max(SlowUntil, Now + HitSlowTime);
	}

	const UNoFearHealthComponent* Health = BoundHealth.Get();
	const float MaxHealth = Health ? Health->GetMaxHealth() : 0.f;
	const bool bHeavy = StaggerDamageFraction > 0.f && MaxHealth > 0.f && Info.Amount >= StaggerDamageFraction * MaxHealth;
	// Удар, что свалил раненым, не шатает: он уже лежит.
	const bool bWantsStagger = !Info.bFatal && !Info.bDowned && (bHeavy || (bStaggerOnHeadshot && Info.bHeadshot));

	bool bStaggered = false;
	if (bWantsStagger && Now >= NextStaggerTime)
	{
		FVector Direction = Info.Direction;
		if (Direction.IsNearlyZero() && Info.bHasSource)
		{
			Direction = GetOwner()->GetActorLocation() - Info.SourceLocation;
		}
		Stagger(Direction, StaggerTime);
		bStaggered = true;
	}

	OnHitReaction.Broadcast(this, bStaggered);
}

void UNoFearPoseComponent::Stagger(FVector Direction, float Duration)
{
	const AActor* Owner = GetOwner();
	if (!Owner || bDead)
	{
		return;
	}

	const float Time = Duration >= 0.f ? Duration : StaggerTime;
	const double Now = GetNow();
	StaggerUntil = Now + Time;
	NextStaggerTime = StaggerUntil + StaggerCooldown;

	Direction.Z = 0.f;
	float Yaw = 180.f;
	if (!Direction.IsNearlyZero())
	{
		const FVector Local = Owner->GetActorQuat().UnrotateVector(Direction.GetSafeNormal());
		Yaw = FMath::RadiansToDegrees(FMath::Atan2(Local.Y, Local.X));
	}
	PlayPose(ENoFearPose::Stagger, Time, 1.f, Yaw);
}

bool UNoFearPoseComponent::IsStaggered() const
{
	return !bDead && GetNow() < StaggerUntil;
}

bool UNoFearPoseComponent::IsSlowed() const
{
	return !bDead && (GetNow() < SlowUntil || bWounded);
}

bool UNoFearPoseComponent::IsActorStaggered(const AActor* Actor)
{
	if (const AController* Controller = Cast<AController>(Actor))
	{
		Actor = Controller->GetPawn();
	}
	const UNoFearPoseComponent* Component = Actor ? Actor->FindComponentByClass<UNoFearPoseComponent>() : nullptr;
	return Component && Component->IsStaggered();
}

void UNoFearPoseComponent::HandleDamaged(UNoFearHealthComponent* Component, const FNoFearDamageInfo& Info)
{
	ApplyHitReaction(Info);
}

void UNoFearPoseComponent::HandleHealthChanged(UNoFearHealthComponent* Component, float NewHealth, float Delta)
{
	UpdateWounded();
}

void UNoFearPoseComponent::HandleDeath(UNoFearHealthComponent* Component, const FNoFearDamageInfo& Info)
{
	bDead = true;
	StaggerUntil = -1.0e9;
	SlowUntil = -1.0e9;
	for (FPoseState& State : States)
	{
		State = FPoseState();
	}
	bWounded = false;
	bInjuredOverlayApplied = false;
}

// ---------------------------------------------------------------------------
// Раненый
// ---------------------------------------------------------------------------

void UNoFearPoseComponent::UpdateWounded()
{
	const UNoFearHealthComponent* Health = BoundHealth.Get();
	if (!Health || bDead)
	{
		return;
	}

	// Управляемый игроком (если реакции для него выключены) не сутулится, при возврате в бота поза включится снова.
	bool bShouldBeWounded = false;
	if (bWoundedStance && CanReact())
	{
		const float Fraction = Health->GetHealthFraction();
		bShouldBeWounded = bWounded
			? Fraction < WoundedHealthFraction + WoundedRecoverMargin
			: Fraction < WoundedHealthFraction;
	}

	if (bShouldBeWounded != bWounded)
	{
		SetWounded(bShouldBeWounded);
	}
}

void UNoFearPoseComponent::SetWounded(bool bNewWounded)
{
	bWounded = bNewWounded;
	SetPoseHeld(ENoFearPose::Wounded, bNewWounded);
	ApplyInjuredOverlay(bNewWounded);
}

void UNoFearPoseComponent::ApplyInjuredOverlay(bool bInjured)
{
	if (!bUseInjuredOverlayWhenUnarmed)
	{
		return;
	}

	const ACharacter* Character = Cast<ACharacter>(GetOwner());
	const UNoFearWeaponComponent* Weapons = Character ? Character->FindComponentByClass<UNoFearWeaponComponent>() : nullptr;
	if (!Weapons)
	{
		return;
	}

	if (bInjured)
	{
		// Вооружённому поза Injured сломала бы стойку с оружием: ему достаточно процедурной сутулости.
		if (!Weapons->GetCurrentWeapon())
		{
			Weapons->SetOverlayState(WoundedOverlayState);
			bInjuredOverlayApplied = true;
		}
	}
	else if (bInjuredOverlayApplied)
	{
		bInjuredOverlayApplied = false;
		Weapons->RefreshOverlayState();
	}
}
