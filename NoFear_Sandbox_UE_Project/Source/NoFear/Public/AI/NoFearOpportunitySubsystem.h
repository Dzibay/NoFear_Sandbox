#pragma once

#include "CoreMinimal.h"
#include "AI/NoFearMonsterTypes.h"
#include "Subsystems/WorldSubsystem.h"
#include "NoFearOpportunitySubsystem.generated.h"

/**
 * Метки возможностей на людях (раздел 3.3 Docs/Monsters_Design.md): один вид монстров ставит, другие охотятся.
 * Сбит с ног ставит удар с сильным отбросом, слизь — прямое попадание плевка, в кислоте — лужа.
 * «Отставший» не ставится, а считается: ближайший живой союзник дальше IsolatedDistance.
 *
 * Отладка: `NoFear.Monster.Debug 1` — зоны монстров, их вид, цель и окна, метки над людьми.
 * Консоль: `NoFear.Monster.Tag <метка> [сек]` — метка на ближайшего к взгляду игрока человека;
 * `NoFear.Monster.Window <окно> [сек]` — окно ближайшему монстру.
 */
UCLASS()
class NOFEAR_API UNoFearOpportunitySubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	/** Поставить метку на Duration секунд (продлевает). Duration <= 0 — пока не снимут RemoveTag. */
	UFUNCTION(BlueprintCallable, Category = "NoFear|Opportunity")
	static void AddTag(AActor* Target, ENoFearOpportunity Tag, float Duration);

	UFUNCTION(BlueprintCallable, Category = "NoFear|Opportunity")
	static void RemoveTag(AActor* Target, ENoFearOpportunity Tag);

	/** Метка висит (Isolated считается на лету). Мёртвым меток нет. */
	UFUNCTION(BlueprintPure, Category = "NoFear|Opportunity")
	static bool HasTag(const AActor* Target, ENoFearOpportunity Tag);

	/** Сколько секунд метке осталось. -1 — бессрочно, 0 — нет метки. */
	UFUNCTION(BlueprintPure, Category = "NoFear|Opportunity")
	static float GetTimeRemaining(const AActor* Target, ENoFearOpportunity Tag);

	/** Сумма бонусов за метки цели. */
	static float GetOpportunityBonus(const AActor* Target, const TMap<ENoFearOpportunity, float>& Bonuses);

	/** Категория цели для весов вида: герой, напарник, солдат, мирный, монстр. */
	UFUNCTION(BlueprintPure, Category = "NoFear|Opportunity")
	static ENoFearTargetCategory GetTargetCategory(const AActor* Target);

	/** Включена отладка монстров (NoFear.Monster.Debug). */
	static bool IsDebugEnabled();

	/** Союзник дальше этого — человек «отставший», см. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Opportunity")
	float IsolatedDistance = 1200.f;

	// UTickableWorldSubsystem
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

private:
	static UNoFearOpportunitySubsystem* Get(const AActor* Actor);
	bool ComputeIsolated(const AActor* Target) const;
	void DrawDebug() const;

	struct FTags
	{
		/** Метка -> до какого времени (бесконечность — бессрочно). */
		TMap<ENoFearOpportunity, double> Until;
	};
	TMap<TWeakObjectPtr<AActor>, FTags> Tags;

	struct FIsolatedCache
	{
		bool bIsolated = false;
		double Time = -1.0e9;
	};
	mutable TMap<TWeakObjectPtr<const AActor>, FIsolatedCache> IsolatedCache;

	double NextCleanupTime = 0.0;
};
