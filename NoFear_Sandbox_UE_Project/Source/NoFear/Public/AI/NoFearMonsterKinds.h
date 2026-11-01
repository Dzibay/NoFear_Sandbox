#pragma once

#include "CoreMinimal.h"
#include "AI/NoFearMonsterAIController.h"
class UCameraShakeBase;

#include "NoFearMonsterKinds.generated.h"

/**
 * Прыгун: мелкий и быстрый. Гонится спринтом, нападает прыжком издалека (UNoFearMonsterAttack_Leap) и после приземления
 * отскакивает назад. Почуяв, что на него навели оружие, отпрыгивает в сторону.
 * В кольцо стаи не встаёт и жетон ближнего боя не занимает: кружит на краю зрения цели, у фланга (StalkDistance) и прыгает,
 * когда цель отвлеклась (IsTargetDistracted): перезаряжается, стреляет или бьёт в другую сторону, стоит спиной,
 * сбита с ног, в слизи, прижата или ослеплена. Ждал дольше LeapPatience — прыгает и так.
 * Тело: CBP_Bot_Jumper.
 */
UCLASS(Blueprintable)
class NOFEAR_API ANoFearJumperAIController : public ANoFearMonsterAIController
{
	GENERATED_BODY()

public:
	ANoFearJumperAIController(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	/** Отпрыгивать в сторону, когда на прыгуна смотрит ствол врага. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|Monster|Dodge")
	bool bDodgeWhenAimedAt = true;

	/** Ствол смотрит на прыгуна точнее этого угла — уклоняется, градусы (вблизи угол шире — по ширине тела). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|Monster|Dodge", meta = (ClampMin = "0", ClampMax = "45"))
	float DodgeAimAngle = 6.f;

	/** Дальше этого от стрелка не уклоняется, см. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|Monster|Dodge", meta = (ClampMin = "0"))
	float DodgeMaxDistance = 2500.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|Monster|Dodge", meta = (ClampMin = "0"))
	float DodgeSpeed = 750.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|Monster|Dodge", meta = (ClampMin = "0"))
	float DodgeUpSpeed = 250.f;

	/** Уклоняется не чаще, чем раз в столько секунд. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|Monster|Dodge", meta = (ClampMin = "0"))
	float DodgeCooldown = 2.5f;

	/** Сколько раз уклонился с появления. */
	UFUNCTION(BlueprintPure, Category = "NoFear|Monster|Dodge")
	int32 GetDodgeCount() const { return DodgeCount; }

	/** Уклониться прочь с линии прицела From (вбок от направления AimDirection). */
	UFUNCTION(BlueprintCallable, Category = "NoFear|Monster|Dodge")
	bool Dodge(FVector AimDirection);

	/** Пока стая окружает цель, держится от неё на этом расстоянии (в пределах прыжка), см. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|Monster|Stalk", meta = (ClampMin = "0"))
	float StalkDistance = 850.f;

	/**
	 * На сколько градусов от взгляда цели держится: 90 — фланг, 180 — точно за спину. Меньше BackTurnedAngle — на краю
	 * зрения: следящий за флангом его сдерживает, отвернулся или отвлёкся — прыжок.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|Monster|Stalk", meta = (ClampMin = "0", ClampMax = "180"))
	float StalkAngle = 80.f;

	/** Цель отвернулась от прыгуна больше чем на столько градусов — «стоит спиной». */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|Monster|Stalk", meta = (ClampMin = "0", ClampMax = "180"))
	float BackTurnedAngle = 100.f;

	/** Цель стреляет или бьёт и смотрит мимо прыгуна больше чем на столько градусов — занята другим. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|Monster|Stalk", meta = (ClampMin = "0", ClampMax = "180"))
	float BusyAngle = 35.f;

	/** «Стреляет» — выстрел был не раньше столько секунд назад. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|Monster|Stalk", meta = (ClampMin = "0"))
	float BusyFireWindow = 0.8f;

	/** После своей атаки следующий прыжок не раньше чем через столько секунд. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|Monster|Stalk", meta = (ClampMin = "0"))
	float LeapRest = 4.f;

	/** Цель так и не отвлеклась за столько секунд — прыгает всё равно. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|Monster|Stalk", meta = (ClampMin = "0"))
	float LeapPatience = 8.f;

	/** Цель отвлеклась от прыгуна — можно прыгать. */
	UFUNCTION(BlueprintPure, Category = "NoFear|Monster|Stalk")
	bool IsTargetDistracted(const AActor* Target) const;

	/** Сколько атак начал по возможности (цель отвлеклась) и от нетерпения. */
	UFUNCTION(BlueprintPure, Category = "NoFear|Monster|Stalk")
	int32 GetOpportunityLeapCount() const { return OpportunityLeaps; }

	UFUNCTION(BlueprintPure, Category = "NoFear|Monster|Stalk")
	int32 GetImpatientLeapCount() const { return ImpatientLeaps; }

	virtual bool IsRingFighter() const override { return false; }
	virtual bool GetStandoffPoint(const FVector& TargetLocation, const FVector& PackSide, FVector& OutPoint) const override;

protected:
	virtual void UpdateBot() override;
	virtual void UpdateMovementStance() override;
	virtual bool WantsFreeAttack(AActor* Target) override;
	virtual void OnMeleeAttackFinished(AActor* Target) override;

private:
	/** Враг с оружием в руках, который сейчас целится в прыгуна. Возвращает направление его прицела. */
	bool FindAimingThreat(FVector& OutAimDirection) const;

	int32 DodgeCount = 0;
	double NextDodgeTime = 0.0;

	/** Решил прыгать — держит решение до конца атаки. */
	bool bLeapCommitted = false;
	double NextLeapTime = 0.0;
	double WaitStartTime = -1.0;
	TWeakObjectPtr<AActor> WaitTarget;
	int32 OpportunityLeaps = 0;
	int32 ImpatientLeaps = 0;
};

/**
 * Плевун: держится на 800–1500 см и плюёт по дуге кислотой (UNoFearMonsterAttack_Spit): снаряд ранит при попадании
 * и оставляет лужу, которая жжёт стоящих в ней. Не стоит в очереди за жетоном и в кольцо стаи не встаёт: пока стая
 * окружает цель, стоит за кольцом со своей стороны (StandDistance) и прижимает цель огнём; между плевками
 * перебирается на своё место. Цель ближе RetreatStartDistance — отступает (ключ KeepAway, ветка дерева «Отступить»);
 * прижали — отбивается слабым ударом.
 * Тело: CBP_Bot_Spitter.
 */
UCLASS(Blueprintable)
class NOFEAR_API ANoFearSpitterAIController : public ANoFearMonsterAIController
{
	GENERATED_BODY()

public:
	ANoFearSpitterAIController(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	/** Цель ближе этого — отступать, см. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|Monster|Spitter", meta = (ClampMin = "0"))
	float RetreatStartDistance = 700.f;

	/** Отступает, пока цель не дальше этого, см. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|Monster|Spitter", meta = (ClampMin = "0"))
	float RetreatStopDistance = 950.f;

	UFUNCTION(BlueprintPure, Category = "NoFear|Monster|Spitter")
	bool IsKeepingAway() const { return bKeepingAway; }

	/** Сколько раз начинал отступать. */
	UFUNCTION(BlueprintPure, Category = "NoFear|Monster|Spitter")
	int32 GetRetreatCount() const { return RetreatCount; }

	/** Пока стая окружает цель, стоит от неё на этом расстоянии, за кольцом со стороны стаи, см. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|Monster|Spitter", meta = (ClampMin = "0"))
	float StandDistance = 1150.f;

	/** Дальше этого от своего места — после плевка сначала перебирается, потом плюёт снова, см. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|Monster|Spitter", meta = (ClampMin = "0"))
	float StandTolerance = 300.f;

	/** Перебирается не дольше столько секунд после плевка — потом плюёт откуда стоит. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|Monster|Spitter", meta = (ClampMin = "0"))
	float RelocateTime = 3.f;

	virtual bool GetStandoffPoint(const FVector& TargetLocation, const FVector& PackSide, FVector& OutPoint) const override;

protected:
	virtual void UpdateBot() override;
	virtual void WriteBlackboard() override;
	virtual void ConfigureMelee(UNoFearMeleeComponent* Melee) override;
	virtual bool WantsFreeAttack(AActor* Target) override;
	virtual void OnMeleeAttackFinished(AActor* Target) override;

private:
	bool bKeepingAway = false;
	int32 RetreatCount = 0;
	double LastAttackTime = -1.0e9;
};

/**
 * Колосс (танк, раздел 4.5 Docs/Monsters_Design.md): большой, медленный (шагом), много здоровья.
 * - Броня: спереди ×0, бока ×0.75, спина ×1, нарост на спине (spine_03) ×2.5 и светится. Три пластины спереди
 *   (грудь, плечи) по 60 прочности сбивает только взрыв ближе 400; сорванная открывает зону ×0.8, сорваны все — перед ×0.6.
 * - Таран (UNoFearMonsterAttack_Charge): врезался в стену — 2.5 с оглушения без передней брони (всё ×1.5, нарост ×3);
 *   промах без стены — 0.8 с на разворот. Ломает укрытия (тег NoFearBreakable) и сносит физические предметы.
 * - Рёв раз в RoarInterval: страх людям в RoarFearRadius, мирные в RoarPanicRadius срываются в бег, у героя трясётся камера.
 * - Шаги слышны за StepFootstepRange, у героя в StepShakeRadius слабая дрожь камеры.
 * - Хват и бросок (UNoFearMonsterAttack_Grab).
 * - Щит: по стае стреляют хотя бы два стрелка с одной стороны — колосс идёт на них, союзники в ShieldAllyRadius встают в его тень.
 * - Ярость: здоровье ≤ RageHealthFraction — ревёт, бегом RageSpeed, перезарядки ×RageCooldownScale, таран вдвое чаще.
 * Тело: CBP_Bot_Tank.
 */
UCLASS(Blueprintable)
class NOFEAR_API ANoFearTankAIController : public ANoFearMonsterAIController
{
	GENERATED_BODY()

public:
	ANoFearTankAIController(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	// --- Рёв ---

	/** Рёв раз в столько секунд, если цель ближе RoarTargetRange. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|Tank|Roar", meta = (ClampMin = "0"))
	float RoarInterval = 18.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|Tank|Roar", meta = (ClampMin = "0"))
	float RoarTargetRange = 2500.f;

	/** Страх людям в радиусе: на сколько и не выше какого уровня. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|Tank|Roar", meta = (ClampMin = "0"))
	float RoarFearAmount = 18.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|Tank|Roar", meta = (ClampMin = "0", ClampMax = "100"))
	float RoarFearCap = 80.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|Tank|Roar", meta = (ClampMin = "0"))
	float RoarFearRadius = 1500.f;

	/** Мирные ближе этого срываются в бег, см. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|Tank|Roar", meta = (ClampMin = "0"))
	float RoarPanicRadius = 800.f;

	/** Дрожь камеры героя на рёв и на шаги (UCameraShakeBase). Пусто — без дрожи. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|Tank|Shake")
	TSubclassOf<UCameraShakeBase> RoarShake;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|Tank|Shake")
	TSubclassOf<UCameraShakeBase> StepShake;

	// --- Шаги ---

	/** Шаги слышны ботам на столько, см. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|Tank|Steps", meta = (ClampMin = "0"))
	float StepFootstepRange = 4000.f;

	/** Дрожь камеры героя от шагов в этом радиусе, см. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|Tank|Steps", meta = (ClampMin = "0"))
	float StepShakeRadius = 1500.f;

	/** Дрожь на каждые столько см пути. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|Tank|Steps", meta = (ClampMin = "50"))
	float StepShakeDistance = 240.f;

	// --- Щит ---

	/** Столько разных стрелков с одной стороны включают щит. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|Tank|Shield", meta = (ClampMin = "1"))
	int32 ShieldMinShooters = 2;

	/** Стрелок «стреляет по стае», если ствол смотрит на колосса точнее этого угла, градусы. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|Tank|Shield", meta = (ClampMin = "0", ClampMax = "90"))
	float ShieldAimAngle = 30.f;

	/** Выстрелы помнит столько секунд. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|Tank|Shield", meta = (ClampMin = "0"))
	float ShieldMemory = 3.f;

	/** Стрелки «с одной стороны», если направления на них не расходятся больше чем на столько от среднего, градусы. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|Tank|Shield", meta = (ClampMin = "0", ClampMax = "180"))
	float ShieldSideAngle = 60.f;

	/** Стрелки дальше этого не считаются, см. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|Tank|Shield", meta = (ClampMin = "0"))
	float ShieldShooterRange = 3500.f;

	/** Союзники в этом радиусе встают в тень, см. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|Tank|Shield", meta = (ClampMin = "0"))
	float ShieldAllyRadius = 900.f;

	/** Место в тени: столько см позади колосса, и по столько в стороны друг от друга. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|Tank|Shield", meta = (ClampMin = "0"))
	float ShieldShadeDistance = 190.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|Tank|Shield", meta = (ClampMin = "0"))
	float ShieldShadeSpacing = 120.f;

	/** Прибавка к оценке стрелков, пока щит включён. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|Tank|Shield", meta = (ClampMin = "0"))
	float ShieldTargetBonus = 3.f;

	// --- Ярость ---

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|Tank|Rage", meta = (ClampMin = "0", ClampMax = "1"))
	float RageHealthFraction = 0.5f;

	/** Скорость в ярости, см/с: шаг колосса ускоряется до неё (время тела идёт быстрее, пока он идёт). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|Tank|Rage", meta = (ClampMin = "0"))
	float RageSpeed = 360.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|Tank|Rage", meta = (ClampMin = "0.05"))
	float RageCooldownScale = 0.6f;

	/** Таран в ярости перезаряжается во столько раз быстрее и стартует ближе (слепой). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|Tank|Rage", meta = (ClampMin = "0.05"))
	float RageChargeCooldownScale = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|Tank|Rage", meta = (ClampMin = "0"))
	float RageChargeMinRange = 250.f;

	UFUNCTION(BlueprintPure, Category = "NoFear|Tank")
	bool IsRaging() const { return bRaging; }

	UFUNCTION(BlueprintPure, Category = "NoFear|Tank")
	bool IsShielding() const { return bShielding; }

	/** Центр огня, от которого колосс прикрывает (есть, пока щит включён). */
	UFUNCTION(BlueprintPure, Category = "NoFear|Tank")
	FVector GetShieldFireCenter() const { return ShieldFireCenter; }

	/** Рёвов с испугом людей (для тестов). */
	UFUNCTION(BlueprintPure, Category = "NoFear|Tank")
	int32 GetTankRoarCount() const { return TankRoarCount; }

	UFUNCTION(BlueprintPure, Category = "NoFear|Tank")
	int32 GetStepShakeCount() const { return StepShakeCount; }

	UFUNCTION(BlueprintPure, Category = "NoFear|Tank")
	int32 GetShieldedAllyCount() const { return ShieldedAllyCount; }

	/** Включить ярость сейчас: Duration > 0 — на столько секунд (смерть шептуна), иначе навсегда. */
	UFUNCTION(BlueprintCallable, Category = "NoFear|Tank")
	void StartRage(float Duration = 0.f);

	virtual void Roar() override;

protected:
	virtual void OnPossess(APawn* InPawn) override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void UpdateBot() override;
	virtual bool ShouldWalk() const override { return true; }
	virtual float ScoreTarget(const AActor* Candidate, const FNoFearKnownActor& Knowledge) const override;
	virtual void HandleDanger(ENoFearDangerType Type, AActor* Source, FVector ThreatLocation) override;

private:
	struct FShot
	{
		TWeakObjectPtr<AActor> Shooter;
		FVector Location = FVector::ZeroVector;
		double Time = 0.0;
	};

	void UpdateShield();
	void UpdateSteps();
	void UpdateRage();
	void ShakeHeroCamera(TSubclassOf<UCameraShakeBase> Shake, float Radius, float Scale);

	TArray<FShot> Shots;
	TArray<TWeakObjectPtr<AActor>> ShieldShooters;
	FVector ShieldFireCenter = FVector::ZeroVector;
	FVector LastStepLocation = FVector::ZeroVector;
	float StepTravel = 0.f;
	bool bShielding = false;
	bool bRaging = false;
	bool bHasLastStepLocation = false;
	int32 TankRoarCount = 0;
	int32 StepShakeCount = 0;
	int32 ShieldedAllyCount = 0;
	float BaseChargeCooldown = 0.f;
	float BaseChargeMinRange = 0.f;
	float BaseMaxWalkSpeed = 0.f;
	/** Ярость на время (смерть шептуна); 0 — навсегда (здоровье). */
	double RageUntil = 0.0;
};

class UAudioComponent;
class USoundBase;
class UNoFearHealthComponent;
struct FNoFearDamageInfo;

/**
 * Шептун (раздел 4.6 Docs/Monsters_Design.md): слабый, сам не дерётся — пугает людей, ослабляет их и усиливает своих.
 * - Держится в KeepMinDistance–KeepMaxDistance от ближайшего вооружённого человека, за колоссом, если тот рядом.
 * - Аура: люди в AuraRadius (сквозь стены) получают страх, герой — через браслет (источник «Шёпот»). Слышен шёпот.
 * - Крик-волна раз в ScreamInterval: замах ScreamWindup (голова светится и получает ×3), потом страх в ScreamRadius и
 *   паника мирных. Урон не меньше ScreamInterruptDamage за замах срывает крик и оглушает шептуна.
 * - Метка жертвы раз в MarkInterval: самый напуганный человек в MarkRange, канал MarkChannelTime с лучом. Метка
 *   (ENoFearOpportunity::Marked) висит MarkDuration: страх помеченного не спадает и растёт, монстры бьют его первым.
 *   Канал срывает урон не меньше MarkInterruptFraction здоровья или потеря видимости.
 * - Видения: боты-люди в ауре со страхом от VisionMinFear время от времени стреляют в пустоту (UNoFearCombatComponent::SeePhantom).
 * - Бафф: монстры в BuffRadius быстрее, сильнее, чаще бьют (ApplyWhisperBuff).
 * - Охрана: ранили или человек ближе GuardTriggerRadius — два ближайших загонщика или ловчих идут на обидчика,
 *   колосс закрывает линию огня, сам шептун уходит фазовым шагом.
 * - Смерть: всё пропадает сразу, колоссы рядом впадают в ярость.
 * Тело: CBP_Bot_Whisperer.
 */
UCLASS(Blueprintable)
class NOFEAR_API ANoFearWhispererAIController : public ANoFearMonsterAIController
{
	GENERATED_BODY()

public:
	ANoFearWhispererAIController(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	// --- Место ---

	/** Держится в этом коридоре расстояний от ближайшего вооружённого человека, см. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|Whisperer|Position", meta = (ClampMin = "0"))
	float KeepMinDistance = 1500.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|Whisperer|Position", meta = (ClampMin = "0"))
	float KeepMaxDistance = 2200.f;

	/** Колосс ближе этого — шептун встаёт в его тень, столько см позади него от стрелков. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|Whisperer|Position", meta = (ClampMin = "0"))
	float TankShadeRange = 1500.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|Whisperer|Position", meta = (ClampMin = "0"))
	float TankShadeDistance = 250.f;

	// --- Аура ---

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|Whisperer|Aura", meta = (ClampMin = "0"))
	float AuraRadius = 1200.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|Whisperer|Aura", meta = (ClampMin = "0"))
	float AuraFearPerSecond = 3.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|Whisperer|Aura", meta = (ClampMin = "0", ClampMax = "100"))
	float AuraFearCap = 85.f;

	/** Шёпот (зацикливается), громче ближе к шептуну. Пусто — без звука. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|Whisperer|Aura")
	TSoftObjectPtr<USoundBase> WhisperSound;

	// --- Крик-волна ---

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|Whisperer|Scream", meta = (ClampMin = "0"))
	float ScreamInterval = 14.f;

	/** Замах: шептун стоит, голова светится и уязвима. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|Whisperer|Scream", meta = (ClampMin = "0.1"))
	float ScreamWindup = 1.5f;

	/** Кричит, только если цель ближе этого, см. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|Whisperer|Scream", meta = (ClampMin = "0"))
	float ScreamTargetRange = 2500.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|Whisperer|Scream", meta = (ClampMin = "0"))
	float ScreamRadius = 1800.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|Whisperer|Scream", meta = (ClampMin = "0"))
	float ScreamFear = 22.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|Whisperer|Scream", meta = (ClampMin = "0", ClampMax = "100"))
	float ScreamFearCap = 95.f;

	/** Столько урона за замах срывает крик. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|Whisperer|Scream", meta = (ClampMin = "0"))
	float ScreamInterruptDamage = 15.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|Whisperer|Scream", meta = (ClampMin = "0"))
	float ScreamStunTime = 1.5f;

	// --- Метка ---

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|Whisperer|Mark", meta = (ClampMin = "0"))
	float MarkInterval = 20.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|Whisperer|Mark", meta = (ClampMin = "0"))
	float MarkRange = 2500.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|Whisperer|Mark", meta = (ClampMin = "0.1"))
	float MarkChannelTime = 3.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|Whisperer|Mark", meta = (ClampMin = "0"))
	float MarkDuration = 20.f;

	/** Страх помеченного растёт на столько в секунду (и не спадает). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|Whisperer|Mark", meta = (ClampMin = "0"))
	float MarkFearPerSecond = 1.f;

	/** Урон за канал от этой доли здоровья срывает метку. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|Whisperer|Mark", meta = (ClampMin = "0", ClampMax = "1"))
	float MarkInterruptFraction = 0.2f;

	// --- Видения ---

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|Whisperer|Visions", meta = (ClampMin = "0", ClampMax = "100"))
	float VisionMinFear = 60.f;

	/** Видение у одного из ботов раз в столько секунд (случайно в пределах). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|Whisperer|Visions")
	FVector2D VisionInterval = FVector2D(6.f, 10.f);

	/** Ложный монстр в стольких см от бота. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|Whisperer|Visions")
	FVector2D VisionDistance = FVector2D(800.f, 1500.f);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|Whisperer|Visions")
	FVector2D VisionDuration = FVector2D(2.f, 3.f);

	// --- Бафф ---

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|Whisperer|Buff", meta = (ClampMin = "0"))
	float BuffRadius = 1200.f;

	// --- Охрана ---

	/** Человек ближе этого — охрана поднимается, см. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|Whisperer|Guard", meta = (ClampMin = "0"))
	float GuardTriggerRadius = 900.f;

	/** Сколько ближайших загонщиков и ловчих идут на обидчика и на сколько секунд. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|Whisperer|Guard", meta = (ClampMin = "0"))
	int32 GuardDefenders = 2;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|Whisperer|Guard", meta = (ClampMin = "0"))
	float GuardTime = 10.f;

	/** Охрана поднимается не чаще, чем раз в столько секунд. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|Whisperer|Guard", meta = (ClampMin = "0"))
	float GuardCooldown = 2.f;

	/** Колосс ближе этого идёт закрывать линию огня, см. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|Whisperer|Guard", meta = (ClampMin = "0"))
	float TankCoverRange = 2000.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|Whisperer|Guard", meta = (ClampMin = "0"))
	float PhaseStepDistance = 500.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|Whisperer|Guard", meta = (ClampMin = "0"))
	float PhaseStepCooldown = 8.f;

	// --- Смерть ---

	/** Колоссы ближе этого впадают в ярость на DeathRageTime секунд, см. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|Whisperer|Death", meta = (ClampMin = "0"))
	float DeathRageRange = 2000.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|Whisperer|Death", meta = (ClampMin = "0"))
	float DeathRageTime = 20.f;

	// --- Состояние (для тестов) ---

	UFUNCTION(BlueprintPure, Category = "NoFear|Whisperer")
	bool IsScreamWindup() const { return bScreamWindup; }

	UFUNCTION(BlueprintPure, Category = "NoFear|Whisperer")
	int32 GetScreamCount() const { return ScreamCount; }

	UFUNCTION(BlueprintPure, Category = "NoFear|Whisperer")
	int32 GetScreamInterruptCount() const { return ScreamInterruptCount; }

	UFUNCTION(BlueprintPure, Category = "NoFear|Whisperer")
	AActor* GetMarkTarget() const;

	UFUNCTION(BlueprintPure, Category = "NoFear|Whisperer")
	bool IsChannelingMark() const { return bChanneling; }

	UFUNCTION(BlueprintPure, Category = "NoFear|Whisperer")
	int32 GetMarkCount() const { return MarkCount; }

	UFUNCTION(BlueprintPure, Category = "NoFear|Whisperer")
	int32 GetMarkInterruptCount() const { return MarkInterruptCount; }

	UFUNCTION(BlueprintPure, Category = "NoFear|Whisperer")
	int32 GetVisionCount() const { return VisionCount; }

	UFUNCTION(BlueprintPure, Category = "NoFear|Whisperer")
	int32 GetPhaseStepCount() const { return PhaseStepCount; }

	UFUNCTION(BlueprintPure, Category = "NoFear|Whisperer")
	int32 GetGuardCount() const { return GuardCount; }

	/** Сколько монстров под баффом на последнем пересчёте. */
	UFUNCTION(BlueprintPure, Category = "NoFear|Whisperer")
	int32 GetBuffedCount() const { return BuffedCount; }

	/** Начать канал метки на Victim сейчас (отладка и тесты). */
	UFUNCTION(BlueprintCallable, Category = "NoFear|Whisperer")
	bool StartMark(AActor* Victim);

	/** Фазовый шаг прочь от From (отладка и тесты). */
	UFUNCTION(BlueprintCallable, Category = "NoFear|Whisperer")
	bool PhaseStep(FVector From);

	/** Поднять охрану против Offender (отладка и тесты). */
	UFUNCTION(BlueprintCallable, Category = "NoFear|Whisperer")
	void RaiseGuard(AActor* Offender);

	/** Видение у бота сейчас (отладка и тесты). */
	UFUNCTION(BlueprintCallable, Category = "NoFear|Whisperer")
	bool SendVision(APawn* Victim);

	/** Замах крика вместо рыка. */
	virtual void Roar() override;
	virtual bool IsRingFighter() const override { return false; }

protected:
	virtual void OnPossess(APawn* InPawn) override;
	virtual void OnUnPossess() override;
	virtual void UpdateBot() override;
	virtual bool WantsFreeAttack(AActor* Target) override;

private:
	UFUNCTION()
	void HandleDamaged(UNoFearHealthComponent* Component, const FNoFearDamageInfo& Info);

	UFUNCTION()
	void HandleDeath(UNoFearHealthComponent* Component, const FNoFearDamageInfo& Info);

	UFUNCTION()
	void HandleWhisperFinished();

	void UpdateAura(float DeltaTime);
	void UpdateBuff();
	void UpdateVisions();
	void UpdateMark(float DeltaTime);
	void UpdateGuardTrigger();
	void UpdatePosition();
	void FinishScream();
	void CancelScream();
	void CancelMark();
	/** Всё, что держит шептун, пропадает: смерть или уход тела. */
	void ClearEffects();
	/** Ближайший живой враждебный человек (вооружённый, если bArmed) и расстояние до него. */
	APawn* FindNearestHuman(bool bArmed, float& OutDistance) const;
	bool CanSee(const AActor* Other) const;
	void Stun(const FVector& From);

	TWeakObjectPtr<UAudioComponent> WhisperAudio;
	TWeakObjectPtr<UNoFearHealthComponent> BoundHealth;

	bool bScreamWindup = false;
	float ScreamDamage = 0.f;
	double NextScreamTime = 0.0;
	FTimerHandle ScreamTimer;
	int32 ScreamCount = 0;
	int32 ScreamInterruptCount = 0;

	TWeakObjectPtr<AActor> MarkVictim;
	TWeakObjectPtr<AActor> ChannelVictim;
	bool bChanneling = false;
	double ChannelEndTime = 0.0;
	double MarkUntil = 0.0;
	double NextMarkTime = 0.0;
	float ChannelDamage = 0.f;
	/** Ниже этого страх помеченного не опускается (растёт на MarkFearPerSecond). */
	float MarkFearFloor = 0.f;
	int32 MarkCount = 0;
	int32 MarkInterruptCount = 0;

	double NextVisionTime = 0.0;
	int32 VisionCount = 0;
	TArray<TWeakObjectPtr<APawn>> VisionVictims;

	double NextGuardTime = 0.0;
	double NextPhaseStepTime = 0.0;
	int32 GuardCount = 0;
	int32 PhaseStepCount = 0;
	int32 BuffedCount = 0;

	double LastUpdateTime = -1.0;
};
