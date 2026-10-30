#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "NoFearCombatSubsystem.generated.h"

class UNoFearCombatComponent;

/**
 * Реестр всех NoFear Combat в мире и точка входа для оружия.
 */
UCLASS()
class NOFEAR_API UNoFearCombatSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	void RegisterCombatant(UNoFearCombatComponent* Combatant);
	void UnregisterCombatant(UNoFearCombatComponent* Combatant);

	/** Все живые NoFear Combat в мире. */
	TArray<UNoFearCombatComponent*> GetCombatants() const;

	/**
	 * Сообщить о выстреле. Все боты рядом с траекторией пули получат подавление, а тот, в кого попали, — ещё и бонус.
	 * Вызывать в BP_Weapon сразу после LineTraceSingle — и для игрока, и для ботов.
	 * Shooter — оружие или его владелец.
	 */
	UFUNCTION(BlueprintCallable, Category = "NoFear|AI", meta = (WorldContext = "WorldContextObject", AdvancedDisplay = "SuppressionRadius,SuppressionAmount,HitBonus"))
	static void ReportShot(const UObject* WorldContextObject, AActor* Shooter, FVector TraceStart, FVector TraceEnd, const FHitResult& Hit,
		float SuppressionRadius = 300.f, float SuppressionAmount = 0.25f, float HitBonus = 0.35f);

	/**
	 * Конечная точка выстрела бота с учётом его точности. Shooter — оружие или бот.
	 * Если у стрелка нет NoFear Combat, возвращает точку на линии MuzzleLocation → FallbackAimPoint.
	 */
	UFUNCTION(BlueprintCallable, Category = "NoFear|AI")
	static FVector ComputeAIShotEnd(AActor* Shooter, FVector MuzzleLocation, float Range, FVector FallbackAimPoint);

	/** Найти NoFear Combat у актора, его владельца, инстигатора, родителя (для оружия) или пешки контроллера. */
	UFUNCTION(BlueprintPure, Category = "NoFear|AI")
	static UNoFearCombatComponent* FindCombatComponent(AActor* Actor);

	/**
	 * Актор погиб: все, кто целился в него или прятался от него, забывают цель. Вызывается из
	 * UNoFearHealthComponent автоматически, вручную нужен только для смерти без компонента здоровья.
	 */
	UFUNCTION(BlueprintCallable, Category = "NoFear|AI")
	static void NotifyActorDied(AActor* DeadActor);

private:
	static AActor* ResolveShooterPawn(AActor* Shooter);

	TArray<TWeakObjectPtr<UNoFearCombatComponent>> Combatants;
};
