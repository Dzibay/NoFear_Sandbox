#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "GenericTeamAgentInterface.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "NoFearTeam.generated.h"

/**
 * Команды. Значение совпадает с FGenericTeamId, который видит AI Perception.
 * Кто кому враг, задаёт таблица UNoFearTeamSettings (Project Settings → Game → NoFear Teams).
 */
UENUM(BlueprintType)
enum class ENoFearTeam : uint8
{
	Player = 0,
	/** Враждебные NPC: стреляют в игрока и его союзников. Раньше называлась NPC. */
	Enemy = 1,
	/** Монстры: нападают на игрока, союзников, врагов и мирных, но не друг на друга. */
	Monster = 2,
	/** Агрессивные монстры: бьют всех, включая монстров и других агрессивных монстров. */
	AggroMonster = 3,
	/** Союзники и напарники игрока. */
	Ally = 4,
	/** Мирные жители: ни на кого не нападают. */
	Civilian = 5,
	Neutral = 255 UMETA(DisplayName = "Neutral (No Team)")
};

/**
 * Роль стрелка в отряде (ANoFearCharacter::SquadRole). Роль даёт оружие (ANoFearShooterAIController::RoleWeapons)
 * и знак над головой. Командира, если его нет, назначает отряд (UNoFearSquadSubsystem).
 */
UENUM(BlueprintType)
enum class ENoFearSquadRole : uint8
{
	/** Не задана: в отряде — стрелок. */
	None,
	/** Командир: винтовка, выбирает тактику. Золотое двойное кольцо, корона. */
	Commander,
	/** Штурмовик: дробовик, перебежки и фланг. Оранжевое сплошное кольцо, шеврон. */
	Assault,
	/** Поддержка: винтовка, огонь на подавление. Голубое пунктирное кольцо, две черты. */
	Support,
	/** Стрелок: винтовка. Белое тонкое кольцо, точка. */
	Rifleman
};

/** Отношение пары команд. Действует в обе стороны: A к B и B к A. */
USTRUCT(BlueprintType)
struct NOFEAR_API FNoFearTeamRelation
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Team")
	ENoFearTeam TeamA = ENoFearTeam::Player;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Team")
	ENoFearTeam TeamB = ENoFearTeam::Enemy;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Team")
	TEnumAsByte<ETeamAttitude::Type> Attitude = ETeamAttitude::Hostile;

	FNoFearTeamRelation() = default;
	FNoFearTeamRelation(ENoFearTeam InA, ENoFearTeam InB, ETeamAttitude::Type InAttitude)
		: TeamA(InA), TeamB(InB), Attitude(InAttitude)
	{
	}
};

/**
 * Таблица отношений между командами. Хранится в DefaultGame.ini, правится в Project Settings → Game → NoFear Teams.
 * Порядок проверки: нет команды → нейтрал; одна команда → друг (или враг, если она в Self Hostile Teams);
 * пара из Relations; иначе Default Attitude.
 */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "NoFear Teams"))
class NOFEAR_API UNoFearTeamSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UNoFearTeamSettings();

	virtual FName GetCategoryName() const override { return TEXT("Game"); }

	/** Пары команд и их отношение. Пара без записи получает Default Attitude. */
	UPROPERTY(Config, EditAnywhere, Category = "Teams", meta = (TitleProperty = "{TeamA} - {TeamB}: {Attitude}"))
	TArray<FNoFearTeamRelation> Relations;

	/** Команды, члены которых враждуют друг с другом (каждый сам за себя). */
	UPROPERTY(Config, EditAnywhere, Category = "Teams")
	TArray<ENoFearTeam> SelfHostileTeams;

	/** Отношение для пар, которых нет в Relations. */
	UPROPERTY(Config, EditAnywhere, Category = "Teams")
	TEnumAsByte<ETeamAttitude::Type> DefaultAttitude = ETeamAttitude::Neutral;

	ETeamAttitude::Type GetAttitude(ENoFearTeam A, ENoFearTeam B) const;
};

UCLASS()
class NOFEAR_API UNoFearTeamLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** Решатель для FGenericTeamId (ставится в ANoFearGameMode::InitGame), по нему работает AI Perception. */
	static ETeamAttitude::Type GetAttitude(FGenericTeamId A, FGenericTeamId B);

	/** Отношение команды A к команде B по таблице NoFear Teams. */
	UFUNCTION(BlueprintPure, Category = "NoFear|Team")
	static TEnumAsByte<ETeamAttitude::Type> GetTeamAttitude(ENoFearTeam A, ENoFearTeam B);

	/** Отношение актора A к актору B. */
	UFUNCTION(BlueprintPure, Category = "NoFear|Team")
	static TEnumAsByte<ETeamAttitude::Type> GetActorAttitude(const AActor* A, const AActor* B);

	/** Команда актора: сам актор, его контроллер или владелец-пешка. Neutral, если команды нет. */
	UFUNCTION(BlueprintPure, Category = "NoFear|Team")
	static ENoFearTeam GetTeam(const AActor* Actor);

	UFUNCTION(BlueprintPure, Category = "NoFear|Team")
	static bool AreHostile(const AActor* A, const AActor* B);

	UFUNCTION(BlueprintPure, Category = "NoFear|Team")
	static bool AreFriendly(const AActor* A, const AActor* B);
};
