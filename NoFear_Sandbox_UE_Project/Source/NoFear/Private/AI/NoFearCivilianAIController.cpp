#include "AI/NoFearCivilianAIController.h"
#include "Core/NoFearPerf.h"

#include "AI/NoFearCalmComponent.h"
#include "AI/NoFearFleeLibrary.h"
#include "Animation/NoFearPoseComponent.h"
#include "BehaviorTree/BehaviorTree.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "Core/NoFearAISettings.h"
#include "Core/NoFearCharacter.h"
#include "Core/NoFearFearComponent.h"
#include "Core/NoFearFearSubsystem.h"
#include "Core/NoFearHealthComponent.h"
#include "Core/NoFearTeam.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	/** Угроза сдвинулась настолько, что план бегства стоит пересчитать. */
	constexpr float ThreatMovedReplanDistance = 300.f;

	const FNoFearFearTuning& GetFearTuning(const UObject* WorldContext)
	{
		if (const UNoFearFearSubsystem* Subsystem = UNoFearFearSubsystem::Get(WorldContext))
		{
			return *Subsystem;
		}
		return UNoFearAISettings::Get()->Fear;
	}
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

	// Страх уже добавлен в ReceiveDanger. Далёкая стрельба уже напуганного не пугает сильнее, но не даёт страху спадать.
	float Amount = 0.f;
	float Cap = 100.f;
	if (const UNoFearFearSubsystem* Fear = UNoFearFearSubsystem::Get(this))
	{
		Fear->GetDangerFear(Type, static_cast<float>(FVector::Dist(MyPawn->GetActorLocation(), ThreatLocation)), Amount, Cap);
	}
	const bool bScream = Type == ENoFearDangerType::Scream;
	// Соседа испугало что-то в ThreatLocation; кричащий сам не угроза, и второй раз крик не передаётся.
	ReactToFear(ThreatLocation, bScream ? nullptr : Source, Cap, !bScream);
}

void ANoFearCivilianAIController::NoteCorpse(ANoFearCharacter* Corpse)
{
	// Убийца где-то рядом: прочь от тела (страх от трупа уже добавлен в ScanCorpses).
	ReactToFear(Corpse->GetActorLocation(), nullptr, GetFearTuning(this).CorpseCap, true);
}

void ANoFearCivilianAIController::ReactToFear(const FVector& Location, AActor* Source, float EventCap, bool bScream)
{
	const FNoFearFearTuning& Tuning = GetFearTuning(this);
	const float Fear = GetFear();
	if (Fear >= Tuning.PanicThreshold && EventCap >= Tuning.PanicThreshold)
	{
		Panic(Location, Source, bScream);
	}
	else if (Fear >= Tuning.AnxiousThreshold
		&& (CivilianState == ENoFearCivilianState::Calm || CivilianState == ENoFearCivilianState::Wary))
	{
		BecomeWary(Location);
	}
}

void ANoFearCivilianAIController::Panic(FVector ThreatLocation, AActor* Threat, bool bScream)
{
	const APawn* MyPawn = GetPawn();
	if (!MyPawn || UNoFearHealthComponent::IsActorDead(MyPawn))
	{
		return;
	}

	AddThreat(Threat, ThreatLocation);
	// Паника держится, пока страх не упадёт ниже порога тревоги; вызов извне (сценарий, тест) поднимает страх до порога паники.
	if (UNoFearFearComponent* FearComponent = UNoFearFearComponent::FindFear(MyPawn))
	{
		const float PanicThreshold = GetFearTuning(this).PanicThreshold;
		if (FearComponent->GetFear() < PanicThreshold)
		{
			FearComponent->AddFear(100.f, PanicThreshold, ThreatLocation, false);
		}
	}

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
	if (UNoFearFearComponent* FearComponent = UNoFearFearComponent::FindFear(MyPawn);
		FearComponent && FearComponent->GetFear() >= GetFearTuning(this).AnxiousThreshold)
	{
		FearComponent->SetFear(0.f);
	}
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

	// Кого бот уверенно знает (увидел монстра, в него стреляли) — угроза, от неё бежать; страх от неё уже добавлен
	// (UpdateSightFear, HandlePawnDamaged). Кого едва заметил — немного страха и повод насторожиться.
	const FNoFearFearTuning& Tuning = GetFearTuning(this);
	AActor* FreshThreat = nullptr;
	FVector FreshThreatLocation = FVector::ZeroVector;
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
			AddThreat(Actor, Location);
			const float Distance = FVector::Dist2D(MyLocation, Location);
			if (Distance < FreshThreatDistance)
			{
				FreshThreat = Actor;
				FreshThreatLocation = Location;
				FreshThreatDistance = Distance;
			}
		}
		else if (Knowledge.Awareness >= SuspiciousThreshold && UNoFearTeamLibrary::AreHostile(MyPawn, Actor)
			&& Now - FMath::Max(Knowledge.LastHeardTime, Knowledge.LastSeenTime) < 1.0)
		{
			AddFear(Tuning.SuspiciousFear, Tuning.SuspiciousCap, Knowledge.LastKnownLocation, Actor);
			if (GetFear() >= Tuning.AnxiousThreshold
				&& (CivilianState == ENoFearCivilianState::Calm || CivilianState == ENoFearCivilianState::Wary))
			{
				BecomeWary(Knowledge.LastKnownLocation);
			}
		}
	}

	SyncFear(FreshThreat, FreshThreatLocation);
}

void ANoFearCivilianAIController::SyncFear(AActor* FreshThreat, const FVector& FreshThreatLocation)
{
	const APawn* MyPawn = GetPawn();
	const double Now = GetNow();
	const FNoFearFearTuning& Tuning = GetFearTuning(this);
	const UNoFearFearComponent* FearComponent = UNoFearFearComponent::FindFear(MyPawn);
	const float Fear = FearComponent ? FearComponent->GetFear() : 0.f;
	// Без видимой угрозы — туда, откуда пришёл страх (монстр, выстрел, паникующий сосед).
	const FVector FearLocation = FreshThreat ? FreshThreatLocation : (FearComponent ? FearComponent->GetSourceLocation() : MyPawn->GetActorLocation());
	const float FreshThreatDistance = FreshThreat ? static_cast<float>(FVector::Dist2D(MyPawn->GetActorLocation(), FreshThreatLocation)) : TNumericLimits<float>::Max();

	switch (CivilianState)
	{
	case ENoFearCivilianState::Calm:
	case ENoFearCivilianState::Wary:
		if (Fear >= Tuning.PanicThreshold)
		{
			// Кричит, только если сам видит угрозу; заразившийся паникой от соседей бежит молча.
			Panic(FearLocation, FreshThreat, FreshThreat != nullptr);
			break;
		}
		if (CivilianState == ENoFearCivilianState::Calm)
		{
			if (Fear >= Tuning.AnxiousThreshold)
			{
				BecomeWary(FearLocation);
			}
			break;
		}
		if (bWaryFocus && Now >= WaryLookUntil)
		{
			// Посмотрел на звук — теперь отходит, глядя куда идёт.
			ClearFocus(EAIFocusPriority::Gameplay);
			bWaryFocus = false;
		}
		// Успокоил герой — тревога проходит сразу, без WaryDuration.
		if (Fear < Tuning.AnxiousThreshold && (Now >= WaryUntil || (FearComponent && Now - FearComponent->GetLastComfortTime() < 1.0)))
		{
			bHasFleeLocation = false;
			SetCivilianState(ENoFearCivilianState::Calm);
		}
		break;

	case ENoFearCivilianState::Panic:
		if (Fear < Tuning.AnxiousThreshold)
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
		if (Fear < Tuning.AnxiousThreshold)
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

void ANoFearCivilianAIController::OnPortalTranceChanged(bool bTrance)
{
	Super::OnPortalTranceChanged(bTrance);
	if (bTrance)
	{
		// В трансе стоят в полный рост и тянутся к порталу, а не прячутся.
		SetCowerPose(false);
		return;
	}
	const APawn* MyPawn = GetPawn();
	if (!MyPawn || UNoFearHealthComponent::IsActorIncapacitated(MyPawn))
	{
		return;
	}
	// Очнулся: от портала — бегом. Без крика: иначе разбуженный пугал бы соседей и ломал их снова.
	const AActor* Portal = GetTrancePortal();
	const UNoFearFearComponent* FearComponent = UNoFearFearComponent::FindFear(MyPawn);
	const FVector ThreatLocation = Portal ? Portal->GetActorLocation() : (FearComponent ? FearComponent->GetSourceLocation() : MyPawn->GetActorLocation());
	if (CivilianState == ENoFearCivilianState::Panic || CivilianState == ENoFearCivilianState::Cower)
	{
		SetCivilianState(ENoFearCivilianState::Wary);
	}
	Panic(ThreatLocation, const_cast<AActor*>(Portal), false);
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

void ANoFearCivilianAIController::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);
	// Героя можно попросить успокоить испуганного мирного: удерживать E рядом с ним.
	if (InPawn && !InPawn->FindComponentByClass<UNoFearCalmComponent>())
	{
		UNoFearCalmComponent* Calm = NewObject<UNoFearCalmComponent>(InPawn, TEXT("CalmComponent"));
		Calm->RegisterComponent();
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
