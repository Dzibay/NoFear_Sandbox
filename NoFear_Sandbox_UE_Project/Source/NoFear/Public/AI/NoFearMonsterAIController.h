#pragma once

#include "CoreMinimal.h"
#include "AI/NoFearAIController.h"
#include "NoFearMonsterAIController.generated.h"

class UNoFearMeleeComponent;
class UNoFearMonsterAttack;

/**
 * Монстр: бродит стаей вокруг дома, заметив врага — рычит (стая в PackAlarmRadius бросается следом) и нападает
 * набором атак UNoFearMeleeComponent (удар с места, рывок...).
 *
 * - Окружение: бить одну цель одновременно могут двое (жетоны атаки UNoFearBotSubsystem), остальные кружат вокруг
 *   на 400–600 см и ждут; ударивший уступает жетон ожидающему.
 * - Стая: монстры с одинаковым PackName у тела, а без имени — стоящие рядом (PackFormRadius). Спокойная стая бродит
 *   за вожаком, территория у стаи общая — вокруг дома вожака.
 * - Охотится на слух: видит хуже стрелков, зато слышит дальше и на шум бежит, а не идёт.
 * - Территория: цель дальше TerritoryRadius + TerritoryMargin от дома бросает и шагом идёт назад, но обидчика
 *   запоминает: вернётся на территорию — узнает сразу и нападёт в первую очередь.
 * - Агрессивный монстр (команда AggroMonster): одиночка без стаи, территории и очереди за жетоном, бьёт ближайшего,
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

	/** Можно бить цель (жетон атаки есть или он не нужен). */
	UFUNCTION(BlueprintPure, Category = "NoFear|Monster")
	bool HasAttackToken() const { return bHasAttackToken; }

	UFUNCTION(BlueprintPure, Category = "NoFear|Monster")
	bool IsRoaring() const { return bRoaring; }

	/** Сколько раз рычал с появления. */
	UFUNCTION(BlueprintPure, Category = "NoFear|Monster")
	int32 GetRoarCount() const { return RoarCount; }

	/** Рыкнуть сейчас: поза, шум и тревога стае о текущей цели. */
	UFUNCTION(BlueprintCallable, Category = "NoFear|Monster")
	void Roar();

	/** Центр территории: дом вожака стаи. */
	UFUNCTION(BlueprintPure, Category = "NoFear|Monster")
	FVector GetTerritoryCenter() const;

	/** Точка за границей территории (с запасом TerritoryMargin). Агрессивный монстр территории не имеет. */
	UFUNCTION(BlueprintPure, Category = "NoFear|Monster")
	bool IsOutsideTerritory(FVector Location) const;

	/** Монстр помнит обиду на Actor (тот нападал на него или ушёл за территорию). */
	UFUNCTION(BlueprintPure, Category = "NoFear|Monster")
	bool HasGrudgeAgainst(const AActor* Actor) const;

	/** Бросил цель за границей территории и идёт домой. */
	UFUNCTION(BlueprintPure, Category = "NoFear|Monster")
	bool IsReturningHome() const;

	/** Если у тела нет UNoFearMeleeComponent, добавить его с настройками по умолчанию. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|AI")
	bool bAddMeleeComponentIfMissing = true;

	/** Становиться в очередь за жетоном атаки (не больше двоих на цель). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|Monster")
	bool bUseAttackTokens = true;

	/**
	 * Свой набор атак вида монстра (прыгун, плевун, танк). Не пустой — заменяет набор UNoFearMeleeComponent тела
	 * при вселении. Пустой — у тела остаются его атаки.
	 */
	UPROPERTY(EditAnywhere, Instanced, BlueprintReadOnly, Category = "NoFear|Monster")
	TArray<TObjectPtr<UNoFearMonsterAttack>> AttackSet;

	/** На каком расстоянии от цели кружит без жетона (задача Find Circle Location), см. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|Monster", meta = (ClampMin = "0"))
	float CircleMinRadius = 400.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|Monster", meta = (ClampMin = "0"))
	float CircleMaxRadius = 600.f;

	// --- Стая ---

	/** Монстры без PackName ближе этого друг к другу (по цепочке) — одна стая, см. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|Monster|Pack", meta = (ClampMin = "0"))
	float PackFormRadius = 1500.f;

	/** Рык поднимает членов стаи в этом радиусе, см. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|Monster|Pack", meta = (ClampMin = "0"))
	float PackAlarmRadius = 3000.f;

	/** Как часто пересобирать стаю, сек. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|Monster|Pack", meta = (ClampMin = "0.1"))
	float PackUpdateInterval = 2.f;

	/** Сколько длится рык, сек (монстр стоит). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|Monster|Pack", meta = (ClampMin = "0"))
	float RoarDuration = 1.2f;

	/** Не рычит чаще, чем раз в столько секунд. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|Monster|Pack", meta = (ClampMin = "0"))
	float RoarCooldown = 10.f;

	/** Рык слышно на столько, см (мирные настораживаются). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|Monster|Pack", meta = (ClampMin = "0"))
	float RoarNoiseRange = 2500.f;

	// --- Территория ---

	/** Радиус территории вокруг дома вожака, см. 0 — без территории. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|Monster|Territory", meta = (ClampMin = "0"))
	float TerritoryRadius = 2500.f;

	/** Уже выбранную цель преследует ещё столько за границей, см. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|Monster|Territory", meta = (ClampMin = "0"))
	float TerritoryMargin = 500.f;

	/** Сколько помнит обидчика, сек. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|Monster|Territory", meta = (ClampMin = "0"))
	float GrudgeTime = 90.f;

	/** Прибавка к оценке цели за обиду. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|Monster|Territory", meta = (ClampMin = "0"))
	float GrudgeScoreBonus = 1.f;

	// --- Агрессивный ---

	/** Множитель перезарядки атак агрессивного монстра. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|Monster|Aggro", meta = (ClampMin = "0.05"))
	float AggroCooldownScale = 0.6f;

	/** Прибавка к оценке текущей цели у агрессивного: маленькая — легко переключается на ближайшего. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|Monster|Aggro", meta = (ClampMin = "0"))
	float AggroTargetStickiness = 0.1f;

protected:
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
	virtual bool CanInvestigateLocation(const FVector& Location) const override;

	/** Настроить компонент ближнего боя тела под вид монстра: набор атак AttackSet и т. п. */
	virtual void ConfigureMelee(UNoFearMeleeComponent* Melee);

	/** Кружит вокруг цели без жетона. */
	bool IsCircling() const;

	/** Расстояние до текущей цели по горизонтали (без цели — очень большое). */
	float GetTargetDistance2D() const;

private:
	UFUNCTION()
	void HandleAttackFinished(AActor* Target);

	void UpdatePack();
	void UpdateGrudges();
	void UpdateAttackToken();
	void AlarmPack(AActor* Threat);
	void EndRoar();
	void SetBlackboardBool(FName Key, bool bValue);

	TWeakObjectPtr<UNoFearMeleeComponent> ConfiguredMelee;

	TArray<TWeakObjectPtr<ANoFearMonsterAIController>> PackMembers;
	TWeakObjectPtr<ANoFearMonsterAIController> PackLeader;
	double NextPackUpdateTime = 0.0;

	TMap<TWeakObjectPtr<AActor>, double> Grudges;

	bool bHasAttackToken = false;
	TWeakObjectPtr<AActor> TokenTarget;

	bool bRoaring = false;
	int32 RoarCount = 0;
	double NextRoarTime = 0.0;
	double NextPackAlarmTime = 0.0;
	FTimerHandle RoarTimer;
};
