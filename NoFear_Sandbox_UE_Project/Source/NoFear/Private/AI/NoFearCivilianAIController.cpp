#include "AI/NoFearCivilianAIController.h"
#include "Core/NoFearPerf.h"

#include "AI/NoFearFleeLibrary.h"
#include "Animation/NoFearPoseComponent.h"
#include "BehaviorTree/BehaviorTree.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "Core/NoFearCharacter.h"
#include "Core/NoFearHealthComponent.h"
#include "Core/NoFearTeam.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	/** Угроза сдвинулась настолько, что план бегства стоит пересчитать. */
	constexpr float ThreatMovedReplanDistance = 300.f;
}

ANoFearCivilianAIController::ANoFearCivilianAIController(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	bCanAttack = false;
	bInvestigateLostTarget = false;
	bInvestigateNoises = false;
	bStrafeInCombat = false;
	bReactToCorpses = true;

	static ConstructorHelpers::FObjectFinderOptional<UBehaviorTree> Tree(TEXT("/Game/AI/BT_NoFear_Civilian.BT_NoFear_Civilian"));
	BehaviorTree = Tree.Get();
}

// --- Опасность ---

void ANoFearCivilianAIController::HandleDanger(ENoFearDangerType Type, AActor* Source, FVector ThreatLocation)
{
	const APawn* MyPawn = GetPawn();
	if (!MyPawn || UNoFearHealthComponent::IsActorDead(MyPawn))
	{
		return;
	}

	switch (Type)
	{
	case ENoFearDangerType::Gunshot:
		if (FVector::Dist(MyPawn->GetActorLocation(), ThreatLocation) <= PanicGunshotDistance)
		{
			Panic(ThreatLocation, Source, true);
		}
		else if (CivilianState == ENoFearCivilianState::Panic || CivilianState == ENoFearCivilianState::Cower)
		{
			// Уже напуган: далёкая стрельба просто не даёт успокоиться.
			ResetCalmDownTimer();
		}
		else
		{
			BecomeWary(ThreatLocation);
		}
		break;

	case ENoFearDangerType::Scream:
		// Соседа испугало что-то в ThreatLocation; кричащий сам не угроза.
		Panic(ThreatLocation, nullptr, false);
		break;

	case ENoFearDangerType::NearMiss:
	case ENoFearDangerType::Fight:
	case ENoFearDangerType::Explosion:
	case ENoFearDangerType::Grenade:
	default:
		Panic(ThreatLocation, Source, true);
		break;
	}
}

void ANoFearCivilianAIController::NoteCorpse(ANoFearCharacter* Corpse)
{
	// Убийца где-то рядом: прочь от тела.
	Panic(Corpse->GetActorLocation(), nullptr, true);
}

void ANoFearCivilianAIController::Panic(FVector ThreatLocation, AActor* Threat, bool bScream)
{
	const APawn* MyPawn = GetPawn();
	if (!MyPawn || UNoFearHealthComponent::IsActorDead(MyPawn))
	{
		return;
	}

	AddThreat(Threat, ThreatLocation);
	ResetCalmDownTimer();

	if (CivilianState == ENoFearCivilianState::Cower)
	{
		// Спрятался: вскакивает, только если угроза совсем рядом и есть куда бежать.
		if (FVector::Dist2D(MyPawn->GetActorLocation(), ThreatLocation) < CowerBreakDistance && PlanFlee())
		{
			SetCivilianState(ENoFearCivilianState::Panic);
		}
		return;
	}

	const bool bWasPanicking = CivilianState == ENoFearCivilianState::Panic;
	if (bScream)
	{
		Scream(ThreatLocation);
	}
	if (bWasPanicking)
	{
		// Уже бежит: новая угроза учтётся при перепланировании (раз в ReplanInterval).
		return;
	}

	if (PlanFlee())
	{
		SetCivilianState(ENoFearCivilianState::Panic);
	}
	else
	{
		SetCivilianState(ENoFearCivilianState::Cower);
	}
}

void ANoFearCivilianAIController::NotifyReachedSafety()
{
	if (CivilianState == ENoFearCivilianState::Panic)
	{
		SetCivilianState(ENoFearCivilianState::Cower);
	}
}

void ANoFearCivilianAIController::CalmDown()
{
	const APawn* MyPawn = GetPawn();
	Threats.Reset();
	bThreatsChanged = false;
	bHasFleeLocation = false;
	if (MyPawn && FMath::FRand() >= ReturnHomeChance)
	{
		// Не пойдёт домой: бродит там, где успокоился.
		SetHomeLocation(MyPawn->GetActorLocation());
	}
	SetCivilianState(ENoFearCivilianState::Calm);
}

void ANoFearCivilianAIController::BecomeWary(const FVector& Location)
{
	const double Now = GetNow();
	const bool bWasCalm = CivilianState == ENoFearCivilianState::Calm;
	WaryLocation = Location;
	WaryUntil = Now + WaryDuration;
	if (bWasCalm)
	{
		// Сначала постоять и посмотреть на звук, потом отходить.
		WaryLookUntil = Now + WaryLookTime;
		SetFocalPoint(Location);
		bWaryFocus = true;
	}

	TArray<FVector> Sources = { Location };
	FVector Retreat;
	if (UNoFearFleeLibrary::FindFleeLocation(GetPawn(), Sources, Retreat, WaryRetreatMin, WaryRetreatMax, 0.f, 16))
	{
		FleeLocation = Retreat;
		bHasFleeLocation = true;
	}
	else if (bWasCalm)
	{
		bHasFleeLocation = false;
	}

	if (bWasCalm)
	{
		SetCivilianState(ENoFearCivilianState::Wary);
	}
	else
	{
		WriteBlackboard();
	}
}

void ANoFearCivilianAIController::Scream(const FVector& ThreatLocation)
{
	APawn* MyPawn = GetPawn();
	const double Now = GetNow();
	if (!MyPawn || Now - LastScreamTime < ScreamCooldown)
	{
		return;
	}
	LastScreamTime = Now;

	const FVector Location = MyPawn->GetActorLocation();
	// Соседи-мирные пугаются, монстры слышат крик и идут на него.
	UNoFearBotSubsystem::ReportDanger(this, ENoFearDangerType::Scream, MyPawn, Location, ScreamRadius, ThreatLocation);
	UNoFearBotSubsystem::ReportNoise(this, MyPawn, Location, ScreamNoiseRange, ENoFearNoiseType::Other);
}

// --- Угрозы и план бегства ---

void ANoFearCivilianAIController::AddThreat(AActor* Actor, const FVector& Location)
{
	const double Now = GetNow();
	LastThreatTime = Now;
	for (FThreat& Threat : Threats)
	{
		const bool bSame = Actor ? Threat.Actor.Get() == Actor : (!Threat.Actor.IsValid() && FVector::DistSquared(Threat.Location, Location) < FMath::Square(ThreatMovedReplanDistance));
		if (bSame)
		{
			if (FVector::DistSquared(Threat.Location, Location) > FMath::Square(ThreatMovedReplanDistance))
			{
				Threat.Location = Location;
				bThreatsChanged = true;
			}
			Threat.Time = Now;
			return;
		}
	}
	FThreat& NewThreat = Threats.AddDefaulted_GetRef();
	NewThreat.Actor = Actor;
	NewThreat.Location = Location;
	NewThreat.Time = Now;
	bThreatsChanged = true;
}

void ANoFearCivilianAIController::GetThreatLocations(TArray<FVector>& OutLocations) const
{
	OutLocations.Reset(Threats.Num());
	for (const FThreat& Threat : Threats)
	{
		OutLocations.Add(Threat.Location);
	}
}

bool ANoFearCivilianAIController::PlanFlee()
{
	TArray<FVector> Locations;
	GetThreatLocations(Locations);
	bThreatsChanged = false;
	NextReplanTime = GetNow() + ReplanInterval;

	FVector Result;
	if (UNoFearFleeLibrary::FindFleeLocation(GetPawn(), Locations, Result, FleeMinDistance, FleeMaxDistance, SafePathDistance))
	{
		FleeLocation = Result;
		bHasFleeLocation = true;
		WriteBlackboard();
		return true;
	}
	return false;
}

void ANoFearCivilianAIController::ResetCalmDownTimer()
{
	LastThreatTime = GetNow();
	CalmDownDelay = FMath::FRandRange(CalmDownMin, FMath::Max(CalmDownMin, CalmDownMax));
}

// --- Обновление ---

void ANoFearCivilianAIController::UpdateBot()
{
	Super::UpdateBot();

	const APawn* MyPawn = GetPawn();
	if (!MyPawn || UNoFearHealthComponent::IsActorDead(MyPawn))
	{
		return;
	}
	UpdateCivilian();
	UpdateAlertState();
	UpdateMovementStance();
	WriteBlackboard();
}

void ANoFearCivilianAIController::UpdateCivilian()
{
	const APawn* MyPawn = GetPawn();
	const double Now = GetNow();
	const FVector MyLocation = MyPawn->GetActorLocation();

	// Забыть старые угрозы и мёртвых.
	Threats.RemoveAll([&](const FThreat& Threat)
	{
		const bool bDeadActor = Threat.Actor.IsValid() && UNoFearHealthComponent::IsActorDead(Threat.Actor.Get());
		return bDeadActor || Now - Threat.Time > ThreatMemoryTime;
	});

	// Кого бот уверенно знает (увидел монстра, в него стреляли) — угроза; кого едва заметил — повод насторожиться.
	AActor* FreshThreat = nullptr;
	float FreshThreatDistance = TNumericLimits<float>::Max();
	for (const TPair<TWeakObjectPtr<AActor>, FNoFearKnownActor>& Pair : KnownActors)
	{
		AActor* Actor = Pair.Key.Get();
		const FNoFearKnownActor& Knowledge = Pair.Value;
		if (!Actor || UNoFearHealthComponent::IsActorDead(Actor) || UNoFearTeamLibrary::AreFriendly(MyPawn, Actor))
		{
			continue;
		}

		const bool bRecentlyAttacked = Now - Knowledge.LastAttackedTime < 1.0;
		if (Knowledge.bVisible || bRecentlyAttacked)
		{
			const FVector Location = Knowledge.bVisible ? Actor->GetActorLocation() : Knowledge.LastKnownLocation;
			if (CivilianState == ENoFearCivilianState::Calm || CivilianState == ENoFearCivilianState::Wary)
			{
				Panic(Location, Actor, true);
			}
			else
			{
				AddThreat(Actor, Location);
			}
			const float Distance = FVector::Dist2D(MyLocation, Location);
			if (Distance < FreshThreatDistance)
			{
				FreshThreat = Actor;
				FreshThreatDistance = Distance;
			}
		}
		else if (Knowledge.Awareness >= SuspiciousThreshold && UNoFearTeamLibrary::AreHostile(MyPawn, Actor)
			&& Now - FMath::Max(Knowledge.LastHeardTime, Knowledge.LastSeenTime) < 1.0
			&& (CivilianState == ENoFearCivilianState::Calm || CivilianState == ENoFearCivilianState::Wary))
		{
			BecomeWary(Knowledge.LastKnownLocation);
		}
	}

	switch (CivilianState)
	{
	case ENoFearCivilianState::Wary:
		if (bWaryFocus && Now >= WaryLookUntil)
		{
			// Посмотрел на звук — теперь отходит, глядя куда идёт.
			ClearFocus(EAIFocusPriority::Gameplay);
			bWaryFocus = false;
		}
		if (Now >= WaryUntil)
		{
			bHasFleeLocation = false;
			SetCivilianState(ENoFearCivilianState::Calm);
		}
		break;

	case ENoFearCivilianState::Panic:
		if (Now - LastThreatTime > CalmDownDelay)
		{
			CalmDown();
			break;
		}
		// Перепланирование — плановый поиск: при исчерпанном лимите кадра ждёт следующего обновления.
		if (Now >= NextReplanTime && FNoFearSearchBudget::HasBudget(ENoFearSearchKind::Location))
		{
			TArray<FVector> Locations;
			GetThreatLocations(Locations);
			const bool bFleePointSafe = bHasFleeLocation && UNoFearFleeLibrary::IsLocationSafe(FleeLocation, Locations, SafePathDistance);
			if ((bThreatsChanged || !bFleePointSafe) && !PlanFlee() && !bFleePointSafe)
			{
				// Бежать больше некуда: прятаться на месте.
				SetCivilianState(ENoFearCivilianState::Cower);
			}
			NextReplanTime = Now + ReplanInterval;
		}
		break;

	case ENoFearCivilianState::Cower:
		if (Now - LastThreatTime > CalmDownDelay)
		{
			CalmDown();
			break;
		}
		if (FreshThreat && FreshThreatDistance < CowerBreakDistance && Now >= NextReplanTime)
		{
			// Угроза подошла вплотную: вскочить и бежать, если есть куда.
			if (PlanFlee())
			{
				SetCivilianState(ENoFearCivilianState::Panic);
			}
		}
		break;

	default:
		break;
	}
}

void ANoFearCivilianAIController::SetCivilianState(ENoFearCivilianState NewState)
{
	if (CivilianState == NewState)
	{
		return;
	}
	const ENoFearCivilianState OldState = CivilianState;
	CivilianState = NewState;

	if (OldState == ENoFearCivilianState::Cower)
	{
		SetCowerPose(false);
	}
	if (bWaryFocus && NewState != ENoFearCivilianState::Wary)
	{
		bWaryFocus = false;
	}
	if (NewState != ENoFearCivilianState::Wary)
	{
		// Бегут и прячутся, не оглядываясь; спокойный смотрит куда идёт.
		ClearFocus(EAIFocusPriority::Gameplay);
	}

	switch (NewState)
	{
	case ENoFearCivilianState::Panic:
		NextReplanTime = GetNow() + ReplanInterval;
		break;
	case ENoFearCivilianState::Cower:
		StopMovement();
		SetCowerPose(true);
		NextReplanTime = GetNow() + ReplanInterval;
		break;
	default:
		break;
	}

	UpdateAlertState();
	UpdateMovementStance();
	WriteBlackboard();
	OnCivilianStateChanged.Broadcast(this, NewState, OldState);
}

void ANoFearCivilianAIController::SetCowerPose(bool bCower)
{
	if (bCowerPose == bCower)
	{
		return;
	}
	bCowerPose = bCower;
	SetCrouching(bCower);
	const ANoFearCharacter* BotCharacter = Cast<ANoFearCharacter>(GetPawn());
	if (UNoFearPoseComponent* Pose = BotCharacter ? BotCharacter->GetPoseComponent() : nullptr)
	{
		Pose->SetPoseHeld(ENoFearPose::CoverHead, bCower);
	}
}

void ANoFearCivilianAIController::UpdateAlertState()
{
	switch (CivilianState)
	{
	case ENoFearCivilianState::Panic:
	case ENoFearCivilianState::Cower:
		SetAlertState(ENoFearAlertState::Combat);
		break;
	case ENoFearCivilianState::Wary:
		SetAlertState(ENoFearAlertState::Suspicious);
		break;
	default:
		SetAlertState(ENoFearAlertState::Calm);
		break;
	}
}

void ANoFearCivilianAIController::UpdateMovementStance()
{
	const ANoFearCharacter* BotCharacter = Cast<ANoFearCharacter>(GetPawn());
	const UNoFearPoseComponent* Pose = BotCharacter ? BotCharacter->GetPoseComponent() : nullptr;
	// Раненый или только что задетый не может спринтовать: бежит, прихрамывая.
	const bool bHurt = Pose && (Pose->IsWounded() || Pose->IsSlowed());

	switch (CivilianState)
	{
	case ENoFearCivilianState::Panic:
		SetMovementFlags(false, false, false, !bHurt);
		break;
	case ENoFearCivilianState::Wary:
		SetMovementFlags(bWaryFocus || bLookAroundStance, false, true);
		break;
	case ENoFearCivilianState::Cower:
		SetMovementFlags(false, false, true);
		break;
	default:
		SetMovementFlags(bLookAroundStance, false, true);
		break;
	}
}

void ANoFearCivilianAIController::WriteBlackboard()
{
	Super::WriteBlackboard();
	if (UBlackboardComponent* BlackboardComp = GetBlackboardComponent())
	{
		BlackboardComp->SetValueAsBool(FNoFearBotKeys::IsPanicking, CivilianState == ENoFearCivilianState::Panic);
		BlackboardComp->SetValueAsBool(FNoFearBotKeys::IsCowering, CivilianState == ENoFearCivilianState::Cower);
		BlackboardComp->SetValueAsBool(FNoFearBotKeys::IsWary, CivilianState == ENoFearCivilianState::Wary);
		if (bHasFleeLocation)
		{
			BlackboardComp->SetValueAsVector(FNoFearBotKeys::FleeLocation, FleeLocation);
		}
		else
		{
			BlackboardComp->ClearValue(FNoFearBotKeys::FleeLocation);
		}
	}
}

void ANoFearCivilianAIController::OnUnPossess()
{
	SetCowerPose(false);
	Threats.Reset();
	bHasFleeLocation = false;
	CivilianState = ENoFearCivilianState::Calm;
	Super::OnUnPossess();
}
