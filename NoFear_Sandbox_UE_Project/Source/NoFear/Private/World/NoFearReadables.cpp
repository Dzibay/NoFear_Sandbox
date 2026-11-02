#include "World/NoFearReadables.h"

#include "CanvasItem.h"
#include "Components/PointLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Core/NoFearHealthComponent.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/HUD.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Inventory/NoFearItemData.h"
#include "Materials/MaterialInterface.h"
#include "Sound/SoundBase.h"
#include "UObject/ConstructorHelpers.h"
#include "World/NoFearDoor.h"
#include "World/NoFearLightSubsystem.h"
#include "World/NoFearWorldLibrary.h"

#define LOCTEXT_NAMESPACE "NoFearReadables"

namespace
{
	constexpr float ReadPanelWidth = 640.f;
	constexpr float ReadPanelPadding = 22.f;

	/** Видно для трассы E и пуль, пешки и навигация не замечают. */
	void MakeReadableVisualOnly(UStaticMeshComponent* Mesh)
	{
		Mesh->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		Mesh->SetCollisionObjectType(ECC_WorldDynamic);
		Mesh->SetCollisionResponseToAllChannels(ECR_Ignore);
		Mesh->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
		Mesh->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
		Mesh->SetCanEverAffectNavigation(false);
		Mesh->SetGenerateOverlapEvents(false);
	}

	UStaticMeshComponent* MakeReadablePart(AActor* Owner, USceneComponent* Parent, FName Name)
	{
		UStaticMeshComponent* Part = Owner->CreateDefaultSubobject<UStaticMeshComponent>(Name);
		Part->SetupAttachment(Parent);
		MakeReadableVisualOnly(Part);
		return Part;
	}

	UStaticMesh* GetReadableCube()
	{
		static UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
		return Cube;
	}

	UNoFearLightSubsystem* GetReadableLightSubsystem(const UObject* Context)
	{
		const UWorld* World = Context ? Context->GetWorld() : nullptr;
		return World ? World->GetSubsystem<UNoFearLightSubsystem>() : nullptr;
	}

	/** Разбить текст на строки не шире Width (по словам; \n — новая строка). */
	void WrapReadableText(UCanvas* Canvas, const UFont* Font, const FString& Text, float Width, TArray<FString>& OutLines)
	{
		TArray<FString> Paragraphs;
		Text.Replace(TEXT("\r"), TEXT("")).ParseIntoArray(Paragraphs, TEXT("\n"), false);
		for (const FString& Paragraph : Paragraphs)
		{
			TArray<FString> Words;
			Paragraph.ParseIntoArrayWS(Words);
			FString Line;
			for (const FString& Word : Words)
			{
				const FString Candidate = Line.IsEmpty() ? Word : Line + TEXT(" ") + Word;
				float XL = 0.f, YL = 0.f;
				Canvas->StrLen(Font, Candidate, XL, YL);
				if (XL > Width && !Line.IsEmpty())
				{
					OutLines.Add(Line);
					Line = Word;
				}
				else
				{
					Line = Candidate;
				}
			}
			OutLines.Add(Line);
		}
	}
}

// ---------------------------------------------------------------------------
// Экран чтения
// ---------------------------------------------------------------------------

UNoFearReadingSubsystem* UNoFearReadingSubsystem::Get(const UObject* Context)
{
	const UWorld* World = Context ? Context->GetWorld() : nullptr;
	return World ? World->GetSubsystem<UNoFearReadingSubsystem>() : nullptr;
}

void UNoFearReadingSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	DrawHandle = AHUD::OnHUDPostRender.AddUObject(this, &UNoFearReadingSubsystem::Draw);
}

void UNoFearReadingSubsystem::Deinitialize()
{
	AHUD::OnHUDPostRender.Remove(DrawHandle);
	Readings.Reset();
	Codes.Reset();
	Super::Deinitialize();
}

TStatId UNoFearReadingSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UNoFearReadingSubsystem, STATGROUP_Tickables);
}

void UNoFearReadingSubsystem::Tick(float DeltaTime)
{
	// Отошёл, упал или предмет исчез — панель закрывается.
	for (auto It = Readings.CreateIterator(); It; ++It)
	{
		const APawn* Reader = It.Key().Get();
		const AActor* Source = It.Value().Source.Get();
		if (!Reader || !Source || UNoFearHealthComponent::IsActorIncapacitated(Reader)
			|| FVector::Dist(Reader->GetActorLocation(), Source->GetActorLocation()) > It.Value().CloseDistance)
		{
			It.RemoveCurrent();
		}
	}
}

void UNoFearReadingSubsystem::Open(APawn* Reader, AActor* Source, const FText& Title, const FText& Body, float CloseDistance)
{
	if (!Reader || !Source)
	{
		return;
	}
	FReading& Reading = Readings.FindOrAdd(Reader);
	Reading.Source = Source;
	Reading.Title = Title;
	Reading.Body = Body;
	Reading.CloseDistance = CloseDistance;
}

void UNoFearReadingSubsystem::Close(const APawn* Reader)
{
	Readings.Remove(Reader);
}

const UNoFearReadingSubsystem::FReading* UNoFearReadingSubsystem::Find(const APawn* Reader) const
{
	const FReading* Reading = Reader ? Readings.Find(Reader) : nullptr;
	return Reading && Reading->Source.IsValid() ? Reading : nullptr;
}

AActor* UNoFearReadingSubsystem::GetSource(const APawn* Reader) const
{
	const FReading* Reading = Find(Reader);
	return Reading ? Reading->Source.Get() : nullptr;
}

FText UNoFearReadingSubsystem::GetTitle(const APawn* Reader) const
{
	const FReading* Reading = Find(Reader);
	return Reading ? Reading->Title : FText::GetEmpty();
}

FText UNoFearReadingSubsystem::GetBody(const APawn* Reader) const
{
	const FReading* Reading = Find(Reader);
	return Reading ? Reading->Body : FText::GetEmpty();
}

void UNoFearReadingSubsystem::LearnCode(const APawn* Reader, FName CodeId)
{
	if (Reader && !CodeId.IsNone())
	{
		Codes.FindOrAdd(Reader).Add(CodeId);
	}
}

bool UNoFearReadingSubsystem::KnowsCode(const APawn* Reader, FName CodeId) const
{
	const TSet<FName>* Known = Reader && !CodeId.IsNone() ? Codes.Find(Reader) : nullptr;
	return Known && Known->Contains(CodeId);
}

void UNoFearReadingSubsystem::Draw(AHUD* HUD, UCanvas* Canvas)
{
	if (!HUD || HUD->GetWorld() != GetWorld())
	{
		return;
	}
	const APawn* Pawn = HUD->GetOwningPawn();
	if (!Canvas || !Pawn || !GEngine)
	{
		return;
	}
	const UFont* Font = GEngine->GetMediumFont();
	const float Scale = FMath::Clamp(Canvas->ClipY / 1080.f, 0.75f, 2.f);

	// Полоса взлома над прицелом.
	for (TActorIterator<ANoFearTerminal> It(GetWorld()); It; ++It)
	{
		const float Progress = It->GetHackProgress();
		if (Progress > 0.f && It->IsHackedBy(Pawn))
		{
			const float W = 260.f * Scale, H = 10.f * Scale;
			const float X = (Canvas->ClipX - W) * 0.5f, Y = Canvas->ClipY * 0.62f;
			FCanvasTileItem Back(FVector2D(X, Y), FVector2D(W, H), FLinearColor(0.f, 0.f, 0.f, 0.6f));
			Back.BlendMode = SE_BLEND_Translucent;
			Canvas->DrawItem(Back);
			FCanvasTileItem Bar(FVector2D(X, Y), FVector2D(W * Progress, H), FLinearColor(0.2f, 0.9f, 0.4f, 0.9f));
			Bar.BlendMode = SE_BLEND_Translucent;
			Canvas->DrawItem(Bar);
			Canvas->SetDrawColor(FColor(140, 255, 160));
			Canvas->DrawText(Font, LOCTEXT("Hacking", "Взлом…").ToString(), X, Y - 22.f * Scale, Scale, Scale);
			break;
		}
	}

	const FReading* Reading = Find(Pawn);
	if (!Reading)
	{
		return;
	}

	const float Width = ReadPanelWidth * Scale;
	const float Pad = ReadPanelPadding * Scale;
	TArray<FString> Lines;
	WrapReadableText(Canvas, Font, Reading->Body.ToString(), (Width - Pad * 2.f) / Scale, Lines);
	float LineXL = 0.f, LineH = 0.f;
	Canvas->StrLen(Font, TEXT("Ag"), LineXL, LineH);
	LineH *= Scale * 1.15f;

	const float Height = Pad * 2.f + LineH * (Lines.Num() + 3);
	const float X = (Canvas->ClipX - Width) * 0.5f;
	const float Y = FMath::Max(20.f, (Canvas->ClipY - Height) * 0.45f);

	FCanvasTileItem Panel(FVector2D(X, Y), FVector2D(Width, Height), FLinearColor(0.02f, 0.02f, 0.02f, 0.85f));
	Panel.BlendMode = SE_BLEND_Translucent;
	Canvas->DrawItem(Panel);
	FCanvasTileItem Line(FVector2D(X + Pad, Y + Pad + LineH * 1.4f), FVector2D(Width - Pad * 2.f, 1.5f * Scale), FLinearColor(0.9f, 0.8f, 0.5f, 0.6f));
	Line.BlendMode = SE_BLEND_Translucent;
	Canvas->DrawItem(Line);

	Canvas->SetDrawColor(FColor(240, 210, 130));
	Canvas->DrawText(Font, Reading->Title.ToString(), X + Pad, Y + Pad, Scale * 1.15f, Scale * 1.15f);
	Canvas->SetDrawColor(FColor(225, 225, 220));
	float LineY = Y + Pad + LineH * 2.f;
	for (const FString& Text : Lines)
	{
		Canvas->DrawText(Font, Text, X + Pad, LineY, Scale, Scale);
		LineY += LineH;
	}
	Canvas->SetDrawColor(FColor(160, 160, 160));
	Canvas->DrawText(Font, LOCTEXT("CloseHint", "[E] Закрыть").ToString(), X + Pad, Y + Height - Pad - LineH * 0.9f, Scale * 0.9f, Scale * 0.9f);
}

// ---------------------------------------------------------------------------
// Взаимодействие
// ---------------------------------------------------------------------------

UNoFearReadableInteraction::UNoFearReadableInteraction()
{
	InteractionDistance = 200.f;
}

bool UNoFearReadableInteraction::CanInteract_Implementation(APawn* User) const
{
	if (!Super::CanInteract_Implementation(User))
	{
		return false;
	}
	if (const ANoFearNote* Note = Cast<ANoFearNote>(GetOwner()))
	{
		return Note->CanInteractBy(User);
	}
	if (const ANoFearTerminal* Terminal = Cast<ANoFearTerminal>(GetOwner()))
	{
		return Terminal->CanInteractBy(User);
	}
	return false;
}

FText UNoFearReadableInteraction::GetInteractionPrompt_Implementation(APawn* User) const
{
	if (const ANoFearNote* Note = Cast<ANoFearNote>(GetOwner()))
	{
		return Note->GetPrompt(User);
	}
	if (const ANoFearTerminal* Terminal = Cast<ANoFearTerminal>(GetOwner()))
	{
		return Terminal->GetPrompt(User);
	}
	return PromptText;
}

void UNoFearReadableInteraction::HandleInteract_Implementation(APawn* User)
{
	if (ANoFearNote* Note = Cast<ANoFearNote>(GetOwner()))
	{
		Note->InteractBy(User);
	}
	else if (ANoFearTerminal* Terminal = Cast<ANoFearTerminal>(GetOwner()))
	{
		Terminal->InteractBy(User);
	}
}

// ---------------------------------------------------------------------------
// Записка
// ---------------------------------------------------------------------------

ANoFearNote::ANoFearNote()
{
	PrimaryActorTick.bCanEverTick = false;

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = Root;
	Sheet = MakeReadablePart(this, Root, TEXT("Sheet"));
	Interaction = CreateDefaultSubobject<UNoFearReadableInteraction>(TEXT("Interaction"));

	static ConstructorHelpers::FObjectFinderOptional<UMaterialInterface> ShapeMaterial(TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	static ConstructorHelpers::FObjectFinderOptional<USoundBase> RustleFinder(TEXT("/Game/External/LyraContent/Audio/Sounds/Weapons/Shotgun/Weapons_Shotgun_HandHit_01.Weapons_Shotgun_HandHit_01"));
	PlaceholderMaterial = ShapeMaterial.Get();
	ReadSound = RustleFinder.Get();

	Title = LOCTEXT("NoteTitle", "Записка");
}

void ANoFearNote::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	RefreshLook();
}

void ANoFearNote::BeginPlay()
{
	Super::BeginPlay();
	RefreshLook();
}

void ANoFearNote::RefreshLook()
{
	// Листок A4: лежит на опоре (низ в нуле) или висит на стене лицом по +X.
	const FVector Size = bOnWall ? FVector(0.6f, 21.f, 30.f) : FVector(21.f, 30.f, 0.6f);
	const FVector Center = bOnWall ? FVector(0.3f, 0.f, 0.f) : FVector(0.f, 0.f, 0.3f);
	NoFearWorld::FitMesh(Sheet, GetReadableCube(), Size, Center);
	NoFearWorld::TintPlaceholder(Sheet, PlaceholderMaterial, bRead ? Color * 0.75f : Color);
}

FText ANoFearNote::GetPrompt(const APawn* User) const
{
	const UNoFearReadingSubsystem* Reading = UNoFearReadingSubsystem::Get(this);
	if (Reading && Reading->GetSource(User) == this)
	{
		return LOCTEXT("CloseNote", "Закрыть");
	}
	return FText::Format(LOCTEXT("ReadNote", "Читать: {0}"), Title);
}

void ANoFearNote::InteractBy(APawn* User)
{
	UNoFearReadingSubsystem* Reading = UNoFearReadingSubsystem::Get(this);
	if (Reading && Reading->GetSource(User) == this)
	{
		Reading->Close(User);
		return;
	}
	Read(User);
}

bool ANoFearNote::Read(APawn* User)
{
	UNoFearReadingSubsystem* Reading = UNoFearReadingSubsystem::Get(this);
	if (!User || !Reading)
	{
		return false;
	}
	Reading->Open(User, this, Title, Text, Interaction->InteractionDistance + 100.f);
	NoFearWorld::PlaySoundAndNoise(this, ReadSound, GetActorLocation(), 0.f, 0.f, User, NAME_None, 0.35f);
	if (!CodeId.IsNone() && !Reading->KnowsCode(User, CodeId))
	{
		Reading->LearnCode(User, CodeId);
		NoFearWorld::Notify(User, LOCTEXT("CodeLearned", "Запомнил пароль"));
	}
	if (!bRead)
	{
		bRead = true;
		RefreshLook();
	}
	OnRead.Broadcast(this, User);
	return true;
}

// ---------------------------------------------------------------------------
// Терминал
// ---------------------------------------------------------------------------

ANoFearTerminal::ANoFearTerminal()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = Root;

	// Стол держит пешек и пули, монитор и экран только для глаз и трассы E.
	Desk = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Desk"));
	Desk->SetupAttachment(Root);
	Desk->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
	Desk->SetCanEverAffectNavigation(true);
	Monitor = MakeReadablePart(this, Root, TEXT("Monitor"));
	Screen = MakeReadablePart(this, Root, TEXT("Screen"));
	Screen->CastShadow = false;

	ScreenLight = CreateDefaultSubobject<UPointLightComponent>(TEXT("ScreenLight"));
	ScreenLight->SetupAttachment(Root);
	ScreenLight->SetMobility(EComponentMobility::Movable);
	ScreenLight->SetIntensity(400.f);
	ScreenLight->SetAttenuationRadius(250.f);
	ScreenLight->SetCastShadows(false);

	Interaction = CreateDefaultSubobject<UNoFearReadableInteraction>(TEXT("Interaction"));

	static ConstructorHelpers::FObjectFinderOptional<UMaterialInterface> ShapeMaterial(TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	static ConstructorHelpers::FObjectFinderOptional<USoundBase> BeepFinder(TEXT("/Game/External/LyraContent/Audio/Sounds/Weapons/Shotgun/Weapons_Shotgun_HandHit_01.Weapons_Shotgun_HandHit_01"));
	PlaceholderMaterial = ShapeMaterial.Get();
	BeepSound = BeepFinder.Get();
	DeniedSound = BeepFinder.Get();

	Title = LOCTEXT("TerminalTitle", "Терминал");
}

void ANoFearTerminal::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	RefreshLook();
}

void ANoFearTerminal::BeginPlay()
{
	Super::BeginPlay();
	if (UNoFearLightSubsystem* Subsystem = GetReadableLightSubsystem(this))
	{
		CircuitHandle = Subsystem->OnCircuitChanged.AddUObject(this, &ANoFearTerminal::HandleCircuitChanged);
	}
	RefreshLook();
}

void ANoFearTerminal::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UNoFearLightSubsystem* Subsystem = GetReadableLightSubsystem(this))
	{
		Subsystem->OnCircuitChanged.Remove(CircuitHandle);
	}
	Super::EndPlay(EndPlayReason);
}

void ANoFearTerminal::RefreshLook()
{
	// Стол 120×60×75 вдоль Y, монитор на дальнем краю, экран смотрит на +X (к тому, кто сидит).
	UStaticMesh* Cube = GetReadableCube();
	NoFearWorld::FitMesh(Desk, Cube, FVector(60.f, 120.f, 75.f), FVector(0.f, 0.f, 37.5f));
	NoFearWorld::FitMesh(Monitor, Cube, FVector(12.f, 55.f, 38.f), FVector(-15.f, 0.f, 75.f + 21.f));
	NoFearWorld::FitMesh(Screen, Cube, FVector(1.f, 48.f, 31.f), FVector(-8.5f, 0.f, 75.f + 21.f));
	NoFearWorld::TintPlaceholder(Desk, PlaceholderMaterial, FLinearColor(0.25f, 0.25f, 0.27f));
	NoFearWorld::TintPlaceholder(Monitor, PlaceholderMaterial, FLinearColor(0.06f, 0.06f, 0.07f));

	const bool bPowered = IsPowered();
	const FLinearColor ScreenColor = !bPowered ? FLinearColor(0.02f, 0.02f, 0.02f)
		: bUnlocked ? FLinearColor(0.15f, 0.9f, 0.3f) : FLinearColor(0.15f, 0.4f, 0.9f);
	NoFearWorld::TintPlaceholder(Screen, PlaceholderMaterial, ScreenColor);
	ScreenLight->SetRelativeLocation(FVector(30.f, 0.f, 75.f + 21.f));
	ScreenLight->SetLightColor(ScreenColor);
	ScreenLight->SetVisibility(bPowered);
}

bool ANoFearTerminal::IsPowered() const
{
	const UNoFearLightSubsystem* Subsystem = GetReadableLightSubsystem(this);
	return Subsystem ? Subsystem->IsCircuitPowered(Circuit) : Circuit.IsNone();
}

void ANoFearTerminal::HandleCircuitChanged(FName ChangedCircuit, bool bPowered)
{
	if (ChangedCircuit != Circuit)
	{
		return;
	}
	if (!bPowered)
	{
		// Экран погас: взлом сорвался, журнал закрылся.
		Hacker.Reset();
		HackElapsed = 0.f;
		if (UNoFearReadingSubsystem* Reading = UNoFearReadingSubsystem::Get(this))
		{
			for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
			{
				const APawn* Pawn = It->IsValid() ? (*It)->GetPawn() : nullptr;
				if (Reading->GetSource(Pawn) == this)
				{
					Reading->Close(Pawn);
				}
			}
		}
	}
	RefreshLook();
}

float ANoFearTerminal::GetHackProgress() const
{
	return Hacker.IsValid() ? FMath::Clamp(HackElapsed / FMath::Max(HackTime, 0.01f), 0.f, 1.f) : 0.f;
}

bool ANoFearTerminal::HasAccess(const APawn* User) const
{
	if (!User)
	{
		return false;
	}
	if (NoFearWorld::FindKey(User, KeyId))
	{
		return true;
	}
	const UNoFearReadingSubsystem* Reading = UNoFearReadingSubsystem::Get(this);
	return Reading && Reading->KnowsCode(User, CodeId);
}

bool ANoFearTerminal::LogIn(APawn* User)
{
	if (!User || !IsPowered())
	{
		NoFearWorld::Notify(User, LOCTEXT("NoPower", "Нет питания"));
		return false;
	}
	if (!bUnlocked && !HasAccess(User))
	{
		NoFearWorld::PlaySoundAndNoise(this, DeniedSound, GetActorLocation(), 0.f, 0.f, User, NAME_None, 0.5f);
		NoFearWorld::Notify(User, LOCTEXT("Denied", "Нет доступа"));
		return false;
	}
	if (!bUnlocked)
	{
		Unlock(User);
	}
	ShowScreen(User);
	return true;
}

bool ANoFearTerminal::Hack(APawn* User)
{
	if (!User || !IsPowered() || HackTime <= 0.f)
	{
		return false;
	}
	Hacker.Reset();
	HackElapsed = 0.f;
	if (!bUnlocked)
	{
		Unlock(User);
	}
	ShowScreen(User);
	return true;
}

void ANoFearTerminal::Unlock(APawn* User)
{
	bUnlocked = true;
	UnlockedDoors = 0;
	for (ANoFearDoor* Door : Doors)
	{
		if (!Door)
		{
			continue;
		}
		Door->SetLocked(false);
		if (bOpenDoors)
		{
			Door->OpenDoor(User);
		}
		++UnlockedDoors;
	}
	NoFearWorld::PlaySoundAndNoise(this, BeepSound, GetActorLocation(), 0.f, 0.f, User, NAME_None, 0.6f);
	NoFearWorld::Notify(User, UnlockedDoors > 0 ? LOCTEXT("UnlockedDoors", "Доступ открыт — двери отперты") : LOCTEXT("Unlocked", "Доступ открыт"));
	RefreshLook();
	OnUnlocked.Broadcast(this, User);
}

void ANoFearTerminal::ShowScreen(APawn* User)
{
	if (UNoFearReadingSubsystem* Reading = UNoFearReadingSubsystem::Get(this))
	{
		Reading->Open(User, this, Title, Text, Interaction->InteractionDistance + 100.f);
	}
}

bool ANoFearTerminal::IsHackedBy(const APawn* User) const
{
	return User && Hacker.Get() == User;
}

FText ANoFearTerminal::GetPrompt(const APawn* User) const
{
	const UNoFearReadingSubsystem* Reading = UNoFearReadingSubsystem::Get(this);
	if (Reading && Reading->GetSource(User) == this)
	{
		return LOCTEXT("CloseScreen", "Закрыть");
	}
	if (!IsPowered())
	{
		return LOCTEXT("NoPowerPrompt", "Терминал (нет питания)");
	}
	if (bUnlocked)
	{
		return LOCTEXT("ReadLog", "Читать журнал");
	}
	if (const UNoFearItemData* Card = NoFearWorld::FindKey(User, KeyId))
	{
		return FText::Format(LOCTEXT("LogInCard", "Войти: {0}"), Card->GetDisplayName());
	}
	if (Reading && Reading->KnowsCode(User, CodeId))
	{
		return LOCTEXT("LogInCode", "Ввести пароль");
	}
	if (HackTime > 0.f)
	{
		return LOCTEXT("HackPrompt", "Взломать (держать)");
	}
	return LOCTEXT("LockedPrompt", "Терминал (нет доступа)");
}

void ANoFearTerminal::InteractBy(APawn* User)
{
	UNoFearReadingSubsystem* Reading = UNoFearReadingSubsystem::Get(this);
	if (Reading && Reading->GetSource(User) == this)
	{
		Reading->Close(User);
		return;
	}
	if (!IsPowered() || bUnlocked || HasAccess(User) || HackTime <= 0.f)
	{
		LogIn(User); // Войдёт или скажет, почему нет.
		return;
	}
	// Бот или вызов из кода — сразу; игрок держит E.
	if (!NoFearWorld::IsInteractHeld(User))
	{
		Hack(User);
		return;
	}
	Hacker = User;
	HackElapsed = 0.f;
	BeepTime = 0.f;
	SetActorTickEnabled(true);
}

void ANoFearTerminal::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	APawn* User = Hacker.Get();
	if (!User)
	{
		SetActorTickEnabled(false);
		return;
	}
	if (bUnlocked || !IsPowered() || !NoFearWorld::IsInteractHeld(User) || !Interaction->IsInRange(User) || UNoFearHealthComponent::IsActorIncapacitated(User))
	{
		Hacker.Reset();
		HackElapsed = 0.f;
		SetActorTickEnabled(false);
		return;
	}
	// Пищит раз в секунду — шум от героя, монстры идут к нему.
	BeepTime -= DeltaSeconds;
	if (BeepTime <= 0.f)
	{
		BeepTime = 1.f;
		NoFearWorld::PlaySoundAndNoise(this, BeepSound, GetActorLocation(), HackNoiseLoudness, HackNoiseRange, User, TEXT("Terminal"), 0.4f);
	}
	HackElapsed += DeltaSeconds;
	if (HackElapsed >= HackTime)
	{
		Hack(User);
		SetActorTickEnabled(false);
	}
}

#undef LOCTEXT_NAMESPACE
