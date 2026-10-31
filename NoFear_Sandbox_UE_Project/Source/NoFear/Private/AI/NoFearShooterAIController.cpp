#include "AI/NoFearShooterAIController.h"

#include "AI/NoFearBotSubsystem.h"
#include "AI/NoFearCombatComponent.h"
#include "BehaviorTree/BehaviorTree.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "Core/NoFearHealthComponent.h"
#include "NavigationSystem.h"
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
	ReleaseCover();
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
		UpdateCover();
		TryThrowGrenade();
		WriteBlackboard();
	}
	else if (HasCover())
	{
		ReleaseCover();
	}
}

void ANoFearShooterAIController::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	ReleaseCover();
	Super::EndPlay(EndPlayReason);
}

void ANoFearShooterAIController::WriteBlackboard()
{
	Super::WriteBlackboard();
	UBlackboardComponent* BlackboardComp = GetBlackboardComponent();
	if (!BlackboardComp)
	{
		return;
	}
	if (HasCover())
	{
		BlackboardComp->SetValueAsVector(FNoFearBotKeys::CoverLocation, Cover.Location);
	}
	else
	{
		BlackboardComp->ClearValue(FNoFearBotKeys::CoverLocation);
	}
	BlackboardComp->SetValueAsBool(FNoFearBotKeys::InCover, IsInCover());
	BlackboardComp->SetValueAsBool(FNoFearBotKeys::CoverIsLow, IsCoverLow());
	BlackboardComp->SetValueAsBool(FNoFearBotKeys::IsRetreating, bRetreating);
	BlackboardComp->SetValueAsBool(FNoFearBotKeys::InWeaponRange, IsTargetInWeaponRange());
}

bool ANoFearShooterAIController::IsInCover() const
{
	const APawn* MyPawn = GetPawn();
	return HasCover() && MyPawn && FVector::Dist2D(MyPawn->GetActorLocation(), Cover.Location) < 60.f;
}

bool ANoFearShooterAIController::IsCoveringFire() const
{
	return GetNow() < CoveringFireUntil;
}

FVector2D ANoFearShooterAIController::GetPreferredRange() const
{
	const UNoFearWeaponComponent* Weapons = GetWeaponComponent();
	const ANoFearWeapon* Weapon = Weapons ? Weapons->GetCurrentWeapon() : nullptr;
	const UNoFearWeaponData* Data = Weapon ? Weapon->GetWeaponData() : nullptr;
	return Data ? Data->GetBotPreferredRange() : FVector2D(800.f, 2000.f);
}

bool ANoFearShooterAIController::IsTargetInWeaponRange() const
{
	const APawn* MyPawn = GetPawn();
	if (!MyPawn || !GetCurrentTarget())
	{
		return false;
	}
	// Чуть дальше удобной дистанции ещё стреляют, чтобы не бегать туда-сюда на границе.
	return FVector::Dist2D(MyPawn->GetActorLocation(), GetLastKnownTargetLocation()) <= GetPreferredRange().Y * 1.25f + 100.f;
}

void ANoFearShooterAIController::RequestCoverRefresh()
{
	bCoverRefreshRequested = true;
	NextCoverCheckTime = 0.0;
}

void ANoFearShooterAIController::ReleaseCover()
{
	if (UNoFearBotSubsystem* Bots = UNoFearBotSubsystem::Get(this))
	{
		if (AActor* Reserved = CoverOwner.Get())
		{
			Bots->ReleaseCover(Reserved);
		}
	}
	Cover = FNoFearCoverPoint();
	CoverOwner.Reset();
	CoverLostChecks = 0;
	CoverPhase.Reset();
}

bool ANoFearShooterAIController::SearchCover(bool bRetreat)
{
	APawn* MyPawn = GetPawn();
	AActor* Target = GetCurrentTarget();
	if (!MyPawn || !Target)
	{
		return false;
	}
	const FVector Threat = GetLastKnownTargetLocation();
	const FVector2D Range = GetPreferredRange();

	FNoFearCoverSearch Search;
	Search.Pawn = MyPawn;
	Search.ThreatLocation = Threat;
	Search.ThreatActor = Target;
	Search.SearchRadius = bRetreat ? RetreatSearchRadius : CoverSearchRadius;
	Search.MinThreatDistance = MinCoverThreatDistance;
	Search.PreferredRange = Range;
	Search.bRetreat = bRetreat;
	Search.RetreatGain = RetreatGain;
	Search.MaxObstacleDistance = CoverMaxObstacleDistance;
	Search.ReserveRadius = CoverReserveRadius;
	Search.LeashAnchor = GetLeashAnchor();
	Search.LeashRadius = GetLeashRadius() > 0.f && bRetreat ? GetLeashRadius() * 1.5f : GetLeashRadius();
	if (!bRetreat)
	{
		// Не дальше, чем бьёт оружие: дробовик ищет укрытие рядом с целью.
		Search.MaxThreatDistance = Range.Y * 1.25f + 200.f;
		if (GetNow() < AdvanceUntil)
		{
			// Цель давно не видна: только укрытия ближе к ней.
			const float Current = FVector::Dist2D(MyPawn->GetActorLocation(), Threat);
			Search.MaxThreatDistance = FMath::Min(Search.MaxThreatDistance, FMath::Max(Current - 300.f, MinCoverThreatDistance + 50.f));
		}
	}

	FNoFearCoverPoint Found;
	if (!UNoFearCoverLibrary::FindCover(Search, Found))
	{
		return false;
	}
	ReleaseCover();
	Cover = Found;
	CoverOwner = MyPawn;
	if (UNoFearBotSubsystem* Bots = UNoFearBotSubsystem::Get(this))
	{
		Bots->ReserveCover(MyPawn, Found.Location);
	}
	return true;
}

void ANoFearShooterAIController::UpdateCover()
{
	APawn* MyPawn = GetPawn();
	AActor* Target = GetCurrentTarget();
	const double Now = GetNow();
	if (!bUseCover || !MyPawn || !Target || GetAlertState() != ENoFearAlertState::Combat || IsEvadingGrenade())
	{
		if (HasCover())
		{
			ReleaseCover();
		}
		if (!Target)
		{
			bRetreating = false;
			AdvanceUntil = -1.0;
		}
		return;
	}

	// Раненый: в дальнее укрытие, своих — прикрыть.
	const UNoFearHealthComponent* Health = UNoFearHealthComponent::FindHealth(MyPawn);
	const bool bWounded = RetreatHealthFraction > 0.f && Health && Health->GetHealthFraction() < RetreatHealthFraction;
	if (bWounded && !bRetreating && Now >= NextRetreatTime)
	{
		NextRetreatTime = Now + 5.0;
		if (SearchCover(true))
		{
			bRetreating = true;
			Shout(RetreatShout, 2.f);
			RequestCoveringFire(Target);
			NextCoverCheckTime = Now + FMath::FRandRange(CoverSearchInterval.X, CoverSearchInterval.Y);
			return;
		}
	}

	if (Now < NextCoverCheckTime)
	{
		return;
	}
	NextCoverCheckTime = Now + FMath::FRandRange(CoverSearchInterval.X, FMath::Max(CoverSearchInterval.X, CoverSearchInterval.Y));

	const FVector Threat = GetLastKnownTargetLocation();
	if (HasCover())
	{
		bool bKeep = true;
		const UNoFearCombatComponent* Combat = GetCombatComponent();
		const UWorld* World = GetWorld();
		if (FVector::Dist2D(Cover.Location, Threat) < 0.8f * MinCoverThreatDistance)
		{
			// Угроза подошла вплотную.
			bKeep = false;
		}
		else if (UNoFearCoverLibrary::EvaluateCover(World, Cover.Location, Threat + FVector(0.f, 0.f, UNoFearCoverLibrary::ThreatEyeHeight),
			UNoFearCoverLibrary::MakeCoverTraceParams(World), ECC_Visibility) != Cover.Type)
		{
			// Угроза обошла (или укрытие стало другим): две проверки подряд — искать заново.
			bKeep = ++CoverLostChecks < 2;
		}
		else
		{
			CoverLostChecks = 0;
		}
		if (bKeep && !bRetreating && IsInCover() && Combat && Combat->GetTimeSinceTargetSeen() > AdvanceAfterUnseenTime)
		{
			// Цель давно не видна: подойти ближе.
			bKeep = false;
			AdvanceUntil = Now + 5.0;
		}
		if (bKeep && !bCoverRefreshRequested)
		{
			return;
		}
		bCoverRefreshRequested = false;
		if (bKeep)
		{
			// Задача боя отработала циклы: поискать лучше, иначе остаться.
			const FNoFearCoverPoint Old = Cover;
			if (!SearchCover(false))
			{
				Cover = Old;
				CoverOwner = MyPawn;
				if (UNoFearBotSubsystem* Bots = UNoFearBotSubsystem::Get(this))
				{
					Bots->ReserveCover(MyPawn, Old.Location);
				}
			}
			return;
		}
		ReleaseCover();
	}

	SearchCover(bRetreating);
}

void ANoFearShooterAIController::RequestCoveringFire(AActor* Threat)
{
	APawn* MyPawn = GetPawn();
	UNoFearBotSubsystem* Bots = UNoFearBotSubsystem::Get(this);
	if (!MyPawn || !Bots)
	{
		return;
	}
	const FVector ThreatLocation = GetLastKnownTargetLocation();
	for (ANoFearAIController* Bot : Bots->GetBots())
	{
		ANoFearShooterAIController* Shooter = Cast<ANoFearShooterAIController>(Bot);
		APawn* OtherPawn = Shooter ? Shooter->GetPawn() : nullptr;
		if (!OtherPawn || Shooter == this || UNoFearHealthComponent::IsActorDead(OtherPawn) || UNoFearTeamLibrary::AreHostile(MyPawn, OtherPawn)
			|| FVector::Dist(MyPawn->GetActorLocation(), OtherPawn->GetActorLocation()) > CoverFireRequestRadius)
		{
			continue;
		}
		Shooter->ReceiveCoveringFireRequest(MyPawn, Threat, ThreatLocation);
	}
}

void ANoFearShooterAIController::ReceiveCoveringFireRequest(AActor* Requester, AActor* Threat, const FVector& ThreatLocation)
{
	if (!GetPawn() || UNoFearHealthComponent::IsActorDead(GetPawn()))
	{
		return;
	}
	if (!IsCoveringFire())
	{
		Shout(CoveringFireShout, 1.5f);
	}
	CoveringFireUntil = GetNow() + CoveringFireTime;
	if (!GetCurrentTarget() && Threat)
	{
		ReceiveAlarm(Threat, ThreatLocation);
	}
}

bool ANoFearShooterAIController::FindCombatPosition(FVector& OutLocation) const
{
	const APawn* MyPawn = GetPawn();
	const AActor* Target = GetCurrentTarget();
	UWorld* World = GetWorld();
	UNavigationSystemV1* NavSys = World ? FNavigationSystem::GetCurrent<UNavigationSystemV1>(World) : nullptr;
	if (!MyPawn || !Target || !NavSys)
	{
		return false;
	}
	const FVector Location = MyPawn->GetActorLocation();
	const FVector TargetLocation = GetLastKnownTargetLocation();
	const float LeashRadiusNow = GetLeashRadius();
	const FVector Anchor = GetLeashAnchor();

	if (!IsTargetVisible())
	{
		// Не видно — туда, где видели, но не вплотную и в пределах поводка.
		FVector Goal = TargetLocation;
		const FVector FromGoal = Location - Goal;
		if (FromGoal.Size2D() > 150.f)
		{
			Goal += FromGoal.GetSafeNormal2D() * 150.f;
		}
		if (LeashRadiusNow > 0.f)
		{
			FVector Offset = Goal - Anchor;
			Offset.Z = 0.f;
			if (Offset.Size() > LeashRadiusNow)
			{
				Goal = Anchor + Offset.GetClampedToMaxSize(LeashRadiusNow);
				Goal.Z = Location.Z;
			}
		}
		FNavLocation NavGoal;
		if (!NavSys->ProjectPointToNavigation(Goal, NavGoal, FVector(300.f, 300.f, 500.f)))
		{
			return false;
		}
		OutLocation = NavGoal.Location;
		return true;
	}

	// Видно — на удобной дистанции оружия, шагом вбок, чтобы не стоять на месте.
	const FVector2D Range = GetPreferredRange();
	const float Distance = FVector::Dist2D(Location, TargetLocation);
	float Desired = Distance;
	if (Distance > Range.Y)
	{
		Desired = FMath::Max(Range.Y * 0.85f, Range.X);
	}
	else if (Distance < Range.X)
	{
		Desired = Range.X * 1.1f;
	}
	Desired = FMath::Max(Desired, 250.f);

	const FCollisionQueryParams Params = UNoFearCoverLibrary::MakeCoverTraceParams(World);
	const FVector TargetEyes = TargetLocation + FVector(0.f, 0.f, UNoFearCoverLibrary::ThreatEyeHeight);
	const FVector FromTarget = (Location - TargetLocation).GetSafeNormal2D();
	const UNoFearBotSubsystem* Bots = UNoFearBotSubsystem::Get(this);

	float BestScore = -FLT_MAX;
	bool bFound = false;
	static const float Angles[] = { 12.f, 25.f, 40.f, 55.f };
	const float FirstSign = FMath::RandBool() ? 1.f : -1.f;
	for (const float Angle : Angles)
	{
		for (const float Sign : { FirstSign, -FirstSign })
		{
			const FVector Dir = FromTarget.RotateAngleAxis(Angle * Sign + FMath::FRandRange(-5.f, 5.f), FVector::UpVector);
			FNavLocation Nav;
			if (!NavSys->ProjectPointToNavigation(TargetLocation + Dir * Desired, Nav, FVector(100.f, 100.f, 300.f)))
			{
				continue;
			}
			const FVector Point = Nav.Location;
			const float Travel = FVector::Dist2D(Point, Location);
			if (Travel < 150.f || (LeashRadiusNow > 0.f && FVector::Dist2D(Point, Anchor) > LeashRadiusNow))
			{
				continue;
			}
			if (Bots && Bots->IsCoverReserved(Point, CoverReserveRadius, MyPawn))
			{
				continue;
			}
			const FVector Eyes = Point + FVector(0.f, 0.f, UNoFearCoverLibrary::HighCoverHeight - 10.f);
			if (UNoFearCombatComponent::FindFriendInLine(MyPawn, Target, Eyes, TargetEyes, 40.f))
			{
				continue;
			}
			float Score = World->LineTraceTestByChannel(Eyes, TargetEyes, ECC_Visibility, Params) ? 0.f : 1.f;
			Score -= FMath::Abs(FVector::Dist2D(Point, TargetLocation) - Desired) / 1000.f;
			Score -= Travel / 2000.f;
			if (Score > BestScore)
			{
				BestScore = Score;
				OutLocation = Point;
				bFound = true;
			}
		}
	}
	return bFound;
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

	if (bInfiniteReserveAmmo)
	{
		for (ANoFearWeapon* Weapon : Weapons->GetWeapons())
		{
			const UNoFearWeaponData* Data = Weapon ? Weapon->GetWeaponData() : nullptr;
			if (Data && Weapon->GetReserveAmmo() < Data->MagazineSize)
			{
				Weapon->AddReserveAmmo(FMath::Max(Data->MaxReserveAmmo, Data->MagazineSize));
			}
		}
	}

	// Без цели — перезарядиться, если магазин почти пуст (в бою перезаряжаются в укрытии).
	const ANoFearWeapon* Current = Weapons->GetCurrentWeapon();
	const UNoFearWeaponData* CurrentData = Current ? Current->GetWeaponData() : nullptr;
	if (!GetCurrentTarget() && CurrentData && Current->GetWeaponState() == ENoFearWeaponState::Idle && Current->GetReserveAmmo() > 0
		&& Current->GetMagazineAmmo() < FMath::CeilToInt(CurrentData->MagazineSize * IdleReloadFraction))
	{
		Weapons->Reload();
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
