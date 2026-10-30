#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "NoFearWeapon.generated.h"

class ACharacter;
class UNoFearWeaponData;
class USkeletalMeshComponent;
struct FNoFearImpactEffect;
struct FNoFearTimedSound;

UENUM(BlueprintType)
enum class ENoFearWeaponState : uint8
{
	/** В инвентаре, не в руках. */
	Holstered,
	Equipping,
	Idle,
	Reloading,
	/** Убирается с анимацией; после неё становится Holstered. */
	Holstering
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FNoFearAmmoChangedSignature, int32, MagazineAmmo, int32, ReserveAmmo);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FNoFearWeaponStateChangedSignature, ENoFearWeaponState, NewState);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FNoFearWeaponFiredSignature);

/**
 * Оружие в руках персонажа. Все настройки берутся из UNoFearWeaponData.
 * Создаётся и управляется UNoFearWeaponComponent; вручную на уровень ставить не нужно.
 *
 * Стрельба построена на одном таймере повторного выстрела: StopFire всегда его гасит,
 * поэтому автомат не может «залипнуть» после отпускания кнопки.
 */
UCLASS(Blueprintable)
class NOFEAR_API ANoFearWeapon : public AActor
{
	GENERATED_BODY()

public:
	ANoFearWeapon();

	/** Задаёт данные, заполняет магазин и вешает оружие на HolsterSocket. Вызывается сразу после спавна. */
	void Initialize(UNoFearWeaponData* InData, ACharacter* InCharacter);

	/** Взять в руки: прикрепить к сокету персонажа и проиграть доставание. */
	void Equip(ACharacter* InCharacter);

	/**
	 * Убрать с анимацией и звуками. Возвращает, сколько секунд это займёт; по их истечении оружие само
	 * перейдёт в Holstered. 0 — убрано мгновенно (нет анимации убирания).
	 */
	float BeginHolster();

	/** Убрать мгновенно: прервать стрельбу и перезарядку, повесить на HolsterSocket (или спрятать). */
	void Holster();

	UFUNCTION(BlueprintCallable, Category = "NoFear|Weapon")
	void StartFire();

	UFUNCTION(BlueprintCallable, Category = "NoFear|Weapon")
	void StopFire();

	UFUNCTION(BlueprintCallable, Category = "NoFear|Weapon")
	bool Reload();

	UFUNCTION(BlueprintCallable, Category = "NoFear|Weapon")
	void SetAiming(bool bNewAiming);

	/** Добавить патроны в запас (подбор). Возвращает, сколько реально взято. */
	UFUNCTION(BlueprintCallable, Category = "NoFear|Weapon")
	int32 AddReserveAmmo(int32 Amount);

	UFUNCTION(BlueprintPure, Category = "NoFear|Weapon")
	UNoFearWeaponData* GetWeaponData() const { return Data; }

	UFUNCTION(BlueprintPure, Category = "NoFear|Weapon")
	ENoFearWeaponState GetWeaponState() const { return State; }

	UFUNCTION(BlueprintPure, Category = "NoFear|Weapon")
	int32 GetMagazineAmmo() const { return MagazineAmmo; }

	UFUNCTION(BlueprintPure, Category = "NoFear|Weapon")
	int32 GetReserveAmmo() const { return ReserveAmmo; }

	UFUNCTION(BlueprintPure, Category = "NoFear|Weapon")
	bool IsAiming() const { return bAiming; }

	/** Текущий разброс в градусах (для прицела в UI). */
	UFUNCTION(BlueprintPure, Category = "NoFear|Weapon")
	float GetCurrentSpread() const;

	UFUNCTION(BlueprintPure, Category = "NoFear|Weapon")
	FVector GetMuzzleLocation() const;

	UFUNCTION(BlueprintPure, Category = "NoFear|Weapon")
	USkeletalMeshComponent* GetMesh() const { return Mesh; }

	/** Сила отдачи рук сейчас: 0 — покой, 1 — пик. Умножается на повороты костей из UNoFearWeaponData. */
	UFUNCTION(BlueprintPure, Category = "NoFear|Weapon")
	float GetArmRecoilAlpha() const;

	UPROPERTY(BlueprintAssignable, Category = "NoFear|Weapon")
	FNoFearAmmoChangedSignature OnAmmoChanged;

	UPROPERTY(BlueprintAssignable, Category = "NoFear|Weapon")
	FNoFearWeaponStateChangedSignature OnStateChanged;

	UPROPERTY(BlueprintAssignable, Category = "NoFear|Weapon")
	FNoFearWeaponFiredSignature OnFired;

	virtual void Tick(float DeltaSeconds) override;

protected:
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "NoFear|Weapon")
	TObjectPtr<USkeletalMeshComponent> Mesh;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "NoFear|Weapon")
	TObjectPtr<UNoFearWeaponData> Data;

	UPROPERTY(Transient)
	TObjectPtr<ACharacter> Character;

private:
	void SetState(ENoFearWeaponState NewState);
	void BroadcastAmmo();

	/** Есть ли невыполненный запрос на выстрел (кнопка зажата, очередь не закончена…). */
	bool WantsToFire() const;
	void ClearFireRequest();

	/** Единственная точка, где производится выстрел. Сама планирует следующий. */
	void HandleFireRequest();

	void FireShot();
	/** Одна пуля/дробина: трасса, урон, эффекты попадания. Возвращает точку, где пуля остановилась. */
	FVector FirePellet(const FVector& Start, const FVector& Direction, int32 PelletCount);
	void SpawnTracer(const FVector& Start, const TArray<FVector>& BulletEnds);
	bool ComputeAim(FVector& OutStart, FVector& OutDirection) const;
	void PlayFireEffects(const FVector& MuzzleLocation);
	void PlayImpactEffects(const FHitResult& Hit, const FVector& TraceStart);
	void AddRecoil();
	void ApplyRecoil(float DeltaSeconds);
	void KickArms();
	void AttachToHolster();
	void AttachToHand();

	/** Прервать всё, что делало оружие: стрельбу, перезарядку, доставание, их таймеры и звуки. */
	void CancelActions();

	void FinishReload();
	void FinishEquip();

	/** Расставить звуки по таймерам. Звуки с нулевым временем играют сразу. */
	void PlayTimedSounds(const TArray<FNoFearTimedSound>& Sounds);
	void PlayHandlingSound(FNoFearTimedSound Sound);
	void ClearTimedSounds();

	class UAnimInstance* GetCharacterAnimInstance() const;
	float PlayCharacterMontage(class UAnimMontage* Montage) const;
	void StopCharacterMontage(class UAnimMontage* Montage) const;

	bool IsPlayerControlled() const;
	class APlayerController* GetOwnerPlayerController() const;

	ENoFearWeaponState State = ENoFearWeaponState::Holstered;
	int32 MagazineAmmo = 0;
	int32 ReserveAmmo = 0;
	bool bAiming = false;

	// Запрос на огонь
	bool bTriggerHeld = false;
	bool bSingleShotQueued = false;
	int32 BurstShotsRemaining = 0;
	double NextFireTime = 0.0;
	double LastFireTime = -100.0;
	FTimerHandle RefireTimer;
	FTimerHandle ReloadTimer;
	FTimerHandle EquipTimer;
	FTimerHandle HolsterTimer;
	/** Переносит ствол между кобурой и рукой посреди доставания/убирания. */
	FTimerHandle SocketSwapTimer;
	TArray<FTimerHandle> SoundTimers;

	// Разброс и отдача
	float Bloom = 0.f;
	FRotator PendingRecoil = FRotator::ZeroRotator;
	FRotator RecoilToRecover = FRotator::ZeroRotator;

	// Отдача рук: считается по времени с последнего выстрела, тик не нужен.
	double ArmKickTime = -100.0;
	float ArmKickScale = 0.f;
	/** Сила в момент выстрела: от неё плавно переходим к новому пику, без рывка к нулю. */
	float ArmKickFromAlpha = 0.f;
};
