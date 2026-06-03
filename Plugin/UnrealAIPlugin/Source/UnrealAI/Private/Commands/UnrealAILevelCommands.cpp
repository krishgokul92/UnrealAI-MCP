#include "Commands/UnrealAILevelCommands.h"
#include "Commands/UnrealAICommonUtils.h"

#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Kismet/GameplayStatics.h"
#include "Editor.h"
#include "EditorViewportClient.h"
#include "FileHelpers.h"
#include "LevelEditorViewport.h"
#include "Subsystems/EditorActorSubsystem.h"
#include "ScopedTransaction.h"
#include "Misc/PackageName.h"

namespace
{
    AActor* FindActorByName(const FString& Name)
    {
        if (!GWorld)
        {
            return nullptr;
        }
        TArray<AActor*> AllActors;
        UGameplayStatics::GetAllActorsOfClass(GWorld, AActor::StaticClass(), AllActors);
        for (AActor* Actor : AllActors)
        {
            if (!Actor) continue;
            if (Actor->GetName() == Name || Actor->GetActorLabel() == Name)
            {
                return Actor;
            }
        }
        return nullptr;
    }

    bool GetStringArray(const TSharedPtr<FJsonObject>& Params, const FString& Field, TArray<FString>& Out)
    {
        const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
        if (!Params->TryGetArrayField(Field, Arr) || !Arr)
        {
            return false;
        }
        for (const TSharedPtr<FJsonValue>& V : *Arr)
        {
            FString S;
            if (V.IsValid() && V->TryGetString(S))
            {
                Out.Add(S);
            }
        }
        return true;
    }

    float SnapValue(float Value, float Grid)
    {
        if (Grid <= 0.0f) return Value;
        return FMath::RoundToFloat(Value / Grid) * Grid;
    }

    bool TryGetTrimmedStringField(const TSharedPtr<FJsonObject>& Params, const TCHAR* FieldName, FString& OutValue)
    {
        if (!Params.IsValid() || !Params->TryGetStringField(FieldName, OutValue))
        {
            return false;
        }

        OutValue = OutValue.TrimStartAndEnd();
        return !OutValue.IsEmpty();
    }

    bool ResolveMapFilename(const FString& RequestedPath, FString& OutLongPackageName, FString& OutFilename)
    {
        FString Normalized = RequestedPath.TrimStartAndEnd();
        if (Normalized.IsEmpty())
        {
            return false;
        }

        if (Normalized.StartsWith(TEXT("/")))
        {
            FString PackageName = FPackageName::ObjectPathToPackageName(Normalized);
            if (PackageName.IsEmpty())
            {
                PackageName = Normalized;
            }

            if (FPackageName::DoesPackageExist(PackageName, &OutFilename))
            {
                OutLongPackageName = PackageName;
                return true;
            }

            if (FPackageName::SearchForPackageOnDisk(PackageName, &OutLongPackageName, &OutFilename))
            {
                return true;
            }

            if (FPackageName::TryConvertLongPackageNameToFilename(PackageName, OutFilename, FPackageName::GetMapPackageExtension())
                && FPaths::FileExists(OutFilename))
            {
                OutLongPackageName = PackageName;
                return true;
            }
        }
        else
        {
            if (FPackageName::SearchForPackageOnDisk(Normalized, &OutLongPackageName, &OutFilename))
            {
                return true;
            }

            if (FPaths::FileExists(Normalized))
            {
                OutFilename = FPaths::ConvertRelativePathToFull(Normalized);
                FPackageName::TryConvertFilenameToLongPackageName(OutFilename, OutLongPackageName);
                return true;
            }
        }

        return false;
    }
}

FUnrealAILevelCommands::FUnrealAILevelCommands()
{
}

TSharedPtr<FJsonObject> FUnrealAILevelCommands::HandleCommand(const FString& CommandType, const TSharedPtr<FJsonObject>& Params)
{
    if (CommandType == TEXT("new_blank_map"))      return HandleNewBlankMap(Params);
    if (CommandType == TEXT("open_level"))         return HandleOpenLevel(Params);
    if (CommandType == TEXT("snap_actors_to_grid")) return HandleSnapActorsToGrid(Params);
    if (CommandType == TEXT("align_actors"))        return HandleAlignActors(Params);
    if (CommandType == TEXT("duplicate_actor"))     return HandleDuplicateActor(Params);
    if (CommandType == TEXT("focus_viewport"))      return HandleFocusViewport(Params);
    return FUnrealAICommonUtils::CreateErrorResponse(
        FString::Printf(TEXT("Unknown level command: %s"), *CommandType));
}

// --------------------------------------------------------------------------- //
// new_blank_map
// --------------------------------------------------------------------------- //
TSharedPtr<FJsonObject> FUnrealAILevelCommands::HandleNewBlankMap(const TSharedPtr<FJsonObject>& Params)
{
    bool bSaveExistingMap = false;
    if (Params.IsValid())
    {
        Params->TryGetBoolField(TEXT("save_existing_map"), bSaveExistingMap);
    }

    UWorld* NewWorld = UEditorLoadingAndSavingUtils::NewBlankMap(bSaveExistingMap);
    if (!NewWorld)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Failed to create a new blank map"));
    }

    TSharedPtr<FJsonObject> Out = MakeShared<FJsonObject>();
    Out->SetBoolField(TEXT("success"), true);
    Out->SetBoolField(TEXT("save_existing_map"), bSaveExistingMap);
    Out->SetStringField(TEXT("current_level"), NewWorld->GetMapName());

    if (UPackage* WorldPackage = NewWorld->GetOutermost())
    {
        Out->SetStringField(TEXT("package_name"), WorldPackage->GetName());
    }

    return Out;
}

// --------------------------------------------------------------------------- //
// open_level
// --------------------------------------------------------------------------- //
TSharedPtr<FJsonObject> FUnrealAILevelCommands::HandleOpenLevel(const TSharedPtr<FJsonObject>& Params)
{
    FString AssetPath;
    if (!TryGetTrimmedStringField(Params, TEXT("asset_path"), AssetPath))
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing 'asset_path' parameter"));
    }

    bool bSaveCurrentLevel = false;
    if (Params.IsValid())
    {
        Params->TryGetBoolField(TEXT("save_current_level"), bSaveCurrentLevel);
    }

    FString LongPackageName;
    FString Filename;
    if (!ResolveMapFilename(AssetPath, LongPackageName, Filename))
    {
        return FUnrealAICommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Could not resolve level asset path: %s"), *AssetPath));
    }

    if (bSaveCurrentLevel && !UEditorLoadingAndSavingUtils::SaveDirtyPackages(true, false))
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Failed to save the current level before opening the requested map"));
    }

    UWorld* LoadedWorld = UEditorLoadingAndSavingUtils::LoadMap(Filename);
    if (!LoadedWorld)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Failed to load level from %s"), *Filename));
    }

    TSharedPtr<FJsonObject> Out = MakeShared<FJsonObject>();
    Out->SetBoolField(TEXT("success"), true);
    Out->SetBoolField(TEXT("save_current_level"), bSaveCurrentLevel);
    Out->SetStringField(TEXT("requested_asset_path"), AssetPath);
    Out->SetStringField(TEXT("resolved_package_name"), LongPackageName);
    Out->SetStringField(TEXT("filename"), Filename);
    Out->SetStringField(TEXT("current_level"), LoadedWorld->GetMapName());

    if (UPackage* WorldPackage = LoadedWorld->GetOutermost())
    {
        Out->SetStringField(TEXT("package_name"), WorldPackage->GetName());
    }

    return Out;
}

// --------------------------------------------------------------------------- //
// snap_actors_to_grid
// --------------------------------------------------------------------------- //
TSharedPtr<FJsonObject> FUnrealAILevelCommands::HandleSnapActorsToGrid(const TSharedPtr<FJsonObject>& Params)
{
    TArray<FString> Names;
    if (!GetStringArray(Params, TEXT("actor_names"), Names) || Names.Num() == 0)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing or empty 'actor_names' array"));
    }

    double GridSize = 100.0;
    Params->TryGetNumberField(TEXT("grid_size"), GridSize);
    if (GridSize <= 0.0)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("'grid_size' must be > 0"));
    }

    bool bSnapRotation = false;
    Params->TryGetBoolField(TEXT("snap_rotation"), bSnapRotation);

    double RotationGrid = 15.0;
    Params->TryGetNumberField(TEXT("rotation_grid"), RotationGrid);

    FScopedTransaction Transaction(NSLOCTEXT("UnrealAI", "SnapActorsToGrid", "Snap Actors To Grid"));

    TArray<TSharedPtr<FJsonValue>> Results;
    int32 SnappedCount = 0;
    int32 MissingCount = 0;

    for (const FString& Name : Names)
    {
        AActor* Actor = FindActorByName(Name);
        TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
        Entry->SetStringField(TEXT("name"), Name);
        if (!Actor)
        {
            Entry->SetBoolField(TEXT("snapped"), false);
            Entry->SetStringField(TEXT("error"), TEXT("Actor not found"));
            Results.Add(MakeShared<FJsonValueObject>(Entry));
            ++MissingCount;
            continue;
        }
        Actor->Modify();
        FVector Loc = Actor->GetActorLocation();
        FVector NewLoc(SnapValue(Loc.X, GridSize), SnapValue(Loc.Y, GridSize), SnapValue(Loc.Z, GridSize));
        Actor->SetActorLocation(NewLoc);

        if (bSnapRotation)
        {
            FRotator Rot = Actor->GetActorRotation();
            FRotator NewRot(SnapValue(Rot.Pitch, RotationGrid), SnapValue(Rot.Yaw, RotationGrid), SnapValue(Rot.Roll, RotationGrid));
            Actor->SetActorRotation(NewRot);
        }

        Entry->SetBoolField(TEXT("snapped"), true);
        TArray<TSharedPtr<FJsonValue>> LocArr;
        LocArr.Add(MakeShared<FJsonValueNumber>(NewLoc.X));
        LocArr.Add(MakeShared<FJsonValueNumber>(NewLoc.Y));
        LocArr.Add(MakeShared<FJsonValueNumber>(NewLoc.Z));
        Entry->SetArrayField(TEXT("new_location"), LocArr);
        Results.Add(MakeShared<FJsonValueObject>(Entry));
        ++SnappedCount;
    }

    TSharedPtr<FJsonObject> Out = MakeShared<FJsonObject>();
    Out->SetArrayField(TEXT("results"), Results);
    Out->SetNumberField(TEXT("snapped_count"), SnappedCount);
    Out->SetNumberField(TEXT("missing_count"), MissingCount);
    Out->SetNumberField(TEXT("grid_size"), GridSize);
    Out->SetBoolField(TEXT("success"), SnappedCount > 0);
    return Out;
}

// --------------------------------------------------------------------------- //
// align_actors
// --------------------------------------------------------------------------- //
TSharedPtr<FJsonObject> FUnrealAILevelCommands::HandleAlignActors(const TSharedPtr<FJsonObject>& Params)
{
    TArray<FString> Names;
    if (!GetStringArray(Params, TEXT("actor_names"), Names) || Names.Num() < 2)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("'actor_names' must contain at least 2 entries"));
    }

    FString Axis = TEXT("z");
    Params->TryGetStringField(TEXT("axis"), Axis);
    Axis = Axis.ToLower();
    int32 AxisIdx = -1;
    if      (Axis == TEXT("x")) AxisIdx = 0;
    else if (Axis == TEXT("y")) AxisIdx = 1;
    else if (Axis == TEXT("z")) AxisIdx = 2;
    else return FUnrealAICommonUtils::CreateErrorResponse(TEXT("'axis' must be one of x|y|z"));

    FString Mode = TEXT("min");
    Params->TryGetStringField(TEXT("mode"), Mode);
    Mode = Mode.ToLower();
    if (Mode != TEXT("min") && Mode != TEXT("max") && Mode != TEXT("center") && Mode != TEXT("average"))
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("'mode' must be one of min|max|center|average"));
    }

    // Resolve actors.
    TArray<AActor*> Resolved;
    TArray<FString> Missing;
    for (const FString& N : Names)
    {
        if (AActor* A = FindActorByName(N)) Resolved.Add(A);
        else Missing.Add(N);
    }
    if (Resolved.Num() < 2)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Need at least 2 resolvable actors to align"));
    }

    // Compute target value along axis.
    double MinV = TNumericLimits<double>::Max();
    double MaxV = -TNumericLimits<double>::Max();
    double Sum  = 0.0;
    for (AActor* A : Resolved)
    {
        double V = A->GetActorLocation().Component(AxisIdx);
        MinV = FMath::Min(MinV, V);
        MaxV = FMath::Max(MaxV, V);
        Sum += V;
    }
    double Target = 0.0;
    if      (Mode == TEXT("min"))     Target = MinV;
    else if (Mode == TEXT("max"))     Target = MaxV;
    else if (Mode == TEXT("center"))  Target = (MinV + MaxV) * 0.5;
    else /* average */                Target = Sum / Resolved.Num();

    FScopedTransaction Transaction(NSLOCTEXT("UnrealAI", "AlignActors", "Align Actors"));
    TArray<TSharedPtr<FJsonValue>> Results;
    for (AActor* A : Resolved)
    {
        A->Modify();
        FVector Loc = A->GetActorLocation();
        Loc.Component(AxisIdx) = Target;
        A->SetActorLocation(Loc);
        TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
        Entry->SetStringField(TEXT("name"), A->GetActorLabel());
        Entry->SetNumberField(TEXT("new_value"), Target);
        Results.Add(MakeShared<FJsonValueObject>(Entry));
    }

    TSharedPtr<FJsonObject> Out = MakeShared<FJsonObject>();
    Out->SetArrayField(TEXT("results"), Results);
    Out->SetNumberField(TEXT("aligned_count"), Resolved.Num());
    Out->SetNumberField(TEXT("missing_count"), Missing.Num());
    Out->SetStringField(TEXT("axis"), Axis);
    Out->SetStringField(TEXT("mode"), Mode);
    Out->SetNumberField(TEXT("target_value"), Target);
    Out->SetBoolField(TEXT("success"), true);
    return Out;
}

// --------------------------------------------------------------------------- //
// duplicate_actor
// --------------------------------------------------------------------------- //
TSharedPtr<FJsonObject> FUnrealAILevelCommands::HandleDuplicateActor(const TSharedPtr<FJsonObject>& Params)
{
    FString ActorName;
    if (!Params->TryGetStringField(TEXT("actor_name"), ActorName))
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing 'actor_name'"));
    }

    AActor* Source = FindActorByName(ActorName);
    if (!Source)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(FString::Printf(TEXT("Actor not found: %s"), *ActorName));
    }

    FVector Offset = FVector::ZeroVector;
    if (Params->HasField(TEXT("offset_location")))
    {
        Offset = FUnrealAICommonUtils::GetVectorFromJson(Params, TEXT("offset_location"));
    }

    FString NewName;
    Params->TryGetStringField(TEXT("new_name"), NewName);

    UEditorActorSubsystem* EditorActorSubsystem = GEditor ? GEditor->GetEditorSubsystem<UEditorActorSubsystem>() : nullptr;
    if (!EditorActorSubsystem)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("EditorActorSubsystem unavailable"));
    }

    FScopedTransaction Transaction(NSLOCTEXT("UnrealAI", "DuplicateActor", "Duplicate Actor"));
    AActor* Dup = EditorActorSubsystem->DuplicateActor(Source, GWorld, Offset);
    if (!Dup)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("DuplicateActor returned null"));
    }
    if (!NewName.IsEmpty())
    {
        Dup->SetActorLabel(NewName);
    }

    TSharedPtr<FJsonObject> Out = MakeShared<FJsonObject>();
    Out->SetStringField(TEXT("source_name"), Source->GetActorLabel());
    Out->SetStringField(TEXT("new_name"), Dup->GetActorLabel());
    Out->SetStringField(TEXT("internal_name"), Dup->GetName());
    TArray<TSharedPtr<FJsonValue>> LocArr;
    FVector NewLoc = Dup->GetActorLocation();
    LocArr.Add(MakeShared<FJsonValueNumber>(NewLoc.X));
    LocArr.Add(MakeShared<FJsonValueNumber>(NewLoc.Y));
    LocArr.Add(MakeShared<FJsonValueNumber>(NewLoc.Z));
    Out->SetArrayField(TEXT("location"), LocArr);
    Out->SetBoolField(TEXT("success"), true);
    return Out;
}

// --------------------------------------------------------------------------- //
// focus_viewport
// --------------------------------------------------------------------------- //
TSharedPtr<FJsonObject> FUnrealAILevelCommands::HandleFocusViewport(const TSharedPtr<FJsonObject>& Params)
{
    if (!GEditor)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("GEditor unavailable"));
    }

    FString ActorName;
    Params->TryGetStringField(TEXT("actor_name"), ActorName);

    if (!ActorName.IsEmpty())
    {
        AActor* Actor = FindActorByName(ActorName);
        if (!Actor)
        {
            return FUnrealAICommonUtils::CreateErrorResponse(FString::Printf(TEXT("Actor not found: %s"), *ActorName));
        }
        TArray<AActor*> Targets = { Actor };
        GEditor->MoveViewportCamerasToActor(Targets, /*bActiveViewportOnly=*/false);

        TSharedPtr<FJsonObject> Out = MakeShared<FJsonObject>();
        Out->SetStringField(TEXT("focused_on"), Actor->GetActorLabel());
        Out->SetBoolField(TEXT("success"), true);
        return Out;
    }

    if (Params->HasField(TEXT("location")))
    {
        FVector Loc = FUnrealAICommonUtils::GetVectorFromJson(Params, TEXT("location"));
        // Move all level viewports to the location.
        for (FLevelEditorViewportClient* VC : GEditor->GetLevelViewportClients())
        {
            if (VC)
            {
                VC->SetViewLocation(Loc);
                VC->Invalidate();
            }
        }
        TSharedPtr<FJsonObject> Out = MakeShared<FJsonObject>();
        TArray<TSharedPtr<FJsonValue>> LocArr;
        LocArr.Add(MakeShared<FJsonValueNumber>(Loc.X));
        LocArr.Add(MakeShared<FJsonValueNumber>(Loc.Y));
        LocArr.Add(MakeShared<FJsonValueNumber>(Loc.Z));
        Out->SetArrayField(TEXT("focused_on_location"), LocArr);
        Out->SetBoolField(TEXT("success"), true);
        return Out;
    }

    return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Provide either 'actor_name' or 'location'"));
}
