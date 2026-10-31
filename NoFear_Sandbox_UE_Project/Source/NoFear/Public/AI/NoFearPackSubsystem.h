#pragma once

#include "CoreMinimal.h"
#include "Core/NoFearTeam.h"
#include "Subsystems/WorldSubsystem.h"
#include "NoFearPackSubsystem.generated.h"

class ANoFearMonsterAIController;

/** Что сейчас делает стая монстров. */
UENUM(BlueprintType)
enum class ENoFearPackState : uint8
{
	/** Цели нет: вожак бродит по карте, остальные идут клином за ним. */
	Roam,
	/** Цель далеко: вожак ведёт стаю к ней, остальные бегут цепью по бокам, отставших ждут. */
	Gather,
	/** Стая у цели: расходится по местам кольца вокруг неё, на дальние места — в обход. */
	Encircle,
	/** Окружили: бьют волнами по 2–3 с разных сторон, остальные держат кольцо и отрезают отход. */
	Attack,
	/** Вожак погиб: стая разбегается, потом новый вожак рыком собирает её снова. */
	Scatter,
};

/** Решение стаи для одного монстра (читает ANoFearMonsterAIController). */
struct FNoFearPackMemberView
{
	ENoFearPackState State = ENoFearPackState::Roam;
	ANoFearMonsterAIController* Leader = nullptr;
	int32 PackSize = 1;
	/** Цель стаи. nullptr — цели нет. */
	AActor* Target = nullptr;
	/** Куда идти: место в строю, в кольце или точка бегства. */
	bool bHasGoal = false;
	FVector Goal = FVector::ZeroVector;
	/** Очередь монстра бить цель (его волна, ещё не ударил). */
	bool bAttackTurn = false;
	/** Место в кольце. INDEX_NONE — не в кольце. */
	int32 Slot = INDEX_NONE;
};

/**
 * Стаи монстров. Обычные монстры одной команды с одинаковым ANoFearCharacter::PackName — одна стая, где бы ни стояли;
 * без имени — стая собирается из стоящих рядом (PackFormRadius) и держится вместе: кто попал в стаю, из неё не
 * выпадает, пока не отстанет дальше LeaveDistance. Агрессивные монстры — одиночки, в стаи не входят.
 *
 * Каждые UpdateInterval секунд подсистема:
 *  - назначает вожака: старейший монстр стаи (появился раньше всех);
 *  - выбирает цель стаи (цель вожака, иначе самая частая у остальных) и делится знанием о ней: кто её видит
 *    или в кого она только что попала, сообщает остальным, где она;
 *  - ведёт стаю по состояниям ENoFearPackState и раздаёт каждому точку (ключ PackGoal) и очередь удара;
 *  - погиб вожак — стая на ScatterTime разбегается от угрозы, потом новый вожак рычит и собирает её.
 */
UCLASS()
class NOFEAR_API UNoFearPackSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	/** Подсистема стай мира WorldContextObject. */
	UFUNCTION(BlueprintPure, Category = "NoFear|Pack", meta = (WorldContext = "WorldContextObject", DisplayName = "Get Pack Subsystem"))
	static UNoFearPackSubsystem* Get(const UObject* WorldContextObject);

	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;

	/** Пересобрать стаи и решения сейчас (раз в UpdateInterval это происходит само). */
	UFUNCTION(BlueprintCallable, Category = "NoFear|Pack")
	void UpdatePacks();

	/** Состояние стаи Pawn. Одиночка — Roam. */
	UFUNCTION(BlueprintPure, Category = "NoFear|Pack")
	ENoFearPackState GetPackState(const APawn* Pawn) const;

	/** Вожак стаи Pawn (может быть он сам). nullptr — Pawn не в стае. */
	UFUNCTION(BlueprintPure, Category = "NoFear|Pack")
	APawn* GetPackLeader(const APawn* Pawn) const;

	/** Живые монстры стаи Pawn, включая его самого. Пусто — не в стае. */
	UFUNCTION(BlueprintPure, Category = "NoFear|Pack")
	TArray<APawn*> GetPackMembers(const APawn* Pawn) const;

	/** Цель стаи Pawn. */
	UFUNCTION(BlueprintPure, Category = "NoFear|Pack")
	AActor* GetPackTarget(const APawn* Pawn) const;

	/** Куда стая ведёт Pawn сейчас. false — точки нет. */
	UFUNCTION(BlueprintPure, Category = "NoFear|Pack")
	bool GetPackGoal(const APawn* Pawn, FVector& OutGoal) const;

	/** Место Pawn в кольце вокруг цели. -1 — не в кольце. */
	UFUNCTION(BlueprintPure, Category = "NoFear|Pack")
	int32 GetPackSlot(const APawn* Pawn) const;

	/** Номер волны, которая бьёт сейчас, и сколько волн у стаи Pawn. */
	UFUNCTION(BlueprintPure, Category = "NoFear|Pack")
	void GetPackWave(const APawn* Pawn, int32& OutWave, int32& OutWaveCount) const;

	/** Очередь Pawn бить цель. */
	UFUNCTION(BlueprintPure, Category = "NoFear|Pack")
	bool IsAttackTurn(const APawn* Pawn) const;

	/** Сколько сейчас стай (вместе с одиночками). */
	UFUNCTION(BlueprintPure, Category = "NoFear|Pack")
	int32 GetPackCount() const { return Packs.Num(); }

	/** Для контроллера монстра: false — монстр ещё не в стае. */
	bool GetMemberView(const ANoFearMonsterAIController* Bot, FNoFearPackMemberView& OutView) const;

	/** Монстр закончил удар: его часть волны сделана. */
	void NotifyAttackFinished(const ANoFearMonsterAIController* Bot);

	/** Как часто пересобирать стаи и решения, сек. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NoFear|Pack", meta = (ClampMin = "0.05"))
	float UpdateInterval = 0.2f;

	/** Монстр без PackName ближе этого к кому-то из стаи без имени — вступает в неё; спокойные стаи так же сливаются, см. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NoFear|Pack", meta = (ClampMin = "0"))
	float PackFormRadius = 1500.f;

	/** Больше этого в стае не бывает. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NoFear|Pack", meta = (ClampMin = "1"))
	int32 MaxPackSize = 8;

	/** Монстр без PackName отстал от спокойного вожака дальше этого — уходит из стаи, см. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NoFear|Pack", meta = (ClampMin = "0"))
	float LeaveDistance = 6000.f;

	/** Цель, которую кто-то видел (или которая в него попала) не дольше этого назад, сообщается всей стае, сек. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NoFear|Pack", meta = (ClampMin = "0"))
	float PackIntelTime = 1.f;

	/** Отставший дальше этого от вожака — вожак ждёт его (не дольше MaxHoldTime), см. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NoFear|Pack", meta = (ClampMin = "0"))
	float StragglerDistance = 1500.f;

	/** Сколько вожак ждёт отставших, сек. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NoFear|Pack", meta = (ClampMin = "0"))
	float MaxHoldTime = 4.f;

	/** Шаг цепи при сборе: расстояние между соседями, см. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NoFear|Pack", meta = (ClampMin = "0"))
	float GatherSpacing = 350.f;

	/** Вожак подвёл стаю к видимой цели ближе этого — окружать, см. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NoFear|Pack", meta = (ClampMin = "0"))
	float EngageDistance = 1500.f;

	/** Кто-то из стаи уже ближе этого к видимой цели — окружать сразу, см. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NoFear|Pack", meta = (ClampMin = "0"))
	float ContactDistance = 700.f;

	/** Все дальше этого от цели (сбежала) — снова собираться и догонять, см. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NoFear|Pack", meta = (ClampMin = "0"))
	float DisengageDistance = 2500.f;

	/** Монстр ближе этого к своему месту в кольце — на месте, см. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NoFear|Pack", meta = (ClampMin = "0"))
	float SlotReachedDistance = 300.f;

	/** На местах такая доля кольца — атаковать. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NoFear|Pack", meta = (ClampMin = "0", ClampMax = "1"))
	float EncircleReadyFraction = 0.6f;

	/** Окружают не дольше этого, потом атакуют как есть, сек. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NoFear|Pack", meta = (ClampMin = "0"))
	float EncircleMaxTime = 5.f;

	/** Место дальше этого угла от монстра — идёт к нему в обход, шагами по столько градусов. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NoFear|Pack", meta = (ClampMin = "10", ClampMax = "90"))
	float FlankStepAngle = 50.f;

	/** Волна, которая не успела ударить за столько секунд, уступает следующей. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NoFear|Pack", meta = (ClampMin = "0.5"))
	float WaveMaxTime = 5.f;

	/** Сколько разбегаются после гибели вожака, сек. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NoFear|Pack", meta = (ClampMin = "0"))
	float ScatterTime = 6.f;

	/** На сколько разбегаются от угрозы, см. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NoFear|Pack", meta = (ClampMin = "0"))
	float ScatterMinDistance = 1500.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NoFear|Pack", meta = (ClampMin = "0"))
	float ScatterMaxDistance = 2500.f;

private:
	struct FMember
	{
		TWeakObjectPtr<ANoFearMonsterAIController> Bot;
		TWeakObjectPtr<APawn> Pawn;
		int32 Slot = INDEX_NONE;
		bool bAttackedThisWave = false;
		bool bAttackTurn = false;
		bool bHasGoal = false;
		FVector Goal = FVector::ZeroVector;
		/** Точка бегства, пока стая разбегается. */
		bool bHasFleeGoal = false;
		FVector FleeGoal = FVector::ZeroVector;
	};

	struct FPack
	{
		int32 Id = 0;
		FName Name;
		ENoFearTeam Team = ENoFearTeam::Monster;
		TArray<FMember> Members;
		TWeakObjectPtr<ANoFearMonsterAIController> Leader;
		TWeakObjectPtr<APawn> LeaderPawn;
		FVector LeaderLocation = FVector::ZeroVector;
		/** Куда смотрит строй: по ходу вожака. */
		FVector Heading = FVector::ForwardVector;

		ENoFearPackState State = ENoFearPackState::Roam;
		double StateStartTime = 0.0;
		/** С какого времени вожак ждёт отставших (меньше нуля — не ждёт). */
		double HoldStartTime = -1.0;

		TWeakObjectPtr<AActor> Target;
		FVector TargetLocation = FVector::ZeroVector;
		/** Кто-то из стаи видит цель или она только что в кого-то попала. */
		bool bTargetSeen = false;

		/** Кольцо: место 0 смотрит на стаю, места через равные углы. */
		float SlotBaseAngle = 0.f;
		int32 SlotCount = 0;
		int32 Wave = 0;
		int32 WaveCount = 1;
		double WaveStartTime = 0.0;

		double ScatterUntil = 0.0;
	};

	void UpdateMembership(double Now);
	void UpdatePack(FPack& Pack, double Now);
	void SetState(FPack& Pack, ENoFearPackState NewState, double Now);
	void PickTarget(FPack& Pack, double Now);
	void ShareIntel(FPack& Pack, double Now);
	void BeginScatter(FPack& Pack, const FVector& From, double Now);
	/** Кольцо вокруг цели: места монстрам, которые бьют её (или ещё без цели), по их направлению от цели. */
	void AssignSlots(FPack& Pack);
	bool NeedsSlots(const FPack& Pack) const;
	void UpdateWaves(FPack& Pack, double Now);
	void ComputeGoals(FPack& Pack, double Now);
	/** Точка места Slot в кольце (без обхода). */
	FVector GetSlotPoint(const FPack& Pack, const FMember& Member) const;
	bool IsRingMember(const FPack& Pack, const FMember& Member) const;
	bool ProjectGoal(const FVector& Point, FVector& OutPoint) const;
	void RebuildIndex();
	void DrawDebug() const;

	const FPack* FindPack(const UObject* Key, const FMember** OutMember = nullptr) const;
	FPack* FindPack(const UObject* Key, FMember** OutMember = nullptr);

	TArray<FPack> Packs;
	/** Контроллер или тело -> (стая, монстр). Пересобирается при каждом обновлении. */
	TMap<const UObject*, FIntPoint> Index;
	int32 NextPackId = 1;
	float TimeSinceUpdate = 0.f;
};
