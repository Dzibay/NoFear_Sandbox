#include "AI/NoFearCombatSubsystem.h"

#include "AI/NoFearCombatComponent.h"
#include "Core/NoFearTeam.h"
#include "Engine/Engine.h"
#include "Engine/HitResult.h"
#include "Engine/World.h"
#include "GameFramework/Controller.h"
#include "GameFramework/Pawn.h"

void UNoFearCombatSubsystem::RegisterCombatant(UNoFearCombatComponent* Combatant)
{
	Combatants.RemoveAll([](const TWeakObjectPtr<UNoFearCombatComponent>& Entry) { return !Entry.IsValid(); });
	Combatants.AddUnique(TWeakObjectPtr<UNoFearCombatComponent>(Combatant));
}

void UNoFearCombatSubsystem::UnregisterCombatant(UNoFearCombatComponent* Combatant)
{
	Combatants.RemoveAll([Combatant](const TWeakObjectPtr<UNoFearCombatComponent>& Entry)
	{
		return !Entry.IsValid() || Entry.Get() == Combatant;
	});
}

TArray<UNoFearCombatComponent*> UNoFearCombatSubsystem::GetCombatants() const
{
	TArray<UNoFearCombatComponent*> Result;
	Result.Reserve(Combatants.Num());
	for (const TWeakObjectPtr<UNoFearCombatComponent>& Entry : Combatants)
	{
		if (UNoFearCombatComponent* Combatant = Entry.Get())
		{
			Result.Add(Combatant);
		}
	}
	return Result;
}

void UNoFearCombatSubsystem::ReportShot(const UObject* WorldContextObject, AActor* Shooter, FVector TraceStart, FVector TraceEnd, const FHitResult& Hit,
	float SuppressionRadius, float SuppressionAmount, float HitBonus)
{
	UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::LogAndReturnNull);
	const UNoFearCombatSubsystem* Subsystem = World ? World->GetSubsystem<UNoFearCombatSubsystem>() : nullptr;
	if (!Subsystem || SuppressionRadius <= 0.f)
	{
		return;
	}

	AActor* ShooterPawn = ResolveShooterPawn(Shooter);

	const FVector SegmentEnd = Hit.bBlockingHit ? FVector(Hit.ImpactPoint) : TraceEnd;
	const AActor* HitActor = Hit.GetActor();

	for (UNoFearCombatComponent* Combatant : Subsystem->GetCombatants())
	{
		AActor* Owner = Combatant->GetOwner();
		if (!Owner || Owner == ShooterPawn)
		{
			continue;
		}
		// Пули своих (по таблице NoFear Teams) не подавляют.
		if (UNoFearTeamLibrary::AreFriendly(ShooterPawn, Owner))
		{
			continue;
		}

		float Amount = 0.f;
		if (HitActor && (HitActor == Owner || HitActor->GetAttachParentActor() == Owner))
		{
			Amount = SuppressionAmount + HitBonus;
		}
		else
		{
			FVector EyesLocation;
			FRotator EyesRotation;
			Owner->GetActorEyesViewPoint(EyesLocation, EyesRotation);

			const float Distance = FMath::Min(
				FMath::PointDistToSegment(EyesLocation, TraceStart, SegmentEnd),
				FMath::PointDistToSegment(Owner->GetActorLocation(), TraceStart, SegmentEnd));
			if (Distance < SuppressionRadius)
			{
				Amount = SuppressionAmount * (1.f - Distance / SuppressionRadius);
			}
		}

		if (Amount > 0.f)
		{
			Combatant->AddSuppression(Amount, ShooterPawn, TraceStart);
		}
	}
}

FVector UNoFearCombatSubsystem::ComputeAIShotEnd(AActor* Shooter, FVector MuzzleLocation, float Range, FVector FallbackAimPoint)
{
	if (UNoFearCombatComponent* Combat = FindCombatComponent(Shooter))
	{
		return Combat->ComputeShotEnd(MuzzleLocation, Range);
	}
	return MuzzleLocation + (FallbackAimPoint - MuzzleLocation).GetSafeNormal() * Range;
}

UNoFearCombatComponent* UNoFearCombatSubsystem::FindCombatComponent(AActor* Actor)
{
	if (!Actor)
	{
		return nullptr;
	}

	if (const AController* Controller = Cast<AController>(Actor))
	{
		Actor = Controller->GetPawn();
		if (!Actor)
		{
			return nullptr;
		}
	}

	if (UNoFearCombatComponent* Combat = Actor->FindComponentByClass<UNoFearCombatComponent>())
	{
		return Combat;
	}

	AActor* ShooterPawn = ResolveShooterPawn(Actor);
	return ShooterPawn && ShooterPawn != Actor ? ShooterPawn->FindComponentByClass<UNoFearCombatComponent>() : nullptr;
}

void UNoFearCombatSubsystem::NotifyActorDied(AActor* DeadActor)
{
	const UWorld* World = DeadActor ? DeadActor->GetWorld() : nullptr;
	const UNoFearCombatSubsystem* Subsystem = World ? World->GetSubsystem<UNoFearCombatSubsystem>() : nullptr;
	if (!Subsystem)
	{
		return;
	}

	// Копия списка: обработчик смерти цели может менять реестр.
	for (UNoFearCombatComponent* Combatant : Subsystem->GetCombatants())
	{
		if (Combatant->GetOwner() != DeadActor)
		{
			Combatant->HandleActorDied(DeadActor);
		}
	}
}

AActor* UNoFearCombatSubsystem::ResolveShooterPawn(AActor* Shooter)
{
	if (!Shooter || Shooter->IsA<APawn>())
	{
		return Shooter;
	}
	if (APawn* InstigatorPawn = Shooter->GetInstigator())
	{
		return InstigatorPawn;
	}
	if (APawn* OwnerPawn = Cast<APawn>(Shooter->GetOwner()))
	{
		return OwnerPawn;
	}
	if (APawn* ParentPawn = Cast<APawn>(Shooter->GetAttachParentActor()))
	{
		return ParentPawn;
	}
	return Shooter;
}
