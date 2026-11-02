#pragma once

#include "CoreMinimal.h"
#include "Core/NoFearInteractableComponent.h"
#include "GameFramework/Actor.h"
#include "NoFearHideSpot.generated.h"

class UBoxComponent;
class UCameraComponent;
class UMaterialInterface;
class USoundBase;
class UStaticMesh;
class UStaticMeshComponent;

class ANoFearHideSpot;
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FNoFearHideEvent, ANoFearHideSpot*, Spot, APawn*, Pawn, bool, bForced);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FNoFearHideSearchEvent, ANoFearHideSpot*, Spot, APawn*, Searcher);

/** «[E] Спрятаться», изнутри — «Выйти». */
UCLASS(ClassGroup = (NoFear))
class NOFEAR_API UNoFearHideInteraction : public UNoFearInteractableComponent
{
	GENERATED_BODY()

public:
	UNoFearHideInteraction();

	virtual bool CanInteract_Implementation(APawn* User) const override;
	virtual FText GetInteractionPrompt_Implementation(APawn* User) const override;

protected:
	virtual void HandleInteract_Implementation(APawn* User) override;
};

/**
 * Шкаф-укрытие (Docs/Interactables.md): E — залезть внутрь, E ещё раз — выйти. Внутри героя не видно: стенки и дверца
 * перекрывают трассы зрения ботов, вид — из шкафа сквозь щели дверцы. Залезть и вылезти — шумно.
 * Монстр может обыскать шкаф (SearchBy): дверца распахивается, сидящего внутри выбрасывает наружу и оглушает.
 * Точка актора — центр пола шкафа, дверца смотрит по оси X.
 */
UCLASS(Blueprintable)
class NOFEAR_API ANoFearHideSpot : public AActor
{
	GENERATED_BODY()

public:
	ANoFearHideSpot();

	/** Глубина (X, от задней стенки к дверце), ширина (Y) и высота (Z) шкафа, см. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hide|Shape")
	FVector Size = FVector(65.f, 85.f, 210.f);

	/** Меш корпуса: растягивается на Size. Пусто — стенки из кубов-заглушек. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hide|Shape")
	TObjectPtr<UStaticMesh> BodyMesh;

	/** Меш дверцы: растягивается на её размер. Пусто — дверца из реек со щелями. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hide|Shape")
	TObjectPtr<UStaticMesh> DoorMesh;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hide|Shape")
	FLinearColor Color = FLinearColor(0.22f, 0.26f, 0.24f);

	/** На какой угол распахивается дверца. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hide|Shape", meta = (ClampMin = "10", ClampMax = "170"))
	float OpenAngle = 105.f;

	/** Сколько секунд дверца открывается или закрывается. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hide|Shape", meta = (ClampMin = "0.05"))
	float OpenTime = 0.25f;

	/** Сколько секунд выброшенный из шкафа не может двигаться. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hide|Search", meta = (ClampMin = "0"))
	float ForcedStunTime = 0.6f;

	/** С какого расстояния монстр может обыскать шкаф (от точки перед дверцей), см. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hide|Search", meta = (ClampMin = "50"))
	float SearchReach = 220.f;

	/** Сколько секунд после входа или выхода шкаф считается «недавно хлопнувшим». */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hide|Search", meta = (ClampMin = "0"))
	float RecentNoiseTime = 5.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hide|Sound")
	TObjectPtr<USoundBase> OpenSound;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hide|Sound")
	TObjectPtr<USoundBase> CloseSound;

	/** Громкость шума при входе и выходе. Обыск и выброс — вдвое громче. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hide|Sound", meta = (ClampMin = "0"))
	float NoiseLoudness = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hide|Sound", meta = (ClampMin = "0"))
	float NoiseRange = 700.f;

	/** Кто-то залез (bForced всегда false). */
	UPROPERTY(BlueprintAssignable, Category = "NoFear|Hide")
	FNoFearHideEvent OnEntered;

	/** Кто-то вышел; bForced — выброшен обыском или поломкой. */
	UPROPERTY(BlueprintAssignable, Category = "NoFear|Hide")
	FNoFearHideEvent OnExited;

	UPROPERTY(BlueprintAssignable, Category = "NoFear|Hide")
	FNoFearHideSearchEvent OnSearched;

	/** Залезть. false — занято, идёт обыск или пешка не может (мертва, далеко). */
	UFUNCTION(BlueprintCallable, Category = "NoFear|Hide")
	bool EnterBy(APawn* Pawn);

	/** Вылезти (сам или выброшен, если bForced). */
	UFUNCTION(BlueprintCallable, Category = "NoFear|Hide")
	bool ExitBy(APawn* Pawn, bool bForced = false);

	/** Монстр обыскивает: дверца распахивается, сидящего выбрасывает наружу. false — уже обыскивают. */
	UFUNCTION(BlueprintCallable, Category = "NoFear|Hide")
	bool SearchBy(APawn* Searcher);

	UFUNCTION(BlueprintPure, Category = "NoFear|Hide")
	APawn* GetOccupant() const { return Occupant.Get(); }

	UFUNCTION(BlueprintPure, Category = "NoFear|Hide")
	bool IsOccupied() const { return Occupant.IsValid(); }

	UFUNCTION(BlueprintPure, Category = "NoFear|Hide")
	bool IsBeingSearched() const { return bSearching; }

	/** Куда встать, чтобы обыскать (перед дверцей, на полу). */
	UFUNCTION(BlueprintPure, Category = "NoFear|Hide")
	FVector GetSearchLocation() const;

	/** В шкаф залезали или из него вылезали меньше RecentNoiseTime секунд назад. */
	UFUNCTION(BlueprintPure, Category = "NoFear|Hide")
	bool WasHeardRecently() const;

	/** Сколько раз шкаф обыскивали (для тестов). */
	UFUNCTION(BlueprintPure, Category = "NoFear|Hide")
	int32 GetSearchCount() const { return SearchCount; }

	/** 0 — дверца закрыта, 1 — открыта. */
	UFUNCTION(BlueprintPure, Category = "NoFear|Hide")
	float GetOpenAmount() const { return OpenAmount; }

	UNoFearHideInteraction* GetInteraction() const { return Interaction; }

	/** Шкаф, в котором сидит Actor, или nullptr. */
	UFUNCTION(BlueprintPure, Category = "NoFear|Hide")
	static ANoFearHideSpot* GetSpotOf(const AActor* Actor);

	/** Actor сидит в каком-нибудь шкафу. */
	UFUNCTION(BlueprintPure, Category = "NoFear|Hide")
	static bool IsActorHidden(const AActor* Actor) { return GetSpotOf(Actor) != nullptr; }

	/** Шкафы, у которых точка обыска не дальше Radius от Location; ближние первыми. */
	static void FindSpotsNear(const UWorld* World, const FVector& Location, float Radius, TArray<ANoFearHideSpot*>& OutSpots);

	static void GetSpots(const UWorld* World, TArray<ANoFearHideSpot*>& OutSpots);

	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void Tick(float DeltaSeconds) override;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Destroyed() override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Hide")
	TObjectPtr<USceneComponent> Root;

	/** Корпус: задняя стенка, бока, крыша (заглушки) или BodyMesh. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Hide")
	TArray<TObjectPtr<UStaticMeshComponent>> Walls;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Hide")
	TObjectPtr<USceneComponent> Hinge;

	/** Рейки дверцы или DoorMesh: только для вида. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Hide")
	TArray<TObjectPtr<UStaticMeshComponent>> DoorSlats;

	/** Сплошная преграда дверцы: держит пешек и трассы зрения, камеру пропускает. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Hide")
	TObjectPtr<UBoxComponent> DoorBlocker;

	/** Вид изнутри сквозь щели. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Hide")
	TObjectPtr<UCameraComponent> InsideCamera;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Hide")
	TObjectPtr<UNoFearHideInteraction> Interaction;

private:
	void RefreshShape();
	void RefreshDoorPose();
	void OpenDoorFor(float HoldTime);
	void MakeHideNoise(float LoudnessScale, AActor* NoiseInstigator);
	void ReleaseStun();
	FVector GetInsideLocation(const APawn* Pawn) const;

	TWeakObjectPtr<APawn> Occupant;
	TArray<TWeakObjectPtr<AActor>> HiddenAttached;
	TWeakObjectPtr<APawn> StunnedPawn;
	FTimerHandle StunTimer;
	FTimerHandle SearchTimer;
	TWeakObjectPtr<APawn> CurrentSearcher;
	float OpenAmount = 0.f;
	/** Дверца открыта до этого времени, потом закрывается. */
	double HoldOpenUntil = -1.0;
	double LastNoiseTime = -100.0;
	bool bSearching = false;
	bool bOccupantIsPlayer = false;
	int32 SearchCount = 0;

	UPROPERTY()
	TObjectPtr<UStaticMesh> PlaceholderMesh;

	UPROPERTY()
	TObjectPtr<UMaterialInterface> PlaceholderMaterial;
};
