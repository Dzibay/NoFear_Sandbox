#include "AI/NoFearSpitEffectComponent.h"

#include "AI/NoFearOpportunitySubsystem.h"
#include "Core/NoFearHealthComponent.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"

UNoFearSpitEffectComponent::UNoFearSpitEffectComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
	PrimaryComponentTick.TickInterval = 0.1f;
}

UNoFearSpitEffectComponent* UNoFearSpitEffectComponent::FindSpitEffect(const AActor* Actor)
{
	return Actor ? Actor->FindComponentByClass<UNoFearSpitEffectComponent>() : nullptr;
}

void UNoFearSpitEffectComponent::ApplySlime(AActor* Victim, float Time, float SpeedScale)
{
	if (!Victim || Time <= 0.f || UNoFearHealthComponent::IsActorDead(Victim) || !Victim->GetWorld())
	{
		return;
	}
	UNoFearOpportunitySubsystem::AddTag(Victim, ENoFearOpportunity::Slimed, Time);
	UNoFearSpitEffectComponent* Effect = FindSpitEffect(Victim);
	if (!Effect)
	{
		Effect = NewObject<UNoFearSpitEffectComponent>(Victim, TEXT("NoFearSpitEffect"));
		Effect->RegisterComponent();
	}
	++Effect->SlimeCount;
	Effect->SlimeUntil = FMath::Max(Effect->SlimeUntil, Victim->GetWorld()->GetTimeSeconds() + Time);
	Effect->SlowScale = FMath::Clamp(SpeedScale, 0.1f, 1.f);
	// Слизь липнет: всё тело (ходьба, анимации) идёт медленнее.
	Victim->CustomTimeDilation = Effect->SlowScale;
	Effect->bSlowApplied = true;
	Effect->SetComponentTickEnabled(true);
}

void UNoFearSpitEffectComponent::ApplyBlind(AActor* Victim, float Time)
{
	if (!Victim || Time <= 0.f || UNoFearHealthComponent::IsActorDead(Victim))
	{
		return;
	}
	UNoFearOpportunitySubsystem::AddTag(Victim, ENoFearOpportunity::Blinded, Time);
	UNoFearSpitEffectComponent* Effect = FindSpitEffect(Victim);
	if (!Effect)
	{
		Effect = NewObject<UNoFearSpitEffectComponent>(Victim, TEXT("NoFearSpitEffect"));
		Effect->RegisterComponent();
	}
	++Effect->BlindCount;
}

void UNoFearSpitEffectComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	if (!GetWorld() || GetWorld()->GetTimeSeconds() >= SlimeUntil || UNoFearHealthComponent::IsActorDead(GetOwner()))
	{
		Restore();
		SetComponentTickEnabled(false);
	}
}

void UNoFearSpitEffectComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	Restore();
	Super::EndPlay(EndPlayReason);
}

void UNoFearSpitEffectComponent::Restore()
{
	if (bSlowApplied && GetOwner())
	{
		GetOwner()->CustomTimeDilation = 1.f;
	}
	bSlowApplied = false;
}
