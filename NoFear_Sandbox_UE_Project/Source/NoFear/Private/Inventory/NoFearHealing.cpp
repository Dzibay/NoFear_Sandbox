// Лечение героя бинтом и аптечкой из кейса: UseTime секунд шагом и без стрельбы, потом здоровье и предмет тратится.
// Урон, спринт, клавиши действий (ЛКМ, ПКМ, R, G), колесо предметов и экран кейса прерывают лечение, предмет остаётся.
// Запускают колесо (UseWheelSlot) и меню экрана кейса («Использовать»); клавиши — в NoFearInventoryScreen.cpp,
// полоса — ANoFearHUD::DrawHealing.

#include "Inventory/NoFearInventoryComponent.h"

#include "Core/NoFearHealthComponent.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameFramework/PlayerController.h"
#include "Inventory/NoFearItemData.h"
#include "UObject/UnrealType.h"
#include "Weapons/NoFearGrenadeComponent.h"
#include "Weapons/NoFearWeaponComponent.h"

#define LOCTEXT_NAMESPACE "NoFearHealing"

namespace
{
	/** Структура ввода тела GASP: поля пользовательской структуры называются вроде WantsToWalk_5_3A4AA0AA... */
	const FName InputStateName = TEXT("CharacterInputState");

	FBoolProperty* FindInputStateFlag(AActor* Owner, const TCHAR* Prefix, void*& OutState)
	{
		const FStructProperty* StateProperty = Owner ? FindFProperty<FStructProperty>(Owner->GetClass(), InputStateName) : nullptr;
		if (!StateProperty)
		{
			return nullptr;
		}
		for (TFieldIterator<FBoolProperty> It(StateProperty->Struct); It; ++It)
		{
			if (It->GetName().StartsWith(Prefix))
			{
				OutState = StateProperty->ContainerPtrToValuePtr<void>(Owner);
				return *It;
			}
		}
		return nullptr;
	}

	const TCHAR* WalkFlag = TEXT("WantsToWalk");
	const TCHAR* SprintFlag = TEXT("WantsToSprint");
}

bool UNoFearInventoryComponent::GetInputStateFlag(const TCHAR* Prefix, bool& bOutValue) const
{
	void* State = nullptr;
	const FBoolProperty* Flag = FindInputStateFlag(GetOwner(), Prefix, State);
	if (!Flag)
	{
		return false;
	}
	bOutValue = Flag->GetPropertyValue_InContainer(State);
	return true;
}

void UNoFearInventoryComponent::SetInputStateFlag(const TCHAR* Prefix, bool bValue) const
{
	void* State = nullptr;
	if (const FBoolProperty* Flag = FindInputStateFlag(GetOwner(), Prefix, State))
	{
		Flag->SetPropertyValue_InContainer(State, bValue);
	}
}

bool UNoFearInventoryComponent::StartHealing(UNoFearItemData* Item)
{
	if (!bCaseActive || !Item || Item->Kind != ENoFearItemKind::Medicine || HealingItem || bScreenOpen || bItemWheelOpen)
	{
		return false;
	}
	APlayerController* PC = GetOwnerPlayerController();
	UNoFearHealthComponent* Health = UNoFearHealthComponent::FindHealth(GetOwner());
	if (!PC || !Health || CountItem(Item) <= 0 || UNoFearHealthComponent::IsActorIncapacitated(GetOwner()))
	{
		return false;
	}
	if (Health->GetHealth() >= Health->GetMaxHealth())
	{
		PushNotice(LOCTEXT("HealthFull", "Здоровье и так полное"));
		return false;
	}
	bool bSprinting = false;
	if (GetInputStateFlag(SprintFlag, bSprinting) && bSprinting)
	{
		PushNotice(LOCTEXT("HealSprinting", "На бегу лечиться нельзя"));
		return false;
	}

	HealingItem = Item;
	HealingController = PC;
	HealingStartTime = GetWorld()->GetTimeSeconds();
	HealingDuration = FMath::Max(Item->UseTime, 0.f);
	bHealingWalkSaved = GetInputStateFlag(WalkFlag, bWalkBeforeHealing);

	// Руки заняты: очередь, прицел и замах гранатой обрываются.
	if (UNoFearWeaponComponent* Weapons = GetWeaponComponent())
	{
		Weapons->StopFire();
		Weapons->SetAiming(false);
	}
	if (UNoFearGrenadeComponent* Grenades = GetOwner()->FindComponentByClass<UNoFearGrenadeComponent>(); Grenades && Grenades->IsAiming())
	{
		Grenades->CancelAim();
	}
	Health->OnDamaged.AddUniqueDynamic(this, &UNoFearInventoryComponent::HandleHealingDamaged);

	if (HealingDuration <= 0.f)
	{
		FinishHealing();
		return true;
	}
	SetInputStateFlag(WalkFlag, true);
	EnsureCaseInput(PC);
	return true;
}

void UNoFearInventoryComponent::CancelHealing(bool bNotice)
{
	if (!HealingItem)
	{
		return;
	}
	EndHealing();
	if (bNotice)
	{
		PushNotice(LOCTEXT("HealingCancelled", "Лечение прервано"));
	}
}

float UNoFearInventoryComponent::GetHealingProgress() const
{
	if (!HealingItem)
	{
		return 0.f;
	}
	if (HealingDuration <= 0.f)
	{
		return 1.f;
	}
	return FMath::Clamp(float(GetWorld()->GetTimeSeconds() - HealingStartTime) / HealingDuration, 0.f, 1.f);
}

float UNoFearInventoryComponent::GetHealingTimeRemaining() const
{
	return HealingItem ? FMath::Max(HealingDuration - float(GetWorld()->GetTimeSeconds() - HealingStartTime), 0.f) : 0.f;
}

void UNoFearInventoryComponent::UpdateHealing(APlayerController* PC)
{
	if (!bCaseActive || !PC || PC != HealingController.Get() || UNoFearHealthComponent::IsActorIncapacitated(GetOwner()))
	{
		CancelHealing(false);
		return;
	}
	const UNoFearHealthComponent* Health = UNoFearHealthComponent::FindHealth(GetOwner());
	if (!Health || Health->GetHealth() >= Health->GetMaxHealth())
	{
		// Вылечили иначе (напарник с бинтом): предмет не нужен.
		CancelHealing(false);
		PushNotice(LOCTEXT("HealthFull", "Здоровье и так полное"));
		return;
	}
	bool bSprinting = false;
	if (CountItem(HealingItem) <= 0 || bScreenOpen || bItemWheelOpen || (GetInputStateFlag(SprintFlag, bSprinting) && bSprinting))
	{
		CancelHealing(true);
		return;
	}
	if (GetHealingProgress() >= 1.f)
	{
		FinishHealing();
		return;
	}
	// Тело возвращает свою походку по клавише шага: держим шаг каждый кадр.
	SetInputStateFlag(WalkFlag, true);
}

void UNoFearInventoryComponent::FinishHealing()
{
	UNoFearItemData* Item = HealingItem;
	EndHealing();
	UNoFearHealthComponent* Health = UNoFearHealthComponent::FindHealth(GetOwner());
	if (!Item || !Health || CountItem(Item) <= 0)
	{
		return;
	}
	const float Healed = Health->Heal(Item->bHealToFull ? Health->GetMaxHealth() : Item->HealAmount);
	RemoveItem(Item, 1);
	PushNotice(FText::Format(LOCTEXT("Healed", "{0}: +{1} здоровья"), Item->DisplayName, FText::AsNumber(FMath::RoundToInt(Healed))));
}

void UNoFearInventoryComponent::EndHealing()
{
	if (!HealingItem)
	{
		return;
	}
	HealingItem = nullptr;
	if (UNoFearHealthComponent* Health = UNoFearHealthComponent::FindHealth(GetOwner()))
	{
		Health->OnDamaged.RemoveDynamic(this, &UNoFearInventoryComponent::HandleHealingDamaged);
	}
	if (bHealingWalkSaved)
	{
		SetInputStateFlag(WalkFlag, bWalkBeforeHealing);
	}
	bHealingWalkSaved = false;
	if (APlayerController* PC = HealingController.Get())
	{
		RemoveHealingInput(PC);
	}
	HealingController.Reset();
}

void UNoFearInventoryComponent::HandleHealingDamaged(UNoFearHealthComponent* Component, const FNoFearDamageInfo& Info)
{
	// Упавший тоже получает событие с нулевым уроном — его снимает UpdateHealing без строки.
	if (Info.Amount > 0.f)
	{
		CancelHealing(true);
	}
}

#undef LOCTEXT_NAMESPACE
