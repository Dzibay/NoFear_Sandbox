#pragma once

#include "CoreMinimal.h"
#include "AIController.h"
#include "AI/NoFearBotSubsystem.h"
#include "Core/NoFearHealthComponent.h"
#include "Perception/AIPerceptionTypes.h"
#include "NoFearAIController.generated.h"

class ANoFearAIController;
class ANoFearCharacter;
class UAISenseConfig_Hearing;
class UAISenseConfig_Sight;
class UBehaviorTree;
class UNoFearPoseComponent;

/** Имена ключей блэкборда BB_NoFearBot. Контроллеры и задачи пишут и читают их по этим именам. */
struct NOFEAR_API FNoFearBotKeys
{
	/** Object (Actor): текущая цель. */
	static const FName AttackTarget;
	/** Bool: цель видна (пишет UNoFearCombatComponent). */
	static const FName HasLineOfSight;
	/** Bool: цель видна и время реакции прошло (пишет UNoFearCombatComponent). */
	static const FName CanFire;
	/** Vector: где цель видели последний раз (пишет UNoFearCombatComponent). */
	static const FName LastKnownLocation;
	/** Vector: точка, которую бот охраняет и куда возвращается. */
	static const FName HomeLocation;
	/** Vector: куда идти сейчас (заполняют задачи поиска точки). */
	static const FName MoveGoal;
	/** Vector: что проверить (звук выстрела, место, где пропала цель). */
	static const FName InvestigateLocation;
	/** Object (Actor): за кем идёт напарник. */
	static const FName Leader;
	/** Bool: напарник следует за лидером. */
	static const FName IsFollowing;
	/** Int: ENoFearAlertState (0 спокоен, 1 насторожен, 2 бой, 3 поиск). */
	static const FName AlertState;
};

/** Уровень тревоги бота. Пишется в блэкборд (AlertState) и показывается значком над головой. */
UENUM(BlueprintType)
enum class ENoFearAlertState : uint8
{
	/** Ничего не происходит. */
	Calm,
	/** Шкала обнаружения перешла порог, услышал шум или нашёл труп: смотрит и идёт проверить. */
	Suspicious,
	/** Есть цель (или мирный понял, что рядом угроза). */
	Combat,
	/** Потерял цель и обыскивает точки вокруг её последнего места. */
	Search
};

/** Что бот знает о враге. */
struct FNoFearKnownActor
{
	FVector LastKnownLocation = FVector::ZeroVector;
	/** Куда двигался, когда его видели в последний раз (по этому направлению ищут). */
	FVector LastKnownVelocity = FVector::ZeroVector;
	double LastSeenTime = -1.0e9;
	double LastAttackedTime = -1.0e9;
	double LastHeardTime = -1.0e9;
	/** Шкала обнаружения 0..1. На 1 бот уверен, что перед ним враг, и может выбрать его целью. */
	float Awareness = 0.f;
	/** Враг в поле зрения (по данным зрения). */
	bool bInView = false;
	/** Враг в поле зрения и шкала полна: бот видит его. */
	bool bVisible = false;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FNoFearBotTargetChangedSignature, AActor*, NewTarget, AActor*, OldTarget);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FNoFearAlertStateChangedSignature, ANoFearAIController*, Bot, ENoFearAlertState, NewState, ENoFearAlertState, OldState);

/**
 * База всех ботов. Тело — Blueprint-наследник CBP_SandboxCharacter (команда, цвет, оружие),
 * поведение — этот контроллер и его Behavior Tree.
 *
 * Общее для всех: зрение и слух (видит только врагов по таблице NoFear Teams), память о врагах,
 * выбор цели (ближе, видимее, кто стрелял, текущая), реакция на урон, «дом» и поводок,
 * походка (шагом без цели, боком с прицелом в бою) и запись всего этого в блэкборд.
 *
 * Наследники: ANoFearCivilianAIController (мирный), ANoFearMonsterAIController (ближний бой),
 * ANoFearShooterAIController (враги и союзники), ANoFearCompanionAIController (напарник игрока).
 */
UCLASS(Blueprintable)
class NOFEAR_API ANoFearAIController : public AAIController
{
	GENERATED_BODY()

public:
	ANoFearAIController(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	UFUNCTION(BlueprintPure, Category = "NoFear|AI")
	ANoFearCharacter* GetNoFearCharacter() const;

	UFUNCTION(BlueprintPure, Category = "NoFear|AI")
	AActor* GetCurrentTarget() const { return CurrentTarget.Get(); }

	/** Цель сейчас в поле зрения. */
	UFUNCTION(BlueprintPure, Category = "NoFear|AI")
	virtual bool IsTargetVisible() const;

	/** Где бот последний раз видел текущую цель (видимую — где она сейчас). Без цели — своё положение. */
	UFUNCTION(BlueprintPure, Category = "NoFear|AI")
	FVector GetLastKnownTargetLocation() const;

	UFUNCTION(BlueprintPure, Category = "NoFear|AI")
	FVector GetHomeLocation() const { return HomeLocation; }

	/** Новая точка охраны. Бот вернётся туда, когда закончит бой. */
	UFUNCTION(BlueprintCallable, Category = "NoFear|AI")
	void SetHomeLocation(FVector NewHome);

	/** Вокруг какой точки бот держится в бою: дом, а у следующего напарника — лидер. */
	virtual FVector GetLeashAnchor() const { return HomeLocation; }

	/** Как далеко от GetLeashAnchor бот может уйти в бою. 0 — без поводка. */
	UFUNCTION(BlueprintPure, Category = "NoFear|AI")
	virtual float GetLeashRadius() const { return LeashRadius; }

	/** Сообщить боту о враге (как будто тот напал): бот запомнит его и, скорее всего, выберет целью. */
	UFUNCTION(BlueprintCallable, Category = "NoFear|AI")
	void ReportThreat(AActor* Threat);

	/** Выставить флаги CharacterInputState у тела GASP: боком, прицел, шагом. */
	UFUNCTION(BlueprintCallable, Category = "NoFear|AI")
	void SetMovementFlags(bool bStrafe, bool bAim, bool bWalk);

	/**
	 * Присесть / встать (ACharacter::Crouch). Присевший бот тише (шаги по CrouchFootstepRange) и труднее заметен,
	 * но медленнее. Само по себе поведение не приседает: вызывается из задач дерева или Blueprint.
	 */
	UFUNCTION(BlueprintCallable, Category = "NoFear|AI")
	void SetCrouching(bool bCrouch);

	UFUNCTION(BlueprintPure, Category = "NoFear|AI")
	bool IsCrouching() const;

	UPROPERTY(BlueprintAssignable, Category = "NoFear|AI")
	FNoFearBotTargetChangedSignature OnTargetChanged;

	UPROPERTY(BlueprintAssignable, Category = "NoFear|AI")
	FNoFearAlertStateChangedSignature OnAlertStateChanged;

	// --- Тревога ---

	UFUNCTION(BlueprintPure, Category = "NoFear|AI")
	ENoFearAlertState GetAlertState() const { return AlertState; }

	/** Шкала обнаружения этого актора 0..1 (1 — бот уверен, что видит врага). */
	UFUNCTION(BlueprintPure, Category = "NoFear|AI")
	float GetAwarenessOf(const AActor* Actor) const;

	/** Максимум шкалы по всем известным врагам. Значок «?» над головой заполняется по этому числу. */
	UFUNCTION(BlueprintPure, Category = "NoFear|AI")
	float GetTopAwareness() const;

	/** Сколько точек поиска осталось проверить. */
	UFUNCTION(BlueprintPure, Category = "NoFear|AI")
	int32 GetSearchPointCount() const { return SearchPoints.Num(); }

	/** Радиус, в котором бот зовёт на помощь своих. 0 — не зовёт. */
	UFUNCTION(BlueprintPure, Category = "NoFear|AI")
	float GetAlertShareRadius() const { return AlertShareRadius; }

	/** Шум. Выстрел выдаёт источник сразу, шаги и приземление только повышают подозрение. */
	UFUNCTION(BlueprintCallable, Category = "NoFear|AI")
	void HearNoise(AActor* Source, FVector Location, ENoFearNoiseType Type);

	/** Общая тревога от своего: бот узнаёт угрозу и её положение и идёт к ней как к цели, даже не видя её сам. */
	UFUNCTION(BlueprintCallable, Category = "NoFear|AI")
	void ReceiveAlarm(AActor* Threat, FVector ThreatLocation);

	// IGenericTeamAgentInterface: команда берётся у тела.
	virtual FGenericTeamId GetGenericTeamId() const override;

	/** Дерево поведения. Если пусто — бот только стоит. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|AI")
	TObjectPtr<UBehaviorTree> BehaviorTree;

	// --- Цели ---

	/** Выбирать цели и сражаться. У мирных выключено. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|AI|Targeting")
	bool bCanAttack = true;

	/** Как часто пересматривать цель, память и походку, сек. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|AI|Targeting", meta = (ClampMin = "0.05"))
	float UpdateInterval = 0.25f;

	/** Дальше этого цель не выбирается. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|AI|Targeting", meta = (ClampMin = "0"))
	float MaxTargetDistance = 5000.f;

	/** Сколько секунд помнить врага, которого больше не видно. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|AI|Targeting", meta = (ClampMin = "0"))
	float TargetMemoryTime = 6.f;

	/** Сколько секунд обидчик в приоритете при выборе цели. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|AI|Targeting", meta = (ClampMin = "0"))
	float AttackerMemoryTime = 6.f;

	/** Прибавка к оценке текущей цели, чтобы бот не метался между равными. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|AI|Targeting", meta = (ClampMin = "0"))
	float TargetStickiness = 0.35f;

	/** Цель пропала из памяти — пойти проверить, где её видели последней. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|AI|Targeting")
	bool bInvestigateLostTarget = true;

	/** Без цели услышал выстрел врага — пойти проверить. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|AI|Targeting")
	bool bInvestigateNoises = true;

	// --- Движение ---

	/** Без цели ходить шагом. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|AI|Movement")
	bool bWalkWhenCalm = true;

	/** Насторожен (Suspicious) и с оружием в руках: поднять оружие (поза прицеливания), пока цели ещё нет. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|AI|Movement")
	bool bRaiseWeaponWhenAlert = true;

	/** Сильный удар (UNoFearPoseComponent::Stagger) останавливает бота: дерево на паузе, шаги и стрельба прекращены. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|AI|Movement")
	bool bFreezeWhenStaggered = true;

	/** В бою смотреть на цель и двигаться боком (стрелки). Выключено — бежать лицом по ходу (монстры). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|AI|Movement")
	bool bStrafeInCombat = true;

	/** Дальше этого от дома бот в бою не уходит. 0 — без поводка. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|AI|Movement", meta = (ClampMin = "0"))
	float LeashRadius = 0.f;

	/** Имя структуры ввода у тела GASP (CBP_SandboxCharacter). */
	UPROPERTY(EditAnywhere, Category = "NoFear|AI|Movement")
	FName InputStateProperty = TEXT("CharacterInputState");

	// --- Обнаружение ---

	/** Общий множитель скорости заполнения шкалы обнаружения. 2 — бот замечает вдвое быстрее, 0 — слепой. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|AI|Detection", meta = (ClampMin = "0"))
	float DetectionSpeedMultiplier = 1.f;

	/** Ближе этого враг замечается за CloseDetectTime. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|AI|Detection", meta = (ClampMin = "0"))
	float CloseDetectDistance = 300.f;

	/** На этой дистанции враг замечается за MidDetectTime (дальше до радиуса зрения время растёт до FarDetectTime). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|AI|Detection", meta = (ClampMin = "0"))
	float MidDetectDistance = 1000.f;

	/** Секунды до полной шкалы вплотную. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|AI|Detection", meta = (ClampMin = "0.01"))
	float CloseDetectTime = 0.2f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|AI|Detection", meta = (ClampMin = "0.01"))
	float MidDetectTime = 1.5f;

	/** Секунды до полной шкалы на краю радиуса зрения. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|AI|Detection", meta = (ClampMin = "0.01"))
	float FarDetectTime = 5.f;

	/** В пределах этого угла от взгляда враг виден «в упор»; дальше до края зрения шкала растёт медленнее. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|AI|Detection", meta = (ClampMin = "0", ClampMax = "180"))
	float CenterViewAngle = 25.f;

	/** Во сколько раз медленнее шкала на самом краю бокового зрения. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|AI|Detection", meta = (ClampMin = "0", ClampMax = "1"))
	float PeripheralDetectFactor = 0.33f;

	/** Множитель скорости шкалы, если враг пригнулся. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|AI|Detection", meta = (ClampMin = "0"))
	float CrouchDetectFactor = 0.5f;

	/** Множитель скорости шкалы, если враг бежит (500 см/с); на ходу между 1 и этим числом. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|AI|Detection", meta = (ClampMin = "0"))
	float MovingDetectFactor = 2.f;

	/** Множитель скорости шкалы, если бот уже насторожен. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|AI|Detection", meta = (ClampMin = "0"))
	float SuspiciousRateMultiplier = 1.5f;

	/** С какого значения шкалы бот насторожен («?» над головой). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|AI|Detection", meta = (ClampMin = "0", ClampMax = "1"))
	float SuspiciousThreshold = 0.25f;

	/** Насколько падает неполная шкала за секунду, когда врага не видно. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|AI|Detection", meta = (ClampMin = "0"))
	float AwarenessDecayRate = 0.2f;

	/** Сколько секунд после последнего контакта шкала не падает. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|AI|Detection", meta = (ClampMin = "0"))
	float AwarenessDecayDelay = 1.f;

	/** Сколько секунд бот остаётся насторожен после подозрительного события. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|AI|Detection", meta = (ClampMin = "0"))
	float SuspiciousHoldTime = 6.f;

	/** Прибавка к шкале за один услышанный шаг. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|AI|Detection", meta = (ClampMin = "0"))
	float FootstepAwarenessGain = 0.3f;

	/** Выше этого значения шаги шкалу не поднимают (только увидеть). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|AI|Detection", meta = (ClampMin = "0", ClampMax = "1"))
	float FootstepAwarenessCap = 0.9f;

	/** Ближе этого шаги за спиной могут довести шкалу до 1. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|AI|Detection", meta = (ClampMin = "0"))
	float CloseFootstepDistance = 300.f;

	/** Выстрел невидимого стрелка поднимает шкалу минимум до этого значения (виден — до 1). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|AI|Detection", meta = (ClampMin = "0", ClampMax = "1"))
	float GunshotHiddenAwareness = 0.6f;

	// --- Поиск ---

	/** Сколько точек, кроме места, где пропала цель, обыскать. 0 — только место. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|AI|Search", meta = (ClampMin = "0", ClampMax = "6"))
	int32 SearchPointCount = 2;

	/** В каком радиусе от места потери выбирать точки поиска. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|AI|Search", meta = (ClampMin = "0"))
	float SearchRadius = 1500.f;

	/** Через сколько секунд поиск заканчивается в любом случае. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|AI|Search", meta = (ClampMin = "0"))
	float SearchDuration = 25.f;

	/** Сколько секунд стоять настороже после последней точки. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|AI|Search", meta = (ClampMin = "0"))
	float SearchLingerTime = 3.f;

	// --- Трупы и тревога ---

	/** Замечать тела своих и нейтральных (кроме врагов) и идти проверять. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|AI|Alarm")
	bool bReactToCorpses = false;

	/** Дальше этого тела не замечаются. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|AI|Alarm", meta = (ClampMin = "0"))
	float CorpseNoticeDistance = 1500.f;

	/** Пока бот видит цель, свои в этом радиусе узнают о ней. 0 — не зовёт на помощь. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|AI|Alarm", meta = (ClampMin = "0"))
	float AlertShareRadius = 0.f;

	/** Как часто повторять общую тревогу, сек. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|AI|Alarm", meta = (ClampMin = "0.5"))
	float AlarmInterval = 3.5f;

protected:
	virtual void OnPossess(APawn* InPawn) override;
	virtual void OnUnPossess() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** Раз в UpdateInterval: память, выбор цели, фокус, походка, блэкборд. */
	virtual void UpdateBot();

	/** Можно ли вообще атаковать этого актора (жив, враг, не дальше MaxTargetDistance). */
	virtual bool IsValidTarget(const AActor* Candidate) const;

	/** Чем больше, тем охотнее бот выберет цель. Отрицательное — не выбирать. */
	virtual float ScoreTarget(const AActor* Candidate, const FNoFearKnownActor& Knowledge) const;

	virtual void HandleTargetChanged(AActor* NewTarget, AActor* OldTarget);

	/** Флаги походки для текущего состояния. */
	virtual void UpdateMovementStance();
	virtual bool ShouldWalk() const;

	/** Свежие значения в блэкборд (дом, цель). Наследники дописывают свои. */
	virtual void WriteBlackboard();

	UFUNCTION()
	void HandlePerceptionUpdated(AActor* Actor, FAIStimulus Stimulus);

	UFUNCTION()
	void HandlePawnDamaged(UNoFearHealthComponent* Component, const FNoFearDamageInfo& Info);

	/** Реакция на попадание из UNoFearPoseComponent: замедление и (при сильном ударе) остановка. */
	UFUNCTION()
	void HandleHitReaction(UNoFearPoseComponent* Component, bool bStagger);

	void BeginStagger();
	/** Раз в 0.1 с, пока пошатнувшийся стоит: отпустить дерево, когда пошатывание закончилось. */
	void PollStagger();
	void ReleaseStagger();

	void SetTarget(AActor* NewTarget);
	void SetInvestigateLocation(const FVector& Location);
	void ForgetStaleActors();
	double GetNow() const;

	/** Шкала обнаружения: растёт у видимых врагов, падает у потерянных. Dt — секунды с прошлого обновления. */
	void UpdateAwareness(float DeltaTime);

	/** На сколько шкала растёт за секунду, пока враг в поле зрения (с учётом дистанции, угла, позы, укрытия). */
	virtual float ComputeDetectionRate(const AActor* Target, const FNoFearKnownActor& Knowledge) const;

	/** Доля цели, не закрытая укрытием: проверка голова/центр/ноги. */
	float ComputeExposure(const AActor* Target) const;

	void UpdateAlertState();
	void SetAlertState(ENoFearAlertState NewState);

	/** Что-то подозрительное в Location: бот насторожен, смотрит туда и (если можно) идёт проверить. */
	void NoteSuspicion(const FVector& Location, bool bInvestigate);

	/** Потерял цель: проверить место, где она пропала, и NumPoints точек рядом (по направлению её движения и за укрытиями). */
	void BeginSearch(const FVector& Origin, const FVector& Velocity, int32 NumPoints);
	void EndSearch();
	void PumpSearch();
	void GenerateSearchPoints(const FVector& Origin, const FVector& Velocity, int32 NumPoints);

	void ScanCorpses();
	void NoteCorpse(ANoFearCharacter* Corpse);
	void ShareAlarm();
	void DrawDebug() const;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "NoFear|AI")
	TObjectPtr<UAISenseConfig_Sight> SightConfig;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "NoFear|AI")
	TObjectPtr<UAISenseConfig_Hearing> HearingConfig;

	FVector HomeLocation = FVector::ZeroVector;
	TWeakObjectPtr<AActor> CurrentTarget;
	TMap<TWeakObjectPtr<AActor>, FNoFearKnownActor> KnownActors;

private:
	FTimerHandle UpdateTimer;
	FTimerHandle StaggerTimer;
	TWeakObjectPtr<UNoFearHealthComponent> BoundHealth;
	TWeakObjectPtr<UNoFearPoseComponent> BoundPose;
	/** Дерево поставлено на паузу пошатыванием (и только им). */
	bool bLogicPausedByStagger = false;

	ENoFearAlertState AlertState = ENoFearAlertState::Calm;

	double LastUpdateTime = 0.0;
	/** До какого времени бот насторожен после подозрительного события (шум, труп, шкала выше порога). */
	double SuspiciousUntil = -1.0e9;
	FVector SuspicionLocation = FVector::ZeroVector;
	/** Фокальная точка выставлена из-за подозрения (её надо снять, когда подозрение прошло). */
	bool bSuspicionFocus = false;

	bool bSearching = false;
	double SearchEndTime = 0.0;
	/** Когда закончить стоять на месте после последней точки (0 — таймер не запущен). */
	double SearchLingerUntil = 0.0;
	TArray<FVector> SearchPoints;

	double NextCorpseScanTime = 0.0;
	double NextAlarmTime = 0.0;
	TSet<TWeakObjectPtr<AActor>> NoticedCorpses;
};
