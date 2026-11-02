#include "World/NoFearPower.h"

#include "Components/PointLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Core/NoFearHealthComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Controller.h"
#include "GameFramework/Pawn.h"
#include "Inventory/NoFearInventoryComponent.h"
#include "Inventory/NoFearItemData.h"
#include "Materials/MaterialInterface.h"
#include "NavModifierComponent.h"
#include "Sound/SoundBase.h"
#include "UObject/ConstructorHelpers.h"
#include "World/NoFearLightSubsystem.h"
#include "World/NoFearWorldLibrary.h"

#define LOCTEXT_NAMESPACE "NoFearPower"

namespace
{
	UNoFearLightSubsystem* GetLightSubsystem(const UObject* Context)
	{
		const UWorld* World = Context ? Context->GetWorld() : nullptr;
		return World ? World->GetSubsystem<UNoFearLightSubsystem>() : nullptr;
	}
}

// ---------------------------------------------------------------------------
// Лампа
// ---------------------------------------------------------------------------

ANoFearLamp::ANoFearLamp()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = Root;

	static ConstructorHelpers::FObjectFinderOptional<UStaticMesh> Sphere(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	static ConstructorHelpers::FObjectFinderOptional<UStaticMesh> Cylinder(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	static ConstructorHelpers::FObjectFinderOptional<UMaterialInterface> ShapeMaterial(TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	PlaceholderMaterial = ShapeMaterial.Get();

	Bulb = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Bulb"));
	Bulb->SetupAttachment(Root);
	Bulb->SetStaticMesh(Sphere.Get());
	Bulb->SetRelativeScale3D(FVector(0.22f));
	Bulb->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Bulb->SetCollisionObjectType(ECC_WorldDynamic);
	Bulb->SetCollisionResponseToAllChannels(ECR_Ignore);
	Bulb->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	Bulb->SetCanEverAffectNavigation(false);
	Bulb->SetGenerateOverlapEvents(false);
	Bulb->CastShadow = false;

	Cord = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Cord"));
	Cord->SetupAttachment(Root);
	Cord->SetStaticMesh(Cylinder.Get());
	Cord->SetRelativeLocation(FVector(0.f, 0.f, 30.f));
	Cord->SetRelativeScale3D(FVector(0.02f, 0.02f, 0.4f));
	Cord->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Cord->SetCanEverAffectNavigation(false);

	Light = CreateDefaultSubobject<UPointLightComponent>(TEXT("Light"));
	Light->SetupAttachment(Root);
	Light->SetRelativeLocation(FVector(0.f, 0.f, -15.f));
	Light->SetMobility(EComponentMobility::Movable);
	Light->SetCastShadows(false);
}

void ANoFearLamp::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	Light->SetIntensity(Intensity);
	Light->SetAttenuationRadius(Radius);
	Light->SetLightColor(LightColor);
	NoFearWorld::TintPlaceholder(Cord, PlaceholderMaterial, FLinearColor(0.05f, 0.05f, 0.05f));
	Refresh();
}

void ANoFearLamp::BeginPlay()
{
	Super::BeginPlay();
	Light->SetIntensity(Intensity);
	Light->SetAttenuationRadius(Radius);
	Light->SetLightColor(LightColor);
	NoFearWorld::TintPlaceholder(Cord, PlaceholderMaterial, FLinearColor(0.05f, 0.05f, 0.05f));
	if (UNoFearLightSubsystem* Subsystem = GetLightSubsystem(this))
	{
		Subsystem->RegisterLight(Light, LightWeight);
		CircuitHandle = Subsystem->OnCircuitChanged.AddUObject(this, &ANoFearLamp::HandleCircuitChanged);
	}
	Refresh();
}

void ANoFearLamp::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UNoFearLightSubsystem* Subsystem = GetLightSubsystem(this))
	{
		Subsystem->UnregisterLight(Light);
		Subsystem->OnCircuitChanged.Remove(CircuitHandle);
	}
	Super::EndPlay(EndPlayReason);
}

bool ANoFearLamp::IsPowered() const
{
	const UNoFearLightSubsystem* Subsystem = GetLightSubsystem(this);
	return Subsystem ? Subsystem->IsCircuitPowered(Circuit) : Circuit.IsNone();
}

void ANoFearLamp::SetSwitchedOn(bool bOn)
{
	bSwitchedOn = bOn;
	Refresh();
}

void ANoFearLamp::HandleCircuitChanged(FName ChangedCircuit, bool bPowered)
{
	if (ChangedCircuit == Circuit)
	{
		Refresh();
	}
}

void ANoFearLamp::Refresh()
{
	bLit = !bBroken && bSwitchedOn && (GetWorld() && GetWorld()->IsGameWorld() ? IsPowered() : true);
	Light->SetVisibility(bLit);
	// Горящий плафон светлый, погасший — серый, разбитый — почти не виден.
	const FLinearColor BulbColor = bBroken ? FLinearColor(0.08f, 0.08f, 0.08f) : bLit ? FLinearColor(1.f, 0.95f, 0.75f) : FLinearColor(0.35f, 0.35f, 0.33f);
	NoFearWorld::TintPlaceholder(Bulb, PlaceholderMaterial, BulbColor);
	Bulb->SetRelativeScale3D(FVector(bBroken ? 0.1f : 0.22f));
	SetActorTickEnabled(bLit && bFlicker);
	if (bLit)
	{
		Light->SetIntensity(Intensity);
	}
}

void ANoFearLamp::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	// Мигание: короткие провалы яркости в случайные моменты.
	FlickerTime -= DeltaSeconds;
	if (FlickerTime <= 0.f)
	{
		const bool bDip = FMath::FRand() < 0.3f;
		Light->SetIntensity(bDip ? Intensity * FMath::FRandRange(0.f, 0.3f) : Intensity * FMath::FRandRange(0.85f, 1.f));
		FlickerTime = bDip ? FMath::FRandRange(0.04f, 0.12f) : FMath::FRandRange(0.1f, 1.2f);
	}
}

float ANoFearLamp::TakeDamage(float DamageAmount, const FDamageEvent& DamageEvent, AController* EventInstigator, AActor* DamageCauser)
{
	Super::TakeDamage(DamageAmount, DamageEvent, EventInstigator, DamageCauser);
	if (DamageAmount <= 0.f || bBroken)
	{
		return 0.f;
	}
	AActor* Breaker = EventInstigator && EventInstigator->GetPawn() ? Cast<AActor>(EventInstigator->GetPawn()) : DamageCauser;
	BreakBulb(Breaker);
	return DamageAmount;
}

void ANoFearLamp::BreakBulb(AActor* Breaker)
{
	if (bBroken)
	{
		return;
	}
	bBroken = true;
	Bulb->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	const FVector Location = Bulb->GetComponentLocation();
	NoFearWorld::SpawnDebris(this, FTransform(Location), FVector(15.f), FVector(3.f), 5, FVector::ZeroVector, 3.f, PlaceholderMaterial, FLinearColor(0.9f, 0.9f, 0.85f));
	NoFearWorld::PlaySoundAndNoise(this, BreakSound, Location, 0.4f, 800.f, Cast<APawn>(Breaker), TEXT("Glass"));
	Refresh();
}

// ---------------------------------------------------------------------------
// Выключатель
// ---------------------------------------------------------------------------

UNoFearLightSwitchInteraction::UNoFearLightSwitchInteraction()
{
	InteractionDistance = 180.f;
}

FText UNoFearLightSwitchInteraction::GetInteractionPrompt_Implementation(APawn* User) const
{
	const ANoFearLightSwitch* Switch = Cast<ANoFearLightSwitch>(GetOwner());
	if (!Switch)
	{
		return PromptText;
	}
	return Switch->IsOn() ? LOCTEXT("SwitchOff", "Выключить свет") : LOCTEXT("SwitchOn", "Включить свет");
}

void UNoFearLightSwitchInteraction::HandleInteract_Implementation(APawn* User)
{
	if (ANoFearLightSwitch* Switch = Cast<ANoFearLightSwitch>(GetOwner()))
	{
		Switch->Toggle(User);
	}
}

ANoFearLightSwitch::ANoFearLightSwitch()
{
	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = Root;

	static ConstructorHelpers::FObjectFinderOptional<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
	static ConstructorHelpers::FObjectFinderOptional<UMaterialInterface> ShapeMaterial(TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	PlaceholderMaterial = ShapeMaterial.Get();

	Plate = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Plate"));
	Plate->SetupAttachment(Root);
	Plate->SetStaticMesh(Cube.Get());
	Plate->SetRelativeScale3D(FVector(0.02f, 0.08f, 0.12f));
	Plate->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Plate->SetCanEverAffectNavigation(false);

	Lever = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Lever"));
	Lever->SetupAttachment(Root);
	Lever->SetStaticMesh(Cube.Get());
	Lever->SetRelativeScale3D(FVector(0.03f, 0.025f, 0.04f));
	Lever->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Lever->SetCanEverAffectNavigation(false);

	Interaction = CreateDefaultSubobject<UNoFearLightSwitchInteraction>(TEXT("Interaction"));
}

void ANoFearLightSwitch::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	NoFearWorld::TintPlaceholder(Plate, PlaceholderMaterial, FLinearColor(0.8f, 0.78f, 0.7f));
	RefreshLever();
}

void ANoFearLightSwitch::BeginPlay()
{
	Super::BeginPlay();
	NoFearWorld::TintPlaceholder(Plate, PlaceholderMaterial, FLinearColor(0.8f, 0.78f, 0.7f));
	// Лампы под выключателем начинают в его положении.
	for (ANoFearLamp* Lamp : GetLamps())
	{
		Lamp->SetSwitchedOn(bOn);
	}
	RefreshLever();
}

TArray<ANoFearLamp*> ANoFearLightSwitch::GetLamps() const
{
	TArray<ANoFearLamp*> Result;
	for (ANoFearLamp* Lamp : Lamps)
	{
		if (IsValid(Lamp))
		{
			Result.Add(Lamp);
		}
	}
	if (Lamps.IsEmpty() && !Circuit.IsNone() && GetWorld())
	{
		for (TActorIterator<ANoFearLamp> It(GetWorld()); It; ++It)
		{
			if (It->Circuit == Circuit)
			{
				Result.Add(*It);
			}
		}
	}
	return Result;
}

void ANoFearLightSwitch::Toggle(APawn* User)
{
	SetOn(!bOn, User);
}

void ANoFearLightSwitch::SetOn(bool bNewOn, APawn* User)
{
	bOn = bNewOn;
	bool bAnyPowered = false;
	bool bAnyLamp = false;
	for (ANoFearLamp* Lamp : GetLamps())
	{
		Lamp->SetSwitchedOn(bOn);
		bAnyLamp = true;
		bAnyPowered |= Lamp->IsPowered() && !Lamp->IsBroken();
	}
	RefreshLever();
	NoFearWorld::PlaySoundAndNoise(this, ClickSound, GetActorLocation(), 0.1f, 300.f, User, TEXT("Switch"));
	if (bOn && bAnyLamp && !bAnyPowered && User)
	{
		NoFearWorld::Notify(User, LOCTEXT("NoPower", "Нет питания"));
	}
}

void ANoFearLightSwitch::RefreshLever()
{
	Lever->SetRelativeLocation(FVector(2.f, 0.f, bOn ? 2.5f : -2.5f));
	NoFearWorld::TintPlaceholder(Lever, PlaceholderMaterial, bOn ? FLinearColor(0.2f, 0.2f, 0.2f) : FLinearColor(0.6f, 0.1f, 0.1f));
}

// ---------------------------------------------------------------------------
// Генератор
// ---------------------------------------------------------------------------

ANoFearGenerator::ANoFearGenerator()
{
	Color = FLinearColor(0.55f, 0.45f, 0.1f);
	Health->MaxHealth = 120.f;
	NoiseLoudness = 0.8f;
	NoiseRange = 1500.f;
	DebrisCount = 6;
	FuelItem = TSoftObjectPtr<UNoFearItemData>(FSoftObjectPath(TEXT("/Game/Items/DA_Item_Fuel.DA_Item_Fuel")));

	StatusLight = CreateDefaultSubobject<UPointLightComponent>(TEXT("StatusLight"));
	StatusLight->SetupAttachment(Root);
	StatusLight->SetMobility(EComponentMobility::Movable);
	StatusLight->SetIntensity(150.f);
	StatusLight->SetAttenuationRadius(120.f);
	StatusLight->SetCastShadows(false);
	StatusLight->SetLightColor(FLinearColor(1.f, 0.1f, 0.05f));
}

void ANoFearGenerator::BeginPlay()
{
	Super::BeginPlay();
	StatusLight->SetRelativeLocation(FVector(Size.X * 0.5f + 5.f, 0.f, Size.Z * 0.8f));
	Fuel = MaxFuel * StartFuelFraction;
	SetRunning(bStartRunning && Fuel > 0.f);
}

void ANoFearGenerator::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UNoFearLightSubsystem* Subsystem = GetLightSubsystem(this))
	{
		Subsystem->SetCircuitPowered(Circuit, this, false);
	}
	Super::EndPlay(EndPlayReason);
}

void ANoFearGenerator::SetFuel(float Seconds)
{
	Fuel = FMath::Clamp(Seconds, 0.f, MaxFuel);
}

void ANoFearGenerator::SetRunning(bool bNewRunning)
{
	const bool bChanged = bRunning != bNewRunning;
	bRunning = bNewRunning;
	if (UNoFearLightSubsystem* Subsystem = GetLightSubsystem(this))
	{
		Subsystem->SetCircuitPowered(Circuit, this, bRunning);
	}
	StatusLight->SetLightColor(bRunning ? FLinearColor(0.1f, 1.f, 0.2f) : FLinearColor(1.f, 0.1f, 0.05f));
	StatusLight->SetVisibility(!IsBroken());
	NoiseTime = 0.f;
	SetActorTickEnabled(bRunning || Worker.IsValid());
	if (!bRunning)
	{
		Body->SetRelativeRotation(FRotator::ZeroRotator);
	}
	if (bChanged)
	{
		OnRunningChanged.Broadcast(this, bRunning);
	}
}

bool ANoFearGenerator::PullStart(APawn* User)
{
	if (IsBroken() || bRunning)
	{
		return bRunning;
	}
	if (Fuel <= 0.f)
	{
		NoFearWorld::Notify(User, LOCTEXT("NoFuel", "Бак пуст"));
		return false;
	}
	const bool bStarted = FMath::FRand() < StartChance;
	// Рывок шнура и чих слышно и без запуска.
	NoFearWorld::PlaySoundAndNoise(this, PullSound, GetActorLocation(), bStarted ? RunNoiseLoudness : 0.5f, bStarted ? RunNoiseRange : 1500.f, User, TEXT("Generator"));
	if (bStarted)
	{
		SetRunning(true);
	}
	else
	{
		NoFearWorld::Notify(User, LOCTEXT("Sputter", "Генератор чихнул и заглох"));
	}
	return bStarted;
}

void ANoFearGenerator::Stop()
{
	SetRunning(false);
}

bool ANoFearGenerator::HasFuelItem(const APawn* User) const
{
	const UNoFearInventoryComponent* Case = UNoFearInventoryComponent::FindCase(User);
	const UNoFearItemData* Item = NoFearWorld::LoadItem(FuelItem);
	return Case && Item && Case->CountItem(Item) > 0;
}

bool ANoFearGenerator::Refuel(APawn* User)
{
	UNoFearInventoryComponent* Case = UNoFearInventoryComponent::FindCase(User);
	UNoFearItemData* Item = NoFearWorld::LoadItem(FuelItem);
	if (IsBroken() || !Case || !Item || Case->RemoveItem(Item, 1) < 1)
	{
		return false;
	}
	Fuel = MaxFuel;
	NoFearWorld::Notify(User, LOCTEXT("Refueled", "Бак полон"));
	return true;
}

float ANoFearGenerator::GetWorkProgress() const
{
	return Worker.IsValid() ? FMath::Clamp(WorkTime / FMath::Max(StartTime, 0.01f), 0.f, 1.f) : 0.f;
}

bool ANoFearGenerator::CanInteractBy(const APawn* User) const
{
	return User != nullptr;
}

FText ANoFearGenerator::GetPrompt(const APawn* User) const
{
	if (bRunning)
	{
		return LOCTEXT("StopPrompt", "Заглушить генератор");
	}
	if (Fuel <= 0.f)
	{
		return HasFuelItem(User) ? LOCTEXT("RefuelPrompt", "Заправить генератор (держать)") : LOCTEXT("EmptyPrompt", "Бак пуст");
	}
	return LOCTEXT("StartPrompt", "Завести генератор (держать)");
}

void ANoFearGenerator::InteractBy(APawn* User)
{
	if (bRunning)
	{
		Stop();
		return;
	}
	if (Fuel <= 0.f && !HasFuelItem(User))
	{
		NoFearWorld::Notify(User, LOCTEXT("NoFuelItem", "Нужно топливо"));
		return;
	}
	// Бот или вызов из кода — сразу; игрок держит E.
	if (!NoFearWorld::IsInteractHeld(User))
	{
		FinishWork(User);
		return;
	}
	Worker = User;
	WorkTime = 0.f;
	SetActorTickEnabled(true);
}

void ANoFearGenerator::FinishWork(APawn* User)
{
	if (Fuel <= 0.f)
	{
		Refuel(User);
		return;
	}
	PullStart(User);
}

void ANoFearGenerator::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (APawn* User = Worker.Get())
	{
		if (IsBroken() || bRunning || !NoFearWorld::IsInteractHeld(User) || !Interaction->IsInRange(User) || UNoFearHealthComponent::IsActorIncapacitated(User))
		{
			Worker.Reset();
			WorkTime = 0.f;
		}
		else
		{
			WorkTime += DeltaSeconds;
			if (WorkTime >= StartTime)
			{
				Worker.Reset();
				WorkTime = 0.f;
				FinishWork(User);
			}
		}
	}

	if (bRunning)
	{
		Fuel -= DeltaSeconds;
		if (Fuel <= 0.f)
		{
			Fuel = 0.f;
			SetRunning(false);
			return;
		}
		// Тарахтит: корпус дрожит, шум раз в RunNoiseInterval — монстры идут на звук.
		ShakeTime += DeltaSeconds;
		Body->SetRelativeRotation(FRotator(FMath::Sin(ShakeTime * 60.f) * 0.6f, 0.f, FMath::Sin(ShakeTime * 47.f) * 0.6f));
		NoiseTime -= DeltaSeconds;
		if (NoiseTime <= 0.f)
		{
			NoiseTime = RunNoiseInterval;
			NoFearWorld::PlaySoundAndNoise(this, RunSound, GetActorLocation(), RunNoiseLoudness, RunNoiseRange, nullptr, TEXT("Generator"));
			NoFearWorld::LureMonsters(this, GetActorLocation(), RunNoiseLoudness > 0.f ? RunNoiseRange : 0.f);
		}
	}
	else if (!Worker.IsValid())
	{
		SetActorTickEnabled(false);
	}
}

void ANoFearGenerator::HandleBroken(AActor* Breaker)
{
	// Корпус остаётся грудой железа: держит пешек и пули, только больше не заводится.
	SetRunning(false);
	Worker.Reset();
	Body->SetVisibility(true);
	Body->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	NavBlocker->SetNavigationRelevancy(true);
	NoFearWorld::TintPlaceholder(Body, PlaceholderMaterial, FLinearColor(0.12f, 0.1f, 0.08f));
	StatusLight->SetVisibility(false);
	const FTransform BodyTransform(GetActorQuat(), GetActorTransform().TransformPosition(GetBodyCenter()));
	NoFearWorld::SpawnDebris(this, BodyTransform, Size * 0.6f, FVector(8.f), DebrisCount, FVector::ZeroVector, 4.f, PlaceholderMaterial, FLinearColor(0.2f, 0.2f, 0.2f));
	NoFearWorld::PlaySoundAndNoise(this, BreakSound, BodyTransform.GetLocation(), NoiseLoudness, NoiseRange, Cast<APawn>(Breaker), TEXT("Generator"));
}

#undef LOCTEXT_NAMESPACE
