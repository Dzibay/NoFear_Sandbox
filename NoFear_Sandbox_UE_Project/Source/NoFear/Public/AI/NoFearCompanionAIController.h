#pragma once

#include "CoreMinimal.h"
#include "AI/NoFearShooterAIController.h"
#include "NoFearCompanionAIController.generated.h"

class ANoFearCompanionAIController;
class ANoFearPlayerController;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FNoFearCompanionStateChangedSignature, ANoFearCompanionAIController*, Companion);

/** Приказ напарнику (колесо на F, ANoFearPlayerController). */
UENUM(BlueprintType)
enum class ENoFearCompanionOrder : uint8
{
	None,
	/** «За мной». */
	Follow,
	/** «Иди туда»: дойти до точки под прицелом и ждать там. */
	MoveTo,
	/** «Атакуй цель»: враг под прицелом. */
	Attack,
	/** «Граната туда»: подойти на бросок и бросить в точку под прицелом. */
	Grenade,
	/** «Жди здесь». */
	Hold,
	/** «Огонь по готовности» / «Не стрелять». */
	ToggleFire,
	/** «Снабжение»: принести магазин или бинт, что нужнее. */
	Supply,
	/** «Ко мне»: бегом к игроку, потом за ним. */
	ComeHere,
};

/** Чем напарник занят (для HUD). */
UENUM(BlueprintType)
enum class ENoFearCompanionActivity : uint8
{
	Idle,
	Following,
	Waiting,
	Fighting,
	Sneaking,
	Moving,
	ComingBack,
	Attacking,
	ThrowingGrenade,
	Supplying,
	Reviving,
	Dodging,
	Downed,
};

/** Враг, которого напарник отметил для игрока. */
struct FNoFearCompanionMark
{
	TWeakObjectPtr<AActor> Actor;
	FVector Location = FVector::ZeroVector;
	double Until = 0.0;
	/** Цель приказа «Атакуй». */
	bool bOrdered = false;
};

/**
 * Напарник. Пока не завербован, ведёт себя как союзник: держит точку и стреляет по врагам.
 * Игрок подходит и жмёт E (UNoFearRecruitableComponent) — напарник идёт за ним и дерётся рядом,
 * не отходя от лидера дальше Follow Leash Radius. Повторное E — «Жди здесь» / «За мной».
 * Список напарников и их лимит держит ANoFearPlayerController. Дерево то же, что у стрелков: BT_NoFear_Soldier.
 *
 * Завербованный:
 * - исполняет приказы с колеса (IssueOrder); куда идти мимо боя — RallyLocation (ветка «По приказу» выше боя);
 * - в бою ищет укрытие рядом с игроком, сбоку или позади него, и уходит с линии огня игрока;
 * - помогает с целями игрока: кто стреляет в игрока и в кого стреляет игрок, тот важнее;
 * - игрок пригнулся и боя нет — тоже пригибается, идёт шагом, говорит шёпотом и не стреляет первым;
 * - отмечает врагов, которых игрок не видит, и говорит по рации (контакт, перезарядка, готов, чисто);
 * - сам приносит магазины и бинты из своего запаса (пополняется со временем);
 * - падает раненым, пока лидер на ногах (UNoFearHealthComponent::bCanBeDowned), и поднимает упавшего игрока.
 */
UCLASS(Blueprintable)
class NOFEAR_API ANoFearCompanionAIController : public ANoFearShooterAIController
{
	GENERATED_BODY()

public:
	ANoFearCompanionAIController(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	/** Новый лидер (контроллер игрока). nullptr — отпустить: напарник остаётся союзником на месте. */
	UFUNCTION(BlueprintCallable, Category = "NoFear|AI|Companion")
	void SetLeader(AController* NewLeader);

	UFUNCTION(BlueprintPure, Category = "NoFear|AI|Companion")
	AController* GetLeader() const { return Leader.Get(); }

	/** Пешка лидера, если он жив (упавший раненым тоже жив). */
	UFUNCTION(BlueprintPure, Category = "NoFear|AI|Companion")
	APawn* GetLeaderPawn() const;

	UFUNCTION(BlueprintPure, Category = "NoFear|AI|Companion")
	bool IsRecruited() const { return Leader.IsValid(); }

	/** true — идти за лидером, false — ждать на текущем месте. */
	UFUNCTION(BlueprintCallable, Category = "NoFear|AI|Companion")
	void SetFollowing(bool bNewFollowing);

	UFUNCTION(BlueprintPure, Category = "NoFear|AI|Companion")
	bool IsFollowing() const { return bFollowing && Leader.IsValid(); }

	/** Завербован, отпущен, остался ждать, пошёл следом, получил приказ, упал или поднялся. */
	UPROPERTY(BlueprintAssignable, Category = "NoFear|AI|Companion")
	FNoFearCompanionStateChangedSignature OnCompanionStateChanged;

	virtual FVector GetLeashAnchor() const override;
	virtual float GetLeashRadius() const override;
	virtual void HandleDanger(ENoFearDangerType Type, AActor* Source, FVector ThreatLocation) override;

	// --- Приказы ---

	/**
	 * Приказ с колеса. Point — точка под прицелом игрока, Target — враг под прицелом (для «Атакуй»).
	 * false — приказ не принят (напарник ответит почему).
	 */
	UFUNCTION(BlueprintCallable, Category = "NoFear|AI|Companion")
	bool IssueOrder(ENoFearCompanionOrder Order, FVector Point, AActor* Target);

	/** Приказ, который напарник исполняет сейчас (MoveTo, Attack, Grenade, ComeHere), иначе None. */
	UFUNCTION(BlueprintPure, Category = "NoFear|AI|Companion")
	ENoFearCompanionOrder GetActiveOrder() const { return ActiveOrder; }

	/** Точка для маркера приказа: куда идёт, куда бросит гранату, кого атакует, где ждёт. false — точки нет. */
	bool GetOrderPoint(FVector& OutPoint) const;

	/** Цель приказа «Атакуй». */
	UFUNCTION(BlueprintPure, Category = "NoFear|AI|Companion")
	AActor* GetOrderTarget() const { return OrderTarget.Get(); }

	/** «Не стрелять»: огонь только в ответ (по нему или по игроку стреляли) и по цели приказа. */
	UFUNCTION(BlueprintPure, Category = "NoFear|AI|Companion")
	bool IsHoldingFire() const { return bHoldFire; }

	/** Крадётся следом за пригнувшимся игроком. */
	UFUNCTION(BlueprintPure, Category = "NoFear|AI|Companion")
	bool IsSneaking() const { return bSneaking; }

	UFUNCTION(BlueprintPure, Category = "NoFear|AI|Companion")
	ENoFearCompanionActivity GetActivity() const;

	/** Поднимает раненого лидера: доля от ReviveTime (0 — не поднимает). */
	UFUNCTION(BlueprintPure, Category = "NoFear|AI|Companion")
	float GetLeaderReviveProgress() const { return bReviving ? FMath::Clamp(ReviveProgress / FMath::Max(ReviveTime, 0.1f), 0.f, 1.f) : 0.f; }

	UFUNCTION(BlueprintPure, Category = "NoFear|AI|Companion")
	int32 GetSupplyMagazines() const { return SupplyMagazines; }

	UFUNCTION(BlueprintPure, Category = "NoFear|AI|Companion")
	int32 GetSupplyBandages() const { return SupplyBandages; }

	/** Враги, отмеченные для игрока. */
	const TArray<FNoFearCompanionMark>& GetMarks() const { return Marks; }

	UFUNCTION(BlueprintPure, Category = "NoFear|AI|Companion")
	int32 GetMarkCount() const { return Marks.Num(); }

	// --- Настройки ---

	/** В бою при следовании не отходить от лидера дальше этого. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|AI|Companion", meta = (ClampMin = "0"))
	float FollowLeashRadius = 900.f;

	/** Отстал от лидера дальше этого — бросает цель и догоняет. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|AI|Companion", meta = (ClampMin = "0"))
	float BreakOffDistance = 1600.f;

	/** Дальше этого от лидера бежит, ближе — подстраивается под его шаг. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|AI|Companion", meta = (ClampMin = "0"))
	float RunToLeaderDistance = 600.f;

	/** Лидер быстрее этого (см/с) — напарник тоже бежит. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|AI|Companion", meta = (ClampMin = "0"))
	float LeaderRunSpeed = 300.f;

	/** Если у тела нет UNoFearRecruitableComponent, добавить его, чтобы напарника можно было завербовать. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|AI|Companion")
	bool bAddRecruitableComponentIfMissing = true;

	/** Укрытие в бою — не дальше этого от игрока, сбоку или позади него, см. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|AI|Companion", meta = (ClampMin = "200"))
	float NearLeaderCoverRadius = 900.f;

	/** Напарник ближе этого к линии прицела игрока — отходит в сторону, см. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|AI|Companion", meta = (ClampMin = "0"))
	float FireLaneRadius = 80.f;

	/** Дальше этого от камеры на линии прицела не мешает, см. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|AI|Companion", meta = (ClampMin = "0"))
	float FireLaneMaxDistance = 3000.f;

	/** Сколько держится отметка врага, которого игрок не видит, с. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|AI|Companion", meta = (ClampMin = "0"))
	float MarkTime = 6.f;

	/** Цель приказа «Атакуй» важнее остальных на столько. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|AI|Companion", meta = (ClampMin = "0"))
	float OrderedTargetBonus = 5.f;

	/** Тот, в кого игрок попал за последние 5 с, важнее на столько. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|AI|Companion", meta = (ClampMin = "0"))
	float LeaderTargetBonus = 0.8f;

	/** Тот, кто стрелял в игрока за последние 6 с, важнее на столько. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|AI|Companion", meta = (ClampMin = "0"))
	float LeaderAttackerBonus = 1.f;

	/** Не чаще одной реплики за столько секунд (важные — без очереди). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|AI|Companion", meta = (ClampMin = "0"))
	float BarkGap = 1.5f;

	/** Сколько стоять рядом с упавшим лидером, чтобы поднять, с. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|AI|Companion|Revive", meta = (ClampMin = "0.1"))
	float ReviveTime = 3.f;

	/** Ближе этого к упавшему лидеру — можно поднимать, см. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|AI|Companion|Revive", meta = (ClampMin = "50"))
	float ReviveDistance = 150.f;

	/** Запас магазинов для игрока (один магазин его оружия за раз). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|AI|Companion|Supply", meta = (ClampMin = "0"))
	int32 MaxSupplyMagazines = 3;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|AI|Companion|Supply", meta = (ClampMin = "0"))
	int32 MaxSupplyBandages = 2;

	/** За сколько секунд в запасе прибавляется магазин. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|AI|Companion|Supply", meta = (ClampMin = "1"))
	float MagazineRefillTime = 40.f;

	/** За сколько секунд в запасе прибавляется бинт. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|AI|Companion|Supply", meta = (ClampMin = "1"))
	float BandageRefillTime = 60.f;

	/** Сколько здоровья даёт бинт. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|AI|Companion|Supply", meta = (ClampMin = "0"))
	float BandageHeal = 40.f;

	/** Здоровье игрока ниже этой доли — напарник сам несёт бинт. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|AI|Companion|Supply", meta = (ClampMin = "0", ClampMax = "1"))
	float SupplyHealthFraction = 0.4f;

	/** Пауза между тем, как напарник сам что-то принёс, с. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|AI|Companion|Supply", meta = (ClampMin = "0"))
	float SupplyCooldown = 8.f;

	/** Ближе этого к игроку — отдаёт магазин или бинт, см. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|AI|Companion|Supply", meta = (ClampMin = "50"))
	float SupplyGiveDistance = 200.f;

protected:
	virtual void OnPossess(APawn* InPawn) override;
	virtual void OnUnPossess() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void UpdateBot() override;
	virtual bool IsValidTarget(const AActor* Candidate) const override;
	virtual float ScoreTarget(const AActor* Candidate, const FNoFearKnownActor& Knowledge) const override;
	virtual bool ShouldWalk() const override;
	virtual void WriteBlackboard() override;
	virtual void MakeCoverSearch(bool bRetreat, AActor* Threat, const FVector& ThreatLocation, FNoFearCoverSearch& OutSearch) const override;
	virtual bool CanInvestigateLocation(const FVector& Location) const override;

	float GetDistanceToLeader() const;
	ANoFearPlayerController* GetLeaderController() const;

	/** Сказать вслух и по рации лидеру. Key — своя пауза Cooldown; bForce — без общей паузы BarkGap. */
	bool Say(FName Key, const FText& Text, float Cooldown, bool bForce = false);

	TWeakObjectPtr<AController> Leader;
	bool bFollowing = false;

private:
	void BindLeader();
	void UnbindLeader();
	void ReleaseHelperReservations();

	void UpdateDowned();
	void RecoverFromDowned();
	void UpdateSupplyStock(double Now);
	void UpdateStealth();
	void StopReviving();

	/** Свои дела мимо боя, по важности: поднять лидера, приказ, снабжение, уйти с линии огня. true — есть куда идти. */
	bool UpdateReviveLeader(float DeltaTime, FVector& OutGoal);
	bool UpdateOrderMove(FVector& OutGoal);
	bool UpdateSupply(FVector& OutGoal);
	bool UpdateFireLane(FVector& OutGoal);

	void UpdateMarksAndCallouts();
	void UpdateCombatBarks();
	void AddMark(AActor* Actor, bool bOrdered);
	void ClearOrder();

	/** Что принести: 1 — магазин, 2 — бинт, 0 — ничего. bOrdered — по приказу (хватит и малой нужды). */
	int32 PickSupply(bool bOrdered) const;
	bool GiveSupply(int32 Kind);

	/** По нему или по лидеру недавно стреляли, а если крадёмся — лидер сам открыл огонь. */
	bool IsProvoked(const AActor* Candidate) const;
	/** Игрок видит актёра: в поле зрения камеры и ничто не загораживает. */
	bool DoesLeaderSee(const AActor* Actor) const;
	/** «справа, 25 м» относительно взгляда игрока. */
	FString DescribeFromLeader(const FVector& Location) const;
	double GetLeaderLastFireTime() const;

	UFUNCTION()
	void HandleLeaderDamaged(UNoFearHealthComponent* Component, const FNoFearDamageInfo& Info);

	UFUNCTION()
	void HandleLeaderHitConfirmed(AActor* Victim, float Damage, bool bHeadshot, bool bKilled);

	UFUNCTION()
	void HandleSelfDowned(UNoFearHealthComponent* Component, const FNoFearDamageInfo& Info);

	UFUNCTION()
	void HandleSelfRevived(UNoFearHealthComponent* Component, AActor* Reviver);

	TWeakObjectPtr<ANoFearPlayerController> BoundLeaderController;
	TWeakObjectPtr<UNoFearHealthComponent> BoundLeaderHealth;
	TWeakObjectPtr<UNoFearHealthComponent> BoundSelfHealth;
	TWeakObjectPtr<APawn> ReservedLeaderPawn;

	double LastCompanionUpdate = 0.0;

	// Приказ.
	ENoFearCompanionOrder ActiveOrder = ENoFearCompanionOrder::None;
	FVector OrderPoint = FVector::ZeroVector;
	TWeakObjectPtr<AActor> OrderTarget;
	double OrderUntil = 0.0;
	bool bHoldFire = false;

	// Куда идти мимо боя (RallyLocation).
	bool bHasCompanionGoal = false;
	FVector CompanionGoal = FVector::ZeroVector;

	// Скрытность.
	bool bSneaking = false;
	bool bCrouchedForStealth = false;
	bool bDefaultThrowGrenades = true;

	// Ранение: своё и лидера.
	bool bDowned = false;
	double NextDownedBarkTime = 0.0;
	bool bLeaderDownNoticed = false;
	bool bReviving = false;
	float ReviveProgress = 0.f;

	// Снабжение.
	int32 SupplyMagazines = 0;
	int32 SupplyBandages = 0;
	double NextMagazineRefill = 0.0;
	double NextBandageRefill = 0.0;
	int32 SupplyKind = 0;
	double SupplyUntil = 0.0;
	double NextSupplyTime = 0.0;

	// Линия огня игрока.
	FVector LaneGoal = FVector::ZeroVector;
	double LaneUntil = 0.0;
	double LaneReservationUntil = 0.0;

	// Помощь с целями игрока.
	TWeakObjectPtr<AActor> LeaderHitTarget;
	double LeaderHitTime = -1.0e9;
	TWeakObjectPtr<AActor> LeaderKilledActor;
	double LeaderKillTime = -1.0e9;
	TMap<TWeakObjectPtr<AActor>, double> LeaderAttackers;

	// Отметки и реплики.
	TArray<FNoFearCompanionMark> Marks;
	TMap<TWeakObjectPtr<AActor>, double> CalledOut;
	TMap<FName, double> NextBarkTimes;
	double LastBarkTime = -1.0e9;
	TWeakObjectPtr<AActor> LastFightTarget;
	ENoFearAlertState LastAlertState = ENoFearAlertState::Calm;
	bool bWasReloading = false;
};
