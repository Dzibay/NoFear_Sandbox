#include "Core/NoFearBraceletComponent.h"

#include "AI/NoFearAIController.h"
#include "AI/NoFearCompanionAIController.h"
#include "Components/AudioComponent.h"
#include "Core/NoFearAISettings.h"
#include "Engine/PostProcessVolume.h"
#include "Core/NoFearCharacter.h"
#include "Core/NoFearFearComponent.h"
#include "Core/NoFearFearSubsystem.h"
#include "Core/NoFearPlayerController.h"
#include "Core/NoFearTeam.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundBase.h"
#include "Sound/SoundWaveProcedural.h"
#include "Weapons/NoFearWeaponComponent.h"

namespace NoFearBracelet
{
	static TAutoConsoleVariable<int32> CVarDebug(
		TEXT("NoFear.Hero.Debug"), 0,
		TEXT("1 — строка состояния страха героя на экране (источники, эффекты, способности)."));

	constexpr float ScanInterval = 0.2f;
	constexpr int32 SampleRate = 22050;

	const FName Monster(TEXT("Monster"));
	const FName Health(TEXT("Health"));
	const FName Damage(TEXT("Damage"));
	const FName Companion(TEXT("Companion"));
	const FName Combat(TEXT("Combat"));
	const FName Corpse(TEXT("Corpse"));
	const FName Relief(TEXT("Relief"));
	const FName Stabilize(TEXT("Stabilize"));

	/** 0 у From, 1 у To, между — линейно. */
	float Ramp(float Value, float From, float To)
	{
		return To > From ? FMath::Clamp((Value - From) / (To - From), 0.f, 1.f) : (Value >= To ? 1.f : 0.f);
	}

	/** Затухающий синус в буфер (складывается с тем, что там уже есть). */
	void AddTone(TArray<float>& Buffer, float StartTime, float Duration, float Frequency, float Amplitude, float Decay)
	{
		const int32 Start = FMath::RoundToInt(StartTime * SampleRate);
		const int32 Count = FMath::RoundToInt(Duration * SampleRate);
		for (int32 i = 0; i < Count && Start + i < Buffer.Num(); ++i)
		{
			const float T = static_cast<float>(i) / SampleRate;
			const float Attack = FMath::Min(1.f, T / 0.004f);
			Buffer[Start + i] += Amplitude * Attack * FMath::Exp(-Decay * T) * FMath::Sin(2.f * PI * Frequency * T);
		}
	}

	TArray<int16> ToPcm(const TArray<float>& Buffer, float Volume)
	{
		TArray<int16> Samples;
		Samples.SetNumUninitialized(Buffer.Num());
		for (int32 i = 0; i < Buffer.Num(); ++i)
		{
			Samples[i] = static_cast<int16>(FMath::Clamp(Buffer[i] * Volume, -1.f, 1.f) * 32000.f);
		}
		return Samples;
	}
}

UNoFearBraceletComponent::UNoFearBraceletComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
}

// ---------------------------------------------------------------------------
// Поиск
// ---------------------------------------------------------------------------

UNoFearBraceletComponent* UNoFearBraceletComponent::FindBracelet(const AActor* PawnOrController)
{
	if (!PawnOrController)
	{
		return nullptr;
	}
	if (const AController* Controller = Cast<AController>(PawnOrController))
	{
		return Controller->FindComponentByClass<UNoFearBraceletComponent>();
	}
	const APawn* Pawn = Cast<APawn>(PawnOrController);
	const APlayerController* Controller = Pawn ? Cast<APlayerController>(Pawn->GetController()) : nullptr;
	return Controller ? Controller->FindComponentByClass<UNoFearBraceletComponent>() : nullptr;
}

float UNoFearBraceletComponent::GetSpreadMultiplierFor(const AActor* Pawn)
{
	const UNoFearBraceletComponent* Bracelet = FindBracelet(Pawn);
	return Bracelet ? Bracelet->GetSpreadMultiplier() : 1.f;
}

float UNoFearBraceletComponent::GetReloadSpeedFor(const AActor* Pawn)
{
	const UNoFearBraceletComponent* Bracelet = FindBracelet(Pawn);
	return Bracelet ? Bracelet->GetReloadSpeed() : 1.f;
}

APlayerController* UNoFearBraceletComponent::GetPlayerController() const
{
	return Cast<APlayerController>(GetOwner());
}

APawn* UNoFearBraceletComponent::GetHeroPawn() const
{
	const APlayerController* Controller = GetPlayerController();
	return Controller ? Controller->GetPawn() : nullptr;
}

bool UNoFearBraceletComponent::IsHeroActive(const APawn* Pawn) const
{
	return Pawn && !UNoFearHealthComponent::IsActorDead(Pawn) && !UNoFearHealthComponent::IsActorDowned(Pawn);
}

double UNoFearBraceletComponent::GetNow() const
{
	const UWorld* World = GetWorld();
	return World ? World->GetTimeSeconds() : 0.0;
}

UNoFearFearComponent* UNoFearBraceletComponent::GetHeroFear() const
{
	return UNoFearFearComponent::FindFear(GetHeroPawn());
}

float UNoFearBraceletComponent::GetFear() const
{
	const UNoFearFearComponent* Fear = GetHeroFear();
	return Fear ? Fear->GetFear() : 0.f;
}

float UNoFearBraceletComponent::GetThreshold(int32 Level) const
{
	const UNoFearFearSubsystem* Subsystem = UNoFearFearSubsystem::Get(this);
	switch (Level)
	{
	case 0: return Subsystem ? Subsystem->AnxiousThreshold : 30.f;
	case 1: return Subsystem ? Subsystem->PanicThreshold : 60.f;
	default: return Subsystem ? Subsystem->TerrorThreshold : 85.f;
	}
}

// ---------------------------------------------------------------------------
// Жизненный цикл
// ---------------------------------------------------------------------------

void UNoFearBraceletComponent::BeginPlay()
{
	Super::BeginPlay();

	if (const UNoFearAISettings* Settings = GetDefault<UNoFearAISettings>())
	{
		Tuning = Settings->HeroFear;
	}

	UWorld* World = GetWorld();
	if (World && !PostProcess)
	{
		FActorSpawnParameters Params;
		Params.Owner = GetOwner();
		Params.ObjectFlags |= RF_Transient;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		PostProcess = World->SpawnActor<APostProcessVolume>(Params);
		if (PostProcess)
		{
			PostProcess->bUnbound = true;
			PostProcess->Priority = 1000.f;
			PostProcess->BlendWeight = 1.f;
			PostProcess->bEnabled = false;
		}
	}
}

void UNoFearBraceletComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (bInStupor)
	{
		EndStupor();
	}
	ApplyBreathModifiers(false);
	UnbindPawn();
	for (UAudioComponent* Audio : { HeartAudio.Get(), BeepAudio.Get() })
	{
		if (Audio)
		{
			Audio->Stop();
		}
	}
	if (PostProcess)
	{
		PostProcess->Destroy();
		PostProcess = nullptr;
	}
	Super::EndPlay(EndPlayReason);
}

void UNoFearBraceletComponent::BindPawn(APawn* Pawn)
{
	BoundPawn = Pawn;
	if (UNoFearHealthComponent* Health = UNoFearHealthComponent::FindHealth(Pawn))
	{
		Health->OnDamaged.AddUniqueDynamic(this, &UNoFearBraceletComponent::HandleHeroDamaged);
	}
}

void UNoFearBraceletComponent::UnbindPawn()
{
	if (UNoFearHealthComponent* Health = UNoFearHealthComponent::FindHealth(BoundPawn.Get()))
	{
		Health->OnDamaged.RemoveDynamic(this, &UNoFearBraceletComponent::HandleHeroDamaged);
	}
	BoundPawn.Reset();
}

void UNoFearBraceletComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	APawn* Pawn = GetHeroPawn();
	if (Pawn != BoundPawn.Get())
	{
		// Новая жизнь (или отпустил пешку): страх с нуля, эффекты сброшены.
		if (bInStupor)
		{
			EndStupor();
		}
		ApplyBreathModifiers(false);
		bHoldingBreath = false;
		UnbindPawn();
		SeenMonsters.Reset();
		SeenCorpses.Reset();
		bCompanionWasDowned = false;
		EffectFear = 0.f;
		AppliedShake = FRotator::ZeroRotator;
		StabilizeRemaining = 0.f;
		Breath = 1.f;
		if (Pawn)
		{
			BindPawn(Pawn);
		}
	}

	UNoFearFearComponent* Fear = GetHeroFear();
	if (Fear && !bHoldingBreath)
	{
		Fear->DecayMultiplier = Tuning.DecayMultiplier;
	}

	if (IsHeroActive(Pawn) && Fear)
	{
		ScanTimer -= DeltaTime;
		const bool bScan = ScanTimer <= 0.f;
		if (bScan)
		{
			ScanTimer += NoFearBracelet::ScanInterval;
			if (ScanTimer < 0.f)
			{
				ScanTimer = NoFearBracelet::ScanInterval;
			}
		}
		UpdateMonsters(Pawn, bScan ? NoFearBracelet::ScanInterval : 0.f);
		if (bScan)
		{
			UpdateCorpses(Pawn);
		}
		// Страх от монстра на виду — каждый кадр, по ближайшему с последнего осмотра.
		if (VisibleMonsters > 0 && NearestMonsterDistance >= 0.f)
		{
			const float Alpha = NoFearBracelet::Ramp(NearestMonsterDistance, Tuning.MonsterNearDistance, Tuning.MonsterFarDistance);
			const float Rate = FMath::Lerp(Tuning.MonsterRateNear, Tuning.MonsterRateFar, Alpha);
			const float Cap = FMath::Lerp(Tuning.MonsterCapNear, Tuning.MonsterCapFar, Alpha);
			AddHeroFear(NoFearBracelet::Monster, Rate * DeltaTime, Cap, Pawn->GetActorLocation());
		}
		UpdateHealth(Pawn, DeltaTime);
		UpdateCompanion(Pawn, DeltaTime);
	}
	else
	{
		VisibleMonsters = 0;
		NearestMonsterDistance = -1.f;
	}

	UpdateStabilize(DeltaTime);
	UpdateBreath(Pawn, DeltaTime);
	UpdateStupor(Pawn);

	EffectFear = FMath::FInterpTo(EffectFear, GetFear(), DeltaTime, 3.f);
	UpdateAimShake(DeltaTime);
	UpdateHeartbeat(DeltaTime);
	UpdateScreenEffects();
	UpdateRiftBeep();

	if (NoFearBracelet::CVarDebug.GetValueOnGameThread() > 0 && GEngine)
	{
		GEngine->AddOnScreenDebugMessage(static_cast<uint64>(GetUniqueID()), 0.f, FColor::Orange, GetDebugText());
	}
}

// ---------------------------------------------------------------------------
// Источники страха
// ---------------------------------------------------------------------------

float UNoFearBraceletComponent::AddHeroFear(FName Tag, float Amount, float Cap, const FVector& Location, const AActor* Source)
{
	UNoFearFearComponent* Fear = GetHeroFear();
	if (!Fear || Amount <= 0.f || !IsHeroActive(GetHeroPawn()))
	{
		return 0.f;
	}
	const float Before = Fear->GetFear();
	if (Source)
	{
		Fear->AddFearFrom(Amount, Cap, Location, Source, true);
	}
	else
	{
		Fear->AddFear(Amount, Cap, Location, true);
	}
	const float Gain = FMath::Max(0.f, Fear->GetFear() - Before);
	if (Gain > 0.f)
	{
		SourceTotals.FindOrAdd(Tag) += Gain;
	}
	return Gain;
}

void UNoFearBraceletComponent::Relieve(FName Tag, float Amount)
{
	UNoFearFearComponent* Fear = GetHeroFear();
	if (!Fear || Amount <= 0.f)
	{
		return;
	}
	const float Before = Fear->GetFear();
	Fear->Soothe(Amount);
	const float Removed = FMath::Max(0.f, Before - Fear->GetFear());
	if (Removed > 0.f)
	{
		SourceTotals.FindOrAdd(Tag) += Removed;
	}
}

bool UNoFearBraceletComponent::CanSee(const APawn* Pawn, const AActor* Target, const FVector& ViewLocation, const FVector& TargetPoint) const
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}
	FCollisionQueryParams Params(SCENE_QUERY_STAT(NoFearHeroSight), true, Pawn);
	Params.AddIgnoredActor(Target);
	TArray<AActor*> AttachedActors;
	Pawn->GetAttachedActors(AttachedActors, false, true);
	Target->GetAttachedActors(AttachedActors, false, true);
	Params.AddIgnoredActors(AttachedActors);
	return !World->LineTraceTestByChannel(ViewLocation, TargetPoint, ECC_Visibility, Params);
}

void UNoFearBraceletComponent::UpdateMonsters(APawn* Pawn, float ScanDelta)
{
	if (ScanDelta <= 0.f)
	{
		return;
	}
	VisibleMonsters = 0;
	NearestMonsterDistance = -1.f;

	const APlayerController* Controller = GetPlayerController();
	const UNoFearBotSubsystem* Bots = UNoFearBotSubsystem::Get(this);
	if (!Controller || !Bots)
	{
		return;
	}
	FVector ViewLocation;
	FRotator ViewRotation;
	Controller->GetPlayerViewPoint(ViewLocation, ViewRotation);
	const FVector Forward = ViewRotation.Vector();
	const float MinDot = FMath::Cos(FMath::DegreesToRadians(Tuning.SightAngle));
	const FVector HeroLocation = Pawn->GetActorLocation();
	const double Now = GetNow();

	for (const ANoFearAIController* Bot : Bots->GetBots())
	{
		APawn* Monster = Bot ? Bot->GetPawn() : nullptr;
		if (!Monster || !UNoFearFearSubsystem::IsFrightening(UNoFearTeamLibrary::GetTeam(Monster)) || UNoFearHealthComponent::IsActorDead(Monster))
		{
			continue;
		}
		const FVector MonsterLocation = Monster->GetActorLocation();
		const float Distance = FVector::Dist(HeroLocation, MonsterLocation);
		if (Distance > Tuning.SightRange)
		{
			continue;
		}
		const bool bInView = FVector::DotProduct(Forward, (MonsterLocation - ViewLocation).GetSafeNormal()) >= MinDot;
		if (!bInView && Distance > Tuning.CloseSenseDistance)
		{
			continue;
		}
		if (!CanSee(Pawn, Monster, ViewLocation, MonsterLocation))
		{
			continue;
		}

		++VisibleMonsters;
		if (NearestMonsterDistance < 0.f || Distance < NearestMonsterDistance)
		{
			NearestMonsterDistance = Distance;
		}
		double& LastSeen = SeenMonsters.FindOrAdd(Monster, -1.0e9);
		if (Now - LastSeen > Tuning.MonsterForgetTime)
		{
			const float Alpha = NoFearBracelet::Ramp(Distance, Tuning.MonsterNearDistance, Tuning.MonsterFarDistance);
			AddHeroFear(NoFearBracelet::Monster, FMath::Lerp(Tuning.MonsterShockNear, Tuning.MonsterShockFar, Alpha),
				FMath::Lerp(Tuning.MonsterCapNear, Tuning.MonsterCapFar, Alpha), MonsterLocation);
		}
		LastSeen = Now;
	}

	// Забытых и исчезнувших не храним.
	for (auto It = SeenMonsters.CreateIterator(); It; ++It)
	{
		if (!It.Key().IsValid() || Now - It.Value() > Tuning.MonsterForgetTime * 2.f)
		{
			It.RemoveCurrent();
		}
	}
}

void UNoFearBraceletComponent::UpdateCorpses(APawn* Pawn)
{
	const APlayerController* Controller = GetPlayerController();
	const UNoFearBotSubsystem* Bots = UNoFearBotSubsystem::Get(this);
	if (!Controller || !Bots || Tuning.CorpseFear <= 0.f)
	{
		return;
	}
	FVector ViewLocation;
	FRotator ViewRotation;
	Controller->GetPlayerViewPoint(ViewLocation, ViewRotation);
	const FVector Forward = ViewRotation.Vector();
	const float MinDot = FMath::Cos(FMath::DegreesToRadians(Tuning.SightAngle));

	for (ANoFearCharacter* Corpse : Bots->GetCorpses())
	{
		if (!Corpse || SeenCorpses.Contains(Corpse))
		{
			continue;
		}
		const ENoFearTeam Team = UNoFearTeamLibrary::GetTeam(Corpse);
		if (Team != ENoFearTeam::Civilian && Team != ENoFearTeam::Ally)
		{
			continue;
		}
		const FVector Point = Corpse->GetActorLocation() + FVector(0.f, 0.f, 30.f);
		const FVector ToCorpse = Point - ViewLocation;
		if (FVector::Dist(Pawn->GetActorLocation(), Corpse->GetActorLocation()) > Tuning.CorpseDistance
			|| FVector::DotProduct(Forward, ToCorpse.GetSafeNormal()) < MinDot
			|| !CanSee(Pawn, Corpse, ViewLocation, Point))
		{
			continue;
		}
		SeenCorpses.Add(Corpse);
		AddHeroFear(NoFearBracelet::Corpse, Tuning.CorpseFear, Tuning.CorpseCap, Corpse->GetActorLocation());
	}
	for (auto It = SeenCorpses.CreateIterator(); It; ++It)
	{
		if (!It->IsValid())
		{
			It.RemoveCurrent();
		}
	}
}

void UNoFearBraceletComponent::UpdateHealth(APawn* Pawn, float DeltaTime)
{
	const UNoFearHealthComponent* Health = UNoFearHealthComponent::FindHealth(Pawn);
	if (!Health || Tuning.LowHealthFraction <= 0.f)
	{
		return;
	}
	const float Fraction = Health->GetHealthFraction();
	if (Fraction >= Tuning.LowHealthFraction)
	{
		return;
	}
	const float Severity = 1.f - Fraction / Tuning.LowHealthFraction;
	// Порог ранения уже пугает заметно (половина скорости), почти пустое здоровье — во всю силу.
	AddHeroFear(NoFearBracelet::Health, Tuning.LowHealthRate * FMath::Lerp(0.5f, 1.f, Severity) * DeltaTime,
		FMath::Lerp(Tuning.LowHealthCapMin, Tuning.LowHealthCapMax, Severity), Pawn->GetActorLocation());
}

void UNoFearBraceletComponent::HandleHeroDamaged(UNoFearHealthComponent* Component, const FNoFearDamageInfo& Info)
{
	if (Info.Amount <= 0.f)
	{
		return;
	}
	const APawn* Pawn = BoundPawn.Get();
	const AActor* Source = Info.InstigatorController ? Info.InstigatorController->GetPawn() : nullptr;
	if (!Source)
	{
		Source = Info.DamageCauser;
	}
	const FVector Location = Info.bHasSource ? Info.SourceLocation : (Pawn ? Pawn->GetActorLocation() : FVector::ZeroVector);
	const float Amount = FMath::Max(Tuning.DamageFearMin, Info.Amount * Tuning.DamageFearPerPoint);
	// От людей — не выше потолка перестрелки (AddFearFrom), от монстров и без виновника — до DamageCap.
	AddHeroFear(NoFearBracelet::Damage, Amount, Tuning.DamageCap, Location, Source);
}

void UNoFearBraceletComponent::UpdateCompanion(APawn* Pawn, float DeltaTime)
{
	const ANoFearPlayerController* Controller = Cast<ANoFearPlayerController>(GetOwner());
	const ANoFearCompanionAIController* Companion = Controller ? Controller->GetMainCompanion() : nullptr;
	const APawn* CompanionPawn = Companion ? Companion->GetPawn() : nullptr;
	const UNoFearHealthComponent* Health = UNoFearHealthComponent::FindHealth(CompanionPawn);
	const bool bDowned = Health && Health->IsDowned();
	if (bDowned)
	{
		const FVector Location = CompanionPawn->GetActorLocation();
		if (!bCompanionWasDowned)
		{
			AddHeroFear(NoFearBracelet::Companion, Tuning.CompanionDownedShock, Tuning.CompanionDownedCap, Location);
		}
		AddHeroFear(NoFearBracelet::Companion, Tuning.CompanionDownedRate * DeltaTime, Tuning.CompanionDownedCap, Location);
	}
	bCompanionWasDowned = bDowned;
}

void UNoFearBraceletComponent::ReceiveDanger(ENoFearDangerType Type, AActor* Source, FVector ThreatLocation)
{
	APawn* Pawn = GetHeroPawn();
	if (!IsHeroActive(Pawn))
	{
		return;
	}
	if (Type == ENoFearDangerType::Scream)
	{
		// Паника людей: от людей — не выше потолка перестрелки.
		AddHeroFear(NoFearBracelet::Combat, Tuning.ScreamFear, 100.f, ThreatLocation, Source ? Source : Pawn);
		return;
	}
	// Свои выстрелы и выстрелы союзников не пугают.
	if (Source && (Source == Pawn || Source->GetOwner() == Pawn || UNoFearTeamLibrary::AreFriendly(Pawn, Source)))
	{
		return;
	}
	const bool bMonster = Source && UNoFearFearSubsystem::IsFrightening(UNoFearTeamLibrary::GetTeam(Source));
	switch (Type)
	{
	case ENoFearDangerType::Gunshot:
		if (FVector::Dist(Pawn->GetActorLocation(), ThreatLocation) <= Tuning.GunshotRadius)
		{
			AddHeroFear(NoFearBracelet::Combat, Tuning.GunshotFear, 100.f, ThreatLocation, Source ? Source : Pawn);
		}
		break;
	case ENoFearDangerType::NearMiss:
		AddHeroFear(NoFearBracelet::Combat, Tuning.NearMissFear, 100.f, ThreatLocation, Source ? Source : Pawn);
		break;
	case ENoFearDangerType::Explosion:
		AddHeroFear(NoFearBracelet::Combat, Tuning.ExplosionFear, 100.f, ThreatLocation, Source ? Source : Pawn);
		break;
	case ENoFearDangerType::Grenade:
		AddHeroFear(NoFearBracelet::Combat, Tuning.ExplosionFear * 0.5f, 100.f, ThreatLocation, Source ? Source : Pawn);
		break;
	case ENoFearDangerType::Fight:
		if (bMonster)
		{
			AddHeroFear(NoFearBracelet::Combat, Tuning.MonsterFightFear, Tuning.MonsterFightCap, ThreatLocation);
		}
		else
		{
			AddHeroFear(NoFearBracelet::Combat, Tuning.GunshotFear, 100.f, ThreatLocation, Source ? Source : Pawn);
		}
		break;
	default:
		break;
	}
}

void UNoFearBraceletComponent::NotifyCalmedCivilian()
{
	Relieve(NoFearBracelet::Relief, Tuning.CalmRelief);
}

void UNoFearBraceletComponent::NotifyMonsterKilled()
{
	Relieve(NoFearBracelet::Relief, Tuning.MonsterKillRelief);
}

float UNoFearBraceletComponent::GetSourceTotal(FName Source) const
{
	const float* Total = SourceTotals.Find(Source);
	return Total ? *Total : 0.f;
}

// ---------------------------------------------------------------------------
// Способности
// ---------------------------------------------------------------------------

bool UNoFearBraceletComponent::StartStabilize()
{
	const double Now = GetNow();
	if (Now < StabilizeReadyTime || !IsHeroActive(GetHeroPawn()) || !GetHeroFear())
	{
		return false;
	}
	StabilizeRemaining = Tuning.StabilizeTime;
	StabilizeReadyTime = Now + Tuning.StabilizeCooldown;
	return true;
}

float UNoFearBraceletComponent::GetStabilizeCooldownRemaining() const
{
	return FMath::Max(0.f, static_cast<float>(StabilizeReadyTime - GetNow()));
}

float UNoFearBraceletComponent::GetStabilizeReadyFraction() const
{
	return Tuning.StabilizeCooldown > 0.f ? 1.f - FMath::Clamp(GetStabilizeCooldownRemaining() / Tuning.StabilizeCooldown, 0.f, 1.f) : 1.f;
}

void UNoFearBraceletComponent::UpdateStabilize(float DeltaTime)
{
	if (StabilizeRemaining <= 0.f)
	{
		return;
	}
	const float Step = FMath::Min(DeltaTime, StabilizeRemaining);
	StabilizeRemaining -= Step;
	Relieve(NoFearBracelet::Stabilize, Tuning.StabilizeAmount * Step / FMath::Max(Tuning.StabilizeTime, 0.05f));
}

void UNoFearBraceletComponent::SetBreathHold(bool bHold)
{
	bBreathWanted = bHold;
}

bool UNoFearBraceletComponent::IsBreathLocked() const
{
	return GetNow() < BreathLockedUntil;
}

void UNoFearBraceletComponent::UpdateBreath(APawn* Pawn, float DeltaTime)
{
	const UNoFearWeaponComponent* Weapons = Pawn ? Pawn->FindComponentByClass<UNoFearWeaponComponent>() : nullptr;
	const bool bAiming = Weapons && Weapons->IsAiming();
	const bool bLocked = IsBreathLocked();
	bool bHold = bBreathWanted && bAiming && !bLocked && !bInStupor && Breath > 0.f && IsHeroActive(Pawn) && GetHeroFear();

	if (bHold)
	{
		Breath -= DeltaTime / Tuning.BreathMaxTime;
		if (Breath <= 0.f)
		{
			// Выдохнул до дна: какое-то время не задержать.
			Breath = 0.f;
			BreathLockedUntil = GetNow() + Tuning.BreathCooldown;
			bHold = false;
		}
	}
	else if (!bLocked)
	{
		Breath = FMath::Min(1.f, Breath + DeltaTime / Tuning.BreathRecoverTime);
	}

	if (bHold != bHoldingBreath)
	{
		bHoldingBreath = bHold;
		ApplyBreathModifiers(bHold);
	}
}

void UNoFearBraceletComponent::ApplyBreathModifiers(bool bApply)
{
	if (bApply)
	{
		UNoFearFearComponent* Fear = GetHeroFear();
		if (!Fear || BreathFear.IsValid())
		{
			return;
		}
		BreathFear = Fear;
		SavedGainMultiplier = Fear->GainMultiplier;
		Fear->GainMultiplier = SavedGainMultiplier * Tuning.BreathFearGainMultiplier;
		Fear->DecayMultiplier = 0.f;
		return;
	}
	if (UNoFearFearComponent* Fear = BreathFear.Get())
	{
		Fear->GainMultiplier = SavedGainMultiplier;
		Fear->DecayMultiplier = Tuning.DecayMultiplier;
	}
	BreathFear.Reset();
}

// ---------------------------------------------------------------------------
// Ступор
// ---------------------------------------------------------------------------

float UNoFearBraceletComponent::GetStuporCooldownRemaining() const
{
	return FMath::Max(0.f, static_cast<float>(StuporReadyTime - GetNow()));
}

void UNoFearBraceletComponent::UpdateStupor(APawn* Pawn)
{
	const double Now = GetNow();
	if (bInStupor)
	{
		if (Now >= StuporEndTime || !IsHeroActive(Pawn))
		{
			EndStupor();
		}
		return;
	}
	if (!IsHeroActive(Pawn) || Now < StuporReadyTime || GetFear() < Tuning.StuporTriggerFear || Tuning.StuporDuration <= 0.f)
	{
		return;
	}

	bInStupor = true;
	StuporEndTime = Now + Tuning.StuporDuration;
	++StuporCount;
	if (APlayerController* Controller = GetPlayerController())
	{
		Controller->SetIgnoreMoveInput(true);
		bStuporBlocksMove = true;
	}
	if (UNoFearWeaponComponent* Weapons = Pawn->FindComponentByClass<UNoFearWeaponComponent>())
	{
		Weapons->SetFireBlocked(true);
		StuporWeapons = Weapons;
	}
}

void UNoFearBraceletComponent::EndStupor()
{
	bInStupor = false;
	StuporReadyTime = GetNow() + Tuning.StuporCooldown;
	if (bStuporBlocksMove)
	{
		if (APlayerController* Controller = GetPlayerController())
		{
			Controller->SetIgnoreMoveInput(false);
		}
		bStuporBlocksMove = false;
	}
	if (UNoFearWeaponComponent* Weapons = StuporWeapons.Get())
	{
		Weapons->SetFireBlocked(false);
	}
	StuporWeapons.Reset();
	// Отпустило: страх падает, но не до спокойствия.
	UNoFearFearComponent* Fear = GetHeroFear();
	if (Fear && Fear->GetFear() > Tuning.StuporFearAfter)
	{
		Fear->SetFear(Tuning.StuporFearAfter);
	}
}

// ---------------------------------------------------------------------------
// Эффекты
// ---------------------------------------------------------------------------

float UNoFearBraceletComponent::GetSpreadMultiplier() const
{
	const float Fear = GetFear();
	const float Panic = GetThreshold(1);
	const float Terror = GetThreshold(2);
	float Multiplier = 1.f;
	if (Fear >= Terror)
	{
		Multiplier = FMath::Lerp(Tuning.SpreadMultiplierTerror, Tuning.SpreadMultiplierMax, NoFearBracelet::Ramp(Fear, Terror, 100.f));
	}
	else if (Fear > Panic)
	{
		Multiplier = FMath::Lerp(1.f, Tuning.SpreadMultiplierTerror, NoFearBracelet::Ramp(Fear, Panic, Terror));
	}
	if (bHoldingBreath)
	{
		Multiplier = 1.f + (Multiplier - 1.f) * Tuning.BreathSpreadBonusScale;
	}
	return Multiplier;
}

float UNoFearBraceletComponent::GetReloadSpeed() const
{
	const float Fear = GetFear();
	return FMath::Lerp(1.f, Tuning.TerrorReloadSpeed, NoFearBracelet::Ramp(Fear, GetThreshold(1), GetThreshold(2)));
}

float UNoFearBraceletComponent::GetHeartRate() const
{
	if (bInStupor)
	{
		return Tuning.HeartRateMax;
	}
	return FMath::Lerp(Tuning.HeartRateCalm, Tuning.HeartRateMax, FMath::Clamp(EffectFear / 100.f, 0.f, 1.f));
}

void UNoFearBraceletComponent::UpdateAimShake(float DeltaTime)
{
	APlayerController* Controller = GetPlayerController();
	APawn* Pawn = GetHeroPawn();
	ShakeTime += DeltaTime;
	ShakeFade = FMath::FInterpTo(ShakeFade, bHoldingBreath ? 0.f : 1.f, DeltaTime, 6.f);

	float Tremor = Tuning.TremorMaxDegrees * NoFearBracelet::Ramp(EffectFear, Tuning.TremorStartFear, 100.f);
	float Drift = Tuning.DriftMaxDegrees * NoFearBracelet::Ramp(EffectFear, Tuning.DriftStartFear, 100.f);
	if (bInStupor)
	{
		Tremor = FMath::Max(Tremor, Tuning.TremorMaxDegrees) * 2.5f;
	}
	Tremor *= ShakeFade;
	Drift *= ShakeFade;
	if (!Controller || !IsHeroActive(Pawn) || Controller->IsPaused())
	{
		Tremor = Drift = 0.f;
	}
	CurrentShakeDegrees = Tremor + Drift;

	const float T = ShakeTime;
	const float TF = Tuning.TremorFrequency;
	const float DF = Tuning.DriftFrequency;
	const FRotator Target(
		Tremor * FMath::PerlinNoise1D(T * TF) + Drift * FMath::PerlinNoise1D(T * DF + 17.3f),
		Tremor * FMath::PerlinNoise1D(T * TF + 31.7f) + Drift * FMath::PerlinNoise1D(T * DF + 53.1f),
		0.f);
	if (Controller && Pawn)
	{
		// Только приращение: свой поворот мышью игрок не теряет.
		const FRotator Delta = Target - AppliedShake;
		if (!Delta.IsNearlyZero(1.e-4f))
		{
			Controller->SetControlRotation(Controller->GetControlRotation() + Delta);
		}
	}
	AppliedShake = Target;
}

void UNoFearBraceletComponent::UpdateHeartbeat(float DeltaTime)
{
	const float BeatsPerSecond = GetHeartRate() / 60.f;
	HeartPulse = FMath::Max(0.f, HeartPulse - DeltaTime * BeatsPerSecond * 2.5f);
	HeartPhase += DeltaTime * BeatsPerSecond;
	if (HeartPhase < 1.f)
	{
		return;
	}
	HeartPhase = FMath::Fmod(HeartPhase, 1.f);
	HeartPulse = 1.f;
	if (!IsHeroActive(GetHeroPawn()) || (EffectFear < Tuning.HeartbeatStartFear && !bInStupor))
	{
		return;
	}
	++HeartbeatCount;
	const float Loudness = bInStupor ? 1.f : FMath::Max(0.25f, NoFearBracelet::Ramp(EffectFear, Tuning.HeartbeatStartFear, 100.f));
	PlayHeartbeat(Tuning.HeartbeatVolume * Loudness);
}

void UNoFearBraceletComponent::UpdateScreenEffects()
{
	const float Panic = GetThreshold(1);
	float Strength = NoFearBracelet::Ramp(EffectFear, Panic, 100.f);
	if (bInStupor)
	{
		Strength += 0.6f;
	}
	if (!IsHeroActive(GetHeroPawn()))
	{
		Strength = 0.f;
	}
	ScreenStrength = Strength * Tuning.ScreenEffectScale;

	if (!PostProcess)
	{
		return;
	}
	const bool bEnabled = ScreenStrength > 0.01f;
	PostProcess->bEnabled = bEnabled;
	if (!bEnabled)
	{
		return;
	}
	const float S = ScreenStrength;
	const float Pulse = HeartPulse;
	FPostProcessSettings& Settings = PostProcess->Settings;
	Settings.bOverride_VignetteIntensity = true;
	Settings.VignetteIntensity = 0.4f + 0.8f * S + 0.25f * S * Pulse;
	Settings.bOverride_SceneFringeIntensity = true;
	Settings.SceneFringeIntensity = 2.5f * S + (bInStupor ? 2.5f : 0.f);
	Settings.bOverride_ColorSaturation = true;
	const float Saturation = 1.f - 0.4f * FMath::Clamp(NoFearBracelet::Ramp(EffectFear, GetThreshold(2), 100.f) + (bInStupor ? 0.5f : 0.f), 0.f, 1.f) * Tuning.ScreenEffectScale;
	Settings.ColorSaturation = FVector4(Saturation, Saturation, Saturation, 1.f);
	Settings.bOverride_FilmGrainIntensity = true;
	Settings.FilmGrainIntensity = bInStupor ? 0.7f : 0.15f * S;
	Settings.bOverride_SceneColorTint = true;
	const float Tint = FMath::Clamp(0.12f * S * Pulse, 0.f, 0.3f);
	Settings.SceneColorTint = FLinearColor(1.f, 1.f - Tint, 1.f - Tint, 1.f);
}

float UNoFearBraceletComponent::GetVignetteIntensity() const
{
	return PostProcess && PostProcess->bEnabled ? PostProcess->Settings.VignetteIntensity : 0.f;
}

// ---------------------------------------------------------------------------
// Разлом (заготовка этапа 13)
// ---------------------------------------------------------------------------

void UNoFearBraceletComponent::ReportRift(FVector Location)
{
	RiftLocation = Location;
	RiftTime = GetNow();
	RiftBeepsPlayed = 0;
}

bool UNoFearBraceletComponent::HasRiftWarning() const
{
	return GetNow() - RiftTime < Tuning.RiftWarningTime;
}

float UNoFearBraceletComponent::GetRiftWarningAlpha() const
{
	return Tuning.RiftWarningTime > 0.f ? FMath::Clamp(1.f - static_cast<float>(GetNow() - RiftTime) / Tuning.RiftWarningTime, 0.f, 1.f) : 0.f;
}

void UNoFearBraceletComponent::UpdateRiftBeep()
{
	if (!HasRiftWarning() || RiftBeepsPlayed >= 3)
	{
		return;
	}
	if (GetNow() - RiftTime >= 0.35 * RiftBeepsPlayed)
	{
		++RiftBeepsPlayed;
		PlayBeep();
	}
}

// ---------------------------------------------------------------------------
// Звук
// ---------------------------------------------------------------------------

void UNoFearBraceletComponent::QueueSynth(TObjectPtr<UAudioComponent>& Audio, TObjectPtr<USoundWaveProcedural>& Wave, const TArray<int16>& Samples)
{
	if (!Wave)
	{
		Wave = NewObject<USoundWaveProcedural>(this);
		Wave->SetSampleRate(NoFearBracelet::SampleRate);
		Wave->NumChannels = 1;
		Wave->Duration = INDEFINITELY_LOOPING_DURATION;
		Wave->SoundGroup = SOUNDGROUP_Default;
		Wave->bLooping = false;
	}
	if (!Audio)
	{
		Audio = UGameplayStatics::CreateSound2D(this, Wave, 1.f, 1.f, 0.f, nullptr, false, false);
		if (!Audio)
		{
			return; // Нет звукового устройства (сервер, -nosound).
		}
	}
	if (!Audio->IsPlaying())
	{
		Wave->ResetAudio();
		Audio->Play();
	}
	Wave->QueueAudio(reinterpret_cast<const uint8*>(Samples.GetData()), Samples.Num() * sizeof(int16));
}

void UNoFearBraceletComponent::PlayHeartbeat(float Volume)
{
	if (Volume <= 0.f)
	{
		return;
	}
	if (HeartbeatSound)
	{
		UGameplayStatics::PlaySound2D(this, HeartbeatSound, Volume);
		return;
	}
	// «Тук-тук»: два низких глухих удара.
	TArray<float> Buffer;
	Buffer.SetNumZeroed(FMath::RoundToInt(0.32f * NoFearBracelet::SampleRate));
	NoFearBracelet::AddTone(Buffer, 0.f, 0.14f, 52.f, 0.9f, 28.f);
	NoFearBracelet::AddTone(Buffer, 0.f, 0.06f, 104.f, 0.25f, 60.f);
	NoFearBracelet::AddTone(Buffer, 0.15f, 0.14f, 60.f, 0.65f, 32.f);
	NoFearBracelet::AddTone(Buffer, 0.15f, 0.05f, 120.f, 0.18f, 70.f);
	QueueSynth(HeartAudio, HeartWave, NoFearBracelet::ToPcm(Buffer, Volume));
}

void UNoFearBraceletComponent::PlayBeep()
{
	if (BeepSound)
	{
		UGameplayStatics::PlaySound2D(this, BeepSound, 0.6f);
		return;
	}
	TArray<float> Buffer;
	Buffer.SetNumZeroed(FMath::RoundToInt(0.12f * NoFearBracelet::SampleRate));
	NoFearBracelet::AddTone(Buffer, 0.f, 0.12f, 1200.f, 0.35f, 18.f);
	QueueSynth(BeepAudio, BeepWave, NoFearBracelet::ToPcm(Buffer, 1.f));
}

// ---------------------------------------------------------------------------
// Отладка
// ---------------------------------------------------------------------------

FString UNoFearBraceletComponent::GetDebugText() const
{
	return FString::Printf(
		TEXT("HERO fear %.1f (fx %.1f) | monsters %d near %.0f | spread x%.2f reload x%.2f shake %.2f° | HR %.0f beats %d | stupor %s n=%d cd %.1f | stab cd %.1f | breath %.2f%s%s | src M%.0f H%.0f D%.0f C%.0f F%.0f B%.0f R%.0f S%.0f"),
		GetFear(), EffectFear, VisibleMonsters, NearestMonsterDistance, GetSpreadMultiplier(), GetReloadSpeed(), CurrentShakeDegrees,
		GetHeartRate(), HeartbeatCount, bInStupor ? TEXT("ON") : TEXT("off"), StuporCount, GetStuporCooldownRemaining(),
		GetStabilizeCooldownRemaining(), Breath, bHoldingBreath ? TEXT(" HOLD") : TEXT(""), IsBreathLocked() ? TEXT(" LOCK") : TEXT(""),
		GetSourceTotal(NoFearBracelet::Monster), GetSourceTotal(NoFearBracelet::Health), GetSourceTotal(NoFearBracelet::Damage),
		GetSourceTotal(NoFearBracelet::Companion), GetSourceTotal(NoFearBracelet::Combat), GetSourceTotal(NoFearBracelet::Corpse),
		GetSourceTotal(NoFearBracelet::Relief), GetSourceTotal(NoFearBracelet::Stabilize));
}
