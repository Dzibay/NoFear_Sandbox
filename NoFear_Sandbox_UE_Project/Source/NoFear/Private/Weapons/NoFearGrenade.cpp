#include "Weapons/NoFearGrenade.h"

#include "AI/NoFearAIController.h"
#include "AI/NoFearBotSubsystem.h"
#include "AI/NoFearCombatComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Core/NoFearCharacter.h"
#include "Core/NoFearHealthComponent.h"
#include "Core/NoFearTeam.h"
#include "Engine/DamageEvents.h"
#include "Engine/OverlapResult.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/DamageType.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"
#include "Particles/ParticleSystem.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"
#include "Weapons/NoFearGrenadeData.h"

namespace
{
	/** Сфера движка: радиус 50. */
	constexpr float SphereMeshRadius = 50.f;
	constexpr float GrenadeRadius = 6.f;
	/** Центр взрыва чуть над землёй: лежащая граната не прячется за неровностью пола. */
	constexpr float BlastHeight = 15.f;
	/** Полный шар вспышки (доля радиуса поражения). */
	constexpr float FlashScaleOfRadius = 0.55f;
}

float UNoFearGrenadeData::GetDamageAtDistance(float Distance) const
{
	if (Distance > Radius)
	{
		return 0.f;
	}
	if (Distance <= InnerRadius || Radius <= InnerRadius)
	{
		return MaxDamage;
	}
	const float Alpha = (Distance - InnerRadius) / (Radius - InnerRadius);
	return FMath::Lerp(MaxDamage, MinDamage, Alpha);
}

ANoFearGrenade::ANoFearGrenade()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;
	InitialLifeSpan = 0.f;

	Collision = CreateDefaultSubobject<USphereComponent>(TEXT("Collision"));
	Collision->InitSphereRadius(GrenadeRadius);
	Collision->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Collision->SetCollisionObjectType(ECC_WorldDynamic);
	Collision->SetCollisionResponseToAllChannels(ECR_Ignore);
	Collision->SetCollisionResponseToChannel(ECC_WorldStatic, ECR_Block);
	Collision->SetCollisionResponseToChannel(ECC_WorldDynamic, ECR_Block);
	Collision->SetGenerateOverlapEvents(false);
	Collision->bReturnMaterialOnMove = false;
	RootComponent = Collision;

	static ConstructorHelpers::FObjectFinderOptional<UStaticMesh> Sphere(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	static ConstructorHelpers::FObjectFinderOptional<UMaterialInterface> ShapeMaterial(TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));

	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	Mesh->SetupAttachment(Collision);
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Mesh->SetRelativeScale3D(FVector(GrenadeRadius * 1.3f / SphereMeshRadius, GrenadeRadius * 1.3f / SphereMeshRadius, GrenadeRadius * 1.7f / SphereMeshRadius));
	Mesh->SetStaticMesh(Sphere.Get());
	Mesh->SetMaterial(0, ShapeMaterial.Get());

	FlashMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("FlashMesh"));
	FlashMesh->SetupAttachment(Collision);
	FlashMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	FlashMesh->SetCastShadow(false);
	FlashMesh->SetStaticMesh(Sphere.Get());
	FlashMesh->SetMaterial(0, ShapeMaterial.Get());
	FlashMesh->SetVisibility(false);

	FlashLight = CreateDefaultSubobject<UPointLightComponent>(TEXT("FlashLight"));
	FlashLight->SetupAttachment(Collision);
	FlashLight->SetVisibility(false);
	FlashLight->SetIntensity(0.f);
	FlashLight->SetLightColor(FLinearColor(1.f, 0.6f, 0.25f));
	FlashLight->SetCastShadows(false);

	ProjectileMovement = CreateDefaultSubobject<UProjectileMovementComponent>(TEXT("ProjectileMovement"));
	ProjectileMovement->UpdatedComponent = Collision;
	ProjectileMovement->InitialSpeed = 0.f;
	ProjectileMovement->MaxSpeed = 4000.f;
	ProjectileMovement->ProjectileGravityScale = 1.f;
	ProjectileMovement->bRotationFollowsVelocity = false;
	ProjectileMovement->bShouldBounce = true;
	ProjectileMovement->Bounciness = 0.35f;
	ProjectileMovement->Friction = 0.4f;
	ProjectileMovement->BounceVelocityStopSimulatingThreshold = 25.f;
	ProjectileMovement->bAutoActivate = false;
}

void ANoFearGrenade::BeginPlay()
{
	Super::BeginPlay();

	if (APawn* Thrower = GetInstigator())
	{
		Collision->IgnoreActorWhenMoving(Thrower, true);
	}
	ProjectileMovement->OnProjectileBounce.AddDynamic(this, &ANoFearGrenade::HandleBounce);

	if (Data)
	{
		ProjectileMovement->Bounciness = Data->Bounciness;
		ProjectileMovement->Friction = Data->Friction;
		if (UMaterialInstanceDynamic* Material = Mesh->CreateAndSetMaterialInstanceDynamic(0))
		{
			Material->SetVectorParameterValue(TEXT("Color"), Data->Color);
		}
	}
	if (UMaterialInstanceDynamic* Material = FlashMesh->CreateAndSetMaterialInstanceDynamic(0))
	{
		Material->SetVectorParameterValue(TEXT("Color"), FLinearColor(1.f, 0.55f, 0.15f));
	}

	if (UNoFearBotSubsystem* Subsystem = UNoFearBotSubsystem::Get(this))
	{
		Subsystem->RegisterGrenade(this);
	}
}

void ANoFearGrenade::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	GetWorldTimerManager().ClearTimer(FuseTimer);
	GetWorldTimerManager().ClearTimer(DangerTimer);
	if (UNoFearBotSubsystem* Subsystem = UNoFearBotSubsystem::Get(this))
	{
		Subsystem->UnregisterGrenade(this);
	}
	Super::EndPlay(EndPlayReason);
}

void ANoFearGrenade::Launch(FVector Velocity)
{
	if (bExploded || LaunchTime >= 0.0)
	{
		return;
	}
	LaunchTime = GetWorld()->GetTimeSeconds();
	ProjectileMovement->Velocity = Velocity;
	ProjectileMovement->Activate(true);

	const float Fuse = Data ? Data->FuseTime : 3.f;
	GetWorldTimerManager().SetTimer(FuseTimer, this, &ANoFearGrenade::Explode, Fuse, false);
}

float ANoFearGrenade::GetTimeToExplode() const
{
	if (bExploded || LaunchTime < 0.0)
	{
		return 0.f;
	}
	const float Fuse = Data ? Data->FuseTime : 3.f;
	return FMath::Max(0.f, Fuse - static_cast<float>(GetWorld()->GetTimeSeconds() - LaunchTime));
}

float ANoFearGrenade::GetRadius() const
{
	return Data ? Data->Radius : 500.f;
}

void ANoFearGrenade::HandleBounce(const FHitResult& ImpactResult, const FVector& ImpactVelocity)
{
	if (bExploded)
	{
		return;
	}

	const double Now = GetWorld()->GetTimeSeconds();
	if (Data && Data->BounceSound && ImpactVelocity.Size() > 150.f && Now - LastBounceSoundTime > 0.15)
	{
		LastBounceSoundTime = Now;
		UGameplayStatics::PlaySoundAtLocation(this, Data->BounceSound, ImpactResult.ImpactPoint, FMath::Clamp(ImpactVelocity.Size() / 800.f, 0.2f, 1.f));
	}

	if (!bLanded)
	{
		// Упала: кто рядом видит или слышит стук, бежит. Дальше напоминаем — подошедшие тоже заметят.
		bLanded = true;
		NotifyDanger();
		const float Interval = FMath::Max(0.1f, DangerNotifyInterval);
		GetWorldTimerManager().SetTimer(DangerTimer, this, &ANoFearGrenade::NotifyDanger, Interval, true);
	}
}

bool ANoFearGrenade::HasClearLine(const FVector& From, const FVector& To, const AActor* Target) const
{
	FCollisionQueryParams Params(SCENE_QUERY_STAT(NoFearGrenadeLine), false, this);
	if (Target)
	{
		Params.AddIgnoredActor(Target);
		TArray<AActor*> Attached;
		Target->GetAttachedActors(Attached);
		Params.AddIgnoredActors(Attached);
	}
	FCollisionObjectQueryParams Objects;
	Objects.AddObjectTypesToQuery(ECC_WorldStatic);
	Objects.AddObjectTypesToQuery(ECC_WorldDynamic);
	TArray<FHitResult> Hits;
	GetWorld()->LineTraceMultiByObjectType(Hits, From, To, Objects, Params);
	for (const FHitResult& Hit : Hits)
	{
		const AActor* Actor = Hit.GetActor();
		// Оружие в руках и прочее, прикреплённое к персонажу, — часть персонажа.
		const AActor* Root = Actor;
		while (Root && Root->GetAttachParentActor())
		{
			Root = Root->GetAttachParentActor();
		}
		if (Root && (Root->IsA<APawn>() || Root->IsA<ANoFearGrenade>()))
		{
			continue;
		}
		return false;
	}
	return true;
}

void ANoFearGrenade::NotifyDanger()
{
	UNoFearBotSubsystem* Subsystem = UNoFearBotSubsystem::Get(this);
	if (bExploded || !Subsystem || !Data)
	{
		return;
	}

	const FVector Location = GetActorLocation();
	const float ViewCos = FMath::Cos(FMath::DegreesToRadians(DangerViewAngle));
	const APawn* Thrower = GetInstigator();
	for (ANoFearAIController* Bot : Subsystem->GetBots())
	{
		APawn* BotPawn = Bot->GetPawn();
		if (!BotPawn || UNoFearHealthComponent::IsActorDead(BotPawn))
		{
			continue;
		}
		if (!Data->bFriendlyFire && Thrower && BotPawn != Thrower && UNoFearTeamLibrary::AreFriendly(Thrower, BotPawn))
		{
			// Своя граната своих не ранит (Explode): бежать от неё незачем, и мораль отряда от неё не падает.
			continue;
		}
		const float Distance = FVector::Dist(BotPawn->GetActorLocation(), Location);
		if (Distance > Data->DangerNoticeRadius)
		{
			continue;
		}

		// Слышит стук рядом с собой — где бы ни смотрел; иначе должен увидеть гранату.
		bool bNoticed = Distance <= Data->DangerHearRadius;
		if (!bNoticed)
		{
			FVector EyeLocation;
			FRotator EyeRotation;
			BotPawn->GetActorEyesViewPoint(EyeLocation, EyeRotation);
			const FVector ToGrenade = (Location - EyeLocation).GetSafeNormal();
			bNoticed = FVector::DotProduct(EyeRotation.Vector(), ToGrenade) >= ViewCos && HasClearLine(EyeLocation, Location + FVector(0.f, 0.f, 5.f), BotPawn);
		}
		if (bNoticed)
		{
			Bot->HandleDanger(ENoFearDangerType::Grenade, this, Location);
		}
	}
}

void ANoFearGrenade::Explode()
{
	if (bExploded)
	{
		return;
	}
	bExploded = true;
	ExplodeTime = GetWorld()->GetTimeSeconds();
	GetWorldTimerManager().ClearTimer(FuseTimer);
	GetWorldTimerManager().ClearTimer(DangerTimer);
	ProjectileMovement->StopMovementImmediately();
	ProjectileMovement->Deactivate();

	const FVector Center = GetActorLocation() + FVector(0.f, 0.f, BlastHeight);
	ApplyBlast(Center);

	APawn* Thrower = GetInstigator();
	const float NoiseRange = Data ? Data->NoiseRange : 4000.f;
	if (Thrower)
	{
		// Враги бросившего слышат взрыв и настораживаются (сам бросивший не выдаётся, как при выстреле).
		UNoFearBotSubsystem::ReportNoise(this, Thrower, Center, NoiseRange, ENoFearNoiseType::Other);
	}
	UNoFearBotSubsystem::ReportDanger(this, ENoFearDangerType::Explosion, Thrower, Center, NoiseRange * 0.5f, Center);

	// Временные эффекты: вспышка, шар, эффект из ассета и звук.
	if (Data && Data->ExplosionEffect)
	{
		if (UParticleSystem* Particles = Cast<UParticleSystem>(Data->ExplosionEffect))
		{
			UGameplayStatics::SpawnEmitterAtLocation(this, Particles, Center, FRotator::ZeroRotator, FVector(1.f), true);
		}
		else if (UNiagaraSystem* Niagara = Cast<UNiagaraSystem>(Data->ExplosionEffect))
		{
			UNiagaraFunctionLibrary::SpawnSystemAtLocation(this, Niagara, Center);
		}
	}
	if (Data && Data->ExplosionSound)
	{
		UGameplayStatics::PlaySoundAtLocation(this, Data->ExplosionSound, Center, 1.5f, Data->ExplosionSoundPitch);
	}

	Mesh->SetVisibility(false);
	FlashMesh->SetVisibility(true);
	FlashMesh->SetWorldScale3D(FVector(0.1f));
	FlashLight->SetAttenuationRadius(GetRadius() * 2.f);
	FlashLight->SetIntensity(80000.f);
	FlashLight->SetVisibility(true);
	SetActorTickEnabled(true);
	SetLifeSpan(FlashTime + 0.1f);

	OnExploded.Broadcast(this);
}

void ANoFearGrenade::ApplyBlast(const FVector& Center)
{
	UWorld* World = GetWorld();
	if (!World || !Data)
	{
		return;
	}

	APawn* Thrower = GetInstigator();
	AController* ThrowerController = Thrower ? Thrower->GetController() : nullptr;
	const float QueryRadius = FMath::Max(Data->Radius, Data->SuppressionRadius);

	FCollisionObjectQueryParams Objects;
	Objects.AddObjectTypesToQuery(ECC_Pawn);
	Objects.AddObjectTypesToQuery(ECC_PhysicsBody);
	Objects.AddObjectTypesToQuery(ECC_WorldDynamic);
	FCollisionQueryParams Params(SCENE_QUERY_STAT(NoFearGrenadeBlast), false, this);
	TArray<FOverlapResult> Overlaps;
	World->OverlapMultiByObjectType(Overlaps, Center, FQuat::Identity, Objects, FCollisionShape::MakeSphere(QueryRadius), Params);

	TArray<AActor*> Actors;
	for (const FOverlapResult& Overlap : Overlaps)
	{
		if (AActor* Actor = Overlap.GetActor(); Actor && Actor != this)
		{
			Actors.AddUnique(Actor);
		}
	}

	// Урон: только тем, до кого от центра есть прямая линия (до пояса или до головы).
	for (AActor* Victim : Actors)
	{
		if (!UNoFearHealthComponent::FindHealth(Victim) || UNoFearHealthComponent::IsActorDead(Victim))
		{
			continue;
		}
		const FVector Body = Victim->GetActorLocation();
		const float Distance = FVector::Dist(Body, Center);
		if (Distance > Data->Radius)
		{
			continue;
		}

		const APawn* VictimPawn = Cast<APawn>(Victim);
		const FVector Head = Body + FVector(0.f, 0.f, VictimPawn ? VictimPawn->BaseEyeHeight : 50.f);
		if (!HasClearLine(Center, Body, Victim) && !HasClearLine(Center, Head, Victim))
		{
			++ShieldedCount;
			continue;
		}
		if (!Data->bFriendlyFire && Thrower && Victim != Thrower && UNoFearTeamLibrary::AreFriendly(Thrower, Victim))
		{
			continue;
		}

		const float Damage = Data->GetDamageAtDistance(Distance);
		if (Damage <= 0.f)
		{
			continue;
		}
		// Падение урона с расстоянием уже посчитано: движок не должен уменьшать его ещё раз.
		FRadialDamageEvent DamageEvent;
		DamageEvent.DamageTypeClass = Data->DamageType ? Data->DamageType.Get() : UDamageType::StaticClass();
		DamageEvent.Origin = Center;
		DamageEvent.Params.BaseDamage = Damage;
		DamageEvent.Params.MinimumDamage = Damage;
		DamageEvent.Params.InnerRadius = Data->Radius;
		DamageEvent.Params.OuterRadius = Data->Radius;
		DamageEvent.Params.DamageFalloff = 0.f;
		const float Dealt = Victim->TakeDamage(Damage, DamageEvent, ThrowerController, this);
		if (Dealt > 0.f)
		{
			TotalDamageDealt += Dealt;
			++VictimCount;
		}
	}

	// Подавление ботов, которые видят вспышку.
	for (AActor* Actor : Actors)
	{
		UNoFearCombatComponent* Combat = Actor->FindComponentByClass<UNoFearCombatComponent>();
		if (!Combat || UNoFearHealthComponent::IsActorDead(Actor))
		{
			continue;
		}
		if (Thrower && (Actor == Thrower || (!Data->bFriendlyFire && UNoFearTeamLibrary::AreFriendly(Thrower, Actor))))
		{
			// Как и пули своих (NoFearCombatSubsystem): своя граната не прижимает ни бросившего, ни своих, если их не ранит.
			continue;
		}
		const float Distance = FVector::Dist(Actor->GetActorLocation(), Center);
		if (Distance > Data->SuppressionRadius || !HasClearLine(Center, Actor->GetActorLocation() + FVector(0.f, 0.f, 50.f), Actor))
		{
			continue;
		}
		const float Falloff = FMath::Lerp(1.f, 0.5f, Distance / FMath::Max(1.f, Data->SuppressionRadius));
		Combat->AddSuppression(Data->SuppressionAmount * Falloff, Thrower, Center);
	}

	// Толчок: рэгдоллы (в том числе только что убитые) и физические предметы.
	if (Data->ImpulseStrength > 0.f)
	{
		for (AActor* Actor : Actors)
		{
			TInlineComponentArray<UPrimitiveComponent*> Primitives(Actor);
			for (UPrimitiveComponent* Primitive : Primitives)
			{
				if (Primitive && Primitive->IsSimulatingPhysics() && FVector::Dist(Primitive->GetComponentLocation(), Center) <= Data->Radius + 100.f)
				{
					Primitive->AddRadialImpulse(Center, Data->Radius + 100.f, Data->ImpulseStrength, ERadialImpulseFalloff::RIF_Linear, /*bVelChange=*/true);
				}
			}
		}
	}
}

void ANoFearGrenade::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (!bExploded)
	{
		return;
	}
	const float Alpha = FMath::Clamp(static_cast<float>(GetWorld()->GetTimeSeconds() - ExplodeTime) / FlashTime, 0.f, 1.f);
	const float FullScale = GetRadius() * FlashScaleOfRadius / SphereMeshRadius;
	FlashMesh->SetWorldScale3D(FVector(FMath::Lerp(0.1f, FullScale, FMath::Sqrt(Alpha)) * (1.f - Alpha * 0.3f)));
	FlashLight->SetIntensity(80000.f * FMath::Square(1.f - Alpha));
	if (Alpha >= 1.f)
	{
		FlashMesh->SetVisibility(false);
		FlashLight->SetVisibility(false);
	}
}
