#include "Weapons/NoFearWeaponComponent.h"

#include "Animation/AnimInstance.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/LocalPlayer.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "GameFramework/Character.h"
#include "GameFramework/PlayerController.h"
#include "InputActionValue.h"
#include "Weapons/NoFearWeapon.h"
#include "Weapons/NoFearWeaponData.h"

UNoFearWeaponComponent::UNoFearWeaponComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PrePhysics;
	WeaponClass = ANoFearWeapon::StaticClass();
}

void UNoFearWeaponComponent::BeginPlay()
{
	Super::BeginPlay();

	const ACharacter* Character = Cast<ACharacter>(GetOwner());
	if (bDriveArmRecoil && Character && Character->GetMesh())
	{
		// Пишем переменные отдачи до того, как меш начнёт считать анимацию в этом кадре.
		Character->GetMesh()->PrimaryComponentTick.AddPrerequisite(this, PrimaryComponentTick);
	}
	else
	{
		SetComponentTickEnabled(false);
	}

	for (UNoFearWeaponData* WeaponData : StartingWeapons)
	{
		AddWeapon(WeaponData);
	}

	if (bEquipOnStart && Weapons.Num() > 0)
	{
		EquipWeapon(0);
	}
	else
	{
		SetOverlayState(UnarmedOverlayState);
	}
}

void UNoFearWeaponComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	for (ANoFearWeapon* Weapon : Weapons)
	{
		if (IsValid(Weapon))
		{
			Weapon->Destroy();
		}
	}
	Weapons.Reset();
	CurrentIndex = INDEX_NONE;

	Super::EndPlay(EndPlayReason);
}

// ---------------------------------------------------------------------------
// Инвентарь
// ---------------------------------------------------------------------------

ANoFearWeapon* UNoFearWeaponComponent::AddWeapon(UNoFearWeaponData* WeaponData)
{
	if (!WeaponData)
	{
		return nullptr;
	}

	for (ANoFearWeapon* Existing : Weapons)
	{
		if (Existing && Existing->GetWeaponData() == WeaponData)
		{
			Existing->AddReserveAmmo(WeaponData->StartingReserveAmmo);
			return Existing;
		}
	}

	ACharacter* Character = Cast<ACharacter>(GetOwner());
	UWorld* World = GetWorld();
	if (!Character || !World)
	{
		return nullptr;
	}

	FActorSpawnParameters Params;
	Params.Owner = Character;
	Params.Instigator = Character;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	UClass* ClassToSpawn = WeaponClass ? WeaponClass.Get() : ANoFearWeapon::StaticClass();
	ANoFearWeapon* Weapon = World->SpawnActor<ANoFearWeapon>(ClassToSpawn, Character->GetActorTransform(), Params);
	if (!Weapon)
	{
		return nullptr;
	}

	Weapon->Initialize(WeaponData, Character);
	Weapons.Add(Weapon);
	return Weapon;
}

void UNoFearWeaponComponent::EquipWeapon(int32 Index)
{
	if (!Weapons.IsValidIndex(Index) || Index == CurrentIndex)
	{
		return;
	}

	ANoFearWeapon* OldWeapon = GetCurrentWeapon();
	if (OldWeapon)
	{
		OldWeapon->Holster();
	}

	CurrentIndex = Index;
	LastEquippedIndex = Index;
	ANoFearWeapon* NewWeapon = Weapons[Index];

	// Поза ставится до доставания, чтобы монтаж доставания играл уже в нужной стойке.
	SetOverlayState(NewWeapon->GetWeaponData()->OverlayState);
	NewWeapon->Equip(Cast<ACharacter>(GetOwner()));
	NewWeapon->SetAiming(bWantsToAim);

	OnWeaponChanged.Broadcast(NewWeapon, OldWeapon);
}

void UNoFearWeaponComponent::HolsterWeapon()
{
	ANoFearWeapon* OldWeapon = GetCurrentWeapon();
	if (!OldWeapon)
	{
		return;
	}

	OldWeapon->Holster();
	CurrentIndex = INDEX_NONE;
	SetOverlayState(UnarmedOverlayState);

	OnWeaponChanged.Broadcast(nullptr, OldWeapon);
}

void UNoFearWeaponComponent::NextWeapon()
{
	if (Weapons.Num() == 0)
	{
		return;
	}
	const int32 From = CurrentIndex == INDEX_NONE ? LastEquippedIndex - 1 : CurrentIndex;
	EquipWeapon((From + 1 + Weapons.Num()) % Weapons.Num());
}

void UNoFearWeaponComponent::PreviousWeapon()
{
	if (Weapons.Num() == 0)
	{
		return;
	}
	const int32 From = CurrentIndex == INDEX_NONE ? LastEquippedIndex + 1 : CurrentIndex;
	EquipWeapon((From - 1 + Weapons.Num()) % Weapons.Num());
}

ANoFearWeapon* UNoFearWeaponComponent::GetCurrentWeapon() const
{
	return Weapons.IsValidIndex(CurrentIndex) ? Weapons[CurrentIndex].Get() : nullptr;
}

// ---------------------------------------------------------------------------
// Действия
// ---------------------------------------------------------------------------

void UNoFearWeaponComponent::StartFire()
{
	if (ANoFearWeapon* Weapon = GetCurrentWeapon())
	{
		Weapon->StartFire();
	}
}

void UNoFearWeaponComponent::StopFire()
{
	// Отпускаем спуск у всех стволов: вдруг кнопку отпустили во время смены оружия.
	for (ANoFearWeapon* Weapon : Weapons)
	{
		if (Weapon)
		{
			Weapon->StopFire();
		}
	}
}

void UNoFearWeaponComponent::Reload()
{
	if (ANoFearWeapon* Weapon = GetCurrentWeapon())
	{
		Weapon->Reload();
	}
}

void UNoFearWeaponComponent::SetAiming(bool bNewAiming)
{
	bWantsToAim = bNewAiming;
	if (ANoFearWeapon* Weapon = GetCurrentWeapon())
	{
		Weapon->SetAiming(bNewAiming);
	}
}

void UNoFearWeaponComponent::HandleControllerChanged()
{
	StopFire();
	SetAiming(false);
}

// ---------------------------------------------------------------------------
// Ввод
// ---------------------------------------------------------------------------

void UNoFearWeaponComponent::SetupPlayerInput(UInputComponent* PlayerInputComponent)
{
	const APawn* Pawn = Cast<APawn>(GetOwner());
	const APlayerController* PC = Pawn ? Cast<APlayerController>(Pawn->GetController()) : nullptr;
	if (PC && InputMapping)
	{
		if (UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PC->GetLocalPlayer()))
		{
			if (!Subsystem->HasMappingContext(InputMapping))
			{
				Subsystem->AddMappingContext(InputMapping, InputMappingPriority);
			}
		}
	}

	UEnhancedInputComponent* Input = Cast<UEnhancedInputComponent>(PlayerInputComponent);
	if (!Input)
	{
		return;
	}

	if (FireAction)
	{
		// Started/Completed, а не Triggered: огонь держит оружие, ввод только жмёт и отпускает спуск.
		// Canceled тоже отпускает — иначе потеря фокуса окна или смена контекста оставляли бы спуск зажатым.
		Input->BindAction(FireAction, ETriggerEvent::Started, this, &UNoFearWeaponComponent::StartFire);
		Input->BindAction(FireAction, ETriggerEvent::Completed, this, &UNoFearWeaponComponent::StopFire);
		Input->BindAction(FireAction, ETriggerEvent::Canceled, this, &UNoFearWeaponComponent::StopFire);
	}
	if (AimAction)
	{
		Input->BindAction(AimAction, ETriggerEvent::Started, this, &UNoFearWeaponComponent::OnAimStarted);
		Input->BindAction(AimAction, ETriggerEvent::Completed, this, &UNoFearWeaponComponent::OnAimStopped);
		Input->BindAction(AimAction, ETriggerEvent::Canceled, this, &UNoFearWeaponComponent::OnAimStopped);
	}
	if (ReloadAction)
	{
		Input->BindAction(ReloadAction, ETriggerEvent::Started, this, &UNoFearWeaponComponent::Reload);
	}
	if (SwitchWeaponAction)
	{
		Input->BindAction(SwitchWeaponAction, ETriggerEvent::Triggered, this, &UNoFearWeaponComponent::OnSwitchWeaponInput);
	}
	if (HolsterAction)
	{
		Input->BindAction(HolsterAction, ETriggerEvent::Started, this, &UNoFearWeaponComponent::OnHolsterInput);
	}
}

void UNoFearWeaponComponent::OnSwitchWeaponInput(const FInputActionValue& Value)
{
	const float Direction = Value.Get<float>();
	if (Direction > 0.f)
	{
		NextWeapon();
	}
	else if (Direction < 0.f)
	{
		PreviousWeapon();
	}
}

void UNoFearWeaponComponent::OnHolsterInput()
{
	if (GetCurrentWeapon())
	{
		HolsterWeapon();
	}
	else
	{
		EquipWeapon(LastEquippedIndex);
	}
}

// ---------------------------------------------------------------------------
// Анимация
// ---------------------------------------------------------------------------

void UNoFearWeaponComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	UpdateArmRecoilVariables();
}

void UNoFearWeaponComponent::UpdateArmRecoilVariables()
{
	const ACharacter* Character = Cast<ACharacter>(GetOwner());
	UAnimInstance* AnimInstance = Character && Character->GetMesh() ? Character->GetMesh()->GetAnimInstance() : nullptr;
	if (!AnimInstance)
	{
		return;
	}

	UClass* AnimClass = AnimInstance->GetClass();
	if (RecoilAnimClass.Get() != AnimClass)
	{
		RecoilAnimClass = AnimClass;

		RecoilAlphaProperty = AnimClass->FindPropertyByName(RecoilAlphaVariable);
		if (RecoilAlphaProperty && !RecoilAlphaProperty->IsA<FDoubleProperty>() && !RecoilAlphaProperty->IsA<FFloatProperty>())
		{
			RecoilAlphaProperty = nullptr;
		}

		auto FindRotator = [AnimClass](FName Name) -> FStructProperty*
		{
			FStructProperty* Property = CastField<FStructProperty>(AnimClass->FindPropertyByName(Name));
			return Property && Property->Struct == TBaseStructure<FRotator>::Get() ? Property : nullptr;
		};
		UpperArmRecoilProperty = FindRotator(UpperArmRecoilVariable);
		LowerArmRecoilProperty = FindRotator(LowerArmRecoilVariable);
		HandRecoilProperty = FindRotator(HandRecoilVariable);

		if (!RecoilAlphaProperty)
		{
			UE_LOG(LogTemp, Log, TEXT("NoFearWeaponComponent: в %s нет float-переменной '%s', отдача рук не передаётся"),
				*AnimClass->GetName(), *RecoilAlphaVariable.ToString());
		}
	}

	if (!RecoilAlphaProperty)
	{
		return;
	}

	const ANoFearWeapon* Weapon = GetCurrentWeapon();
	const UNoFearWeaponData* WeaponData = Weapon ? Weapon->GetWeaponData() : nullptr;
	const double Alpha = WeaponData ? Weapon->GetArmRecoilAlpha() : 0.0;

	if (const FDoubleProperty* DoubleProperty = CastField<FDoubleProperty>(RecoilAlphaProperty))
	{
		DoubleProperty->SetPropertyValue_InContainer(AnimInstance, Alpha);
	}
	else
	{
		CastFieldChecked<FFloatProperty>(RecoilAlphaProperty)->SetPropertyValue_InContainer(AnimInstance, static_cast<float>(Alpha));
	}

	auto SetRotator = [AnimInstance](const FStructProperty* Property, const FRotator& Value)
	{
		if (Property)
		{
			*Property->ContainerPtrToValuePtr<FRotator>(AnimInstance) = Value;
		}
	};
	SetRotator(UpperArmRecoilProperty, WeaponData ? WeaponData->UpperArmRecoil : FRotator::ZeroRotator);
	SetRotator(LowerArmRecoilProperty, WeaponData ? WeaponData->LowerArmRecoil : FRotator::ZeroRotator);
	SetRotator(HandRecoilProperty, WeaponData ? WeaponData->HandRecoil : FRotator::ZeroRotator);
}

// ---------------------------------------------------------------------------
// Поза GASP
// ---------------------------------------------------------------------------

void UNoFearWeaponComponent::SetOverlayState(FName StateName) const
{
	AActor* Owner = GetOwner();
	if (!bDriveOverlayState || !Owner || StateName.IsNone())
	{
		return;
	}

	FProperty* Property = Owner->GetClass()->FindPropertyByName(OverlayStateProperty);
	if (!Property)
	{
		return;
	}

	const UEnum* Enum = nullptr;
	if (const FByteProperty* ByteProperty = CastField<FByteProperty>(Property))
	{
		Enum = ByteProperty->Enum;
	}
	else if (const FEnumProperty* EnumProperty = CastField<FEnumProperty>(Property))
	{
		Enum = EnumProperty->GetEnum();
	}
	if (!Enum)
	{
		return;
	}

	// У Blueprint-перечислений внутренние имена вида NewEnumerator3, поэтому сравниваем и с отображаемым.
	const FString Wanted = StateName.ToString();
	int64 Value = INDEX_NONE;
	for (int32 Index = 0; Index < Enum->NumEnums() - 1; ++Index)
	{
		if (Enum->GetNameStringByIndex(Index) == Wanted || Enum->GetDisplayNameTextByIndex(Index).ToString() == Wanted)
		{
			Value = Enum->GetValueByIndex(Index);
			break;
		}
	}
	if (Value == INDEX_NONE)
	{
		UE_LOG(LogTemp, Warning, TEXT("NoFearWeaponComponent: в %s нет позы '%s'"), *Enum->GetName(), *Wanted);
		return;
	}

	void* ValuePtr = Property->ContainerPtrToValuePtr<void>(Owner);
	if (const FByteProperty* ByteProperty = CastField<FByteProperty>(Property))
	{
		ByteProperty->SetPropertyValue(ValuePtr, static_cast<uint8>(Value));
	}
	else
	{
		CastField<FEnumProperty>(Property)->GetUnderlyingProperty()->SetIntPropertyValue(ValuePtr, Value);
	}

	// Blueprint-сеттер RepNotify-переменной вызывает OnRep сам; из C++ — вызываем вручную.
	const FName OnRepName(*FString::Printf(TEXT("OnRep_%s"), *OverlayStateProperty.ToString()));
	if (UFunction* OnRep = Owner->FindFunction(OnRepName); OnRep && OnRep->NumParms == 0)
	{
		Owner->ProcessEvent(OnRep, nullptr);
	}
}
