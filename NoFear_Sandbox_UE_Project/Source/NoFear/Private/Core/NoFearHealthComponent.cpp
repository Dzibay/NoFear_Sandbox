#include "Core/NoFearHealthComponent.h"

#include "AI/NoFearCombatSubsystem.h"
#include "AI/NoFearVulnerabilityComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Core/NoFearPlayerController.h"
#include "Core/NoFearTeam.h"
#include "Engine/DamageEvents.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/Controller.h"
#include "GameFramework/Pawn.h"

UNoFearHealthComponent::UNoFearHealthComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
	// Восстановлению хватает пяти тиков в секунду.
	PrimaryComponentTick.TickInterval = 0.2f;

	Health = MaxHealth;

	BoneMultipliers.Add(TEXT("neck_01"), 1.5f);
	BoneMultipliers.Add(TEXT("upperarm_l"), 0.75f);
	BoneMultipliers.Add(TEXT("upperarm_r"), 0.75f);
	BoneMultipliers.Add(TEXT("thigh_l"), 0.75f);
	BoneMultipliers.Add(TEXT("thigh_r"), 0.75f);
}

void UNoFearHealthComponent::BeginPlay()
{
	Super::BeginPlay();

	Health = MaxHealth;
	bDead = false;
	bDowned = false;
}

double UNoFearHealthComponent::GetNow() const
{
	const UWorld* World = GetWorld();
	return World ? World->GetTimeSeconds() : 0.0;
}

float UNoFearHealthComponent::GetTimeSinceLastDamage() const
{
	return static_cast<float>(GetNow() - LastDamageTime);
}

UNoFearHealthComponent* UNoFearHealthComponent::FindHealth(const AActor* Actor)
{
	if (const AController* Controller = Cast<AController>(Actor))
	{
		Actor = Controller->GetPawn();
	}
	return Actor ? Actor->FindComponentByClass<UNoFearHealthComponent>() : nullptr;
}

bool UNoFearHealthComponent::IsActorDead(const AActor* Actor)
{
	const UNoFearHealthComponent* HealthComponent = FindHealth(Actor);
	return HealthComponent && HealthComponent->IsDead();
}

bool UNoFearHealthComponent::IsActorDowned(const AActor* Actor)
{
	const UNoFearHealthComponent* HealthComponent = FindHealth(Actor);
	return HealthComponent && HealthComponent->IsDowned();
}

bool UNoFearHealthComponent::IsActorIncapacitated(const AActor* Actor)
{
	const UNoFearHealthComponent* HealthComponent = FindHealth(Actor);
	return HealthComponent && (HealthComponent->IsDead() || HealthComponent->IsDowned());
}

// ---------------------------------------------------------------------------
// Урон
// ---------------------------------------------------------------------------

float UNoFearHealthComponent::ProcessDamage(float Damage, const FDamageEvent& DamageEvent, AController* EventInstigator, AActor* DamageCauser)
{
	AActor* Owner = GetOwner();
	if (bDead || Damage <= 0.f || !Owner)
	{
		return 0.f;
	}
	if (!bAllowFriendlyFire && IsFriendlyFire(EventInstigator, DamageCauser))
	{
		return 0.f;
	}

	FNoFearDamageInfo Info;
	Info.InstigatorController = EventInstigator;
	Info.DamageCauser = DamageCauser;
	Info.DamageType = DamageEvent.DamageTypeClass;
	Info.HitLocation = Owner->GetActorLocation();

	float Multiplier = 1.f;
	if (DamageEvent.IsOfType(FPointDamageEvent::ClassID))
	{
		const FPointDamageEvent& PointEvent = static_cast<const FPointDamageEvent&>(DamageEvent);
		Info.BoneName = PointEvent.HitInfo.BoneName;
		Info.HitLocation = PointEvent.HitInfo.ImpactPoint;
		Info.Direction = PointEvent.ShotDirection.GetSafeNormal();
		Multiplier = GetBoneMultiplier(Info.BoneName, Info.bHeadshot);

		if (!PointEvent.HitInfo.TraceStart.IsZero())
		{
			Info.SourceLocation = PointEvent.HitInfo.TraceStart;
			Info.bHasSource = true;
		}
	}
	else if (DamageEvent.IsOfType(FRadialDamageEvent::ClassID))
	{
		const FRadialDamageEvent& RadialEvent = static_cast<const FRadialDamageEvent&>(DamageEvent);
		Info.SourceLocation = RadialEvent.Origin;
		Info.bHasSource = true;
		Info.Direction = (Owner->GetActorLocation() - RadialEvent.Origin).GetSafeNormal();
	}

	if (!Info.bHasSource)
	{
		if (const APawn* InstigatorPawn = EventInstigator ? EventInstigator->GetPawn() : nullptr)
		{
			Info.SourceLocation = InstigatorPawn->GetActorLocation();
			Info.bHasSource = true;
		}
		else if (DamageCauser)
		{
			Info.SourceLocation = DamageCauser->GetActorLocation();
			Info.bHasSource = true;
		}
	}

	// Слабые места, броня и окна уязвимости монстра.
	if (UNoFearVulnerabilityComponent* Vulnerability = Owner->FindComponentByClass<UNoFearVulnerabilityComponent>())
	{
		Multiplier = Vulnerability->ModifyDamageMultiplier(Info, DamageEvent.IsOfType(FPointDamageEvent::ClassID),
			DamageEvent.IsOfType(FRadialDamageEvent::ClassID), Damage, Multiplier);
	}

	// Упавшего добивают: здоровья уже нет, урон сокращает время до смерти.
	if (bDowned)
	{
		const bool bExplosion = DamageEvent.IsOfType(FRadialDamageEvent::ClassID);
		DownedUntil -= Damage * Multiplier * (bExplosion ? DownedExplosionTimePerDamage : DownedTimePerDamage);
		LastDamageTime = GetNow();
		Info.Amount = 0.f;
		Info.bDowned = true;
		OnDamaged.Broadcast(this, Info);
		return 0.f;
	}

	const float Applied = FMath::Min(Damage * Multiplier, Health);
	if (Applied <= 0.f)
	{
		return 0.f;
	}

	Health -= Applied;
	LastDamageTime = GetNow();
	Info.Amount = Applied;
	Info.bFatal = Health <= KINDA_SMALL_NUMBER;
	if (Info.bFatal)
	{
		Health = 0.f;
		if (bCanBeDowned)
		{
			Info.bFatal = false;
			Info.bDowned = true;
		}
	}

	OnHealthChanged.Broadcast(this, Health, -Applied);
	OnDamaged.Broadcast(this, Info);

	// Игроку, который стрелял, — подтверждение попадания для маркера на прицеле.
	if (ANoFearPlayerController* Shooter = Cast<ANoFearPlayerController>(EventInstigator))
	{
		Shooter->NotifyHitConfirmed(Owner, Applied, Info.bHeadshot, Info.bFatal);
	}

	if (Info.bFatal)
	{
		Die(Info);
	}
	else if (Info.bDowned)
	{
		GoDown(Info);
	}
	else
	{
		UpdateRegenTick();
	}
	return Applied;
}

float UNoFearHealthComponent::GetBoneMultiplier(FName HitBone, bool& bOutHeadshot) const
{
	bOutHeadshot = false;
	if (HitBone.IsNone())
	{
		return 1.f;
	}

	const AActor* Owner = GetOwner();
	const ACharacter* Character = Cast<ACharacter>(Owner);
	const USkeletalMeshComponent* Mesh = Character ? Character->GetMesh() : (Owner ? Owner->FindComponentByClass<USkeletalMeshComponent>() : nullptr);

	// Идём от кости попадания к корню и берём первую кость, для которой есть правило.
	FName Bone = HitBone;
	for (int32 Depth = 0; !Bone.IsNone() && Depth < 64; ++Depth)
	{
		if (Bone == HeadBone)
		{
			bOutHeadshot = true;
			return HeadMultiplier;
		}
		if (const float* Multiplier = BoneMultipliers.Find(Bone))
		{
			return *Multiplier;
		}
		Bone = Mesh ? Mesh->GetParentBone(Bone) : NAME_None;
	}
	return 1.f;
}

bool UNoFearHealthComponent::IsFriendlyFire(const AController* EventInstigator, const AActor* DamageCauser) const
{
	// Команда берётся у пешки: у AI-контроллера своей команды обычно нет.
	const AActor* Source = EventInstigator && EventInstigator->GetPawn() ? static_cast<const AActor*>(EventInstigator->GetPawn())
		: (DamageCauser ? DamageCauser : static_cast<const AActor*>(EventInstigator));
	const AActor* Owner = GetOwner();
	if (!Source || Source == Owner || Source->GetOwner() == Owner)
	{
		return false;
	}
	return UNoFearTeamLibrary::AreFriendly(Source, Owner);
}

// ---------------------------------------------------------------------------
// Здоровье, лечение, смерть
// ---------------------------------------------------------------------------

float UNoFearHealthComponent::Heal(float Amount)
{
	// Упавшего бинтом не поднять — только Revive.
	if (bDead || bDowned || Amount <= 0.f)
	{
		return 0.f;
	}

	const float Added = FMath::Min(Amount, MaxHealth - Health);
	if (Added > 0.f)
	{
		Health += Added;
		OnHealthChanged.Broadcast(this, Health, Added);
	}
	return Added;
}

void UNoFearHealthComponent::SetHealth(float NewHealth)
{
	if (bDead || bDowned)
	{
		return;
	}
	if (NewHealth <= 0.f)
	{
		Kill();
		return;
	}

	const float Clamped = FMath::Min(NewHealth, MaxHealth);
	const float Delta = Clamped - Health;
	Health = Clamped;
	if (!FMath::IsNearlyZero(Delta))
	{
		OnHealthChanged.Broadcast(this, Health, Delta);
	}
	UpdateRegenTick();
}

void UNoFearHealthComponent::Kill(AController* Killer, AActor* Causer)
{
	if (bDead)
	{
		return;
	}

	FNoFearDamageInfo Info;
	Info.Amount = Health;
	Info.bFatal = true;
	Info.InstigatorController = Killer;
	Info.DamageCauser = Causer;
	if (const AActor* Owner = GetOwner())
	{
		Info.HitLocation = Owner->GetActorLocation();
	}

	const float Old = Health;
	Health = 0.f;
	LastDamageTime = GetNow();
	if (Old > 0.f)
	{
		OnHealthChanged.Broadcast(this, Health, -Old);
	}
	Die(Info);
}

void UNoFearHealthComponent::Die(const FNoFearDamageInfo& Info)
{
	bDead = true;
	bDowned = false;
	SetComponentTickEnabled(false);

	// Сначала реакция владельца (рэгдолл, снятие управления), потом остальной мир.
	OnDeath.Broadcast(this, Info);

	if (AActor* Owner = GetOwner())
	{
		UNoFearCombatSubsystem::NotifyActorDied(Owner);
	}
}

void UNoFearHealthComponent::GoDown(const FNoFearDamageInfo& Info)
{
	bDowned = true;
	DownedUntil = GetNow() + DownedDuration;
	ReviveProgress = 0.f;
	LastReviveProgressTime = -1.0e9;
	DownedInfo = Info;
	SetComponentTickEnabled(true);

	OnDowned.Broadcast(this, Info);
}

float UNoFearHealthComponent::GetDownedTimeRemaining() const
{
	return IsDowned() ? FMath::Max(0.f, static_cast<float>(DownedUntil - GetNow())) : 0.f;
}

float UNoFearHealthComponent::GetDownedFraction() const
{
	return IsDowned() && DownedDuration > 0.f ? FMath::Clamp(GetDownedTimeRemaining() / DownedDuration, 0.f, 1.f) : 0.f;
}

bool UNoFearHealthComponent::Revive(AActor* Reviver)
{
	if (!IsDowned())
	{
		return false;
	}

	bDowned = false;
	ReviveProgress = 0.f;
	LastReviveProgressTime = -1.0e9;
	Health = FMath::Max(1.f, MaxHealth * ReviveHealthFraction);
	// Восстановление начнётся не сразу: поднятый ещё хромает.
	LastDamageTime = GetNow();
	OnHealthChanged.Broadcast(this, Health, Health);
	OnRevived.Broadcast(this, Reviver);
	UpdateRegenTick();
	return true;
}

void UNoFearHealthComponent::NotifyReviveProgress(float Progress)
{
	if (IsDowned())
	{
		LastReviveProgressTime = GetNow();
		ReviveProgress = FMath::Clamp(Progress, 0.f, 1.f);
	}
}

bool UNoFearHealthComponent::IsBeingRevived() const
{
	constexpr double ReviveWindow = 0.5;
	return IsDowned() && GetNow() - LastReviveProgressTime <= ReviveWindow;
}

bool UNoFearHealthComponent::CanRegenerate() const
{
	if (RegenRate <= 0.f || bDead || bDowned || Health >= MaxHealth)
	{
		return false;
	}
	if (!bRegenPlayerOnly)
	{
		return true;
	}
	const APawn* Pawn = Cast<APawn>(GetOwner());
	return Pawn && Pawn->IsPlayerControlled();
}

void UNoFearHealthComponent::UpdateRegenTick()
{
	SetComponentTickEnabled(!bDead && (bDowned || (RegenRate > 0.f && Health < MaxHealth)));
}

void UNoFearHealthComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (bDowned && !bDead)
	{
		// Пока поднимают, кровь не уходит: таймер стоит.
		if (IsBeingRevived())
		{
			DownedUntil += DeltaTime;
		}
		if (GetNow() >= DownedUntil)
		{
			FNoFearDamageInfo Info = DownedInfo;
			Info.bDowned = false;
			Info.bFatal = true;
			Info.Amount = 0.f;
			Die(Info);
		}
		return;
	}

	if (bDead || RegenRate <= 0.f || Health >= MaxHealth)
	{
		SetComponentTickEnabled(false);
		return;
	}

	if (CanRegenerate() && GetTimeSinceLastDamage() >= RegenDelay)
	{
		Heal(RegenRate * DeltaTime);
	}
}
