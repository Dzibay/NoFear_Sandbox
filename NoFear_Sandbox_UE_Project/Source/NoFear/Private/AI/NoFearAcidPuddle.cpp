#include "AI/NoFearAcidPuddle.h"

#include "Components/StaticMeshComponent.h"
#include "Core/NoFearHealthComponent.h"
#include "Core/NoFearTeam.h"
#include "Engine/OverlapResult.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/DamageType.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	/** Цилиндр движка: радиус 50, высота 100. */
	constexpr float CylinderRadius = 50.f;
	constexpr float PuddleThickness = 2.f;
	/** Задевает тех, чей центр не выше столько над лужей. */
	constexpr float PuddleReachHeight = 130.f;

	/** Когда кого жгла последняя лужа: лужи не складываются, стоящий в нескольких получает урон как от одной. */
	struct FLastBurn
	{
		TWeakObjectPtr<UWorld> World;
		double Time = 0.0;
	};
	TMap<TWeakObjectPtr<AActor>, FLastBurn> LastBurn;
}

ANoFearAcidPuddle::ANoFearAcidPuddle()
{
	PrimaryActorTick.bCanEverTick = true;
	DamageType = UDamageType::StaticClass();

	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Mesh->SetCastShadow(false);
	static ConstructorHelpers::FObjectFinderOptional<UStaticMesh> Cylinder(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	static ConstructorHelpers::FObjectFinderOptional<UMaterialInterface> ShapeMaterial(TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	Mesh->SetStaticMesh(Cylinder.Get());
	Mesh->SetMaterial(0, ShapeMaterial.Get());
	RootComponent = Mesh;
}

void ANoFearAcidPuddle::BeginPlay()
{
	Super::BeginPlay();

	SpawnTime = GetWorld()->GetTimeSeconds();
	SetLifeSpan(Lifetime);
	Mesh->SetWorldScale3D(FVector(Radius / CylinderRadius, Radius / CylinderRadius, PuddleThickness / 100.f));
	if (UMaterialInstanceDynamic* Material = Mesh->CreateAndSetMaterialInstanceDynamic(0))
	{
		Material->SetVectorParameterValue(TEXT("Color"), Color);
	}
	GetWorldTimerManager().SetTimer(DamageTimer, this, &ANoFearAcidPuddle::DealDamage, TickInterval, true, 0.f);
}

void ANoFearAcidPuddle::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	// Последнюю секунду лужа сжимается и исчезает.
	const float Remaining = Lifetime - static_cast<float>(GetWorld()->GetTimeSeconds() - SpawnTime);
	const float Scale = FMath::Clamp(Remaining, 0.05f, 1.f) * Radius / CylinderRadius;
	Mesh->SetWorldScale3D(FVector(Scale, Scale, PuddleThickness / 100.f));
}

void ANoFearAcidPuddle::DealDamage()
{
	UWorld* World = GetWorld();
	if (!World || DamagePerTick <= 0.f)
	{
		return;
	}
	for (auto It = LastBurn.CreateIterator(); It; ++It)
	{
		if (!It->Key.IsValid())
		{
			It.RemoveCurrent();
		}
	}

	APawn* Shooter = GetInstigator();
	const FVector Center = GetActorLocation();
	FCollisionQueryParams Params(SCENE_QUERY_STAT(NoFearAcidPuddle), false, this);
	TArray<FOverlapResult> Overlaps;
	World->OverlapMultiByObjectType(Overlaps, Center + FVector(0.f, 0.f, PuddleReachHeight * 0.5f), FQuat::Identity, FCollisionObjectQueryParams(ECC_Pawn),
		FCollisionShape::MakeCapsule(Radius, PuddleReachHeight * 0.5f + Radius), Params);

	TSet<AActor*> Damaged;
	for (const FOverlapResult& Overlap : Overlaps)
	{
		AActor* Victim = Overlap.GetActor();
		if (!Victim || Damaged.Contains(Victim) || UNoFearHealthComponent::IsActorDead(Victim) || !UNoFearHealthComponent::FindHealth(Victim))
		{
			continue;
		}
		if (Shooter && (Victim == Shooter || UNoFearTeamLibrary::AreFriendly(Shooter, Victim)))
		{
			continue;
		}
		// Только кто стоит в луже: по горизонтали внутри круга, по высоте — у земли.
		const FVector Offset = Victim->GetActorLocation() - Center;
		if (Offset.Size2D() > Radius || Offset.Z > PuddleReachHeight || Offset.Z < -40.f)
		{
			continue;
		}
		const double Now = World->GetTimeSeconds();
		FLastBurn& Burn = LastBurn.FindOrAdd(Victim);
		if (Burn.World == World && Now - Burn.Time < TickInterval * 0.9)
		{
			continue;
		}
		Burn.World = World;
		Burn.Time = Now;
		Damaged.Add(Victim);
		UGameplayStatics::ApplyDamage(Victim, DamagePerTick, Shooter ? Shooter->GetController() : nullptr, this, DamageType);
		TotalDamageDealt += DamagePerTick;
	}
}
