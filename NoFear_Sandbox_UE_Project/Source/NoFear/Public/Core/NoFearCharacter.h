#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "GenericTeamAgentInterface.h"
#include "Core/NoFearHealthComponent.h"
#include "Core/NoFearTeam.h"
#include "NoFearCharacter.generated.h"

class UMaterialInterface;
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

	UFUNCTION(BlueprintPure, Category = "NoFear|Health")
	UNoFearHealthComponent* GetHealthComponent() const { return HealthComponent; }

	UFUNCTION(BlueprintPure, Category = "NoFear|Health")
	bool IsDead() const { return HealthComponent && HealthComponent->IsDead(); }

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

	// IGenericTeamAgentInterface
	virtual void SetGenericTeamId(const FGenericTeamId& NewTeamId) override;
	virtual FGenericTeamId GetGenericTeamId() const override;

	/** Урон от оружия, взрывов и ApplyDamage сходится в UNoFearHealthComponent::ProcessDamage. */
	virtual float TakeDamage(float DamageAmount, struct FDamageEvent const& DamageEvent, AController* EventInstigator, AActor* DamageCauser) override;

protected:
	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void PostInitializeComponents() override;
	virtual void BeginPlay() override;
	virtual void PossessedBy(AController* NewController) override;
	virtual void NotifyControllerChanged() override;
	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "NoFear|Weapon")
	TObjectPtr<UNoFearWeaponComponent> WeaponComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "NoFear|Health")
	TObjectPtr<UNoFearHealthComponent> HealthComponent;

private:
	UFUNCTION()
	void HandleDeath(UNoFearHealthComponent* Component, const FNoFearDamageInfo& Info);

	void StartRagdoll(const FNoFearDamageInfo& Info);
	void DisableLegacyCamera();
};
