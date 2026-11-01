#pragma once

#include "CoreMinimal.h"
#include "NoFearMonsterTypes.generated.h"

/** С какой стороны (относительно взгляда монстра) должно прилететь, чтобы зона сработала. */
UENUM(BlueprintType)
enum class ENoFearWeakSide : uint8
{
	Any,
	/** Конус спереди (UNoFearVulnerabilityComponent::FrontHalfAngle). */
	Front,
	/** Конус сзади (BackHalfAngle). */
	Back,
	/** Не спереди и не сзади. */
	Flank,
	/** Всё, кроме переднего конуса. */
	NotFront
};

/** Когда зона открыта. Окна см. раздел 3.2 Docs/Monsters_Design.md. */
UENUM(BlueprintType)
enum class ENoFearWeakCondition : uint8
{
	Always,
	/** Восстановление после удара (ближний бой). */
	Recovery,
	/** Пошатывается или оглушён (UNoFearPoseComponent::IsActorStaggered). */
	Stunned,
	/** Замах атаки. */
	Windup,
	/** Выдох бешеного после серии ударов. */
	Exhale,
	/** В воздухе (прыжок ловчего). */
	Flight,
	/** Ест тело. */
	Eating,
	/** Броня сорвана (колосс). */
	ArmorBroken,
	/** Замах крика (шептун). */
	Scream,
	/** Включается вручную: UNoFearVulnerabilityComponent::SetZoneOpen (пластины колосса). */
	Manual
};

/** Слабое место (или броня, если множитель меньше 1). */
USTRUCT(BlueprintType)
struct NOFEAR_API FNoFearWeakZone
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weak Zone")
	FName Name;

	/**
	 * Кости зоны. Попадание в кость или в её потомков. Пусто — зона по стороне: действует на любое попадание
	 * с нужной стороны, в том числе взрыв и удар.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weak Zone")
	TArray<FName> Bones;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weak Zone")
	ENoFearWeakSide Side = ENoFearWeakSide::Any;

	/** Множитель урона поверх обычного множителя кости. 0 — непробиваемо. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weak Zone", meta = (ClampMin = "0"))
	float DamageMultiplier = 1.5f;

	/** Множитель накопления оглушения (этап 15). Пока только передаётся в FNoFearDamageInfo. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weak Zone", meta = (ClampMin = "0"))
	float StaggerMultiplier = 1.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weak Zone")
	ENoFearWeakCondition Condition = ENoFearWeakCondition::Always;

	/** Для Windup и Recovery: только у атаки с этим именем. Пусто — у любой. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weak Zone")
	FName AttackName;

	/** Зона открыта, пока условие НЕ выполнено (броня, которая перестаёт работать). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weak Zone")
	bool bInvert = false;

	/** Костевая зона: зоны по стороне на это попадание не умножаются (сорванная пластина поверх брони). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weak Zone")
	bool bIgnoresSideZones = false;

	/** Светиться, пока зона открыта (сфера на первой кости). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weak Zone")
	bool bGlow = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weak Zone")
	FLinearColor GlowColor = FLinearColor(1.f, 0.85f, 0.1f);

	/** Радиус светящейся сферы, см. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weak Zone", meta = (ClampMin = "1"))
	float GlowRadius = 9.f;
};

/** Пластина брони: сбивается только взрывом спереди. Сорванная открывает зону PlateZone. */
USTRUCT(BlueprintType)
struct NOFEAR_API FNoFearArmorPlate
{
	GENERATED_BODY()

	/** Зона (Manual), которую открывает сорванная пластина. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Plate")
	FName PlateZone;

	/** Направление пластины относительно взгляда монстра в градусах (0 — прямо, минус — влево). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Plate")
	float AngleDeg = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Plate", meta = (ClampMin = "1"))
	float MaxDurability = 60.f;

	UPROPERTY(BlueprintReadOnly, Category = "Plate")
	float Durability = 60.f;

	UPROPERTY(BlueprintReadOnly, Category = "Plate")
	bool bStripped = false;
};

/** Метка возможности на человеке: один вид монстров ставит, другие охотятся (раздел 3.3). */
UENUM(BlueprintType)
enum class ENoFearOpportunity : uint8
{
	KnockedDown,
	Slimed,
	Pinned,
	Blinded,
	/** Ближайший союзник дальше UNoFearOpportunitySubsystem::IsolatedDistance. Считается сам. */
	Isolated,
	Marked,
	InAcid,
	MAX UMETA(Hidden)
};
ENUM_RANGE_BY_COUNT(ENoFearOpportunity, ENoFearOpportunity::MAX);

/** Категория цели для весов вида (раздел 3.4). */
UENUM(BlueprintType)
enum class ENoFearTargetCategory : uint8
{
	Hero,
	Companion,
	Soldier,
	Civilian,
	Monster,
	Other
};

/** Веса категорий целей: прибавляются к оценке цели. */
USTRUCT(BlueprintType)
struct NOFEAR_API FNoFearTargetWeights
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Targets")
	float Hero = 1.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Targets")
	float Companion = 0.8f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Targets")
	float Soldier = 0.8f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Targets")
	float Civilian = 0.3f;

	/** Только для тех, кто вообще бьёт монстров (бешеный). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Targets")
	float Monster = 0.f;

	float Get(ENoFearTargetCategory Category) const
	{
		switch (Category)
		{
		case ENoFearTargetCategory::Hero: return Hero;
		case ENoFearTargetCategory::Companion: return Companion;
		case ENoFearTargetCategory::Soldier: return Soldier;
		case ENoFearTargetCategory::Civilian: return Civilian;
		case ENoFearTargetCategory::Monster: return Monster;
		default: return 0.f;
		}
	}
};
