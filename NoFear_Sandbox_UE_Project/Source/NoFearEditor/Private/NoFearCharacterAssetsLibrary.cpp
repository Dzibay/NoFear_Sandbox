#include "NoFearCharacterAssetsLibrary.h"

#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphNode_Comment.h"
#include "EdGraphSchema_K2.h"
#include "Engine/Blueprint.h"
#include "K2Node_InputKey.h"
#include "Kismet2/BlueprintEditorUtils.h"

DEFINE_LOG_CATEGORY_STATIC(LogNoFearCharacterAssets, Log, All);

namespace
{
	bool IsExecPin(const UEdGraphPin* Pin)
	{
		return Pin && Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec;
	}

	/** Исполняется только от убранных: есть связанный вход исполнения, и все связанные входы идут от них. */
	bool IsExecOnlyFrom(const UEdGraphNode* Node, const TSet<UEdGraphNode*>& Removed)
	{
		bool bAnyLinked = false;
		for (const UEdGraphPin* Pin : Node->Pins)
		{
			if (!IsExecPin(Pin) || Pin->Direction != EGPD_Input)
			{
				continue;
			}
			for (const UEdGraphPin* Linked : Pin->LinkedTo)
			{
				bAnyLinked = true;
				if (!Removed.Contains(Linked->GetOwningNode()))
				{
					return false;
				}
			}
		}
		return bAnyLinked;
	}

	bool HasExecPins(const UEdGraphNode* Node)
	{
		return Node->Pins.ContainsByPredicate([](const UEdGraphPin* Pin) { return IsExecPin(Pin); });
	}

	/** Чистый узел, все связанные выходы которого идут в убранные. */
	bool IsPureOnlyFor(const UEdGraphNode* Node, const TSet<UEdGraphNode*>& Removed)
	{
		bool bAnyLinked = false;
		for (const UEdGraphPin* Pin : Node->Pins)
		{
			if (Pin->Direction != EGPD_Output)
			{
				continue;
			}
			for (const UEdGraphPin* Linked : Pin->LinkedTo)
			{
				bAnyLinked = true;
				if (!Removed.Contains(Linked->GetOwningNode()))
				{
					return false;
				}
			}
		}
		return bAnyLinked;
	}

	FString Describe(const UEdGraphNode* Node)
	{
		return FString::Printf(TEXT("%s [%s] '%s'"), *Node->GetName(), *Node->GetClass()->GetName(),
			*Node->GetNodeTitle(ENodeTitleType::ListView).ToString().Replace(TEXT("\n"), TEXT(" ")));
	}
}

int32 UNoFearCharacterAssetsLibrary::RemoveInputKeyHandlers(UBlueprint* Blueprint, const TArray<FName>& KeyNames, const TArray<FString>& CommentsToRemove, bool bDryRun)
{
	if (!Blueprint)
	{
		return 0;
	}
	TArray<UEdGraph*> Graphs;
	Graphs.Append(Blueprint->UbergraphPages);
	Graphs.Append(Blueprint->FunctionGraphs);
	Graphs.Append(Blueprint->MacroGraphs);

	int32 Total = 0;
	for (UEdGraph* Graph : Graphs)
	{
		TSet<UEdGraphNode*> Removed;
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			const UK2Node_InputKey* InputKey = Cast<UK2Node_InputKey>(Node);
			if (InputKey && KeyNames.Contains(InputKey->InputKey.GetFName()))
			{
				Removed.Add(Node);
			}
		}
		if (Removed.Num() == 0)
		{
			continue;
		}

		// Расширять, пока находятся узлы, живущие только ради убранных.
		for (bool bGrew = true; bGrew;)
		{
			bGrew = false;
			for (UEdGraphNode* Node : Graph->Nodes)
			{
				if (!Node || Removed.Contains(Node) || Node->IsA<UEdGraphNode_Comment>())
				{
					continue;
				}
				const bool bOnlyForRemoved = HasExecPins(Node) ? IsExecOnlyFrom(Node, Removed) : IsPureOnlyFor(Node, Removed);
				if (bOnlyForRemoved)
				{
					Removed.Add(Node);
					bGrew = true;
				}
			}
		}

		// Связи, которые оборвутся у остающихся узлов: их надо проверить глазами.
		for (UEdGraphNode* Node : Removed)
		{
			UE_LOG(LogNoFearCharacterAssets, Display, TEXT("NFC remove %s / %s"), *Graph->GetName(), *Describe(Node));
			for (const UEdGraphPin* Pin : Node->Pins)
			{
				for (const UEdGraphPin* Linked : Pin->LinkedTo)
				{
					if (!Removed.Contains(Linked->GetOwningNode()))
					{
						UE_LOG(LogNoFearCharacterAssets, Warning, TEXT("NFC   cut %s.%s -> %s.%s"), *Node->GetName(), *Pin->PinName.ToString(),
							*Describe(Linked->GetOwningNode()), *Linked->PinName.ToString());
					}
				}
			}
		}
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			const UEdGraphNode_Comment* Comment = Cast<UEdGraphNode_Comment>(Node);
			if (Comment && CommentsToRemove.ContainsByPredicate([Comment](const FString& Text) { return Comment->NodeComment.Contains(Text); }))
			{
				UE_LOG(LogNoFearCharacterAssets, Display, TEXT("NFC remove %s / comment '%s'"), *Graph->GetName(), *Comment->NodeComment);
				Removed.Add(Node);
			}
		}

		Total += Removed.Num();
		if (!bDryRun)
		{
			for (UEdGraphNode* Node : Removed)
			{
				FBlueprintEditorUtils::RemoveNode(Blueprint, Node, /*bDontRecompile=*/true);
			}
		}
	}

	if (!bDryRun && Total > 0)
	{
		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
	}
	UE_LOG(LogNoFearCharacterAssets, Display, TEXT("NFC %s %d nodes"), bDryRun ? TEXT("would remove") : TEXT("removed"), Total);
	return Total;
}
