#include "World/NoFearSignals.h"

#include "Components/AudioComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Core/NoFearHealthComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "NavAreas/NavArea_Default.h"
#include "NavModifierComponent.h"
#include "Sound/SoundBase.h"
#include "UObject/ConstructorHelpers.h"
#include "World/NoFearLightSubsystem.h"
#include "World/NoFearWorldLibrary.h"

#define LOCTEXT_NAMESPACE "NoFearSignals"

namespace
{
	UNoFearLightSubsystem* GetLightSubsystem(const UObject* Context)
	{
		const UWorld* World = Context ? Context->GetWorld() : nullptr;
		return World ? World->GetSubsystem<UNoFearLightSubsystem>() : nullptr;
	}

	UStaticMeshComponent* MakeDecor(AActor* Owner, USceneComponent* Parent, FName Name)
	{
		UStaticMeshComponent* Part = Owner->CreateDefaultSubobject<UStaticMeshComponent>(Name);
		Part->SetupAttachment(Parent);
		Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Part->SetCanEverAffectNavigation(false);
		Part->SetGenerateOverlapEvents(false);
		return Part;
	}
}

// ---------------------------------------------------------------------------
// Радио
// ---------------------------------------------------------------------------

ANoFearRadio::ANoFearRadio()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;

	Color = FLinearColor(0.25f, 0.18f, 0.1f);
	Health->MaxHealth = 10.f;
	NoiseLoudness = 0.4f;
	NoiseRange = 800.f;
	DebrisCount = 4;
	Interaction->InteractionDistance = 180.f;
	// Маленькое, стоит на столе: навмеш не режем.
	NavBlocker->AreaClass = UNavArea_Default::StaticClass();

	static ConstructorHelpers::FObjectFinderOptional<USoundBase> MusicFinder(TEXT("/Game/Audio/Ambient/Starter_Music01.Starter_Music01"));
	static ConstructorHelpers::FObjectFinderOptional<USoundBase> ClickFinder(TEXT("/Game/External/LyraContent/Audio/Sounds/Weapons/Pistol/Weapons_Pistol_Slide_01.Weapons_Pistol_Slide_01"));
	PlaySound = MusicFinder.Get();
	ClickSound = ClickFinder.Get();

	Audio = CreateDefaultSubobject<UAudioComponent>(TEXT("Audio"));
	Audio->SetupAttachment(Root);
	Audio->bAutoActivate = false;
	Audio->bAllowSpatialization = true;
	Audio->bOverrideAttenuation = true;
	Audio->AttenuationOverrides.FalloffDistance = 1500.f;

	DialLight = CreateDefaultSubobject<UPointLightComponent>(TEXT("DialLight"));
	DialLight->SetupAttachment(Root);
	DialLight->SetMobility(EComponentMobility::Movable);
	DialLight->SetIntensity(60.f);
	DialLight->SetAttenuationRadius(80.f);
	DialLight->SetCastShadows(false);
	DialLight->SetLightColor(FLinearColor(1.f, 0.7f, 0.3f));
	DialLight->SetVisibility(false);
}

void ANoFearRadio::BeginPlay()
{
	Super::BeginPlay();
	DialLight->SetRelativeLocation(FVector(Size.X * 0.5f + 3.f, 0.f, Size.Z * 0.6f));
	Audio->SetRelativeLocation(GetBodyCenter());
	Audio->SetSound(PlaySound);
	Battery = BatteryTime;
	if (bStartOn)
	{
		bBroadcastShown = true; // Эфир, игравший до прихода героя, не пересказываем.
		SetOn(true, nullptr);
	}
}

void ANoFearRadio::SetBattery(float Seconds)
{
	Battery = FMath::Max(Seconds, 0.f);
}

bool ANoFearRadio::SetOn(bool bNewOn, APawn* User)
{
	if (IsBroken())
	{
		return false;
	}
	if (bNewOn && BatteryTime > 0.f && Battery <= 0.f)
	{
		NoFearWorld::Notify(User, LOCTEXT("DeadBattery", "Батарея села"));
		return false;
	}
	const bool bChanged = bOn != bNewOn;
	bOn = bNewOn;
	NoFearWorld::PlaySoundAndNoise(this, ClickSound, GetActorLocation(), 0.1f, 300.f, User, TEXT("Radio"));
	DialLight->SetVisibility(bOn);
	if (bOn)
	{
		Audio->Play();
		NoiseTime = 0.f;
		if (!bBroadcastShown && !Broadcast.IsEmpty() && User)
		{
			bBroadcastShown = true;
			NoFearWorld::Notify(User, Broadcast);
		}
	}
	else
	{
		Audio->Stop();
	}
	SetActorTickEnabled(bOn);
	if (bChanged)
	{
		OnRadioChanged.Broadcast(this, bOn);
	}
	return true;
}

bool ANoFearRadio::CanInteractBy(const APawn* User) const
{
	return User != nullptr;
}

FText ANoFearRadio::GetPrompt(const APawn* User) const
{
	if (bOn)
	{
		return LOCTEXT("TurnOff", "Выключить радио");
	}
	return BatteryTime > 0.f && Battery <= 0.f ? LOCTEXT("Dead", "Радио (батарея села)") : LOCTEXT("TurnOn", "Включить радио");
}

void ANoFearRadio::InteractBy(APawn* User)
{
	SetOn(!bOn, User);
}

void ANoFearRadio::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!bOn)
	{
		SetActorTickEnabled(false);
		return;
	}
	if (BatteryTime > 0.f)
	{
		Battery -= DeltaSeconds;
		if (Battery <= 0.f)
		{
			Battery = 0.f;
			SetOn(false, nullptr);
			return;
		}
	}
	// Эфир слышно далеко: монстры идут на радио, а не на героя.
	NoiseTime -= DeltaSeconds;
	if (NoiseTime <= 0.f)
	{
		NoiseTime = PlayNoiseInterval;
		NoFearWorld::PlaySoundAndNoise(this, nullptr, GetActorLocation(), PlayNoiseLoudness, PlayNoiseRange, nullptr, TEXT("Radio"));
		NoFearWorld::LureMonsters(this, GetActorLocation(), PlayNoiseLoudness > 0.f ? PlayNoiseRange : 0.f);
	}
}

void ANoFearRadio::HandleBroken(AActor* Breaker)
{
	bOn = false;
	Audio->Stop();
	DialLight->SetVisibility(false);
	SetActorTickEnabled(false);
	OnRadioChanged.Broadcast(this, false);
	Super::HandleBroken(Breaker);
}

// ---------------------------------------------------------------------------
// Сирена
// ---------------------------------------------------------------------------

ANoFearSiren::ANoFearSiren()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;

	Color = FLinearColor(0.45f, 0.42f, 0.15f);
	Health->MaxHealth = 80.f;
	NoiseLoudness = 0.8f;
	NoiseRange = 1500.f;
	DebrisCount = 6;
	Interaction->InteractionDistance = 200.f;

	static ConstructorHelpers::FObjectFinderOptional<USoundBase> ClickFinder(TEXT("/Game/External/LyraContent/Audio/Sounds/Weapons/Shotgun/Weapons_Shotgun_HandHit_01.Weapons_Shotgun_HandHit_01"));
	ClickSound = ClickFinder.Get();

	Horn = MakeDecor(this, Root, TEXT("Horn"));
	Beacon = MakeDecor(this, Root, TEXT("Beacon"));
	Beacon->CastShadow = false;

	BeaconLight = CreateDefaultSubobject<UPointLightComponent>(TEXT("BeaconLight"));
	BeaconLight->SetupAttachment(Beacon);
	BeaconLight->SetMobility(EComponentMobility::Movable);
	BeaconLight->SetIntensity(3000.f);
	BeaconLight->SetAttenuationRadius(900.f);
	BeaconLight->SetCastShadows(false);
	BeaconLight->SetLightColor(FLinearColor(1.f, 0.08f, 0.03f));
	BeaconLight->SetVisibility(false);

	Audio = CreateDefaultSubobject<UAudioComponent>(TEXT("Audio"));
	Audio->SetupAttachment(Root);
	Audio->bAutoActivate = false;
}

void ANoFearSiren::PlaceParts()
{
	Horn->SetStaticMesh(CylinderMesh);
	Horn->SetRelativeLocationAndRotation(FVector(10.f, 0.f, Size.Z + 25.f), FRotator(90.f, 0.f, 0.f));
	Horn->SetRelativeScale3D(FVector(0.3f, 0.3f, 0.45f));
	Beacon->SetStaticMesh(CylinderMesh);
	Beacon->SetRelativeLocation(FVector(-8.f, 0.f, Size.Z + 5.f));
	Beacon->SetRelativeScale3D(FVector(0.16f, 0.16f, 0.12f));
	BeaconLight->SetRelativeLocation(FVector(300.f, 0.f, 0.f));
	Audio->SetRelativeLocation(FVector(10.f, 0.f, Size.Z + 25.f));
	NoFearWorld::TintPlaceholder(Horn, PlaceholderMaterial, FLinearColor(0.15f, 0.15f, 0.15f));
	NoFearWorld::TintPlaceholder(Beacon, PlaceholderMaterial, bSounding ? FLinearColor(1.f, 0.1f, 0.05f) : FLinearColor(0.3f, 0.05f, 0.03f));
}

void ANoFearSiren::BeginPlay()
{
	Super::BeginPlay();
	PlaceParts();
	Audio->SetSound(WailSound);
	if (UNoFearLightSubsystem* Subsystem = GetLightSubsystem(this))
	{
		CircuitHandle = Subsystem->OnCircuitChanged.AddUObject(this, &ANoFearSiren::HandleCircuitChanged);
	}
}

void ANoFearSiren::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UNoFearLightSubsystem* Subsystem = GetLightSubsystem(this))
	{
		Subsystem->OnCircuitChanged.Remove(CircuitHandle);
	}
	Super::EndPlay(EndPlayReason);
}

bool ANoFearSiren::IsPowered() const
{
	const UNoFearLightSubsystem* Subsystem = GetLightSubsystem(this);
	return Subsystem ? Subsystem->IsCircuitPowered(Circuit) : Circuit.IsNone();
}

void ANoFearSiren::HandleCircuitChanged(FName ChangedCircuit, bool bPowered)
{
	// Пропало питание — замолкает.
	if (ChangedCircuit == Circuit && !bPowered && bSounding)
	{
		SetSounding(false, nullptr);
	}
}

float ANoFearSiren::GetWorkProgress() const
{
	return Worker.IsValid() ? FMath::Clamp(WorkTime / FMath::Max(ActivateTime, 0.01f), 0.f, 1.f) : 0.f;
}

bool ANoFearSiren::Activate(APawn* User)
{
	if (IsBroken() || bSounding)
	{
		return false;
	}
	if (!IsPowered())
	{
		NoFearWorld::Notify(User, LOCTEXT("NoPower", "Нет питания"));
		return false;
	}
	if (CooldownLeft > 0.f)
	{
		return false;
	}
	SetSounding(true, User);
	return true;
}

void ANoFearSiren::Silence()
{
	if (bSounding)
	{
		SetSounding(false, nullptr);
	}
}

void ANoFearSiren::SetSounding(bool bNewSounding, APawn* User)
{
	const bool bChanged = bSounding != bNewSounding;
	bSounding = bNewSounding;
	NoFearWorld::PlaySoundAndNoise(this, ClickSound, GetActorLocation(), 0.2f, 500.f, User, TEXT("Siren"));
	if (bSounding)
	{
		TimeLeft = Duration;
		NoiseTime = 0.f;
		Audio->Play();
	}
	else
	{
		Audio->Stop();
		if (bChanged)
		{
			CooldownLeft = Cooldown;
		}
	}
	BeaconLight->SetVisibility(bSounding);
	NoFearWorld::TintPlaceholder(Beacon, PlaceholderMaterial, bSounding ? FLinearColor(1.f, 0.1f, 0.05f) : FLinearColor(0.3f, 0.05f, 0.03f));
	SetActorTickEnabled(true);
	if (bChanged)
	{
		OnSirenChanged.Broadcast(this, bSounding);
	}
}

bool ANoFearSiren::CanInteractBy(const APawn* User) const
{
	return User != nullptr;
}

FText ANoFearSiren::GetPrompt(const APawn* User) const
{
	if (bSounding)
	{
		return LOCTEXT("Silence", "Выключить сирену");
	}
	if (!IsPowered())
	{
		return LOCTEXT("NoPowerPrompt", "Сирена (нет питания)");
	}
	if (CooldownLeft > 0.f)
	{
		return FText::Format(LOCTEXT("CooldownPrompt", "Сирена остывает ({0} с)"), FText::AsNumber(FMath::CeilToInt(CooldownLeft)));
	}
	return LOCTEXT("ActivatePrompt", "Включить сирену (держать)");
}

void ANoFearSiren::InteractBy(APawn* User)
{
	if (bSounding)
	{
		Silence();
		return;
	}
	if (!IsPowered() || CooldownLeft > 0.f)
	{
		Activate(User); // Скажет, почему нет.
		return;
	}
	// Бот или вызов из кода — сразу; игрок держит E.
	if (ActivateTime <= 0.f || !NoFearWorld::IsInteractHeld(User))
	{
		Activate(User);
		return;
	}
	Worker = User;
	WorkTime = 0.f;
	SetActorTickEnabled(true);
}

void ANoFearSiren::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (APawn* User = Worker.Get())
	{
		if (IsBroken() || bSounding || !NoFearWorld::IsInteractHeld(User) || !Interaction->IsInRange(User) || UNoFearHealthComponent::IsActorIncapacitated(User))
		{
			Worker.Reset();
			WorkTime = 0.f;
		}
		else
		{
			WorkTime += DeltaSeconds;
			if (WorkTime >= ActivateTime)
			{
				Worker.Reset();
				WorkTime = 0.f;
				Activate(User);
			}
		}
	}

	if (bSounding)
	{
		TimeLeft -= DeltaSeconds;
		if (TimeLeft <= 0.f)
		{
			SetSounding(false, nullptr);
			return;
		}
		// Мигалка крутится, вой слышно на всю карту.
		SpinTime += DeltaSeconds;
		Beacon->SetRelativeRotation(FRotator(0.f, SpinTime * 360.f, 0.f));
		NoiseTime -= DeltaSeconds;
		if (NoiseTime <= 0.f)
		{
			NoiseTime = WailNoiseInterval;
			// Источник шума — сама сирена: с героем в инстигаторах монстры шли бы искать его, а не на вой.
			NoFearWorld::PlaySoundAndNoise(this, nullptr, Horn->GetComponentLocation(), WailNoiseLoudness, WailNoiseRange, nullptr, TEXT("Siren"));
			NoFearWorld::LureMonsters(this, GetActorLocation(), WailNoiseLoudness > 0.f ? WailNoiseRange : 0.f);
		}
	}
	else if (CooldownLeft > 0.f)
	{
		CooldownLeft = FMath::Max(CooldownLeft - DeltaSeconds, 0.f);
	}
	if (!bSounding && CooldownLeft <= 0.f && !Worker.IsValid())
	{
		SetActorTickEnabled(false);
	}
}

void ANoFearSiren::HandleBroken(AActor* Breaker)
{
	// Шкаф остаётся стоять разбитым: держит пешек и пули, только больше не воет.
	bSounding = false;
	Audio->Stop();
	Worker.Reset();
	BeaconLight->SetVisibility(false);
	OnSirenChanged.Broadcast(this, false);
	SetActorTickEnabled(false);
	Body->SetVisibility(true);
	Body->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	NavBlocker->SetNavigationRelevancy(true);
	NoFearWorld::TintPlaceholder(Body, PlaceholderMaterial, FLinearColor(0.12f, 0.1f, 0.08f));
	Horn->SetVisibility(false);
	const FTransform HornTransform = Horn->GetComponentTransform();
	NoFearWorld::SpawnDebris(this, HornTransform, FVector(40.f), FVector(8.f), DebrisCount, FVector::ZeroVector, 4.f, PlaceholderMaterial, FLinearColor(0.2f, 0.2f, 0.2f));
	NoFearWorld::PlaySoundAndNoise(this, BreakSound, HornTransform.GetLocation(), NoiseLoudness, NoiseRange, Cast<APawn>(Breaker), TEXT("Siren"));
}

#undef LOCTEXT_NAMESPACE
