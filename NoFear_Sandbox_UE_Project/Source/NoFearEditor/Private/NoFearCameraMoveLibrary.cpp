#include "NoFearCameraMoveLibrary.h"

#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "Engine/Blueprint.h"
#include "K2Node_VariableGet.h"
#include "EdGraphSchema_K2.h"
#include "Kismet2/BlueprintEditorUtils.h"

DEFINE_LOG_CATEGORY_STATIC(LogNoFearCameraMove, Log, All);

namespace
{
void AllGraphs(UBlueprint* Blueprint, TArray<UEdGraph*>& Out)
{
	Out.Append(Blueprint->UbergraphPages);
	Out.Append(Blueprint->FunctionGraphs);
	Out.Append(Blueprint->MacroGraphs);
}

bool IsVarNode(const UEdGraphNode* Node, FName VariableName)
{
	const UK2Node_VariableGet* Get = Cast<UK2Node_VariableGet>(Node);
	return Get && Get->GetVarName() == VariableName;
}
}

void UNoFearCameraMoveLibrary::DumpComponentNodes(UBlueprint* Blueprint, FName VariableName)
{
	TArray<UEdGraph*> Graphs;
	AllGraphs(Blueprint, Graphs);
	for (UEdGraph* Graph : Graphs)
	{
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (!IsVarNode(Node, VariableName))
			{
				continue;
			}
			UE_LOG(LogNoFearCameraMove, Warning, TEXT("NFB var node in graph %s: %s"), *Graph->GetName(), *Node->GetName());
			for (UEdGraphPin* Pin : Node->Pins)
			{
				for (UEdGraphPin* Linked : Pin->LinkedTo)
				{
					UEdGraphNode* Other = Linked->GetOwningNode();
					UE_LOG(LogNoFearCameraMove, Warning, TEXT("NFB   -> %s (%s) pin %s"), *Other->GetNodeTitle(ENodeTitleType::ListView).ToString(), *Other->GetClass()->GetName(), *Linked->PinName.ToString());
				}
			}
		}
	}
}

int32 UNoFearCameraMoveLibrary::RemoveComponentUsage(UBlueprint* Blueprint, FName VariableName)
{
	int32 Removed = 0;
	TArray<UEdGraph*> Graphs;
	AllGraphs(Blueprint, Graphs);
	for (UEdGraph* Graph : Graphs)
	{
		TArray<UEdGraphNode*> VarNodes;
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (IsVarNode(Node, VariableName))
			{
				VarNodes.Add(Node);
			}
		}
		for (UEdGraphNode* VarNode : VarNodes)
		{
			TArray<UEdGraphNode*> Users;
			for (UEdGraphPin* Pin : VarNode->Pins)
			{
				for (UEdGraphPin* Linked : Pin->LinkedTo)
				{
					Users.AddUnique(Linked->GetOwningNode());
				}
			}
			for (UEdGraphNode* User : Users)
			{
				UEdGraphPin* ExecIn = User->FindPin(UEdGraphSchema_K2::PN_Execute, EGPD_Input);
				UEdGraphPin* ExecOut = User->FindPin(UEdGraphSchema_K2::PN_Then, EGPD_Output);
				if (ExecIn && ExecOut)
				{
					TArray<UEdGraphPin*> Prev = ExecIn->LinkedTo;
					TArray<UEdGraphPin*> Next = ExecOut->LinkedTo;
					for (UEdGraphPin* P : Prev)
					{
						for (UEdGraphPin* N : Next)
						{
							P->MakeLinkTo(N);
						}
					}
				}
				User->BreakAllNodeLinks();
				Graph->RemoveNode(User);
				++Removed;
			}
			VarNode->BreakAllNodeLinks();
			Graph->RemoveNode(VarNode);
		}
	}
	if (Removed > 0)
	{
		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
	}
	return Removed;
}
