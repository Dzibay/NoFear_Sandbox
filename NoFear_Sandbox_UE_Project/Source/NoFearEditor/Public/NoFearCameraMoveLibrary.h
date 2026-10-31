#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "NoFearCameraMoveLibrary.generated.h"

/** Разовый помощник переноса камеры из CBP_SandboxCharacter в CBP_Player (только редактор). */
UCLASS()
class NOFEAREDITOR_API UNoFearCameraMoveLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** Выписать в лог узлы графов, связанные с переменной-компонентом. */
	UFUNCTION(BlueprintCallable, Category = "NoFear|Editor")
	static void DumpComponentNodes(UBlueprint* Blueprint, FName VariableName);

	/** Убрать из графов вызовы, где переменная-компонент служит целью (поток исполнения замыкается в обход), и сами узлы переменной. */
	UFUNCTION(BlueprintCallable, Category = "NoFear|Editor")
	static int32 RemoveComponentUsage(UBlueprint* Blueprint, FName VariableName);
};
