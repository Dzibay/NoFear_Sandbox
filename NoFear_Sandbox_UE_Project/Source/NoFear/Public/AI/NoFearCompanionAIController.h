#pragma once

#include "CoreMinimal.h"
#include "AI/NoFearShooterAIController.h"
#include "NoFearCompanionAIController.generated.h"

class ANoFearCompanionAIController;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FNoFearCompanionStateChangedSignature, ANoFearCompanionAIController*, Companion);

/**
 * Напарник. Пока не завербован, ведёт себя как союзник: держит точку и стреляет по врагам.
 * Игрок подходит и жмёт E (UNoFearRecruitableComponent) — напарник идёт за ним и дерётся рядом,
 * не отходя от лидера дальше Follow Leash Radius. Повторное E — «Жди здесь» / «За мной».
 * Список напарников и их лимит держит ANoFearPlayerController. Дерево то же, что у стрелков: BT_NoFear_Soldier.
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

	/** Пешка лидера, если он жив. */
	UFUNCTION(BlueprintPure, Category = "NoFear|AI|Companion")
	APawn* GetLeaderPawn() const;

	UFUNCTION(BlueprintPure, Category = "NoFear|AI|Companion")
	bool IsRecruited() const { return Leader.IsValid(); }

	/** true — идти за лидером, false — ждать на текущем месте. */
	UFUNCTION(BlueprintCallable, Category = "NoFear|AI|Companion")
	void SetFollowing(bool bNewFollowing);

	UFUNCTION(BlueprintPure, Category = "NoFear|AI|Companion")
	bool IsFollowing() const { return bFollowing && Leader.IsValid(); }

	/** Завербован, отпущен, остался ждать или пошёл следом. */
	UPROPERTY(BlueprintAssignable, Category = "NoFear|AI|Companion")
	FNoFearCompanionStateChangedSignature OnCompanionStateChanged;

	virtual FVector GetLeashAnchor() const override;
	virtual float GetLeashRadius() const override;

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

protected:
	virtual void OnPossess(APawn* InPawn) override;
	virtual void UpdateBot() override;
	virtual bool IsValidTarget(const AActor* Candidate) const override;
	virtual bool ShouldWalk() const override;
	virtual void WriteBlackboard() override;

	float GetDistanceToLeader() const;

	TWeakObjectPtr<AController> Leader;
	bool bFollowing = false;
};
