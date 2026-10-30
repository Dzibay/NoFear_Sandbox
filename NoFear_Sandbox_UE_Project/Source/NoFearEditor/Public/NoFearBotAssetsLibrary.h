#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "NoFearBotAssetsLibrary.generated.h"

/**
 * Генератор ассетов ботов (только редактор).
 *
 *  /Game/AI/BB_NoFearBot — блэкборд со всеми ключами FNoFearBotKeys и UNoFearCombatComponent.
 *      Если уже есть — только дописываются недостающие ключи.
 *  /Game/AI/BT_NoFear_Soldier, BT_NoFear_Monster, BT_NoFear_Civilian — деревья, которые грузят контроллеры.
 *  /Game/Blueprints/Bots/ — тела: наследники CBP_SandboxCharacter с командой, цветом, контроллером и оружием.
 *      CBP_NoFearBot → CBP_Bot_Civilian, CBP_Bot_Monster → CBP_Bot_AggroMonster,
 *      CBP_NoFearBot_Soldier → CBP_Bot_Enemy, CBP_Bot_Ally, CBP_Bot_Companion.
 *
 * Готовые деревья и тела не трогаются, пока не попросили перезаписать: их можно спокойно править руками.
 * Запуск: консоль редактора «NoFear.GenerateBotAssets [trees] [bodies]» или Python
 * unreal.NoFearBotAssetsLibrary.generate_bot_assets(False, False).
 */
UCLASS()
class NOFEAREDITOR_API UNoFearBotAssetsLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Создать недостающие ассеты ботов.
	 * @param bOverwriteTrees  Пересобрать деревья заново, даже если они уже есть (ручные правки пропадут).
	 * @param bUpdateBodies    Заново выставить настройки готовым телам (родитель, команда, цвет, контроллер, оружие).
	 * @return true, если всё создано и сохранено без ошибок.
	 */
	UFUNCTION(BlueprintCallable, Category = "NoFear|Editor")
	static bool GenerateBotAssets(bool bOverwriteTrees = false, bool bUpdateBodies = false);
};
