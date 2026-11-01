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
 * Мирный житель: никогда не нападает, только убегает. Состояние следует за шкалой страха (UNoFearFearComponent, этап 11).
 *
 * Спокоен → бродит вокруг дома (BT_NoFear_Civilian). Страх от порога тревоги (далёкий выстрел, шорох монстра,
 * паника соседей) → насторожен: смотрит на источник и отходит шагом. Страх от порога паники (монстр на виду, драка,
 * близкий выстрел, пуля мимо, ранение, труп, взрыв, граната, крик соседа) → паника: крик (пугает соседей и слышен
 * монстрам) и бег к точке UNoFearFleeLibrary::FindFleeLocation. Добежал или бежать некуда → прячется.
 * Страх спал ниже порога тревоги (сам, рядом со стражем или героя успокоил) → встаёт и идёт домой или бродит, где оказался.
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

	/** Успокоиться сразу (для тестов и сценариев): страх выше порога тревоги сбрасывается в 0. */
	UFUNCTION(BlueprintCallable, Category = "NoFear|Civilian")
	void CalmDown();

	virtual void HandleDanger(ENoFearDangerType Type, AActor* Source, FVector ThreatLocation) override;

	UPROPERTY(BlueprintAssignable, Category = "NoFear|Civilian")
	FNoFearCivilianStateChangedSignature OnCivilianStateChanged;

	/** Настороженность длится не меньше этого после последнего звука (и пока страх не ниже порога тревоги). */
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
	virtual void OnPossess(APawn* InPawn) override;
	virtual void OnUnPossess() override;
	/** Транс проводника: прятавшийся встаёт; очнувшись — в панике бежит от портала. */
	virtual void OnPortalTranceChanged(bool bTrance) override;

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
	/** Событие подняло страх: паника, если страх и потолок события не ниже порога паники, иначе — настороженность. */
	void ReactToFear(const FVector& Location, AActor* Source, float EventCap, bool bScream);
	/** Состояние по шкале страха. FreshThreat — ближайшая видимая угроза (или nullptr). */
	void SyncFear(AActor* FreshThreat, const FVector& FreshThreatLocation);

	ENoFearCivilianState CivilianState = ENoFearCivilianState::Calm;
	TArray<FThreat> Threats;
	/** Угрозы появились или сдвинулись с прошлого плана бегства. */
	bool bThreatsChanged = false;
	FVector FleeLocation = FVector::ZeroVector;
	bool bHasFleeLocation = false;
	double NextReplanTime = 0.0;
	double LastScreamTime = -1.0e9;
	FVector WaryLocation = FVector::ZeroVector;
	double WaryUntil = 0.0;
	double WaryLookUntil = 0.0;
	bool bWaryFocus = false;
	bool bCowerPose = false;
};
