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

	/** Бить сферой перед телом в окне удара. false — урон наносит сама атака (плевок снарядом). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Damage")
	bool bSweepHits = true;

	/** Отбросить задетого: горизонтальная и вертикальная скорость, см/с. 0 — не отбрасывать. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Damage", meta = (ClampMin = "0"))
	float KnockbackSpeed = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Damage", meta = (ClampMin = "0"))
	float KnockbackUpSpeed = 0.f;

	/** Отброс от этой скорости сбивает с ног: метка KnockedDown на KnockDownTime сек. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Damage", meta = (ClampMin = "0"))
	float KnockDownMinSpeed = 600.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Damage", meta = (ClampMin = "0"))
	float KnockDownTime = 1.2f;

	/** Ломает укрытия: актёры с тегом NoFearBreakable исчезают, физические предметы отлетают (таран, Smash). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Damage")
	bool bBreaksCover = false;

	/** Импульс, который получают физические предметы на пути, см/с. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Damage", meta = (ClampMin = "0", EditCondition = "bBreaksCover"))
	float CoverImpulse = 800.f;

	/** Сколько укрытий сломано этой атакой за всё время (для тестов). */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "Damage")
	int32 BrokenCoverCount = 0;

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

	/** Под ударом оказался предмет (не персонаж); вызывается при bBreaksCover. */
	virtual void BreakCover(UNoFearMeleeComponent* Melee, AActor* Prop, UPrimitiveComponent* Component);

	/** Тег актёров, которые ломает bBreaksCover. */
	static const FName BreakableTag;

	/** Окно удара закончилось, началось восстановление. */
	virtual void BeginRecovery(UNoFearMeleeComponent* Melee, ACharacter* Owner, AActor* Target) {}

	/** Атака закончилась (или прервана смертью). */
	virtual void EndAttack(UNoFearMeleeComponent* Melee, ACharacter* Owner) {}

	/** Когда атаку снова можно выбрать (время мира). Ведёт компонент. */
	double NextUseTime = 0.0;

	/** В этой атаке уже кого-то задело. Ведёт компонент. */
	bool bHitThisAttack = false;

	/** Когда началось окно удара (время мира). Ведёт компонент. */
	double StrikeStartTime = 0.0;
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

/**
 * Прыжок издалека (прыгун): высокая дуга с приземлением перед целью, удар при посадке, после — отскок назад.
 * Окно удара длится до приземления (не дольше HitWindow).
 */
UCLASS(meta = (DisplayName = "Leap"))
class NOFEAR_API UNoFearMonsterAttack_Leap : public UNoFearMonsterAttack
{
	GENERATED_BODY()

public:
	UNoFearMonsterAttack_Leap();

	/** Отскок после приземления: от цели и вверх, см/с. 0 — не отскакивать. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Lunge", meta = (ClampMin = "0"))
	float BounceSpeed = 650.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Lunge", meta = (ClampMin = "0"))
	float BounceUpSpeed = 350.f;

	virtual bool TickStrike(UNoFearMeleeComponent* Melee, ACharacter* Owner, AActor* Target, float DeltaTime) override;
	virtual void BeginRecovery(UNoFearMeleeComponent* Melee, ACharacter* Owner, AActor* Target) override;
};

/**
 * Плевок (плевун): на ударе выпускает по дуге снаряд ProjectileClass с упреждением по скорости цели.
 * Урон наносят снаряд и лужа, которую он оставляет. Нужна прямая видимость цели.
 */
UCLASS(meta = (DisplayName = "Spit"))
class NOFEAR_API UNoFearMonsterAttack_Spit : public UNoFearMonsterAttack
{
	GENERATED_BODY()

public:
	UNoFearMonsterAttack_Spit();

	/** Снаряд (по умолчанию ANoFearSpitProjectile). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spit")
	TSubclassOf<AActor> ProjectileClass;

	/** Высота дуги: 0 — почти прямо, 1 — очень навесом. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spit", meta = (ClampMin = "0.05", ClampMax = "0.95"))
	float ArcParam = 0.35f;

	/** Откуда вылетает снаряд: столько см вверх и вперёд от центра тела. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spit")
	float MuzzleUp = 60.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spit")
	float MuzzleForward = 50.f;

	/** Насколько упреждать движение цели (0 — бить в текущую точку, 1 — в точку, где цель будет). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spit", meta = (ClampMin = "0", ClampMax = "1.5"))
	float LeadFactor = 0.7f;

	/** Сколько снарядов выпущено (для тестов). */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "Spit")
	int32 ShotCount = 0;

	virtual bool CanUse(const UNoFearMeleeComponent* Melee, const AActor* Target) const override;
	virtual void BeginStrike(UNoFearMeleeComponent* Melee, ACharacter* Owner, AActor* Target) override;
};

/**
 * Таран (танк): после замаха несётся по прямой с ChargeSpeed, не доворачивая. Задел цель — отбросил и остановился;
 * врезался в стену — оглушён на StunTime (поза пошатывания, бот стоит).
 */
UCLASS(meta = (DisplayName = "Charge"))
class NOFEAR_API UNoFearMonsterAttack_Charge : public UNoFearMonsterAttack
{
	GENERATED_BODY()

public:
	UNoFearMonsterAttack_Charge();

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Charge", meta = (ClampMin = "0"))
	float ChargeSpeed = 1200.f;

	/** Стена ближе этого перед капсулой — удар о стену, см. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Charge", meta = (ClampMin = "0"))
	float WallCheckDistance = 40.f;

	/** Оглушение после удара о стену, сек. Броня спереди не работает всё это время. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Charge", meta = (ClampMin = "0"))
	float StunTime = 2.5f;

	/** Восстановление, если таран попал в цель или в стену, сек. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Charge", meta = (ClampMin = "0"))
	float HitRecoveryTime = 0.6f;

	/** Восстановление, если таран прошёл мимо и стены не было: разворот, сек. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Charge", meta = (ClampMin = "0"))
	float MissRecoveryTime = 0.8f;

	/** Сколько раз врезался в стену (для тестов). */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "Charge")
	int32 WallHitCount = 0;

	virtual void BeginStrike(UNoFearMeleeComponent* Melee, ACharacter* Owner, AActor* Target) override;
	virtual bool TickStrike(UNoFearMeleeComponent* Melee, ACharacter* Owner, AActor* Target, float DeltaTime) override;
	virtual void BeginRecovery(UNoFearMeleeComponent* Melee, ACharacter* Owner, AActor* Target) override;

	/** Сколько таранов закончились промахом (ни цели, ни стены). Для тестов. */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "Charge")
	int32 MissCount = 0;

private:
	FVector ChargeDirection = FVector::ForwardVector;
	bool bWallHitThisCharge = false;
};

/**
 * Хват и бросок (колосс): цель вплотную, замах, держит HoldTime, потом бросает на ThrowPersonRange в другого человека
 * (в пределах ThrowPersonAngle от взгляда) или в ближайшую стену. Урон Damage при броске, ImpactDamage при ударе
 * о стену или человека; сбиты с ног оба на KnockDownTime.
 */
UCLASS(meta = (DisplayName = "Grab"))
class NOFEAR_API UNoFearMonsterAttack_Grab : public UNoFearMonsterAttack
{
	GENERATED_BODY()

public:
	UNoFearMonsterAttack_Grab();

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grab", meta = (ClampMin = "0"))
	float HoldTime = 0.6f;

	/** Где держит: столько см перед телом. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grab", meta = (ClampMin = "0"))
	float HoldDistance = 110.f;

	/** Дальность броска в стену, см. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grab", meta = (ClampMin = "0"))
	float ThrowRange = 1500.f;

	/** Бросает в человека, если он ближе этого, см. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grab", meta = (ClampMin = "0"))
	float ThrowPersonRange = 1800.f;

	/** ... и в пределах стольких градусов от взгляда. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grab", meta = (ClampMin = "0", ClampMax = "180"))
	float ThrowPersonAngle = 45.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grab", meta = (ClampMin = "0"))
	float ThrowUpSpeed = 450.f;

	/** Наибольшая горизонтальная скорость броска, см/с. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grab", meta = (ClampMin = "0"))
	float ThrowMaxSpeed = 1500.f;

	/** Урон при ударе брошенного о стену или человека (ему и задетому). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grab", meta = (ClampMin = "0"))
	float ImpactDamage = 15.f;

	/** Сколько бросков было (для тестов). */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "Grab")
	int32 ThrowCount = 0;

	/** Последний бросок целил в человека (иначе в стену). */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "Grab")
	bool bLastThrowAtPerson = false;

	/** Последний бросок закончился ударом о стену или человека. */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "Grab")
	bool bLastThrowImpacted = false;

	/** Кого задел последний брошенный (человек на пути). */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "Grab")
	TWeakObjectPtr<AActor> LastImpactVictim;

	virtual bool CanUse(const UNoFearMeleeComponent* Melee, const AActor* Target) const override;
	virtual void BeginStrike(UNoFearMeleeComponent* Melee, ACharacter* Owner, AActor* Target) override;
	virtual bool TickStrike(UNoFearMeleeComponent* Melee, ACharacter* Owner, AActor* Target, float DeltaTime) override;
	virtual void EndAttack(UNoFearMeleeComponent* Melee, ACharacter* Owner) override;

private:
	void Release(ACharacter* Victim);
	void Throw(UNoFearMeleeComponent* Melee, ACharacter* Owner);
	void ImpactVictim(UNoFearMeleeComponent* Melee, ACharacter* Owner, AActor* Other);

	TWeakObjectPtr<ACharacter> Held;
	bool bThrown = false;
	float ThrownSpeed = 0.f;
	double ThrowTime = 0.0;
};
