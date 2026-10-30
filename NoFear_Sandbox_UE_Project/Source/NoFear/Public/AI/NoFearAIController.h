#pragma once

#include "CoreMinimal.h"
#include "AIController.h"
#include "Core/NoFearHealthComponent.h"
#include "Perception/AIPerceptionTypes.h"
#include "NoFearAIController.generated.h"

class ANoFearCharacter;
class UAISenseConfig_Hearing;
class UAISenseConfig_Sight;
class UBehaviorTree;

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
};

/** Что бот знает о враге. */
struct FNoFearKnownActor
{
	FVector LastKnownLocation = FVector::ZeroVector;
	double LastSeenTime = -1.0e9;
	double LastAttackedTime = -1.0e9;
	bool bVisible = false;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FNoFearBotTargetChangedSignature, AActor*, NewTarget, AActor*, OldTarget);

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

	UPROPERTY(BlueprintAssignable, Category = "NoFear|AI")
	FNoFearBotTargetChangedSignature OnTargetChanged;

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

	/** В бою смотреть на цель и двигаться боком (стрелки). Выключено — бежать лицом по ходу (монстры). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|AI|Movement")
	bool bStrafeInCombat = true;

	/** Дальше этого от дома бот в бою не уходит. 0 — без поводка. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|AI|Movement", meta = (ClampMin = "0"))
	float LeashRadius = 0.f;

	/** Имя структуры ввода у тела GASP (CBP_SandboxCharacter). */
	UPROPERTY(EditAnywhere, Category = "NoFear|AI|Movement")
	FName InputStateProperty = TEXT("CharacterInputState");

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

	void SetTarget(AActor* NewTarget);
	void SetInvestigateLocation(const FVector& Location);
	void ForgetStaleActors();
	double GetNow() const;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "NoFear|AI")
	TObjectPtr<UAISenseConfig_Sight> SightConfig;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "NoFear|AI")
	TObjectPtr<UAISenseConfig_Hearing> HearingConfig;

	FVector HomeLocation = FVector::ZeroVector;
	TWeakObjectPtr<AActor> CurrentTarget;
	TMap<TWeakObjectPtr<AActor>, FNoFearKnownActor> KnownActors;

private:
	FTimerHandle UpdateTimer;
	TWeakObjectPtr<UNoFearHealthComponent> BoundHealth;
};
