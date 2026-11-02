#pragma once

#include "CoreMinimal.h"
#include "AI/NoFearBotSubsystem.h"
#include "Components/ActorComponent.h"
#include "Core/NoFearHealthComponent.h"
#include "NoFearBraceletComponent.generated.h"

class APawn;
class APostProcessVolume;
class UNoFearFearComponent;
class UAudioComponent;
class UNoFearWeaponComponent;
class USoundBase;
class USoundWaveProcedural;

/**
 * Числа страха героя (этап 12): Project Settings → Game → NoFear AI → Hero Fear.
 * Страх событий — до множителя команды Player (PlayerGainScale), потолок — после.
 */
USTRUCT(BlueprintType)
struct FNoFearHeroFearTuning
{
	GENERATED_BODY()

	// --- Монстры на виду ---

	/** Дальше этого монстров не замечает, см. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HeroFear|Monsters", meta = (ClampMin = "0"))
	float SightRange = 3500.f;

	/** Монстр в стороне от взгляда камеры не больше чем на столько — на виду, градусы. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HeroFear|Monsters", meta = (ClampMin = "0", ClampMax = "180"))
	float SightAngle = 60.f;

	/** Ближе этого монстра чувствует и за спиной (если нет стены), см. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HeroFear|Monsters", meta = (ClampMin = "0"))
	float CloseSenseDistance = 500.f;

	/** Ближе — «ближние» числа, дальше MonsterFarDistance — «дальние», между — плавно, см. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HeroFear|Monsters", meta = (ClampMin = "0"))
	float MonsterNearDistance = 600.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HeroFear|Monsters", meta = (ClampMin = "0"))
	float MonsterFarDistance = 3000.f;

	/** Разовый испуг, когда монстра впервые увидел. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HeroFear|Monsters", meta = (ClampMin = "0"))
	float MonsterShockNear = 25.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HeroFear|Monsters", meta = (ClampMin = "0"))
	float MonsterShockFar = 10.f;

	/** Пока монстр на виду — страх в секунду (по ближайшему). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HeroFear|Monsters", meta = (ClampMin = "0"))
	float MonsterRateNear = 18.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HeroFear|Monsters", meta = (ClampMin = "0"))
	float MonsterRateFar = 3.f;

	/** Потолок страха от монстра на виду. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HeroFear|Monsters", meta = (ClampMin = "0", ClampMax = "100"))
	float MonsterCapNear = 100.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HeroFear|Monsters", meta = (ClampMin = "0", ClampMax = "100"))
	float MonsterCapFar = 65.f;

	/** Монстр, не попадавшийся на глаза столько секунд, снова пугает разовым испугом. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HeroFear|Monsters", meta = (ClampMin = "0"))
	float MonsterForgetTime = 12.f;

	// --- Здоровье и раны ---

	/** Ниже этой доли здоровья страх растёт сам. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HeroFear|Health", meta = (ClampMin = "0", ClampMax = "1"))
	float LowHealthFraction = 0.35f;

	/** Страх в секунду при здоровье около нуля; на пороге LowHealthFraction — половина. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HeroFear|Health", meta = (ClampMin = "0"))
	float LowHealthRate = 6.f;

	/** Потолок страха от ран: на границе LowHealthFraction и при здоровье около нуля. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HeroFear|Health", meta = (ClampMin = "0", ClampMax = "100"))
	float LowHealthCapMin = 45.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HeroFear|Health", meta = (ClampMin = "0", ClampMax = "100"))
	float LowHealthCapMax = 85.f;

	/** Страх за единицу полученного урона (не меньше DamageFearMin за удар). От людей — не выше CombatFearCap. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HeroFear|Health", meta = (ClampMin = "0"))
	float DamageFearPerPoint = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HeroFear|Health", meta = (ClampMin = "0"))
	float DamageFearMin = 4.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HeroFear|Health", meta = (ClampMin = "0", ClampMax = "100"))
	float DamageCap = 100.f;

	// --- Напарник ---

	/** Напарник упал: разовый испуг... */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HeroFear|Companion", meta = (ClampMin = "0"))
	float CompanionDownedShock = 20.f;

	/** ...и страх в секунду, пока он лежит. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HeroFear|Companion", meta = (ClampMin = "0"))
	float CompanionDownedRate = 3.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HeroFear|Companion", meta = (ClampMin = "0", ClampMax = "100"))
	float CompanionDownedCap = 80.f;

	// --- Перестрелка (от людей — не выше CombatFearCap) ---

	/** Чужой выстрел рядом. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HeroFear|Combat", meta = (ClampMin = "0"))
	float GunshotFear = 4.f;

	/** Дальше этого чужие выстрелы не пугают, см. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HeroFear|Combat", meta = (ClampMin = "0"))
	float GunshotRadius = 2000.f;

	/** Пуля пролетела рядом. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HeroFear|Combat", meta = (ClampMin = "0"))
	float NearMissFear = 10.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HeroFear|Combat", meta = (ClampMin = "0"))
	float ExplosionFear = 20.f;

	/** Крик паникующего мирного рядом. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HeroFear|Combat", meta = (ClampMin = "0"))
	float ScreamFear = 4.f;

	/** Монстр ударил кого-то рядом. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HeroFear|Combat", meta = (ClampMin = "0"))
	float MonsterFightFear = 10.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HeroFear|Combat", meta = (ClampMin = "0", ClampMax = "100"))
	float MonsterFightCap = 75.f;

	// --- Трупы ---

	/** Увидел тело мирного или союзника: разовый испуг. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HeroFear|Corpses", meta = (ClampMin = "0"))
	float CorpseFear = 14.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HeroFear|Corpses", meta = (ClampMin = "0", ClampMax = "100"))
	float CorpseCap = 60.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HeroFear|Corpses", meta = (ClampMin = "0"))
	float CorpseDistance = 2000.f;

	// --- Облегчение ---

	/** Успокоил мирного (держал E до конца). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HeroFear|Relief", meta = (ClampMin = "0"))
	float CalmRelief = 8.f;

	/** Убил монстра. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HeroFear|Relief", meta = (ClampMin = "0"))
	float MonsterKillRelief = 10.f;

	/** Множитель спада страха героя поверх общего (рядом с союзниками и напарником спад и так быстрее). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HeroFear|Relief", meta = (ClampMin = "0"))
	float DecayMultiplier = 1.f;

	// --- Эффекты ---

	/** С этого страха дрожат руки (прицел гуляет мелко). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HeroFear|Effects", meta = (ClampMin = "0", ClampMax = "100"))
	float TremorStartFear = 30.f;

	/** Размах дрожи при страхе 100, градусы. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HeroFear|Effects", meta = (ClampMin = "0"))
	float TremorMaxDegrees = 0.45f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HeroFear|Effects", meta = (ClampMin = "0.1"))
	float TremorFrequency = 7.f;

	/** С этого страха прицел медленно уплывает; к 100 — на DriftMaxDegrees. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HeroFear|Effects", meta = (ClampMin = "0", ClampMax = "100"))
	float DriftStartFear = 60.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HeroFear|Effects", meta = (ClampMin = "0"))
	float DriftMaxDegrees = 1.2f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HeroFear|Effects", meta = (ClampMin = "0.01"))
	float DriftFrequency = 0.45f;

	/** С PanicThreshold растёт разброс оружия: столько при TerrorThreshold и SpreadMultiplierMax при 100. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HeroFear|Effects", meta = (ClampMin = "1"))
	float SpreadMultiplierTerror = 1.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HeroFear|Effects", meta = (ClampMin = "1"))
	float SpreadMultiplierMax = 1.8f;

	/** Скорость перезарядки в ужасе (от TerrorThreshold). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HeroFear|Effects", meta = (ClampMin = "0.1", ClampMax = "1"))
	float TerrorReloadSpeed = 0.7f;

	/** Сердце слышно с этого страха; пульс от HeartRateCalm (страх 0) до HeartRateMax (100), удары в минуту. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HeroFear|Effects", meta = (ClampMin = "0", ClampMax = "100"))
	float HeartbeatStartFear = 30.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HeroFear|Effects", meta = (ClampMin = "20"))
	float HeartRateCalm = 70.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HeroFear|Effects", meta = (ClampMin = "20"))
	float HeartRateMax = 150.f;

	/** Громкость сердца при страхе 100. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HeroFear|Effects", meta = (ClampMin = "0"))
	float HeartbeatVolume = 0.7f;

	/** Сила искажений экрана (виньетка, хроматическая аберрация, обесцвечивание): 0 — выключены. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HeroFear|Effects", meta = (ClampMin = "0", ClampMax = "2"))
	float ScreenEffectScale = 1.f;

	// --- Ступор ---

	/** Страх дошёл до этого — ступор: не стреляет и не ходит StuporDuration секунд, потом страх StuporFearAfter. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HeroFear|Stupor", meta = (ClampMin = "0", ClampMax = "100"))
	float StuporTriggerFear = 99.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HeroFear|Stupor", meta = (ClampMin = "0"))
	float StuporDuration = 1.8f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HeroFear|Stupor", meta = (ClampMin = "0", ClampMax = "100"))
	float StuporFearAfter = 80.f;

	/** Ступор не чаще раза в столько секунд. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HeroFear|Stupor", meta = (ClampMin = "0"))
	float StuporCooldown = 20.f;

	// --- Браслет: стабилизация ---

	/** Сколько страха снимает стабилизация за StabilizeTime секунд. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HeroFear|Stabilize", meta = (ClampMin = "0"))
	float StabilizeAmount = 40.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HeroFear|Stabilize", meta = (ClampMin = "0.05"))
	float StabilizeTime = 1.5f;

	/** Перезарядка стабилизации от её начала, с. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HeroFear|Stabilize", meta = (ClampMin = "0"))
	float StabilizeCooldown = 45.f;

	// --- Задержка дыхания ---

	/** Дольше этого дыхание не задержать, с. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HeroFear|Breath", meta = (ClampMin = "0.1"))
	float BreathMaxTime = 4.f;

	/** За столько секунд запас дыхания восполняется с нуля. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HeroFear|Breath", meta = (ClampMin = "0.1"))
	float BreathRecoverTime = 6.f;

	/** Выдохнул до дна — столько секунд задержать снова нельзя. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HeroFear|Breath", meta = (ClampMin = "0"))
	float BreathCooldown = 3.f;

	/** Пока дыхание задержано, страх растёт во столько раз быстрее и не спадает. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HeroFear|Breath", meta = (ClampMin = "1"))
	float BreathFearGainMultiplier = 1.5f;

	/** Какая доля прибавки разброса от страха остаётся при задержке дыхания. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HeroFear|Breath", meta = (ClampMin = "0", ClampMax = "1"))
	float BreathSpreadBonusScale = 0.5f;

	// --- Предупреждение о разломе (этап 13) ---

	/** Сколько секунд держится стрелка на разлом. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HeroFear|Rift", meta = (ClampMin = "0"))
	float RiftWarningTime = 3.f;
};

/**
 * Браслет стража (этап 12). Живёт на ANoFearPlayerController и работает со шкалой страха пешки игрока
 * (UNoFearFearComponent), так что после возрождения страх начинается с нуля.
 *
 * Пугает героя: монстры на виду и вплотную, раны и низкое здоровье, упавший напарник, чужая перестрелка
 * (выстрелы, пули рядом, взрывы — не выше CombatFearCap), тела мирных и союзников, паника людей вокруг
 * (передача страха — UNoFearFearSubsystem). Облегчает: спад без угрозы (быстрее рядом с союзниками),
 * успокоенный мирный, убитый монстр.
 *
 * Эффекты по страху: дрожь и увод прицела, разброс, сердцебиение, виньетка и аберрация, медленная перезарядка,
 * при 100 — короткий ступор. Способности: стабилизация (B) и задержка дыхания (держать Alt, целясь).
 */
UCLASS(ClassGroup = (NoFear), meta = (BlueprintSpawnableComponent))
class NOFEAR_API UNoFearBraceletComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UNoFearBraceletComponent();

	/** Браслет игрока, управляющего Pawn (или самого контроллера). */
	UFUNCTION(BlueprintPure, Category = "NoFear|Bracelet")
	static UNoFearBraceletComponent* FindBracelet(const AActor* PawnOrController);

	/** Множитель разброса оружия от страха для Pawn (1 — у ботов и спокойного героя). */
	static float GetSpreadMultiplierFor(const AActor* Pawn);

	/** Скорость перезарядки от страха для Pawn (1 — обычная). */
	static float GetReloadSpeedFor(const AActor* Pawn);

	/** Текущие числа (копия). */
	UFUNCTION(BlueprintPure, Category = "NoFear|Bracelet")
	FNoFearHeroFearTuning GetTuning() const { return Tuning; }

	/** Заменить числа на ходу. */
	UFUNCTION(BlueprintCallable, Category = "NoFear|Bracelet")
	void SetTuning(const FNoFearHeroFearTuning& NewTuning) { Tuning = NewTuning; }

	/** Шкала страха пешки игрока (или nullptr). */
	UFUNCTION(BlueprintPure, Category = "NoFear|Bracelet")
	UNoFearFearComponent* GetHeroFear() const;

	/** Страх героя, 0–100. */
	UFUNCTION(BlueprintPure, Category = "NoFear|Bracelet")
	float GetFear() const;

	/** Сглаженный страх, по которому идут эффекты. */
	UFUNCTION(BlueprintPure, Category = "NoFear|Bracelet")
	float GetEffectFear() const { return EffectFear; }

	// --- События (зовут подсистемы и контроллер) ---

	/** Опасность рядом с героем: выстрел, пуля, драка, крик, взрыв. Свои и союзные выстрелы не пугают. */
	UFUNCTION(BlueprintCallable, Category = "NoFear|Bracelet")
	void ReceiveDanger(ENoFearDangerType Type, AActor* Source, FVector ThreatLocation);

	/** Герой успокоил мирного. */
	/** Испуг героя от рёва монстра: Amount, но не выше Cap. Возвращает, на сколько вырос страх. */
	UFUNCTION(BlueprintCallable, Category = "NoFear|Bracelet")
	float ReceiveMonsterRoar(float Amount, float Cap, FVector Location, AActor* Source);

	/** Страх героя от шептуна (аура, крик, метка): источник Whisper. Возвращает, на сколько вырос страх. */
	UFUNCTION(BlueprintCallable, Category = "NoFear|Bracelet")
	float ReceiveWhisper(float Amount, float Cap, FVector Location, AActor* Source);

	UFUNCTION(BlueprintCallable, Category = "NoFear|Bracelet")
	void NotifyCalmedCivilian();

	/** Герой убил монстра. */
	UFUNCTION(BlueprintCallable, Category = "NoFear|Bracelet")
	void NotifyMonsterKilled();

	/** Браслет почуял разлом в Location: стрелка и сигнал на RiftWarningTime секунд (этап 13). */
	UFUNCTION(BlueprintCallable, Category = "NoFear|Bracelet")
	void ReportRift(FVector Location);

	UFUNCTION(BlueprintPure, Category = "NoFear|Bracelet")
	bool HasRiftWarning() const;

	UFUNCTION(BlueprintPure, Category = "NoFear|Bracelet")
	FVector GetRiftLocation() const { return RiftLocation; }

	/** 1 — только что сработало, 0 — погасло. */
	UFUNCTION(BlueprintPure, Category = "NoFear|Bracelet")
	float GetRiftWarningAlpha() const;

	// --- Эффекты ---

	/** Множитель разброса оружия сейчас. */
	UFUNCTION(BlueprintPure, Category = "NoFear|Bracelet")
	float GetSpreadMultiplier() const;

	/** Скорость перезарядки сейчас (1 — обычная). */
	UFUNCTION(BlueprintPure, Category = "NoFear|Bracelet")
	float GetReloadSpeed() const;

	/** Размах дрожи прицела сейчас, градусы (дрожь + увод). */
	UFUNCTION(BlueprintPure, Category = "NoFear|Bracelet")
	float GetAimShakeDegrees() const { return CurrentShakeDegrees; }

	/** Пульс, удары в минуту. */
	UFUNCTION(BlueprintPure, Category = "NoFear|Bracelet")
	float GetHeartRate() const;

	/** Сколько ударов сердца прозвучало (слышных, со страха HeartbeatStartFear). */
	UFUNCTION(BlueprintPure, Category = "NoFear|Bracelet")
	int32 GetHeartbeatCount() const { return HeartbeatCount; }

	/** Толчок сердца: 1 в момент удара, к следующему гаснет до 0. */
	UFUNCTION(BlueprintPure, Category = "NoFear|Bracelet")
	float GetHeartPulse() const { return HeartPulse; }

	/** Сила искажений экрана сейчас, 0..1+. */
	UFUNCTION(BlueprintPure, Category = "NoFear|Bracelet")
	float GetScreenEffectStrength() const { return ScreenStrength; }

	/** Виньетка, которую сейчас ставит браслет (для тестов). */
	UFUNCTION(BlueprintPure, Category = "NoFear|Bracelet")
	float GetVignetteIntensity() const;

	// --- Ступор ---

	UFUNCTION(BlueprintPure, Category = "NoFear|Bracelet")
	bool IsInStupor() const { return bInStupor; }

	/** Сколько раз за жизнь браслета был ступор. */
	UFUNCTION(BlueprintPure, Category = "NoFear|Bracelet")
	int32 GetStuporCount() const { return StuporCount; }

	/** Сколько секунд до возможности нового ступора. */
	UFUNCTION(BlueprintPure, Category = "NoFear|Bracelet")
	float GetStuporCooldownRemaining() const;

	// --- Стабилизация ---

	/** Нажать стабилизацию. false — на перезарядке, герой мёртв или нет шкалы. */
	UFUNCTION(BlueprintCallable, Category = "NoFear|Bracelet")
	bool StartStabilize();

	UFUNCTION(BlueprintPure, Category = "NoFear|Bracelet")
	bool IsStabilizing() const { return StabilizeRemaining > 0.f; }

	UFUNCTION(BlueprintPure, Category = "NoFear|Bracelet")
	float GetStabilizeCooldownRemaining() const;

	/** 1 — готова, 0 — только что использована. */
	UFUNCTION(BlueprintPure, Category = "NoFear|Bracelet")
	float GetStabilizeReadyFraction() const;

	// --- Дыхание ---

	/** Держать дыхание (клавиша или сценарий). Работает, только пока герой целится и запас есть. */
	UFUNCTION(BlueprintCallable, Category = "NoFear|Bracelet")
	void SetBreathHold(bool bHold);

	UFUNCTION(BlueprintPure, Category = "NoFear|Bracelet")
	bool IsHoldingBreath() const { return bHoldingBreath; }

	/** Запас дыхания, 0..1. */
	UFUNCTION(BlueprintPure, Category = "NoFear|Bracelet")
	float GetBreath() const { return Breath; }

	/** Выдохнул до дна и ещё не может задержать снова. */
	UFUNCTION(BlueprintPure, Category = "NoFear|Bracelet")
	bool IsBreathLocked() const;

	// --- Отладка ---

	/** Сколько страха всего добавил источник: Monster, Health, Damage, Companion, Combat, Corpse; Relief — снято. */
	UFUNCTION(BlueprintPure, Category = "NoFear|Bracelet")
	float GetSourceTotal(FName Source) const;

	UFUNCTION(BlueprintCallable, Category = "NoFear|Bracelet")
	void ResetSourceTotals() { SourceTotals.Reset(); }

	/** Сколько монстров сейчас на виду. */
	UFUNCTION(BlueprintPure, Category = "NoFear|Bracelet")
	int32 GetVisibleMonsterCount() const { return VisibleMonsters; }

	/** Строка состояния для экрана отладки (NoFear.Hero.Debug 1). */
	UFUNCTION(BlueprintPure, Category = "NoFear|Bracelet")
	FString GetDebugText() const;

	/** Звук удара сердца. Не задан — синтезируется. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NoFear|Bracelet")
	TObjectPtr<USoundBase> HeartbeatSound;

	/** Сигнал браслета (разлом). Не задан — синтезируется. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NoFear|Bracelet")
	TObjectPtr<USoundBase> BeepSound;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
	APlayerController* GetPlayerController() const;
	APawn* GetHeroPawn() const;
	bool IsHeroActive(const APawn* Pawn) const;
	double GetNow() const;

	/** Добавить страх от источника Tag; bCombat — от людей (потолок CombatFearCap). Возвращает прирост. */
	float AddHeroFear(FName Tag, float Amount, float Cap, const FVector& Location, const AActor* Source = nullptr);
	float GetThreshold(int32 Level) const;
	void Relieve(FName Tag, float Amount);

	void BindPawn(APawn* Pawn);
	void UnbindPawn();
	UFUNCTION()
	void HandleHeroDamaged(UNoFearHealthComponent* Component, const FNoFearDamageInfo& Info);

	void UpdateMonsters(APawn* Pawn, float DeltaTime);
	void UpdateCorpses(APawn* Pawn);
	void UpdateHealth(APawn* Pawn, float DeltaTime);
	void UpdateCompanion(APawn* Pawn, float DeltaTime);
	void UpdateStabilize(float DeltaTime);
	void UpdateBreath(APawn* Pawn, float DeltaTime);
	void UpdateStupor(APawn* Pawn);
	void EndStupor();
	void UpdateAimShake(float DeltaTime);
	void UpdateHeartbeat(float DeltaTime);
	void UpdateScreenEffects();
	void UpdateRiftBeep();

	void ApplyBreathModifiers(bool bApply);
	bool CanSee(const APawn* Pawn, const AActor* Target, const FVector& ViewLocation, const FVector& TargetPoint) const;
	void PlayHeartbeat(float Volume);
	void PlayBeep();
	/** Синтезированный звук: поток, в который дописываются удары и сигналы. */
	void QueueSynth(TObjectPtr<UAudioComponent>& Audio, TObjectPtr<USoundWaveProcedural>& Wave, const TArray<int16>& Samples);

	UPROPERTY(EditAnywhere, Category = "NoFear|Bracelet")
	FNoFearHeroFearTuning Tuning;

	/**
	 * Искажения экрана. Отдельный объём, а не UPostProcessComponent на контроллере: компонент работает,
	 * только если владелец виден, а контроллер всегда скрыт.
	 */
	UPROPERTY(Transient)
	TObjectPtr<APostProcessVolume> PostProcess;

	UPROPERTY(Transient)
	TObjectPtr<UAudioComponent> HeartAudio;

	UPROPERTY(Transient)
	TObjectPtr<USoundWaveProcedural> HeartWave;

	UPROPERTY(Transient)
	TObjectPtr<UAudioComponent> BeepAudio;

	UPROPERTY(Transient)
	TObjectPtr<USoundWaveProcedural> BeepWave;

	TWeakObjectPtr<APawn> BoundPawn;

	// Монстры и трупы.
	TMap<TWeakObjectPtr<AActor>, double> SeenMonsters;
	TSet<TWeakObjectPtr<AActor>> SeenCorpses;
	float ScanTimer = 0.f;
	float NearestMonsterDistance = -1.f;
	int32 VisibleMonsters = 0;

	bool bCompanionWasDowned = false;

	// Эффекты.
	float EffectFear = 0.f;
	float CurrentShakeDegrees = 0.f;
	FRotator AppliedShake = FRotator::ZeroRotator;
	float ShakeTime = 0.f;
	float ShakeFade = 1.f;
	float HeartPhase = 0.f;
	float HeartPulse = 0.f;
	int32 HeartbeatCount = 0;
	float ScreenStrength = 0.f;

	// Ступор.
	bool bInStupor = false;
	bool bStuporBlocksMove = false;
	TWeakObjectPtr<UNoFearWeaponComponent> StuporWeapons;
	double StuporEndTime = 0.0;
	double StuporReadyTime = 0.0;
	int32 StuporCount = 0;

	// Стабилизация.
	float StabilizeRemaining = 0.f;
	double StabilizeReadyTime = 0.0;

	// Дыхание.
	bool bBreathWanted = false;
	bool bHoldingBreath = false;
	float Breath = 1.f;
	double BreathLockedUntil = 0.0;
	TWeakObjectPtr<UNoFearFearComponent> BreathFear;
	float SavedGainMultiplier = 1.f;

	// Разлом.
	FVector RiftLocation = FVector::ZeroVector;
	double RiftTime = -1.0e9;
	int32 RiftBeepsPlayed = 0;

	TMap<FName, float> SourceTotals;
};
