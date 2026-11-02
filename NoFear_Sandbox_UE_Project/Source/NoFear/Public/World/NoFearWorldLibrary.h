#pragma once

#include "CoreMinimal.h"

class AAIController;
class AActor;
class APawn;
class UMaterialInterface;
class UNoFearGrenadeData;
class UNoFearItemData;
class USoundBase;
class UStaticMesh;
class UStaticMeshComponent;

/** Общие помощники интерактивных объектов мира (Source/NoFear/World): двери, баррикады, ловушки, бочки. */
namespace NoFearWorld
{
	/** Монстр (обычный или взбешённый). */
	NOFEAR_API bool IsMonsterTeam(const AActor* Actor);

	/** Отрезок Start–End задевает коробку Box в осях BoxTransform. */
	NOFEAR_API bool SegmentCrossesBox(const FTransform& BoxTransform, const FBox& Box, const FVector& Start, const FVector& End);

	/** Ближайшие MaxLength см пути бота проходят через коробку. */
	NOFEAR_API bool IsPathThroughBox(const AAIController* AI, const APawn* Pawn, const FTransform& BoxTransform, const FBox& Box, float MaxLength);

	/**
	 * Цель бота (фокус) по другую сторону плоскости X=0 в осях PlaneTransform, и линия к ней проходит через проём
	 * HalfWidth × HalfHeight (с запасом Slack).
	 */
	NOFEAR_API bool IsFocusBehindPlane(const AAIController* AI, const APawn* Pawn, const FTransform& PlaneTransform, float HalfWidth, float HalfHeight, float Slack = 150.f, float MaxDistance = 3000.f);

	/** Звук в точке и шум для слуха ботов (ReportNoiseEvent). */
	NOFEAR_API void PlaySoundAndNoise(AActor* Source, USoundBase* Sound, const FVector& Location, float Loudness, float Range, AActor* NoiseInstigator, FName Tag, float VolumeMultiplier = 1.f);

	/**
	 * Приманка: монстры в Range (с их остротой слуха) идут проверить Location и осматриваются CheckDuration секунд.
	 * Нужна для шума без героя-инстигатора (радио, сирена, генератор): нейтральный предмет слух ботов не замечает.
	 */
	NOFEAR_API void LureMonsters(const AActor* Source, const FVector& Location, float Range, float CheckDuration = 6.f);

	/** Покрасить заглушку (BasicShapeMaterial, параметр Color). */
	NOFEAR_API void TintPlaceholder(UStaticMeshComponent* Mesh, UMaterialInterface* PlaceholderMaterial, const FLinearColor& Color);

	/** Растянуть меш компонента так, чтобы его границы заняли Size (см) с центром в Center (в осях родителя). */
	NOFEAR_API void FitMesh(UStaticMeshComponent* Mesh, UStaticMesh* StaticMesh, const FVector& Size, const FVector& Center);

	/** Разлёт кубиков-обломков с физикой, как у сломанной двери. */
	NOFEAR_API void SpawnDebris(AActor* Source, const FTransform& Transform, const FVector& AreaSize, const FVector& PieceSize, int32 Count, const FVector& Push, float Lifetime, UMaterialInterface* PlaceholderMaterial, const FLinearColor& Color);

	/** Взрыв как от гранаты Data (урон, подавление, шум, вспышка) в точке Location от имени Instigator. */
	NOFEAR_API void Explode(AActor* Source, UNoFearGrenadeData* Data, const FVector& Location, APawn* Instigator);

	/** Ассет гранаты по умолчанию (/Game/Weapons/Data/DA_Grenade). */
	NOFEAR_API UNoFearGrenadeData* GetDefaultGrenadeData();

	/** Найти предмет по мягкому пути, загрузив при необходимости. */
	NOFEAR_API UNoFearItemData* LoadItem(const TSoftObjectPtr<UNoFearItemData>& Item);

	/** Ключ Interact (E) зажат у игрока, управляющего пешкой. false — бот, нет игрока или ни одна клавиша не нажата. */
	NOFEAR_API bool IsInteractHeld(const APawn* User);

	/** Ключ KeyId в кейсе User. Пустой KeyId или нет кейса — nullptr. */
	NOFEAR_API UNoFearItemData* FindKey(const APawn* User, FName KeyId);

	/** «Отпереть: <имя ключа>». */
	NOFEAR_API FText UnlockPrompt(const UNoFearItemData* Key);

	/** Сообщение внизу экрана игрока (как «В кейсе нет места»). */
	NOFEAR_API void Notify(const APawn* User, const FText& Text);
}
