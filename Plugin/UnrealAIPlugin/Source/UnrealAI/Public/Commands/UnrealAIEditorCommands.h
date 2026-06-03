#pragma once

#include "CoreMinimal.h"
#include "Json.h"

/**
 * Handler class for Editor-related MCP commands
 * Handles viewport control, actor manipulation, and level management
 */
class UNREALAI_API FUnrealAIEditorCommands
{
public:
    	FUnrealAIEditorCommands();

    // Handle editor commands
    TSharedPtr<FJsonObject> HandleCommand(const FString& CommandType, const TSharedPtr<FJsonObject>& Params);

private:
    // Actor manipulation commands
    TSharedPtr<FJsonObject> HandleGetActorsInLevel(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleGetSelectedActors(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleGetLevelViewportInfo(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleGetWorldPartitionInfo(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleGetAllLayers(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleCreateLayer(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleRenameLayer(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleDeleteLayer(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleSetLayerVisibility(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleToggleLayerVisibility(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleMakeAllLayersVisible(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleGetActorLayers(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleGetActorsInLayer(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleAddActorToLayer(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleRemoveActorFromLayer(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleAddSelectedActorsToLayer(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleRemoveSelectedActorsFromLayer(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleSelectActorsInLayer(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleDeselectActorsInLayer(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleSelectActors(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleClearActorSelection(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleFindActorsByName(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleSpawnActor(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleDeleteActor(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleSetActorTransform(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleSaveLevel(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleUndoLastAction(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleRedoLastAction(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleCaptureViewportScreenshot(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandlePlaceInGrid(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandlePlaceInCircle(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandlePlaceAlongSpline(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleScatterInArea(const TSharedPtr<FJsonObject>& Params);

    // Blueprint actor spawning
    TSharedPtr<FJsonObject> HandleSpawnBlueprintActor(const TSharedPtr<FJsonObject>& Params);
}; 