#include "AI/NoFearAIController.h"

#include "BehaviorTree/BehaviorTree.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "Core/NoFearCharacter.h"
#include "Core/NoFearTeam.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Perception/AIPerceptionComponent.h"
#include "Perception/AISenseConfig_Hearing.h"
#include "Perception/AISenseConfig_Sight.h"
#include "Perception/AISense_Hearing.h"
#include "Perception/AISense_Sight.h"
#include "TimerManager.h"

const FName FNoFearBotKeys::AttackTarget(TEXT("AttackTarget"));
const FName FNoFearBotKeys::HasLineOfSight(TEXT("HasLineOfSight"));
const FName FNoFearBotKeys::CanFire(TEXT("CanFire"));
const FName FNoFearBotKeys::LastKnownLocation(TEXT("LastKnownLocation"));
const FName FNoFearBotKeys::HomeLocation(TEXT("HomeLocation"));
const FName FNoFearBotKeys::MoveGoal(TEXT("MoveGoal"));
const FName FNoFearBotKeys::InvestigateLocation(TEXT("InvestigateLocation"));
const FName FNoFearBotKeys::Leader(TEXT("Leader"));
const FName FNoFearBotKeys::IsFollowing(TEXT("IsFollowing"));

ANoFearAIController::ANoFearAIController(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	bWantsPlayerState = false;

	UAIPerceptionComponent* Perception = CreateDefaultSubobject<UAIPerceptionComponent>(TEXT("Perception"));

	// Зрение замечает только врагов: кто враг, решает таблица NoFear Teams через FGenericTeamId.
	SightConfig = CreateDefaultSubobject<UAISenseConfig_Sight>(TEXT("SightConfig"));
	SightConfig->SightRadius = 3000.f;
	SightConfig->LoseSightRadius = 3500.f;
	SightConfig->PeripheralVisionAngleDegrees = 75.f;
	SightConfig->DetectionByAffiliation.bDetectEnemies = true;
	SightConfig->DetectionByAffiliation.bDetectNeutrals = false;
	SightConfig->DetectionByAffiliation.bDetectFriendlies = false;
	SightConfig->SetMaxAge(TargetMemoryTime);

	// Слух: выстрелы (оружие сообщает шум). Свои не интересны.
	HearingConfig = CreateDefaultSubobject<UAISenseConfig_Hearing>(TEXT("HearingConfig"));
	HearingConfig->HearingRange = 4000.f;
	HearingConfig->DetectionByAffiliation.bDetectEnemies = true;
	HearingConfig->DetectionByAffiliation.bDetectNeutrals = true;
	HearingConfig->DetectionByAffiliation.bDetectFriendlies = false;
	HearingConfig->SetMaxAge(3.f);

	Perception->ConfigureSense(*SightConfig);
	Perception->ConfigureSense(*HearingConfig);
	Perception->SetDominantSense(SightConfig->GetSenseImplementation());
	SetPerceptionComponent(*Perception);
}

ANoFearCharacter* ANoFearAIController::GetNoFearCharacter() const
{
	return Cast<ANoFearCharacter>(GetPawn());
}

FGenericTeamId ANoFearAIController::GetGenericTeamId() const
{
	if (const IGenericTeamAgentInterface* PawnTeam = Cast<IGenericTeamAgentInterface>(GetPawn()))
	{
		return PawnTeam->GetGenericTeamId();
	}
	return Super::GetGenericTeamId();
}

bool ANoFearAIController::IsTargetVisible() const
{
	const FNoFearKnownActor* Knowledge = KnownActors.Find(CurrentTarget);
	return Knowledge && Knowledge->bVisible;
}

FVector ANoFearAIController::GetLastKnownTargetLocation() const
{
	if (const AActor* Target = CurrentTarget.Get())
	{
		const FNoFearKnownActor* Knowledge = KnownActors.Find(CurrentTarget);
		return Knowledge && !Knowledge->bVisible ? Knowledge->LastKnownLocation : Target->GetActorLocation();
	}
	return GetPawn() ? GetPawn()->GetActorLocation() : FVector::ZeroVector;
}

void ANoFearAIController::SetHomeLocation(FVector NewHome)
{
	HomeLocation = NewHome;
	WriteBlackboard();
}

double ANoFearAIController::GetNow() const
{
	const UWorld* World = GetWorld();
	return World ? World->GetTimeSeconds() : 0.0;
}

void ANoFearAIController::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);
	if (!InPawn)
	{
		return;
	}

	HomeLocation = InPawn->GetActorLocation();
	KnownActors.Reset();
	CurrentTarget.Reset();

	// Motion matching GASP ждёт ускорения, а не готовой скорости от следования по пути.
	if (const ACharacter* BotCharacter = Cast<ACharacter>(InPawn))
	{
		if (UCharacterMovementComponent* Movement = BotCharacter->GetCharacterMovement())
		{
			Movement->GetNavMovementProperties()->bUseAccelerationForPaths = true;
		}
	}

	if (UNoFearHealthComponent* Health = UNoFearHealthComponent::FindHealth(InPawn))
	{
		Health->OnDamaged.AddUniqueDynamic(this, &ANoFearAIController::HandlePawnDamaged);
		BoundHealth = Health;
	}

	if (UAIPerceptionComponent* Perception = GetPerceptionComponent())
	{
		Perception->OnTargetPerceptionUpdated.AddUniqueDynamic(this, &ANoFearAIController::HandlePerceptionUpdated);
		// Слушатель регистрируется раньше, чем появляется тело с командой; без обновления зрение не знает, кто враг.
		Perception->RequestStimuliListenerUpdate();
	}

	if (BehaviorTree)
	{
		// Блэкборд заполняется до старта дерева, чтобы первая же задача видела дом.
		UBlackboardComponent* BlackboardComp = nullptr;
		if (BehaviorTree->BlackboardAsset)
		{
			UseBlackboard(BehaviorTree->BlackboardAsset, BlackboardComp);
		}
		WriteBlackboard();
		RunBehaviorTree(BehaviorTree);
	}

	UpdateMovementStance();

	// Разносим обновления ботов по кадрам.
	GetWorldTimerManager().SetTimer(UpdateTimer, this, &ANoFearAIController::UpdateBot, UpdateInterval, true, FMath::FRandRange(0.05f, UpdateInterval));
}

void ANoFearAIController::OnUnPossess()
{
	GetWorldTimerManager().ClearTimer(UpdateTimer);
	if (UNoFearHealthComponent* Health = BoundHealth.Get())
	{
		Health->OnDamaged.RemoveDynamic(this, &ANoFearAIController::HandlePawnDamaged);
	}
	BoundHealth.Reset();

	Super::OnUnPossess();
}

void ANoFearAIController::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	GetWorldTimerManager().ClearTimer(UpdateTimer);
	Super::EndPlay(EndPlayReason);
}

void ANoFearAIController::UpdateBot()
{
	APawn* MyPawn = GetPawn();
	if (!MyPawn || UNoFearHealthComponent::IsActorDead(MyPawn))
	{
		return;
	}

	// Пока враг в поле зрения, его позиция и время свежие.
	const double Now = GetNow();
	for (TPair<TWeakObjectPtr<AActor>, FNoFearKnownActor>& Pair : KnownActors)
	{
		if (const AActor* Actor = Pair.Key.Get(); Actor && Pair.Value.bVisible)
		{
			Pair.Value.LastKnownLocation = Actor->GetActorLocation();
			Pair.Value.LastSeenTime = Now;
		}
	}
	ForgetStaleActors();

	AActor* BestTarget = nullptr;
	if (bCanAttack)
	{
		float BestScore = 0.f;
		for (const TPair<TWeakObjectPtr<AActor>, FNoFearKnownActor>& Pair : KnownActors)
		{
			AActor* Candidate = Pair.Key.Get();
			if (!IsValidTarget(Candidate))
			{
				continue;
			}
			const float Score = ScoreTarget(Candidate, Pair.Value);
			if (Score >= 0.f && (!BestTarget || Score > BestScore))
			{
				BestTarget = Candidate;
				BestScore = Score;
			}
		}
	}
	SetTarget(BestTarget);

	// Видимую цель отслеживаем взглядом, невидимую — смотрим туда, где видели последний раз (сквозь стены не подглядываем).
	if (AActor* Target = CurrentTarget.Get())
	{
		const FNoFearKnownActor* Knowledge = KnownActors.Find(Target);
		if (!Knowledge || Knowledge->bVisible)
		{
			SetFocus(Target);
		}
		else
		{
			SetFocalPoint(Knowledge->LastKnownLocation);
		}
	}

	UpdateMovementStance();
	WriteBlackboard();
}

void ANoFearAIController::ForgetStaleActors()
{
	const double Now = GetNow();
	for (auto It = KnownActors.CreateIterator(); It; ++It)
	{
		AActor* Actor = It.Key().Get();
		const FNoFearKnownActor& Knowledge = It.Value();
		const bool bDead = !Actor || UNoFearHealthComponent::IsActorDead(Actor);
		const double LastContact = FMath::Max(Knowledge.LastSeenTime, Knowledge.LastAttackedTime);
		const bool bStale = !Knowledge.bVisible && Now - LastContact > TargetMemoryTime;
		if (!bDead && !bStale)
		{
			continue;
		}
		// Цель не погибла, а потерялась: проверить, где её видели последней.
		if (!bDead && Actor == CurrentTarget.Get() && bInvestigateLostTarget)
		{
			SetInvestigateLocation(Knowledge.LastKnownLocation);
		}
		It.RemoveCurrent();
	}
}

bool ANoFearAIController::IsValidTarget(const AActor* Candidate) const
{
	const APawn* MyPawn = GetPawn();
	if (!Candidate || !MyPawn || Candidate == MyPawn || UNoFearHealthComponent::IsActorDead(Candidate))
	{
		return false;
	}
	if (!UNoFearTeamLibrary::AreHostile(MyPawn, Candidate))
	{
		return false;
	}
	return FVector::DistSquared(MyPawn->GetActorLocation(), Candidate->GetActorLocation()) <= FMath::Square(MaxTargetDistance);
}

float ANoFearAIController::ScoreTarget(const AActor* Candidate, const FNoFearKnownActor& Knowledge) const
{
	const float Distance = FVector::Dist(GetPawn()->GetActorLocation(), Candidate->GetActorLocation());
	float Score = 1.f - FMath::Clamp(Distance / FMath::Max(MaxTargetDistance, 1.f), 0.f, 1.f);
	if (Knowledge.bVisible)
	{
		Score += 1.f;
	}
	if (GetNow() - Knowledge.LastAttackedTime <= AttackerMemoryTime)
	{
		Score += 0.75f;
	}
	if (Candidate == CurrentTarget.Get())
	{
		Score += TargetStickiness;
	}
	return Score;
}

void ANoFearAIController::SetTarget(AActor* NewTarget)
{
	AActor* OldTarget = CurrentTarget.Get();
	if (NewTarget == OldTarget)
	{
		return;
	}
	CurrentTarget = NewTarget;
	HandleTargetChanged(NewTarget, OldTarget);
	OnTargetChanged.Broadcast(NewTarget, OldTarget);
}

void ANoFearAIController::HandleTargetChanged(AActor* NewTarget, AActor* OldTarget)
{
	if (UBlackboardComponent* BlackboardComp = GetBlackboardComponent())
	{
		if (NewTarget)
		{
			BlackboardComp->SetValueAsObject(FNoFearBotKeys::AttackTarget, NewTarget);
			BlackboardComp->ClearValue(FNoFearBotKeys::InvestigateLocation);
		}
		else
		{
			BlackboardComp->ClearValue(FNoFearBotKeys::AttackTarget);
		}
	}
	if (!NewTarget)
	{
		ClearFocus(EAIFocusPriority::Gameplay);
	}
	UpdateMovementStance();
}

void ANoFearAIController::SetInvestigateLocation(const FVector& Location)
{
	if (UBlackboardComponent* BlackboardComp = GetBlackboardComponent())
	{
		BlackboardComp->SetValueAsVector(FNoFearBotKeys::InvestigateLocation, Location);
	}
}

void ANoFearAIController::ReportThreat(AActor* Threat)
{
	APawn* MyPawn = GetPawn();
	if (!Threat || !MyPawn || Threat == MyPawn || UNoFearHealthComponent::IsActorDead(Threat) || UNoFearTeamLibrary::AreFriendly(MyPawn, Threat))
	{
		return;
	}

	FNoFearKnownActor& Knowledge = KnownActors.FindOrAdd(Threat);
	Knowledge.LastAttackedTime = GetNow();
	if (!Knowledge.bVisible)
	{
		Knowledge.LastKnownLocation = Threat->GetActorLocation();
	}

	// Без цели реагируем сразу, не дожидаясь таймера.
	if (!CurrentTarget.IsValid())
	{
		UpdateBot();
	}
}

void ANoFearAIController::HandlePerceptionUpdated(AActor* Actor, FAIStimulus Stimulus)
{
	if (!Actor || Actor == GetPawn() || UNoFearHealthComponent::IsActorDead(Actor))
	{
		return;
	}

	if (Stimulus.Type == UAISense::GetSenseID<UAISense_Sight>())
	{
		FNoFearKnownActor& Knowledge = KnownActors.FindOrAdd(Actor);
		Knowledge.bVisible = Stimulus.WasSuccessfullySensed();
		Knowledge.LastKnownLocation = Knowledge.bVisible ? Actor->GetActorLocation() : Stimulus.StimulusLocation;
		Knowledge.LastSeenTime = GetNow();
		if (Knowledge.bVisible && !CurrentTarget.IsValid())
		{
			UpdateBot();
		}
	}
	else if (Stimulus.Type == UAISense::GetSenseID<UAISense_Hearing>() && Stimulus.WasSuccessfullySensed())
	{
		// Звук выстрела: Actor — стрелявший. Без цели идём посмотреть, кто там.
		if (bCanAttack && bInvestigateNoises && !CurrentTarget.IsValid() && UNoFearTeamLibrary::AreHostile(GetPawn(), Actor))
		{
			SetInvestigateLocation(Stimulus.StimulusLocation);
		}
	}
}

void ANoFearAIController::HandlePawnDamaged(UNoFearHealthComponent* Component, const FNoFearDamageInfo& Info)
{
	AActor* Attacker = Info.InstigatorController ? Info.InstigatorController->GetPawn() : nullptr;
	if (!Attacker && Info.DamageCauser)
	{
		Attacker = Info.DamageCauser->GetInstigator() ? Info.DamageCauser->GetInstigator() : Info.DamageCauser.Get();
	}
	ReportThreat(Attacker);
}

bool ANoFearAIController::ShouldWalk() const
{
	return bWalkWhenCalm && !CurrentTarget.IsValid();
}

void ANoFearAIController::UpdateMovementStance()
{
	const bool bCombatStance = CurrentTarget.IsValid() && bStrafeInCombat;
	SetMovementFlags(bCombatStance, bCombatStance, ShouldWalk());
}

void ANoFearAIController::SetMovementFlags(bool bStrafe, bool bAim, bool bWalk)
{
	APawn* MyPawn = GetPawn();
	const FStructProperty* StateProperty = MyPawn ? FindFProperty<FStructProperty>(MyPawn->GetClass(), InputStateProperty) : nullptr;
	if (!StateProperty)
	{
		return;
	}

	// CBP_SandboxCharacter каждый кадр выбирает поворот и походку по этой структуре: боком/прицел — лицом к
	// ControlRotation (фокус контроллера), иначе лицом по ходу. Тело сбрасывает её к своим умолчаниям уже после
	// захвата контроллером, поэтому пишем при каждом обновлении бота, а не только при смене состояния.
	void* State = StateProperty->ContainerPtrToValuePtr<void>(MyPawn);
	for (TFieldIterator<FBoolProperty> It(StateProperty->Struct); It; ++It)
	{
		// Поля пользовательской структуры называются вроде WantsToAim_7_3A4AA0AA... — сравниваем по началу.
		const FString FieldName = It->GetName();
		if (FieldName.StartsWith(TEXT("WantsToStrafe")))
		{
			It->SetPropertyValue_InContainer(State, bStrafe);
		}
		else if (FieldName.StartsWith(TEXT("WantsToAim")))
		{
			It->SetPropertyValue_InContainer(State, bAim);
		}
		else if (FieldName.StartsWith(TEXT("WantsToWalk")))
		{
			It->SetPropertyValue_InContainer(State, bWalk);
		}
	}
}

void ANoFearAIController::WriteBlackboard()
{
	if (UBlackboardComponent* BlackboardComp = GetBlackboardComponent())
	{
		BlackboardComp->SetValueAsVector(FNoFearBotKeys::HomeLocation, HomeLocation);
	}
}
