#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "NoFearSpitProjectile.generated.h"

class ANoFearAcidPuddle;
class UDamageType;
class UProjectileMovementComponent;
class USoundBase;
class USphereComponent;
class UStaticMeshComponent;

/**
 * Плевок кислотой (плевун): летит по дуге с гравитацией. Попал в не-союзника — урон DirectDamage; где бы ни упал —
 * оставляет на земле лужу PuddleClass. Своих (команду стрелявшего) не задевает. Instigator — монстр, который плюнул.
 */
UCLASS(Blueprintable)
class NOFEAR_API ANoFearSpitProjectile : public AActor
{
	GENERATED_BODY()

public:
	ANoFearSpitProjectile();

	UFUNCTION(BlueprintPure, Category = "NoFear|Spit")
	UProjectileMovementComponent* GetProjectileMovement() const { return ProjectileMovement; }

	/** Запустить со скоростью Velocity (см/с). */
	UFUNCTION(BlueprintCallable, Category = "NoFear|Spit")
	void Launch(FVector Velocity);

	/** Урон при прямом попадании. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spit", meta = (ClampMin = "0"))
	float DirectDamage = 10.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spit")
	TSubclassOf<UDamageType> DamageType;

	/** Лужа на месте падения. Пусто — без лужи. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spit")
	TSubclassOf<ANoFearAcidPuddle> PuddleClass;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spit")
	FLinearColor Color = FLinearColor(0.25f, 0.85f, 0.05f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spit")
	TObjectPtr<USoundBase> ImpactSound;

protected:
	virtual void BeginPlay() override;

	UFUNCTION()
	void HandleHit(UPrimitiveComponent* HitComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, FVector NormalImpulse, const FHitResult& Hit);

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Spit")
	TObjectPtr<USphereComponent> Collision;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Spit")
	TObjectPtr<UStaticMeshComponent> Mesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Spit")
	TObjectPtr<UProjectileMovementComponent> ProjectileMovement;

private:
	void SpawnPuddle(const FVector& Location);

	bool bExploded = false;
};
