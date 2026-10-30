#include "Core/NoFearCharacter.h"

#include "AIController.h"
#include "Animation/AnimInstance.h"
#include "BrainComponent.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Core/NoFearPlayerController.h"
#include "Engine/DamageEvents.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/SpringArmComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"
#include "Perception/AIPerceptionComponent.h"
#include "Perception/AIPerceptionSystem.h"
#include "Perception/AISense_Sight.h"
#include "Weapons/NoFearWeaponComponent.h"

ANoFearCharacter::ANoFearCharacter()
{
	WeaponComponent = CreateDefaultSubobject<UNoFearWeaponComponent>(TEXT("WeaponComponent"));
	HealthComponent = CreateDefaultSubobject<UNoFearHealthComponent>(TEXT("HealthComponent"));

	static ConstructorHelpers::FObjectFinderOptional<UMaterialInterface> MaskedMaterial(
		TEXT("/Game/Characters/UEFN_Mannequin/Materials/M_UEFN_Mannequin_Masked.M_UEFN_Mannequin_Masked"));
	BodyMaterial = MaskedMaterial.Get();
}

FText ANoFearCharacter::GetCharacterDisplayName() const
{
	return CharacterName.IsEmpty() ? FText::FromString(GetName()) : CharacterName;
}

void ANoFearCharacter::SetBodyColor(FLinearColor NewColor)
{
	bApplyBodyColor = true;
	BodyColor = NewColor;
	ApplyBodyColor();
}

void ANoFearCharacter::ApplyBodyColor()
{
	USkeletalMeshComponent* CharacterMesh = GetMesh();
	if (!bApplyBodyColor || !BodyMaterial || !CharacterMesh)
	{
		return;
	}

	TArray<FMaterialParameterInfo> ColorParameters;
	TArray<FGuid> ParameterIds;
	BodyMaterial->GetAllVectorParameterInfo(ColorParameters, ParameterIds);

	for (int32 Slot = 0; Slot < CharacterMesh->GetNumMaterials(); ++Slot)
	{
		UMaterialInstanceDynamic* Instance = Cast<UMaterialInstanceDynamic>(CharacterMesh->GetMaterial(Slot));
		if (!Instance || Instance->Parent != BodyMaterial)
		{
			Instance = UMaterialInstanceDynamic::Create(BodyMaterial, this);
			CharacterMesh->SetMaterial(Slot, Instance);
		}
		for (const FMaterialParameterInfo& Parameter : ColorParameters)
		{
			const FLinearColor* PartColor = BodyPartColors.Find(Parameter.Name);
			Instance->SetVectorParameterValue(Parameter.Name, PartColor ? *PartColor : BodyColor);
		}
	}
}

void ANoFearCharacter::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);

	// И в редакторе (превью Blueprint, актор на уровне), и в игре.
	ApplyBodyColor();
}

void ANoFearCharacter::SetTeam(ENoFearTeam NewTeam)
{
	if (Team == NewTeam)
	{
		return;
	}
	Team = NewTeam;

	// Зрение решает «свой/чужой» при регистрации источника: после смены команды регистрируем тело заново.
	if (HasActorBegunPlay() && !UNoFearHealthComponent::IsActorDead(this))
	{
		if (UAIPerceptionSystem* Perception = UAIPerceptionSystem::GetCurrent(this))
		{
			Perception->UnregisterSource(*this, UAISense_Sight::StaticClass());
			Perception->RegisterSourceForSenseClass(UAISense_Sight::StaticClass(), *this);
		}
		if (AAIController* AIController = Cast<AAIController>(GetController()))
		{
			if (UAIPerceptionComponent* Listener = AIController->GetPerceptionComponent())
			{
				Listener->RequestStimuliListenerUpdate();
			}
		}
	}
}

void ANoFearCharacter::SetGenericTeamId(const FGenericTeamId& NewTeamId)
{
	SetTeam(static_cast<ENoFearTeam>(NewTeamId.GetId()));
}

FGenericTeamId ANoFearCharacter::GetGenericTeamId() const
{
	return FGenericTeamId(static_cast<uint8>(Team));
}

void ANoFearCharacter::PostInitializeComponents()
{
	Super::PostInitializeComponents();

	if (HealthComponent)
	{
		HealthComponent->OnDeath.AddUniqueDynamic(this, &ANoFearCharacter::HandleDeath);
	}
}

void ANoFearCharacter::BeginPlay()
{
	Super::BeginPlay();

	if (bDisableLegacySpringArmCamera)
	{
		DisableLegacyCamera();
	}

	// Актор с уровня в игре конструктор не проходит заново — красим ещё раз (материал переиспользуется).
	ApplyBodyColor();

	if (USkeletalMeshComponent* CharacterMesh = GetMesh())
	{
		// Без этого выстрелы проходят сквозь персонажа, а урон по костям (голова) не работает.
		if (CharacterMesh->GetCollisionEnabled() == ECollisionEnabled::NoCollision || CharacterMesh->GetCollisionEnabled() == ECollisionEnabled::PhysicsOnly)
		{
			CharacterMesh->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		}
		CharacterMesh->SetCollisionResponseToChannel(HitTraceChannel, ECR_Block);
	}
}

float ANoFearCharacter::TakeDamage(float DamageAmount, FDamageEvent const& DamageEvent, AController* EventInstigator, AActor* DamageCauser)
{
	// Super раздаёт стандартные события (OnTakeAnyDamage и т.д.) для Blueprint-наследников; здоровье считаем сами.
	const float ActualDamage = Super::TakeDamage(DamageAmount, DamageEvent, EventInstigator, DamageCauser);
	return HealthComponent ? HealthComponent->ProcessDamage(ActualDamage, DamageEvent, EventInstigator, DamageCauser) : ActualDamage;
}

void ANoFearCharacter::HandleDeath(UNoFearHealthComponent* Component, const FNoFearDamageInfo& Info)
{
	if (WeaponComponent)
	{
		WeaponComponent->HandleOwnerDeath();
	}

	if (UAIPerceptionSystem* Perception = UAIPerceptionSystem::GetCurrent(this))
	{
		Perception->UnregisterSource(*this);
	}

	AController* DeadController = GetController();
	if (ANoFearPlayerController* PlayerController = Cast<ANoFearPlayerController>(DeadController))
	{
		// Игрок остаётся в теле до респауна: камера смотрит на труп, а управление выключено контроллером.
		PlayerController->NotifyPawnDied(Info);
	}
	else if (DeadController)
	{
		if (AAIController* AI = Cast<AAIController>(DeadController))
		{
			AI->StopMovement();
			if (UBrainComponent* Brain = AI->GetBrainComponent())
			{
				Brain->StopLogic(TEXT("Dead"));
			}
		}
		// Контроллер без игрока уничтожается вместе с отсоединением.
		DetachFromControllerPendingDestroy();
	}

	if (UCharacterMovementComponent* Movement = GetCharacterMovement())
	{
		Movement->StopMovementImmediately();
		Movement->DisableMovement();
	}
	if (UCapsuleComponent* Capsule = GetCapsuleComponent())
	{
		Capsule->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}

	if (bRagdollOnDeath)
	{
		StartRagdoll(Info);
	}
	if (CorpseLifeSpan > 0.f)
	{
		SetLifeSpan(CorpseLifeSpan);
	}
}

void ANoFearCharacter::StartRagdoll(const FNoFearDamageInfo& Info)
{
	USkeletalMeshComponent* CharacterMesh = GetMesh();
	if (!CharacterMesh)
	{
		return;
	}

	const FVector Velocity = GetVelocity();
	if (UAnimInstance* AnimInstance = CharacterMesh->GetAnimInstance())
	{
		AnimInstance->StopAllMontages(0.f);
	}

	CharacterMesh->SetCollisionProfileName(TEXT("Ragdoll"));
	CharacterMesh->SetAllBodiesSimulatePhysics(true);
	CharacterMesh->SetSimulatePhysics(true);
	CharacterMesh->SetAllBodiesPhysicsBlendWeight(1.f);
	CharacterMesh->WakeAllRigidBodies();
	CharacterMesh->bBlendPhysics = true;

	// Бежавший персонаж падает вперёд, а не замирает в воздухе.
	CharacterMesh->SetAllPhysicsLinearVelocity(Velocity);
	if (RagdollImpulse > 0.f && !Info.Direction.IsNearlyZero())
	{
		CharacterMesh->AddImpulseAtLocation(Info.Direction * RagdollImpulse, Info.HitLocation, Info.BoneName);
	}
}

void ANoFearCharacter::DisableLegacyCamera()
{
	// GameplayCameraComponent из плагина GameplayCameras — ищем по имени, чтобы не тянуть модуль в Build.cs.
	UClass* GameplayCameraClass = FindObject<UClass>(nullptr, TEXT("/Script/GameplayCameras.GameplayCameraComponent"));
	if (!GameplayCameraClass || !GetComponentByClass(TSubclassOf<UActorComponent>(GameplayCameraClass)))
	{
		return;
	}

	TInlineComponentArray<USpringArmComponent*> SpringArms(this);
	for (USpringArmComponent* SpringArm : SpringArms)
	{
		TArray<USceneComponent*> ArmChildren;
		SpringArm->GetChildrenComponents(/*bIncludeAllDescendants=*/true, ArmChildren);
		for (USceneComponent* Child : ArmChildren)
		{
			if (UCameraComponent* LegacyCamera = Cast<UCameraComponent>(Child))
			{
				LegacyCamera->Deactivate();
			}
		}
	}
}

void ANoFearCharacter::PossessedBy(AController* NewController)
{
	Super::PossessedBy(NewController);

	if (NewController && NewController->IsPlayerController())
	{
		SetTeam(ENoFearTeam::Player);
	}
}

void ANoFearCharacter::NotifyControllerChanged()
{
	Super::NotifyControllerChanged();

	if (WeaponComponent)
	{
		WeaponComponent->HandleControllerChanged();
	}
}

void ANoFearCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);

	if (WeaponComponent)
	{
		WeaponComponent->SetupPlayerInput(PlayerInputComponent);
	}
}
