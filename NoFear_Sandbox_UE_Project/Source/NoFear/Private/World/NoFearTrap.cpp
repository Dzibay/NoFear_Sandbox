#include "World/NoFearTrap.h"

#include "AIController.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Core/NoFearHealthComponent.h"
#include "Core/NoFearTeam.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/DamageType.h"
#include "Inventory/NoFearInventoryComponent.h"
#include "Inventory/NoFearItemData.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundBase.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"
#include "Weapons/NoFearGrenadeData.h"
#include "World/NoFearHideSpot.h"
#include "World/NoFearWorldLibrary.h"

#define LOCTEXT_NAMESPACE "NoFearTrap"

namespace
{
	TArray<TWeakObjectPtr<ANoFearTrap>> GTraps;
}

// ---------------------------------------------------------------------------
// Взаимодействие
// ---------------------------------------------------------------------------

UNoFearTrapInteraction::UNoFearTrapInteraction()
{
	InteractionDistance = 170.f;
}

bool UNoFearTrapInteraction::CanInteract_Implementation(APawn* User) const
{
	const ANoFearTrap* Trap = Cast<ANoFearTrap>(GetOwner());
	return Super::CanInteract_Implementation(User) && Trap && Trap->CanInteractBy(User);
}

FText UNoFearTrapInteraction::GetInteractionPrompt_Implementation(APawn* User) const
{
	const ANoFearTrap* Trap = Cast<ANoFearTrap>(GetOwner());
	return Trap ? Trap->GetPrompt(User) : PromptText;
}

void UNoFearTrapInteraction::HandleInteract_Implementation(APawn* User)
{
	if (ANoFearTrap* Trap = Cast<ANoFearTrap>(GetOwner()))
	{
		Trap->InteractBy(User);
	}
}

// ---------------------------------------------------------------------------
// Ловушка
// ---------------------------------------------------------------------------

ANoFearTrap::ANoFearTrap()
{
	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = Root;

	Trigger = CreateDefaultSubobject<UBoxComponent>(TEXT("Trigger"));
	Trigger->SetupAttachment(Root);
	Trigger->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Trigger->SetCollisionObjectType(ECC_WorldDynamic);
	Trigger->SetCollisionResponseToAllChannels(ECR_Ignore);
	Trigger->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	Trigger->SetGenerateOverlapEvents(true);
	Trigger->SetCanEverAffectNavigation(false);
	Trigger->SetHiddenInGame(true);
	Trigger->OnComponentBeginOverlap.AddDynamic(this, &ANoFearTrap::HandleTriggerOverlap);

	Interaction = CreateDefaultSubobject<UNoFearTrapInteraction>(TEXT("Interaction"));

	static ConstructorHelpers::FObjectFinderOptional<UStaticMesh> CubeMesh(TEXT("/Engine/BasicShapes/Cube.Cube"));
	static ConstructorHelpers::FObjectFinderOptional<UMaterialInterface> ShapeMaterial(TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	PlaceholderMesh = CubeMesh.Get();
	PlaceholderMaterial = ShapeMaterial.Get();
}

UStaticMeshComponent* ANoFearTrap::MakePart(const TCHAR* Name, USceneComponent* Parent)
{
	UStaticMeshComponent* Part = CreateDefaultSubobject<UStaticMeshComponent>(Name);
	Part->SetupAttachment(Parent);
	Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Part->SetCanEverAffectNavigation(false);
	Part->SetGenerateOverlapEvents(false);
	return Part;
}

void ANoFearTrap::GetTraps(const UWorld* World, TArray<ANoFearTrap*>& OutTraps)
{
	OutTraps.Reset();
	for (const TWeakObjectPtr<ANoFearTrap>& Entry : GTraps)
	{
		if (ANoFearTrap* Trap = Entry.Get(); Trap && Trap->GetWorld() == World)
		{
			OutTraps.Add(Trap);
		}
	}
}

void ANoFearTrap::BeginPlay()
{
	Super::BeginPlay();
	GTraps.AddUnique(this);

	// Кто-то уже стоит в зоне, когда ловушку поставили.
	TArray<AActor*> Overlapping;
	Trigger->GetOverlappingActors(Overlapping, APawn::StaticClass());
	for (AActor* Actor : Overlapping)
	{
		if (APawn* Pawn = Cast<APawn>(Actor); Pawn && bArmed && IsValidVictim(Pawn))
		{
			Spring(Pawn);
			break;
		}
	}
}

void ANoFearTrap::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	GTraps.RemoveAll([this](const TWeakObjectPtr<ANoFearTrap>& Entry)
	{
		return !Entry.IsValid() || Entry.Get() == this;
	});
	Super::EndPlay(EndPlayReason);
}

bool ANoFearTrap::IsValidVictim(const APawn* Pawn) const
{
	if (!Pawn || UNoFearHealthComponent::IsActorIncapacitated(Pawn) || ANoFearHideSpot::IsActorHidden(Pawn))
	{
		return false;
	}
	// Поставленная кем-то — только на его врагов; ничья — на всех.
	const APawn* Placer = GetInstigator();
	return !Placer || (Placer != Pawn && UNoFearTeamLibrary::AreHostile(Placer, Pawn));
}

bool ANoFearTrap::CanPickUp(const APawn* User) const
{
	const APawn* Placer = GetInstigator();
	return User && bArmed && !ItemAsset.IsNull() && (!Placer || Placer == User || UNoFearTeamLibrary::AreFriendly(Placer, User));
}

bool ANoFearTrap::PickUp(APawn* User)
{
	if (!CanPickUp(User))
	{
		return false;
	}
	UNoFearInventoryComponent* Case = UNoFearInventoryComponent::FindCase(User);
	UNoFearItemData* Item = NoFearWorld::LoadItem(ItemAsset);
	if (!Case || !Item)
	{
		return false;
	}
	Case->AddItemOrDrop(Item, 1);
	bArmed = false;
	Destroy();
	return true;
}

bool ANoFearTrap::CanInteractBy(const APawn* User) const
{
	return CanPickUp(User);
}

FText ANoFearTrap::GetPrompt(const APawn* User) const
{
	return LOCTEXT("PickUp", "Забрать");
}

void ANoFearTrap::InteractBy(APawn* User)
{
	PickUp(User);
}

void ANoFearTrap::HandleTriggerOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{
	APawn* Pawn = Cast<APawn>(OtherActor);
	if (HasActorBegunPlay() && bArmed && IsValidVictim(Pawn))
	{
		Spring(Pawn);
	}
}

// ---------------------------------------------------------------------------
// Капкан
// ---------------------------------------------------------------------------

ANoFearBearTrap::ANoFearBearTrap()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;

	Plate = MakePart(TEXT("Plate"), Root);
	LeftHinge = CreateDefaultSubobject<USceneComponent>(TEXT("LeftHinge"));
	LeftHinge->SetupAttachment(Root);
	RightHinge = CreateDefaultSubobject<USceneComponent>(TEXT("RightHinge"));
	RightHinge->SetupAttachment(Root);
	LeftJaw = MakePart(TEXT("LeftJaw"), LeftHinge);
	RightJaw = MakePart(TEXT("RightJaw"), RightHinge);

	Trigger->SetBoxExtent(FVector(28.f, 28.f, 20.f));
	Trigger->SetRelativeLocation(FVector(0.f, 0.f, 20.f));

	ItemAsset = TSoftObjectPtr<UNoFearItemData>(FSoftObjectPath(TEXT("/Game/Items/DA_Item_BearTrap.DA_Item_BearTrap")));

	static ConstructorHelpers::FObjectFinderOptional<USoundBase> SnapFinder(TEXT("/Game/External/LyraContent/Audio/Sounds/Weapons/Shotgun/Weapons_Shotgun_HandHit_01.Weapons_Shotgun_HandHit_01"));
	static ConstructorHelpers::FObjectFinderOptional<USoundBase> ArmFinder(TEXT("/Game/External/LyraContent/Audio/Sounds/Weapons/Pistol/Weapons_Pistol_Slide_01.Weapons_Pistol_Slide_01"));
	SnapSound = SnapFinder.Get();
	ArmSound = ArmFinder.Get();
}

void ANoFearBearTrap::BeginPlay()
{
	NoFearWorld::FitMesh(Plate, PlaceholderMesh, FVector(36.f, 36.f, 3.f), FVector(0.f, 0.f, 1.5f));
	NoFearWorld::TintPlaceholder(Plate, PlaceholderMaterial, Color * 0.7f);
	LeftHinge->SetRelativeLocation(FVector(0.f, -18.f, 3.f));
	RightHinge->SetRelativeLocation(FVector(0.f, 18.f, 3.f));
	// Челюсть в осях петли: стоит вверх, когда капкан захлопнут.
	NoFearWorld::FitMesh(LeftJaw, PlaceholderMesh, FVector(40.f, 2.f, 18.f), FVector(0.f, 0.f, 9.f));
	NoFearWorld::FitMesh(RightJaw, PlaceholderMesh, FVector(40.f, 2.f, 18.f), FVector(0.f, 0.f, 9.f));
	NoFearWorld::TintPlaceholder(LeftJaw, PlaceholderMaterial, Color);
	NoFearWorld::TintPlaceholder(RightJaw, PlaceholderMaterial, Color);
	RefreshJaws();
	Super::BeginPlay();
}

void ANoFearBearTrap::RefreshJaws()
{
	// Раскрытые челюсти лежат наружу, захлопнутые сходятся над пластиной.
	const float Angle = FMath::Lerp(80.f, -12.f, Closed);
	LeftHinge->SetRelativeRotation(FRotator(0.f, 0.f, Angle));
	RightHinge->SetRelativeRotation(FRotator(0.f, 0.f, -Angle));
}

void ANoFearBearTrap::Spring(APawn* Victim)
{
	if (!bArmed)
	{
		return;
	}
	bArmed = false;
	++SprungCount;
	Closed = 1.f;
	RefreshJaws();

	const FVector Location = GetActorLocation();
	// Лязг и крик пойманного: шум жертвы, на него сбегаются.
	NoFearWorld::PlaySoundAndNoise(this, SnapSound, Location, 1.f, 1500.f, Victim ? static_cast<AActor*>(Victim) : this, TEXT("Trap"));

	if (Victim)
	{
		Held = Victim;
		ReleaseTime = GetWorld()->GetTimeSeconds() + HoldTime;
		if (ACharacter* Character = Cast<ACharacter>(Victim))
		{
			Character->GetCharacterMovement()->StopMovementImmediately();
			Character->GetCharacterMovement()->DisableMovement();
		}
		if (AAIController* AI = Cast<AAIController>(Victim->GetController()))
		{
			AI->StopMovement();
		}
		if (Damage > 0.f)
		{
			UGameplayStatics::ApplyDamage(Victim, Damage, GetInstigatorController(), this, UDamageType::StaticClass());
		}
		NoFearWorld::Notify(Victim, LOCTEXT("Caught", "Нога в капкане!"));
	}
	OnSprung.Broadcast(this, Victim);
}

void ANoFearBearTrap::Release()
{
	APawn* Pawn = Held.Get();
	Held.Reset();
	if (ACharacter* Character = Cast<ACharacter>(Pawn); Character && !UNoFearHealthComponent::IsActorDead(Character))
	{
		if (Character->GetCharacterMovement()->MovementMode == MOVE_None)
		{
			Character->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
		}
	}
	Closed = 0.85f;
	RefreshJaws();
}

bool ANoFearBearTrap::Rearm(APawn* User)
{
	if (bArmed || Held.IsValid())
	{
		return false;
	}
	bArmed = true;
	Closed = 0.f;
	RefreshJaws();
	NoFearWorld::PlaySoundAndNoise(this, ArmSound, GetActorLocation(), 0.3f, 500.f, User, TEXT("Trap"));
	return true;
}

bool ANoFearBearTrap::CanInteractBy(const APawn* User) const
{
	return Super::CanInteractBy(User) || (!bArmed && !Held.IsValid() && User);
}

FText ANoFearBearTrap::GetPrompt(const APawn* User) const
{
	return bArmed ? LOCTEXT("PickUpBear", "Забрать капкан") : LOCTEXT("Rearm", "Взвести (держать)");
}

void ANoFearBearTrap::InteractBy(APawn* User)
{
	if (bArmed)
	{
		PickUp(User);
		return;
	}
	Worker = User;
	WorkTime = 0.f;
}

void ANoFearBearTrap::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (APawn* Pawn = Held.Get())
	{
		if (GetWorld()->GetTimeSeconds() >= ReleaseTime || UNoFearHealthComponent::IsActorDead(Pawn))
		{
			Release();
		}
	}
	else if (Held.IsStale())
	{
		Release();
	}

	if (APawn* User = Worker.Get())
	{
		if (!NoFearWorld::IsInteractHeld(User) || !Interaction->IsInRange(User) || bArmed || Held.IsValid())
		{
			Worker.Reset();
		}
		else if ((WorkTime += DeltaSeconds) >= RearmTime)
		{
			Worker.Reset();
			Rearm(User);
		}
	}
}

// ---------------------------------------------------------------------------
// Растяжка
// ---------------------------------------------------------------------------

ANoFearTripwire::ANoFearTripwire()
{
	Wire = MakePart(TEXT("Wire"), Root);
	LeftPost = MakePart(TEXT("LeftPost"), Root);
	RightPost = MakePart(TEXT("RightPost"), Root);
	Charge = MakePart(TEXT("Charge"), Root);
	Color = FLinearColor(0.75f, 0.75f, 0.7f);

	ItemAsset = TSoftObjectPtr<UNoFearItemData>(FSoftObjectPath(TEXT("/Game/Items/DA_Item_Tripwire.DA_Item_Tripwire")));

	static ConstructorHelpers::FObjectFinderOptional<USoundBase> TriggerFinder(TEXT("/Game/External/LyraContent/Audio/Sounds/Weapons/Pistol/Weapons_Pistol_Slide_01.Weapons_Pistol_Slide_01"));
	TriggerSound = TriggerFinder.Get();
}

void ANoFearTripwire::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	RefreshShape();
}

void ANoFearTripwire::BeginPlay()
{
	Stretch();
	Super::BeginPlay();
}

void ANoFearTripwire::Stretch()
{
	UWorld* World = GetWorld();
	const FVector Origin = GetActorLocation() + GetActorUpVector() * WireHeight;
	const FVector Right = GetActorRightVector();
	FCollisionQueryParams Params(SCENE_QUERY_STAT(NoFearTripwire), false, this);
	FCollisionObjectQueryParams Objects;
	Objects.AddObjectTypesToQuery(ECC_WorldStatic);
	Objects.AddObjectTypesToQuery(ECC_WorldDynamic);

	auto FindEnd = [&](float Sign)
	{
		FHitResult Hit;
		const FVector End = Origin + Right * Sign * MaxHalfLength;
		if (World && World->LineTraceSingleByObjectType(Hit, Origin, End, Objects, Params))
		{
			return Sign * FMath::Max(Hit.Distance - 2.f, 5.f);
		}
		return Sign * MaxHalfLength;
	};
	WireStart = FVector(0.f, FindEnd(-1.f), WireHeight);
	WireEnd = FVector(0.f, FindEnd(1.f), WireHeight);
	RefreshShape();
}

void ANoFearTripwire::RefreshShape()
{
	const float Length = FMath::Max(WireEnd.Y - WireStart.Y, 10.f);
	const FVector Middle = (WireStart + WireEnd) * 0.5f;
	NoFearWorld::FitMesh(Wire, PlaceholderMesh, FVector(0.6f, Length, 0.6f), Middle);
	NoFearWorld::FitMesh(LeftPost, PlaceholderMesh, FVector(3.f, 3.f, WireHeight + 6.f), FVector(0.f, WireStart.Y, (WireHeight + 6.f) * 0.5f));
	NoFearWorld::FitMesh(RightPost, PlaceholderMesh, FVector(3.f, 3.f, WireHeight + 6.f), FVector(0.f, WireEnd.Y, (WireHeight + 6.f) * 0.5f));
	NoFearWorld::FitMesh(Charge, PlaceholderMesh, FVector(10.f, 8.f, 12.f), FVector(0.f, WireStart.Y + 8.f, 6.f));
	NoFearWorld::TintPlaceholder(Wire, PlaceholderMaterial, Color);
	NoFearWorld::TintPlaceholder(LeftPost, PlaceholderMaterial, Color * 0.5f);
	NoFearWorld::TintPlaceholder(RightPost, PlaceholderMaterial, Color * 0.5f);
	NoFearWorld::TintPlaceholder(Charge, PlaceholderMaterial, FLinearColor(0.25f, 0.32f, 0.18f));

	Trigger->SetBoxExtent(FVector(6.f, Length * 0.5f, 10.f));
	Trigger->SetRelativeLocation(Middle);
}

FText ANoFearTripwire::GetPrompt(const APawn* User) const
{
	return LOCTEXT("PickUpWire", "Снять растяжку");
}

void ANoFearTripwire::Spring(APawn* Victim)
{
	if (!bArmed)
	{
		return;
	}
	bArmed = false;
	++SprungCount;
	TriggeredBy = Victim;
	Wire->SetVisibility(false);
	NoFearWorld::PlaySoundAndNoise(this, TriggerSound, GetActorLocation(), 0.f, 0.f, nullptr, NAME_None);
	OnSprung.Broadcast(this, Victim);
	if (FuseDelay > 0.f)
	{
		GetWorldTimerManager().SetTimer(FuseTimer, this, &ANoFearTripwire::Detonate, FuseDelay, false);
	}
	else
	{
		Detonate();
	}
}

void ANoFearTripwire::Detonate()
{
	// Взрыв посередине проволоки: там тот, кто её задел.
	const FVector Location = GetActorTransform().TransformPosition((WireStart + WireEnd) * 0.5f);
	NoFearWorld::Explode(this, Explosion, Location, GetInstigator());
	Destroy();
}

#undef LOCTEXT_NAMESPACE
