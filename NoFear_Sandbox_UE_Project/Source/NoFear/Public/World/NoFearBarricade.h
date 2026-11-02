#pragma once

#include "CoreMinimal.h"
#include "Core/NoFearInteractableComponent.h"
#include "GameFramework/Actor.h"
#include "NavAreas/NavArea.h"
#include "NavFilters/NavigationQueryFilter.h"
#include "NoFearBarricade.generated.h"

class ANoFearDoor;
class UBoxComponent;
class UMaterialInterface;
class UNavModifierComponent;
class UNoFearHealthComponent;
class UNoFearItemData;
class USoundBase;
class UStaticMesh;
class UStaticMeshComponent;

class ANoFearBarricade;
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FNoFearBarricadeChanged, ANoFearBarricade*, Barricade, int32, Boards);

/**
 * Навмеш заколоченного проёма: не вырезан, а очень дорог. Монстр, которому некуда больше идти, строит путь сквозь
 * доски и бьёт их; путь в обход, если он есть, дешевле.
 */
UCLASS()
class NOFEAR_API UNoFearNavArea_Barricade : public UNavArea
{
	GENERATED_BODY()

public:
	UNoFearNavArea_Barricade();
};

/** Фильтр путей для людей: заколоченные проёмы для них закрыты (ставится в DefaultNavigationFilterClass контроллера). */
UCLASS()
class NOFEAR_API UNoFearHumanNavFilter : public UNavigationQueryFilter
{
	GENERATED_BODY()

public:
	UNoFearHumanNavFilter();
};

/** «[E] Заколотить (держать)» или «[E] Снять доску (держать)». */
UCLASS(ClassGroup = (NoFear))
class NOFEAR_API UNoFearBarricadeInteraction : public UNoFearInteractableComponent
{
	GENERATED_BODY()

public:
	UNoFearBarricadeInteraction();

	virtual bool CanInteract_Implementation(APawn* User) const override;
	virtual FText GetInteractionPrompt_Implementation(APawn* User) const override;

protected:
	virtual void HandleInteract_Implementation(APawn* User) override;
};

/**
 * Баррикада в проёме (Docs/Interactables.md): зажать E — прибить доску из кейса (до MaxBoards), без досок или при
 * полной баррикаде — снять верхнюю обратно в кейс. Пока стоит хоть одна доска, проём закрыт для пешек и навмеша.
 * Монстры, которым надо пройти или чья цель за проёмом, бьют доски; у каждой доски свой запас прочности.
 * Сквозь щели между досками видно и можно стрелять. Точка актора — середина проёма на полу, ось X — нормаль.
 */
UCLASS(Blueprintable)
class NOFEAR_API ANoFearBarricade : public AActor
{
	GENERATED_BODY()

public:
	ANoFearBarricade();

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Barricade|Shape", meta = (ClampMin = "40"))
	float Width = 120.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Barricade|Shape", meta = (ClampMin = "60"))
	float Height = 210.f;

	/** Меш доски: растягивается на размер доски. Пусто — куб-заглушка. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Barricade|Shape")
	TObjectPtr<UStaticMesh> BoardMesh;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Barricade|Shape")
	FLinearColor BoardColor = FLinearColor(0.42f, 0.30f, 0.18f);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Barricade", meta = (ClampMin = "1", ClampMax = "6"))
	int32 MaxBoards = 3;

	/** Сколько досок прибито к началу игры. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Barricade", meta = (ClampMin = "0", ClampMax = "6"))
	int32 InitialBoards = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Barricade", meta = (ClampMin = "1"))
	float BoardHealth = 100.f;

	/** Сколько секунд держать E, чтобы прибить или снять доску. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Barricade", meta = (ClampMin = "0.1"))
	float BoardTime = 1.5f;

	/** Предмет кейса, который тратится на доску. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Barricade")
	TSoftObjectPtr<UNoFearItemData> PlanksItem;

	/** Дверь в этом проёме: пока баррикада стоит, её не открыть. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Barricade")
	TObjectPtr<ANoFearDoor> LinkedDoor;

	/** С какого расстояния от плоскости проёма монстры замечают баррикаду. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Barricade|AI", meta = (ClampMin = "50"))
	float BotReactDistance = 200.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Barricade|Sound")
	TObjectPtr<USoundBase> HammerSound;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Barricade|Sound")
	TObjectPtr<USoundBase> HitSound;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Barricade|Sound")
	TObjectPtr<USoundBase> BreakSound;

	/** Стук молотка слышно далеко: заколачиваться — шумно. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Barricade|Sound", meta = (ClampMin = "0"))
	float NoiseLoudness = 0.8f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Barricade|Sound", meta = (ClampMin = "0"))
	float NoiseRange = 1400.f;

	UPROPERTY(BlueprintAssignable, Category = "NoFear|Barricade")
	FNoFearBarricadeChanged OnBoardsChanged;

	UFUNCTION(BlueprintPure, Category = "NoFear|Barricade")
	int32 GetBoards() const { return Boards; }

	UFUNCTION(BlueprintPure, Category = "NoFear|Barricade")
	bool IsBarricaded() const { return Boards > 0; }

	/** Прочность верхней доски, 0..1. */
	UFUNCTION(BlueprintPure, Category = "NoFear|Barricade")
	float GetTopBoardHealthFraction() const;

	/** Прогресс удержания E, 0..1 (для HUD и тестов). */
	UFUNCTION(BlueprintPure, Category = "NoFear|Barricade")
	float GetWorkProgress() const;

	/** Пользователь может прибить доску: есть доски в кейсе и место в проёме. */
	UFUNCTION(BlueprintPure, Category = "NoFear|Barricade")
	bool CanAddBoard(const APawn* User) const;

	/** Прибить доску сразу (без удержания и без траты досок, если User == nullptr). */
	UFUNCTION(BlueprintCallable, Category = "NoFear|Barricade")
	bool AddBoard(APawn* User);

	/** Снять верхнюю доску; User получает её в кейс. */
	UFUNCTION(BlueprintCallable, Category = "NoFear|Barricade")
	bool RemoveBoard(APawn* User);

	/** Начать работу (E нажата): дальше Tick ждёт, пока E держат BoardTime секунд. */
	void BeginWork(APawn* User);

	/** Сколько раз монстры начинали бить баррикаду (для тестов). */
	UFUNCTION(BlueprintPure, Category = "NoFear|Barricade")
	int32 GetMonsterAttackCount() const { return MonsterAttackCount; }

	/** Какая баррикада держит эту дверь, или nullptr. */
	static ANoFearBarricade* FindForDoor(const ANoFearDoor* Door);

	static void GetBarricades(const UWorld* World, TArray<ANoFearBarricade*>& OutBarricades);

	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void Tick(float DeltaSeconds) override;
	virtual float TakeDamage(float DamageAmount, const FDamageEvent& DamageEvent, AController* EventInstigator, AActor* DamageCauser) override;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Barricade")
	TObjectPtr<USceneComponent> Root;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Barricade")
	TArray<TObjectPtr<UStaticMeshComponent>> BoardMeshes;

	/** Держит пешек, пока прибита хоть одна доска. Зрение и пули проходят в щели. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Barricade")
	TObjectPtr<UBoxComponent> PawnBlocker;

	/** Цель для ударов монстров (тип Pawn, только запросы). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Barricade")
	TObjectPtr<UBoxComponent> HitProxy;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Barricade")
	TObjectPtr<UNavModifierComponent> NavBlocker;

	/** Нужен, чтобы удары монстров засчитывались; сам урон считает TakeDamage по доскам. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Barricade")
	TObjectPtr<UNoFearHealthComponent> Health;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Barricade")
	TObjectPtr<UNoFearBarricadeInteraction> Interaction;

private:
	void SetBoards(int32 NewBoards);
	void RefreshShape();
	void RefreshState();
	void UpdateWork(float DeltaSeconds);
	void UpdateBots();
	void BreakTopBoard(AActor* Breaker);
	UNoFearItemData* GetPlanks() const;
	int32 GetSideOf(const FVector& Location) const;

	int32 Boards = 0;
	float TopBoardHealth = 0.f;
	int32 MonsterAttackCount = 0;
	double LastBotUpdateTime = 0.0;

	TWeakObjectPtr<APawn> Worker;
	bool bWorkAdds = true;
	float WorkTime = 0.f;
	double LastHammerTime = -10.0;

	UPROPERTY()
	TObjectPtr<UStaticMesh> PlaceholderMesh;

	UPROPERTY()
	TObjectPtr<UMaterialInterface> PlaceholderMaterial;
};
