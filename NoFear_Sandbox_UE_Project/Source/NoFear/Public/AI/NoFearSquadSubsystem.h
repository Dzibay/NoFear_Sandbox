#pragma once

#include "CoreMinimal.h"
#include "Core/NoFearTeam.h"
#include "Subsystems/WorldSubsystem.h"
#include "NoFearSquadSubsystem.generated.h"

class ANoFearShooterAIController;

/**
 * Тактика отряда: что командир решил делать сейчас. Выбирается по шкале морали и обстановке
 * (UNoFearSquadSubsystem::UpdateTactic), над командиром видна словом.
 */
UENUM(BlueprintType)
enum class ENoFearSquadTactic : uint8
{
	/** Боя нет: каждый на своём посту. */
	None,
	/** Первый контакт: все в укрытие лицом к угрозе. */
	Contact,
	/** Обычный бой: каждый воюет из своего укрытия, отряд только помогает своим. */
	Engage,
	/** Перебежки: одна группа подавляет, другая перебегает в укрытие ближе к врагу, потом меняются. */
	Bounding,
	/** Фланг: штурмовик скрытно обходит засевшего врага, остальные прижимают его огнём. */
	Flank,
	/** Натиск: враг перезаряжается, ранен или прижат — сблизиться, пока он не может ответить. */
	Rush,
	/** Оборона: держать укрытия, не выходить вперёд. */
	Defend,
	/** Отход перекатами: часть прикрывает, часть отходит к рубежу, потом меняются. */
	Fallback,
	/** Сбор после боя у командира: перевязка, потом — по постам. */
	Regroup,
	/**
	 * Спасение у портала (только союзники): отряд зачищает монстров у проводника, потом один боец выводит его из транса
	 * (или успокаивает паникующего мирного), остальные прикрывают. Монстров больше, чем бойцов, — не идут.
	 */
	Rescue
};

/** Приказ бойцу. Слово приказа видно в панели над головой рядом со знаком роли. */
UENUM(BlueprintType)
enum class ENoFearSquadOrder : uint8
{
	None,
	/** В укрытие лицом к угрозе (контакт). */
	TakeCover,
	/** Огонь на подавление по врагу, даже не видя его (по сведениям отряда). */
	Suppress,
	/** Перебежка в укрытие ближе к врагу. */
	Bound,
	/** Обход в заданное командиром укрытие сбоку от врага. */
	Flank,
	/** Натиск: рывок в укрытие поближе, нет укрытия — в открытую. */
	Rush,
	/** Держать позицию. */
	Hold,
	/** Отход к рубежу (своя очередь переката). */
	FallBack,
	/** Прикрывать отходящих огнём (очередь переката — у других). */
	CoverFallback,
	/** Прикрыть огнём своего, который просил (раненый отходит). */
	CoveringFire,
	/** На подмогу прижатому своему: в укрытие рядом с ним. */
	Reinforce,
	/** Перевязать раненого своего. */
	Heal,
	/** Сбор у командира после боя. */
	Regroup,
	/** Помощь соседнему отряду. */
	Assist,
	/** Вывести проводника из транса (ведёт подсистема порталов). */
	Free,
	/** Успокоить паникующего мирного у портала (ведёт подсистема порталов). */
	Calm,
	/** Прикрыть спасающего: укрытие рядом с проводником, лицом к порталу. */
	CoverRescue
};

/** Приказ бойца целиком (для контроллера, тестов и Blueprint). */
USTRUCT(BlueprintType)
struct NOFEAR_API FNoFearSquadOrderInfo
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "NoFear|Squad")
	ENoFearSquadOrder Order = ENoFearSquadOrder::None;

	/** Точка приказа: место в сборе, рубеж отхода, укрытие на фланге, куда идти на помощь. */
	UPROPERTY(BlueprintReadOnly, Category = "NoFear|Squad")
	FVector Location = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "NoFear|Squad")
	bool bHasLocation = false;

	/** Свой, к кому приказ: кого прикрыть, перевязать, к кому на подмогу. */
	UPROPERTY(BlueprintReadOnly, Category = "NoFear|Squad")
	TObjectPtr<AActor> Target = nullptr;

	/** Враг, по которому работать (подавить, прижать, обойти). */
	UPROPERTY(BlueprintReadOnly, Category = "NoFear|Squad")
	TObjectPtr<AActor> Enemy = nullptr;

	/** Где этот враг по сведениям отряда. */
	UPROPERTY(BlueprintReadOnly, Category = "NoFear|Squad")
	FVector EnemyLocation = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "NoFear|Squad")
	bool bHasEnemyLocation = false;

	/** Куда смотреть без цели (прикрывающие спасение — на портал). */
	UPROPERTY(BlueprintReadOnly, Category = "NoFear|Squad")
	FVector FacingLocation = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "NoFear|Squad")
	bool bHasFacing = false;

	/** Когда отдан (время мира, с). */
	UPROPERTY(BlueprintReadOnly, Category = "NoFear|Squad")
	double Time = 0.0;

	/** Растёт с каждым новым приказом бойцу: так контроллер видит, что приказ сменился. */
	UPROPERTY(BlueprintReadOnly, Category = "NoFear|Squad")
	int32 Serial = 0;
};

/** Боец отряда глазами интерфейса: роль, командир, приказ, тактика и мораль отряда. */
struct FNoFearSquadMemberView
{
	ENoFearSquadRole Role = ENoFearSquadRole::Rifleman;
	/** Роль до последней смены (кольцо перекрашивается из неё). */
	ENoFearSquadRole PreviousRole = ENoFearSquadRole::Rifleman;
	/** Когда боец встал в строй: при сборе отряда или когда догнал уже собранный. */
	double JoinTime = -1.0e9;
	double RoleChangeTime = -1.0e9;
	/** Когда отряд сменил командира (погиб прежний). */
	double CommanderChangeTime = -1.0e9;
	const APawn* Commander = nullptr;
	int32 SquadSize = 0;
	/** Выполняет приказ с движением или огнём (кольцо пульсирует). */
	bool bExecutingOrder = false;

	ENoFearSquadOrder Order = ENoFearSquadOrder::None;
	FVector OrderLocation = FVector::ZeroVector;
	bool bHasOrderLocation = false;
	/** Свой, к кому приказ (линия к нему). */
	const AActor* OrderTarget = nullptr;
	FVector OrderEnemyLocation = FVector::ZeroVector;
	bool bHasOrderEnemyLocation = false;
	double OrderTime = -1.0e9;
	/** Командир только что велел бросить гранату (слово «граната» в панели). */
	bool bGrenadeOrder = false;

	ENoFearSquadTactic Tactic = ENoFearSquadTactic::None;
	double TacticChangeTime = -1.0e9;
	/** Мораль отряда 0..1. */
	float Morale = 0.f;
	/** Отряд держит оборону на рубеже после отхода. */
	bool bHoldingLine = false;
	/** Имя отряда, которому боец помогает (пусто — своему). */
	FName AssistingSquad;
};

/**
 * Числа отрядов: тактика, мораль, отход, помощь, гранаты.
 * Хранятся в Project Settings → Game → NoFear AI; подсистема копирует их к себе при создании мира.
 */
USTRUCT(BlueprintType)
struct FNoFearSquadTuning
{
	GENERATED_BODY()

	/** Как часто пересобирать отряды и делиться знанием о врагах, с. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Squad", meta = (ClampMin = "0.05"))
	float UpdateInterval = 0.2f;

	/** Враг, которого кто-то из отряда видел не дольше этого назад, известен всему отряду, с. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Squad", meta = (ClampMin = "0"))
	float SquadIntelTime = 1.f;

	/** Через столько после последнего взгляда отряд забывает врага, с. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Squad", meta = (ClampMin = "0"))
	float EnemyMemoryTime = 30.f;

	/** Бой идёт, пока у кого-то есть цель или врага видели не дольше этого назад, с. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Squad", meta = (ClampMin = "0"))
	float CombatMemoryTime = 8.f;

	/** Поводок бойца в отряде: от центра постов отряда, см. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Squad", meta = (ClampMin = "0"))
	float SquadLeashRadius = 2500.f;

	// --- Мораль ---

	/** Мораль собранного отряда. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Squad|Morale", meta = (ClampMin = "0", ClampMax = "1"))
	float StartMorale = 0.7f;

	/** Не ниже этого — натиск, если враг уязвим. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Squad|Morale", meta = (ClampMin = "0", ClampMax = "1"))
	float RushMorale = 0.65f;

	/** Ниже этого — оборона; выход из неё выше DefendExitMorale. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Squad|Morale", meta = (ClampMin = "0", ClampMax = "1"))
	float DefendMorale = 0.4f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Squad|Morale", meta = (ClampMin = "0", ClampMax = "1"))
	float DefendExitMorale = 0.5f;

	/** Ниже этого — отход перекатами. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Squad|Morale", meta = (ClampMin = "0", ClampMax = "1"))
	float FallbackMorale = 0.2f;

	/** Ниже этого отряд зовёт на помощь соседей (и в обороне, и при отходе — всегда). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Squad|Morale", meta = (ClampMin = "0", ClampMax = "1"))
	float AssistMorale = 0.45f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Squad|Morale", meta = (ClampMin = "0"))
	float DeathMoraleLoss = 0.2f;

	/** Сверх DeathMoraleLoss, если погиб командир. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Squad|Morale", meta = (ClampMin = "0"))
	float CommanderDeathMoraleLoss = 0.25f;

	/** За каждое потерянное «полное здоровье» бойца (половина — вдвое меньше). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Squad|Morale", meta = (ClampMin = "0"))
	float DamageMoraleLoss = 0.2f;

	/** В секунду за каждого прижатого огнём бойца. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Squad|Morale", meta = (ClampMin = "0"))
	float SuppressionMoraleLoss = 0.03f;

	/** Боец убегает от гранаты (не чаще раза в 3 с на бойца). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Squad|Morale", meta = (ClampMin = "0"))
	float GrenadeMoraleLoss = 0.08f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Squad|Morale", meta = (ClampMin = "0"))
	float KillMoraleGain = 0.15f;

	/** Пришла помощь соседнего отряда. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Squad|Morale", meta = (ClampMin = "0"))
	float HelpArrivedMoraleGain = 0.15f;

	/** Подмога добежала до прижатого своего. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Squad|Morale", meta = (ClampMin = "0"))
	float ReinforceMoraleGain = 0.05f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Squad|Morale", meta = (ClampMin = "0"))
	float BandageMoraleGain = 0.05f;

	/** Как быстро мораль в бою тянется к «спокойной» (по числу своих против врагов), в секунду. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Squad|Morale", meta = (ClampMin = "0"))
	float MoraleRecoveryRate = 0.01f;

	/** То же вне боя. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Squad|Morale", meta = (ClampMin = "0"))
	float MoraleRestRate = 0.05f;

	// --- Тактика ---

	/** Как часто командир пересматривает тактику, с (срочное — контакт, отход — сразу). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Squad|Tactics", meta = (ClampMin = "0.2"))
	float TacticThinkInterval = 1.f;

	/** Меньше этого тактика не меняется на равноценную, с. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Squad|Tactics", meta = (ClampMin = "0"))
	float MinTacticTime = 3.f;

	/** Сколько длится «Контакт», с. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Squad|Tactics", meta = (ClampMin = "0"))
	float ContactDuration = 2.5f;

	/** Враг не виден столько — сближаться перебежками, с. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Squad|Tactics", meta = (ClampMin = "0"))
	float BoundHiddenTime = 3.f;

	/** Перебежка дольше этого — смена групп, даже если не все дошли, с. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Squad|Tactics", meta = (ClampMin = "1"))
	float BoundPhaseMaxTime = 8.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Squad|Tactics", meta = (ClampMin = "1"))
	int32 MaxBoundSwaps = 6;

	/** Враг сидит на одном месте (в пределах 300 см) столько — обходить с фланга, с. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Squad|Tactics", meta = (ClampMin = "0"))
	float FlankDugInTime = 5.f;

	/** Укрытие на фланге: угол у врага от направления на отряд не меньше этого, градусы. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Squad|Tactics", meta = (ClampMin = "10", ClampMax = "170"))
	float FlankMinAngle = 60.f;

	/** Не вышло (или вышло) обойти — следующая попытка не раньше, с. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Squad|Tactics", meta = (ClampMin = "0"))
	float FlankCooldown = 8.f;

	/** Обход дольше этого — бросить, с. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Squad|Tactics", meta = (ClampMin = "1"))
	float FlankMaxTime = 20.f;

	/** Дошёл до фланга — столько держит укрытие и бьёт перекрёстным огнём, пока отряд прижимает, с. Потом — обычный бой. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Squad|Tactics", meta = (ClampMin = "0"))
	float FlankFireTime = 6.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Squad|Tactics", meta = (ClampMin = "1"))
	float RushMaxTime = 6.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Squad|Tactics", meta = (ClampMin = "0"))
	float RushCooldown = 10.f;

	/** Враг дальше этого от отряда — натиском не взять, см. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Squad|Tactics", meta = (ClampMin = "0"))
	float RushMaxDistance = 2500.f;

	/** Здоровье ниже этой доли — боец «ранен»: не бегает по приказам, а когда ранены все — отход. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Squad|Tactics", meta = (ClampMin = "0", ClampMax = "1"))
	float WoundedFraction = 0.5f;

	// --- Отход ---

	/** Рубеж — посты отряда, а если они не дальше от врага — на столько прочь от врага, см. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Squad|Fallback", meta = (ClampMin = "300"))
	float FallbackDistance = 1500.f;

	/** Все ближе этого к рубежу — дошли, см. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Squad|Fallback", meta = (ClampMin = "100"))
	float FallbackArriveRadius = 700.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Squad|Fallback", meta = (ClampMin = "1"))
	int32 MaxFallbackSwaps = 4;

	/** Оборона на рубеже: поводок вокруг него, см. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Squad|Fallback", meta = (ClampMin = "100"))
	float FallbackLeashRadius = 900.f;

	/** После отхода снова отходить по морали не раньше, с. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Squad|Fallback", meta = (ClampMin = "0"))
	float FallbackCooldown = 20.f;

	// --- Помощь своим ---

	/** Сколько бойцов прикрывают отходящего. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Squad|Help", meta = (ClampMin = "1"))
	int32 CoveringFireHelpers = 2;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Squad|Help", meta = (ClampMin = "0"))
	float CoveringFireTime = 8.f;

	/** Прижат огнём дольше этого — слать подмогу, с. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Squad|Help", meta = (ClampMin = "0"))
	float ReinforcePinnedTime = 2.f;

	/** Только что ранен и здоровья меньше этой доли — слать подмогу. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Squad|Help", meta = (ClampMin = "0", ClampMax = "1"))
	float ReinforceHealthFraction = 0.6f;

	/** Подмога — из бойцов не дальше этого от прижатого, см. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Squad|Help", meta = (ClampMin = "0"))
	float ReinforceRadius = 2000.f;

	/** Ближе этого к прижатому — подмога пришла, см. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Squad|Help", meta = (ClampMin = "100"))
	float ReinforceArriveRadius = 700.f;

	/** К одному и тому же прижатому — не чаще, с. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Squad|Help", meta = (ClampMin = "0"))
	float ReinforceCooldown = 15.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Squad|Help", meta = (ClampMin = "1"))
	float ReinforceMaxTime = 12.f;

	/** В бою перевязывают раненых ниже этой доли здоровья — до HealCombatTarget. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Squad|Help", meta = (ClampMin = "0", ClampMax = "1"))
	float HealCombatFraction = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Squad|Help", meta = (ClampMin = "0", ClampMax = "1"))
	float HealCombatTarget = 0.7f;

	/** Вне боя — всех ниже этой доли, до полного. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Squad|Help", meta = (ClampMin = "0", ClampMax = "1"))
	float HealRestFraction = 0.95f;

	/** Раненого не перевязывают, пока по нему попадали меньше этого назад, с. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Squad|Help", meta = (ClampMin = "0"))
	float HealSafeTime = 3.f;

	/** Одного и того же — не чаще, с. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Squad|Help", meta = (ClampMin = "0"))
	float HealCooldown = 20.f;

	/** В бою санитар идёт не дальше этого, см. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Squad|Help", meta = (ClampMin = "0"))
	float HealMaxDistance = 2000.f;

	/** Не успел перевязать за это время — приказ снимается, с. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Squad|Help", meta = (ClampMin = "1"))
	float HealOrderTimeout = 25.f;

	// --- Помощь между отрядами ---

	/** Соседний отряд (центр) не дальше этого может прийти на помощь, см. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Squad|Assist", meta = (ClampMin = "0"))
	float AssistRadius = 5000.f;

	/** Помогающие держатся не дальше этого от отряда, которому помогают, см. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Squad|Assist", meta = (ClampMin = "100"))
	float AssistLeashRadius = 2000.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Squad|Assist", meta = (ClampMin = "100"))
	float AssistArriveRadius = 900.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Squad|Assist", meta = (ClampMin = "1"))
	float AssistMaxTime = 60.f;

	/** Звать соседей снова не раньше, с. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Squad|Assist", meta = (ClampMin = "0"))
	float AssistCallCooldown = 30.f;

	// --- Гранаты отряда ---

	/** Враг сидит на месте столько — командир велит бросить гранату, с. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Squad|Grenade", meta = (ClampMin = "0"))
	float GrenadeDugInTime = 4.f;

	/** Дольше этого врага не видели — где он, неизвестно, не бросать, с. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Squad|Grenade", meta = (ClampMin = "0"))
	float GrenadeMaxUnseenTime = 12.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Squad|Grenade", meta = (ClampMin = "0"))
	float GrenadeClusterRadius = 400.f;

	/** Столько врагов кучкой — тоже повод для гранаты. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Squad|Grenade", meta = (ClampMin = "2"))
	int32 GrenadeClusterCount = 2;

	/** Между гранатами отряда, с (случайно в пределах). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Squad|Grenade", meta = (ClampMin = "0"))
	FVector2D SquadGrenadeCooldown = FVector2D(8.f, 12.f);

	/** Первая граната не раньше, чем через столько после контакта, с. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Squad|Grenade", meta = (ClampMin = "0"))
	float FirstSquadGrenadeDelay = 5.f;

	// --- Спасение у портала (только союзники) ---

	/** Отряды союзников выводят проводников из транса и успокаивают паникующих у порталов. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Squad|Rescue")
	bool bPortalRescue = true;

	/** Цель спасения не дальше этого от центра отряда, см. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Squad|Rescue", meta = (ClampMin = "0"))
	float RescueSearchRadius = 3000.f;

	/** Монстры ближе этого к цели (и враги, которых видит отряд, у цели или у отряда) — угроза, см. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Squad|Rescue", meta = (ClampMin = "0"))
	float RescueThreatRadius = 1500.f;

	/** Спасающий идёт, только когда в этом радиусе от цели нет монстров (зачищено), см. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Squad|Rescue", meta = (ClampMin = "0"))
	float RescueClearRadius = 900.f;

	/** Колосс в счёте угрозы — за столько монстров. Угроза больше числа бойцов — не идти. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Squad|Rescue", meta = (ClampMin = "1"))
	float RescueTankWeight = 3.f;

	/** Монстр ближе этого к спасающему — бросить и отбиваться (вернётся, когда снова чисто), см. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Squad|Rescue", meta = (ClampMin = "0"))
	float RescueAbortDistance = 600.f;

	/** Прикрывающие встают вокруг цели на таком расстоянии (по бокам, лицом к порталу), если цель вне ауры портала, см. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Squad|Rescue", meta = (ClampMin = "100"))
	float RescueGuardRadius = 450.f;

	/** Цель в ауре портала — прикрывающие стоят дугой на столько за краем ауры (в ауре страх ломает), см. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Squad|Rescue", meta = (ClampMin = "0"))
	float RescueAuraMargin = 150.f;

	/** Слом бойца отряда дошёл до этой доли — спасение бросить, отряд отходит и не спасает RescueRetryTime. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Squad|Rescue", meta = (ClampMin = "0", ClampMax = "1"))
	float RescueBreakAbort = 0.2f;

	/** Поводок отряда вокруг цели, пока идёт спасение, см. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Squad|Rescue", meta = (ClampMin = "300"))
	float RescueLeashRadius = 1500.f;

	/** Спасающий дважды не дошёл до цели — не браться за неё столько, с. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Squad|Rescue", meta = (ClampMin = "0"))
	float RescueRetryTime = 15.f;

	// --- Сплочённость (только союзники) ---

	/** Бойцы союзного отряда держат строй: свои рядом и монстров не больше, чем бойцов, — монстры не доводят до слома. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Squad|Steady")
	bool bSquadSteady = true;

	/** Страх от монстров у держащего строй не выше этого (ниже порога слома). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Squad|Steady", meta = (ClampMin = "0", ClampMax = "100"))
	float SteadyFearCap = 90.f;

	/** Страх выше потолка строя спадает с такой скоростью, в секунду. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Squad|Steady", meta = (ClampMin = "0"))
	float SteadySootheRate = 15.f;

	/** Свой ближе этого — боец держит строй, см. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Squad|Steady", meta = (ClampMin = "0"))
	float SteadyMateRadius = 1500.f;

	/** Монстры ближе этого к центру отряда — в счёте (колосс за RescueTankWeight). Больше, чем бойцов, — строй не держится, см. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Squad|Steady", meta = (ClampMin = "0"))
	float SteadyThreatRadius = 2500.f;
};

/**
 * Отряды стрелков. Стрелки одной команды с одинаковым ANoFearCharacter::Squad — один отряд (до MaxSquadSize бойцов).
 * Состав пересобирается каждые UpdateInterval секунд из живых стрелков, поэтому отряд не надо регистрировать:
 * погибший выбывает сам, поменявший имя отряда — переходит.
 *
 * Отряд:
 *  - назначает роли: роль с тела (пусто — стрелок); командир — заданный на теле, иначе первый стрелок
 *    (потом поддержка, потом штурмовик). Погиб командир — роль переходит к следующему;
 *  - собирает, кого видят его бойцы, и сообщает остальным (ReceiveAlarm) — где бы они ни стояли;
 *  - считает, сколько бойцов уже бьют каждую цель (ANoFearShooterAIController::ScoreTarget делит цели);
 *  - ведёт шкалу морали: падает от потерь, гибели командира, ранений, подавления и гранат, растёт от убитых врагов
 *    и пришедшей помощи. По морали и обстановке командир выбирает тактику (ENoFearSquadTactic) и раздаёт приказы
 *    (ENoFearSquadOrder): перебежки, фланг, натиск, оборона, отход перекатами, сбор после боя;
 *  - помогает своим: прикрывает огнём отходящего раненого, шлёт подмогу к прижатому, перевязывает раненых,
 *    велит бросить гранату в засевшего врага, зовёт на помощь соседний отряд своей команды.
 * Приказы исполняет ANoFearShooterAIController (поиск укрытий по приказу, огонь вслепую, поводок отряда).
 */
UCLASS()
class NOFEAR_API UNoFearSquadSubsystem : public UTickableWorldSubsystem, public FNoFearSquadTuning
{
	GENERATED_BODY()

public:
	/** Копирует числа из Project Settings → NoFear AI. */
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;

	/** Подсистема отрядов мира WorldContextObject. */
	UFUNCTION(BlueprintPure, Category = "NoFear|Squad", meta = (WorldContext = "WorldContextObject", DisplayName = "Get Squad Subsystem"))
	static UNoFearSquadSubsystem* Get(const UObject* WorldContextObject);

	/** Текущие числа отряда (копия). */
	UFUNCTION(BlueprintPure, Category = "NoFear|Squad")
	FNoFearSquadTuning GetTuning() const { return *this; }

	/** Заменить числа отряда на ходу, до конца этого мира. */
	UFUNCTION(BlueprintCallable, Category = "NoFear|Squad")
	void SetTuning(const FNoFearSquadTuning& NewTuning) { static_cast<FNoFearSquadTuning&>(*this) = NewTuning; }

	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;

	/** Пересобрать отряды сейчас (раз в UpdateInterval это происходит само). */
	UFUNCTION(BlueprintCallable, Category = "NoFear|Squad")
	void UpdateSquads();

	/** Роль бойца в отряде. None — не в отряде. */
	UFUNCTION(BlueprintPure, Category = "NoFear|Squad")
	ENoFearSquadRole GetSquadRole(const APawn* Pawn) const;

	/** Командир отряда Pawn (может быть он сам). nullptr — не в отряде. */
	UFUNCTION(BlueprintPure, Category = "NoFear|Squad")
	APawn* GetSquadCommander(const APawn* Pawn) const;

	/** Живые бойцы отряда Pawn, включая его самого. Пусто — не в отряде. */
	UFUNCTION(BlueprintPure, Category = "NoFear|Squad")
	TArray<APawn*> GetSquadMembers(const APawn* Pawn) const;

	/** Отряд Pawn собран (в нём хоть раз было двое): им командуют приказы, даже если остался один. */
	UFUNCTION(BlueprintPure, Category = "NoFear|Squad")
	bool IsInFormedSquad(const APawn* Pawn) const;

	/** Сколько врагов отряд видел за последние SquadIntelTime секунд. */
	UFUNCTION(BlueprintPure, Category = "NoFear|Squad")
	int32 GetSquadKnownEnemyCount(const APawn* Pawn) const;

	/** Сколько бойцов отряда Pawn (кроме него самого) сейчас целятся в Target. */
	UFUNCTION(BlueprintPure, Category = "NoFear|Squad")
	int32 CountSquadmatesTargeting(const APawn* Pawn, const AActor* Target) const;

	/** Боец выполняет приказ (кольцо пульсирует) — для приказов из Blueprint; свои приказы отряд отмечает сам. */
	UFUNCTION(BlueprintCallable, Category = "NoFear|Squad")
	void SetExecutingOrder(const APawn* Pawn, bool bExecuting);

	// --- Тактика и приказы ---

	/** Приказ бойцу сейчас. None — приказа нет (воюет сам) или не в отряде. */
	UFUNCTION(BlueprintPure, Category = "NoFear|Squad")
	ENoFearSquadOrder GetSquadOrder(const APawn* Pawn) const;

	/** Приказ бойцу целиком: точка, свой, враг, номер. */
	UFUNCTION(BlueprintPure, Category = "NoFear|Squad")
	FNoFearSquadOrderInfo GetSquadOrderInfo(const APawn* Pawn) const;

	/** Тактика отряда Pawn. */
	UFUNCTION(BlueprintPure, Category = "NoFear|Squad")
	ENoFearSquadTactic GetSquadTactic(const APawn* Pawn) const;

	/** Мораль отряда Pawn 0..1. -1 — не в отряде. */
	UFUNCTION(BlueprintPure, Category = "NoFear|Squad")
	float GetSquadMorale(const APawn* Pawn) const;

	/** Отряд бойца держит строй (союзники рядом сильнее монстров — не ломаются). */
	UFUNCTION(BlueprintPure, Category = "NoFear|Squad")
	bool IsSquadSteady(const APawn* Pawn) const;

	/** Задать мораль отряда Pawn (тесты, скриптовые сцены: «отряд сломлен»). */
	UFUNCTION(BlueprintCallable, Category = "NoFear|Squad")
	void SetSquadMorale(const APawn* Pawn, float NewMorale);

	/** Отряд, которому помогает Pawn (пусто — своему). */
	UFUNCTION(BlueprintPure, Category = "NoFear|Squad")
	FName GetAssistingSquad(const APawn* Pawn) const;

	/** Отряд, который пришёл на помощь отряду Pawn (пусто — никто). */
	UFUNCTION(BlueprintPure, Category = "NoFear|Squad")
	FName GetHelperSquad(const APawn* Pawn) const;

	/** Слово приказа для панели над головой («перебегает», «подавляет»…). Пусто для None. */
	UFUNCTION(BlueprintPure, Category = "NoFear|Squad")
	static FText GetOrderText(ENoFearSquadOrder Order);

	/** Название тактики («Перебежки», «Отход»…). Пусто для None. */
	UFUNCTION(BlueprintPure, Category = "NoFear|Squad")
	static FText GetTacticText(ENoFearSquadTactic Tactic);

	/** Отладка: состояние отряда Pawn одной строкой (тактика и почему, бой, главный враг, таймеры фланга и гранат). */
	UFUNCTION(BlueprintPure, Category = "NoFear|Squad|Debug")
	FString GetSquadDebugText(const APawn* Pawn) const;

	/**
	 * Свой просит прикрыть (раненый отходит): CoveringFireHelpers бойцов его отряда прикрывают огнём по Threat.
	 * false — просящий не в отряде (тогда контроллер зовёт всех своих рядом, как раньше).
	 */
	UFUNCTION(BlueprintCallable, Category = "NoFear|Squad")
	bool RequestCoveringFire(APawn* Requester, AActor* Threat, FVector ThreatLocation);

	/** Санитар закончил перевязку (задача NoFear Bandage): раненому — здоровье, отряду — мораль. */
	UFUNCTION(BlueprintCallable, Category = "NoFear|Squad")
	bool CompleteBandage(APawn* Medic, APawn* Patient);

	/** Перевязка сорвалась (раненый ушёл, санитар не догнал): приказ снят, к раненому попробуют позже. */
	UFUNCTION(BlueprintCallable, Category = "NoFear|Squad")
	void AbortBandage(APawn* Medic, APawn* Patient);

	/** Кого спасает отряд Pawn (проводник или паникующий мирный). nullptr — никого. */
	UFUNCTION(BlueprintPure, Category = "NoFear|Squad")
	APawn* GetSquadRescueTarget(const APawn* Pawn) const;

	/** Кто из отряда Pawn сейчас выводит или успокаивает цель спасения. nullptr — никто (зачищают или спасения нет). */
	UFUNCTION(BlueprintPure, Category = "NoFear|Squad")
	APawn* GetSquadRescuer(const APawn* Pawn) const;

	/** Кто идёт перевязывать Patient (приказ отряда). nullptr — никто. */
	UFUNCTION(BlueprintPure, Category = "NoFear|Squad")
	APawn* GetMedicFor(const APawn* Patient) const;

	/**
	 * Поводок бойца в отряде вместо своего: дом отряда (центр постов) и SquadLeashRadius; рубеж после отхода;
	 * при отходе — без поводка (OutRadius 0); помогающий соседям — вокруг них; при спасении — вокруг цели (RescueLeashRadius).
	 * false — не в собранном отряде.
	 */
	bool GetSquadLeash(const APawn* Pawn, FVector& OutAnchor, float& OutRadius) const;

	/** Где отряд Pawn последний раз видел Enemy. false — не видел. */
	bool GetSquadEnemyLocation(const APawn* Pawn, const AActor* Enemy, FVector& OutLocation) const;

	/** Для интерфейса: false — Pawn не в отряде. */
	bool GetMemberView(const APawn* Pawn, FNoFearSquadMemberView& OutView) const;

	/** Бойцов в отряде не больше этого; лишние воюют сами по себе. */
	static constexpr int32 MaxSquadSize = 6;

private:
	struct FMember
	{
		TWeakObjectPtr<ANoFearShooterAIController> Bot;
		TWeakObjectPtr<APawn> Pawn;
		ENoFearSquadRole Role = ENoFearSquadRole::Rifleman;
		ENoFearSquadRole PreviousRole = ENoFearSquadRole::Rifleman;
		double JoinTime = -1.0e9;
		double RoleChangeTime = -1.0e9;
		bool bExecutingOrder = false;
		/** Роль уже назначалась (первое назначение — не смена). */
		bool bRoleAssigned = false;

		// Итоговый приказ (тактика или помощь поверх неё).
		ENoFearSquadOrder Order = ENoFearSquadOrder::None;
		FVector OrderLocation = FVector::ZeroVector;
		bool bHasOrderLocation = false;
		TWeakObjectPtr<AActor> OrderTarget;
		TWeakObjectPtr<AActor> OrderEnemy;
		double OrderTime = -1.0e9;
		int32 OrderSerial = 0;
		/** Номер приказа, который боец уже довёл до конца (дошёл, занял укрытие или упёрся). */
		int32 SettledSerial = -1;

		// Помощь поверх тактики: прикрыть, подмога, перевязка.
		ENoFearSquadOrder HelpOrder = ENoFearSquadOrder::None;
		TWeakObjectPtr<AActor> HelpTarget;
		TWeakObjectPtr<AActor> HelpEnemy;
		double HelpUntil = 0.0;
		/** До какой доли здоровья перевязать (у санитара). */
		float HealTargetFraction = 1.f;

		// Огневая группа в перебежках и отходе.
		bool bTeamB = false;

		// Для морали и помощи.
		float LastHealth = -1.f;
		double LastDamageTime = -1.0e9;
		/** С какого времени прижат огнём (<0 — не прижат). */
		double SuppressedSince = -1.0;
		bool bWasEvading = false;
		double LastGrenadeScareTime = -1.0e9;
		/** Перевязывать снова не раньше (у раненого). */
		double NextHealTime = 0.0;
		/** Слать подмогу снова не раньше (у прижатого). */
		double NextReinforceTime = 0.0;
		/** Командир велел бросить гранату (слово в панели). */
		double GrenadeOrderTime = -1.0e9;

		/** Помогает этому отряду своей команды (пусто — своему). */
		FName AssistSquad;
	};

	struct FEnemy
	{
		FVector Location = FVector::ZeroVector;
		double LastSeenTime = -1.0e9;
	};

	struct FSquad
	{
		FName Name;
		ENoFearTeam Team = ENoFearTeam::Neutral;
		TArray<FMember> Members;
		TWeakObjectPtr<APawn> Commander;
		double CommanderChangeTime = -1.0e9;
		/** Собран (в нём хоть раз было двое). */
		bool bFormed = false;
		TMap<TWeakObjectPtr<AActor>, FEnemy> Enemies;
		int32 PeakSize = 0;

		// Тактика и мораль.
		ENoFearSquadTactic Tactic = ENoFearSquadTactic::None;
		double TacticStartTime = -1.0e9;
		float Morale = 0.7f;
		double NextThinkTime = 0.0;
		/** У кого-то есть цель или врага видели недавно. */
		bool bEngaged = false;
		/** Кто-то в бою или обыскивает (до конца поиска сбор не начинается). */
		bool bAlerted = false;
		/** Был бой — после него сбор. */
		bool bWasEngaged = false;
		double ContactTime = -1.0e9;
		double CommanderDeathTime = -1.0e9;
		/** Когда последний раз шёл бой (новый контакт — после долгого затишья). */
		double LastEngagedTime = -1.0e9;
		/** Почему выбрана тактика (отладка: «враг перезаряжается», «засел 6 с»). */
		FString TacticReason;

		// Главная угроза и сколько она сидит на месте.
		TWeakObjectPtr<AActor> MainEnemy;
		FVector MainEnemyLocation = FVector::ZeroVector;
		double MainEnemyLastSeen = -1.0e9;
		FVector DugInAnchor = FVector::ZeroVector;
		double DugInSince = 0.0;

		// Перебежки и отход перекатами.
		bool bTeamBMoving = true;
		double PhaseStartTime = 0.0;
		int32 Swaps = 0;
		int32 StuckPhases = 0;
		double NextBoundTime = 0.0;

		FVector FallbackAnchor = FVector::ZeroVector;
		bool bFallbackDone = false;
		/** Оборона на рубеже после отхода (поводок — вокруг рубежа). */
		bool bHoldingLine = false;
		double NextFallbackTime = 0.0;

		// Фланг и натиск.
		TWeakObjectPtr<APawn> Flanker;
		FVector FlankPoint = FVector::ZeroVector;
		/** Когда обходящий дошёл до фланга (меньше нуля — ещё идёт). */
		double FlankArriveTime = -1.0;
		double NextFlankTime = 0.0;
		double NextRushTime = 0.0;

		// Гранаты.
		double NextGrenadeTime = 0.0;

		// Сбор после боя.
		FVector RallyCenter = FVector::ZeroVector;

		// Помощь между отрядами (у того, кому помогают).
		FName HelperSquad;
		double AssistStartTime = -1.0e9;
		bool bAssistArrived = false;
		double NextHelpCallTime = 0.0;

		// Спасение у портала.
		TWeakObjectPtr<APawn> RescueTarget;
		/** Цель — паникующий мирный (успокоить), а не проводник (вывести из транса). */
		bool bRescueCalm = false;
		TWeakObjectPtr<AActor> RescuePortal;
		TWeakObjectPtr<APawn> Rescuer;
		/** Ближайший к цели монстр — по нему работает прикрытие. */
		TWeakObjectPtr<AActor> RescueEnemy;
		/** У цели нет монстров (спасающий может идти). */
		bool bRescueClear = false;
		float RescueThreat = 0.f;
		double NextRescueThinkTime = 0.0;
		int32 RescueFails = 0;
		/** Кого не брать до этого времени (не дошли дважды). */
		TMap<TWeakObjectPtr<APawn>, double> RescueSkips;
		/** Боец начал ломаться у портала — не спасать до этого времени. */
		double RescueBlockedUntil = 0.0;

		/** Держит строй (UpdateSteady). */
		bool bSteady = false;
		/** Монстров у отряда больше, чем бойцов (союзники) — отходить, а не ломаться на месте. */
		bool bOutmatched = false;
		double LastSteadyShoutTime = -1.0e9;
	};

	const FSquad* FindSquadOf(const APawn* Pawn, const FMember** OutMember = nullptr) const;
	FSquad* FindSquadOf(const APawn* Pawn, FMember** OutMember);
	FSquad* FindSquadByName(FName Name, ENoFearTeam Team);
	const FSquad* FindSquadByName(FName Name, ENoFearTeam Team) const;

	void AssignRoles(FSquad& Squad, double Now);
	void ShareIntel(FSquad& Squad, double Now);
	void HandleMemberDeath(FSquad& Squad, const FMember& Member, double Now);

	/** Урон, подавление, гранаты — в мораль; кто когда ранен и прижат. */
	void UpdateMembers(FSquad& Squad, double Now, float DeltaTime);
	/** Идёт ли бой, главная угроза и сколько она сидит на месте. */
	void UpdateEngagement(FSquad& Squad, double Now);
	/** Мораль сама тянется к «спокойной» по числу своих против врагов. */
	void UpdateMorale(FSquad& Squad, float DeltaTime);
	void UpdateSteady(FSquad& Squad, double Now, float DeltaTime);
	void UpdateTactic(FSquad& Squad, double Now);
	void SetTactic(FSquad& Squad, ENoFearSquadTactic Tactic, double Now);
	void BeginBounding(FSquad& Squad, double Now);
	void BeginFallback(FSquad& Squad, double Now, const FText& ShoutText);
	void BeginRegroup(FSquad& Squad, double Now);
	/** Спасение у портала: выбор цели, риск, зачистка, спасающий. true — отряд занят спасением (тактику дальше не думать). */
	bool UpdateRescue(FSquad& Squad, double Now);
	void EndSquadRescue(FSquad& Squad, double Now, const TCHAR* Reason);
	/** Кого спасать: ближайший доступный проводник, иначе паникующий мирный у портала. */
	APawn* ChooseRescueTarget(const FSquad& Squad, const FVector& Centroid, int32 Alive, double Now, bool& bOutCalm, AActor*& OutPortal) const;
	/**
	 * Угроза у цели: монстры в RescueThreatRadius (колосс — RescueTankWeight) и недавно виденные отрядом враги рядом.
	 * OutClearWeight — сколько из них в ClearRadius, OutNearest — ближайший к цели.
	 */
	float GetRescueThreat(const FSquad& Squad, const FVector& Target, const FVector& Centroid, double Now, float ClearRadius,
		float& OutClearWeight, AActor*& OutNearest) const;
	bool TryBeginFlank(FSquad& Squad, double Now);
	/** Враг уязвим для натиска (перезаряжается, ранен, прижат). Пусто — нет. */
	FString GetRushReason(const FSquad& Squad, double Now) const;
	/**
	 * Врага не достать оттуда, где стоят: далеко для оружия или давно не виден.
	 * bKeepClosing — перебежки уже идут: продолжать, пока не достаёт хотя бы половина (а не больше половины),
	 * чтобы вторая группа тоже успела подтянуться.
	 */
	bool NeedsToClose(const FSquad& Squad, double Now, bool bKeepClosing = false) const;
	/** Перебежки и отход: смена групп, когда идущие дошли. */
	void UpdateLeapfrog(FSquad& Squad, double Now);
	bool IsRegroupDone(const FSquad& Squad, double Now) const;
	bool IsFlankActive(FSquad& Squad, double Now);
	bool IsRushDone(const FSquad& Squad, double Now) const;

	/** Приказы по тактике и помощь поверх них. */
	void IssueOrders(FSquad& Squad, double Now);
	void SetOrder(FMember& Member, ENoFearSquadOrder Order, double Now, const FVector* Location, AActor* Target, AActor* Enemy);
	/** Подмога прижатым, перевязка раненых; снять отработанную помощь. */
	void UpdateHelp(FSquad& Squad, double Now);
	void UpdateGrenades(FSquad& Squad, double Now);
	void UpdateAssists(double Now);
	void ReleaseHelpers(FSquad& Caller);

	static void ClearHelp(FMember& Member);
	/** Бойцы, воюющие за свой отряд (без ушедших на помощь соседям). */
	int32 CountActive(const FSquad& Squad) const;
	bool IsSettled(const FMember& Member) const { return Member.SettledSerial == Member.OrderSerial; }
	void AddMorale(FSquad& Squad, float Delta);
	void SquadShout(const FSquad& Squad, const APawn* Who, const FText& Text, float Duration = 1.8f) const;
	FVector GetCentroid(const FSquad& Squad) const;
	/** Куда идти на помощь отряду: его центр на навмеше, если туда есть путь; иначе — к бойцу, ближайшему к центру. */
	FVector GetAssistPoint(const FSquad& Caller) const;
	FVector GetHomeCentroid(const FSquad& Squad) const;
	/** Боец сейчас не занят движением по приказу и может помочь другим. */
	bool IsFreeForHelp(const FMember& Member) const;
	/** Боец дошёл туда, куда велел приказ, или пришёл к выводу, что идти некуда. */
	bool HasSettled(const FMember& Member, double Now) const;
	void DrawDebug(double Now) const;

	TArray<FSquad> Squads;
	float TimeSinceUpdate = 0.f;
	double LastUpdateTime = -1.0;
	/** Номера приказов общие на все отряды: перешедший в другой отряд не получит старый номер. */
	int32 OrderSerialCounter = 0;
};
