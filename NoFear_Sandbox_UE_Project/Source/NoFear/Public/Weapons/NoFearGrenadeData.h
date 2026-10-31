#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "NoFearGrenadeData.generated.h"

class UDamageType;
class UFXSystemAsset;
class USoundBase;

/**
 * Настройки гранаты: запал, урон, разлёт, шум. Один ассет на вид гранаты (DA_Grenade — осколочная).
 * Читают граната (ANoFearGrenade), компонент броска (UNoFearGrenadeComponent) и HUD (дуга, значок опасности).
 */
UCLASS(BlueprintType)
class NOFEAR_API UNoFearGrenadeData : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grenade")
	FText DisplayName = NSLOCTEXT("NoFear", "GrenadeName", "Граната");

	/** Запал: через столько секунд после броска взрывается, где бы ни была. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grenade", meta = (ClampMin = "0.1"))
	float FuseTime = 3.f;

	// --- Урон ---

	/** Радиус поражения, см. Дальше урона нет. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grenade|Damage", meta = (ClampMin = "0"))
	float Radius = 500.f;

	/** Ближе этого — полный урон, см. Дальше урон падает линейно до MinDamage на краю Radius. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grenade|Damage", meta = (ClampMin = "0"))
	float InnerRadius = 150.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grenade|Damage", meta = (ClampMin = "0"))
	float MaxDamage = 150.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grenade|Damage", meta = (ClampMin = "0"))
	float MinDamage = 10.f;

	/** Ранит ли своих (команду бросившего). Самого бросившего ранит всегда. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grenade|Damage")
	bool bFriendlyFire = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grenade|Damage")
	TSubclassOf<UDamageType> DamageType;

	/** Толчок рэгдоллам и физическим предметам в радиусе (изменение скорости на расстоянии 0, см/с). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grenade|Damage", meta = (ClampMin = "0"))
	float ImpulseStrength = 900.f;

	// --- Подавление и шум ---

	/** Боты в этом радиусе (и на прямой видимости) получают подавление, см. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grenade|Suppression", meta = (ClampMin = "0"))
	float SuppressionRadius = 900.f;

	/** Сколько подавления даёт взрыв вплотную (дальше — меньше, до половины на краю). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grenade|Suppression", meta = (ClampMin = "0", ClampMax = "1"))
	float SuppressionAmount = 0.9f;

	/** Слышимость взрыва, см: враги бросившего настораживаются, все боты узнают об опасности. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grenade|Suppression", meta = (ClampMin = "0"))
	float NoiseRange = 4000.f;

	// --- Опасность ---

	/** Граната ближе этого к боту — он её замечает (если видит её или слышит, как упала), см. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grenade|Danger", meta = (ClampMin = "0"))
	float DangerNoticeRadius = 900.f;

	/** Ближе этого стук упавшей гранаты слышно и за спиной, и за углом, см. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grenade|Danger", meta = (ClampMin = "0"))
	float DangerHearRadius = 450.f;

	/** На сколько дальше Radius убегают от гранаты, см. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grenade|Danger", meta = (ClampMin = "0"))
	float EvadeMargin = 200.f;

	/** HUD показывает значок опасности, если граната ближе этого к игроку, см. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grenade|Danger", meta = (ClampMin = "0"))
	float HUDWarningRadius = 1000.f;

	// --- Полёт ---

	/** Скорость броска, см/с. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grenade|Flight", meta = (ClampMin = "0"))
	float ThrowSpeed = 1500.f;

	/** Бросок идёт выше линии взгляда на столько градусов. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grenade|Flight", meta = (ClampMin = "-45", ClampMax = "45"))
	float ThrowPitchOffset = 10.f;

	/** Доля скорости, сохраняемая при отскоке (по нормали). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grenade|Flight", meta = (ClampMin = "0", ClampMax = "1"))
	float Bounciness = 0.35f;

	/** Трение при отскоке и качении (0 — скользит, 1 — сразу останавливается). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grenade|Flight", meta = (ClampMin = "0", ClampMax = "1"))
	float Friction = 0.4f;

	// --- Вид и звук ---

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grenade|FX")
	FLinearColor Color = FLinearColor(0.08f, 0.2f, 0.05f);

	/** Эффект взрыва (Cascade или Niagara). Пусто — только вспышка света. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grenade|FX")
	TObjectPtr<UFXSystemAsset> ExplosionEffect;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grenade|FX")
	TObjectPtr<USoundBase> ExplosionSound;

	/** Высота звука взрыва (временный звук — выстрел, сыгранный ниже). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grenade|FX", meta = (ClampMin = "0.1"))
	float ExplosionSoundPitch = 0.45f;

	/** Стук при отскоке. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grenade|FX")
	TObjectPtr<USoundBase> BounceSound;

	/** Урон с учётом расстояния от центра взрыва: MaxDamage внутри InnerRadius, линейно до MinDamage на краю, 0 за Radius. */
	UFUNCTION(BlueprintPure, Category = "Grenade")
	float GetDamageAtDistance(float Distance) const;
};
