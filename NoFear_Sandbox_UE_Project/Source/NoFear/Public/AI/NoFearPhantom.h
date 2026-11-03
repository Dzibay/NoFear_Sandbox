#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "NoFearPhantom.generated.h"

class UAnimSequence;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class USkeletalMeshComponent;

/**
 * Видение героя от шептуна (раздел 4.6 Docs/Monsters_Design.md): полупрозрачный загонщик бежит к герою.
 * - Виден только герою: владелец — пешка героя, тело bOnlyOwnerSee. Не пешка — боты его не замечают и не задевают.
 * - Пули проходят насквозь: выстрел героя через него (UNoFearCombatSubsystem::ReportShot → NotifyShot) — рассыпается,
 *   напарник говорит «Там никого нет».
 * - Рассыпается ближе VanishDistance к герою или через LifeTime.
 * - Пугает героя как монстр на виду, но слабее (браслет, PhantomFearScale в настройках страха героя).
 */
UCLASS(Blueprintable)
class NOFEAR_API ANoFearPhantom : public AActor
{
	GENERATED_BODY()

public:
	ANoFearPhantom();

	/** Скорость бега к герою, см/с. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NoFear|Phantom", meta = (ClampMin = "0"))
	float RunSpeed = 550.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NoFear|Phantom", meta = (ClampMin = "0.1"))
	float LifeTime = 8.f;

	/** Ближе этого к герою — рассыпается, см. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NoFear|Phantom", meta = (ClampMin = "0"))
	float VanishDistance = 300.f;

	/** Пуля ближе этого к оси тела попала в видение, см. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NoFear|Phantom", meta = (ClampMin = "0"))
	float HitRadius = 45.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NoFear|Phantom", meta = (ClampMin = "0", ClampMax = "1"))
	float Opacity = 0.35f;

	/** За столько секунд проявляется и рассыпается. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NoFear|Phantom", meta = (ClampMin = "0.05"))
	float FadeTime = 0.35f;

	/** Чьё тело показывать (меш и его посадка в капсуле). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NoFear|Phantom")
	TSoftClassPtr<APawn> BodyClass;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NoFear|Phantom")
	TSoftObjectPtr<UAnimSequence> RunAnimation;

	/** Полупрозрачный материал с параметром Opacity (плащ тени). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NoFear|Phantom")
	TSoftObjectPtr<UMaterialInterface> GhostMaterial;

	/** Запустить видение: стоит в Location (на земле) и бежит к Hero. */
	void Init(APawn* Hero, const FVector& GroundLocation);

	UFUNCTION(BlueprintPure, Category = "NoFear|Phantom")
	APawn* GetHero() const { return Hero.Get(); }

	UFUNCTION(BlueprintPure, Category = "NoFear|Phantom")
	bool IsVanishing() const { return VanishStart >= 0.0; }

	/** Рассыпалось от выстрела. */
	UFUNCTION(BlueprintPure, Category = "NoFear|Phantom")
	bool WasShot() const { return bShot; }

	/** Рассыпаться (bByShot — от выстрела: напарник скажет, что там никого нет). */
	UFUNCTION(BlueprintCallable, Category = "NoFear|Phantom")
	void Vanish(bool bByShot);

	/** Выстрел Shooter от Start до End: видения его героя на линии рассыпаются. */
	static void NotifyShot(const UWorld* World, const AActor* Shooter, const FVector& Start, const FVector& End);

	/** Видения в мире, ещё не рассыпавшиеся (для браслета героя и шептуна). */
	static void GetPhantoms(const UWorld* World, TArray<ANoFearPhantom*>& OutPhantoms);

	/** Сколько видений бежит к герою (для тестов). */
	UFUNCTION(BlueprintPure, Category = "NoFear|Phantom", meta = (WorldContext = "WorldContextObject"))
	static int32 CountPhantoms(const UObject* WorldContextObject);

	virtual void Tick(float DeltaSeconds) override;

protected:
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	void SetOpacity(float Value);

	UPROPERTY(VisibleAnywhere, Category = "NoFear|Phantom")
	TObjectPtr<USceneComponent> Root;

	UPROPERTY(VisibleAnywhere, Category = "NoFear|Phantom")
	TObjectPtr<USkeletalMeshComponent> Body;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UMaterialInstanceDynamic>> Materials;

	TWeakObjectPtr<APawn> Hero;
	float HalfHeight = 90.f;
	double SpawnTime = 0.0;
	double VanishStart = -1.0;
	bool bShot = false;

	static TArray<TWeakObjectPtr<ANoFearPhantom>> Active;
};
