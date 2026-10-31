#include "AI/NoFearMonsterAIController.h"

#include "AI/NoFearBotSubsystem.h"
#include "AI/NoFearMeleeComponent.h"
#include "Animation/NoFearPoseComponent.h"
#include "BehaviorTree/BehaviorTree.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "Core/NoFearCharacter.h"
#include "Core/NoFearHealthComponent.h"
#include "Core/NoFearTeam.h"
#include "Perception/AISenseConfig_Hearing.h"
#include "Perception/AISenseConfig_Sight.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	/** Ближе этого к цели монстр без жетона кружит шагом боком, а не бежит. */
	constexpr float CircleStanceDistance = 900.f;
	/** Ближе этого — только что ударил и отходит на кольцо: пятится бегом, не сводя глаз с цели. */
	constexpr float CircleBackOffDistance = 330.f;
}

ANoFearMonsterAIController::ANoFearMonsterAIController(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// Бежит к цели лицом по ходу; прицела и стрейфа у монстра нет.
	bStrafeInCombat = false;
	bWalkWhenCalm = true;
	LeashRadius = 0.f;
	MaxTargetDistance = 4000.f;

	// Зовёт своих, когда на него напали (NotifyAttack). Рык стаи — отдельно, PackAlarmRadius.
	AlertShareRadius = 3000.f;

	// Видит хуже стрелков, зато слышит дальше и острее.
	SightConfig->SightRadius = 1800.f;
	SightConfig->LoseSightRadius = 2300.f;
	SightConfig->PeripheralVisionAngleDegrees = 70.f;
	HearingConfig->HearingRange = 5000.f;
	HearingRangeScale = 1.75f;
	FootstepAwarenessGain = 0.45f;
	FootstepAwarenessCap = 0.95f;

	static ConstructorHelpers::FObjectFinderOptional<UBehaviorTree> Tree(TEXT("/Game/AI/BT_NoFear_Monster.BT_NoFear_Monster"));
	BehaviorTree = Tree.Get();
}

UNoFearMeleeComponent* ANoFearMonsterAIController::GetMeleeComponent() const
{
	return GetPawn() ? GetPawn()->FindComponentByClass<UNoFearMeleeComponent>() : nullptr;
}

bool ANoFearMonsterAIController::IsAggro() const
{
	return GetPawn() && UNoFearTeamLibrary::GetTeam(GetPawn()) == ENoFearTeam::AggroMonster;
}

void ANoFearMonsterAIController::OnPossess(APawn* InPawn)
{
	if (InPawn && bAddMeleeComponentIfMissing && !InPawn->FindComponentByClass<UNoFearMeleeComponent>())
	{
		UNoFearMeleeComponent* Melee = NewObject<UNoFearMeleeComponent>(InPawn, TEXT("MeleeComponent"));
		Melee->RegisterComponent();
	}

	PackMembers.Reset();
	PackLeader.Reset();
	Grudges.Reset();
	bHasAttackToken = false;
	TokenTarget.Reset();
	bRoaring = false;
	NextPackUpdateTime = 0.0;

	Super::OnPossess(InPawn);

	if (UNoFearMeleeComponent* Melee = GetMeleeComponent())
	{
		Melee->OnAttackFinished.AddUniqueDynamic(this, &ANoFearMonsterAIController::HandleAttackFinished);
	}
}

void ANoFearMonsterAIController::OnUnPossess()
{
	if (UNoFearBotSubsystem* Subsystem = UNoFearBotSubsystem::Get(this); Subsystem && GetPawn())
	{
		Subsystem->ReleaseAllAttackTokens(GetPawn());
	}
	if (UNoFearMeleeComponent* Melee = GetMeleeComponent())
	{
		Melee->OnAttackFinished.RemoveDynamic(this, &ANoFearMonsterAIController::HandleAttackFinished);
	}
	GetWorldTimerManager().ClearTimer(RoarTimer);
	bRoaring = false;
	Super::OnUnPossess();
}

// --- Обновление ---

void ANoFearMonsterAIController::UpdateBot()
{
	APawn* MyPawn = GetPawn();
	if (!MyPawn || UNoFearHealthComponent::IsActorDead(MyPawn))
	{
		if (MyPawn && bHasAttackToken)
		{
			if (UNoFearBotSubsystem* Subsystem = UNoFearBotSubsystem::Get(this))
			{
				Subsystem->ReleaseAllAttackTokens(MyPawn);
			}
			bHasAttackToken = false;
		}
		return;
	}

	if (GetNow() >= NextPackUpdateTime)
	{
		UpdatePack();
		NextPackUpdateTime = GetNow() + PackUpdateInterval;
	}
	UpdateGrudges();

	if (UNoFearMeleeComponent* Melee = GetMeleeComponent())
	{
		Melee->CooldownScale = IsAggro() ? AggroCooldownScale : 1.f;
		// Компонент мог появиться (или смениться) уже после OnPossess.
		if (!Melee->OnAttackFinished.IsAlreadyBound(this, &ANoFearMonsterAIController::HandleAttackFinished))
		{
			Melee->OnAttackFinished.AddDynamic(this, &ANoFearMonsterAIController::HandleAttackFinished);
		}
	}

	Super::UpdateBot();

	UpdateAttackToken();
	// Жетон мог смениться после того, как база записала блэкборд.
	SetBlackboardBool(FNoFearBotKeys::HasAttackToken, bHasAttackToken);
}

void ANoFearMonsterAIController::UpdatePack()
{
	PackMembers.Reset();
	PackLeader = this;

	const APawn* MyPawn = GetPawn();
	UNoFearBotSubsystem* Subsystem = UNoFearBotSubsystem::Get(this);
	if (!MyPawn || !Subsystem || IsAggro())
	{
		PackMembers.Add(this);
		return;
	}

	const ANoFearCharacter* MyCharacter = Cast<ANoFearCharacter>(MyPawn);
	const FName MyPack = MyCharacter ? MyCharacter->PackName : NAME_None;

	// Кандидаты: живые не-агрессивные монстры-союзники.
	TArray<ANoFearMonsterAIController*> Candidates;
	for (ANoFearAIController* Bot : Subsystem->GetBots())
	{
		ANoFearMonsterAIController* Monster = Cast<ANoFearMonsterAIController>(Bot);
		const APawn* OtherPawn = Monster ? Monster->GetPawn() : nullptr;
		if (!OtherPawn || UNoFearHealthComponent::IsActorDead(OtherPawn) || Monster->IsAggro() || !UNoFearTeamLibrary::AreFriendly(MyPawn, OtherPawn))
		{
			continue;
		}
		const ANoFearCharacter* OtherCharacter = Cast<ANoFearCharacter>(OtherPawn);
		if ((OtherCharacter ? OtherCharacter->PackName : NAME_None) == MyPack)
		{
			Candidates.Add(Monster);
		}
	}

	TArray<ANoFearMonsterAIController*> Pack;
	if (!MyPack.IsNone())
	{
		// Именованная стая — все с тем же именем, где бы ни стояли.
		Pack = Candidates;
	}
	else
	{
		// Безымянная: все, до кого можно дойти цепочкой соседей ближе PackFormRadius.
		Pack.Add(this);
		const float RadiusSquared = FMath::Square(PackFormRadius);
		for (int32 Index = 0; Index < Pack.Num(); ++Index)
		{
			const FVector From = Pack[Index]->GetPawn()->GetActorLocation();
			for (ANoFearMonsterAIController* Candidate : Candidates)
			{
				if (!Pack.Contains(Candidate) && FVector::DistSquared2D(From, Candidate->GetPawn()->GetActorLocation()) <= RadiusSquared)
				{
					Pack.Add(Candidate);
				}
			}
		}
	}
	if (!Pack.Contains(this))
	{
		Pack.Add(this);
	}

	// Вожак — старейший (наименьший UniqueID тела): все члены стаи выбирают одного и того же.
	ANoFearMonsterAIController* Leader = this;
	for (ANoFearMonsterAIController* Member : Pack)
	{
		PackMembers.Add(Member);
		if (Member->GetPawn()->GetUniqueID() < Leader->GetPawn()->GetUniqueID())
		{
			Leader = Member;
		}
	}
	PackLeader = Leader;
}

void ANoFearMonsterAIController::UpdateGrudges()
{
	const double Now = GetNow();
	for (auto It = Grudges.CreateIterator(); It; ++It)
	{
		if (!It.Key().IsValid() || UNoFearHealthComponent::IsActorDead(It.Key().Get()) || Now - It.Value() > GrudgeTime)
		{
			It.RemoveCurrent();
		}
	}
	if (IsAggro())
	{
		return;
	}

	for (TPair<TWeakObjectPtr<AActor>, FNoFearKnownActor>& Pair : KnownActors)
	{
		AActor* Actor = Pair.Key.Get();
		if (!Actor)
		{
			continue;
		}
		// Кто нападал — тот обидчик.
		if (Now - Pair.Value.LastAttackedTime <= 1.0)
		{
			Grudges.Add(Pair.Key, Now);
		}
		// Обидчика узнаёт с первого взгляда.
		if (Pair.Value.bInView && Grudges.Contains(Pair.Key))
		{
			Pair.Value.Awareness = 1.f;
		}
	}
}

void ANoFearMonsterAIController::UpdateAttackToken()
{
	APawn* MyPawn = GetPawn();
	AActor* Target = CurrentTarget.Get();
	UNoFearBotSubsystem* Subsystem = UNoFearBotSubsystem::Get(this);

	if (TokenTarget.IsValid() && TokenTarget.Get() != Target && Subsystem)
	{
		Subsystem->ReleaseAttackToken(MyPawn, TokenTarget.Get());
		TokenTarget.Reset();
	}

	if (!Target)
	{
		bHasAttackToken = false;
		return;
	}
	if (IsAggro() || !bUseAttackTokens || !Subsystem)
	{
		bHasAttackToken = true;
		return;
	}
	bHasAttackToken = Subsystem->RequestAttackToken(MyPawn, Target);
	if (bHasAttackToken)
	{
		TokenTarget = Target;
	}
}

void ANoFearMonsterAIController::HandleAttackFinished(AActor* Target)
{
	if (IsAggro() || !bUseAttackTokens || !TokenTarget.IsValid())
	{
		return;
	}
	UNoFearBotSubsystem* Subsystem = UNoFearBotSubsystem::Get(this);
	if (!Subsystem)
	{
		return;
	}
	// Ударил — уступи жетон тому, кто ждёт. Никто не ждёт — жетон сразу вернётся.
	AActor* Held = TokenTarget.Get();
	Subsystem->ReleaseAttackToken(GetPawn(), Held, true);
	bHasAttackToken = Subsystem->RequestAttackToken(GetPawn(), Held);
	if (!bHasAttackToken)
	{
		TokenTarget.Reset();
	}
	SetBlackboardBool(FNoFearBotKeys::HasAttackToken, bHasAttackToken);
}

// --- Цели и территория ---

FVector ANoFearMonsterAIController::GetTerritoryCenter() const
{
	const ANoFearMonsterAIController* Leader = PackLeader.Get();
	return Leader && Leader != this ? Leader->HomeLocation : HomeLocation;
}

bool ANoFearMonsterAIController::IsOutsideTerritory(FVector Location) const
{
	if (IsAggro() || TerritoryRadius <= 0.f)
	{
		return false;
	}
	return FVector::DistSquared2D(Location, GetTerritoryCenter()) > FMath::Square(TerritoryRadius + TerritoryMargin);
}

bool ANoFearMonsterAIController::HasGrudgeAgainst(const AActor* Actor) const
{
	return Actor && Grudges.Contains(Actor);
}

bool ANoFearMonsterAIController::IsReturningHome() const
{
	const UBlackboardComponent* BlackboardComp = GetBlackboardComponent();
	return BlackboardComp && BlackboardComp->GetValueAsBool(FNoFearBotKeys::ReturnHome);
}

bool ANoFearMonsterAIController::CanInvestigateLocation(const FVector& Location) const
{
	return !IsOutsideTerritory(Location);
}

bool ANoFearMonsterAIController::IsValidTarget(const AActor* Candidate) const
{
	if (!Super::IsValidTarget(Candidate))
	{
		return false;
	}
	if (IsAggro() || TerritoryRadius <= 0.f)
	{
		return true;
	}
	// Новую цель берёт только на своей территории, уже выбранную гонит ещё TerritoryMargin за границу.
	const float Limit = TerritoryRadius + (Candidate == CurrentTarget.Get() ? TerritoryMargin : 0.f);
	return FVector::DistSquared2D(Candidate->GetActorLocation(), GetTerritoryCenter()) <= FMath::Square(Limit);
}

float ANoFearMonsterAIController::ScoreTarget(const AActor* Candidate, const FNoFearKnownActor& Knowledge) const
{
	if (IsAggro())
	{
		// Ближайший, кто бы он ни был; текущую цель держит слабо.
		const float Distance = FVector::Dist(GetPawn()->GetActorLocation(), Candidate->GetActorLocation());
		float Score = 2.f * (1.f - FMath::Clamp(Distance / FMath::Max(MaxTargetDistance, 1.f), 0.f, 1.f));
		if (Knowledge.bVisible)
		{
			Score += 0.3f;
		}
		if (Candidate == CurrentTarget.Get())
		{
			Score += AggroTargetStickiness;
		}
		return Score;
	}

	float Score = Super::ScoreTarget(Candidate, Knowledge);
	if (HasGrudgeAgainst(Candidate))
	{
		Score += GrudgeScoreBonus;
	}
	return Score;
}

void ANoFearMonsterAIController::HandleTargetChanged(AActor* NewTarget, AActor* OldTarget)
{
	Super::HandleTargetChanged(NewTarget, OldTarget);

	UBlackboardComponent* BlackboardComp = GetBlackboardComponent();
	if (NewTarget)
	{
		SetBlackboardBool(FNoFearBotKeys::ReturnHome, false);
		// Заметил добычу из спокойствия — рык, стая поднимается.
		if (!OldTarget && GetNow() >= NextRoarTime)
		{
			Roar();
		}
		return;
	}

	// Цель ушла за границу территории: бросить, запомнить и вернуться домой.
	if (OldTarget && !UNoFearHealthComponent::IsActorDead(OldTarget) && IsOutsideTerritory(OldTarget->GetActorLocation()))
	{
		Grudges.Add(OldTarget, GetNow());
		if (BlackboardComp)
		{
			BlackboardComp->ClearValue(FNoFearBotKeys::InvestigateLocation);
		}
		SetBlackboardBool(FNoFearBotKeys::ReturnHome, true);
	}
}

// --- Рык и стая ---

void ANoFearMonsterAIController::Roar()
{
	APawn* MyPawn = GetPawn();
	if (!MyPawn || UNoFearHealthComponent::IsActorDead(MyPawn))
	{
		return;
	}
	bRoaring = true;
	++RoarCount;
	NextRoarTime = GetNow() + RoarCooldown;
	StopMovement();
	SetBlackboardBool(FNoFearBotKeys::IsRoaring, true);
	GetWorldTimerManager().SetTimer(RoarTimer, this, &ANoFearMonsterAIController::EndRoar, FMath::Max(RoarDuration, 0.05f), false);

	if (const ANoFearCharacter* MyCharacter = Cast<ANoFearCharacter>(MyPawn))
	{
		if (UNoFearPoseComponent* Pose = MyCharacter->GetPoseComponent())
		{
			Pose->PlayPose(ENoFearPose::Roar, RoarDuration * 0.75f);
		}
	}

	UNoFearBotSubsystem::ReportNoise(this, MyPawn, MyPawn->GetActorLocation(), RoarNoiseRange, ENoFearNoiseType::Other);
	if (AActor* Target = CurrentTarget.Get())
	{
		AlarmPack(Target);
		NextPackAlarmTime = GetNow() + AlarmInterval;
	}
}

void ANoFearMonsterAIController::EndRoar()
{
	bRoaring = false;
	SetBlackboardBool(FNoFearBotKeys::IsRoaring, false);
}

void ANoFearMonsterAIController::AlarmPack(AActor* Threat)
{
	const APawn* MyPawn = GetPawn();
	if (!MyPawn || !Threat)
	{
		return;
	}
	const float RadiusSquared = FMath::Square(PackAlarmRadius);
	for (const TWeakObjectPtr<ANoFearMonsterAIController>& MemberPtr : PackMembers)
	{
		ANoFearMonsterAIController* Member = MemberPtr.Get();
		const APawn* MemberPawn = Member ? Member->GetPawn() : nullptr;
		if (Member && Member != this && MemberPawn && FVector::DistSquared(MemberPawn->GetActorLocation(), MyPawn->GetActorLocation()) <= RadiusSquared)
		{
			// Поднятый рыком не рычит сам: рычит тот, кто заметил.
			Member->NextRoarTime = FMath::Max(Member->NextRoarTime, GetNow() + RoarCooldown);
			Member->ReceiveAlarm(Threat, Threat->GetActorLocation());
		}
	}
}

void ANoFearMonsterAIController::ShareAlarm()
{
	// Пока цель видна, время от времени напоминаем стае, где она (только своей стае, не всем монстрам).
	if (!CurrentTarget.IsValid() || !IsTargetVisible() || GetNow() < NextPackAlarmTime)
	{
		return;
	}
	NextPackAlarmTime = GetNow() + AlarmInterval;
	AlarmPack(CurrentTarget.Get());
}

APawn* ANoFearMonsterAIController::GetPackLeader() const
{
	const ANoFearMonsterAIController* Leader = PackLeader.Get();
	return Leader ? Leader->GetPawn() : GetPawn();
}

bool ANoFearMonsterAIController::IsPackLeader() const
{
	return !PackLeader.IsValid() || PackLeader.Get() == this;
}

int32 ANoFearMonsterAIController::GetPackSize() const
{
	int32 Count = 0;
	for (const TWeakObjectPtr<ANoFearMonsterAIController>& Member : PackMembers)
	{
		Count += Member.IsValid() && Member->GetPawn() && !UNoFearHealthComponent::IsActorDead(Member->GetPawn()) ? 1 : 0;
	}
	return FMath::Max(Count, 1);
}

// --- Походка и блэкборд ---

bool ANoFearMonsterAIController::IsCircling() const
{
	const APawn* MyPawn = GetPawn();
	const AActor* Target = CurrentTarget.Get();
	return MyPawn && Target && !bHasAttackToken
		&& FVector::DistSquared2D(MyPawn->GetActorLocation(), Target->GetActorLocation()) <= FMath::Square(CircleStanceDistance);
}

bool ANoFearMonsterAIController::ShouldWalk() const
{
	// Шум проверяет бегом, спокойно бродит и возвращается домой шагом.
	const UBlackboardComponent* BlackboardComp = GetBlackboardComponent();
	const bool bInvestigating = BlackboardComp && BlackboardComp->IsVectorValueSet(FNoFearBotKeys::InvestigateLocation)
		&& !BlackboardComp->GetValueAsBool(FNoFearBotKeys::ReturnHome);
	return Super::ShouldWalk() && !bInvestigating;
}

void ANoFearMonsterAIController::UpdateMovementStance()
{
	if (IsCircling())
	{
		// Без жетона кружит шагом боком, не сводя глаз с цели; слишком близко — отходит бегом.
		const float Distance = FVector::Dist2D(GetPawn()->GetActorLocation(), CurrentTarget->GetActorLocation());
		SetMovementFlags(true, false, Distance >= CircleBackOffDistance);
		return;
	}
	Super::UpdateMovementStance();
}

void ANoFearMonsterAIController::WriteBlackboard()
{
	Super::WriteBlackboard();
	if (UBlackboardComponent* BlackboardComp = GetBlackboardComponent())
	{
		BlackboardComp->SetValueAsBool(FNoFearBotKeys::HasAttackToken, bHasAttackToken);
		BlackboardComp->SetValueAsBool(FNoFearBotKeys::IsRoaring, bRoaring);

		// Вожак бродит вокруг дома, остальные — вокруг вожака.
		const ANoFearMonsterAIController* Leader = PackLeader.Get();
		const APawn* LeaderPawn = Leader ? Leader->GetPawn() : nullptr;
		const FVector Center = Leader && Leader != this && LeaderPawn ? LeaderPawn->GetActorLocation() : HomeLocation;
		BlackboardComp->SetValueAsVector(FNoFearBotKeys::WanderCenter, Center);
	}
}

void ANoFearMonsterAIController::SetBlackboardBool(FName Key, bool bValue)
{
	if (UBlackboardComponent* BlackboardComp = GetBlackboardComponent())
	{
		BlackboardComp->SetValueAsBool(Key, bValue);
	}
}
