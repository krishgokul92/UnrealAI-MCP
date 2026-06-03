#include "Commands/UnrealAIBlueprintGraphCommands.h"
#include "Commands/UnrealAICommonUtils.h"
#include "Commands/BlueprintGraph/NodeManager.h"
#include "Commands/BlueprintGraph/BPConnector.h"
#include "Commands/BlueprintGraph/BPVariables.h"
#include "Commands/BlueprintGraph/EventManager.h"
#include "Commands/BlueprintGraph/NodeDeleter.h"
#include "Commands/BlueprintGraph/NodePropertyManager.h"
#include "Commands/BlueprintGraph/Function/FunctionManager.h"
#include "Commands/BlueprintGraph/Function/FunctionIO.h"

// Path A: T3D paste support
#include "EdGraphUtilities.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "Engine/Blueprint.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"

FUnrealAIBlueprintGraphCommands::FUnrealAIBlueprintGraphCommands()
{
}

FUnrealAIBlueprintGraphCommands::~FUnrealAIBlueprintGraphCommands()
{
}

TSharedPtr<FJsonObject> FUnrealAIBlueprintGraphCommands::HandleCommand(const FString& CommandType, const TSharedPtr<FJsonObject>& Params)
{
    if (CommandType == TEXT("add_blueprint_node"))
    {
        return HandleAddBlueprintNode(Params);
    }
    else if (CommandType == TEXT("connect_nodes"))
    {
        return HandleConnectNodes(Params);
    }
    else if (CommandType == TEXT("create_variable"))
    {
        return HandleCreateVariable(Params);
    }
    else if (CommandType == TEXT("set_blueprint_variable_properties"))
    {
        return HandleSetVariableProperties(Params);
    }
    else if (CommandType == TEXT("add_event_node"))
    {
        return HandleAddEventNode(Params);
    }
    else if (CommandType == TEXT("delete_node"))
    {
        return HandleDeleteNode(Params);
    }
    else if (CommandType == TEXT("set_node_property"))
    {
        return HandleSetNodeProperty(Params);
    }
    else if (CommandType == TEXT("create_function"))
    {
        return HandleCreateFunction(Params);
    }
    else if (CommandType == TEXT("add_function_input"))
    {
        return HandleAddFunctionInput(Params);
    }
    else if (CommandType == TEXT("add_function_output"))
    {
        return HandleAddFunctionOutput(Params);
    }
    else if (CommandType == TEXT("delete_function"))
    {
        return HandleDeleteFunction(Params);
    }
    else if (CommandType == TEXT("rename_function"))
    {
        return HandleRenameFunction(Params);
    }
    else if (CommandType == TEXT("paste_nodes_to_blueprint"))
    {
        return HandlePasteNodesToBlueprint(Params);
    }

    return FUnrealAICommonUtils::CreateErrorResponse(FString::Printf(TEXT("Unknown blueprint graph command: %s"), *CommandType));
}

TSharedPtr<FJsonObject> FUnrealAIBlueprintGraphCommands::HandleAddBlueprintNode(const TSharedPtr<FJsonObject>& Params)
{
    // Get required parameters
    FString BlueprintName;
    if (!Params->TryGetStringField(TEXT("blueprint_name"), BlueprintName))
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing 'blueprint_name' parameter"));
    }

    FString NodeType;
    if (!Params->TryGetStringField(TEXT("node_type"), NodeType))
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing 'node_type' parameter"));
    }

    UE_LOG(LogTemp, Display, TEXT("FUnrealAIBlueprintGraphCommands::HandleAddBlueprintNode: Adding %s node to blueprint '%s'"), *NodeType, *BlueprintName);

    // Use the NodeManager to add the node
    return FBlueprintNodeManager::AddNode(Params);
}

TSharedPtr<FJsonObject> FUnrealAIBlueprintGraphCommands::HandleConnectNodes(const TSharedPtr<FJsonObject>& Params)
{
    // Get required parameters
    FString BlueprintName;
    if (!Params->TryGetStringField(TEXT("blueprint_name"), BlueprintName))
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing 'blueprint_name' parameter"));
    }

    FString SourceNodeId;
    if (!Params->TryGetStringField(TEXT("source_node_id"), SourceNodeId))
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing 'source_node_id' parameter"));
    }

    FString SourcePinName;
    if (!Params->TryGetStringField(TEXT("source_pin_name"), SourcePinName))
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing 'source_pin_name' parameter"));
    }

    FString TargetNodeId;
    if (!Params->TryGetStringField(TEXT("target_node_id"), TargetNodeId))
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing 'target_node_id' parameter"));
    }

    FString TargetPinName;
    if (!Params->TryGetStringField(TEXT("target_pin_name"), TargetPinName))
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing 'target_pin_name' parameter"));
    }

    UE_LOG(LogTemp, Display, TEXT("FUnrealAIBlueprintGraphCommands::HandleConnectNodes: Connecting %s.%s to %s.%s in blueprint '%s'"),
        *SourceNodeId, *SourcePinName, *TargetNodeId, *TargetPinName, *BlueprintName);

    // Use the BPConnector to connect the nodes
    return FBPConnector::ConnectNodes(Params);
}

TSharedPtr<FJsonObject> FUnrealAIBlueprintGraphCommands::HandleCreateVariable(const TSharedPtr<FJsonObject>& Params)
{
    // Get required parameters
    FString BlueprintName;
    if (!Params->TryGetStringField(TEXT("blueprint_name"), BlueprintName))
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing 'blueprint_name' parameter"));
    }

    FString VariableName;
    if (!Params->TryGetStringField(TEXT("variable_name"), VariableName))
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing 'variable_name' parameter"));
    }

    FString VariableType;
    if (!Params->TryGetStringField(TEXT("variable_type"), VariableType))
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing 'variable_type' parameter"));
    }

    UE_LOG(LogTemp, Display, TEXT("FUnrealAIBlueprintGraphCommands::HandleCreateVariable: Creating %s variable '%s' in blueprint '%s'"),
        *VariableType, *VariableName, *BlueprintName);

    // Use the BPVariables to create the variable
    return FBPVariables::CreateVariable(Params);
}

TSharedPtr<FJsonObject> FUnrealAIBlueprintGraphCommands::HandleSetVariableProperties(const TSharedPtr<FJsonObject>& Params)
{
    // Get required parameters
    FString BlueprintName;
    if (!Params->TryGetStringField(TEXT("blueprint_name"), BlueprintName))
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing 'blueprint_name' parameter"));
    }

    FString VariableName;
    if (!Params->TryGetStringField(TEXT("variable_name"), VariableName))
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing 'variable_name' parameter"));
    }

    UE_LOG(LogTemp, Display, TEXT("FUnrealAIBlueprintGraphCommands::HandleSetVariableProperties: Modifying variable '%s' in blueprint '%s'"),
        *VariableName, *BlueprintName);

    // Use the BPVariables to set the variable properties
    return FBPVariables::SetVariableProperties(Params);
}

TSharedPtr<FJsonObject> FUnrealAIBlueprintGraphCommands::HandleAddEventNode(const TSharedPtr<FJsonObject>& Params)
{
    // Get required parameters
    FString BlueprintName;
    if (!Params->TryGetStringField(TEXT("blueprint_name"), BlueprintName))
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing 'blueprint_name' parameter"));
    }

    FString EventName;
    if (!Params->TryGetStringField(TEXT("event_name"), EventName))
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing 'event_name' parameter"));
    }

    UE_LOG(LogTemp, Display, TEXT("FUnrealAIBlueprintGraphCommands::HandleAddEventNode: Adding event '%s' to blueprint '%s'"),
        *EventName, *BlueprintName);

    // Use the EventManager to add the event node
    return FEventManager::AddEventNode(Params);
}

TSharedPtr<FJsonObject> FUnrealAIBlueprintGraphCommands::HandleDeleteNode(const TSharedPtr<FJsonObject>& Params)
{
    FString BlueprintName;
    if (!Params->TryGetStringField(TEXT("blueprint_name"), BlueprintName))
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing 'blueprint_name' parameter"));
    }

    FString NodeID;
    if (!Params->TryGetStringField(TEXT("node_id"), NodeID))
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing 'node_id' parameter"));
    }

    UE_LOG(LogTemp, Display,
        TEXT("FUnrealAIBlueprintGraphCommands::HandleDeleteNode: Deleting node '%s' from blueprint '%s'"),
        *NodeID, *BlueprintName);

    return FNodeDeleter::DeleteNode(Params);
}

TSharedPtr<FJsonObject> FUnrealAIBlueprintGraphCommands::HandleSetNodeProperty(const TSharedPtr<FJsonObject>& Params)
{
    FString BlueprintName;
    if (!Params->TryGetStringField(TEXT("blueprint_name"), BlueprintName))
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing 'blueprint_name' parameter"));
    }

    FString NodeID;
    if (!Params->TryGetStringField(TEXT("node_id"), NodeID))
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing 'node_id' parameter"));
    }

    // Check if this is semantic mode (action parameter) or legacy mode (property_name)
    bool bHasAction = Params->HasField(TEXT("action"));

    if (bHasAction)
    {
        // Semantic mode - delegate directly to SetNodeProperty
        FString Action;
        Params->TryGetStringField(TEXT("action"), Action);
        UE_LOG(LogTemp, Display,
            TEXT("FUnrealAIBlueprintGraphCommands::HandleSetNodeProperty: Semantic mode - action '%s' on node '%s' in blueprint '%s'"),
            *Action, *NodeID, *BlueprintName);
    }
    else
    {
        // Legacy mode - require property_name
        FString PropertyName;
        if (!Params->TryGetStringField(TEXT("property_name"), PropertyName))
        {
            return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing 'property_name' parameter"));
        }

        UE_LOG(LogTemp, Display,
            TEXT("FUnrealAIBlueprintGraphCommands::HandleSetNodeProperty: Legacy mode - Setting '%s' on node '%s' in blueprint '%s'"),
            *PropertyName, *NodeID, *BlueprintName);
    }

    return FNodePropertyManager::SetNodeProperty(Params);
}


TSharedPtr<FJsonObject> FUnrealAIBlueprintGraphCommands::HandleCreateFunction(const TSharedPtr<FJsonObject>& Params)
{
    FString BlueprintName;
    if (!Params->TryGetStringField(TEXT("blueprint_name"), BlueprintName))
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing 'blueprint_name' parameter"));
    }

    FString FunctionName;
    if (!Params->TryGetStringField(TEXT("function_name"), FunctionName))
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing 'function_name' parameter"));
    }

    UE_LOG(LogTemp, Display, TEXT("FUnrealAIBlueprintGraphCommands::HandleCreateFunction: Creating function '%s' in blueprint '%s'"),
        *FunctionName, *BlueprintName);

    return FFunctionManager::CreateFunction(Params);
}

TSharedPtr<FJsonObject> FUnrealAIBlueprintGraphCommands::HandleAddFunctionInput(const TSharedPtr<FJsonObject>& Params)
{
    FString BlueprintName;
    if (!Params->TryGetStringField(TEXT("blueprint_name"), BlueprintName))
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing 'blueprint_name' parameter"));
    }

    FString FunctionName;
    if (!Params->TryGetStringField(TEXT("function_name"), FunctionName))
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing 'function_name' parameter"));
    }

    FString ParamName;
    if (!Params->TryGetStringField(TEXT("param_name"), ParamName))
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing 'param_name' parameter"));
    }

    UE_LOG(LogTemp, Display, TEXT("FUnrealAIBlueprintGraphCommands::HandleAddFunctionInput: Adding input '%s' to function '%s' in blueprint '%s'"),
        *ParamName, *FunctionName, *BlueprintName);

    return FFunctionIO::AddFunctionInput(Params);
}

TSharedPtr<FJsonObject> FUnrealAIBlueprintGraphCommands::HandleAddFunctionOutput(const TSharedPtr<FJsonObject>& Params)
{
    FString BlueprintName;
    if (!Params->TryGetStringField(TEXT("blueprint_name"), BlueprintName))
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing 'blueprint_name' parameter"));
    }

    FString FunctionName;
    if (!Params->TryGetStringField(TEXT("function_name"), FunctionName))
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing 'function_name' parameter"));
    }

    FString ParamName;
    if (!Params->TryGetStringField(TEXT("param_name"), ParamName))
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing 'param_name' parameter"));
    }

    UE_LOG(LogTemp, Display, TEXT("FUnrealAIBlueprintGraphCommands::HandleAddFunctionOutput: Adding output '%s' to function '%s' in blueprint '%s'"),
        *ParamName, *FunctionName, *BlueprintName);

    return FFunctionIO::AddFunctionOutput(Params);
}

TSharedPtr<FJsonObject> FUnrealAIBlueprintGraphCommands::HandleDeleteFunction(const TSharedPtr<FJsonObject>& Params)
{
    FString BlueprintName;
    if (!Params->TryGetStringField(TEXT("blueprint_name"), BlueprintName))
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing 'blueprint_name' parameter"));
    }

    FString FunctionName;
    if (!Params->TryGetStringField(TEXT("function_name"), FunctionName))
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing 'function_name' parameter"));
    }

    UE_LOG(LogTemp, Display, TEXT("FUnrealAIBlueprintGraphCommands::HandleDeleteFunction: Deleting function '%s' from blueprint '%s'"),
        *FunctionName, *BlueprintName);

    return FFunctionManager::DeleteFunction(Params);
}

TSharedPtr<FJsonObject> FUnrealAIBlueprintGraphCommands::HandleRenameFunction(const TSharedPtr<FJsonObject>& Params)
{
    FString BlueprintName;
    if (!Params->TryGetStringField(TEXT("blueprint_name"), BlueprintName))
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing 'blueprint_name' parameter"));
    }

    FString OldFunctionName;
    if (!Params->TryGetStringField(TEXT("old_function_name"), OldFunctionName))
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing 'old_function_name' parameter"));
    }

    FString NewFunctionName;
    if (!Params->TryGetStringField(TEXT("new_function_name"), NewFunctionName))
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing 'new_function_name' parameter"));
    }

    UE_LOG(LogTemp, Display, TEXT("FUnrealAIBlueprintGraphCommands::HandleRenameFunction: Renaming function '%s' to '%s' in blueprint '%s'"),
        *OldFunctionName, *NewFunctionName, *BlueprintName);

    return FFunctionManager::RenameFunction(Params);
}

// ---------------------------------------------------------------------------
// Path A: Paste T3D-format node text into a Blueprint graph.
//
// Mirrors what the editor does when the user copies/pastes nodes (Ctrl+V):
// it calls FEdGraphUtilities::ImportNodesFromText on the target graph.
//
// Params:
//   blueprint_name : Blueprint asset name or path (e.g. "BP_HealthActor"
//                    or "/Game/Blueprints/BP_HealthActor")
//   graph_name     : Target graph. Defaults to "EventGraph". Pass a function
//                    name to paste into a function graph (e.g. "OnHit").
//   t3d_text       : The Begin Object / End Object T3D text the LLM produced.
//   auto_compile   : Optional bool, default true. Compile after pasting.
//   clear_graph    : Optional bool, default false. If true, deletes existing
//                    nodes in the target graph before pasting (useful for
//                    full-graph replacement).
//
// Returns:
//   { status, blueprint, graph, pasted_count, node_names[], compiled }
// ---------------------------------------------------------------------------
TSharedPtr<FJsonObject> FUnrealAIBlueprintGraphCommands::HandlePasteNodesToBlueprint(const TSharedPtr<FJsonObject>& Params)
{
    FString BlueprintName;
    if (!Params->TryGetStringField(TEXT("blueprint_name"), BlueprintName))
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing 'blueprint_name' parameter"));
    }

    FString T3DText;
    if (!Params->TryGetStringField(TEXT("t3d_text"), T3DText))
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing 't3d_text' parameter"));
    }

    if (T3DText.TrimStartAndEnd().IsEmpty())
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("'t3d_text' is empty"));
    }

    FString GraphName = TEXT("EventGraph");
    Params->TryGetStringField(TEXT("graph_name"), GraphName);

    bool bAutoCompile = true;
    Params->TryGetBoolField(TEXT("auto_compile"), bAutoCompile);

    bool bClearGraph = false;
    Params->TryGetBoolField(TEXT("clear_graph"), bClearGraph);

    // Locate the Blueprint asset.
    UBlueprint* Blueprint = FUnrealAICommonUtils::FindBlueprint(BlueprintName);
    if (!Blueprint)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Blueprint '%s' not found"), *BlueprintName));
    }

    // Locate the target graph.
    UEdGraph* TargetGraph = nullptr;

    // Match against EventGraph (UbergraphPages) by exact or contains.
    for (UEdGraph* Graph : Blueprint->UbergraphPages)
    {
        if (Graph && (Graph->GetName().Equals(GraphName, ESearchCase::IgnoreCase)
                      || (GraphName.Equals(TEXT("EventGraph"), ESearchCase::IgnoreCase)
                          && Graph->GetName().Contains(TEXT("EventGraph")))))
        {
            TargetGraph = Graph;
            break;
        }
    }

    // Match against function graphs.
    if (!TargetGraph)
    {
        for (UEdGraph* Graph : Blueprint->FunctionGraphs)
        {
            if (Graph && Graph->GetName().Equals(GraphName, ESearchCase::IgnoreCase))
            {
                TargetGraph = Graph;
                break;
            }
        }
    }

    // Fallback: if the user asked for "EventGraph" and the BP has none yet,
    // create one (matches how FindOrCreateEventGraph already behaves).
    if (!TargetGraph && GraphName.Equals(TEXT("EventGraph"), ESearchCase::IgnoreCase))
    {
        TargetGraph = FUnrealAICommonUtils::FindOrCreateEventGraph(Blueprint);
    }

    if (!TargetGraph)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Graph '%s' not found in blueprint '%s'"),
                            *GraphName, *BlueprintName));
    }

    // Optional: clear existing nodes before pasting.
    if (bClearGraph)
    {
        TArray<UEdGraphNode*> NodesToRemove = TargetGraph->Nodes;
        for (UEdGraphNode* Node : NodesToRemove)
        {
            if (Node)
            {
                TargetGraph->RemoveNode(Node);
            }
        }
    }

    // Sanity check: T3D text from UE always begins with "Begin Object Class=".
    // We don't hard-fail on this (the importer will reject bad text anyway),
    // but log a warning for the model's benefit.
    if (!T3DText.Contains(TEXT("Begin Object")))
    {
        UE_LOG(LogTemp, Warning,
               TEXT("paste_nodes_to_blueprint: t3d_text does not contain 'Begin Object'. "
                    "This will likely fail. Got: %s"),
               *T3DText.Left(120));
    }

    // The actual paste. ImportNodesFromText is the same call the editor uses
    // for Ctrl+V. It populates PastedNodes with whatever it successfully created.
    TSet<UEdGraphNode*> PastedNodes;
    FEdGraphUtilities::ImportNodesFromText(TargetGraph, T3DText, /*out*/ PastedNodes);

    if (PastedNodes.Num() == 0)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(
            TEXT("ImportNodesFromText produced 0 nodes. The T3D text is likely malformed. "
                 "Verify Begin Object/End Object pairs, NodeGuids, and PinIds."));
    }

    // Mark blueprint dirty so the editor saves changes.
    FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);

    // Optionally compile.
    bool bCompiled = false;
    if (bAutoCompile)
    {
        FKismetEditorUtilities::CompileBlueprint(Blueprint);
        bCompiled = true;
    }

    // Build success response.
    TSharedPtr<FJsonObject> Response = MakeShared<FJsonObject>();
    Response->SetStringField(TEXT("status"), TEXT("success"));
    Response->SetStringField(TEXT("blueprint"), BlueprintName);
    Response->SetStringField(TEXT("graph"), TargetGraph->GetName());
    Response->SetNumberField(TEXT("pasted_count"), PastedNodes.Num());
    Response->SetBoolField(TEXT("compiled"), bCompiled);

    TArray<TSharedPtr<FJsonValue>> NodeNames;
    for (UEdGraphNode* Node : PastedNodes)
    {
        if (Node)
        {
            NodeNames.Add(MakeShared<FJsonValueString>(Node->GetClass()->GetName() + TEXT("/") + Node->NodeGuid.ToString()));
        }
    }
    Response->SetArrayField(TEXT("node_names"), NodeNames);

    UE_LOG(LogTemp, Display,
           TEXT("paste_nodes_to_blueprint: pasted %d nodes into '%s' of '%s' (compiled=%s)"),
           PastedNodes.Num(), *TargetGraph->GetName(), *BlueprintName,
           bCompiled ? TEXT("true") : TEXT("false"));

    return Response;
}
