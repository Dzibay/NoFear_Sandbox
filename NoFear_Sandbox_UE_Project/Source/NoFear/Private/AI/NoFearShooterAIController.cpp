#include "AI/NoFearShooterAIController.h"

#include "AI/NoFearBotSubsystem.h"
#include "AI/NoFearCombatComponent.h"
#include "BehaviorTree/BehaviorTree.h"
#include "Animation/NoFearPoseComponent.h"
#include "Core/NoFearCharacter.h"
#include "Core/NoFearTeam.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Weapons/NoFearGrenadeComponent.h"
#include "UObject/ConstructorHelpers.h"
#include "Weapons/NoFearWeapon.h"
#include "Weapons/NoFearWeaponComponent.h"
#include "Weapons/NoFearWeaponData.h"

ANoFearShooterAIController::ANoFearShooterAIController(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	LeashRadius = 1500.f;

	// Стрелки зовут своих в бою и замечают тела погибших своих.
	AlertShareRadius = 2500.f;
	bReactToCorpses = true;
	BotGrenades = 2;

	static ConstructorHelpers::FObjectFinderOptional<UBehaviorTree> Tree(TEXT("/Game/AI/BT_NoFear_Soldier.BT_NoFear_Soldier"));
	BehaviorTree = Tree.Get();
}

UNoFearWeaponComponent* ANoFearShooterAIController::GetWeaponComponent() const
{
	return GetPawn() ? GetPawn()->FindComponentByClass<UNoFearWeaponComponent>() : nullptr;
}

UNoFearCombatComponent* ANoFearShooterAIController::GetCombatComponent() const
{
	return GetPawn() ? GetPawn()->FindComponentByClass<UNoFearCombatComponent>() : nullptr;
}

bool ANoFearShooterAIController::IsTargetVisible() const
{
	// Зрение восприятия видит тело целиком; компонент боя проверяет линию огня до груди и головы.
	if (const UNoFearCombatComponent* Combat = GetCombatComponent(); Combat && Combat->GetTarget() == GetCurrentTarget())
	{
		return Combat->HasLineOfSight();
	}
	return Super::IsTargetVisible();
}

void ANoFearShooterAIController::OnPossess(APawn* InPawn)
{
	// Компонент нужен до старта дерева: он читает цель из блэкборда и пишет туда CanFire.
	if (InPawn && bAddCombatComponentIfMissing && !InPawn->FindComponentByClass<UNoFearCombatComponent>())
	{
		UNoFearCombatComponent* Combat = NewObject<UNoFearCombatComponent>(InPawn, TEXT("CombatComponent"));
		Combat->RegisterComponent();
	}

	Super::OnPossess(InPawn);
}

void ANoFearShooterAIController::OnUnPossess()
{
	if (UNoFearWeaponComponent* Weapons = GetWeaponComponent())
	{
		Weapons->StopFire();
		Weapons->SetAiming(false);
	}
	Super::OnUnPossess();
}

void ANoFearShooterAIController::UpdateBot()
{
	Super::UpdateBot();

	if (GetPawn() && !UNoFearHealthComponent::IsActorDead(GetPawn()))
	{
		MaintainWeapon();
		TryThrowGrenade();
	}
}

void ANoFearShooterAIController::MaintainWeapon()
{
	UNoFearWeaponComponent* Weapons = GetWeaponComponent();
	if (!Weapons || Weapons->IsOwnerDead() || Weapons->GetWeapons().Num() == 0 || Weapons->AreHandsBusy())
	{
		return;
	}

	if (Weapons->GetTargetWeaponIndex() == INDEX_NONE)
	{
		Weapons->EquipWeapon(0);
	}

	const bool bWantsAim = bAimInCombat && GetCurrentTarget() != nullptr && !IsEvadingGrenade();
	if (Weapons->IsAiming() != bWantsAim)
	{
		Weapons->SetAiming(bWantsAim);
	}

	if (!bInfiniteReserveAmmo)
	{
		return;
	}
	for (ANoFearWeapon* Weapon : Weapons->GetWeapons())
	{
		const UNoFearWeaponData* Data = Weapon ? Weapon->GetWeaponData() : nullptr;
		if (Data && Weapon->GetReserveAmmo() < Data->MagazineSize)
		{
			Weapon->AddReserveAmmo(FMath::Max(Data->MaxReserveAmmo, Data->MagazineSize));
		}
	}
}

void ANoFearShooterAIController::HandleTargetChanged(AActor* NewTarget, AActor* OldTarget)
{
	Super::HandleTargetChanged(NewTarget, OldTarget);

	// Только что заметил врага: гранату не сразу.
	if (NewTarget && !OldTarget)
	{
		NextGrenadeTime = FMath::Max(NextGrenadeTime, GetNow() + FMath::FRandRange(FirstGrenadeDelay.X, FirstGrenadeDelay.Y));
	}

	if (!NewTarget)
	{
		if (UNoFearWeaponComponent* Weapons = GetWeaponComponent())
		{
			Weapons->StopFire();
		}
	}
	if (GetPawn())
	{
		MaintainWeapon();
	}
}

bool ANoFearShooterAIController::GetGrenadeSpot(const AActor* Target, FVector& OutSpot) const
{
	const FNoFearKnownActor* Knowledge = Target ? KnownActors.Find(Target) : nullptr;
	UWorld* World = GetWorld();
	if (!Knowledge || !World)
	{
		return false;
	}
	const FVector Known = Knowledge->LastKnownLocation;
	FHitResult Hit;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(NoFearGrenadeSpot), false);
	Params.AddIgnoredActor(Target);
	if (World->LineTraceSingleByObjectType(Hit, Known + FVector(0.f, 0.f, 50.f), Known - FVector(0.f, 0.f, 300.f), FCollisionObjectQueryParams(ECC_WorldStatic), Params))
	{
		OutSpot = Hit.ImpactPoint + FVector(0.f, 0.f, 5.f);
		return true;
	}
	OutSpot = Known;
	return true;
}

void ANoFearShooterAIController::TryThrowGrenade()
{
	const double Now = GetNow();
	if (!bThrowGrenades || Now < NextGrenadeTime)
	{
		return;
	}
	APawn* MyPawn = GetPawn();
	const ANoFearCharacter* BotCharacter = GetNoFearCharacter();
	UNoFearGrenadeComponent* Grenades = BotCharacter ? BotCharacter->GetGrenadeComponent() : nullptr;
	AActor* Target = GetCurrentTarget();
	if (!MyPawn || !Grenades || Grenades->GetGrenadeCount() <= 0)
	{
		GrenadeDebugReason = TEXT("no grenades");
		return;
	}
	if (!Target || GetAlertState() != ENoFearAlertState::Combat)
	{
		GrenadeDebugReason = TEXT("no target");
		return;
	}
	if (IsEvadingGrenade() || UNoFearPoseComponent::IsActorStaggered(MyPawn) || !Grenades->CanThrow())
	{
		GrenadeDebugReason = TEXT("busy");
		return;
	}
	const FNoFearKnownActor* Knowledge = KnownActors.Find(Target);
	FVector Spot;
	if (!Knowledge || !GetGrenadeSpot(Target, Spot))
	{
		GrenadeDebugReason = TEXT("no spot");
		return;
	}
	const float Distance = FVector::Dist2D(MyPawn->GetActorLocation(), Spot);
	if (Distance < MinGrenadeDistance || Distance > MaxGrenadeDistance)
	{
		GrenadeDebugReason = FString::Printf(TEXT("distance %.0f"), Distance);
		return;
	}

	// Повод: цель засела в укрытии или враги стоят кучкой.
	const double Unseen = Knowledge->bVisible ? 0.0 : Now - Knowledge->LastSeenTime;
	const bool bTargetHidden = Unseen >= HiddenTargetTime && Unseen <= MaxUnseenTime;
	int32 Cluster = 0;
	for (const TPair<TWeakObjectPtr<AActor>, FNoFearKnownActor>& Pair : KnownActors)
	{
		const AActor* Other = Pair.Key.Get();
		if (Other && Pair.Value.Awareness >= 1.f && Now - Pair.Value.LastSeenTime <= 3.0 && IsValidTarget(Other)
			&& FVector::Dist(Pair.Value.LastKnownLocation, Knowledge->LastKnownLocation) <= ClusterRadius)
		{
			++Cluster;
		}
	}
	if (!bTargetHidden && Cluster < ClusterCount)
	{
		GrenadeDebugReason = FString::Printf(TEXT("no reason (unseen %.1f, cluster %d)"), Unseen, Cluster);
		return;
	}

	// Своих и мирных рядом с точкой не задеть.
	const float SafeDistance = Grenades->GetRadius() + FriendlyGrenadeMargin;
	for (TActorIterator<ANoFearCharacter> It(GetWorld()); It; ++It)
	{
		const ANoFearCharacter* Other = *It;
		if (Other == MyPawn || Other->IsDead() || UNoFearTeamLibrary::AreHostile(MyPawn, Other))
		{
			continue;
		}
		if (FVector::Dist(Other->GetActorLocation(), Spot) < SafeDistance)
		{
			GrenadeDebugReason = FString::Printf(TEXT("friend near %s"), *Other->GetName());
			NextGrenadeTime = Now + 1.0;
			return;
		}
	}

	if (!Grenades->ThrowGrenadeAt(Spot))
	{
		GrenadeDebugReason = TEXT("cannot reach");
		NextGrenadeTime = Now + 1.5;
		return;
	}
	SetFocalPoint(Spot);
	Shout(GrenadeShout, 1.5f);
	++GrenadesThrown;
	GrenadeDebugReason.Reset();
	NextGrenadeTime = Now + FMath::FRandRange(GrenadeCooldown.X, GrenadeCooldown.Y);
}
