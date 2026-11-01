#pragma once

#include "CoreMinimal.h"
#include "InputCoreTypes.h"
#include "Portal/NoFearPortalTypes.h"
#include "Subsystems/WorldSubsystem.h"
#include "NoFearPortalSubsystem.generated.h"

class ANoFearAIController;
class ANoFearPortal;
class APlayerController;
class UNoFearFreeComponent;

/**
 * Порталы страха (этап 13).
 *  - Слом: человек (не герой) в непрерывном ужасе — страх 100 и прямой испуг не реже BreakFreshTime — копит слом;
 *    за BreakTime ломается: впадает в транс на месте и становится проводником ближайшего портала или открывает новый.
 *  - Рост: портал растёт от проводников (с убылью), без них сжимается и затягивается. Аура пугает всех вокруг,
 *    у сердца — до слома. Проводник сам не успокаивается: страх держится на 100, пока его не освободят.
 *  - Монстры: по ступеням размера — одиночки, стайки с прыгунами и плевунами, волны с танком; бродят у портала.
 *    Портал затянулся — монстры бегут обратно, не успевшие за RecallTime растворяются.
 *  - Освобождение: герой держит E рядом с проводником, союзник-страж подходит и выводит из транса сам,
 *    крайняя мера — убить проводника.
 * Числа — Project Settings → Game → NoFear AI → Portal. Отладка: NoFear.Portal.Debug 1.
 */
UCLASS()
class NOFEAR_API UNoFearPortalSubsystem : public UTickableWorldSubsystem, public FNoFearPortalTuning
{
	GENERATED_BODY()

public:
	/** Копирует числа из Project Settings → NoFear AI → Portal. */
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	UFUNCTION(BlueprintPure, Category = "NoFear|Portal", meta = (WorldContext = "WorldContextObject", DisplayName = "Get Portal Subsystem"))
	static UNoFearPortalSubsystem* Get(const UObject* WorldContextObject);

	/** Текущие числа порталов (копия). */
	UFUNCTION(BlueprintPure, Category = "NoFear|Portal")
	FNoFearPortalTuning GetTuning() const { return *this; }

	/** Заменить числа порталов на ходу, до конца этого мира. */
	UFUNCTION(BlueprintCallable, Category = "NoFear|Portal")
	void SetTuning(const FNoFearPortalTuning& NewTuning) { static_cast<FNoFearPortalTuning&>(*this) = NewTuning; }

	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;

	/** Посчитать слом, рост, ауру, монстров и освобождение за DeltaTime сейчас (раз в UpdateInterval это происходит само). */
	UFUNCTION(BlueprintCallable, Category = "NoFear|Portal")
	void UpdatePortals(float DeltaTime);

	// --- Запросы ---

	/** Pawn — проводник (в трансе питает портал). */
	UFUNCTION(BlueprintPure, Category = "NoFear|Portal")
	bool IsFeeder(const APawn* Pawn) const;

	/** То же для актора или его контроллера в мире WorldContext; без подсистемы — false. */
	static bool IsActorFeeder(const AActor* Actor);

	/** Портал, который питает Pawn, или nullptr. */
	UFUNCTION(BlueprintPure, Category = "NoFear|Portal")
	ANoFearPortal* GetFeederPortal(const APawn* Pawn) const;

	/** Открытые порталы (и затягивающиеся). */
	UFUNCTION(BlueprintPure, Category = "NoFear|Portal")
	TArray<ANoFearPortal*> GetPortals() const;

	/** Сколько слома накопил Pawn, 0..1 (0 — не ломается). */
	UFUNCTION(BlueprintPure, Category = "NoFear|Portal")
	float GetBreakProgress(const APawn* Pawn) const;

	/** Насколько освобождён проводник, 0..1. */
	UFUNCTION(BlueprintPure, Category = "NoFear|Portal")
	float GetFreeProgress(const APawn* Pawn) const;

	/** Кого освобождает герой PlayerController и насколько. false — никого. */
	UFUNCTION(BlueprintPure, Category = "NoFear|Portal")
	bool GetHeroFreeing(const APlayerController* PlayerController, APawn*& OutFeeder, float& OutProgress) const;

	/** Кого выводит из транса страж Guardian, или nullptr. */
	UFUNCTION(BlueprintPure, Category = "NoFear|Portal")
	APawn* GetRescueTarget(const ANoFearAIController* Guardian) const;

	/** Actor сейчас освобождает проводника: герой держит E или страж выводит из транса. */
	UFUNCTION(BlueprintPure, Category = "NoFear|Portal")
	bool IsFreeingFeeder(const AActor* Actor) const;

	/** Монстр вышел из портала: его портал или nullptr. */
	UFUNCTION(BlueprintPure, Category = "NoFear|Portal")
	ANoFearPortal* GetMonsterPortal(const APawn* Monster) const;

	// --- Действия ---

	/** Сломать Pawn сейчас (отладка, сценарии): в транс и к порталу. Герой и монстры не ломаются. */
	UFUNCTION(BlueprintCallable, Category = "NoFear|Portal")
	bool BreakPerson(APawn* Pawn);

	/** Открыть портал в Location размером Size (0 — StartSize). Без проводника он сразу начнёт сжиматься. */
	UFUNCTION(BlueprintCallable, Category = "NoFear|Portal")
	ANoFearPortal* OpenPortal(FVector Location, float Size = 0.f);

	/** Вывести проводника из транса (Freer — кто освободил, может быть nullptr). */
	UFUNCTION(BlueprintCallable, Category = "NoFear|Portal")
	bool FreeFeeder(APawn* Feeder, AActor* Freer = nullptr);

	/** Добавить освобождению проводника Amount (доля от полного); дошло до 1 — освобождён. */
	UFUNCTION(BlueprintCallable, Category = "NoFear|Portal")
	bool ApplyFreeing(APawn* Feeder, float Amount, AActor* Freer = nullptr);

	/**
	 * Герой начинает освобождать проводника (UNoFearFreeComponent, держать E). Пока E держат, герой рядом и стоит —
	 * освобождение растёт за HeroFreeTime. bHeldByInput = false (Blueprint, тест) — идёт до StopHeroFreeing.
	 */
	UFUNCTION(BlueprintCallable, Category = "NoFear|Portal")
	bool StartHeroFreeing(APlayerController* PlayerController, APawn* Feeder, bool bHeldByInput = true);

	UFUNCTION(BlueprintCallable, Category = "NoFear|Portal")
	void StopHeroFreeing(APlayerController* PlayerController);

	/** Начать затягивать портал сейчас: проводники освобождаются, монстры бегут обратно. */
	UFUNCTION(BlueprintCallable, Category = "NoFear|Portal")
	void ClosePortal(ANoFearPortal* Portal);

	/** Поставить порталу размер (отладка). */
	UFUNCTION(BlueprintCallable, Category = "NoFear|Portal")
	void SetPortalSize(ANoFearPortal* Portal, float NewSize);

	/** Ближайший к Location портал (nullptr — нет ни одного). */
	UFUNCTION(BlueprintPure, Category = "NoFear|Portal")
	ANoFearPortal* FindNearestPortal(FVector Location, bool bIncludeClosing = true) const;

	/** Ступень для размера по текущим порогам. */
	UFUNCTION(BlueprintPure, Category = "NoFear|Portal")
	ENoFearPortalStage GetStageForSize(float Size) const;

	/** Радиус ауры портала, см. */
	UFUNCTION(BlueprintPure, Category = "NoFear|Portal")
	float GetAuraRadius(const ANoFearPortal* Portal) const;

	/** Сколько секунд непрерывного ужаса ломает Pawn (стрелки держатся дольше). */
	float GetBreakTimeFor(const APawn* Pawn) const;

	/** Клавиши взаимодействия игрока (IA_Interact), без раскладки — E. */
	static void GetInteractKeys(const APlayerController* PlayerController, TArray<FKey>& OutKeys);

private:
	struct FBreakState
	{
		float Progress = 0.f;
		double ImmuneUntil = 0.0;
	};

	struct FFeederState
	{
		TWeakObjectPtr<ANoFearPortal> Portal;
		TWeakObjectPtr<UNoFearFreeComponent> FreeComponent;
		float FreeProgress = 0.f;
		/** Когда освобождение последний раз росло: без рук оно спадает. */
		double LastFreeTime = -1.0e9;
		float SavedDecayMultiplier = 1.f;
		/** Когда проводник шепнёт снова. */
		double NextWhisperTime = 0.0;
	};

	struct FRescueState
	{
		TWeakObjectPtr<APawn> Feeder;
		double StartTime = 0.0;
		double NextMoveTime = 0.0;
		bool bInReach = false;
	};

	struct FHeroFreeState
	{
		TWeakObjectPtr<APawn> Feeder;
		TArray<FKey> Keys;
		bool bHeldByInput = false;
		/** Герой стоит на месте (SetIgnoreMoveInput) — снять при остановке. */
		bool bMoveLocked = false;
	};

	void UpdateBreaking(float DeltaTime, double Now);
	void UpdateFeeders(float DeltaTime, double Now);
	void UpdateGrowth(float DeltaTime, double Now);
	void UpdateAura(float DeltaTime);
	void UpdateMonsters(double Now);
	/** Расставить стражу портала у проводников, остальным — охотничий поводок. */
	void UpdateGuards(ANoFearPortal* Portal);
	void UpdateClosing(double Now);
	void UpdateHeroFreeing(float DeltaTime, double Now);
	void UpdateRescues(float DeltaTime, double Now);
	void AssignRescuers(double Now);

	/** Пешки, которых пугает портал: боты и герои. */
	void GatherPeople(TArray<APawn*>& OutPeople) const;
	bool CanBreak(const APawn* Pawn) const;
	ANoFearPortal* ChoosePortalFor(const APawn* Pawn, double Now);
	bool FindOpenLocation(const APawn* Pawn, FVector& OutLocation) const;
	void AddFeeder(ANoFearPortal* Portal, APawn* Pawn);
	/** Убрать проводника из списков (смерть, освобождение). bWake — вывести из транса и дать иммунитет. */
	void RemoveFeeder(APawn* Pawn, bool bWake, AActor* Freer);
	void SpawnWave(ANoFearPortal* Portal, int32 Count, double Now);
	APawn* SpawnMonster(ANoFearPortal* Portal, UClass* MonsterClass);
	void BeginClosing(ANoFearPortal* Portal, double Now);
	/** Затягивающийся портал снова получил проводника: открыт, монстры снова бродят. */
	void ReopenPortal(ANoFearPortal* Portal);
	void FinishPortal(ANoFearPortal* Portal);
	void EndRescue(ANoFearAIController* Guardian, bool bCooldown);
	void EndRescuesOf(const APawn* Feeder);
	int32 CountMonsters(const ANoFearPortal* Portal, UClass* OfClass) const;
	int32 CountAllMonsters() const;
	void PreloadMonsterClasses();
	void DrawDebug() const;

	TMap<TWeakObjectPtr<APawn>, FBreakState> BreakStates;
	TMap<TWeakObjectPtr<APawn>, FFeederState> Feeders;
	TMap<TWeakObjectPtr<ANoFearAIController>, FRescueState> Rescues;
	TMap<TWeakObjectPtr<ANoFearAIController>, double> RescueCooldowns;
	TMap<TWeakObjectPtr<APlayerController>, FHeroFreeState> HeroFreeing;
	TArray<TWeakObjectPtr<ANoFearPortal>> Portals;
	/** Когда возвращающемуся монстру последний раз прокладывали путь к порталу. */
	TMap<TWeakObjectPtr<APawn>, double> RecallMoveTimes;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UClass>> LoadedMonsterClasses;

	double LastOpenTime = -1.0e9;
	double NextRescueAssignTime = 0.0;
	float TimeSinceUpdate = 0.f;
};
