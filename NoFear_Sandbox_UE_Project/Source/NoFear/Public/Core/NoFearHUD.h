#pragma once

#include "CoreMinimal.h"
#include "Core/NoFearHealthComponent.h"
#include "GameFramework/HUD.h"
#include "NoFearHUD.generated.h"

class ANoFearPlayerController;
class ANoFearWeapon;
class UFont;

/**
 * HUD игрока. Рисуется на Canvas (UMG не нужен), все размеры заданы для экрана высотой 1080 и масштабируются.
 *
 * - Прицел в центре экрана — туда же летит пуля (см. ANoFearWeapon::ComputeAim). Зазор между чёрточками равен
 *   реальному разбросу оружия, поэтому прицел расходится при беге и стрельбе и сходится при прицеливании. Над врагом краснеет.
 * - Маркер попадания вокруг прицела: белый — обычное, жёлтый — в голову, красный и крупный — убийство.
 * - Здоровье внизу слева: полоска с «хвостом» последнего урона, число.
 * - Патроны внизу справа: имя оружия, магазин / запас, полоска перезарядки, подсказки.
 * - Слоты оружия внизу по центру.
 * - Указатели урона: красные клинья по кругу в сторону стрелявшего. Красная виньетка при ударе и при низком здоровье.
 * - Экран смерти: затемнение, надпись и отсчёт до возрождения.
 *
 * Подключается в ANoFearGameMode, настраивать в редакторе ничего не нужно. Состояние обновляется в Tick, а не при
 * отрисовке, поэтому его можно читать из Blueprint и тестов (Get...).
 */
UCLASS(Blueprintable)
class NOFEAR_API ANoFearHUD : public AHUD
{
	GENERATED_BODY()

public:
	ANoFearHUD();

	virtual void Tick(float DeltaSeconds) override;
	virtual void DrawHUD() override;

	// --- Состояние (для Blueprint и тестов) ---

	/** Игрок мёртв: пешка погибла или контроллер ждёт возрождения. */
	UFUNCTION(BlueprintPure, Category = "NoFear|HUD")
	bool IsOwnerDead() const;

	/** Здоровье, как его показывает полоска: 0..1. */
	UFUNCTION(BlueprintPure, Category = "NoFear|HUD")
	float GetDisplayedHealthFraction() const { return DisplayedHealth; }

	/** «Хвост» полоски здоровья: держится на старом значении после удара, потом догоняет. */
	UFUNCTION(BlueprintPure, Category = "NoFear|HUD")
	float GetGhostHealthFraction() const { return GhostHealth; }

	/** Прозрачность маркера попадания: 0 — не виден, 1 — только что попали. */
	UFUNCTION(BlueprintPure, Category = "NoFear|HUD")
	float GetHitMarkerAlpha() const;

	UFUNCTION(BlueprintPure, Category = "NoFear|HUD")
	int32 GetDamageIndicatorCount() const { return DamageIndicators.Num(); }

	/** Яркость красной вспышки после удара: 0..1. */
	UFUNCTION(BlueprintPure, Category = "NoFear|HUD")
	float GetDamageFlash() const { return DamageFlash; }

	/** Затемнение экрана смерти: 0..1. */
	UFUNCTION(BlueprintPure, Category = "NoFear|HUD")
	float GetDeathScreenAlpha() const;

	// --- Что показывать ---

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NoFear|HUD")
	bool bShowHealth = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NoFear|HUD")
	bool bShowAmmo = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NoFear|HUD")
	bool bShowWeaponSlots = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NoFear|HUD")
	bool bShowHitMarker = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NoFear|HUD")
	bool bShowDamageIndicators = true;

	/** Красная виньетка по краям экрана при ударе и при низком здоровье. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NoFear|HUD")
	bool bShowVignette = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NoFear|HUD")
	bool bShowDeathScreen = true;

	/** Шрифт. Не задан — стандартный шрифт движка. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NoFear|HUD")
	TObjectPtr<UFont> HudFont;

	/** Отступ от краёв экрана (пиксели при высоте 1080). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NoFear|HUD", meta = (ClampMin = "0"))
	float Margin = 48.f;

	// --- Прицел ---

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

	// --- Маркер попадания ---

	/** Сколько секунд виден маркер обычного попадания (и в голову). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hit Marker", meta = (ClampMin = "0.05"))
	float HitMarkerDuration = 0.25f;

	/** Сколько секунд виден маркер убийства. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hit Marker", meta = (ClampMin = "0.05"))
	float KillMarkerDuration = 0.6f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hit Marker")
	FLinearColor HitMarkerColor = FLinearColor::White;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hit Marker")
	FLinearColor HeadshotMarkerColor = FLinearColor(1.f, 0.82f, 0.15f, 1.f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hit Marker")
	FLinearColor KillMarkerColor = FLinearColor(1.f, 0.12f, 0.08f, 1.f);

	// --- Здоровье и урон ---

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Health")
	FLinearColor HealthColor = FLinearColor(0.35f, 0.85f, 0.4f, 1.f);

	/** Цвет полоски почти пустого здоровья; между ним и HealthColor цвет плавно меняется. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Health")
	FLinearColor LowHealthColor = FLinearColor(0.95f, 0.18f, 0.12f, 1.f);

	/** Ниже этой доли здоровья число и полоска пульсируют, а по краям экрана краснеет виньетка. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Health", meta = (ClampMin = "0", ClampMax = "1"))
	float LowHealthFraction = 0.3f;

	/** Через сколько секунд после удара «хвост» полоски начинает догонять. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Health", meta = (ClampMin = "0"))
	float GhostHoldTime = 0.5f;

	/** Сколько секунд виден указатель направления на стрелявшего. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Health", meta = (ClampMin = "0.1"))
	float DamageIndicatorDuration = 2.f;

	/** Радиус круга указателей: доля высоты экрана. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Health", meta = (ClampMin = "0.05", ClampMax = "0.5"))
	float DamageIndicatorRadius = 0.3f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Health")
	FLinearColor DamageColor = FLinearColor(0.85f, 0.04f, 0.03f, 1.f);

	// --- Смерть ---

	/** За сколько секунд экран темнеет после смерти. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Death", meta = (ClampMin = "0.1"))
	float DeathScreenFadeTime = 1.5f;

	// --- Тексты ---

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Text")
	FText DeadText = NSLOCTEXT("NoFearHUD", "Dead", "ВЫ ПОГИБЛИ");

	/** {0} — секунды до возрождения. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Text")
	FText RespawnInText = NSLOCTEXT("NoFearHUD", "RespawnIn", "Возрождение через {0} с");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Text")
	FText ReloadingText = NSLOCTEXT("NoFearHUD", "Reloading", "ПЕРЕЗАРЯДКА");

	/** Магазин пуст, а запас есть. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Text")
	FText ReloadHintText = NSLOCTEXT("NoFearHUD", "ReloadHint", "ПЕРЕЗАРЯДИТЕ");

	/** Нет патронов вообще. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Text")
	FText NoAmmoText = NSLOCTEXT("NoFearHUD", "NoAmmo", "НЕТ ПАТРОНОВ");

private:
	/** Один указатель урона: откуда прилетело и сколько он уже виден. */
	struct FDamageIndicator
	{
		FVector SourceLocation = FVector::ZeroVector;
		float Age = 0.f;
	};

	enum class ELabelAlign : uint8
	{
		Left,
		Center,
		Right
	};

	// Подписки и состояние
	void EnsureBindings();
	UFUNCTION()
	void HandleHitConfirmed(AActor* Victim, float Damage, bool bHeadshot, bool bKilled);
	UFUNCTION()
	void HandleDamaged(UNoFearHealthComponent* Component, const FNoFearDamageInfo& Info);
	UFUNCTION()
	void HandlePlayerDied(const FNoFearDamageInfo& Info);

	// Отрисовка
	void DrawCrosshair(ANoFearWeapon* Weapon);
	bool IsEnemyUnderCrosshair(const ANoFearWeapon* Weapon) const;
	void DrawHitMarker();
	void DrawHealth();
	void DrawAmmo(ANoFearWeapon* Weapon);
	void DrawWeaponSlots();
	void DrawDamageIndicators();
	void DrawVignette();
	void DrawDeathScreen();

	void DrawLabel(const FString& Text, float X, float Y, const FLinearColor& Color, float Scale, ELabelAlign Align);
	void DrawBar(float X, float Y, float Width, float Height, float Fraction, const FLinearColor& Fill, float GhostFraction = -1.f);
	/** Четырёхугольник из двух треугольников; цвета вершин по кругу A, B, C, D. */
	void DrawColoredQuad(const FVector2D& A, const FVector2D& B, const FVector2D& C, const FVector2D& D, const FLinearColor& ColorA, const FLinearColor& ColorB, const FLinearColor& ColorC, const FLinearColor& ColorD);
	UFont* GetFont() const;
	float GetUIScale() const;
	float GetLowHealthAlpha() const;

	TWeakObjectPtr<ANoFearPlayerController> BoundController;
	TWeakObjectPtr<UNoFearHealthComponent> BoundHealth;

	float CurrentGap = -1.f;

	float DisplayedHealth = 1.f;
	float GhostHealth = 1.f;
	double LastDamageTime = -1.0e9;
	float DamageFlash = 0.f;

	TArray<FDamageIndicator> DamageIndicators;

	float HitMarkerAge = 1.0e9f;
	bool bHitHeadshot = false;
	bool bHitKill = false;

	double DeathTime = -1.0e9;
	bool bWasDead = false;
};
