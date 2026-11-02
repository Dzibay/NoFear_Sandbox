#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "NoFearSpitEffectComponent.generated.h"

/**
 * Следы плевков плевуна на человеке (раздел 4.4 Docs/Monsters_Design.md). Вешается сам при первом попадании.
 * - Слизь: метка Slimed, тело движется медленнее (время тела ×SpeedScale) до конца слизи.
 * - Ослепление: метка Blinded. Боты с ней хуже целятся (UNoFearCombatComponent::BlindedSpreadMultiplier)
 *   и не видят дальше ANoFearAIController::BlindedSightRange; у героя HUD рисует пятна.
 */
UCLASS(ClassGroup = (NoFear), meta = (BlueprintSpawnableComponent))
class NOFEAR_API UNoFearSpitEffectComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UNoFearSpitEffectComponent();

	/** Облепить слизью на Time с: метка Slimed и время тела ×SpeedScale. */
	UFUNCTION(BlueprintCallable, Category = "NoFear|Spit")
	static void ApplySlime(AActor* Victim, float Time, float SpeedScale = 0.6f);

	/** Ослепить на Time с (метка Blinded). */
	UFUNCTION(BlueprintCallable, Category = "NoFear|Spit")
	static void ApplyBlind(AActor* Victim, float Time);

	UFUNCTION(BlueprintPure, Category = "NoFear|Spit")
	static UNoFearSpitEffectComponent* FindSpitEffect(const AActor* Actor);

	UFUNCTION(BlueprintPure, Category = "NoFear|Spit")
	bool IsSlowed() const { return bSlowApplied; }

	/** Сколько раз облепляло и ослепляло (для тестов). */
	UFUNCTION(BlueprintPure, Category = "NoFear|Spit")
	int32 GetSlimeCount() const { return SlimeCount; }

	UFUNCTION(BlueprintPure, Category = "NoFear|Spit")
	int32 GetBlindCount() const { return BlindCount; }

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

protected:
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	void Restore();

	double SlimeUntil = -1.0;
	float SlowScale = 1.f;
	bool bSlowApplied = false;
	int32 SlimeCount = 0;
	int32 BlindCount = 0;
};
