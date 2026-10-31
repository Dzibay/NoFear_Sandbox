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
 * Опасность, о которой узнают все боты рядом, а не только враги источника (в отличие от шума).
 * Мирные по ней пугаются и бегут; остальные пока не реагируют (наследники переопределяют HandleDanger).
 */
UENUM(BlueprintType)
enum class ENoFearDangerType : uint8
{
	/** Выстрел. Location — где стреляли. */
	Gunshot,
	/** Пуля пролетела рядом. Location — где стрелок. */
	NearMiss,
	/** Рядом дерутся (удар, рывок монстра). Location — где нападающий. */
	Fight,
	/** Крик паникующего мирного. Location — от чего он бежит. */
	Scream,
	/** Взрыв. Location — центр. */
	Explosion,
	/** Упала граната. Location — где она лежит. */
	Grenade
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

	/**
	 * Опасность: все боты (кроме самого Source) в пределах Radius от Origin получают HandleDanger(Type, Source, ThreatLocation).
	 * Origin — откуда разносится (выстрел, крик), ThreatLocation — от чего спасаться.
	 */
	UFUNCTION(BlueprintCallable, Category = "NoFear|AI", meta = (WorldContext = "WorldContextObject"))
	static void ReportDanger(const UObject* WorldContextObject, ENoFearDangerType Type, AActor* Source, FVector Origin, float Radius, FVector ThreatLocation);

	/** Пуля пролетела от Start до End: боты ближе NearMissRadius к её пути получают опасность NearMiss. */
	static void ReportBulletPath(const UObject* WorldContextObject, AActor* Shooter, const FVector& Start, const FVector& End);

	/** Насколько близко должна пройти пуля, чтобы её «услышали», см. */
	static constexpr float NearMissRadius = 250.f;

	/** На каком расстоянии замечают драку (удар монстра, попадание по боту), см. */
	static constexpr float FightNoticeRadius = 1500.f;

	/** Во сколько раз дальность шума падает, если между источником и слушателем стена. */
	static constexpr float WallNoiseFactor = 0.6f;

private:
	TArray<TWeakObjectPtr<ANoFearAIController>> Bots;
	TArray<TWeakObjectPtr<ANoFearCharacter>> Corpses;
};
