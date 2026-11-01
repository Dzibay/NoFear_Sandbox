#include "Core/NoFearFearComponent.h"

#include "Core/NoFearAISettings.h"
#include "Core/NoFearFearSubsystem.h"
#include "Core/NoFearHealthComponent.h"
#include "Core/NoFearTeam.h"
#include "Engine/World.h"
#include "GameFramework/Controller.h"

namespace
{
	const FNoFearFearTuning& GetFearTuning(const UObject* WorldContext)
	{
		if (const UNoFearFearSubsystem* Subsystem = UNoFearFearSubsystem::Get(WorldContext))
		{
			return *Subsystem;
		}
		return UNoFearAISettings::Get()->Fear;
	}
}

UNoFearFearComponent::UNoFearFearComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

UNoFearFearComponent* UNoFearFearComponent::FindFear(const AActor* Actor)
{
	if (const AController* Controller = Cast<AController>(Actor))
	{
		Actor = Controller->GetPawn();
	}
	return Actor ? Actor->FindComponentByClass<UNoFearFearComponent>() : nullptr;
}

float UNoFearFearComponent::GetActorFear(const AActor* Actor)
{
	const UNoFearFearComponent* Component = FindFear(Actor);
	return Component ? Component->GetFear() : 0.f;
}

void UNoFearFearComponent::BeginPlay()
{
	Super::BeginPlay();
	Level = GetLevelFor(Fear);
	if (UNoFearFearSubsystem* Subsystem = UNoFearFearSubsystem::Get(this))
	{
		Subsystem->RegisterFear(this);
	}
}

void UNoFearFearComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UNoFearFearSubsystem* Subsystem = UNoFearFearSubsystem::Get(this))
	{
		Subsystem->UnregisterFear(this);
	}
	Super::EndPlay(EndPlayReason);
}

double UNoFearFearComponent::GetNow() const
{
	const UWorld* World = GetWorld();
	return World ? World->GetTimeSeconds() : 0.0;
}

ENoFearFearLevel UNoFearFearComponent::GetLevelFor(float Value) const
{
	const FNoFearFearTuning& Tuning = GetFearTuning(this);
	if (Value >= Tuning.TerrorThreshold)
	{
		return ENoFearFearLevel::Terror;
	}
	if (Value >= Tuning.PanicThreshold)
	{
		return ENoFearFearLevel::Panic;
	}
	if (Value >= Tuning.AnxiousThreshold)
	{
		return ENoFearFearLevel::Anxious;
	}
	return ENoFearFearLevel::Calm;
}

ENoFearFearLevel UNoFearFearComponent::GetFearLevel() const
{
	return GetLevelFor(Fear);
}

float UNoFearFearComponent::GetGainScale() const
{
	const ENoFearTeam Team = UNoFearTeamLibrary::GetTeam(GetOwner());
	if (const UNoFearFearSubsystem* Subsystem = UNoFearFearSubsystem::Get(this))
	{
		return Subsystem->GetTeamGainScale(Team) * GainMultiplier;
	}
	return UNoFearFearSubsystem::IsFrightening(Team) ? 0.f : GainMultiplier;
}

void UNoFearFearComponent::SetFear(float NewFear)
{
	ApplyFear(NewFear);
}

float UNoFearFearComponent::AddFear(float Amount, float Cap, FVector InSourceLocation, bool bDirect)
{
	if (Amount <= 0.f || UNoFearHealthComponent::IsActorDead(GetOwner()))
	{
		return 0.f;
	}
	const float Scale = GetGainScale();
	if (Scale <= 0.f)
	{
		return 0.f;
	}

	// Испуг держит страх: пока он повторяется, спад не начинается, даже если страх уже выше потолка события.
	const double Now = GetNow();
	LastGainTime = Now;
	if (bDirect)
	{
		LastDirectGainTime = Now;
	}

	Cap = FMath::Clamp(Cap, 0.f, 100.f);
	if (Fear >= Cap)
	{
		return 0.f;
	}
	SourceLocation = InSourceLocation;
	const float OldFear = Fear;
	ApplyFear(FMath::Min(Fear + Amount * Scale, Cap));
	return Fear - OldFear;
}

float UNoFearFearComponent::AddFearFrom(float Amount, float Cap, FVector InSourceLocation, const AActor* Source, bool bDirect)
{
	const ENoFearTeam OwnTeam = UNoFearTeamLibrary::GetTeam(GetOwner());
	const ENoFearTeam SourceTeam = Source ? UNoFearTeamLibrary::GetTeam(Source) : ENoFearTeam::Neutral;
	if (UNoFearFearSubsystem::IsShooterTeam(OwnTeam) && !UNoFearFearSubsystem::IsFrightening(SourceTeam))
	{
		Cap = FMath::Min(Cap, GetFearTuning(this).CombatFearCap);
	}
	if (UNoFearFearSubsystem::IsFrightening(SourceTeam) && IsSteady())
	{
		// Свои рядом: страшно, но не до ужаса — монстр выше потолка строя страх не держит.
		Cap = FMath::Min(Cap, SteadyCap);
		bDirect = bDirect && Fear < SteadyCap;
	}
	return AddFear(Amount, Cap, InSourceLocation, bDirect);
}

bool UNoFearFearComponent::IsSteady() const
{
	return GetNow() < SteadyUntil;
}

void UNoFearFearComponent::Soothe(float Amount)
{
	if (Amount > 0.f)
	{
		ApplyFear(Fear - Amount);
	}
}

void UNoFearFearComponent::Comfort(float Amount)
{
	LastComfortTime = GetNow();
	Soothe(Amount);
}

bool UNoFearFearComponent::IsSpreading() const
{
	const FNoFearFearTuning& Tuning = GetFearTuning(this);
	return Fear >= Tuning.SpreadMinFear && GetNow() - LastDirectGainTime <= Tuning.SpreadRecentTime;
}

void UNoFearFearComponent::ApplyFear(float NewFear)
{
	Fear = FMath::Clamp(NewFear, 0.f, 100.f);
	const ENoFearFearLevel NewLevel = GetLevelFor(Fear);
	if (NewLevel != Level)
	{
		const ENoFearFearLevel OldLevel = Level;
		Level = NewLevel;
		OnFearLevelChanged.Broadcast(this, OldLevel, NewLevel);
	}
}
