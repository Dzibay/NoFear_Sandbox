#pragma once

#include "CoreMinimal.h"
#include "Core/NoFearInteractableComponent.h"
#include "GameFramework/Actor.h"
#include "World/NoFearChest.h"
#include "NoFearBody.generated.h"

class ANoFearBody;
class UMaterialInterface;
class UNoFearItemContainerComponent;
class USoundBase;
class UStaticMeshComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FNoFearBodySearched, ANoFearBody*, Body, APawn*, User);

/** Подсказка и действие берутся у тела (ANoFearBody). */
UCLASS(ClassGroup = (NoFear))
class NOFEAR_API UNoFearBodyInteraction : public UNoFearInteractableComponent
{
	GENERATED_BODY()

public:
	UNoFearBodyInteraction();

	virtual bool CanInteract_Implementation(APawn* User) const override;
	virtual FText GetInteractionPrompt_Implementation(APawn* User) const override;

protected:
	virtual void HandleInteract_Implementation(APawn* User) override;
};

/**
 * Тело на полу (Docs/Interactables.md): обыскивается удержанием E (SearchTime, слышно рядом), потом открывается
 * экран его вещей рядом с кейсом героя — как у сундука. Обысканное по E открывает экран сразу.
 */
UCLASS(Blueprintable)
class NOFEAR_API ANoFearBody : public AActor
{
	GENERATED_BODY()

public:
	ANoFearBody();

	/** Что при нём. Раскладывается по сетке Storage в BeginPlay. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Body")
	TArray<FNoFearChestItem> Contents;

	/** Имя на экране вещей («Тело солдата»). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Body")
	FText DisplayName;

	/** Сколько держать E на первый обыск. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Body", meta = (ClampMin = "0"))
	float SearchTime = 1.5f;

	/** Цвет одежды заглушки. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Body")
	FLinearColor Color = FLinearColor(0.2f, 0.25f, 0.18f);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Body|Sound")
	TObjectPtr<USoundBase> SearchSound;

	/** Шум обыска для слуха ботов. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Body|Sound", meta = (ClampMin = "0"))
	float NoiseLoudness = 0.2f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Body|Sound", meta = (ClampMin = "0"))
	float NoiseRange = 400.f;

	UPROPERTY(BlueprintAssignable, Category = "NoFear|Body")
	FNoFearBodySearched OnSearched;

	UFUNCTION(BlueprintPure, Category = "NoFear|Body")
	bool IsSearched() const { return bSearched; }

	UFUNCTION(BlueprintPure, Category = "NoFear|Body")
	bool IsEmpty() const;

	UFUNCTION(BlueprintPure, Category = "NoFear|Body")
	UNoFearItemContainerComponent* GetStorage() const { return Storage; }

	/** Прогресс удержания E, 0..1 (для HUD и тестов). */
	UFUNCTION(BlueprintPure, Category = "NoFear|Body")
	float GetSearchProgress() const;

	/** Обыскать сразу (без удержания) и открыть экран вещей у User. */
	UFUNCTION(BlueprintCallable, Category = "NoFear|Body")
	bool Search(APawn* User);

	/** Открыть экран вещей у User. false — ещё не обыскано или нет кейса. */
	UFUNCTION(BlueprintCallable, Category = "NoFear|Body")
	bool OpenScreen(APawn* User);

	void InteractBy(APawn* User);
	FText GetPrompt(const APawn* User) const;

	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void Tick(float DeltaSeconds) override;

protected:
	virtual void BeginPlay() override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Body")
	TObjectPtr<USceneComponent> Root;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Body")
	TObjectPtr<UStaticMeshComponent> Torso;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Body")
	TObjectPtr<UStaticMeshComponent> Head;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Body")
	TObjectPtr<UStaticMeshComponent> LegL;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Body")
	TObjectPtr<UStaticMeshComponent> LegR;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Body")
	TObjectPtr<UNoFearItemContainerComponent> Storage;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Body")
	TObjectPtr<UNoFearBodyInteraction> Interaction;

	UPROPERTY()
	TObjectPtr<UMaterialInterface> PlaceholderMaterial;

private:
	void RefreshLook();

	bool bSearched = false;
	TWeakObjectPtr<APawn> Searcher;
	float SearchElapsed = 0.f;
};
