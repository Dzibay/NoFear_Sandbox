#include "Portal/NoFearPortal.h"

#include "Components/PointLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	/** Сфера и цилиндр движка: радиус 50, высота цилиндра 100. */
	constexpr float ShapeRadius = 50.f;
	constexpr float ShapeHeight = 100.f;
	constexpr float CrackThickness = 1.f;
	/** Трещина на земле шире провала во столько раз. */
	constexpr float CrackScale = 1.7f;
	/** Луч упирается проводнику в грудь. */
	constexpr float BeamTargetHeight = 40.f;

	UStaticMeshComponent* MakeShape(AActor* Owner, USceneComponent* Parent, const FName Name, UStaticMesh* Mesh, UMaterialInterface* Material)
	{
		UStaticMeshComponent* Shape = Owner->CreateDefaultSubobject<UStaticMeshComponent>(Name);
		Shape->SetupAttachment(Parent);
		Shape->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Shape->SetCanEverAffectNavigation(false);
		Shape->SetCastShadow(false);
		Shape->SetStaticMesh(Mesh);
		Shape->SetMaterial(0, Material);
		return Shape;
	}
}

ANoFearPortal::ANoFearPortal()
{
	PrimaryActorTick.bCanEverTick = true;

	static ConstructorHelpers::FObjectFinderOptional<UStaticMesh> SphereFinder(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	static ConstructorHelpers::FObjectFinderOptional<UStaticMesh> CylinderFinder(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	static ConstructorHelpers::FObjectFinderOptional<UMaterialInterface> MaterialFinder(TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	CylinderMesh = CylinderFinder.Get();
	ShapeMaterial = MaterialFinder.Get();

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = Root;

	Core = MakeShape(this, Root, TEXT("Core"), SphereFinder.Get(), ShapeMaterial);
	Crack = MakeShape(this, Root, TEXT("Crack"), CylinderMesh, ShapeMaterial);

	Light = CreateDefaultSubobject<UPointLightComponent>(TEXT("Light"));
	Light->SetupAttachment(Root);
	Light->SetCastShadows(false);
	Light->SetIntensity(LightIntensity);
	Light->SetAttenuationRadius(400.f);
}

void ANoFearPortal::BeginPlay()
{
	Super::BeginPlay();

	DisplaySize = Size;
	SetMeshColor(Core, CoreColor);
	SetMeshColor(Crack, GlowColor);
	Light->SetLightColor(GlowColor);
}

int32 ANoFearPortal::GetFeederCount() const
{
	int32 Count = 0;
	for (const TWeakObjectPtr<APawn>& Feeder : Feeders)
	{
		Count += Feeder.IsValid() ? 1 : 0;
	}
	return Count;
}

int32 ANoFearPortal::GetMonsterCount() const
{
	int32 Count = 0;
	for (const TWeakObjectPtr<APawn>& Monster : Monsters)
	{
		Count += Monster.IsValid() ? 1 : 0;
	}
	return Count;
}

TArray<APawn*> ANoFearPortal::GetFeeders() const
{
	TArray<APawn*> Result;
	for (const TWeakObjectPtr<APawn>& Feeder : Feeders)
	{
		if (APawn* Pawn = Feeder.Get())
		{
			Result.Add(Pawn);
		}
	}
	return Result;
}

TArray<APawn*> ANoFearPortal::GetMonsters() const
{
	TArray<APawn*> Result;
	for (const TWeakObjectPtr<APawn>& Monster : Monsters)
	{
		if (APawn* Pawn = Monster.Get())
		{
			Result.Add(Pawn);
		}
	}
	return Result;
}

float ANoFearPortal::GetCoreRadius() const
{
	return CoreRadius + CoreRadiusPerSize * Size;
}

FVector ANoFearPortal::GetCoreLocation() const
{
	return GetActorLocation() + FVector(0.f, 0.f, CoreLift + GetCoreRadius());
}

void ANoFearPortal::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	const UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	const float Now = static_cast<float>(World->GetTimeSeconds());

	// Размер на экране догоняет настоящий: рывки роста и сжатия видны плавно.
	DisplaySize = FMath::FInterpTo(DisplaySize, Size, DeltaSeconds, 2.f);

	// Пульс: чем больше проводников, тем чаще бьётся. Закрываясь, портал дрожит и схлопывается.
	const int32 FeederCount = GetFeederCount();
	const float PulseSpeed = bClosing ? 14.f : 2.f + 1.5f * FMath::Min(FeederCount, 4);
	const float PulseDepth = bClosing ? 0.15f : 0.06f + 0.02f * FMath::Min(FeederCount, 4);
	const float Pulse = 1.f + PulseDepth * FMath::Sin(Now * PulseSpeed);
	float Collapse = 1.f;
	if (bClosing)
	{
		Collapse = FMath::Clamp(1.f - static_cast<float>(World->GetTimeSeconds() - CloseTime) * 0.12f, 0.35f, 1.f);
	}

	const float Radius = (CoreRadius + CoreRadiusPerSize * DisplaySize) * Pulse * Collapse;
	const float CoreZ = CoreLift + Radius;
	Core->SetRelativeLocation(FVector(0.f, 0.f, CoreZ));
	Core->SetRelativeScale3D(FVector(Radius / ShapeRadius));

	const float CrackRadius = Radius * CrackScale;
	Crack->SetRelativeLocation(FVector(0.f, 0.f, CrackThickness * 0.5f + 1.f));
	Crack->SetRelativeScale3D(FVector(CrackRadius / ShapeRadius, CrackRadius / ShapeRadius, CrackThickness / ShapeHeight));
	SetMeshColor(Crack, GlowColor * (0.55f + 0.45f * (Pulse - 1.f) / FMath::Max(PulseDepth, KINDA_SMALL_NUMBER)));

	Light->SetRelativeLocation(FVector(0.f, 0.f, CoreZ));
	Light->SetIntensity((LightIntensity + LightIntensityPerSize * DisplaySize) * Pulse * Collapse);
	Light->SetAttenuationRadius(300.f + 8.f * DisplaySize);

	UpdateBeams(Pulse);
}

void ANoFearPortal::UpdateBeams(float Pulse)
{
	const FVector Start = GetCoreLocation();
	int32 Used = 0;
	for (const TWeakObjectPtr<APawn>& Feeder : Feeders)
	{
		const APawn* Pawn = Feeder.Get();
		if (!Pawn)
		{
			continue;
		}
		const FVector End = Pawn->GetActorLocation() + FVector(0.f, 0.f, BeamTargetHeight);
		const FVector Delta = End - Start;
		const float Length = Delta.Size();
		if (Length < 1.f)
		{
			continue;
		}

		if (!Beams.IsValidIndex(Used))
		{
			UStaticMeshComponent* Beam = NewObject<UStaticMeshComponent>(this);
			Beam->SetupAttachment(Root);
			Beam->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			Beam->SetCanEverAffectNavigation(false);
			Beam->SetCastShadow(false);
			Beam->SetStaticMesh(CylinderMesh);
			Beam->SetMaterial(0, ShapeMaterial);
			Beam->RegisterComponent();
			SetMeshColor(Beam, GlowColor);
			Beams.Add(Beam);
		}
		UStaticMeshComponent* Beam = Beams[Used++];
		Beam->SetVisibility(true);
		// Цилиндр вытянут по Z: ставим его серединой между провалом и проводником и разворачиваем осью к проводнику.
		const float Thickness = BeamRadius * (0.7f + 0.6f * (Pulse - 1.f) * 5.f) / ShapeRadius;
		Beam->SetWorldLocationAndRotation(Start + Delta * 0.5f, FRotationMatrix::MakeFromZ(Delta / Length).Rotator());
		Beam->SetWorldScale3D(FVector(FMath::Max(Thickness, 0.01f), FMath::Max(Thickness, 0.01f), Length / ShapeHeight));
	}
	for (int32 Index = Used; Index < Beams.Num(); ++Index)
	{
		if (Beams[Index])
		{
			Beams[Index]->SetVisibility(false);
		}
	}
}

void ANoFearPortal::SetMeshColor(UStaticMeshComponent* Mesh, const FLinearColor& Color) const
{
	if (!Mesh)
	{
		return;
	}
	UMaterialInstanceDynamic* Material = Cast<UMaterialInstanceDynamic>(Mesh->GetMaterial(0));
	if (!Material)
	{
		Material = Mesh->CreateAndSetMaterialInstanceDynamic(0);
	}
	if (Material)
	{
		Material->SetVectorParameterValue(TEXT("Color"), Color);
	}
}
