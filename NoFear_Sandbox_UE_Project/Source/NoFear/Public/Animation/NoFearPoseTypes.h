#pragma once

#include "CoreMinimal.h"
#include "NoFearPoseTypes.generated.h"

class UAnimMontage;

/**
 * Процедурные позы поверх анимации GASP. Каждая — набор поворотов костей, который включается плавно (вес 0..1).
 * Новые позы (укрытие, рык, замах...) добавляются сюда и в таблицу UNoFearPoseComponent::Poses.
 */
UENUM(BlueprintType)
enum class ENoFearPose : uint8
{
	/** Вздрагивание от попадания: корпус отклоняется по направлению выстрела и возвращается за долю секунды. */
	Flinch,
	/** Пошатывание от сильного удара или выстрела в голову: корпус откинут, колени подогнуты. */
	Stagger,
	/** Ранен (здоровья меньше порога): сутулится, голова опущена, прихрамывает. */
	Wounded,

	Count UMETA(Hidden)
};

/** Кости, которыми управляют позы. Имена костей — в UNoFearPoseComponent::GetBoneName (скелет манекена UEFN). */
UENUM(BlueprintType)
enum class ENoFearPoseBone : uint8
{
	Pelvis,
	Spine01,
	Spine02,
	Spine03,
	Spine04,
	Spine05,
	Neck01,
	Neck02,
	Head,
	ClavicleL,
	ClavicleR,
	UpperArmL,
	UpperArmR,
	LowerArmL,
	LowerArmR,
	HandL,
	HandR,
	ThighL,
	ThighR,
	CalfL,
	CalfR,
	FootL,
	FootR,

	Count UMETA(Hidden)
};

/**
 * Поворот кости в осях персонажа (X вперёд, Y вправо, Z вверх), градусы. Не зависит от осей самой кости,
 * поэтому поза читается одинаково для любой кости. Ось поворота — через сторону, в которую «качнётся» часть тела:
 * для костей, торчащих вверх (позвоночник, шея, голова), плюс = вершина идёт вперёд / вправо;
 * для висящих вниз (руки, ноги) то же правило даёт обратное — свободный конец уходит назад / влево.
 */
USTRUCT(BlueprintType)
struct NOFEAR_API FNoFearBoneAngles
{
	GENERATED_BODY()

	/** Наклон вперёд-назад: плюс — часть, торчащая вверх, идёт вперёд. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pose")
	float Forward = 0.f;

	/** Наклон вбок: плюс — часть, торчащая вверх, идёт вправо. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pose")
	float Side = 0.f;

	/** Поворот вокруг вертикали: плюс — вправо (по часовой, если смотреть сверху). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pose")
	float Twist = 0.f;

	/** Поворот в кватернион в осях персонажа. Scale — доля от полного угла (вес позы). */
	FQuat ToQuat(float Scale = 1.f) const;
};

USTRUCT(BlueprintType)
struct NOFEAR_API FNoFearPoseBoneEntry
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pose")
	ENoFearPoseBone Bone = ENoFearPoseBone::Spine02;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pose")
	FNoFearBoneAngles Angles;
};

/** Описание одной позы. Таблица лежит в UNoFearPoseComponent::Poses и правится в Blueprint. */
USTRUCT(BlueprintType)
struct NOFEAR_API FNoFearPoseSettings
{
	GENERATED_BODY()

	/** Повороты костей при полном весе. Несколько записей на одну кость складываются. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pose")
	TArray<FNoFearPoseBoneEntry> Bones;

	/** За сколько секунд поза включается. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pose", meta = (ClampMin = "0"))
	float BlendInTime = 0.1f;

	/** За сколько секунд выключается. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pose", meta = (ClampMin = "0"))
	float BlendOutTime = 0.3f;

	/** Направленная поза: «вперёд» в углах — это направление выстрела (корпус отклоняется по пуле), а не взгляд. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pose")
	bool bDirectional = false;

	/**
	 * Готовая анимация вместо процедурных поворотов. Если задана, PlayPose проигрывает этот монтаж на основном
	 * AnimBP (GASP), а повороты костей не применяются. Так позу можно заменить настоящей анимацией, не трогая код.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pose")
	TObjectPtr<UAnimMontage> ReplacementMontage = nullptr;
};

/**
 * Итог для анимации: поворот на кость в пространстве меша. Заполняется на игровом потоке
 * (UNoFearPoseComponent::BuildFrame), читается из анимационных потоков (FAnimNode_NoFearPose).
 */
struct NOFEAR_API FNoFearPoseFrame
{
	FQuat Rotations[static_cast<int32>(ENoFearPoseBone::Count)];
	/** Бит i — у кости i есть поворот. */
	uint32 ActiveMask = 0;

	FNoFearPoseFrame()
	{
		for (FQuat& Rotation : Rotations)
		{
			Rotation = FQuat::Identity;
		}
	}

	bool IsEmpty() const { return ActiveMask == 0; }
	bool IsBoneActive(ENoFearPoseBone Bone) const { return (ActiveMask & (1u << static_cast<uint32>(Bone))) != 0; }
};
