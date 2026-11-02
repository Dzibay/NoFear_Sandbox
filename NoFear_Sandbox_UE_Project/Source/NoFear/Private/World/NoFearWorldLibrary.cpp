#include "World/NoFearWorldLibrary.h"

#include "AIController.h"
#include "AI/NoFearBotSubsystem.h"
#include "AI/NoFearMonsterAIController.h"
#include "Components/StaticMeshComponent.h"
#include "Core/NoFearPlayerController.h"
#include "Core/NoFearTeam.h"
#include "EnhancedInputSubsystems.h"
#include "Engine/LocalPlayer.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "Inventory/NoFearInventoryComponent.h"
#include "Inventory/NoFearItemData.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "NavigationSystem.h"
#include "Navigation/PathFollowingComponent.h"
#include "Perception/AISense_Hearing.h"
#include "Sound/SoundBase.h"
#include "Weapons/NoFearGrenade.h"
#include "Weapons/NoFearGrenadeData.h"

namespace NoFearWorld
{
	bool IsMonsterTeam(const AActor* Actor)
	{
		const ENoFearTeam Team = UNoFearTeamLibrary::GetTeam(Actor);
		return Team == ENoFearTeam::Monster || Team == ENoFearTeam::AggroMonster;
	}

	bool SegmentCrossesBox(const FTransform& BoxTransform, const FBox& Box, const FVector& Start, const FVector& End)
	{
		const FVector A = BoxTransform.InverseTransformPosition(Start);
		const FVector B = BoxTransform.InverseTransformPosition(End);
		if (Box.IsInside(A) || Box.IsInside(B))
		{
			return true;
		}
		const FVector Delta = B - A;
		return !Delta.IsNearlyZero() && FMath::LineBoxIntersection(Box, A, B, Delta);
	}

	bool IsPathThroughBox(const AAIController* AI, const APawn* Pawn, const FTransform& BoxTransform, const FBox& Box, float MaxLength)
	{
		const UPathFollowingComponent* PathFollowing = AI ? AI->GetPathFollowingComponent() : nullptr;
		if (!Pawn || !PathFollowing || PathFollowing->GetStatus() != EPathFollowingStatus::Moving)
		{
			return false;
		}
		const FNavPathSharedPtr Path = PathFollowing->GetPath();
		if (!Path.IsValid())
		{
			return false;
		}
		const TArray<FNavPathPoint>& Points = Path->GetPathPoints();
		FVector Previous = Pawn->GetNavAgentLocation();
		float Length = 0.f;
		for (int32 Index = static_cast<int32>(PathFollowing->GetCurrentPathIndex()) + 1; Index < Points.Num() && Length < MaxLength; ++Index)
		{
			const FVector Point = Points[Index].Location;
			if (SegmentCrossesBox(BoxTransform, Box, Previous, Point))
			{
				return true;
			}
			Length += FVector::Dist(Previous, Point);
			Previous = Point;
		}
		return false;
	}

	bool IsFocusBehindPlane(const AAIController* AI, const APawn* Pawn, const FTransform& PlaneTransform, float HalfWidth, float HalfHeight, float Slack, float MaxDistance)
	{
		const AActor* Target = AI ? AI->GetFocusActor() : nullptr;
		if (!Pawn || !Target || Target == Pawn || FVector::DistSquared(Target->GetActorLocation(), Pawn->GetActorLocation()) > FMath::Square(MaxDistance))
		{
			return false;
		}
		const FVector From = PlaneTransform.InverseTransformPosition(Pawn->GetActorLocation());
		const FVector To = PlaneTransform.InverseTransformPosition(Target->GetActorLocation());
		if (FMath::Sign(From.X) == FMath::Sign(To.X) || FMath::IsNearlyEqual(From.X, To.X))
		{
			return false;
		}
		const float Alpha = From.X / (From.X - To.X);
		const FVector Cross = FMath::Lerp(From, To, Alpha);
		return FMath::Abs(Cross.Y) <= HalfWidth + Slack && FMath::Abs(Cross.Z) <= HalfHeight + Slack;
	}

	void PlaySoundAndNoise(AActor* Source, USoundBase* Sound, const FVector& Location, float Loudness, float Range, AActor* NoiseInstigator, FName Tag, float VolumeMultiplier)
	{
		if (!Source)
		{
			return;
		}
		if (Sound)
		{
			UGameplayStatics::PlaySoundAtLocation(Source, Sound, Location, VolumeMultiplier);
		}
		if (Loudness > 0.f && Range > 0.f)
		{
			UAISense_Hearing::ReportNoiseEvent(Source, Location, Loudness, NoiseInstigator ? NoiseInstigator : Source, Range, Tag);
		}
	}

	void LureMonsters(const AActor* Source, const FVector& Location, float Range, float CheckDuration)
	{
		UNoFearBotSubsystem* Bots = Source && Range > 0.f ? UNoFearBotSubsystem::Get(Source) : nullptr;
		if (!Bots)
		{
			return;
		}
		// Источник стоит на столе или режет навмеш своим корпусом: идти надо к ближайшей точке навмеша рядом.
		FVector Goal = Location;
		const UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(Source->GetWorld());
		FNavLocation Projected;
		if (Nav && Nav->ProjectPointToNavigation(Location, Projected, FVector(300.f, 300.f, 400.f)))
		{
			Goal = Projected.Location;
		}
		for (ANoFearAIController* Bot : Bots->GetBots())
		{
			const APawn* Listener = Bot ? Bot->GetPawn() : nullptr;
			if (Listener && Cast<ANoFearMonsterAIController>(Bot)
				&& FVector::Dist(Listener->GetActorLocation(), Location) <= Range * Bot->GetHearingRangeScale())
			{
				Bot->CheckNoise(Goal, CheckDuration);
			}
		}
	}

	void TintPlaceholder(UStaticMeshComponent* Mesh, UMaterialInterface* PlaceholderMaterial, const FLinearColor& Color)
	{
		// В шаблоне класса (вызов из конструктора) не красим: материал шаблона — чужой объект, карта с ним не сохранится.
		if (!Mesh || !PlaceholderMaterial || Mesh->IsTemplate())
		{
			return;
		}
		UMaterialInstanceDynamic* Material = Cast<UMaterialInstanceDynamic>(Mesh->GetMaterial(0));
		if (!Material || Material->Parent != PlaceholderMaterial || Material->GetOuter() != Mesh)
		{
			Material = Mesh->CreateDynamicMaterialInstance(0, PlaceholderMaterial);
		}
		if (Material)
		{
			Material->SetVectorParameterValue(TEXT("Color"), Color);
		}
	}

	void FitMesh(UStaticMeshComponent* Mesh, UStaticMesh* StaticMesh, const FVector& Size, const FVector& Center)
	{
		if (!Mesh)
		{
			return;
		}
		Mesh->SetStaticMesh(StaticMesh);
		const FBox MeshBox = StaticMesh ? StaticMesh->GetBoundingBox() : FBox(FVector(-50.f), FVector(50.f));
		const FVector Scale = Size / MeshBox.GetSize().ComponentMax(FVector(1.f));
		Mesh->SetRelativeScale3D(Scale);
		Mesh->SetRelativeLocation(Center - MeshBox.GetCenter() * Scale);
	}

	void SpawnDebris(AActor* Source, const FTransform& Transform, const FVector& AreaSize, const FVector& PieceSize, int32 Count, const FVector& Push, float Lifetime, UMaterialInterface* PlaceholderMaterial, const FLinearColor& Color)
	{
		UWorld* World = Source ? Source->GetWorld() : nullptr;
		UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
		if (!World || !Mesh || Count <= 0)
		{
			return;
		}
		const FVector MeshSize = Mesh->GetBoundingBox().GetSize().ComponentMax(FVector(1.f));
		FActorSpawnParameters SpawnParams;
		SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		for (int32 Index = 0; Index < Count; ++Index)
		{
			const FVector LocalPoint(FMath::FRandRange(-0.4f, 0.4f) * AreaSize.X, FMath::FRandRange(-0.4f, 0.4f) * AreaSize.Y, FMath::FRandRange(-0.4f, 0.4f) * AreaSize.Z);
			const FTransform PieceTransform(Transform.GetRotation() * FRotator(FMath::FRandRange(-30.f, 30.f), FMath::FRandRange(-30.f, 30.f), FMath::FRandRange(-30.f, 30.f)).Quaternion(),
				Transform.TransformPosition(LocalPoint), PieceSize / MeshSize);
			AStaticMeshActor* Piece = World->SpawnActor<AStaticMeshActor>(AStaticMeshActor::StaticClass(), PieceTransform, SpawnParams);
			if (!Piece)
			{
				continue;
			}
			UStaticMeshComponent* PieceMesh = Piece->GetStaticMeshComponent();
			PieceMesh->SetMobility(EComponentMobility::Movable);
			PieceMesh->SetStaticMesh(Mesh);
			PieceMesh->SetCanEverAffectNavigation(false);
			PieceMesh->SetCollisionProfileName(UCollisionProfile::PhysicsActor_ProfileName);
			PieceMesh->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
			PieceMesh->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
			PieceMesh->SetCollisionResponseToChannel(ECC_Visibility, ECR_Ignore);
			// Декорация: не расталкивает выпавшие предметы и другие физические тела.
			PieceMesh->SetCollisionResponseToChannel(ECC_PhysicsBody, ECR_Ignore);
			PieceMesh->SetCollisionResponseToChannel(ECC_WorldDynamic, ECR_Ignore);
			TintPlaceholder(PieceMesh, PlaceholderMaterial, Color);
			PieceMesh->SetSimulatePhysics(true);
			const FVector Direction = (Push.IsNearlyZero() ? FMath::VRand().GetSafeNormal2D() : Push.GetSafeNormal()) + FMath::VRand() * 0.5f + FVector(0.f, 0.f, 0.4f);
			PieceMesh->SetPhysicsLinearVelocity(Direction.GetSafeNormal() * FMath::FRandRange(250.f, 550.f));
			PieceMesh->SetPhysicsAngularVelocityInDegrees(FMath::VRand() * 360.f);
			Piece->SetLifeSpan(Lifetime);
		}
	}

	void Explode(AActor* Source, UNoFearGrenadeData* Data, const FVector& Location, APawn* Instigator)
	{
		UWorld* World = Source ? Source->GetWorld() : nullptr;
		if (!World)
		{
			return;
		}
		if (!Data)
		{
			Data = GetDefaultGrenadeData();
		}
		FActorSpawnParameters SpawnParams;
		SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		SpawnParams.Instigator = Instigator;
		SpawnParams.Owner = Source;
		SpawnParams.bDeferConstruction = true;
		ANoFearGrenade* Blast = World->SpawnActor<ANoFearGrenade>(ANoFearGrenade::StaticClass(), FTransform(Location), SpawnParams);
		if (!Blast)
		{
			return;
		}
		Blast->Data = Data;
		Blast->FinishSpawning(FTransform(Location));
		Blast->Explode();
	}

	UNoFearGrenadeData* GetDefaultGrenadeData()
	{
		return LoadObject<UNoFearGrenadeData>(nullptr, TEXT("/Game/Weapons/Data/DA_Grenade.DA_Grenade"));
	}

	UNoFearItemData* LoadItem(const TSoftObjectPtr<UNoFearItemData>& Item)
	{
		return Item.IsNull() ? nullptr : Item.LoadSynchronous();
	}

	bool IsInteractHeld(const APawn* User)
	{
		const ANoFearPlayerController* PC = User ? Cast<ANoFearPlayerController>(User->GetController()) : nullptr;
		const ULocalPlayer* LocalPlayer = PC ? PC->GetLocalPlayer() : nullptr;
		const UEnhancedInputLocalPlayerSubsystem* Input = LocalPlayer ? ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(LocalPlayer) : nullptr;
		if (!Input || !PC->InteractAction)
		{
			return false;
		}
		for (const FKey& Key : Input->QueryKeysMappedToAction(PC->InteractAction))
		{
			if (PC->IsInputKeyDown(Key))
			{
				return true;
			}
		}
		return false;
	}

	UNoFearItemData* FindKey(const APawn* User, FName KeyId)
	{
		const UNoFearInventoryComponent* Case = UNoFearInventoryComponent::FindCase(User);
		return Case && !KeyId.IsNone() ? Case->FindItemByKeyId(KeyId) : nullptr;
	}

	FText UnlockPrompt(const UNoFearItemData* Key)
	{
		return Key ? FText::Format(NSLOCTEXT("NoFearWorld", "UnlockWith", "Отпереть: {0}"), Key->GetDisplayName())
			: NSLOCTEXT("NoFearWorld", "Unlock", "Отпереть");
	}

	void Notify(const APawn* User, const FText& Text)
	{
		if (UNoFearInventoryComponent* Case = UNoFearInventoryComponent::FindCase(User))
		{
			Case->PushNotice(Text);
		}
	}
}
