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

	UFUNCTION(BlueprintCallable, Category = "NoFear|Weapon")
	void EquipWeapon(int32 Index);

	UFUNCTION(BlueprintCallable, Category = "NoFear|Weapon")
	void HolsterWeapon();

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

	UFUNCTION(BlueprintPure, Category = "NoFear|Weapon")
	TArray<ANoFearWeapon*> GetWeapons() const { return ObjectPtrDecay(Weapons); }

	UFUNCTION(BlueprintPure, Category = "NoFear|Weapon")
	bool IsAiming() const { return bWantsToAim; }

	UPROPERTY(BlueprintAssignable, Category = "NoFear|Weapon")
	FNoFearWeaponChangedSignature OnWeaponChanged;

	/** Привязать ввод. Вызывается из ANoFearCharacter::SetupPlayerInputComponent. */
	void SetupPlayerInput(UInputComponent* PlayerInputComponent);

	/** Сменился контроллер: отпустить спуск, чтобы ничего не продолжало стрелять. */
	void HandleControllerChanged();

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	void SetOverlayState(FName StateName) const;
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

	// Переменные отдачи в AnimInstance; ищутся заново, если сменился класс анимблюпринта.
	TWeakObjectPtr<UClass> RecoilAnimClass;
	FProperty* RecoilAlphaProperty = nullptr;
	FStructProperty* UpperArmRecoilProperty = nullptr;
	FStructProperty* LowerArmRecoilProperty = nullptr;
	FStructProperty* HandRecoilProperty = nullptr;
};
