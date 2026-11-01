#include "Inventory/NoFearPickup.h"

#include "Components/BoxComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Inventory/NoFearInventoryComponent.h"
#include "Inventory/NoFearItemData.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Weapons/NoFearWeaponData.h"
#include "UObject/ConstructorHelpers.h"

#define LOCTEXT_NAMESPACE "NoFearPickup"

// ---------------------------------------------------------------------------
// Взаимодействие
// ---------------------------------------------------------------------------

UNoFearPickupInteraction::UNoFearPickupInteraction()
{
	InteractionDistance = 250.f;
	PromptText = LOCTEXT("PickUp", "Подобрать");
}

bool UNoFearPickupInteraction::CanInteract_Implementation(APawn* User) const
{
	const ANoFearPickup* Pickup = Cast<ANoFearPickup>(GetOwner());
	const UNoFearInventoryComponent* Case = UNoFearInventoryComponent::FindCase(User);
	if (!Super::CanInteract_Implementation(User) || !Pickup || !Pickup->Item || !Case)
	{
		return false;
	}
	// Кейс уже самый большой — расширение подобрать нельзя, пусть лежит.
	return Pickup->Item->Kind != ENoFearItemKind::CaseExpansion || Case->CanExpand();
}

FText UNoFearPickupInteraction::GetInteractionPrompt_Implementation(APawn* User) const
{
	const ANoFearPickup* Pickup = Cast<ANoFearPickup>(GetOwner());
	return Pickup ? FText::Format(LOCTEXT("PickUpItem", "Подобрать: {0}"), Pickup->GetPickupName()) : PromptText;
}

void UNoFearPickupInteraction::HandleInteract_Implementation(APawn* User)
{
	if (ANoFearPickup* Pickup = Cast<ANoFearPickup>(GetOwner()))
	{
		Pickup->TryPickUp(User);
	}
}

// ---------------------------------------------------------------------------
// Предмет
// ---------------------------------------------------------------------------

ANoFearPickup::ANoFearPickup()
{
	PrimaryActorTick.bCanEverTick = false;

	Box = CreateDefaultSubobject<UBoxComponent>(TEXT("Box"));
	Box->SetCollisionProfileName(UCollisionProfile::PhysicsActor_ProfileName);
	// Под ногами не мешает, камеру и прицел не перекрывает.
	Box->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
	Box->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
	Box->SetCollisionResponseToChannel(ECC_Visibility, ECR_Ignore);
	Box->SetCanEverAffectNavigation(false);
	Box->SetBoxExtent(FVector(5.f));
	Box->BodyInstance.bUseCCD = true;
	Box->SetLinearDamping(0.5f);
	Box->SetAngularDamping(1.f);
	RootComponent = Box;

	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	Mesh->SetupAttachment(Box);
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Mesh->SetCanEverAffectNavigation(false);

	WeaponMesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("WeaponMesh"));
	WeaponMesh->SetupAttachment(Box);
	WeaponMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	WeaponMesh->SetCanEverAffectNavigation(false);
	WeaponMesh->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::OnlyTickPoseWhenRendered;

	Interaction = CreateDefaultSubobject<UNoFearPickupInteraction>(TEXT("Interaction"));

	static ConstructorHelpers::FObjectFinderOptional<UStaticMesh> CubeMesh(TEXT("/Engine/BasicShapes/Cube.Cube"));
	static ConstructorHelpers::FObjectFinderOptional<UMaterialInterface> ShapeMaterial(TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	PlaceholderMesh = CubeMesh.Get();
	PlaceholderMaterial = ShapeMaterial.Get();
}

void ANoFearPickup::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	RefreshAppearance();
}

void ANoFearPickup::BeginPlay()
{
	Super::BeginPlay();
	if (Item && Count <= 0)
	{
		Count = Item->GetDefaultPickupCount();
	}
	Box->SetSimulatePhysics(bSimulatePhysics);
}

void ANoFearPickup::SetItem(UNoFearItemData* NewItem, int32 NewCount, int32 NewMagazineAmmo)
{
	Item = NewItem;
	Count = NewCount > 0 ? NewCount : (Item ? Item->GetDefaultPickupCount() : 0);
	MagazineAmmo = NewMagazineAmmo;
	RefreshAppearance();
}

int32 ANoFearPickup::GetCount() const
{
	return Count > 0 ? Count : (Item ? Item->GetDefaultPickupCount() : 0);
}

FText ANoFearPickup::GetPickupName() const
{
	if (!Item)
	{
		return FText::GetEmpty();
	}
	const int32 Total = GetCount();
	return Total > 1 ? FText::Format(LOCTEXT("NameCount", "{0} ×{1}"), Item->GetDisplayName(), FText::AsNumber(Total)) : Item->GetDisplayName();
}

bool ANoFearPickup::TryPickUp(APawn* User)
{
	UNoFearInventoryComponent* Case = UNoFearInventoryComponent::FindCase(User);
	if (!Case || !Item)
	{
		return false;
	}

	const int32 Total = GetCount();
	const int32 Added = Case->AddItem(Item, Total, MagazineAmmo);
	if (Added <= 0)
	{
		// Экран кейса с предметом в руке — этап 2; пока только сообщение.
		Case->PushNotice(LOCTEXT("NoRoom", "Нет места в кейсе"));
		return false;
	}

	const FText Name = Added > 1 ? FText::Format(LOCTEXT("NameCount", "{0} ×{1}"), Item->GetDisplayName(), FText::AsNumber(Added)) : Item->GetDisplayName();
	if (Added < Total)
	{
		Count = Total - Added;
		Case->PushNotice(FText::Format(LOCTEXT("PickedPart", "Подобрано: {0}. Больше не влезает"), Name));
		return true;
	}
	Case->PushNotice(FText::Format(LOCTEXT("Picked", "Подобрано: {0}"), Name));
	Destroy();
	return true;
}

void ANoFearPickup::RefreshAppearance()
{
	if (!Mesh || !WeaponMesh || !Box)
	{
		return;
	}

	UStaticMesh* WorldMesh = Item ? Item->WorldMesh.Get() : nullptr;
	USkeletalMesh* GunMesh = !WorldMesh && Item && Item->WeaponData ? Item->WeaponData->Mesh.Get() : nullptr;
	const FTransform ItemTransform = Item ? FTransform(Item->WorldMeshRotation, FVector::ZeroVector, Item->WorldMeshScale) : FTransform::Identity;
	WeaponMesh->SetSkeletalMesh(GunMesh);
	WeaponMesh->SetVisibility(GunMesh != nullptr);
	Mesh->SetVisibility(GunMesh == nullptr);

	// Габариты меша в координатах коробки: по ним коробка столкновений, а меш сдвигается в её центр.
	FBoxSphereBounds Bounds(FVector::ZeroVector, FVector(5.f), 5.f);
	USceneComponent* Shown = Mesh;
	if (GunMesh)
	{
		Mesh->SetStaticMesh(nullptr);
		WeaponMesh->SetRelativeTransform(ItemTransform);
		Bounds = GunMesh->GetBounds().TransformBy(ItemTransform);
		Shown = WeaponMesh;
	}
	else if (WorldMesh)
	{
		Mesh->SetStaticMesh(WorldMesh);
		Mesh->SetRelativeTransform(ItemTransform);
		Mesh->EmptyOverrideMaterials();
		Bounds = WorldMesh->GetBounds().TransformBy(ItemTransform);
	}
	else
	{
		// Брусок длиной и шириной в клетках кейса, высотой чуть больше половины узкой стороны.
		const FIntPoint Cells = Item ? Item->GetGridSize() : FIntPoint(1, 1);
		const float Length = Cells.X * PlaceholderCellSize;
		const float Width = Cells.Y * PlaceholderCellSize;
		const float Height = FMath::Min(Length, Width) * 0.6f;
		Mesh->SetStaticMesh(PlaceholderMesh);
		// Куб движка — 100 см с центром посередине.
		Mesh->SetRelativeTransform(FTransform(FRotator::ZeroRotator, FVector::ZeroVector, FVector(Length, Width, Height) / 100.f));
		if (PlaceholderMaterial)
		{
			UMaterialInstanceDynamic* Material = Mesh->CreateDynamicMaterialInstance(0, PlaceholderMaterial);
			if (Material)
			{
				Material->SetVectorParameterValue(TEXT("Color"), Item ? Item->Color : FLinearColor::Gray);
			}
		}
		if (PlaceholderMesh)
		{
			Bounds = PlaceholderMesh->GetBounds().TransformBy(Mesh->GetRelativeTransform());
		}
	}

	Shown->SetRelativeLocation(Shown->GetRelativeLocation() - Bounds.Origin);
	Box->SetBoxExtent(Bounds.BoxExtent.ComponentMax(FVector(2.f)));
}

ANoFearPickup* ANoFearPickup::SpawnPickup(UWorld* World, UNoFearItemData* Item, int32 Count, int32 MagazineAmmo, const FTransform& Transform, const FVector& Velocity)
{
	if (!World || !Item || Count < 0)
	{
		return nullptr;
	}
	ANoFearPickup* Pickup = World->SpawnActorDeferred<ANoFearPickup>(ANoFearPickup::StaticClass(), Transform, nullptr, nullptr,
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!Pickup)
	{
		return nullptr;
	}
	Pickup->Item = Item;
	Pickup->Count = Count;
	Pickup->MagazineAmmo = MagazineAmmo;
	Pickup->FinishSpawning(Transform);

	if (!Velocity.IsNearlyZero() && Pickup->Box->IsSimulatingPhysics())
	{
		Pickup->Box->SetPhysicsLinearVelocity(Velocity);
	}
	return Pickup;
}

#undef LOCTEXT_NAMESPACE
