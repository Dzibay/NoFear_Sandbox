#pragma once

#include "CoreMinimal.h"
#include "AI/NoFearAIController.h"
#include "NoFearCivilianAIController.generated.h"

class ANoFearCivilianAIController;

/** Что сейчас делает мирный. */
UENUM(BlueprintType)
enum class ENoFearCivilianState : uint8
{
	/** Бродит у дома, останавливается и осматривается. */
	Calm,
	/** Далёкие выстрелы или шорох монстра: остановился, посмотрел на звук, отходит шагом. */
	Wary,
	/** Угроза рядом: крикнул и бежит со всех ног к безопасной точке. */
	Panic,
	/** Добежал или бежать некуда: присел и закрыл голову руками. */
	Cower
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FNoFearCivilianStateChangedSignature, ANoFearCivilianAIController*, Civilian, ENoFearCivilianState, NewState, ENoFearCivilianState, OldState);

/**
 * Мирный житель: никогда не нападает, только убегает.
 *
 * Спокоен → бродит вокруг дома (BT_NoFear_Civilian). Далёкий выстрел или шорох монстра → насторожен:
 * смотрит на звук и отходит шагом. Угроза рядом (монстр, драка, выстрел ближе PanicGunshotDistance,
 * пуля мимо, ранение, труп, взрыв, граната) → паника: крик (пугает соседей и слышен монстрам) и бег
 * к точке UNoFearFleeLibrary::FindFleeLocation. Добежал или бежать некуда → прячется. Через
 * CalmDownMin..CalmDownMax секунд без новых угроз встаёт и идёт домой или бродит, где оказался.
 */
UCLASS(Blueprintable)
class NOFEAR_API ANoFearCivilianAIController : public ANoFearAIController
{
	GENERATED_BODY()

public:
	ANoFearCivilianAIController(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	UFUNCTION(BlueprintPure, Category = "NoFear|Civilian")
	ENoFearCivilianState GetCivilianState() const { return CivilianState; }

	/** Куда бежит (паника) или отходит (насторожен). */
	UFUNCTION(BlueprintPure, Category = "NoFear|Civilian")
	FVector GetFleeLocation() const { return FleeLocation; }

	UFUNCTION(BlueprintPure, Category = "NoFear|Civilian")
	bool HasFleeLocation() const { return bHasFleeLocation; }

	/** Сколько угроз мирный сейчас помнит. */
	UFUNCTION(BlueprintPure, Category = "NoFear|Civilian")
	int32 GetThreatCount() const { return Threats.Num(); }

	/** Испугаться угрозы в точке (крик и бегство). Threat — кто пугает, если известно. */
	UFUNCTION(BlueprintCallable, Category = "NoFear|Civilian")
	void Panic(FVector ThreatLocation, AActor* Threat = nullptr, bool bScream = true);

	/** Добежал до точки бегства: спрятаться (зовёт задача NoFear Civilian Shelter). */
	UFUNCTION(BlueprintCallable, Category = "NoFear|Civilian")
	void NotifyReachedSafety();

	/** Успокоиться сразу (для тестов и сценариев). */
	UFUNCTION(BlueprintCallable, Category = "NoFear|Civilian")
	void CalmDown();

	virtual void HandleDanger(ENoFearDangerType Type, AActor* Source, FVector ThreatLocation) override;

	UPROPERTY(BlueprintAssignable, Category = "NoFear|Civilian")
	FNoFearCivilianStateChangedSignature OnCivilianStateChanged;

	/** Выстрел ближе этого — паника, дальше — настороженность. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NoFear|Civilian", meta = (ClampMin = "0"))
	float PanicGunshotDistance = 1500.f;

	/** Сколько секунд длится настороженность после звука. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NoFear|Civilian", meta = (ClampMin = "0"))
	float WaryDuration = 8.f;

	/** Сколько секунд смотрит на звук, прежде чем отходить. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NoFear|Civilian", meta = (ClampMin = "0"))
	float WaryLookTime = 1.5f;

	/** Насколько отходит от звука шагом. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NoFear|Civilian", meta = (ClampMin = "0"))
	float WaryRetreatMin = 600.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NoFear|Civilian", meta = (ClampMin = "0"))
	float WaryRetreatMax = 1200.f;

	/** Насколько далеко бежит в панике. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NoFear|Civilian", meta = (ClampMin = "0"))
	float FleeMinDistance = 1500.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NoFear|Civilian", meta = (ClampMin = "0"))
	float FleeMaxDistance = 3000.f;

	/** Путь бегства не проходит ближе этого к угрозе; точка ближе этого к угрозе перестаёт быть безопасной. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NoFear|Civilian", meta = (ClampMin = "0"))
	float SafePathDistance = 500.f;

	/** Успокаивается через случайное время в этом диапазоне без новых угроз. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NoFear|Civilian", meta = (ClampMin = "0"))
	float CalmDownMin = 15.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NoFear|Civilian", meta = (ClampMin = "0"))
	float CalmDownMax = 25.f;

	/** Вероятность, что после паники пойдёт домой; иначе новым домом станет место, где успокоился. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NoFear|Civilian", meta = (ClampMin = "0", ClampMax = "1"))
	float ReturnHomeChance = 0.5f;

	/** Крик пугает мирных в этом радиусе. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NoFear|Civilian", meta = (ClampMin = "0"))
	float ScreamRadius = 1500.f;

	/** Крик слышат монстры в этом радиусе. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NoFear|Civilian", meta = (ClampMin = "0"))
	float ScreamNoiseRange = 2500.f;

	/** Не кричит чаще, чем раз в столько секунд. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NoFear|Civilian", meta = (ClampMin = "0"))
	float ScreamCooldown = 4.f;

	/** Угроза забывается, если о ней ничего не слышно столько секунд. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NoFear|Civilian", meta = (ClampMin = "0"))
	float ThreatMemoryTime = 20.f;

	/** Спрятавшийся вскакивает и бежит снова, если угроза подошла ближе этого. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NoFear|Civilian", meta = (ClampMin = "0"))
	float CowerBreakDistance = 600.f;

	/** Как часто (сек) перепроверять точку бегства. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NoFear|Civilian", meta = (ClampMin = "0.1"))
	float ReplanInterval = 1.f;

protected:
	virtual void UpdateBot() override;
	virtual void UpdateAlertState() override;
	virtual void UpdateMovementStance() override;
	virtual void WriteBlackboard() override;
	virtual void NoteCorpse(ANoFearCharacter* Corpse) override;
	virtual void OnUnPossess() override;

private:
	struct FThreat
	{
		TWeakObjectPtr<AActor> Actor;
		FVector Location = FVector::ZeroVector;
		double Time = 0.0;
	};

	void UpdateCivilian();
	void AddThreat(AActor* Actor, const FVector& Location);
	void GetThreatLocations(TArray<FVector>& OutLocations) const;
	bool PlanFlee();
	void BecomeWary(const FVector& Location);
	void Scream(const FVector& ThreatLocation);
	void SetCivilianState(ENoFearCivilianState NewState);
	void SetCowerPose(bool bCower);
	void ResetCalmDownTimer();

	ENoFearCivilianState CivilianState = ENoFearCivilianState::Calm;
	TArray<FThreat> Threats;
	/** Угрозы появились или сдвинулись с прошлого плана бегства. */
	bool bThreatsChanged = false;
	FVector FleeLocation = FVector::ZeroVector;
	bool bHasFleeLocation = false;
	double LastThreatTime = -1.0e9;
	float CalmDownDelay = 20.f;
	double NextReplanTime = 0.0;
	double LastScreamTime = -1.0e9;
	FVector WaryLocation = FVector::ZeroVector;
	double WaryUntil = 0.0;
	double WaryLookUntil = 0.0;
	bool bWaryFocus = false;
	bool bCowerPose = false;
};
