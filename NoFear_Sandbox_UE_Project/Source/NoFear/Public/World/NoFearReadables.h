#pragma once

#include "CoreMinimal.h"
#include "Core/NoFearInteractableComponent.h"
#include "GameFramework/Actor.h"
#include "Subsystems/WorldSubsystem.h"
#include "Tickable.h"
#include "NoFearReadables.generated.h"

class AHUD;
class ANoFearDoor;
class ANoFearNote;
class ANoFearTerminal;
class UCanvas;
class UMaterialInterface;
class UPointLightComponent;
class USoundBase;
class UStaticMeshComponent;

/**
 * Экран чтения (Docs/Interactables.md, «Записки и терминалы»): записка или терминал открывает у героя панель
 * с заголовком и текстом поверх HUD (AHUD::OnHUDPostRender). Закрывается E на том же предмете или если герой
 * отошёл дальше CloseDistance. Ещё хранит пароли, которые герой узнал из записок.
 */
UCLASS()
class NOFEAR_API UNoFearReadingSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintPure, Category = "NoFear|Reading", meta = (WorldContext = "Context", DisplayName = "Get Reading Subsystem"))
	static UNoFearReadingSubsystem* Get(const UObject* Context);

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;

	/** Открыть у Reader панель от Source. */
	void Open(APawn* Reader, AActor* Source, const FText& Title, const FText& Body, float CloseDistance = 300.f);

	UFUNCTION(BlueprintCallable, Category = "NoFear|Reading")
	void Close(const APawn* Reader);

	/** Что читает Reader (nullptr — ничего). */
	UFUNCTION(BlueprintPure, Category = "NoFear|Reading")
	AActor* GetSource(const APawn* Reader) const;

	UFUNCTION(BlueprintPure, Category = "NoFear|Reading")
	FText GetTitle(const APawn* Reader) const;

	UFUNCTION(BlueprintPure, Category = "NoFear|Reading")
	FText GetBody(const APawn* Reader) const;

	/** Пароль CodeId — герой его прочитал. */
	UFUNCTION(BlueprintCallable, Category = "NoFear|Reading")
	void LearnCode(const APawn* Reader, FName CodeId);

	UFUNCTION(BlueprintPure, Category = "NoFear|Reading")
	bool KnowsCode(const APawn* Reader, FName CodeId) const;

private:
	struct FReading
	{
		TWeakObjectPtr<AActor> Source;
		FText Title;
		FText Body;
		float CloseDistance = 300.f;
	};

	void Draw(AHUD* HUD, UCanvas* Canvas);
	const FReading* Find(const APawn* Reader) const;

	TMap<TWeakObjectPtr<const APawn>, FReading> Readings;
	TMap<TWeakObjectPtr<const APawn>, TSet<FName>> Codes;
	FDelegateHandle DrawHandle;
};

/** Читать — подсказка и действие берутся у владельца (записка или терминал). */
UCLASS(ClassGroup = (NoFear))
class NOFEAR_API UNoFearReadableInteraction : public UNoFearInteractableComponent
{
	GENERATED_BODY()

public:
	UNoFearReadableInteraction();

	virtual bool CanInteract_Implementation(APawn* User) const override;
	virtual FText GetInteractionPrompt_Implementation(APawn* User) const override;

protected:
	virtual void HandleInteract_Implementation(APawn* User) override;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FNoFearNoteRead, ANoFearNote*, Note, APawn*, Reader);

/**
 * Записка: листок на столе, стене или полу. E — прочитать (панель с Title и Text), E ещё раз — закрыть.
 * Если задан CodeId, прочитавший узнаёт пароль: терминал с тем же CodeId пустит его.
 */
UCLASS(Blueprintable)
class NOFEAR_API ANoFearNote : public AActor
{
	GENERATED_BODY()

public:
	ANoFearNote();

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Note")
	FText Title;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Note", meta = (MultiLine = true))
	FText Text;

	/** Пароль, который узнаёт прочитавший (пусто — нет). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Note")
	FName CodeId;

	/** Листок висит на стене (вертикально, лицом по +X), иначе лежит. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Note")
	bool bOnWall = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Note")
	FLinearColor Color = FLinearColor(0.85f, 0.82f, 0.7f);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Note|Sound")
	TObjectPtr<USoundBase> ReadSound;

	UPROPERTY(BlueprintAssignable, Category = "NoFear|Note")
	FNoFearNoteRead OnRead;

	UFUNCTION(BlueprintPure, Category = "NoFear|Note")
	bool WasRead() const { return bRead; }

	/** Открыть панель у User. */
	UFUNCTION(BlueprintCallable, Category = "NoFear|Note")
	bool Read(APawn* User);

	bool CanInteractBy(const APawn* User) const { return User != nullptr; }
	FText GetPrompt(const APawn* User) const;
	void InteractBy(APawn* User);

	virtual void OnConstruction(const FTransform& Transform) override;

protected:
	virtual void BeginPlay() override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Note")
	TObjectPtr<USceneComponent> Root;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Note")
	TObjectPtr<UStaticMeshComponent> Sheet;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Note")
	TObjectPtr<UNoFearReadableInteraction> Interaction;

	UPROPERTY()
	TObjectPtr<UMaterialInterface> PlaceholderMaterial;

private:
	void RefreshLook();

	bool bRead = false;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FNoFearTerminalUnlocked, ANoFearTerminal*, Terminal, APawn*, User);

/**
 * Терминал: стол с монитором. Нужна линия питания Circuit (пусто — всегда). Вход — по карте KeyId из кейса, по
 * паролю CodeId из прочитанной записки или взломом (держать E HackTime, пищит — монстры слышат). Открытый
 * показывает журнал Text и отпирает двери Doors (и открывает их, если bOpenDoors).
 */
UCLASS(Blueprintable)
class NOFEAR_API ANoFearTerminal : public AActor
{
	GENERATED_BODY()

public:
	ANoFearTerminal();

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Terminal")
	FText Title;

	/** Журнал на экране после входа. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Terminal", meta = (MultiLine = true))
	FText Text;

	/** Линия питания (UNoFearLightSubsystem). Пусто — питание не нужно. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Terminal")
	FName Circuit;

	/** Карта доступа (предмет-ключ с этим KeyId). Пусто — карта не подходит. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Terminal|Access")
	FName KeyId;

	/** Пароль из записки с тем же CodeId. Пусто — пароль не подходит. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Terminal|Access")
	FName CodeId;

	/** Сколько держать E на взлом. 0 — не взламывается. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Terminal|Access", meta = (ClampMin = "0"))
	float HackTime = 6.f;

	/** Что отпирает вход. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Terminal|Doors")
	TArray<TObjectPtr<ANoFearDoor>> Doors;

	/** Отпертые двери ещё и открываются. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Terminal|Doors")
	bool bOpenDoors = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Terminal|Sound")
	TObjectPtr<USoundBase> BeepSound;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Terminal|Sound")
	TObjectPtr<USoundBase> DeniedSound;

	/** Писк взлома для слуха ботов: раз в секунду. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Terminal|Sound", meta = (ClampMin = "0"))
	float HackNoiseLoudness = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Terminal|Sound", meta = (ClampMin = "0"))
	float HackNoiseRange = 1500.f;

	UPROPERTY(BlueprintAssignable, Category = "NoFear|Terminal")
	FNoFearTerminalUnlocked OnUnlocked;

	UFUNCTION(BlueprintPure, Category = "NoFear|Terminal")
	bool IsPowered() const;

	UFUNCTION(BlueprintPure, Category = "NoFear|Terminal")
	bool IsUnlocked() const { return bUnlocked; }

	/** Прогресс взлома, 0..1. */
	UFUNCTION(BlueprintPure, Category = "NoFear|Terminal")
	float GetHackProgress() const;

	/** User сейчас держит E на взломе. */
	bool IsHackedBy(const APawn* User) const;

	/** Есть ли у User карта или пароль. */
	UFUNCTION(BlueprintPure, Category = "NoFear|Terminal")
	bool HasAccess(const APawn* User) const;

	/** Войти от User (по карте или паролю) и открыть экран. false — нет питания или доступа. */
	UFUNCTION(BlueprintCallable, Category = "NoFear|Terminal")
	bool LogIn(APawn* User);

	/** Взломать сразу (без удержания). false — нет питания или не взламывается. */
	UFUNCTION(BlueprintCallable, Category = "NoFear|Terminal")
	bool Hack(APawn* User);

	bool CanInteractBy(const APawn* User) const { return User != nullptr; }
	FText GetPrompt(const APawn* User) const;
	void InteractBy(APawn* User);

	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void Tick(float DeltaSeconds) override;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Terminal")
	TObjectPtr<USceneComponent> Root;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Terminal")
	TObjectPtr<UStaticMeshComponent> Desk;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Terminal")
	TObjectPtr<UStaticMeshComponent> Monitor;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Terminal")
	TObjectPtr<UStaticMeshComponent> Screen;

	/** Свечение экрана: зелёное — вошли, синее — ждёт, нет — без питания. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Terminal")
	TObjectPtr<UPointLightComponent> ScreenLight;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Terminal")
	TObjectPtr<UNoFearReadableInteraction> Interaction;

	UPROPERTY()
	TObjectPtr<UMaterialInterface> PlaceholderMaterial;

private:
	void Unlock(APawn* User);
	void ShowScreen(APawn* User);
	void RefreshLook();
	void HandleCircuitChanged(FName ChangedCircuit, bool bPowered);

	bool bUnlocked = false;
	int32 UnlockedDoors = 0;
	TWeakObjectPtr<APawn> Hacker;
	float HackElapsed = 0.f;
	float BeepTime = 0.f;
	FDelegateHandle CircuitHandle;
};
