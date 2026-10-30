#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "NoFearPoseAssetsLibrary.generated.h"

/**
 * Генератор пост-процесс AnimBP для поз (только редактор).
 *
 * /Game/Blueprints/ABP_NoFearPose — копия пост-процесса манекена (ABP_UEFN_Mannequin_PostProcess, он остаётся на месте),
 * у которой родитель сменён на UNoFearPoseAnimInstance, а перед выходом вставлен узел «NoFear Pose».
 * Копия нужна, потому что у меша может быть только один пост-процесс AnimBP: наложение поз не должно ломать штатный.
 * ANoFearCharacter подставляет ABP_NoFearPose в меш на BeginPlay (PoseAnimBlueprint).
 *
 * Если ассет уже есть, он только проверяется и чинится (родитель, узел). Запуск: консоль редактора
 * «NoFear.GeneratePoseAnimBP» или Python unreal.NoFearPoseAssetsLibrary.generate_pose_anim_blueprint().
 */
UCLASS()
class NOFEAREDITOR_API UNoFearPoseAssetsLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** Создать или починить ABP_NoFearPose. Возвращает true, если ассет готов, скомпилирован и сохранён. */
	UFUNCTION(BlueprintCallable, Category = "NoFear|Editor")
	static bool GeneratePoseAnimBlueprint();
};
