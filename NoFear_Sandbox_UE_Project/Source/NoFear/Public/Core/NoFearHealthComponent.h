#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "NoFearHealthComponent.generated.h"

class AController;
class UDamageType;
class UNoFearHealthComponent;
struct FDamageEvent;

/** Что произошло при попадании. Приходит в OnDamaged и OnDeath. */
USTRUCT(BlueprintType)
struct NOFEAR_API FNoFearDamageInfo
{
	GENERATED_BODY()

	/** Сколько здоровья реально снято (после множителей зон, без «перебора» при смертельном ударе). */
	UPROPERTY(BlueprintReadOnly, Category = "Damage")
	float Amount = 0.f;

	/** Попадание в голову (см. UNoFearHealthComponent::HeadBone). */
	UPROPERTY(BlueprintReadOnly, Category = "Damage")
	bool bHeadshot = false;

	/** Этот удар убил. */
	UPROPERTY(BlueprintReadOnly, Category = "Damage")
	bool bFatal = false;

	/** Этот удар не убил, а ранил: персонаж упал и ждёт помощи (см. UNoFearHealthComponent::bCanBeDowned). */
	UPROPERTY(BlueprintReadOnly, Category = "Damage")
	bool bDowned = false;

	/** Кость, в которую попали (у точечного урона от оружия). */
	UPROPERTY(BlueprintReadOnly, Category = "Damage")
	FName BoneName;

	/** Точка попадания в мире. */
	UPROPERTY(BlueprintReadOnly, Category = "Damage")
	FVector HitLocation = FVector::ZeroVector;

	/** Куда летел выстрел (единичный вектор). Нулевой, если направление неизвестно. */
	UPROPERTY(BlueprintReadOnly, Category = "Damage")
	FVector Direction = FVector::ZeroVector;

	/** Откуда прилетело: дуло стрелка, центр взрыва или позиция виновника. Есть, только если bHasSource. */
	UPROPERTY(BlueprintReadOnly, Category = "Damage")
	FVector SourceLocation = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "Damage")
	bool bHasSource = false;

	UPROPERTY(BlueprintReadOnly, Category = "Damage")
	TObjectPtr<AController> InstigatorController = nullptr;

	UPROPERTY(BlueprintReadOnly, Category = "Damage")
	TObjectPtr<AActor> DamageCauser = nullptr;

	UPROPERTY(BlueprintReadOnly, Category = "Damage")
	TSubclassOf<UDamageType> DamageType;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FNoFearHealthChangedSignature, UNoFearHealthComponent*, Component, float, NewHealth, float, Delta);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FNoFearDamagedSignature, UNoFearHealthComponent*, Component, const FNoFearDamageInfo&, Info);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FNoFearDeathSignature, UNoFearHealthComponent*, Component, const FNoFearDamageInfo&, Info);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FNoFearDownedSignature, UNoFearHealthComponent*, Component, const FNoFearDamageInfo&, Info);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FNoFearRevivedSignature, UNoFearHealthComponent*, Component, AActor*, Reviver);

/**
 * Здоровье. Уже добавлен в ANoFearCharacter: урон от оружия (UGameplayStatics::ApplyPointDamage),
 * взрывов и ApplyDamage сходится в ProcessDamage.
 *
 * - Урон по зонам тела: голова и «слабые» кости умножают урон (кости ищутся вверх по скелету, поэтому
 *   множитель `upperarm_l` действует и на предплечье, и на кисть).
 * - Огонь по своим выключен (команды — UNoFearTeamLibrary), пока не включён bAllowFriendlyFire.
 * - Восстановление здоровья со временем (по умолчанию только у игрока).
 * - Смерть: событие OnDeath. Реакцию (рэгдолл, ИИ, респаун) делают ANoFearCharacter и подписчики.
 * - Ранение (bCanBeDowned): смертельный удар не убивает, а роняет. Упавший истекает кровью DownedDuration секунд,
 *   его можно поднять (Revive) — встаёт с ReviveHealthFraction здоровья. Включают игроку и напарнику.
 */
UCLASS(ClassGroup = (NoFear), meta = (BlueprintSpawnableComponent))
class NOFEAR_API UNoFearHealthComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UNoFearHealthComponent();

	// --- Здоровье ---

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Health", meta = (ClampMin = "1"))
	float MaxHealth = 100.f;

	// --- Урон ---

	/** Если выключено, урон от своей команды игнорируется (свой выстрел по себе проходит). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Health|Damage")
	bool bAllowFriendlyFire = false;

	/** Кость головы: попадание в неё или в её потомков считается хедшотом. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Health|Damage")
	FName HeadBone = TEXT("head");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Health|Damage", meta = (ClampMin = "0"))
	float HeadMultiplier = 2.5f;

	/** Множители для остальных зон: кость -> множитель. Действует и на кости ниже по иерархии. Нет в списке — 1. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Health|Damage")
	TMap<FName, float> BoneMultipliers;

	// --- Восстановление ---

	/** Здоровья в секунду. 0 — не восстанавливается. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Health|Regeneration", meta = (ClampMin = "0"))
	float RegenRate = 5.f;

	/** Сколько секунд без урона до начала восстановления. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Health|Regeneration", meta = (ClampMin = "0"))
	float RegenDelay = 8.f;

	/** Восстанавливать только персонажа, которым управляет игрок (боты и монстры не лечатся). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Health|Regeneration")
	bool bRegenPlayerOnly = true;

	// --- Ранение ---

	/**
	 * Смертельный удар роняет, а не убивает. Меняется во время игры: игроку — пока есть живой напарник,
	 * напарнику — пока жив его игрок. Kill() убивает и упавшего.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Health|Downed")
	bool bCanBeDowned = false;

	/** Сколько секунд упавший ждёт помощи, прежде чем умереть. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Health|Downed", meta = (ClampMin = "1"))
	float DownedDuration = 30.f;

	/** На сколько секунд сокращает таймер каждая единица урона по упавшему (пули, удары). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Health|Downed", meta = (ClampMin = "0"))
	float DownedTimePerDamage = 0.05f;

	/** То же для взрывов: граната рядом с упавшим почти добивает. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Health|Downed", meta = (ClampMin = "0"))
	float DownedExplosionTimePerDamage = 0.15f;

	/** С какой долей здоровья встаёт поднятый. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Health|Downed", meta = (ClampMin = "0.01", ClampMax = "1"))
	float ReviveHealthFraction = 0.3f;

	// --- События ---

	/** Здоровье изменилось: урон, лечение, восстановление. Delta < 0 при уроне. */
	UPROPERTY(BlueprintAssignable, Category = "Health")
	FNoFearHealthChangedSignature OnHealthChanged;

	/** Получен урон (уже после множителей). Для смертельного удара вызывается перед OnDeath. */
	UPROPERTY(BlueprintAssignable, Category = "Health")
	FNoFearDamagedSignature OnDamaged;

	UPROPERTY(BlueprintAssignable, Category = "Health")
	FNoFearDeathSignature OnDeath;

	/** Упал раненым (смертельный удар при bCanBeDowned). Info.bDowned = true. */
	UPROPERTY(BlueprintAssignable, Category = "Health")
	FNoFearDownedSignature OnDowned;

	/** Упавшего подняли. */
	UPROPERTY(BlueprintAssignable, Category = "Health")
	FNoFearRevivedSignature OnRevived;

	// --- API ---

	UFUNCTION(BlueprintPure, Category = "Health")
	float GetHealth() const { return Health; }

	UFUNCTION(BlueprintPure, Category = "Health")
	float GetMaxHealth() const { return MaxHealth; }

	UFUNCTION(BlueprintPure, Category = "Health")
	float GetHealthFraction() const { return MaxHealth > 0.f ? Health / MaxHealth : 0.f; }

	UFUNCTION(BlueprintPure, Category = "Health")
	bool IsDead() const { return bDead; }

	/** Сколько секунд прошло с последнего полученного урона. */
	UFUNCTION(BlueprintPure, Category = "Health")
	float GetTimeSinceLastDamage() const;

	/** Лежит раненым и ждёт помощи. */
	UFUNCTION(BlueprintPure, Category = "Health|Downed")
	bool IsDowned() const { return bDowned && !bDead; }

	/** Сколько секунд осталось до смерти упавшего (0 — не упал). */
	UFUNCTION(BlueprintPure, Category = "Health|Downed")
	float GetDownedTimeRemaining() const;

	/** Доля оставшегося времени упавшего: 1 — только упал, 0 — умирает. */
	UFUNCTION(BlueprintPure, Category = "Health|Downed")
	float GetDownedFraction() const;

	/** Поднять упавшего. Reviver — кто поднял. Возвращает false, если не лежал. */
	UFUNCTION(BlueprintCallable, Category = "Health|Downed")
	bool Revive(AActor* Reviver);

	/** Кто-то сейчас поднимает упавшего: таймер стоит, пока зовут каждый кадр (окно полсекунды). */
	UFUNCTION(BlueprintCallable, Category = "Health|Downed")
	void NotifyReviveProgress(float Progress);

	/** Упавшего поднимают прямо сейчас. */
	UFUNCTION(BlueprintPure, Category = "Health|Downed")
	bool IsBeingRevived() const;

	/** Сколько поднятия набрано (0..1), пока IsBeingRevived. */
	UFUNCTION(BlueprintPure, Category = "Health|Downed")
	float GetReviveProgress() const { return IsBeingRevived() ? ReviveProgress : 0.f; }

	/** Что уронило: для смерти по истечении таймера. */
	const FNoFearDamageInfo& GetDownedInfo() const { return DownedInfo; }

	/** Вылечить. Возвращает, сколько реально добавлено. Мёртвых не лечит. */
	UFUNCTION(BlueprintCallable, Category = "Health")
	float Heal(float Amount);

	/** Задать здоровье напрямую (без события урона). 0 и меньше — смерть. */
	UFUNCTION(BlueprintCallable, Category = "Health")
	void SetHealth(float NewHealth);

	/** Убить на месте. */
	UFUNCTION(BlueprintCallable, Category = "Health")
	void Kill(AController* Killer = nullptr, AActor* Causer = nullptr);

	/** Найти здоровье у актора или у пешки контроллера. */
	UFUNCTION(BlueprintPure, Category = "Health")
	static UNoFearHealthComponent* FindHealth(const AActor* Actor);

	/** Актор мёртв (нет компонента здоровья — не мёртв). */
	UFUNCTION(BlueprintPure, Category = "Health")
	static bool IsActorDead(const AActor* Actor);

	/** Актор лежит раненым. */
	UFUNCTION(BlueprintPure, Category = "Health|Downed")
	static bool IsActorDowned(const AActor* Actor);

	/** Мёртв или лежит раненым: воевать не может. */
	UFUNCTION(BlueprintPure, Category = "Health|Downed")
	static bool IsActorIncapacitated(const AActor* Actor);

	/**
	 * Принять урон. Вызывается из ANoFearCharacter::TakeDamage, вручную не нужен.
	 * Возвращает, сколько здоровья снято (0 — урон проигнорирован).
	 */
	float ProcessDamage(float Damage, const FDamageEvent& DamageEvent, AController* EventInstigator, AActor* DamageCauser);

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

protected:
	virtual void BeginPlay() override;

private:
	/** Множитель зоны по кости; bOutHeadshot — попали ли в голову. */
	float GetBoneMultiplier(FName HitBone, bool& bOutHeadshot) const;
	bool IsFriendlyFire(const AController* EventInstigator, const AActor* DamageCauser) const;
	bool CanRegenerate() const;
	void Die(const FNoFearDamageInfo& Info);
	void GoDown(const FNoFearDamageInfo& Info);
	void UpdateRegenTick();
	double GetNow() const;

	UPROPERTY(VisibleInstanceOnly, Category = "Health")
	float Health = 100.f;

	bool bDead = false;
	double LastDamageTime = -1.0e9;

	bool bDowned = false;
	double DownedUntil = 0.0;
	double LastReviveProgressTime = -1.0e9;
	float ReviveProgress = 0.f;

	UPROPERTY(Transient)
	FNoFearDamageInfo DownedInfo;
};
