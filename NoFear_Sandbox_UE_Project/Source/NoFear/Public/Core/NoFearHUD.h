#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "NoFearHUD.generated.h"

/**
 * HUD игрока: прицел. Рисуется в центре экрана — туда же летит пуля (см. ANoFearWeapon::ComputeAim).
 * Зазор между чёрточками равен реальному разбросу оружия, поэтому прицел расходится при беге
 * и стрельбе и сходится при прицеливании. Над врагом краснеет.
 * Подключается в ANoFearGameMode, настраивать в редакторе ничего не нужно.
 */
UCLASS(Blueprintable)
class NOFEAR_API ANoFearHUD : public AHUD
{
	GENERATED_BODY()

public:
	virtual void DrawHUD() override;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crosshair")
	bool bShowCrosshair = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crosshair")
	FLinearColor DefaultColor = FLinearColor::White;

	/** Цвет, когда в центре экрана враг. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crosshair")
	FLinearColor EnemyColor = FLinearColor(1.f, 0.1f, 0.05f, 1.f);

	/** Длина чёрточки в пикселях при высоте экрана 1080 (масштабируется под разрешение). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crosshair", meta = (ClampMin = "1"))
	float BarLength = 10.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crosshair", meta = (ClampMin = "1"))
	float BarThickness = 2.f;

	/** Минимальный зазор от центра (пиксели при высоте 1080). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crosshair", meta = (ClampMin = "0"))
	float MinGap = 4.f;

	/** Во сколько раз увеличить зазор относительно настоящего разброса. 1 — как есть. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crosshair", meta = (ClampMin = "0"))
	float SpreadScale = 1.f;

	/** Скорость, с которой зазор подтягивается к текущему разбросу. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crosshair", meta = (ClampMin = "0"))
	float GapInterpSpeed = 18.f;

	/** Прозрачность, пока оружие перезаряжается или достаётся. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crosshair", meta = (ClampMin = "0", ClampMax = "1"))
	float BusyOpacity = 0.35f;

private:
	void DrawCrosshair(class ANoFearWeapon* Weapon);
	bool IsEnemyUnderCrosshair(const ANoFearWeapon* Weapon) const;

	float CurrentGap = -1.f;
};
