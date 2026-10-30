#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "NoFearMeleeComponent.generated.h"

class UAnimMontage;
class UDamageType;
class USoundBase;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FNoFearMeleeAttackSignature, AActor*, Target);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FNoFearMeleeHitSignature, AActor*, Victim, float, Damage);

/**
 * Удар с рывком для монстров. Атака: замах (разворот к цели) → прыжок вперёд → окно удара,
 * в котором всех не-союзников перед телом задевает сфера → восстановление → перезарядка удара.
 * Каждого задетого бьёт один раз за атаку. Анимации нет: поле Montage пустое, пока нет своих анимаций.
 * Монстру его добавляет ANoFearMonsterAIController, можно поставить и вручную на любого персонажа.
 */
UCLASS(ClassGroup = (NoFear), meta = (BlueprintSpawnableComponent))
class NOFEAR_API UNoFearMeleeComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UNoFearMeleeComponent();

	/** Можно начинать атаку: не атакует, удар перезарядился, жив и стоит на земле. */
	UFUNCTION(BlueprintPure, Category = "NoFear|Melee")
	bool CanAttack() const;

	/** Начать атаку по цели. false — сейчас нельзя (см. CanAttack). */
	UFUNCTION(BlueprintCallable, Category = "NoFear|Melee")
	bool StartAttack(AActor* Target);

	/** Идёт атака (замах, рывок или восстановление). */
	UFUNCTION(BlueprintPure, Category = "NoFear|Melee")
	bool IsAttacking() const { return Phase != EPhase::Idle; }

	/** Цель достаточно близко, чтобы начать атаку. */
	UFUNCTION(BlueprintPure, Category = "NoFear|Melee")
	bool IsInAttackRange(const AActor* Target) const;

	UPROPERTY(BlueprintAssignable, Category = "NoFear|Melee")
	FNoFearMeleeAttackSignature OnAttackStarted;

	UPROPERTY(BlueprintAssignable, Category = "NoFear|Melee")
	FNoFearMeleeHitSignature OnAttackHit;

	// --- Урон ---

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Melee", meta = (ClampMin = "0"))
	float Damage = 25.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Melee")
	TSubclassOf<UDamageType> DamageType;

	/** С какого расстояния (между центрами) начинать атаку. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Melee", meta = (ClampMin = "0"))
	float AttackRange = 300.f;

	/** Центр сферы удара: столько см перед телом. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Melee", meta = (ClampMin = "0"))
	float HitReach = 90.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Melee", meta = (ClampMin = "1"))
	float HitRadius = 80.f;

	// --- Тайминги ---

	/** Замах до рывка, сек: монстр разворачивается к цели, игрок успевает среагировать. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Melee|Timing", meta = (ClampMin = "0"))
	float WindupTime = 0.3f;

	/** Сколько длится окно удара после начала рывка, сек. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Melee|Timing", meta = (ClampMin = "0.01"))
	float HitWindow = 0.45f;

	/** Пауза после окна удара, сек. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Melee|Timing", meta = (ClampMin = "0"))
	float RecoveryTime = 0.4f;

	/** Минимум между началами атак, сек. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Melee|Timing", meta = (ClampMin = "0"))
	float Cooldown = 1.4f;

	/** Скорость поворота к цели на замахе, град/с. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Melee|Timing", meta = (ClampMin = "0"))
	float WindupTurnRate = 720.f;

	// --- Рывок ---

	/** Горизонтальная скорость рывка, см/с. 0 — бить без рывка. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Melee|Lunge", meta = (ClampMin = "0"))
	float LungeSpeed = 700.f;

	/** Вертикальная скорость рывка, см/с: монстр подпрыгивает и пролетает вперёд. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Melee|Lunge", meta = (ClampMin = "0"))
	float LungeUpSpeed = 220.f;

	// --- Анимация и звук ---

	/** Монтаж атаки. Пока своих анимаций нет — пусто. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Melee|Effects")
	TObjectPtr<UAnimMontage> Montage;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Melee|Effects", meta = (ClampMin = "0.01"))
	float MontagePlayRate = 1.f;

	/** Звук замаха/рывка. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Melee|Effects")
	TObjectPtr<USoundBase> SwingSound;

	/** Звук попадания. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Melee|Effects")
	TObjectPtr<USoundBase> HitSound;

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

protected:
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	enum class EPhase : uint8
	{
		Idle,
		Windup,
		Lunge,
		Recovery
	};

	void BeginLunge();
	void EndHitWindow();
	void FinishAttack();
	void SweepHits();
	void FaceTarget(float DeltaTime);
	bool IsOwnerAlive() const;

	EPhase Phase = EPhase::Idle;
	TWeakObjectPtr<AActor> AttackTarget;
	TArray<TWeakObjectPtr<AActor>> HitActors;
	double NextAttackTime = 0.0;
	FTimerHandle PhaseTimer;
};
