#include "AI/NoFearAILodSubsystem.h"

#include "AI/NoFearAIController.h"
#include "AI/NoFearBotSubsystem.h"
#include "AI/NoFearCombatComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Core/NoFearAISettings.h"
#include "Core/NoFearHealthComponent.h"
#include "Core/NoFearPerf.h"
#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"

namespace
{
	TAutoConsoleVariable<int32> CVarNoFearAILod(
		TEXT("NoFear.AI.Lod"), 1,
		TEXT("Уровни детализации ботов: 1 — включены, 0 — все боты Near (для замеров до/после)."));

	TAutoConsoleVariable<int32> CVarNoFearAILodDebug(
		TEXT("NoFear.AI.LodDebug"), 0,
		TEXT("1 — подписывать уровень детализации над ботами."));

	USkeletalMeshComponent* GetBotMesh(const APawn* Pawn)
	{
		const ACharacter* Character = Cast<ACharacter>(Pawn);
		return Character ? Character->GetMesh() : nullptr;
	}
}

UNoFearAILodSubsystem* UNoFearAILodSubsystem::Get(const UObject* WorldContextObject)
{
	const UWorld* World = WorldContextObject ? WorldContextObject->GetWorld() : nullptr;
	return World ? World->GetSubsystem<UNoFearAILodSubsystem>() : nullptr;
}

bool UNoFearAILodSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	const UWorld* World = Cast<UWorld>(Outer);
	return World && World->IsGameWorld() && Super::ShouldCreateSubsystem(Outer);
}

void UNoFearAILodSubsystem::Deinitialize()
{
	for (TPair<TWeakObjectPtr<ANoFearAIController>, FBotState>& Pair : States)
	{
		Restore(Pair.Value);
	}
	States.Reset();
	Super::Deinitialize();
}

TStatId UNoFearAILodSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UNoFearAILodSubsystem, STATGROUP_Tickables);
}

ENoFearAILod UNoFearAILodSubsystem::GetBotLod(const ANoFearAIController* Bot) const
{
	const FBotState* State = States.Find(Bot);
	return State ? State->Lod : ENoFearAILod::Near;
}

void UNoFearAILodSubsystem::GetTierCounts(int32& OutNear, int32& OutMid, int32& OutFar) const
{
	OutNear = Counts[0];
	OutMid = Counts[1];
	OutFar = Counts[2];
}

void UNoFearAILodSubsystem::Tick(float DeltaTime)
{
	Accumulator += DeltaTime;
	if (Accumulator < UNoFearAISettings::Get()->Lod.Interval)
	{
		return;
	}
	Accumulator = 0.f;
	NOFEAR_PERF_SCOPE(Lod);
	UpdateTiers();
}

void UNoFearAILodSubsystem::UpdateTiers()
{
	const FNoFearAILodTuning& Tuning = UNoFearAISettings::Get()->Lod;
	const bool bEnabled = CVarNoFearAILod.GetValueOnGameThread() != 0;
	const bool bDebug = CVarNoFearAILodDebug.GetValueOnGameThread() != 0;
	UWorld* World = GetWorld();
	const UNoFearBotSubsystem* BotSubsystem = UNoFearBotSubsystem::Get(this);
	if (!World || !BotSubsystem)
	{
		return;
	}

	// Камера игрока: от неё и меряем (в виде сверху/сзади она ближе к тому, что видно, чем тело).
	FVector ViewLocation = FVector::ZeroVector;
	const APawn* PlayerPawn = nullptr;
	if (APlayerController* PlayerController = World->GetFirstPlayerController())
	{
		FRotator ViewRotation;
		PlayerController->GetPlayerViewPoint(ViewLocation, ViewRotation);
		PlayerPawn = PlayerController->GetPawn();
	}

	const float NearSq = FMath::Square(Tuning.NearDistance);
	const float MidSq = FMath::Square(Tuning.MidDistance);
	const float NearOutSq = FMath::Square(Tuning.NearDistance * (1.f + Tuning.Hysteresis));
	const float MidOutSq = FMath::Square(Tuning.MidDistance * (1.f + Tuning.Hysteresis));

	// Ушедшие (умершие, удалённые) боты: вернуть сетке исходные настройки.
	for (auto It = States.CreateIterator(); It; ++It)
	{
		const ANoFearAIController* Bot = It.Key().Get();
		if (!Bot || Bot->GetPawn() != It.Value().Pawn.Get() || UNoFearHealthComponent::IsActorDead(Bot->GetPawn()))
		{
			Restore(It.Value());
			It.RemoveCurrent();
		}
	}

	Counts[0] = Counts[1] = Counts[2] = 0;
	for (ANoFearAIController* Bot : BotSubsystem->GetBots())
	{
		APawn* Pawn = Bot ? Bot->GetPawn() : nullptr;
		if (!Pawn)
		{
			continue;
		}
		FBotState& State = States.FindOrAdd(Bot);
		if (!State.Pawn.IsValid())
		{
			State.Pawn = Pawn;
		}

		ENoFearAILod NewLod = ENoFearAILod::Near;
		if (bEnabled)
		{
			const float DistSq = FVector::DistSquared(ViewLocation, Pawn->GetActorLocation());
			// Повышаем сразу, понижаем с запасом.
			if (DistSq < NearSq || (State.Lod == ENoFearAILod::Near && DistSq < NearOutSq))
			{
				NewLod = ENoFearAILod::Near;
			}
			else if (DistSq < MidSq || (State.Lod != ENoFearAILod::Far && DistSq < MidOutSq))
			{
				NewLod = ENoFearAILod::Mid;
			}
			else
			{
				NewLod = ENoFearAILod::Far;
			}
			// Кто дерётся с игроком, должен реагировать без задержек, как бы далеко ни был.
			if (PlayerPawn && Bot->GetCurrentTarget() == PlayerPawn)
			{
				NewLod = ENoFearAILod::Near;
			}
		}
		State.Lod = NewLod;
		++Counts[static_cast<int32>(NewLod)];
		Apply(Bot, State, bEnabled);

		if (bDebug)
		{
			static const TCHAR* Names[] = {TEXT("NEAR"), TEXT("MID"), TEXT("FAR")};
			static const FColor Colors[] = {FColor::Green, FColor::Yellow, FColor::Red};
			const USkeletalMeshComponent* Mesh = GetBotMesh(Pawn);
			const bool bRendered = Mesh && Mesh->WasRecentlyRendered(0.2f);
			DrawDebugString(World, Pawn->GetActorLocation() + FVector(0.f, 0.f, 130.f),
				FString::Printf(TEXT("%s %.2fs%s"), Names[static_cast<int32>(NewLod)], Bot->GetThinkInterval(), bRendered ? TEXT("") : TEXT(" hidden")),
				nullptr, Colors[static_cast<int32>(NewLod)], Tuning.Interval + 0.02f, false, 1.f);
		}
	}
}

void UNoFearAILodSubsystem::Apply(ANoFearAIController* Bot, FBotState& State, bool bEnabled)
{
	if (State.bApplied && State.AppliedLod == State.Lod && State.bAppliedEnabled == bEnabled)
	{
		return;
	}
	const FNoFearAILodTuning& Tuning = UNoFearAISettings::Get()->Lod;
	APawn* Pawn = State.Pawn.Get();
	USkeletalMeshComponent* Mesh = GetBotMesh(Pawn);
	if (!State.bApplied && Mesh)
	{
		State.OriginalTickOption = Mesh->VisibilityBasedAnimTickOption;
		State.bOriginalUro = Mesh->bEnableUpdateRateOptimizations;
	}

	const float ThinkScale = State.Lod == ENoFearAILod::Far ? Tuning.FarThinkScale : State.Lod == ENoFearAILod::Mid ? Tuning.MidThinkScale : 1.f;
	Bot->SetThinkScale(ThinkScale);

	if (UNoFearCombatComponent* Combat = Pawn ? Pawn->FindComponentByClass<UNoFearCombatComponent>() : nullptr)
	{
		const float CombatScale = State.Lod == ENoFearAILod::Far ? Tuning.FarCombatScale : State.Lod == ENoFearAILod::Mid ? Tuning.MidCombatScale : 1.f;
		Combat->SetComponentTickInterval(Combat->UpdateInterval * CombatScale);
	}

	if (Mesh)
	{
		const bool bUro = bEnabled ? Tuning.bAnimUpdateRateOptimization : State.bOriginalUro;
		// Параметры URO сетка создаёт при регистрации всегда — достаточно флага.
		Mesh->bEnableUpdateRateOptimizations = bUro;

		EVisibilityBasedAnimTickOption TickOption = State.OriginalTickOption;
		if (bEnabled && State.Lod != ENoFearAILod::Near && TickOption == EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones)
		{
			// За кадром кости не пересчитываем (поза всё равно тикает).
			TickOption = EVisibilityBasedAnimTickOption::AlwaysTickPose;
		}
		if (bEnabled && State.Lod == ENoFearAILod::Far && Tuning.bFreezeFarOffscreenPose)
		{
			TickOption = EVisibilityBasedAnimTickOption::OnlyTickMontagesWhenNotRendered;
		}
		Mesh->VisibilityBasedAnimTickOption = TickOption;
	}

	State.bApplied = true;
	State.AppliedLod = State.Lod;
	State.bAppliedEnabled = bEnabled;
}

void UNoFearAILodSubsystem::Restore(FBotState& State)
{
	if (!State.bApplied)
	{
		return;
	}
	if (USkeletalMeshComponent* Mesh = GetBotMesh(State.Pawn.Get()))
	{
		Mesh->bEnableUpdateRateOptimizations = State.bOriginalUro;
		Mesh->VisibilityBasedAnimTickOption = State.OriginalTickOption;
	}
	if (APawn* Pawn = State.Pawn.Get())
	{
		if (UNoFearCombatComponent* Combat = Pawn->FindComponentByClass<UNoFearCombatComponent>())
		{
			Combat->SetComponentTickInterval(Combat->UpdateInterval);
		}
	}
	State.bApplied = false;
}
