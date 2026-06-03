#pragma once

#include "CoreMinimal.h"
#include "Json.h"

/**
 * Phase 7 Wave 3b — landscape helpers.
 *
 * Initial slice:
 *   - get_landscapes
 *   - read_landscape_content
 *   - sample_landscape_point
 *   - sample_landscape_points
 *   - sample_landscape_grid
 *   - sample_landscape_region
 *   - sample_landscape_height_region
 *   - sample_landscape_weight_region
 *   - paint_landscape_layer_region
 *   - sculpt_landscape_height_region
 *   - rebuild_landscape
 *   - create_landscape
 *   - set_landscape_flat_height
 *   - import_landscape_heightmap
 */
class UNREALAI_API FUnrealAILandscapeCommands
{
public:
    FUnrealAILandscapeCommands();

    TSharedPtr<FJsonObject> HandleCommand(const FString& CommandType, const TSharedPtr<FJsonObject>& Params);

private:
    TSharedPtr<FJsonObject> HandleGetLandscapes(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleReadLandscapeContent(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleSampleLandscapePoint(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleSampleLandscapePoints(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleSampleLandscapeGrid(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleSampleLandscapeRegion(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleSampleLandscapeHeightRegion(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleSampleLandscapeWeightRegion(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandlePaintLandscapeLayerRegion(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleSculptLandscapeHeightRegion(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleRebuildLandscape(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleCreateLandscape(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleSetLandscapeFlatHeight(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleImportLandscapeHeightmap(const TSharedPtr<FJsonObject>& Params);
};