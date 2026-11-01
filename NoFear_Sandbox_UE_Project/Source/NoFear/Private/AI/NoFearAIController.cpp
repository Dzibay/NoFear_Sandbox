#include "AI/NoFearAIController.h"
#include "Core/NoFearPerf.h"

#include "AI/NoFearFleeLibrary.h"
#include "Animation/NoFearPoseComponent.h"
#include "BehaviorTree/BehaviorTree.h"
#include "BehaviorTree/BehaviorTreeComponent.h"
#include "BrainComponent.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "Core/NoFearCharacter.h"
#include "Core/NoFearFearComponent.h"
#include "Core/NoFearFearSubsystem.h"
#include "Core/NoFearTeam.h"
#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Navigation/PathFollowingComponent.h"
#include "NavigationSystem.h"
#include "Perception/AIPerceptionComponent.h"
#include "Perception/AISenseConfig_Hearing.h"
#include "Perception/AISenseConfig_Sight.h"
#include "Perception/AISense_Hearing.h"
#include "Perception/AISense_Sight.h"
#include "TimerManager.h"
#include "Weapons/NoFearGrenade.h"
#include "Weapons/NoFearGrenadeComponent.h"
#include "Weapons/NoFearGrenadeData.h"
#include "Weapons/NoFearWeaponComponent.h"

namespace
{
	TAutoConsoleVariable<int32> CVarAIDebug(
		TEXT("NoFear.AI.Debug"), 0,
		TEXT("Над ботами показывать состояние тревоги, шкалу обнаружения, цель и точки поиска. 0 — выключено."),
		ECVF_Cheat);

	/** Скорость, при которой враг считается бегущим для шкалы обнаружения (см/с). */
	constexpr float RunSpeedForDetection = 500.f;

	/** Как часто искать глазами тела погибших. */
	constexpr float CorpseScanInterval = 0.5f;

	/** Доля 0..1, на которую Value продвинулась от A к B. Если диапазон вырожден — 1. */
	float Remap01(float Value, float A, float B)
	{
		return B > A ? FMath::Clamp((Value - A) / (B - A), 0.f, 1.f) : 1.f;
	}

	FColor GetAlertColor(ENoFearAlertState State)
	{
		switch (State)
		{
		case ENoFearAlertState::Suspicious: return FColor::Yellow;
		case ENoFearAlertState::Combat: return FColor::Red;
		case ENoFearAlertState::Search: return FColor::Orange;
		default: return FColor::Green;
		}
	}

	const TCHAR* GetAlertName(ENoFearAlertState State)
	{
		switch (State)
		{
		case ENoFearAlertState::Suspicious: return TEXT("Suspicious");
		case ENoFearAlertState::Combat: return TEXT("Combat");
		case ENoFearAlertState::Search: return TEXT("Search");
		default: return TEXT("Calm");
		}
	}
}

const FName FNoFearBotKeys::AttackTarget(TEXT("AttackTarget"));
const FName FNoFearBotKeys::HasLineOfSight(TEXT("HasLineOfSight"));
const FName FNoFearBotKeys::CanFire(TEXT("CanFire"));
const FName FNoFearBotKeys::LastKnownLocation(TEXT("LastKnownLocation"));
const FName FNoFearBotKeys::HomeLocation(TEXT("HomeLocation"));
const FName FNoFearBotKeys::MoveGoal(TEXT("MoveGoal"));
const FName FNoFearBotKeys::InvestigateLocation(TEXT("InvestigateLocation"));
const FName FNoFearBotKeys::Leader(TEXT("Leader"));
const FName FNoFearBotKeys::IsFollowing(TEXT("IsFollowing"));
const FName FNoFearBotKeys::AlertState(TEXT("AlertState"));
const FName FNoFearBotKeys::FleeLocation(TEXT("FleeLocation"));
const FName FNoFearBotKeys::IsPanicking(TEXT("IsPanicking"));
const FName FNoFearBotKeys::IsCowering(TEXT("IsCowering"));
const FName FNoFearBotKeys::IsWary(TEXT("IsWary"));
const FName FNoFearBotKeys::HasAttackToken(TEXT("HasAttackToken"));
const FName FNoFearBotKeys::IsRoaring(TEXT("IsRoaring"));
const FName FNoFearBotKeys::PackGoal(TEXT("PackGoal"));
const FName FNoFearBotKeys::IsScattering(TEXT("IsScattering"));
const FName FNoFearBotKeys::WanderCenter(TEXT("WanderCenter"));
const FName FNoFearBotKeys::KeepAway(TEXT("KeepAway"));
const FName FNoFearBotKeys::CoverLocation(TEXT("CoverLocation"));
const FName FNoFearBotKeys::InCover(TEXT("InCover"));
const FName FNoFearBotKeys::CoverIsLow(TEXT("CoverIsLow"));
const FName FNoFearBotKeys::IsRetreating(TEXT("IsRetreating"));
const FName FNoFearBotKeys::InWeaponRange(TEXT("InWeaponRange"));
const FName FNoFearBotKeys::HealTarget(TEXT("HealTarget"));
const FName FNoFearBotKeys::RallyLocation(TEXT("RallyLocation"));
const FName FNoFearBotKeys::IsSuppressing(TEXT("IsSuppressing"));
const FName FNoFearBotKeys::SquadOrder(TEXT("SquadOrder"));

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

	// Слух: выстрелы (оружие сообщает шум). Свои не интересны. Шаги и приземление идут мимо него, через UNoFearBotSubsystem.
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

float ANoFearAIController::GetAwarenessOf(const AActor* Actor) const
{
	const FNoFearKnownActor* Knowledge = KnownActors.Find(TWeakObjectPtr<AActor>(const_cast<AActor*>(Actor)));
	return Knowledge ? Knowledge->Awareness : 0.f;
}

float ANoFearAIController::GetTopAwareness() const
{
	float Top = 0.f;
	for (const TPair<TWeakObjectPtr<AActor>, FNoFearKnownActor>& Pair : KnownActors)
	{
		if (Pair.Key.IsValid() && UNoFearTeamLibrary::AreHostile(GetPawn(), Pair.Key.Get()))
		{
			Top = FMath::Max(Top, Pair.Value.Awareness);
		}
	}
	return Top;
}

FString ANoFearAIController::GetActiveTaskName() const
{
	const UBehaviorTreeComponent* TreeComp = Cast<UBehaviorTreeComponent>(BrainComponent);
	const UBTNode* Node = TreeComp ? TreeComp->GetActiveNode() : nullptr;
	return Node ? Node->GetNodeName() : FString();
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
	AlertState = ENoFearAlertState::Calm;
	SuspiciousUntil = -1.0e9;
	bSuspicionFocus = false;
	EndSearch();
	NoticedCorpses.Reset();
	LastUpdateTime = GetNow();

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

	if (const ANoFearCharacter* BotCharacter = Cast<ANoFearCharacter>(InPawn))
	{
		if (UNoFearPoseComponent* Pose = BotCharacter->GetPoseComponent())
		{
			Pose->OnHitReaction.AddUniqueDynamic(this, &ANoFearAIController::HandleHitReaction);
			BoundPose = Pose;
		}
		if (UNoFearGrenadeComponent* Grenades = BotCharacter->GetGrenadeComponent())
		{
			Grenades->SetGrenadeCount(BotGrenades);
		}
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

	if (UNoFearBotSubsystem* Subsystem = UNoFearBotSubsystem::Get(this))
	{
		Subsystem->RegisterBot(this);
	}

	// Разносим обновления ботов по кадрам.
	GetWorldTimerManager().SetTimer(UpdateTimer, this, &ANoFearAIController::RunUpdateBot, GetThinkInterval(), true, FMath::FRandRange(0.05f, GetThinkInterval()));
}

void ANoFearAIController::RunUpdateBot()
{
	NOFEAR_PERF_SCOPE(BotUpdate);
	UpdateBot();
}

void ANoFearAIController::SetThinkScale(float Scale)
{
	Scale = FMath::Max(Scale, 1.f);
	if (FMath::IsNearlyEqual(Scale, ThinkScale) || !GetPawn())
	{
		ThinkScale = Scale;
		return;
	}
	ThinkScale = Scale;
	// Первый вызов — в случайный момент нового интервала, чтобы боты одного уровня не думали в одном кадре.
	const float Interval = GetThinkInterval();
	GetWorldTimerManager().SetTimer(UpdateTimer, this, &ANoFearAIController::RunUpdateBot, Interval, true, FMath::FRandRange(0.02f, Interval));
}

void ANoFearAIController::OnUnPossess()
{
	GetWorldTimerManager().ClearTimer(UpdateTimer);
	if (UNoFearBotSubsystem* Subsystem = UNoFearBotSubsystem::Get(this))
	{
		Subsystem->UnregisterBot(this);
	}
	if (UNoFearHealthComponent* Health = BoundHealth.Get())
	{
		Health->OnDamaged.RemoveDynamic(this, &ANoFearAIController::HandlePawnDamaged);
	}
	BoundHealth.Reset();

	if (UNoFearPoseComponent* Pose = BoundPose.Get())
	{
		Pose->OnHitReaction.RemoveDynamic(this, &ANoFearAIController::HandleHitReaction);
	}
	BoundPose.Reset();
	EndGrenadeEvade();
	ReleaseStagger();

	Super::OnUnPossess();
}

void ANoFearAIController::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	GetWorldTimerManager().ClearTimer(UpdateTimer);
	if (UNoFearBotSubsystem* Subsystem = UNoFearBotSubsystem::Get(this))
	{
		Subsystem->UnregisterBot(this);
	}
	Super::EndPlay(EndPlayReason);
}

void ANoFearAIController::UpdateBot()
{
	APawn* MyPawn = GetPawn();
	if (!MyPawn || UNoFearHealthComponent::IsActorDead(MyPawn))
	{
		return;
	}

	const double Now = GetNow();
	// Далёкие боты (LOD) думают реже, поэтому шаг может быть до секунды.
	const float DeltaTime = FMath::Clamp(static_cast<float>(Now - LastUpdateTime), 0.f, 1.f);
	LastUpdateTime = Now;

	UpdateAwareness(DeltaTime);
	ForgetStaleActors();
	UpdateSightFear(DeltaTime);
	ScanCorpses();

	// Целью становится только тот, в ком бот уверен (шкала полна).
	AActor* BestTarget = nullptr;
	if (bCanAttack)
	{
		float BestScore = 0.f;
		for (const TPair<TWeakObjectPtr<AActor>, FNoFearKnownActor>& Pair : KnownActors)
		{
			AActor* Candidate = Pair.Key.Get();
			if (Pair.Value.Awareness < 1.f || !IsValidTarget(Candidate))
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
		bSuspicionFocus = false;
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
	else if (bCanAttack && Now < SuspiciousUntil)
	{
		// Насторожен и цели нет: смотрим туда, откуда донеслось подозрительное.
		SetFocalPoint(SuspicionLocation);
		bSuspicionFocus = true;
	}
	else if (bSuspicionFocus)
	{
		ClearFocus(EAIFocusPriority::Gameplay);
		bSuspicionFocus = false;
	}
	if (bEvadingGrenade)
	{
		// Убегая, смотрит куда бежит, а не на врага.
		ClearFocus(EAIFocusPriority::Gameplay);
	}

	ShareAlarm();
	PumpSearch();
	UpdateAlertState();
	UpdateMovementStance();
	WriteBlackboard();
	DrawDebug();
}

void ANoFearAIController::UpdateAwareness(float DeltaTime)
{
	const APawn* MyPawn = GetPawn();
	const double Now = GetNow();

	for (TPair<TWeakObjectPtr<AActor>, FNoFearKnownActor>& Pair : KnownActors)
	{
		AActor* Actor = Pair.Key.Get();
		FNoFearKnownActor& Knowledge = Pair.Value;
		if (!Actor || UNoFearHealthComponent::IsActorDead(Actor) || !UNoFearTeamLibrary::AreHostile(MyPawn, Actor))
		{
			Knowledge.bVisible = false;
			continue;
		}

		const float PreviousAwareness = Knowledge.Awareness;
		bool bRising = false;

		if (Knowledge.bInView)
		{
			if (Knowledge.Awareness < 1.f)
			{
				const float Rate = ComputeDetectionRate(Actor, Knowledge);
				if (Rate > 0.f)
				{
					bRising = true;
					Knowledge.Awareness = FMath::Min(1.f, Knowledge.Awareness + Rate * DeltaTime);
				}
			}
			// Пока враг в поле зрения и шкала растёт (или полна), его позиция и время свежие.
			if (bRising || Knowledge.Awareness >= 1.f)
			{
				Knowledge.LastKnownLocation = Actor->GetActorLocation();
				Knowledge.LastKnownVelocity = Actor->GetVelocity();
				Knowledge.LastSeenTime = Now;
			}
		}

		// Не видно (или укрыт) и шкала не полна: после паузы она остывает.
		const double LastContact = FMath::Max3(Knowledge.LastSeenTime, Knowledge.LastHeardTime, Knowledge.LastAttackedTime);
		if (!bRising && Knowledge.Awareness < 1.f && Now - LastContact > AwarenessDecayDelay)
		{
			Knowledge.Awareness = FMath::Max(0.f, Knowledge.Awareness - AwarenessDecayRate * DeltaTime);
		}

		Knowledge.bVisible = Knowledge.bInView && Knowledge.Awareness >= 1.f;

		// Шкала перешла порог — насторожен и идёт проверить; пока она растёт выше порога, подозрение освежается.
		if (Knowledge.Awareness >= SuspiciousThreshold && (bRising || PreviousAwareness < SuspiciousThreshold))
		{
			NoteSuspicion(Knowledge.LastKnownLocation, PreviousAwareness < SuspiciousThreshold);
		}
	}
}

float ANoFearAIController::ComputeDetectionRate(const AActor* Target, const FNoFearKnownActor& Knowledge) const
{
	if (!GetPawn() || !Target || !SightConfig)
	{
		return 0.f;
	}

	FVector EyeLocation;
	FRotator EyeRotation;
	GetActorEyesViewPoint(EyeLocation, EyeRotation);
	const FVector ToTarget = Target->GetActorLocation() - EyeLocation;
	const float Distance = ToTarget.Size();

	// Чем дальше, тем дольше: вплотную мгновенно, на краю зрения секунды.
	float DetectTime;
	if (Distance <= MidDetectDistance)
	{
		DetectTime = FMath::Lerp(CloseDetectTime, MidDetectTime, Remap01(Distance, CloseDetectDistance, MidDetectDistance));
	}
	else
	{
		DetectTime = FMath::Lerp(MidDetectTime, FarDetectTime, Remap01(Distance, MidDetectDistance, SightConfig->SightRadius));
	}
	float Rate = 1.f / FMath::Max(DetectTime, 0.01f);

	// Боковое зрение медленнее прямого.
	const float AngleDegrees = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(FVector::DotProduct(EyeRotation.Vector(), ToTarget.GetSafeNormal()), -1.f, 1.f)));
	if (AngleDegrees > CenterViewAngle)
	{
		Rate *= FMath::Lerp(1.f, PeripheralDetectFactor, Remap01(AngleDegrees, CenterViewAngle, SightConfig->PeripheralVisionAngleDegrees));
	}

	// Пригнувшегося заметить трудно, бегущего легко.
	if (const ACharacter* TargetCharacter = Cast<ACharacter>(Target); TargetCharacter && TargetCharacter->IsCrouched())
	{
		Rate *= CrouchDetectFactor;
	}
	Rate *= FMath::Lerp(1.f, MovingDetectFactor, FMath::Clamp(Target->GetVelocity().Size2D() / RunSpeedForDetection, 0.f, 1.f));

	// Насторожен — смотрит внимательнее.
	if (AlertState != ENoFearAlertState::Calm)
	{
		Rate *= SuspiciousRateMultiplier;
	}

	return Rate * ComputeExposure(Target) * DetectionSpeedMultiplier;
}

float ANoFearAIController::ComputeExposure(const AActor* Target) const
{
	const APawn* MyPawn = GetPawn();
	UWorld* World = GetWorld();
	if (!MyPawn || !Target || !World)
	{
		return 0.f;
	}

	FVector EyeLocation;
	FRotator EyeRotation;
	GetActorEyesViewPoint(EyeLocation, EyeRotation);

	float Radius = 0.f;
	float HalfHeight = 0.f;
	Target->GetSimpleCollisionCylinder(Radius, HalfHeight);
	if (HalfHeight <= 0.f)
	{
		HalfHeight = 88.f;
	}

	FCollisionQueryParams Params(SCENE_QUERY_STAT(NoFearDetectionExposure), true, MyPawn);
	Params.AddIgnoredActor(Target);
	TArray<AActor*> AttachedActors;
	MyPawn->GetAttachedActors(AttachedActors, false, true);
	Target->GetAttachedActors(AttachedActors, false, true);
	Params.AddIgnoredActors(AttachedActors);

	// Голова, центр, ноги: чем больше открыто, тем быстрее замечают.
	const FVector Center = Target->GetActorLocation();
	const FVector Offset(0.f, 0.f, HalfHeight * 0.8f);
	const FVector Points[3] = { Center + Offset, Center, Center - Offset };
	int32 VisiblePoints = 0;
	for (const FVector& Point : Points)
	{
		if (!World->LineTraceTestByChannel(EyeLocation, Point, ECC_Visibility, Params))
		{
			++VisiblePoints;
		}
	}

	static constexpr float Exposure[4] = { 0.f, 0.4f, 0.7f, 1.f };
	return Exposure[VisiblePoints];
}

void ANoFearAIController::ForgetStaleActors()
{
	const double Now = GetNow();
	for (auto It = KnownActors.CreateIterator(); It; ++It)
	{
		AActor* Actor = It.Key().Get();
		const FNoFearKnownActor& Knowledge = It.Value();
		const bool bDead = !Actor || UNoFearHealthComponent::IsActorDead(Actor);
		const double LastContact = FMath::Max3(Knowledge.LastSeenTime, Knowledge.LastHeardTime, Knowledge.LastAttackedTime);
		const bool bStale = !Knowledge.bInView && Now - LastContact > TargetMemoryTime;
		// Шкала остыла до нуля и врага не видно — тихо забываем (бот так и не понял, что это был враг).
		const bool bDissipated = !Knowledge.bInView && Knowledge.Awareness <= 0.f;
		if (!bDead && !bStale && !bDissipated)
		{
			continue;
		}
		// Цель не погибла, а потерялась: проверить, где её видели последней, и осмотреться рядом.
		if (!bDead && Actor == CurrentTarget.Get() && bInvestigateLostTarget)
		{
			BeginSearch(Knowledge.LastKnownLocation, Knowledge.LastKnownVelocity, SearchPointCount);
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
	// Упавшего раненым не добивают, пока он сам не стреляет по этому боту.
	if (UNoFearHealthComponent::IsActorDowned(Candidate))
	{
		constexpr double DownedProvokeTime = 3.0;
		const FNoFearKnownActor* Knowledge = KnownActors.Find(const_cast<AActor*>(Candidate));
		if (!Knowledge || GetNow() - Knowledge->LastAttackedTime > DownedProvokeTime)
		{
			return false;
		}
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
	if (NewTarget)
	{
		// Нашёл — поиск и настороженность больше не нужны.
		EndSearch();
		SuspiciousUntil = -1.0e9;
	}
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
	if (!CanInvestigateLocation(Location))
	{
		return;
	}
	if (UBlackboardComponent* BlackboardComp = GetBlackboardComponent())
	{
		BlackboardComp->SetValueAsVector(FNoFearBotKeys::InvestigateLocation, Location);
	}
}

// --- Тревога ---

void ANoFearAIController::UpdateAlertState()
{
	bool bAnyAware = false;
	bool bAnySuspicious = false;
	for (const TPair<TWeakObjectPtr<AActor>, FNoFearKnownActor>& Pair : KnownActors)
	{
		if (!Pair.Key.IsValid() || !UNoFearTeamLibrary::AreHostile(GetPawn(), Pair.Key.Get()))
		{
			continue;
		}
		bAnyAware |= Pair.Value.Awareness >= 1.f;
		bAnySuspicious |= Pair.Value.Awareness >= SuspiciousThreshold;
	}

	ENoFearAlertState NewState = ENoFearAlertState::Calm;
	if (CurrentTarget.IsValid() || (!bCanAttack && bAnyAware))
	{
		NewState = ENoFearAlertState::Combat;
	}
	else if (GetNow() < SuspiciousUntil || bAnySuspicious)
	{
		NewState = ENoFearAlertState::Suspicious;
	}
	else if (bSearching)
	{
		NewState = ENoFearAlertState::Search;
	}
	SetAlertState(NewState);
}

void ANoFearAIController::SetAlertState(ENoFearAlertState NewState)
{
	if (NewState == AlertState)
	{
		return;
	}
	const ENoFearAlertState OldState = AlertState;
	AlertState = NewState;
	OnAlertStateChanged.Broadcast(this, NewState, OldState);
}

void ANoFearAIController::NoteSuspicion(const FVector& Location, bool bInvestigate)
{
	SuspiciousUntil = GetNow() + SuspiciousHoldTime;
	SuspicionLocation = Location;

	// Идти проверять, только если бот сейчас ничем не занят (не перебиваем уже выбранную точку).
	if (bInvestigate && bCanAttack && bInvestigateNoises && !CurrentTarget.IsValid())
	{
		const UBlackboardComponent* BlackboardComp = GetBlackboardComponent();
		if (BlackboardComp && !BlackboardComp->IsVectorValueSet(FNoFearBotKeys::InvestigateLocation))
		{
			SetInvestigateLocation(Location);
		}
	}
}

void ANoFearAIController::HearNoise(AActor* Source, FVector Location, ENoFearNoiseType Type)
{
	const APawn* MyPawn = GetPawn();
	if (!MyPawn || !Source || Source == MyPawn || UNoFearHealthComponent::IsActorDead(MyPawn) || UNoFearHealthComponent::IsActorDead(Source)
		|| !UNoFearTeamLibrary::AreHostile(MyPawn, Source))
	{
		return;
	}

	FNoFearKnownActor& Knowledge = KnownActors.FindOrAdd(Source);
	Knowledge.LastHeardTime = GetNow();

	if (Type == ENoFearNoiseType::Gunshot)
	{
		// Выстрел выдаёт стрелка: если он в поле зрения — бот уверен сразу, иначе знает хотя бы направление.
		if (Knowledge.bInView)
		{
			Knowledge.Awareness = 1.f;
		}
		else
		{
			Knowledge.Awareness = FMath::Max(Knowledge.Awareness, GunshotHiddenAwareness);
			Knowledge.LastKnownLocation = Location;
		}
		NoteSuspicion(Location, false);
		// Выстрел важнее прежней точки проверки.
		if (bCanAttack && bInvestigateNoises && !CurrentTarget.IsValid())
		{
			SetInvestigateLocation(Location);
		}
		return;
	}

	// Шаги и приземление: подозрение растёт, но уверенным бот становится, только увидев (или если шум совсем рядом).
	const float Distance = FVector::Dist(MyPawn->GetActorLocation(), Location);
	const float Cap = Distance <= CloseFootstepDistance ? 1.f : FootstepAwarenessCap;
	if (Knowledge.Awareness < Cap)
	{
		Knowledge.Awareness = FMath::Min(Cap, Knowledge.Awareness + FootstepAwarenessGain);
	}
	if (!Knowledge.bInView)
	{
		Knowledge.LastKnownLocation = Location;
	}
	NoteSuspicion(Location, true);
}

void ANoFearAIController::ReceiveAlarm(AActor* Threat, FVector ThreatLocation)
{
	const APawn* MyPawn = GetPawn();
	if (!bCanAttack || !MyPawn || !Threat || Threat == MyPawn || UNoFearHealthComponent::IsActorDead(MyPawn) || UNoFearHealthComponent::IsActorDead(Threat)
		|| !UNoFearTeamLibrary::AreHostile(MyPawn, Threat))
	{
		return;
	}

	// Бот узнаёт угрозу и её положение; целью она станет при ближайшем плановом обновлении (звать UpdateBot отсюда незачем).
	FNoFearKnownActor& Knowledge = KnownActors.FindOrAdd(Threat);
	Knowledge.Awareness = 1.f;
	Knowledge.LastHeardTime = GetNow();
	if (!Knowledge.bInView)
	{
		Knowledge.LastKnownLocation = ThreatLocation;
	}
}

void ANoFearAIController::ShareAlarm()
{
	if (AlertShareRadius <= 0.f || !CurrentTarget.IsValid() || !IsTargetVisible())
	{
		return;
	}
	const double Now = GetNow();
	if (Now < NextAlarmTime)
	{
		return;
	}
	NextAlarmTime = Now + AlarmInterval;

	if (UNoFearBotSubsystem* Subsystem = UNoFearBotSubsystem::Get(this))
	{
		Subsystem->BroadcastAlarm(this, CurrentTarget.Get(), CurrentTarget->GetActorLocation());
	}
}

// --- Поиск ---

void ANoFearAIController::BeginSearch(const FVector& Origin, const FVector& Velocity, int32 NumPoints)
{
	bSearching = true;
	SearchEndTime = GetNow() + SearchDuration;
	SearchLingerUntil = 0.0;
	SearchPoints.Reset();

	SetInvestigateLocation(Origin);
	GenerateSearchPoints(Origin, Velocity, NumPoints);
}

void ANoFearAIController::EndSearch()
{
	bSearching = false;
	SearchLingerUntil = 0.0;
	SearchPoints.Reset();
}

void ANoFearAIController::PumpSearch()
{
	if (!bSearching)
	{
		return;
	}
	const double Now = GetNow();
	const UBlackboardComponent* BlackboardComp = GetBlackboardComponent();
	if (CurrentTarget.IsValid() || !BlackboardComp || Now > SearchEndTime)
	{
		EndSearch();
		return;
	}

	// Дерево очищает InvestigateLocation, когда бот дошёл и постоял: тогда даём следующую точку.
	if (BlackboardComp->IsVectorValueSet(FNoFearBotKeys::InvestigateLocation))
	{
		return;
	}
	if (SearchPoints.Num() > 0)
	{
		SetInvestigateLocation(SearchPoints[0]);
		SearchPoints.RemoveAt(0);
		SearchLingerUntil = 0.0;
	}
	else if (SearchLingerUntil <= 0.0)
	{
		SearchLingerUntil = Now + SearchLingerTime;
	}
	else if (Now >= SearchLingerUntil)
	{
		EndSearch();
	}
}

void ANoFearAIController::GenerateSearchPoints(const FVector& Origin, const FVector& Velocity, int32 NumPoints)
{
	UWorld* World = GetWorld();
	UNavigationSystemV1* NavSystem = World ? FNavigationSystem::GetCurrent<UNavigationSystemV1>(World) : nullptr;
	if (!NavSystem || NumPoints <= 0 || SearchRadius <= 0.f)
	{
		return;
	}
	FNavLocation Projected;
	if (!NavSystem->ProjectPointToNavigation(Origin, Projected, FVector(300.f, 300.f, 500.f)))
	{
		return;
	}

	const FVector MoveDirection = Velocity.GetSafeNormal2D();
	const FVector Anchor = GetLeashAnchor();
	const float Leash = GetLeashRadius();
	const FVector EyeOffset(0.f, 0.f, 60.f);
	FCollisionQueryParams Params(SCENE_QUERY_STAT(NoFearSearchPoint), false, GetPawn());

	struct FCandidate
	{
		FVector Location;
		float Score;
	};
	TArray<FCandidate> Candidates;
	constexpr int32 CandidateCount = 8;
	for (int32 Index = 0; Index < CandidateCount; ++Index)
	{
		FNavLocation Random;
		if (!NavSystem->GetRandomReachablePointInRadius(Projected.Location, SearchRadius, Random))
		{
			continue;
		}
		const float DistanceFromOrigin = FVector::Dist2D(Random.Location, Origin);
		if (DistanceFromOrigin < 300.f)
		{
			continue;
		}
		if (Leash > 0.f && FVector::Dist2D(Random.Location, Anchor) > Leash + 500.f)
		{
			continue;
		}

		// Лучше точки по ходу движения цели и те, что не видны из места пропажи (куда можно спрятаться).
		float Score = FMath::FRand() * 0.5f + FMath::Clamp(DistanceFromOrigin / SearchRadius, 0.f, 1.f) * 0.2f;
		if (!MoveDirection.IsNearlyZero())
		{
			Score += FVector::DotProduct((Random.Location - Origin).GetSafeNormal2D(), MoveDirection);
		}
		if (World->LineTraceTestByChannel(Origin + EyeOffset, Random.Location + EyeOffset, ECC_Visibility, Params))
		{
			Score += 0.5f;
		}
		Candidates.Add({ Random.Location, Score });
	}
	Candidates.Sort([](const FCandidate& A, const FCandidate& B) { return A.Score > B.Score; });

	// Жадно берём лучшие, но чтобы точки не лежали кучей.
	constexpr float MinSeparation = 400.f;
	for (const FCandidate& Candidate : Candidates)
	{
		if (SearchPoints.Num() >= NumPoints)
		{
			break;
		}
		const bool bTooClose = SearchPoints.ContainsByPredicate([&](const FVector& Point) { return FVector::Dist2D(Point, Candidate.Location) < MinSeparation; });
		if (!bTooClose)
		{
			SearchPoints.Add(Candidate.Location);
		}
	}
}

// --- Трупы ---

void ANoFearAIController::ScanCorpses()
{
	const APawn* MyPawn = GetPawn();
	const double Now = GetNow();
	if (!bReactToCorpses || !MyPawn || CurrentTarget.IsValid() || Now < NextCorpseScanTime)
	{
		return;
	}
	NextCorpseScanTime = Now + CorpseScanInterval;

	UNoFearBotSubsystem* Subsystem = UNoFearBotSubsystem::Get(this);
	UWorld* World = GetWorld();
	if (!Subsystem || !World || !SightConfig)
	{
		return;
	}

	FVector EyeLocation;
	FRotator EyeRotation;
	GetActorEyesViewPoint(EyeLocation, EyeRotation);
	const FVector Forward = EyeRotation.Vector();
	const float MinDot = FMath::Cos(FMath::DegreesToRadians(SightConfig->PeripheralVisionAngleDegrees));

	for (ANoFearCharacter* Corpse : Subsystem->GetCorpses())
	{
		// Тела врагов не пугают: пугают тела своих и нейтральных.
		if (!Corpse || NoticedCorpses.Contains(Corpse) || UNoFearTeamLibrary::AreHostile(MyPawn, Corpse))
		{
			continue;
		}
		const FVector ToCorpse = Corpse->GetActorLocation() - EyeLocation;
		if (ToCorpse.SizeSquared() > FMath::Square(CorpseNoticeDistance) || FVector::DotProduct(Forward, ToCorpse.GetSafeNormal()) < MinDot)
		{
			continue;
		}
		FCollisionQueryParams Params(SCENE_QUERY_STAT(NoFearCorpseSight), true, MyPawn);
		Params.AddIgnoredActor(Corpse);
		TArray<AActor*> AttachedActors;
		MyPawn->GetAttachedActors(AttachedActors, false, true);
		Corpse->GetAttachedActors(AttachedActors, false, true);
		Params.AddIgnoredActors(AttachedActors);
		if (World->LineTraceTestByChannel(EyeLocation, Corpse->GetActorLocation() + FVector(0.f, 0.f, 30.f), ECC_Visibility, Params))
		{
			continue;
		}
		NoticedCorpses.Add(Corpse);
		if (const UNoFearFearSubsystem* Fear = UNoFearFearSubsystem::Get(this))
		{
			AddFear(Fear->CorpseFear, Fear->CorpseCap, Corpse->GetActorLocation(), Corpse);
		}
		NoteCorpse(Corpse);
		return;
	}
}

void ANoFearAIController::NoteCorpse(ANoFearCharacter* Corpse)
{
	NoteSuspicion(Corpse->GetActorLocation(), false);
	// Подойти к телу и осмотреться: убийца где-то рядом.
	BeginSearch(Corpse->GetActorLocation(), FVector::ZeroVector, SearchPointCount);
}

// --- Урон и восприятие ---

void ANoFearAIController::ReportThreat(AActor* Threat)
{
	APawn* MyPawn = GetPawn();
	if (!Threat || !MyPawn || Threat == MyPawn || UNoFearHealthComponent::IsActorDead(Threat) || UNoFearTeamLibrary::AreFriendly(MyPawn, Threat))
	{
		return;
	}

	// Напавшего узнают сразу, без шкалы.
	FNoFearKnownActor& Knowledge = KnownActors.FindOrAdd(Threat);
	Knowledge.LastAttackedTime = GetNow();
	Knowledge.Awareness = 1.f;
	if (!Knowledge.bInView)
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
		// Память о потерянных ведём сами: сообщение о том, что UE забыл стимул, не нужно.
		if (Stimulus.IsExpired())
		{
			return;
		}
		FNoFearKnownActor& Knowledge = KnownActors.FindOrAdd(Actor);
		Knowledge.bInView = Stimulus.WasSuccessfullySensed();
		if (Knowledge.bInView)
		{
			Knowledge.LastKnownLocation = Actor->GetActorLocation();
			Knowledge.LastKnownVelocity = Actor->GetVelocity();
		}
		else
		{
			// Вышел из поля зрения: запоминаем, где и куда он шёл.
			Knowledge.bVisible = false;
			Knowledge.LastKnownLocation = Stimulus.StimulusLocation;
			if (Knowledge.LastKnownVelocity.IsNearlyZero())
			{
				Knowledge.LastKnownVelocity = Actor->GetVelocity();
			}
		}
		Knowledge.LastSeenTime = GetNow();
	}
	else if (Stimulus.Type == UAISense::GetSenseID<UAISense_Hearing>() && Stimulus.WasSuccessfullySensed())
	{
		// Звук выстрела: Actor — стрелявший.
		HearNoise(Actor, Stimulus.StimulusLocation, Stimulus.Tag == TEXT("Gunshot") ? ENoFearNoiseType::Gunshot : ENoFearNoiseType::Other);
	}
}

void ANoFearAIController::HandlePawnDamaged(UNoFearHealthComponent* Component, const FNoFearDamageInfo& Info)
{
	AActor* Attacker = Info.InstigatorController ? Info.InstigatorController->GetPawn() : nullptr;
	if (!Attacker && Info.DamageCauser)
	{
		Attacker = Info.DamageCauser->GetInstigator() ? Info.DamageCauser->GetInstigator() : Info.DamageCauser.Get();
	}
	if (const UNoFearFearSubsystem* Fear = UNoFearFearSubsystem::Get(this); Fear && GetPawn() && Info.Amount > 0.f)
	{
		AddFear(Fear->DamageFear, Fear->DamageCap, Attacker ? Attacker->GetActorLocation() : GetPawn()->GetActorLocation(), Attacker);
	}
	ReportThreat(Attacker);

	// Остальные поблизости: свои пострадавшего узнают нападавшего, свои нападавшего — пострадавшего.
	if (Attacker && GetPawn())
	{
		if (UNoFearBotSubsystem* Subsystem = UNoFearBotSubsystem::Get(this))
		{
			Subsystem->NotifyAttack(GetPawn(), Attacker);
		}
	}
}

// --- Походка и блэкборд ---

bool ANoFearAIController::ShouldWalk() const
{
	return bWalkWhenCalm && !CurrentTarget.IsValid();
}

void ANoFearAIController::UpdateMovementStance()
{
	// В бою: боком и с прицелом. Насторожен: боком (смотрит на источник), оружие поднято, если оно есть в руках.
	const bool bCombatStance = CurrentTarget.IsValid() && bStrafeInCombat;
	const bool bAlertStance = bCanAttack && !CurrentTarget.IsValid() && AlertState == ENoFearAlertState::Suspicious && bStrafeInCombat;

	bool bArmed = false;
	if (bAlertStance && bRaiseWeaponWhenAlert)
	{
		const ANoFearCharacter* BotCharacter = GetNoFearCharacter();
		const UNoFearWeaponComponent* Weapons = BotCharacter ? BotCharacter->GetWeaponComponent() : nullptr;
		bArmed = Weapons && Weapons->GetCurrentWeapon();
	}

	// Ранен или только что получил пулю: идёт шагом.
	const UNoFearPoseComponent* Pose = BoundPose.Get();
	const bool bSlowed = Pose && Pose->IsSlowed();

	SetMovementFlags(bCombatStance || bAlertStance || bLookAroundStance, bCombatStance || bArmed, ShouldWalk() || bSlowed);
}

void ANoFearAIController::SetLookAroundStance(bool bEnable)
{
	if (bLookAroundStance != bEnable)
	{
		bLookAroundStance = bEnable;
		UpdateMovementStance();
	}
}

void ANoFearAIController::SetCrouching(bool bCrouch)
{
	ACharacter* BotCharacter = Cast<ACharacter>(GetPawn());
	if (!BotCharacter || UNoFearHealthComponent::IsActorDead(BotCharacter))
	{
		return;
	}
	if (bCrouch)
	{
		BotCharacter->Crouch();
	}
	else
	{
		BotCharacter->UnCrouch();
	}
}

bool ANoFearAIController::IsCrouching() const
{
	const ACharacter* BotCharacter = Cast<ACharacter>(GetPawn());
	return BotCharacter && BotCharacter->IsCrouched();
}

void ANoFearAIController::HandleHitReaction(UNoFearPoseComponent* Component, bool bStagger)
{
	UpdateMovementStance();
	if (bStagger && bFreezeWhenStaggered)
	{
		BeginStagger();
	}
}

void ANoFearAIController::BeginStagger()
{
	APawn* MyPawn = GetPawn();
	if (!MyPawn || UNoFearHealthComponent::IsActorIncapacitated(MyPawn))
	{
		return;
	}

	StopMovement();
	if (const ANoFearCharacter* BotCharacter = GetNoFearCharacter())
	{
		// Дерево на паузе и не отпустит спуск само.
		if (UNoFearWeaponComponent* Weapons = BotCharacter->GetWeaponComponent())
		{
			Weapons->StopFire();
		}
	}

	// Пауза дерева замораживает задачи и пересчёт веток; задача MoveTo получает сообщение об отмене пути и закончится после снятия паузы.
	if (UBrainComponent* Brain = GetBrainComponent(); Brain && !bLogicPausedByStagger && !Brain->IsPaused())
	{
		Brain->PauseLogic(TEXT("Stagger"));
		bLogicPausedByStagger = true;
	}
	GetWorldTimerManager().SetTimer(StaggerTimer, this, &ANoFearAIController::PollStagger, 0.1f, true);
}

void ANoFearAIController::PollStagger()
{
	const UNoFearPoseComponent* Pose = BoundPose.Get();
	if (Pose && Pose->IsStaggered() && !UNoFearHealthComponent::IsActorDead(GetPawn()))
	{
		return;
	}
	ReleaseStagger();
}

void ANoFearAIController::ReleaseStagger()
{
	GetWorldTimerManager().ClearTimer(StaggerTimer);
	if (!bLogicPausedByStagger)
	{
		return;
	}
	bLogicPausedByStagger = false;

	// Мёртвого дерево не продолжает: StopLogic уже вызван при смерти. Бегство от гранаты держит паузу дальше.
	UBrainComponent* Brain = GetBrainComponent();
	if (Brain && Brain->IsPaused() && !bLogicPausedByGrenade && !UNoFearHealthComponent::IsActorIncapacitated(GetPawn()))
	{
		Brain->ResumeLogic(TEXT("Stagger over"));
	}
	UpdateMovementStance();
}

void ANoFearAIController::HandleDanger(ENoFearDangerType Type, AActor* Source, FVector ThreatLocation)
{
	if (Type == ENoFearDangerType::Grenade && bEvadeGrenades)
	{
		BeginGrenadeEvade(Cast<ANoFearGrenade>(Source));
	}
}

// --- Страх ---

void ANoFearAIController::ReceiveDanger(ENoFearDangerType Type, AActor* Source, FVector ThreatLocation)
{
	const APawn* MyPawn = GetPawn();
	if (!MyPawn || UNoFearHealthComponent::IsActorDead(MyPawn))
	{
		return;
	}
	if (const UNoFearFearSubsystem* Fear = UNoFearFearSubsystem::Get(this))
	{
		float Amount = 0.f;
		float Cap = 0.f;
		Fear->GetDangerFear(Type, static_cast<float>(FVector::Dist(MyPawn->GetActorLocation(), ThreatLocation)), Amount, Cap);
		// Крик соседа пугает, но дальше не передаётся: иначе крики гоняли бы панику по толпе по кругу.
		AddFear(Amount, Cap, ThreatLocation, Source, Type != ENoFearDangerType::Scream);
	}
	HandleDanger(Type, Source, ThreatLocation);
}

float ANoFearAIController::GetFear() const
{
	return UNoFearFearComponent::GetActorFear(GetPawn());
}

void ANoFearAIController::AddFear(float Amount, float Cap, const FVector& Location, const AActor* Source, bool bDirect)
{
	if (UNoFearFearComponent* Fear = UNoFearFearComponent::FindFear(GetPawn()))
	{
		Fear->AddFearFrom(Amount, Cap, Location, Source, bDirect);
	}
}

void ANoFearAIController::UpdateSightFear(float DeltaTime)
{
	const APawn* MyPawn = GetPawn();
	UNoFearFearComponent* FearComponent = UNoFearFearComponent::FindFear(MyPawn);
	const UNoFearFearSubsystem* Fear = UNoFearFearSubsystem::Get(this);
	if (!FearComponent || !Fear || !FearComponent->CanBeAfraid())
	{
		return;
	}

	const FVector MyLocation = MyPawn->GetActorLocation();
	const AActor* WorstMonster = nullptr;
	float WorstRate = 0.f;
	float WorstCap = 0.f;
	for (TPair<TWeakObjectPtr<AActor>, FNoFearKnownActor>& Pair : KnownActors)
	{
		const AActor* Actor = Pair.Key.Get();
		if (!Actor || !Pair.Value.bVisible || !UNoFearFearSubsystem::IsFrightening(UNoFearTeamLibrary::GetTeam(Actor))
			|| UNoFearHealthComponent::IsActorDead(Actor))
		{
			continue;
		}
		float Shock = 0.f;
		float Rate = 0.f;
		float Cap = 0.f;
		Fear->GetMonsterSightFear(static_cast<float>(FVector::Dist(MyLocation, Actor->GetActorLocation())), Shock, Rate, Cap);
		if (!Pair.Value.bFearShocked)
		{
			Pair.Value.bFearShocked = true;
			FearComponent->AddFearFrom(Shock, 100.f, Actor->GetActorLocation(), Actor);
		}
		if (Rate > WorstRate)
		{
			WorstMonster = Actor;
			WorstRate = Rate;
			WorstCap = Cap;
		}
	}
	if (WorstMonster)
	{
		FearComponent->AddFearFrom(WorstRate * DeltaTime, WorstCap, WorstMonster->GetActorLocation(), WorstMonster);
	}
}

void ANoFearAIController::GetKnownGrenadeThreats(TArray<FVector>& OutLocations, float& OutSafeDistance) const
{
	OutLocations.Reset();
	OutSafeDistance = 0.f;
	for (const TWeakObjectPtr<ANoFearGrenade>& Weak : KnownGrenades)
	{
		const ANoFearGrenade* Grenade = Weak.Get();
		if (!Grenade || Grenade->HasExploded())
		{
			continue;
		}
		OutLocations.Add(Grenade->GetActorLocation());
		const float Margin = Grenade->Data ? Grenade->Data->EvadeMargin : 200.f;
		OutSafeDistance = FMath::Max(OutSafeDistance, Grenade->GetRadius() + Margin);
	}
}

bool ANoFearAIController::IsInGrenadeDanger(const FVector& Location) const
{
	for (const TWeakObjectPtr<ANoFearGrenade>& Weak : KnownGrenades)
	{
		const ANoFearGrenade* Grenade = Weak.Get();
		if (!Grenade || Grenade->HasExploded())
		{
			continue;
		}
		const float Margin = Grenade->Data ? Grenade->Data->EvadeMargin : 200.f;
		if (FVector::Dist(Location, Grenade->GetActorLocation()) < Grenade->GetRadius() + Margin)
		{
			return true;
		}
	}
	return false;
}

bool ANoFearAIController::FindGrenadeEvadeLocation(FVector& OutLocation) const
{
	APawn* MyPawn = GetPawn();
	TArray<FVector> Threats;
	float SafeDistance = 0.f;
	GetKnownGrenadeThreats(Threats, SafeDistance);
	if (!MyPawn || Threats.Num() == 0)
	{
		return false;
	}
	const FVector Origin = MyPawn->GetActorLocation();
	float Nearest = TNumericLimits<float>::Max();
	FVector NearestThreat = Threats[0];
	for (const FVector& Threat : Threats)
	{
		const float Distance = FVector::Dist2D(Origin, Threat);
		if (Distance < Nearest)
		{
			Nearest = Distance;
			NearestThreat = Threat;
		}
	}

	// Сначала бегство как у мирных (подальше и за укрытие), но на короткую дистанцию: из радиуса и чуть дальше.
	const float MinRun = FMath::Max(SafeDistance - Nearest + 80.f, 200.f);
	FVector Candidate;
	if (UNoFearFleeLibrary::FindFleeLocation(MyPawn, Threats, Candidate, MinRun, MinRun + 500.f, 60.f, 24)
		&& UNoFearFleeLibrary::IsLocationSafe(Candidate, Threats, SafeDistance))
	{
		OutLocation = Candidate;
		return true;
	}

	// Иначе — по прямой от гранаты и веером вбок, до навигации.
	UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
	FVector Away = (Origin - NearestThreat) * FVector(1.f, 1.f, 0.f);
	Away = Away.IsNearlyZero() ? -MyPawn->GetActorForwardVector() : Away.GetSafeNormal();
	for (const float Angle : { 0.f, 35.f, -35.f, 70.f, -70.f, 110.f, -110.f })
	{
		const FVector Raw = NearestThreat + Away.RotateAngleAxis(Angle, FVector::UpVector) * (SafeDistance + 120.f);
		FNavLocation NavLocation;
		if (NavSys && NavSys->ProjectPointToNavigation(Raw, NavLocation, FVector(150.f, 150.f, 300.f))
			&& UNoFearFleeLibrary::IsLocationSafe(NavLocation.Location, Threats, SafeDistance))
		{
			OutLocation = NavLocation.Location;
			return true;
		}
	}
	return false;
}

void ANoFearAIController::BeginGrenadeEvade(ANoFearGrenade* Grenade)
{
	APawn* MyPawn = GetPawn();
	// Раненый лёжа не убегает: ползком всё равно не успеть.
	if (!Grenade || Grenade->HasExploded() || !MyPawn || UNoFearHealthComponent::IsActorIncapacitated(MyPawn))
	{
		return;
	}
	KnownGrenades.RemoveAll([](const TWeakObjectPtr<ANoFearGrenade>& Weak) { return !Weak.IsValid() || Weak->HasExploded(); });
	KnownGrenades.AddUnique(Grenade);

	if (!IsInGrenadeDanger(MyPawn->GetActorLocation()))
	{
		return;
	}
	GrenadeEvadeTimeout = FMath::Max(GrenadeEvadeTimeout, GetNow() + Grenade->GetTimeToExplode() + 1.5);
	GrenadeClearSince = -1.0;

	// Уже бежит, и точка по-прежнему вне всех известных гранат — бег не трогаем.
	if (bEvadingGrenade && bHasGrenadeEvadeGoal && !IsInGrenadeDanger(GrenadeEvadeGoal))
	{
		return;
	}

	if (!bEvadingGrenade)
	{
		bEvadingGrenade = true;
		if (const ANoFearCharacter* BotCharacter = GetNoFearCharacter())
		{
			if (UNoFearWeaponComponent* Weapons = BotCharacter->GetWeaponComponent())
			{
				Weapons->StopFire();
				Weapons->SetAiming(false);
			}
		}
		if (UBrainComponent* Brain = GetBrainComponent(); Brain && !Brain->IsPaused())
		{
			Brain->PauseLogic(TEXT("Grenade"));
		}
		bLogicPausedByGrenade = true;
		GetWorldTimerManager().SetTimer(GrenadeEvadeTimer, this, &ANoFearAIController::PollGrenadeEvade, 0.2f, true);
	}

	bHasGrenadeEvadeGoal = FindGrenadeEvadeLocation(GrenadeEvadeGoal);
	if (bHasGrenadeEvadeGoal && !UNoFearPoseComponent::IsActorStaggered(MyPawn))
	{
		MoveToLocation(GrenadeEvadeGoal, 40.f, /*bStopOnOverlap=*/false, /*bUsePathfinding=*/true, /*bProjectDestinationToNavigation=*/true, /*bCanStrafe=*/false);
	}
	ClearFocus(EAIFocusPriority::Gameplay);
	UpdateMovementStance();
}

void ANoFearAIController::PollGrenadeEvade()
{
	APawn* MyPawn = GetPawn();
	if (!MyPawn || UNoFearHealthComponent::IsActorDead(MyPawn))
	{
		EndGrenadeEvade();
		return;
	}
	const double Now = GetNow();
	if (!IsInGrenadeDanger(MyPawn->GetActorLocation()))
	{
		// Чуть постоять за радиусом: граната может откатиться.
		if (GrenadeClearSince < 0.0)
		{
			GrenadeClearSince = Now;
		}
		if (Now - GrenadeClearSince >= 0.3)
		{
			EndGrenadeEvade();
		}
		return;
	}
	GrenadeClearSince = -1.0;
	if (Now > GrenadeEvadeTimeout)
	{
		EndGrenadeEvade();
		return;
	}
	if (UNoFearPoseComponent::IsActorStaggered(MyPawn))
	{
		return;
	}
	// Пошатывание остановило бег, или точка оказалась под другой гранатой: бежать дальше.
	if (GetMoveStatus() == EPathFollowingStatus::Idle || !bHasGrenadeEvadeGoal || IsInGrenadeDanger(GrenadeEvadeGoal))
	{
		bHasGrenadeEvadeGoal = FindGrenadeEvadeLocation(GrenadeEvadeGoal);
		if (bHasGrenadeEvadeGoal)
		{
			MoveToLocation(GrenadeEvadeGoal, 40.f, false, true, true, false);
		}
	}
}

void ANoFearAIController::EndGrenadeEvade()
{
	GetWorldTimerManager().ClearTimer(GrenadeEvadeTimer);
	if (!bEvadingGrenade && !bLogicPausedByGrenade)
	{
		return;
	}
	bEvadingGrenade = false;
	bHasGrenadeEvadeGoal = false;
	GrenadeEvadeTimeout = 0.0;
	GrenadeClearSince = -1.0;

	const bool bWasPaused = bLogicPausedByGrenade;
	bLogicPausedByGrenade = false;
	UBrainComponent* Brain = GetBrainComponent();
	if (bWasPaused && Brain && Brain->IsPaused() && !bLogicPausedByStagger && !UNoFearHealthComponent::IsActorIncapacitated(GetPawn()))
	{
		StopMovement();
		Brain->ResumeLogic(TEXT("Grenade over"));
	}
	if (GetPawn())
	{
		UpdateMovementStance();
	}
}

void ANoFearAIController::Shout(FText Text, float Duration)
{
	ShoutText = Text;
	ShoutUntil = GetNow() + FMath::Max(Duration, 0.f);
}

FText ANoFearAIController::GetShoutText() const
{
	return GetNow() < ShoutUntil ? ShoutText : FText::GetEmpty();
}

void ANoFearAIController::SetMovementFlags(bool bStrafe, bool bAim, bool bWalk, bool bSprint)
{
	if (bEvadingGrenade)
	{
		// Кто бы ни выставлял походку (наследники тоже), от гранаты бегут спринтом и лицом по ходу.
		bStrafe = false;
		bAim = false;
		bWalk = false;
		bSprint = true;
	}
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
		else if (FieldName.StartsWith(TEXT("WantsToSprint")))
		{
			It->SetPropertyValue_InContainer(State, bSprint);
		}
	}
}

void ANoFearAIController::WriteBlackboard()
{
	if (UBlackboardComponent* BlackboardComp = GetBlackboardComponent())
	{
		BlackboardComp->SetValueAsVector(FNoFearBotKeys::HomeLocation, HomeLocation);
		BlackboardComp->SetValueAsInt(FNoFearBotKeys::AlertState, static_cast<int32>(AlertState));
	}
}

void ANoFearAIController::DrawDebug() const
{
#if ENABLE_DRAW_DEBUG
	if (CVarAIDebug.GetValueOnGameThread() <= 0)
	{
		return;
	}
	const APawn* MyPawn = GetPawn();
	UWorld* World = GetWorld();
	if (!MyPawn || !World)
	{
		return;
	}

	const float Duration = GetThinkInterval() + 0.05f;
	const FColor Color = GetAlertColor(AlertState);
	const FVector Location = MyPawn->GetActorLocation();

	FString Text = FString::Printf(TEXT("%s  %.2f"), GetAlertName(AlertState), GetTopAwareness());
	if (SearchPoints.Num() > 0)
	{
		Text += FString::Printf(TEXT("  search +%d"), SearchPoints.Num());
	}
	if (const AActor* Target = CurrentTarget.Get())
	{
		Text += FString::Printf(TEXT("\n-> %s%s"), *Target->GetName(), IsTargetVisible() ? TEXT("") : TEXT(" (lost)"));
		DrawDebugLine(World, Location, Target->GetActorLocation(), Color, false, Duration, 0, 2.f);
	}
	if (const UNoFearPoseComponent* Pose = BoundPose.Get())
	{
		const FString PoseText = Pose->DescribeState();
		if (!PoseText.IsEmpty())
		{
			Text += TEXT("\n") + PoseText;
		}
	}
	if (IsCrouching())
	{
		Text += TEXT("  [crouch]");
	}
	DrawDebugString(World, Location + FVector(0.f, 0.f, 140.f), Text, nullptr, Color, Duration, true);

	if (const UBlackboardComponent* BlackboardComp = GetBlackboardComponent();
		BlackboardComp && BlackboardComp->IsVectorValueSet(FNoFearBotKeys::InvestigateLocation))
	{
		const FVector Investigate = BlackboardComp->GetValueAsVector(FNoFearBotKeys::InvestigateLocation);
		DrawDebugLine(World, Location, Investigate, FColor::Yellow, false, Duration, 0, 1.f);
		DrawDebugSphere(World, Investigate, 50.f, 8, FColor::Yellow, false, Duration);
	}
	for (const FVector& Point : SearchPoints)
	{
		DrawDebugSphere(World, Point, 40.f, 8, FColor::Orange, false, Duration);
	}
#endif
}
