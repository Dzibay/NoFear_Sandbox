#include "World/NoFearBody.h"

#include "Components/StaticMeshComponent.h"
#include "Core/NoFearHealthComponent.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/Pawn.h"
#include "Inventory/NoFearInventoryComponent.h"
#include "Inventory/NoFearItemContainerComponent.h"
#include "Inventory/NoFearItemData.h"
#include "Materials/MaterialInterface.h"
#include "UObject/ConstructorHelpers.h"
#include "World/NoFearWorldLibrary.h"

#define LOCTEXT_NAMESPACE "NoFearBody"

// ---------------------------------------------------------------------------
// Взаимодействие
// ---------------------------------------------------------------------------

UNoFearBodyInteraction::UNoFearBodyInteraction()
{
	InteractionDistance = 200.f;
	PromptText = LOCTEXT("Search", "Обыскать (держать)");
}

bool UNoFearBodyInteraction::CanInteract_Implementation(APawn* User) const
{
	const ANoFearBody* Body = Cast<ANoFearBody>(GetOwner());
	return Super::CanInteract_Implementation(User) && Body && UNoFearInventoryComponent::FindCase(User);
}

FText UNoFearBodyInteraction::GetInteractionPrompt_Implementation(APawn* User) const
{
	const ANoFearBody* Body = Cast<ANoFearBody>(GetOwner());
	return Body ? Body->GetPrompt(User) : PromptText;
}

void UNoFearBodyInteraction::HandleInteract_Implementation(APawn* User)
{
	if (ANoFearBody* Body = Cast<ANoFearBody>(GetOwner()))
	{
		Body->InteractBy(User);
	}
}

// ---------------------------------------------------------------------------
// Тело
// ---------------------------------------------------------------------------

namespace
{
	UStaticMeshComponent* MakePart(AActor* Owner, USceneComponent* Parent, FName Name, UStaticMesh* Mesh, const FVector& Location, const FRotator& Rotation, const FVector& Scale)
	{
		UStaticMeshComponent* Part = Owner->CreateDefaultSubobject<UStaticMeshComponent>(Name);
		Part->SetupAttachment(Parent);
		Part->SetStaticMesh(Mesh);
		Part->SetRelativeLocationAndRotation(Location, Rotation);
		Part->SetRelativeScale3D(Scale);
		// Пешки перешагивают, пули и взгляд проходят; трасса Visibility нужна только чтобы тело было видно для E.
		Part->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		Part->SetCollisionObjectType(ECC_WorldDynamic);
		Part->SetCollisionResponseToAllChannels(ECR_Ignore);
		Part->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
		Part->SetCanEverAffectNavigation(false);
		Part->SetGenerateOverlapEvents(false);
		return Part;
	}
}

ANoFearBody::ANoFearBody()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = Root;

	static ConstructorHelpers::FObjectFinderOptional<UStaticMesh> Sphere(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	static ConstructorHelpers::FObjectFinderOptional<UStaticMesh> Cylinder(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	static ConstructorHelpers::FObjectFinderOptional<UMaterialInterface> ShapeMaterial(TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	PlaceholderMaterial = ShapeMaterial.Get();

	// Лежит на спине головой по +X. Цилиндр 100×100 см, ось Z — поворачиваем на 90 по тангажу.
	Torso = MakePart(this, Root, TEXT("Torso"), Cylinder.Get(), FVector(10.f, 0.f, 14.f), FRotator(90.f, 0.f, 0.f), FVector(0.4f, 0.28f, 0.65f));
	Head = MakePart(this, Root, TEXT("Head"), Sphere.Get(), FVector(57.f, 0.f, 13.f), FRotator::ZeroRotator, FVector(0.24f));
	LegL = MakePart(this, Root, TEXT("LegL"), Cylinder.Get(), FVector(-60.f, 11.f, 8.f), FRotator(90.f, 6.f, 0.f), FVector(0.15f, 0.15f, 0.85f));
	LegR = MakePart(this, Root, TEXT("LegR"), Cylinder.Get(), FVector(-60.f, -11.f, 8.f), FRotator(90.f, -6.f, 0.f), FVector(0.15f, 0.15f, 0.85f));

	Storage = CreateDefaultSubobject<UNoFearItemContainerComponent>(TEXT("Storage"));
	Storage->GridSize = FIntPoint(6, 4);
	Interaction = CreateDefaultSubobject<UNoFearBodyInteraction>(TEXT("Interaction"));

	DisplayName = LOCTEXT("DefaultName", "Тело");
}

void ANoFearBody::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	RefreshLook();
}

void ANoFearBody::BeginPlay()
{
	Super::BeginPlay();
	Storage->DisplayName = DisplayName;
	for (const FNoFearChestItem& Entry : Contents)
	{
		if (!Entry.Item || Entry.Count <= 0)
		{
			continue;
		}
		const int32 Added = Storage->AddItem(Entry.Item, Entry.Count, Entry.MagazineAmmo);
		if (Added < Entry.Count)
		{
			UE_LOG(LogTemp, Warning, TEXT("%s: %s x%d не влезло в сетку тела"), *GetName(), *Entry.Item->GetName(), Entry.Count - Added);
		}
	}
	RefreshLook();
}

void ANoFearBody::RefreshLook()
{
	NoFearWorld::TintPlaceholder(Torso, PlaceholderMaterial, Color);
	NoFearWorld::TintPlaceholder(LegL, PlaceholderMaterial, Color * 0.7f);
	NoFearWorld::TintPlaceholder(LegR, PlaceholderMaterial, Color * 0.7f);
	NoFearWorld::TintPlaceholder(Head, PlaceholderMaterial, FLinearColor(0.35f, 0.3f, 0.27f));
}

bool ANoFearBody::IsEmpty() const
{
	return Storage->IsEmpty();
}

float ANoFearBody::GetSearchProgress() const
{
	return Searcher.IsValid() ? FMath::Clamp(SearchElapsed / FMath::Max(SearchTime, 0.01f), 0.f, 1.f) : 0.f;
}

FText ANoFearBody::GetPrompt(const APawn* User) const
{
	if (!bSearched)
	{
		return SearchTime > 0.f ? LOCTEXT("Search", "Обыскать (держать)") : LOCTEXT("SearchNow", "Обыскать");
	}
	return IsEmpty() ? LOCTEXT("Empty", "Обыскано: пусто") : LOCTEXT("Loot", "Вещи");
}

void ANoFearBody::InteractBy(APawn* User)
{
	if (bSearched)
	{
		OpenScreen(User);
		return;
	}
	// Бот или вызов из кода — сразу; игрок держит E.
	if (SearchTime <= 0.f || !NoFearWorld::IsInteractHeld(User))
	{
		Search(User);
		return;
	}
	Searcher = User;
	SearchElapsed = 0.f;
	SetActorTickEnabled(true);
	NoFearWorld::PlaySoundAndNoise(this, SearchSound, GetActorLocation(), NoiseLoudness, NoiseRange, User, TEXT("Search"));
}

bool ANoFearBody::Search(APawn* User)
{
	Searcher.Reset();
	SearchElapsed = 0.f;
	SetActorTickEnabled(false);
	if (!bSearched)
	{
		bSearched = true;
		OnSearched.Broadcast(this, User);
	}
	return OpenScreen(User);
}

bool ANoFearBody::OpenScreen(APawn* User)
{
	UNoFearInventoryComponent* Case = UNoFearInventoryComponent::FindCase(User);
	if (!bSearched || !Case)
	{
		return false;
	}
	if (IsEmpty())
	{
		NoFearWorld::Notify(User, LOCTEXT("NothingFound", "Ничего нет"));
		return false;
	}
	return Case->OpenContainerScreen(Storage);
}

void ANoFearBody::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	APawn* User = Searcher.Get();
	if (!User || !NoFearWorld::IsInteractHeld(User) || !Interaction->IsInRange(User) || UNoFearHealthComponent::IsActorIncapacitated(User))
	{
		Searcher.Reset();
		SearchElapsed = 0.f;
		SetActorTickEnabled(false);
		return;
	}
	SearchElapsed += DeltaSeconds;
	if (SearchElapsed >= SearchTime)
	{
		Search(User);
	}
}

#undef LOCTEXT_NAMESPACE
