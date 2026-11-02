#pragma once

#include "CoreMinimal.h"
#include "AI/NoFearCompanionAIController.h"
#include "Core/NoFearHealthComponent.h"
#include "GameFramework/PlayerController.h"
#include "GenericTeamAgentInterface.h"
#include "NoFearPlayerController.generated.h"

class UInputAction;
class UInputMappingContext;
class UNoFearBraceletComponent;
class UNoFearInteractableComponent;

/** Строка рации: что сказал напарник (HUD, левый нижний угол). */
struct FNoFearRadioLine
{
	TWeakObjectPtr<APawn> Speaker;
	FText SpeakerName;
	FText Text;
	double Time = 0.0;
	/** Шёпотом: напарник крадётся. */
	bool bWhisper = false;
};

/** Сектор колеса приказов. */
struct FNoFearOrderWheelEntry
{
	ENoFearCompanionOrder Order = ENoFearCompanionOrder::None;
	FText Label;
	/** Подпись мельче: запас, цель, почему нельзя. */
	FText Detail;
	bool bEnabled = true;
};

/** Игрок попал по кому-то: для маркера попадания на прицеле. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_FourParams(FNoFearHitConfirmedSignature, AActor*, Victim, float, Damage, bool, bHeadshot, bool, bKilled);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FNoFearPlayerDiedSignature, const FNoFearDamageInfo&, Info);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FNoFearPlayerRespawnedSignature);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FNoFearCompanionsChangedSignature);

/**
 * Контроллер игрока. Всегда в команде Player.
 * Ведёт смерть и возрождение: после гибели пешки (ANoFearCharacter вызывает NotifyPawnDied) через RespawnDelay секунд
 * создаётся новая пешка на PlayerStart, а если его на уровне нет — там, где игрок появился в первый раз.
 * Держит список напарников (не больше MaxCompanions) и взаимодействие клавишей E (IA_Interact):
 * каждый кадр выбирает ближайший к центру экрана UNoFearInteractableComponent в пределах его дистанции.
 *
 * Напарник:
 * - колесо приказов: держать Tab — время замедляется, мышью выбрать сектор, отпустить — приказ;
 *   точечные приказы берут точку и врага под прицелом;
 * - пока есть стоящий на ногах напарник, игрок не умирает, а падает раненым (UNoFearHealthComponent::bCanBeDowned);
 * - упавшего напарника поднимают, удерживая E ReviveHoldTime секунд; урон по игроку сбрасывает подъём;
 * - реплики напарника приходят сюда же строками рации (AddRadioLine).
 *
 * Испуганного мирного успокаивают, удерживая E рядом с ним (StartCalming, этап 11).
 *
 * Браслет стража (UNoFearBraceletComponent, этап 12): страх героя, его эффекты; B — стабилизация,
 * держать Left Alt целясь — задержка дыхания.
 */
UCLASS()
class NOFEAR_API ANoFearPlayerController : public APlayerController, public IGenericTeamAgentInterface
{
	GENERATED_BODY()

public:
	ANoFearPlayerController();

	// IGenericTeamAgentInterface
	virtual FGenericTeamId GetGenericTeamId() const override;

	/** Возрождать игрока автоматически. Выключено — экран смерти висит, пока не вызван RespawnPlayer. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NoFear|Respawn")
	bool bAutoRespawn = true;

	/** Через сколько секунд после смерти игрок возрождается. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NoFear|Respawn", meta = (ClampMin = "0"))
	float RespawnDelay = 5.f;

	/** Игрок попал по цели (после множителей зон, с учётом отключённого огня по своим). */
	UPROPERTY(BlueprintAssignable, Category = "NoFear|Health")
	FNoFearHitConfirmedSignature OnHitConfirmed;

	UPROPERTY(BlueprintAssignable, Category = "NoFear|Respawn")
	FNoFearPlayerDiedSignature OnPlayerDied;

	UPROPERTY(BlueprintAssignable, Category = "NoFear|Respawn")
	FNoFearPlayerRespawnedSignature OnPlayerRespawned;

	/** Игрок мёртв и ждёт возрождения. */
	UFUNCTION(BlueprintPure, Category = "NoFear|Respawn")
	bool IsWaitingForRespawn() const { return bWaitingForRespawn; }

	/** Сколько секунд до автоматического возрождения; 0, если оно не запланировано. */
	UFUNCTION(BlueprintPure, Category = "NoFear|Respawn")
	float GetRespawnTimeRemaining() const;

	/** Возродить сейчас. Живого игрока не трогает. Старое тело остаётся лежать до своего исчезновения. */
	UFUNCTION(BlueprintCallable, Category = "NoFear|Respawn")
	void RespawnPlayer();

	/** Консоль: нанести себе урон, чтобы проверить HUD. Пример: NoFearHurt 30. */
	UFUNCTION(Exec)
	void NoFearHurt(float Amount = 25.f);

	/** Консоль: умереть, чтобы проверить экран смерти и возрождение. */
	UFUNCTION(Exec)
	void NoFearKill();

	/**
	 * Консоль: поставить ботов перед игроком. Тип — суффикс Blueprint из /Game/Blueprints/Bots/CBP_Bot_<Type>:
	 * Civilian, Monster, AggroMonster, Jumper, Spitter, Tank, Whisperer, Enemy, Ally, Companion. Пример: NoFearSpawnBot Enemy 3.
	 */
	UFUNCTION(Exec)
	void NoFearSpawnBot(const FString& Type, int32 Count = 1);

	/**
	 * Консоль: нагрузочная сцена (этап 10). Убирает ботов (кроме напарников) и расставляет Count ботов по навмешу вокруг игрока:
	 * отряд союзников, три вражеских отряда, две стаи монстров, мирные; больше 40 — дополнительные вражеские отряды.
	 * Mode: battle — враги рядом, бой начинается сразу; calm — всё в 2,5 раза дальше. Пример: NoFearStressTest 40 battle.
	 */
	UFUNCTION(Exec)
	void NoFearStressTest(int32 Count = 40, const FString& Mode = TEXT("battle"));

	/** Консоль: замер нагрузки на Seconds секунд, отчёт на экран и в лог (NOFEAR_PERF). Пример: NoFearPerf 10. */
	UFUNCTION(Exec)
	void NoFearPerf(float Seconds = 10.f);

	/** Консоль: поставить страх героя. Пример: NoFearHeroFear 90. */
	UFUNCTION(Exec)
	void NoFearHeroFear(float Value = 100.f);

	/** Консоль: браслет почуял разлом в Distance см перед игроком (заготовка этапа 13). */
	UFUNCTION(Exec)
	void NoFearRift(float Distance = 2000.f);

	// --- Браслет (этап 12) ---

	UFUNCTION(BlueprintPure, Category = "NoFear|Bracelet")
	UNoFearBraceletComponent* GetBracelet() const { return Bracelet; }

	/** Стабилизация браслетом. Не задано — создаётся на Q / крестовину вниз. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|Bracelet")
	TObjectPtr<UInputAction> StabilizeAction;

	/** Задержка дыхания (держать). Не задано — создаётся на Left Alt / нажатие левого стика. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|Bracelet")
	TObjectPtr<UInputAction> BreathAction;

	/** Вырываться из-под ловчего (жать часто). Не задано — создаётся на Пробел / нижнюю кнопку геймпада; прыжок не глушит. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|Bracelet")
	TObjectPtr<UInputAction> BreakFreeAction;

	/** Одно нажатие «вырваться»: ловчий слетает, когда за окно набрано нужное число нажатий. */
	UFUNCTION(BlueprintCallable, Category = "NoFear|Bracelet")
	void BreakFree();

	/** Вызывается из ANoFearCharacter::HandleDeath, когда пешка этого игрока погибла. */
	void NotifyPawnDied(const FNoFearDamageInfo& Info);

	/** Вызывается из UNoFearHealthComponent, когда выстрел этого игрока нанёс урон. */
	void NotifyHitConfirmed(AActor* Victim, float Damage, bool bHeadshot, bool bKilled);

	// --- Напарники ---

	/** Сколько напарников может быть одновременно. Новый сверх лимита заменяет самого старого, тот остаётся союзником. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NoFear|Companions", meta = (ClampMin = "1"))
	int32 MaxCompanions = 1;

	/** Состав напарников изменился: завербован, отпущен, погиб или сменил «жди/за мной». */
	UPROPERTY(BlueprintAssignable, Category = "NoFear|Companions")
	FNoFearCompanionsChangedSignature OnCompanionsChanged;

	/** Завербовать бота с ANoFearCompanionAIController. Уже свой — просто позвать за собой. */
	UFUNCTION(BlueprintCallable, Category = "NoFear|Companions")
	bool RecruitCompanion(APawn* CompanionPawn);

	/** Отпустить: напарник остаётся союзником там, где стоит. */
	UFUNCTION(BlueprintCallable, Category = "NoFear|Companions")
	void DismissCompanion(APawn* CompanionPawn);

	/** true — «За мной», false — «Жди здесь». */
	UFUNCTION(BlueprintCallable, Category = "NoFear|Companions")
	void SetCompanionFollowing(APawn* CompanionPawn, bool bFollow);

	UFUNCTION(BlueprintCallable, Category = "NoFear|Companions")
	void ToggleCompanionFollowing(APawn* CompanionPawn);

	/** Живые напарники, от самого давнего к новому. */
	UFUNCTION(BlueprintPure, Category = "NoFear|Companions")
	TArray<APawn*> GetCompanions() const;

	UFUNCTION(BlueprintPure, Category = "NoFear|Companions")
	bool IsCompanion(const APawn* CompanionPawn) const;

	/** Первый живой напарник (их один) или nullptr. */
	ANoFearCompanionAIController* GetMainCompanion() const;

	/** Напарник сказал реплику: строка рации на HUD. */
	void AddRadioLine(APawn* Speaker, const FText& Text, bool bWhisper);

	/** Последние строки рации, от старых к новым. */
	const TArray<FNoFearRadioLine>& GetRadioLines() const { return RadioLines; }

	/** Сколько секунд строка рации видна на HUD. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NoFear|Companions", meta = (ClampMin = "1"))
	float RadioLineTime = 6.f;

	// --- Колесо приказов ---

	/** Колесо приказов (держать). Не задано — создаётся на Tab. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|Companions|Orders")
	TObjectPtr<UInputAction> OrderWheelAction;

	/** Замедление времени, пока колесо открыто. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NoFear|Companions|Orders", meta = (ClampMin = "0.05", ClampMax = "1"))
	float OrderWheelTimeDilation = 0.3f;

	/** Насколько надо сдвинуть мышь от центра, чтобы выбрать сектор (сырые единицы мыши). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NoFear|Companions|Orders", meta = (ClampMin = "1"))
	float OrderWheelDeadZone = 12.f;

	/** Дальше этого точку и врага под прицелом для приказа не ищем, см. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NoFear|Companions|Orders", meta = (ClampMin = "100"))
	float OrderTraceDistance = 8000.f;

	UFUNCTION(BlueprintPure, Category = "NoFear|Companions|Orders")
	bool IsOrderWheelOpen() const { return bOrderWheelOpen; }

	/** Сектора колеса по часовой стрелке, начиная сверху. */
	const TArray<FNoFearOrderWheelEntry>& GetOrderWheelEntries() const { return OrderWheelEntries; }

	/** Выбранный сектор или INDEX_NONE. */
	UFUNCTION(BlueprintPure, Category = "NoFear|Companions|Orders")
	int32 GetOrderWheelSelection() const { return OrderWheelSelection; }

	/** Куда смотрит курсор колеса (единичный вектор, Y вверх; ноль — в центре). */
	FVector2D GetOrderWheelCursor() const;

	/** Точка под прицелом для «Иди туда» и «Граната туда». */
	bool GetOrderAimPoint(FVector& OutPoint) const;

	/** Враг под прицелом для «Атакуй цель». */
	UFUNCTION(BlueprintPure, Category = "NoFear|Companions|Orders")
	AActor* GetOrderAimEnemy() const { return OrderAimEnemy.Get(); }

	/** Отдать приказ напарнику: точка и враг — под прицелом. false — нет напарника или он отказался. */
	UFUNCTION(BlueprintCallable, Category = "NoFear|Companions|Orders")
	bool IssueCompanionOrder(ENoFearCompanionOrder Order);

	// --- Ранение ---

	/** Сколько держать E, чтобы поднять упавшего напарника, с. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NoFear|Companions|Revive", meta = (ClampMin = "0.1"))
	float ReviveHoldTime = 3.f;

	/**
	 * Начать поднимать упавшего. Начатое клавишей E идёт, пока E держат; вызванное иначе (Blueprint, тест) —
	 * пока не вызван StopRevive. Игрок при этом стоит на месте.
	 */
	UFUNCTION(BlueprintCallable, Category = "NoFear|Companions|Revive")
	bool StartRevive(APawn* DownedPawn);

	UFUNCTION(BlueprintCallable, Category = "NoFear|Companions|Revive")
	void StopRevive();

	/** Кого сейчас поднимает игрок. */
	UFUNCTION(BlueprintPure, Category = "NoFear|Companions|Revive")
	APawn* GetReviveTarget() const { return ReviveTarget.Get(); }

	/** Сколько поднятия набрано, 0..1. */
	UFUNCTION(BlueprintPure, Category = "NoFear|Companions|Revive")
	float GetReviveProgress() const { return ReviveTarget.IsValid() ? FMath::Clamp(ReviveElapsed / FMath::Max(ReviveHoldTime, 0.1f), 0.f, 1.f) : 0.f; }

	// --- Успокоение (этап 11) ---

	/**
	 * Начать успокаивать испуганного (UNoFearCalmComponent на мирном). Пока E держат и игрок ближе CalmDistance,
	 * страх цели снимается со скоростью CalmRate до CalmTargetFear. Вызванное не клавишей (Blueprint, тест) идёт,
	 * пока не вызван StopCalming. Игрок при этом стоит на месте.
	 */
	UFUNCTION(BlueprintCallable, Category = "NoFear|Fear")
	bool StartCalming(APawn* ScaredPawn);

	UFUNCTION(BlueprintCallable, Category = "NoFear|Fear")
	void StopCalming();

	/** Кого сейчас успокаивает игрок. */
	UFUNCTION(BlueprintPure, Category = "NoFear|Fear")
	APawn* GetCalmTarget() const { return CalmTarget.Get(); }

	/** Сколько страха снято от начала до CalmTargetFear, 0..1. */
	UFUNCTION(BlueprintPure, Category = "NoFear|Fear")
	float GetCalmProgress() const;

	/** Консоль: уронить напарника раненым. */
	UFUNCTION(Exec)
	void NoFearDownCompanion();

	/** Консоль: упасть раненым самому (только при живом напарнике, иначе смерть). */
	UFUNCTION(Exec)
	void NoFearDownSelf();

	/** Консоль: приказ напарнику. Follow, Hold, Move, Come, Attack, Grenade, Fire, Supply. */
	UFUNCTION(Exec)
	void NoFearOrder(const FString& Order);

	/** Консоль: открыть колесо приказов (Sector — выбранный сектор, -1 — никакой) или закрыть без приказа (Open 0). */
	UFUNCTION(Exec)
	void NoFearOrderWheel(int32 Open = 1, int32 Sector = -1);

	// --- Взаимодействие ---

	/** Действие «взаимодействовать». По умолчанию IA_Interact из GASP (E / верхняя кнопка геймпада в IMC_Sandbox). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|Interaction")
	TObjectPtr<UInputAction> InteractAction;

	/** Насколько объект может быть в стороне от направления камеры, чтобы попасть в фокус (градусы). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NoFear|Interaction", meta = (ClampMin = "0", ClampMax = "180"))
	float InteractionMaxAngle = 40.f;

	/** Ближе этого объект попадает в фокус под любым углом, лишь бы был перед камерой. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NoFear|Interaction", meta = (ClampMin = "0"))
	float InteractionCloseDistance = 130.f;

	/** С чем сейчас сработает E; для подсказки на HUD. */
	UFUNCTION(BlueprintPure, Category = "NoFear|Interaction")
	UNoFearInteractableComponent* GetFocusedInteractable() const;

	/** Нажать E: взаимодействовать с объектом в фокусе. */
	UFUNCTION(BlueprintCallable, Category = "NoFear|Interaction")
	bool InteractWithFocused();

protected:
	virtual void OnPossess(APawn* InPawn) override;
	virtual void OnUnPossess() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void SetupInputComponent() override;
	virtual void PlayerTick(float DeltaTime) override;

	void OnInteractInput();
	void UpdateFocusedInteractable();

	void OnOrderWheelPressed();
	void OnOrderWheelReleased();
	void OnStabilizeInput();
	void OnBreathPressed();
	void OnBreakFreeInput();
	void OnBreathReleased();
	void OpenOrderWheel();
	/** bIssue — отдать выбранный приказ. */
	void CloseOrderWheel(bool bIssue);
	void UpdateOrderWheel();
	void UpdateOrderAim();
	void BuildOrderWheelEntries();

	void UpdateRevive(float DeltaTime);
	void UpdateCalming(float DeltaTime);
	/** Клавиши IA_Interact (или E): удержание подъёма и успокоения. */
	void GetInteractKeys(TArray<FKey>& OutKeys) const;
	/** Падать раненым можно, пока рядом есть напарник на ногах. */
	void UpdateCanBeDowned();

	/** Убрать погибших и чужих; true — список изменился. */
	bool PruneCompanions();

	/** Убрать из списка без оповещения и отпустить. */
	void ReleaseCompanion(APawn* CompanionPawn);

private:
	UPROPERTY(VisibleAnywhere, Category = "NoFear|Bracelet")
	TObjectPtr<UNoFearBraceletComponent> Bracelet;

	TArray<TWeakObjectPtr<APawn>> Companions;
	TWeakObjectPtr<UNoFearInteractableComponent> FocusedInteractable;

	TArray<FNoFearRadioLine> RadioLines;

	UPROPERTY(Transient)
	TObjectPtr<UInputMappingContext> CompanionInputContext;

	// Колесо приказов.
	bool bOrderWheelOpen = false;
	TArray<FNoFearOrderWheelEntry> OrderWheelEntries;
	int32 OrderWheelSelection = INDEX_NONE;
	FVector2D OrderWheelCursor = FVector2D::ZeroVector;
	float TimeDilationBeforeWheel = 1.f;
	bool bOrderAimValid = false;
	FVector OrderAimPoint = FVector::ZeroVector;
	TWeakObjectPtr<AActor> OrderAimEnemy;

	/** В кого игрок последним попал: запасная цель для «Атакуй». */
	TWeakObjectPtr<AActor> LastHitEnemy;
	double LastHitEnemyTime = -1.0e9;

	// Подъём упавшего.
	TWeakObjectPtr<APawn> ReviveTarget;
	float ReviveElapsed = 0.f;
	bool bReviveHeldByInput = false;
	bool bReviveBlocksMove = false;
	TArray<FKey> ReviveKeys;
	// Успокоение мирного.
	TWeakObjectPtr<APawn> CalmTarget;
	float CalmStartFear = 0.f;
	bool bCalmHeldByInput = false;
	bool bCalmBlocksMove = false;
	TArray<FKey> CalmKeys;
	/** Идёт обработка нажатия E: подъём, начатый сейчас, держится клавишей. */
	bool bHandlingInteractInput = false;

	FTimerHandle RespawnTimer;
	bool bWaitingForRespawn = false;
	double RespawnTime = 0.0;

	/** Где игрок появился в первый раз: запасная точка возрождения. */
	FTransform RespawnTransform;
	bool bHasRespawnTransform = false;
};
