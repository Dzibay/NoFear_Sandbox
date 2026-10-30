#include "NoFearPoseAssetsLibrary.h"

#include "AnimGraphNode_NoFearPose.h"
#include "AnimGraphNode_Root.h"
#include "AssetToolsModule.h"
#include "Animation/AnimBlueprint.h"
#include "Animation/NoFearPoseAnimInstance.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphSchema.h"
#include "HAL/IConsoleManager.h"
#include "IAssetTools.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/PackageName.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"

DEFINE_LOG_CATEGORY_STATIC(LogNoFearPoseAssets, Log, All);

namespace
{
const TCHAR* PosePackagePath = TEXT("/Game/Blueprints");
const TCHAR* PoseAssetName = TEXT("ABP_NoFearPose");
const TCHAR* SourceObjectPath = TEXT("/Game/Characters/UEFN_Mannequin/Rigs/ABP_UEFN_Mannequin_PostProcess.ABP_UEFN_Mannequin_PostProcess");

bool SavePackageOf(UObject* Asset)
{
	UPackage* Package = Asset->GetOutermost();
	const FString Filename = FPackageName::LongPackageNameToFilename(Package->GetName(), FPackageName::GetAssetPackageExtension());
	FSavePackageArgs Args;
	Args.TopLevelFlags = RF_Public | RF_Standalone;
	const bool bSaved = UPackage::SavePackage(Package, Asset, *Filename, Args);
	UE_LOG(LogNoFearPoseAssets, Display, TEXT("%s %s"), bSaved ? TEXT("Saved") : TEXT("FAILED to save"), *Package->GetName());
	return bSaved;
}

UEdGraph* FindAnimGraph(const UAnimBlueprint* AnimBlueprint)
{
	for (UEdGraph* Graph : AnimBlueprint->FunctionGraphs)
	{
		if (Graph && Graph->GetFName() == TEXT("AnimGraph"))
		{
			return Graph;
		}
	}
	return nullptr;
}

UEdGraphPin* FindPinByDirection(const UEdGraphNode* Node, EEdGraphPinDirection Direction)
{
	for (UEdGraphPin* Pin : Node->Pins)
	{
		if (Pin && Pin->Direction == Direction && !Pin->bHidden)
		{
			return Pin;
		}
	}
	return nullptr;
}

/** Вставить узел NoFear Pose между последним узлом графа и Root. */
bool InsertPoseNode(UAnimBlueprint* AnimBlueprint)
{
	UEdGraph* Graph = FindAnimGraph(AnimBlueprint);
	if (!Graph)
	{
		UE_LOG(LogNoFearPoseAssets, Error, TEXT("В %s нет графа AnimGraph"), *AnimBlueprint->GetName());
		return false;
	}

	UAnimGraphNode_Root* Root = nullptr;
	for (UEdGraphNode* Node : Graph->Nodes)
	{
		if (Cast<UAnimGraphNode_NoFearPose>(Node))
		{
			return true;
		}
		if (UAnimGraphNode_Root* Candidate = Cast<UAnimGraphNode_Root>(Node))
		{
			Root = Candidate;
		}
	}
	if (!Root)
	{
		UE_LOG(LogNoFearPoseAssets, Error, TEXT("В AnimGraph нет Root"));
		return false;
	}

	UEdGraphPin* ResultPin = FindPinByDirection(Root, EGPD_Input);
	if (!ResultPin || ResultPin->LinkedTo.IsEmpty())
	{
		UE_LOG(LogNoFearPoseAssets, Error, TEXT("Root не подключён"));
		return false;
	}
	UEdGraphPin* PreviousOutput = ResultPin->LinkedTo[0];

	FGraphNodeCreator<UAnimGraphNode_NoFearPose> Creator(*Graph);
	UAnimGraphNode_NoFearPose* PoseNode = Creator.CreateNode();
	PoseNode->NodePosX = Root->NodePosX - 260;
	PoseNode->NodePosY = Root->NodePosY;
	Creator.Finalize();

	UEdGraphPin* PoseInput = PoseNode->FindPin(TEXT("Source"));
	UEdGraphPin* PoseOutput = PoseNode->FindPin(TEXT("Pose"));
	if (!PoseInput)
	{
		PoseInput = FindPinByDirection(PoseNode, EGPD_Input);
	}
	if (!PoseOutput)
	{
		PoseOutput = FindPinByDirection(PoseNode, EGPD_Output);
	}
	if (!PoseInput || !PoseOutput)
	{
		UE_LOG(LogNoFearPoseAssets, Error, TEXT("У узла NoFear Pose не нашлись пины позы"));
		return false;
	}

	ResultPin->BreakAllPinLinks();
	const UEdGraphSchema* Schema = Graph->GetSchema();
	const bool bLinked = Schema->TryCreateConnection(PreviousOutput, PoseInput) && Schema->TryCreateConnection(PoseOutput, ResultPin);
	if (!bLinked)
	{
		UE_LOG(LogNoFearPoseAssets, Error, TEXT("Не удалось подключить узел NoFear Pose"));
	}
	return bLinked;
}

FAutoConsoleCommand GeneratePoseAnimBPCommand(
	TEXT("NoFear.GeneratePoseAnimBP"),
	TEXT("Create or repair /Game/Blueprints/ABP_NoFearPose (post-process AnimBP with the NoFear Pose node)."),
	FConsoleCommandDelegate::CreateLambda([]() { UNoFearPoseAssetsLibrary::GeneratePoseAnimBlueprint(); }));
}

bool UNoFearPoseAssetsLibrary::GeneratePoseAnimBlueprint()
{
	const FString ObjectPath = FString::Printf(TEXT("%s/%s.%s"), PosePackagePath, PoseAssetName, PoseAssetName);
	UAnimBlueprint* AnimBlueprint = LoadObject<UAnimBlueprint>(nullptr, *ObjectPath, nullptr, LOAD_NoWarn | LOAD_Quiet);

	if (!AnimBlueprint)
	{
		UAnimBlueprint* Source = LoadObject<UAnimBlueprint>(nullptr, SourceObjectPath);
		if (!Source)
		{
			UE_LOG(LogNoFearPoseAssets, Error, TEXT("Нет исходного пост-процесса %s"), SourceObjectPath);
			return false;
		}

		IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
		AnimBlueprint = Cast<UAnimBlueprint>(AssetTools.DuplicateAsset(PoseAssetName, PosePackagePath, Source));
		if (!AnimBlueprint)
		{
			UE_LOG(LogNoFearPoseAssets, Error, TEXT("Не удалось скопировать %s"), SourceObjectPath);
			return false;
		}
		UE_LOG(LogNoFearPoseAssets, Display, TEXT("Создан %s (копия %s)"), *ObjectPath, SourceObjectPath);
	}

	if (!AnimBlueprint->ParentClass || !AnimBlueprint->ParentClass->IsChildOf(UNoFearPoseAnimInstance::StaticClass()))
	{
		AnimBlueprint->ParentClass = UNoFearPoseAnimInstance::StaticClass();
		FBlueprintEditorUtils::RefreshAllNodes(AnimBlueprint);
	}

	if (!InsertPoseNode(AnimBlueprint))
	{
		return false;
	}

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(AnimBlueprint);
	FKismetEditorUtilities::CompileBlueprint(AnimBlueprint);
	if (AnimBlueprint->Status == BS_Error)
	{
		UE_LOG(LogNoFearPoseAssets, Error, TEXT("%s не скомпилировался"), *AnimBlueprint->GetName());
		return false;
	}

	const bool bSaved = SavePackageOf(AnimBlueprint);
	UE_LOG(LogNoFearPoseAssets, Display, TEXT("Pose AnimBP generation %s"), bSaved ? TEXT("succeeded") : TEXT("FAILED"));
	return bSaved;
}
