#include "AI/NoFearOpportunitySubsystem.h"

#include "AI/NoFearCompanionAIController.h"
#include "AI/NoFearMonsterAIController.h"
#include "AI/NoFearShooterAIController.h"
#include "AI/NoFearVulnerabilityComponent.h"
#include "Core/NoFearCharacter.h"
#include "Core/NoFearHealthComponent.h"
#include "Core/NoFearTeam.h"
#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"

namespace
{
	TAutoConsoleVariable<int32> CVarMonsterDebug(
		TEXT("NoFear.Monster.Debug"),
		0,
		TEXT("1 — монстры: слабые места (зелёное — открыто, серое — закрыто), вид, цель и окна; метки возможностей над людьми."),
		ECVF_Default);

	constexpr double InfiniteTime = 1.0e12;
	constexpr double IsolatedCacheTime = 0.5;

	bool IsMonsterTeam(ENoFearTeam Team)
	{
		return Team == ENoFearTeam::Monster || Team == ENoFearTeam::AggroMonster;
	}

	FString EnumName(const UEnum* Enum, int64 Value)
	{
		return Enum ? Enum->GetNameStringByValue(Value) : FString::FromInt(int32(Value));
	}

	APawn* GetPlayerPawn(UWorld* World)
	{
		const APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
		return PC ? PC->GetPawn() : nullptr;
	}

	void TagCommand(const TArray<FString>& Args, UWorld* World)
	{
		APawn* Player = GetPlayerPawn(World);
		const int64 Value = Args.Num() > 0 ? StaticEnum<ENoFearOpportunity>()->GetValueByNameString(Args[0]) : INDEX_NONE;
		if (!Player || Value == INDEX_NONE)
		{
			UE_LOG(LogTemp, Warning, TEXT("NoFear.Monster.Tag <KnockedDown|Slimed|Pinned|Blinded|Marked|InAcid> [сек] [self]"));
			return;
		}
		const float Duration = Args.Num() > 1 ? FCString::Atof(*Args[1]) : 5.f;
		AActor* Target = Player;
		if (!(Args.Num() > 2 && Args[2] == TEXT("self")))
		{
			// Ближайший к игроку живой человек, не монстр.
			float BestDistSq = TNumericLimits<float>::Max();
			for (TActorIterator<ANoFearCharacter> It(World); It; ++It)
			{
				if (*It == Player || IsMonsterTeam(UNoFearTeamLibrary::GetTeam(*It)) || UNoFearHealthComponent::IsActorDead(*It))
				{
					continue;
				}
				const float DistSq = FVector::DistSquared(It->GetActorLocation(), Player->GetActorLocation());
				if (DistSq < BestDistSq)
				{
					BestDistSq = DistSq;
					Target = *It;
				}
			}
		}
		UNoFearOpportunitySubsystem::AddTag(Target, ENoFearOpportunity(Value), Duration);
		UE_LOG(LogTemp, Log, TEXT("NoFear.Monster.Tag: %s -> %s на %.1f с"), *Args[0], *GetNameSafe(Target), Duration);
	}

	void WindowCommand(const TArray<FString>& Args, UWorld* World)
	{
		APawn* Player = GetPlayerPawn(World);
		const int64 Value = Args.Num() > 0 ? StaticEnum<ENoFearWeakCondition>()->GetValueByNameString(Args[0]) : INDEX_NONE;
		if (!Player || Value == INDEX_NONE)
		{
			UE_LOG(LogTemp, Warning, TEXT("NoFear.Monster.Window <Exhale|Eating|ArmorBroken|Scream|...> [сек]"));
			return;
		}
		UNoFearVulnerabilityComponent* Best = nullptr;
		float BestDistSq = TNumericLimits<float>::Max();
		for (TActorIterator<ANoFearCharacter> It(World); It; ++It)
		{
			UNoFearVulnerabilityComponent* Vulnerability = It->FindComponentByClass<UNoFearVulnerabilityComponent>();
			const float DistSq = FVector::DistSquared(It->GetActorLocation(), Player->GetActorLocation());
			if (Vulnerability && !UNoFearHealthComponent::IsActorDead(*It) && DistSq < BestDistSq)
			{
				BestDistSq = DistSq;
				Best = Vulnerability;
			}
		}
		if (Best)
		{
			Best->OpenWindow(ENoFearWeakCondition(Value), Args.Num() > 1 ? FCString::Atof(*Args[1]) : 3.f);
		}
	}

	FAutoConsoleCommandWithWorldAndArgs MonsterTagCommand(
		TEXT("NoFear.Monster.Tag"),
		TEXT("Метка возможности на ближайшего к игроку человека: NoFear.Monster.Tag Slimed [сек=5] [self — на героя]."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&TagCommand));

	FAutoConsoleCommandWithWorldAndArgs MonsterWindowCommand(
		TEXT("NoFear.Monster.Window"),
		TEXT("Открыть окно уязвимости ближайшему к игроку монстру: NoFear.Monster.Window Exhale [сек=3]."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&WindowCommand));
}

bool UNoFearOpportunitySubsystem::IsDebugEnabled()
{
	return CVarMonsterDebug.GetValueOnGameThread() != 0;
}

UNoFearOpportunitySubsystem* UNoFearOpportunitySubsystem::Get(const AActor* Actor)
{
	const UWorld* World = Actor ? Actor->GetWorld() : nullptr;
	return World ? World->GetSubsystem<UNoFearOpportunitySubsystem>() : nullptr;
}

bool UNoFearOpportunitySubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

TStatId UNoFearOpportunitySubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UNoFearOpportunitySubsystem, STATGROUP_Tickables);
}

// ---------------------------------------------------------------------------
// Метки
// ---------------------------------------------------------------------------

void UNoFearOpportunitySubsystem::AddTag(AActor* Target, ENoFearOpportunity Tag, float Duration)
{
	UNoFearOpportunitySubsystem* Self = Get(Target);
	if (!Self || Tag == ENoFearOpportunity::Isolated || UNoFearHealthComponent::IsActorDead(Target))
	{
		return;
	}
	const double Until = Duration > 0.f ? Target->GetWorld()->GetTimeSeconds() + Duration : InfiniteTime;
	double& Current = Self->Tags.FindOrAdd(Target).Until.FindOrAdd(Tag, 0.0);
	Current = FMath::Max(Current, Until);
}

void UNoFearOpportunitySubsystem::RemoveTag(AActor* Target, ENoFearOpportunity Tag)
{
	if (UNoFearOpportunitySubsystem* Self = Get(Target))
	{
		if (FTags* Entry = Self->Tags.Find(Target))
		{
			Entry->Until.Remove(Tag);
		}
	}
}

bool UNoFearOpportunitySubsystem::HasTag(const AActor* Target, ENoFearOpportunity Tag)
{
	return GetTimeRemaining(Target, Tag) != 0.f;
}

float UNoFearOpportunitySubsystem::GetTimeRemaining(const AActor* Target, ENoFearOpportunity Tag)
{
	const UNoFearOpportunitySubsystem* Self = Get(Target);
	if (!Self || UNoFearHealthComponent::IsActorDead(Target))
	{
		return 0.f;
	}
	if (Tag == ENoFearOpportunity::Isolated)
	{
		return Self->ComputeIsolated(Target) ? -1.f : 0.f;
	}
	const FTags* Entry = Self->Tags.Find(Target);
	const double* Until = Entry ? Entry->Until.Find(Tag) : nullptr;
	if (!Until)
	{
		return 0.f;
	}
	if (*Until >= InfiniteTime)
	{
		return -1.f;
	}
	return float(FMath::Max(0.0, *Until - Target->GetWorld()->GetTimeSeconds()));
}

float UNoFearOpportunitySubsystem::GetOpportunityBonus(const AActor* Target, const TMap<ENoFearOpportunity, float>& Bonuses)
{
	float Bonus = 0.f;
	for (const TPair<ENoFearOpportunity, float>& Pair : Bonuses)
	{
		if (Pair.Value != 0.f && HasTag(Target, Pair.Key))
		{
			Bonus += Pair.Value;
		}
	}
	return Bonus;
}

bool UNoFearOpportunitySubsystem::ComputeIsolated(const AActor* Target) const
{
	const UWorld* World = GetWorld();
	if (!World || !Target)
	{
		return false;
	}
	const double Now = World->GetTimeSeconds();
	FIsolatedCache& Cache = IsolatedCache.FindOrAdd(Target);
	if (Now - Cache.Time < IsolatedCacheTime)
	{
		return Cache.bIsolated;
	}
	Cache.Time = Now;
	Cache.bIsolated = false;
	if (IsMonsterTeam(UNoFearTeamLibrary::GetTeam(Target)))
	{
		return false;
	}
	const float MaxDistSq = FMath::Square(IsolatedDistance);
	bool bHasFriendNear = false;
	for (TActorIterator<ANoFearCharacter> It(World); It && !bHasFriendNear; ++It)
	{
		if (*It != Target && !UNoFearHealthComponent::IsActorIncapacitated(*It) && UNoFearTeamLibrary::AreFriendly(Target, *It)
			&& FVector::DistSquared(It->GetActorLocation(), Target->GetActorLocation()) <= MaxDistSq)
		{
			bHasFriendNear = true;
		}
	}
	Cache.bIsolated = !bHasFriendNear;
	return Cache.bIsolated;
}

ENoFearTargetCategory UNoFearOpportunitySubsystem::GetTargetCategory(const AActor* Target)
{
	const APawn* Pawn = Cast<APawn>(Target);
	if (!Pawn)
	{
		return ENoFearTargetCategory::Other;
	}
	if (Pawn->IsPlayerControlled())
	{
		return ENoFearTargetCategory::Hero;
	}
	const ENoFearTeam Team = UNoFearTeamLibrary::GetTeam(Pawn);
	if (IsMonsterTeam(Team))
	{
		return ENoFearTargetCategory::Monster;
	}
	const AController* Controller = Pawn->GetController();
	if (Cast<ANoFearCompanionAIController>(Controller))
	{
		return ENoFearTargetCategory::Companion;
	}
	if (Team == ENoFearTeam::Civilian)
	{
		return ENoFearTargetCategory::Civilian;
	}
	if (Cast<ANoFearShooterAIController>(Controller) || Team == ENoFearTeam::Enemy || Team == ENoFearTeam::Ally || Team == ENoFearTeam::Player)
	{
		return ENoFearTargetCategory::Soldier;
	}
	return ENoFearTargetCategory::Other;
}

// ---------------------------------------------------------------------------
// Тик: чистка и отладка
// ---------------------------------------------------------------------------

void UNoFearOpportunitySubsystem::Tick(float DeltaTime)
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	const double Now = World->GetTimeSeconds();
	if (Now >= NextCleanupTime)
	{
		NextCleanupTime = Now + 1.0;
		for (auto It = Tags.CreateIterator(); It; ++It)
		{
			if (!It.Key().IsValid() || UNoFearHealthComponent::IsActorDead(It.Key().Get()))
			{
				It.RemoveCurrent();
				continue;
			}
			for (auto TagIt = It.Value().Until.CreateIterator(); TagIt; ++TagIt)
			{
				if (TagIt.Value() <= Now)
				{
					TagIt.RemoveCurrent();
				}
			}
			if (It.Value().Until.Num() == 0)
			{
				It.RemoveCurrent();
			}
		}
		for (auto It = IsolatedCache.CreateIterator(); It; ++It)
		{
			if (!It.Key().IsValid())
			{
				It.RemoveCurrent();
			}
		}
	}

	if (IsDebugEnabled())
	{
		DrawDebug();
	}
}

void UNoFearOpportunitySubsystem::DrawDebug() const
{
	UWorld* World = GetWorld();
	const UEnum* TagEnum = StaticEnum<ENoFearOpportunity>();
	const UEnum* WindowEnum = StaticEnum<ENoFearWeakCondition>();

	for (TActorIterator<ANoFearCharacter> It(World); It; ++It)
	{
		ANoFearCharacter* Character = *It;
		if (UNoFearHealthComponent::IsActorDead(Character))
		{
			continue;
		}
		const FVector Head = Character->GetActorLocation() + FVector(0.f, 0.f, 120.f);

		if (const ANoFearMonsterAIController* Monster = Cast<ANoFearMonsterAIController>(Character->GetController()))
		{
			FString Text = Monster->GetKindName().ToString();
			if (const AActor* Target = Monster->GetCurrentTarget())
			{
				Text += FString::Printf(TEXT(" -> %s"), *Target->GetName());
			}
			if (const UNoFearVulnerabilityComponent* Vulnerability = Character->FindComponentByClass<UNoFearVulnerabilityComponent>())
			{
				for (int32 Value = 1; Value < WindowEnum->NumEnums() - 1; ++Value)
				{
					if (Vulnerability->IsWindowOpen(ENoFearWeakCondition(Value)))
					{
						Text += TEXT(" [") + EnumName(WindowEnum, Value) + TEXT("]");
					}
				}
			}
			DrawDebugString(World, Head, Text, nullptr, FColor::Orange, 0.f, true);
			continue;
		}

		FString Text;
		for (ENoFearOpportunity Tag : TEnumRange<ENoFearOpportunity>())
		{
			const float Left = GetTimeRemaining(Character, Tag);
			if (Left != 0.f)
			{
				Text += Left > 0.f ? FString::Printf(TEXT("%s %.1f  "), *EnumName(TagEnum, int64(Tag)), Left)
					: EnumName(TagEnum, int64(Tag)) + TEXT("  ");
			}
		}
		if (!Text.IsEmpty())
		{
			DrawDebugString(World, Head, Text, nullptr, FColor::Cyan, 0.f, true);
		}
	}
}
