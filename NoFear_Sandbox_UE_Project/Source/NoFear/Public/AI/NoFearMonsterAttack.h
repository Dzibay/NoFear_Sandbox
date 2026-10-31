#pragma once

#include "CoreMinimal.h"
#include "Animation/NoFearPoseTypes.h"
#include "UObject/Object.h"
#include "NoFearMonsterAttack.generated.h"

class ACharacter;
class UAnimMontage;
class UDamageType;
class UNoFearMeleeComponent;
class USoundBase;

/**
 * Одна атака монстра из набора UNoFearMeleeComponent::Attacks. Ход атаки общий: замах (разворот к цели, поза) →
 * удар (рывок, окно удара: сфера перед телом задевает не-союзников) → восстановление. Подклассы меняют дистанцию,
 * тайминги и то, что происходит в начале удара и при попадании (BeginStrike, ApplyHit).
 * Из подходящих по дистанции и перезарядке атак компонент выбирает случайно с весом Weight.
 */
UCLASS(Abstract, EditInlineNew, DefaultToInstanced, BlueprintType, CollapseCategories)
class NOFEAR_API UNoFearMonsterAttack : public UObject
{
	GENERATED_BODY()

public:
	UNoFearMonsterAttack();

	/** Имя для отладки, тестов и панели. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Attack")
	FName AttackName;

	/** С какого расстояния (между центрами) атаку можно начать, см. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Attack", meta = (ClampMin = "0"))
	float MinRange = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Attack", meta = (ClampMin = "0"))
	float MaxRange = 260.f;

	/** Перезарядка именно этой атаки, сек (умножается на CooldownScale компонента). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Attack", meta = (ClampMin = "0"))
	float Cooldown = 1.4f;

	/** Вес при случайном выборе среди подходящих атак. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Attack", meta = (ClampMin = "0"))
	float Weight = 1.f;

	/** Цель выше или ниже больше чем на столько — атака не подходит, см. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Attack", meta = (ClampMin = "0"))
	float MaxHeightDifference = 150.f;

	// --- Урон ---

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Damage", meta = (ClampMin = "0"))
	float Damage = 25.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Damage")
	TSubclassOf<UDamageType> DamageType;

	/** Центр сферы удара: столько см перед телом. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Damage", meta = (ClampMin = "0"))
	float HitReach = 90.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Damage", meta = (ClampMin = "1"))
	float HitRadius = 80.f;

	/** Отбросить задетого: горизонтальная и вертикальная скорость, см/с. 0 — не отбрасывать. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Damage", meta = (ClampMin = "0"))
	float KnockbackSpeed = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Damage", meta = (ClampMin = "0"))
	float KnockbackUpSpeed = 0.f;

	// --- Тайминги ---

	/** Замах до удара, сек: монстр разворачивается к цели, игрок успевает среагировать. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Timing", meta = (ClampMin = "0"))
	float WindupTime = 0.3f;

	/** Сколько длится окно удара, сек. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Timing", meta = (ClampMin = "0.01"))
	float HitWindow = 0.3f;

	/** Пауза после окна удара, сек. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Timing", meta = (ClampMin = "0"))
	float RecoveryTime = 0.4f;

	/** Скорость поворота к цели на замахе, град/с. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Timing", meta = (ClampMin = "0"))
	float WindupTurnRate = 720.f;

	// --- Рывок ---

	/** Горизонтальная скорость рывка в начале удара, см/с. 0 — бить с места. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Lunge", meta = (ClampMin = "0"))
	float LungeSpeed = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Lunge", meta = (ClampMin = "0"))
	float LungeUpSpeed = 0.f;

	/**
	 * Прыжок с LungeUpSpeed > 0 гасится так, чтобы приземлиться за столько см до цели, а не на неё; ближе этого
	 * скорость к цели гасится до конца атаки (капсулы персонажей друг друга не блокируют).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Lunge", meta = (ClampMin = "0"))
	float LungeStopDistance = 110.f;

	// --- Поза, анимация, звук ---

	/** Процедурная поза на замах и удар. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Effects")
	ENoFearPose Pose = ENoFearPose::Swing;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Effects", meta = (ClampMin = "0", ClampMax = "2"))
	float PoseIntensity = 1.f;

	/** Монтаж атаки. Пока своих анимаций нет — пусто. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Effects")
	TObjectPtr<UAnimMontage> Montage;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Effects", meta = (ClampMin = "0.01"))
	float MontagePlayRate = 1.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Effects")
	TObjectPtr<USoundBase> SwingSound;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Effects")
	TObjectPtr<USoundBase> HitSound;

	/** Атаку можно начать по этой цели: дистанция и высота подходят. Перезарядку проверяет компонент. */
	virtual bool CanUse(const UNoFearMeleeComponent* Melee, const AActor* Target) const;

	/** Начало замаха. */
	virtual void BeginWindup(UNoFearMeleeComponent* Melee, ACharacter* Owner, AActor* Target);

	/** Начало удара после замаха: по умолчанию рывок вперёд. */
	virtual void BeginStrike(UNoFearMeleeComponent* Melee, ACharacter* Owner, AActor* Target);

	/** Каждый кадр окна удара. Вернуть false, чтобы закончить окно раньше. */
	virtual bool TickStrike(UNoFearMeleeComponent* Melee, ACharacter* Owner, AActor* Target, float DeltaTime) { return true; }

	/** Задел Victim: урон и отбрасывание. */
	virtual void ApplyHit(UNoFearMeleeComponent* Melee, AActor* Victim, const FHitResult& Hit);

	/** Атака закончилась (или прервана смертью). */
	virtual void EndAttack(UNoFearMeleeComponent* Melee, ACharacter* Owner) {}

	/** Когда атаку снова можно выбрать (время мира). Ведёт компонент. */
	double NextUseTime = 0.0;
};

/** Удар когтями с места: короткая дистанция, быстрая перезарядка. */
UCLASS(meta = (DisplayName = "Swing"))
class NOFEAR_API UNoFearMonsterAttack_Swing : public UNoFearMonsterAttack
{
	GENERATED_BODY()

public:
	UNoFearMonsterAttack_Swing();
};

/** Рывок с дистанции: прыжок вперёд, удар на лету. Дальше и сильнее, но долго перезаряжается. */
UCLASS(meta = (DisplayName = "Lunge"))
class NOFEAR_API UNoFearMonsterAttack_Lunge : public UNoFearMonsterAttack
{
	GENERATED_BODY()

public:
	UNoFearMonsterAttack_Lunge();
};
