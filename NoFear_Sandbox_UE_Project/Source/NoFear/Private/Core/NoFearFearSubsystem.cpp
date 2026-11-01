#include "Core/NoFearFearSubsystem.h"
#include "Core/NoFearAISettings.h"
#include "Core/NoFearPerf.h"

#include "Core/NoFearFearComponent.h"
#include "Core/NoFearHealthComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"

namespace
{
	TAutoConsoleVariable<int32> CVarFearDebug(
		TEXT("NoFear.Fear.Debug"),
		0,
		TEXT("1 — число страха над каждым человеком (цвет по уровню)."),
		ECVF_Default);

	struct FFearEntry
	{
		UNoFearFearComponent* Component = nullptr;
		FVector Location = FVector::ZeroVector;
		ENoFearTeam Team = ENoFearTeam::Neutral;
		float Fear = 0.f;
		bool bCanBeAfraid = false;
		bool bGuardian = false;
		bool bSpreading = false;
		bool bCalm = false;
	};

	float DistanceAlpha(float Distance, float Near, float Far)
	{
		return Far > Near ? FMath::Clamp((Distance - Near) / (Far - Near), 0.f, 1.f) : (Distance > Near ? 1.f : 0.f);
	}
}

UNoFearFearSubsystem* UNoFearFearSubsystem::Get(const UObject* WorldContextObject)
{
	const UWorld* World = WorldContextObject ? WorldContextObject->GetWorld() : nullptr;
	return World ? World->GetSubsystem<UNoFearFearSubsystem>() : nullptr;
}

bool UNoFearFearSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

TStatId UNoFearFearSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UNoFearFearSubsystem, STATGROUP_Tickables);
}

void UNoFearFearSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	static_cast<FNoFearFearTuning&>(*this) = UNoFearAISettings::Get()->Fear;
}

void UNoFearFearSubsystem::RegisterFear(UNoFearFearComponent* Component)
{
	if (Component)
	{
		Components.AddUnique(Component);
	}
}

void UNoFearFearSubsystem::UnregisterFear(UNoFearFearComponent* Component)
{
	Components.Remove(Component);
}

void UNoFearFearSubsystem::Tick(float DeltaTime)
{
	NOFEAR_PERF_SCOPE(Fear);
	Super::Tick(DeltaTime);
	TimeSinceUpdate += DeltaTime;
	if (TimeSinceUpdate >= UpdateInterval)
	{
		UpdateFear(TimeSinceUpdate);
		TimeSinceUpdate = 0.f;
	}
	if (CVarFearDebug.GetValueOnGameThread() > 0)
	{
		DrawDebug();
	}
}

float UNoFearFearSubsystem::GetTeamGainScale(ENoFearTeam Team) const
{
	switch (Team)
	{
	case ENoFearTeam::Monster:
	case ENoFearTeam::AggroMonster:
		return 0.f;
	case ENoFearTeam::Enemy:
		return EnemyGainScale;
	case ENoFearTeam::Ally:
		return AllyGainScale;
	case ENoFearTeam::Player:
		return PlayerGainScale;
	default:
		return CivilianGainScale;
	}
}

void UNoFearFearSubsystem::GetDangerFear(ENoFearDangerType Type, float Distance, float& OutAmount, float& OutCap) const
{
	OutAmount = 0.f;
	OutCap = 0.f;
	switch (Type)
	{
	case ENoFearDangerType::Gunshot:
		if (Distance <= NearGunshotRadius)
		{
			OutAmount = NearGunshotFear;
			OutCap = NearGunshotCap;
		}
		else
		{
			OutAmount = FarGunshotFear;
			OutCap = FarGunshotCap;
		}
		break;
	case ENoFearDangerType::NearMiss:
		OutAmount = NearMissFear;
		OutCap = NearMissCap;
		break;
	case ENoFearDangerType::Fight:
		OutAmount = FightFear;
		OutCap = FightCap;
		break;
	case ENoFearDangerType::Scream:
		OutAmount = FMath::Lerp(ScreamNearFear, ScreamFarFear, DistanceAlpha(Distance, ScreamNearRadius, ScreamFarRadius));
		OutCap = ScreamCap;
		break;
	case ENoFearDangerType::Explosion:
		OutAmount = ExplosionFear;
		OutCap = ExplosionCap;
		break;
	case ENoFearDangerType::Grenade:
		OutAmount = GrenadeFear;
		OutCap = GrenadeCap;
		break;
	default:
		break;
	}
}

void UNoFearFearSubsystem::GetMonsterSightFear(float Distance, float& OutShock, float& OutRate, float& OutCap) const
{
	const float Alpha = DistanceAlpha(Distance, MonsterNearDistance, MonsterFarDistance);
	OutShock = FMath::Lerp(MonsterShockNear, MonsterShockFar, Alpha);
	OutRate = FMath::Lerp(MonsterSightRateNear, MonsterSightRateFar, Alpha);
	OutCap = FMath::Lerp(MonsterSightCapNear, MonsterSightCapFar, Alpha);
}

bool UNoFearFearSubsystem::IsGuardianNear(const AActor* Actor) const
{
	if (!Actor)
	{
		return false;
	}
	const FVector Location = Actor->GetActorLocation();
	const float RadiusSq = FMath::Square(GuardianRadius);
	for (const TWeakObjectPtr<UNoFearFearComponent>& Weak : Components)
	{
		const UNoFearFearComponent* Component = Weak.Get();
		const AActor* Owner = Component ? Component->GetOwner() : nullptr;
		if (!Owner || Owner == Actor || !IsGuardianTeam(UNoFearTeamLibrary::GetTeam(Owner)) || UNoFearHealthComponent::IsActorIncapacitated(Owner))
		{
			continue;
		}
		if (FVector::DistSquared(Owner->GetActorLocation(), Location) <= RadiusSq)
		{
			return true;
		}
	}
	return false;
}

float UNoFearFearSubsystem::GetAverageFear(ENoFearTeam Team) const
{
	float Sum = 0.f;
	int32 Count = 0;
	for (const TWeakObjectPtr<UNoFearFearComponent>& Weak : Components)
	{
		const UNoFearFearComponent* Component = Weak.Get();
		const AActor* Owner = Component ? Component->GetOwner() : nullptr;
		if (Owner && UNoFearTeamLibrary::GetTeam(Owner) == Team && !UNoFearHealthComponent::IsActorDead(Owner))
		{
			Sum += Component->GetFear();
			++Count;
		}
	}
	return Count > 0 ? Sum / Count : 0.f;
}

void UNoFearFearSubsystem::UpdateFear(float DeltaTime)
{
	const UWorld* World = GetWorld();
	if (!World || DeltaTime <= 0.f)
	{
		return;
	}
	const double Now = World->GetTimeSeconds();

	Components.RemoveAll([](const TWeakObjectPtr<UNoFearFearComponent>& Weak) { return !Weak.IsValid(); });

	// Снимок до изменений: кто паникует и кто спокоен, считается по началу шага.
	TArray<FFearEntry> Entries;
	Entries.Reserve(Components.Num());
	for (const TWeakObjectPtr<UNoFearFearComponent>& Weak : Components)
	{
		UNoFearFearComponent* Component = Weak.Get();
		const AActor* Owner = Component->GetOwner();
		if (!Owner || UNoFearHealthComponent::IsActorDead(Owner))
		{
			continue;
		}
		FFearEntry& Entry = Entries.AddDefaulted_GetRef();
		Entry.Component = Component;
		Entry.Location = Owner->GetActorLocation();
		Entry.Team = UNoFearTeamLibrary::GetTeam(Owner);
		Entry.Fear = Component->GetFear();
		Entry.bCanBeAfraid = GetTeamGainScale(Entry.Team) * Component->GainMultiplier > 0.f;
		Entry.bGuardian = IsGuardianTeam(Entry.Team) && !UNoFearHealthComponent::IsActorDowned(Owner);
		Entry.bSpreading = Entry.bCanBeAfraid && Entry.Fear >= SpreadMinFear && Now - Component->GetLastDirectGainTime() <= SpreadRecentTime;
		Entry.bCalm = Entry.bCanBeAfraid && Entry.Fear < AnxiousThreshold;
	}

	const float GuardianRadiusSq = FMath::Square(GuardianRadius);
	const float SpreadRadiusSq = FMath::Square(SpreadRadius);
	const float CalmRadiusSq = FMath::Square(CalmNeighbourRadius);

	for (int32 Index = 0; Index < Entries.Num(); ++Index)
	{
		const FFearEntry& Entry = Entries[Index];
		if (!Entry.bCanBeAfraid)
		{
			continue;
		}

		bool bGuardianNear = false;
		int32 Spreaders = 0;
		int32 CalmNeighbours = 0;
		float NearestSpreaderSq = TNumericLimits<float>::Max();
		FVector SpreaderLocation = FVector::ZeroVector;
		for (int32 Other = 0; Other < Entries.Num(); ++Other)
		{
			if (Other == Index)
			{
				continue;
			}
			const FFearEntry& Neighbour = Entries[Other];
			const float DistSq = static_cast<float>(FVector::DistSquared(Entry.Location, Neighbour.Location));
			bGuardianNear |= Neighbour.bGuardian && DistSq <= GuardianRadiusSq;
			if (Neighbour.bSpreading && DistSq <= SpreadRadiusSq)
			{
				++Spreaders;
				if (DistSq < NearestSpreaderSq)
				{
					NearestSpreaderSq = DistSq;
					// Сосед пугает тем же, чем напуган сам: бегут от источника, а не от соседа.
					SpreaderLocation = Neighbour.Component->GetSourceLocation();
				}
			}
			CalmNeighbours += Neighbour.bCalm && DistSq <= CalmRadiusSq ? 1 : 0;
		}

		UNoFearFearComponent* Component = Entry.Component;
		if (Spreaders > 0)
		{
			const float Cap = IsShooterTeam(Entry.Team) ? FMath::Min(SpreadCap, CombatFearCap) : SpreadCap;
			Component->AddFear(SpreadRate * FMath::Min(Spreaders, SpreadMaxSources) * DeltaTime, Cap, SpreaderLocation, false);
		}

		if (Component->GetFear() <= 0.f || Component->DecayMultiplier <= 0.f)
		{
			continue;
		}
		const float GuardianScale = bGuardianNear ? GuardianDecayMultiplier : 1.f;
		const float Delay = DecayDelay / FMath::Min(GuardianScale, 2.f);
		if (Now - Component->GetLastGainTime() < Delay)
		{
			continue;
		}
		const float CalmBonus = FMath::Min(CalmNeighbours * CalmNeighbourBonus, CalmNeighbourMaxBonus);
		Component->Soothe(DecayRate * GuardianScale * (1.f + CalmBonus) * Component->DecayMultiplier * DeltaTime);
	}
}

void UNoFearFearSubsystem::DrawDebug() const
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	for (const TWeakObjectPtr<UNoFearFearComponent>& Weak : Components)
	{
		const UNoFearFearComponent* Component = Weak.Get();
		const AActor* Owner = Component ? Component->GetOwner() : nullptr;
		if (!Owner || !Component->CanBeAfraid() || UNoFearHealthComponent::IsActorDead(Owner))
		{
			continue;
		}
		FColor Color = FColor(120, 220, 120);
		switch (Component->GetFearLevel())
		{
		case ENoFearFearLevel::Anxious: Color = FColor(255, 220, 60); break;
		case ENoFearFearLevel::Panic: Color = FColor(255, 140, 30); break;
		case ENoFearFearLevel::Terror: Color = FColor(255, 50, 40); break;
		default: break;
		}
		const FString Text = FString::Printf(TEXT("%.0f%s"), Component->GetFear(), Component->IsSpreading() ? TEXT("!") : TEXT(""));
		DrawDebugString(World, Owner->GetActorLocation() + FVector(0.f, 0.f, 135.f), Text, nullptr, Color, 0.f, true, 1.2f);
	}
}
