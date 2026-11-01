#pragma once

#include "CoreMinimal.h"
#include "AI/NoFearAIController.h"
#include "AI/NoFearCoverLibrary.h"
#include "AI/NoFearSquadSubsystem.h"
#include "Core/NoFearTeam.h"
#include "NoFearShooterAIController.generated.h"

class UNoFearCombatComponent;
class UNoFearWeaponComponent;
class UNoFearWeaponData;

/**
 * Стрелок: враги (команда Enemy) и союзники (Ally). Держит точку («дом»), заметив врага — целится и
 * стреляет очередями, теряя из виду — подходит ближе, но не дальше поводка от дома.
 *
 * Точность, линия огня и подавление — UNoFearCombatComponent на теле (добавляется сам, если его нет).
 * Оружие — UNoFearWeaponComponent тела (StartingWeapons). Дерево: BT_NoFear_Soldier.
 *
 * В бою ищет укрытие от цели (UNoFearCoverLibrary, бронь в UNoFearBotSubsystem) и пишет его в CoverLocation;
 * бой из укрытия — задача NoFear Cover Fight. Без укрытия держит дистанцию оружия и не стоит на месте
 * (NoFear Find Combat Position). Раненый отходит в дальнее укрытие и просит прикрыть.
 *
 * В отряде исполняет приказы командира (UNoFearSquadSubsystem): новый приказ — новое укрытие по его правилам
 * (перебежка — ближе к врагу, отход — к рубежу, подмога — рядом со своим, обход — то, что нашёл командир);
 * подавление — огонь вслепую по сведениям отряда; перевязка — задача NoFear Bandage; сбор — RallyLocation.
 * Поводок в отряде — отрядный (GetSquadLeash), гранаты в отряде — по приказу командира.
 */
UCLASS(Blueprintable)
class NOFEAR_API ANoFearShooterAIController : public ANoFearAIController
{
	GENERATED_BODY()

public:
	ANoFearShooterAIController(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	UFUNCTION(BlueprintPure, Category = "NoFear|AI")
	UNoFearWeaponComponent* GetWeaponComponent() const;

	UFUNCTION(BlueprintPure, Category = "NoFear|AI")
	UNoFearCombatComponent* GetCombatComponent() const;

	virtual bool IsTargetVisible() const override;

	/** Запас патронов не кончается (магазин всё равно перезаряжается). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|AI|Weapon")
	bool bInfiniteReserveAmmo = true;

	/** С целью держать оружие в прицеле (поза прицеливания у тела GASP). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|AI|Weapon")
	bool bAimInCombat = true;

	/** Если у тела нет UNoFearCombatComponent, добавить его с настройками по умолчанию. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|AI|Weapon")
	bool bAddCombatComponentIfMissing = true;

	// --- Отряд ---

	/**
	 * Оружие по роли в отряде (ANoFearCharacter::SquadRole): боец берёт его в руки вместо первого из StartingWeapons.
	 * Роли без записи воюют стартовым оружием. По умолчанию штурмовик — с дробовиком.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|AI|Squad")
	TMap<ENoFearSquadRole, TObjectPtr<UNoFearWeaponData>> RoleWeapons;

	/**
	 * Цель, которую уже бьют другие бойцы отряда, менее желанна: оценка делится на 1 + это × число таких бойцов (цели делятся).
	 * Только перераспределяет: цель, которую бьют все, остаётся целью, если других нет.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|AI|Squad", meta = (ClampMin = "0"))
	float SquadTargetSharePenalty = 0.5f;

	/** Роль в отряде. None — не в отряде (UNoFearSquadSubsystem). */
	UFUNCTION(BlueprintPure, Category = "NoFear|AI")
	ENoFearSquadRole GetSquadRole() const;

	/** Приказ отряда, который боец исполняет. None — воюет сам. */
	UFUNCTION(BlueprintPure, Category = "NoFear|AI")
	ENoFearSquadOrder GetSquadOrder() const { return SquadOrder.Order; }

	/**
	 * Подавляет огнём по приказу (подавление, прикрытие отхода или своего): стреляет по цели и не видя её —
	 * туда, где она по последним сведениям (своим или отряда).
	 */
	UFUNCTION(BlueprintPure, Category = "NoFear|AI")
	bool IsSuppressing() const;

	/** Кого перевязать по приказу отряда. nullptr — некого. */
	UFUNCTION(BlueprintPure, Category = "NoFear|AI")
	APawn* GetHealTarget() const;

	/**
	 * Санитар идёт к этому бойцу и уже ближе MedicWaitRadius: боец замирает — в укрытии не высовывается
	 * (NoFear Cover Fight), без укрытия стоит на месте (RallyLocation), пока его перевязывают.
	 */
	UFUNCTION(BlueprintPure, Category = "NoFear|AI")
	bool IsAwaitingMedic() const;

	/** Отладка: итог последнего поиска укрытия на фланге (сколько точек отсеял каждый фильтр). */
	UFUNCTION(BlueprintPure, Category = "NoFear|AI|Debug")
	FString GetFlankSearchStats() const { return FlankSearchStats; }

	/** Номер приказа отряда, который боец уже принял (отряд сверяет со своим: дошёл ли приказ). */
	int32 GetHandledOrderSerial() const { return HandledOrderSerial; }

	/** Принятый приказ выполнить нельзя: укрытия по нему нет, ранен или уже отходит. Отряд считает такого «дошедшим». */
	bool IsOrderStuck() const { return bOrderStuck; }

	/**
	 * Найти и занять укрытие на фланге: угол у угрозы между направлениями на FlankFrom (где отряд) и на укрытие
	 * не меньше MinAngle, путь туда скрытный. Командир зовёт перед приказом «обход». false — нет (старое укрытие остаётся).
	 */
	bool SearchFlankCover(AActor* Threat, const FVector& ThreatLocation, const FVector& FlankFrom, float MinAngle, FVector& OutCover);

	/** Бросить гранату по приказу командира в Enemy, который по сведениям отряда у KnownLocation. false — не вышло. */
	bool OrderGrenade(AActor* Enemy, const FVector& KnownLocation);

	/** В собранном отряде поводок — отрядный (дом отряда, рубеж, соседи), иначе свой. */
	virtual FVector GetLeashAnchor() const override;
	virtual float GetLeashRadius() const override;

	/** Отступ при перебежке: укрытие хотя бы на столько ближе к врагу, чем боец сейчас, см. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|AI|Squad", meta = (ClampMin = "0"))
	float BoundGain = 300.f;

	/**
	 * Контакт или огонь с места (подавить, прикрыть), а враг дальше, чем бьёт оружие: укрытие не больше чем на столько
	 * ближе к врагу, см. Нет такого — стоять, к врагу отряд пойдёт перебежками.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|AI|Squad", meta = (ClampMin = "0"))
	float ContactCoverAdvance = 600.f;

	/** Натиск: укрытие хотя бы на столько ближе к врагу, см. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|AI|Squad", meta = (ClampMin = "0"))
	float RushGain = 500.f;

	/** Подмога ищет укрытие не дальше этого от прижатого своего, см. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|AI|Squad", meta = (ClampMin = "100"))
	float ReinforceSearchRadius = 700.f;

	/** Санитар ближе этого — раненый замирает и ждёт перевязки (IsAwaitingMedic), см. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|AI|Squad", meta = (ClampMin = "0"))
	float MedicWaitRadius = 600.f;

	// --- Гранаты ---

	/**
	 * Бросать гранаты (запас — BotGrenades): в цель, засевшую в укрытии дольше HiddenTargetTime, или в кучку
	 * из ClusterCount врагов. Не бросает, если рядом с точкой падения свои или нейтральные.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|AI|Grenade")
	bool bThrowGrenades = true;

	/** Пауза между бросками, с (случайно в пределах). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|AI|Grenade", meta = (ClampMin = "0"))
	FVector2D GrenadeCooldown = FVector2D(10.f, 15.f);

	/** Первый бросок не раньше, чем через столько после того, как появилась цель, с. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|AI|Grenade", meta = (ClampMin = "0"))
	FVector2D FirstGrenadeDelay = FVector2D(5.f, 8.f);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|AI|Grenade", meta = (ClampMin = "0"))
	float MinGrenadeDistance = 600.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|AI|Grenade", meta = (ClampMin = "0"))
	float MaxGrenadeDistance = 2000.f;

	/** Цель не видна столько секунд — выкуривать гранатой. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|AI|Grenade", meta = (ClampMin = "0"))
	float HiddenTargetTime = 4.f;

	/** Дольше этого цель не видна — уже неизвестно, где она, не бросать, с. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|AI|Grenade", meta = (ClampMin = "0"))
	float MaxUnseenTime = 12.f;

	/** Враги в этом радиусе от цели считаются кучкой, см. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|AI|Grenade", meta = (ClampMin = "0"))
	float ClusterRadius = 400.f;

	/** Сколько врагов (с целью) в кучке — повод бросить. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|AI|Grenade", meta = (ClampMin = "2"))
	int32 ClusterCount = 2;

	/** Свои и нейтральные ближе Radius + этого к точке падения — не бросать, см. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|AI|Grenade", meta = (ClampMin = "0"))
	float FriendlyGrenadeMargin = 150.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|AI|Grenade")
	FText GrenadeShout = NSLOCTEXT("NoFear", "GrenadeShout", "Граната!");

	// --- Укрытия ---

	/** Искать укрытия в бою. Выключено — только открытый бой со сменой позиций. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|AI|Cover")
	bool bUseCover = true;

	/** Где искать укрытие вокруг себя, см. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|AI|Cover", meta = (ClampMin = "150"))
	float CoverSearchRadius = 1500.f;

	/**
	 * Укрытие на фланге (приказ отряда «обход») — не дальше этого от угрозы, см, даже если оружие бьёт ближе:
	 * фланг ради перекрёстного огня, дробовик подойдёт ближе уже из укрытия.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|AI|Cover", meta = (ClampMin = "0"))
	float FlankMaxThreatDistance = 2500.f;

	/** Укрытие не ближе этого к угрозе, см. Угроза подошла ближе 0.8 от этого — менять укрытие. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|AI|Cover", meta = (ClampMin = "0"))
	float MinCoverThreatDistance = 500.f;

	/** Как часто перепроверять укрытие (или искать, если его нет), с. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|AI|Cover", meta = (ClampMin = "0.1"))
	FVector2D CoverSearchInterval = FVector2D(0.8f, 1.4f);

	/** Чужая бронь или другой бот ближе этого — укрытие занято, см. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|AI|Cover", meta = (ClampMin = "0"))
	float CoverReserveRadius = 150.f;

	/** Препятствие должно быть вплотную: не дальше этого от точки, см. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|AI|Cover", meta = (ClampMin = "0"))
	float CoverMaxObstacleDistance = 150.f;

	/** Сколько сидеть в укрытии между выглядываниями, с. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|AI|Cover", meta = (ClampMin = "0"))
	FVector2D CoverHideTime = FVector2D(1.f, 2.2f);

	/** Перезаряжаться в укрытии, если в магазине меньше этой доли. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|AI|Cover", meta = (ClampMin = "0", ClampMax = "1"))
	float CoverReloadFraction = 0.6f;

	/** Без цели перезаряжаться, если в магазине меньше этой доли. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|AI|Cover", meta = (ClampMin = "0", ClampMax = "1"))
	float IdleReloadFraction = 0.33f;

	/** Цель не видна дольше этого — бросить укрытие и подойти ближе, с. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|AI|Cover", meta = (ClampMin = "0"))
	float AdvanceAfterUnseenTime = 6.f;

	// --- Отход раненого и прикрытие ---

	/** Здоровье ниже этой доли — отойти в дальнее укрытие. 0 — не отходить. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|AI|Cover", meta = (ClampMin = "0", ClampMax = "1"))
	float RetreatHealthFraction = 0.3f;

	/** Где искать укрытие для отхода, см. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|AI|Cover", meta = (ClampMin = "150"))
	float RetreatSearchRadius = 2500.f;

	/** Отход хотя бы на столько дальше от угрозы, чем сейчас, см. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|AI|Cover", meta = (ClampMin = "0"))
	float RetreatGain = 400.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|AI|Cover")
	FText RetreatShout = NSLOCTEXT("NoFear", "RetreatShout", "Прикройте!");

	/** Свои стрелки в этом радиусе прикрывают отходящего, см. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|AI|Cover", meta = (ClampMin = "0"))
	float CoverFireRequestRadius = 2500.f;

	/** Сколько прикрывать: чаще выглядывать и дольше стрелять, с. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|AI|Cover", meta = (ClampMin = "0"))
	float CoveringFireTime = 8.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|AI|Cover")
	FText CoveringFireShout = NSLOCTEXT("NoFear", "CoveringFireShout", "Прикрываю!");

	/** Есть укрытие (занято за ботом). */
	UFUNCTION(BlueprintPure, Category = "NoFear|AI")
	bool HasCover() const { return Cover.Type != ENoFearCoverType::None; }

	UFUNCTION(BlueprintPure, Category = "NoFear|AI")
	FVector GetCoverLocation() const { return Cover.Location; }

	UFUNCTION(BlueprintPure, Category = "NoFear|AI")
	FVector GetCoverPeekLocation() const { return Cover.PeekLocation; }

	/** Укрытие низкое: сидеть за ним, стрелять стоя. */
	UFUNCTION(BlueprintPure, Category = "NoFear|AI")
	bool IsCoverLow() const { return Cover.Type == ENoFearCoverType::Low; }

	/** Из укрытия есть откуда стрелять. */
	UFUNCTION(BlueprintPure, Category = "NoFear|AI")
	bool CanPeekFromCover() const { return HasCover() && Cover.bCanPeek; }

	/** Бот стоит в своём укрытии (не дальше 60 см от точки). */
	UFUNCTION(BlueprintPure, Category = "NoFear|AI")
	bool IsInCover() const;

	/** Раненый отходит (или уже в дальнем укрытии). */
	UFUNCTION(BlueprintPure, Category = "NoFear|AI")
	bool IsRetreating() const { return bRetreating; }

	/** Сейчас смотрит на портал, прикрывая спасение (приказ CoverRescue без цели). */
	bool IsRescueFacing() const { return bRescueFacing; }

	/** Прикрывает отходящего своего. */
	UFUNCTION(BlueprintPure, Category = "NoFear|AI")
	bool IsCoveringFire() const;

	/** Фаза боя из укрытия: Hide, Peek, Fire, Return или пусто (для отладки и тестов). */
	UFUNCTION(BlueprintPure, Category = "NoFear|AI")
	FString GetCoverPhase() const { return CoverPhase; }

	void SetCoverPhase(const FString& Phase) { CoverPhase = Phase; }

	/** Удобная дистанция до цели для текущего оружия (UNoFearWeaponData::GetBotPreferredRange). */
	UFUNCTION(BlueprintPure, Category = "NoFear|AI")
	FVector2D GetPreferredRange() const;

	/** Цель в удобной дистанции оружия (дробовик — близко, винтовка — до 2000 и чуть дальше). */
	UFUNCTION(BlueprintPure, Category = "NoFear|AI")
	bool IsTargetInWeaponRange() const;

	/** Перепроверить укрытие при следующем обновлении (задача боя из укрытия отработала свои циклы). */
	void RequestCoverRefresh();

	/** Отпустить укрытие и бронь. */
	UFUNCTION(BlueprintCallable, Category = "NoFear|AI")
	void ReleaseCover();

	/**
	 * Точка для открытого боя: цель не видна — к месту, где её видели; видна — шаг вбок на удобной дистанции,
	 * откуда видно цель и свои не на линии огня. false — подходящей точки нет.
	 */
	bool FindCombatPosition(FVector& OutLocation) const;

	/** Свой просит прикрыть: поднять тревогу на угрозу и чаще стрелять CoveringFireTime секунд. */
	void ReceiveCoveringFireRequest(AActor* Requester, AActor* Threat, const FVector& ThreatLocation);

	/** Почему бот не бросил гранату в прошлый раз (для отладки и тестов). Пусто — бросил. */
	UFUNCTION(BlueprintPure, Category = "NoFear|AI")
	FString GetGrenadeDebugReason() const { return GrenadeDebugReason; }

	/** Сколько гранат бот бросил. */
	UFUNCTION(BlueprintPure, Category = "NoFear|AI")
	int32 GetGrenadesThrown() const { return GrenadesThrown; }

protected:
	virtual void OnPossess(APawn* InPawn) override;
	virtual void OnUnPossess() override;
	virtual void UpdateBot() override;
	virtual void HandleTargetChanged(AActor* NewTarget, AActor* OldTarget) override;
	virtual void WriteBlackboard() override;
	virtual float ScoreTarget(const AActor* Candidate, const FNoFearKnownActor& Knowledge) const override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** Раз в обновление: прочитать приказ отряда; новый — принять. */
	void UpdateSquadOrder();
	/** Принять новый приказ: найти укрытие по его правилам. false — выполнить нельзя. */
	bool ExecuteSquadOrder();
	/** Приказ с движением принят, укрытие по нему есть, а боец ещё не дошёл: укрытие не менять. */
	bool IsMovingToOrderCover() const;
	/** От кого прятаться по приказу: своя цель, иначе враг из приказа по сведениям отряда. */
	bool GetOrderThreat(AActor*& OutThreat, FVector& OutLocation) const;
	/**
	 * Куда вести бойца по приказу, когда укрытия по нему нет: место в сборе, к соседям, рубеж отхода,
	 * к прижатому своему. false — приказ никуда не ведёт.
	 */
	bool GetOrderMoveGoal(FVector& OutGoal) const;

	/** Раз в обновление: проверить укрытие, найти новое, начать отход. */
	void UpdateCover();
	/** Прикрывающий спасение без цели — лицом к порталу. */
	void UpdateRescueFacing();
	/** Найти и занять укрытие. bRetreat — отход раненого. */
	bool SearchCover(bool bRetreat);
	/** Параметры поиска укрытия от Threat по настройкам бойца (приказы потом меняют их). */
	virtual void MakeCoverSearch(bool bRetreat, AActor* Threat, const FVector& ThreatLocation, FNoFearCoverSearch& OutSearch) const;
	/** Найти укрытие по Search и занять его (старое — отпустить). false — не нашлось, старое остаётся. */
	bool ApplyCoverSearch(const FNoFearCoverSearch& Search);
	/**
	 * Контакт или огонь с места, а враг дальше, чем бьёт оружие (Current — до него сейчас): занять укрытие рядом —
	 * не дальше от врага и не больше чем на ContactCoverAdvance ближе. false — враг в дистанции или рядом укрытий нет.
	 */
	bool ApplyContactCoverSearch(const FNoFearCoverSearch& Search, float Current);
	/** Позвать своих прикрыть. */
	void RequestCoveringFire(AActor* Threat);

	/** Достать оружие, если убрано, и пополнить запас. */
	void MaintainWeapon();

	/** Раз в обновление: пора ли бросить гранату, и если да — бросить. */
	void TryThrowGrenade();
	/** Куда лечь гранате у цели: земля под Known. */
	bool GetGrenadeSpot(const AActor* Target, const FVector& Known, FVector& OutSpot) const;
	/** Бросить в Spot, если рядом нет своих и мирных. false — не бросил (причина в GrenadeDebugReason). */
	bool ThrowGrenadeAtSpot(const FVector& Spot);

	/** Приказ отряда (обновляется каждое обновление бота). */
	UPROPERTY(Transient)
	FNoFearSquadOrderInfo SquadOrder;
	int32 HandledOrderSerial = -1;
	bool bOrderStuck = false;
	bool bRescueFacing = false;
	/** Где стоять, ожидая санитара без укрытия (запомнено, когда санитар подошёл). */
	bool bHoldingForMedic = false;
	FVector MedicWaitLocation = FVector::ZeroVector;
	FString FlankSearchStats;

	FNoFearCoverPoint Cover;
	/** На кого записана бронь укрытия (пешка может уже пропасть при снятии). */
	TWeakObjectPtr<AActor> CoverOwner;
	FString CoverPhase;
	double AdvanceUntil = -1.0;
	bool bCoverRefreshRequested = false;
	double NextCoverCheckTime = 0.0;
	double NextRetreatTime = 0.0;
	double CoveringFireUntil = -1.0;
	int32 CoverLostChecks = 0;
	bool bRetreating = false;

	/** Оружие роли, которое боец держит сейчас (роль сменилась — вернуть стартовое). */
	TWeakObjectPtr<UNoFearWeaponData> AppliedRoleWeapon;

	double NextGrenadeTime = 0.0;
	FString GrenadeDebugReason;
	int32 GrenadesThrown = 0;
};
