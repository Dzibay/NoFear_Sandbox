#include "Weapons/NoFearWeaponComponent.h"

#include "Animation/AnimInstance.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/LocalPlayer.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "GameFramework/Character.h"
#include "GameFramework/PlayerController.h"
#include "InputActionValue.h"
#include "TimerManager.h"
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
	if ((bDriveArmRecoil || bDriveOverlayState) && Character && Character->GetMesh())
	{
		// Пишем переменные отдачи и позу до того, как меш начнёт считать анимацию в этом кадре.
		Character->GetMesh()->PrimaryComponentTick.AddPrerequisite(this, PrimaryComponentTick);
		if (bDriveOverlayState)
		{
			// Пересоздание анимации (пост-процесс поз в BeginPlay персонажа) сбрасывает слои позы к Default.
			Character->GetMesh()->OnAnimInitialized.AddDynamic(this, &UNoFearWeaponComponent::HandleAnimInitialized);
		}
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
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(SwitchTimer);
	}
	bSwitching = false;

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
	if (Weapons.IsValidIndex(Index))
	{
		RequestWeapon(Index);
	}
}

void UNoFearWeaponComponent::HolsterWeapon()
{
	RequestWeapon(INDEX_NONE);
}

void UNoFearWeaponComponent::RequestWeapon(int32 Index)
{
	if (bOwnerDead || bHandsBusy)
	{
		return;
	}
	if (bSwitching)
	{
		// Старое оружие уже убирается: просто меняем цель, таймер смены тот же.
		PendingIndex = Index;
		return;
	}
	if (Index == CurrentIndex)
	{
		return;
	}

	if (ANoFearWeapon* OldWeapon = GetCurrentWeapon())
	{
		const float HolsterDuration = OldWeapon->BeginHolster();
		if (HolsterDuration > KINDA_SMALL_NUMBER)
		{
			bSwitching = true;
			PendingIndex = Index;
			GetWorld()->GetTimerManager().SetTimer(SwitchTimer, this, &UNoFearWeaponComponent::FinishSwitch, HolsterDuration, false);
			return;
		}
	}

	CompleteSwitch(Index);
}

void UNoFearWeaponComponent::FinishSwitch()
{
	bSwitching = false;
	const int32 NewIndex = PendingIndex;
	PendingIndex = INDEX_NONE;
	CompleteSwitch(NewIndex);
}

void UNoFearWeaponComponent::CompleteSwitch(int32 NewIndex)
{
	ANoFearWeapon* OldWeapon = GetCurrentWeapon();
	if (OldWeapon && OldWeapon->GetWeaponState() != ENoFearWeaponState::Holstered)
	{
		OldWeapon->Holster();
	}

	if (!Weapons.IsValidIndex(NewIndex))
	{
		CurrentIndex = INDEX_NONE;
		SetOverlayState(UnarmedOverlayState);
		if (OldWeapon)
		{
			OnWeaponChanged.Broadcast(nullptr, OldWeapon);
		}
		return;
	}

	CurrentIndex = NewIndex;
	LastEquippedIndex = NewIndex;
	ANoFearWeapon* NewWeapon = Weapons[NewIndex];

	// Поза ставится до доставания, чтобы монтаж доставания играл уже в нужной стойке.
	SetOverlayState(NewWeapon->GetWeaponData()->OverlayState);
	NewWeapon->Equip(Cast<ACharacter>(GetOwner()));
	NewWeapon->SetAiming(bWantsToAim);

	OnWeaponChanged.Broadcast(NewWeapon, OldWeapon);
}

void UNoFearWeaponComponent::NextWeapon()
{
	if (Weapons.Num() == 0)
	{
		return;
	}
	// Отсчитываем от цели, а не от текущего оружия: быстрая прокрутка колеса листает дальше, не дожидаясь смены.
	const int32 Target = GetTargetWeaponIndex();
	const int32 From = Target == INDEX_NONE ? LastEquippedIndex - 1 : Target;
	EquipWeapon((From + 1 + Weapons.Num()) % Weapons.Num());
}

void UNoFearWeaponComponent::PreviousWeapon()
{
	if (Weapons.Num() == 0)
	{
		return;
	}
	const int32 Target = GetTargetWeaponIndex();
	const int32 From = Target == INDEX_NONE ? LastEquippedIndex + 1 : Target;
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
	if (bOwnerDead || bHandsBusy || bFireBlocked)
	{
		return;
	}
	if (ANoFearWeapon* Weapon = GetCurrentWeapon())
	{
		Weapon->StartFire();
	}
}

void UNoFearWeaponComponent::SetFireBlocked(bool bBlocked)
{
	bFireBlocked = bBlocked;
	if (bBlocked)
	{
		StopFire();
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
	if (bOwnerDead || bHandsBusy)
	{
		return;
	}
	if (ANoFearWeapon* Weapon = GetCurrentWeapon())
	{
		Weapon->Reload();
	}
}

void UNoFearWeaponComponent::SetHandsBusy(bool bBusy)
{
	if (bHandsBusy == bBusy)
	{
		return;
	}
	bHandsBusy = bBusy;

	ANoFearWeapon* Weapon = GetCurrentWeapon();
	if (bBusy)
	{
		StopFire();
		if (Weapon)
		{
			Weapon->SetActorHiddenInGame(true);
		}
		return;
	}
	if (Weapon && Weapon->GetWeaponState() != ENoFearWeaponState::Holstered && !bOwnerDead)
	{
		Weapon->SetActorHiddenInGame(false);
	}
	if (!bOwnerDead)
	{
		RefreshOverlayState();
	}
}

bool UNoFearWeaponComponent::IsWeaponBusy() const
{
	if (bSwitching)
	{
		return true;
	}
	const ANoFearWeapon* Weapon = GetCurrentWeapon();
	return Weapon && (Weapon->GetWeaponState() == ENoFearWeaponState::Reloading || Weapon->GetWeaponState() == ENoFearWeaponState::Equipping
		|| Weapon->GetWeaponState() == ENoFearWeaponState::Holstering);
}

void UNoFearWeaponComponent::SetAiming(bool bNewAiming)
{
	bWantsToAim = bNewAiming && !bOwnerDead;
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

void UNoFearWeaponComponent::HandleOwnerDeath()
{
	if (bOwnerDead)
	{
		return;
	}
	bOwnerDead = true;

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(SwitchTimer);
	}
	bSwitching = false;
	PendingIndex = INDEX_NONE;
	bWantsToAim = false;

	// Оружие, которое в этот момент убиралось или доставалось, остаётся там, где было.
	for (ANoFearWeapon* Weapon : Weapons)
	{
		if (Weapon)
		{
			Weapon->Abandon();
		}
	}
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
		// У IA_Aim из GASP триггеры Pressed и Released: Triggered приходит один раз на нажатие (true) и один на
		// отпускание (false). Completed/Started тут не годятся: Completed прилетает уже на следующем кадре после нажатия.
		Input->BindAction(AimAction, ETriggerEvent::Triggered, this, &UNoFearWeaponComponent::OnAimInput);
		Input->BindAction(AimAction, ETriggerEvent::Canceled, this, &UNoFearWeaponComponent::OnAimCanceled);
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

void UNoFearWeaponComponent::OnAimInput(const FInputActionValue& Value)
{
	SetAiming(Value.Get<bool>());
}

void UNoFearWeaponComponent::OnAimCanceled()
{
	SetAiming(false);
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
	if (GetTargetWeaponIndex() != INDEX_NONE)
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
	if (bDriveArmRecoil)
	{
		UpdateArmRecoilVariables();
	}
	EnforceOverlayState();
}

void UNoFearWeaponComponent::HandleAnimInitialized()
{
	// Перепривязываем в тике, а не посреди инициализации анимации.
	RelinkOverlayState();
}

void UNoFearWeaponComponent::EnforceOverlayState()
{
	AActor* Owner = GetOwner();
	if (!bDriveOverlayState || DesiredOverlayState.IsNone() || !Owner)
	{
		return;
	}

	if (bOverlayLayersLost)
	{
		bOverlayLayersLost = false;
		SetOverlayState(DesiredOverlayState);
		return;
	}

	const FProperty* Property = Owner->GetClass()->FindPropertyByName(OverlayStateProperty);
	const UEnum* Enum = nullptr;
	int64 Current = INDEX_NONE;
	if (const FByteProperty* ByteProperty = CastField<FByteProperty>(Property))
	{
		Enum = ByteProperty->Enum;
		Current = ByteProperty->GetPropertyValue_InContainer(Owner);
	}
	else if (const FEnumProperty* EnumProperty = CastField<FEnumProperty>(Property))
	{
		Enum = EnumProperty->GetEnum();
		Current = EnumProperty->GetUnderlyingProperty()->GetSignedIntPropertyValue(EnumProperty->ContainerPtrToValuePtr<void>(Owner));
	}
	if (!Enum)
	{
		return;
	}

	const FString Wanted = DesiredOverlayState.ToString();
	for (int32 Index = 0; Index < Enum->NumEnums() - 1; ++Index)
	{
		if (Enum->GetNameStringByIndex(Index) == Wanted || Enum->GetDisplayNameTextByIndex(Index).ToString() == Wanted)
		{
			if (Enum->GetValueByIndex(Index) != Current)
			{
				SetOverlayState(DesiredOverlayState);
			}
			return;
		}
	}
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

void UNoFearWeaponComponent::RefreshOverlayState() const
{
	const ANoFearWeapon* Weapon = GetCurrentWeapon();
	SetOverlayState(Weapon && Weapon->GetWeaponData() ? Weapon->GetWeaponData()->OverlayState : UnarmedOverlayState);
}

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
	DesiredOverlayState = StateName;

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
