#include "AI/NoFearShooterAIController.h"

#include "AI/NoFearCombatComponent.h"
#include "BehaviorTree/BehaviorTree.h"
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
	}
}

void ANoFearShooterAIController::MaintainWeapon()
{
	UNoFearWeaponComponent* Weapons = GetWeaponComponent();
	if (!Weapons || Weapons->IsOwnerDead() || Weapons->GetWeapons().Num() == 0)
	{
		return;
	}

	if (Weapons->GetTargetWeaponIndex() == INDEX_NONE)
	{
		Weapons->EquipWeapon(0);
	}

	const bool bWantsAim = bAimInCombat && GetCurrentTarget() != nullptr;
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
