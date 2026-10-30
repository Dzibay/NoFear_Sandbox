#pragma once

#include "CoreMinimal.h"
#include "Chaos/ChaosEngineInterface.h"
#include "Engine/DataAsset.h"
#include "Engine/EngineTypes.h"
#include "NoFearWeaponData.generated.h"

class UAnimMontage;
class UAnimSequenceBase;
class UCameraShakeBase;
class UCurveFloat;
class UDamageType;
class UFXSystemAsset;
class UMaterialInterface;
class USkeletalMesh;
class USoundAttenuation;
class USoundBase;
class UTexture2D;

/** Тип оружия. По нему персонаж выбирает позу (Overlay State). */
UENUM(BlueprintType)
enum class ENoFearWeaponType : uint8
{
	Pistol,
	Rifle,
	Shotgun
};

UENUM(BlueprintType)
enum class ENoFearFireMode : uint8
{
	/** Один выстрел на нажатие. */
	Single,
	/** Очередь из BurstCount выстрелов на нажатие. */
	Burst,
	/** Стреляет, пока нажата кнопка. */
	Auto
};

/** Эффект попадания в поверхность одного типа. */
USTRUCT(BlueprintType)
struct FNoFearImpactEffect
{
	GENERATED_BODY()

	/** Niagara или Cascade (P_*). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	TObjectPtr<UFXSystemAsset> Effect = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	TObjectPtr<USoundBase> Sound = nullptr;

	/** Материал декаля (Material Domain = Deferred Decal). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	TObjectPtr<UMaterialInterface> Decal = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FVector DecalSize = FVector(5.f, 8.f, 8.f);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta = (ClampMin = 0))
	float DecalLifeSpan = 20.f;
};

/** Звук, который играется через заданное время после начала действия: перезарядки, доставания или убирания. */
USTRUCT(BlueprintType)
struct FNoFearTimedSound
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	TObjectPtr<USoundBase> Sound = nullptr;

	/** Через сколько секунд после начала действия играть звук. 0 — сразу. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta = (ClampMin = 0))
	float Time = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta = (ClampMin = 0))
	float VolumeMultiplier = 1.f;
};

/**
 * Все настройки одного вида оружия. Создаётся в Content Browser:
 * Miscellaneous → Data Asset → NoFearWeaponData.
 */
UCLASS(BlueprintType)
class NOFEAR_API UNoFearWeaponData : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	// --- Внешний вид ---

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Display")
	FText DisplayName;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Display")
	TObjectPtr<UTexture2D> Icon = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Display")
	ENoFearWeaponType WeaponType = ENoFearWeaponType::Rifle;

	/** Поза персонажа: имя значения Enum_OverlayState (Rifle, Pistol1H, Pistol2H…). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Display")
	FName OverlayState = TEXT("Rifle");

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Mesh")
	TObjectPtr<USkeletalMesh> Mesh = nullptr;

	/** Сокет на скелете персонажа: overlay_rifle или overlay_pistol. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Mesh")
	FName AttachSocket = TEXT("overlay_rifle");

	/** Подгонка положения оружия в руке относительно сокета. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Mesh")
	FTransform AttachOffset;

	/**
	 * Куда вешается оружие, пока не в руках: spine_weapon_first_socket / spine_weapon_second_socket (спина),
	 * pelvis_weapon_first_socket / pelvis_weapon_second_socket (пояс). Пусто — убранное оружие не видно.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Mesh")
	FName HolsterSocket;

	/** Подгонка положения убранного оружия относительно HolsterSocket. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Mesh")
	FTransform HolsterOffset;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Mesh")
	FName MuzzleSocket = TEXT("Muzzle");

	// --- Стрельба ---

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fire")
	ENoFearFireMode FireMode = ENoFearFireMode::Auto;

	/** Темп стрельбы, выстрелов в минуту. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fire", meta = (ClampMin = 1))
	float RoundsPerMinute = 650.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fire", meta = (ClampMin = 2, EditCondition = "FireMode == ENoFearFireMode::Burst"))
	int32 BurstCount = 3;

	/** Дробин за выстрел (дробовик — 8–12). Патрон тратится один. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fire", meta = (ClampMin = 1))
	int32 PelletsPerShot = 1;

	/** Разброс дробин внутри одного выстрела, градусы. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fire", meta = (ClampMin = 0, EditCondition = "PelletsPerShot > 1"))
	float PelletSpreadDegrees = 4.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fire")
	TEnumAsByte<ECollisionChannel> TraceChannel = ECC_Visibility;

	// --- Патроны ---

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ammo", meta = (ClampMin = 1))
	int32 MagazineSize = 30;

	/** Патронов в запасе при выдаче оружия. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ammo", meta = (ClampMin = 0))
	int32 StartingReserveAmmo = 120;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ammo", meta = (ClampMin = 0))
	int32 MaxReserveAmmo = 240;

	/** Время перезарядки. 0 — по длине монтажа CharacterReloadMontage. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ammo", meta = (ClampMin = 0))
	float ReloadTime = 0.f;

	/** Время доставания оружия. 0 — по длине монтажа CharacterEquipMontage. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ammo", meta = (ClampMin = 0))
	float EquipTime = 0.f;

	/** Перезаряжать само, когда магазин пуст и нажат огонь. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ammo")
	bool bAutoReload = true;

	// --- Доставание и убирание ---

	/**
	 * Время убирания оружия. 0 — по длине монтажа убирания. Если монтажа убирания нет, проигрывается
	 * монтаж доставания задом наперёд: он растягивается или сжимается под это время.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Handling", meta = (ClampMin = 0))
	float HolsterTime = 0.f;

	/**
	 * Через сколько секунд после начала доставания ствол перепрыгивает с кобуры в руку (когда рука дотянулась до кобуры).
	 * 0 — сразу в начале.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Handling", meta = (ClampMin = 0))
	float EquipGrabTime = 0.f;

	/**
	 * Через сколько секунд после начала убирания ствол уходит из руки на кобуру.
	 * 0 — в самом конце убирания.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Handling", meta = (ClampMin = 0))
	float HolsterStowTime = 0.f;

	// --- Урон ---

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Damage", meta = (ClampMin = 0))
	float Damage = 25.f;

	/** Дальность выстрела, см. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Damage", meta = (ClampMin = 100))
	float Range = 15000.f;

	/** С какого расстояния урон начинает падать, см. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Damage", meta = (ClampMin = 0))
	float FalloffStart = 3000.f;

	/** Расстояние, на котором урон падает до MinDamageMultiplier, см. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Damage", meta = (ClampMin = 0))
	float FalloffEnd = 8000.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Damage", meta = (ClampMin = 0, ClampMax = 1))
	float MinDamageMultiplier = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Damage")
	TSubclassOf<UDamageType> DamageType;

	// --- Разброс (градусы, половина угла конуса) ---

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spread", meta = (ClampMin = 0))
	float HipSpread = 2.5f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spread", meta = (ClampMin = 0))
	float AimSpread = 0.4f;

	/** Насколько растёт разброс с каждым выстрелом. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spread", meta = (ClampMin = 0))
	float SpreadPerShot = 0.35f;

	/** Максимум прироста разброса от стрельбы. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spread", meta = (ClampMin = 0))
	float MaxBloom = 4.f;

	/** Сколько градусов разброса восстанавливается за секунду. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spread", meta = (ClampMin = 0))
	float BloomRecoveryRate = 8.f;

	/** Прибавка разброса на беге (при 600 см/с). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spread", meta = (ClampMin = 0))
	float MovingSpread = 2.f;

	/** Прибавка разброса в прыжке. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spread", meta = (ClampMin = 0))
	float AirSpread = 5.f;

	// --- Отдача (только у игрока, градусы) ---

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Recoil")
	FVector2D RecoilPitch = FVector2D(0.6f, 0.9f);

	/** Случайный сдвиг вбок, от -X до X. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Recoil", meta = (ClampMin = 0))
	float RecoilYaw = 0.3f;

	/** Множитель отдачи в прицеливании. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Recoil", meta = (ClampMin = 0))
	float AimRecoilMultiplier = 0.6f;

	/** Скорость, с которой отдача поднимает прицел (чем больше — тем резче). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Recoil", meta = (ClampMin = 1))
	float RecoilKickSpeed = 25.f;

	/** Какую долю отдачи прицел возвращает сам после стрельбы. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Recoil", meta = (ClampMin = 0, ClampMax = 1))
	float RecoilRecovery = 0.6f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Recoil", meta = (ClampMin = 0))
	float RecoilRecoverySpeed = 6.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Recoil")
	TSubclassOf<UCameraShakeBase> FireCameraShake;

	// --- Отдача рук (процедурная, у всех; её применяет ABP_SandboxCharacter) ---

	/** Поворот upperarm_r на пике отдачи, градусы (аддитивно, в пространстве родительской кости). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Recoil|Arms")
	FRotator UpperArmRecoil = FRotator::ZeroRotator;

	/** Поворот lowerarm_r на пике отдачи. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Recoil|Arms")
	FRotator LowerArmRecoil = FRotator::ZeroRotator;

	/** Поворот hand_r на пике отдачи. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Recoil|Arms")
	FRotator HandRecoil = FRotator::ZeroRotator;

	/** Сила отдачи рук: 1 — как задано выше. В прицеливании дополнительно умножается на AimRecoilMultiplier. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Recoil|Arms", meta = (ClampMin = 0))
	float ArmRecoilScale = 1.f;

	/**
	 * Форма отдачи рук: время после выстрела (с) → сила 0..1.
	 * Пусто — встроенная, как в старой версии: пик через 0.05 с, к 0.5 с рука возвращается.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Recoil|Arms")
	TObjectPtr<UCurveFloat> ArmRecoilCurve = nullptr;

	// --- Для ИИ ---

	/** Радиус подавления: пуля, пролетевшая ближе, пугает бота. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "AI", meta = (ClampMin = 0))
	float SuppressionRadius = 300.f;

	/** Подавление за одну пулю (у дробовика делится на дробины). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "AI", meta = (ClampMin = 0))
	float SuppressionAmount = 0.25f;

	/** Громкость выстрела для AI Hearing. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "AI", meta = (ClampMin = 0))
	float NoiseLoudness = 1.f;

	/** Дальность слышимости выстрела, см. 0 — по настройке слуха бота. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "AI", meta = (ClampMin = 0))
	float NoiseRange = 5000.f;

	// --- Звук и эффекты ---

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Effects")
	TObjectPtr<USoundBase> FireSound = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Effects")
	TObjectPtr<USoundBase> DryFireSound = nullptr;

	/** Звуки перезарядки (магазин, затвор…). Время отсчитывается от начала перезарядки. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Effects")
	TArray<FNoFearTimedSound> ReloadSounds;

	/** Звуки доставания оружия. Время отсчитывается от начала доставания. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Effects")
	TArray<FNoFearTimedSound> EquipSounds;

	/** Звуки убирания оружия. Время отсчитывается от начала убирания. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Effects")
	TArray<FNoFearTimedSound> HolsterSounds;

	/** Затухание для звуков перезарядки, доставания и убирания. Пусто — как настроено в самом звуке. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Effects")
	TObjectPtr<USoundAttenuation> HandlingAttenuation = nullptr;

	/** Вспышка у дула (Niagara или Cascade). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Effects")
	TObjectPtr<UFXSystemAsset> MuzzleFlash = nullptr;

	/** Трассер (Niagara). Конец луча передаётся в параметр TracerEndParameter. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Effects")
	TObjectPtr<UFXSystemAsset> Tracer = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Effects")
	FName TracerEndParameter = TEXT("BeamEnd");

	/** Эффект попадания по умолчанию. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Effects")
	FNoFearImpactEffect DefaultImpact;

	/** Эффекты по типу поверхности (Project Settings → Physics → Physical Surface). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Effects")
	TMap<TEnumAsByte<EPhysicalSurface>, FNoFearImpactEffect> SurfaceImpacts;

	// --- Анимации ---

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation")
	TObjectPtr<UAnimMontage> CharacterFireMontage = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation")
	TObjectPtr<UAnimMontage> CharacterReloadMontage = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation")
	TObjectPtr<UAnimMontage> CharacterEquipMontage = nullptr;

	/** Монтаж убирания. Пусто — монтаж доставания проигрывается задом наперёд. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation")
	TObjectPtr<UAnimMontage> CharacterHolsterMontage = nullptr;

	/** Анимация самого оружия при выстреле (затвор). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation")
	TObjectPtr<UAnimSequenceBase> WeaponFireAnimation = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation")
	TObjectPtr<UAnimSequenceBase> WeaponReloadAnimation = nullptr;

	float GetFireInterval() const { return 60.f / FMath::Max(RoundsPerMinute, 1.f); }

	const FNoFearImpactEffect& GetImpactEffect(EPhysicalSurface Surface) const
	{
		const FNoFearImpactEffect* Found = SurfaceImpacts.Find(Surface);
		return Found ? *Found : DefaultImpact;
	}
};
