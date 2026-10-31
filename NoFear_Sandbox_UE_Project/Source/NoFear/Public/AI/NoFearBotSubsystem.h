#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "NoFearBotSubsystem.generated.h"

class ANoFearAIController;
class ANoFearCharacter;
class ANoFearGrenade;

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

	/** Гранаты регистрируются сами (ANoFearGrenade::BeginPlay / EndPlay). */
	void RegisterGrenade(ANoFearGrenade* Grenade);
	void UnregisterGrenade(ANoFearGrenade* Grenade);

	/** Брошенные гранаты, которые ещё не взорвались (HUD рисует по ним значок опасности, боты — от них убегают). */
	UFUNCTION(BlueprintPure, Category = "NoFear|AI")
	TArray<ANoFearGrenade*> GetLiveGrenades() const;

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

	/**
	 * Жетон атаки: одновременно бить одну цель могут не больше MaxAttackersPerTarget монстров, остальные кружат рядом.
	 * true — жетон у Attacker (выдан сейчас или уже был). Пока жетонов нет, Attacker записывается в очередь.
	 */
	UFUNCTION(BlueprintCallable, Category = "NoFear|AI")
	bool RequestAttackToken(AActor* Attacker, AActor* Target);

	/**
	 * Вернуть жетон. bPassOn — атакующий только что ударил: если жетона кто-то ждёт, сам он снова попросить
	 * сможет только через TokenPassDelay, и жетон перейдёт к ожидающему.
	 */
	UFUNCTION(BlueprintCallable, Category = "NoFear|AI")
	void ReleaseAttackToken(AActor* Attacker, AActor* Target, bool bPassOn = false);

	/** Вернуть все жетоны Attacker (сменил цель, погиб). */
	void ReleaseAllAttackTokens(AActor* Attacker);

	UFUNCTION(BlueprintPure, Category = "NoFear|AI")
	bool HasAttackToken(const AActor* Attacker, const AActor* Target) const;

	/** Сколько монстров сейчас держат жетон на эту цель. */
	UFUNCTION(BlueprintPure, Category = "NoFear|AI")
	int32 GetAttackTokenCount(const AActor* Target) const;

	/** Сколько жетонов атаки на одну цель. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NoFear|AI", meta = (ClampMin = "1"))
	int32 MaxAttackersPerTarget = 2;

	/**
	 * Держатель, который не ударил за столько секунд, теряет жетон, если его ждут другие (застрял, не может дойти
	 * или достать цель). Иначе двое застрявших держали бы жетоны вечно, а остальные кружили бы вокруг.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NoFear|AI", meta = (ClampMin = "1"))
	float TokenHoldTimeout = 8.f;

	/** Ударивший монстр уступает жетон ожидающему на столько секунд. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NoFear|AI", meta = (ClampMin = "0"))
	float TokenPassDelay = 2.f;

	// --- Бронь укрытий ---

	/** Занять укрытие: другие стрелки не выберут точку ближе ReserveRadius к нему. Прежняя бронь Owner снимается. */
	void ReserveCover(AActor* Owner, const FVector& Location);

	/** Снять бронь Owner (ушёл из укрытия, погиб). */
	void ReleaseCover(AActor* Owner);

	/** Занята ли точка чужой бронью (ближе Radius). Бронь Except не считается. */
	bool IsCoverReserved(const FVector& Location, float Radius, const AActor* Except) const;

	/** Бронь Owner. false — нет брони. */
	UFUNCTION(BlueprintPure, Category = "NoFear|AI")
	bool GetReservedCover(const AActor* Owner, FVector& OutLocation) const;

	/** Сколько укрытий сейчас занято. */
	UFUNCTION(BlueprintPure, Category = "NoFear|AI")
	int32 GetReservedCoverCount() const;

	/** Пуля пролетела от Start до End: боты ближе NearMissRadius к её пути получают опасность NearMiss. */
	static void ReportBulletPath(const UObject* WorldContextObject, AActor* Shooter, const FVector& Start, const FVector& End);

	/** Насколько близко должна пройти пуля, чтобы её «услышали», см. */
	static constexpr float NearMissRadius = 250.f;

	/** На каком расстоянии замечают драку (удар монстра, попадание по боту), см. */
	static constexpr float FightNoticeRadius = 1500.f;

	/** Во сколько раз дальность шума падает, если между источником и слушателем стена. */
	static constexpr float WallNoiseFactor = 0.6f;

private:
	struct FAttackTokens
	{
		TArray<TWeakObjectPtr<AActor>> Holders;
		/** Когда держатель получил жетон. */
		TMap<TWeakObjectPtr<AActor>, double> HeldSince;
		/** Кто просил жетон и не получил, и когда. */
		TMap<TWeakObjectPtr<AActor>, double> Waiting;
		/** Кто уступил жетон и до какого времени не может попросить снова. */
		TMap<TWeakObjectPtr<AActor>, double> PassedUntil;
	};

	/** Убрать погибших и исчезнувших держателей. */
	void PruneTokens(FAttackTokens& Tokens, double Now) const;

	TArray<TWeakObjectPtr<ANoFearAIController>> Bots;
	TMap<TWeakObjectPtr<AActor>, FAttackTokens> AttackTokens;
	TArray<TWeakObjectPtr<ANoFearCharacter>> Corpses;
	TArray<TWeakObjectPtr<ANoFearGrenade>> Grenades;
	TMap<TWeakObjectPtr<AActor>, FVector> CoverReservations;
};
