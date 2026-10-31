#pragma once

#include "CoreMinimal.h"
#include "Core/NoFearTeam.h"
#include "Subsystems/WorldSubsystem.h"
#include "NoFearSquadSubsystem.generated.h"

class ANoFearShooterAIController;

/** Боец отряда глазами интерфейса: роль, командир и когда что менялось (для колец над головой). */
struct FNoFearSquadMemberView
{
	ENoFearSquadRole Role = ENoFearSquadRole::Rifleman;
	/** Роль до последней смены (кольцо перекрашивается из неё). */
	ENoFearSquadRole PreviousRole = ENoFearSquadRole::Rifleman;
	/** Когда боец встал в строй: при сборе отряда или когда догнал уже собранный. */
	double JoinTime = -1.0e9;
	double RoleChangeTime = -1.0e9;
	/** Когда отряд сменил командира (погиб прежний). */
	double CommanderChangeTime = -1.0e9;
	const APawn* Commander = nullptr;
	int32 SquadSize = 0;
	bool bExecutingOrder = false;
};

/**
 * Отряды стрелков. Стрелки одной команды с одинаковым ANoFearCharacter::Squad — один отряд (до MaxSquadSize бойцов).
 * Состав пересобирается каждые UpdateInterval секунд из живых стрелков, поэтому отряд не надо регистрировать:
 * погибший выбывает сам, поменявший имя отряда — переходит.
 *
 * Отряд:
 *  - назначает роли: роль с тела (пусто — стрелок); командир — заданный на теле, иначе первый стрелок
 *    (потом поддержка, потом штурмовик). Погиб командир — роль переходит к следующему;
 *  - собирает, кого видят его бойцы, и сообщает остальным (ReceiveAlarm) — где бы они ни стояли;
 *  - считает, сколько бойцов уже бьют каждую цель (ANoFearShooterAIController::ScoreTarget делит цели).
 */
UCLASS()
class NOFEAR_API UNoFearSquadSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	/** Подсистема отрядов мира WorldContextObject. */
	UFUNCTION(BlueprintPure, Category = "NoFear|Squad", meta = (WorldContext = "WorldContextObject", DisplayName = "Get Squad Subsystem"))
	static UNoFearSquadSubsystem* Get(const UObject* WorldContextObject);

	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;

	/** Пересобрать отряды сейчас (раз в UpdateInterval это происходит само). */
	UFUNCTION(BlueprintCallable, Category = "NoFear|Squad")
	void UpdateSquads();

	/** Роль бойца в отряде. None — не в отряде. */
	UFUNCTION(BlueprintPure, Category = "NoFear|Squad")
	ENoFearSquadRole GetSquadRole(const APawn* Pawn) const;

	/** Командир отряда Pawn (может быть он сам). nullptr — не в отряде. */
	UFUNCTION(BlueprintPure, Category = "NoFear|Squad")
	APawn* GetSquadCommander(const APawn* Pawn) const;

	/** Живые бойцы отряда Pawn, включая его самого. Пусто — не в отряде. */
	UFUNCTION(BlueprintPure, Category = "NoFear|Squad")
	TArray<APawn*> GetSquadMembers(const APawn* Pawn) const;

	/** Сколько врагов отряд видел за последние SquadIntelTime секунд. */
	UFUNCTION(BlueprintPure, Category = "NoFear|Squad")
	int32 GetSquadKnownEnemyCount(const APawn* Pawn) const;

	/** Сколько бойцов отряда Pawn (кроме него самого) сейчас целятся в Target. */
	UFUNCTION(BlueprintPure, Category = "NoFear|Squad")
	int32 CountSquadmatesTargeting(const APawn* Pawn, const AActor* Target) const;

	/** Боец выполняет приказ командира (кольцо пульсирует). Приказы появятся с тактиками отряда. */
	UFUNCTION(BlueprintCallable, Category = "NoFear|Squad")
	void SetExecutingOrder(const APawn* Pawn, bool bExecuting);

	/** Для интерфейса: false — Pawn не в отряде. */
	bool GetMemberView(const APawn* Pawn, FNoFearSquadMemberView& OutView) const;

	/** Бойцов в отряде не больше этого; лишние воюют сами по себе. */
	static constexpr int32 MaxSquadSize = 6;

	/** Как часто пересобирать отряды и делиться знанием о врагах, с. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NoFear|Squad", meta = (ClampMin = "0.05"))
	float UpdateInterval = 0.2f;

	/** Враг, которого кто-то из отряда видел не дольше этого назад, известен всему отряду, с. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NoFear|Squad", meta = (ClampMin = "0"))
	float SquadIntelTime = 1.f;

	/** Через столько после последнего взгляда отряд забывает врага, с. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NoFear|Squad", meta = (ClampMin = "0"))
	float EnemyMemoryTime = 30.f;

private:
	struct FMember
	{
		TWeakObjectPtr<ANoFearShooterAIController> Bot;
		TWeakObjectPtr<APawn> Pawn;
		ENoFearSquadRole Role = ENoFearSquadRole::Rifleman;
		ENoFearSquadRole PreviousRole = ENoFearSquadRole::Rifleman;
		double JoinTime = -1.0e9;
		double RoleChangeTime = -1.0e9;
		bool bExecutingOrder = false;
		/** Роль уже назначалась (первое назначение — не смена). */
		bool bRoleAssigned = false;
	};

	struct FEnemy
	{
		FVector Location = FVector::ZeroVector;
		double LastSeenTime = -1.0e9;
	};

	struct FSquad
	{
		FName Name;
		ENoFearTeam Team = ENoFearTeam::Neutral;
		TArray<FMember> Members;
		TWeakObjectPtr<APawn> Commander;
		double CommanderChangeTime = -1.0e9;
		/** Собран (в нём хоть раз было двое). */
		bool bFormed = false;
		TMap<TWeakObjectPtr<AActor>, FEnemy> Enemies;
	};

	const FSquad* FindSquadOf(const APawn* Pawn, const FMember** OutMember = nullptr) const;
	void AssignRoles(FSquad& Squad, double Now);
	void ShareIntel(FSquad& Squad, double Now);

	TArray<FSquad> Squads;
	float TimeSinceUpdate = 0.f;
};
