#pragma once

#include "CoreMinimal.h"
#include "GenericTeamAgentInterface.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "NoFearTeam.generated.h"

/** Команды. Значение совпадает с FGenericTeamId, который видит AI Perception. */
UENUM(BlueprintType)
enum class ENoFearTeam : uint8
{
	Player = 0,
	NPC = 1,
	Monster = 2,
	AggroMonster = 3,
	Neutral = 255 UMETA(DisplayName = "Neutral (No Team)")
};

/**
 * Отношения между командами. Своя команда — друг, нейтральные — нейтральны,
 * остальные враждуют. Регистрируется в FGenericTeamId при старте GameMode.
 */
UCLASS()
class NOFEAR_API UNoFearTeamLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	static ETeamAttitude::Type GetAttitude(FGenericTeamId A, FGenericTeamId B);

	/** Команда актора: сам актор, его контроллер или владелец-пешка. Neutral, если команды нет. */
	UFUNCTION(BlueprintPure, Category = "NoFear|Team")
	static ENoFearTeam GetTeam(const AActor* Actor);

	UFUNCTION(BlueprintPure, Category = "NoFear|Team")
	static bool AreHostile(const AActor* A, const AActor* B);

	UFUNCTION(BlueprintPure, Category = "NoFear|Team")
	static bool AreFriendly(const AActor* A, const AActor* B);
};
