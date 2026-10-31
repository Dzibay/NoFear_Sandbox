#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "GenericTeamAgentInterface.h"
#include "Core/NoFearHealthComponent.h"
#include "Core/NoFearTeam.h"
#include "NoFearCharacter.generated.h"

class UAnimInstance;
class UMaterialInterface;
class UNoFearGrenadeComponent;
class UNoFearPoseComponent;
class UNoFearWeaponComponent;

/**
 * База для всех персонажей: игрока, ботов и монстров.
 * CBP_SandboxCharacter наследуется от неё (Class Settings → Parent Class).
 * Команда, оружие и здоровье. Смерть — рэгдолл, снятие управления, конец работы ИИ; игрока возрождает контроллер.
 */
UCLASS()
class NOFEAR_API ANoFearCharacter : public ACharacter, public IGenericTeamAgentInterface
{
	GENERATED_BODY()

public:
	ANoFearCharacter();

	/** Команда. Меняется у каждого экземпляра на уровне. Игрок получает Player при вселении. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|Team", meta = (ExposeOnSpawn = true))
	ENoFearTeam Team = ENoFearTeam::Enemy;

	UFUNCTION(BlueprintCallable, Category = "NoFear|Team")
	void SetTeam(ENoFearTeam NewTeam);

	/**
	 * Стая монстра. Монстры с одинаковым именем — одна стая, где бы ни стояли. Пусто — стая собирается сама
	 * из монстров без имени, стоящих рядом (UNoFearPackSubsystem::PackFormRadius).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NoFear|Team")
	FName PackName;

	/**
	 * Отряд стрелка. Стрелки одной команды с одинаковым именем — один отряд (до 6 бойцов), где бы ни стояли:
	 * общее знание о врагах, распределение целей, командир. Пусто — воюет сам по себе.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NoFear|Squad", meta = (ExposeOnSpawn = true))
	FName Squad;

	/** Роль в отряде. Пусто — стрелок; командира, если его не задали, отряд назначит сам. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NoFear|Squad", meta = (ExposeOnSpawn = true))
	ENoFearSquadRole SquadRole = ENoFearSquadRole::None;

	/** Имя для интерфейса (панель напарника, подсказка «[E] ...»). Пусто — имя актора. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|Appearance")
	FText CharacterName;

	UFUNCTION(BlueprintPure, Category = "NoFear|Appearance")
	FText GetCharacterDisplayName() const;

	/**
	 * Перекрасить тело. На все слоты меша ставится Body Material, а все его цветовые параметры
	 * (Head, Torso, L Hand и т. д. у манекена UEFN) получают Body Color.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|Appearance")
	bool bApplyBodyColor = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|Appearance", meta = (EditCondition = "bApplyBodyColor"))
	FLinearColor BodyColor = FLinearColor::White;

	/** Отдельные части поверх Body Color: имя параметра материала (Head, Torso, L Hand...) → цвет. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|Appearance", meta = (EditCondition = "bApplyBodyColor"))
	TMap<FName, FLinearColor> BodyPartColors;

	/** Материал с цветовыми параметрами. По умолчанию M_UEFN_Mannequin_Masked (параметр на каждую часть тела). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|Appearance", meta = (EditCondition = "bApplyBodyColor"))
	TObjectPtr<UMaterialInterface> BodyMaterial;

	/** Сменить цвет тела во время игры (включает Apply Body Color). */
	UFUNCTION(BlueprintCallable, Category = "NoFear|Appearance")
	void SetBodyColor(FLinearColor NewColor);

	UFUNCTION(BlueprintCallable, Category = "NoFear|Appearance")
	void ApplyBodyColor();

	UFUNCTION(BlueprintPure, Category = "NoFear|Weapon")
	UNoFearWeaponComponent* GetWeaponComponent() const { return WeaponComponent; }

	UFUNCTION(BlueprintPure, Category = "NoFear|Weapon")
	UNoFearGrenadeComponent* GetGrenadeComponent() const { return GrenadeComponent; }

	UFUNCTION(BlueprintPure, Category = "NoFear|Health")
	UNoFearHealthComponent* GetHealthComponent() const { return HealthComponent; }

	UFUNCTION(BlueprintPure, Category = "NoFear|Pose")
	UNoFearPoseComponent* GetPoseComponent() const { return PoseComponent; }

	/**
	 * Пост-процесс AnimBP с узлом NoFear Pose (генерирует NoFear.GeneratePoseAnimBP: копия пост-процесса манекена с узлом поз).
	 * На BeginPlay ставится на меш вместо штатного пост-процесса, но только если скелет меша совпадает со скелетом этого AnimBP:
	 * у монстров и дрона другие скелеты, они остаются без визуальных поз (шатание на геймплей действует всё равно).
	 * Пусто — позы не рисуются.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|Pose")
	TSoftClassPtr<UAnimInstance> PoseAnimBlueprint;

	UFUNCTION(BlueprintPure, Category = "NoFear|Health")
	bool IsDead() const { return HealthComponent && HealthComponent->IsDead(); }

	/** Лежит раненым (UNoFearHealthComponent::IsDowned): пригнут, ползёт, не прыгает. */
	UFUNCTION(BlueprintPure, Category = "NoFear|Health")
	bool IsDowned() const { return HealthComponent && HealthComponent->IsDowned(); }

	/** Во сколько раз медленнее ползёт раненый (множитель ввода движения). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|Health", meta = (ClampMin = "0", ClampMax = "1"))
	float DownedMoveScale = 0.3f;

	/**
	 * Канал, которым стреляет оружие (UNoFearWeaponData::TraceChannel). Профили Pawn и CharacterMesh его игнорируют,
	 * поэтому на BeginPlay меш персонажа переключается на Block по этому каналу: пули попадают по костям.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|Health")
	TEnumAsByte<ECollisionChannel> HitTraceChannel = ECC_Visibility;

	/** При смерти включить рэгдолл. Выключено — персонаж просто замирает (анимацию смерти поставит Blueprint по OnDeath). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|Health")
	bool bRagdollOnDeath = true;

	/** Через сколько секунд после смерти тело исчезает. 0 — остаётся навсегда. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|Health", meta = (ClampMin = "0"))
	float CorpseLifeSpan = 30.f;

	/** Импульс на кость, в которую попала смертельная пуля (кг·см/с). 0 — тело просто падает. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|Health", meta = (ClampMin = "0"))
	float RagdollImpulse = 20000.f;

	/**
	 * В CBP_SandboxCharacter остались старые Camera(NotUsedByDefault) + SpringArm, и они активны.
	 * AActor::CalcCamera берёт первую активную камеру, поэтому они перебивают камеру GASP (риги
	 * прицеливания, приседания и стойки не видны). Пока включено, старая камера гасится на BeginPlay,
	 * если на персонаже есть GameplayCameraComponent.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|Camera")
	bool bDisableLegacySpringArmCamera = true;

	/** Шаги и приземление шумят: враги-боты в радиусе слышат и настораживаются (UNoFearBotSubsystem::ReportNoise). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|Noise")
	bool bMakeMovementNoise = true;

	/** Через сколько сантиметров пути звучит шаг. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|Noise", meta = (ClampMin = "50"))
	float FootstepDistance = 220.f;

	/** Слышимость шага шагом (до 320 см/с), см. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|Noise", meta = (ClampMin = "0"))
	float WalkFootstepRange = 400.f;

	/** Слышимость шага бегом (до 620 см/с), см. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|Noise", meta = (ClampMin = "0"))
	float RunFootstepRange = 1200.f;

	/** Слышимость шага спринтом, см. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|Noise", meta = (ClampMin = "0"))
	float SprintFootstepRange = 1800.f;

	/** Слышимость шага пригнувшись, см. 0 — бесшумно. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|Noise", meta = (ClampMin = "0"))
	float CrouchFootstepRange = 0.f;

	/** С какой скорости падения (см/с) приземление шумит. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|Noise", meta = (ClampMin = "0"))
	float LandingNoiseMinFallSpeed = 300.f;

	/** Слышимость приземления при падении 1200 см/с и быстрее, см (на минимальной скорости 600). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|Noise", meta = (ClampMin = "0"))
	float LandingNoiseRange = 1800.f;

	// IGenericTeamAgentInterface
	virtual void SetGenericTeamId(const FGenericTeamId& NewTeamId) override;
	virtual FGenericTeamId GetGenericTeamId() const override;

	/** Урон от оружия, взрывов и ApplyDamage сходится в UNoFearHealthComponent::ProcessDamage. */
	virtual float TakeDamage(float DamageAmount, struct FDamageEvent const& DamageEvent, AController* EventInstigator, AActor* DamageCauser) override;

	virtual void Landed(const FHitResult& Hit) override;

	/** Раненый ползёт: ввод ослаблен в DownedMoveScale раз. */
	virtual void AddMovementInput(FVector WorldDirection, float ScaleValue = 1.f, bool bForce = false) override;

	/** Раненый не встаёт, пока его не поднимут. */
	virtual void UnCrouch(bool bClientSimulation = false) override;

protected:
	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void PostInitializeComponents() override;
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void PossessedBy(AController* NewController) override;
	virtual void NotifyControllerChanged() override;
	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;
	virtual bool CanJumpInternal_Implementation() const override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "NoFear|Weapon")
	TObjectPtr<UNoFearWeaponComponent> WeaponComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "NoFear|Weapon")
	TObjectPtr<UNoFearGrenadeComponent> GrenadeComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "NoFear|Health")
	TObjectPtr<UNoFearHealthComponent> HealthComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "NoFear|Pose")
	TObjectPtr<UNoFearPoseComponent> PoseComponent;

private:
	void ApplyPoseAnimBlueprint();

	UFUNCTION()
	void HandleDeath(UNoFearHealthComponent* Component, const FNoFearDamageInfo& Info);

	UFUNCTION()
	void HandleDowned(UNoFearHealthComponent* Component, const FNoFearDamageInfo& Info);

	UFUNCTION()
	void HandleRevived(UNoFearHealthComponent* Component, AActor* Reviver);

	void StartRagdoll(const FNoFearDamageInfo& Info);
	void DisableLegacyCamera();
	void UpdateMovementNoise(float DeltaSeconds);

	FVector LastNoiseLocation = FVector::ZeroVector;
	bool bHasLastNoiseLocation = false;
	/** Пройдено по земле с прошлого шага. */
	float FootstepTravel = 0.f;
	/** Самая большая скорость падения в текущем прыжке. */
	float MaxFallSpeed = 0.f;
};
