#include "World/NoFearTraversal.h"

#include "AIController.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Core/NoFearHealthComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "Materials/MaterialInterface.h"
#include "Navigation/PathFollowingComponent.h"
#include "NavLinkCustomComponent.h"
#include "Sound/SoundBase.h"
#include "UObject/ConstructorHelpers.h"
#include "World/NoFearLightSubsystem.h"
#include "World/NoFearWorldLibrary.h"

#define LOCTEXT_NAMESPACE "NoFearTraversal"

namespace
{
	constexpr int32 MaxLadderRungs = 20;
	constexpr float LadderRungStep = 30.f;

	/** Видно для трассы E и пуль, пешки и навигация не замечают. */
	void MakeTraversalVisualOnly(UStaticMeshComponent* Mesh)
	{
		Mesh->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		Mesh->SetCollisionObjectType(ECC_WorldDynamic);
		Mesh->SetCollisionResponseToAllChannels(ECR_Ignore);
		Mesh->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
		Mesh->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
		Mesh->SetCanEverAffectNavigation(false);
		Mesh->SetGenerateOverlapEvents(false);
	}

	UStaticMeshComponent* MakeTraversalPart(AActor* Owner, USceneComponent* Parent, FName Name)
	{
		UStaticMeshComponent* Part = Owner->CreateDefaultSubobject<UStaticMeshComponent>(Name);
		Part->SetupAttachment(Parent);
		MakeTraversalVisualOnly(Part);
		return Part;
	}

	UNoFearLightSubsystem* GetTraversalLightSubsystem(const UObject* Context)
	{
		const UWorld* World = Context ? Context->GetWorld() : nullptr;
		return World ? World->GetSubsystem<UNoFearLightSubsystem>() : nullptr;
	}

	FVector TraversalFeetOf(const APawn* Pawn)
	{
		const ACharacter* Character = Cast<ACharacter>(Pawn);
		const float HalfHeight = Character && Character->GetCapsuleComponent() ? Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() : 0.f;
		return Pawn->GetActorLocation() - FVector(0.f, 0.f, HalfHeight);
	}
}

// ---------------------------------------------------------------------------
// Ручка и взаимодействие
// ---------------------------------------------------------------------------

UNoFearTraversalInteraction::UNoFearTraversalInteraction()
{
	InteractionDistance = 200.f;
}

bool UNoFearTraversalInteraction::CanInteract_Implementation(APawn* User) const
{
	const ANoFearTraversalHandle* Handle = Cast<ANoFearTraversalHandle>(GetOwner());
	const ANoFearTraversal* Traversal = Handle ? Handle->GetTraversal() : nullptr;
	return Super::CanInteract_Implementation(User) && Traversal && Traversal->CanUseFrom(Handle, User);
}

FText UNoFearTraversalInteraction::GetInteractionPrompt_Implementation(APawn* User) const
{
	const ANoFearTraversalHandle* Handle = Cast<ANoFearTraversalHandle>(GetOwner());
	const ANoFearTraversal* Traversal = Handle ? Handle->GetTraversal() : nullptr;
	return Traversal ? Traversal->GetPromptFrom(Handle, User) : PromptText;
}

void UNoFearTraversalInteraction::HandleInteract_Implementation(APawn* User)
{
	ANoFearTraversalHandle* Handle = Cast<ANoFearTraversalHandle>(GetOwner());
	if (ANoFearTraversal* Traversal = Handle ? Handle->GetTraversal() : nullptr)
	{
		Traversal->UseFrom(Handle, User);
	}
}

ANoFearTraversalHandle::ANoFearTraversalHandle()
{
	PrimaryActorTick.bCanEverTick = false;
	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = Root;
	Mesh = MakeTraversalPart(this, Root, TEXT("Mesh"));
	Mesh->SetVisibility(false);
	Interaction = CreateDefaultSubobject<UNoFearTraversalInteraction>(TEXT("Interaction"));
}

ANoFearTraversal* ANoFearTraversalHandle::GetTraversal() const
{
	return Cast<ANoFearTraversal>(GetOwner());
}

void ANoFearTraversalHandle::SetLook(const FVector& Size, const FLinearColor& Color, UMaterialInterface* PlaceholderMaterial)
{
	static UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (Size.IsNearlyZero())
	{
		Mesh->SetVisibility(false);
		return;
	}
	NoFearWorld::FitMesh(Mesh, Cube, Size, FVector::ZeroVector);
	NoFearWorld::TintPlaceholder(Mesh, PlaceholderMaterial, Color);
	Mesh->SetVisibility(true);
}

// ---------------------------------------------------------------------------
// Основа
// ---------------------------------------------------------------------------

ANoFearTraversal::ANoFearTraversal()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = Root;

	static ConstructorHelpers::FObjectFinderOptional<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
	static ConstructorHelpers::FObjectFinderOptional<UMaterialInterface> ShapeMaterial(TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	static ConstructorHelpers::FObjectFinderOptional<USoundBase> ClankFinder(TEXT("/Game/External/LyraContent/Audio/Sounds/Weapons/Shotgun/Weapons_Shotgun_HandHit_01.Weapons_Shotgun_HandHit_01"));
	CubeMesh = Cube.Get();
	PlaceholderMaterial = ShapeMaterial.Get();
	Sound = ClankFinder.Get();
}

void ANoFearTraversal::BeginPlay()
{
	Super::BeginPlay();
	TArray<FVector> Spots;
	GetHandleSpots(Spots);
	for (int32 Index = 0; Index < Spots.Num(); ++Index)
	{
		SpawnHandle(Spots[Index], Index);
	}
}

void ANoFearTraversal::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	for (FMove& Move : Moves)
	{
		EndMove(Move, false);
	}
	Moves.Reset();
	for (ANoFearTraversalHandle* Handle : Handles)
	{
		if (IsValid(Handle))
		{
			Handle->Destroy();
		}
	}
	Handles.Reset();
	Super::EndPlay(EndPlayReason);
}

ANoFearTraversalHandle* ANoFearTraversal::SpawnHandle(const FVector& LocalSpot, int32 Index)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return nullptr;
	}
	FActorSpawnParameters Params;
	Params.Owner = this;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ANoFearTraversalHandle* Handle = World->SpawnActor<ANoFearTraversalHandle>(ANoFearTraversalHandle::StaticClass(), GetActorTransform().TransformPosition(LocalSpot), GetActorRotation(), Params);
	if (!Handle)
	{
		return nullptr;
	}
	Handle->Index = Index;
	Handle->GetInteraction()->InteractionDistance = GetHandleReach();
	Handle->AttachToComponent(Root, FAttachmentTransformRules::KeepWorldTransform);
	Handles.Add(Handle);
	return Handle;
}

void ANoFearTraversal::SetupLink()
{
	if (Link)
	{
		Link->SetMoveReachedLink(this, &ANoFearTraversal::HandleLinkReached);
	}
}

bool ANoFearTraversal::IsTraversing(const APawn* Pawn) const
{
	return Pawn && Moves.ContainsByPredicate([Pawn](const FMove& Move) { return Move.Pawn.Get() == Pawn; });
}

ANoFearTraversal* ANoFearTraversal::GetTraversalOf(const APawn* Pawn)
{
	UWorld* World = Pawn ? Pawn->GetWorld() : nullptr;
	if (!World)
	{
		return nullptr;
	}
	for (TActorIterator<ANoFearTraversal> It(World); It; ++It)
	{
		if (It->IsTraversing(Pawn))
		{
			return *It;
		}
	}
	return nullptr;
}

bool ANoFearTraversal::CanUseFrom(const ANoFearTraversalHandle* Handle, const APawn* User) const
{
	TArray<FVector> Feet;
	float Yaw = 0.f;
	return User && !GetTraversalOf(User) && !UNoFearHealthComponent::IsActorIncapacitated(User) && BuildPath(User, Feet, Yaw);
}

FText ANoFearTraversal::GetPromptFrom(const ANoFearTraversalHandle* Handle, const APawn* User) const
{
	return FText::GetEmpty();
}

bool ANoFearTraversal::UseFrom(ANoFearTraversalHandle* Handle, APawn* User)
{
	TArray<FVector> Feet;
	float Yaw = 0.f;
	if (!CanUseFrom(Handle, User) || !BuildPath(User, Feet, Yaw))
	{
		return false;
	}
	return BeginTraverse(User, Feet, Yaw, nullptr);
}

bool ANoFearTraversal::Use(APawn* User)
{
	for (ANoFearTraversalHandle* Handle : Handles)
	{
		if (IsValid(Handle) && CanUseFrom(Handle, User))
		{
			return UseFrom(Handle, User);
		}
	}
	return false;
}

void ANoFearTraversal::HandleLinkReached(UNavLinkCustomComponent* ThisComp, UObject* PathComp, const FVector& DestPoint)
{
	UPathFollowingComponent* PathFollowing = Cast<UPathFollowingComponent>(PathComp);
	const AAIController* AI = PathFollowing ? Cast<AAIController>(PathFollowing->GetOwner()) : nullptr;
	APawn* Pawn = AI ? AI->GetPawn() : nullptr;
	TArray<FVector> Feet;
	float Yaw = 0.f;
	if (!Pawn || GetTraversalOf(Pawn) || UNoFearHealthComponent::IsActorIncapacitated(Pawn) || !BuildPath(Pawn, Feet, Yaw) || !BeginTraverse(Pawn, Feet, Yaw, PathComp))
	{
		// Пройти не вышло — отпускаем ссылку, путь перестроится.
		if (PathFollowing)
		{
			PathFollowing->FinishUsingCustomLink(Link);
		}
	}
}

bool ANoFearTraversal::BeginTraverse(APawn* Pawn, const TArray<FVector>& Feet, float Yaw, UObject* PathComp)
{
	ACharacter* Character = Cast<ACharacter>(Pawn);
	if (!Character || Feet.Num() < 2)
	{
		return false;
	}
	const float HalfHeight = Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	FMove& Move = Moves.AddDefaulted_GetRef();
	Move.Pawn = Pawn;
	Move.PathComp = PathComp;
	Move.Yaw = Yaw;
	// Первая точка — где пешка стоит сейчас: подтягивается к началу ломаной, а не прыгает.
	Move.Points.Add(Pawn->GetActorLocation());
	for (const FVector& Foot : Feet)
	{
		Move.Points.Add(Foot + FVector(0.f, 0.f, HalfHeight));
	}

	UCharacterMovementComponent* Movement = Character->GetCharacterMovement();
	Movement->StopMovementImmediately();
	Movement->DisableMovement();
	if (APlayerController* PC = Cast<APlayerController>(Pawn->GetController()))
	{
		PC->SetIgnoreMoveInput(true);
	}
	Pawn->SetActorRotation(FRotator(0.f, Yaw, 0.f));
	NoFearWorld::PlaySoundAndNoise(this, Sound, Pawn->GetActorLocation(), NoiseLoudness, NoiseRange, Pawn, TEXT("Traverse"));
	OnTraverse.Broadcast(this, Pawn, false);
	SetActorTickEnabled(true);
	return true;
}

void ANoFearTraversal::EndMove(FMove& Move, bool bFinished)
{
	APawn* Pawn = Move.Pawn.Get();
	if (ACharacter* Character = Cast<ACharacter>(Pawn))
	{
		// Сбит с лестницы — падает; дошёл — снова ходит.
		Character->GetCharacterMovement()->SetMovementMode(bFinished ? MOVE_Walking : MOVE_Falling);
	}
	if (APlayerController* PC = Pawn ? Cast<APlayerController>(Pawn->GetController()) : nullptr)
	{
		PC->SetIgnoreMoveInput(false);
	}
	if (UPathFollowingComponent* PathFollowing = Cast<UPathFollowingComponent>(Move.PathComp.Get()))
	{
		PathFollowing->FinishUsingCustomLink(Link);
	}
	Move.Pawn.Reset();
	Move.PathComp.Reset();
	if (Pawn)
	{
		OnTraverse.Broadcast(this, Pawn, bFinished);
	}
}

void ANoFearTraversal::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	for (int32 Index = Moves.Num() - 1; Index >= 0; --Index)
	{
		FMove& Move = Moves[Index];
		APawn* Pawn = Move.Pawn.Get();
		const ACharacter* Character = Cast<ACharacter>(Pawn);
		if (!Character || UNoFearHealthComponent::IsActorIncapacitated(Pawn) || Character->GetCharacterMovement()->MovementMode != MOVE_None)
		{
			EndMove(Move, false);
			Moves.RemoveAt(Index);
			continue;
		}
		float Step = TraverseSpeed * DeltaSeconds;
		FVector Location = Pawn->GetActorLocation();
		while (Step > 0.f && Move.Next < Move.Points.Num())
		{
			const FVector Target = Move.Points[Move.Next];
			const float Left = FVector::Dist(Location, Target);
			if (Left <= Step)
			{
				Location = Target;
				Step -= Left;
				++Move.Next;
			}
			else
			{
				Location += (Target - Location) / Left * Step;
				Step = 0.f;
			}
		}
		Pawn->SetActorLocationAndRotation(Location, FRotator(0.f, Move.Yaw, 0.f), false, nullptr, ETeleportType::TeleportPhysics);
		if (Move.Next >= Move.Points.Num())
		{
			EndMove(Move, true);
			Moves.RemoveAt(Index);
		}
	}
	if (Moves.IsEmpty() && !IsA<ANoFearLift>())
	{
		SetActorTickEnabled(false);
	}
}

// ---------------------------------------------------------------------------
// Лестница
// ---------------------------------------------------------------------------

ANoFearLadder::ANoFearLadder()
{
	TraverseSpeed = 130.f;
	NoiseLoudness = 0.35f;
	NoiseRange = 700.f;

	RailL = MakeTraversalPart(this, Root, TEXT("RailL"));
	RailR = MakeTraversalPart(this, Root, TEXT("RailR"));
	for (int32 Index = 0; Index < MaxLadderRungs; ++Index)
	{
		Rungs.Add(MakeTraversalPart(this, Root, *FString::Printf(TEXT("Rung%02d"), Index)));
	}

	Link = CreateDefaultSubobject<UNavLinkCustomComponent>(TEXT("Link"));
	PlaceParts();
}

void ANoFearLadder::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	PlaceParts();
}

void ANoFearLadder::PlaceParts()
{
	const float Top = Height + 100.f;
	// Тетивы торчат над площадкой на метр — за них берутся, вылезая.
	for (UStaticMeshComponent* Rail : { RailL.Get(), RailR.Get() })
	{
		const float Side = Rail == RailL ? -Width * 0.5f : Width * 0.5f;
		NoFearWorld::FitMesh(Rail, CubeMesh, FVector(6.f, 5.f, Top), FVector(6.f, Side, Top * 0.5f));
		NoFearWorld::TintPlaceholder(Rail, PlaceholderMaterial, Color);
	}
	for (int32 Index = 0; Index < Rungs.Num(); ++Index)
	{
		const float Z = LadderRungStep * (Index + 1);
		UStaticMeshComponent* Rung = Rungs[Index];
		const bool bUsed = Z < Height;
		Rung->SetVisibility(bUsed);
		Rung->SetCollisionEnabled(bUsed ? ECollisionEnabled::QueryOnly : ECollisionEnabled::NoCollision);
		NoFearWorld::FitMesh(Rung, CubeMesh, FVector(4.f, Width, 4.f), FVector(6.f, 0.f, Z));
		NoFearWorld::TintPlaceholder(Rung, PlaceholderMaterial, Color * 0.8f);
	}
	// Концы ссылки — на навмеше: внизу перед лестницей, наверху на площадке.
	Link->SetLinkData(FVector(StandOff + 35.f, 0.f, 0.f), FVector(-TopInset - 15.f, 0.f, Height), ENavLinkDirection::BothWays);
}

void ANoFearLadder::BeginPlay()
{
	Super::BeginPlay();
	SetupLink();
}

FVector ANoFearLadder::GetBottomFoot() const
{
	return GetActorTransform().TransformPosition(FVector(StandOff, 0.f, 0.f));
}

FVector ANoFearLadder::GetTopFoot() const
{
	return GetActorTransform().TransformPosition(FVector(-TopInset, 0.f, Height));
}

void ANoFearLadder::GetHandleSpots(TArray<FVector>& OutSpots) const
{
	// Низ — на уровне груди перед лестницей, верх — у края площадки.
	OutSpots.Add(FVector(10.f, 0.f, 110.f));
	OutSpots.Add(FVector(-20.f, 0.f, Height + 60.f));
}

bool ANoFearLadder::IsAtTop(const APawn* User) const
{
	const FVector Feet = TraversalFeetOf(User);
	return FVector::DistSquared(Feet, GetTopFoot()) < FVector::DistSquared(Feet, GetBottomFoot());
}

bool ANoFearLadder::BuildPath(const APawn* User, TArray<FVector>& OutFeet, float& OutYaw) const
{
	if (!User)
	{
		return false;
	}
	const bool bTop = IsAtTop(User);
	const FVector Entry = bTop ? GetTopFoot() : GetBottomFoot();
	const FVector Feet = TraversalFeetOf(User);
	if (FVector::DistSquared2D(Feet, Entry) > FMath::Square(EntryReach) || FMath::Abs(Feet.Z - Entry.Z) > 80.f)
	{
		return false;
	}
	const FTransform& Xf = GetActorTransform();
	const FVector OnLadderBottom = Xf.TransformPosition(FVector(StandOff * 0.6f, 0.f, 0.f));
	const FVector OnLadderTop = Xf.TransformPosition(FVector(StandOff * 0.6f, 0.f, Height + 15.f));
	const FVector OverEdge = Xf.TransformPosition(FVector(-20.f, 0.f, Height + 15.f));
	if (bTop)
	{
		OutFeet = { GetTopFoot(), OverEdge, OnLadderTop, OnLadderBottom, GetBottomFoot() };
	}
	else
	{
		OutFeet = { GetBottomFoot(), OnLadderBottom, OnLadderTop, OverEdge, GetTopFoot() };
	}
	// Лицом к лестнице (по −X).
	OutYaw = GetActorRotation().Yaw + 180.f;
	return true;
}

bool ANoFearLadder::CanUseFrom(const ANoFearTraversalHandle* Handle, const APawn* User) const
{
	// Каждая ручка — только для своего конца.
	return Super::CanUseFrom(Handle, User) && (!Handle || (Handle->Index == 1) == IsAtTop(User));
}

FText ANoFearLadder::GetPromptFrom(const ANoFearTraversalHandle* Handle, const APawn* User) const
{
	return User && IsAtTop(User) ? LOCTEXT("ClimbDown", "Спуститься по лестнице") : LOCTEXT("ClimbUp", "Подняться по лестнице");
}

// ---------------------------------------------------------------------------
// Перелаз
// ---------------------------------------------------------------------------

ANoFearVault::ANoFearVault()
{
	TraverseSpeed = 280.f;
	NoiseLoudness = 0.3f;
	NoiseRange = 600.f;
	Color = FLinearColor(0.32f, 0.24f, 0.16f);

	Wall = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Wall"));
	Wall->SetupAttachment(Root);
	Wall->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
	Wall->SetCanEverAffectNavigation(true);

	Link = CreateDefaultSubobject<UNavLinkCustomComponent>(TEXT("Link"));
	PlaceParts();
}

void ANoFearVault::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	PlaceParts();
}

void ANoFearVault::PlaceParts()
{
	NoFearWorld::FitMesh(Wall, CubeMesh, FVector(Thickness, Length, WallHeight), FVector(0.f, 0.f, WallHeight * 0.5f));
	NoFearWorld::TintPlaceholder(Wall, PlaceholderMaterial, Color);
	const float End = Thickness * 0.5f + Approach + 40.f;
	Link->SetLinkData(FVector(-End, 0.f, 0.f), FVector(End, 0.f, 0.f), ENavLinkDirection::BothWays);
}

void ANoFearVault::BeginPlay()
{
	Super::BeginPlay();
	SetupLink();
}

void ANoFearVault::GetHandleSpots(TArray<FVector>& OutSpots) const
{
	OutSpots.Add(FVector(0.f, 0.f, WallHeight * 0.7f));
}

float ANoFearVault::GetHandleReach() const
{
	return FVector2D(Reach + 50.f, Length * 0.5f).Size();
}

bool ANoFearVault::BuildPath(const APawn* User, TArray<FVector>& OutFeet, float& OutYaw) const
{
	if (!User)
	{
		return false;
	}
	const FTransform& Xf = GetActorTransform();
	const FVector Local = Xf.InverseTransformPosition(TraversalFeetOf(User));
	const float HalfLength = Length * 0.5f;
	if (FMath::Abs(Local.X) > Reach || FMath::Abs(Local.Y) > HalfLength || FMath::Abs(Local.Z) > 80.f)
	{
		return false;
	}
	const float Side = Local.X >= 0.f ? 1.f : -1.f;
	const float Y = FMath::Clamp(Local.Y, -HalfLength + 40.f, HalfLength - 40.f);
	const float Out = Thickness * 0.5f + Approach;
	const float Over = WallHeight + 15.f;
	OutFeet = {
		Xf.TransformPosition(FVector(Side * Out, Y, 0.f)),
		Xf.TransformPosition(FVector(Side * Thickness, Y, Over)),
		Xf.TransformPosition(FVector(-Side * Thickness, Y, Over)),
		Xf.TransformPosition(FVector(-Side * Out, Y, 0.f)),
	};
	// Лицом туда, куда лезет.
	OutYaw = GetActorRotation().Yaw + (Side > 0.f ? 180.f : 0.f);
	return true;
}

bool ANoFearVault::CanUseFrom(const ANoFearTraversalHandle* Handle, const APawn* User) const
{
	return Super::CanUseFrom(Handle, User);
}

FText ANoFearVault::GetPromptFrom(const ANoFearTraversalHandle* Handle, const APawn* User) const
{
	return LOCTEXT("Vault", "Перелезть");
}

// ---------------------------------------------------------------------------
// Лифт
// ---------------------------------------------------------------------------

ANoFearLift::ANoFearLift()
{
	PrimaryActorTick.bStartWithTickEnabled = true;
	TraverseSpeed = 120.f;
	NoiseLoudness = 0.4f;
	NoiseRange = 1200.f;
	Color = FLinearColor(0.3f, 0.32f, 0.34f);
	Sound = nullptr;

	Root->SetMobility(EComponentMobility::Movable);
	Platform = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Platform"));
	Platform->SetupAttachment(Root);
	Platform->SetMobility(EComponentMobility::Movable);
	Platform->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
	// Платформа едет — навмеш под ней не режем; боты лифтом не пользуются.
	Platform->SetCanEverAffectNavigation(false);
	Post = MakeTraversalPart(this, Root, TEXT("Post"));
	Post->SetMobility(EComponentMobility::Movable);
	PlaceParts();
}

void ANoFearLift::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	PlaceParts();
}

void ANoFearLift::PlaceParts()
{
	NoFearWorld::FitMesh(Platform, CubeMesh, PlatformSize, FVector(0.f, 0.f, -PlatformSize.Z * 0.5f));
	NoFearWorld::TintPlaceholder(Platform, PlaceholderMaterial, Color);
	const FVector Spot = GetPostSpot();
	NoFearWorld::FitMesh(Post, CubeMesh, FVector(8.f, 8.f, Spot.Z), FVector(Spot.X, Spot.Y, Spot.Z * 0.5f));
	NoFearWorld::TintPlaceholder(Post, PlaceholderMaterial, Color * 0.6f);
}

FVector ANoFearLift::GetPostSpot() const
{
	return FVector(PlatformSize.X * 0.5f - 12.f, PlatformSize.Y * 0.5f - 12.f, 100.f);
}

void ANoFearLift::BeginPlay()
{
	Super::BeginPlay();
	BaseZ = GetActorLocation().Z;
	CurrentFloor = 0;
	if (Handles.Num() > 0)
	{
		Handles[0]->SetLook(FVector(16.f, 16.f, 22.f), FLinearColor(0.6f, 0.5f, 0.1f), PlaceholderMaterial);
	}
}

void ANoFearLift::GetHandleSpots(TArray<FVector>& OutSpots) const
{
	OutSpots.Add(GetPostSpot() + FVector(0.f, 0.f, 10.f));
}

bool ANoFearLift::IsPowered() const
{
	const UNoFearLightSubsystem* Subsystem = GetTraversalLightSubsystem(this);
	return Circuit.IsNone() || (Subsystem && Subsystem->IsCircuitPowered(Circuit));
}

float ANoFearLift::GetFloorZ(int32 Floor) const
{
	return BaseZ + (Floors.IsValidIndex(Floor) ? Floors[Floor] : 0.f);
}

bool ANoFearLift::MoveToFloor(int32 Floor, APawn* User)
{
	if (!Floors.IsValidIndex(Floor) || IsMoving() || !IsPowered() || Floor == CurrentFloor)
	{
		return false;
	}
	TargetFloor = Floor;
	Rider = User;
	HumTime = 0.f;
	NoFearWorld::PlaySoundAndNoise(this, Sound, GetActorLocation(), NoiseLoudness, NoiseRange, User, TEXT("Lift"));
	return true;
}

bool ANoFearLift::CanUseFrom(const ANoFearTraversalHandle* Handle, const APawn* User) const
{
	return User && !IsMoving() && Floors.Num() > 1;
}

FText ANoFearLift::GetPromptFrom(const ANoFearTraversalHandle* Handle, const APawn* User) const
{
	if (!IsPowered())
	{
		return LOCTEXT("NoPower", "Лифт (нет питания)");
	}
	const int32 Next = (CurrentFloor + 1) % FMath::Max(Floors.Num(), 1);
	return Floors.IsValidIndex(Next) && Floors[Next] > Floors[CurrentFloor] ? LOCTEXT("Up", "Лифт: вверх") : LOCTEXT("Down", "Лифт: вниз");
}

bool ANoFearLift::UseFrom(ANoFearTraversalHandle* Handle, APawn* User)
{
	if (!CanUseFrom(Handle, User))
	{
		return false;
	}
	if (!IsPowered())
	{
		NoFearWorld::Notify(User, LOCTEXT("NoPowerNote", "Нет питания — заведи генератор"));
		return false;
	}
	return MoveToFloor((CurrentFloor + 1) % Floors.Num(), User);
}

void ANoFearLift::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!IsMoving() || !IsPowered())
	{
		// Без питания стоит, где застал обрыв; вернётся питание — поедет дальше.
		return;
	}
	const float GoalZ = GetFloorZ(TargetFloor);
	FVector Location = GetActorLocation();
	const float Step = Speed * DeltaSeconds;
	const bool bArrived = FMath::Abs(GoalZ - Location.Z) <= Step;
	Location.Z = bArrived ? GoalZ : Location.Z + FMath::Sign(GoalZ - Location.Z) * Step;
	SetActorLocation(Location);

	HumTime += DeltaSeconds;
	if (HumTime >= HumInterval)
	{
		HumTime = 0.f;
		NoFearWorld::PlaySoundAndNoise(this, nullptr, Location, NoiseLoudness, NoiseRange, Rider.Get(), TEXT("Lift"));
	}
	if (bArrived)
	{
		CurrentFloor = TargetFloor;
		TargetFloor = INDEX_NONE;
		Rider.Reset();
		NoFearWorld::PlaySoundAndNoise(this, Sound, Location, NoiseLoudness, NoiseRange, nullptr, TEXT("Lift"));
		OnArrived.Broadcast(this, CurrentFloor);
	}
}

// ---------------------------------------------------------------------------
// Кнопка вызова
// ---------------------------------------------------------------------------

UNoFearLiftButtonInteraction::UNoFearLiftButtonInteraction()
{
	InteractionDistance = 180.f;
}

bool UNoFearLiftButtonInteraction::CanInteract_Implementation(APawn* User) const
{
	const ANoFearLiftButton* Button = Cast<ANoFearLiftButton>(GetOwner());
	return Super::CanInteract_Implementation(User) && Button && Button->CanCall();
}

FText UNoFearLiftButtonInteraction::GetInteractionPrompt_Implementation(APawn* User) const
{
	const ANoFearLiftButton* Button = Cast<ANoFearLiftButton>(GetOwner());
	return Button ? Button->GetPrompt() : PromptText;
}

void UNoFearLiftButtonInteraction::HandleInteract_Implementation(APawn* User)
{
	if (ANoFearLiftButton* Button = Cast<ANoFearLiftButton>(GetOwner()))
	{
		Button->Call(User);
	}
}

ANoFearLiftButton::ANoFearLiftButton()
{
	PrimaryActorTick.bCanEverTick = false;
	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = Root;

	static ConstructorHelpers::FObjectFinderOptional<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
	Box = MakeTraversalPart(this, Root, TEXT("Box"));
	// Столбик до пояса с коробкой кнопки наверху; точка актора — у пола.
	NoFearWorld::FitMesh(Box, Cube.Get(), FVector(14.f, 14.f, 115.f), FVector(0.f, 0.f, 57.5f));
	Interaction = CreateDefaultSubobject<UNoFearLiftButtonInteraction>(TEXT("Interaction"));
}

bool ANoFearLiftButton::CanCall() const
{
	// Лифт уже здесь и стоит — звать нечего.
	return Lift && !Lift->IsMoving() && Lift->GetCurrentFloor() != Floor;
}

FText ANoFearLiftButton::GetPrompt() const
{
	return Lift && !Lift->IsPowered() ? LOCTEXT("CallNoPower", "Вызвать лифт (нет питания)") : LOCTEXT("Call", "Вызвать лифт");
}

bool ANoFearLiftButton::Call(APawn* User)
{
	if (!CanCall())
	{
		return false;
	}
	if (!Lift->IsPowered())
	{
		NoFearWorld::Notify(User, LOCTEXT("NoPowerNote", "Нет питания — заведи генератор"));
		return false;
	}
	return Lift->MoveToFloor(Floor, User);
}

#undef LOCTEXT_NAMESPACE
