#include "World/NoFearChest.h"

#include "Components/StaticMeshComponent.h"
#include "Core/NoFearHealthComponent.h"
#include "Engine/DamageEvents.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Inventory/NoFearInventoryComponent.h"
#include "Inventory/NoFearItemContainerComponent.h"
#include "Inventory/NoFearItemData.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Perception/AISense_Hearing.h"
#include "Sound/SoundBase.h"
#include "UObject/ConstructorHelpers.h"
#include "World/NoFearWorldLibrary.h"

#define LOCTEXT_NAMESPACE "NoFearChest"

namespace
{
	TArray<TWeakObjectPtr<ANoFearChest>> GChests;
}

// ---------------------------------------------------------------------------
// Взаимодействие
// ---------------------------------------------------------------------------

UNoFearChestInteraction::UNoFearChestInteraction()
{
	InteractionDistance = 200.f;
	PromptText = LOCTEXT("Open", "Открыть");
}

bool UNoFearChestInteraction::CanInteract_Implementation(APawn* User) const
{
	const ANoFearChest* Chest = Cast<ANoFearChest>(GetOwner());
	return Super::CanInteract_Implementation(User) && Chest;
}

FText UNoFearChestInteraction::GetInteractionPrompt_Implementation(APawn* User) const
{
	const ANoFearChest* Chest = Cast<ANoFearChest>(GetOwner());
	if (!Chest)
	{
		return PromptText;
	}
	if (Chest->IsLocked())
	{
		const UNoFearItemData* Key = NoFearWorld::FindKey(User, Chest->KeyId);
		return Key ? NoFearWorld::UnlockPrompt(Key) : LOCTEXT("Locked", "Заперто");
	}
	if (!Chest->IsOpen())
	{
		return PromptText;
	}
	if (!Chest->IsEmpty())
	{
		return Chest->bTakeAllOnOpen ? LOCTEXT("TakeAll", "Забрать всё") : LOCTEXT("Search", "Обыскать");
	}
	return LOCTEXT("Close", "Закрыть");
}

void UNoFearChestInteraction::HandleInteract_Implementation(APawn* User)
{
	ANoFearChest* Chest = Cast<ANoFearChest>(GetOwner());
	if (!Chest)
	{
		return;
	}
	if (Chest->IsLocked() && !Chest->UnlockWithKey(User))
	{
		Chest->OpenChest(User); // Дёрнуть крышку: звук, не открывается.
		return;
	}
	if (!Chest->IsOpen())
	{
		Chest->OpenChest(User);
		if (Chest->bTakeAllOnOpen)
		{
			Chest->TakeAll(User);
		}
		else
		{
			Chest->OpenScreen(User);
		}
		return;
	}
	if (!Chest->IsEmpty())
	{
		if (Chest->bTakeAllOnOpen)
		{
			Chest->TakeAll(User);
		}
		else
		{
			Chest->OpenScreen(User);
		}
		return;
	}
	Chest->CloseChest();
}

// ---------------------------------------------------------------------------
// Сундук
// ---------------------------------------------------------------------------

ANoFearChest::ANoFearChest()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = Root;

	Body = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Body"));
	Body->SetupAttachment(Root);
	Body->SetCollisionProfileName(TEXT("BlockAllDynamic"));
	Body->SetGenerateOverlapEvents(false);

	LidHinge = CreateDefaultSubobject<USceneComponent>(TEXT("LidHinge"));
	LidHinge->SetupAttachment(Root);

	Lid = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Lid"));
	Lid->SetupAttachment(LidHinge);
	Lid->SetCollisionProfileName(TEXT("BlockAllDynamic"));
	Lid->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
	Lid->SetCanEverAffectNavigation(false);
	Lid->SetGenerateOverlapEvents(false);

	Health = CreateDefaultSubobject<UNoFearHealthComponent>(TEXT("Health"));
	Health->RegenRate = 0.f;
	Health->bCanBeDowned = false;

	Interaction = CreateDefaultSubobject<UNoFearChestInteraction>(TEXT("Interaction"));

	Storage = CreateDefaultSubobject<UNoFearItemContainerComponent>(TEXT("Storage"));
	Storage->DisplayName = LOCTEXT("StorageName", "Сундук");

	static ConstructorHelpers::FObjectFinderOptional<UStaticMesh> BodyFinder(TEXT("/Game/Environment/Chests/MediumChest/SM_crate_bottom_T_scifi_crate_0.SM_crate_bottom_T_scifi_crate_0"));
	static ConstructorHelpers::FObjectFinderOptional<UStaticMesh> LidFinder(TEXT("/Game/Environment/Chests/MediumChest/SM_crate_top_T_scifi_crate_0.SM_crate_top_T_scifi_crate_0"));
	static ConstructorHelpers::FObjectFinderOptional<UStaticMesh> CubeMesh(TEXT("/Engine/BasicShapes/Cube.Cube"));
	static ConstructorHelpers::FObjectFinderOptional<UMaterialInterface> ShapeMaterial(TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	BodyMesh = BodyFinder.Get();
	LidMesh = LidFinder.Get();
	PlaceholderMesh = CubeMesh.Get();
	PlaceholderMaterial = ShapeMaterial.Get();

	static ConstructorHelpers::FObjectFinderOptional<USoundBase> OpenFinder(TEXT("/Game/Audio/Foley/SoundWaves/Hand/Concrete/Foley_hs_1p_barehand_concrete_plant_07.Foley_hs_1p_barehand_concrete_plant_07"));
	static ConstructorHelpers::FObjectFinderOptional<USoundBase> CloseFinder(TEXT("/Game/External/LyraContent/Audio/Sounds/Weapons/Shotgun/Weapons_Shotgun_HandHit_01.Weapons_Shotgun_HandHit_01"));
	static ConstructorHelpers::FObjectFinderOptional<USoundBase> LockedFinder(TEXT("/Game/External/LyraContent/Audio/Sounds/Weapons/Pistol/Weapons_Pistol_Slide_01.Weapons_Pistol_Slide_01"));
	static ConstructorHelpers::FObjectFinderOptional<USoundBase> HitFinder(TEXT("/Game/External/LyraContent/Audio/Sounds/Impacts/Lyra_ImpactPlaster_05.Lyra_ImpactPlaster_05"));
	static ConstructorHelpers::FObjectFinderOptional<USoundBase> BreakFinder(TEXT("/Game/External/LyraContent/Audio/Sounds/Impacts/Lyra_ImpactPlasterDebris_05.Lyra_ImpactPlasterDebris_05"));
	OpenSound = OpenFinder.Get();
	CloseSound = CloseFinder.Get();
	LockedSound = LockedFinder.Get();
	HitSound = HitFinder.Get();
	BreakSound = BreakFinder.Get();
}

void ANoFearChest::GetChests(const UWorld* World, TArray<ANoFearChest*>& OutChests)
{
	OutChests.Reset();
	for (const TWeakObjectPtr<ANoFearChest>& Entry : GChests)
	{
		if (ANoFearChest* Chest = Entry.Get(); Chest && Chest->GetWorld() == World)
		{
			OutChests.Add(Chest);
		}
	}
}

void ANoFearChest::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	RefreshShape();
}

void ANoFearChest::BeginPlay()
{
	Super::BeginPlay();
	GChests.AddUnique(this);

	Health->MaxHealth = LockHealth;
	Health->SetHealth(LockHealth);
	Health->OnDeath.AddDynamic(this, &ANoFearChest::HandleDeath);

	// Стартовое содержимое — в сетку хранилища.
	for (const FNoFearChestItem& Entry : Contents)
	{
		if (!Entry.Item || Entry.Count <= 0)
		{
			continue;
		}
		const int32 Added = Storage->AddItem(Entry.Item, Entry.Count, Entry.MagazineAmmo);
		if (Added < Entry.Count)
		{
			UE_LOG(LogTemp, Warning, TEXT("%s: %s x%d не влезло в сетку сундука"), *GetName(), *Entry.Item->GetName(), Entry.Count - Added);
		}
	}
	Storage->OnInventoryChanged.AddDynamic(this, &ANoFearChest::HandleStorageChanged);

	RefreshShape();
	RefreshLidPose();
}

void ANoFearChest::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	GChests.RemoveAll([this](const TWeakObjectPtr<ANoFearChest>& Entry)
	{
		return !Entry.IsValid() || Entry.Get() == this;
	});
	Super::EndPlay(EndPlayReason);
}

// --- Форма ---

void ANoFearChest::RefreshShape()
{
	const bool bPlaceholderBody = !BodyMesh;
	UStaticMesh* BodyAsset = bPlaceholderBody ? PlaceholderMesh.Get() : BodyMesh.Get();
	const FBox BodyBox = BodyAsset ? BodyAsset->GetBoundingBox() : FBox(FVector(-50.f), FVector(50.f));

	// Корпус: центр по XY и дно на точке актора. Заглушка растягивается на BodySize.
	const FVector BodyScale = bPlaceholderBody ? BodySize / BodyBox.GetSize().ComponentMax(FVector(1.f)) : FVector::OneVector;
	const FVector MeshOffset = -FVector(BodyBox.GetCenter().X, BodyBox.GetCenter().Y, BodyBox.Min.Z) * BodyScale;
	Body->SetStaticMesh(BodyAsset);
	Body->SetRelativeLocation(MeshOffset);
	Body->SetRelativeScale3D(BodyScale);

	// Крышка: тот же сдвиг, что у корпуса (меши крышки и корпуса стоят друг на друге). Заглушка — плита поверх корпуса.
	FVector LidScale = FVector::OneVector;
	FVector LidOffset = MeshOffset;
	FVector Hinge;
	UStaticMesh* LidAsset = LidMesh.Get();
	if (LidAsset && !bPlaceholderBody)
	{
		// Петля — задний (−X) нижний край крышки.
		const FBox LidBox = LidAsset->GetBoundingBox();
		Hinge = FVector(LidBox.Min.X, LidBox.GetCenter().Y, LidBox.Min.Z) + LidOffset;
	}
	else
	{
		LidAsset = PlaceholderMesh;
		const FBox CubeBox = LidAsset ? LidAsset->GetBoundingBox() : FBox(FVector(-50.f), FVector(50.f));
		const FVector LidSize(BodySize.X + 2.f, BodySize.Y + 2.f, 8.f);
		LidScale = LidSize / CubeBox.GetSize().ComponentMax(FVector(1.f));
		LidOffset = FVector(0.f, 0.f, BodySize.Z + LidSize.Z * 0.5f) - CubeBox.GetCenter() * LidScale;
		Hinge = FVector(-LidSize.X * 0.5f, 0.f, BodySize.Z);
	}
	LidHinge->SetRelativeLocation(Hinge);
	Lid->SetStaticMesh(LidAsset);
	Lid->SetRelativeScale3D(LidScale);
	Lid->SetRelativeLocation(LidOffset - Hinge);

	for (UStaticMeshComponent* Mesh : { Body.Get(), Lid.Get() })
	{
		Mesh->EmptyOverrideMaterials();
		if (OverrideMaterial)
		{
			Mesh->SetMaterial(0, OverrideMaterial);
		}
		else if (Mesh->GetStaticMesh() == PlaceholderMesh && PlaceholderMaterial)
		{
			if (UMaterialInstanceDynamic* Material = Mesh->CreateDynamicMaterialInstance(0, PlaceholderMaterial))
			{
				const FLinearColor Color = bLocked ? PlaceholderColor * FLinearColor(1.6f, 0.4f, 0.3f) : PlaceholderColor;
				Material->SetVectorParameterValue(TEXT("Color"), Color);
			}
		}
	}
}

void ANoFearChest::RefreshLidPose()
{
	const float Alpha = FMath::InterpEaseInOut(0.f, 1.f, FMath::Clamp(LidAmount, 0.f, 1.f), 2.f);
	// Положительный тангаж поднимает передний (+X) край крышки над задней петлёй.
	LidHinge->SetRelativeRotation(FRotator(LidOpenAngle * Alpha, 0.f, 0.f));
}

// --- Действия ---

bool ANoFearChest::OpenChest(APawn* User)
{
	if (bLocked)
	{
		PlayChestSound(LockedSound, NoiseLoudness * 0.7f, NoiseRange * 0.7f, User);
		return false;
	}
	if (bOpen)
	{
		return false;
	}
	bOpen = true;
	LastUser = User;
	SetActorTickEnabled(true);
	PlayChestSound(OpenSound, NoiseLoudness, NoiseRange, User);
	OnOpened.Broadcast(this, User);
	return true;
}

bool ANoFearChest::CloseChest()
{
	if (!bOpen)
	{
		return false;
	}
	bOpen = false;
	SetActorTickEnabled(true);
	OnClosed.Broadcast(this);
	return true;
}

int32 ANoFearChest::TakeAll(APawn* User)
{
	UNoFearInventoryComponent* Case = UNoFearInventoryComponent::FindCase(User);
	if (!Case || bLocked)
	{
		return 0;
	}

	int32 Moved = 0;
	TArray<FText> Taken;
	bool bLeftSomething = false;
	for (const FNoFearInventoryEntry& Entry : Storage->GetEntries())
	{
		if (!Entry.Item || Entry.Count <= 0)
		{
			continue;
		}
		const int32 Added = Case->AddItem(Entry.Item, Entry.Count, Entry.MagazineAmmo);
		if (Added > 0)
		{
			Storage->TakeFromEntry(Entry.Id, Added);
			Moved += Added;
			Taken.Add(Added > 1 ? FText::Format(LOCTEXT("TakenStack", "{0} ×{1}"), Entry.Item->GetDisplayName(), Added) : Entry.Item->GetDisplayName());
		}
		bLeftSomething |= Added < Entry.Count;
	}

	if (!Taken.IsEmpty())
	{
		Case->PushNotice(FText::Format(LOCTEXT("Taken", "Взято: {0}"), FText::Join(FText::FromString(TEXT(", ")), Taken)));
	}
	if (bLeftSomething)
	{
		Case->PushNotice(LOCTEXT("CaseFull", "В кейсе нет места — остальное в сундуке"));
	}
	else if (Taken.IsEmpty())
	{
		Case->PushNotice(LOCTEXT("Empty", "Пусто"));
	}
	return Moved;
}

bool ANoFearChest::OpenScreen(APawn* User)
{
	UNoFearInventoryComponent* Case = UNoFearInventoryComponent::FindCase(User);
	if (!Case || bLocked || !bOpen)
	{
		return false;
	}
	LastUser = User;
	Case->OnContainerScreenClosed.AddUniqueDynamic(this, &ANoFearChest::HandleScreenClosed);
	return Case->OpenContainerScreen(Storage);
}

void ANoFearChest::HandleScreenClosed(UNoFearItemContainerComponent* Container)
{
	if (Container != Storage)
	{
		return;
	}
	if (UNoFearInventoryComponent* Case = UNoFearInventoryComponent::FindCase(LastUser.Get()))
	{
		Case->OnContainerScreenClosed.RemoveDynamic(this, &ANoFearChest::HandleScreenClosed);
	}
	// Опустевший сундук закрывается сам, если так настроен: Tick ждёт, пока герой отойдёт.
	if (bOpen && bCloseWhenEmptied && IsEmpty())
	{
		SetActorTickEnabled(true);
	}
}

void ANoFearChest::HandleStorageChanged(UNoFearItemContainerComponent* Container)
{
	OnContentsChanged.Broadcast(this);
}

TArray<FNoFearChestItem> ANoFearChest::GetContents() const
{
	TArray<FNoFearChestItem> Result;
	for (const FNoFearInventoryEntry& Entry : Storage->GetEntries())
	{
		FNoFearChestItem& Item = Result.AddDefaulted_GetRef();
		Item.Item = Entry.Item;
		Item.Count = Entry.Count;
		Item.MagazineAmmo = Entry.MagazineAmmo;
	}
	return Result;
}

bool ANoFearChest::IsEmpty() const
{
	return Storage->IsEmpty();
}

int32 ANoFearChest::GetItemCount() const
{
	int32 Total = 0;
	for (const FNoFearInventoryEntry& Entry : Storage->GetEntries())
	{
		Total += Entry.Count;
	}
	return Total;
}

void ANoFearChest::SetLocked(bool bNewLocked)
{
	if (bNewLocked && bOpen)
	{
		return;
	}
	bLocked = bNewLocked;
	if (bLocked && HasActorBegunPlay())
	{
		Health->SetHealth(LockHealth);
	}
	RefreshShape();
}

bool ANoFearChest::UnlockWithKey(APawn* User)
{
	UNoFearItemData* Key = NoFearWorld::FindKey(User, KeyId);
	if (!bLocked || !Key)
	{
		return false;
	}
	if (bConsumeKey)
	{
		UNoFearInventoryComponent::FindCase(User)->RemoveItem(Key, 1);
	}
	PlayChestSound(LockedSound, NoiseLoudness * 0.3f, NoiseRange * 0.3f, User);
	SetLocked(false);
	return true;
}

void ANoFearChest::BreakLock(AActor* Breaker)
{
	if (!bLocked)
	{
		return;
	}
	bLocked = false;
	RefreshShape();
	PlayChestSound(BreakSound, NoiseLoudness * 2.f, NoiseRange * 1.5f, Breaker);
	// Крышку подбрасывает: открыта, но содержимое никто не забрал.
	bOpen = true;
	SetActorTickEnabled(true);
	OnOpened.Broadcast(this, nullptr);
}

// --- Анимация ---

void ANoFearChest::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	const float Target = bOpen ? 1.f : 0.f;
	const float Step = DeltaSeconds / FMath::Max(LidOpenTime, 0.05f);
	const float Previous = LidAmount;
	LidAmount = FMath::FInterpConstantTo(LidAmount, Target, 1.f, Step);
	RefreshLidPose();
	if (Previous > 0.f && LidAmount <= 0.f)
	{
		PlayChestSound(CloseSound, NoiseLoudness, NoiseRange, nullptr);
	}

	if (bOpen && bCloseWhenEmptied && IsEmpty())
	{
		const APawn* User = LastUser.Get();
		if (!User || FVector::DistSquared(User->GetActorLocation(), GetActorLocation()) > FMath::Square(Interaction->InteractionDistance * 1.5f))
		{
			CloseChest();
		}
	}

	if (FMath::IsNearlyEqual(LidAmount, Target) && !(bOpen && bCloseWhenEmptied))
	{
		SetActorTickEnabled(false);
	}
}

// --- Урон ---

float ANoFearChest::TakeDamage(float DamageAmount, const FDamageEvent& DamageEvent, AController* EventInstigator, AActor* DamageCauser)
{
	// Бьётся только замок: открытому сундуку урон ничего не делает.
	if (!bLocked)
	{
		return 0.f;
	}
	Super::TakeDamage(DamageAmount, DamageEvent, EventInstigator, DamageCauser);
	const float Applied = Health->ProcessDamage(DamageAmount, DamageEvent, EventInstigator, DamageCauser);
	if (Applied > 0.f && bLocked && HitSound)
	{
		FVector Location = GetActorLocation();
		if (DamageEvent.IsOfType(FPointDamageEvent::ClassID))
		{
			Location = static_cast<const FPointDamageEvent&>(DamageEvent).HitInfo.ImpactPoint;
		}
		UGameplayStatics::PlaySoundAtLocation(this, HitSound, Location);
	}
	return Applied;
}

void ANoFearChest::HandleDeath(UNoFearHealthComponent* Component, const FNoFearDamageInfo& Info)
{
	AActor* Breaker = Info.InstigatorController ? static_cast<AActor*>(Info.InstigatorController->GetPawn()) : nullptr;
	BreakLock(Breaker ? Breaker : Info.DamageCauser.Get());
}

void ANoFearChest::PlayChestSound(USoundBase* Sound, float Loudness, float Range, AActor* NoiseSource) const
{
	const FVector Location = GetActorLocation() + FVector(0.f, 0.f, 30.f);
	if (Sound)
	{
		UGameplayStatics::PlaySoundAtLocation(this, Sound, Location);
	}
	if (Loudness > 0.f && Range > 0.f)
	{
		UAISense_Hearing::ReportNoiseEvent(const_cast<ANoFearChest*>(this), Location, Loudness, NoiseSource ? NoiseSource : const_cast<ANoFearChest*>(this), Range, TEXT("Chest"));
	}
}

#undef LOCTEXT_NAMESPACE
