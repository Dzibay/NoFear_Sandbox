#include "World/NoFearHideSpot.h"

#include "AIController.h"
#include "Camera/CameraComponent.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Core/NoFearHealthComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "Sound/SoundBase.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"
#include "Weapons/NoFearWeaponComponent.h"
#include "World/NoFearWorldLibrary.h"

#define LOCTEXT_NAMESPACE "NoFearHideSpot"

namespace
{
	TArray<TWeakObjectPtr<ANoFearHideSpot>> GHideSpots;

	constexpr int32 SlatCount = 7;
	constexpr float WallThickness = 4.f;
	constexpr float DoorThickness = 3.f;
	/** Доля высоты дверцы, занятая рейками; остальное — щели. */
	constexpr float SlatFill = 0.62f;
	/** Насколько выше точки ног смотрит камера внутри. */
	constexpr float EyeHeight = 165.f;
	/** Пауза между тем, как дверца распахнулась при обыске, и рывком сидящего наружу. */
	constexpr float SearchPullDelay = 0.4f;
	constexpr float SearchHoldTime = 1.5f;
}

// ---------------------------------------------------------------------------
// Взаимодействие
// ---------------------------------------------------------------------------

UNoFearHideInteraction::UNoFearHideInteraction()
{
	InteractionDistance = 180.f;
	PromptText = LOCTEXT("Hide", "Спрятаться");
}

bool UNoFearHideInteraction::CanInteract_Implementation(APawn* User) const
{
	const ANoFearHideSpot* Spot = Cast<ANoFearHideSpot>(GetOwner());
	if (!Super::CanInteract_Implementation(User) || !Spot || Spot->IsBeingSearched())
	{
		return false;
	}
	// Изнутри — только выйти; снаружи — только в пустой шкаф и только спереди.
	if (Spot->GetOccupant())
	{
		return Spot->GetOccupant() == User;
	}
	if (ANoFearHideSpot::IsActorHidden(User))
	{
		return false;
	}
	const FVector Local = Spot->GetActorTransform().InverseTransformPosition(User->GetActorLocation());
	return Local.X > 0.f;
}

FText UNoFearHideInteraction::GetInteractionPrompt_Implementation(APawn* User) const
{
	const ANoFearHideSpot* Spot = Cast<ANoFearHideSpot>(GetOwner());
	return Spot && Spot->GetOccupant() == User ? LOCTEXT("Leave", "Выйти") : PromptText;
}

void UNoFearHideInteraction::HandleInteract_Implementation(APawn* User)
{
	ANoFearHideSpot* Spot = Cast<ANoFearHideSpot>(GetOwner());
	if (!Spot)
	{
		return;
	}
	if (Spot->GetOccupant() == User)
	{
		Spot->ExitBy(User);
	}
	else
	{
		Spot->EnterBy(User);
	}
}

// ---------------------------------------------------------------------------
// Шкаф
// ---------------------------------------------------------------------------

ANoFearHideSpot::ANoFearHideSpot()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = Root;

	static const TCHAR* WallNames[] = { TEXT("BackWall"), TEXT("LeftWall"), TEXT("RightWall"), TEXT("Roof") };
	for (const TCHAR* Name : WallNames)
	{
		UStaticMeshComponent* Wall = CreateDefaultSubobject<UStaticMeshComponent>(Name);
		Wall->SetupAttachment(Root);
		Wall->SetCollisionProfileName(TEXT("BlockAll"));
		Wall->SetGenerateOverlapEvents(false);
		Walls.Add(Wall);
	}

	Hinge = CreateDefaultSubobject<USceneComponent>(TEXT("Hinge"));
	Hinge->SetupAttachment(Root);

	for (int32 Index = 0; Index < SlatCount; ++Index)
	{
		UStaticMeshComponent* Slat = CreateDefaultSubobject<UStaticMeshComponent>(*FString::Printf(TEXT("Slat%d"), Index));
		Slat->SetupAttachment(Hinge);
		Slat->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Slat->SetCanEverAffectNavigation(false);
		Slat->SetGenerateOverlapEvents(false);
		DoorSlats.Add(Slat);
	}

	DoorBlocker = CreateDefaultSubobject<UBoxComponent>(TEXT("DoorBlocker"));
	DoorBlocker->SetupAttachment(Hinge);
	DoorBlocker->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	DoorBlocker->SetCollisionObjectType(ECC_WorldDynamic);
	DoorBlocker->SetCollisionResponseToAllChannels(ECR_Block);
	DoorBlocker->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
	DoorBlocker->SetCanEverAffectNavigation(false);
	DoorBlocker->SetGenerateOverlapEvents(false);
	DoorBlocker->SetHiddenInGame(true);

	InsideCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("InsideCamera"));
	InsideCamera->SetupAttachment(Root);
	InsideCamera->FieldOfView = 80.f;
	InsideCamera->bAutoActivate = true;

	Interaction = CreateDefaultSubobject<UNoFearHideInteraction>(TEXT("Interaction"));

	static ConstructorHelpers::FObjectFinderOptional<UStaticMesh> CubeMesh(TEXT("/Engine/BasicShapes/Cube.Cube"));
	static ConstructorHelpers::FObjectFinderOptional<UMaterialInterface> ShapeMaterial(TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	PlaceholderMesh = CubeMesh.Get();
	PlaceholderMaterial = ShapeMaterial.Get();

	static ConstructorHelpers::FObjectFinderOptional<USoundBase> OpenFinder(TEXT("/Game/External/LyraContent/Audio/Sounds/Weapons/Pistol/Weapons_Pistol_Slide_01.Weapons_Pistol_Slide_01"));
	static ConstructorHelpers::FObjectFinderOptional<USoundBase> CloseFinder(TEXT("/Game/External/LyraContent/Audio/Sounds/Weapons/Shotgun/Weapons_Shotgun_HandHit_01.Weapons_Shotgun_HandHit_01"));
	OpenSound = OpenFinder.Get();
	CloseSound = CloseFinder.Get();

	RefreshShape();
}

void ANoFearHideSpot::GetSpots(const UWorld* World, TArray<ANoFearHideSpot*>& OutSpots)
{
	OutSpots.Reset();
	for (const TWeakObjectPtr<ANoFearHideSpot>& Entry : GHideSpots)
	{
		if (ANoFearHideSpot* Spot = Entry.Get(); Spot && Spot->GetWorld() == World)
		{
			OutSpots.Add(Spot);
		}
	}
}

void ANoFearHideSpot::FindSpotsNear(const UWorld* World, const FVector& Location, float Radius, TArray<ANoFearHideSpot*>& OutSpots)
{
	GetSpots(World, OutSpots);
	OutSpots.RemoveAll([&Location, Radius](const ANoFearHideSpot* Spot)
	{
		return FVector::DistSquared(Spot->GetSearchLocation(), Location) > FMath::Square(Radius);
	});
	OutSpots.Sort([&Location](const ANoFearHideSpot& A, const ANoFearHideSpot& B)
	{
		return FVector::DistSquared(A.GetSearchLocation(), Location) < FVector::DistSquared(B.GetSearchLocation(), Location);
	});
}

ANoFearHideSpot* ANoFearHideSpot::GetSpotOf(const AActor* Actor)
{
	if (!Actor)
	{
		return nullptr;
	}
	for (const TWeakObjectPtr<ANoFearHideSpot>& Entry : GHideSpots)
	{
		if (ANoFearHideSpot* Spot = Entry.Get(); Spot && Spot->Occupant.Get() == Actor)
		{
			return Spot;
		}
	}
	return nullptr;
}

void ANoFearHideSpot::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	RefreshShape();
}

void ANoFearHideSpot::BeginPlay()
{
	Super::BeginPlay();
	GHideSpots.AddUnique(this);
	RefreshShape();
}

void ANoFearHideSpot::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (APawn* Pawn = Occupant.Get(); Pawn && EndPlayReason == EEndPlayReason::Destroyed)
	{
		ExitBy(Pawn, true);
	}
	ReleaseStun();
	GHideSpots.RemoveAll([this](const TWeakObjectPtr<ANoFearHideSpot>& Entry)
	{
		return !Entry.IsValid() || Entry.Get() == this;
	});
	Super::EndPlay(EndPlayReason);
}

void ANoFearHideSpot::Destroyed()
{
	if (APawn* Pawn = Occupant.Get())
	{
		ExitBy(Pawn, true);
	}
	Super::Destroyed();
}

void ANoFearHideSpot::RefreshShape()
{
	const float Width = FMath::Max(Size.Y, 40.f);
	const float Depth = FMath::Max(Size.X, 40.f);
	const float Height = FMath::Max(Size.Z, 100.f);

	// Корпус. Дверца — по +X, петли слева (−Y).
	if (BodyMesh)
	{
		NoFearWorld::FitMesh(Walls[0], BodyMesh, FVector(Depth, Width, Height), FVector(0.f, 0.f, Height * 0.5f));
		for (int32 Index = 1; Index < Walls.Num(); ++Index)
		{
			Walls[Index]->SetStaticMesh(nullptr);
			Walls[Index]->SetVisibility(false);
			Walls[Index]->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		}
	}
	else
	{
		const FVector Sizes[] = {
			FVector(WallThickness, Width, Height),
			FVector(Depth, WallThickness, Height),
			FVector(Depth, WallThickness, Height),
			FVector(Depth, Width, WallThickness) };
		const FVector Centers[] = {
			FVector(-Depth * 0.5f + WallThickness * 0.5f, 0.f, Height * 0.5f),
			FVector(0.f, -Width * 0.5f + WallThickness * 0.5f, Height * 0.5f),
			FVector(0.f, Width * 0.5f - WallThickness * 0.5f, Height * 0.5f),
			FVector(0.f, 0.f, Height - WallThickness * 0.5f) };
		for (int32 Index = 0; Index < Walls.Num(); ++Index)
		{
			NoFearWorld::FitMesh(Walls[Index], PlaceholderMesh, Sizes[Index], Centers[Index]);
			Walls[Index]->SetVisibility(true);
			Walls[Index]->SetCollisionProfileName(TEXT("BlockAll"));
			NoFearWorld::TintPlaceholder(Walls[Index], PlaceholderMaterial, Color);
		}
	}

	// Дверца: петля в левом переднем углу, полотно уходит вдоль +Y.
	Hinge->SetRelativeLocation(FVector(Depth * 0.5f - DoorThickness * 0.5f, -Width * 0.5f, 0.f));
	const float DoorHeight = Height - WallThickness;
	const FLinearColor DoorColor = Color * 1.25f;
	if (DoorMesh)
	{
		NoFearWorld::FitMesh(DoorSlats[0], DoorMesh, FVector(DoorThickness, Width, DoorHeight), FVector(0.f, Width * 0.5f, DoorHeight * 0.5f));
		DoorSlats[0]->SetVisibility(true);
		for (int32 Index = 1; Index < DoorSlats.Num(); ++Index)
		{
			DoorSlats[Index]->SetVisibility(false);
		}
	}
	else
	{
		const float Pitch = DoorHeight / DoorSlats.Num();
		for (int32 Index = 0; Index < DoorSlats.Num(); ++Index)
		{
			NoFearWorld::FitMesh(DoorSlats[Index], PlaceholderMesh, FVector(DoorThickness, Width, Pitch * SlatFill), FVector(0.f, Width * 0.5f, Pitch * (Index + 0.5f)));
			DoorSlats[Index]->SetVisibility(true);
			NoFearWorld::TintPlaceholder(DoorSlats[Index], PlaceholderMaterial, DoorColor);
		}
	}
	DoorBlocker->SetBoxExtent(FVector(DoorThickness * 0.5f, Width * 0.5f, DoorHeight * 0.5f));
	DoorBlocker->SetRelativeLocation(FVector(0.f, Width * 0.5f, DoorHeight * 0.5f));

	// Камера стоит у задней стенки, чтобы точка шкафа (центр пола) была перед ней: так подсказка «Выйти» остаётся в фокусе.
	InsideCamera->SetRelativeLocationAndRotation(FVector(-Depth * 0.5f + WallThickness + 12.f, 0.f, FMath::Min(EyeHeight, Height - 20.f)), FRotator::ZeroRotator);
	RefreshDoorPose();
}

void ANoFearHideSpot::RefreshDoorPose()
{
	Hinge->SetRelativeRotation(FRotator(0.f, -OpenAngle * FMath::InterpEaseInOut(0.f, 1.f, OpenAmount, 2.f), 0.f));
	// Распахнутая дверца не толкает пешек, но заслоняет то, что за ней.
	DoorBlocker->SetCollisionResponseToChannel(ECC_Pawn, OpenAmount > 0.f ? ECR_Ignore : ECR_Block);
}

FVector ANoFearHideSpot::GetSearchLocation() const
{
	return GetActorTransform().TransformPosition(FVector(FMath::Max(Size.X, 40.f) * 0.5f + 70.f, 0.f, 0.f));
}

FVector ANoFearHideSpot::GetInsideLocation(const APawn* Pawn) const
{
	const ACharacter* Character = Cast<ACharacter>(Pawn);
	const float HalfHeight = Character ? Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() : 90.f;
	return GetActorTransform().TransformPosition(FVector(-5.f, 0.f, HalfHeight + 2.f));
}

bool ANoFearHideSpot::WasHeardRecently() const
{
	const UWorld* World = GetWorld();
	return World && World->GetTimeSeconds() - LastNoiseTime < RecentNoiseTime;
}

bool ANoFearHideSpot::EnterBy(APawn* Pawn)
{
	if (!Pawn || Occupant.IsValid() || bSearching || IsActorHidden(Pawn) || UNoFearHealthComponent::IsActorIncapacitated(Pawn))
	{
		return false;
	}
	Occupant = Pawn;

	if (ACharacter* Character = Cast<ACharacter>(Pawn))
	{
		Character->GetCharacterMovement()->StopMovementImmediately();
		Character->GetCharacterMovement()->DisableMovement();
	}
	if (AAIController* AI = Cast<AAIController>(Pawn->GetController()))
	{
		AI->StopMovement();
	}
	Pawn->SetActorEnableCollision(false);
	Pawn->SetActorLocationAndRotation(GetInsideLocation(Pawn), GetActorRotation(), false, nullptr, ETeleportType::TeleportPhysics);
	Pawn->SetActorHiddenInGame(true);

	HiddenAttached.Reset();
	TArray<AActor*> Attached;
	Pawn->GetAttachedActors(Attached, true, true);
	for (AActor* Actor : Attached)
	{
		if (Actor && !Actor->IsHidden())
		{
			Actor->SetActorHiddenInGame(true);
			HiddenAttached.Add(Actor);
		}
	}

	bOccupantIsPlayer = false;
	if (APlayerController* PC = Cast<APlayerController>(Pawn->GetController()))
	{
		bOccupantIsPlayer = true;
		if (UNoFearWeaponComponent* Weapon = Pawn->FindComponentByClass<UNoFearWeaponComponent>())
		{
			Weapon->StopFire();
		}
		// Оружие и движение — на вводе пешки; E живёт на контроллере, поэтому выйти можно.
		Pawn->DisableInput(PC);
		PC->SetControlRotation(GetActorRotation());
		PC->SetViewTargetWithBlend(this, 0.2f);
	}

	OpenDoorFor(0.15f);
	MakeHideNoise(1.f, Pawn);
	OnEntered.Broadcast(this, Pawn, false);
	return true;
}

bool ANoFearHideSpot::ExitBy(APawn* Pawn, bool bForced)
{
	if (!Pawn || Occupant.Get() != Pawn)
	{
		return false;
	}
	Occupant.Reset();

	const FVector Forward = GetActorForwardVector();
	const ACharacter* Character = Cast<ACharacter>(Pawn);
	const float HalfHeight = Character ? Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() : 90.f;
	const FVector OutLocation = GetSearchLocation() - Forward * 15.f + FVector(0.f, 0.f, HalfHeight + 2.f);
	Pawn->SetActorLocationAndRotation(OutLocation, GetActorRotation(), false, nullptr, ETeleportType::TeleportPhysics);
	Pawn->SetActorHiddenInGame(false);
	Pawn->SetActorEnableCollision(true);
	for (const TWeakObjectPtr<AActor>& Entry : HiddenAttached)
	{
		if (AActor* Actor = Entry.Get())
		{
			Actor->SetActorHiddenInGame(false);
		}
	}
	HiddenAttached.Reset();

	if (ACharacter* MovingCharacter = Cast<ACharacter>(Pawn))
	{
		MovingCharacter->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
		if (bForced)
		{
			MovingCharacter->LaunchCharacter(Forward * 350.f + FVector(0.f, 0.f, 150.f), true, true);
		}
	}

	if (APlayerController* PC = Cast<APlayerController>(Pawn->GetController()))
	{
		Pawn->EnableInput(PC);
		PC->SetControlRotation(GetActorRotation());
		PC->SetViewTargetWithBlend(Pawn, 0.2f);
		if (bForced && ForcedStunTime > 0.f)
		{
			ReleaseStun();
			PC->SetIgnoreMoveInput(true);
			StunnedPawn = Pawn;
			GetWorldTimerManager().SetTimer(StunTimer, this, &ANoFearHideSpot::ReleaseStun, ForcedStunTime, false);
			NoFearWorld::Notify(Pawn, LOCTEXT("Dragged", "Тебя вытащили из шкафа!"));
		}
	}
	bOccupantIsPlayer = false;

	OpenDoorFor(bForced ? SearchHoldTime : 0.3f);
	if (!bForced)
	{
		MakeHideNoise(1.f, Pawn);
	}
	OnExited.Broadcast(this, Pawn, bForced);
	return true;
}

bool ANoFearHideSpot::SearchBy(APawn* Searcher)
{
	if (!Searcher || bSearching || UNoFearHealthComponent::IsActorIncapacitated(Searcher)
		|| FVector::Dist2D(Searcher->GetActorLocation(), GetSearchLocation()) > SearchReach)
	{
		return false;
	}
	bSearching = true;
	CurrentSearcher = Searcher;
	++SearchCount;
	OpenDoorFor(SearchHoldTime);
	MakeHideNoise(2.f, Searcher);
	OnSearched.Broadcast(this, Searcher);

	GetWorldTimerManager().SetTimer(SearchTimer, FTimerDelegate::CreateWeakLambda(this, [this]()
	{
		if (APawn* Pawn = Occupant.Get())
		{
			ExitBy(Pawn, true);
			// Выброшенного слышно далеко: это шум жертвы, на него сбегаются.
			NoFearWorld::PlaySoundAndNoise(this, nullptr, GetSearchLocation(), NoiseLoudness * 2.f, NoiseRange * 2.f, Pawn, TEXT("HideSpot"));
		}
	}), SearchPullDelay, false);
	return true;
}

void ANoFearHideSpot::OpenDoorFor(float HoldTime)
{
	const double Now = GetWorld()->GetTimeSeconds();
	if (OpenAmount <= 0.f && HoldOpenUntil < Now)
	{
		NoFearWorld::PlaySoundAndNoise(this, OpenSound, GetSearchLocation(), 0.f, 0.f, nullptr, NAME_None, 0.6f);
	}
	HoldOpenUntil = FMath::Max(HoldOpenUntil, Now + OpenTime + HoldTime);
}

void ANoFearHideSpot::MakeHideNoise(float LoudnessScale, AActor* NoiseInstigator)
{
	LastNoiseTime = GetWorld()->GetTimeSeconds();
	NoFearWorld::PlaySoundAndNoise(this, nullptr, GetSearchLocation(), NoiseLoudness * LoudnessScale, NoiseRange * LoudnessScale, NoiseInstigator, TEXT("HideSpot"));
}

void ANoFearHideSpot::ReleaseStun()
{
	GetWorldTimerManager().ClearTimer(StunTimer);
	if (APawn* Pawn = StunnedPawn.Get())
	{
		if (APlayerController* PC = Cast<APlayerController>(Pawn->GetController()))
		{
			PC->SetIgnoreMoveInput(false);
		}
	}
	StunnedPawn.Reset();
}

void ANoFearHideSpot::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	const double Now = GetWorld()->GetTimeSeconds();
	const float Target = Now < HoldOpenUntil ? 1.f : 0.f;
	if (!FMath::IsNearlyEqual(OpenAmount, Target))
	{
		const bool bWasOpen = OpenAmount > 0.f;
		OpenAmount = FMath::FInterpConstantTo(OpenAmount, Target, DeltaSeconds, 1.f / FMath::Max(OpenTime, 0.05f));
		if (bWasOpen && OpenAmount <= 0.f)
		{
			NoFearWorld::PlaySoundAndNoise(this, CloseSound, GetSearchLocation(), 0.f, 0.f, nullptr, NAME_None, 0.5f);
		}
		RefreshDoorPose();
	}
	if (bSearching && OpenAmount <= 0.f && Target <= 0.f)
	{
		bSearching = false;
		CurrentSearcher.Reset();
	}

	// Сидящий умер или его убрали — освободить шкаф.
	if (APawn* Pawn = Occupant.Get(); Pawn && UNoFearHealthComponent::IsActorDead(Pawn))
	{
		ExitBy(Pawn, true);
	}
	else if (Occupant.IsStale())
	{
		Occupant.Reset();
	}
}

#undef LOCTEXT_NAMESPACE
