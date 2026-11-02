#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "NoFearCharacterAssetsLibrary.generated.h"

/** Правки графов персонажа GASP из Python (только редактор). */
UCLASS()
class NOFEAREDITOR_API UNoFearCharacterAssetsLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Убрать из графов узлы клавиш KeyNames (K2Node_InputKey) и всё, что исполняется только от них: узлы, у которых
	 * каждый связанный вход исполнения идёт от убранных, и чистые узлы, все выходы которых идут в убранные.
	 * Комментарии, текст которых содержит строку из CommentsToRemove, тоже уходят. Каждый узел пишется в лог;
	 * bDryRun — только лог, граф не меняется. Возвращает число убранных (или подлежащих) узлов.
	 */
	UFUNCTION(BlueprintCallable, Category = "NoFear|Editor")
	static int32 RemoveInputKeyHandlers(UBlueprint* Blueprint, const TArray<FName>& KeyNames, const TArray<FString>& CommentsToRemove, bool bDryRun);
};
