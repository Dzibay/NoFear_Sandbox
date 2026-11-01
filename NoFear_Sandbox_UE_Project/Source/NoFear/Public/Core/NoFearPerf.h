#pragma once

#include "CoreMinimal.h"
#include "HAL/PlatformTime.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "Subsystems/WorldSubsystem.h"
#include "NoFearPerf.generated.h"

/** Что меряем: время игрового потока по системам ИИ (NoFearPerf печатает мс на кадр по каждой). */
enum class ENoFearPerfStat : uint8
{
	/** ANoFearAIController::UpdateBot и наследники: память, цель, тревога, блэкборд. */
	BotUpdate,
	/** UNoFearCombatComponent: линия огня, CanFire. */
	Combat,
	/** UNoFearSquadSubsystem::Tick. */
	Squad,
	/** UNoFearPackSubsystem::Tick. */
	Pack,
	/** Поиск укрытий (UNoFearCoverLibrary). */
	Cover,
	/** Поиск точки бегства и прочих точек на навмеше (UNoFearFleeLibrary, задачи Find Location). */
	FindLocation,
	/** Индикаторы ботов в HUD. */
	HUD,
	/** UNoFearPoseComponent. */
	Pose,
	/** UNoFearAILodSubsystem: уровни детализации. */
	Lod,
	Count
};

/** Накопитель времени по ENoFearPerfStat (только игровой поток). */
struct NOFEAR_API FNoFearPerf
{
	static void Add(ENoFearPerfStat Stat, uint64 Cycles)
	{
		const int32 Index = static_cast<int32>(Stat);
		AccumCycles[Index] += Cycles;
		++AccumCalls[Index];
	}
	static uint64 GetCycles(ENoFearPerfStat Stat) { return AccumCycles[static_cast<int32>(Stat)]; }
	static uint64 GetCalls(ENoFearPerfStat Stat) { return AccumCalls[static_cast<int32>(Stat)]; }
	static const TCHAR* GetName(ENoFearPerfStat Stat);
	static void Reset();

private:
	static uint64 AccumCycles[static_cast<int32>(ENoFearPerfStat::Count)];
	static uint64 AccumCalls[static_cast<int32>(ENoFearPerfStat::Count)];
};

struct FNoFearPerfScope
{
	explicit FNoFearPerfScope(ENoFearPerfStat InStat) : Stat(InStat), Start(FPlatformTime::Cycles64()) {}
	~FNoFearPerfScope() { FNoFearPerf::Add(Stat, FPlatformTime::Cycles64() - Start); }

	ENoFearPerfStat Stat;
	uint64 Start;
};

/** Какой поиск ограничиваем (FNoFearSearchBudget). */
enum class ENoFearSearchKind : uint8
{
	/** Полный поиск укрытия стрелка. */
	Cover,
	/** Поиск точки бегства и прочих точек на навмеше. */
	Location,
	Count
};

/**
 * Лимит дорогих поисков на кадр (Project Settings → NoFear AI → Search). Перед плановым поиском бот спрашивает HasBudget:
 * нет — откладывает поиск до следующего размышления (так всплеск, когда весь отряд вступил в бой разом, расходится по кадрам).
 * Срочные поиски (уход от гранаты, первый испуг) лимит не спрашивают, но расходуют.
 */
struct NOFEAR_API FNoFearSearchBudget
{
	static bool HasBudget(ENoFearSearchKind Kind);
	static void Consume(ENoFearSearchKind Kind);
	/** Сколько поисков отложено с начала замера NoFearPerf. */
	static int32 GetDeferred(ENoFearSearchKind Kind) { return Deferred[static_cast<int32>(Kind)]; }
	static void ResetDeferred();

private:
	static uint64 Frame;
	static int32 Used[static_cast<int32>(ENoFearSearchKind::Count)];
	static int32 Deferred[static_cast<int32>(ENoFearSearchKind::Count)];
};

/** Замер блока: и в NoFearPerf, и в Unreal Insights (событие NoFear_<Stat>). */
#define NOFEAR_PERF_SCOPE(StatName) \
	TRACE_CPUPROFILER_EVENT_SCOPE(NoFear_##StatName); \
	FNoFearPerfScope NoFearPerfScope_##StatName(ENoFearPerfStat::StatName)

/**
 * Замер нагрузки: NoFearPerf <секунды> собирает время кадра и игрового потока каждый кадр и время систем ИИ,
 * потом печатает среднее, 95-й процентиль и максимум (на экран и в лог с меткой NOFEAR_PERF).
 */
UCLASS()
class NOFEAR_API UNoFearPerfSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	static UNoFearPerfSubsystem* Get(const UObject* WorldContextObject);

	/** Начать замер на Seconds секунд (идущий замер начинается заново). */
	UFUNCTION(BlueprintCallable, Category = "NoFear|Perf")
	void StartSample(float Seconds);

	UFUNCTION(BlueprintPure, Category = "NoFear|Perf")
	bool IsSampling() const { return bSampling; }

	/** Отчёт последнего законченного замера (пусто — замеров не было). */
	UFUNCTION(BlueprintPure, Category = "NoFear|Perf")
	FString GetLastReport() const { return LastReport; }

	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual bool IsTickableInEditor() const override { return false; }

private:
	void FinishSample();

	bool bSampling = false;
	double SampleEnd = 0.0;
	double SampleStart = 0.0;
	TArray<float> FrameMs;
	TArray<float> GameMs;
	int32 BotSum = 0;
	FString LastReport;
};
