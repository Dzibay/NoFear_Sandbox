#pragma once

#include "CoreMinimal.h"
#include "Core/NoFearHealthComponent.h"
#include "Core/NoFearTeam.h"
#include "GameFramework/HUD.h"
#include "NoFearHUD.generated.h"

class ANoFearPlayerController;
class ANoFearWeapon;
class UFont;

/**
 * HUD игрока. Рисуется на Canvas (UMG не нужен), все размеры заданы для экрана высотой 1080 и масштабируются.
 *
 * - Прицел в центре экрана — туда же летит пуля (см. ANoFearWeapon::ComputeAim). Зазор между чёрточками равен
 *   реальному разбросу оружия, поэтому прицел расходится при беге и стрельбе и сходится при прицеливании. Над врагом краснеет.
 * - Маркер попадания вокруг прицела: белый — обычное, жёлтый — в голову, красный и крупный — убийство.
 * - Здоровье внизу слева: полоска с «хвостом» последнего урона, число.
 * - Патроны внизу справа: имя оружия, магазин / запас, полоска перезарядки, подсказки.
 * - Слоты оружия внизу по центру.
 * - Указатели урона: красные клинья по кругу в сторону стрелявшего. Красная виньетка при ударе и при низком здоровье.
 * - Экран смерти: затемнение, надпись и отсчёт до возрождения.
 * - Напарники: панель вверху слева, зелёная метка над головой, стрелка у края экрана, если напарника не видно.
 *   Бледные метки над ещё не завербованными напарниками поблизости.
 * - Подсказка взаимодействия под прицелом: «[E] Завербовать» и имя того, с кем.
 * - Напарник (NoFearHUDCompanion.cpp): чем занят, запас магазинов и бинтов, отметки врагов, которых видит он,
 *   точка приказа, рация слева, колесо приказов на Tab. Упавший напарник — красная метка со стрелкой и таймером,
 *   кольцо подъёма у прицела. Упавший игрок — тёмные края, таймер и кто идёт на помощь.
 * - Панель над ботами: полоска здоровья с «хвостом» и значок тревоги — «?» с кольцом-шкалой обнаружения, «!» в бою.
 *   Показывается по правилам (раненым, в бою, под прицелом, рядом), только при прямой видимости; см. NoFear.HUD.BotIndicators.
 * - Кольца отрядов под ногами: узор и цвет — роль, тонкий внешний ободок — сторона (красный враг, зелёный союзник).
 *   Союзник в отряде — всегда, враг — только на виду, монстры — двойное красное у вожака стаи, напарник — без кольца.
 *   При сборе отряда и смене командира — линии к командиру, при смене роли — вспышка; значок роли рядом с «◆ N».
 *
 * Подключается в ANoFearGameMode, настраивать в редакторе ничего не нужно. Состояние обновляется в Tick, а не при
 * отрисовке, поэтому его можно читать из Blueprint и тестов (Get...).
 */
UCLASS(Blueprintable)
class NOFEAR_API ANoFearHUD : public AHUD
{
	GENERATED_BODY()

public:
	ANoFearHUD();

	virtual void Tick(float DeltaSeconds) override;
	virtual void DrawHUD() override;

	// --- Состояние (для Blueprint и тестов) ---

	/** Игрок мёртв: пешка погибла или контроллер ждёт возрождения. */
	UFUNCTION(BlueprintPure, Category = "NoFear|HUD")
	bool IsOwnerDead() const;

	/** Здоровье, как его показывает полоска: 0..1. */
	UFUNCTION(BlueprintPure, Category = "NoFear|HUD")
	float GetDisplayedHealthFraction() const { return DisplayedHealth; }

	/** «Хвост» полоски здоровья: держится на старом значении после удара, потом догоняет. */
	UFUNCTION(BlueprintPure, Category = "NoFear|HUD")
	float GetGhostHealthFraction() const { return GhostHealth; }

	/** Прозрачность маркера попадания: 0 — не виден, 1 — только что попали. */
	UFUNCTION(BlueprintPure, Category = "NoFear|HUD")
	float GetHitMarkerAlpha() const;

	UFUNCTION(BlueprintPure, Category = "NoFear|HUD")
	int32 GetDamageIndicatorCount() const { return DamageIndicators.Num(); }

	/** Яркость красной вспышки после удара: 0..1. */
	UFUNCTION(BlueprintPure, Category = "NoFear|HUD")
	float GetDamageFlash() const { return DamageFlash; }

	/** Затемнение экрана смерти: 0..1. */
	UFUNCTION(BlueprintPure, Category = "NoFear|HUD")
	float GetDeathScreenAlpha() const;

	// --- Что показывать ---

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NoFear|HUD")
	bool bShowHealth = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NoFear|HUD")
	bool bShowAmmo = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NoFear|HUD")
	bool bShowWeaponSlots = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NoFear|HUD")
	bool bShowHitMarker = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NoFear|HUD")
	bool bShowDamageIndicators = true;

	/** Красная виньетка по краям экрана при ударе и при низком здоровье. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NoFear|HUD")
	bool bShowVignette = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NoFear|HUD")
	bool bShowDeathScreen = true;

	/** Счётчик гранат, дуга броска с кругом поражения, значок у живой гранаты рядом. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NoFear|HUD")
	bool bShowGrenades = true;

	/** Шрифт. Не задан — стандартный шрифт движка. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NoFear|HUD")
	TObjectPtr<UFont> HudFont;

	/** Отступ от краёв экрана (пиксели при высоте 1080). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NoFear|HUD", meta = (ClampMin = "0"))
	float Margin = 48.f;

	// --- Прицел ---

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crosshair")
	bool bShowCrosshair = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crosshair")
	FLinearColor DefaultColor = FLinearColor::White;

	/** Цвет, когда в центре экрана враг. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crosshair")
	FLinearColor EnemyColor = FLinearColor(1.f, 0.1f, 0.05f, 1.f);

	/** Длина чёрточки в пикселях при высоте экрана 1080 (масштабируется под разрешение). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crosshair", meta = (ClampMin = "1"))
	float BarLength = 10.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crosshair", meta = (ClampMin = "1"))
	float BarThickness = 2.f;

	/** Минимальный зазор от центра (пиксели при высоте 1080). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crosshair", meta = (ClampMin = "0"))
	float MinGap = 4.f;

	/** Во сколько раз увеличить зазор относительно настоящего разброса. 1 — как есть. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crosshair", meta = (ClampMin = "0"))
	float SpreadScale = 1.f;

	/** Скорость, с которой зазор подтягивается к текущему разбросу. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crosshair", meta = (ClampMin = "0"))
	float GapInterpSpeed = 18.f;

	/** Прозрачность, пока оружие перезаряжается или достаётся. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crosshair", meta = (ClampMin = "0", ClampMax = "1"))
	float BusyOpacity = 0.35f;

	// --- Маркер попадания ---

	/** Сколько секунд виден маркер обычного попадания (и в голову). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hit Marker", meta = (ClampMin = "0.05"))
	float HitMarkerDuration = 0.25f;

	/** Сколько секунд виден маркер убийства. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hit Marker", meta = (ClampMin = "0.05"))
	float KillMarkerDuration = 0.6f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hit Marker")
	FLinearColor HitMarkerColor = FLinearColor::White;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hit Marker")
	FLinearColor HeadshotMarkerColor = FLinearColor(1.f, 0.82f, 0.15f, 1.f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hit Marker")
	FLinearColor KillMarkerColor = FLinearColor(1.f, 0.12f, 0.08f, 1.f);

	// --- Здоровье и урон ---

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Health")
	FLinearColor HealthColor = FLinearColor(0.35f, 0.85f, 0.4f, 1.f);

	/** Цвет полоски почти пустого здоровья; между ним и HealthColor цвет плавно меняется. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Health")
	FLinearColor LowHealthColor = FLinearColor(0.95f, 0.18f, 0.12f, 1.f);

	/** Ниже этой доли здоровья число и полоска пульсируют, а по краям экрана краснеет виньетка. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Health", meta = (ClampMin = "0", ClampMax = "1"))
	float LowHealthFraction = 0.3f;

	/** Через сколько секунд после удара «хвост» полоски начинает догонять. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Health", meta = (ClampMin = "0"))
	float GhostHoldTime = 0.5f;

	/** Сколько секунд виден указатель направления на стрелявшего. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Health", meta = (ClampMin = "0.1"))
	float DamageIndicatorDuration = 2.f;

	/** Радиус круга указателей: доля высоты экрана. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Health", meta = (ClampMin = "0.05", ClampMax = "0.5"))
	float DamageIndicatorRadius = 0.3f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Health")
	FLinearColor DamageColor = FLinearColor(0.85f, 0.04f, 0.03f, 1.f);

	// --- Смерть ---

	/** За сколько секунд экран темнеет после смерти. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Death", meta = (ClampMin = "0.1"))
	float DeathScreenFadeTime = 1.5f;

	// --- Тексты ---

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Text")
	FText DeadText = NSLOCTEXT("NoFearHUD", "Dead", "ВЫ ПОГИБЛИ");

	/** {0} — секунды до возрождения. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Text")
	FText RespawnInText = NSLOCTEXT("NoFearHUD", "RespawnIn", "Возрождение через {0} с");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Text")
	FText ReloadingText = NSLOCTEXT("NoFearHUD", "Reloading", "ПЕРЕЗАРЯДКА");

	/** Магазин пуст, а запас есть. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Text")
	FText ReloadHintText = NSLOCTEXT("NoFearHUD", "ReloadHint", "ПЕРЕЗАРЯДИТЕ");

	/** Нет патронов вообще. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Text")
	FText NoAmmoText = NSLOCTEXT("NoFearHUD", "NoAmmo", "НЕТ ПАТРОНОВ");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Text")
	FText CompanionTitleText = NSLOCTEXT("NoFearHUD", "CompanionTitle", "НАПАРНИК");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Text")
	FText CompanionFollowingText = NSLOCTEXT("NoFearHUD", "CompanionFollowing", "идёт за вами");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Text")
	FText CompanionWaitingText = NSLOCTEXT("NoFearHUD", "CompanionWaiting", "ждёт");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Text")
	FText CompanionFightingText = NSLOCTEXT("NoFearHUD", "CompanionFighting", "в бою");

	/** {0} — расстояние в метрах. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Text")
	FText DistanceText = NSLOCTEXT("NoFearHUD", "Distance", "{0} м");

	// --- Напарники ---

	/** Панель вверху слева: имя, здоровье, «идёт за вами / ждёт / в бою», расстояние. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Companions")
	bool bShowCompanions = true;

	/** Метка над головой напарника, а когда он за краем экрана — стрелка в его сторону. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Companions")
	bool bShowCompanionMarkers = true;

	/** Бледная метка над ещё не завербованными напарниками поблизости. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Companions")
	bool bShowRecruitableMarkers = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Companions", meta = (ClampMin = "0"))
	float RecruitableMarkerDistance = 2500.f;

	/** Ближе этого над напарником рисуется только метка, без расстояния. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Companions", meta = (ClampMin = "0"))
	float CompanionCaptionDistance = 600.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Companions")
	FLinearColor CompanionColor = FLinearColor(0.3f, 0.95f, 0.4f, 1.f);

	/** Упавший раненым: метка, стрелка, таймер. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Companions")
	FLinearColor DownedColor = FLinearColor(1.f, 0.22f, 0.12f, 1.f);

	/** Враги, которых отметил напарник. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Companions")
	FLinearColor CompanionMarkColor = FLinearColor(1.f, 0.6f, 0.1f, 1.f);

	/** Реплики напарника слева. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Companions")
	bool bShowRadio = true;

	// --- Панель над ботами ---

	/**
	 * Полоска здоровья и значок «?» / «!» над ботами. Что именно показывать, решают правила ниже;
	 * консольная переменная NoFear.HUD.BotIndicators: 0 — выключить, 1 — по правилам, 2 — показывать у всех.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bots")
	bool bShowBotIndicators = true;

	/** Сколько секунд после урона над врагом или монстром висит полоска здоровья. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bots", meta = (ClampMin = "0"))
	float BotRecentDamageTime = 5.f;

	/** Ближе этого враги показывают полоску здоровья всегда, дальше — только после урона, в бою или под прицелом. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bots", meta = (ClampMin = "0"))
	float EnemyPanelDistance = 1500.f;

	/** Союзники (не напарники) показывают здоровье в этом радиусе. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bots", meta = (ClampMin = "0"))
	float AllyPanelDistance = 3500.f;

	/** Дальше этого панель исчезает совсем. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bots", meta = (ClampMin = "0"))
	float BotPanelMaxDistance = 5000.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bots")
	FLinearColor BotEnemyColor = FLinearColor(0.95f, 0.2f, 0.15f, 1.f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bots")
	FLinearColor BotAllyColor = FLinearColor(0.35f, 0.75f, 1.f, 1.f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bots")
	FLinearColor BotNeutralColor = FLinearColor(0.75f, 0.75f, 0.75f, 1.f);

	/** Кольцо и «?» у насторожённого бота. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bots")
	FLinearColor AlertSuspiciousColor = FLinearColor(1.f, 0.85f, 0.2f, 1.f);

	/** «!» у бота, который вас заметил. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bots")
	FLinearColor AlertCombatColor = FLinearColor(1.f, 0.15f, 0.1f, 1.f);

	/** «?» у бота, потерявшего вас и обыскивающего местность. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bots")
	FLinearColor AlertSearchColor = FLinearColor(1.f, 0.55f, 0.1f, 1.f);

	/**
	 * Запас гранат «◆ N» и у врагов. У напарника и союзников он виден всегда, у монстров и мирных — никогда.
	 * Включается и командой NoFear.HUD.ShowEnemyLoadout 1.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bots")
	bool bShowEnemyLoadout = false;

	// --- Отряды ---

	/** Кольца отрядов и вожаков стай под ногами (вместе с панелями: NoFear.HUD.BotIndicators). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bots|Squad")
	bool bShowSquadRings = true;

	/** Радиус кольца, см. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bots|Squad", meta = (ClampMin = "10"))
	float SquadRingRadius = 60.f;

	/** Дальше этого кольца не рисуются, см. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bots|Squad", meta = (ClampMin = "0"))
	float SquadRingMaxDistance = 4000.f;

	/** Сколько видны линии к командиру при сборе отряда, вступлении бойца и смене командира, с. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bots|Squad", meta = (ClampMin = "0"))
	float SquadLineTime = 1.5f;

	/** За сколько кольцо перекрашивается при смене роли (со вспышкой), с. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bots|Squad", meta = (ClampMin = "0.01"))
	float SquadRoleChangeTime = 0.3f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bots|Squad")
	FLinearColor SquadCommanderColor = FLinearColor(1.f, 0.78f, 0.15f, 1.f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bots|Squad")
	FLinearColor SquadAssaultColor = FLinearColor(1.f, 0.48f, 0.08f, 1.f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bots|Squad")
	FLinearColor SquadSupportColor = FLinearColor(0.45f, 0.82f, 1.f, 1.f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bots|Squad")
	FLinearColor SquadRiflemanColor = FLinearColor(0.95f, 0.95f, 0.95f, 1.f);

	/** Ободок кольца у союзника (у врага — BotEnemyColor). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bots|Squad")
	FLinearColor SquadAllyBorderColor = FLinearColor(0.3f, 0.95f, 0.35f, 1.f);

	/**
	 * Приказы отряда: слово приказа в панели рядом со знаком роли, тактика и шкала морали над командиром,
	 * линии приказов на земле, вспышка кольца при новом приказе.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bots|Squad")
	bool bShowSquadOrders = true;

	/** Сколько видна линия на земле к точке нового приказа (рубеж, укрытие на фланге, свой, к кому идти), с. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bots|Squad", meta = (ClampMin = "0"))
	float SquadOrderLineTime = 3.f;

	/** Вспышка кольца и слова приказа при новом приказе, с. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bots|Squad", meta = (ClampMin = "0.01"))
	float SquadOrderFlashTime = 0.6f;

	/** Линия огня на подавление и броска гранаты по приказу — к врагу. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bots|Squad")
	FLinearColor SquadFireLineColor = FLinearColor(1.f, 0.3f, 0.2f, 1.f);

	/** Шкала морали над командиром: от низкой к высокой. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bots|Squad")
	FLinearColor SquadMoraleLowColor = FLinearColor(1.f, 0.25f, 0.15f, 1.f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bots|Squad")
	FLinearColor SquadMoraleHighColor = FLinearColor(0.35f, 0.95f, 0.4f, 1.f);

	UFUNCTION(BlueprintPure, Category = "NoFear|HUD")
	FLinearColor GetSquadRoleColor(ENoFearSquadRole SquadRole) const;

	/** Сколько колец отрядов и вожаков нарисовано в последнем кадре (для тестов). */
	UFUNCTION(BlueprintPure, Category = "NoFear|HUD")
	int32 GetDrawnSquadRingCount() const { return DrawnSquadRings; }

	/** Сколько линий к командиру нарисовано в последнем кадре (для тестов). */
	UFUNCTION(BlueprintPure, Category = "NoFear|HUD")
	int32 GetDrawnSquadLineCount() const { return DrawnSquadLines; }

	/** Сколько линий приказов на земле нарисовано в последнем кадре (для тестов). */
	UFUNCTION(BlueprintPure, Category = "NoFear|HUD")
	int32 GetDrawnSquadOrderLineCount() const { return DrawnOrderLines; }

	/** Сколько слов приказа в панелях нарисовано в последнем кадре (для тестов). */
	UFUNCTION(BlueprintPure, Category = "NoFear|HUD")
	int32 GetDrawnSquadOrderWordCount() const { return DrawnOrderWords; }

	/** Сколько табло «тактика + мораль» над командирами нарисовано в последнем кадре (для тестов). */
	UFUNCTION(BlueprintPure, Category = "NoFear|HUD")
	int32 GetDrawnSquadTacticCount() const { return DrawnTactics; }

	// --- Страх (этап 11) ---

	/** Полоска страха над панелью бота (не у монстров): видна от FearBarMinFear и ближе FearBarDistance. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bots|Fear")
	bool bShowFearBars = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bots|Fear", meta = (ClampMin = "0", ClampMax = "100"))
	float FearBarMinFear = 30.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bots|Fear", meta = (ClampMin = "0"))
	float FearBarDistance = 2500.f;

	/** Цвета полоски: тревога, паника, ужас (пороги из настроек страха). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bots|Fear")
	FLinearColor FearAnxiousColor = FLinearColor(1.f, 0.85f, 0.2f, 1.f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bots|Fear")
	FLinearColor FearPanicColor = FLinearColor(1.f, 0.5f, 0.1f, 1.f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bots|Fear")
	FLinearColor FearTerrorColor = FLinearColor(0.95f, 0.15f, 0.15f, 1.f);

	/** Кольцо «Успокаиваю» у прицела. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bots|Fear")
	FLinearColor CalmColor = FLinearColor(0.55f, 0.85f, 1.f, 1.f);

	/** Сколько полосок страха нарисовано в последнем кадре (для тестов). */
	UFUNCTION(BlueprintPure, Category = "NoFear|HUD")
	int32 GetDrawnFearBarCount() const { return DrawnFearBars; }

	/** Цвет крика над ботом («Граната!»). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bots")
	FLinearColor ShoutColor = FLinearColor(1.f, 0.95f, 0.6f, 1.f);

	// --- Гранаты ---

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grenades")
	FLinearColor GrenadeColor = FLinearColor(1.f, 0.75f, 0.2f, 1.f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grenades")
	FLinearColor GrenadeArcColor = FLinearColor(1.f, 0.9f, 0.45f, 0.9f);

	/** Круг поражения и значок гранаты, в радиус которой попал игрок. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grenades")
	FLinearColor GrenadeDangerColor = FLinearColor(1.f, 0.2f, 0.1f, 1.f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grenades")
	FText GrenadeWarningText = NSLOCTEXT("NoFearHUD", "GrenadeWarning", "ГРАНАТА {0}");

	// --- Взаимодействие ---

	/** Подсказка «[E] Завербовать» под прицелом, когда есть с чем взаимодействовать. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interaction")
	bool bShowInteractionPrompt = true;

	/** Клавиша в подсказке. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interaction")
	FText InteractKeyText = NSLOCTEXT("NoFearHUD", "InteractKey", "E");

private:
	/** Один указатель урона: откуда прилетело и сколько он уже виден. */
	struct FDamageIndicator
	{
		FVector SourceLocation = FVector::ZeroVector;
		float Age = 0.f;
	};

	enum class ELabelAlign : uint8
	{
		Left,
		Center,
		Right
	};

	// Подписки и состояние
	void EnsureBindings();
	UFUNCTION()
	void HandleHitConfirmed(AActor* Victim, float Damage, bool bHeadshot, bool bKilled);
	UFUNCTION()
	void HandleDamaged(UNoFearHealthComponent* Component, const FNoFearDamageInfo& Info);
	UFUNCTION()
	void HandlePlayerDied(const FNoFearDamageInfo& Info);

	// Отрисовка
	void DrawCrosshair(ANoFearWeapon* Weapon);
	bool IsEnemyUnderCrosshair(const ANoFearWeapon* Weapon) const;
	void DrawHitMarker();
	void DrawHealth();
	/** Возвращает верх блока патронов (Y), над ним рисуется счётчик гранат. */
	float DrawAmmo(ANoFearWeapon* Weapon);
	/** «◆ N» над нижней границей Bottom. */
	void DrawGrenadeCounter(float Bottom);
	/** Дуга броска и круг поражения, пока игрок держит G. */
	void DrawGrenadeAim();
	/** Значок со стрелкой у края экрана у живой гранаты в UNoFearGrenadeData::HUDWarningRadius от игрока. */
	void DrawGrenadeDanger();
	/** Круг в мире (в плоскости XY), спроецированный на экран. */
	void DrawWorldCircle(const FVector& Center, float Radius, const FLinearColor& Color, float Thickness);
	void DrawWeaponSlots();
	void DrawDamageIndicators();
	void DrawVignette();
	void DrawDeathScreen();
	void DrawCompanionPanel();
	void DrawCompanionMarkers();
	void DrawRecruitableMarkers();
	void DrawBotIndicators();
	/** Кольца отрядов и вожаков под ногами, линии к командиру. */
	void DrawSquadRings();
	/** Кольцо в плоскости XY из 24 отрезков; bDashed — через один. */
	void DrawGroundRing(const FVector& Center, float Radius, const FLinearColor& Color, float Thickness, bool bDashed = false);
	/** Отрезок в мире, спроецированный на экран (части за камерой не рисуются). */
	void DrawWorldLine(const FVector& From, const FVector& To, const FLinearColor& Color, float Thickness);
	/** Пунктир в мире: штрихи по Dash см через такой же промежуток. */
	void DrawWorldDashedLine(const FVector& From, const FVector& To, const FLinearColor& Color, float Thickness, float Dash);
	/** Значок роли: корона, шеврон вверх, две черты, точка. Size — половина ширины. */
	void DrawRoleIcon(const FVector2D& Center, float Size, ENoFearSquadRole SquadRole, const FLinearColor& Color);
	void DrawInteractionPrompt();

	/** Состояние панели одного бота: плавная полоска здоровья, «хвост», проверка видимости, плавное появление. */
	struct FBotPanelState
	{
		float DisplayedHealth = 1.f;
		float GhostHealth = 1.f;
		float LastHealth = 1.f;
		double LastDamageSeen = -1.0e9;
		double NextVisibilityCheck = 0.0;
		bool bLineOfSight = false;
		float Alpha = 0.f;
		double LastUpdate = -1.0;
		/** Кольцо отряда: своя проверка видимости (врагов показываем только на виду) и плавное появление. */
		double NextRingVisibilityCheck = 0.0;
		bool bRingLineOfSight = false;
		float RingAlpha = 0.f;
		/** Плавная полоска страха, 0..100. */
		float DisplayedFear = 0.f;
	};

	/** Кольцо из отрезков, заполненное на Fraction по часовой стрелке от верхней точки. */
	void DrawRing(const FVector2D& Center, float Radius, float Fraction, const FLinearColor& Color, float Thickness);

	/**
	 * Ромб над точкой мира с подписью сверху. Точка за краем экрана — при bEdgeArrow стрелка у края в её сторону.
	 * Возвращает true, если точка на экране.
	 */
	bool DrawWorldMarker(const FVector& WorldLocation, const FLinearColor& Color, float Size, bool bEdgeArrow, const FString& Caption);
	void DrawDiamond(const FVector2D& Center, float Radius, const FLinearColor& Color);
	/** Где рисовать метку над персонажем: чуть выше макушки капсулы. */
	static FVector GetOverheadLocation(const AActor* Actor);
	FString FormatDistance(float Centimeters) const;

	void DrawLabel(const FString& Text, float X, float Y, const FLinearColor& Color, float Scale, ELabelAlign Align);
	void DrawBar(float X, float Y, float Width, float Height, float Fraction, const FLinearColor& Fill, float GhostFraction = -1.f, float Opacity = 1.f);
	/** Четырёхугольник из двух треугольников; цвета вершин по кругу A, B, C, D. */
	void DrawColoredQuad(const FVector2D& A, const FVector2D& B, const FVector2D& C, const FVector2D& D, const FLinearColor& ColorA, const FLinearColor& ColorB, const FLinearColor& ColorC, const FLinearColor& ColorD);
	UFont* GetFont() const;
	float GetUIScale() const;
	float GetLowHealthAlpha() const;

	// Напарник, колесо приказов, ранение — NoFearHUDCompanion.cpp.
	/** Тёмные края, таймер и кто идёт на помощь, пока игрок лежит раненым. */
	void DrawDownedScreen();
	/** Враги, отмеченные напарником, и точка его приказа. */
	void DrawCompanionMarks();
	void DrawRadio();
	/** Кольцо у прицела, пока игрок поднимает упавшего. */
	void DrawReviveProgress();
	/** Кольцо у прицела, пока игрок успокаивает мирного. */
	void DrawCalmProgress();
	void DrawOrderWheel();
	/** Закрашенный сектор кольца Inner..Outer от угла From до To в градусах (0 — вверх, по часовой). */
	void DrawWedge(const FVector2D& Center, float Inner, float Outer, float From, float To, const FLinearColor& Color);

	TWeakObjectPtr<ANoFearPlayerController> BoundController;
	TWeakObjectPtr<UNoFearHealthComponent> BoundHealth;

	float CurrentGap = -1.f;

	float DisplayedHealth = 1.f;
	float GhostHealth = 1.f;
	double LastDamageTime = -1.0e9;
	float DamageFlash = 0.f;

	TArray<FDamageIndicator> DamageIndicators;

	TMap<TWeakObjectPtr<const AActor>, FBotPanelState> BotPanels;

	int32 DrawnSquadRings = 0;
	int32 DrawnSquadLines = 0;
	int32 DrawnOrderLines = 0;
	int32 DrawnOrderWords = 0;
	int32 DrawnTactics = 0;
	int32 DrawnFearBars = 0;

	float HitMarkerAge = 1.0e9f;
	bool bHitHeadshot = false;
	bool bHitKill = false;

	double DeathTime = -1.0e9;
	bool bWasDead = false;
};
