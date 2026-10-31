#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "NoFearMeleeComponent.generated.h"

class UNoFearMonsterAttack;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FNoFearMeleeAttackSignature, AActor*, Target);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FNoFearMeleeHitSignature, AActor*, Victim, float, Damage);

/**
 * Ближний бой монстров: набор атак (Attacks — удар с места, рывок и т. д., см. UNoFearMonsterAttack).
 * Атака: замах (разворот к цели, поза атаки) → удар (рывок, окно удара: всех не-союзников перед телом задевает
 * сфера, каждого один раз) → восстановление. Из атак, подходящих по дистанции и перезарядке, выбирается случайная
 * с учётом веса. Монстру компонент добавляет ANoFearMonsterAIController, можно поставить и вручную на любого персонажа.
 */
UCLASS(ClassGroup = (NoFear), meta = (BlueprintSpawnableComponent))
class NOFEAR_API UNoFearMeleeComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UNoFearMeleeComponent();

	/** Можно начинать атаку вообще: не атакует, жив, не пошатывается и стоит на земле. */
	UFUNCTION(BlueprintPure, Category = "NoFear|Melee")
	bool CanAttack() const;

	/** Есть атака, которую можно начать по цели прямо сейчас (дистанция и перезарядка). */
	UFUNCTION(BlueprintPure, Category = "NoFear|Melee")
	bool CanAttackTarget(const AActor* Target) const;

	/** Начать подходящую атаку по цели. false — сейчас нельзя. */
	UFUNCTION(BlueprintCallable, Category = "NoFear|Melee")
	bool StartAttack(AActor* Target);

	/** Начать атаку с именем Name (для тестов и сценариев). bIgnoreRange — не проверять дистанцию и перезарядку. */
	UFUNCTION(BlueprintCallable, Category = "NoFear|Melee")
	bool StartAttackByName(AActor* Target, FName Name, bool bIgnoreRange = false);

	/** Идёт атака (замах, удар или восстановление). */
	UFUNCTION(BlueprintPure, Category = "NoFear|Melee")
	bool IsAttacking() const { return Phase != EPhase::Idle; }

	/** Цель на дистанции хотя бы одной атаки (перезарядку не учитывает). */
	UFUNCTION(BlueprintPure, Category = "NoFear|Melee")
	bool IsInAttackRange(const AActor* Target) const;

	/** Наибольшая дистанция среди атак, см. */
	UFUNCTION(BlueprintPure, Category = "NoFear|Melee")
	float GetMaxAttackRange() const;

	/** Атака, которая идёт сейчас (или null). */
	UFUNCTION(BlueprintPure, Category = "NoFear|Melee")
	UNoFearMonsterAttack* GetCurrentAttack() const { return CurrentAttack; }

	/** Имя последней начатой атаки. */
	UFUNCTION(BlueprintPure, Category = "NoFear|Melee")
	FName GetLastAttackName() const { return LastAttackName; }

	/** Сколько атак начато с появления. */
	UFUNCTION(BlueprintPure, Category = "NoFear|Melee")
	int32 GetAttackCount() const { return AttackCount; }

	UFUNCTION(BlueprintPure, Category = "NoFear|Melee")
	UNoFearMonsterAttack* FindAttack(FName Name) const;

	UPROPERTY(BlueprintAssignable, Category = "NoFear|Melee")
	FNoFearMeleeAttackSignature OnAttackStarted;

	/** Атака закончилась (восстановление прошло или прервана). */
	UPROPERTY(BlueprintAssignable, Category = "NoFear|Melee")
	FNoFearMeleeAttackSignature OnAttackFinished;

	UPROPERTY(BlueprintAssignable, Category = "NoFear|Melee")
	FNoFearMeleeHitSignature OnAttackHit;

	/** Набор атак. По умолчанию удар с места и рывок. */
	UPROPERTY(EditAnywhere, Instanced, BlueprintReadOnly, Category = "Melee")
	TArray<TObjectPtr<UNoFearMonsterAttack>> Attacks;

	/** Множитель перезарядки всех атак (агрессивный монстр бьёт чаще). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Melee", meta = (ClampMin = "0.05"))
	float CooldownScale = 1.f;

	/** Множитель урона всех атак. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Melee", meta = (ClampMin = "0"))
	float DamageScale = 1.f;

	/** Минимальная пауза между концом одной атаки и началом следующей, сек. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Melee", meta = (ClampMin = "0"))
	float MinTimeBetweenAttacks = 0.25f;

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

protected:
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	enum class EPhase : uint8
	{
		Idle,
		Windup,
		Strike,
		Recovery
	};

	bool IsReady(const UNoFearMonsterAttack* Attack) const;
	UNoFearMonsterAttack* ChooseAttack(const AActor* Target) const;
	bool BeginAttack(UNoFearMonsterAttack* Attack, AActor* Target);
	void BeginStrike();
	void EndHitWindow();
	void FinishAttack();
	void SweepHits();
	void FaceTarget(float DeltaTime);
	void KeepOffTarget();
	bool IsOwnerAlive() const;

	EPhase Phase = EPhase::Idle;
	UPROPERTY(Transient)
	TObjectPtr<UNoFearMonsterAttack> CurrentAttack;
	TWeakObjectPtr<AActor> AttackTarget;
	TArray<TWeakObjectPtr<AActor>> HitActors;
	double NextAttackTime = 0.0;
	FName LastAttackName;
	int32 AttackCount = 0;
	FTimerHandle PhaseTimer;
};
