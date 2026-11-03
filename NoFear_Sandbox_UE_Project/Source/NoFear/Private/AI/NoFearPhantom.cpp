#include "AI/NoFearPhantom.h"

#include "AI/NoFearCompanionAIController.h"
#include "Animation/AnimSequence.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Core/NoFearHealthComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Character.h"
#include "Materials/MaterialInstanceDynamic.h"

TArray<TWeakObjectPtr<ANoFearPhantom>> ANoFearPhantom::Active;

ANoFearPhantom::ANoFearPhantom()
{
	PrimaryActorTick.bCanEverTick = true;
	SetCanBeDamaged(false);

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = Root;

	// Без коллизии: пули и люди проходят насквозь, попадание ловит NotifyShot.
	Body = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("Body"));
	Body->SetupAttachment(Root);
	Body->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Body->SetGenerateOverlapEvents(false);
	Body->SetCastShadow(false);
	Body->SetOnlyOwnerSee(true);
	Body->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;

	BodyClass = TSoftClassPtr<APawn>(FSoftObjectPath(TEXT("/Game/Blueprints/Bots/CBP_Bot_Monster.CBP_Bot_Monster_C")));
	RunAnimation = TSoftObjectPtr<UAnimSequence>(FSoftObjectPath(TEXT("/Game/Characters/UEFN_Mannequin/Animations/Run/M_Neutral_Run_Loop_F.M_Neutral_Run_Loop_F")));
	GhostMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/Materials/M_NoFear_ShadeCloak.M_NoFear_ShadeCloak")));
}

void ANoFearPhantom::Init(APawn* InHero, const FVector& GroundLocation)
{
	Hero = InHero;
	SetOwner(InHero);
	SpawnTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
	Active.AddUnique(this);

	// Тело — как у загонщика: тот же меш и та же посадка в капсуле.
	if (const UClass* Class = BodyClass.LoadSynchronous())
	{
		if (const ACharacter* Defaults = Cast<ACharacter>(Class->GetDefaultObject()))
		{
			if (const USkeletalMeshComponent* Template = Defaults->GetMesh())
			{
				Body->SetSkeletalMesh(Template->GetSkeletalMeshAsset());
				Body->SetRelativeTransform(Template->GetRelativeTransform());
			}
			if (const UCapsuleComponent* Capsule = Defaults->GetCapsuleComponent())
			{
				HalfHeight = Capsule->GetScaledCapsuleHalfHeight();
			}
		}
	}
	if (UAnimSequence* Run = RunAnimation.LoadSynchronous())
	{
		Body->PlayAnimation(Run, true);
	}
	Materials.Reset();
	if (UMaterialInterface* Ghost = GhostMaterial.LoadSynchronous())
	{
		for (int32 Slot = 0; Slot < Body->GetNumMaterials(); ++Slot)
		{
			UMaterialInstanceDynamic* Instance = UMaterialInstanceDynamic::Create(Ghost, this);
			Body->SetMaterial(Slot, Instance);
			Materials.Add(Instance);
		}
	}
	SetOpacity(0.f);

	const FVector HeroLocation = InHero ? InHero->GetActorLocation() : GroundLocation;
	SetActorLocationAndRotation(GroundLocation + FVector(0.f, 0.f, HalfHeight),
		FRotator(0.f, (HeroLocation - GroundLocation).GetSafeNormal2D().Rotation().Yaw, 0.f));
}

void ANoFearPhantom::SetOpacity(float Value)
{
	for (UMaterialInstanceDynamic* Material : Materials)
	{
		if (Material)
		{
			Material->SetScalarParameterValue(TEXT("Opacity"), Value);
		}
	}
}

void ANoFearPhantom::Vanish(bool bByShot)
{
	if (IsVanishing())
	{
		return;
	}
	VanishStart = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
	bShot = bByShot;
	Active.Remove(this);
	if (!bByShot)
	{
		return;
	}
	// Стрелял в пустоту: напарник это видит.
	for (ANoFearCompanionAIController* Companion : TActorRange<ANoFearCompanionAIController>(GetWorld()))
	{
		if (Companion && Companion->GetLeaderPawn() == Hero.Get())
		{
			Companion->ReactToPhantomShot();
			break;
		}
	}
}

void ANoFearPhantom::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	UWorld* World = GetWorld();
	const APawn* HeroPawn = Hero.Get();
	if (!World)
	{
		return;
	}
	const double Now = World->GetTimeSeconds();
	if (IsVanishing())
	{
		const float Alpha = static_cast<float>((Now - VanishStart) / FadeTime);
		SetOpacity(Opacity * FMath::Clamp(1.f - Alpha, 0.f, 1.f));
		if (Alpha >= 1.f)
		{
			Destroy();
		}
		return;
	}
	if (!HeroPawn || UNoFearHealthComponent::IsActorDead(HeroPawn) || Now - SpawnTime >= LifeTime)
	{
		Vanish(false);
		return;
	}
	SetOpacity(Opacity * FMath::Clamp(static_cast<float>((Now - SpawnTime) / FadeTime), 0.f, 1.f));

	const FVector Location = GetActorLocation();
	const FVector ToHero = (HeroPawn->GetActorLocation() - Location).GetSafeNormal2D();
	if (FVector::Dist2D(Location, HeroPawn->GetActorLocation()) <= VanishDistance)
	{
		Vanish(false);
		return;
	}
	// Бежит по прямой, держась земли.
	FVector Next = Location + ToHero * RunSpeed * DeltaSeconds;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(NoFearPhantomGround), false, this);
	Params.AddIgnoredActor(HeroPawn);
	FHitResult Hit;
	if (World->LineTraceSingleByChannel(Hit, Next + FVector(0.f, 0.f, 50.f), Next - FVector(0.f, 0.f, HalfHeight + 150.f), ECC_Visibility, Params))
	{
		Next.Z = Hit.ImpactPoint.Z + HalfHeight;
	}
	SetActorLocationAndRotation(Next, FRotator(0.f, ToHero.Rotation().Yaw, 0.f));
}

void ANoFearPhantom::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	Active.Remove(this);
	Super::EndPlay(EndPlayReason);
}

void ANoFearPhantom::NotifyShot(const UWorld* World, const AActor* Shooter, const FVector& Start, const FVector& End)
{
	if (!World || !Shooter)
	{
		return;
	}
	TArray<ANoFearPhantom*> Phantoms;
	GetPhantoms(World, Phantoms);
	for (ANoFearPhantom* Phantom : Phantoms)
	{
		if (Phantom->Hero.Get() != Shooter)
		{
			continue;
		}
		const FVector Center = Phantom->GetActorLocation();
		const FVector Up(0.f, 0.f, Phantom->HalfHeight * 0.9f);
		FVector OnShot;
		FVector OnBody;
		FMath::SegmentDistToSegmentSafe(Start, End, Center - Up, Center + Up, OnShot, OnBody);
		if (FVector::Dist(OnShot, OnBody) <= Phantom->HitRadius)
		{
			Phantom->Vanish(true);
		}
	}
}

void ANoFearPhantom::GetPhantoms(const UWorld* World, TArray<ANoFearPhantom*>& OutPhantoms)
{
	OutPhantoms.Reset();
	Active.RemoveAll([](const TWeakObjectPtr<ANoFearPhantom>& Item) { return !Item.IsValid(); });
	for (const TWeakObjectPtr<ANoFearPhantom>& Item : Active)
	{
		if (Item->GetWorld() == World && !Item->IsVanishing())
		{
			OutPhantoms.Add(Item.Get());
		}
	}
}

int32 ANoFearPhantom::CountPhantoms(const UObject* WorldContextObject)
{
	TArray<ANoFearPhantom*> Phantoms;
	GetPhantoms(WorldContextObject ? WorldContextObject->GetWorld() : nullptr, Phantoms);
	return Phantoms.Num();
}
