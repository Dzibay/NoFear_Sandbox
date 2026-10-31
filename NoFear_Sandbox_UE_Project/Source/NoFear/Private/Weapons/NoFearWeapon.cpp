#include "Weapons/NoFearWeapon.h"

#include "AI/NoFearBotSubsystem.h"
#include "AI/NoFearCombatSubsystem.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Camera/PlayerCameraManager.h"
#include "Core/NoFearHealthComponent.h"
#include "Components/DecalComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Curves/CurveFloat.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "NiagaraComponent.h"
#include "NiagaraDataInterfaceArrayFunctionLibrary.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"
#include "Particles/ParticleSystem.h"
#include "Particles/ParticleSystemComponent.h"
#include "Perception/AISense_Hearing.h"
#include "PhysicalMaterials/PhysicalMaterial.h"
#include "TimerManager.h"
#include "Weapons/NoFearWeaponData.h"

namespace NoFearWeapon
{
	/** Скорость, при которой разброс от движения достигает MovingSpread. */
	constexpr float FullMovingSpreadSpeed = 600.f;

	/** Пауза после выстрела, после которой прицел начинает возвращаться. */
	constexpr float RecoilRecoveryDelay = 0.12f;

	/** Встроенная отдача рук (как кривая Recoil_Float в старой версии): пик к 0.05 с, покой к 0.5 с. */
	constexpr float ArmRecoilPeakTime = 0.05f;
	constexpr float ArmRecoilEndTime = 0.5f;

	float DefaultArmRecoil(float Time)
	{
		if (Time <= 0.f || Time >= ArmRecoilEndTime)
		{
			return 0.f;
		}
		if (Time < ArmRecoilPeakTime)
		{
			return 1.f - FMath::Square(1.f - Time / ArmRecoilPeakTime);
		}
		return FMath::Square(1.f - (Time - ArmRecoilPeakTime) / (ArmRecoilEndTime - ArmRecoilPeakTime));
	}

	UFXSystemComponent* SpawnFXAttached(UFXSystemAsset* Asset, USceneComponent* AttachTo, FName Socket)
	{
		if (UNiagaraSystem* Niagara = Cast<UNiagaraSystem>(Asset))
		{
			return UNiagaraFunctionLibrary::SpawnSystemAttached(Niagara, AttachTo, Socket, FVector::ZeroVector, FRotator::ZeroRotator,
				EAttachLocation::SnapToTarget, true);
		}
		if (UParticleSystem* Cascade = Cast<UParticleSystem>(Asset))
		{
			return UGameplayStatics::SpawnEmitterAttached(Cascade, AttachTo, Socket);
		}
		return nullptr;
	}

	UFXSystemComponent* SpawnFXAtLocation(const UObject* WorldContext, UFXSystemAsset* Asset, const FVector& Location, const FRotator& Rotation)
	{
		if (UNiagaraSystem* Niagara = Cast<UNiagaraSystem>(Asset))
		{
			return UNiagaraFunctionLibrary::SpawnSystemAtLocation(WorldContext, Niagara, Location, Rotation);
		}
		if (UParticleSystem* Cascade = Cast<UParticleSystem>(Asset))
		{
			return UGameplayStatics::SpawnEmitterAtLocation(WorldContext, Cascade, Location, Rotation);
		}
		return nullptr;
	}
}

ANoFearWeapon::ANoFearWeapon()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;
	// Отдачу применяем после того, как контроллер обработал ввод мыши.
	PrimaryActorTick.TickGroup = TG_PostPhysics;

	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));

	Mesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("Mesh"));
	Mesh->SetupAttachment(RootComponent);
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Mesh->SetGenerateOverlapEvents(false);
	Mesh->CastShadow = true;
	Mesh->bCastDynamicShadow = true;

	SetActorHiddenInGame(true);
}

void ANoFearWeapon::Initialize(UNoFearWeaponData* InData, ACharacter* InCharacter)
{
	Data = InData;
	Character = InCharacter;
	if (!Data)
	{
		return;
	}

	Mesh->SetSkeletalMesh(Data->Mesh);
	MagazineAmmo = Data->MagazineSize;
	ReserveAmmo = FMath::Min(Data->StartingReserveAmmo, Data->MaxReserveAmmo);
	AttachToHolster();
	BroadcastAmmo();
}

void ANoFearWeapon::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	GetWorldTimerManager().ClearAllTimersForObject(this);
	Super::EndPlay(EndPlayReason);
}

// ---------------------------------------------------------------------------
// Доставание / убирание
// ---------------------------------------------------------------------------

void ANoFearWeapon::Equip(ACharacter* InCharacter)
{
	if (!Data || !InCharacter)
	{
		return;
	}

	Character = InCharacter;
	CancelActions();
	SetActorTickEnabled(true);

	if (Data->EquipGrabTime > KINDA_SMALL_NUMBER)
	{
		// Рука ещё тянется к кобуре: ствол остаётся на ней, а в руку перепрыгивает в момент хвата.
		AttachToHolster();
		GetWorldTimerManager().SetTimer(SocketSwapTimer, this, &ANoFearWeapon::AttachToHand, Data->EquipGrabTime, false);
	}
	else
	{
		AttachToHand();
	}

	SetState(ENoFearWeaponState::Equipping);

	const float MontageLength = PlayCharacterMontage(Data->CharacterEquipMontage);
	PlayTimedSounds(Data->EquipSounds);

	const float Duration = Data->EquipTime > 0.f ? Data->EquipTime : MontageLength;
	if (Duration > KINDA_SMALL_NUMBER)
	{
		GetWorldTimerManager().SetTimer(EquipTimer, this, &ANoFearWeapon::FinishEquip, Duration, false);
	}
	else
	{
		FinishEquip();
	}
	BroadcastAmmo();
}

void ANoFearWeapon::FinishEquip()
{
	if (State == ENoFearWeaponState::Equipping)
	{
		// Если ствол так и не добрался до руки (EquipGrabTime больше времени доставания) — отдаём его сейчас.
		AttachToHand();
		GetWorldTimerManager().ClearTimer(SocketSwapTimer);

		SetState(ENoFearWeaponState::Idle);
		// Если кнопку зажали ещё во время доставания — сразу стреляем.
		HandleFireRequest();
	}
}

float ANoFearWeapon::BeginHolster()
{
	if (!Data || State == ENoFearWeaponState::Holstered)
	{
		return 0.f;
	}

	FTimerManager& Timers = GetWorldTimerManager();
	if (State == ENoFearWeaponState::Holstering)
	{
		return FMath::Max(Timers.GetTimerRemaining(HolsterTimer), 0.f);
	}

	UAnimMontage* HolsterMontage = Data->CharacterHolsterMontage;
	UAnimMontage* EquipMontage = Data->CharacterEquipMontage;
	// Нет монтажа убирания — играем доставание задом наперёд.
	const bool bReversed = !HolsterMontage && EquipMontage;
	UAnimMontage* Montage = HolsterMontage ? HolsterMontage : EquipMontage;
	UAnimInstance* AnimInstance = GetCharacterAnimInstance();

	// Если доставание ещё не доиграло, убираем с того места, где оно остановилось, иначе поза скачет.
	const float Length = Montage ? Montage->GetPlayLength() : 0.f;
	float Fraction = 1.f;
	if (bReversed && Length > KINDA_SMALL_NUMBER && State == ENoFearWeaponState::Equipping && AnimInstance
		&& AnimInstance->Montage_IsPlaying(EquipMontage))
	{
		Fraction = FMath::Clamp(AnimInstance->Montage_GetPosition(EquipMontage) / Length, 0.f, 1.f);
	}

	CancelActions();

	// HolsterTime растягивает или сжимает весь монтаж; Fraction укорачивает убирание недоигранного доставания.
	const float FullDuration = Data->HolsterTime > 0.f ? Data->HolsterTime : Length;
	const float Duration = FullDuration * Fraction;

	PlayTimedSounds(Data->HolsterSounds);

	if (Duration <= 0.05f)
	{
		Holster();
		return 0.f;
	}

	if (Montage && AnimInstance && Length > KINDA_SMALL_NUMBER)
	{
		const float PlayRate = Length / FullDuration;
		if (bReversed)
		{
			AnimInstance->Montage_Play(Montage, -PlayRate, EMontagePlayReturnType::MontageLength, Length * Fraction);
		}
		else
		{
			AnimInstance->Montage_Play(Montage, PlayRate);
		}
	}

	const float StowTime = Data->HolsterStowTime * Fraction;
	if (StowTime > KINDA_SMALL_NUMBER && StowTime < Duration)
	{
		Timers.SetTimer(SocketSwapTimer, this, &ANoFearWeapon::AttachToHolster, StowTime, false);
	}

	SetState(ENoFearWeaponState::Holstering);
	Timers.SetTimer(HolsterTimer, this, &ANoFearWeapon::Holster, Duration, false);
	return Duration;
}

void ANoFearWeapon::Holster()
{
	CancelActions();

	if (Data)
	{
		StopCharacterMontage(Data->CharacterEquipMontage);
		StopCharacterMontage(Data->CharacterHolsterMontage);
	}

	AttachToHolster();
	SetActorTickEnabled(false);
	SetState(ENoFearWeaponState::Holstered);
}

void ANoFearWeapon::Abandon()
{
	CancelActions();

	if (Data)
	{
		StopCharacterMontage(Data->CharacterEquipMontage);
		StopCharacterMontage(Data->CharacterHolsterMontage);
	}

	SetActorTickEnabled(false);
	// Holstered: из этого состояния оружие не стреляет и не перезаряжается, что бы ни вызвали.
	SetState(ENoFearWeaponState::Holstered);
}

float ANoFearWeapon::GetReloadProgress() const
{
	if (State != ENoFearWeaponState::Reloading || !GetWorld())
	{
		return 0.f;
	}

	const FTimerManager& Timers = GetWorldTimerManager();
	const float Rate = Timers.GetTimerRate(ReloadTimer);
	return Rate > KINDA_SMALL_NUMBER ? FMath::Clamp(Timers.GetTimerElapsed(ReloadTimer) / Rate, 0.f, 1.f) : 0.f;
}

void ANoFearWeapon::CancelActions()
{
	ClearFireRequest();
	FTimerManager& Timers = GetWorldTimerManager();
	Timers.ClearTimer(RefireTimer);
	Timers.ClearTimer(ReloadTimer);
	Timers.ClearTimer(EquipTimer);
	Timers.ClearTimer(HolsterTimer);
	Timers.ClearTimer(SocketSwapTimer);
	ClearTimedSounds();

	if (Data)
	{
		StopCharacterMontage(Data->CharacterReloadMontage);
		StopCharacterMontage(Data->CharacterFireMontage);
	}

	PendingRecoil = FRotator::ZeroRotator;
	RecoilToRecover = FRotator::ZeroRotator;
	Bloom = 0.f;
	bAiming = false;
	ArmKickTime = -100.0;

	// Иначе ствол повиснет на спине в позе недоигранной перезарядки (без магазина).
	if (Mesh->GetSingleNodeInstance())
	{
		Mesh->SetAnimation(nullptr);
	}
}

void ANoFearWeapon::AttachToHand()
{
	if (!Data || !Character)
	{
		return;
	}
	AttachToComponent(Character->GetMesh(), FAttachmentTransformRules::SnapToTargetNotIncludingScale, Data->AttachSocket);
	Mesh->SetRelativeTransform(Data->AttachOffset);
	SetActorHiddenInGame(false);
}

void ANoFearWeapon::AttachToHolster()
{
	USkeletalMeshComponent* CharacterMesh = Character ? Character->GetMesh() : nullptr;
	if (Data && CharacterMesh && !Data->HolsterSocket.IsNone() && CharacterMesh->DoesSocketExist(Data->HolsterSocket))
	{
		AttachToComponent(CharacterMesh, FAttachmentTransformRules::SnapToTargetNotIncludingScale, Data->HolsterSocket);
		Mesh->SetRelativeTransform(Data->HolsterOffset);
		SetActorHiddenInGame(false);
	}
	else
	{
		SetActorHiddenInGame(true);
	}
}

void ANoFearWeapon::PlayTimedSounds(const TArray<FNoFearTimedSound>& Sounds)
{
	FTimerManager& Timers = GetWorldTimerManager();
	for (const FNoFearTimedSound& Entry : Sounds)
	{
		if (!Entry.Sound)
		{
			continue;
		}
		if (Entry.Time <= KINDA_SMALL_NUMBER)
		{
			PlayHandlingSound(Entry);
			continue;
		}
		FTimerHandle& Handle = SoundTimers.AddDefaulted_GetRef();
		Timers.SetTimer(Handle, FTimerDelegate::CreateUObject(this, &ANoFearWeapon::PlayHandlingSound, Entry), Entry.Time, false);
	}
}

void ANoFearWeapon::PlayHandlingSound(FNoFearTimedSound Sound)
{
	if (!Sound.Sound || !Data)
	{
		return;
	}
	UGameplayStatics::SpawnSoundAttached(Sound.Sound, Mesh, NAME_None, FVector::ZeroVector, EAttachLocation::KeepRelativeOffset,
		false, Sound.VolumeMultiplier, 1.f, 0.f, Data->HandlingAttenuation);
}

void ANoFearWeapon::ClearTimedSounds()
{
	FTimerManager& Timers = GetWorldTimerManager();
	for (FTimerHandle& Handle : SoundTimers)
	{
		Timers.ClearTimer(Handle);
	}
	SoundTimers.Reset();
}

// ---------------------------------------------------------------------------
// Стрельба
// ---------------------------------------------------------------------------

void ANoFearWeapon::StartFire()
{
	if (!Data)
	{
		return;
	}

	bTriggerHeld = true;
	switch (Data->FireMode)
	{
	case ENoFearFireMode::Single:
		bSingleShotQueued = true;
		break;
	case ENoFearFireMode::Burst:
		if (BurstShotsRemaining <= 0)
		{
			BurstShotsRemaining = Data->BurstCount;
		}
		break;
	case ENoFearFireMode::Auto:
		break;
	}

	HandleFireRequest();
}

void ANoFearWeapon::StopFire()
{
	bTriggerHeld = false;

	// Очередь (Burst) достреливается сама; для остальных режимов — сразу гасим таймер.
	if (!WantsToFire())
	{
		GetWorldTimerManager().ClearTimer(RefireTimer);
	}
}

bool ANoFearWeapon::WantsToFire() const
{
	if (!Data)
	{
		return false;
	}
	switch (Data->FireMode)
	{
	case ENoFearFireMode::Single:
		return bSingleShotQueued;
	case ENoFearFireMode::Burst:
		return BurstShotsRemaining > 0;
	case ENoFearFireMode::Auto:
		return bTriggerHeld;
	}
	return false;
}

void ANoFearWeapon::ClearFireRequest()
{
	bTriggerHeld = false;
	bSingleShotQueued = false;
	BurstShotsRemaining = 0;
	GetWorldTimerManager().ClearTimer(RefireTimer);
}

void ANoFearWeapon::HandleFireRequest()
{
	FTimerManager& Timers = GetWorldTimerManager();
	Timers.ClearTimer(RefireTimer);

	if (!WantsToFire() || State != ENoFearWeaponState::Idle)
	{
		// В Equipping/Reloading запрос не сбрасываем: FinishEquip/FinishReload вызовут нас снова.
		return;
	}

	const double Now = GetWorld()->GetTimeSeconds();
	if (Now < NextFireTime)
	{
		Timers.SetTimer(RefireTimer, this, &ANoFearWeapon::HandleFireRequest, FMath::Max(NextFireTime - Now, 0.001), false);
		return;
	}

	if (MagazineAmmo <= 0)
	{
		bSingleShotQueued = false;
		BurstShotsRemaining = 0;
		if (Data->DryFireSound)
		{
			UGameplayStatics::PlaySoundAtLocation(this, Data->DryFireSound, GetMuzzleLocation());
		}
		NextFireTime = Now + 0.25;
		if (Data->bAutoReload)
		{
			Reload();
		}
		return;
	}

	FireShot();
	--MagazineAmmo;
	BroadcastAmmo();

	// Держим ровный темп: если таймер сработал на кадр позже, следующий выстрел не сдвигается.
	const double Interval = Data->GetFireInterval();
	NextFireTime = (Now - NextFireTime < Interval) ? NextFireTime + Interval : Now + Interval;
	LastFireTime = Now;

	if (Data->FireMode == ENoFearFireMode::Single)
	{
		bSingleShotQueued = false;
	}
	else if (Data->FireMode == ENoFearFireMode::Burst)
	{
		--BurstShotsRemaining;
	}

	if (WantsToFire())
	{
		Timers.SetTimer(RefireTimer, this, &ANoFearWeapon::HandleFireRequest, FMath::Max(NextFireTime - Now, 0.001), false);
	}
}

void ANoFearWeapon::FireShot()
{
	FVector Start;
	FVector AimDirection;
	if (!ComputeAim(Start, AimDirection))
	{
		return;
	}

	const bool bPlayer = IsPlayerControlled();
	// У ботов точность считает UNoFearCombatComponent, у игрока — разброс оружия.
	const FVector ShotDirection = bPlayer
		? FMath::VRandCone(AimDirection, FMath::DegreesToRadians(GetCurrentSpread()))
		: AimDirection;

	const int32 Pellets = FMath::Max(Data->PelletsPerShot, 1);
	TArray<FVector> BulletEnds;
	BulletEnds.Reserve(Pellets);
	for (int32 Index = 0; Index < Pellets; ++Index)
	{
		const FVector PelletDirection = Pellets > 1
			? FMath::VRandCone(ShotDirection, FMath::DegreesToRadians(Data->PelletSpreadDegrees))
			: ShotDirection;
		BulletEnds.Add(FirePellet(Start, PelletDirection, Pellets));
	}

	PlayFireEffects(Start);
	SpawnTracer(Start, BulletEnds);

	APawn* InstigatorPawn = GetInstigator();
	UAISense_Hearing::ReportNoiseEvent(this, Start, Data->NoiseLoudness, InstigatorPawn ? static_cast<AActor*>(InstigatorPawn) : this,
		Data->NoiseRange, TEXT("Gunshot"));
	// Мирные: далёкий выстрел настораживает, близкий и пуля мимо — паника.
	UNoFearBotSubsystem::ReportDanger(this, ENoFearDangerType::Gunshot, InstigatorPawn, Start, Data->NoiseRange, Start);
	for (const FVector& BulletEnd : BulletEnds)
	{
		UNoFearBotSubsystem::ReportBulletPath(this, InstigatorPawn, Start, BulletEnd);
	}

	Bloom = FMath::Min(Bloom + Data->SpreadPerShot, Data->MaxBloom);
	KickArms();
	if (bPlayer)
	{
		AddRecoil();
	}

	OnFired.Broadcast();
}

FVector ANoFearWeapon::FirePellet(const FVector& Start, const FVector& Direction, int32 PelletCount)
{
	UWorld* World = GetWorld();
	const FVector End = Start + Direction * Data->Range;

	FCollisionQueryParams Params(SCENE_QUERY_STAT(NoFearWeaponShot), true, this);
	Params.AddIgnoredActor(GetOwner());
	Params.bReturnPhysicalMaterial = true;

	FHitResult Hit;
	const bool bHit = World->LineTraceSingleByChannel(Hit, Start, End, Data->TraceChannel, Params);
	const FVector BulletEnd = bHit ? Hit.ImpactPoint : End;

	if (bHit)
	{
		if (AActor* HitActor = Hit.GetActor())
		{
			const float Multiplier = FMath::GetMappedRangeValueClamped(
				FVector2D(Data->FalloffStart, FMath::Max(Data->FalloffEnd, Data->FalloffStart + 1.f)),
				FVector2D(1.f, Data->MinDamageMultiplier), Hit.Distance);
			UGameplayStatics::ApplyPointDamage(HitActor, Data->Damage * Multiplier, Direction, Hit,
				GetInstigatorController(), this, Data->DamageType);
		}
		PlayImpactEffects(Hit, Start);
	}

	UNoFearCombatSubsystem::ReportShot(this, GetOwner(), Start, BulletEnd, Hit, Data->SuppressionRadius,
		Data->SuppressionAmount / PelletCount);

	return BulletEnd;
}

void ANoFearWeapon::SpawnTracer(const FVector& Start, const TArray<FVector>& BulletEnds)
{
	if (!Data->Tracer || BulletEnds.Num() == 0)
	{
		return;
	}

	const FRotator Rotation = (BulletEnds[0] - Start).Rotation();
	UFXSystemComponent* Tracer = NoFearWeapon::SpawnFXAtLocation(this, Data->Tracer, Start, Rotation);
	if (!Tracer)
	{
		return;
	}

	// Обычный трассер: одна точка конца луча.
	Tracer->SetVectorParameter(Data->TracerEndParameter, BulletEnds[0]);

	// Трассеры из Lyra (NS_WeaponFire_Tracer) ждут массив точек попадания и позицию дула.
	if (UNiagaraComponent* Niagara = Cast<UNiagaraComponent>(Tracer))
	{
		UNiagaraDataInterfaceArrayFunctionLibrary::SetNiagaraArrayVector(Niagara, TEXT("User.ImpactPositions"), BulletEnds);
		Niagara->SetVariableVec3(TEXT("User.MuzzlePosition"), Start);
		Niagara->SetVariableInt(TEXT("User.NumberOfHits"), BulletEnds.Num());
	}
}

bool ANoFearWeapon::ComputeAim(FVector& OutStart, FVector& OutDirection) const
{
	if (!Data)
	{
		return false;
	}

	OutStart = GetMuzzleLocation();

	if (APlayerController* PC = GetOwnerPlayerController())
	{
		// Целимся туда, куда смотрит центр экрана, а летит пуля из дула.
		FVector CameraLocation;
		FRotator CameraRotation;
		PC->GetPlayerViewPoint(CameraLocation, CameraRotation);
		const FVector CameraForward = CameraRotation.Vector();

		// Начинаем луч не от камеры, а на уровне персонажа, чтобы не попадать в предметы за спиной.
		const FVector Pivot = GetOwner() ? GetOwner()->GetActorLocation() : OutStart;
		const FVector TraceStart = CameraLocation + CameraForward * FMath::Max(FVector::DotProduct(Pivot - CameraLocation, CameraForward), 0.f);
		const FVector TraceEnd = TraceStart + CameraForward * Data->Range;

		FCollisionQueryParams Params(SCENE_QUERY_STAT(NoFearWeaponAim), true, this);
		Params.AddIgnoredActor(GetOwner());

		FHitResult Hit;
		const FVector AimPoint = GetWorld()->LineTraceSingleByChannel(Hit, TraceStart, TraceEnd, Data->TraceChannel, Params)
			? Hit.ImpactPoint
			: TraceEnd;

		OutDirection = (AimPoint - OutStart).GetSafeNormal();
		// Точка прицела оказалась позади дула (упёрлись в стену) — стреляем по взгляду.
		if (FVector::DotProduct(OutDirection, CameraForward) < 0.2f)
		{
			OutDirection = CameraForward;
		}
		return true;
	}

	const AActor* OwnerActor = GetOwner();
	const FRotator AimRotation = OwnerActor && Cast<APawn>(OwnerActor) ? Cast<APawn>(OwnerActor)->GetBaseAimRotation() : GetActorRotation();
	const FVector Fallback = OutStart + AimRotation.Vector() * Data->Range;
	const FVector ShotEnd = UNoFearCombatSubsystem::ComputeAIShotEnd(GetOwner(), OutStart, Data->Range, Fallback);
	OutDirection = (ShotEnd - OutStart).GetSafeNormal();
	return !OutDirection.IsNearlyZero();
}

// ---------------------------------------------------------------------------
// Эффекты
// ---------------------------------------------------------------------------

void ANoFearWeapon::PlayFireEffects(const FVector& MuzzleLocation)
{
	if (Data->FireSound)
	{
		UGameplayStatics::PlaySoundAtLocation(this, Data->FireSound, MuzzleLocation);
	}
	if (Data->MuzzleFlash)
	{
		NoFearWeapon::SpawnFXAttached(Data->MuzzleFlash, Mesh, Data->MuzzleSocket);
	}
	if (Data->WeaponFireAnimation)
	{
		Mesh->PlayAnimation(Data->WeaponFireAnimation, false);
	}
	PlayCharacterMontage(Data->CharacterFireMontage);

	if (Data->FireCameraShake)
	{
		if (APlayerController* PC = GetOwnerPlayerController())
		{
			PC->ClientStartCameraShake(Data->FireCameraShake, bAiming ? Data->AimRecoilMultiplier : 1.f);
		}
	}
}

void ANoFearWeapon::PlayImpactEffects(const FHitResult& Hit, const FVector& TraceStart)
{
	const EPhysicalSurface Surface = UPhysicalMaterial::DetermineSurfaceType(Hit.PhysMaterial.Get());
	const FNoFearImpactEffect& Impact = Data->GetImpactEffect(Surface);

	const FRotator NormalRotation = Hit.ImpactNormal.Rotation();
	if (Impact.Effect)
	{
		NoFearWeapon::SpawnFXAtLocation(this, Impact.Effect, Hit.ImpactPoint, NormalRotation);
	}
	if (Impact.Sound)
	{
		UGameplayStatics::PlaySoundAtLocation(this, Impact.Sound, Hit.ImpactPoint);
	}
	if (Impact.Decal && Hit.GetComponent())
	{
		FRotator DecalRotation = (-Hit.ImpactNormal).Rotation();
		DecalRotation.Roll = FMath::FRandRange(-180.f, 180.f);
		if (UDecalComponent* Decal = UGameplayStatics::SpawnDecalAttached(Impact.Decal, Impact.DecalSize, Hit.GetComponent(), Hit.BoneName,
			Hit.ImpactPoint, DecalRotation, EAttachLocation::KeepWorldPosition, Impact.DecalLifeSpan))
		{
			Decal->SetFadeScreenSize(0.001f);
		}
	}
}

// ---------------------------------------------------------------------------
// Разброс и отдача
// ---------------------------------------------------------------------------

float ANoFearWeapon::GetCurrentSpread() const
{
	if (!Data)
	{
		return 0.f;
	}

	float Spread = (bAiming ? Data->AimSpread : Data->HipSpread) + Bloom;
	if (Character)
	{
		const float Speed = Character->GetVelocity().Size2D();
		Spread += Data->MovingSpread * FMath::Clamp(Speed / NoFearWeapon::FullMovingSpreadSpeed, 0.f, 1.f);

		const UCharacterMovementComponent* Movement = Character->GetCharacterMovement();
		if (Movement && Movement->IsFalling())
		{
			Spread += Data->AirSpread;
		}

		// Раненый лёжа стреляет с дрожащих рук.
		if (UNoFearHealthComponent::IsActorDowned(Character))
		{
			Spread = Spread * 2.f + 2.5f;
		}
	}
	return Spread;
}

void ANoFearWeapon::AddRecoil()
{
	const float Multiplier = bAiming ? Data->AimRecoilMultiplier : 1.f;
	const FRotator Kick(
		FMath::FRandRange(Data->RecoilPitch.X, Data->RecoilPitch.Y) * Multiplier,
		FMath::FRandRange(-Data->RecoilYaw, Data->RecoilYaw) * Multiplier,
		0.f);

	PendingRecoil += Kick;
	RecoilToRecover += Kick * Data->RecoilRecovery;
}

void ANoFearWeapon::KickArms()
{
	ArmKickFromAlpha = GetArmRecoilAlpha();
	ArmKickTime = GetWorld()->GetTimeSeconds();
	ArmKickScale = Data->ArmRecoilScale * (bAiming ? Data->AimRecoilMultiplier : 1.f);
}

float ANoFearWeapon::GetArmRecoilAlpha() const
{
	if (!Data || State == ENoFearWeaponState::Holstered)
	{
		return 0.f;
	}

	const float Elapsed = static_cast<float>(GetWorld()->GetTimeSeconds() - ArmKickTime);
	const float Kick = ArmKickScale * (Data->ArmRecoilCurve
		? Data->ArmRecoilCurve->GetFloatValue(Elapsed)
		: NoFearWeapon::DefaultArmRecoil(Elapsed));

	// Новый выстрел не сбрасывает руку в ноль: прежняя отдача гаснет, пока новая набирает силу.
	const float Carry = ArmKickFromAlpha * FMath::Clamp(1.f - Elapsed / NoFearWeapon::ArmRecoilPeakTime, 0.f, 1.f);
	return FMath::Max(Kick, Carry);
}

void ANoFearWeapon::ApplyRecoil(float DeltaSeconds)
{
	APlayerController* PC = GetOwnerPlayerController();
	if (!PC)
	{
		PendingRecoil = RecoilToRecover = FRotator::ZeroRotator;
		return;
	}

	FRotator Step = FRotator::ZeroRotator;
	if (!PendingRecoil.IsNearlyZero(0.001f))
	{
		Step = PendingRecoil * FMath::Min(Data->RecoilKickSpeed * DeltaSeconds, 1.f);
		PendingRecoil -= Step;
	}
	else if (!RecoilToRecover.IsNearlyZero(0.001f)
		&& !bTriggerHeld
		&& GetWorld()->GetTimeSeconds() - LastFireTime > NoFearWeapon::RecoilRecoveryDelay)
	{
		const FRotator Back = RecoilToRecover * FMath::Min(Data->RecoilRecoverySpeed * DeltaSeconds, 1.f);
		RecoilToRecover -= Back;
		Step = Back * -1.f;
	}

	if (Step.IsNearlyZero(0.0001f))
	{
		return;
	}

	FRotator Control = PC->GetControlRotation();
	float Pitch = FRotator::NormalizeAxis(Control.Pitch + Step.Pitch);
	if (const APlayerCameraManager* Camera = PC->PlayerCameraManager)
	{
		Pitch = FMath::Clamp(Pitch, Camera->ViewPitchMin, Camera->ViewPitchMax);
	}
	Control.Pitch = Pitch;
	Control.Yaw += Step.Yaw;
	PC->SetControlRotation(Control);
}

void ANoFearWeapon::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (!Data)
	{
		return;
	}

	if (GetWorld()->GetTimeSeconds() - LastFireTime > Data->GetFireInterval())
	{
		Bloom = FMath::Max(Bloom - Data->BloomRecoveryRate * DeltaSeconds, 0.f);
	}
	ApplyRecoil(DeltaSeconds);
}

void ANoFearWeapon::SetAiming(bool bNewAiming)
{
	bAiming = bNewAiming && State != ENoFearWeaponState::Holstered && State != ENoFearWeaponState::Holstering;
}

// ---------------------------------------------------------------------------
// Перезарядка и патроны
// ---------------------------------------------------------------------------

bool ANoFearWeapon::Reload()
{
	if (!Data || State != ENoFearWeaponState::Idle || MagazineAmmo >= Data->MagazineSize || ReserveAmmo <= 0)
	{
		return false;
	}
	// Раненый достреливает то, что в магазине.
	if (UNoFearHealthComponent::IsActorDowned(Character))
	{
		return false;
	}

	GetWorldTimerManager().ClearTimer(RefireTimer);
	// Режимы Single/Burst: запрос, сделанный до перезарядки, не переносим на после неё.
	bSingleShotQueued = false;
	BurstShotsRemaining = 0;

	SetState(ENoFearWeaponState::Reloading);

	const float MontageLength = PlayCharacterMontage(Data->CharacterReloadMontage);
	if (Data->WeaponReloadAnimation)
	{
		Mesh->PlayAnimation(Data->WeaponReloadAnimation, false);
	}
	ClearTimedSounds();
	PlayTimedSounds(Data->ReloadSounds);

	float Duration = Data->ReloadTime > 0.f ? Data->ReloadTime : MontageLength;
	if (Duration <= KINDA_SMALL_NUMBER)
	{
		Duration = 2.f;
	}
	GetWorldTimerManager().SetTimer(ReloadTimer, this, &ANoFearWeapon::FinishReload, Duration, false);
	return true;
}

void ANoFearWeapon::FinishReload()
{
	if (State != ENoFearWeaponState::Reloading)
	{
		return;
	}

	const int32 Needed = Data->MagazineSize - MagazineAmmo;
	const int32 Taken = FMath::Min(Needed, ReserveAmmo);
	MagazineAmmo += Taken;
	ReserveAmmo -= Taken;
	BroadcastAmmo();

	SetState(ENoFearWeaponState::Idle);
	// Автомат с зажатой кнопкой продолжает стрелять после перезарядки.
	HandleFireRequest();
}

int32 ANoFearWeapon::AddReserveAmmo(int32 Amount)
{
	if (!Data || Amount <= 0)
	{
		return 0;
	}
	const int32 Taken = FMath::Min(Amount, Data->MaxReserveAmmo - ReserveAmmo);
	if (Taken > 0)
	{
		ReserveAmmo += Taken;
		BroadcastAmmo();
	}
	return FMath::Max(Taken, 0);
}

// ---------------------------------------------------------------------------
// Вспомогательное
// ---------------------------------------------------------------------------

void ANoFearWeapon::SetState(ENoFearWeaponState NewState)
{
	if (State != NewState)
	{
		State = NewState;
		OnStateChanged.Broadcast(State);
	}
}

void ANoFearWeapon::BroadcastAmmo()
{
	OnAmmoChanged.Broadcast(MagazineAmmo, ReserveAmmo);
}

FVector ANoFearWeapon::GetMuzzleLocation() const
{
	if (Data && Mesh->DoesSocketExist(Data->MuzzleSocket))
	{
		return Mesh->GetSocketLocation(Data->MuzzleSocket);
	}
	return Mesh->GetComponentLocation();
}

UAnimInstance* ANoFearWeapon::GetCharacterAnimInstance() const
{
	return Character && Character->GetMesh() ? Character->GetMesh()->GetAnimInstance() : nullptr;
}

float ANoFearWeapon::PlayCharacterMontage(UAnimMontage* Montage) const
{
	UAnimInstance* AnimInstance = Montage ? GetCharacterAnimInstance() : nullptr;
	return AnimInstance ? AnimInstance->Montage_Play(Montage) : 0.f;
}

void ANoFearWeapon::StopCharacterMontage(UAnimMontage* Montage) const
{
	if (UAnimInstance* AnimInstance = Montage ? GetCharacterAnimInstance() : nullptr)
	{
		AnimInstance->Montage_Stop(0.2f, Montage);
	}
}

bool ANoFearWeapon::IsPlayerControlled() const
{
	return GetOwnerPlayerController() != nullptr;
}

APlayerController* ANoFearWeapon::GetOwnerPlayerController() const
{
	const APawn* OwnerPawn = Cast<APawn>(GetOwner());
	return OwnerPawn ? Cast<APlayerController>(OwnerPawn->GetController()) : nullptr;
}
