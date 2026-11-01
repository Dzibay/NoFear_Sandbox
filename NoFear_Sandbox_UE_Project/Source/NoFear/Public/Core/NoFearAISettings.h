#pragma once

#include "CoreMinimal.h"
#include "AI/NoFearPackSubsystem.h"
#include "AI/NoFearSquadSubsystem.h"
#include "Core/NoFearBraceletComponent.h"
#include "Core/NoFearFearSubsystem.h"
#include "Engine/DeveloperSettings.h"
#include "Portal/NoFearPortalTypes.h"
#include "NoFearAISettings.generated.h"

/** Уровни детализации ботов (UNoFearAILodSubsystem). */
USTRUCT(BlueprintType)
struct FNoFearAILodTuning
{
	GENERATED_BODY()

	/** Как часто пересчитывать уровни, сек. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lod", meta = (ClampMin = "0.05"))
	float Interval = 0.25f;

	/** Ближе этого до камеры игрока — Near (всё каждый кадр), см. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lod", meta = (ClampMin = "0"))
	float NearDistance = 2500.f;

	/** Ближе этого — Mid, дальше — Far, см. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lod", meta = (ClampMin = "0"))
	float MidDistance = 5000.f;

	/** Запас против дребезга на границе: уровень понижается, только когда бот ушёл дальше границы на эту долю. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lod", meta = (ClampMin = "0", ClampMax = "0.5"))
	float Hysteresis = 0.1f;

	/** Во сколько раз реже думает бот Mid (интервал UpdateBot умножается). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lod", meta = (ClampMin = "1"))
	float MidThinkScale = 1.6f;

	/** Во сколько раз реже думает бот Far. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lod", meta = (ClampMin = "1"))
	float FarThinkScale = 3.f;

	/** Во сколько раз реже бот Mid проверяет линию огня и подавление. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lod", meta = (ClampMin = "1"))
	float MidCombatScale = 1.5f;

	/** Во сколько раз реже бот Far проверяет линию огня и подавление. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lod", meta = (ClampMin = "1"))
	float FarCombatScale = 3.f;

	/** URO у сетки бота: пропуск кадров анимации по размеру на экране, за кадром — обновление раз в 4 кадра. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lod")
	bool bAnimUpdateRateOptimization = true;

	/** Бот Far за кадром не считает позу (монтажи и их события идут). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lod")
	bool bFreezeFarOffscreenPose = true;
};

/** Лимиты поиска точек на навмеше (укрытия, бегство). */
USTRUCT(BlueprintType)
struct FNoFearAISearchTuning
{
	GENERATED_BODY()

	/** Сколько полных поисков укрытия разрешено за кадр. Остальные ждут следующего кадра (задача бота висит в InProgress). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Search", meta = (ClampMin = "1"))
	int32 CoverSearchesPerFrame = 2;

	/** Сколько поисков точки (бегство, обход, Find Location) разрешено за кадр. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Search", meta = (ClampMin = "1"))
	int32 LocationSearchesPerFrame = 3;

	/** Доля точек-кандидатов укрытия для бота Mid. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Search", meta = (ClampMin = "0.1", ClampMax = "1"))
	float MidSampleScale = 0.75f;

	/** Доля точек-кандидатов укрытия для бота Far. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Search", meta = (ClampMin = "0.1", ClampMax = "1"))
	float FarSampleScale = 0.5f;
};

/** Ограничения индикаторов HUD. */
USTRUCT(BlueprintType)
struct FNoFearAIHudTuning
{
	GENERATED_BODY()

	/** Больше этого панелей над ботами не рисуется (ближние первыми). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HUD", meta = (ClampMin = "0"))
	int32 MaxBotPanels = 24;

	/** Больше этого колец ролей отряда не рисуется (ближние первыми). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HUD", meta = (ClampMin = "0"))
	int32 MaxSquadRings = 12;
};

/**
 * Общие настройки ИИ (этап 10): Project Settings → Game → NoFear AI, хранятся в DefaultGame.ini.
 * Подсистемы копируют свои числа отсюда при создании мира — правка в редакторе действует со следующего запуска.
 */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "NoFear AI"))
class NOFEAR_API UNoFearAISettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	virtual FName GetCategoryName() const override { return TEXT("Game"); }

	static const UNoFearAISettings* Get() { return GetDefault<UNoFearAISettings>(); }

	UPROPERTY(Config, EditAnywhere, Category = "Squad", meta = (ShowOnlyInnerProperties))
	FNoFearSquadTuning Squad;

	UPROPERTY(Config, EditAnywhere, Category = "Pack", meta = (ShowOnlyInnerProperties))
	FNoFearPackTuning Pack;

	UPROPERTY(Config, EditAnywhere, Category = "Lod", meta = (ShowOnlyInnerProperties))
	FNoFearAILodTuning Lod;

	UPROPERTY(Config, EditAnywhere, Category = "Search", meta = (ShowOnlyInnerProperties))
	FNoFearAISearchTuning Search;

	UPROPERTY(Config, EditAnywhere, Category = "HUD", meta = (ShowOnlyInnerProperties))
	FNoFearAIHudTuning Hud;

	/** Страх людей (этап 11). */
	UPROPERTY(Config, EditAnywhere, Category = "Fear", meta = (ShowOnlyInnerProperties))
	FNoFearFearTuning Fear;

	/** Страх героя и браслет (этап 12): источники, эффекты, ступор, стабилизация, дыхание. */
	UPROPERTY(Config, EditAnywhere, Category = "HeroFear", meta = (ShowOnlyInnerProperties))
	FNoFearHeroFearTuning HeroFear;

	/** Порталы страха (этап 13): слом, рост, аура, монстры по ступеням, освобождение проводников. */
	UPROPERTY(Config, EditAnywhere, Category = "Portal", meta = (ShowOnlyInnerProperties))
	FNoFearPortalTuning Portal;
};
