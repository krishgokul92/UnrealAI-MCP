#pragma once

#include "CoreMinimal.h"
#include "Json.h"

/**
 * Handler class for asset-registry / asset-import / material-instance
 * MCP commands (Phase 7 Wave 2).
 */
class FUnrealAIAssetCommands
{
public:
    FUnrealAIAssetCommands();

    TSharedPtr<FJsonObject> HandleCommand(const FString& CommandType, const TSharedPtr<FJsonObject>& Params);

private:
    TSharedPtr<FJsonObject> HandleFindAssets(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleReadPCGGraphContent(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleCreatePCGGraphAsset(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleCreatePCGGraphInstance(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleReadPCGComponentContent(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleAddPCGComponentToActor(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleCreatePCGVolume(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleListPCGNodeTypes(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleReadPCGGraphNodes(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleReadPCGGraphNode(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleAddPCGGraphNode(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleDeletePCGGraphNode(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleConnectPCGGraphNodes(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleDisconnectPCGGraphNodes(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleSetPCGGraphNodePosition(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleSetPCGSubgraphNodeAsset(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleAddPCGGraphComment(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleUpdatePCGGraphComment(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleDeletePCGGraphComment(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleAddPCGGraphReroute(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleUpdatePCGGraphNodeSettings(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleSetPCGGraphNodeState(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleCreatePCGGraphParameter(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleDeletePCGGraphParameter(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleRenamePCGGraphParameter(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleSetPCGGraphParameter(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleResetPCGGraphParameterOverride(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleReadBehaviorTreeContent(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleCreateBehaviorTreeAsset(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleUpdateBehaviorTreeSubtree(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleSetBehaviorTreeNodeProperties(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleValidateBehaviorTree(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleReadBlackboardContent(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleCreateBlackboardAsset(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleUpdateBlackboardKeys(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleReadNiagaraSystemContent(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleCreateNiagaraSystemAsset(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleReadNiagaraSystemEmitter(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleSetNiagaraSystemUserParameters(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleValidateNiagaraSystem(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleAddNiagaraEmitterToSystem(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleDuplicateNiagaraSystemEmitter(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleRenameNiagaraSystemEmitter(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleRemoveNiagaraSystemEmitter(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleReadAnimBlueprintContent(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleCreateAnimBlueprintAsset(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleReadAnimStateMachine(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleValidateAnimBlueprint(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleCreateAnimStateMachine(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleCreateAnimState(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleRenameAnimState(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleDeleteAnimState(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleSetAnimStateSequencePlayer(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleSetAnimStateBlendSpacePlayer(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleSetAnimStateAssetPlayerParameters(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleCreateAnimTransition(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleDeleteAnimTransition(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleSetAnimTransitionRule(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleReadDataTableContent(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleReadDataTableRow(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleReadCurveTableContent(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleReadCurveTableRow(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleCreateCurveTableAsset(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleUpsertCurveTableRow(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleDeleteCurveTableRow(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleRenameCurveTableRow(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleValidateDataTableRowImport(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleValidateCurveTableRowImport(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleUpsertDataTableRow(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleDeleteDataTableRow(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleRenameDataTableRow(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleDuplicateDataTableRow(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleMoveDataTableRow(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleCreateDataTableAsset(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleGetAssetDependencies(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleGetReferencers(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleCreateMaterialInstance(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleImportAsset(const TSharedPtr<FJsonObject>& Params);
};
