#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "NoFearPortalTypes.generated.h"

/** Ступень портала по размеру: от неё зависит, кого он выпускает. */
UENUM(BlueprintType)
enum class ENoFearPortalStage : uint8
{
	/** Трещина: растёт, но монстров ещё нет. */
	Rift,
	/** Малый: одиночные монстры. */
	Small,
	/** Средний: стайки с прыгунами и плевунами. */
	Medium,
	/** Большой: танки и чаще. */
	Large
};

/**
 * Порталы страха (этап 13): человек в непрерывном ужасе ломается и становится проводником, портал растёт от проводников
 * и по ступеням выпускает монстров, без проводников затягивается и забирает монстров обратно.
 * Project Settings → Game → NoFear AI → Portal.
 */
USTRUCT(BlueprintType)
struct FNoFearPortalTuning
{
	GENERATED_BODY()

	// --- Слом ---

	/** С этого страха копится слом. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Portal|Break", meta = (ClampMin = "0", ClampMax = "100"))
	float BreakFear = 99.5f;

	/** Сколько секунд непрерывного ужаса ломает человека. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Portal|Break", meta = (ClampMin = "0.1"))
	float BreakTime = 3.f;

	/** Стрелкам (союзникам, врагам) — столько: обучены держаться, ломаются реже мирных. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Portal|Break", meta = (ClampMin = "0.1"))
	float ShooterBreakTime = 6.f;

	/**
	 * Ужас непрерывен, пока прямой испуг (монстр рядом на виду, сердце портала, стрельба над ухом) повторяется не реже раза
	 * в столько секунд. Страх 100 держится и после испуга (задержка спада), но успевший убежать не ломается.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Portal|Break", meta = (ClampMin = "0.1"))
	float BreakFreshTime = 1.5f;

	/** Во сколько раз быстрее слом спадает, чем копится. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Portal|Break", meta = (ClampMin = "0"))
	float BreakDrainScale = 1.f;

	/** Освобождённый столько секунд не ломается снова. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Portal|Break", meta = (ClampMin = "0"))
	float BreakImmunityTime = 20.f;

	/** Сломавшийся ближе этого к открытому порталу питает его, а не открывает новый. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Portal|Break", meta = (ClampMin = "0"))
	float JoinRadius = 1800.f;

	/** Больше этого порталов не бывает: сломавшийся питает ближайший, как бы далеко тот ни был. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Portal|Break", meta = (ClampMin = "1"))
	int32 MaxPortals = 3;

	/** Новый портал открывается не раньше, чем через столько секунд после прошлого. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Portal|Break", meta = (ClampMin = "0"))
	float OpenCooldown = 8.f;

	/** Разлом открывается на таком расстоянии от сломавшегося, см. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Portal|Break", meta = (ClampMin = "0"))
	float OpenDistanceMin = 350.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Portal|Break", meta = (ClampMin = "0"))
	float OpenDistanceMax = 600.f;

	// --- Рост ---

	/** Размер нового разлома (0–100). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Portal|Growth", meta = (ClampMin = "0", ClampMax = "100"))
	float StartSize = 5.f;

	/** Сколько размера в секунду даёт один проводник. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Portal|Growth", meta = (ClampMin = "0"))
	float FeedRate = 1.f;

	/** Проводники складываются с убылью: рост = FeedRate × N^FeedExponent. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Portal|Growth", meta = (ClampMin = "0", ClampMax = "1"))
	float FeedExponent = 0.8f;

	/** Без проводников портал сжимается на столько в секунду; в 0 — затягивается. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Portal|Growth", meta = (ClampMin = "0"))
	float StarveRate = 3.f;

	/** С этого размера — малый портал (раньше — трещина без монстров). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Portal|Growth", meta = (ClampMin = "0", ClampMax = "100"))
	float SmallSize = 20.f;

	/** С этого размера — средний. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Portal|Growth", meta = (ClampMin = "0", ClampMax = "100"))
	float MediumSize = 45.f;

	/** С этого размера — большой. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Portal|Growth", meta = (ClampMin = "0", ClampMax = "100"))
	float LargeSize = 75.f;

	// --- Аура ---

	/** Радиус ауры страха: AuraRadius + AuraRadiusPerSize × размер, см. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Portal|Aura", meta = (ClampMin = "0"))
	float AuraRadius = 800.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Portal|Aura", meta = (ClampMin = "0"))
	float AuraRadiusPerSize = 12.f;

	/** Доля радиуса у сердца портала: там испуг прямой и доводит до 100 (стоящего там ломает). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Portal|Aura", meta = (ClampMin = "0", ClampMax = "1"))
	float AuraCoreFraction = 0.35f;

	/** Страх в секунду у сердца; к краю — вдвое меньше. Множитель команды действует, потолок стрелков — нет. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Portal|Aura", meta = (ClampMin = "0"))
	float AuraFearRate = 10.f;

	/** Потолок страха у сердца. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Portal|Aura", meta = (ClampMin = "0", ClampMax = "100"))
	float AuraCoreCap = 100.f;

	/** Потолок страха на краю ауры. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Portal|Aura", meta = (ClampMin = "0", ClampMax = "100"))
	float AuraEdgeCap = 80.f;

	/** Проводник пугает соседей в этом радиусе (как паника соседа: не прямой испуг, не ломает). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Portal|Aura", meta = (ClampMin = "0"))
	float FeederFearRadius = 900.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Portal|Aura", meta = (ClampMin = "0"))
	float FeederFearRate = 5.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Portal|Aura", meta = (ClampMin = "0", ClampMax = "100"))
	float FeederFearCap = 80.f;

	/** Портал затянулся: страх людей в его ауре падает на столько. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Portal|Aura", meta = (ClampMin = "0"))
	float CloseRelief = 30.f;

	// --- Монстры ---

	/** Малый портал: одиночный монстр раз в столько секунд. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Portal|Monsters", meta = (ClampMin = "1"))
	float SmallSpawnInterval = 18.f;

	/** Живых монстров малого портала не больше этого. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Portal|Monsters", meta = (ClampMin = "0"))
	int32 SmallMaxAlive = 2;

	/** Средний портал: стайка (монстр, прыгун, плевун) раз в столько секунд. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Portal|Monsters", meta = (ClampMin = "1"))
	float MediumSpawnInterval = 20.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Portal|Monsters", meta = (ClampMin = "1"))
	int32 MediumWaveSize = 2;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Portal|Monsters", meta = (ClampMin = "0"))
	int32 MediumMaxAlive = 5;

	/** Большой портал: волна с танком раз в столько секунд. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Portal|Monsters", meta = (ClampMin = "1"))
	float LargeSpawnInterval = 15.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Portal|Monsters", meta = (ClampMin = "1"))
	int32 LargeWaveSize = 3;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Portal|Monsters", meta = (ClampMin = "0"))
	int32 LargeMaxAlive = 8;

	/** Живых танков одного портала не больше этого. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Portal|Monsters", meta = (ClampMin = "0"))
	int32 MaxTanksPerPortal = 1;

	/** Живых монстров всех порталов не больше этого. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Portal|Monsters", meta = (ClampMin = "0"))
	int32 MaxMonsters = 16;

	/** Первый монстр выходит через столько секунд после того, как портал дорос до малого. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Portal|Monsters", meta = (ClampMin = "0"))
	float FirstSpawnDelay = 4.f;

	/** Охотник портала (не стража) уходит от него не дальше MonsterLeashRadius + MonsterLeashPerSize × размер, см. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Portal|Monsters", meta = (ClampMin = "0"))
	float MonsterLeashRadius = 3500.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Portal|Monsters", meta = (ClampMin = "0"))
	float MonsterLeashPerSize = 10.f;

	/** Портал затянулся: столько секунд монстры бегут обратно, не успевшие растворяются. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Portal|Monsters", meta = (ClampMin = "0"))
	float RecallTime = 6.f;

	/** Ближе этого к порталу монстр уходит в него. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Portal|Monsters", meta = (ClampMin = "0"))
	float RecallReach = 220.f;

	/**
	 * Стража портала: столько монстров (малый / средний / большой) стоят у проводников спиной к ним и бросаются
	 * на тех, кто подходит освобождать. Хотя бы один монстр всегда уходит на охоту.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Portal|Monsters", meta = (ClampMin = "0"))
	int32 SmallGuards = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Portal|Monsters", meta = (ClampMin = "0"))
	int32 MediumGuards = 2;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Portal|Monsters", meta = (ClampMin = "0"))
	int32 LargeGuards = 3;

	/** Пост стража — на столько дальше самого дальнего проводника, см. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Portal|Monsters", meta = (ClampMin = "0"))
	float GuardStandOff = 250.f;

	/** Посты считаются по проводникам не дальше этого от края провала (дальние ещё бредут к порталу), см. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Portal|Monsters", meta = (ClampMin = "0"))
	float GuardMaxFeederDistance = 600.f;

	/** Страж бросается на тех, кто ближе к порталу, чем его пост + столько, см. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Portal|Monsters", meta = (ClampMin = "0"))
	float GuardReach = 900.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Portal|Monsters")
	TSoftClassPtr<APawn> MonsterClass = TSoftClassPtr<APawn>(FSoftObjectPath(TEXT("/Game/Blueprints/Bots/CBP_Bot_Monster.CBP_Bot_Monster_C")));

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Portal|Monsters")
	TSoftClassPtr<APawn> JumperClass = TSoftClassPtr<APawn>(FSoftObjectPath(TEXT("/Game/Blueprints/Bots/CBP_Bot_Jumper.CBP_Bot_Jumper_C")));

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Portal|Monsters")
	TSoftClassPtr<APawn> SpitterClass = TSoftClassPtr<APawn>(FSoftObjectPath(TEXT("/Game/Blueprints/Bots/CBP_Bot_Spitter.CBP_Bot_Spitter_C")));

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Portal|Monsters")
	TSoftClassPtr<APawn> TankClass = TSoftClassPtr<APawn>(FSoftObjectPath(TEXT("/Game/Blueprints/Bots/CBP_Bot_Tank.CBP_Bot_Tank_C")));

	// --- Освобождение ---

	/** Сколько секунд герой держит E рядом с проводником (стоит и не стреляет). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Portal|Freeing", meta = (ClampMin = "0.1"))
	float HeroFreeTime = 3.5f;

	/** Герой освобождает с такого расстояния, см (дальше — держание прерывается). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Portal|Freeing", meta = (ClampMin = "0"))
	float HeroFreeDistance = 260.f;

	/** Сколько секунд союзник-страж освобождает проводника. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Portal|Freeing", meta = (ClampMin = "0.1"))
	float GuardianFreeTime = 8.f;

	/** Союзник берётся освобождать проводников не дальше этого. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Portal|Freeing", meta = (ClampMin = "0"))
	float GuardianSearchRadius = 2500.f;

	/** Союзник освобождает с такого расстояния, см. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Portal|Freeing", meta = (ClampMin = "0"))
	float GuardianReach = 160.f;

	/** Не дошёл до проводника за столько секунд — бросает и пробует позже. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Portal|Freeing", meta = (ClampMin = "0"))
	float GuardianApproachTime = 15.f;

	/** Цель ближе этого — сначала бой, освобождение потом. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Portal|Freeing", meta = (ClampMin = "0"))
	float GuardianCombatDistance = 2000.f;

	/** Бросивший освобождение союзник не берётся снова столько секунд. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Portal|Freeing", meta = (ClampMin = "0"))
	float GuardianRetryTime = 5.f;

	/** Освобождение без рук спадает на столько в секунду (доля от полного). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Portal|Freeing", meta = (ClampMin = "0"))
	float FreeProgressDecay = 0.2f;

	/** Освобождённый очнулся: страх в этих пределах, бежит прочь от портала. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Portal|Freeing", meta = (ClampMin = "0", ClampMax = "100"))
	float FreedFearMin = 60.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Portal|Freeing", meta = (ClampMin = "0", ClampMax = "100"))
	float FreedFearMax = 66.f;

	// --- Обновление ---

	/** Как часто считать слом, рост, ауру и монстров, сек. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Portal", meta = (ClampMin = "0.02"))
	float UpdateInterval = 0.1f;
};
