#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "NoFearBotSubsystem.generated.h"

class ANoFearAIController;
class ANoFearCharacter;

/** Откуда шум. Контроллеры по типу решают, насколько он их насторожил. */
UENUM(BlueprintType)
enum class ENoFearNoiseType : uint8
{
	/** Шаги: слышно близко, громче при беге. Только повышают подозрение. */
	Footstep,
	/** Приземление после прыжка. */
	Landing,
	/** Выстрел: выдаёт позицию стрелка сразу. */
	Gunshot,
	Other
};

/**
 * Реестр ботов и трупов в мире, шум и общая тревога.
 * Боты регистрируются сами (ANoFearAIController::OnPossess), трупы — ANoFearCharacter при смерти.
 * HUD обходит список ботов отсюда, а не ищет их по миру каждый кадр.
 */
UCLASS()
class NOFEAR_API UNoFearBotSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	static UNoFearBotSubsystem* Get(const UObject* WorldContextObject);

	void RegisterBot(ANoFearAIController* Bot);
	void UnregisterBot(ANoFearAIController* Bot);

	/** Контроллеры ботов с живым телом. */
	TArray<ANoFearAIController*> GetBots() const;

	void RegisterCorpse(ANoFearCharacter* Character);

	/** Тела погибших (кроме уже исчезнувших). */
	TArray<ANoFearCharacter*> GetCorpses() const;

	/**
	 * Шум. Все враги Source в пределах Range получают его (через стену слышно тише) и настораживаются:
	 * шаги и приземление только повышают подозрение, выстрел выдаёт стрелка сразу.
	 */
	UFUNCTION(BlueprintCallable, Category = "NoFear|AI", meta = (WorldContext = "WorldContextObject"))
	static void ReportNoise(const UObject* WorldContextObject, AActor* Source, FVector Location, float Range, ENoFearNoiseType Type = ENoFearNoiseType::Other);

	/** Общая тревога: свои Origin в радиусе его AlertShareRadius узнают об угрозе. */
	void BroadcastAlarm(const ANoFearAIController* Origin, AActor* Threat, const FVector& ThreatLocation);

	/** Victim получил урон от Attacker: свои пострадавшего узнают нападавшего, свои нападавшего — пострадавшего. */
	void NotifyAttack(AActor* Victim, AActor* Attacker);

	/** Во сколько раз дальность шума падает, если между источником и слушателем стена. */
	static constexpr float WallNoiseFactor = 0.6f;

private:
	TArray<TWeakObjectPtr<ANoFearAIController>> Bots;
	TArray<TWeakObjectPtr<ANoFearCharacter>> Corpses;
};
