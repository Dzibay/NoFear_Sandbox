#pragma once

#include "CoreMinimal.h"
#include "AI/NoFearBotSubsystem.h"
#include "Core/NoFearTeam.h"
#include "Subsystems/WorldSubsystem.h"
#include "NoFearFearSubsystem.generated.h"

class UNoFearFearComponent;

/** Числа страха людей (этап 11): Project Settings → Game → NoFear AI → Fear. */
USTRUCT(BlueprintType)
struct FNoFearFearTuning
{
	GENERATED_BODY()

	// --- Пороги ---

	/** С этого страха — тревога: мирный настороже, у стрелков ниже мораль. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fear", meta = (ClampMin = "0", ClampMax = "100"))
	float AnxiousThreshold = 30.f;

	/** С этого страха — паника: мирный бежит и прячется, стрелок хуже целится. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fear", meta = (ClampMin = "0", ClampMax = "100"))
	float PanicThreshold = 60.f;

	/** С этого страха — ужас. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fear", meta = (ClampMin = "0", ClampMax = "100"))
	float TerrorThreshold = 85.f;

	// --- Кто как пугается ---

	/** Множитель прироста страха у мирных и персонажей без команды. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fear|Teams", meta = (ClampMin = "0"))
	float CivilianGainScale = 1.f;

	/** У враждебных стрелков. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fear|Teams", meta = (ClampMin = "0"))
	float EnemyGainScale = 0.5f;

	/** У союзников — стражей с браслетами: страх растёт медленнее всех. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fear|Teams", meta = (ClampMin = "0"))
	float AllyGainScale = 0.35f;

	/** У героя (эффекты страха героя — этап 12). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fear|Teams", meta = (ClampMin = "0"))
	float PlayerGainScale = 0.6f;

	/** Выше этого стрелки (союзники, враги, герой) не пугаются от людей и перестрелки — только от монстров. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fear|Teams", meta = (ClampMin = "0", ClampMax = "100"))
	float CombatFearCap = 45.f;

	// --- Спад ---

	/** Через столько секунд без нового испуга страх начинает спадать. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fear|Decay", meta = (ClampMin = "0"))
	float DecayDelay = 6.f;

	/** На столько в секунду спадает страх. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fear|Decay", meta = (ClampMin = "0"))
	float DecayRate = 2.5f;

	/** Рядом страж (союзник или герой) ближе этого — спад быстрее и начинается раньше, см. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fear|Decay", meta = (ClampMin = "0"))
	float GuardianRadius = 1200.f;

	/** Во сколько раз быстрее спад рядом со стражем (и во столько же раз короче DecayDelay, но не меньше половины). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fear|Decay", meta = (ClampMin = "1"))
	float GuardianDecayMultiplier = 2.5f;

	/** Спокойные люди ближе этого немного гасят страх соседа, см. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fear|Decay", meta = (ClampMin = "0"))
	float CalmNeighbourRadius = 800.f;

	/** Прибавка к скорости спада за каждого спокойного соседа (доля). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fear|Decay", meta = (ClampMin = "0"))
	float CalmNeighbourBonus = 0.15f;

	/** Больше этой прибавки от спокойных соседей не бывает. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fear|Decay", meta = (ClampMin = "0"))
	float CalmNeighbourMaxBonus = 0.45f;

	// --- Передача ---

	/** Человек в панике пугает соседей ближе этого, см. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fear|Spread", meta = (ClampMin = "0"))
	float SpreadRadius = 800.f;

	/** Передаёт страх тот, у кого не меньше этого... */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fear|Spread", meta = (ClampMin = "0", ClampMax = "100"))
	float SpreadMinFear = 60.f;

	/** ...и кто сам испугался (не от соседа) за последние столько секунд. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fear|Spread", meta = (ClampMin = "0"))
	float SpreadRecentTime = 5.f;

	/** Сколько страха в секунду получает сосед от каждого паникующего. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fear|Spread", meta = (ClampMin = "0"))
	float SpreadRate = 6.f;

	/** Больше скольких паникующих рядом передача не растёт. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fear|Spread", meta = (ClampMin = "1"))
	int32 SpreadMaxSources = 3;

	/** Выше этого от соседей не напугаться. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fear|Spread", meta = (ClampMin = "0", ClampMax = "100"))
	float SpreadCap = 75.f;

	// --- События (страх / потолок: событие не поднимает страх выше потолка) ---

	/** Выстрел ближе этого — «близкий», см. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fear|Events", meta = (ClampMin = "0"))
	float NearGunshotRadius = 1500.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fear|Events", meta = (ClampMin = "0"))
	float NearGunshotFear = 70.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fear|Events", meta = (ClampMin = "0", ClampMax = "100"))
	float NearGunshotCap = 100.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fear|Events", meta = (ClampMin = "0"))
	float FarGunshotFear = 35.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fear|Events", meta = (ClampMin = "0", ClampMax = "100"))
	float FarGunshotCap = 50.f;

	/** Пуля пролетела рядом. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fear|Events", meta = (ClampMin = "0"))
	float NearMissFear = 65.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fear|Events", meta = (ClampMin = "0", ClampMax = "100"))
	float NearMissCap = 90.f;

	/** Драка рядом (удар монстра, попадание в соседа). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fear|Events", meta = (ClampMin = "0"))
	float FightFear = 60.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fear|Events", meta = (ClampMin = "0", ClampMax = "100"))
	float FightCap = 85.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fear|Events", meta = (ClampMin = "0"))
	float ExplosionFear = 70.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fear|Events", meta = (ClampMin = "0", ClampMax = "100"))
	float ExplosionCap = 100.f;

	/** Граната упала рядом. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fear|Events", meta = (ClampMin = "0"))
	float GrenadeFear = 65.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fear|Events", meta = (ClampMin = "0", ClampMax = "100"))
	float GrenadeCap = 90.f;

	/** Крик соседа или рёв монстра: ScreamNearFear ближе ScreamNearRadius, к ScreamFarRadius падает до ScreamFarFear. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fear|Events", meta = (ClampMin = "0"))
	float ScreamNearFear = 65.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fear|Events", meta = (ClampMin = "0"))
	float ScreamFarFear = 35.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fear|Events", meta = (ClampMin = "0"))
	float ScreamNearRadius = 1000.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fear|Events", meta = (ClampMin = "0"))
	float ScreamFarRadius = 2500.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fear|Events", meta = (ClampMin = "0", ClampMax = "100"))
	float ScreamCap = 75.f;

	/** Увидел труп. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fear|Events", meta = (ClampMin = "0"))
	float CorpseFear = 65.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fear|Events", meta = (ClampMin = "0", ClampMax = "100"))
	float CorpseCap = 70.f;

	/** Ранен. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fear|Events", meta = (ClampMin = "0"))
	float DamageFear = 60.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fear|Events", meta = (ClampMin = "0", ClampMax = "100"))
	float DamageCap = 100.f;

	// --- Монстры на виду ---

	/** Монстр рядом (ближе MonsterNearDistance) — всё по «ближним» числам, дальше MonsterFarDistance — по «дальним», см. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fear|Monsters", meta = (ClampMin = "0"))
	float MonsterNearDistance = 500.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fear|Monsters", meta = (ClampMin = "0"))
	float MonsterFarDistance = 3000.f;

	/** Разовый испуг, когда монстра впервые разглядели. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fear|Monsters", meta = (ClampMin = "0"))
	float MonsterShockNear = 70.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fear|Monsters", meta = (ClampMin = "0"))
	float MonsterShockFar = 35.f;

	/** Пока монстр на виду — страх в секунду (по ближайшему). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fear|Monsters", meta = (ClampMin = "0"))
	float MonsterSightRateNear = 40.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fear|Monsters", meta = (ClampMin = "0"))
	float MonsterSightRateFar = 8.f;

	/** Потолок страха от монстра на виду. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fear|Monsters", meta = (ClampMin = "0", ClampMax = "100"))
	float MonsterSightCapNear = 100.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fear|Monsters", meta = (ClampMin = "0", ClampMax = "100"))
	float MonsterSightCapFar = 80.f;

	/** Мирный заметил что-то похожее на монстра (не разглядел или слышал). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fear|Monsters", meta = (ClampMin = "0"))
	float SuspiciousFear = 35.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fear|Monsters", meta = (ClampMin = "0", ClampMax = "100"))
	float SuspiciousCap = 40.f;

	// --- Стрелки ---

	/** Мораль отряда ниже на столько при среднем страхе бойцов 100 (от AnxiousThreshold — линейно). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fear|Shooters", meta = (ClampMin = "0", ClampMax = "1"))
	float MoralePenalty = 0.3f;

	/** Разброс стрелка при страхе 100 (от PanicThreshold — линейно от 1). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fear|Shooters", meta = (ClampMin = "1"))
	float PanicSpreadMultiplier = 1.6f;

	// --- Успокоение героем ---

	/** Сколько страха в секунду снимает герой, удерживая E. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fear|Calming", meta = (ClampMin = "0"))
	float CalmRate = 35.f;

	/** Успокаивать можно ближе этого, см. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fear|Calming", meta = (ClampMin = "0"))
	float CalmDistance = 350.f;

	/** Успокоение заканчивается, когда страх ниже этого. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fear|Calming", meta = (ClampMin = "0", ClampMax = "100"))
	float CalmTargetFear = 15.f;

	// --- Обновление ---

	/** Как часто считать спад и передачу, сек. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fear", meta = (ClampMin = "0.05"))
	float UpdateInterval = 0.2f;
};

/**
 * Страх людей (этап 11). Хранит все UNoFearFearComponent мира и несколько раз в секунду:
 *  - гасит страх тех, кого давно не пугали (быстрее рядом со стражем и спокойными соседями);
 *  - передаёт панику: кто сам испугался и паникует, пугает соседей;
 *  - при NoFear.Fear.Debug 1 рисует число страха над каждым.
 * Числа событий (выстрел, крик, монстр) тоже здесь — их спрашивают контроллеры ботов.
 */
UCLASS()
class NOFEAR_API UNoFearFearSubsystem : public UTickableWorldSubsystem, public FNoFearFearTuning
{
	GENERATED_BODY()

public:
	/** Копирует числа из Project Settings → NoFear AI → Fear. */
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;

	UFUNCTION(BlueprintPure, Category = "NoFear|Fear", meta = (WorldContext = "WorldContextObject", DisplayName = "Get Fear Subsystem"))
	static UNoFearFearSubsystem* Get(const UObject* WorldContextObject);

	/** Текущие числа страха (копия). */
	UFUNCTION(BlueprintPure, Category = "NoFear|Fear")
	FNoFearFearTuning GetTuning() const { return *this; }

	/** Заменить числа страха на ходу, до конца этого мира. */
	UFUNCTION(BlueprintCallable, Category = "NoFear|Fear")
	void SetTuning(const FNoFearFearTuning& NewTuning) { static_cast<FNoFearFearTuning&>(*this) = NewTuning; }

	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;

	void RegisterFear(UNoFearFearComponent* Component);
	void UnregisterFear(UNoFearFearComponent* Component);

	/** Посчитать спад и передачу за DeltaTime сейчас (раз в UpdateInterval это происходит само). */
	UFUNCTION(BlueprintCallable, Category = "NoFear|Fear")
	void UpdateFear(float DeltaTime);

	/** Множитель прироста страха команды: 0 — не пугается вовсе (монстры). */
	UFUNCTION(BlueprintPure, Category = "NoFear|Fear")
	float GetTeamGainScale(ENoFearTeam Team) const;

	/** Сколько страха даёт опасность Type на расстоянии Distance и выше какого потолка не поднимает. */
	UFUNCTION(BlueprintPure, Category = "NoFear|Fear")
	void GetDangerFear(ENoFearDangerType Type, float Distance, float& OutAmount, float& OutCap) const;

	/** Монстр на виду на расстоянии Distance: разовый испуг, страх в секунду и потолок. */
	UFUNCTION(BlueprintPure, Category = "NoFear|Fear")
	void GetMonsterSightFear(float Distance, float& OutShock, float& OutRate, float& OutCap) const;

	/** Рядом с Actor есть живой страж (союзник или герой, не он сам). */
	UFUNCTION(BlueprintPure, Category = "NoFear|Fear")
	bool IsGuardianNear(const AActor* Actor) const;

	/** Средний страх живых персонажей команды Team (0, если их нет). */
	UFUNCTION(BlueprintPure, Category = "NoFear|Fear")
	float GetAverageFear(ENoFearTeam Team) const;

	/** Команда пугает своим видом (монстры). */
	static bool IsFrightening(ENoFearTeam Team) { return Team == ENoFearTeam::Monster || Team == ENoFearTeam::AggroMonster; }

	/** Команда — стрелки: страх от людей и перестрелки у них не выше CombatFearCap. */
	static bool IsShooterTeam(ENoFearTeam Team) { return Team == ENoFearTeam::Ally || Team == ENoFearTeam::Enemy || Team == ENoFearTeam::Player; }

	/** Команда — стражи: рядом с ними люди успокаиваются быстрее. */
	static bool IsGuardianTeam(ENoFearTeam Team) { return Team == ENoFearTeam::Ally || Team == ENoFearTeam::Player; }

private:
	void DrawDebug() const;

	TArray<TWeakObjectPtr<UNoFearFearComponent>> Components;
	float TimeSinceUpdate = 0.f;
};
