#pragma once

#include "CoreMinimal.h"
#include "Core/NoFearHealthComponent.h"
#include "GameFramework/PlayerController.h"
#include "GenericTeamAgentInterface.h"
#include "NoFearPlayerController.generated.h"

/** Игрок попал по кому-то: для маркера попадания на прицеле. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_FourParams(FNoFearHitConfirmedSignature, AActor*, Victim, float, Damage, bool, bHeadshot, bool, bKilled);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FNoFearPlayerDiedSignature, const FNoFearDamageInfo&, Info);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FNoFearPlayerRespawnedSignature);

/**
 * Контроллер игрока. Всегда в команде Player.
 * Ведёт смерть и возрождение: после гибели пешки (ANoFearCharacter вызывает NotifyPawnDied) через RespawnDelay секунд
 * создаётся новая пешка на PlayerStart, а если его на уровне нет — там, где игрок появился в первый раз.
 */
UCLASS()
class NOFEAR_API ANoFearPlayerController : public APlayerController, public IGenericTeamAgentInterface
{
	GENERATED_BODY()

public:
	// IGenericTeamAgentInterface
	virtual FGenericTeamId GetGenericTeamId() const override;

	/** Возрождать игрока автоматически. Выключено — экран смерти висит, пока не вызван RespawnPlayer. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NoFear|Respawn")
	bool bAutoRespawn = true;

	/** Через сколько секунд после смерти игрок возрождается. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NoFear|Respawn", meta = (ClampMin = "0"))
	float RespawnDelay = 5.f;

	/** Игрок попал по цели (после множителей зон, с учётом отключённого огня по своим). */
	UPROPERTY(BlueprintAssignable, Category = "NoFear|Health")
	FNoFearHitConfirmedSignature OnHitConfirmed;

	UPROPERTY(BlueprintAssignable, Category = "NoFear|Respawn")
	FNoFearPlayerDiedSignature OnPlayerDied;

	UPROPERTY(BlueprintAssignable, Category = "NoFear|Respawn")
	FNoFearPlayerRespawnedSignature OnPlayerRespawned;

	/** Игрок мёртв и ждёт возрождения. */
	UFUNCTION(BlueprintPure, Category = "NoFear|Respawn")
	bool IsWaitingForRespawn() const { return bWaitingForRespawn; }

	/** Сколько секунд до автоматического возрождения; 0, если оно не запланировано. */
	UFUNCTION(BlueprintPure, Category = "NoFear|Respawn")
	float GetRespawnTimeRemaining() const;

	/** Возродить сейчас. Живого игрока не трогает. Старое тело остаётся лежать до своего исчезновения. */
	UFUNCTION(BlueprintCallable, Category = "NoFear|Respawn")
	void RespawnPlayer();

	/** Консоль: нанести себе урон, чтобы проверить HUD. Пример: NoFearHurt 30. */
	UFUNCTION(Exec)
	void NoFearHurt(float Amount = 25.f);

	/** Консоль: умереть, чтобы проверить экран смерти и возрождение. */
	UFUNCTION(Exec)
	void NoFearKill();

	/** Вызывается из ANoFearCharacter::HandleDeath, когда пешка этого игрока погибла. */
	void NotifyPawnDied(const FNoFearDamageInfo& Info);

	/** Вызывается из UNoFearHealthComponent, когда выстрел этого игрока нанёс урон. */
	void NotifyHitConfirmed(AActor* Victim, float Damage, bool bHeadshot, bool bKilled);

protected:
	virtual void OnPossess(APawn* InPawn) override;

private:
	FTimerHandle RespawnTimer;
	bool bWaitingForRespawn = false;
	double RespawnTime = 0.0;

	/** Где игрок появился в первый раз: запасная точка возрождения. */
	FTransform RespawnTransform;
	bool bHasRespawnTransform = false;
};
