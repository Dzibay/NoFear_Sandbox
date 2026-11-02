#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Portal/NoFearPortalTypes.h"
#include "NoFearPortal.generated.h"

class UMaterialInstanceDynamic;
class UMaterialInterface;
class UPointLightComponent;
class UStaticMesh;
class UStaticMeshComponent;

/**
 * Портал страха (этап 13). Состояние ведёт UNoFearPortalSubsystem: размер, проводники, монстры, закрытие.
 * Актор только показывает его: тёмный провал растёт с размером и пульсирует, под ним светится трещина,
 * от провала к каждому проводнику тянется луч. Без проводников провал сжимается; закрываясь, схлопывается.
 */
UCLASS(Blueprintable)
class NOFEAR_API ANoFearPortal : public AActor
{
	GENERATED_BODY()

public:
	ANoFearPortal();

	virtual void Tick(float DeltaSeconds) override;

	/** Размер 0–100: от него ступень, аура и поводок монстров. */
	UFUNCTION(BlueprintPure, Category = "NoFear|Portal")
	float GetSize() const { return Size; }

	UFUNCTION(BlueprintPure, Category = "NoFear|Portal")
	ENoFearPortalStage GetStage() const { return Stage; }

	/** Затягивается: проводников нет, монстры бегут обратно. */
	UFUNCTION(BlueprintPure, Category = "NoFear|Portal")
	bool IsClosing() const { return bClosing; }

	UFUNCTION(BlueprintPure, Category = "NoFear|Portal")
	int32 GetFeederCount() const;

	/** Живые монстры, вышедшие из этого портала. */
	UFUNCTION(BlueprintPure, Category = "NoFear|Portal")
	int32 GetMonsterCount() const;

	UFUNCTION(BlueprintPure, Category = "NoFear|Portal")
	TArray<APawn*> GetFeeders() const;

	UFUNCTION(BlueprintPure, Category = "NoFear|Portal")
	TArray<APawn*> GetMonsters() const;

	/** Центр провала (над землёй). Актор стоит на земле под ним. */
	UFUNCTION(BlueprintPure, Category = "NoFear|Portal")
	FVector GetCoreLocation() const;

	/** Радиус провала при текущем размере, см. */
	UFUNCTION(BlueprintPure, Category = "NoFear|Portal")
	float GetCoreRadius() const;

	/**
	 * Шептун у портала (Docs/Monsters_Design.md, 4.6): следующие Duration секунд рост от проводников ×GrowthScale,
	 * освобождение проводников ×FreeScale. Шептун обновляет буст каждый пересчёт, пока стоит рядом.
	 */
	UFUNCTION(BlueprintCallable, Category = "NoFear|Portal")
	void ApplyWhisperBoost(float GrowthScale, float FreeScale, float Duration);

	UFUNCTION(BlueprintPure, Category = "NoFear|Portal")
	bool IsWhisperBoosted() const;

	/** Множитель роста от проводников сейчас (1 — без буста). */
	UFUNCTION(BlueprintPure, Category = "NoFear|Portal")
	float GetGrowthScale() const { return IsWhisperBoosted() ? WhisperGrowthScale : 1.f; }

	/** Множитель освобождения проводников сейчас (1 — без буста). */
	UFUNCTION(BlueprintPure, Category = "NoFear|Portal")
	float GetFreeScale() const { return IsWhisperBoosted() ? WhisperFreeScale : 1.f; }

	/** Цвет провала. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NoFear|Portal")
	FLinearColor CoreColor = FLinearColor(0.02f, 0.f, 0.04f, 1.f);

	/** Цвет трещины, лучей и света. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NoFear|Portal")
	FLinearColor GlowColor = FLinearColor(0.75f, 0.4f, 1.f, 1.f);

	/** Радиус провала: CoreRadius + CoreRadiusPerSize × размер, см. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NoFear|Portal", meta = (ClampMin = "1"))
	float CoreRadius = 20.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NoFear|Portal", meta = (ClampMin = "0"))
	float CoreRadiusPerSize = 1.1f;

	/** Нижний край провала над землёй, см. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NoFear|Portal", meta = (ClampMin = "0"))
	float CoreLift = 30.f;

	/** Толщина луча к проводнику, см. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NoFear|Portal", meta = (ClampMin = "0.1"))
	float BeamRadius = 2.5f;

	/** Яркость света: LightIntensity + LightIntensityPerSize × размер. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NoFear|Portal", meta = (ClampMin = "0"))
	float LightIntensity = 1500.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NoFear|Portal", meta = (ClampMin = "0"))
	float LightIntensityPerSize = 60.f;

protected:
	friend class UNoFearPortalSubsystem;

	virtual void BeginPlay() override;

	UPROPERTY(VisibleAnywhere, Category = "NoFear|Portal")
	TObjectPtr<USceneComponent> Root;

	UPROPERTY(VisibleAnywhere, Category = "NoFear|Portal")
	TObjectPtr<UStaticMeshComponent> Core;

	UPROPERTY(VisibleAnywhere, Category = "NoFear|Portal")
	TObjectPtr<UStaticMeshComponent> Crack;

	UPROPERTY(VisibleAnywhere, Category = "NoFear|Portal")
	TObjectPtr<UPointLightComponent> Light;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UStaticMeshComponent>> Beams;

	UPROPERTY(Transient)
	TObjectPtr<UStaticMesh> CylinderMesh;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> ShapeMaterial;

	// --- Состояние (ведёт UNoFearPortalSubsystem) ---

	float Size = 0.f;
	ENoFearPortalStage Stage = ENoFearPortalStage::Rift;
	bool bClosing = false;
	double OpenTime = 0.0;
	double CloseTime = 0.0;
	/** Когда выпустить следующую волну (0 — ещё не назначено). */
	double NextSpawnTime = 0.0;
	TArray<TWeakObjectPtr<APawn>> Feeders;
	TArray<TWeakObjectPtr<APawn>> Monsters;

private:
	void UpdateBeams(float Pulse);
	void SetMeshColor(UStaticMeshComponent* Mesh, const FLinearColor& Color) const;

	/** Размер на экране догоняет настоящий плавно. */
	float DisplaySize = 0.f;

	double WhisperBoostUntil = -1.0;
	float WhisperGrowthScale = 1.f;
	float WhisperFreeScale = 1.f;
};
