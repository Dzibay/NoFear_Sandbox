#include "World/NoFearDoor.h"

#include "AIController.h"
#include "AI/NoFearBotSubsystem.h"
#include "AI/NoFearMeleeComponent.h"
#include "AI/NoFearMonsterAttack.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Core/NoFearHealthComponent.h"
#include "Core/NoFearTeam.h"
#include "Engine/DamageEvents.h"
#include "Engine/OverlapResult.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "NavAreas/NavArea_Null.h"
#include "NavModifierComponent.h"
#include "Navigation/PathFollowingComponent.h"
#include "Perception/AISense_Hearing.h"
#include "Sound/SoundBase.h"
#include "UObject/ConstructorHelpers.h"
#include "Inventory/NoFearInventoryComponent.h"
#include "World/NoFearBarricade.h"
#include "World/NoFearWorldLibrary.h"

#define LOCTEXT_NAMESPACE "NoFearDoor"

namespace
{
	TArray<TWeakObjectPtr<ANoFearDoor>> GDoors;

	/** Как часто дверь смотрит на ботов рядом. */
	constexpr double BotUpdateInterval = 0.2;

	float EaseInOut(float Alpha)
	{
		return FMath::InterpEaseInOut(0.f, 1.f, FMath::Clamp(Alpha, 0.f, 1.f), 2.f);
	}

	bool IsMonsterTeam(const AActor* Actor)
	{
		const ENoFearTeam Team = UNoFearTeamLibrary::GetTeam(Actor);
		return Team == ENoFearTeam::Monster || Team == ENoFearTeam::AggroMonster;
	}
}

// ---------------------------------------------------------------------------
// Взаимодействие
// ---------------------------------------------------------------------------

UNoFearDoorInteraction::UNoFearDoorInteraction()
{
	InteractionDistance = 250.f;
	PromptText = LOCTEXT("Open", "Открыть");
}

bool UNoFearDoorInteraction::CanInteract_Implementation(APawn* User) const
{
	const ANoFearDoor* Door = Cast<ANoFearDoor>(GetOwner());
	return Super::CanInteract_Implementation(User) && Door && !Door->IsBroken();
}

FText UNoFearDoorInteraction::GetInteractionPrompt_Implementation(APawn* User) const
{
	const ANoFearDoor* Door = Cast<ANoFearDoor>(GetOwner());
	if (!Door)
	{
		return PromptText;
	}
	if (Door->IsLocked())
	{
		const UNoFearItemData* Key = NoFearWorld::FindKey(User, Door->KeyId);
		return Key ? NoFearWorld::UnlockPrompt(Key) : LOCTEXT("Locked", "Заперто");
	}
	if (!Door->IsOpen() && ANoFearBarricade::FindForDoor(Door))
	{
		return LOCTEXT("Boarded", "Заколочено");
	}
	return Door->IsOpen() ? LOCTEXT("Close", "Закрыть") : PromptText;
}

void UNoFearDoorInteraction::HandleInteract_Implementation(APawn* User)
{
	ANoFearDoor* Door = Cast<ANoFearDoor>(GetOwner());
	if (!Door)
	{
		return;
	}
	if (Door->IsLocked() && Door->UnlockWithKey(User))
	{
		return;
	}
	if (Door->IsLocked() || (!Door->IsOpen() && ANoFearBarricade::FindForDoor(Door)))
	{
		Door->RattleLocked(User);
	}
	else
	{
		Door->ToggleDoor(User);
	}
}

// ---------------------------------------------------------------------------
// Дверь
// ---------------------------------------------------------------------------

ANoFearDoor::ANoFearDoor()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = Root;

	Frame = CreateDefaultSubobject<USceneComponent>(TEXT("Frame"));
	Frame->SetupAttachment(Root);

	LeftHinge = CreateDefaultSubobject<USceneComponent>(TEXT("LeftHinge"));
	LeftHinge->SetupAttachment(Frame);
	RightHinge = CreateDefaultSubobject<USceneComponent>(TEXT("RightHinge"));
	RightHinge->SetupAttachment(Frame);

	auto MakeLeaf = [this](const TCHAR* Name, USceneComponent* Parent)
	{
		UStaticMeshComponent* Leaf = CreateDefaultSubobject<UStaticMeshComponent>(Name);
		Leaf->SetupAttachment(Parent);
		Leaf->SetCollisionProfileName(TEXT("BlockAllDynamic"));
		Leaf->SetCanEverAffectNavigation(false);
		Leaf->SetGenerateOverlapEvents(false);
		return Leaf;
	};
	LeftLeaf = MakeLeaf(TEXT("LeftLeaf"), LeftHinge);
	RightLeaf = MakeLeaf(TEXT("RightLeaf"), RightHinge);

	HitProxy = CreateDefaultSubobject<UBoxComponent>(TEXT("HitProxy"));
	HitProxy->SetupAttachment(Frame);
	HitProxy->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	HitProxy->SetCollisionObjectType(ECC_Pawn);
	HitProxy->SetCollisionResponseToAllChannels(ECR_Ignore);
	HitProxy->SetGenerateOverlapEvents(false);
	HitProxy->SetCanEverAffectNavigation(false);
	HitProxy->SetHiddenInGame(true);

	NavBlocker = CreateDefaultSubobject<UNavModifierComponent>(TEXT("NavBlocker"));
	NavBlocker->AreaClass = UNavArea_Null::StaticClass();

	Health = CreateDefaultSubobject<UNoFearHealthComponent>(TEXT("Health"));
	Health->RegenRate = 0.f;
	Health->bCanBeDowned = false;

	Interaction = CreateDefaultSubobject<UNoFearDoorInteraction>(TEXT("Interaction"));

	// Таран колосса сносит такие актёры целиком (UNoFearMonsterAttack::BreakCover).
	Tags.Add(UNoFearMonsterAttack::BreakableTag);

	static ConstructorHelpers::FObjectFinderOptional<UStaticMesh> CubeMesh(TEXT("/Engine/BasicShapes/Cube.Cube"));
	static ConstructorHelpers::FObjectFinderOptional<UMaterialInterface> ShapeMaterial(TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	PlaceholderMesh = CubeMesh.Get();
	PlaceholderMaterial = ShapeMaterial.Get();

	static ConstructorHelpers::FObjectFinderOptional<USoundBase> OpenFinder(TEXT("/Game/Audio/Foley/SoundWaves/Hand/Concrete/Foley_hs_1p_barehand_concrete_plant_03.Foley_hs_1p_barehand_concrete_plant_03"));
	static ConstructorHelpers::FObjectFinderOptional<USoundBase> CloseFinder(TEXT("/Game/External/LyraContent/Audio/Sounds/Weapons/Shotgun/Weapons_Shotgun_HandHit_01.Weapons_Shotgun_HandHit_01"));
	static ConstructorHelpers::FObjectFinderOptional<USoundBase> LockedFinder(TEXT("/Game/External/LyraContent/Audio/Sounds/Weapons/Pistol/Weapons_Pistol_Slide_01.Weapons_Pistol_Slide_01"));
	static ConstructorHelpers::FObjectFinderOptional<USoundBase> HitFinder(TEXT("/Game/External/LyraContent/Audio/Sounds/Impacts/Lyra_ImpactPlaster_02.Lyra_ImpactPlaster_02"));
	static ConstructorHelpers::FObjectFinderOptional<USoundBase> BreakFinder(TEXT("/Game/External/LyraContent/Audio/Sounds/Impacts/Lyra_ImpactPlasterDebris_03.Lyra_ImpactPlasterDebris_03"));
	OpenSound = OpenFinder.Get();
	CloseSound = CloseFinder.Get();
	LockedSound = LockedFinder.Get();
	HitSound = HitFinder.Get();
	BreakSound = BreakFinder.Get();
}

void ANoFearDoor::GetDoors(const UWorld* World, TArray<ANoFearDoor*>& OutDoors)
{
	OutDoors.Reset();
	for (const TWeakObjectPtr<ANoFearDoor>& Entry : GDoors)
	{
		if (ANoFearDoor* Door = Entry.Get(); Door && Door->GetWorld() == World)
		{
			OutDoors.Add(Door);
		}
	}
}

void ANoFearDoor::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	RefreshShape();
	// В редакторе тоже: при запекании навмеша вырезан только проём запертых дверей.
	RefreshNavigation();
}

void ANoFearDoor::BeginPlay()
{
	Super::BeginPlay();
	GDoors.AddUnique(this);

	Health->MaxHealth = MaxHealth;
	Health->SetHealth(MaxHealth);
	Health->OnDeath.AddDynamic(this, &ANoFearDoor::HandleDeath);

	RefreshShape();
	if (bStartOpen && !bLocked)
	{
		SwingSign = OpenSide >= 0 ? 1.f : -1.f;
		OpenAmount = 1.f;
		State = ENoFearDoorState::Open;
	}
	RefreshLeafPose();
	RefreshCollision();
	RefreshNavigation();
}

void ANoFearDoor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	GDoors.RemoveAll([this](const TWeakObjectPtr<ANoFearDoor>& Entry)
	{
		return !Entry.IsValid() || Entry.Get() == this;
	});
	Super::EndPlay(EndPlayReason);
}

void ANoFearDoor::Destroyed()
{
	// Снесли целиком (таран колосса): обломки и грохот, как при поломке.
	UWorld* World = GetWorld();
	if (World && World->IsGameWorld() && !World->bIsTearingDown && HasActorBegunPlay() && State != ENoFearDoorState::Broken)
	{
		State = ENoFearDoorState::Broken;
		OnDoorStateChanged.Broadcast(this, State);
		PlayDoorSound(BreakSound, NoiseLoudness * 2.5f, NoiseRange * 2.f, nullptr);
		SpawnDebris(FVector::ZeroVector);
	}
	Super::Destroyed();
}

// --- Форма ---

FVector ANoFearDoor::GetNavBlockerExtent() const
{
	// Проём с запасом по толщине стены и до пола.
	return FVector(Thickness * 0.5f + 40.f, Width * 0.5f, Height * 0.5f + 30.f);
}

void ANoFearDoor::RefreshShape()
{
	Frame->SetRelativeLocationAndRotation(GetFrameOffset(), GetFrameRotation());

	const float LeafWidth = bDoubleDoor ? Width * 0.5f : Width;
	LeftHinge->SetRelativeLocation(FVector(0.f, -Width * 0.5f, 0.f));
	RightHinge->SetRelativeLocation(FVector(0.f, Width * 0.5f, 0.f));

	UStaticMesh* Mesh = LeafMesh ? LeafMesh.Get() : PlaceholderMesh.Get();
	const FBox MeshBox = Mesh ? Mesh->GetBoundingBox() : FBox(FVector(-50.f), FVector(50.f));
	const FVector MeshSize = MeshBox.GetSize().ComponentMax(FVector(1.f));
	// Створка в осях петли: X — толщина, Y — от петли к краю, Z — высота. Чуть меньше проёма, чтобы не тёрлась о стены.
	const FVector LeafSize(Thickness, LeafWidth - 1.f, Height - 2.f);
	const FVector Scale = LeafSize / MeshSize;

	auto PlaceLeaf = [&](UStaticMeshComponent* Leaf, float Direction)
	{
		Leaf->SetStaticMesh(Mesh);
		Leaf->SetRelativeScale3D(Scale);
		// Направление Direction: +1 — створка уходит от петли в +Y, -1 — в -Y.
		const FVector Center(0.f, Direction * (LeafSize.Y * 0.5f + 0.5f), 0.f);
		Leaf->SetRelativeLocation(Center - MeshBox.GetCenter() * Scale);

		Leaf->EmptyOverrideMaterials();
		if (LeafMaterial)
		{
			Leaf->SetMaterial(0, LeafMaterial);
		}
		else if (PlaceholderMaterial && !LeafMesh)
		{
			if (UMaterialInstanceDynamic* Material = Leaf->CreateDynamicMaterialInstance(0, PlaceholderMaterial))
			{
				Material->SetVectorParameterValue(TEXT("Color"), IsLocked() ? LockedColor : LeafColor);
			}
		}
	};
	PlaceLeaf(LeftLeaf, 1.f);
	PlaceLeaf(RightLeaf, -1.f);
	RightLeaf->SetVisibility(bDoubleDoor && State != ENoFearDoorState::Broken);
	LeftLeaf->SetVisibility(State != ENoFearDoorState::Broken);

	HitProxy->SetBoxExtent(FVector(Thickness * 0.5f + 15.f, Width * 0.5f, Height * 0.5f));
	NavBlocker->FailsafeExtent = GetNavBlockerExtent();
}

void ANoFearDoor::RefreshLeafPose()
{
	const float Angle = OpenAngle * EaseInOut(OpenAmount) * SwingSign;
	// Левая створка уходит от петли в +Y: поворот на -угол открывает её в сторону +X рамы, правая — наоборот.
	LeftHinge->SetRelativeRotation(FRotator(0.f, -Angle, 0.f));
	RightHinge->SetRelativeRotation(FRotator(0.f, Angle, 0.f));
}

void ANoFearDoor::RefreshCollision()
{
	const bool bBroken = State == ENoFearDoorState::Broken;
	// Пока створка движется, сквозь неё можно пройти: иначе она толкает и застревает в людях.
	const bool bMoving = State == ENoFearDoorState::Opening || State == ENoFearDoorState::Closing;
	for (UStaticMeshComponent* Leaf : { LeftLeaf.Get(), RightLeaf.Get() })
	{
		const bool bActive = !bBroken && (Leaf == LeftLeaf || bDoubleDoor);
		Leaf->SetCollisionEnabled(bActive ? ECollisionEnabled::QueryAndPhysics : ECollisionEnabled::NoCollision);
		Leaf->SetCollisionResponseToChannel(ECC_Pawn, bMoving ? ECR_Ignore : ECR_Block);
	}
	HitProxy->SetCollisionEnabled(State == ENoFearDoorState::Closed ? ECollisionEnabled::QueryOnly : ECollisionEnabled::NoCollision);
}

void ANoFearDoor::RefreshNavigation()
{
	// Навмеш вырезан, только пока дверь заперта и цела. Нужен RuntimeGeneration = DynamicModifiersOnly.
	NavBlocker->SetNavigationRelevancy(IsLocked());
}

// --- Состояние ---

void ANoFearDoor::SetState(ENoFearDoorState NewState)
{
	if (State == NewState)
	{
		return;
	}
	State = NewState;
	SetActorTickEnabled(true);
	RefreshCollision();
	OnDoorStateChanged.Broadcast(this, State);
}

bool ANoFearDoor::OpenDoor(AActor* User)
{
	// Заколоченную не открыть, пока стоят доски.
	if (IsBroken() || IsLocked() || IsOpen() || ANoFearBarricade::FindForDoor(this))
	{
		return false;
	}
	// Закрытую — от себя. Закрывающуюся разворачиваем обратно в ту же сторону.
	if (State == ENoFearDoorState::Closed || OpenAmount <= KINDA_SMALL_NUMBER)
	{
		SwingSign = OpenSide >= 0 ? 1.f : -1.f;
		if (bSwingBothWays && User)
		{
			SwingSign = -static_cast<float>(GetSideOf(User->GetActorLocation()));
		}
	}
	bOpenedByBot = false;
	OpenedTime = GetWorld()->GetTimeSeconds();
	SetState(ENoFearDoorState::Opening);
	PlayDoorSound(OpenSound, NoiseLoudness, NoiseRange, User);
	return true;
}

bool ANoFearDoor::CloseDoor(AActor* User)
{
	if (IsBroken() || State == ENoFearDoorState::Closed || State == ENoFearDoorState::Closing)
	{
		return false;
	}
	SetState(ENoFearDoorState::Closing);
	return true;
}

bool ANoFearDoor::ToggleDoor(AActor* User)
{
	return IsOpen() ? CloseDoor(User) : OpenDoor(User);
}

void ANoFearDoor::SetLocked(bool bNewLocked)
{
	if (IsBroken() || (bNewLocked && State != ENoFearDoorState::Closed))
	{
		return;
	}
	const bool bChanged = bLocked != bNewLocked;
	bLocked = bNewLocked;
	RefreshShape();
	RefreshNavigation();
	if (bChanged)
	{
		OnLockedChanged.Broadcast(this, bLocked);
	}
}

bool ANoFearDoor::HasKey(const APawn* User) const
{
	return NoFearWorld::FindKey(User, KeyId) != nullptr;
}

bool ANoFearDoor::UnlockWithKey(APawn* User)
{
	UNoFearItemData* Key = NoFearWorld::FindKey(User, KeyId);
	if (!IsLocked() || !Key)
	{
		return false;
	}
	if (bConsumeKey)
	{
		UNoFearInventoryComponent::FindCase(User)->RemoveItem(Key, 1);
	}
	// Щелчок замка слышно рядом.
	PlayDoorSound(LockedSound, NoiseLoudness * 0.3f, NoiseRange * 0.3f, User);
	SetLocked(false);
	OpenDoor(User);
	return true;
}

void ANoFearDoor::RattleLocked(AActor* User)
{
	PlayDoorSound(LockedSound, NoiseLoudness * 0.7f, NoiseRange * 0.7f, User);
}

float ANoFearDoor::GetHealthFraction() const
{
	return IsBroken() ? 0.f : Health->GetHealthFraction();
}

void ANoFearDoor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (State == ENoFearDoorState::Opening || State == ENoFearDoorState::Closing)
	{
		const float Step = DeltaSeconds / FMath::Max(OpenTime, 0.05f);
		if (State == ENoFearDoorState::Opening)
		{
			OpenAmount = FMath::Min(1.f, OpenAmount + Step);
			if (OpenAmount >= 1.f)
			{
				SetState(ENoFearDoorState::Open);
			}
		}
		else
		{
			OpenAmount = FMath::Max(0.f, OpenAmount - Step);
			if (OpenAmount <= 0.f)
			{
				SetState(ENoFearDoorState::Closed);
				PlayDoorSound(CloseSound, NoiseLoudness, NoiseRange, nullptr);
			}
		}
		RefreshLeafPose();
	}

	const double Now = GetWorld()->GetTimeSeconds();
	if (Now - LastBotUpdateTime >= BotUpdateInterval)
	{
		LastBotUpdateTime = Now;
		UpdateBots();

		// Закрыть за ботом или по таймеру — когда в проёме и под створками никого.
		if (State == ENoFearDoorState::Open)
		{
			const double Elapsed = Now - OpenedTime;
			const bool bAutoClose = AutoCloseDelay > 0.f && Elapsed >= AutoCloseDelay;
			const bool bBotClose = bOpenedByBot && BotCloseDelay > 0.f && Elapsed >= BotCloseDelay;
			if ((bAutoClose || bBotClose) && IsDoorwayClear())
			{
				CloseDoor(nullptr);
			}
		}
	}
}

// --- Боты ---

void ANoFearDoor::UpdateBots()
{
	UWorld* World = GetWorld();
	if (!World || IsBroken() || (!bBotsOpen && !bMonstersBreak))
	{
		return;
	}

	const FVector Center = Frame->GetComponentLocation();
	const float Radius = BotReactDistance + FMath::Max(Width, Height) * 0.5f;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(NoFearDoorBots), false, this);
	TArray<FOverlapResult> Overlaps;
	World->OverlapMultiByObjectType(Overlaps, Center, FQuat::Identity, FCollisionObjectQueryParams(ECC_Pawn), FCollisionShape::MakeSphere(Radius), Params);

	TSet<APawn*> Seen;
	for (const FOverlapResult& Overlap : Overlaps)
	{
		APawn* Pawn = Cast<APawn>(Overlap.GetActor());
		AAIController* AI = Pawn ? Cast<AAIController>(Pawn->GetController()) : nullptr;
		if (!AI || Seen.Contains(Pawn) || UNoFearHealthComponent::IsActorIncapacitated(Pawn))
		{
			continue;
		}
		Seen.Add(Pawn);

		// Далеко от плоскости проёма — пока не наша забота.
		const FVector Local = Frame->GetComponentTransform().InverseTransformPosition(Pawn->GetActorLocation());
		if (FMath::Abs(Local.X) > BotReactDistance)
		{
			continue;
		}

		if (IsMonsterTeam(Pawn))
		{
			if (!bMonstersBreak || State != ENoFearDoorState::Closed)
			{
				continue;
			}
			UNoFearMeleeComponent* Melee = Pawn->FindComponentByClass<UNoFearMeleeComponent>();
			if (Melee && !Melee->IsAttacking() && (IsPathThroughDoor(AI, Pawn) || IsTargetBehindDoor(AI, Pawn)))
			{
				Melee->StartAttack(this);
			}
			continue;
		}

		if (!bBotsOpen || IsLocked() || !IsPathThroughDoor(AI, Pawn))
		{
			continue;
		}
		if (IsOpen())
		{
			// Кто-то ещё идёт через проём: не закрывать перед носом.
			OpenedTime = World->GetTimeSeconds();
		}
		else if (OpenDoor(Pawn))
		{
			bOpenedByBot = true;
			++BotOpenCount;
		}
	}
}

bool ANoFearDoor::IsPathThroughDoor(const AAIController* AI, const APawn* Pawn) const
{
	const UPathFollowingComponent* PathFollowing = AI ? AI->GetPathFollowingComponent() : nullptr;
	if (!PathFollowing || PathFollowing->GetStatus() != EPathFollowingStatus::Moving)
	{
		return false;
	}
	const FNavPathSharedPtr Path = PathFollowing->GetPath();
	if (!Path.IsValid())
	{
		return false;
	}
	const TArray<FNavPathPoint>& Points = Path->GetPathPoints();
	const int32 First = static_cast<int32>(PathFollowing->GetCurrentPathIndex()) + 1;
	FVector Previous = Pawn->GetNavAgentLocation();
	float Length = 0.f;
	// Смотрим только ближайшие метры пути: дверь в другом конце маршрута открывать рано.
	const float MaxLength = BotReactDistance * 2.f + FMath::Max(Width, Height);
	for (int32 Index = First; Index < Points.Num() && Length < MaxLength; ++Index)
	{
		const FVector Point = Points[Index].Location;
		if (SegmentCrossesDoorway(Previous, Point))
		{
			return true;
		}
		Length += FVector::Dist(Previous, Point);
		Previous = Point;
	}
	return false;
}

bool ANoFearDoor::IsTargetBehindDoor(const AAIController* AI, const APawn* Pawn) const
{
	const AActor* Target = AI ? AI->GetFocusActor() : nullptr;
	if (!Target || Target == Pawn || FVector::DistSquared(Target->GetActorLocation(), Pawn->GetActorLocation()) > FMath::Square(3000.f))
	{
		return false;
	}
	const FTransform& FrameTransform = Frame->GetComponentTransform();
	const FVector From = FrameTransform.InverseTransformPosition(Pawn->GetActorLocation());
	const FVector To = FrameTransform.InverseTransformPosition(Target->GetActorLocation());
	if (FMath::Sign(From.X) == FMath::Sign(To.X) || FMath::IsNearlyEqual(From.X, To.X))
	{
		return false;
	}
	// Линия к цели проходит через проём или рядом: дверь — путь к ней.
	const float Alpha = From.X / (From.X - To.X);
	const FVector Cross = FMath::Lerp(From, To, Alpha);
	return FMath::Abs(Cross.Y) <= Width * 0.5f + 150.f && FMath::Abs(Cross.Z) <= Height * 0.5f + 150.f;
}

bool ANoFearDoor::SegmentCrossesDoorway(FVector Start, FVector End) const
{
	const FTransform& FrameTransform = Frame->GetComponentTransform();
	const FVector A = FrameTransform.InverseTransformPosition(Start);
	const FVector B = FrameTransform.InverseTransformPosition(End);
	// Коробка проёма с запасом: точки пути лежат у пола, чуть выше или ниже края.
	const FBox Doorway(FVector(-Thickness * 0.5f - 40.f, -Width * 0.5f, -Height * 0.5f - 60.f),
		FVector(Thickness * 0.5f + 40.f, Width * 0.5f, Height * 0.5f + 60.f));
	if (Doorway.IsInside(A) || Doorway.IsInside(B))
	{
		return true;
	}
	const FVector Delta = B - A;
	return !Delta.IsNearlyZero() && FMath::LineBoxIntersection(Doorway, A, B, Delta);
}

int32 ANoFearDoor::GetSideOf(FVector Location) const
{
	return Frame->GetComponentTransform().InverseTransformPosition(Location).X >= 0.f ? 1 : -1;
}

bool ANoFearDoor::IsDoorwayClear() const
{
	const float LeafWidth = bDoubleDoor ? Width * 0.5f : Width;
	const FVector Extent(LeafWidth + 30.f, Width * 0.5f + 30.f, Height * 0.5f);
	FCollisionQueryParams Params(SCENE_QUERY_STAT(NoFearDoorClear), false, this);
	return !GetWorld()->OverlapAnyTestByObjectType(Frame->GetComponentLocation(), Frame->GetComponentQuat(),
		FCollisionObjectQueryParams(ECC_Pawn), FCollisionShape::MakeBox(Extent), Params);
}

// --- Урон и поломка ---

float ANoFearDoor::TakeDamage(float DamageAmount, const FDamageEvent& DamageEvent, AController* EventInstigator, AActor* DamageCauser)
{
	if (IsBroken())
	{
		return 0.f;
	}
	Super::TakeDamage(DamageAmount, DamageEvent, EventInstigator, DamageCauser);
	const float Applied = Health->ProcessDamage(DamageAmount, DamageEvent, EventInstigator, DamageCauser);
	if (Applied > 0.f && (IsMonsterTeam(DamageCauser) || IsMonsterTeam(EventInstigator ? EventInstigator->GetPawn() : nullptr)))
	{
		++MonsterAttackCount;
	}
	if (Applied > 0.f && !IsBroken() && HitSound)
	{
		FVector Location = Frame->GetComponentLocation();
		if (DamageEvent.IsOfType(FPointDamageEvent::ClassID))
		{
			Location = static_cast<const FPointDamageEvent&>(DamageEvent).HitInfo.ImpactPoint;
		}
		UGameplayStatics::PlaySoundAtLocation(this, HitSound, Location);
	}
	return Applied;
}

void ANoFearDoor::HandleDeath(UNoFearHealthComponent* Component, const FNoFearDamageInfo& Info)
{
	AActor* Breaker = Info.InstigatorController ? static_cast<AActor*>(Info.InstigatorController->GetPawn()) : nullptr;
	if (!Breaker)
	{
		Breaker = Info.DamageCauser;
	}
	BreakDoor(Breaker);
}

void ANoFearDoor::BreakDoor(AActor* Breaker)
{
	if (IsBroken() || bBreaking)
	{
		return;
	}
	TGuardValue<bool> Guard(bBreaking, true);

	// Обломки летят от того, кто ломал.
	FVector Push = FVector::ZeroVector;
	if (Breaker)
	{
		Push = -static_cast<float>(GetSideOf(Breaker->GetActorLocation())) * Frame->GetForwardVector();
	}

	SetState(ENoFearDoorState::Broken);
	LeftLeaf->SetVisibility(false);
	RightLeaf->SetVisibility(false);
	Interaction->bInteractionEnabled = false;
	RefreshNavigation();

	PlayDoorSound(BreakSound, NoiseLoudness * 2.5f, NoiseRange * 2.f, Breaker);
	const FVector Location = Frame->GetComponentLocation();
	UNoFearBotSubsystem::ReportDanger(this, ENoFearDangerType::Fight, Breaker ? Breaker : this, Location, UNoFearBotSubsystem::FightNoticeRadius, Location);
	SpawnDebris(Push);
}

void ANoFearDoor::SpawnDebris(const FVector& Push)
{
	UWorld* World = GetWorld();
	UStaticMesh* Mesh = PlaceholderMesh;
	if (!World || !Mesh || DebrisCount <= 0)
	{
		return;
	}
	const FBox MeshBox = Mesh->GetBoundingBox();
	const FVector MeshSize = MeshBox.GetSize().ComponentMax(FVector(1.f));
	const FTransform& FrameTransform = Frame->GetComponentTransform();

	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	for (int32 Index = 0; Index < DebrisCount; ++Index)
	{
		// Куски из плоскости проёма: доски шириной в шестую часть проёма.
		const FVector LocalPoint(0.f, FMath::FRandRange(-0.4f, 0.4f) * Width, FMath::FRandRange(-0.4f, 0.4f) * Height);
		const FVector PieceSize(Thickness, FMath::Max(10.f, Width / 6.f), FMath::Max(15.f, Height / 4.f));
		const FTransform PieceTransform(FrameTransform.GetRotation() * FRotator(FMath::FRandRange(-30.f, 30.f), 0.f, FMath::FRandRange(-30.f, 30.f)).Quaternion(),
			FrameTransform.TransformPosition(LocalPoint), PieceSize / MeshSize);

		AStaticMeshActor* Piece = World->SpawnActor<AStaticMeshActor>(AStaticMeshActor::StaticClass(), PieceTransform, SpawnParams);
		if (!Piece)
		{
			continue;
		}
		UStaticMeshComponent* PieceMesh = Piece->GetStaticMeshComponent();
		PieceMesh->SetMobility(EComponentMobility::Movable);
		PieceMesh->SetStaticMesh(Mesh);
		PieceMesh->SetCanEverAffectNavigation(false);
		PieceMesh->SetCollisionProfileName(UCollisionProfile::PhysicsActor_ProfileName);
		PieceMesh->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
		PieceMesh->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
		PieceMesh->SetCollisionResponseToChannel(ECC_Visibility, ECR_Ignore);
		if (PlaceholderMaterial && !LeafMaterial)
		{
			if (UMaterialInstanceDynamic* Material = PieceMesh->CreateDynamicMaterialInstance(0, PlaceholderMaterial))
			{
				Material->SetVectorParameterValue(TEXT("Color"), bLocked ? LockedColor : LeafColor);
			}
		}
		else if (LeafMaterial)
		{
			PieceMesh->SetMaterial(0, LeafMaterial);
		}
		PieceMesh->SetSimulatePhysics(true);
		const FVector Direction = (Push.IsNearlyZero() ? FMath::VRand().GetSafeNormal2D() : Push) + FMath::VRand() * 0.5f + FVector(0.f, 0.f, 0.4f);
		PieceMesh->SetPhysicsLinearVelocity(Direction.GetSafeNormal() * FMath::FRandRange(250.f, 550.f));
		PieceMesh->SetPhysicsAngularVelocityInDegrees(FMath::VRand() * 360.f);
		Piece->SetLifeSpan(DebrisLifetime);
	}
}

void ANoFearDoor::PlayDoorSound(USoundBase* Sound, float Loudness, float Range, AActor* NoiseSource) const
{
	const FVector Location = Frame->GetComponentLocation();
	if (Sound)
	{
		UGameplayStatics::PlaySoundAtLocation(this, Sound, Location);
	}
	if (Loudness > 0.f && Range > 0.f)
	{
		UAISense_Hearing::ReportNoiseEvent(const_cast<ANoFearDoor*>(this), Location, Loudness, NoiseSource ? NoiseSource : const_cast<ANoFearDoor*>(this), Range, TEXT("Door"));
	}
}

// ---------------------------------------------------------------------------
// Люк
// ---------------------------------------------------------------------------

ANoFearHatch::ANoFearHatch()
{
	Width = 140.f;
	Height = 320.f;
	Thickness = 8.f;
	bDoubleDoor = true;
	OpenAngle = 105.f;
	bSwingBothWays = false;
	OpenSide = 1;
	OpenTime = 0.8f;
	MaxHealth = 300.f;
	LeafColor = FLinearColor(0.18f, 0.2f, 0.16f);
	Interaction->InteractionDistance = 300.f;
}

FRotator ANoFearHatch::GetFrameRotation() const
{
	// Ось X рамы смотрит вверх: створки откидываются вверх, высота проёма идёт вдоль оси X актора.
	return FRotator(90.f, 0.f, 0.f);
}

FVector ANoFearHatch::GetFrameOffset() const
{
	return FVector(0.f, 0.f, -Thickness * 0.5f);
}

FVector ANoFearHatch::GetNavBlockerExtent() const
{
	// Под люком — пандус: вырезаем его верх, чтобы боты не строили путь в подвал.
	return FVector(Height * 0.5f, Width * 0.5f, NavBlockDepth);
}

#undef LOCTEXT_NAMESPACE
