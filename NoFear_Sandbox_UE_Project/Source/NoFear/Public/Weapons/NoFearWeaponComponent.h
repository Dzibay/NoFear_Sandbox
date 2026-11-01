#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "NoFearWeaponComponent.generated.h"

class ANoFearWeapon;
class UInputAction;
class UInputComponent;
class UInputMappingContext;
class UNoFearWeaponData;
struct FInputActionValue;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FNoFearWeaponChangedSignature, ANoFearWeapon*, NewWeapon, ANoFearWeapon*, OldWeapon);

/**
 * Оружие персонажа: список стволов, переключение, ввод игрока.
 * Уже добавлен в ANoFearCharacter; боты вызывают StartFire/StopFire напрямую.
 */
UCLASS(ClassGroup = (NoFear), meta = (BlueprintSpawnableComponent))
class NOFEAR_API UNoFearWeaponComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UNoFearWeaponComponent();

	// --- Настройки ---

	/** Оружие, которое выдаётся при старте. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapons")
	TArray<TObjectPtr<UNoFearWeaponData>> StartingWeapons;

	/** Сразу взять в руки первое оружие из списка. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapons")
	bool bEquipOnStart = true;

	/** Класс актора оружия. Можно подменить на Blueprint-наследника ANoFearWeapon. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapons")
	TSubclassOf<ANoFearWeapon> WeaponClass;

	/**
	 * Менять позу персонажа под оружие: компонент ищет у владельца переменную OverlayStateProperty
	 * (enum из GASP) и ставит в неё значение UNoFearWeaponData::OverlayState.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapons|Overlay")
	bool bDriveOverlayState = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapons|Overlay", meta = (EditCondition = "bDriveOverlayState"))
	FName OverlayStateProperty = TEXT("OverlayState");

	/** Поза без оружия в руках. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapons|Overlay", meta = (EditCondition = "bDriveOverlayState"))
	FName UnarmedOverlayState = TEXT("Default");

	/**
	 * Передавать отдачу рук в анимблюпринт персонажа: каждый кадр компонент пишет силу отдачи (float)
	 * и повороты костей (Rotator) в переменные AnimInstance с этими именами. Нет переменной — пропускается.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapons|Animation")
	bool bDriveArmRecoil = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapons|Animation", meta = (EditCondition = "bDriveArmRecoil"))
	FName RecoilAlphaVariable = TEXT("Recoil_Amount");

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapons|Animation", meta = (EditCondition = "bDriveArmRecoil"))
	FName UpperArmRecoilVariable = TEXT("UpperArm_R_Rotation");

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapons|Animation", meta = (EditCondition = "bDriveArmRecoil"))
	FName LowerArmRecoilVariable = TEXT("LowerArm_R_Rotations");

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapons|Animation", meta = (EditCondition = "bDriveArmRecoil"))
	FName HandRecoilVariable = TEXT("Hand_R_Rotations");

	// --- Ввод игрока ---

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputMappingContext> InputMapping;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input")
	int32 InputMappingPriority = 1;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputAction> FireAction;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputAction> AimAction;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputAction> ReloadAction;

	/** Axis1D: колесо мыши. Больше нуля — следующее оружие, меньше — предыдущее. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputAction> SwitchWeaponAction;

	/** Убрать оружие / достать обратно. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputAction> HolsterAction;

	// --- API ---

	/** Выдать оружие. Если такое уже есть — добавить патроны в запас. */
	UFUNCTION(BlueprintCallable, Category = "NoFear|Weapon")
	ANoFearWeapon* AddWeapon(UNoFearWeaponData* WeaponData);

	/**
	 * Достать оружие с этим номером. Текущее сначала убирается с анимацией; если во время убирания
	 * запросить другое, достанется последнее запрошенное.
	 */
	UFUNCTION(BlueprintCallable, Category = "NoFear|Weapon")
	void EquipWeapon(int32 Index);

	/** Убрать оружие с анимацией. */
	UFUNCTION(BlueprintCallable, Category = "NoFear|Weapon")
	void HolsterWeapon();

	/** Оружие, которое будет в руках, когда закончится смена. INDEX_NONE — никакого. */
	UFUNCTION(BlueprintPure, Category = "NoFear|Weapon")
	int32 GetTargetWeaponIndex() const { return bSwitching ? PendingIndex : CurrentIndex; }

	UFUNCTION(BlueprintCallable, Category = "NoFear|Weapon")
	void NextWeapon();

	UFUNCTION(BlueprintCallable, Category = "NoFear|Weapon")
	void PreviousWeapon();

	UFUNCTION(BlueprintCallable, Category = "NoFear|Weapon")
	void StartFire();

	UFUNCTION(BlueprintCallable, Category = "NoFear|Weapon")
	void StopFire();

	UFUNCTION(BlueprintCallable, Category = "NoFear|Weapon")
	void Reload();

	UFUNCTION(BlueprintCallable, Category = "NoFear|Weapon")
	void SetAiming(bool bNewAiming);

	UFUNCTION(BlueprintPure, Category = "NoFear|Weapon")
	ANoFearWeapon* GetCurrentWeapon() const;

	/** Поставить позу GASP по имени значения OverlayState (временно, до смены оружия или RefreshOverlayState). */
	void SetOverlayState(FName StateName) const;

	/** Вернуть позу по текущему оружию (или безоружную). */
	void RefreshOverlayState() const;

	/**
	 * Анимацию меша пересоздали (SetOverridePostProcessAnimBP и т.п. не шлют OnAnimInitialized): слои позы GASP
	 * сброшены к Default, хотя переменная прежняя. В ближайший тик поза привяжется заново.
	 */
	void RelinkOverlayState() { bOverlayLayersLost = true; }

	UFUNCTION(BlueprintPure, Category = "NoFear|Weapon")
	TArray<ANoFearWeapon*> GetWeapons() const { return ObjectPtrDecay(Weapons); }

	UFUNCTION(BlueprintPure, Category = "NoFear|Weapon")
	bool IsAiming() const { return bWantsToAim; }

	/**
	 * Руки заняты (бросок гранаты): спуск отпущен, оружие в руках спрятано, стрельба, перезарядка и смена оружия
	 * не работают. Снятие возвращает оружие и позу по нему.
	 */
	UFUNCTION(BlueprintCallable, Category = "NoFear|Weapon")
	void SetHandsBusy(bool bBusy);

	UFUNCTION(BlueprintPure, Category = "NoFear|Weapon")
	bool AreHandsBusy() const { return bHandsBusy; }

	/** Стрельба и броски запрещены (ступор героя от страха): спуск отпущен, нажатия игнорируются. */
	UFUNCTION(BlueprintCallable, Category = "NoFear|Weapon")
	void SetFireBlocked(bool bBlocked);

	UFUNCTION(BlueprintPure, Category = "NoFear|Weapon")
	bool IsFireBlocked() const { return bFireBlocked; }

	/** Идёт смена оружия или перезарядка: руки заняты оружием. */
	UFUNCTION(BlueprintPure, Category = "NoFear|Weapon")
	bool IsWeaponBusy() const;

	UPROPERTY(BlueprintAssignable, Category = "NoFear|Weapon")
	FNoFearWeaponChangedSignature OnWeaponChanged;

	/** Привязать ввод. Вызывается из ANoFearCharacter::SetupPlayerInputComponent. */
	void SetupPlayerInput(UInputComponent* PlayerInputComponent);

	/** Сменился контроллер: отпустить спуск, чтобы ничего не продолжало стрелять. */
	void HandleControllerChanged();

	/** Владелец погиб: все стволы прерывают действия, дальнейшие команды игнорируются. Вызывает ANoFearCharacter. */
	void HandleOwnerDeath();

	UFUNCTION(BlueprintPure, Category = "NoFear|Weapon")
	bool IsOwnerDead() const { return bOwnerDead; }

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	/** Общая точка для EquipWeapon/HolsterWeapon. Index == INDEX_NONE — остаться без оружия. */
	void RequestWeapon(int32 Index);
	void FinishSwitch();
	/** Убранное старое оружие заменяется новым (или ничем) без анимации убирания. */
	void CompleteSwitch(int32 NewIndex);

	void UpdateArmRecoilVariables();

	void OnSwitchWeaponInput(const FInputActionValue& Value);
	void OnAimInput(const FInputActionValue& Value);
	void OnAimCanceled();
	void OnHolsterInput();

	UPROPERTY(Transient)
	TArray<TObjectPtr<ANoFearWeapon>> Weapons;

	int32 CurrentIndex = INDEX_NONE;
	int32 LastEquippedIndex = 0;
	bool bWantsToAim = false;
	bool bOwnerDead = false;

	/** Последняя заказанная поза: Blueprint персонажа может сбросить OverlayState сам, тогда тик вернёт её. */
	mutable FName DesiredOverlayState;

	/** Анимация меша пересоздана: слои позы GASP слетели, хотя переменная осталась прежней. */
	bool bOverlayLayersLost = false;

	/**
	 * Вернуть заказанную позу, если переменная персонажа изменилась помимо компонента
	 * или анимацию пересоздали (слои позы привязывает только OnRep_OverlayState).
	 */
	void EnforceOverlayState();

	UFUNCTION()
	void HandleAnimInitialized();
	bool bHandsBusy = false;
	bool bFireBlocked = false;

	// Идёт убирание текущего оружия; после него достаём PendingIndex.
	bool bSwitching = false;
	int32 PendingIndex = INDEX_NONE;
	FTimerHandle SwitchTimer;

	// Переменные отдачи в AnimInstance; ищутся заново, если сменился класс анимблюпринта.
	TWeakObjectPtr<UClass> RecoilAnimClass;
	FProperty* RecoilAlphaProperty = nullptr;
	FStructProperty* UpperArmRecoilProperty = nullptr;
	FStructProperty* LowerArmRecoilProperty = nullptr;
	FStructProperty* HandRecoilProperty = nullptr;
};
