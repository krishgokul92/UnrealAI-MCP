#pragma once

#include "CoreMinimal.h"
#include "Json.h"

/**
 * Phase 7 Wave 3a — Level design helpers.
 *
 * Tools:
 *   new_blank_map(save_existing_map=false)
 *   open_level(asset_path, save_current_level=false)
 *   snap_actors_to_grid(actor_names, grid_size=100.0, snap_rotation=false, rotation_grid=15.0)
 *   align_actors(actor_names, axis="z", mode="min")  // axis: x|y|z ; mode: min|max|center|average
 *   duplicate_actor(actor_name, new_name="", offset_location=[0,0,0])
 *   focus_viewport(actor_name="", location=[x,y,z])  // either focus on actor or fly to coords
 */
class UNREALAI_API FUnrealAILevelCommands
{
public:
    FUnrealAILevelCommands();

    TSharedPtr<FJsonObject> HandleCommand(const FString& CommandType, const TSharedPtr<FJsonObject>& Params);

private:
    TSharedPtr<FJsonObject> HandleNewBlankMap(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleOpenLevel(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleSnapActorsToGrid(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleAlignActors(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleDuplicateActor(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleFocusViewport(const TSharedPtr<FJsonObject>& Params);
};
