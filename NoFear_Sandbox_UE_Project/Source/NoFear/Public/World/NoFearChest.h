#pragma once

#include "CoreMinimal.h"
#include "Core/NoFearInteractableComponent.h"
#include "GameFramework/Actor.h"
#include "NoFearChest.generated.h"

class UMaterialInterface;
class UNoFearHealthComponent;
class UNoFearItemContainerComponent;
class UNoFearItemData;
class USoundBase;
class UStaticMesh;
class UStaticMeshComponent;
struct FNoFearDamageInfo;

/** Что лежит в сундуке. */
USTRUCT(BlueprintType)
struct FNoFearChestItem
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chest")
	TObjectPtr<UNoFearItemData> Item = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chest", meta = (ClampMin = "1"))
	int32 Count = 1;

	/** Патроны в магазине ствола, -1 — полный. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chest")
	int32 MagazineAmmo = -1;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FNoFearChestEvent, ANoFearChest*, Chest);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FNoFearChestUserEvent, ANoFearChest*, Chest, APawn*, User);

/** «[E] Открыть / Забрать всё / Закрыть», у запертого — «Заперто». */
UCLASS(ClassGroup = (NoFear))
class NOFEAR_API UNoFearChestInteraction : public UNoFearInteractableComponent
{
	GENERATED_BODY()

public:
	UNoFearChestInteraction();

	virtual bool CanInteract_Implementation(APawn* User) const override;
	virtual FText GetInteractionPrompt_Implementation(APawn* User) const override;

protected:
	virtual void HandleInteract_Implementation(APawn* User) override;
};

/**
 * Сундук (Docs/Interactables.md): крышка на петлях по заднему краю, внутри — хранилище Storage (сетка инвентаря).
 * E открывает крышку и экран сундука: сетка Storage рядом с кейсом героя, предметы перетаскивают между ними.
 * С bTakeAllOnOpen E вместо экрана перекладывает в кейс всё, что влезает. Пустой открытый сундук E закрывает.
 * Запертый E не открыть — только сбить замок уроном: крышка отлетает, содержимое цело.
 * Точка актора — центр дна, лицевая сторона смотрит по оси X актора.
 */
UCLASS(Blueprintable)
class NOFEAR_API ANoFearChest : public AActor
{
	GENERATED_BODY()

public:
	ANoFearChest();

	// --- Вид ---

	/** Корпус. Центрируется по своим границам, дно — на точке актора. Пусто — куб-заглушка BodySize. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Chest|Shape")
	TObjectPtr<UStaticMesh> BodyMesh;

	/** Крышка: ставится над корпусом с тем же сдвигом, петля — по её заднему нижнему краю. Пусто — плита-заглушка. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Chest|Shape")
	TObjectPtr<UStaticMesh> LidMesh;

	/** Материал вместо родного у мешей. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Chest|Shape")
	TObjectPtr<UMaterialInterface> OverrideMaterial;

	/** Размер заглушки (см), если меша нет. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Chest|Shape")
	FVector BodySize = FVector(46.f, 84.f, 36.f);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Chest|Shape", meta = (ClampMin = "10", ClampMax = "175"))
	float LidOpenAngle = 105.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Chest|Shape", meta = (ClampMin = "0.05"))
	float LidOpenTime = 0.45f;

	/** Цвет запертого (тонирует меш поверх родного материала, если он умеет параметр Color, и заглушку). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Chest|Shape")
	FLinearColor PlaceholderColor = FLinearColor(0.25f, 0.27f, 0.22f);

	// --- Содержимое ---

	/** Что лежит к началу игры: в BeginPlay раскладывается по сетке Storage (что не влезло — теряется с предупреждением). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Chest|Contents")
	TArray<FNoFearChestItem> Contents;

	/** E сразу перекладывает содержимое в кейс. Выключено — E открывает экран сундука. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Chest|Contents")
	bool bTakeAllOnOpen = false;

	/** Закрыть крышку сама, когда сундук опустел и герой отошёл. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Chest|Contents")
	bool bCloseWhenEmptied = false;

	// --- Замок ---

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Chest|Lock")
	bool bLocked = false;

	/** Ключ (UNoFearItemData::KeyId): с ним в кейсе E отпирает замок. Пусто — только сбить. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Chest|Lock")
	FName KeyId;

	/** Ключ пропадает из кейса, когда отпер. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Chest|Lock")
	bool bConsumeKey = false;

	/** Сколько урона выдерживает замок. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Chest|Lock", meta = (ClampMin = "1"))
	float LockHealth = 60.f;

	// --- Звук ---

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Chest|Sound")
	TObjectPtr<USoundBase> OpenSound;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Chest|Sound")
	TObjectPtr<USoundBase> CloseSound;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Chest|Sound")
	TObjectPtr<USoundBase> LockedSound;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Chest|Sound")
	TObjectPtr<USoundBase> HitSound;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Chest|Sound")
	TObjectPtr<USoundBase> BreakSound;

	/** Громкость шума для слуха ботов при открывании (сбитый замок — вдвое громче). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Chest|Sound", meta = (ClampMin = "0"))
	float NoiseLoudness = 0.4f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Chest|Sound", meta = (ClampMin = "0"))
	float NoiseRange = 700.f;

	// --- События ---

	/** Крышка начала открываться (User — кто открыл, может быть пустым). */
	UPROPERTY(BlueprintAssignable, Category = "NoFear|Chest")
	FNoFearChestUserEvent OnOpened;

	UPROPERTY(BlueprintAssignable, Category = "NoFear|Chest")
	FNoFearChestEvent OnClosed;

	UPROPERTY(BlueprintAssignable, Category = "NoFear|Chest")
	FNoFearChestEvent OnContentsChanged;

	// --- Действия ---

	/** Открыть крышку. false — заперт или уже открыт. */
	UFUNCTION(BlueprintCallable, Category = "NoFear|Chest")
	bool OpenChest(APawn* User);

	UFUNCTION(BlueprintCallable, Category = "NoFear|Chest")
	bool CloseChest();

	/** Переложить всё, что влезает, в кейс User. Возвращает, сколько штук переложено. */
	UFUNCTION(BlueprintCallable, Category = "NoFear|Chest")
	int32 TakeAll(APawn* User);

	/** Открыть экран сундука у героя User (сундук должен быть открыт). false — у User нет кейса или экран занят. */
	UFUNCTION(BlueprintCallable, Category = "NoFear|Chest")
	bool OpenScreen(APawn* User);

	UFUNCTION(BlueprintCallable, Category = "NoFear|Chest")
	void SetLocked(bool bNewLocked);

	/** Сбить замок: крышка откидывается, сундук открыт. */
	UFUNCTION(BlueprintCallable, Category = "NoFear|Chest")
	void BreakLock(AActor* Breaker = nullptr);

	/** Отпереть ключом из кейса User (замок цел, сундук остаётся закрытым). false — не заперт или нет ключа. */
	UFUNCTION(BlueprintCallable, Category = "NoFear|Chest")
	bool UnlockWithKey(APawn* User);

	/** Хранилище сундука: класть и забирать предметы — через его API. */
	UFUNCTION(BlueprintPure, Category = "NoFear|Chest")
	UNoFearItemContainerComponent* GetStorage() const { return Storage; }

	/** Что сейчас лежит (из Storage). */
	UFUNCTION(BlueprintPure, Category = "NoFear|Chest")
	TArray<FNoFearChestItem> GetContents() const;

	UFUNCTION(BlueprintPure, Category = "NoFear|Chest")
	bool IsEmpty() const;

	/** Сколько штук всего. */
	UFUNCTION(BlueprintPure, Category = "NoFear|Chest")
	int32 GetItemCount() const;

	UFUNCTION(BlueprintPure, Category = "NoFear|Chest")
	bool IsOpen() const { return bOpen; }

	UFUNCTION(BlueprintPure, Category = "NoFear|Chest")
	bool IsLocked() const { return bLocked; }

	/** 0 — закрыт, 1 — крышка откинута. */
	UFUNCTION(BlueprintPure, Category = "NoFear|Chest")
	float GetLidAmount() const { return LidAmount; }

	/** Все сундуки мира. */
	static void GetChests(const UWorld* World, TArray<ANoFearChest*>& OutChests);

	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void Tick(float DeltaSeconds) override;
	virtual float TakeDamage(float DamageAmount, const FDamageEvent& DamageEvent, AController* EventInstigator, AActor* DamageCauser) override;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Chest")
	TObjectPtr<USceneComponent> Root;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Chest")
	TObjectPtr<UStaticMeshComponent> Body;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Chest")
	TObjectPtr<USceneComponent> LidHinge;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Chest")
	TObjectPtr<UStaticMeshComponent> Lid;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Chest")
	TObjectPtr<UNoFearHealthComponent> Health;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Chest")
	TObjectPtr<UNoFearChestInteraction> Interaction;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Chest")
	TObjectPtr<UNoFearItemContainerComponent> Storage;

private:
	void RefreshShape();
	void RefreshLidPose();
	void PlayChestSound(USoundBase* Sound, float Loudness, float Range, AActor* NoiseSource) const;

	UFUNCTION()
	void HandleStorageChanged(UNoFearItemContainerComponent* Container);

	UFUNCTION()
	void HandleScreenClosed(UNoFearItemContainerComponent* Container);

	UFUNCTION()
	void HandleDeath(UNoFearHealthComponent* Component, const FNoFearDamageInfo& Info);

	bool bOpen = false;
	float LidAmount = 0.f;
	TWeakObjectPtr<APawn> LastUser;

	UPROPERTY()
	TObjectPtr<UStaticMesh> PlaceholderMesh;

	UPROPERTY()
	TObjectPtr<UMaterialInterface> PlaceholderMaterial;
};
