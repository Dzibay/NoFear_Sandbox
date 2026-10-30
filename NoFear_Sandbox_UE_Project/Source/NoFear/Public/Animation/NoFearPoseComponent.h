#pragma once

#include "CoreMinimal.h"
#include "Animation/NoFearPoseTypes.h"
#include "Components/ActorComponent.h"
#include "Core/NoFearHealthComponent.h"
#include "NoFearPoseComponent.generated.h"

class UAnimMontage;
class USkeletalMeshComponent;
class UNoFearPoseComponent;

/** Реакция на попадание. bStagger — сильный удар: персонаж пошатнулся и на время выведен из строя. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FNoFearHitReactionSignature, UNoFearPoseComponent*, Component, bool, bStagger);

/**
 * Позы и реакции на попадания. Уже добавлен в ANoFearCharacter.
 *
 * Позы — это повороты костей поверх GASP (таблица Poses). Применяет их отдельный пост-процесс AnimBP
 * ABP_NoFearPose (ANoFearCharacter::PoseAnimBlueprint), основной ABP_SandboxCharacter остаётся нетронутым.
 * Каждой позе можно задать ReplacementMontage: тогда вместо процедурных поворотов играет готовая анимация.
 *
 * Сами реакции на урон (боты; у игрока выключены, см. bReactWhenPlayerControlled):
 * - вздрагивание (Flinch) от любого попадания + короткое замедление (бот переходит на шаг);
 * - сильный удар или выстрел в голову — пошатывание (Stagger): стоит, не стреляет и не бьёт ~секунду;
 * - ранен (здоровья меньше WoundedHealthFraction) — поза Wounded, хромает и ходит шагом.
 */
UCLASS(ClassGroup = (NoFear), meta = (BlueprintSpawnableComponent))
class NOFEAR_API UNoFearPoseComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UNoFearPoseComponent();

	/** Описание поз. Заполняется по умолчанию (корпус, колени, голова под манекен UEFN); правится в Blueprint. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pose")
	TMap<ENoFearPose, FNoFearPoseSettings> Poses;

	// --- Реакции на урон ---

	/** Реагировать на попадания. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pose|Hit Reaction")
	bool bHitReactions = true;

	/** Реагировать, пока телом управляет игрок. Выключено: у игрока корпус не дёргается и прицел не сбивается. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pose|Hit Reaction")
	bool bReactWhenPlayerControlled = false;

	/** Урон меньше этого значения не вызывает реакции. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pose|Hit Reaction", meta = (ClampMin = "0"))
	float FlinchMinDamage = 1.f;

	/** Урон, при котором вздрагивание максимальное. Слабее — пропорционально меньше. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pose|Hit Reaction", meta = (ClampMin = "1"))
	float FlinchFullDamage = 25.f;

	/** Сколько секунд вздрагивание держится на максимуме до возврата. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pose|Hit Reaction", meta = (ClampMin = "0"))
	float FlinchHoldTime = 0.1f;

	/** Сколько секунд после попадания бот идёт шагом (замедление). 0 — не замедляется. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pose|Hit Reaction", meta = (ClampMin = "0"))
	float HitSlowTime = 0.8f;

	/** Одного попадания на столько долей максимального здоровья хватает, чтобы пошатнуть. 0 — не шатает от силы удара. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pose|Hit Reaction", meta = (ClampMin = "0", ClampMax = "1"))
	float StaggerDamageFraction = 0.25f;

	/** Попадание в голову всегда шатает. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pose|Hit Reaction")
	bool bStaggerOnHeadshot = true;

	/** Сколько секунд пошатнувшийся не двигается, не стреляет и не бьёт. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pose|Hit Reaction", meta = (ClampMin = "0"))
	float StaggerTime = 0.9f;

	/** Через сколько секунд после конца пошатывания его можно вызвать снова (против «стан-лока» очередью). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pose|Hit Reaction", meta = (ClampMin = "0"))
	float StaggerCooldown = 2.5f;

	// --- Раненый ---

	/** Включать позу Wounded при низком здоровье. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pose|Wounded")
	bool bWoundedStance = true;

	/** Здоровье ниже этой доли максимума — персонаж ранен. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pose|Wounded", meta = (ClampMin = "0", ClampMax = "1"))
	float WoundedHealthFraction = 0.3f;

	/** Выйти из раненого состояния, когда здоровье снова выше порога на столько долей (гистерезис). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pose|Wounded", meta = (ClampMin = "0", ClampMax = "1"))
	float WoundedRecoverMargin = 0.05f;

	/** Хромота: качание таза вбок на полной скорости шага, градусы. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pose|Wounded", meta = (ClampMin = "0"))
	float LimpPelvisRoll = 5.f;

	/** Хромота: просадка на больную ногу (сгиб колена) на полной скорости шага, градусы. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pose|Wounded", meta = (ClampMin = "0"))
	float LimpKneeBend = 12.f;

	/** Шагов в секунду при скорости 300 см/с. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pose|Wounded", meta = (ClampMin = "0"))
	float LimpStepRate = 1.2f;

	/** Поза GASP (имя значения OverlayState), которую получает безоружный раненый. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pose|Wounded")
	FName WoundedOverlayState = TEXT("Injured");

	/** Безоружному раненому ставить WoundedOverlayState вместе с процедурной сутулостью. Вооружённому поза оружия не меняется. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pose|Wounded")
	bool bUseInjuredOverlayWhenUnarmed = true;

	// --- События ---

	UPROPERTY(BlueprintAssignable, Category = "Pose")
	FNoFearHitReactionSignature OnHitReaction;

	// --- API ---

	/**
	 * Включить позу на Duration секунд (потом плавно выключится). Intensity 0..1 — доля полных углов.
	 * DirectionYaw — для направленных поз: куда «идёт» корпус, градусы относительно взгляда персонажа (180 — назад).
	 */
	UFUNCTION(BlueprintCallable, Category = "Pose")
	void PlayPose(ENoFearPose Pose, float Duration, float Intensity = 1.f, float DirectionYaw = 0.f);

	/** Держать позу, пока не выключат (bHeld = false). */
	UFUNCTION(BlueprintCallable, Category = "Pose")
	void SetPoseHeld(ENoFearPose Pose, bool bHeld, float Intensity = 1.f);

	/** Текущий вес позы 0..1. */
	UFUNCTION(BlueprintPure, Category = "Pose")
	float GetPoseWeight(ENoFearPose Pose) const;

	/** Реакция на попадание. Вызывается сама по OnDamaged; можно позвать вручную (тест, скрипты). */
	UFUNCTION(BlueprintCallable, Category = "Pose")
	void ApplyHitReaction(const FNoFearDamageInfo& Info);

	/** Принудительно пошатнуть. Direction — куда толкает (мировой вектор), нулевой — назад. */
	UFUNCTION(BlueprintCallable, Category = "Pose")
	void Stagger(FVector Direction = FVector::ZeroVector, float Duration = -1.f);

	/** Пошатнулся: не двигается, не стреляет и не бьёт. */
	UFUNCTION(BlueprintPure, Category = "Pose")
	bool IsStaggered() const;

	/** Замедлен после попадания (или ранен): должен идти шагом. */
	UFUNCTION(BlueprintPure, Category = "Pose")
	bool IsSlowed() const;

	UFUNCTION(BlueprintPure, Category = "Pose")
	bool IsWounded() const { return bWounded; }

	/** Пошатнулся ли актор (нет компонента — нет). Удобно из задач и компонентов боя. */
	UFUNCTION(BlueprintPure, Category = "Pose")
	static bool IsActorStaggered(const AActor* Actor);

	/** Повороты костей на этот кадр в пространстве меша. Вызывается анимацией с игрового потока. */
	FNoFearPoseFrame BuildFrame() const;

	/** Имя кости скелета манекена UEFN для слота. */
	static FName GetBoneName(ENoFearPoseBone Bone);

	/** Строка для отладки: активные позы с весами. */
	FString DescribeState() const;

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	struct FPoseState
	{
		/** Плавный вес 0..1: растёт, пока поза включена, спадает после. */
		float Weight = 0.f;
		/** Доля полных углов (Intensity из PlayPose). */
		float Intensity = 0.f;
		float DirectionYaw = 0.f;
		bool bHeld = false;
		double HoldUntil = 0.0;
		/** Поза играет монтажом, а не поворотами костей. */
		bool bMontage = false;
	};

	void FillDefaultPoses();
	void StartPose(ENoFearPose Pose, float Duration, bool bHeld, float Intensity, float DirectionYaw);
	USkeletalMeshComponent* GetMesh() const;
	double GetNow() const;

	/** Направление выстрела в углах «от взгляда персонажа». */
	float ComputePushYaw(const FNoFearDamageInfo& Info) const;
	bool CanReact() const;
	void UpdateWounded();
	void SetWounded(bool bNewWounded);
	void ApplyInjuredOverlay(bool bInjured);

	UFUNCTION()
	void HandleDamaged(UNoFearHealthComponent* Component, const FNoFearDamageInfo& Info);

	UFUNCTION()
	void HandleHealthChanged(UNoFearHealthComponent* Component, float NewHealth, float Delta);

	UFUNCTION()
	void HandleDeath(UNoFearHealthComponent* Component, const FNoFearDamageInfo& Info);

	FPoseState States[static_cast<int32>(ENoFearPose::Count)];

	TWeakObjectPtr<UNoFearHealthComponent> BoundHealth;
	bool bWounded = false;
	bool bDead = false;
	bool bInjuredOverlayApplied = false;
	double StaggerUntil = -1.0e9;
	double NextStaggerTime = -1.0e9;
	double SlowUntil = -1.0e9;
	/** Фаза шагов хромоты, радианы. */
	float LimpPhase = 0.f;
	float LimpSpeedAlpha = 0.f;
};
