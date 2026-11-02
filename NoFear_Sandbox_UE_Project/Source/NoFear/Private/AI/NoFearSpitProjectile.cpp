#include "AI/NoFearSpitProjectile.h"

#include "AI/NoFearAcidPuddle.h"
#include "AI/NoFearOpportunitySubsystem.h"
#include "AI/NoFearSpitEffectComponent.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/Character.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Core/NoFearHealthComponent.h"
#include "Core/NoFearTeam.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/DamageType.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"

ANoFearSpitProjectile::ANoFearSpitProjectile()
{
	PrimaryActorTick.bCanEverTick = false;
	InitialLifeSpan = 6.f;
	DamageType = UDamageType::StaticClass();
	PuddleClass = ANoFearAcidPuddle::StaticClass();

	Collision = CreateDefaultSubobject<USphereComponent>(TEXT("Collision"));
	Collision->InitSphereRadius(12.f);
	Collision->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Collision->SetCollisionObjectType(ECC_WorldDynamic);
	Collision->SetCollisionResponseToAllChannels(ECR_Ignore);
	Collision->SetCollisionResponseToChannel(ECC_WorldStatic, ECR_Block);
	Collision->SetCollisionResponseToChannel(ECC_WorldDynamic, ECR_Block);
	Collision->SetCollisionResponseToChannel(ECC_Pawn, ECR_Block);
	Collision->SetCollisionResponseToChannel(ECC_PhysicsBody, ECR_Block);
	Collision->SetGenerateOverlapEvents(false);
	Collision->bReturnMaterialOnMove = false;
	RootComponent = Collision;

	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	Mesh->SetupAttachment(Collision);
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Mesh->SetCastShadow(false);
	Mesh->SetRelativeScale3D(FVector(0.28f));
	static ConstructorHelpers::FObjectFinderOptional<UStaticMesh> Sphere(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	static ConstructorHelpers::FObjectFinderOptional<UMaterialInterface> ShapeMaterial(TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	Mesh->SetStaticMesh(Sphere.Get());
	Mesh->SetMaterial(0, ShapeMaterial.Get());

	ProjectileMovement = CreateDefaultSubobject<UProjectileMovementComponent>(TEXT("ProjectileMovement"));
	ProjectileMovement->UpdatedComponent = Collision;
	ProjectileMovement->InitialSpeed = 0.f;
	ProjectileMovement->MaxSpeed = 4000.f;
	ProjectileMovement->ProjectileGravityScale = 1.f;
	ProjectileMovement->bRotationFollowsVelocity = true;
	ProjectileMovement->bShouldBounce = false;
	ProjectileMovement->bAutoActivate = false;
}

void ANoFearSpitProjectile::BeginPlay()
{
	Super::BeginPlay();

	if (APawn* Shooter = GetInstigator())
	{
		Collision->IgnoreActorWhenMoving(Shooter, true);
	}
	Collision->OnComponentHit.AddDynamic(this, &ANoFearSpitProjectile::HandleHit);

	if (UMaterialInstanceDynamic* Material = Mesh->CreateAndSetMaterialInstanceDynamic(0))
	{
		Material->SetVectorParameterValue(TEXT("Color"), Color);
	}
}

void ANoFearSpitProjectile::Launch(FVector Velocity)
{
	ProjectileMovement->Velocity = Velocity;
	ProjectileMovement->Activate(true);
}

void ANoFearSpitProjectile::HandleHit(UPrimitiveComponent* HitComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, FVector NormalImpulse, const FHitResult& Hit)
{
	if (bExploded)
	{
		return;
	}
	bExploded = true;

	APawn* Shooter = GetInstigator();
	const bool bFriendly = Shooter && OtherActor && UNoFearTeamLibrary::AreFriendly(Shooter, OtherActor);
	if (OtherActor && OtherActor != Shooter && !bFriendly && DirectDamage > 0.f && UNoFearHealthComponent::FindHealth(OtherActor)
		&& !UNoFearHealthComponent::IsActorDead(OtherActor))
	{
		const FVector Direction = ProjectileMovement->Velocity.GetSafeNormal();
		UGameplayStatics::ApplyPointDamage(OtherActor, DirectDamage, Direction, Hit, Shooter ? Shooter->GetController() : nullptr, this, DamageType);
		// Прямое попадание облепляет слизью: метка для ловчего и замедление.
		UNoFearSpitEffectComponent::ApplySlime(OtherActor, SlimeTime, SlimeSpeedScale);
		// В голову или верх корпуса — ослепляет. Героя — только в лицо.
		const ACharacter* Victim = Cast<ACharacter>(OtherActor);
		if (Victim && BlindTime > 0.f && Victim->GetCapsuleComponent())
		{
			const float Half = Victim->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
			const bool bHigh = Hit.ImpactPoint.Z >= Victim->GetActorLocation().Z + Half * BlindHeightFraction;
			const FVector Facing = Victim->GetViewRotation().Vector().GetSafeNormal2D();
			const bool bFront = !Victim->IsPlayerControlled() || FVector::DotProduct(Facing, -Direction.GetSafeNormal2D()) >= 0.3f;
			if (bHigh && bFront)
			{
				UNoFearSpitEffectComponent::ApplyBlind(OtherActor, BlindTime);
			}
		}
	}
	if (ImpactSound)
	{
		UGameplayStatics::PlaySoundAtLocation(this, ImpactSound, Hit.ImpactPoint);
	}

	SpawnPuddle(Hit.ImpactPoint);
	Destroy();
}

void ANoFearSpitProjectile::SpawnPuddle(const FVector& Location)
{
	UWorld* World = GetWorld();
	if (!PuddleClass || !World)
	{
		return;
	}

	// Лужа — на земле под точкой попадания (попал в персонажа или стену — стекает вниз).
	FHitResult Ground;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(NoFearSpitPuddle), false, this);
	Params.AddIgnoredActor(GetInstigator());
	const FVector Start = Location + FVector(0.f, 0.f, 20.f);
	const FVector End = Location - FVector(0.f, 0.f, 400.f);
	FCollisionObjectQueryParams GroundObjects;
	GroundObjects.AddObjectTypesToQuery(ECC_WorldStatic);
	GroundObjects.AddObjectTypesToQuery(ECC_WorldDynamic);
	if (!World->LineTraceSingleByObjectType(Ground, Start, End, GroundObjects, Params))
	{
		return;
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.Owner = GetOwner();
	SpawnParams.Instigator = GetInstigator();
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	World->SpawnActor<ANoFearAcidPuddle>(PuddleClass, Ground.ImpactPoint + FVector(0.f, 0.f, 1.f), FRotator::ZeroRotator, SpawnParams);
}
