#pragma once

#include "CoreMinimal.h"
#include "Core/NoFearInteractableComponent.h"
#include "GameFramework/Actor.h"
#include "NoFearTraversal.generated.h"

class ANoFearTraversal;
class ANoFearTraversalHandle;
class UMaterialInterface;
class UNavLinkCustomComponent;
class USoundBase;
class UStaticMesh;
class UStaticMeshComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FNoFearTraverseEvent, ANoFearTraversal*, Traversal, APawn*, Pawn, bool, bFinished);

/** Подсказка и действие берутся у объекта перемещения (ANoFearTraversal), которому принадлежит ручка. */
UCLASS(ClassGroup = (NoFear))
class NOFEAR_API UNoFearTraversalInteraction : public UNoFearInteractableComponent
{
	GENERATED_BODY()

public:
	UNoFearTraversalInteraction();

	virtual bool CanInteract_Implementation(APawn* User) const override;
	virtual FText GetInteractionPrompt_Implementation(APawn* User) const override;

protected:
	virtual void HandleInteract_Implementation(APawn* User) override;
};

/**
 * Ручка — точка, на которую смотрит игрок, чтобы нажать E (низ и верх лестницы, пульт лифта). Её создаёт
 * ANoFearTraversal в BeginPlay; на уровень её не ставят.
 */
UCLASS(NotPlaceable)
class NOFEAR_API ANoFearTraversalHandle : public AActor
{
	GENERATED_BODY()

public:
	ANoFearTraversalHandle();

	/** Номер ручки у владельца (0 — низ лестницы, 1 — верх). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Traversal")
	int32 Index = 0;

	UFUNCTION(BlueprintPure, Category = "NoFear|Traversal")
	ANoFearTraversal* GetTraversal() const;

	UFUNCTION(BlueprintPure, Category = "NoFear|Traversal")
	UNoFearTraversalInteraction* GetInteraction() const { return Interaction; }

	/** Показать ручку кубом-заглушкой Size (пусто — невидимая). */
	void SetLook(const FVector& Size, const FLinearColor& Color, UMaterialInterface* PlaceholderMaterial);

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Traversal")
	TObjectPtr<USceneComponent> Root;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Traversal")
	TObjectPtr<UStaticMeshComponent> Mesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Traversal")
	TObjectPtr<UNoFearTraversalInteraction> Interaction;
};

/**
 * Основа лестницы и перелаза (Docs/Interactables.md, «Лестницы, перелазы, лифт»): по E пешка проходит заданную
 * ломаную сама (движение выключено, ввод игрока заблокирован), в конце снова ходит. Боты пользуются тем же
 * через умную навигационную ссылку Link: путь к цели может идти по лестнице или через забор.
 */
UCLASS(Abstract)
class NOFEAR_API ANoFearTraversal : public AActor
{
	GENERATED_BODY()

public:
	ANoFearTraversal();

	/** Скорость прохода, см/с. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Traversal", meta = (ClampMin = "10"))
	float TraverseSpeed = 150.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Traversal")
	FLinearColor Color = FLinearColor(0.35f, 0.33f, 0.3f);

	/** Звук в начале прохода. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Traversal|Sound")
	TObjectPtr<USoundBase> Sound;

	/** Шум для слуха ботов: громкость и дальность. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Traversal|Sound", meta = (ClampMin = "0"))
	float NoiseLoudness = 0.3f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Traversal|Sound", meta = (ClampMin = "0"))
	float NoiseRange = 600.f;

	/** Начало (bFinished = false) и конец прохода. */
	UPROPERTY(BlueprintAssignable, Category = "NoFear|Traversal")
	FNoFearTraverseEvent OnTraverse;

	/** Пешка сейчас проходит этот объект. */
	UFUNCTION(BlueprintPure, Category = "NoFear|Traversal")
	bool IsTraversing(const APawn* Pawn) const;

	/** Объект, который пешка сейчас проходит (nullptr — никакой). */
	static ANoFearTraversal* GetTraversalOf(const APawn* Pawn);

	/** Пройти от User (как по E). false — сейчас нельзя. */
	UFUNCTION(BlueprintCallable, Category = "NoFear|Traversal")
	bool Use(APawn* User);

	/** Для ручки: доступно ли, подсказка, действие. */
	virtual bool CanUseFrom(const ANoFearTraversalHandle* Handle, const APawn* User) const;
	virtual FText GetPromptFrom(const ANoFearTraversalHandle* Handle, const APawn* User) const;
	virtual bool UseFrom(ANoFearTraversalHandle* Handle, APawn* User);

	virtual void Tick(float DeltaSeconds) override;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** Ломаная точек ног (в мире) от места User до выхода и взгляд пешки на ней. false — отсюда нельзя. */
	virtual bool BuildPath(const APawn* User, TArray<FVector>& OutFeet, float& OutYaw) const { return false; }

	/** Ручки, через которые игрок жмёт E (центры в осях актора), и с какого расстояния до ручки. */
	virtual void GetHandleSpots(TArray<FVector>& OutSpots) const {}
	virtual float GetHandleReach() const { return 200.f; }

	/** Ручки ставятся в BeginPlay по GetHandleSpots. */
	ANoFearTraversalHandle* SpawnHandle(const FVector& LocalSpot, int32 Index);

	/** Повесить на Link обработчик и включить его (наследник задаёт концы через SetLinkData). */
	void SetupLink();

	/** Начать проход пешки по ломаной Feet. PathComp — путевой компонент бота на ссылке. */
	bool BeginTraverse(APawn* Pawn, const TArray<FVector>& Feet, float Yaw, UObject* PathComp);

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Traversal")
	TObjectPtr<USceneComponent> Root;

	/** Умная навигационная ссылка для ботов (может не быть). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Traversal")
	TObjectPtr<UNavLinkCustomComponent> Link;

	UPROPERTY()
	TObjectPtr<UMaterialInterface> PlaceholderMaterial;

	UPROPERTY()
	TObjectPtr<UStaticMesh> CubeMesh;

	UPROPERTY(Transient)
	TArray<TObjectPtr<ANoFearTraversalHandle>> Handles;

private:
	struct FMove
	{
		TWeakObjectPtr<APawn> Pawn;
		TWeakObjectPtr<UObject> PathComp;
		TArray<FVector> Points;
		int32 Next = 1;
		float Yaw = 0.f;
	};

	void HandleLinkReached(UNavLinkCustomComponent* ThisComp, UObject* PathComp, const FVector& DestPoint);
	void EndMove(FMove& Move, bool bFinished);

	TArray<FMove> Moves;
};

/**
 * Лестница: стоит у стены лицом по +X, точка актора — у подножия. E внизу — подняться, наверху — спуститься.
 * Пока лезешь, не стреляешь и не отвечаешь на ввод. Монстры лезут следом за героем.
 */
UCLASS(Blueprintable)
class NOFEAR_API ANoFearLadder : public ANoFearTraversal
{
	GENERATED_BODY()

public:
	ANoFearLadder();

	/** Высота верхней площадки над подножием, см. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ladder", meta = (ClampMin = "50"))
	float Height = 300.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ladder", meta = (ClampMin = "20"))
	float Width = 50.f;

	/** Где стоит лезущий внизу: от стены по +X, см. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ladder", meta = (ClampMin = "20"))
	float StandOff = 45.f;

	/** Куда выходит наверху: от края за стену (по −X), см. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ladder", meta = (ClampMin = "20"))
	float TopInset = 90.f;

	/** С какого расстояния от точки входа (низ или верх) можно начать, см. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ladder", meta = (ClampMin = "30"))
	float EntryReach = 130.f;

	virtual bool CanUseFrom(const ANoFearTraversalHandle* Handle, const APawn* User) const override;
	virtual FText GetPromptFrom(const ANoFearTraversalHandle* Handle, const APawn* User) const override;
	virtual void OnConstruction(const FTransform& Transform) override;

protected:
	virtual void BeginPlay() override;
	virtual bool BuildPath(const APawn* User, TArray<FVector>& OutFeet, float& OutYaw) const override;
	virtual void GetHandleSpots(TArray<FVector>& OutSpots) const override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ladder")
	TObjectPtr<UStaticMeshComponent> RailL;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ladder")
	TObjectPtr<UStaticMeshComponent> RailR;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ladder")
	TArray<TObjectPtr<UStaticMeshComponent>> Rungs;

private:
	/** Пешка наверху (ближе к верхней точке входа, чем к нижней). */
	bool IsAtTop(const APawn* User) const;
	FVector GetBottomFoot() const;
	FVector GetTopFoot() const;
	void PlaceParts();
};

/**
 * Перелаз — забор или завал по пояс: стенка вдоль Y, точка актора — середина основания. E рядом — перелезть на
 * другую сторону там, где стоишь. Обойти его нельзя, монстры перелезают так же (через ссылку в середине).
 */
UCLASS(Blueprintable)
class NOFEAR_API ANoFearVault : public ANoFearTraversal
{
	GENERATED_BODY()

public:
	ANoFearVault();

	/** Длина (Y), высота и толщина (X) стенки, см. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Vault", meta = (ClampMin = "40"))
	float Length = 300.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Vault", meta = (ClampMin = "30"))
	float WallHeight = 110.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Vault", meta = (ClampMin = "5"))
	float Thickness = 20.f;

	/** Где стоит пешка до и после перелаза: от стенки, см. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Vault", meta = (ClampMin = "30"))
	float Approach = 70.f;

	/** С какого расстояния от стенки можно перелезть, см. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Vault", meta = (ClampMin = "40"))
	float Reach = 150.f;

	virtual bool CanUseFrom(const ANoFearTraversalHandle* Handle, const APawn* User) const override;
	virtual FText GetPromptFrom(const ANoFearTraversalHandle* Handle, const APawn* User) const override;
	virtual void OnConstruction(const FTransform& Transform) override;

protected:
	virtual void BeginPlay() override;
	virtual bool BuildPath(const APawn* User, TArray<FVector>& OutFeet, float& OutYaw) const override;
	virtual void GetHandleSpots(TArray<FVector>& OutSpots) const override;
	virtual float GetHandleReach() const override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Vault")
	TObjectPtr<UStaticMeshComponent> Wall;

private:
	void PlaceParts();
};

class ANoFearLift;
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FNoFearLiftEvent, ANoFearLift*, Lift, int32, Floor);

/**
 * Грузовой лифт — платформа с пультом: E на пульте везёт на другой этаж, кнопки ANoFearLiftButton на этажах
 * вызывают его. Нужна линия питания Circuit (пусто — всегда); без питания встаёт и едет дальше, когда питание
 * вернётся. Гудит на ходу — слышно. Точка актора — середина верха платформы на нижнем этаже. Боты им не пользуются.
 */
UCLASS(Blueprintable)
class NOFEAR_API ANoFearLift : public ANoFearTraversal
{
	GENERATED_BODY()

public:
	ANoFearLift();

	/** Платформа: X, Y и толщина, см. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Lift")
	FVector PlatformSize = FVector(220.f, 220.f, 20.f);

	/** Высоты этажей над начальной точкой, см (первый — 0). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Lift")
	TArray<float> Floors = { 0.f, 300.f };

	/** Скорость, см/с. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lift", meta = (ClampMin = "10"))
	float Speed = 120.f;

	/** Линия питания (UNoFearLightSubsystem). Пусто — питание не нужно. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Lift")
	FName Circuit;

	/** Гул на ходу: шум раз в HumInterval. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Lift|Sound", meta = (ClampMin = "0.2"))
	float HumInterval = 1.f;

	UPROPERTY(BlueprintAssignable, Category = "NoFear|Lift")
	FNoFearLiftEvent OnArrived;

	UFUNCTION(BlueprintPure, Category = "NoFear|Lift")
	bool IsPowered() const;

	UFUNCTION(BlueprintPure, Category = "NoFear|Lift")
	bool IsMoving() const { return TargetFloor != INDEX_NONE; }

	/** Этаж, на котором стоит (INDEX_NONE — едет). */
	UFUNCTION(BlueprintPure, Category = "NoFear|Lift")
	int32 GetCurrentFloor() const { return IsMoving() ? INDEX_NONE : CurrentFloor; }

	/** Высота этажа Floor в мире. */
	UFUNCTION(BlueprintPure, Category = "NoFear|Lift")
	float GetFloorZ(int32 Floor) const;

	/** Отправить на этаж Floor. false — нет такого, нет питания или уже едет. */
	UFUNCTION(BlueprintCallable, Category = "NoFear|Lift")
	bool MoveToFloor(int32 Floor, APawn* User = nullptr);

	virtual bool CanUseFrom(const ANoFearTraversalHandle* Handle, const APawn* User) const override;
	virtual FText GetPromptFrom(const ANoFearTraversalHandle* Handle, const APawn* User) const override;
	virtual bool UseFrom(ANoFearTraversalHandle* Handle, APawn* User) override;
	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void Tick(float DeltaSeconds) override;

protected:
	virtual void BeginPlay() override;
	virtual void GetHandleSpots(TArray<FVector>& OutSpots) const override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Lift")
	TObjectPtr<UStaticMeshComponent> Platform;

	/** Стойка пульта на углу платформы. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Lift")
	TObjectPtr<UStaticMeshComponent> Post;

private:
	void PlaceParts();
	FVector GetPostSpot() const;

	float BaseZ = 0.f;
	int32 CurrentFloor = 0;
	int32 TargetFloor = INDEX_NONE;
	float HumTime = 0.f;
	TWeakObjectPtr<APawn> Rider;
};

/** «Вызвать лифт» на кнопке у этажа. */
UCLASS(ClassGroup = (NoFear))
class NOFEAR_API UNoFearLiftButtonInteraction : public UNoFearInteractableComponent
{
	GENERATED_BODY()

public:
	UNoFearLiftButtonInteraction();

	virtual bool CanInteract_Implementation(APawn* User) const override;
	virtual FText GetInteractionPrompt_Implementation(APawn* User) const override;

protected:
	virtual void HandleInteract_Implementation(APawn* User) override;
};

/** Кнопка вызова лифта Lift на этаж Floor: столбик с коробкой. */
UCLASS(Blueprintable)
class NOFEAR_API ANoFearLiftButton : public AActor
{
	GENERATED_BODY()

public:
	ANoFearLiftButton();

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Lift")
	TObjectPtr<ANoFearLift> Lift;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Lift", meta = (ClampMin = "0"))
	int32 Floor = 0;

	/** Вызвать лифт от User. */
	UFUNCTION(BlueprintCallable, Category = "NoFear|Lift")
	bool Call(APawn* User);

	bool CanCall() const;
	FText GetPrompt() const;

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Lift")
	TObjectPtr<USceneComponent> Root;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Lift")
	TObjectPtr<UStaticMeshComponent> Box;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Lift")
	TObjectPtr<UNoFearLiftButtonInteraction> Interaction;
};
