#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "NoFearFearComponent.generated.h"

class UNoFearFearComponent;

/** Уровень страха по порогам FNoFearFearTuning. */
UENUM(BlueprintType)
enum class ENoFearFearLevel : uint8
{
	/** Спокоен: 0–29. */
	Calm,
	/** Тревога: 30–59. */
	Anxious,
	/** Паника: 60–84. */
	Panic,
	/** Ужас: 85–100. */
	Terror
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FNoFearFearLevelChangedSignature, UNoFearFearComponent*, Component, ENoFearFearLevel, OldLevel, ENoFearFearLevel, NewLevel);

/**
 * Шкала страха персонажа, 0–100 (этап 11). Есть у каждого ANoFearCharacter.
 * Страх поднимают события (AddFear, AddFearFrom): у каждого события свой потолок — выше него оно страх не поднимает.
 * Спад и передачу соседям считает UNoFearFearSubsystem. Монстры не пугаются (множитель команды 0).
 */
UCLASS(ClassGroup = (NoFear), meta = (BlueprintSpawnableComponent))
class NOFEAR_API UNoFearFearComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UNoFearFearComponent();

	/** Страх актора, его пешки (если это контроллер) или nullptr. */
	UFUNCTION(BlueprintPure, Category = "NoFear|Fear")
	static UNoFearFearComponent* FindFear(const AActor* Actor);

	/** Страх актора, 0 — если шкалы нет. */
	UFUNCTION(BlueprintPure, Category = "NoFear|Fear")
	static float GetActorFear(const AActor* Actor);

	UFUNCTION(BlueprintPure, Category = "NoFear|Fear")
	float GetFear() const { return Fear; }

	UFUNCTION(BlueprintPure, Category = "NoFear|Fear")
	float GetFearFraction() const { return Fear / 100.f; }

	UFUNCTION(BlueprintPure, Category = "NoFear|Fear")
	ENoFearFearLevel GetFearLevel() const;

	/** Уровень, в который попадает Value по текущим порогам. */
	ENoFearFearLevel GetLevelFor(float Value) const;

	/** Поставить страх напрямую (отладка, сценарии). */
	UFUNCTION(BlueprintCallable, Category = "NoFear|Fear")
	void SetFear(float NewFear);

	/**
	 * Испуг на Amount (с множителем команды), но не выше Cap. Уже выше Cap — страх не растёт, но и не спадает,
	 * пока испуг повторяется. bDirect — испугался сам (не от соседа): только такие передают панику дальше.
	 * Возвращает, на сколько вырос страх.
	 */
	UFUNCTION(BlueprintCallable, Category = "NoFear|Fear")
	float AddFear(float Amount, float Cap, FVector SourceLocation, bool bDirect = true);

	/** То же, но от Source: стрелков люди и перестрелка пугают не выше CombatFearCap, монстры — как всех. */
	UFUNCTION(BlueprintCallable, Category = "NoFear|Fear")
	float AddFearFrom(float Amount, float Cap, FVector SourceLocation, const AActor* Source, bool bDirect = true);

	/** Снять Amount страха (успокоение, спад). */
	UFUNCTION(BlueprintCallable, Category = "NoFear|Fear")
	void Soothe(float Amount);

	/** Снять Amount страха и запомнить, что успокаивал кто-то (герой держит E): мирный выходит из тревоги сразу. */
	UFUNCTION(BlueprintCallable, Category = "NoFear|Fear")
	void Comfort(float Amount);

	/** Когда последний раз успокаивали (Comfort). */
	UFUNCTION(BlueprintPure, Category = "NoFear|Fear")
	double GetLastComfortTime() const { return LastComfortTime; }

	/** Пугается ли вообще (у монстров множитель команды 0). */
	UFUNCTION(BlueprintPure, Category = "NoFear|Fear")
	bool CanBeAfraid() const { return GetGainScale() > 0.f; }

	/** Множитель прироста: команда × GainMultiplier. */
	UFUNCTION(BlueprintPure, Category = "NoFear|Fear")
	float GetGainScale() const;

	/** Откуда последний испуг. */
	UFUNCTION(BlueprintPure, Category = "NoFear|Fear")
	FVector GetSourceLocation() const { return SourceLocation; }

	UFUNCTION(BlueprintPure, Category = "NoFear|Fear")
	double GetLastGainTime() const { return LastGainTime; }

	UFUNCTION(BlueprintPure, Category = "NoFear|Fear")
	double GetLastDirectGainTime() const { return LastDirectGainTime; }

	/** Паникует и сам недавно испугался — пугает соседей. */
	UFUNCTION(BlueprintPure, Category = "NoFear|Fear")
	bool IsSpreading() const;

	/** Свой множитель прироста поверх множителя команды (смельчак — меньше 1, трус — больше). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fear", meta = (ClampMin = "0"))
	float GainMultiplier = 1.f;

	/** Свой множитель спада страха (0 — не спадает; браслет героя ставит 0, пока тот задерживает дыхание). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fear", meta = (ClampMin = "0"))
	float DecayMultiplier = 1.f;

	/** Уровень страха сменился. */
	UPROPERTY(BlueprintAssignable, Category = "NoFear|Fear")
	FNoFearFearLevelChangedSignature OnFearLevelChanged;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	void ApplyFear(float NewFear);
	double GetNow() const;

	/** Страх на старте. */
	UPROPERTY(EditAnywhere, Category = "Fear", meta = (ClampMin = "0", ClampMax = "100"))
	float Fear = 0.f;

	FVector SourceLocation = FVector::ZeroVector;
	double LastGainTime = -1.0e9;
	double LastDirectGainTime = -1.0e9;
	double LastComfortTime = -1.0e9;
	ENoFearFearLevel Level = ENoFearFearLevel::Calm;
};
