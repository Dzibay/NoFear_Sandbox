#include "NoFearBotAssetsLibrary.h"

#include "AI/BTTask_NoFearCoverFight.h"
#include "AI/BTTask_NoFearFindLocation.h"
#include "AI/BTTask_NoFearLookAround.h"
#include "AI/BTTask_NoFearMeleeAttack.h"
#include "AI/BTTask_NoFearShoot.h"
#include "AI/NoFearAIController.h"
#include "AI/NoFearCivilianAIController.h"
#include "AI/NoFearCombatComponent.h"
#include "AI/NoFearCompanionAIController.h"
#include "AI/NoFearMeleeComponent.h"
#include "AI/NoFearMonsterAIController.h"
#include "AI/NoFearMonsterKinds.h"
#include "AI/NoFearRecruitableComponent.h"
#include "AI/NoFearShooterAIController.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "BehaviorTree/BehaviorTree.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Bool.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Float.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Int.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Object.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Vector.h"
#include "BehaviorTree/BlackboardData.h"
#include "BehaviorTree/Composites/BTComposite_Selector.h"
#include "BehaviorTree/Composites/BTComposite_Sequence.h"
#include "BehaviorTree/Decorators/BTDecorator_Blackboard.h"
#include "BehaviorTree/Decorators/BTDecorator_ForceSuccess.h"
#include "BehaviorTree/Tasks/BTTask_MoveTo.h"
#include "BehaviorTree/Tasks/BTTask_Wait.h"
#include "BehaviorTreeGraph.h"
#include "BehaviorTreeGraphNode_Composite.h"
#include "BehaviorTreeGraphNode_Decorator.h"
#include "BehaviorTreeGraphNode_Root.h"
#include "BehaviorTreeGraphNode_Task.h"
#include "Core/NoFearCharacter.h"
#include "Core/NoFearHealthComponent.h"
#include "Core/NoFearTeam.h"
#include "EdGraph/EdGraphSchema.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/SCS_Node.h"
#include "Engine/SimpleConstructionScript.h"
#include "Components/CapsuleComponent.h"
#include "HAL/IConsoleManager.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/PackageName.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "UObject/UObjectHash.h"
#include "Weapons/NoFearWeaponComponent.h"
#include "Weapons/NoFearWeaponData.h"

DEFINE_LOG_CATEGORY_STATIC(LogNoFearBotAssets, Log, All);

namespace
{
	const TCHAR* AIFolder = TEXT("/Game/AI");
	const TCHAR* BotsFolder = TEXT("/Game/Blueprints/Bots");
	const TCHAR* BlackboardName = TEXT("BB_NoFearBot");
	const TCHAR* SandboxCharacterClassPath = TEXT("/Game/Blueprints/CBP_SandboxCharacter.CBP_SandboxCharacter_C");
	const TCHAR* RiflePath = TEXT("/Game/Weapons/Data/DA_Rifle.DA_Rifle");

	FString MakeObjectPath(const FString& PackageName)
	{
		return PackageName + TEXT(".") + FPackageName::GetLongPackageAssetName(PackageName);
	}

	bool SaveAsset(UObject* Asset)
	{
		UPackage* Package = Asset->GetPackage();
		Package->MarkPackageDirty();

		const FString Filename = FPackageName::LongPackageNameToFilename(Package->GetName(), FPackageName::GetAssetPackageExtension());
		FSavePackageArgs Args;
		Args.TopLevelFlags = RF_Public | RF_Standalone;
		const bool bSaved = UPackage::SavePackage(Package, Asset, *Filename, Args);
		UE_LOG(LogNoFearBotAssets, Display, TEXT("%s %s"), bSaved ? TEXT("Saved") : TEXT("FAILED to save"), *Package->GetName());
		return bSaved;
	}

	/** Найти ассет или создать новый пустой. bCreated — создан сейчас. */
	template <typename T>
	T* FindOrCreateAsset(const FString& PackageName, bool& bCreated)
	{
		bCreated = false;
		if (FPackageName::DoesPackageExist(PackageName))
		{
			return LoadObject<T>(nullptr, *MakeObjectPath(PackageName));
		}

		UPackage* Package = CreatePackage(*PackageName);
		T* Asset = NewObject<T>(Package, *FPackageName::GetLongPackageAssetName(PackageName), RF_Public | RF_Standalone | RF_Transactional);
		FAssetRegistryModule::AssetCreated(Asset);
		bCreated = true;
		return Asset;
	}

	// --- Блэкборд ---

	struct FKeySpec
	{
		FName Name;
		UClass* KeyType;
		const TCHAR* Description;
	};

	UBlackboardData* GenerateBlackboard(bool& bOk)
	{
		bool bCreated = false;
		UBlackboardData* Blackboard = FindOrCreateAsset<UBlackboardData>(FString(AIFolder) / BlackboardName, bCreated);
		if (!Blackboard)
		{
			UE_LOG(LogNoFearBotAssets, Error, TEXT("Cannot load or create %s"), BlackboardName);
			bOk = false;
			return nullptr;
		}

		const FKeySpec Keys[] = {
			{ FNoFearBotKeys::AttackTarget, UBlackboardKeyType_Object::StaticClass(), TEXT("Текущая цель (пишет контроллер)") },
			{ FNoFearBotKeys::HasLineOfSight, UBlackboardKeyType_Bool::StaticClass(), TEXT("Цель видна (UNoFearCombatComponent)") },
			{ FNoFearBotKeys::CanFire, UBlackboardKeyType_Bool::StaticClass(), TEXT("Можно стрелять: цель видна, реакция прошла (UNoFearCombatComponent)") },
			{ TEXT("IsSuppressed"), UBlackboardKeyType_Bool::StaticClass(), TEXT("Бота прижали огнём (UNoFearCombatComponent)") },
			{ TEXT("Suppression"), UBlackboardKeyType_Float::StaticClass(), TEXT("Подавление 0..1 (UNoFearCombatComponent)") },
			{ FNoFearBotKeys::LastKnownLocation, UBlackboardKeyType_Vector::StaticClass(), TEXT("Где цель видели последний раз") },
			{ FNoFearBotKeys::HomeLocation, UBlackboardKeyType_Vector::StaticClass(), TEXT("Точка, которую бот охраняет") },
			{ FNoFearBotKeys::MoveGoal, UBlackboardKeyType_Vector::StaticClass(), TEXT("Куда идти сейчас (задачи поиска точки)") },
			{ FNoFearBotKeys::InvestigateLocation, UBlackboardKeyType_Vector::StaticClass(), TEXT("Что проверить: шум, место, где пропала цель") },
			{ FNoFearBotKeys::Leader, UBlackboardKeyType_Object::StaticClass(), TEXT("За кем идёт напарник") },
			{ FNoFearBotKeys::IsFollowing, UBlackboardKeyType_Bool::StaticClass(), TEXT("Напарник идёт за лидером") },
			{ FNoFearBotKeys::AlertState, UBlackboardKeyType_Int::StaticClass(), TEXT("Тревога: 0 спокоен, 1 насторожен, 2 бой, 3 поиск (ENoFearAlertState)") },
			{ FNoFearBotKeys::FleeLocation, UBlackboardKeyType_Vector::StaticClass(), TEXT("Куда бежать или отходить (мирные)") },
			{ FNoFearBotKeys::IsPanicking, UBlackboardKeyType_Bool::StaticClass(), TEXT("Мирный в панике и бежит") },
			{ FNoFearBotKeys::IsCowering, UBlackboardKeyType_Bool::StaticClass(), TEXT("Мирный прячется: присел, закрыл голову") },
			{ FNoFearBotKeys::IsWary, UBlackboardKeyType_Bool::StaticClass(), TEXT("Мирный насторожен и отходит шагом") },
			{ FNoFearBotKeys::HasAttackToken, UBlackboardKeyType_Bool::StaticClass(), TEXT("Монстру можно бить цель (жетон атаки)") },
			{ FNoFearBotKeys::IsRoaring, UBlackboardKeyType_Bool::StaticClass(), TEXT("Монстр рычит и стоит") },
			{ FNoFearBotKeys::ReturnHome, UBlackboardKeyType_Bool::StaticClass(), TEXT("Монстр бросил цель за территорией и идёт домой") },
			{ FNoFearBotKeys::WanderCenter, UBlackboardKeyType_Vector::StaticClass(), TEXT("Вокруг чего бродить: дом или вожак стаи") },
			{ FNoFearBotKeys::KeepAway, UBlackboardKeyType_Bool::StaticClass(), TEXT("Плевун отступает, держит дистанцию") },
			{ FNoFearBotKeys::CoverLocation, UBlackboardKeyType_Vector::StaticClass(), TEXT("Укрытие стрелка (не задано — укрытия нет)") },
			{ FNoFearBotKeys::InCover, UBlackboardKeyType_Bool::StaticClass(), TEXT("Стрелок стоит в своём укрытии") },
			{ FNoFearBotKeys::CoverIsLow, UBlackboardKeyType_Bool::StaticClass(), TEXT("Укрытие низкое: сидеть за ним, стрелять стоя") },
			{ FNoFearBotKeys::IsRetreating, UBlackboardKeyType_Bool::StaticClass(), TEXT("Раненый стрелок отходит в дальнее укрытие") },
			{ FNoFearBotKeys::InWeaponRange, UBlackboardKeyType_Bool::StaticClass(), TEXT("Цель в удобной дистанции оружия") },
		};

		bool bChanged = bCreated;
		for (const FKeySpec& Spec : Keys)
		{
			if (Blackboard->Keys.ContainsByPredicate([&Spec](const FBlackboardEntry& Entry) { return Entry.EntryName == Spec.Name; }))
			{
				continue;
			}

			FBlackboardEntry Entry;
			Entry.EntryName = Spec.Name;
			Entry.EntryDescription = Spec.Description;
			Entry.KeyType = NewObject<UBlackboardKeyType>(Blackboard, Spec.KeyType);
			if (UBlackboardKeyType_Object* ObjectKey = Cast<UBlackboardKeyType_Object>(Entry.KeyType))
			{
				ObjectKey->BaseClass = AActor::StaticClass();
			}
			Blackboard->Keys.Add(Entry);
			bChanged = true;
		}

		if (bChanged)
		{
			Blackboard->UpdateParentKeys();
			Blackboard->UpdateKeyIDs();
			bOk &= SaveAsset(Blackboard);
		}
		else
		{
			UE_LOG(LogNoFearBotAssets, Display, TEXT("%s is up to date"), BlackboardName);
		}
		return Blackboard;
	}

	// --- Деревья ---

	struct FProp
	{
		const TCHAR* Name;
		FString Value;
	};

	FString KeyValue(FName KeyName)
	{
		return FString::Printf(TEXT("(SelectedKeyName=\"%s\")"), *KeyName.ToString());
	}

	/** Значение для FValueOrBBKey_Float (Wait, MoveTo в 5.8). */
	FString FloatValue(float Value)
	{
		return FString::Printf(TEXT("(DefaultValue=%f)"), Value);
	}

	/** Значение для обычного float-свойства (наши задачи). */
	FString PlainFloat(float Value)
	{
		return FString::Printf(TEXT("%f"), Value);
	}

	/** Собирает граф дерева так же, как его собрал бы человек в редакторе. Порядок детей — порядок добавления. */
	class FTreeBuilder
	{
	public:
		FTreeBuilder(UBehaviorTree* InTree, UBlackboardData* Blackboard)
			: Tree(InTree)
		{
			const TSubclassOf<UEdGraphSchema> SchemaClass = GetDefault<UBehaviorTreeGraph>()->Schema;
			Graph = CastChecked<UBehaviorTreeGraph>(FBlueprintEditorUtils::CreateNewGraph(Tree, TEXT("Behavior Tree"), UBehaviorTreeGraph::StaticClass(), SchemaClass));
			Tree->BTGraph = Graph;
			GetDefault<UEdGraphSchema>(SchemaClass)->CreateDefaultNodesForGraph(*Graph);
			Graph->OnCreated();
			Graph->Initialize();
			Graph->LockUpdates();

			for (UEdGraphNode* Node : Graph->Nodes)
			{
				if (UBehaviorTreeGraphNode_Root* RootNode = Cast<UBehaviorTreeGraphNode_Root>(Node))
				{
					Root = RootNode;
					break;
				}
			}
			check(Root);
			Root->BlackboardAsset = Blackboard;
			Tree->BlackboardAsset = Blackboard;
		}

		UBehaviorTreeGraphNode* Selector(UBehaviorTreeGraphNode* Parent, const TCHAR* Name)
		{
			return AddNode(Parent, UBehaviorTreeGraphNode_Composite::StaticClass(), UBTComposite_Selector::StaticClass(), {}, Name);
		}

		UBehaviorTreeGraphNode* Sequence(UBehaviorTreeGraphNode* Parent, const TCHAR* Name)
		{
			return AddNode(Parent, UBehaviorTreeGraphNode_Composite::StaticClass(), UBTComposite_Sequence::StaticClass(), {}, Name);
		}

		UBehaviorTreeGraphNode* Task(UBehaviorTreeGraphNode* Parent, UClass* TaskClass, const TArray<FProp>& Props = {}, const TCHAR* Name = nullptr)
		{
			return AddNode(Parent, UBehaviorTreeGraphNode_Task::StaticClass(), TaskClass, Props, Name);
		}

		UBehaviorTreeGraphNode* MoveTo(UBehaviorTreeGraphNode* Parent, FName KeyName, float AcceptableRadius, const TCHAR* Name)
		{
			return Task(Parent, UBTTask_MoveTo::StaticClass(), {
				{ TEXT("BlackboardKey"), KeyValue(KeyName) },
				{ TEXT("AcceptableRadius"), FloatValue(AcceptableRadius) },
				{ TEXT("bObserveBlackboardValue"), TEXT("True") },
			}, Name);
		}

		UBehaviorTreeGraphNode* Wait(UBehaviorTreeGraphNode* Parent, float Time, float RandomDeviation = 0.f)
		{
			return Task(Parent, UBTTask_Wait::StaticClass(), {
				{ TEXT("WaitTime"), FloatValue(Time) },
				{ TEXT("RandomDeviation"), FloatValue(RandomDeviation) },
			});
		}

		/** Условие «ключ задан» (для bool — «true»). Abort: None, Self, LowerPriority, Both. */
		void IfSet(UBehaviorTreeGraphNode* Node, FName KeyName, const TCHAR* AbortMode, const TCHAR* Name)
		{
			Decorator(Node, UBTDecorator_Blackboard::StaticClass(), {
				{ TEXT("BlackboardKey"), KeyValue(KeyName) },
				{ TEXT("BasicOperation"), TEXT("Set") },
				// OperationType — то, что проверяется в игре; редактор выставляет его вместе с BasicOperation.
				{ TEXT("OperationType"), TEXT("0") },
				{ TEXT("FlowAbortMode"), AbortMode },
			}, Name);
		}

		void Decorator(UBehaviorTreeGraphNode* Node, UClass* DecoratorClass, const TArray<FProp>& Props = {}, const TCHAR* Name = nullptr)
		{
			UBehaviorTreeGraphNode_Decorator* DecoratorNode = NewObject<UBehaviorTreeGraphNode_Decorator>(Graph);
			DecoratorNode->ClassData = FGraphNodeClassData(DecoratorClass, FString());
			Node->AddSubNode(DecoratorNode, Graph);
			Configure(DecoratorNode, Props, Name);
		}

		/** Расставить узлы и собрать из графа само дерево. */
		void Finish()
		{
			int32 NextLeafX = 0;
			Layout(Root, 0, NextLeafX);
			Graph->UnlockUpdates();
			Graph->UpdateAsset(UBehaviorTreeGraph::ClearDebuggerFlags | UBehaviorTreeGraph::KeepRebuildCounter);
		}

		bool HasErrors() const { return bHasErrors; }

	private:
		UBehaviorTreeGraphNode* AddNode(UBehaviorTreeGraphNode* Parent, TSubclassOf<UBehaviorTreeGraphNode> GraphNodeClass, UClass* NodeClass, const TArray<FProp>& Props, const TCHAR* Name)
		{
			UBehaviorTreeGraphNode* Node = NewObject<UBehaviorTreeGraphNode>(Graph, GraphNodeClass, NAME_None, RF_Transactional);
			Node->ClassData = FGraphNodeClassData(NodeClass, FString());
			Graph->AddNode(Node, false, false);
			Node->CreateNewGuid();
			Node->PostPlacedNewNode();
			Node->AllocateDefaultPins();
			Configure(Node, Props, Name);

			UBehaviorTreeGraphNode* ParentNode = Parent ? Parent : Root.Get();
			ParentNode->GetOutputPin()->MakeLinkTo(Node->GetInputPin());
			Children.FindOrAdd(ParentNode).Add(Node);
			return Node;
		}

		void Configure(UAIGraphNode* Node, const TArray<FProp>& Props, const TCHAR* Name)
		{
			UBTNode* Instance = Cast<UBTNode>(Node->NodeInstance);
			if (!Instance)
			{
				UE_LOG(LogNoFearBotAssets, Error, TEXT("%s: node instance was not created"), *Tree->GetName());
				bHasErrors = true;
				return;
			}

			if (Name)
			{
				Instance->NodeName = Name;
			}
			for (const FProp& Prop : Props)
			{
				const FProperty* Property = Instance->GetClass()->FindPropertyByName(Prop.Name);
				if (!Property || !Property->ImportText_InContainer(*Prop.Value, Instance, Instance, PPF_None))
				{
					UE_LOG(LogNoFearBotAssets, Error, TEXT("%s: cannot set %s.%s = %s"), *Tree->GetName(), *Instance->GetClass()->GetName(), Prop.Name, *Prop.Value);
					bHasErrors = true;
				}
			}
		}

		/** Листья — слева направо с шагом, родитель — над серединой своих детей. Порядок детей в дереве берётся из X. */
		int32 Layout(UBehaviorTreeGraphNode* Node, int32 Depth, int32& NextLeafX)
		{
			int32 X = NextLeafX;
			const TArray<UBehaviorTreeGraphNode*>* NodeChildren = Children.Find(Node);
			if (NodeChildren && NodeChildren->Num() > 0)
			{
				int32 FirstX = 0;
				int32 LastX = 0;
				for (int32 Index = 0; Index < NodeChildren->Num(); ++Index)
				{
					LastX = Layout((*NodeChildren)[Index], Depth + 1, NextLeafX);
					if (Index == 0)
					{
						FirstX = LastX;
					}
				}
				X = (FirstX + LastX) / 2;
			}
			else
			{
				NextLeafX += 320;
			}

			Node->NodePosX = X;
			Node->NodePosY = Depth * 280;
			return X;
		}

		UBehaviorTree* Tree = nullptr;
		UBehaviorTreeGraph* Graph = nullptr;
		TObjectPtr<UBehaviorTreeGraphNode_Root> Root;
		TMap<UBehaviorTreeGraphNode*, TArray<UBehaviorTreeGraphNode*>> Children;
		bool bHasErrors = false;
	};

	/**
	 * Стрелок (враги, союзники, напарник):
	 *   есть цель и укрытие → дойти до укрытия и драться из него (спрятаться, выглянуть, очередь, назад);
	 *   есть цель без укрытия → стрелять, если цель в дистанции оружия, и сменить точку; иначе подойти;
	 *   напарник идёт за лидером → держаться рядом;
	 *   есть что проверить → сходить, осмотреться, забыть;
	 *   иначе → вернуться на свою точку и стоять.
	 */
	void BuildSoldierTree(FTreeBuilder& Builder)
	{
		UBehaviorTreeGraphNode* Root = Builder.Selector(nullptr, TEXT("Стрелок"));

		UBehaviorTreeGraphNode* Combat = Builder.Selector(Root, TEXT("Бой"));
		Builder.IfSet(Combat, FNoFearBotKeys::AttackTarget, TEXT("Both"), TEXT("Есть цель?"));
		UBehaviorTreeGraphNode* InCover = Builder.Sequence(Combat, TEXT("Укрытие"));
		Builder.IfSet(InCover, FNoFearBotKeys::CoverLocation, TEXT("Both"), TEXT("Есть укрытие?"));
		Builder.MoveTo(InCover, FNoFearBotKeys::CoverLocation, 30.f, TEXT("В укрытие"));
		Builder.Task(InCover, UBTTask_NoFearCoverFight::StaticClass());
		UBehaviorTreeGraphNode* Open = Builder.Sequence(Combat, TEXT("В открытую"));
		// Только LowerPriority: CanFire мигает на ходу, а начатый шаг вбок надо дошагать.
		Builder.IfSet(Open, FNoFearBotKeys::CanFire, TEXT("LowerPriority"), TEXT("Можно стрелять?"));
		Builder.IfSet(Open, FNoFearBotKeys::InWeaponRange, TEXT("LowerPriority"), TEXT("В дистанции оружия?"));
		Builder.Task(Open, UBTTask_NoFearShoot::StaticClass(), { { TEXT("BurstsMin"), TEXT("1") }, { TEXT("BurstsMax"), TEXT("2") } });
		UBehaviorTreeGraphNode* OpenFind = Builder.Task(Open, UBTTask_NoFearFindCombatPosition::StaticClass());
		Builder.Decorator(OpenFind, UBTDecorator_ForceSuccess::StaticClass());
		UBehaviorTreeGraphNode* OpenMove = Builder.MoveTo(Open, FNoFearBotKeys::MoveGoal, 50.f, TEXT("На новую точку"));
		Builder.Decorator(OpenMove, UBTDecorator_ForceSuccess::StaticClass());
		UBehaviorTreeGraphNode* Reposition = Builder.Sequence(Combat, TEXT("Сменить позицию"));
		Builder.Task(Reposition, UBTTask_NoFearFindCombatPosition::StaticClass());
		Builder.MoveTo(Reposition, FNoFearBotKeys::MoveGoal, 50.f, TEXT("К точке боя"));
		Builder.Wait(Reposition, 0.3f);
		Builder.Wait(Combat, 0.5f, 0.2f);

		UBehaviorTreeGraphNode* Follow = Builder.Sequence(Root, TEXT("За лидером"));
		Builder.IfSet(Follow, FNoFearBotKeys::IsFollowing, TEXT("Both"), TEXT("Идёт за лидером?"));
		Builder.MoveTo(Follow, FNoFearBotKeys::Leader, 250.f, TEXT("К лидеру"));
		Builder.Wait(Follow, 0.4f);

		UBehaviorTreeGraphNode* Investigate = Builder.Sequence(Root, TEXT("Проверить"));
		Builder.IfSet(Investigate, FNoFearBotKeys::InvestigateLocation, TEXT("Both"), TEXT("Есть что проверить?"));
		UBehaviorTreeGraphNode* GoCheck = Builder.MoveTo(Investigate, FNoFearBotKeys::InvestigateLocation, 100.f, TEXT("К месту"));
		Builder.Decorator(GoCheck, UBTDecorator_ForceSuccess::StaticClass());
		Builder.Wait(Investigate, 3.f, 1.f);
		Builder.Task(Investigate, UBTTask_NoFearClearBlackboardKey::StaticClass(), { { TEXT("BlackboardKey"), KeyValue(FNoFearBotKeys::InvestigateLocation) } });

		UBehaviorTreeGraphNode* Home = Builder.Sequence(Root, TEXT("На точке"));
		Builder.MoveTo(Home, FNoFearBotKeys::HomeLocation, 80.f, TEXT("Домой"));
		Builder.Wait(Home, 3.f, 1.f);
	}

	/**
	 * Монстр (обычный и агрессивный):
	 *   рычит → стоять;
	 *   есть цель и надо держать дистанцию (плевун) → отбежать от неё;
 *   есть цель и жетон → догнать и ударить (атаку выбирает UNoFearMeleeComponent);
	 *   есть цель без жетона → кружить вокруг неё на 400–600 см;
	 *   цель ушла за территорию → шагом домой;
	 *   есть что проверить → сбегать, осмотреться, забыть;
	 *   иначе → бродить вокруг дома (вожак) или вожака (остальные).
	 */
	void BuildMonsterTree(FTreeBuilder& Builder)
	{
		UBehaviorTreeGraphNode* Root = Builder.Selector(nullptr, TEXT("Монстр"));

		UBehaviorTreeGraphNode* RoarWait = Builder.Wait(Root, 1.2f);
		Builder.IfSet(RoarWait, FNoFearBotKeys::IsRoaring, TEXT("Both"), TEXT("Рычит?"));

		UBehaviorTreeGraphNode* Attack = Builder.Selector(Root, TEXT("Атака"));
		Builder.IfSet(Attack, FNoFearBotKeys::AttackTarget, TEXT("Both"), TEXT("Есть цель?"));
		UBehaviorTreeGraphNode* Retreat = Builder.Sequence(Attack, TEXT("Отступить"));
		Builder.IfSet(Retreat, FNoFearBotKeys::KeepAway, TEXT("Both"), TEXT("Держать дистанцию?"));
		Builder.Task(Retreat, UBTTask_NoFearFindRetreatLocation::StaticClass());
		Builder.MoveTo(Retreat, FNoFearBotKeys::MoveGoal, 80.f, TEXT("Прочь от цели"));
		UBehaviorTreeGraphNode* Hit = Builder.Sequence(Attack, TEXT("Ударить"));
		Builder.IfSet(Hit, FNoFearBotKeys::HasAttackToken, TEXT("Both"), TEXT("Есть жетон?"));
		Builder.Task(Hit, UBTTask_NoFearMeleeAttack::StaticClass());
		Builder.Wait(Hit, 0.2f);
		UBehaviorTreeGraphNode* Circle = Builder.Sequence(Attack, TEXT("Кружить"));
		Builder.Task(Circle, UBTTask_NoFearFindCircleLocation::StaticClass());
		Builder.MoveTo(Circle, FNoFearBotKeys::MoveGoal, 60.f, TEXT("По кругу"));
		Builder.Wait(Circle, 0.3f, 0.2f);
		Builder.Wait(Attack, 0.3f);

		UBehaviorTreeGraphNode* GoHome = Builder.Sequence(Root, TEXT("Домой"));
		Builder.IfSet(GoHome, FNoFearBotKeys::ReturnHome, TEXT("Both"), TEXT("Возвращается?"));
		UBehaviorTreeGraphNode* WalkHome = Builder.MoveTo(GoHome, FNoFearBotKeys::HomeLocation, 150.f, TEXT("К дому"));
		Builder.Decorator(WalkHome, UBTDecorator_ForceSuccess::StaticClass());
		Builder.Task(GoHome, UBTTask_NoFearClearBlackboardKey::StaticClass(), { { TEXT("BlackboardKey"), KeyValue(FNoFearBotKeys::ReturnHome) } });

		UBehaviorTreeGraphNode* Investigate = Builder.Sequence(Root, TEXT("Проверить"));
		Builder.IfSet(Investigate, FNoFearBotKeys::InvestigateLocation, TEXT("Both"), TEXT("Есть что проверить?"));
		UBehaviorTreeGraphNode* GoCheck = Builder.MoveTo(Investigate, FNoFearBotKeys::InvestigateLocation, 100.f, TEXT("К месту"));
		Builder.Decorator(GoCheck, UBTDecorator_ForceSuccess::StaticClass());
		Builder.Wait(Investigate, 2.f, 1.f);
		Builder.Task(Investigate, UBTTask_NoFearClearBlackboardKey::StaticClass(), { { TEXT("BlackboardKey"), KeyValue(FNoFearBotKeys::InvestigateLocation) } });

		UBehaviorTreeGraphNode* Wander = Builder.Sequence(Root, TEXT("Бродить"));
		Builder.Task(Wander, UBTTask_NoFearFindWanderLocation::StaticClass(), {
			{ TEXT("OriginKey"), KeyValue(FNoFearBotKeys::WanderCenter) },
			{ TEXT("Radius"), PlainFloat(700.f) },
		});
		Builder.MoveTo(Wander, FNoFearBotKeys::MoveGoal, 50.f, TEXT("К точке"));
		Builder.Wait(Wander, 3.f, 1.5f);
	}

	/**
	 * Мирный (состояние решает ANoFearCivilianAIController, дерево только исполняет):
	 *   прячется → сидеть;
	 *   паника → бежать к FleeLocation и спрятаться там (нет точки — спрятаться сразу);
	 *   насторожен → посмотреть на звук, отойти шагом, осмотреться;
	 *   иначе → бродить вокруг дома с паузами и осматриваться.
	 */
	void BuildCivilianTree(FTreeBuilder& Builder)
	{
		UBehaviorTreeGraphNode* Root = Builder.Selector(nullptr, TEXT("Мирный"));

		UBehaviorTreeGraphNode* Cower = Builder.Wait(Root, 1.f);
		Builder.IfSet(Cower, FNoFearBotKeys::IsCowering, TEXT("Both"), TEXT("Прячется?"));

		UBehaviorTreeGraphNode* Panic = Builder.Selector(Root, TEXT("Паника"));
		Builder.IfSet(Panic, FNoFearBotKeys::IsPanicking, TEXT("Both"), TEXT("В панике?"));
		UBehaviorTreeGraphNode* Run = Builder.Sequence(Panic, TEXT("Бежать"));
		Builder.MoveTo(Run, FNoFearBotKeys::FleeLocation, 100.f, TEXT("К безопасной точке"));
		Builder.Task(Run, UBTTask_NoFearCivilianShelter::StaticClass());
		Builder.Task(Panic, UBTTask_NoFearCivilianShelter::StaticClass());

		UBehaviorTreeGraphNode* Wary = Builder.Selector(Root, TEXT("Насторожен"));
		Builder.IfSet(Wary, FNoFearBotKeys::IsWary, TEXT("Both"), TEXT("Насторожен?"));
		UBehaviorTreeGraphNode* StepAway = Builder.Sequence(Wary, TEXT("Отойти"));
		Builder.Wait(StepAway, 1.5f, 0.3f);
		Builder.MoveTo(StepAway, FNoFearBotKeys::FleeLocation, 80.f, TEXT("Подальше от звука"));
		Builder.Task(StepAway, UBTTask_NoFearLookAround::StaticClass(), { { TEXT("Duration"), PlainFloat(3.f) }, { TEXT("RandomDeviation"), PlainFloat(1.f) } });
		Builder.Task(Wary, UBTTask_NoFearLookAround::StaticClass(), { { TEXT("Duration"), PlainFloat(2.f) }, { TEXT("RandomDeviation"), PlainFloat(0.5f) } });

		UBehaviorTreeGraphNode* Wander = Builder.Sequence(Root, TEXT("Бродить"));
		Builder.Task(Wander, UBTTask_NoFearFindWanderLocation::StaticClass());
		Builder.MoveTo(Wander, FNoFearBotKeys::MoveGoal, 50.f, TEXT("К точке"));
		Builder.Task(Wander, UBTTask_NoFearLookAround::StaticClass(), { { TEXT("Duration"), PlainFloat(4.f) }, { TEXT("RandomDeviation"), PlainFloat(2.f) } });
	}

	/** Собранное дерево в лог: так видно, что вышло на самом деле (порядок детей, ключи, условия). */
	void LogTree(const UBTCompositeNode* Node, int32 Depth)
	{
		const FString Indent = FString::ChrN(Depth * 2, TEXT(' '));
		UE_LOG(LogNoFearBotAssets, Log, TEXT("%s%s"), *Indent, *Node->GetNodeName());
		for (const FBTCompositeChild& Child : Node->Children)
		{
			for (const UBTDecorator* Decorator : Child.Decorators)
			{
				UE_LOG(LogNoFearBotAssets, Log, TEXT("%s  [%s | %s]"), *Indent, *Decorator->GetStaticDescription().Replace(TEXT("\n"), TEXT("; ")), *UEnum::GetValueAsString(Decorator->GetFlowAbortMode()));
			}
			if (Child.ChildComposite)
			{
				LogTree(Child.ChildComposite, Depth + 1);
			}
			else if (Child.ChildTask)
			{
				UE_LOG(LogNoFearBotAssets, Log, TEXT("%s  %s: %s"), *Indent, *Child.ChildTask->GetNodeName(), *Child.ChildTask->GetStaticDescription().Replace(TEXT("\n"), TEXT("; ")));
			}
		}
	}

	void GenerateTree(const TCHAR* Name, UBlackboardData* Blackboard, bool bOverwrite, void (*Build)(FTreeBuilder&), bool& bOk)
	{
		const FString PackageName = FString(AIFolder) / Name;
		if (FPackageName::DoesPackageExist(PackageName) && !bOverwrite)
		{
			UE_LOG(LogNoFearBotAssets, Display, TEXT("%s exists, skipped"), Name);
			return;
		}

		bool bCreated = false;
		UBehaviorTree* Tree = FindOrCreateAsset<UBehaviorTree>(PackageName, bCreated);
		if (!Tree)
		{
			UE_LOG(LogNoFearBotAssets, Error, TEXT("Cannot load or create %s"), Name);
			bOk = false;
			return;
		}

		if (!bCreated)
		{
			// Пересборка: старый граф, узлы и их экземпляры лежат внутри ассета — выносим всё из пакета,
			// дальше их соберёт GC (часть из них может быть в корне, поэтому не MarkAsGarbage).
			Tree->Modify();
			TArray<UObject*> OldObjects;
			GetObjectsWithOuter(Tree, OldObjects, EGetObjectsFlags::None);
			for (UObject* Object : OldObjects)
			{
				Object->Rename(nullptr, GetTransientPackage(), REN_DontCreateRedirectors | REN_NonTransactional | REN_DoNotDirty);
			}
			Tree->BTGraph = nullptr;
			Tree->RootNode = nullptr;
			Tree->RootDecorators.Reset();
		}

		FTreeBuilder Builder(Tree, Blackboard);
		Build(Builder);
		Builder.Finish();

		if (Builder.HasErrors() || !Tree->RootNode)
		{
			UE_LOG(LogNoFearBotAssets, Error, TEXT("%s was built with errors"), Name);
			bOk = false;
		}
		else
		{
			UE_LOG(LogNoFearBotAssets, Log, TEXT("%s:"), Name);
			LogTree(Tree->RootNode, 1);
		}
		bOk &= SaveAsset(Tree);
	}

	// --- Тела ---

	struct FBodySpec
	{
		const TCHAR* Name;
		/** Имя родителя из этого же списка; nullptr — CBP_SandboxCharacter. */
		const TCHAR* Parent;
		UClass* ControllerClass;
		ENoFearTeam Team;
		FLinearColor Color;
		FText DisplayName;
		/** -1 — как у родителя, 0 — без оружия, 1 — винтовка. */
		int32 Weapons;
		/** Компонент, который появляется у этого тела (и наследуется его детьми). */
		UClass* ComponentClass = nullptr;
		const TCHAR* ComponentName = nullptr;
		/** Запас здоровья; 0 — как у родителя. */
		float MaxHealth = 0.f;
		/** Масштаб тела (капсулы со всем, что на ней); 0 — как у родителя. */
		float Scale = 0.f;
	};

	bool HasOwnComponent(const UBlueprint* Blueprint, const UClass* ComponentClass)
	{
		if (!Blueprint->SimpleConstructionScript)
		{
			return false;
		}
		for (const USCS_Node* Node : Blueprint->SimpleConstructionScript->GetAllNodes())
		{
			if (Node && Node->ComponentClass && Node->ComponentClass->IsChildOf(ComponentClass))
			{
				return true;
			}
		}
		return false;
	}

	UBlueprint* GenerateBody(const FBodySpec& Spec, UClass* ParentClass, UNoFearWeaponData* Rifle, bool bUpdate, bool& bOk)
	{
		const FString PackageName = FString(BotsFolder) / Spec.Name;
		const bool bExists = FPackageName::DoesPackageExist(PackageName);
		UBlueprint* Blueprint = nullptr;

		if (bExists)
		{
			Blueprint = LoadObject<UBlueprint>(nullptr, *MakeObjectPath(PackageName));
			if (!Blueprint)
			{
				UE_LOG(LogNoFearBotAssets, Error, TEXT("Cannot load %s"), *PackageName);
				bOk = false;
				return nullptr;
			}
			if (!bUpdate)
			{
				UE_LOG(LogNoFearBotAssets, Display, TEXT("%s exists, skipped"), Spec.Name);
				return Blueprint;
			}
			if (Blueprint->ParentClass != ParentClass)
			{
				Blueprint->Modify();
				Blueprint->ParentClass = ParentClass;
				FBlueprintEditorUtils::RefreshAllNodes(Blueprint);
				FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
			}
		}
		else
		{
			UPackage* Package = CreatePackage(*PackageName);
			Blueprint = FKismetEditorUtilities::CreateBlueprint(ParentClass, Package, Spec.Name, BPTYPE_Normal, TEXT("NoFearBotAssets"));
			if (!Blueprint)
			{
				UE_LOG(LogNoFearBotAssets, Error, TEXT("Cannot create %s"), *PackageName);
				bOk = false;
				return nullptr;
			}
			FAssetRegistryModule::AssetCreated(Blueprint);
		}

		if (Spec.ComponentClass && !HasOwnComponent(Blueprint, Spec.ComponentClass) && Blueprint->SimpleConstructionScript)
		{
			USCS_Node* Node = Blueprint->SimpleConstructionScript->CreateNode(Spec.ComponentClass, Spec.ComponentName);
			Blueprint->SimpleConstructionScript->AddNode(Node);
		}
		FKismetEditorUtilities::CompileBlueprint(Blueprint);

		ANoFearCharacter* Defaults = Blueprint->GeneratedClass ? Cast<ANoFearCharacter>(Blueprint->GeneratedClass->GetDefaultObject()) : nullptr;
		if (!Defaults)
		{
			UE_LOG(LogNoFearBotAssets, Error, TEXT("%s is not an ANoFearCharacter (parent %s)"), Spec.Name, *GetNameSafe(ParentClass));
			bOk = false;
			return Blueprint;
		}

		Defaults->Modify();
		Defaults->AIControllerClass = Spec.ControllerClass;
		Defaults->AutoPossessAI = EAutoPossessAI::PlacedInWorldOrSpawned;
		Defaults->AutoPossessPlayer = EAutoReceiveInput::Disabled;
		Defaults->Team = Spec.Team;
		Defaults->bApplyBodyColor = true;
		Defaults->BodyColor = Spec.Color;
		Defaults->CharacterName = Spec.DisplayName;
		if (UNoFearHealthComponent* Health = Spec.MaxHealth > 0.f ? Defaults->GetHealthComponent() : nullptr)
		{
			Health->Modify();
			Health->MaxHealth = Spec.MaxHealth;
		}
		if (UCapsuleComponent* Capsule = Spec.Scale > 0.f ? Defaults->GetCapsuleComponent() : nullptr)
		{
			Capsule->Modify();
			Capsule->SetRelativeScale3D(FVector(Spec.Scale));
		}

		if (UNoFearWeaponComponent* WeaponComponent = Spec.Weapons >= 0 ? Defaults->GetWeaponComponent() : nullptr)
		{
			WeaponComponent->Modify();
			WeaponComponent->StartingWeapons.Reset();
			if (Spec.Weapons > 0)
			{
				if (Rifle)
				{
					WeaponComponent->StartingWeapons.Add(Rifle);
				}
				else
				{
					UE_LOG(LogNoFearBotAssets, Error, TEXT("%s: rifle data %s not found"), Spec.Name, RiflePath);
					bOk = false;
				}
			}
		}

		bOk &= SaveAsset(Blueprint);
		return Blueprint;
	}

	void GenerateBodies(bool bUpdate, bool& bOk)
	{
		UClass* SandboxCharacterClass = LoadClass<ACharacter>(nullptr, SandboxCharacterClassPath);
		if (!SandboxCharacterClass)
		{
			UE_LOG(LogNoFearBotAssets, Error, TEXT("Cannot load %s"), SandboxCharacterClassPath);
			bOk = false;
			return;
		}
		UNoFearWeaponData* Rifle = LoadObject<UNoFearWeaponData>(nullptr, RiflePath);

		const FBodySpec Specs[] = {
			{ TEXT("CBP_NoFearBot"), nullptr, ANoFearAIController::StaticClass(), ENoFearTeam::Neutral, FLinearColor(0.5f, 0.5f, 0.5f),
				NSLOCTEXT("NoFearBots", "Bot", "Бот"), 0 },
			{ TEXT("CBP_Bot_Civilian"), TEXT("CBP_NoFearBot"), ANoFearCivilianAIController::StaticClass(), ENoFearTeam::Civilian, FLinearColor(0.85f, 0.85f, 0.85f),
				NSLOCTEXT("NoFearBots", "Civilian", "Мирный житель"), -1 },
			{ TEXT("CBP_Bot_Monster"), TEXT("CBP_NoFearBot"), ANoFearMonsterAIController::StaticClass(), ENoFearTeam::Monster, FLinearColor(0.75f, 0.02f, 0.02f),
				NSLOCTEXT("NoFearBots", "Monster", "Монстр"), -1, UNoFearMeleeComponent::StaticClass(), TEXT("Melee") },
			{ TEXT("CBP_Bot_AggroMonster"), TEXT("CBP_Bot_Monster"), ANoFearMonsterAIController::StaticClass(), ENoFearTeam::AggroMonster, FLinearColor(0.35f, 0.04f, 0.7f),
				NSLOCTEXT("NoFearBots", "AggroMonster", "Бешеный монстр"), -1 },
			{ TEXT("CBP_Bot_Jumper"), TEXT("CBP_Bot_Monster"), ANoFearJumperAIController::StaticClass(), ENoFearTeam::Monster, FLinearColor(1.f, 0.6f, 0.02f),
				NSLOCTEXT("NoFearBots", "Jumper", "Прыгун"), -1, nullptr, nullptr, 60.f, 0.85f },
			{ TEXT("CBP_Bot_Spitter"), TEXT("CBP_Bot_Monster"), ANoFearSpitterAIController::StaticClass(), ENoFearTeam::Monster, FLinearColor(0.2f, 0.75f, 0.05f),
				NSLOCTEXT("NoFearBots", "Spitter", "Плевун"), -1, nullptr, nullptr, 80.f, 1.f },
			{ TEXT("CBP_Bot_Tank"), TEXT("CBP_Bot_Monster"), ANoFearTankAIController::StaticClass(), ENoFearTeam::Monster, FLinearColor(0.35f, 0.02f, 0.05f),
				NSLOCTEXT("NoFearBots", "Tank", "Танк"), -1, nullptr, nullptr, 400.f, 1.3f },
			{ TEXT("CBP_NoFearBot_Soldier"), TEXT("CBP_NoFearBot"), ANoFearShooterAIController::StaticClass(), ENoFearTeam::Enemy, FLinearColor(0.4f, 0.4f, 0.4f),
				NSLOCTEXT("NoFearBots", "Soldier", "Стрелок"), 1, UNoFearCombatComponent::StaticClass(), TEXT("Combat") },
			{ TEXT("CBP_Bot_Enemy"), TEXT("CBP_NoFearBot_Soldier"), ANoFearShooterAIController::StaticClass(), ENoFearTeam::Enemy, FLinearColor(1.f, 0.3f, 0.02f),
				NSLOCTEXT("NoFearBots", "Enemy", "Противник"), -1 },
			{ TEXT("CBP_Bot_Ally"), TEXT("CBP_NoFearBot_Soldier"), ANoFearShooterAIController::StaticClass(), ENoFearTeam::Ally, FLinearColor(0.05f, 0.2f, 0.9f),
				NSLOCTEXT("NoFearBots", "Ally", "Союзник"), -1 },
			{ TEXT("CBP_Bot_Companion"), TEXT("CBP_NoFearBot_Soldier"), ANoFearCompanionAIController::StaticClass(), ENoFearTeam::Ally, FLinearColor(0.1f, 0.75f, 0.15f),
				NSLOCTEXT("NoFearBots", "Companion", "Напарник"), -1, UNoFearRecruitableComponent::StaticClass(), TEXT("Recruitable") },
		};

		TMap<FString, UClass*> CreatedClasses;
		for (const FBodySpec& Spec : Specs)
		{
			UClass* ParentClass = Spec.Parent ? CreatedClasses.FindRef(Spec.Parent) : SandboxCharacterClass;
			if (!ParentClass)
			{
				UE_LOG(LogNoFearBotAssets, Error, TEXT("%s: parent %s is missing"), Spec.Name, Spec.Parent);
				bOk = false;
				continue;
			}

			if (UBlueprint* Blueprint = GenerateBody(Spec, ParentClass, Rifle, bUpdate, bOk))
			{
				CreatedClasses.Add(Spec.Name, Blueprint->GeneratedClass);
			}
		}
	}

	FAutoConsoleCommand GenerateBotAssetsCommand(
		TEXT("NoFear.GenerateBotAssets"),
		TEXT("Create missing bot assets: BB_NoFearBot, BT_NoFear_*, CBP_Bot_*. Args: [trees] — rebuild behavior trees, [bodies] — re-apply body settings."),
		FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
		{
			const bool bTrees = Args.ContainsByPredicate([](const FString& Arg) { return Arg.Equals(TEXT("trees"), ESearchCase::IgnoreCase); });
			const bool bBodies = Args.ContainsByPredicate([](const FString& Arg) { return Arg.Equals(TEXT("bodies"), ESearchCase::IgnoreCase); });
			UNoFearBotAssetsLibrary::GenerateBotAssets(bTrees, bBodies);
		}));
}

bool UNoFearBotAssetsLibrary::GenerateBotAssets(bool bOverwriteTrees, bool bUpdateBodies)
{
	bool bOk = true;

	UBlackboardData* Blackboard = GenerateBlackboard(bOk);
	if (Blackboard)
	{
		GenerateTree(TEXT("BT_NoFear_Soldier"), Blackboard, bOverwriteTrees, &BuildSoldierTree, bOk);
		GenerateTree(TEXT("BT_NoFear_Monster"), Blackboard, bOverwriteTrees, &BuildMonsterTree, bOk);
		GenerateTree(TEXT("BT_NoFear_Civilian"), Blackboard, bOverwriteTrees, &BuildCivilianTree, bOk);
	}

	GenerateBodies(bUpdateBodies, bOk);

	UE_LOG(LogNoFearBotAssets, Display, TEXT("Bot assets generation %s"), bOk ? TEXT("succeeded") : TEXT("FAILED"));
	return bOk;
}
