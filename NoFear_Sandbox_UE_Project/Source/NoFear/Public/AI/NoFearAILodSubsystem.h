#pragma once

#include "CoreMinimal.h"
#include "Components/SkinnedMeshComponent.h"
#include "Subsystems/WorldSubsystem.h"
#include "NoFearAILodSubsystem.generated.h"

class ANoFearAIController;
class APawn;

/** Уровень детализации бота: насколько часто он думает и анимируется. */
UENUM(BlueprintType)
enum class ENoFearAILod : uint8
{
	/** Рядом с игроком или дерётся с ним: всё каждый кадр. */
	Near,
	/** Средняя дальность: думает реже. */
	Mid,
	/** Далеко: думает редко, за кадром анимация стоит (монтажи идут). */
	Far
};

/**
 * Уровни детализации ботов (этап 10). Раз в Interval сортирует ботов по расстоянию до камеры игрока и:
 * - растягивает интервал размышлений контроллера (SetThinkScale) и проверку линии огня (UNoFearCombatComponent);
 * - включает URO у сетки (кадры анимации пропускаются по размеру на экране, за кадром — почти не считаются);
 * - далёким невидимым ботам ставит OnlyTickMontagesWhenNotRendered (поза не считается, монтажи и их события идут).
 * Бот, который целится в игрока, всегда Near. NoFear.AI.Lod 0 выключает всё (для замеров до/после).
 * Настройки — Project Settings → NoFear AI → Lod.
 */
UCLASS()
class NOFEAR_API UNoFearAILodSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	static UNoFearAILodSubsystem* Get(const UObject* WorldContextObject);

	UFUNCTION(BlueprintPure, Category = "NoFear|AI")
	ENoFearAILod GetBotLod(const ANoFearAIController* Bot) const;

	/** Сколько ботов на каждом уровне (по последнему пересчёту). */
	void GetTierCounts(int32& OutNear, int32& OutMid, int32& OutFar) const;

	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Deinitialize() override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual bool IsTickableInEditor() const override { return false; }

private:
	struct FBotState
	{
		TWeakObjectPtr<APawn> Pawn;
		ENoFearAILod Lod = ENoFearAILod::Near;
		EVisibilityBasedAnimTickOption OriginalTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPose;
		bool bOriginalUro = false;
		bool bApplied = false;
		/** Применённое состояние (чтобы не трогать компоненты без нужды). */
		ENoFearAILod AppliedLod = ENoFearAILod::Near;
		bool bAppliedEnabled = false;
	};

	void UpdateTiers();
	void Apply(ANoFearAIController* Bot, FBotState& State, bool bEnabled);
	static void Restore(FBotState& State);

	TMap<TWeakObjectPtr<ANoFearAIController>, FBotState> States;
	float Accumulator = 0.f;
	int32 Counts[3] = {0, 0, 0};
};
