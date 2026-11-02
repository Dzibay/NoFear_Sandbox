#include "World/NoFearBarricade.h"

#include "AIController.h"
#include "AI/NoFearBotSubsystem.h"
#include "AI/NoFearMeleeComponent.h"
#include "AI/NoFearMonsterAttack.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Core/NoFearHealthComponent.h"
#include "Engine/DamageEvents.h"
#include "Engine/OverlapResult.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Inventory/NoFearInventoryComponent.h"
#include "Inventory/NoFearItemData.h"
#include "Kismet/GameplayStatics.h"
#include "NavModifierComponent.h"
#include "Sound/SoundBase.h"
#include "UObject/ConstructorHelpers.h"
#include "World/NoFearDoor.h"
#include "World/NoFearWorldLibrary.h"

#define LOCTEXT_NAMESPACE "NoFearBarricade"

UNoFearNavArea_Barricade::UNoFearNavArea_Barricade()
{
	DefaultCost = 40.f;
	FixedAreaEnteringCost = 3000.f;
	DrawColor = FColor(140, 90, 40);
}

UNoFearHumanNavFilter::UNoFearHumanNavFilter()
{
	FNavigationFilterArea Barricade;
	Barricade.AreaClass = UNoFearNavArea_Barricade::StaticClass();
	Barricade.bIsExcluded = true;
	Areas.Add(Barricade);
}

namespace
{
	TArray<TWeakObjectPtr<ANoFearBarricade>> GBarricades;

	constexpr int32 MaxBoardSlots = 6;
	constexpr double BotUpdateInterval = 0.2;
	constexpr float BoardThickness = 5.f;
	constexpr float BoardHeight = 24.f;
	/** Как часто стучит молоток, пока держат E. */
	constexpr double HammerInterval = 0.45;
	/** Пули своих по доскам: попадают в щели и щепки, а не ломают баррикаду. */
	constexpr float FriendlyPointDamageScale = 0.25f;
}

// ---------------------------------------------------------------------------
// Взаимодействие
// ---------------------------------------------------------------------------

UNoFearBarricadeInteraction::UNoFearBarricadeInteraction()
{
	InteractionDistance = 200.f;
	PromptText = LOCTEXT("Board", "Заколотить (держать)");
}

bool UNoFearBarricadeInteraction::CanInteract_Implementation(APawn* User) const
{
	return Super::CanInteract_Implementation(User) && Cast<ANoFearBarricade>(GetOwner()) != nullptr;
}

FText UNoFearBarricadeInteraction::GetInteractionPrompt_Implementation(APawn* User) const
{
	const ANoFearBarricade* Barricade = Cast<ANoFearBarricade>(GetOwner());
	if (!Barricade || Barricade->CanAddBoard(User))
	{
		return PromptText;
	}
	if (Barricade->IsBarricaded())
	{
		return LOCTEXT("Remove", "Снять доску (держать)");
	}
	return LOCTEXT("NeedPlanks", "Нужны доски");
}

void UNoFearBarricadeInteraction::HandleInteract_Implementation(APawn* User)
{
	if (ANoFearBarricade* Barricade = Cast<ANoFearBarricade>(GetOwner()))
	{
		Barricade->BeginWork(User);
	}
}

// ---------------------------------------------------------------------------
// Баррикада
// ---------------------------------------------------------------------------

ANoFearBarricade::ANoFearBarricade()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = Root;

	for (int32 Index = 0; Index < MaxBoardSlots; ++Index)
	{
		UStaticMeshComponent* Board = CreateDefaultSubobject<UStaticMeshComponent>(*FString::Printf(TEXT("Board%d"), Index));
		Board->SetupAttachment(Root);
		// Доска держит пули и взгляд, но не пешек (их держит PawnBlocker целиком на проём).
		Board->SetCollisionProfileName(TEXT("BlockAllDynamic"));
		Board->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
		Board->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
		Board->SetCanEverAffectNavigation(false);
		Board->SetGenerateOverlapEvents(false);
		BoardMeshes.Add(Board);
	}

	PawnBlocker = CreateDefaultSubobject<UBoxComponent>(TEXT("PawnBlocker"));
	PawnBlocker->SetupAttachment(Root);
	PawnBlocker->SetCollisionObjectType(ECC_WorldDynamic);
	PawnBlocker->SetCollisionResponseToAllChannels(ECR_Ignore);
	PawnBlocker->SetCollisionResponseToChannel(ECC_Pawn, ECR_Block);
	PawnBlocker->SetCanEverAffectNavigation(false);
	PawnBlocker->SetGenerateOverlapEvents(false);
	PawnBlocker->SetHiddenInGame(true);

	HitProxy = CreateDefaultSubobject<UBoxComponent>(TEXT("HitProxy"));
	HitProxy->SetupAttachment(Root);
	HitProxy->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	HitProxy->SetCollisionObjectType(ECC_Pawn);
	HitProxy->SetCollisionResponseToAllChannels(ECR_Ignore);
	HitProxy->SetGenerateOverlapEvents(false);
	HitProxy->SetCanEverAffectNavigation(false);
	HitProxy->SetHiddenInGame(true);

	NavBlocker = CreateDefaultSubobject<UNavModifierComponent>(TEXT("NavBlocker"));
	NavBlocker->AreaClass = UNoFearNavArea_Barricade::StaticClass();

	Health = CreateDefaultSubobject<UNoFearHealthComponent>(TEXT("Health"));
	Health->RegenRate = 0.f;
	Health->bCanBeDowned = false;

	Interaction = CreateDefaultSubobject<UNoFearBarricadeInteraction>(TEXT("Interaction"));

	Tags.Add(UNoFearMonsterAttack::BreakableTag);

	PlanksItem = TSoftObjectPtr<UNoFearItemData>(FSoftObjectPath(TEXT("/Game/Items/DA_Item_Planks.DA_Item_Planks")));

	static ConstructorHelpers::FObjectFinderOptional<UStaticMesh> CubeMesh(TEXT("/Engine/BasicShapes/Cube.Cube"));
	static ConstructorHelpers::FObjectFinderOptional<UMaterialInterface> ShapeMaterial(TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	PlaceholderMesh = CubeMesh.Get();
	PlaceholderMaterial = ShapeMaterial.Get();

	static ConstructorHelpers::FObjectFinderOptional<USoundBase> HammerFinder(TEXT("/Game/External/LyraContent/Audio/Sounds/Weapons/Shotgun/Weapons_Shotgun_HandHit_01.Weapons_Shotgun_HandHit_01"));
	static ConstructorHelpers::FObjectFinderOptional<USoundBase> HitFinder(TEXT("/Game/External/LyraContent/Audio/Sounds/Impacts/Lyra_ImpactPlaster_02.Lyra_ImpactPlaster_02"));
	static ConstructorHelpers::FObjectFinderOptional<USoundBase> BreakFinder(TEXT("/Game/External/LyraContent/Audio/Sounds/Impacts/Lyra_ImpactPlasterDebris_03.Lyra_ImpactPlasterDebris_03"));
	HammerSound = HammerFinder.Get();
	HitSound = HitFinder.Get();
	BreakSound = BreakFinder.Get();
}

void ANoFearBarricade::GetBarricades(const UWorld* World, TArray<ANoFearBarricade*>& OutBarricades)
{
	OutBarricades.Reset();
	for (const TWeakObjectPtr<ANoFearBarricade>& Entry : GBarricades)
	{
		if (ANoFearBarricade* Barricade = Entry.Get(); Barricade && Barricade->GetWorld() == World)
		{
			OutBarricades.Add(Barricade);
		}
	}
}

ANoFearBarricade* ANoFearBarricade::FindForDoor(const ANoFearDoor* Door)
{
	if (!Door)
	{
		return nullptr;
	}
	for (const TWeakObjectPtr<ANoFearBarricade>& Entry : GBarricades)
	{
		if (ANoFearBarricade* Barricade = Entry.Get(); Barricade && Barricade->LinkedDoor == Door && Barricade->IsBarricaded())
		{
			return Barricade;
		}
	}
	return nullptr;
}

void ANoFearBarricade::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	Boards = FMath::Clamp(InitialBoards, 0, FMath::Min(MaxBoards, MaxBoardSlots));
	RefreshShape();
	// В редакторе тоже: при запекании навмеша вырезан проём только заколоченной баррикады.
	RefreshState();
}

void ANoFearBarricade::BeginPlay()
{
	Super::BeginPlay();
	GBarricades.AddUnique(this);
	MaxBoards = FMath::Clamp(MaxBoards, 1, MaxBoardSlots);
	Boards = FMath::Clamp(InitialBoards, 0, MaxBoards);
	TopBoardHealth = Boards > 0 ? BoardHealth : 0.f;
	RefreshShape();
	RefreshState();
}

void ANoFearBarricade::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	GBarricades.RemoveAll([this](const TWeakObjectPtr<ANoFearBarricade>& Entry)
	{
		return !Entry.IsValid() || Entry.Get() == this;
	});
	Super::EndPlay(EndPlayReason);
}

void ANoFearBarricade::RefreshShape()
{
	const int32 Slots = FMath::Clamp(MaxBoards, 1, MaxBoardSlots);
	UStaticMesh* Mesh = BoardMesh ? BoardMesh.Get() : PlaceholderMesh.Get();
	for (int32 Index = 0; Index < BoardMeshes.Num(); ++Index)
	{
		UStaticMeshComponent* Board = BoardMeshes[Index];
		// Доски снизу вверх от четверти до трёх четвертей высоты, чуть наискосок, на стороне +X.
		const float Alpha = Slots > 1 ? static_cast<float>(Index) / (Slots - 1) : 0.5f;
		const FVector Center(BoardThickness * 0.5f + 2.f, 0.f, Height * FMath::Lerp(0.25f, 0.75f, Alpha));
		NoFearWorld::FitMesh(Board, Mesh, FVector(BoardThickness, Width + 30.f, BoardHeight), FVector::ZeroVector);
		const FVector Offset = Board->GetRelativeLocation();
		const FRotator Tilt(0.f, 0.f, (Index % 2 == 0 ? 1.f : -1.f) * 7.f);
		Board->SetRelativeRotation(Tilt);
		Board->SetRelativeLocation(Center + Tilt.RotateVector(Offset));
		if (!BoardMesh)
		{
			NoFearWorld::TintPlaceholder(Board, PlaceholderMaterial, BoardColor * (Index % 2 == 0 ? 1.f : 0.85f));
		}
	}
	const FVector Extent(10.f, Width * 0.5f, Height * 0.5f);
	PawnBlocker->SetBoxExtent(Extent);
	PawnBlocker->SetRelativeLocation(FVector(0.f, 0.f, Height * 0.5f));
	HitProxy->SetBoxExtent(Extent + FVector(15.f, 0.f, 0.f));
	HitProxy->SetRelativeLocation(FVector(0.f, 0.f, Height * 0.5f));
	// Модификатор навмеша берёт коробку вокруг точки актора: проём с запасом по толщине стены и до пола.
	NavBlocker->FailsafeExtent = FVector(50.f, Width * 0.5f, Height * 0.5f + 30.f);
}

void ANoFearBarricade::RefreshState()
{
	for (int32 Index = 0; Index < BoardMeshes.Num(); ++Index)
	{
		const bool bUp = Index < Boards;
		BoardMeshes[Index]->SetVisibility(bUp);
		BoardMeshes[Index]->SetCollisionEnabled(bUp ? ECollisionEnabled::QueryAndPhysics : ECollisionEnabled::NoCollision);
	}
	const bool bBlocked = Boards > 0;
	PawnBlocker->SetCollisionEnabled(bBlocked ? ECollisionEnabled::QueryAndPhysics : ECollisionEnabled::NoCollision);
	HitProxy->SetCollisionEnabled(bBlocked ? ECollisionEnabled::QueryOnly : ECollisionEnabled::NoCollision);
	NavBlocker->SetNavigationRelevancy(bBlocked);
}

void ANoFearBarricade::SetBoards(int32 NewBoards)
{
	NewBoards = FMath::Clamp(NewBoards, 0, MaxBoards);
	if (NewBoards == Boards)
	{
		return;
	}
	const bool bAdded = NewBoards > Boards;
	Boards = NewBoards;
	// Новая верхняя доска — целая; после снятия или поломки верхней становится следующая, тоже целая.
	TopBoardHealth = Boards > 0 ? BoardHealth : 0.f;
	if (!bAdded && Boards == 0)
	{
		TopBoardHealth = 0.f;
	}
	RefreshState();
	OnBoardsChanged.Broadcast(this, Boards);
}

UNoFearItemData* ANoFearBarricade::GetPlanks() const
{
	return NoFearWorld::LoadItem(PlanksItem);
}

bool ANoFearBarricade::CanAddBoard(const APawn* User) const
{
	if (Boards >= MaxBoards)
	{
		return false;
	}
	const UNoFearItemData* Planks = GetPlanks();
	const UNoFearInventoryComponent* Case = UNoFearInventoryComponent::FindCase(User);
	return Planks && Case && Case->CountItem(Planks) > 0;
}

bool ANoFearBarricade::AddBoard(APawn* User)
{
	if (Boards >= MaxBoards)
	{
		return false;
	}
	if (User)
	{
		UNoFearItemData* Planks = GetPlanks();
		UNoFearInventoryComponent* Case = UNoFearInventoryComponent::FindCase(User);
		if (!Planks || !Case || Case->RemoveItem(Planks, 1) < 1)
		{
			NoFearWorld::Notify(User, LOCTEXT("NoPlanks", "Нужны доски"));
			return false;
		}
	}
	SetBoards(Boards + 1);
	NoFearWorld::PlaySoundAndNoise(this, HammerSound, GetActorLocation() + FVector(0.f, 0.f, Height * 0.5f), NoiseLoudness, NoiseRange, User, TEXT("Barricade"));
	return true;
}

bool ANoFearBarricade::RemoveBoard(APawn* User)
{
	if (Boards <= 0)
	{
		return false;
	}
	SetBoards(Boards - 1);
	if (User)
	{
		if (UNoFearInventoryComponent* Case = UNoFearInventoryComponent::FindCase(User))
		{
			if (UNoFearItemData* Planks = GetPlanks())
			{
				Case->AddItemOrDrop(Planks, 1);
			}
		}
	}
	NoFearWorld::PlaySoundAndNoise(this, HitSound, GetActorLocation() + FVector(0.f, 0.f, Height * 0.5f), NoiseLoudness * 0.5f, NoiseRange * 0.5f, User, TEXT("Barricade"));
	return true;
}

void ANoFearBarricade::BeginWork(APawn* User)
{
	if (!User)
	{
		return;
	}
	if (!CanAddBoard(User) && Boards <= 0)
	{
		NoFearWorld::Notify(User, LOCTEXT("NoPlanks", "Нужны доски"));
		return;
	}
	Worker = User;
	bWorkAdds = CanAddBoard(User);
	WorkTime = 0.f;
}

float ANoFearBarricade::GetWorkProgress() const
{
	return Worker.IsValid() ? FMath::Clamp(WorkTime / FMath::Max(BoardTime, 0.1f), 0.f, 1.f) : 0.f;
}

float ANoFearBarricade::GetTopBoardHealthFraction() const
{
	return Boards > 0 ? FMath::Clamp(TopBoardHealth / FMath::Max(BoardHealth, 1.f), 0.f, 1.f) : 0.f;
}

int32 ANoFearBarricade::GetSideOf(const FVector& Location) const
{
	return GetActorTransform().InverseTransformPosition(Location).X >= 0.f ? 1 : -1;
}

void ANoFearBarricade::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	UpdateWork(DeltaSeconds);

	const double Now = GetWorld()->GetTimeSeconds();
	if (Boards > 0 && Now - LastBotUpdateTime >= BotUpdateInterval)
	{
		LastBotUpdateTime = Now;
		UpdateBots();
	}
}

void ANoFearBarricade::UpdateWork(float DeltaSeconds)
{
	APawn* User = Worker.Get();
	if (!User)
	{
		return;
	}
	// Отпустил E, отошёл или упал — работа сорвалась.
	const bool bValid = NoFearWorld::IsInteractHeld(User) && Interaction->IsInRange(User) && !UNoFearHealthComponent::IsActorIncapacitated(User)
		&& (bWorkAdds ? CanAddBoard(User) : Boards > 0);
	if (!bValid)
	{
		Worker.Reset();
		WorkTime = 0.f;
		return;
	}
	WorkTime += DeltaSeconds;

	const double Now = GetWorld()->GetTimeSeconds();
	if (bWorkAdds && Now - LastHammerTime >= HammerInterval)
	{
		LastHammerTime = Now;
		NoFearWorld::PlaySoundAndNoise(this, HammerSound, GetActorLocation() + FVector(0.f, 0.f, Height * 0.5f), NoiseLoudness, NoiseRange, User, TEXT("Barricade"), 0.7f);
	}
	if (WorkTime >= BoardTime)
	{
		Worker.Reset();
		WorkTime = 0.f;
		if (bWorkAdds)
		{
			AddBoard(User);
		}
		else
		{
			RemoveBoard(User);
		}
	}
}

void ANoFearBarricade::UpdateBots()
{
	UWorld* World = GetWorld();
	const FVector Center = GetActorLocation() + GetActorUpVector() * Height * 0.5f;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(NoFearBarricadeBots), false, this);
	TArray<FOverlapResult> Overlaps;
	World->OverlapMultiByObjectType(Overlaps, Center, FQuat::Identity, FCollisionObjectQueryParams(ECC_Pawn),
		FCollisionShape::MakeSphere(BotReactDistance + FMath::Max(Width, Height) * 0.5f), Params);

	const FTransform PlaneTransform(GetActorQuat(), Center);
	const FBox Opening(FVector(-50.f, -Width * 0.5f, -Height * 0.5f - 60.f), FVector(50.f, Width * 0.5f, Height * 0.5f + 60.f));
	TSet<APawn*> Seen;
	for (const FOverlapResult& Overlap : Overlaps)
	{
		APawn* Pawn = Cast<APawn>(Overlap.GetActor());
		AAIController* AI = Pawn ? Cast<AAIController>(Pawn->GetController()) : nullptr;
		if (!AI || Seen.Contains(Pawn) || !NoFearWorld::IsMonsterTeam(Pawn) || UNoFearHealthComponent::IsActorIncapacitated(Pawn))
		{
			continue;
		}
		Seen.Add(Pawn);
		if (FMath::Abs(PlaneTransform.InverseTransformPosition(Pawn->GetActorLocation()).X) > BotReactDistance)
		{
			continue;
		}
		UNoFearMeleeComponent* Melee = Pawn->FindComponentByClass<UNoFearMeleeComponent>();
		if (Melee && !Melee->IsAttacking()
			&& (NoFearWorld::IsPathThroughBox(AI, Pawn, PlaneTransform, Opening, BotReactDistance * 2.f + FMath::Max(Width, Height))
				|| NoFearWorld::IsFocusBehindPlane(AI, Pawn, PlaneTransform, Width * 0.5f, Height * 0.5f)))
		{
			if (Melee->StartAttack(this))
			{
				++MonsterAttackCount;
			}
		}
	}
}

float ANoFearBarricade::TakeDamage(float DamageAmount, const FDamageEvent& DamageEvent, AController* EventInstigator, AActor* DamageCauser)
{
	if (Boards <= 0 || DamageAmount <= 0.f)
	{
		return 0.f;
	}
	Super::TakeDamage(DamageAmount, DamageEvent, EventInstigator, DamageCauser);

	AActor* Attacker = EventInstigator && EventInstigator->GetPawn() ? static_cast<AActor*>(EventInstigator->GetPawn()) : DamageCauser;
	float Applied = DamageAmount;
	if (DamageEvent.IsOfType(FPointDamageEvent::ClassID) && !NoFearWorld::IsMonsterTeam(Attacker))
	{
		Applied *= FriendlyPointDamageScale;
	}
	FVector Location = GetActorLocation() + FVector(0.f, 0.f, Height * 0.5f);
	if (DamageEvent.IsOfType(FPointDamageEvent::ClassID))
	{
		Location = static_cast<const FPointDamageEvent&>(DamageEvent).HitInfo.ImpactPoint;
	}

	TopBoardHealth -= Applied;
	if (TopBoardHealth <= 0.f)
	{
		BreakTopBoard(Attacker);
	}
	else if (HitSound)
	{
		UGameplayStatics::PlaySoundAtLocation(this, HitSound, Location);
	}
	return Applied;
}

void ANoFearBarricade::BreakTopBoard(AActor* Breaker)
{
	if (Boards <= 0)
	{
		return;
	}
	const int32 Index = Boards - 1;
	const FTransform BoardTransform = BoardMeshes[Index]->GetComponentTransform();
	FVector Push = FVector::ZeroVector;
	if (Breaker)
	{
		Push = -static_cast<float>(GetSideOf(Breaker->GetActorLocation())) * GetActorForwardVector();
	}
	SetBoards(Boards - 1);

	const FVector Location = BoardTransform.GetLocation();
	NoFearWorld::PlaySoundAndNoise(this, BreakSound, Location, NoiseLoudness * 1.5f, NoiseRange, Breaker, TEXT("Barricade"));
	NoFearWorld::SpawnDebris(this, FTransform(GetActorQuat(), Location), FVector(BoardThickness, Width, BoardHeight), FVector(4.f, 25.f, 8.f), 5, Push, 6.f, PlaceholderMaterial, BoardColor);
	if (Boards == 0)
	{
		UNoFearBotSubsystem::ReportDanger(this, ENoFearDangerType::Fight, Breaker ? Breaker : this, Location, UNoFearBotSubsystem::FightNoticeRadius, Location);
	}
}

#undef LOCTEXT_NAMESPACE
