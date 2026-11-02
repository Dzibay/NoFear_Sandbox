#include "World/NoFearLightSubsystem.h"

#include "Components/BoxComponent.h"
#include "Components/LocalLightComponent.h"
#include "Engine/World.h"

// ---------------------------------------------------------------------------
// Тёмная зона
// ---------------------------------------------------------------------------

ANoFearDarkVolume::ANoFearDarkVolume()
{
	Bounds = CreateDefaultSubobject<UBoxComponent>(TEXT("Bounds"));
	RootComponent = Bounds;
	Bounds->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Bounds->SetCanEverAffectNavigation(false);
	Bounds->SetHiddenInGame(true);
	Bounds->ShapeColor = FColor(30, 30, 60);
	Bounds->SetBoxExtent(Extent);
}

void ANoFearDarkVolume::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	Bounds->SetBoxExtent(Extent);
}

void ANoFearDarkVolume::BeginPlay()
{
	Super::BeginPlay();
	Bounds->SetBoxExtent(Extent);
	if (UNoFearLightSubsystem* Subsystem = GetWorld()->GetSubsystem<UNoFearLightSubsystem>())
	{
		Subsystem->RegisterDarkVolume(this);
	}
}

void ANoFearDarkVolume::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UNoFearLightSubsystem* Subsystem = GetWorld() ? GetWorld()->GetSubsystem<UNoFearLightSubsystem>() : nullptr)
	{
		Subsystem->UnregisterDarkVolume(this);
	}
	Super::EndPlay(EndPlayReason);
}

bool ANoFearDarkVolume::IsInside(FVector Location) const
{
	const FVector Local = GetActorTransform().InverseTransformPosition(Location);
	return FMath::Abs(Local.X) <= Extent.X && FMath::Abs(Local.Y) <= Extent.Y && FMath::Abs(Local.Z) <= Extent.Z;
}

// ---------------------------------------------------------------------------
// Подсистема
// ---------------------------------------------------------------------------

float UNoFearLightSubsystem::GetLightLevel(const UObject* WorldContextObject, FVector Location)
{
	const UWorld* World = WorldContextObject ? WorldContextObject->GetWorld() : nullptr;
	const UNoFearLightSubsystem* Subsystem = World ? World->GetSubsystem<UNoFearLightSubsystem>() : nullptr;
	return Subsystem ? Subsystem->ComputeLightLevel(Location) : 1.f;
}

float UNoFearLightSubsystem::GetDetectionFactor(const UObject* WorldContextObject, FVector Location)
{
	return FMath::Lerp(DarkDetectFactor, 1.f, GetLightLevel(WorldContextObject, Location));
}

float UNoFearLightSubsystem::GetDarknessFearFactor(const UObject* WorldContextObject, FVector Location)
{
	return FMath::Lerp(DarkFearFactor, 1.f, GetLightLevel(WorldContextObject, Location));
}

void UNoFearLightSubsystem::RegisterLight(ULocalLightComponent* Light, float Weight)
{
	if (!Light)
	{
		return;
	}
	for (FLightEntry& Entry : Lights)
	{
		if (Entry.Light.Get() == Light)
		{
			Entry.Weight = Weight;
			return;
		}
	}
	Lights.Add({ Light, Weight });
}

void UNoFearLightSubsystem::UnregisterLight(ULocalLightComponent* Light)
{
	Lights.RemoveAll([Light](const FLightEntry& Entry)
	{
		return !Entry.Light.IsValid() || Entry.Light.Get() == Light;
	});
}

bool UNoFearLightSubsystem::IsCircuitPowered(FName Circuit) const
{
	if (Circuit.IsNone())
	{
		return true;
	}
	const TArray<TWeakObjectPtr<const UObject>>* Sources = CircuitSources.Find(Circuit);
	return Sources && Sources->ContainsByPredicate([](const TWeakObjectPtr<const UObject>& Source) { return Source.IsValid(); });
}

void UNoFearLightSubsystem::SetCircuitPowered(FName Circuit, const UObject* Source, bool bPowered)
{
	if (Circuit.IsNone() || !Source)
	{
		return;
	}
	const bool bWas = IsCircuitPowered(Circuit);
	TArray<TWeakObjectPtr<const UObject>>& Sources = CircuitSources.FindOrAdd(Circuit);
	Sources.RemoveAll([Source](const TWeakObjectPtr<const UObject>& Entry) { return !Entry.IsValid() || Entry.Get() == Source; });
	if (bPowered)
	{
		Sources.Add(Source);
	}
	const bool bNow = IsCircuitPowered(Circuit);
	if (bNow != bWas)
	{
		OnCircuitChanged.Broadcast(Circuit, bNow);
	}
}

void UNoFearLightSubsystem::RegisterDarkVolume(ANoFearDarkVolume* Volume)
{
	DarkVolumes.AddUnique(Volume);
}

void UNoFearLightSubsystem::UnregisterDarkVolume(ANoFearDarkVolume* Volume)
{
	DarkVolumes.RemoveAll([Volume](const TWeakObjectPtr<ANoFearDarkVolume>& Entry)
	{
		return !Entry.IsValid() || Entry.Get() == Volume;
	});
}

float UNoFearLightSubsystem::ComputeLightLevel(const FVector& Location) const
{
	// Вне тёмных зон — день.
	float Level = 1.f;
	bool bInDark = false;
	for (const TWeakObjectPtr<ANoFearDarkVolume>& Entry : DarkVolumes)
	{
		if (const ANoFearDarkVolume* Volume = Entry.Get(); Volume && Volume->IsInside(Location))
		{
			Level = bInDark ? FMath::Min(Level, Volume->AmbientLight) : Volume->AmbientLight;
			bInDark = true;
		}
	}
	if (!bInDark)
	{
		return 1.f;
	}

	// Лампы: квадратичное затухание до радиуса света; стены не учитываем — лампы стоят в тех же комнатах.
	for (const FLightEntry& Entry : Lights)
	{
		const ULocalLightComponent* Light = Entry.Light.Get();
		if (!Light || !Light->IsVisible() || Light->Intensity <= 0.f || !Light->IsRegistered())
		{
			continue;
		}
		const float Radius = FMath::Max(Light->AttenuationRadius * 0.8f, 1.f);
		const float Distance = FVector::Dist(Light->GetComponentLocation(), Location);
		if (Distance < Radius)
		{
			Level += Entry.Weight * FMath::Square(1.f - Distance / Radius);
		}
	}
	return FMath::Clamp(Level, 0.f, 1.f);
}
