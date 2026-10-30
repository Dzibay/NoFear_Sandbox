#pragma once

#include "CoreMinimal.h"
#include "Core/NoFearHealthComponent.h"
#include "GameFramework/PlayerController.h"
#include "GenericTeamAgentInterface.h"
#include "NoFearPlayerController.generated.h"

class UInputAction;
class UNoFearInteractableComponent;

/** Игрок попал по кому-то: для маркера попадания на прицеле. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_FourParams(FNoFearHitConfirmedSignature, AActor*, Victim, float, Damage, bool, bHeadshot, bool, bKilled);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FNoFearPlayerDiedSignature, const FNoFearDamageInfo&, Info);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FNoFearPlayerRespawnedSignature);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FNoFearCompanionsChangedSignature);

/**
 * Контроллер игрока. Всегда в команде Player.
 * Ведёт смерть и возрождение: после гибели пешки (ANoFearCharacter вызывает NotifyPawnDied) через RespawnDelay секунд
 * создаётся новая пешка на PlayerStart, а если его на уровне нет — там, где игрок появился в первый раз.
 * Держит список напарников (не больше MaxCompanions) и взаимодействие клавишей E (IA_Interact):
 * каждый кадр выбирает ближайший к центру экрана UNoFearInteractableComponent в пределах его дистанции.
 */
UCLASS()
class NOFEAR_API ANoFearPlayerController : public APlayerController, public IGenericTeamAgentInterface
{
	GENERATED_BODY()

public:
	ANoFearPlayerController();

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

	/**
	 * Консоль: поставить ботов перед игроком. Тип — суффикс Blueprint из /Game/Blueprints/Bots/CBP_Bot_<Type>:
	 * Civilian, Monster, AggroMonster, Enemy, Ally, Companion. Пример: NoFearSpawnBot Enemy 3.
	 */
	UFUNCTION(Exec)
	void NoFearSpawnBot(const FString& Type, int32 Count = 1);

	/** Вызывается из ANoFearCharacter::HandleDeath, когда пешка этого игрока погибла. */
	void NotifyPawnDied(const FNoFearDamageInfo& Info);

	/** Вызывается из UNoFearHealthComponent, когда выстрел этого игрока нанёс урон. */
	void NotifyHitConfirmed(AActor* Victim, float Damage, bool bHeadshot, bool bKilled);

	// --- Напарники ---

	/** Сколько напарников может быть одновременно. Новый сверх лимита заменяет самого старого, тот остаётся союзником. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NoFear|Companions", meta = (ClampMin = "1"))
	int32 MaxCompanions = 1;

	/** Состав напарников изменился: завербован, отпущен, погиб или сменил «жди/за мной». */
	UPROPERTY(BlueprintAssignable, Category = "NoFear|Companions")
	FNoFearCompanionsChangedSignature OnCompanionsChanged;

	/** Завербовать бота с ANoFearCompanionAIController. Уже свой — просто позвать за собой. */
	UFUNCTION(BlueprintCallable, Category = "NoFear|Companions")
	bool RecruitCompanion(APawn* CompanionPawn);

	/** Отпустить: напарник остаётся союзником там, где стоит. */
	UFUNCTION(BlueprintCallable, Category = "NoFear|Companions")
	void DismissCompanion(APawn* CompanionPawn);

	/** true — «За мной», false — «Жди здесь». */
	UFUNCTION(BlueprintCallable, Category = "NoFear|Companions")
	void SetCompanionFollowing(APawn* CompanionPawn, bool bFollow);

	UFUNCTION(BlueprintCallable, Category = "NoFear|Companions")
	void ToggleCompanionFollowing(APawn* CompanionPawn);

	/** Живые напарники, от самого давнего к новому. */
	UFUNCTION(BlueprintPure, Category = "NoFear|Companions")
	TArray<APawn*> GetCompanions() const;

	UFUNCTION(BlueprintPure, Category = "NoFear|Companions")
	bool IsCompanion(const APawn* CompanionPawn) const;

	// --- Взаимодействие ---

	/** Действие «взаимодействовать». По умолчанию IA_Interact из GASP (E / верхняя кнопка геймпада в IMC_Sandbox). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NoFear|Interaction")
	TObjectPtr<UInputAction> InteractAction;

	/** Насколько объект может быть в стороне от направления камеры, чтобы попасть в фокус (градусы). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NoFear|Interaction", meta = (ClampMin = "0", ClampMax = "180"))
	float InteractionMaxAngle = 40.f;

	/** Ближе этого объект попадает в фокус под любым углом, лишь бы был перед камерой. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NoFear|Interaction", meta = (ClampMin = "0"))
	float InteractionCloseDistance = 130.f;

	/** С чем сейчас сработает E; для подсказки на HUD. */
	UFUNCTION(BlueprintPure, Category = "NoFear|Interaction")
	UNoFearInteractableComponent* GetFocusedInteractable() const;

	/** Нажать E: взаимодействовать с объектом в фокусе. */
	UFUNCTION(BlueprintCallable, Category = "NoFear|Interaction")
	bool InteractWithFocused();

protected:
	virtual void OnPossess(APawn* InPawn) override;
	virtual void SetupInputComponent() override;
	virtual void PlayerTick(float DeltaTime) override;

	void OnInteractInput();
	void UpdateFocusedInteractable();

	/** Убрать погибших и чужих; true — список изменился. */
	bool PruneCompanions();

	/** Убрать из списка без оповещения и отпустить. */
	void ReleaseCompanion(APawn* CompanionPawn);

private:
	TArray<TWeakObjectPtr<APawn>> Companions;
	TWeakObjectPtr<UNoFearInteractableComponent> FocusedInteractable;

	FTimerHandle RespawnTimer;
	bool bWaitingForRespawn = false;
	double RespawnTime = 0.0;

	/** Где игрок появился в первый раз: запасная точка возрождения. */
	FTransform RespawnTransform;
	bool bHasRespawnTransform = false;
};
