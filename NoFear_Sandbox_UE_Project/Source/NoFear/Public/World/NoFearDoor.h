#pragma once

#include "CoreMinimal.h"
#include "Core/NoFearInteractableComponent.h"
#include "GameFramework/Actor.h"
#include "NoFearDoor.generated.h"

class AAIController;
class UBoxComponent;
class UMaterialInterface;
class UNavModifierComponent;
class UNoFearHealthComponent;
class USoundBase;
class UStaticMesh;
class UStaticMeshComponent;
struct FNoFearDamageInfo;

UENUM(BlueprintType)
enum class ENoFearDoorState : uint8
{
	Closed,
	Opening,
	Open,
	Closing,
	/** Выбита: створок нет, проход свободен. */
	Broken
};

class ANoFearDoor;
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FNoFearDoorStateEvent, ANoFearDoor*, Door, ENoFearDoorState, NewState);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FNoFearDoorLockEvent, ANoFearDoor*, Door, bool, bLocked);

/** «[E] Открыть / Закрыть», у запертой — «Заперто». */
UCLASS(ClassGroup = (NoFear))
class NOFEAR_API UNoFearDoorInteraction : public UNoFearInteractableComponent
{
	GENERATED_BODY()

public:
	UNoFearDoorInteraction();

	virtual bool CanInteract_Implementation(APawn* User) const override;
	virtual FText GetInteractionPrompt_Implementation(APawn* User) const override;

protected:
	virtual void HandleInteract_Implementation(APawn* User) override;
};

/**
 * Дверь (Docs/Interactables.md): одна или две створки на петлях, E открывает от себя и закрывает.
 * Запертую E не открыть — только сломать уроном (выстрелы, гранаты, удары монстров); пока она цела, навмеш в проёме вырезан.
 * Люди-боты сами открывают незапертые двери на своём пути и закрывают за собой, монстры бьют закрытые, таран колосса сносит.
 * Точка актора — центр проёма. Створки — меш LeafMesh (по умолчанию куб-заглушка), растянутый на проём.
 */
UCLASS(Blueprintable)
class NOFEAR_API ANoFearDoor : public AActor
{
	GENERATED_BODY()

public:
	ANoFearDoor();

	// --- Форма ---

	/** Ширина проёма (см). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Door|Shape", meta = (ClampMin = "20"))
	float Width = 120.f;

	/** Высота проёма (см), у люка — длина. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Door|Shape", meta = (ClampMin = "20"))
	float Height = 220.f;

	/** Толщина створки (см). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Door|Shape", meta = (ClampMin = "1"))
	float Thickness = 8.f;

	/** Две створки, петли по обоим краям. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Door|Shape")
	bool bDoubleDoor = false;

	/** Меш створки: растягивается на 1×1×1 м по своим границам. Пусто — куб-заглушка. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Door|Shape")
	TObjectPtr<UStaticMesh> LeafMesh;

	/** Материал створки. Пусто — заглушка цвета LeafColor (у запертой — LockedColor). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Door|Shape")
	TObjectPtr<UMaterialInterface> LeafMaterial;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Door|Shape")
	FLinearColor LeafColor = FLinearColor(0.32f, 0.2f, 0.1f);

	/** Цвет запертой заглушки: видно, что E её не откроет. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Door|Shape")
	FLinearColor LockedColor = FLinearColor(0.4f, 0.06f, 0.04f);

	// --- Открывание ---

	/** На сколько градусов поворачивается открытая створка. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Door|Open", meta = (ClampMin = "10", ClampMax = "175"))
	float OpenAngle = 100.f;

	/** Сколько секунд открывается и закрывается. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Door|Open", meta = (ClampMin = "0.05"))
	float OpenTime = 0.6f;

	/** Открывается от того, кто открыл. Выключено — всегда в сторону OpenSide. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Door|Open")
	bool bSwingBothWays = true;

	/** Куда открывается, если не в обе стороны: 1 — вдоль оси X актора, -1 — против. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Door|Open", meta = (ClampMin = "-1", ClampMax = "1"))
	int32 OpenSide = 1;

	/** Открыта при старте. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Door|Open")
	bool bStartOpen = false;

	/** Через сколько секунд закрывается сама, если в проёме никого. 0 — не закрывается. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Door|Open", meta = (ClampMin = "0"))
	float AutoCloseDelay = 0.f;

	// --- Замок и прочность ---

	/** Заперта: E не открывает, проход для навигации закрыт. Открыть можно, только сломав. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Door|Lock")
	bool bLocked = false;

	/** Ключ (UNoFearItemData::KeyId): с ним в кейсе E отпирает запертую дверь. Пусто — только сломать. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Door|Lock")
	FName KeyId;

	/** Ключ пропадает из кейса, когда отпер. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Door|Lock")
	bool bConsumeKey = false;

	/** Сколько урона выдерживает (пистолет ~20 за выстрел, граната ~100, удар монстра 15–40). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Door|Lock", meta = (ClampMin = "1"))
	float MaxHealth = 250.f;

	/** Сколько кусков разлетается при поломке. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Door|Lock", meta = (ClampMin = "0", ClampMax = "24"))
	int32 DebrisCount = 6;

	/** Сколько секунд обломки лежат. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Door|Lock", meta = (ClampMin = "0.5"))
	float DebrisLifetime = 8.f;

	// --- Боты ---

	/** Люди-боты сами открывают дверь, когда их путь идёт через проём. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Door|AI")
	bool bBotsOpen = true;

	/** Через сколько секунд дверь, открытую ботом, закрыть за ним (когда проём свободен). 0 — оставить открытой. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Door|AI", meta = (ClampMin = "0"))
	float BotCloseDelay = 3.f;

	/** Монстры бьют закрытую дверь, если она у них на пути. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Door|AI")
	bool bMonstersBreak = true;

	/** На каком расстоянии от проёма бот реагирует на дверь (см). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Door|AI", meta = (ClampMin = "50"))
	float BotReactDistance = 220.f;

	// --- Звук и шум ---

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Door|Sound")
	TObjectPtr<USoundBase> OpenSound;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Door|Sound")
	TObjectPtr<USoundBase> CloseSound;

	/** Дёрнули запертую. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Door|Sound")
	TObjectPtr<USoundBase> LockedSound;

	/** Попадание. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Door|Sound")
	TObjectPtr<USoundBase> HitSound;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Door|Sound")
	TObjectPtr<USoundBase> BreakSound;

	/** Громкость шума для слуха ботов при открывании и закрывании (1 — как шаг, выстрел — около 1.5). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Door|Sound", meta = (ClampMin = "0"))
	float NoiseLoudness = 0.6f;

	/** Дальность шума двери (см); поломка слышна вдвое дальше. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Door|Sound", meta = (ClampMin = "0"))
	float NoiseRange = 900.f;

	// --- События ---

	/** Сменилось состояние: Opening, Open, Closing, Closed, Broken. */
	UPROPERTY(BlueprintAssignable, Category = "NoFear|Door")
	FNoFearDoorStateEvent OnDoorStateChanged;

	UPROPERTY(BlueprintAssignable, Category = "NoFear|Door")
	FNoFearDoorLockEvent OnLockedChanged;

	// --- Действия ---

	/** У User в кейсе ключ от этой двери. */
	UFUNCTION(BlueprintPure, Category = "NoFear|Door")
	bool HasKey(const APawn* User) const;

	/** Отпереть ключом из кейса User и открыть. false — не заперта, нет ключа или сломана. */
	UFUNCTION(BlueprintCallable, Category = "NoFear|Door")
	bool UnlockWithKey(APawn* User);

	/** Открыть от User (он может быть пустым). false — заперта, сломана или уже открыта. */
	UFUNCTION(BlueprintCallable, Category = "NoFear|Door")
	bool OpenDoor(AActor* User);

	/** Закрыть. false — уже закрыта или сломана. */
	UFUNCTION(BlueprintCallable, Category = "NoFear|Door")
	bool CloseDoor(AActor* User = nullptr);

	/** Открыть или закрыть — как по E. */
	UFUNCTION(BlueprintCallable, Category = "NoFear|Door")
	bool ToggleDoor(AActor* User);

	/** Запереть или отпереть. Запирается только закрытая и целая. */
	UFUNCTION(BlueprintCallable, Category = "NoFear|Door")
	void SetLocked(bool bNewLocked);

	/** Сломать сразу: створки разлетаются, проход свободен. */
	UFUNCTION(BlueprintCallable, Category = "NoFear|Door")
	void BreakDoor(AActor* Breaker = nullptr);

	/** Дёрнуть запертую: звук и шум, дверь не открывается. */
	UFUNCTION(BlueprintCallable, Category = "NoFear|Door")
	void RattleLocked(AActor* User);

	UFUNCTION(BlueprintPure, Category = "NoFear|Door")
	ENoFearDoorState GetDoorState() const { return State; }

	/** Открыта полностью или открывается. */
	UFUNCTION(BlueprintPure, Category = "NoFear|Door")
	bool IsOpen() const { return State == ENoFearDoorState::Open || State == ENoFearDoorState::Opening; }

	/** Закрыта и цела: проход перекрыт. */
	UFUNCTION(BlueprintPure, Category = "NoFear|Door")
	bool IsClosed() const { return State == ENoFearDoorState::Closed; }

	UFUNCTION(BlueprintPure, Category = "NoFear|Door")
	bool IsBroken() const { return State == ENoFearDoorState::Broken; }

	UFUNCTION(BlueprintPure, Category = "NoFear|Door")
	bool IsLocked() const { return bLocked && State != ENoFearDoorState::Broken; }

	/** 0 — закрыта, 1 — открыта полностью. */
	UFUNCTION(BlueprintPure, Category = "NoFear|Door")
	float GetOpenAmount() const { return OpenAmount; }

	/** Сколько прочности осталось (0..1). */
	UFUNCTION(BlueprintPure, Category = "NoFear|Door")
	float GetHealthFraction() const;

	/** Сколько раз дверь открывали боты (для тестов). */
	UFUNCTION(BlueprintPure, Category = "NoFear|Door")
	int32 GetBotOpenCount() const { return BotOpenCount; }

	/** Сколько ударов монстров попало по двери (для тестов). */
	UFUNCTION(BlueprintPure, Category = "NoFear|Door")
	int32 GetMonsterAttackCount() const { return MonsterAttackCount; }

	/** Отрезок проходит через проём (в мировых координатах; с запасом по толщине). */
	UFUNCTION(BlueprintPure, Category = "NoFear|Door")
	bool SegmentCrossesDoorway(FVector Start, FVector End) const;

	/** Сторона проёма: 1 — по оси «наружу» рамы, -1 — с другой стороны. */
	UFUNCTION(BlueprintPure, Category = "NoFear|Door")
	int32 GetSideOf(FVector Location) const;

	/** Все двери и люки мира. */
	static void GetDoors(const UWorld* World, TArray<ANoFearDoor*>& OutDoors);

	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void Tick(float DeltaSeconds) override;
	virtual float TakeDamage(float DamageAmount, const FDamageEvent& DamageEvent, AController* EventInstigator, AActor* DamageCauser) override;

	/** Рама: X — нормаль проёма, Y — ширина, Z — высота. У люка повёрнута так, что X смотрит вверх. */
	USceneComponent* GetFrame() const { return Frame; }

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Destroyed() override;

	/** Поворот рамы относительно актора. Дверь — без поворота, люк — плашмя. */
	virtual FRotator GetFrameRotation() const { return FRotator::ZeroRotator; }

	/** Сдвиг рамы от точки актора: люк опущен на толщину створки, чтобы лежал вровень с полом. */
	virtual FVector GetFrameOffset() const { return FVector::ZeroVector; }

	/** Полуразмеры коробки, где навмеш вырезан, пока дверь заперта (в осях актора). */
	virtual FVector GetNavBlockerExtent() const;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Door")
	TObjectPtr<USceneComponent> Root;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Door")
	TObjectPtr<USceneComponent> Frame;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Door")
	TObjectPtr<USceneComponent> LeftHinge;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Door")
	TObjectPtr<USceneComponent> RightHinge;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Door")
	TObjectPtr<UStaticMeshComponent> LeftLeaf;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Door")
	TObjectPtr<UStaticMeshComponent> RightLeaf;

	/** Мишень для обычных ударов монстров: они ищут цели среди пешек. Ни с чем не сталкивается. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Door")
	TObjectPtr<UBoxComponent> HitProxy;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Door")
	TObjectPtr<UNavModifierComponent> NavBlocker;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Door")
	TObjectPtr<UNoFearHealthComponent> Health;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Door")
	TObjectPtr<UNoFearDoorInteraction> Interaction;

private:
	void RefreshShape();
	void RefreshLeafPose();
	void RefreshCollision();
	void RefreshNavigation();
	void SetState(ENoFearDoorState NewState);
	void PlayDoorSound(USoundBase* Sound, float Loudness, float Range, AActor* NoiseSource) const;
	void SpawnDebris(const FVector& Push);
	void UpdateBots();
	bool IsDoorwayClear() const;
	/** Путь бота в ближайшие метры проходит через проём. */
	bool IsPathThroughDoor(const AAIController* AI, const APawn* Pawn) const;
	/** Монстр упёрся в дверь, а его цель за ней. */
	bool IsTargetBehindDoor(const AAIController* AI, const APawn* Pawn) const;

	UFUNCTION()
	void HandleDeath(UNoFearHealthComponent* Component, const FNoFearDamageInfo& Info);

	ENoFearDoorState State = ENoFearDoorState::Closed;
	/** Куда открыта: 1 — в сторону +X рамы, -1 — в сторону -X. */
	float SwingSign = 1.f;
	float OpenAmount = 0.f;
	double LastBotUpdateTime = 0.0;
	double OpenedTime = 0.0;
	bool bOpenedByBot = false;
	bool bBreaking = false;
	int32 BotOpenCount = 0;
	int32 MonsterAttackCount = 0;

	UPROPERTY()
	TObjectPtr<UStaticMesh> PlaceholderMesh;

	UPROPERTY()
	TObjectPtr<UMaterialInterface> PlaceholderMaterial;
};

/**
 * Люк в полу: та же дверь, лежащая плашмя. Створки (по умолчанию две) откидываются вверх на петлях по длинным краям.
 * Точка актора — центр проёма на уровне пола, ось X актора — вдоль длинной стороны.
 */
UCLASS(Blueprintable)
class NOFEAR_API ANoFearHatch : public ANoFearDoor
{
	GENERATED_BODY()

public:
	ANoFearHatch();

	/** На какую глубину под люком вырезать навмеш, пока он заперт (пандус в подвал). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Door|Shape", meta = (ClampMin = "50"))
	float NavBlockDepth = 300.f;

protected:
	virtual FRotator GetFrameRotation() const override;
	virtual FVector GetFrameOffset() const override;
	virtual FVector GetNavBlockerExtent() const override;
};
