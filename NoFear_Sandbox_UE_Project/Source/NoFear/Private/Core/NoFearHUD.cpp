#include "Core/NoFearHUD.h"

#include "Camera/PlayerCameraManager.h"
#include "Core/NoFearTeam.h"
#include "Engine/Canvas.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Weapons/NoFearWeapon.h"
#include "Weapons/NoFearWeaponComponent.h"
#include "Weapons/NoFearWeaponData.h"

void ANoFearHUD::DrawHUD()
{
	Super::DrawHUD();

	if (!bShowCrosshair || !Canvas)
	{
		return;
	}

	const APawn* Pawn = GetOwningPawn();
	const UNoFearWeaponComponent* WeaponComponent = Pawn ? Pawn->FindComponentByClass<UNoFearWeaponComponent>() : nullptr;
	ANoFearWeapon* Weapon = WeaponComponent ? WeaponComponent->GetCurrentWeapon() : nullptr;

	// Оружие убрано (или ещё не выдано) — прицел не нужен.
	if (!Weapon || !Weapon->GetWeaponData() || Weapon->GetWeaponState() == ENoFearWeaponState::Holstered)
	{
		CurrentGap = -1.f;
		return;
	}

	DrawCrosshair(Weapon);
}

void ANoFearHUD::DrawCrosshair(ANoFearWeapon* Weapon)
{
	const APlayerController* PC = GetOwningPlayerController();
	if (!PC || !PC->PlayerCameraManager)
	{
		return;
	}

	// Размеры заданы для 1080p; на других разрешениях прицел остаётся того же вида.
	const float UIScale = Canvas->SizeY / 1080.f;

	// Разброс в градусах -> пиксели: сколько пикселей занимает тангенс угла на этом FOV.
	const float HalfFov = FMath::DegreesToRadians(FMath::Clamp(PC->PlayerCameraManager->GetFOVAngle(), 10.f, 170.f) * 0.5f);
	const float PixelsPerTan = Canvas->SizeX * 0.5f / FMath::Tan(HalfFov);
	const float SpreadRad = FMath::DegreesToRadians(Weapon->GetCurrentSpread());
	const float TargetGap = FMath::Clamp(FMath::Tan(SpreadRad) * PixelsPerTan * SpreadScale, MinGap * UIScale, Canvas->SizeY * 0.25f);

	const float DeltaTime = GetWorld() ? GetWorld()->GetDeltaSeconds() : 0.f;
	CurrentGap = CurrentGap < 0.f ? TargetGap : FMath::FInterpTo(CurrentGap, TargetGap, DeltaTime, GapInterpSpeed);

	FLinearColor Color = IsEnemyUnderCrosshair(Weapon) ? EnemyColor : DefaultColor;
	const ENoFearWeaponState State = Weapon->GetWeaponState();
	if (State == ENoFearWeaponState::Reloading || State == ENoFearWeaponState::Equipping)
	{
		Color.A *= BusyOpacity;
	}
	const FLinearColor Outline(0.f, 0.f, 0.f, Color.A * 0.6f);

	const float CenterX = FMath::RoundToFloat(Canvas->SizeX * 0.5f);
	const float CenterY = FMath::RoundToFloat(Canvas->SizeY * 0.5f);
	const float Thickness = FMath::Max(FMath::RoundToFloat(BarThickness * UIScale), 1.f);
	const float Length = FMath::Max(FMath::RoundToFloat(BarLength * UIScale), 1.f);
	const float Gap = FMath::RoundToFloat(CurrentGap);
	const float Border = 1.f;

	// Чёрточка = обводка (чуть больше) + сама чёрточка, чтобы прицел читался на любом фоне.
	auto DrawBar = [&](float X, float Y, float Width, float Height)
	{
		DrawRect(Outline, X - Border, Y - Border, Width + Border * 2.f, Height + Border * 2.f);
		DrawRect(Color, X, Y, Width, Height);
	};

	const float Half = Thickness * 0.5f;
	DrawBar(CenterX - Half, CenterY - Gap - Length, Thickness, Length); // верх
	DrawBar(CenterX - Half, CenterY + Gap, Thickness, Length);          // низ
	DrawBar(CenterX - Gap - Length, CenterY - Half, Length, Thickness); // лево
	DrawBar(CenterX + Gap, CenterY - Half, Length, Thickness);          // право
	DrawBar(CenterX - Half, CenterY - Half, Thickness, Thickness);      // точка в центре
}

bool ANoFearHUD::IsEnemyUnderCrosshair(const ANoFearWeapon* Weapon) const
{
	const APlayerController* PC = GetOwningPlayerController();
	const UNoFearWeaponData* Data = Weapon->GetWeaponData();
	if (!PC || !Data || !GetWorld())
	{
		return false;
	}

	FVector CameraLocation;
	FRotator CameraRotation;
	PC->GetPlayerViewPoint(CameraLocation, CameraRotation);
	const FVector CameraForward = CameraRotation.Vector();

	// Тот же луч, что и у выстрела (ANoFearWeapon::ComputeAim): от уровня персонажа вперёд.
	const AActor* Pawn = PC->GetPawn();
	const FVector Pivot = Pawn ? Pawn->GetActorLocation() : CameraLocation;
	const FVector TraceStart = CameraLocation + CameraForward * FMath::Max(FVector::DotProduct(Pivot - CameraLocation, CameraForward), 0.f);
	const FVector TraceEnd = TraceStart + CameraForward * Data->Range;

	FCollisionQueryParams Params(SCENE_QUERY_STAT(NoFearCrosshair), true, Weapon);
	Params.AddIgnoredActor(Pawn);

	FHitResult Hit;
	if (!GetWorld()->LineTraceSingleByChannel(Hit, TraceStart, TraceEnd, Data->TraceChannel, Params))
	{
		return false;
	}
	return UNoFearTeamLibrary::AreHostile(Pawn, Hit.GetActor());
}
