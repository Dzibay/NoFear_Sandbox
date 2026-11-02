#include "World/NoFearDestructible.h"

#include "AI/NoFearBotSubsystem.h"
#include "Components/PointLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Core/NoFearHealthComponent.h"
#include "Engine/DamageEvents.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/Controller.h"
#include "GameFramework/Pawn.h"
#include "Inventory/NoFearItemData.h"
#include "Inventory/NoFearPickup.h"
#include "Materials/MaterialInterface.h"
#include "NavModifierComponent.h"
#include "NavAreas/NavArea_Default.h"
#include "NavAreas/NavArea_Null.h"
#include "Sound/SoundBase.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"
#include "Weapons/NoFearGrenadeData.h"
#include "World/NoFearWorldLibrary.h"

#define LOCTEXT_NAMESPACE "NoFearDestructible"

// ---------------------------------------------------------------------------
// Взаимодействие
// ---------------------------------------------------------------------------

UNoFearDestructibleInteraction::UNoFearDestructibleInteraction()
{
	InteractionDistance = 200.f;
}

bool UNoFearDestructibleInteraction::CanInteract_Implementation(APawn* User) const
{
	const ANoFearDestructible* Owner = Cast<ANoFearDestructible>(GetOwner());
	return Super::CanInteract_Implementation(User) && Owner && !Owner->IsBroken() && Owner->CanInteractBy(User);
}

FText UNoFearDestructibleInteraction::GetInteractionPrompt_Implementation(APawn* User) const
{
	const ANoFearDestructible* Owner = Cast<ANoFearDestructible>(GetOwner());
	return Owner ? Owner->GetPrompt(User) : PromptText;
}

void UNoFearDestructibleInteraction::HandleInteract_Implementation(APawn* User)
{
	if (ANoFearDestructible* Owner = Cast<ANoFearDestructible>(GetOwner()); Owner && !Owner->IsBroken())
	{
		Owner->InteractBy(User);
	}
}

// ---------------------------------------------------------------------------
// Основа
// ---------------------------------------------------------------------------

ANoFearDestructible::ANoFearDestructible()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = Root;

	// Корпус: держит пешек, ловит пули (Visibility) и взрывы (WorldDynamic). Навмеш режет NavBlocker.
	Body = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Body"));
	Body->SetupAttachment(Root);
	Body->SetCollisionProfileName(UCollisionProfile::BlockAllDynamic_ProfileName);
	Body->SetCanEverAffectNavigation(false);
	Body->SetGenerateOverlapEvents(false);

	Health = CreateDefaultSubobject<UNoFearHealthComponent>(TEXT("Health"));
	Health->RegenRate = 0.f;
	Health->bCanBeDowned = false;

	NavBlocker = CreateDefaultSubobject<UNavModifierComponent>(TEXT("NavBlocker"));
	NavBlocker->AreaClass = UNavArea_Null::StaticClass();

	Interaction = CreateDefaultSubobject<UNoFearDestructibleInteraction>(TEXT("Interaction"));

	static ConstructorHelpers::FObjectFinderOptional<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
	static ConstructorHelpers::FObjectFinderOptional<UStaticMesh> Cylinder(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	static ConstructorHelpers::FObjectFinderOptional<UMaterialInterface> ShapeMaterial(TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	CubeMesh = Cube.Get();
	CylinderMesh = Cylinder.Get();
	PlaceholderMaterial = ShapeMaterial.Get();
}

void ANoFearDestructible::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	RefreshShape();
}

void ANoFearDestructible::BeginPlay()
{
	Super::BeginPlay();
	Health->OnDeath.AddDynamic(this, &ANoFearDestructible::HandleDeath);
	RefreshShape();
}

void ANoFearDestructible::RefreshShape()
{
	const FVector Size = GetBodySize();
	const FVector Center = GetBodyCenter();
	NoFearWorld::FitMesh(Body, BodyMesh ? BodyMesh.Get() : GetPlaceholderMesh(), Size, Center);
	if (!BodyMesh)
	{
		NoFearWorld::TintPlaceholder(Body, PlaceholderMaterial, Color);
	}
	// Корпус навмеш не трогает, поэтому вырез — по FailsafeExtent вокруг точки актора.
	NavBlocker->FailsafeExtent = FVector(Size.X * 0.5f + 5.f, Size.Y * 0.5f + 5.f, Center.Z + Size.Z * 0.5f);
}

float ANoFearDestructible::GetHealth() const
{
	return Health ? Health->GetHealth() : 0.f;
}

AActor* ANoFearDestructible::GetBreaker(AController* EventInstigator, AActor* DamageCauser)
{
	if (EventInstigator && EventInstigator->GetPawn())
	{
		return EventInstigator->GetPawn();
	}
	return DamageCauser;
}

float ANoFearDestructible::TakeDamage(float DamageAmount, const FDamageEvent& DamageEvent, AController* EventInstigator, AActor* DamageCauser)
{
	if (bBroken)
	{
		return 0.f;
	}
	Super::TakeDamage(DamageAmount, DamageEvent, EventInstigator, DamageCauser);
	const float Applied = Health->ProcessDamage(DamageAmount, DamageEvent, EventInstigator, DamageCauser);
	if (Applied > 0.f && !bBroken)
	{
		OnDamaged(Applied, GetBreaker(EventInstigator, DamageCauser));
	}
	return Applied;
}

void ANoFearDestructible::HandleDeath(UNoFearHealthComponent* Component, const FNoFearDamageInfo& Info)
{
	Break(GetBreaker(Info.InstigatorController, Info.DamageCauser));
}

void ANoFearDestructible::Break(AActor* Breaker)
{
	if (bBroken)
	{
		return;
	}
	bBroken = true;
	Body->SetVisibility(false);
	Body->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	NavBlocker->SetNavigationRelevancy(false);
	Interaction->bInteractionEnabled = false;
	HandleBroken(Breaker);
	OnBroken.Broadcast(this, Breaker);
}

void ANoFearDestructible::HandleBroken(AActor* Breaker)
{
	const FVector Size = GetBodySize();
	const FTransform BodyTransform(GetActorQuat(), GetActorTransform().TransformPosition(GetBodyCenter()));
	FVector Push = FVector::ZeroVector;
	if (Breaker)
	{
		Push = (BodyTransform.GetLocation() - Breaker->GetActorLocation()).GetSafeNormal2D();
	}
	const FVector PieceSize = (Size / 3.f).ComponentMax(FVector(4.f));
	NoFearWorld::SpawnDebris(this, BodyTransform, Size * 0.8f, PieceSize, DebrisCount, Push, 6.f, PlaceholderMaterial, Color);
	APawn* NoiseInstigator = Cast<APawn>(Breaker);
	NoFearWorld::PlaySoundAndNoise(this, BreakSound, BodyTransform.GetLocation(), NoiseLoudness, NoiseRange, NoiseInstigator, TEXT("Break"));
	SetLifeSpan(0.5f);
}

// ---------------------------------------------------------------------------
// Бочка
// ---------------------------------------------------------------------------

ANoFearExplosiveBarrel::ANoFearExplosiveBarrel()
{
	Color = FLinearColor(0.55f, 0.08f, 0.05f);
	Health->MaxHealth = 40.f;
	NoiseLoudness = 1.f;
	NoiseRange = 2000.f;
	DebrisCount = 6;

	Fire = CreateDefaultSubobject<UPointLightComponent>(TEXT("Fire"));
	Fire->SetupAttachment(Root);
	Fire->SetRelativeLocation(FVector(0.f, 0.f, 110.f));
	Fire->SetLightColor(FLinearColor(1.f, 0.45f, 0.1f));
	Fire->SetIntensity(0.f);
	Fire->SetAttenuationRadius(450.f);
	Fire->SetCastShadows(false);
	Fire->SetVisibility(false);

	Flame = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Flame"));
	Flame->SetupAttachment(Root);
	Flame->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Flame->SetCanEverAffectNavigation(false);
	Flame->SetGenerateOverlapEvents(false);
	Flame->SetCastShadow(false);
	Flame->SetVisibility(false);
	static ConstructorHelpers::FObjectFinderOptional<UStaticMesh> Cone(TEXT("/Engine/BasicShapes/Cone.Cone"));
	Flame->SetStaticMesh(Cone.Get());
}

void ANoFearExplosiveBarrel::OnDamaged(float Applied, AActor* Breaker)
{
	if (APawn* Pawn = Cast<APawn>(Breaker))
	{
		LastInstigator = Pawn;
	}
	if (!bBurning && Health->GetHealthFraction() <= BurnHealthFraction)
	{
		Ignite(Breaker);
	}
}

void ANoFearExplosiveBarrel::Ignite(AActor* Igniter)
{
	if (bBurning || IsBroken())
	{
		return;
	}
	if (APawn* Pawn = Cast<APawn>(Igniter))
	{
		LastInstigator = Pawn;
	}
	bBurning = true;
	BurnLeft = BurnTime;
	DangerTimer = 0.f;
	Fire->SetVisibility(true);
	Fire->SetRelativeLocation(FVector(0.f, 0.f, BarrelHeight + 20.f));
	NoFearWorld::FitMesh(Flame, Flame->GetStaticMesh(), FVector(Diameter * 0.6f, Diameter * 0.6f, 45.f), FVector(0.f, 0.f, BarrelHeight + 22.f));
	NoFearWorld::TintPlaceholder(Flame, PlaceholderMaterial, FLinearColor(1.f, 0.4f, 0.05f));
	Flame->SetVisibility(true);
	NoFearWorld::PlaySoundAndNoise(this, IgniteSound, Fire->GetComponentLocation(), 0.5f, 800.f, LastInstigator.Get(), TEXT("Fire"));
	SetActorTickEnabled(true);
}

void ANoFearExplosiveBarrel::ReportDanger()
{
	// Боты убегают от горящей бочки, как от упавшей гранаты.
	const UNoFearGrenadeData* Data = Explosion ? Explosion.Get() : NoFearWorld::GetDefaultGrenadeData();
	const float Radius = Data ? Data->DangerNoticeRadius : 900.f;
	const FVector Location = GetActorLocation();
	UNoFearBotSubsystem::ReportDanger(this, ENoFearDangerType::Grenade, this, Location, Radius, Location);
}

void ANoFearExplosiveBarrel::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!bBurning || bExploded)
	{
		SetActorTickEnabled(false);
		return;
	}
	const float Time = GetWorld()->GetTimeSeconds();
	Fire->SetIntensity(6000.f + 3000.f * FMath::Sin(Time * 23.f) * FMath::Sin(Time * 7.f));

	DangerTimer -= DeltaSeconds;
	if (DangerTimer <= 0.f)
	{
		DangerTimer = 0.5f;
		ReportDanger();
	}
	BurnLeft -= DeltaSeconds;
	if (BurnLeft <= 0.f)
	{
		Break(LastInstigator.Get());
	}
}

void ANoFearExplosiveBarrel::HandleBroken(AActor* Breaker)
{
	if (APawn* Pawn = Cast<APawn>(Breaker))
	{
		LastInstigator = Pawn;
	}
	// Корпус уже без коллизии: не заслоняет взрыв соседям. Огонь виден до взрыва.
	const float Fuse = bBurning && BurnLeft <= 0.f ? 0.f : FMath::FRandRange(FuseRange.X, FMath::Max(FuseRange.X, FuseRange.Y));
	Body->SetVisibility(true);
	if (Fuse > 0.f)
	{
		GetWorldTimerManager().SetTimer(FuseTimer, this, &ANoFearExplosiveBarrel::Detonate, Fuse, false);
	}
	else
	{
		Detonate();
	}
}

void ANoFearExplosiveBarrel::Detonate()
{
	if (bExploded)
	{
		return;
	}
	bExploded = true;
	bBurning = false;
	Body->SetVisibility(false);
	Flame->SetVisibility(false);
	Fire->SetVisibility(false);
	const FVector Center = GetActorTransform().TransformPosition(GetBodyCenter());
	NoFearWorld::Explode(this, Explosion, Center, LastInstigator.Get());
	ANoFearDestructible::HandleBroken(LastInstigator.Get());
}

// ---------------------------------------------------------------------------
// Ящик
// ---------------------------------------------------------------------------

ANoFearBreakableCrate::ANoFearBreakableCrate()
{
	Color = FLinearColor(0.5f, 0.36f, 0.2f);
	Health->MaxHealth = 30.f;
	NoiseLoudness = 0.6f;
	NoiseRange = 1000.f;
	DebrisCount = 8;
}

float ANoFearBreakableCrate::GetWorkProgress() const
{
	return Worker.IsValid() ? FMath::Clamp(WorkTime / FMath::Max(OpenTime, 0.01f), 0.f, 1.f) : 0.f;
}

bool ANoFearBreakableCrate::CanInteractBy(const APawn* User) const
{
	return User != nullptr;
}

FText ANoFearBreakableCrate::GetPrompt(const APawn* User) const
{
	return LOCTEXT("CratePrompt", "Разбить ящик (держать)");
}

void ANoFearBreakableCrate::InteractBy(APawn* User)
{
	// Бот или вызов из кода ломает сразу; игрок держит E.
	if (!NoFearWorld::IsInteractHeld(User))
	{
		Break(User);
		return;
	}
	Worker = User;
	WorkTime = 0.f;
	SetActorTickEnabled(true);
}

void ANoFearBreakableCrate::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	APawn* User = Worker.Get();
	if (!User || IsBroken() || !NoFearWorld::IsInteractHeld(User) || !Interaction->IsInRange(User) || UNoFearHealthComponent::IsActorIncapacitated(User))
	{
		Worker.Reset();
		WorkTime = 0.f;
		SetActorTickEnabled(false);
		return;
	}
	WorkTime += DeltaSeconds;
	if (WorkTime >= OpenTime)
	{
		Worker.Reset();
		SetActorTickEnabled(false);
		Break(User);
	}
}

void ANoFearBreakableCrate::HandleBroken(AActor* Breaker)
{
	Super::HandleBroken(Breaker);
	UWorld* World = GetWorld();
	// Над верхом ящика: длинные предметы (доски), появившись у пола, выталкиваются физикой на метры.
	const FVector Center = GetActorTransform().TransformPosition(GetBodyCenter() + FVector(0.f, 0.f, Size.Z * 0.5f + 25.f));
	for (const FNoFearChestItem& Entry : Contents)
	{
		if (!Entry.Item || Entry.Count <= 0)
		{
			continue;
		}
		const FVector Offset(FMath::FRandRange(-0.3f, 0.3f) * Size.X, FMath::FRandRange(-0.3f, 0.3f) * Size.Y, 0.f);
		const FTransform Transform(FRotator(0.f, FMath::FRandRange(0.f, 360.f), 0.f), Center + Offset);
		const FVector Velocity = FMath::VRand().GetSafeNormal2D() * FMath::FRandRange(50.f, 110.f) + FVector(0.f, 0.f, 60.f);
		ANoFearPickup::SpawnPickup(World, Entry.Item, Entry.Count, Entry.MagazineAmmo, Transform, Velocity);
	}
}

// ---------------------------------------------------------------------------
// Окно
// ---------------------------------------------------------------------------

ANoFearWindow::ANoFearWindow()
{
	Color = FLinearColor(0.55f, 0.65f, 0.7f);
	Health->MaxHealth = 1.f;
	NoiseLoudness = 0.9f;
	NoiseRange = 1500.f;
	DebrisCount = 10;
	Interaction->InteractionDistance = 220.f;

	for (int32 Index = 0; Index < 4; ++Index)
	{
		UStaticMeshComponent* Part = CreateDefaultSubobject<UStaticMeshComponent>(*FString::Printf(TEXT("Frame%d"), Index));
		Part->SetupAttachment(Root);
		Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Part->SetCanEverAffectNavigation(false);
		Part->SetGenerateOverlapEvents(false);
		FrameParts.Add(Part);
	}

	// Проём на высоте подоконника: навмеш внизу не режем — стену под окном ставит уровень.
	NavBlocker->AreaClass = UNavArea_Default::StaticClass();
}

void ANoFearWindow::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	RefreshFrame();
}

void ANoFearWindow::RefreshFrame()
{
	const float Bar = 8.f;
	const float Depth = 14.f;
	const FVector Sizes[4] = {
		FVector(Depth, Width + Bar * 2.f, Bar), FVector(Depth, Width + Bar * 2.f, Bar),
		FVector(Depth, Bar, WindowHeight), FVector(Depth, Bar, WindowHeight)};
	const FVector Centers[4] = {
		FVector(0.f, 0.f, SillHeight - Bar * 0.5f), FVector(0.f, 0.f, SillHeight + WindowHeight + Bar * 0.5f),
		FVector(0.f, -(Width + Bar) * 0.5f, SillHeight + WindowHeight * 0.5f), FVector(0.f, (Width + Bar) * 0.5f, SillHeight + WindowHeight * 0.5f)};
	for (int32 Index = 0; Index < FrameParts.Num() && Index < 4; ++Index)
	{
		NoFearWorld::FitMesh(FrameParts[Index], CubeMesh, Sizes[Index], Centers[Index]);
		NoFearWorld::TintPlaceholder(FrameParts[Index], PlaceholderMaterial, FrameColor);
	}
}

bool ANoFearWindow::CanInteractBy(const APawn* User) const
{
	return User != nullptr;
}

FText ANoFearWindow::GetPrompt(const APawn* User) const
{
	return LOCTEXT("WindowPrompt", "Разбить окно");
}

void ANoFearWindow::InteractBy(APawn* User)
{
	Break(User);
}

void ANoFearWindow::HandleBroken(AActor* Breaker)
{
	// Осколки летят от того, кто бил; рама остаётся, сам актор — тоже (рама и проём для баррикады).
	const FVector Size = GetBodySize();
	const FTransform GlassTransform(GetActorQuat(), GetActorTransform().TransformPosition(GetBodyCenter()));
	FVector Push = GetActorForwardVector();
	if (Breaker && FVector::DotProduct(Breaker->GetActorLocation() - GetActorLocation(), Push) > 0.f)
	{
		Push = -Push;
	}
	NoFearWorld::SpawnDebris(this, GlassTransform, Size, FVector(2.f, 18.f, 14.f), DebrisCount, Push, 5.f, PlaceholderMaterial, Color);
	const FVector Location = GlassTransform.GetLocation();
	NoFearWorld::PlaySoundAndNoise(this, BreakSound, Location, NoiseLoudness, NoiseRange, Cast<APawn>(Breaker), TEXT("Glass"));
	UNoFearBotSubsystem::ReportDanger(this, ENoFearDangerType::Fight, Breaker ? Breaker : this, Location, UNoFearBotSubsystem::FightNoticeRadius, Location);
}

#undef LOCTEXT_NAMESPACE
