#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/Tasks/BTTask_BlackboardBase.h"
#include "BTTask_NoFearBandage.generated.h"

/**
 * Перевязка своего по приказу отряда (ключ HealTarget): подбежать к раненому (не дошёл за ApproachTime — провал),
 * присесть рядом, повернуться к нему и BandageTime секунд перевязывать, затем
 * UNoFearSquadSubsystem::CompleteBandage — раненому здоровье, отряду мораль, приказ снят.
 * Раненый отошёл дальше MaxPatientDistance — встать и догнать (время перевязки стоит); не догнал за
 * PatientLostTime или раненый погиб — провал, приказ снят (AbortBandage), дерево перепроверит ситуацию.
 * Бег сбили (оглушение, уклонение от гранаты) — задача сама идёт снова: подход не обрывается, как у BTTask_MoveTo.
 */
UCLASS(meta = (DisplayName = "NoFear Bandage"))
class NOFEAR_API UBTTask_NoFearBandage : public UBTTask_BlackboardBase
{
	GENERATED_UCLASS_BODY()

public:
	/** Сколько секунд перевязывать. */
	UPROPERTY(EditAnywhere, Category = "Bandage", meta = (ClampMin = "0.1"))
	float BandageTime = 4.f;

	/** Перевязывать можно не дальше этого от раненого; дальше — догонять. */
	UPROPERTY(EditAnywhere, Category = "Bandage", meta = (ClampMin = "50"))
	float MaxPatientDistance = 220.f;

	/** Сколько бежать к раненому в начале, прежде чем бросить, с (приказ живёт HealOrderTimeout подсистемы отрядов). */
	UPROPERTY(EditAnywhere, Category = "Bandage", meta = (ClampMin = "0"))
	float ApproachTime = 15.f;

	/** Сколько догонять отошедшего раненого (уже были рядом), прежде чем бросить, с. */
	UPROPERTY(EditAnywhere, Category = "Bandage", meta = (ClampMin = "0"))
	float PatientLostTime = 3.f;

	virtual EBTNodeResult::Type ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;
	virtual EBTNodeResult::Type AbortTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;
	virtual void OnTaskFinished(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, EBTNodeResult::Type TaskResult) override;
	virtual uint16 GetInstanceMemorySize() const override;
	virtual void InitializeMemory(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, EBTMemoryInit::Type InitType) const override;
	virtual void CleanupMemory(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, EBTMemoryClear::Type CleanupType) const override;
	virtual FString GetStaticDescription() const override;

protected:
	virtual void TickTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds) override;

private:
	struct FBandageMemory
	{
		TWeakObjectPtr<APawn> Patient;
		/** Сколько ещё перевязывать, с (пока догоняет — не убывает). */
		float TimeLeft = 0.f;
		/** С какого времени бежит к раненому; < 0 — рядом. */
		double LostSince = -1.0;
		/** Не добежал до этого времени — провал. */
		double Deadline = 0.0;
		/** Раньше этого не переотдавать приказ идти (если бег сбили). */
		double NextMoveTime = 0.0;
		/** Уже был рядом: дальше только догонять за PatientLostTime. */
		bool bReached = false;
		/** Пути нет — в лог один раз. */
		bool bReportedNoPath = false;
		bool bCrouched = false;
	};

	/** Присесть рядом и перевязывать (bClose) или встать и бежать к раненому. */
	void SetClose(UBehaviorTreeComponent& OwnerComp, FBandageMemory& Memory, bool bClose, double Now) const;
	/** Приказ контроллеру идти к раненому. */
	void MoveToPatient(UBehaviorTreeComponent& OwnerComp, FBandageMemory& Memory, double Now) const;
	EBTNodeResult::Type Fail(UBehaviorTreeComponent& OwnerComp, APawn* Medic, APawn* Patient) const;
	void StopBandage(UBehaviorTreeComponent& OwnerComp, FBandageMemory& Memory) const;
};
