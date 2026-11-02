#pragma once

#include "CoreMinimal.h"
#include "AI/NoFearMonsterAIController.h"
class UCameraShakeBase;
class UNoFearHealthComponent;
struct FNoFearDamageInfo;

#include "NoFearMonsterKinds.generated.h"

/**
 * Прыгун: мелкий и быстрый. Гонится спринтом, нападает прыжком издалека (UNoFearMonsterAttack_Leap) и после приземления
 * отскакивает назад. Почуяв, что на него навели оружие, отпрыгивает в сторону.
 * В кольцо стаи не встаёт и жетон ближнего боя не занимает: кружит на краю зрения цели, у фланга (StalkDistance) и прыгает,
 * когда цель отвлеклась (IsTargetDistracted): перезаряжается, стреляет или бьёт в другую сторону, стоит спиной,
 * сбита с ног, в слизи, прижата или ослеплена. Ждал дольше LeapPatience — прыгает и так.
 * Ловчий (раздел 4.3 Docs/Monsters_Design.md):
 * - Насест: ищет трассами уступ на 200–600 см выше цели в пределах 1500 см (FindPerch), бежит к подножию и забирается
 *   короткими прыжками по 150–200 см. Сверху ждёт, пока цель отвлечётся или отстанет (или PerchPatience), и прыгает
 *   на неё (UNoFearMonsterAttack_Pounce).
 * - Прижатие: допрыгнул — цель лежит (метки Pinned, KnockedDown), не ходит и не стреляет, ловчий сидит сверху и рвёт
 *   по 5 урона в 0.5 с. Вырваться: 8 нажатий Пробела за 3 с (12 при страхе от 60). Напарник снимает ловчего уроном
 *   от 30, дробовиком или ударом вплотную: тот отлетает и оглушён 1.5 с. Союзники цели бегут на выручку
 *   (ANoFearAIController, PinRescueRadius).
 * - В полёте (прыжок, Pounce) спина ×1.5 (зона Flight); попадание от 15 урона сбивает его на землю.
 * - Цель спряталась за укрытие — прыгает к ней сразу, не дожидаясь, пока отвлечётся.
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

	// --- Ловчий: насест ---

	/** Искать уступ над целью и прыгать на неё сверху. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|Monster|Perch")
	bool bUsePerches = true;

	/** Уступ ищет не дальше этого от цели, см. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|Monster|Perch", meta = (ClampMin = "0"))
	float PerchSearchRadius = 1500.f;

	/** Уступ выше ног цели на столько, см. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|Monster|Perch", meta = (ClampMin = "0"))
	float PerchMinHeight = 200.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|Monster|Perch", meta = (ClampMin = "0"))
	float PerchMaxHeight = 600.f;

	/** Цель ближе этого — насест не ищет, бьёт как прыгун, см. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|Monster|Perch", meta = (ClampMin = "0"))
	float PerchMinTargetDistance = 500.f;

	/** Один прыжок по стене поднимает не выше этого, см. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|Monster|Perch", meta = (ClampMin = "50"))
	float ClimbHopHeight = 180.f;

	/** На насесте ждёт не дольше столько секунд — потом прыгает и так. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|Monster|Perch", meta = (ClampMin = "0"))
	float PerchPatience = 8.f;

	/** Найти уступ над целью и пойти на него. false — уступа нет или сейчас нельзя. */
	UFUNCTION(BlueprintCallable, Category = "NoFear|Monster|Perch")
	bool TryPerch();

	/** Прыгнуть на текущую цель (Pounce) откуда стоит. */
	UFUNCTION(BlueprintCallable, Category = "NoFear|Monster|Perch")
	bool StartPounce();

	UFUNCTION(BlueprintPure, Category = "NoFear|Monster|Perch")
	bool IsPerched() const { return PerchState == EPerchState::Perched; }

	/** Идёт к подножию или лезет наверх. */
	UFUNCTION(BlueprintPure, Category = "NoFear|Monster|Perch")
	bool IsClimbing() const { return PerchState == EPerchState::Approach || PerchState == EPerchState::Climb; }

	/** Найденный уступ (верх) — для тестов и отладки. */
	UFUNCTION(BlueprintPure, Category = "NoFear|Monster|Perch")
	FVector GetPerchLocation() const { return PerchTop; }

	/** Сколько раз забрался на насест, прыгнул сверху и прыгнул к спрятавшейся за укрытие цели. */
	UFUNCTION(BlueprintPure, Category = "NoFear|Monster|Perch")
	int32 GetPerchCount() const { return PerchCount; }

	UFUNCTION(BlueprintPure, Category = "NoFear|Monster|Perch")
	int32 GetPounceCount() const { return PounceCount; }

	UFUNCTION(BlueprintPure, Category = "NoFear|Monster|Perch")
	int32 GetCoverLeapCount() const { return CoverLeaps; }

	// --- Ловчий: прижатие ---

	/** Урон прижатой цели за раз и раз в столько секунд. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|Monster|Pin", meta = (ClampMin = "0"))
	float PinDamage = 5.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|Monster|Pin", meta = (ClampMin = "0.05"))
	float PinDamageInterval = 0.5f;

	/** Сколько нажатий (Пробел) за PinEscapeWindow секунд нужно, чтобы вырваться; при страхе от PinAfraidFear — больше. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|Monster|Pin", meta = (ClampMin = "1"))
	int32 PinEscapePresses = 8;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|Monster|Pin", meta = (ClampMin = "1"))
	int32 PinEscapePressesAfraid = 12;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|Monster|Pin", meta = (ClampMin = "0", ClampMax = "100"))
	float PinAfraidFear = 60.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|Monster|Pin", meta = (ClampMin = "0.5"))
	float PinEscapeWindow = 3.f;

	/** Сбить с прижатой цели: столько урона за прижатие, дробовик или удар не дальше KnockOffMeleeDistance. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|Monster|Pin", meta = (ClampMin = "0"))
	float KnockOffDamage = 30.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|Monster|Pin", meta = (ClampMin = "0"))
	float KnockOffMeleeDistance = 200.f;

	/** Оглушение сбитого с цели, сек. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|Monster|Pin", meta = (ClampMin = "0"))
	float KnockOffStunTime = 1.5f;

	/** В полёте попадание от столько урона сбивает на землю и оглушает на FlightStunTime. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|Monster|Pin", meta = (ClampMin = "0"))
	float FlightKnockDamage = 15.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|Monster|Pin", meta = (ClampMin = "0"))
	float FlightStunTime = 1.f;

	UFUNCTION(BlueprintPure, Category = "NoFear|Monster|Pin")
	bool IsPinning() const { return PinVictim.IsValid(); }

	UFUNCTION(BlueprintPure, Category = "NoFear|Monster|Pin")
	ACharacter* GetPinVictim() const { return PinVictim.Get(); }

	/** Жертва нажала «вырваться» (Пробел). */
	UFUNCTION(BlueprintCallable, Category = "NoFear|Monster|Pin")
	void AddEscapePress();

	/** Сколько нажатий нужно этой жертве и сколько сделано за окно, 0–1. */
	UFUNCTION(BlueprintPure, Category = "NoFear|Monster|Pin")
	int32 GetEscapeRequired() const;

	UFUNCTION(BlueprintPure, Category = "NoFear|Monster|Pin")
	float GetEscapeProgress() const;

	/** Сбросить с жертвы: отлетает от From и оглушён. */
	UFUNCTION(BlueprintCallable, Category = "NoFear|Monster|Pin")
	void ThrowOff(AActor* From);

	UFUNCTION(BlueprintPure, Category = "NoFear|Monster|Pin")
	int32 GetPinCount() const { return PinCount; }

	UFUNCTION(BlueprintPure, Category = "NoFear|Monster|Pin")
	int32 GetKnockOffCount() const { return KnockOffCount; }

	UFUNCTION(BlueprintPure, Category = "NoFear|Monster|Pin")
	int32 GetEscapeCount() const { return EscapeCount; }

	UFUNCTION(BlueprintPure, Category = "NoFear|Monster|Pin")
	int32 GetFlightKnockCount() const { return FlightKnockCount; }

	/** Ловчий, который прижал Victim (или null). */
	UFUNCTION(BlueprintPure, Category = "NoFear|Monster|Pin")
	static ANoFearJumperAIController* FindPinner(const AActor* Victim);

	/** Все ловчие, которые сейчас кого-то прижали. */
	static void GetActivePinners(const UWorld* World, TArray<ANoFearJumperAIController*>& OutPinners);

protected:
	virtual void OnPossess(APawn* InPawn) override;
	virtual void OnUnPossess() override;
	virtual void UpdateBot() override;
	virtual void UpdateMovementStance() override;
	virtual void ConfigureMelee(UNoFearMeleeComponent* Melee) override;
	virtual bool WantsFreeAttack(AActor* Target) override;
	virtual void OnMeleeAttackFinished(AActor* Target) override;

private:
	enum class EPerchState : uint8
	{
		None,
		Approach,
		Climb,
		Perched
	};

	/** Враг с оружием в руках, который сейчас целится в прыгуна. Возвращает направление его прицела. */
	bool FindAimingThreat(FVector& OutAimDirection) const;

	UFUNCTION()
	void HandleAttackHit(AActor* Victim, float Damage);

	UFUNCTION()
	void HandleDamaged(UNoFearHealthComponent* Component, const FNoFearDamageInfo& Info);

	/** Раз в 0.05 с, пока лезет, сидит на насесте или держит жертву. */
	void TickCatcher();
	void StartCatcherTimer();

	/** Уступ над Target: верх (ноги на нём), подножие стены у моей стороны и край. */
	bool FindPerch(const AActor* Target, FVector& OutTop, FVector& OutFoot) const;
	void TickApproach(double Now);
	void TickClimb(double Now);
	void TickPerched(double Now);
	void EndPerch(bool bFailed);

	void BeginPin(ACharacter* Victim);
	void HoldPin(double Now);
	void ReleasePin();

	void PauseBrain();
	void ResumeBrain();
	void Stun(const FVector& From, float Time);
	bool IsPounceReady() const;

	int32 DodgeCount = 0;
	double NextDodgeTime = 0.0;

	/** Решил прыгать — держит решение до конца атаки. */
	bool bLeapCommitted = false;
	double NextLeapTime = 0.0;
	double WaitStartTime = -1.0;
	TWeakObjectPtr<AActor> WaitTarget;
	int32 OpportunityLeaps = 0;
	int32 ImpatientLeaps = 0;
	int32 CoverLeaps = 0;

	TWeakObjectPtr<UNoFearHealthComponent> BoundHealth;
	FTimerHandle CatcherTimer;
	/** Дерево на паузе — поставил ловчий (насест, прыжок, прижатие). */
	bool bLogicPausedByCatcher = false;

	EPerchState PerchState = EPerchState::None;
	FVector PerchTop = FVector::ZeroVector;
	FVector PerchFoot = FVector::ZeroVector;
	double PerchStateTime = 0.0;
	double NextPerchSearchTime = 0.0;
	double NextHopTime = 0.0;
	bool bOverHopped = false;
	double NoSightSince = -1.0;
	int32 PerchCount = 0;
	int32 PounceCount = 0;
	bool bPouncing = false;
	bool bKnockedFromAir = false;

	TWeakObjectPtr<ACharacter> PinVictim;
	FVector PinSide = FVector::ForwardVector;
	double NextPinDamageTime = 0.0;
	float PinDamageTaken = 0.f;
	bool bCrouchedByPin = false;
	bool bVictimFireWasBlocked = false;
	TArray<double> EscapePresses;
	int32 PinCount = 0;
	int32 KnockOffCount = 0;
	int32 EscapeCount = 0;
	int32 FlightKnockCount = 0;

	static TArray<TWeakObjectPtr<ANoFearJumperAIController>> ActivePinners;
};

/**
 * Плевун: держится на 800–1500 см и плюёт по дуге кислотой (UNoFearMonsterAttack_Spit): снаряд ранит при попадании
 * и оставляет лужу, которая жжёт стоящих в ней. Не стоит в очереди за жетоном и в кольцо стаи не встаёт: пока стая
 * окружает цель, стоит за кольцом со своей стороны (StandDistance) и прижимает цель огнём; между плевками
 * перебирается на своё место. Есть союзник у цели (колосс в первую очередь) — встаёт за его спину (ScreenOffset).
 * Цель ближе RetreatStartDistance — отступает (ключ KeepAway, ветка дерева «Отступить»); прижали — отбивается слабым ударом.
 * Цель за укрытием — плюёт навесом. Прямое попадание облепляет слизью (замедление), в голову — слепит.
 * Мешок (neck_01) пробит на замахе — лопается: плевок сорван, под плевуном лужа (жжёт людей, что подошли вплотную), SacPopDamage урона
 * и оглушение SacPopStunTime.
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

	/** Встаёт за союзником, который ближе к цели, на столько дальше него, см. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|Monster|Spitter", meta = (ClampMin = "0"))
	float ScreenOffset = 350.f;

	/** Лопнувший мешок: урон себе. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|Monster|Spitter|Sac", meta = (ClampMin = "0"))
	float SacPopDamage = 20.f;

	/** Лопнувший мешок: оглушение, с. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|Monster|Spitter|Sac", meta = (ClampMin = "0"))
	float SacPopStunTime = 1.5f;

	/** Лужа под лопнувшим мешком. Пусто — берётся лужа снаряда плевка. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|Monster|Spitter|Sac")
	TSubclassOf<class ANoFearAcidPuddle> SacPuddleClass;

	/** Сколько раз лопался мешок (для тестов). */
	UFUNCTION(BlueprintPure, Category = "NoFear|Monster|Spitter")
	int32 GetSacPopCount() const { return SacPopCount; }

	/** За кем встал в последний раз (для тестов). */
	UFUNCTION(BlueprintPure, Category = "NoFear|Monster|Spitter")
	APawn* GetScreenAlly() const { return ScreenAlly.Get(); }

	virtual bool GetStandoffPoint(const FVector& TargetLocation, const FVector& PackSide, FVector& OutPoint) const override;

protected:
	virtual void OnPossess(APawn* InPawn) override;
	virtual void OnUnPossess() override;
	virtual void UpdateBot() override;
	virtual void WriteBlackboard() override;
	virtual void ConfigureMelee(UNoFearMeleeComponent* Melee) override;
	virtual bool WantsFreeAttack(AActor* Target) override;
	virtual void OnMeleeAttackFinished(AActor* Target) override;

private:
	UFUNCTION()
	void HandleDamaged(UNoFearHealthComponent* Component, const FNoFearDamageInfo& Info);
	void PopSac(const FNoFearDamageInfo& Info);

	bool bKeepingAway = false;
	bool bPoppingSac = false;
	int32 RetreatCount = 0;
	int32 SacPopCount = 0;
	double LastAttackTime = -1.0e9;
	TWeakObjectPtr<UNoFearHealthComponent> BoundHealth;
	mutable TWeakObjectPtr<APawn> ScreenAlly;
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

class UMaterialInterface;
class UMaterialInstanceDynamic;

/** Что сейчас делает тень. */
UENUM(BlueprintType)
enum class ENoFearShadeState : uint8
{
	/** Кружит вокруг группы и ждёт отставшего, бросает ложные звуки. */
	Stalk,
	/** Заходит отставшему за спину по дуге. */
	Approach,
	/** Замах и удар. */
	Strike,
	/** Ударила — уходит, полностью невидима. */
	Retreat,
	/** Потеряла много здоровья — бежит и прячется. */
	Flee
};

/**
 * Тень (раздел 4.7 Docs/Monsters_Design.md): полуневидимый охотник на отставших.
 * - Плащ: материал M_NoFear_ShadeCloak на всех слотах тела, прозрачность MovingOpacity в движении, StandingOpacity стоя.
 *   Проявляется (RevealedOpacity) за StrikeWindup до удара с шипением, на RevealOnHurtTime после ранения и ближе
 *   RevealHumanDistance к человеку.
 * - Восприятие: боты-люди видят её только ближе CloakedSightRange (проявленную — как обычно, исчезнувшую — никак),
 *   слышат как обычно (шаги тише в FootstepScale раз), пули попадают всегда.
 * - Охота: жертва — человек, у которого ближайший союзник дальше IsolatedDistance (дальше лучше, вооружённые
 *   интереснее). Нет такой — кружит в StalkMinDistance–StalkMaxDistance от группы и ждёт.
 * - Удар и отход: заходит со спины по дуге, бьёт на StrikeDamage; удар в спину сбивает с ног на KnockDownTime.
 *   После удара уходит на RetreatDistance и VanishTime полностью невидима (не проявляется и вплотную).
 *   Потеряла больше FleeHealthFraction здоровья за FleeWindow — бежит и прячется на FleeHideTime.
 * - Ложные звуки раз в FakeSoundInterval: крик, шаги или выстрел в FakeSoundDistance от группы, в стороне от тени.
 *   Боты-люди в FakeSoundHearing идут проверить (CheckNoise; из отряда — один боец, PickNoiseChecker), герой
 *   слышит звук без меток.
 * - Слабые места: голова ×2, пока проявлена (зона RevealedHead); спина ×1.5 на отходе (RetreatBack).
 * - В стаю не входит, своим не враждебна.
 * Тело: CBP_Bot_Shade.
 */
UCLASS(Blueprintable)
class NOFEAR_API ANoFearShadeAIController : public ANoFearMonsterAIController
{
	GENERATED_BODY()

public:
	ANoFearShadeAIController(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	// --- Плащ ---

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|Shade|Cloak")
	TSoftObjectPtr<UMaterialInterface> CloakMaterial;

	/** Непрозрачность в движении (85% прозрачности). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|Shade|Cloak", meta = (ClampMin = "0", ClampMax = "1"))
	float MovingOpacity = 0.15f;

	/** Непрозрачность стоя (95% прозрачности). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|Shade|Cloak", meta = (ClampMin = "0", ClampMax = "1"))
	float StandingOpacity = 0.05f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|Shade|Cloak", meta = (ClampMin = "0", ClampMax = "1"))
	float RevealedOpacity = 1.f;

	/** Скорость смены непрозрачности, долей в секунду. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|Shade|Cloak", meta = (ClampMin = "0.1"))
	float OpacityFadeSpeed = 5.f;

	/** Проявляется после ранения на столько секунд. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|Shade|Cloak", meta = (ClampMin = "0"))
	float RevealOnHurtTime = 3.f;

	/** Проявляется, если человек ближе, см. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|Shade|Cloak", meta = (ClampMin = "0"))
	float RevealHumanDistance = 250.f;

	/** Боты видят непроявленную тень только ближе, см. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|Shade|Cloak", meta = (ClampMin = "0"))
	float CloakedSightRange = 400.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|Shade|Cloak")
	TSoftObjectPtr<USoundBase> HissSound;

	// --- Тело ---

	/** Скорость бега, см/с (тело ускоряется временем, как колосс в ярости). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|Shade|Body", meta = (ClampMin = "0"))
	float RunSpeed = 650.f;

	/** Шаги тише во столько раз. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|Shade|Body", meta = (ClampMin = "0", ClampMax = "1"))
	float FootstepScale = 0.2f;

	// --- Охота ---

	/** Отставший: ближайший союзник дальше, см. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|Shade|Hunt", meta = (ClampMin = "0"))
	float IsolatedDistance = 1200.f;

	/** Жертва перестаёт быть отставшей, если союзник ближе, см (чтобы не дёргаться на границе). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|Shade|Hunt", meta = (ClampMin = "0"))
	float RejoinedDistance = 900.f;

	/** Вооружённая жертва интереснее: прибавка к расстоянию до союзника при выборе, см. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|Shade|Hunt", meta = (ClampMin = "0"))
	float ArmedBonus = 400.f;

	/** Видит людей не дальше, см. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|Shade|Hunt", meta = (ClampMin = "0"))
	float HuntRange = 5000.f;

	/** Кружит вокруг группы в этом коридоре, см. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|Shade|Hunt", meta = (ClampMin = "0"))
	float StalkMinDistance = 1500.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|Shade|Hunt", meta = (ClampMin = "0"))
	float StalkMaxDistance = 2500.f;

	/** Скорость обхода группы, градусов в секунду. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|Shade|Hunt", meta = (ClampMin = "0"))
	float StalkOrbitSpeed = 8.f;

	/** Группа: люди ближе этого к ближайшему, см. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|Shade|Hunt", meta = (ClampMin = "0"))
	float GroupRadius = 2000.f;

	// --- Удар ---

	/** Заходит в эту точку за спиной жертвы, см. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|Shade|Strike", meta = (ClampMin = "0"))
	float BehindDistance = 150.f;

	/** Обходит жертву по дуге такого радиуса, см. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|Shade|Strike", meta = (ClampMin = "0"))
	float ArcRadius = 700.f;

	/** Бьёт с такого расстояния, см. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|Shade|Strike", meta = (ClampMin = "0"))
	float StrikeRange = 220.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|Shade|Strike", meta = (ClampMin = "0"))
	float StrikeDamage = 45.f;

	/** Замах: столько тень видна до удара, с. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|Shade|Strike", meta = (ClampMin = "0"))
	float StrikeWindup = 0.4f;

	/** Удар в спину сбивает с ног на столько, с. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|Shade|Strike", meta = (ClampMin = "0"))
	float KnockDownTime = 1.2f;

	/** Спина: удар пришёл из этого полуугла позади жертвы, градусов. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|Shade|Strike", meta = (ClampMin = "0", ClampMax = "180"))
	float BackHalfAngle = 70.f;

	/** Подходит дольше — бьёт откуда есть, с. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|Shade|Strike", meta = (ClampMin = "0"))
	float ApproachPatience = 12.f;

	/** Пауза между ударами, с. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|Shade|Strike", meta = (ClampMin = "0"))
	float StrikeCooldown = 2.f;

	// --- Отход и бегство ---

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|Shade|Retreat", meta = (ClampMin = "0"))
	float RetreatDistance = 1500.f;

	/** После удара полностью невидима, с. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|Shade|Retreat", meta = (ClampMin = "0"))
	float VanishTime = 6.f;

	/** Потеряла больше этой доли здоровья за FleeWindow — бегство. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|Shade|Retreat", meta = (ClampMin = "0", ClampMax = "1"))
	float FleeHealthFraction = 0.3f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|Shade|Retreat", meta = (ClampMin = "0"))
	float FleeWindow = 2.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|Shade|Retreat", meta = (ClampMin = "0"))
	float FleeHideTime = 10.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|Shade|Retreat", meta = (ClampMin = "0"))
	float FleeDistance = 2500.f;

	// --- Ложные звуки ---

	/** Раз в столько секунд (случайно в диапазоне). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|Shade|Decoy")
	FVector2D FakeSoundInterval = FVector2D(12.f, 20.f);

	/** Звук от центра группы, см. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|Shade|Decoy")
	FVector2D FakeSoundDistance = FVector2D(1000.f, 1800.f);

	/** Боты слышат ложный звук в этом радиусе, см. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|Shade|Decoy", meta = (ClampMin = "0"))
	float FakeSoundHearing = 3000.f;

	/** Сколько боец стоит на месте звука, осматриваясь, с. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|Shade|Decoy", meta = (ClampMin = "0"))
	float FakeSoundCheckTime = 3.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|Shade|Decoy")
	TSoftObjectPtr<USoundBase> CrySound;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|Shade|Decoy", meta = (ClampMin = "0.1"))
	float CryPitch = 1.8f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|Shade|Decoy")
	TSoftObjectPtr<USoundBase> StepsSound;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|Shade|Decoy")
	TSoftObjectPtr<USoundBase> ShotSound;

	// --- Состояние (для тестов) ---

	UFUNCTION(BlueprintPure, Category = "NoFear|Shade")
	ENoFearShadeState GetShadeState() const { return State; }

	UFUNCTION(BlueprintPure, Category = "NoFear|Shade")
	AActor* GetVictim() const { return Victim.Get(); }

	/** Проявлена: замах, ранена или человек рядом (и не исчезла). */
	UFUNCTION(BlueprintPure, Category = "NoFear|Shade")
	bool IsRevealed() const;

	/** Полностью невидима: отход или бегство. */
	UFUNCTION(BlueprintPure, Category = "NoFear|Shade")
	bool IsVanished() const;

	UFUNCTION(BlueprintPure, Category = "NoFear|Shade")
	float GetOpacity() const { return Opacity; }

	UFUNCTION(BlueprintPure, Category = "NoFear|Shade")
	int32 GetStrikeCount() const { return StrikeCount; }

	UFUNCTION(BlueprintPure, Category = "NoFear|Shade")
	int32 GetKnockDownCount() const { return KnockDownCount; }

	UFUNCTION(BlueprintPure, Category = "NoFear|Shade")
	int32 GetFleeCount() const { return FleeCount; }

	UFUNCTION(BlueprintPure, Category = "NoFear|Shade")
	int32 GetFakeSoundCount() const { return FakeSoundCount; }

	UFUNCTION(BlueprintPure, Category = "NoFear|Shade")
	FVector GetLastFakeSoundLocation() const { return LastFakeSoundLocation; }

	/** Сколько ботов пошло проверять последний ложный звук. */
	UFUNCTION(BlueprintPure, Category = "NoFear|Shade")
	int32 GetLastFakeSoundCheckers() const { return LastFakeSoundCheckers; }

	/** Ложный звук сейчас: 0 — крик, 1 — шаги, 2 — выстрел (отладка и тесты). */
	UFUNCTION(BlueprintCallable, Category = "NoFear|Shade")
	void PlayFakeSound(FVector Location, int32 Kind);

	/** Ложный звук сейчас в обычном месте (отладка и тесты). Ложь — некому слушать. */
	UFUNCTION(BlueprintCallable, Category = "NoFear|Shade")
	bool PlayFakeSoundNow();

	/** Предел, дальше которого боты не видят Actor: <0 — без предела, 0 — не видят совсем. */
	static float GetSightLimit(const AActor* Actor);

	virtual void Roar() override {}
	virtual bool IsRingFighter() const override { return false; }

	virtual void Tick(float DeltaSeconds) override;

protected:
	virtual void OnPossess(APawn* InPawn) override;
	virtual void OnUnPossess() override;
	virtual void UpdateBot() override;
	virtual void UpdateMovementStance() override;
	virtual bool WantsFreeAttack(AActor* Target) override { return false; }
	virtual void ConfigureMelee(UNoFearMeleeComponent* Melee) override;

private:
	UFUNCTION()
	void HandleDamaged(UNoFearHealthComponent* Component, const FNoFearDamageInfo& Info);

	UFUNCTION()
	void HandleAttackHit(AActor* HitVictim, float Damage);

	void UpdateStalk(float DeltaTime);
	void UpdateApproach();
	void UpdateStrike();
	void UpdateHide();
	void UpdateCloak(float DeltaTime);
	void UpdateFakeSounds();
	void BeginStrike(AActor* Target);
	void BeginHide(ENoFearShadeState NewState, const FVector& From, float Distance, float Duration);
	void SetState(ENoFearShadeState NewState);
	/** Отставший, на кого охотиться (или nullptr). */
	APawn* FindIsolatedVictim() const;
	/** Расстояние от Human до ближайшего живого союзника (огромное, если союзников нет). */
	float GetNearestAllyDistance(const APawn* Human) const;
	/** Живые враждебные люди в HuntRange. */
	void GatherHumans(TArray<APawn*>& OutHumans) const;
	/** Центр группы людей вокруг ближайшего к тени. */
	bool GetGroupCenter(FVector& OutCenter) const;
	void MoveToward(const FVector& Goal);
	void ApplyOpacity();

	ENoFearShadeState State = ENoFearShadeState::Stalk;
	TWeakObjectPtr<AActor> Victim;
	TWeakObjectPtr<UNoFearHealthComponent> BoundHealth;
	TArray<TWeakObjectPtr<UMaterialInstanceDynamic>> CloakInstances;

	float Opacity = 0.05f;
	double StateStartTime = 0.0;
	double RevealUntil = 0.0;
	double HideUntil = 0.0;
	double NextStrikeTime = 0.0;
	double NextFakeSoundTime = 0.0;
	FVector HideGoal = FVector::ZeroVector;
	float OrbitAngle = 0.f;
	bool bHasOrbit = false;
	float ArcSign = 1.f;

	/** Урон за последние FleeWindow секунд: время и величина. */
	TArray<TPair<double, float>> RecentDamage;

	int32 StrikeCount = 0;
	int32 KnockDownCount = 0;
	int32 FleeCount = 0;
	int32 FakeSoundCount = 0;
	int32 LastFakeSoundCheckers = 0;
	FVector LastFakeSoundLocation = FVector::ZeroVector;

	double LastUpdateTime = -1.0;
};
