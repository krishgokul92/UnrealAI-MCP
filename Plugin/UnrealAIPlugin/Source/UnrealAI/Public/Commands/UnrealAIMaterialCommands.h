#pragma once

#include "CoreMinimal.h"
#include "Json.h"

/**
 * Handler class for material graph inspection and authoring MCP commands.
 */
class FUnrealAIMaterialCommands
{
public:
    FUnrealAIMaterialCommands();

    TSharedPtr<FJsonObject> HandleCommand(const FString& CommandType, const TSharedPtr<FJsonObject>& Params);

private:
    TSharedPtr<FJsonObject> HandleCreateMaterialAsset(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleCreateMaterialFunctionAsset(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleGetMaterialExpressions(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleGetMaterialConnections(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleGetMaterialParameters(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleSetMaterialParameters(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleSetMaterialInstanceParameters(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleValidateMaterialGraph(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleCreateMaterialExpression(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleDeleteMaterialExpression(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleDeleteMaterialExpressions(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleReplaceMaterialExpression(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleConnectMaterialExpressions(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleDisconnectMaterialExpressions(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleConnectMaterialProperty(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleDisconnectMaterialProperty(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleRecompileMaterial(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleLayoutMaterialExpressions(const TSharedPtr<FJsonObject>& Params);
};