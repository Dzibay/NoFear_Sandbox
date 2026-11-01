#pragma once

#include "CoreMinimal.h"
#include "AI/NoFearAIController.h"
#include "AI/NoFearMonsterTypes.h"
#include "AI/NoFearPackSubsystem.h"
#include "NoFearMonsterAIController.generated.h"

class UNoFearMeleeComponent;
class UNoFearMonsterAttack;

/**
 * Монстр: бродит стаей по карте, заметив врага — рычит, и стая нападает вся разом; бьёт набором атак
 * UNoFearMeleeComponent (удар с места, рывок...).
 *
 * - Стая (UNoFearPackSubsystem): монстры с одинаковым PackName у тела, а без имени — стоящие рядом. Дома и территории
 *   нет: вожак ведёт стаю куда глаза глядят, остальные идут клином следом. Цель, которую видит один (или которая
 *   в него попала, хоть издалека), за долю секунды узнаёт вся стая и бежит к ней цепью.
 * - Нападение: вожак подводит стаю к цели и ждёт отставших, стая расходится кольцом вокруг цели (на дальние места —
 *   в обход) и бьёт волнами по 2–3 с разных сторон; остальные держат кольцо и отрезают отход. Подошедшего вплотную
 *   бьёт и без очереди. В кольце и очереди только бойцы ближнего боя (IsRingFighter): прыгуны кружат по краю и
 *   прыгают, когда цель отвлеклась, плевуны держат дистанцию за кольцом (GetStandoffPoint) — в лимит жетонов не входят.
 * - Погиб вожак — стая разбегается от угрозы, потом новый вожак рыком собирает её снова.
 * - Охотится на слух: видит хуже стрелков, зато слышит дальше и на шум бежит, а не идёт. Обидчика помнит
 *   (GrudgeTime) и гонит хоть через всю карту.
 * - Монстр без стаи бьёт по жетонам атаки UNoFearBotSubsystem: одновременно до трёх бойцов на цель, остальные кружат.
 * - Монстр портала (bScareCivilians) гонит и пугает мирных, чтобы те сломались и питали портал, а бьёт только
 *   сопротивляющихся. Стража портала (SetPortalGuard) стоит у проводников спиной к ним и бросается на освобождающих.
 * - Агрессивный монстр (команда AggroMonster): одиночка без стаи и очереди за жетоном, бьёт ближайшего,
 *   легко переключается и бьёт чаще.
 * Дерево: BT_NoFear_Monster.
 */
UCLASS(Blueprintable)
class NOFEAR_API ANoFearMonsterAIController : public ANoFearAIController
{
	GENERATED_BODY()

public:
	ANoFearMonsterAIController(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	UFUNCTION(BlueprintPure, Category = "NoFear|AI")
	UNoFearMeleeComponent* GetMeleeComponent() const;

	/** Агрессивный монстр (команда AggroMonster). */
	UFUNCTION(BlueprintPure, Category = "NoFear|Monster")
	bool IsAggro() const;

	/** Вожак стаи (сам монстр, если он вожак или одиночка). */
	UFUNCTION(BlueprintPure, Category = "NoFear|Monster")
	APawn* GetPackLeader() const;

	UFUNCTION(BlueprintPure, Category = "NoFear|Monster")
	bool IsPackLeader() const;

	/** Сколько монстров в стае вместе с этим. */
	UFUNCTION(BlueprintPure, Category = "NoFear|Monster")
	int32 GetPackSize() const;

	/** Что сейчас делает стая монстра (одиночка — Roam). */
	UFUNCTION(BlueprintPure, Category = "NoFear|Monster")
	ENoFearPackState GetPackState() const;

	/** Вожак погиб, монстр разбегается. */
	UFUNCTION(BlueprintPure, Category = "NoFear|Monster")
	bool IsScattering() const;

	/** Можно бить цель (жетон атаки, очередь в волне стаи или он не нужен). */
	UFUNCTION(BlueprintPure, Category = "NoFear|Monster")
	bool HasAttackToken() const { return bHasAttackToken; }

	UFUNCTION(BlueprintPure, Category = "NoFear|Monster")
	bool IsRoaring() const { return bRoaring; }

	/** Сколько раз рычал с появления. */
	UFUNCTION(BlueprintPure, Category = "NoFear|Monster")
	int32 GetRoarCount() const { return RoarCount; }

	/** Рыкнуть сейчас: поза, шум и тревога стае о текущей цели. */
	UFUNCTION(BlueprintCallable, Category = "NoFear|Monster")
	virtual void Roar();

	/** Монстр помнит обиду на Actor (тот нападал на него). */
	UFUNCTION(BlueprintPure, Category = "NoFear|Monster")
	bool HasGrudgeAgainst(const AActor* Actor) const;

	/** Радиус кольца вокруг цели: середина CircleMinRadius..CircleMaxRadius, см. */
	UFUNCTION(BlueprintPure, Category = "NoFear|Monster")
	float GetRingRadius() const { return 0.5f * (CircleMinRadius + CircleMaxRadius); }

	/** Стая сообщает, где цель. Поднятый стаей сам не рычит: рычит тот, кто заметил. */
	void ReceivePackAlarm(AActor* Threat, const FVector& ThreatLocation);

	/** Если у тела нет UNoFearMeleeComponent, добавить его с настройками по умолчанию. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|AI")
	bool bAddMeleeComponentIfMissing = true;

	/** Бить по очереди: в стае — волнами и встаёт в кольцо, без стаи — по жетонам (не больше MaxAttackersPerTarget на цель). false — сам по себе (прыгун, плевун). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|Monster")
	bool bUseAttackTokens = true;

	/**
	 * Свой набор атак вида монстра (прыгун, плевун, танк). Не пустой — заменяет набор UNoFearMeleeComponent тела
	 * при вселении. Пустой — у тела остаются его атаки.
	 */
	UPROPERTY(EditAnywhere, Instanced, BlueprintReadOnly, Category = "NoFear|Monster")
	TArray<TObjectPtr<UNoFearMonsterAttack>> AttackSet;

	/** На каком расстоянии от цели кружит без жетона и стоит в кольце стаи (середина), см. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|Monster", meta = (ClampMin = "0"))
	float CircleMinRadius = 400.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|Monster", meta = (ClampMin = "0"))
	float CircleMaxRadius = 600.f;

	/** Цель, которая сама била монстра (SelfDefenseTime) и стоит ближе этого, монстр бьёт без очереди, см. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|Monster", meta = (ClampMin = "0"))
	float SelfDefenseDistance = 700.f;

	/** Сколько секунд после удара или попадания цели монстр отвечает ей без очереди. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|Monster", meta = (ClampMin = "0"))
	float SelfDefenseTime = 2.f;

	/** Сколько помнит обидчика, сек. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|Monster", meta = (ClampMin = "0"))
	float GrudgeTime = 90.f;

	/** Прибавка к оценке цели за обиду. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|Monster", meta = (ClampMin = "0"))
	float GrudgeScoreBonus = 1.f;

	// --- Стая ---

	/** Прибавка к оценке цели стаи: стая бьёт одну цель. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|Monster|Pack", meta = (ClampMin = "0"))
	float PackTargetBonus = 1.f;

	/** Вожак (и одиночка) бродит шагами по столько вперёд по своему курсу, см. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|Monster|Pack", meta = (ClampMin = "0"))
	float RoamStride = 1200.f;

	/** Сколько длится рык, сек (монстр стоит). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|Monster|Pack", meta = (ClampMin = "0"))
	float RoarDuration = 1.2f;

	/** Не рычит чаще, чем раз в столько секунд. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|Monster|Pack", meta = (ClampMin = "0"))
	float RoarCooldown = 10.f;

	/** Рык слышно на столько, см (мирные настораживаются). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|Monster|Pack", meta = (ClampMin = "0"))
	float RoarNoiseRange = 2500.f;

	// --- Агрессивный ---

	/** Множитель перезарядки атак агрессивного монстра. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|Monster|Aggro", meta = (ClampMin = "0.05"))
	float AggroCooldownScale = 0.6f;

	/** Прибавка к оценке текущей цели у агрессивного: маленькая — легко переключается на ближайшего. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|Monster|Aggro", meta = (ClampMin = "0"))
	float AggroTargetStickiness = 0.1f;

	/** Веса целей агрессивного: бьёт кого угодно, монстров тоже. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|Monster|Aggro")
	FNoFearTargetWeights AggroTargetWeights;

	/** «Выдох»: после стольких ударов подряд агрессивный стоит и тяжело дышит (окно Exhale). 0 — без выдоха. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|Monster|Aggro", meta = (ClampMin = "0"))
	int32 ExhaleAfterAttacks = 3;

	/** Сколько длится выдох, сек: окно открыто, атак нет. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|Monster|Aggro", meta = (ClampMin = "0"))
	float ExhaleTime = 1.5f;

	/** Удары считаются «подряд», если между концом одного и концом следующего не больше этого, сек. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|Monster|Aggro", meta = (ClampMin = "0"))
	float ExhaleChainGap = 2.5f;

	// --- Данные вида (Docs/Monsters_Design.md) ---

	/** Имя вида для отладки и тестов. У агрессивного всегда Rabid. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|Monster|Kind")
	FName KindName = TEXT("Hunter");

	/** Слабые места и броня тела. Ставятся в UNoFearVulnerabilityComponent при вселении. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|Monster|Kind")
	TArray<FNoFearWeakZone> WeakZones;

	/** Если у тела нет UNoFearVulnerabilityComponent, добавить его (окна уязвимости есть у всех монстров). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|Monster|Kind")
	bool bAddVulnerabilityIfMissing = true;

	/** Веса категорий целей: прибавляются к оценке. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|Monster|Kind")
	FNoFearTargetWeights TargetWeights;

	/** На какие метки возможностей охотится вид: метка -> прибавка к оценке цели. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|Monster|Kind")
	TMap<ENoFearOpportunity, float> OpportunityBonuses;

	/** Сбитого с ног ближе FinishDistance бьёт без очереди за жетоном (добивание). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|Monster|Kind")
	bool bFinishKnockedDown = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|Monster|Kind", meta = (ClampMin = "0"))
	float FinishDistance = 800.f;

	UFUNCTION(BlueprintPure, Category = "NoFear|Monster")
	FName GetKindName() const;

	/**
	 * Занять место в тени союзника (колосс держит щит): пока не вышло время, идёт в эту точку вместо места в стае.
	 * Duration <= 0 снимает.
	 */
	UFUNCTION(BlueprintCallable, Category = "NoFear|Monster")
	void SetShadeGoal(FVector Goal, float Duration);

	UFUNCTION(BlueprintPure, Category = "NoFear|Monster")
	bool HasShadeGoal() const;

	UFUNCTION(BlueprintPure, Category = "NoFear|Monster")
	FVector GetShadeGoal() const { return ShadeGoal; }

	/** Сейчас «выдыхает» (агрессивный после серии ударов). */
	UFUNCTION(BlueprintPure, Category = "NoFear|Monster")
	bool IsExhaling() const;

	/** Оценка цели (для тестов): то же, что при выборе цели. */
	UFUNCTION(BlueprintPure, Category = "NoFear|Monster")
	float GetTargetScore(AActor* Candidate) const;

	/**
	 * Боец кольца: встаёт в кольцо стаи и бьёт своей волной, в ближнем бою — по жетону (не больше
	 * UNoFearBotSubsystem::MaxAttackersPerTarget на цель). Прыгун и плевун бьют сами по себе — вне кольца и лимита.
	 */
	UFUNCTION(BlueprintPure, Category = "NoFear|Monster")
	virtual bool IsRingFighter() const { return bUseAttackTokens; }

	/**
	 * Своя точка у цели, пока стая окружает её, а монстр не стоит в кольце: прыгун — у фланга или за спиной цели,
	 * плевун — за кольцом со стороны стаи. PackSide — направление от цели к стае. false — точки нет.
	 */
	virtual bool GetStandoffPoint(const FVector& TargetLocation, const FVector& PackSide, FVector& OutPoint) const { return false; }

	/** Может быть в стае: не агрессивный и не страж портала. */
	bool CanJoinPack() const;

	// --- Портал ---

	/**
	 * Сделать стражем портала (UNoFearPortalSubsystem): стоит на Post лицом наружу, спиной к проводникам, и бьёт только
	 * тех, кто подошёл к Center ближе Radius (первым — того, кто освобождает проводника). В стаю не встаёт.
	 */
	void SetPortalGuard(const FVector& Center, const FVector& Post, float Radius);
	void ClearPortalGuard();

	UFUNCTION(BlueprintPure, Category = "NoFear|Monster|Portal")
	bool IsPortalGuard() const { return bPortalGuard; }

	UFUNCTION(BlueprintPure, Category = "NoFear|Monster|Portal")
	FVector GetGuardPost() const { return GuardPost; }

	/** Людей (категория Civilian) не бьёт, а гонит и пугает, пока не сломаются; бьёт только обидчика. Ставит портал. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NoFear|Monster|Portal")
	bool bScareCivilians = false;

	/** Пугая, подходит к человеку на это расстояние, см. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|Monster|Portal", meta = (ClampMin = "0"))
	float ScareDistance = 220.f;

	/** Пугает, когда человек ближе этого, см. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|Monster|Portal", meta = (ClampMin = "0"))
	float ScareReach = 450.f;

	/** Сколько страха за один наскок (прямой испуг, до 100). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|Monster|Portal", meta = (ClampMin = "0"))
	float ScareFear = 35.f;

	/** Наскок не чаще, чем раз в столько секунд. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|Monster|Portal", meta = (ClampMin = "0.1"))
	float ScareInterval = 1.f;

	/** Сейчас гонит и пугает человека, а не бьёт. */
	UFUNCTION(BlueprintPure, Category = "NoFear|Monster|Portal")
	bool IsScaring() const { return bScaring; }

	/** Сколько раз пугал наскоком с появления. */
	UFUNCTION(BlueprintPure, Category = "NoFear|Monster|Portal")
	int32 GetScareCount() const { return ScareCount; }

protected:
	/** Боец вне кольца (IsRingFighter() == false): бить сейчас? Прыгун ждёт, когда цель отвлечётся. */
	virtual bool WantsFreeAttack(AActor* Target) { return true; }

	/** Атака закончилась (любая, до решений о жетоне). */
	virtual void OnMeleeAttackFinished(AActor* Target) {}

	virtual void OnPossess(APawn* InPawn) override;
	virtual void OnUnPossess() override;
	virtual void UpdateBot() override;
	virtual bool IsValidTarget(const AActor* Candidate) const override;
	virtual float ScoreTarget(const AActor* Candidate, const FNoFearKnownActor& Knowledge) const override;
	virtual void HandleTargetChanged(AActor* NewTarget, AActor* OldTarget) override;
	virtual bool ShouldWalk() const override;
	virtual void UpdateMovementStance() override;
	virtual void WriteBlackboard() override;
	virtual void ShareAlarm() override;

	/** Настроить компонент ближнего боя тела под вид монстра: набор атак AttackSet и т. п. */
	virtual void ConfigureMelee(UNoFearMeleeComponent* Melee);

	/** Рёв доступен: с прошлого прошло RoarCooldown. */
	bool IsRoarReady() const;

	/** Кружит вокруг цели без жетона (или стоит на своём месте в кольце стаи). */
	bool IsCircling() const;

	/** Расстояние до текущей цели по горизонтали (без цели — очень большое). */
	float GetTargetDistance2D() const;

	/** Решение стаи для этого монстра. false — не в стае (одиночка или стая ещё не собрана). */
	bool GetPackView(FNoFearPackMemberView& OutView) const;

	/** Стая ведёт монстра к точке (место в строю, в кольце, бегство). */
	bool GetPackGoal(FVector& OutGoal) const;

private:
	UFUNCTION()
	void HandleAttackFinished(AActor* Target);

	void UpdateGrudges();
	void UpdateAttackToken();
	void ReleaseToken();
	void AlarmPack(AActor* Threat);
	void EndRoar();
	void SetBlackboardBool(FName Key, bool bValue);
	/** Куда бродить вожаку или одиночке: точка впереди по курсу, курс понемногу гуляет. */
	FVector GetRoamCenter();

	TWeakObjectPtr<UNoFearMeleeComponent> ConfiguredMelee;

	TMap<TWeakObjectPtr<AActor>, double> Grudges;

	bool bHasAttackToken = false;
	TWeakObjectPtr<AActor> TokenTarget;
	/** Защищается от цели вплотную (с запасом, чтобы не дёргаться на границе). */
	bool bSelfDefense = false;

	bool bRoaring = false;
	FVector ShadeGoal = FVector::ZeroVector;
	double ShadeUntil = 0.0;
	int32 RoarCount = 0;
	double NextRoarTime = 0.0;
	FTimerHandle RoarTimer;

	FVector RoamHeading = FVector::ForwardVector;
	double NextRoamTurnTime = 0.0;
	FVector RoamCenter = FVector::ZeroVector;
	double NextRoamCenterTime = 0.0;

	/** Удары агрессивного подряд и когда кончился последний (для выдоха). */
	int32 ChainedAttacks = 0;
	double LastAttackFinishTime = -1.0e9;

	/** Взять жетон ближнего боя на цель (без подсистемы — можно). */
	bool AcquireToken(AActor* Target);
	/** Пугать, а не бить: цель — человек без обиды, монстр портальный. */
	void UpdateScare();
	/** Страж на посту без цели смотрит наружу. */
	void UpdateGuardFacing();

	bool bPortalGuard = false;
	FVector GuardCenter = FVector::ZeroVector;
	FVector GuardPost = FVector::ZeroVector;
	float GuardRadius = 0.f;

	bool bScaring = false;
	double NextScareTime = 0.0;
	int32 ScareCount = 0;
};
