#include "Core/NoFearPerf.h"

#include "AI/NoFearAILodSubsystem.h"
#include "AI/NoFearBotSubsystem.h"
#include "Core/NoFearAISettings.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"

uint64 FNoFearPerf::AccumCycles[static_cast<int32>(ENoFearPerfStat::Count)] = {};
uint64 FNoFearPerf::AccumCalls[static_cast<int32>(ENoFearPerfStat::Count)] = {};

uint64 FNoFearSearchBudget::Frame = 0;
int32 FNoFearSearchBudget::Used[static_cast<int32>(ENoFearSearchKind::Count)] = {};
int32 FNoFearSearchBudget::Deferred[static_cast<int32>(ENoFearSearchKind::Count)] = {};

bool FNoFearSearchBudget::HasBudget(ENoFearSearchKind Kind)
{
	if (Frame != GFrameCounter)
	{
		Frame = GFrameCounter;
		FMemory::Memzero(Used);
	}
	const FNoFearAISearchTuning& Tuning = UNoFearAISettings::Get()->Search;
	const int32 Limit = Kind == ENoFearSearchKind::Cover ? Tuning.CoverSearchesPerFrame : Tuning.LocationSearchesPerFrame;
	if (Used[static_cast<int32>(Kind)] < Limit)
	{
		return true;
	}
	++Deferred[static_cast<int32>(Kind)];
	return false;
}

void FNoFearSearchBudget::Consume(ENoFearSearchKind Kind)
{
	if (Frame != GFrameCounter)
	{
		Frame = GFrameCounter;
		FMemory::Memzero(Used);
	}
	++Used[static_cast<int32>(Kind)];
}

void FNoFearSearchBudget::ResetDeferred()
{
	FMemory::Memzero(Deferred);
}

const TCHAR* FNoFearPerf::GetName(ENoFearPerfStat Stat)
{
	switch (Stat)
	{
	case ENoFearPerfStat::BotUpdate: return TEXT("BotUpdate");
	case ENoFearPerfStat::Combat: return TEXT("Combat");
	case ENoFearPerfStat::Squad: return TEXT("Squad");
	case ENoFearPerfStat::Pack: return TEXT("Pack");
	case ENoFearPerfStat::Cover: return TEXT("Cover");
	case ENoFearPerfStat::FindLocation: return TEXT("FindLocation");
	case ENoFearPerfStat::HUD: return TEXT("HUD");
	case ENoFearPerfStat::Pose: return TEXT("Pose");
	case ENoFearPerfStat::Lod: return TEXT("Lod");
	default: return TEXT("?");
	}
}

void FNoFearPerf::Reset()
{
	FMemory::Memzero(AccumCycles);
	FMemory::Memzero(AccumCalls);
}

UNoFearPerfSubsystem* UNoFearPerfSubsystem::Get(const UObject* WorldContextObject)
{
	const UWorld* World = WorldContextObject ? WorldContextObject->GetWorld() : nullptr;
	return World ? World->GetSubsystem<UNoFearPerfSubsystem>() : nullptr;
}

TStatId UNoFearPerfSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UNoFearPerfSubsystem, STATGROUP_Tickables);
}

void UNoFearPerfSubsystem::StartSample(float Seconds)
{
	bSampling = true;
	SampleStart = FPlatformTime::Seconds();
	SampleEnd = SampleStart + FMath::Max(Seconds, 0.5f);
	FrameMs.Reset();
	GameMs.Reset();
	BotSum = 0;
	FNoFearPerf::Reset();
	FNoFearSearchBudget::ResetDeferred();
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(INDEX_NONE, Seconds, FColor::Yellow, FString::Printf(TEXT("NoFearPerf: замер %.0f с..."), Seconds));
	}
}

void UNoFearPerfSubsystem::Tick(float DeltaTime)
{
	if (!bSampling)
	{
		return;
	}
	// Первый кадр после старта несёт время предыдущего кадра — пропускаем.
	if (FPlatformTime::Seconds() - SampleStart > 0.05)
	{
		FrameMs.Add(FApp::GetDeltaTime() * 1000.f);
		GameMs.Add(FPlatformTime::ToMilliseconds(GGameThreadTime));
		if (const UNoFearBotSubsystem* Bots = UNoFearBotSubsystem::Get(this))
		{
			BotSum += Bots->GetBots().Num();
		}
	}
	if (FPlatformTime::Seconds() >= SampleEnd)
	{
		FinishSample();
	}
}

void UNoFearPerfSubsystem::FinishSample()
{
	bSampling = false;
	const int32 Frames = FrameMs.Num();
	if (Frames == 0)
	{
		return;
	}

	auto Summary = [](TArray<float> Values, float& OutAvg, float& OutP95, float& OutMax)
	{
		Values.Sort();
		double Sum = 0.0;
		for (const float V : Values)
		{
			Sum += V;
		}
		OutAvg = static_cast<float>(Sum / Values.Num());
		OutP95 = Values[FMath::Min(Values.Num() - 1, FMath::FloorToInt(Values.Num() * 0.95f))];
		OutMax = Values.Last();
	};

	float FrameAvg, FrameP95, FrameMax, GameAvg, GameP95, GameMax;
	Summary(FrameMs, FrameAvg, FrameP95, FrameMax);
	Summary(GameMs, GameAvg, GameP95, GameMax);

	FString Report = FString::Printf(TEXT("bots=%.0f frames=%d fps=%.1f frame avg/p95/max=%.2f/%.2f/%.2f ms game avg/p95/max=%.2f/%.2f/%.2f ms"),
		static_cast<float>(BotSum) / Frames, Frames, 1000.f / FMath::Max(FrameAvg, 0.01f), FrameAvg, FrameP95, FrameMax, GameAvg, GameP95, GameMax);

	if (const UNoFearAILodSubsystem* Lod = UNoFearAILodSubsystem::Get(this))
	{
		int32 Near = 0, Mid = 0, Far = 0;
		Lod->GetTierCounts(Near, Mid, Far);
		Report += FString::Printf(TEXT(" lod near/mid/far=%d/%d/%d"), Near, Mid, Far);
	}

	Report += FString::Printf(TEXT(" deferred cover/location=%d/%d"),
		FNoFearSearchBudget::GetDeferred(ENoFearSearchKind::Cover), FNoFearSearchBudget::GetDeferred(ENoFearSearchKind::Location));

	Report += TEXT(" | ms/frame:");
	for (int32 Index = 0; Index < static_cast<int32>(ENoFearPerfStat::Count); ++Index)
	{
		const ENoFearPerfStat Stat = static_cast<ENoFearPerfStat>(Index);
		const double Ms = FPlatformTime::ToMilliseconds64(FNoFearPerf::GetCycles(Stat)) / Frames;
		Report += FString::Printf(TEXT(" %s=%.3f(%.1f/f)"), FNoFearPerf::GetName(Stat), Ms, static_cast<double>(FNoFearPerf::GetCalls(Stat)) / Frames);
	}

	LastReport = Report;
	UE_LOG(LogTemp, Display, TEXT("NOFEAR_PERF %s"), *Report);
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(INDEX_NONE, 20.f, FColor::Green, TEXT("NoFearPerf: ") + Report);
	}
}
