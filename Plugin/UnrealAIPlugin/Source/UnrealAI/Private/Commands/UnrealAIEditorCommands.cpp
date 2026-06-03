#include "Commands/UnrealAIEditorCommands.h"
#include "Commands/UnrealAICommonUtils.h"
#include "Editor.h"
#include "EditorViewportClient.h"
#include "LevelEditorViewport.h"
#include "FileHelpers.h"
#include "ImageUtils.h"
#include "HighResScreenshot.h"
#include "Engine/GameViewportClient.h"
#include "Misc/FileHelper.h"
#include "GameFramework/Actor.h"
#include "Engine/Selection.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/DirectionalLight.h"
#include "Engine/PointLight.h"
#include "Engine/SpotLight.h"
#include "Camera/CameraActor.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SplineComponent.h"
#include "EditorSubsystem.h"
#include "Subsystems/EditorActorSubsystem.h"
#include "Layers/Layer.h"
#include "Layers/LayersSubsystem.h"
#include "WorldPartition/WorldPartition.h"
#include "WorldPartition/WorldPartitionSubsystem.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "EditorAssetLibrary.h"
#include "HAL/FileManager.h"
#include "Misc/DateTime.h"
#include "Commands/UnrealAIBlueprintCommands.h"
#include "Misc/Paths.h"

namespace
{
    bool TryGetTrimmedStringParam(const TSharedPtr<FJsonObject>& Params, const TCHAR* FieldName, FString& OutValue)
    {
        if (!Params->TryGetStringField(FieldName, OutValue))
        {
            return false;
        }

        OutValue = OutValue.TrimStartAndEnd();
        return !OutValue.IsEmpty();
    }

    TArray<TSharedPtr<FJsonValue>> GetSelectedActorsJsonArray()
    {
        TArray<TSharedPtr<FJsonValue>> SelectedActorArray;
        if (!GEditor)
        {
            return SelectedActorArray;
        }

        if (USelection* SelectedActors = GEditor->GetSelectedActors())
        {
            for (FSelectionIterator It(*SelectedActors); It; ++It)
            {
                if (AActor* Actor = Cast<AActor>(*It))
                {
                    SelectedActorArray.Add(FUnrealAICommonUtils::ActorToJson(Actor));
                }
            }
        }

        return SelectedActorArray;
    }

    TArray<FString> GetSortedSelectedActorNames()
    {
        TArray<FString> SelectedActorNames;
        if (!GEditor)
        {
            return SelectedActorNames;
        }

        if (USelection* SelectedActors = GEditor->GetSelectedActors())
        {
            for (FSelectionIterator It(*SelectedActors); It; ++It)
            {
                if (AActor* Actor = Cast<AActor>(*It))
                {
                    SelectedActorNames.Add(Actor->GetName());
                }
            }
        }

        SelectedActorNames.Sort();
        return SelectedActorNames;
    }

    bool AreStringArraysEqual(const TArray<FString>& Left, const TArray<FString>& Right)
    {
        if (Left.Num() != Right.Num())
        {
            return false;
        }

        for (int32 Index = 0; Index < Left.Num(); ++Index)
        {
            if (Left[Index] != Right[Index])
            {
                return false;
            }
        }

        return true;
    }

    void AddVectorArrayField(const TSharedPtr<FJsonObject>& JsonObject, const FString& FieldName, const FVector& Value)
    {
        TArray<TSharedPtr<FJsonValue>> ArrayValues;
        ArrayValues.Add(MakeShared<FJsonValueNumber>(Value.X));
        ArrayValues.Add(MakeShared<FJsonValueNumber>(Value.Y));
        ArrayValues.Add(MakeShared<FJsonValueNumber>(Value.Z));
        JsonObject->SetArrayField(FieldName, ArrayValues);
    }

    void AddRotatorArrayField(const TSharedPtr<FJsonObject>& JsonObject, const FString& FieldName, const FRotator& Value)
    {
        TArray<TSharedPtr<FJsonValue>> ArrayValues;
        ArrayValues.Add(MakeShared<FJsonValueNumber>(Value.Pitch));
        ArrayValues.Add(MakeShared<FJsonValueNumber>(Value.Yaw));
        ArrayValues.Add(MakeShared<FJsonValueNumber>(Value.Roll));
        JsonObject->SetArrayField(FieldName, ArrayValues);
    }

    TSharedPtr<FJsonObject> LevelViewportClientToJson(FLevelEditorViewportClient* ViewportClient, int32 ViewportIndex)
    {
        TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
        Result->SetNumberField(TEXT("index"), ViewportIndex);
        AddVectorArrayField(Result, TEXT("view_location"), ViewportClient->GetViewLocation());
        AddRotatorArrayField(Result, TEXT("view_rotation"), ViewportClient->GetViewRotation());
        Result->SetNumberField(TEXT("fov"), ViewportClient->FOVAngle);
        Result->SetBoolField(TEXT("is_perspective"), ViewportClient->IsPerspective());
        Result->SetBoolField(TEXT("is_realtime"), ViewportClient->IsRealtime());
        return Result;
    }

    TArray<TSharedPtr<FJsonValue>> NameArrayToJson(const TArray<FName>& Names)
    {
        TArray<FName> SortedNames = Names;
        SortedNames.Sort([](const FName& Left, const FName& Right)
        {
            return Left.LexicalLess(Right);
        });

        TArray<TSharedPtr<FJsonValue>> JsonArray;
        JsonArray.Reserve(SortedNames.Num());
        for (const FName& Name : SortedNames)
        {
            JsonArray.Add(MakeShared<FJsonValueString>(Name.ToString()));
        }

        return JsonArray;
    }

    TArray<TSharedPtr<FJsonValue>> ActorsToJsonArray(TArray<AActor*> Actors)
    {
        Actors.Sort([](const AActor& Left, const AActor& Right)
        {
            return Left.GetActorLabel() < Right.GetActorLabel();
        });

        TArray<TSharedPtr<FJsonValue>> JsonArray;
        JsonArray.Reserve(Actors.Num());
        for (AActor* Actor : Actors)
        {
            if (Actor)
            {
                JsonArray.Add(FUnrealAICommonUtils::ActorToJson(Actor));
            }
        }

        return JsonArray;
    }

    TSharedPtr<FJsonObject> LayerToJson(ULayer* Layer, ULayersSubsystem* LayersSubsystem)
    {
        TSharedPtr<FJsonObject> LayerObject = MakeShared<FJsonObject>();
        LayerObject->SetStringField(TEXT("name"), Layer->GetLayerName().ToString());
        LayerObject->SetBoolField(TEXT("is_visible"), Layer->IsVisible());
        LayerObject->SetNumberField(TEXT("actor_count"), LayersSubsystem->GetActorsFromLayer(Layer->GetLayerName()).Num());
        return LayerObject;
    }

    AActor* FindActorByNameInWorld(UWorld* World, const FString& ActorName)
    {
        if (!World)
        {
            return nullptr;
        }

        TArray<AActor*> AllActors;
        UGameplayStatics::GetAllActorsOfClass(World, AActor::StaticClass(), AllActors);
        for (AActor* Actor : AllActors)
        {
            if (Actor && (Actor->GetName() == ActorName || Actor->GetActorLabel() == ActorName))
            {
                return Actor;
            }
        }

        return nullptr;
    }

    bool DoesActorNameExistInWorld(UWorld* World, const FString& ActorName)
    {
        return FindActorByNameInWorld(World, ActorName) != nullptr;
    }

    FString BuildIndexedActorName(const FString& NamePrefix, int32 Index)
    {
        return FString::Printf(TEXT("%s_%03d"), *NamePrefix, Index + 1);
    }

    bool TryGetPositiveIntegerField(const TSharedPtr<FJsonObject>& Params, const TCHAR* FieldName, int32& OutValue)
    {
        double NumberValue = 0.0;
        if (!Params->TryGetNumberField(FieldName, NumberValue))
        {
            return false;
        }

        OutValue = static_cast<int32>(NumberValue);
        return OutValue > 0;
    }

    bool TryGetNonNegativeSeedField(const TSharedPtr<FJsonObject>& Params, const TCHAR* FieldName, int32& OutValue)
    {
        double NumberValue = 0.0;
        if (!Params->TryGetNumberField(FieldName, NumberValue))
        {
            return false;
        }

        OutValue = static_cast<int32>(NumberValue);
        return true;
    }

    AActor* SpawnConfiguredActor(
        UWorld* World,
        const FString& ActorType,
        const FString& ActorName,
        const FVector& Location,
        const FRotator& Rotation,
        const FVector& Scale,
        const FString& StaticMeshPath,
        FString& OutError,
        bool bStrictStaticMeshPath = false)
    {
        if (!World)
        {
            OutError = TEXT("Failed to get editor world");
            return nullptr;
        }

        if (DoesActorNameExistInWorld(World, ActorName))
        {
            OutError = FString::Printf(TEXT("Actor with name '%s' already exists"), *ActorName);
            return nullptr;
        }

        FActorSpawnParameters SpawnParams;
        SpawnParams.Name = *ActorName;
        SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;

        AActor* NewActor = nullptr;
        if (ActorType == TEXT("StaticMeshActor"))
        {
            AStaticMeshActor* NewMeshActor = World->SpawnActor<AStaticMeshActor>(AStaticMeshActor::StaticClass(), Location, Rotation, SpawnParams);
            if (!NewMeshActor)
            {
                OutError = TEXT("Failed to create actor");
                return nullptr;
            }

            if (!StaticMeshPath.IsEmpty())
            {
                UStaticMesh* Mesh = Cast<UStaticMesh>(UEditorAssetLibrary::LoadAsset(StaticMeshPath));
                if (!Mesh)
                {
                    if (bStrictStaticMeshPath)
                    {
                        NewMeshActor->Destroy();
                        OutError = FString::Printf(TEXT("Failed to load static mesh: %s"), *StaticMeshPath);
                        return nullptr;
                    }

                    UE_LOG(LogTemp, Warning, TEXT("Could not find static mesh at path: %s"), *StaticMeshPath);
                }
                else
                {
                    NewMeshActor->GetStaticMeshComponent()->SetStaticMesh(Mesh);
                }
            }

            NewActor = NewMeshActor;
        }
        else if (ActorType == TEXT("PointLight"))
        {
            NewActor = World->SpawnActor<APointLight>(APointLight::StaticClass(), Location, Rotation, SpawnParams);
        }
        else if (ActorType == TEXT("SpotLight"))
        {
            NewActor = World->SpawnActor<ASpotLight>(ASpotLight::StaticClass(), Location, Rotation, SpawnParams);
        }
        else if (ActorType == TEXT("DirectionalLight"))
        {
            NewActor = World->SpawnActor<ADirectionalLight>(ADirectionalLight::StaticClass(), Location, Rotation, SpawnParams);
        }
        else if (ActorType == TEXT("CameraActor"))
        {
            NewActor = World->SpawnActor<ACameraActor>(ACameraActor::StaticClass(), Location, Rotation, SpawnParams);
        }
        else
        {
            OutError = FString::Printf(
                TEXT("Unknown actor type: %s. Supported types: StaticMeshActor, PointLight, SpotLight, DirectionalLight, CameraActor"),
                *ActorType);
            return nullptr;
        }

        if (!NewActor)
        {
            OutError = TEXT("Failed to create actor");
            return nullptr;
        }

        FTransform Transform = NewActor->GetActorTransform();
        Transform.SetLocation(Location);
        Transform.SetRotation(Rotation.Quaternion());
        Transform.SetScale3D(Scale);
        NewActor->SetActorTransform(Transform);
        NewActor->SetActorLabel(ActorName);
        return NewActor;
    }

    FRotator ComposeSplinePlacementRotation(const FString& OrientationMode, const FRotator& BaseRotation, USplineComponent* SplineComponent, float Distance)
    {
        if (!SplineComponent || OrientationMode == TEXT("none"))
        {
            return BaseRotation;
        }

        FRotator SplineRotation = SplineComponent->GetRotationAtDistanceAlongSpline(Distance, ESplineCoordinateSpace::World);
        if (OrientationMode == TEXT("yaw"))
        {
            SplineRotation.Pitch = 0.0f;
            SplineRotation.Roll = 0.0f;
        }
        return SplineRotation + BaseRotation;
    }
}

FUnrealAIEditorCommands::FUnrealAIEditorCommands()
{
}

TSharedPtr<FJsonObject> FUnrealAIEditorCommands::HandleCommand(const FString& CommandType, const TSharedPtr<FJsonObject>& Params)
{
    // Actor manipulation commands
    if (CommandType == TEXT("get_actors_in_level"))
    {
        return HandleGetActorsInLevel(Params);
    }
    else if (CommandType == TEXT("get_selected_actors"))
    {
        return HandleGetSelectedActors(Params);
    }
    else if (CommandType == TEXT("get_level_viewport_info"))
    {
        return HandleGetLevelViewportInfo(Params);
    }
    else if (CommandType == TEXT("get_world_partition_info"))
    {
        return HandleGetWorldPartitionInfo(Params);
    }
    else if (CommandType == TEXT("get_all_layers"))
    {
        return HandleGetAllLayers(Params);
    }
    else if (CommandType == TEXT("create_layer"))
    {
        return HandleCreateLayer(Params);
    }
    else if (CommandType == TEXT("rename_layer"))
    {
        return HandleRenameLayer(Params);
    }
    else if (CommandType == TEXT("delete_layer"))
    {
        return HandleDeleteLayer(Params);
    }
    else if (CommandType == TEXT("set_layer_visibility"))
    {
        return HandleSetLayerVisibility(Params);
    }
    else if (CommandType == TEXT("toggle_layer_visibility"))
    {
        return HandleToggleLayerVisibility(Params);
    }
    else if (CommandType == TEXT("make_all_layers_visible"))
    {
        return HandleMakeAllLayersVisible(Params);
    }
    else if (CommandType == TEXT("get_actor_layers"))
    {
        return HandleGetActorLayers(Params);
    }
    else if (CommandType == TEXT("get_actors_in_layer"))
    {
        return HandleGetActorsInLayer(Params);
    }
    else if (CommandType == TEXT("add_actor_to_layer"))
    {
        return HandleAddActorToLayer(Params);
    }
    else if (CommandType == TEXT("remove_actor_from_layer"))
    {
        return HandleRemoveActorFromLayer(Params);
    }
    else if (CommandType == TEXT("add_selected_actors_to_layer"))
    {
        return HandleAddSelectedActorsToLayer(Params);
    }
    else if (CommandType == TEXT("remove_selected_actors_from_layer"))
    {
        return HandleRemoveSelectedActorsFromLayer(Params);
    }
    else if (CommandType == TEXT("select_actors_in_layer"))
    {
        return HandleSelectActorsInLayer(Params);
    }
    else if (CommandType == TEXT("deselect_actors_in_layer"))
    {
        return HandleDeselectActorsInLayer(Params);
    }
    else if (CommandType == TEXT("select_actors"))
    {
        return HandleSelectActors(Params);
    }
    else if (CommandType == TEXT("clear_actor_selection"))
    {
        return HandleClearActorSelection(Params);
    }
    else if (CommandType == TEXT("find_actors_by_name"))
    {
        return HandleFindActorsByName(Params);
    }
    else if (CommandType == TEXT("spawn_actor"))
    {
        return HandleSpawnActor(Params);
    }
    else if (CommandType == TEXT("delete_actor"))
    {
        return HandleDeleteActor(Params);
    }
    else if (CommandType == TEXT("set_actor_transform"))
    {
        return HandleSetActorTransform(Params);
    }
    else if (CommandType == TEXT("save_level"))
    {
        return HandleSaveLevel(Params);
    }
    else if (CommandType == TEXT("undo_last_action"))
    {
        return HandleUndoLastAction(Params);
    }
    else if (CommandType == TEXT("redo_last_action"))
    {
        return HandleRedoLastAction(Params);
    }
    else if (CommandType == TEXT("capture_viewport_screenshot"))
    {
        return HandleCaptureViewportScreenshot(Params);
    }
    else if (CommandType == TEXT("place_in_grid"))
    {
        return HandlePlaceInGrid(Params);
    }
    else if (CommandType == TEXT("place_in_circle"))
    {
        return HandlePlaceInCircle(Params);
    }
    else if (CommandType == TEXT("place_along_spline"))
    {
        return HandlePlaceAlongSpline(Params);
    }
    else if (CommandType == TEXT("scatter_in_area"))
    {
        return HandleScatterInArea(Params);
    }
    // Blueprint actor spawning
    else if (CommandType == TEXT("spawn_blueprint_actor"))
    {
        return HandleSpawnBlueprintActor(Params);
    }
    
    return FUnrealAICommonUtils::CreateErrorResponse(FString::Printf(TEXT("Unknown editor command: %s"), *CommandType));
}

TSharedPtr<FJsonObject> FUnrealAIEditorCommands::HandleGetActorsInLevel(const TSharedPtr<FJsonObject>& Params)
{
    TArray<AActor*> AllActors;
    UGameplayStatics::GetAllActorsOfClass(GWorld, AActor::StaticClass(), AllActors);
    
    TArray<TSharedPtr<FJsonValue>> ActorArray;
    for (AActor* Actor : AllActors)
    {
        if (Actor)
        {
            ActorArray.Add(FUnrealAICommonUtils::ActorToJson(Actor));
        }
    }
    
    TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
    ResultObj->SetArrayField(TEXT("actors"), ActorArray);
    
    return ResultObj;
}

TSharedPtr<FJsonObject> FUnrealAIEditorCommands::HandleGetSelectedActors(const TSharedPtr<FJsonObject>& Params)
{
    if (!GEditor)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("GEditor unavailable"));
    }

    const TArray<TSharedPtr<FJsonValue>> SelectedActorArray = GetSelectedActorsJsonArray();

    TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
    ResultObj->SetArrayField(TEXT("actors"), SelectedActorArray);
    ResultObj->SetNumberField(TEXT("selected_count"), SelectedActorArray.Num());

    if (UWorld* EditorWorld = GEditor->GetEditorWorldContext().World())
    {
        ResultObj->SetStringField(TEXT("current_level"), EditorWorld->GetMapName());
    }

    return ResultObj;
}

TSharedPtr<FJsonObject> FUnrealAIEditorCommands::HandleGetLevelViewportInfo(const TSharedPtr<FJsonObject>& Params)
{
    if (!GEditor)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("GEditor unavailable"));
    }

    TArray<TSharedPtr<FJsonValue>> ViewportArray;
    int32 ViewportIndex = 0;
    for (FLevelEditorViewportClient* ViewportClient : GEditor->GetLevelViewportClients())
    {
        if (!ViewportClient)
        {
            continue;
        }

        ViewportArray.Add(MakeShared<FJsonValueObject>(LevelViewportClientToJson(ViewportClient, ViewportIndex)));
        ++ViewportIndex;
    }

    TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
    ResultObj->SetArrayField(TEXT("viewports"), ViewportArray);
    ResultObj->SetNumberField(TEXT("viewport_count"), ViewportArray.Num());
    return ResultObj;
}

TSharedPtr<FJsonObject> FUnrealAIEditorCommands::HandleGetWorldPartitionInfo(const TSharedPtr<FJsonObject>& Params)
{
    if (!GEditor)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("GEditor unavailable"));
    }

    UWorld* World = GEditor->GetEditorWorldContext().World();
    if (!World)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Failed to get editor world"));
    }

    UWorldPartition* WorldPartition = World->GetWorldPartition();
    UWorldPartitionSubsystem* WorldPartitionSubsystem = World->GetSubsystem<UWorldPartitionSubsystem>();

    TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
    ResultObj->SetBoolField(TEXT("success"), true);
    ResultObj->SetBoolField(TEXT("is_partitioned_world"), World->IsPartitionedWorld());
    ResultObj->SetBoolField(TEXT("world_partition_subsystem_available"), WorldPartitionSubsystem != nullptr);
    ResultObj->SetBoolField(TEXT("is_all_streaming_completed"), WorldPartitionSubsystem ? WorldPartitionSubsystem->IsAllStreamingCompleted() : true);
    ResultObj->SetStringField(TEXT("world_partition_name"), WorldPartition ? WorldPartition->GetName() : TEXT(""));
    ResultObj->SetStringField(TEXT("world_partition_path"), WorldPartition ? WorldPartition->GetPathName() : TEXT(""));
    ResultObj->SetStringField(TEXT("world_partition_class"), WorldPartition ? WorldPartition->GetClass()->GetName() : TEXT(""));
    ResultObj->SetStringField(TEXT("current_level"), World->GetMapName());
    return ResultObj;
}

TSharedPtr<FJsonObject> FUnrealAIEditorCommands::HandleGetAllLayers(const TSharedPtr<FJsonObject>& Params)
{
    if (!GEditor)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("GEditor unavailable"));
    }

    ULayersSubsystem* LayersSubsystem = GEditor->GetEditorSubsystem<ULayersSubsystem>();
    if (!LayersSubsystem)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("ULayersSubsystem unavailable"));
    }

    TArray<ULayer*> Layers;
    LayersSubsystem->AddAllLayersTo(Layers);
    Layers.Sort([](const ULayer& Left, const ULayer& Right)
    {
        return Left.GetLayerName().LexicalLess(Right.GetLayerName());
    });

    int32 VisibleLayerCount = 0;
    TArray<TSharedPtr<FJsonValue>> LayerArray;
    LayerArray.Reserve(Layers.Num());

    for (ULayer* Layer : Layers)
    {
        if (!Layer)
        {
            continue;
        }

        const bool bIsVisible = Layer->IsVisible();
        VisibleLayerCount += bIsVisible ? 1 : 0;
        LayerArray.Add(MakeShared<FJsonValueObject>(LayerToJson(Layer, LayersSubsystem)));
    }

    TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
    ResultObj->SetArrayField(TEXT("layers"), LayerArray);
    ResultObj->SetNumberField(TEXT("layer_count"), LayerArray.Num());
    ResultObj->SetNumberField(TEXT("visible_layer_count"), VisibleLayerCount);
    ResultObj->SetNumberField(TEXT("hidden_layer_count"), LayerArray.Num() - VisibleLayerCount);

    if (UWorld* World = GEditor->GetEditorWorldContext().World())
    {
        ResultObj->SetStringField(TEXT("current_level"), World->GetMapName());
    }

    return ResultObj;
}

TSharedPtr<FJsonObject> FUnrealAIEditorCommands::HandleCreateLayer(const TSharedPtr<FJsonObject>& Params)
{
    if (!GEditor)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("GEditor unavailable"));
    }

    FString LayerNameString;
    if (!TryGetTrimmedStringParam(Params, TEXT("layer_name"), LayerNameString))
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing or empty 'layer_name' parameter"));
    }

    ULayersSubsystem* LayersSubsystem = GEditor->GetEditorSubsystem<ULayersSubsystem>();
    if (!LayersSubsystem)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("ULayersSubsystem unavailable"));
    }

    const FName LayerName(*LayerNameString);
    if (LayersSubsystem->IsLayer(LayerName))
    {
        return FUnrealAICommonUtils::CreateErrorResponse(FString::Printf(TEXT("Layer already exists: %s"), *LayerNameString));
    }

    ULayer* Layer = LayersSubsystem->CreateLayer(LayerName);
    if (!Layer)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(FString::Printf(TEXT("Failed to create layer: %s"), *LayerNameString));
    }

    TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
    ResultObj->SetBoolField(TEXT("success"), true);
    ResultObj->SetBoolField(TEXT("changed"), true);
    ResultObj->SetStringField(TEXT("layer_name"), Layer->GetLayerName().ToString());
    ResultObj->SetBoolField(TEXT("is_visible"), Layer->IsVisible());
    ResultObj->SetObjectField(TEXT("layer"), LayerToJson(Layer, LayersSubsystem));

    if (UWorld* World = GEditor->GetEditorWorldContext().World())
    {
        ResultObj->SetStringField(TEXT("current_level"), World->GetMapName());
    }

    return ResultObj;
}

TSharedPtr<FJsonObject> FUnrealAIEditorCommands::HandleRenameLayer(const TSharedPtr<FJsonObject>& Params)
{
    if (!GEditor)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("GEditor unavailable"));
    }

    FString LayerNameString;
    if (!TryGetTrimmedStringParam(Params, TEXT("layer_name"), LayerNameString))
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing or empty 'layer_name' parameter"));
    }

    FString NewLayerNameString;
    if (!TryGetTrimmedStringParam(Params, TEXT("new_layer_name"), NewLayerNameString))
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing or empty 'new_layer_name' parameter"));
    }

    ULayersSubsystem* LayersSubsystem = GEditor->GetEditorSubsystem<ULayersSubsystem>();
    if (!LayersSubsystem)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("ULayersSubsystem unavailable"));
    }

    const FName LayerName(*LayerNameString);
    const FName NewLayerName(*NewLayerNameString);
    if (!LayersSubsystem->IsLayer(LayerName))
    {
        return FUnrealAICommonUtils::CreateErrorResponse(FString::Printf(TEXT("Layer not found: %s"), *LayerNameString));
    }

    ULayer* ExistingLayer = LayersSubsystem->GetLayer(LayerName);
    if (!ExistingLayer)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(FString::Printf(TEXT("Layer object unavailable: %s"), *LayerNameString));
    }

    if (LayerName == NewLayerName)
    {
        TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
        ResultObj->SetBoolField(TEXT("success"), true);
        ResultObj->SetBoolField(TEXT("changed"), false);
        ResultObj->SetStringField(TEXT("layer_name"), LayerName.ToString());
        ResultObj->SetStringField(TEXT("new_layer_name"), NewLayerName.ToString());
        ResultObj->SetObjectField(TEXT("layer"), LayerToJson(ExistingLayer, LayersSubsystem));

        if (UWorld* World = GEditor->GetEditorWorldContext().World())
        {
            ResultObj->SetStringField(TEXT("current_level"), World->GetMapName());
        }

        return ResultObj;
    }

    if (LayersSubsystem->IsLayer(NewLayerName))
    {
        return FUnrealAICommonUtils::CreateErrorResponse(FString::Printf(TEXT("Layer already exists: %s"), *NewLayerNameString));
    }

    const bool bChanged = LayersSubsystem->RenameLayer(LayerName, NewLayerName);
    ULayer* RenamedLayer = LayersSubsystem->GetLayer(NewLayerName);
    if (!RenamedLayer)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(FString::Printf(TEXT("Layer object unavailable after rename: %s"), *NewLayerNameString));
    }

    TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
    ResultObj->SetBoolField(TEXT("success"), true);
    ResultObj->SetBoolField(TEXT("changed"), bChanged);
    ResultObj->SetStringField(TEXT("layer_name"), LayerName.ToString());
    ResultObj->SetStringField(TEXT("new_layer_name"), NewLayerName.ToString());
    ResultObj->SetObjectField(TEXT("layer"), LayerToJson(RenamedLayer, LayersSubsystem));

    if (UWorld* World = GEditor->GetEditorWorldContext().World())
    {
        ResultObj->SetStringField(TEXT("current_level"), World->GetMapName());
    }

    return ResultObj;
}

TSharedPtr<FJsonObject> FUnrealAIEditorCommands::HandleDeleteLayer(const TSharedPtr<FJsonObject>& Params)
{
    if (!GEditor)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("GEditor unavailable"));
    }

    FString LayerNameString;
    if (!TryGetTrimmedStringParam(Params, TEXT("layer_name"), LayerNameString))
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing or empty 'layer_name' parameter"));
    }

    ULayersSubsystem* LayersSubsystem = GEditor->GetEditorSubsystem<ULayersSubsystem>();
    if (!LayersSubsystem)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("ULayersSubsystem unavailable"));
    }

    const FName LayerName(*LayerNameString);
    if (!LayersSubsystem->IsLayer(LayerName))
    {
        return FUnrealAICommonUtils::CreateErrorResponse(FString::Printf(TEXT("Layer not found: %s"), *LayerNameString));
    }

    TArray<AActor*> ActorsInLayer = LayersSubsystem->GetActorsFromLayer(LayerName);
    TSharedPtr<FJsonObject> DeletedLayerSnapshot = LayerToJson(LayersSubsystem->GetLayer(LayerName), LayersSubsystem);
    const TArray<TSharedPtr<FJsonValue>> ActorsInLayerJson = ActorsToJsonArray(ActorsInLayer);

    LayersSubsystem->DeleteLayer(LayerName);

    TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
    ResultObj->SetBoolField(TEXT("success"), true);
    ResultObj->SetBoolField(TEXT("changed"), true);
    ResultObj->SetStringField(TEXT("layer_name"), LayerName.ToString());
    ResultObj->SetObjectField(TEXT("deleted_layer"), DeletedLayerSnapshot);
    ResultObj->SetArrayField(TEXT("actors_removed"), ActorsInLayerJson);
    ResultObj->SetNumberField(TEXT("actor_count_removed"), ActorsInLayer.Num());
    ResultObj->SetBoolField(TEXT("layer_exists_after_delete"), LayersSubsystem->IsLayer(LayerName));

    if (UWorld* World = GEditor->GetEditorWorldContext().World())
    {
        ResultObj->SetStringField(TEXT("current_level"), World->GetMapName());
    }

    return ResultObj;
}

TSharedPtr<FJsonObject> FUnrealAIEditorCommands::HandleSetLayerVisibility(const TSharedPtr<FJsonObject>& Params)
{
    if (!GEditor)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("GEditor unavailable"));
    }

    FString LayerNameString;
    if (!TryGetTrimmedStringParam(Params, TEXT("layer_name"), LayerNameString))
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing or empty 'layer_name' parameter"));
    }

    if (!Params->HasTypedField<EJson::Boolean>(TEXT("is_visible")))
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing 'is_visible' parameter"));
    }

    const bool bIsVisible = Params->GetBoolField(TEXT("is_visible"));

    ULayersSubsystem* LayersSubsystem = GEditor->GetEditorSubsystem<ULayersSubsystem>();
    if (!LayersSubsystem)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("ULayersSubsystem unavailable"));
    }

    const FName LayerName(*LayerNameString);
    if (!LayersSubsystem->IsLayer(LayerName))
    {
        return FUnrealAICommonUtils::CreateErrorResponse(FString::Printf(TEXT("Layer not found: %s"), *LayerNameString));
    }

    ULayer* Layer = LayersSubsystem->GetLayer(LayerName);
    if (!Layer)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(FString::Printf(TEXT("Layer object unavailable: %s"), *LayerNameString));
    }

    const bool bPreviousIsVisible = Layer->IsVisible();
    LayersSubsystem->SetLayerVisibility(LayerName, bIsVisible);

    Layer = LayersSubsystem->GetLayer(LayerName);
    if (!Layer)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(FString::Printf(TEXT("Layer object unavailable after mutation: %s"), *LayerNameString));
    }

    TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
    ResultObj->SetBoolField(TEXT("success"), true);
    ResultObj->SetBoolField(TEXT("changed"), bPreviousIsVisible != Layer->IsVisible());
    ResultObj->SetStringField(TEXT("layer_name"), LayerName.ToString());
    ResultObj->SetBoolField(TEXT("previous_is_visible"), bPreviousIsVisible);
    ResultObj->SetBoolField(TEXT("is_visible"), Layer->IsVisible());
    ResultObj->SetObjectField(TEXT("layer"), LayerToJson(Layer, LayersSubsystem));

    if (UWorld* World = GEditor->GetEditorWorldContext().World())
    {
        ResultObj->SetStringField(TEXT("current_level"), World->GetMapName());
    }

    return ResultObj;
}

TSharedPtr<FJsonObject> FUnrealAIEditorCommands::HandleToggleLayerVisibility(const TSharedPtr<FJsonObject>& Params)
{
    if (!GEditor)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("GEditor unavailable"));
    }

    FString LayerNameString;
    if (!TryGetTrimmedStringParam(Params, TEXT("layer_name"), LayerNameString))
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing or empty 'layer_name' parameter"));
    }

    ULayersSubsystem* LayersSubsystem = GEditor->GetEditorSubsystem<ULayersSubsystem>();
    if (!LayersSubsystem)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("ULayersSubsystem unavailable"));
    }

    const FName LayerName(*LayerNameString);
    if (!LayersSubsystem->IsLayer(LayerName))
    {
        return FUnrealAICommonUtils::CreateErrorResponse(FString::Printf(TEXT("Layer not found: %s"), *LayerNameString));
    }

    ULayer* Layer = LayersSubsystem->GetLayer(LayerName);
    if (!Layer)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(FString::Printf(TEXT("Layer object unavailable: %s"), *LayerNameString));
    }

    const bool bPreviousIsVisible = Layer->IsVisible();
    LayersSubsystem->ToggleLayerVisibility(LayerName);

    Layer = LayersSubsystem->GetLayer(LayerName);
    if (!Layer)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(FString::Printf(TEXT("Layer object unavailable after mutation: %s"), *LayerNameString));
    }

    TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
    ResultObj->SetBoolField(TEXT("success"), true);
    ResultObj->SetBoolField(TEXT("changed"), bPreviousIsVisible != Layer->IsVisible());
    ResultObj->SetStringField(TEXT("layer_name"), LayerName.ToString());
    ResultObj->SetBoolField(TEXT("previous_is_visible"), bPreviousIsVisible);
    ResultObj->SetBoolField(TEXT("is_visible"), Layer->IsVisible());
    ResultObj->SetObjectField(TEXT("layer"), LayerToJson(Layer, LayersSubsystem));

    if (UWorld* World = GEditor->GetEditorWorldContext().World())
    {
        ResultObj->SetStringField(TEXT("current_level"), World->GetMapName());
    }

    return ResultObj;
}

TSharedPtr<FJsonObject> FUnrealAIEditorCommands::HandleMakeAllLayersVisible(const TSharedPtr<FJsonObject>& Params)
{
    if (!GEditor)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("GEditor unavailable"));
    }

    ULayersSubsystem* LayersSubsystem = GEditor->GetEditorSubsystem<ULayersSubsystem>();
    if (!LayersSubsystem)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("ULayersSubsystem unavailable"));
    }

    TArray<ULayer*> LayersBefore;
    LayersSubsystem->AddAllLayersTo(LayersBefore);

    int32 HiddenLayerCountBefore = 0;
    for (ULayer* Layer : LayersBefore)
    {
        if (Layer && !Layer->IsVisible())
        {
            ++HiddenLayerCountBefore;
        }
    }

    LayersSubsystem->MakeAllLayersVisible();

    TArray<ULayer*> LayersAfter;
    LayersSubsystem->AddAllLayersTo(LayersAfter);
    LayersAfter.Sort([](const ULayer& Left, const ULayer& Right)
    {
        return Left.GetLayerName().LexicalLess(Right.GetLayerName());
    });

    int32 VisibleLayerCount = 0;
    TArray<TSharedPtr<FJsonValue>> LayerArray;
    LayerArray.Reserve(LayersAfter.Num());
    for (ULayer* Layer : LayersAfter)
    {
        if (!Layer)
        {
            continue;
        }

        VisibleLayerCount += Layer->IsVisible() ? 1 : 0;
        LayerArray.Add(MakeShared<FJsonValueObject>(LayerToJson(Layer, LayersSubsystem)));
    }

    TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
    ResultObj->SetBoolField(TEXT("success"), true);
    ResultObj->SetBoolField(TEXT("changed"), HiddenLayerCountBefore > 0);
    ResultObj->SetNumberField(TEXT("hidden_layer_count_before"), HiddenLayerCountBefore);
    ResultObj->SetArrayField(TEXT("layers"), LayerArray);
    ResultObj->SetNumberField(TEXT("layer_count"), LayerArray.Num());
    ResultObj->SetNumberField(TEXT("visible_layer_count"), VisibleLayerCount);
    ResultObj->SetNumberField(TEXT("hidden_layer_count"), LayerArray.Num() - VisibleLayerCount);

    if (UWorld* World = GEditor->GetEditorWorldContext().World())
    {
        ResultObj->SetStringField(TEXT("current_level"), World->GetMapName());
    }

    return ResultObj;
}

TSharedPtr<FJsonObject> FUnrealAIEditorCommands::HandleGetActorLayers(const TSharedPtr<FJsonObject>& Params)
{
    if (!GEditor)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("GEditor unavailable"));
    }

    FString ActorName;
    if (!Params->TryGetStringField(TEXT("actor_name"), ActorName))
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing 'actor_name' parameter"));
    }

    ActorName = ActorName.TrimStartAndEnd();
    if (ActorName.IsEmpty())
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("'actor_name' must be a non-empty string"));
    }

    UWorld* World = GEditor->GetEditorWorldContext().World();
    if (!World)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Failed to get editor world"));
    }

    AActor* Actor = FindActorByNameInWorld(World, ActorName);
    if (!Actor)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(FString::Printf(TEXT("Actor not found: %s"), *ActorName));
    }

    TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
    ResultObj->SetField(TEXT("actor"), FUnrealAICommonUtils::ActorToJson(Actor));
    ResultObj->SetArrayField(TEXT("layers"), NameArrayToJson(Actor->Layers));
    ResultObj->SetNumberField(TEXT("layer_count"), Actor->Layers.Num());
    ResultObj->SetStringField(TEXT("current_level"), World->GetMapName());
    return ResultObj;
}

TSharedPtr<FJsonObject> FUnrealAIEditorCommands::HandleAddSelectedActorsToLayer(const TSharedPtr<FJsonObject>& Params)
{
    if (!GEditor)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("GEditor unavailable"));
    }

    FString LayerNameString;
    if (!TryGetTrimmedStringParam(Params, TEXT("layer_name"), LayerNameString))
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing or empty 'layer_name' parameter"));
    }

    ULayersSubsystem* LayersSubsystem = GEditor->GetEditorSubsystem<ULayersSubsystem>();
    if (!LayersSubsystem)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("ULayersSubsystem unavailable"));
    }

    const FName LayerName(*LayerNameString);
    const bool bLayerExisted = LayersSubsystem->IsLayer(LayerName);
    const TArray<TSharedPtr<FJsonValue>> SelectedActorArray = GetSelectedActorsJsonArray();
    const int32 SelectedActorCount = SelectedActorArray.Num();

    if (SelectedActorCount == 0)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("No actors selected"));
    }

    const bool bChanged = LayersSubsystem->AddSelectedActorsToLayer(LayerName);
    TArray<AActor*> ActorsInLayer = LayersSubsystem->GetActorsFromLayer(LayerName);

    TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
    ResultObj->SetBoolField(TEXT("success"), true);
    ResultObj->SetBoolField(TEXT("changed"), bChanged);
    ResultObj->SetBoolField(TEXT("layer_existed_before"), bLayerExisted);
    ResultObj->SetBoolField(TEXT("layer_created"), !bLayerExisted);
    ResultObj->SetStringField(TEXT("layer_name"), LayerName.ToString());
    ResultObj->SetArrayField(TEXT("selected_actors"), SelectedActorArray);
    ResultObj->SetNumberField(TEXT("selected_count"), SelectedActorCount);
    ResultObj->SetArrayField(TEXT("actors_in_layer"), ActorsToJsonArray(ActorsInLayer));
    ResultObj->SetNumberField(TEXT("actor_count_in_layer"), ActorsInLayer.Num());

    if (UWorld* World = GEditor->GetEditorWorldContext().World())
    {
        ResultObj->SetStringField(TEXT("current_level"), World->GetMapName());
    }

    return ResultObj;
}

TSharedPtr<FJsonObject> FUnrealAIEditorCommands::HandleRemoveSelectedActorsFromLayer(const TSharedPtr<FJsonObject>& Params)
{
    if (!GEditor)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("GEditor unavailable"));
    }

    FString LayerNameString;
    if (!TryGetTrimmedStringParam(Params, TEXT("layer_name"), LayerNameString))
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing or empty 'layer_name' parameter"));
    }

    ULayersSubsystem* LayersSubsystem = GEditor->GetEditorSubsystem<ULayersSubsystem>();
    if (!LayersSubsystem)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("ULayersSubsystem unavailable"));
    }

    const FName LayerName(*LayerNameString);
    if (!LayersSubsystem->IsLayer(LayerName))
    {
        return FUnrealAICommonUtils::CreateErrorResponse(FString::Printf(TEXT("Layer not found: %s"), *LayerNameString));
    }

    const TArray<TSharedPtr<FJsonValue>> SelectedActorArray = GetSelectedActorsJsonArray();
    const int32 SelectedActorCount = SelectedActorArray.Num();

    if (SelectedActorCount == 0)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("No actors selected"));
    }

    const bool bChanged = LayersSubsystem->RemoveSelectedActorsFromLayer(LayerName);
    TArray<AActor*> ActorsInLayer = LayersSubsystem->GetActorsFromLayer(LayerName);

    TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
    ResultObj->SetBoolField(TEXT("success"), true);
    ResultObj->SetBoolField(TEXT("changed"), bChanged);
    ResultObj->SetStringField(TEXT("layer_name"), LayerName.ToString());
    ResultObj->SetArrayField(TEXT("selected_actors"), SelectedActorArray);
    ResultObj->SetNumberField(TEXT("selected_count"), SelectedActorCount);
    ResultObj->SetArrayField(TEXT("actors_in_layer"), ActorsToJsonArray(ActorsInLayer));
    ResultObj->SetNumberField(TEXT("actor_count_in_layer"), ActorsInLayer.Num());

    if (UWorld* World = GEditor->GetEditorWorldContext().World())
    {
        ResultObj->SetStringField(TEXT("current_level"), World->GetMapName());
    }

    return ResultObj;
}

TSharedPtr<FJsonObject> FUnrealAIEditorCommands::HandleSelectActorsInLayer(const TSharedPtr<FJsonObject>& Params)
{
    if (!GEditor)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("GEditor unavailable"));
    }

    FString LayerNameString;
    if (!TryGetTrimmedStringParam(Params, TEXT("layer_name"), LayerNameString))
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing or empty 'layer_name' parameter"));
    }

    const bool bReplaceSelection = !Params->HasField(TEXT("replace_selection")) || Params->GetBoolField(TEXT("replace_selection"));
    const bool bSelectEvenIfHidden = Params->HasField(TEXT("select_even_if_hidden")) && Params->GetBoolField(TEXT("select_even_if_hidden"));

    ULayersSubsystem* LayersSubsystem = GEditor->GetEditorSubsystem<ULayersSubsystem>();
    if (!LayersSubsystem)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("ULayersSubsystem unavailable"));
    }

    const FName LayerName(*LayerNameString);
    if (!LayersSubsystem->IsLayer(LayerName))
    {
        return FUnrealAICommonUtils::CreateErrorResponse(FString::Printf(TEXT("Layer not found: %s"), *LayerNameString));
    }

    const TArray<FString> PreviousSelectedActorNames = GetSortedSelectedActorNames();
    const int32 PreviousSelectedCount = PreviousSelectedActorNames.Num();

    if (bReplaceSelection)
    {
        GEditor->SelectNone(/*bNoteSelectionChange=*/false, /*bDeselectBSPSurfs=*/true, /*WarnAboutManyActors=*/false);
    }

    LayersSubsystem->SelectActorsInLayer(LayerName, /*bSelect=*/true, /*bNotify=*/true, bSelectEvenIfHidden);

    const TArray<FString> CurrentSelectedActorNames = GetSortedSelectedActorNames();
    const TArray<TSharedPtr<FJsonValue>> SelectedActorArray = GetSelectedActorsJsonArray();

    TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
    ResultObj->SetBoolField(TEXT("success"), true);
    ResultObj->SetBoolField(TEXT("changed"), !AreStringArraysEqual(PreviousSelectedActorNames, CurrentSelectedActorNames));
    ResultObj->SetBoolField(TEXT("replace_selection"), bReplaceSelection);
    ResultObj->SetBoolField(TEXT("select_even_if_hidden"), bSelectEvenIfHidden);
    ResultObj->SetStringField(TEXT("layer_name"), LayerName.ToString());
    ResultObj->SetNumberField(TEXT("previous_selected_count"), PreviousSelectedCount);
    ResultObj->SetArrayField(TEXT("selected_actors"), SelectedActorArray);
    ResultObj->SetNumberField(TEXT("selected_count"), SelectedActorArray.Num());

    if (UWorld* World = GEditor->GetEditorWorldContext().World())
    {
        ResultObj->SetStringField(TEXT("current_level"), World->GetMapName());
    }

    return ResultObj;
}

TSharedPtr<FJsonObject> FUnrealAIEditorCommands::HandleDeselectActorsInLayer(const TSharedPtr<FJsonObject>& Params)
{
    if (!GEditor)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("GEditor unavailable"));
    }

    FString LayerNameString;
    if (!TryGetTrimmedStringParam(Params, TEXT("layer_name"), LayerNameString))
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing or empty 'layer_name' parameter"));
    }

    ULayersSubsystem* LayersSubsystem = GEditor->GetEditorSubsystem<ULayersSubsystem>();
    if (!LayersSubsystem)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("ULayersSubsystem unavailable"));
    }

    const FName LayerName(*LayerNameString);
    if (!LayersSubsystem->IsLayer(LayerName))
    {
        return FUnrealAICommonUtils::CreateErrorResponse(FString::Printf(TEXT("Layer not found: %s"), *LayerNameString));
    }

    const TArray<FString> PreviousSelectedActorNames = GetSortedSelectedActorNames();
    const int32 PreviousSelectedCount = PreviousSelectedActorNames.Num();

    LayersSubsystem->SelectActorsInLayer(LayerName, /*bSelect=*/false, /*bNotify=*/true, /*bSelectEvenIfHidden=*/true);

    const TArray<FString> CurrentSelectedActorNames = GetSortedSelectedActorNames();
    const TArray<TSharedPtr<FJsonValue>> SelectedActorArray = GetSelectedActorsJsonArray();

    TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
    ResultObj->SetBoolField(TEXT("success"), true);
    ResultObj->SetBoolField(TEXT("changed"), !AreStringArraysEqual(PreviousSelectedActorNames, CurrentSelectedActorNames));
    ResultObj->SetStringField(TEXT("layer_name"), LayerName.ToString());
    ResultObj->SetNumberField(TEXT("previous_selected_count"), PreviousSelectedCount);
    ResultObj->SetArrayField(TEXT("selected_actors"), SelectedActorArray);
    ResultObj->SetNumberField(TEXT("selected_count"), SelectedActorArray.Num());

    if (UWorld* World = GEditor->GetEditorWorldContext().World())
    {
        ResultObj->SetStringField(TEXT("current_level"), World->GetMapName());
    }

    return ResultObj;
}

TSharedPtr<FJsonObject> FUnrealAIEditorCommands::HandleGetActorsInLayer(const TSharedPtr<FJsonObject>& Params)
{
    if (!GEditor)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("GEditor unavailable"));
    }

    FString LayerNameString;
    if (!Params->TryGetStringField(TEXT("layer_name"), LayerNameString))
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing 'layer_name' parameter"));
    }

    LayerNameString = LayerNameString.TrimStartAndEnd();
    if (LayerNameString.IsEmpty())
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("'layer_name' must be a non-empty string"));
    }

    ULayersSubsystem* LayersSubsystem = GEditor->GetEditorSubsystem<ULayersSubsystem>();
    if (!LayersSubsystem)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("ULayersSubsystem unavailable"));
    }

    const FName LayerName(*LayerNameString);
    if (!LayersSubsystem->IsLayer(LayerName))
    {
        return FUnrealAICommonUtils::CreateErrorResponse(FString::Printf(TEXT("Layer not found: %s"), *LayerNameString));
    }

    TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
    ResultObj->SetStringField(TEXT("layer_name"), LayerName.ToString());
    ResultObj->SetArrayField(TEXT("actors"), ActorsToJsonArray(LayersSubsystem->GetActorsFromLayer(LayerName)));
    ResultObj->SetNumberField(TEXT("actor_count"), LayersSubsystem->GetActorsFromLayer(LayerName).Num());

    if (UWorld* World = GEditor->GetEditorWorldContext().World())
    {
        ResultObj->SetStringField(TEXT("current_level"), World->GetMapName());
    }

    return ResultObj;
}

TSharedPtr<FJsonObject> FUnrealAIEditorCommands::HandleAddActorToLayer(const TSharedPtr<FJsonObject>& Params)
{
    if (!GEditor)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("GEditor unavailable"));
    }

    FString ActorName;
    if (!TryGetTrimmedStringParam(Params, TEXT("actor_name"), ActorName))
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing or empty 'actor_name' parameter"));
    }

    FString LayerNameString;
    if (!TryGetTrimmedStringParam(Params, TEXT("layer_name"), LayerNameString))
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing or empty 'layer_name' parameter"));
    }

    UWorld* World = GEditor->GetEditorWorldContext().World();
    if (!World)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Failed to get editor world"));
    }

    AActor* Actor = FindActorByNameInWorld(World, ActorName);
    if (!Actor)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(FString::Printf(TEXT("Actor not found: %s"), *ActorName));
    }

    ULayersSubsystem* LayersSubsystem = GEditor->GetEditorSubsystem<ULayersSubsystem>();
    if (!LayersSubsystem)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("ULayersSubsystem unavailable"));
    }

    if (!LayersSubsystem->IsActorValidForLayer(Actor))
    {
        return FUnrealAICommonUtils::CreateErrorResponse(FString::Printf(TEXT("Actor does not support editor layers: %s"), *ActorName));
    }

    const FName LayerName(*LayerNameString);
    const bool bLayerExisted = LayersSubsystem->IsLayer(LayerName);
    const bool bWasInLayer = Actor->Layers.Contains(LayerName);
    const bool bChanged = LayersSubsystem->AddActorToLayer(Actor, LayerName);

    TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
    ResultObj->SetBoolField(TEXT("success"), true);
    ResultObj->SetBoolField(TEXT("changed"), bChanged);
    ResultObj->SetBoolField(TEXT("already_in_layer"), bWasInLayer);
    ResultObj->SetBoolField(TEXT("layer_existed_before"), bLayerExisted);
    ResultObj->SetBoolField(TEXT("layer_created"), !bLayerExisted && bChanged);
    ResultObj->SetField(TEXT("actor"), FUnrealAICommonUtils::ActorToJson(Actor));
    ResultObj->SetStringField(TEXT("layer_name"), LayerName.ToString());
    ResultObj->SetArrayField(TEXT("layers"), NameArrayToJson(Actor->Layers));
    ResultObj->SetNumberField(TEXT("layer_count"), Actor->Layers.Num());
    ResultObj->SetStringField(TEXT("current_level"), World->GetMapName());
    return ResultObj;
}

TSharedPtr<FJsonObject> FUnrealAIEditorCommands::HandleRemoveActorFromLayer(const TSharedPtr<FJsonObject>& Params)
{
    if (!GEditor)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("GEditor unavailable"));
    }

    FString ActorName;
    if (!TryGetTrimmedStringParam(Params, TEXT("actor_name"), ActorName))
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing or empty 'actor_name' parameter"));
    }

    FString LayerNameString;
    if (!TryGetTrimmedStringParam(Params, TEXT("layer_name"), LayerNameString))
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing or empty 'layer_name' parameter"));
    }

    UWorld* World = GEditor->GetEditorWorldContext().World();
    if (!World)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Failed to get editor world"));
    }

    AActor* Actor = FindActorByNameInWorld(World, ActorName);
    if (!Actor)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(FString::Printf(TEXT("Actor not found: %s"), *ActorName));
    }

    ULayersSubsystem* LayersSubsystem = GEditor->GetEditorSubsystem<ULayersSubsystem>();
    if (!LayersSubsystem)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("ULayersSubsystem unavailable"));
    }

    if (!LayersSubsystem->IsActorValidForLayer(Actor))
    {
        return FUnrealAICommonUtils::CreateErrorResponse(FString::Printf(TEXT("Actor does not support editor layers: %s"), *ActorName));
    }

    const FName LayerName(*LayerNameString);
    const bool bLayerExisted = LayersSubsystem->IsLayer(LayerName);
    const bool bWasInLayer = Actor->Layers.Contains(LayerName);
    const bool bChanged = LayersSubsystem->RemoveActorFromLayer(Actor, LayerName);

    TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
    ResultObj->SetBoolField(TEXT("success"), true);
    ResultObj->SetBoolField(TEXT("changed"), bChanged);
    ResultObj->SetBoolField(TEXT("was_in_layer"), bWasInLayer);
    ResultObj->SetBoolField(TEXT("layer_existed_before"), bLayerExisted);
    ResultObj->SetField(TEXT("actor"), FUnrealAICommonUtils::ActorToJson(Actor));
    ResultObj->SetStringField(TEXT("layer_name"), LayerName.ToString());
    ResultObj->SetArrayField(TEXT("layers"), NameArrayToJson(Actor->Layers));
    ResultObj->SetNumberField(TEXT("layer_count"), Actor->Layers.Num());
    ResultObj->SetStringField(TEXT("current_level"), World->GetMapName());
    return ResultObj;
}

TSharedPtr<FJsonObject> FUnrealAIEditorCommands::HandleSelectActors(const TSharedPtr<FJsonObject>& Params)
{
    if (!GEditor)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("GEditor unavailable"));
    }

    const TArray<TSharedPtr<FJsonValue>>* ActorNameValues = nullptr;
    if (!Params->TryGetArrayField(TEXT("actor_names"), ActorNameValues) || !ActorNameValues || ActorNameValues->Num() == 0)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing or empty 'actor_names' parameter"));
    }

    const bool bReplaceSelection = !Params->HasField(TEXT("replace_selection")) || Params->GetBoolField(TEXT("replace_selection"));
    UWorld* World = GEditor->GetEditorWorldContext().World();
    if (!World)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Failed to get editor world"));
    }

    TArray<AActor*> ActorsToSelect;
    ActorsToSelect.Reserve(ActorNameValues->Num());
    for (const TSharedPtr<FJsonValue>& ActorNameValue : *ActorNameValues)
    {
        const FString ActorName = ActorNameValue.IsValid() ? ActorNameValue->AsString() : FString();
        if (ActorName.IsEmpty())
        {
            return FUnrealAICommonUtils::CreateErrorResponse(TEXT("'actor_names' entries must be non-empty strings"));
        }

        AActor* Actor = FindActorByNameInWorld(World, ActorName);
        if (!Actor)
        {
            return FUnrealAICommonUtils::CreateErrorResponse(FString::Printf(TEXT("Actor not found: %s"), *ActorName));
        }

        ActorsToSelect.Add(Actor);
    }

    if (bReplaceSelection)
    {
        GEditor->SelectNone(/*bNoteSelectionChange=*/false, /*bDeselectBSPSurfs=*/true, /*WarnAboutManyActors=*/false);
    }

    TArray<TSharedPtr<FJsonValue>> SelectedActorArray;
    SelectedActorArray.Reserve(ActorsToSelect.Num());
    for (AActor* Actor : ActorsToSelect)
    {
        GEditor->SelectActor(Actor, /*bInSelected=*/true, /*bNotify=*/false, /*bSelectEvenIfHidden=*/true, /*bForceRefresh=*/true);
        SelectedActorArray.Add(FUnrealAICommonUtils::ActorToJson(Actor));
    }
    GEditor->NoteSelectionChange();

    TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
    ResultObj->SetBoolField(TEXT("success"), true);
    ResultObj->SetBoolField(TEXT("replace_selection"), bReplaceSelection);
    ResultObj->SetArrayField(TEXT("selected_actors"), SelectedActorArray);
    ResultObj->SetNumberField(TEXT("selected_count"), SelectedActorArray.Num());
    ResultObj->SetStringField(TEXT("current_level"), World->GetMapName());
    return ResultObj;
}

TSharedPtr<FJsonObject> FUnrealAIEditorCommands::HandleClearActorSelection(const TSharedPtr<FJsonObject>& Params)
{
    if (!GEditor)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("GEditor unavailable"));
    }

    int32 PreviousSelectionCount = 0;
    if (USelection* SelectedActors = GEditor->GetSelectedActors())
    {
        PreviousSelectionCount = SelectedActors->Num();
    }

    GEditor->SelectNone(/*bNoteSelectionChange=*/true, /*bDeselectBSPSurfs=*/true, /*WarnAboutManyActors=*/false);

    TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
    ResultObj->SetBoolField(TEXT("success"), true);
    ResultObj->SetNumberField(TEXT("previous_selected_count"), PreviousSelectionCount);
    ResultObj->SetNumberField(TEXT("selected_count"), 0);

    if (UWorld* World = GEditor->GetEditorWorldContext().World())
    {
        ResultObj->SetStringField(TEXT("current_level"), World->GetMapName());
    }

    return ResultObj;
}

TSharedPtr<FJsonObject> FUnrealAIEditorCommands::HandleFindActorsByName(const TSharedPtr<FJsonObject>& Params)
{
    FString Pattern;
    if (!Params->TryGetStringField(TEXT("pattern"), Pattern))
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing 'pattern' parameter"));
    }
    
    TArray<AActor*> AllActors;
    UGameplayStatics::GetAllActorsOfClass(GWorld, AActor::StaticClass(), AllActors);
    
    TArray<TSharedPtr<FJsonValue>> MatchingActors;
    for (AActor* Actor : AllActors)
    {
        if (Actor && Actor->GetName().Contains(Pattern))
        {
            MatchingActors.Add(FUnrealAICommonUtils::ActorToJson(Actor));
        }
    }
    
    TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
    ResultObj->SetArrayField(TEXT("actors"), MatchingActors);
    
    return ResultObj;
}

TSharedPtr<FJsonObject> FUnrealAIEditorCommands::HandleSpawnActor(const TSharedPtr<FJsonObject>& Params)
{
    FString ActorType;
    if (!TryGetTrimmedStringParam(Params, TEXT("type"), ActorType))
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing 'type' parameter"));
    }

    FString ActorName;
    if (!TryGetTrimmedStringParam(Params, TEXT("name"), ActorName))
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing 'name' parameter"));
    }

    FVector Location(0.0f, 0.0f, 0.0f);
    FRotator Rotation(0.0f, 0.0f, 0.0f);
    FVector Scale(1.0f, 1.0f, 1.0f);
    FString StaticMeshPath;

    if (Params->HasField(TEXT("location")))
    {
        Location = FUnrealAICommonUtils::GetVectorFromJson(Params, TEXT("location"));
    }
    if (Params->HasField(TEXT("rotation")))
    {
        Rotation = FUnrealAICommonUtils::GetRotatorFromJson(Params, TEXT("rotation"));
    }
    if (Params->HasField(TEXT("scale")))
    {
        Scale = FUnrealAICommonUtils::GetVectorFromJson(Params, TEXT("scale"));
    }
    Params->TryGetStringField(TEXT("static_mesh"), StaticMeshPath);
    StaticMeshPath = StaticMeshPath.TrimStartAndEnd();

    UWorld* World = GEditor->GetEditorWorldContext().World();
    FString ErrorMessage;
    AActor* NewActor = SpawnConfiguredActor(World, ActorType, ActorName, Location, Rotation, Scale, StaticMeshPath, ErrorMessage);
    if (!NewActor)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(ErrorMessage);
    }

    return FUnrealAICommonUtils::ActorToJsonObject(NewActor, true);
}

TSharedPtr<FJsonObject> FUnrealAIEditorCommands::HandleDeleteActor(const TSharedPtr<FJsonObject>& Params)
{
    FString ActorName;
    if (!Params->TryGetStringField(TEXT("name"), ActorName))
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing 'name' parameter"));
    }

    TArray<AActor*> AllActors;
    UGameplayStatics::GetAllActorsOfClass(GWorld, AActor::StaticClass(), AllActors);
    
    for (AActor* Actor : AllActors)
    {
        if (Actor && Actor->GetName() == ActorName)
        {
            // Store actor info before deletion for the response
            TSharedPtr<FJsonObject> ActorInfo = FUnrealAICommonUtils::ActorToJsonObject(Actor);
            
            // Delete the actor
            Actor->Destroy();
            
            TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
            ResultObj->SetObjectField(TEXT("deleted_actor"), ActorInfo);
            return ResultObj;
        }
    }
    
    return FUnrealAICommonUtils::CreateErrorResponse(FString::Printf(TEXT("Actor not found: %s"), *ActorName));
}

TSharedPtr<FJsonObject> FUnrealAIEditorCommands::HandleSetActorTransform(const TSharedPtr<FJsonObject>& Params)
{
    // Get actor name
    FString ActorName;
    if (!Params->TryGetStringField(TEXT("name"), ActorName))
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing 'name' parameter"));
    }

    // Find the actor
    AActor* TargetActor = nullptr;
    TArray<AActor*> AllActors;
    UGameplayStatics::GetAllActorsOfClass(GWorld, AActor::StaticClass(), AllActors);
    
    for (AActor* Actor : AllActors)
    {
        if (Actor && Actor->GetName() == ActorName)
        {
            TargetActor = Actor;
            break;
        }
    }

    if (!TargetActor)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(FString::Printf(TEXT("Actor not found: %s"), *ActorName));
    }

    // Get transform parameters
    FTransform NewTransform = TargetActor->GetTransform();

    if (Params->HasField(TEXT("location")))
    {
        NewTransform.SetLocation(FUnrealAICommonUtils::GetVectorFromJson(Params, TEXT("location")));
    }
    if (Params->HasField(TEXT("rotation")))
    {
        NewTransform.SetRotation(FQuat(FUnrealAICommonUtils::GetRotatorFromJson(Params, TEXT("rotation"))));
    }
    if (Params->HasField(TEXT("scale")))
    {
        NewTransform.SetScale3D(FUnrealAICommonUtils::GetVectorFromJson(Params, TEXT("scale")));
    }

    // Set the new transform
    TargetActor->SetActorTransform(NewTransform);

    // Return updated actor info
    return FUnrealAICommonUtils::ActorToJsonObject(TargetActor, true);
}

TSharedPtr<FJsonObject> FUnrealAIEditorCommands::HandleSpawnBlueprintActor(const TSharedPtr<FJsonObject>& Params)
{
    // This function will now correctly call the implementation in BlueprintCommands
    FUnrealAIBlueprintCommands BlueprintCommands;
    return BlueprintCommands.HandleCommand(TEXT("spawn_blueprint_actor"), Params);
}

TSharedPtr<FJsonObject> FUnrealAIEditorCommands::HandleSaveLevel(const TSharedPtr<FJsonObject>& Params)
{
    if (!UEditorLoadingAndSavingUtils::SaveDirtyPackages(true, false))
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Failed to save the current level"));
    }

    UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetBoolField(TEXT("success"), true);
    Result->SetStringField(TEXT("current_level"), World ? World->GetMapName() : TEXT(""));
    Result->SetStringField(TEXT("package_name"), World && World->GetOutermost() ? World->GetOutermost()->GetName() : TEXT(""));
    return Result;
}

TSharedPtr<FJsonObject> FUnrealAIEditorCommands::HandleUndoLastAction(const TSharedPtr<FJsonObject>& Params)
{
    if (!GEditor)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("GEditor unavailable"));
    }

    if (!GEditor->UndoTransaction())
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("No undo history available"));
    }

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetBoolField(TEXT("success"), true);
    Result->SetBoolField(TEXT("undid"), true);
    return Result;
}

TSharedPtr<FJsonObject> FUnrealAIEditorCommands::HandleRedoLastAction(const TSharedPtr<FJsonObject>& Params)
{
    if (!GEditor)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("GEditor unavailable"));
    }

    if (!GEditor->RedoTransaction())
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("No redo history available"));
    }

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetBoolField(TEXT("success"), true);
    Result->SetBoolField(TEXT("redid"), true);
    return Result;
}

TSharedPtr<FJsonObject> FUnrealAIEditorCommands::HandleCaptureViewportScreenshot(const TSharedPtr<FJsonObject>& Params)
{
    if (!GEditor)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("GEditor unavailable"));
    }

    FString ImageFormat = TEXT("png");
    Params->TryGetStringField(TEXT("image_format"), ImageFormat);
    ImageFormat = ImageFormat.TrimStartAndEnd().ToLower();
    if (ImageFormat.IsEmpty())
    {
        ImageFormat = TEXT("png");
    }

    if (ImageFormat != TEXT("png"))
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Only 'png' screenshots are currently supported"));
    }

    FViewport* Viewport = GEditor->GetActiveViewport();
    if (!Viewport)
    {
        for (FLevelEditorViewportClient* ViewportClient : GEditor->GetLevelViewportClients())
        {
            if (ViewportClient && ViewportClient->Viewport)
            {
                Viewport = ViewportClient->Viewport;
                break;
            }
        }
    }

    if (!Viewport)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("No active level viewport is available for screenshot capture"));
    }

    const FIntPoint Size = Viewport->GetSizeXY();
    if (Size.X <= 0 || Size.Y <= 0)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Active viewport has an invalid size"));
    }

    TArray<FColor> Bitmap;
    if (!GetViewportScreenShot(Viewport, Bitmap) || Bitmap.Num() == 0)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Failed to capture the active viewport screenshot"));
    }

    for (FColor& Pixel : Bitmap)
    {
        Pixel.A = 255;
    }

    FString FilePath;
    Params->TryGetStringField(TEXT("file_path"), FilePath);
    FilePath = FilePath.TrimStartAndEnd();
    if (FilePath.IsEmpty())
    {
        FilePath = FPaths::Combine(
            FPaths::ProjectSavedDir(),
            TEXT("UnrealAI"),
            TEXT("Screenshots"),
            FString::Printf(TEXT("UnrealAI_%s.png"), *FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S"))));
    }
    else if (FPaths::IsRelative(FilePath))
    {
        FilePath = FPaths::Combine(FPaths::ProjectDir(), FilePath);
    }

    if (!FilePath.EndsWith(TEXT(".png"), ESearchCase::IgnoreCase))
    {
        FilePath += TEXT(".png");
    }

    const FString AbsoluteFilePath = FPaths::ConvertRelativePathToFull(FilePath);
    IFileManager::Get().MakeDirectory(*FPaths::GetPath(AbsoluteFilePath), true);

    TArray64<uint8> CompressedBytes;
    FImageUtils::PNGCompressImageArray(
        Size.X,
        Size.Y,
        TArrayView64<const FColor>(Bitmap.GetData(), Bitmap.Num()),
        CompressedBytes);
    if (!FFileHelper::SaveArrayToFile(CompressedBytes, *AbsoluteFilePath))
    {
        return FUnrealAICommonUtils::CreateErrorResponse(FString::Printf(TEXT("Failed to save screenshot to %s"), *AbsoluteFilePath));
    }

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetBoolField(TEXT("success"), true);
    Result->SetStringField(TEXT("image_format"), TEXT("png"));
    Result->SetStringField(TEXT("file_path"), AbsoluteFilePath);
    Result->SetNumberField(TEXT("width"), Size.X);
    Result->SetNumberField(TEXT("height"), Size.Y);
    Result->SetNumberField(TEXT("byte_size"), static_cast<double>(IFileManager::Get().FileSize(*AbsoluteFilePath)));
    return Result;
}

TSharedPtr<FJsonObject> FUnrealAIEditorCommands::HandlePlaceInGrid(const TSharedPtr<FJsonObject>& Params)
{
    FString ActorType;
    FString NamePrefix;
    if (!TryGetTrimmedStringParam(Params, TEXT("actor_type"), ActorType))
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing 'actor_type' parameter"));
    }
    if (!TryGetTrimmedStringParam(Params, TEXT("name_prefix"), NamePrefix))
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing 'name_prefix' parameter"));
    }

    int32 CountX = 0;
    int32 CountY = 1;
    if (!TryGetPositiveIntegerField(Params, TEXT("count_x"), CountX))
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("'count_x' must be a positive integer"));
    }
    if (Params->HasField(TEXT("count_y")) && !TryGetPositiveIntegerField(Params, TEXT("count_y"), CountY))
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("'count_y' must be a positive integer when provided"));
    }

    double StepX = 0.0;
    double StepY = 200.0;
    if (!Params->TryGetNumberField(TEXT("step_x"), StepX))
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing 'step_x' parameter"));
    }
    Params->TryGetNumberField(TEXT("step_y"), StepY);

    const FVector Origin = FUnrealAICommonUtils::GetVectorFromJson(Params, TEXT("origin"));
    const FRotator Rotation = Params->HasField(TEXT("rotation")) ? FUnrealAICommonUtils::GetRotatorFromJson(Params, TEXT("rotation")) : FRotator::ZeroRotator;
    const FVector Scale = Params->HasField(TEXT("scale")) ? FUnrealAICommonUtils::GetVectorFromJson(Params, TEXT("scale")) : FVector(1.0f, 1.0f, 1.0f);
    FString StaticMeshPath;
    Params->TryGetStringField(TEXT("static_mesh"), StaticMeshPath);
    StaticMeshPath = StaticMeshPath.TrimStartAndEnd();

    UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
    if (!World)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Failed to get editor world"));
    }

    const int32 TotalCount = CountX * CountY;
    for (int32 Index = 0; Index < TotalCount; ++Index)
    {
        if (DoesActorNameExistInWorld(World, BuildIndexedActorName(NamePrefix, Index)))
        {
            return FUnrealAICommonUtils::CreateErrorResponse(FString::Printf(TEXT("Actor with name '%s' already exists"), *BuildIndexedActorName(NamePrefix, Index)));
        }
    }

    TArray<TSharedPtr<FJsonValue>> SpawnedActors;
    SpawnedActors.Reserve(TotalCount);
    for (int32 Y = 0; Y < CountY; ++Y)
    {
        for (int32 X = 0; X < CountX; ++X)
        {
            const int32 Index = Y * CountX + X;
            const FString ActorName = BuildIndexedActorName(NamePrefix, Index);
            const FVector Location = Origin + FVector(static_cast<float>(StepX * X), static_cast<float>(StepY * Y), 0.0f);
            FString ErrorMessage;
            AActor* NewActor = SpawnConfiguredActor(World, ActorType, ActorName, Location, Rotation, Scale, StaticMeshPath, ErrorMessage, true);
            if (!NewActor)
            {
                return FUnrealAICommonUtils::CreateErrorResponse(ErrorMessage);
            }
            SpawnedActors.Add(MakeShared<FJsonValueObject>(FUnrealAICommonUtils::ActorToJsonObject(NewActor, true)));
        }
    }

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetBoolField(TEXT("success"), true);
    Result->SetStringField(TEXT("actor_type"), ActorType);
    Result->SetStringField(TEXT("name_prefix"), NamePrefix);
    Result->SetNumberField(TEXT("spawned_count"), SpawnedActors.Num());
    Result->SetNumberField(TEXT("count_x"), CountX);
    Result->SetNumberField(TEXT("count_y"), CountY);
    Result->SetArrayField(TEXT("actors"), SpawnedActors);
    return Result;
}

TSharedPtr<FJsonObject> FUnrealAIEditorCommands::HandlePlaceInCircle(const TSharedPtr<FJsonObject>& Params)
{
    FString ActorType;
    FString NamePrefix;
    if (!TryGetTrimmedStringParam(Params, TEXT("actor_type"), ActorType))
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing 'actor_type' parameter"));
    }
    if (!TryGetTrimmedStringParam(Params, TEXT("name_prefix"), NamePrefix))
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing 'name_prefix' parameter"));
    }

    int32 Count = 0;
    if (!TryGetPositiveIntegerField(Params, TEXT("count"), Count))
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("'count' must be a positive integer"));
    }

    double Radius = 0.0;
    if (!Params->TryGetNumberField(TEXT("radius"), Radius) || Radius <= 0.0)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("'radius' must be > 0"));
    }

    double StartAngleDegrees = 0.0;
    Params->TryGetNumberField(TEXT("start_angle_degrees"), StartAngleDegrees);

    const FVector Center = FUnrealAICommonUtils::GetVectorFromJson(Params, TEXT("center"));
    const FRotator Rotation = Params->HasField(TEXT("rotation")) ? FUnrealAICommonUtils::GetRotatorFromJson(Params, TEXT("rotation")) : FRotator::ZeroRotator;
    const FVector Scale = Params->HasField(TEXT("scale")) ? FUnrealAICommonUtils::GetVectorFromJson(Params, TEXT("scale")) : FVector(1.0f, 1.0f, 1.0f);
    FString StaticMeshPath;
    Params->TryGetStringField(TEXT("static_mesh"), StaticMeshPath);
    StaticMeshPath = StaticMeshPath.TrimStartAndEnd();

    UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
    if (!World)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Failed to get editor world"));
    }

    for (int32 Index = 0; Index < Count; ++Index)
    {
        if (DoesActorNameExistInWorld(World, BuildIndexedActorName(NamePrefix, Index)))
        {
            return FUnrealAICommonUtils::CreateErrorResponse(FString::Printf(TEXT("Actor with name '%s' already exists"), *BuildIndexedActorName(NamePrefix, Index)));
        }
    }

    TArray<TSharedPtr<FJsonValue>> SpawnedActors;
    SpawnedActors.Reserve(Count);
    const double AngleStepRadians = Count > 0 ? (2.0 * PI / static_cast<double>(Count)) : 0.0;
    for (int32 Index = 0; Index < Count; ++Index)
    {
        const double AngleRadians = FMath::DegreesToRadians(StartAngleDegrees) + (AngleStepRadians * Index);
        const FVector Location = Center + FVector(
            static_cast<float>(FMath::Cos(AngleRadians) * Radius),
            static_cast<float>(FMath::Sin(AngleRadians) * Radius),
            0.0f);
        const FString ActorName = BuildIndexedActorName(NamePrefix, Index);
        FString ErrorMessage;
        AActor* NewActor = SpawnConfiguredActor(World, ActorType, ActorName, Location, Rotation, Scale, StaticMeshPath, ErrorMessage, true);
        if (!NewActor)
        {
            return FUnrealAICommonUtils::CreateErrorResponse(ErrorMessage);
        }
        SpawnedActors.Add(MakeShared<FJsonValueObject>(FUnrealAICommonUtils::ActorToJsonObject(NewActor, true)));
    }

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetBoolField(TEXT("success"), true);
    Result->SetStringField(TEXT("actor_type"), ActorType);
    Result->SetStringField(TEXT("name_prefix"), NamePrefix);
    Result->SetNumberField(TEXT("spawned_count"), SpawnedActors.Num());
    Result->SetNumberField(TEXT("radius"), Radius);
    Result->SetArrayField(TEXT("actors"), SpawnedActors);
    return Result;
}

TSharedPtr<FJsonObject> FUnrealAIEditorCommands::HandlePlaceAlongSpline(const TSharedPtr<FJsonObject>& Params)
{
    FString ActorType;
    FString NamePrefix;
    FString SplineActorName;
    if (!TryGetTrimmedStringParam(Params, TEXT("actor_type"), ActorType))
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing 'actor_type' parameter"));
    }
    if (!TryGetTrimmedStringParam(Params, TEXT("name_prefix"), NamePrefix))
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing 'name_prefix' parameter"));
    }
    if (!TryGetTrimmedStringParam(Params, TEXT("spline_actor_name"), SplineActorName))
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing 'spline_actor_name' parameter"));
    }

    int32 Count = 0;
    if (!TryGetPositiveIntegerField(Params, TEXT("count"), Count))
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("'count' must be a positive integer"));
    }

    FString OrientationMode = TEXT("yaw");
    Params->TryGetStringField(TEXT("orientation_mode"), OrientationMode);
    OrientationMode = OrientationMode.TrimStartAndEnd().ToLower();
    if (OrientationMode.IsEmpty())
    {
        OrientationMode = TEXT("yaw");
    }

    UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
    if (!World)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Failed to get editor world"));
    }

    AActor* SplineActor = FindActorByNameInWorld(World, SplineActorName);
    if (!SplineActor)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(FString::Printf(TEXT("Actor not found: %s"), *SplineActorName));
    }

    USplineComponent* SplineComponent = SplineActor->FindComponentByClass<USplineComponent>();
    if (!SplineComponent)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(FString::Printf(TEXT("Actor '%s' does not have a spline component"), *SplineActorName));
    }

    const FRotator BaseRotation = Params->HasField(TEXT("rotation")) ? FUnrealAICommonUtils::GetRotatorFromJson(Params, TEXT("rotation")) : FRotator::ZeroRotator;
    const FVector Scale = Params->HasField(TEXT("scale")) ? FUnrealAICommonUtils::GetVectorFromJson(Params, TEXT("scale")) : FVector(1.0f, 1.0f, 1.0f);
    FString StaticMeshPath;
    Params->TryGetStringField(TEXT("static_mesh"), StaticMeshPath);
    StaticMeshPath = StaticMeshPath.TrimStartAndEnd();

    for (int32 Index = 0; Index < Count; ++Index)
    {
        if (DoesActorNameExistInWorld(World, BuildIndexedActorName(NamePrefix, Index)))
        {
            return FUnrealAICommonUtils::CreateErrorResponse(FString::Printf(TEXT("Actor with name '%s' already exists"), *BuildIndexedActorName(NamePrefix, Index)));
        }
    }

    const float SplineLength = SplineComponent->GetSplineLength();
    TArray<TSharedPtr<FJsonValue>> SpawnedActors;
    SpawnedActors.Reserve(Count);
    for (int32 Index = 0; Index < Count; ++Index)
    {
        const float Distance = Count == 1 ? 0.0f : (SplineLength * static_cast<float>(Index) / static_cast<float>(Count - 1));
        const FVector Location = SplineComponent->GetLocationAtDistanceAlongSpline(Distance, ESplineCoordinateSpace::World);
        const FRotator Rotation = ComposeSplinePlacementRotation(OrientationMode, BaseRotation, SplineComponent, Distance);
        const FString ActorName = BuildIndexedActorName(NamePrefix, Index);
        FString ErrorMessage;
        AActor* NewActor = SpawnConfiguredActor(World, ActorType, ActorName, Location, Rotation, Scale, StaticMeshPath, ErrorMessage, true);
        if (!NewActor)
        {
            return FUnrealAICommonUtils::CreateErrorResponse(ErrorMessage);
        }
        SpawnedActors.Add(MakeShared<FJsonValueObject>(FUnrealAICommonUtils::ActorToJsonObject(NewActor, true)));
    }

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetBoolField(TEXT("success"), true);
    Result->SetStringField(TEXT("actor_type"), ActorType);
    Result->SetStringField(TEXT("name_prefix"), NamePrefix);
    Result->SetStringField(TEXT("spline_actor_name"), SplineActorName);
    Result->SetStringField(TEXT("orientation_mode"), OrientationMode);
    Result->SetNumberField(TEXT("spawned_count"), SpawnedActors.Num());
    Result->SetArrayField(TEXT("actors"), SpawnedActors);
    return Result;
}

TSharedPtr<FJsonObject> FUnrealAIEditorCommands::HandleScatterInArea(const TSharedPtr<FJsonObject>& Params)
{
    FString ActorType;
    FString NamePrefix;
    if (!TryGetTrimmedStringParam(Params, TEXT("actor_type"), ActorType))
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing 'actor_type' parameter"));
    }
    if (!TryGetTrimmedStringParam(Params, TEXT("name_prefix"), NamePrefix))
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing 'name_prefix' parameter"));
    }

    int32 Count = 0;
    if (!TryGetPositiveIntegerField(Params, TEXT("count"), Count))
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("'count' must be a positive integer"));
    }

    int32 Seed = 12345;
    TryGetNonNegativeSeedField(Params, TEXT("seed"), Seed);

    FString Shape = TEXT("box");
    Params->TryGetStringField(TEXT("shape"), Shape);
    Shape = Shape.TrimStartAndEnd().ToLower();
    if (Shape.IsEmpty())
    {
        Shape = TEXT("box");
    }

    const FVector MinCorner = FUnrealAICommonUtils::GetVectorFromJson(Params, TEXT("min_corner"));
    const FVector MaxCorner = FUnrealAICommonUtils::GetVectorFromJson(Params, TEXT("max_corner"));
    if (MinCorner.X > MaxCorner.X || MinCorner.Y > MaxCorner.Y || MinCorner.Z > MaxCorner.Z)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("'min_corner' must be less than or equal to 'max_corner' on every axis"));
    }

    const FRotator Rotation = Params->HasField(TEXT("rotation")) ? FUnrealAICommonUtils::GetRotatorFromJson(Params, TEXT("rotation")) : FRotator::ZeroRotator;
    const FVector Scale = Params->HasField(TEXT("scale")) ? FUnrealAICommonUtils::GetVectorFromJson(Params, TEXT("scale")) : FVector(1.0f, 1.0f, 1.0f);
    FString StaticMeshPath;
    Params->TryGetStringField(TEXT("static_mesh"), StaticMeshPath);
    StaticMeshPath = StaticMeshPath.TrimStartAndEnd();

    UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
    if (!World)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Failed to get editor world"));
    }

    for (int32 Index = 0; Index < Count; ++Index)
    {
        if (DoesActorNameExistInWorld(World, BuildIndexedActorName(NamePrefix, Index)))
        {
            return FUnrealAICommonUtils::CreateErrorResponse(FString::Printf(TEXT("Actor with name '%s' already exists"), *BuildIndexedActorName(NamePrefix, Index)));
        }
    }

    const FVector Center = (MinCorner + MaxCorner) * 0.5f;
    const FVector Extent = (MaxCorner - MinCorner) * 0.5f;
    FRandomStream RandomStream(Seed);
    TArray<TSharedPtr<FJsonValue>> SpawnedActors;
    SpawnedActors.Reserve(Count);
    for (int32 Index = 0; Index < Count; ++Index)
    {
        FVector Location = FVector::ZeroVector;
        if (Shape == TEXT("ellipse"))
        {
            const float Angle = RandomStream.FRandRange(0.0f, 2.0f * PI);
            const float Radius = FMath::Sqrt(RandomStream.FRand());
            Location.X = Center.X + (FMath::Cos(Angle) * Extent.X * Radius);
            Location.Y = Center.Y + (FMath::Sin(Angle) * Extent.Y * Radius);
            Location.Z = RandomStream.FRandRange(MinCorner.Z, MaxCorner.Z);
        }
        else
        {
            Location.X = RandomStream.FRandRange(MinCorner.X, MaxCorner.X);
            Location.Y = RandomStream.FRandRange(MinCorner.Y, MaxCorner.Y);
            Location.Z = RandomStream.FRandRange(MinCorner.Z, MaxCorner.Z);
        }

        const FString ActorName = BuildIndexedActorName(NamePrefix, Index);
        FString ErrorMessage;
        AActor* NewActor = SpawnConfiguredActor(World, ActorType, ActorName, Location, Rotation, Scale, StaticMeshPath, ErrorMessage, true);
        if (!NewActor)
        {
            return FUnrealAICommonUtils::CreateErrorResponse(ErrorMessage);
        }
        SpawnedActors.Add(MakeShared<FJsonValueObject>(FUnrealAICommonUtils::ActorToJsonObject(NewActor, true)));
    }

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetBoolField(TEXT("success"), true);
    Result->SetStringField(TEXT("actor_type"), ActorType);
    Result->SetStringField(TEXT("name_prefix"), NamePrefix);
    Result->SetStringField(TEXT("shape"), Shape);
    Result->SetNumberField(TEXT("seed"), Seed);
    Result->SetNumberField(TEXT("spawned_count"), SpawnedActors.Num());
    Result->SetArrayField(TEXT("actors"), SpawnedActors);
    return Result;
}
