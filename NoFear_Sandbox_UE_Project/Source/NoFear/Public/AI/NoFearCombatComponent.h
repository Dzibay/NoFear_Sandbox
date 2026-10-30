#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Engine/EngineTypes.h"
#include "Core/NoFearHealthComponent.h"
#include "NoFearCombatComponent.generated.h"

class UBlackboardComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FNoFearTargetChangedSignature, AActor*, NewTarget);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FNoFearSuppressedChangedSignature, bool, bIsSuppressed);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FNoFearUnderFireSignature, AActor*, Shooter, FVector, ShotOrigin, float, SuppressionAdded);

/**
 * Боевая логика бота: время реакции, точность стрельбы, линия видимости и подавление.
 * Добавляется на персонажа бота. Цель читается из ключа блэкборда TargetKeyName (AttackTarget),
 * а состояние пишется обратно в блэкборд, если там заведены ключи с такими же именами.
 */
UCLASS(ClassGroup = (AI), meta = (BlueprintSpawnableComponent, DisplayName = "NoFear Combat"))
class NOFEAR_API UNoFearCombatComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UNoFearCombatComponent();

	// --- Цель ---

	UFUNCTION(BlueprintPure, Category = "NoFear|Combat")
	AActor* GetTarget() const { return CurrentTarget.Get(); }

	/** От кого прятаться: текущая цель, а если её нет — тот, кто стрелял в бота последним. */
	UFUNCTION(BlueprintPure, Category = "NoFear|Combat")
	AActor* GetThreatActor() const;

	/** Задать цель вручную. Обычно не нужно: цель берётся из блэкборда. Пустая цель возвращает чтение из блэкборда. */
	UFUNCTION(BlueprintCallable, Category = "NoFear|Combat")
	void SetTarget(AActor* NewTarget);

	UFUNCTION(BlueprintPure, Category = "NoFear|Combat")
	bool HasLineOfSight() const { return bHasLineOfSight; }

	/** Сколько секунд назад цель была видна последний раз. */
	UFUNCTION(BlueprintPure, Category = "NoFear|Combat")
	float GetTimeSinceTargetSeen() const;

	UFUNCTION(BlueprintPure, Category = "NoFear|Combat")
	FVector GetLastKnownTargetLocation() const { return LastKnownTargetLocation; }

	/** Цель видна и время реакции после её обнаружения прошло. */
	UFUNCTION(BlueprintPure, Category = "NoFear|Combat")
	bool IsReadyToFire() const;

	// --- Точность ---

	/** Направление выстрела с разбросом. MuzzleLocation — начало трассировки выстрела. */
	UFUNCTION(BlueprintCallable, Category = "NoFear|Combat")
	FVector ComputeShotDirection(FVector MuzzleLocation);

	/** Конечная точка трассировки выстрела на дистанции Range. */
	UFUNCTION(BlueprintCallable, Category = "NoFear|Combat")
	FVector ComputeShotEnd(FVector MuzzleLocation, float Range = 5000.f);

	/** Текущий разброс в градусах (стандартное отклонение) для выстрела на дистанцию Distance. */
	UFUNCTION(BlueprintPure, Category = "NoFear|Combat")
	float GetCurrentSpreadDegrees(float Distance) const;

	// --- Подавление ---

	/** Добавить подавление (0..1). Обычно вызывается из UNoFearCombatSubsystem::ReportShot. */
	UFUNCTION(BlueprintCallable, Category = "NoFear|Suppression")
	void AddSuppression(float Amount, AActor* Shooter, FVector ShotOrigin);

	UFUNCTION(BlueprintPure, Category = "NoFear|Suppression")
	float GetSuppression() const { return Suppression; }

	UFUNCTION(BlueprintPure, Category = "NoFear|Suppression")
	bool IsSuppressed() const { return bIsSuppressed; }

	UFUNCTION(BlueprintPure, Category = "NoFear|Suppression")
	AActor* GetLastShooter() const { return LastShooter.Get(); }

	UFUNCTION(BlueprintPure, Category = "NoFear|Suppression")
	FVector GetLastShotOrigin() const { return LastShotOrigin; }

	UPROPERTY(BlueprintAssignable, Category = "NoFear|Combat")
	FNoFearTargetChangedSignature OnTargetChanged;

	/** Кто-то погиб: если это была цель или обидчик, они забываются. Вызывает UNoFearCombatSubsystem::NotifyActorDied. */
	void HandleActorDied(AActor* DeadActor);

	/** Бот стал подавлен или пришёл в себя. */
	UPROPERTY(BlueprintAssignable, Category = "NoFear|Suppression")
	FNoFearSuppressedChangedSignature OnSuppressedChanged;

	/** Пуля прошла рядом или попала в бота. */
	UPROPERTY(BlueprintAssignable, Category = "NoFear|Suppression")
	FNoFearUnderFireSignature OnUnderFire;

	// --- Команда ---

	/** Индекс команды (как в E_DamageTeams: 0 Player, 1 NPC, 2 Monster, 3 AggroMonster). Пули своей команды не подавляют. -1 — без команды. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NoFear|Team")
	int32 TeamId = 1;

	// --- Ключи блэкборда (None — не писать) ---

	/** Object-ключ с текущей целью. */
	UPROPERTY(EditAnywhere, Category = "NoFear|Blackboard")
	FName TargetKeyName = TEXT("AttackTarget");

	/** Bool: цель видна. */
	UPROPERTY(EditAnywhere, Category = "NoFear|Blackboard")
	FName HasLineOfSightKeyName = TEXT("HasLineOfSight");

	/** Bool: цель видна и реакция прошла. */
	UPROPERTY(EditAnywhere, Category = "NoFear|Blackboard")
	FName CanFireKeyName = TEXT("CanFire");

	/** Bool: бот подавлен. */
	UPROPERTY(EditAnywhere, Category = "NoFear|Blackboard")
	FName IsSuppressedKeyName = TEXT("IsSuppressed");

	/** Float: уровень подавления 0..1. */
	UPROPERTY(EditAnywhere, Category = "NoFear|Blackboard")
	FName SuppressionKeyName = TEXT("Suppression");

	/** Vector: где цель видели последний раз. */
	UPROPERTY(EditAnywhere, Category = "NoFear|Blackboard")
	FName LastKnownLocationKeyName = TEXT("LastKnownLocation");

	// --- Восприятие ---

	/** Как часто обновлять видимость цели и подавление, сек. */
	UPROPERTY(EditAnywhere, Category = "NoFear|Perception", meta = (ClampMin = "0.02"))
	float UpdateInterval = 0.1f;

	/** Канал трассировки линии видимости. */
	UPROPERTY(EditAnywhere, Category = "NoFear|Perception")
	TEnumAsByte<ECollisionChannel> SightChannel = ECC_Visibility;

	/** Время реакции после обнаружения цели, сек (случайное в диапазоне). */
	UPROPERTY(EditAnywhere, Category = "NoFear|Perception", meta = (ClampMin = "0"))
	float ReactionTimeMin = 0.25f;

	UPROPERTY(EditAnywhere, Category = "NoFear|Perception", meta = (ClampMin = "0"))
	float ReactionTimeMax = 0.6f;

	/** Если цель пропала из виду дольше этого времени, при повторном обнаружении снова нужны реакция и прицеливание. */
	UPROPERTY(EditAnywhere, Category = "NoFear|Perception", meta = (ClampMin = "0"))
	float TargetMemoryTime = 1.f;

	/** Включить у CharacterMovement следование по пути через ускорение (нужно для motion matching). */
	UPROPERTY(EditAnywhere, Category = "NoFear|Perception")
	bool bUseAccelerationForPaths = true;

	// --- Точность ---

	/** Разброс в идеальных условиях: прицелился, цель стоит, бот стоит, дистанция в пределах EffectiveRange. Градусы. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NoFear|Accuracy", meta = (ClampMin = "0"))
	float BaseSpreadDegrees = 1.2f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NoFear|Accuracy", meta = (ClampMin = "0"))
	float MaxSpreadDegrees = 10.f;

	/** Множитель разброса сразу после обнаружения цели; за AimSettleTime падает до 1. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NoFear|Accuracy", meta = (ClampMin = "1"))
	float AcquireSpreadMultiplier = 3.f;

	/** За сколько секунд бот полностью прицеливается. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NoFear|Accuracy", meta = (ClampMin = "0"))
	float AimSettleTime = 1.5f;

	/** Добавка к множителю разброса, когда цель бежит со скоростью 600 см/с. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NoFear|Accuracy", meta = (ClampMin = "0"))
	float TargetMovingSpread = 1.f;

	/** Добавка к множителю разброса, когда сам бот бежит со скоростью 600 см/с. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NoFear|Accuracy", meta = (ClampMin = "0"))
	float SelfMovingSpread = 0.8f;

	/** Дальше этой дистанции разброс растёт дополнительно. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NoFear|Accuracy", meta = (ClampMin = "0"))
	float EffectiveRange = 1500.f;

	/** Множитель разброса при полном подавлении. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NoFear|Accuracy", meta = (ClampMin = "1"))
	float SuppressedSpreadMultiplier = 2.5f;

	/** Множитель разброса при стрельбе вслепую (цель не видна, огонь по последней известной позиции). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NoFear|Accuracy", meta = (ClampMin = "1"))
	float BlindFireSpreadMultiplier = 2.f;

	/** Запаздывание прицела за движущейся целью, сек. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NoFear|Accuracy", meta = (ClampMin = "0"))
	float TrackingLag = 0.15f;

	/** Вероятность целиться в голову, а не в корпус. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NoFear|Accuracy", meta = (ClampMin = "0", ClampMax = "1"))
	float HeadshotChance = 0.08f;

	UPROPERTY(EditAnywhere, Category = "NoFear|Accuracy")
	FName ChestBoneName = TEXT("spine_03");

	UPROPERTY(EditAnywhere, Category = "NoFear|Accuracy")
	FName HeadBoneName = TEXT("head");

	// --- Подавление ---

	/** Чувствительность к подавлению: 0.5 — ветеран, 1.5 — новобранец. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NoFear|Suppression", meta = (ClampMin = "0"))
	float SuppressionSensitivity = 1.f;

	/** Сколько секунд после последней пули подавление не спадает. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NoFear|Suppression", meta = (ClampMin = "0"))
	float SuppressionDecayDelay = 1.2f;

	/** Скорость спада подавления в секунду. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NoFear|Suppression", meta = (ClampMin = "0"))
	float SuppressionDecayRate = 0.3f;

	/** С этого уровня бот считается подавленным. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NoFear|Suppression", meta = (ClampMin = "0", ClampMax = "1"))
	float SuppressedThreshold = 0.6f;

	/** Ниже этого уровня бот выходит из подавления. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NoFear|Suppression", meta = (ClampMin = "0", ClampMax = "1"))
	float RecoverThreshold = 0.25f;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
	/** Погиб сам бот: перестаёт думать, целиться и попадать в реестр. */
	UFUNCTION()
	void HandleOwnerDeath(UNoFearHealthComponent* Component, const FNoFearDamageInfo& Info);

	void ChangeTarget(AActor* NewTarget);
	void StartAcquisition();
	void UpdateLineOfSight();
	void UpdateSuppression(float DeltaTime);
	void RefreshSuppressedState();
	void WriteBlackboard();

	UBlackboardComponent* GetBlackboard() const;
	FVector GetEyesLocation() const;
	FVector GetTargetAimPoint(const AActor* Target, bool bHead) const;
	double GetNow() const;

	TWeakObjectPtr<AActor> CurrentTarget;
	TWeakObjectPtr<AActor> LastShooter;
	bool bManualTarget = false;

	bool bHasLineOfSight = false;
	bool bIsSuppressed = false;
	float Suppression = 0.f;

	FVector LastKnownTargetLocation = FVector::ZeroVector;
	FVector LastKnownAimPoint = FVector::ZeroVector;
	FVector LastShotOrigin = FVector::ZeroVector;

	double LastSeenTime = -1.0e9;
	double AimStartTime = 0.0;
	double ReadyToFireTime = 0.0;
	double LastSuppressionTime = -1.0e9;
};
