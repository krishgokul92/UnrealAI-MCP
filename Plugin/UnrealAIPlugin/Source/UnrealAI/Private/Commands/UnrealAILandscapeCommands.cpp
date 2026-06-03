#include "Commands/UnrealAILandscapeCommands.h"
#include "Commands/UnrealAICommonUtils.h"

#include "Landscape.h"
#include "LandscapeComponent.h"
#include "LandscapeInfo.h"
#include "LandscapeEdit.h"
#include "LandscapeProxy.h"
#include "LandscapeDataAccess.h"
#include "LandscapeEditorUtils.h"
#include "LandscapeImportHelper.h"

#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInterface.h"
#include "Misc/Paths.h"
#include "ScopedTransaction.h"

namespace
{
    void AddStringArray(TSharedPtr<FJsonObject> JsonObject, const FString& FieldName, const TArray<FString>& Values)
    {
        TArray<TSharedPtr<FJsonValue>> ArrayValues;
        for (const FString& Value : Values)
        {
            ArrayValues.Add(MakeShared<FJsonValueString>(Value));
        }
        JsonObject->SetArrayField(FieldName, ArrayValues);
    }

    void AddNumberArray(TSharedPtr<FJsonObject> JsonObject, const FString& FieldName, const TArray<double>& Values)
    {
        TArray<TSharedPtr<FJsonValue>> ArrayValues;
        for (double Value : Values)
        {
            ArrayValues.Add(MakeShared<FJsonValueNumber>(Value));
        }
        JsonObject->SetArrayField(FieldName, ArrayValues);
    }

    ALandscape* FindLandscapeByName(const FString& Name)
    {
        if (!GWorld)
        {
            return nullptr;
        }

        TArray<AActor*> LandscapeActors;
        UGameplayStatics::GetAllActorsOfClass(GWorld, ALandscape::StaticClass(), LandscapeActors);
        for (AActor* Actor : LandscapeActors)
        {
            ALandscape* Landscape = Cast<ALandscape>(Actor);
            if (!Landscape)
            {
                continue;
            }

            if (Landscape->GetName() == Name || Landscape->GetActorLabel() == Name)
            {
                return Landscape;
            }
        }

        return nullptr;
    }

    bool GetLandscapeExtent(ALandscape* Landscape, int32& OutMinX, int32& OutMinY, int32& OutMaxX, int32& OutMaxY)
    {
        if (!Landscape)
        {
            return false;
        }

        ULandscapeInfo* LandscapeInfo = Landscape->GetLandscapeInfo();
        if (!LandscapeInfo)
        {
            return false;
        }

        return LandscapeInfo->GetLandscapeExtent(OutMinX, OutMinY, OutMaxX, OutMaxY);
    }

    TSharedPtr<FJsonObject> LandscapeToJson(ALandscape* Landscape)
    {
        TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
        Result->SetStringField(TEXT("name"), Landscape->GetActorLabel());
        Result->SetStringField(TEXT("label"), Landscape->GetActorLabel());
        Result->SetStringField(TEXT("internal_name"), Landscape->GetName());

        const FVector Location = Landscape->GetActorLocation();
        AddNumberArray(Result, TEXT("location"), {Location.X, Location.Y, Location.Z});

        const FVector Scale = Landscape->GetActorScale3D();
        AddNumberArray(Result, TEXT("scale"), {Scale.X, Scale.Y, Scale.Z});

        int32 MinX = 0;
        int32 MinY = 0;
        int32 MaxX = 0;
        int32 MaxY = 0;
        if (GetLandscapeExtent(Landscape, MinX, MinY, MaxX, MaxY))
        {
            Result->SetNumberField(TEXT("width"), MaxX - MinX + 1);
            Result->SetNumberField(TEXT("height"), MaxY - MinY + 1);
            AddNumberArray(Result, TEXT("extent"), {static_cast<double>(MinX), static_cast<double>(MinY), static_cast<double>(MaxX), static_cast<double>(MaxY)});
        }

        return Result;
    }

    TSharedPtr<FJsonObject> LayerInfoToJson(ULandscapeLayerInfoObject* LayerInfo)
    {
        TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
        if (!LayerInfo)
        {
            return Result;
        }

        Result->SetStringField(
            TEXT("name"),
            LayerInfo == ALandscapeProxy::VisibilityLayer ? TEXT("Visibility") : LayerInfo->GetLayerName().ToString());
        Result->SetStringField(TEXT("object_name"), LayerInfo->GetName());
        Result->SetStringField(TEXT("path"), LayerInfo->GetPathName());
        Result->SetStringField(TEXT("blend_method"), StaticEnum<ELandscapeTargetLayerBlendMethod>()->GetNameStringByValue(static_cast<int64>(LayerInfo->GetBlendMethod())));
        Result->SetStringField(TEXT("blend_group"), LayerInfo->GetBlendGroup().ToString());
        Result->SetNumberField(TEXT("hardness"), LayerInfo->GetHardness());

        if (UPhysicalMaterial* PhysicalMaterial = LayerInfo->GetPhysicalMaterial())
        {
            Result->SetStringField(TEXT("physical_material"), PhysicalMaterial->GetPathName());
        }

        const FLinearColor DebugColor = LayerInfo->GetLayerUsageDebugColor();
        AddNumberArray(Result, TEXT("debug_color"), {DebugColor.R, DebugColor.G, DebugColor.B, DebugColor.A});
        return Result;
    }

    TSharedPtr<FJsonObject> LandscapeEditLayerToJson(ALandscape* Landscape, const FLandscapeLayer& Layer, int32 LayerIndex)
    {
        TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
        Result->SetNumberField(TEXT("index"), LayerIndex);

        if (Layer.Guid_DEPRECATED.IsValid())
        {
            Result->SetStringField(TEXT("guid"), Layer.Guid_DEPRECATED.ToString(EGuidFormats::DigitsWithHyphens));
        }

        if (!Layer.Name_DEPRECATED.IsNone())
        {
            Result->SetStringField(TEXT("name"), Layer.Name_DEPRECATED.ToString());
        }

        Result->SetBoolField(TEXT("visible"), Layer.bVisible_DEPRECATED);
        Result->SetBoolField(TEXT("locked"), Layer.bLocked_DEPRECATED);
        Result->SetNumberField(TEXT("heightmap_alpha"), Layer.HeightmapAlpha_DEPRECATED);
        Result->SetNumberField(TEXT("weightmap_alpha"), Layer.WeightmapAlpha_DEPRECATED);
        Result->SetNumberField(TEXT("brush_count"), Layer.Brushes.Num());
        Result->SetBoolField(TEXT("is_selected"), Landscape->GetSelectedEditLayerIndex() == LayerIndex);

        Result->SetBoolField(TEXT("has_edit_layer_object"), Layer.EditLayer != nullptr);

        TArray<ULandscapeLayerInfoObject*> UsedPaintLayers;
        Landscape->GetUsedPaintLayers(LayerIndex, UsedPaintLayers);
        TArray<TSharedPtr<FJsonValue>> UsedPaintLayerValues;
        for (ULandscapeLayerInfoObject* UsedPaintLayer : UsedPaintLayers)
        {
            UsedPaintLayerValues.Add(MakeShared<FJsonValueObject>(LayerInfoToJson(UsedPaintLayer)));
        }
        Result->SetArrayField(TEXT("used_paint_layers"), UsedPaintLayerValues);
        Result->SetNumberField(TEXT("used_paint_layer_count"), UsedPaintLayers.Num());

        return Result;
    }

    TSharedPtr<FJsonObject> LayerWeightToJson(ULandscapeLayerInfoObject* LayerInfo, float Weight)
    {
        TSharedPtr<FJsonObject> Result = LayerInfoToJson(LayerInfo);
        Result->SetNumberField(TEXT("weight"), Weight);
        return Result;
    }

    TArray<ULandscapeLayerInfoObject*> CollectLandscapeLayerInfos(ALandscape* Landscape)
    {
        TArray<ULandscapeLayerInfoObject*> UniqueLayerInfos;
        if (!Landscape)
        {
            return UniqueLayerInfos;
        }

        const TArrayView<const FLandscapeLayer> LandscapeLayers = Landscape->GetLayersConst();
        TSet<FString> SeenLayerInfoPaths;
        for (int32 LayerIndex = 0; LayerIndex < LandscapeLayers.Num(); ++LayerIndex)
        {
            TArray<ULandscapeLayerInfoObject*> UsedPaintLayers;
            Landscape->GetUsedPaintLayers(LayerIndex, UsedPaintLayers);
            for (ULandscapeLayerInfoObject* LayerInfo : UsedPaintLayers)
            {
                if (!LayerInfo)
                {
                    continue;
                }

                const FString LayerInfoPath = LayerInfo->GetPathName();
                if (SeenLayerInfoPaths.Contains(LayerInfoPath))
                {
                    continue;
                }

                SeenLayerInfoPaths.Add(LayerInfoPath);
                UniqueLayerInfos.Add(LayerInfo);
            }
        }

        return UniqueLayerInfos;
    }

    ULandscapeComponent* FindLandscapeComponentAtLocation(ALandscape* Landscape, const FVector& SampleLocation)
    {
        if (!Landscape)
        {
            return nullptr;
        }

        for (ULandscapeComponent* LandscapeComponent : Landscape->LandscapeComponents)
        {
            if (!LandscapeComponent)
            {
                continue;
            }

            const FBox ComponentBounds = LandscapeComponent->Bounds.GetBox();
            if (SampleLocation.X >= ComponentBounds.Min.X &&
                SampleLocation.X <= ComponentBounds.Max.X &&
                SampleLocation.Y >= ComponentBounds.Min.Y &&
                SampleLocation.Y <= ComponentBounds.Max.Y)
            {
                return LandscapeComponent;
            }
        }

        return nullptr;
    }

    TOptional<float> SampleLandscapeHeightFromComponent(ULandscapeComponent* LandscapeComponent, const FVector& SampleLocation)
    {
        if (!LandscapeComponent)
        {
            return TOptional<float>();
        }

        FLandscapeComponentDataInterface ComponentData(LandscapeComponent, 0, true);
        const FVector ComponentSpaceLocation = LandscapeComponent->GetComponentTransform().InverseTransformPosition(SampleLocation);
        const FIntRect ComponentExtent = LandscapeComponent->GetComponentExtent();
        const int32 ComponentSizeQuads = ComponentExtent.Max.X - ComponentExtent.Min.X;

        const float ClampedLocalX = FMath::Clamp(ComponentSpaceLocation.X, 0.0f, static_cast<float>(ComponentSizeQuads));
        const float ClampedLocalY = FMath::Clamp(ComponentSpaceLocation.Y, 0.0f, static_cast<float>(ComponentSizeQuads));

        const int32 X0 = FMath::FloorToInt(ClampedLocalX);
        const int32 Y0 = FMath::FloorToInt(ClampedLocalY);
        const int32 X1 = FMath::Min(X0 + 1, ComponentSizeQuads);
        const int32 Y1 = FMath::Min(Y0 + 1, ComponentSizeQuads);
        const float FracX = ClampedLocalX - static_cast<float>(X0);
        const float FracY = ClampedLocalY - static_cast<float>(Y0);

        const float Height00 = ComponentData.GetLocalHeight(X0, Y0);
        const float Height10 = ComponentData.GetLocalHeight(X1, Y0);
        const float Height01 = ComponentData.GetLocalHeight(X0, Y1);
        const float Height11 = ComponentData.GetLocalHeight(X1, Y1);

        const float HeightX0 = FMath::Lerp(Height00, Height10, FracX);
        const float HeightX1 = FMath::Lerp(Height01, Height11, FracX);
        const float LocalHeight = FMath::Lerp(HeightX0, HeightX1, FracY);

        return static_cast<float>(LandscapeComponent->GetComponentTransform().TransformPosition(FVector(ClampedLocalX, ClampedLocalY, LocalHeight)).Z);
    }

    bool TryGetVectorFromJsonValue(const TSharedPtr<FJsonValue>& JsonValue, FVector& OutVector)
    {
        if (!JsonValue.IsValid() || JsonValue->Type != EJson::Array)
        {
            return false;
        }

        const TArray<TSharedPtr<FJsonValue>>& Values = JsonValue->AsArray();
        if (Values.Num() != 3)
        {
            return false;
        }

        double X = 0.0;
        double Y = 0.0;
        double Z = 0.0;
        if (!Values[0].IsValid() || !Values[0]->TryGetNumber(X) ||
            !Values[1].IsValid() || !Values[1]->TryGetNumber(Y) ||
            !Values[2].IsValid() || !Values[2]->TryGetNumber(Z))
        {
            return false;
        }

        OutVector = FVector(X, Y, Z);
        return true;
    }

    void AddLandscapeTargetLayerSummary(TSharedPtr<FJsonObject> JsonObject, const TArray<ULandscapeLayerInfoObject*>& LandscapeLayerInfos)
    {
        TArray<FString> TargetLayerStrings;
        for (ULandscapeLayerInfoObject* LayerInfo : LandscapeLayerInfos)
        {
            TargetLayerStrings.Add(LayerInfo->GetLayerName().ToString());
        }

        AddStringArray(JsonObject, TEXT("target_layer_names"), TargetLayerStrings);
        JsonObject->SetNumberField(TEXT("target_layer_count"), TargetLayerStrings.Num());
    }

    TSharedPtr<FJsonObject> BuildLandscapePointSample(ALandscape* Landscape, const TArray<ULandscapeLayerInfoObject*>& LandscapeLayerInfos, const FVector& SampleLocation, FString& OutError)
    {
        ULandscapeComponent* LandscapeComponent = FindLandscapeComponentAtLocation(Landscape, SampleLocation);
        if (!LandscapeComponent)
        {
            OutError = TEXT("Unable to resolve landscape component at the sample location");
            return nullptr;
        }

        const TOptional<float> HeightAtLocation = SampleLandscapeHeightFromComponent(LandscapeComponent, SampleLocation);
        if (!HeightAtLocation.IsSet())
        {
            OutError = TEXT("Unable to sample landscape height at the requested location");
            return nullptr;
        }

        TArray<TSharedPtr<FJsonValue>> LayerWeightValues;
        for (ULandscapeLayerInfoObject* LayerInfo : LandscapeLayerInfos)
        {
            LayerWeightValues.Add(MakeShared<FJsonValueObject>(LayerWeightToJson(
                LayerInfo,
                LandscapeComponent->GetLayerWeightAtLocation(SampleLocation, LayerInfo, nullptr, true))));
        }

        TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
        AddNumberArray(Result, TEXT("location"), {SampleLocation.X, SampleLocation.Y, SampleLocation.Z});
        Result->SetNumberField(TEXT("height_world"), HeightAtLocation.GetValue());
        Result->SetStringField(TEXT("component_name"), LandscapeComponent->GetName());

        const FIntRect ComponentExtent = LandscapeComponent->GetComponentExtent();
        AddNumberArray(Result, TEXT("component_extent"), {
            static_cast<double>(ComponentExtent.Min.X),
            static_cast<double>(ComponentExtent.Min.Y),
            static_cast<double>(ComponentExtent.Max.X),
            static_cast<double>(ComponentExtent.Max.Y),
        });

        Result->SetArrayField(TEXT("layer_weights"), LayerWeightValues);
        Result->SetNumberField(TEXT("layer_weight_count"), LayerWeightValues.Num());
        return Result;
    }

    ULandscapeLayerInfoObject* ResolveLandscapeLayerInfo(ALandscape* Landscape, const FString& LayerIdentifier)
    {
        if (!Landscape || LayerIdentifier.IsEmpty())
        {
            return nullptr;
        }

        if (ALandscapeProxy::VisibilityLayer)
        {
            const ULandscapeLayerInfoObject* VisibilityLayer = ALandscapeProxy::VisibilityLayer;
            if (LayerIdentifier.Equals(TEXT("Visibility"), ESearchCase::IgnoreCase) ||
                LayerIdentifier.Equals(VisibilityLayer->GetLayerName().ToString(), ESearchCase::IgnoreCase) ||
                LayerIdentifier.Equals(VisibilityLayer->GetName(), ESearchCase::IgnoreCase) ||
                LayerIdentifier.Equals(VisibilityLayer->GetPathName(), ESearchCase::IgnoreCase))
            {
                return ALandscapeProxy::VisibilityLayer;
            }
        }

        const TArray<ULandscapeLayerInfoObject*> LandscapeLayerInfos = CollectLandscapeLayerInfos(Landscape);
        for (ULandscapeLayerInfoObject* LayerInfo : LandscapeLayerInfos)
        {
            if (!LayerInfo)
            {
                continue;
            }

            if (LayerIdentifier.Equals(LayerInfo->GetLayerName().ToString(), ESearchCase::IgnoreCase) ||
                LayerIdentifier.Equals(LayerInfo->GetName(), ESearchCase::IgnoreCase) ||
                LayerIdentifier.Equals(LayerInfo->GetPathName(), ESearchCase::IgnoreCase))
            {
                return LayerInfo;
            }
        }

        return nullptr;
    }

    bool TryResolveLandscapeRegionExtents(ALandscape* Landscape, const FVector& MinCorner, const FVector& MaxCorner, int32& OutMinX, int32& OutMinY, int32& OutMaxX, int32& OutMaxY, FString& OutError)
    {
        if (!Landscape)
        {
            OutError = TEXT("Landscape is required");
            return false;
        }

        int32 LandscapeMinX = 0;
        int32 LandscapeMinY = 0;
        int32 LandscapeMaxX = 0;
        int32 LandscapeMaxY = 0;
        if (!GetLandscapeExtent(Landscape, LandscapeMinX, LandscapeMinY, LandscapeMaxX, LandscapeMaxY))
        {
            OutError = TEXT("Unable to query landscape extent");
            return false;
        }

        const FTransform LandscapeTransform = Landscape->GetTransform();
        const TArray<FVector> WorldCorners = {
            FVector(MinCorner.X, MinCorner.Y, MinCorner.Z),
            FVector(MinCorner.X, MaxCorner.Y, MinCorner.Z),
            FVector(MaxCorner.X, MinCorner.Y, MinCorner.Z),
            FVector(MaxCorner.X, MaxCorner.Y, MinCorner.Z),
        };

        float LocalMinX = 0.0f;
        float LocalMinY = 0.0f;
        float LocalMaxX = 0.0f;
        float LocalMaxY = 0.0f;
        bool bFirstCorner = true;
        for (const FVector& WorldCorner : WorldCorners)
        {
            const FVector LocalCorner = LandscapeTransform.InverseTransformPosition(WorldCorner);
            if (bFirstCorner)
            {
                LocalMinX = LocalMaxX = LocalCorner.X;
                LocalMinY = LocalMaxY = LocalCorner.Y;
                bFirstCorner = false;
                continue;
            }

            LocalMinX = FMath::Min(LocalMinX, LocalCorner.X);
            LocalMinY = FMath::Min(LocalMinY, LocalCorner.Y);
            LocalMaxX = FMath::Max(LocalMaxX, LocalCorner.X);
            LocalMaxY = FMath::Max(LocalMaxY, LocalCorner.Y);
        }

        const int32 RequestedMinX = FMath::FloorToInt(LocalMinX);
        const int32 RequestedMinY = FMath::FloorToInt(LocalMinY);
        const int32 RequestedMaxX = FMath::CeilToInt(LocalMaxX);
        const int32 RequestedMaxY = FMath::CeilToInt(LocalMaxY);

        OutMinX = FMath::Clamp(RequestedMinX, LandscapeMinX, LandscapeMaxX);
        OutMinY = FMath::Clamp(RequestedMinY, LandscapeMinY, LandscapeMaxY);
        OutMaxX = FMath::Clamp(RequestedMaxX, LandscapeMinX, LandscapeMaxX);
        OutMaxY = FMath::Clamp(RequestedMaxY, LandscapeMinY, LandscapeMaxY);
        if (OutMinX > OutMaxX || OutMinY > OutMaxY)
        {
            OutError = TEXT("Requested region does not overlap the landscape extent");
            return false;
        }

        return true;
    }

    bool TryBuildLandscapeGridSamples(
        ALandscape* Landscape,
        const TArray<ULandscapeLayerInfoObject*>& LandscapeLayerInfos,
        const FVector& Origin,
        const double StepX,
        const double StepY,
        const int32 CountX,
        const int32 CountY,
        TArray<TSharedPtr<FJsonValue>>& OutSamples,
        FString& OutError)
    {
        const int64 TotalSamples = static_cast<int64>(CountX) * static_cast<int64>(CountY);
        OutSamples.Reset();
        OutSamples.Reserve(static_cast<int32>(TotalSamples));

        int32 SampleIndex = 0;
        for (int32 GridY = 0; GridY < CountY; ++GridY)
        {
            for (int32 GridX = 0; GridX < CountX; ++GridX)
            {
                const FVector SampleLocation(
                    Origin.X + static_cast<float>(GridX * StepX),
                    Origin.Y + static_cast<float>(GridY * StepY),
                    Origin.Z);

                FString ErrorMessage;
                TSharedPtr<FJsonObject> Sample = BuildLandscapePointSample(Landscape, LandscapeLayerInfos, SampleLocation, ErrorMessage);
                if (!Sample.IsValid())
                {
                    OutError = FString::Printf(TEXT("Grid sample (%d, %d) failed: %s"), GridX, GridY, *ErrorMessage);
                    return false;
                }

                Sample->SetNumberField(TEXT("sample_index"), SampleIndex++);
                Sample->SetNumberField(TEXT("grid_x"), GridX);
                Sample->SetNumberField(TEXT("grid_y"), GridY);
                OutSamples.Add(MakeShared<FJsonValueObject>(Sample));
            }
        }

        return true;
    }

    bool TryBuildLandscapeHeightRows(
        ALandscape* Landscape,
        const FVector& Origin,
        const double StepX,
        const double StepY,
        const int32 CountX,
        const int32 CountY,
        TArray<TSharedPtr<FJsonValue>>& OutHeightRows,
        double& OutMinHeight,
        double& OutMaxHeight,
        FString& OutError)
    {
        OutHeightRows.Reset();
        OutHeightRows.Reserve(CountY);

        bool bHasHeight = false;
        OutMinHeight = 0.0;
        OutMaxHeight = 0.0;

        for (int32 GridY = 0; GridY < CountY; ++GridY)
        {
            TArray<TSharedPtr<FJsonValue>> RowValues;
            RowValues.Reserve(CountX);

            for (int32 GridX = 0; GridX < CountX; ++GridX)
            {
                const FVector SampleLocation(
                    Origin.X + static_cast<float>(GridX * StepX),
                    Origin.Y + static_cast<float>(GridY * StepY),
                    Origin.Z);

                ULandscapeComponent* LandscapeComponent = FindLandscapeComponentAtLocation(Landscape, SampleLocation);
                if (!LandscapeComponent)
                {
                    OutError = FString::Printf(TEXT("Height sample (%d, %d) failed: Unable to resolve landscape component at the sample location"), GridX, GridY);
                    return false;
                }

                const TOptional<float> HeightAtLocation = SampleLandscapeHeightFromComponent(LandscapeComponent, SampleLocation);
                if (!HeightAtLocation.IsSet())
                {
                    OutError = FString::Printf(TEXT("Height sample (%d, %d) failed: Unable to sample landscape height at the requested location"), GridX, GridY);
                    return false;
                }

                const double HeightValue = HeightAtLocation.GetValue();
                if (!bHasHeight)
                {
                    OutMinHeight = HeightValue;
                    OutMaxHeight = HeightValue;
                    bHasHeight = true;
                }
                else
                {
                    OutMinHeight = FMath::Min(OutMinHeight, HeightValue);
                    OutMaxHeight = FMath::Max(OutMaxHeight, HeightValue);
                }

                RowValues.Add(MakeShared<FJsonValueNumber>(HeightValue));
            }

            OutHeightRows.Add(MakeShared<FJsonValueArray>(RowValues));
        }

        return true;
    }

    bool TryBuildLandscapeWeightRows(
        ALandscape* Landscape,
        ULandscapeLayerInfoObject* LayerInfo,
        const FVector& Origin,
        const double StepX,
        const double StepY,
        const int32 CountX,
        const int32 CountY,
        TArray<TSharedPtr<FJsonValue>>& OutWeightRows,
        double& OutMinWeight,
        double& OutMaxWeight,
        FString& OutError)
    {
        OutWeightRows.Reset();
        OutWeightRows.Reserve(CountY);

        ULandscapeInfo* LandscapeInfo = Landscape ? Landscape->GetLandscapeInfo() : nullptr;
        if (!LandscapeInfo)
        {
            OutError = TEXT("Unable to query landscape info");
            return false;
        }

        int32 LandscapeMinX = 0;
        int32 LandscapeMinY = 0;
        int32 LandscapeMaxX = 0;
        int32 LandscapeMaxY = 0;
        if (!GetLandscapeExtent(Landscape, LandscapeMinX, LandscapeMinY, LandscapeMaxX, LandscapeMaxY))
        {
            OutError = TEXT("Unable to query landscape extent");
            return false;
        }

        const FTransform LandscapeTransform = Landscape->GetTransform();
        FLandscapeEditDataInterface LandscapeEdit(LandscapeInfo);

        bool bHasWeight = false;
        OutMinWeight = 0.0;
        OutMaxWeight = 0.0;

        for (int32 GridY = 0; GridY < CountY; ++GridY)
        {
            TArray<TSharedPtr<FJsonValue>> RowValues;
            RowValues.Reserve(CountX);

            for (int32 GridX = 0; GridX < CountX; ++GridX)
            {
                const FVector SampleLocation(
                    Origin.X + static_cast<float>(GridX * StepX),
                    Origin.Y + static_cast<float>(GridY * StepY),
                    Origin.Z);

                const FVector LocalSampleLocation = LandscapeTransform.InverseTransformPosition(SampleLocation);
                const int32 SampleX = FMath::RoundToInt(LocalSampleLocation.X);
                const int32 SampleY = FMath::RoundToInt(LocalSampleLocation.Y);
                if (SampleX < LandscapeMinX || SampleX > LandscapeMaxX || SampleY < LandscapeMinY || SampleY > LandscapeMaxY)
                {
                    OutError = FString::Printf(TEXT("Weight sample (%d, %d) failed: Sample location is outside the landscape extent"), GridX, GridY);
                    return false;
                }

                uint8 WeightByte = 0;
                LandscapeEdit.GetWeightDataFast(LayerInfo, SampleX, SampleY, SampleX, SampleY, &WeightByte, 0);
                const double WeightValue = static_cast<double>(WeightByte) / 255.0;
                if (!bHasWeight)
                {
                    OutMinWeight = WeightValue;
                    OutMaxWeight = WeightValue;
                    bHasWeight = true;
                }
                else
                {
                    OutMinWeight = FMath::Min(OutMinWeight, WeightValue);
                    OutMaxWeight = FMath::Max(OutMaxWeight, WeightValue);
                }

                RowValues.Add(MakeShared<FJsonValueNumber>(WeightValue));
            }

            OutWeightRows.Add(MakeShared<FJsonValueArray>(RowValues));
        }

        return true;
    }
}

FUnrealAILandscapeCommands::FUnrealAILandscapeCommands()
{
}

TSharedPtr<FJsonObject> FUnrealAILandscapeCommands::HandleCommand(const FString& CommandType, const TSharedPtr<FJsonObject>& Params)
{
    if (CommandType == TEXT("get_landscapes"))
    {
        return HandleGetLandscapes(Params);
    }
    if (CommandType == TEXT("read_landscape_content"))
    {
        return HandleReadLandscapeContent(Params);
    }
    if (CommandType == TEXT("sample_landscape_point"))
    {
        return HandleSampleLandscapePoint(Params);
    }
    if (CommandType == TEXT("sample_landscape_points"))
    {
        return HandleSampleLandscapePoints(Params);
    }
    if (CommandType == TEXT("sample_landscape_grid"))
    {
        return HandleSampleLandscapeGrid(Params);
    }
    if (CommandType == TEXT("sample_landscape_region"))
    {
        return HandleSampleLandscapeRegion(Params);
    }
    if (CommandType == TEXT("sample_landscape_height_region"))
    {
        return HandleSampleLandscapeHeightRegion(Params);
    }
    if (CommandType == TEXT("sample_landscape_weight_region"))
    {
        return HandleSampleLandscapeWeightRegion(Params);
    }
    if (CommandType == TEXT("paint_landscape_layer_region"))
    {
        return HandlePaintLandscapeLayerRegion(Params);
    }
    if (CommandType == TEXT("sculpt_landscape_height_region"))
    {
        return HandleSculptLandscapeHeightRegion(Params);
    }
    if (CommandType == TEXT("rebuild_landscape"))
    {
        return HandleRebuildLandscape(Params);
    }
    if (CommandType == TEXT("create_landscape"))
    {
        return HandleCreateLandscape(Params);
    }
    if (CommandType == TEXT("set_landscape_flat_height"))
    {
        return HandleSetLandscapeFlatHeight(Params);
    }
    if (CommandType == TEXT("import_landscape_heightmap"))
    {
        return HandleImportLandscapeHeightmap(Params);
    }

    return FUnrealAICommonUtils::CreateErrorResponse(
        FString::Printf(TEXT("Unknown landscape command: %s"), *CommandType));
}

TSharedPtr<FJsonObject> FUnrealAILandscapeCommands::HandleGetLandscapes(const TSharedPtr<FJsonObject>& Params)
{
    if (!GWorld)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("No active world"));
    }

    TArray<AActor*> LandscapeActors;
    UGameplayStatics::GetAllActorsOfClass(GWorld, ALandscape::StaticClass(), LandscapeActors);

    TArray<TSharedPtr<FJsonValue>> Landscapes;
    for (AActor* Actor : LandscapeActors)
    {
        if (ALandscape* Landscape = Cast<ALandscape>(Actor))
        {
            Landscapes.Add(MakeShared<FJsonValueObject>(LandscapeToJson(Landscape)));
        }
    }

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetArrayField(TEXT("landscapes"), Landscapes);
    Result->SetNumberField(TEXT("count"), Landscapes.Num());
    return Result;
}

TSharedPtr<FJsonObject> FUnrealAILandscapeCommands::HandleReadLandscapeContent(const TSharedPtr<FJsonObject>& Params)
{
    FString LandscapeName;
    if (!Params->TryGetStringField(TEXT("landscape_name"), LandscapeName) || LandscapeName.IsEmpty())
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing 'landscape_name'"));
    }

    ALandscape* Landscape = FindLandscapeByName(LandscapeName);
    if (!Landscape)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(FString::Printf(TEXT("Landscape not found: %s"), *LandscapeName));
    }

    TSharedPtr<FJsonObject> Result = LandscapeToJson(Landscape);

    const int32 SelectedEditLayerIndex = Landscape->GetSelectedEditLayerIndex();
    Result->SetNumberField(TEXT("selected_edit_layer_index"), SelectedEditLayerIndex);

    TArray<TSharedPtr<FJsonValue>> EditLayers;
    const TArrayView<const FLandscapeLayer> LandscapeLayers = Landscape->GetLayersConst();
    for (int32 LayerIndex = 0; LayerIndex < LandscapeLayers.Num(); ++LayerIndex)
    {
        EditLayers.Add(MakeShared<FJsonValueObject>(LandscapeEditLayerToJson(Landscape, LandscapeLayers[LayerIndex], LayerIndex)));
    }
    Result->SetArrayField(TEXT("edit_layers"), EditLayers);
    Result->SetNumberField(TEXT("edit_layer_count"), EditLayers.Num());

    TArray<TSharedPtr<FJsonValue>> UniqueLayerInfos;
    TArray<FString> TargetLayerStrings;
    const TArray<ULandscapeLayerInfoObject*> LandscapeLayerInfos = CollectLandscapeLayerInfos(Landscape);
    for (ULandscapeLayerInfoObject* LayerInfo : LandscapeLayerInfos)
    {
        TargetLayerStrings.Add(LayerInfo->GetLayerName().ToString());
        UniqueLayerInfos.Add(MakeShared<FJsonValueObject>(LayerInfoToJson(LayerInfo)));
    }

    AddStringArray(Result, TEXT("target_layer_names"), TargetLayerStrings);
    Result->SetNumberField(TEXT("target_layer_count"), TargetLayerStrings.Num());
    Result->SetArrayField(TEXT("layer_info_objects"), UniqueLayerInfos);
    Result->SetNumberField(TEXT("layer_info_object_count"), UniqueLayerInfos.Num());
    return Result;
}

TSharedPtr<FJsonObject> FUnrealAILandscapeCommands::HandleSampleLandscapePoint(const TSharedPtr<FJsonObject>& Params)
{
    FString LandscapeName;
    if (!Params->TryGetStringField(TEXT("landscape_name"), LandscapeName) || LandscapeName.IsEmpty())
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing 'landscape_name'"));
    }

    if (!Params->HasField(TEXT("location")))
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing 'location'"));
    }

    ALandscape* Landscape = FindLandscapeByName(LandscapeName);
    if (!Landscape)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(FString::Printf(TEXT("Landscape not found: %s"), *LandscapeName));
    }

    const TArray<ULandscapeLayerInfoObject*> LandscapeLayerInfos = CollectLandscapeLayerInfos(Landscape);
    const FVector SampleLocation = FUnrealAICommonUtils::GetVectorFromJson(Params, TEXT("location"));

    FString ErrorMessage;
    TSharedPtr<FJsonObject> Sample = BuildLandscapePointSample(Landscape, LandscapeLayerInfos, SampleLocation, ErrorMessage);
    if (!Sample.IsValid())
    {
        return FUnrealAICommonUtils::CreateErrorResponse(ErrorMessage);
    }

    TSharedPtr<FJsonObject> Result = Sample;
    Result->SetBoolField(TEXT("success"), true);
    Result->SetStringField(TEXT("landscape_name"), Landscape->GetActorLabel());
    Result->SetStringField(TEXT("label"), Landscape->GetActorLabel());
    Result->SetStringField(TEXT("internal_name"), Landscape->GetName());
    AddLandscapeTargetLayerSummary(Result, LandscapeLayerInfos);
    return Result;
}

TSharedPtr<FJsonObject> FUnrealAILandscapeCommands::HandleSampleLandscapePoints(const TSharedPtr<FJsonObject>& Params)
{
    FString LandscapeName;
    if (!Params->TryGetStringField(TEXT("landscape_name"), LandscapeName) || LandscapeName.IsEmpty())
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing 'landscape_name'"));
    }

    const TArray<TSharedPtr<FJsonValue>>* LocationValues = nullptr;
    if (!Params->TryGetArrayField(TEXT("locations"), LocationValues) || !LocationValues || LocationValues->Num() == 0)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing or empty 'locations'"));
    }

    ALandscape* Landscape = FindLandscapeByName(LandscapeName);
    if (!Landscape)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(FString::Printf(TEXT("Landscape not found: %s"), *LandscapeName));
    }

    const TArray<ULandscapeLayerInfoObject*> LandscapeLayerInfos = CollectLandscapeLayerInfos(Landscape);

    TArray<TSharedPtr<FJsonValue>> Samples;
    Samples.Reserve(LocationValues->Num());
    for (int32 SampleIndex = 0; SampleIndex < LocationValues->Num(); ++SampleIndex)
    {
        FVector SampleLocation = FVector::ZeroVector;
        if (!TryGetVectorFromJsonValue((*LocationValues)[SampleIndex], SampleLocation))
        {
            return FUnrealAICommonUtils::CreateErrorResponse(FString::Printf(TEXT("Invalid location at index %d"), SampleIndex));
        }

        FString ErrorMessage;
        TSharedPtr<FJsonObject> Sample = BuildLandscapePointSample(Landscape, LandscapeLayerInfos, SampleLocation, ErrorMessage);
        if (!Sample.IsValid())
        {
            return FUnrealAICommonUtils::CreateErrorResponse(FString::Printf(TEXT("Sample %d failed: %s"), SampleIndex, *ErrorMessage));
        }

        Sample->SetNumberField(TEXT("sample_index"), SampleIndex);
        Samples.Add(MakeShared<FJsonValueObject>(Sample));
    }

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetBoolField(TEXT("success"), true);
    Result->SetStringField(TEXT("landscape_name"), Landscape->GetActorLabel());
    Result->SetStringField(TEXT("label"), Landscape->GetActorLabel());
    Result->SetStringField(TEXT("internal_name"), Landscape->GetName());
    AddLandscapeTargetLayerSummary(Result, LandscapeLayerInfos);
    Result->SetArrayField(TEXT("samples"), Samples);
    Result->SetNumberField(TEXT("sample_count"), Samples.Num());
    return Result;
}

TSharedPtr<FJsonObject> FUnrealAILandscapeCommands::HandleSampleLandscapeGrid(const TSharedPtr<FJsonObject>& Params)
{
    FString LandscapeName;
    if (!Params->TryGetStringField(TEXT("landscape_name"), LandscapeName) || LandscapeName.IsEmpty())
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing 'landscape_name'"));
    }

    if (!Params->HasField(TEXT("origin")))
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing 'origin'"));
    }

    double StepX = 0.0;
    double StepY = 0.0;
    int32 CountX = 0;
    int32 CountY = 0;
    if (!Params->TryGetNumberField(TEXT("step_x"), StepX) || !Params->TryGetNumberField(TEXT("step_y"), StepY))
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing 'step_x' or 'step_y'"));
    }

    if (!Params->TryGetNumberField(TEXT("count_x"), CountX) || !Params->TryGetNumberField(TEXT("count_y"), CountY))
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing 'count_x' or 'count_y'"));
    }

    if (CountX <= 0 || CountY <= 0)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("'count_x' and 'count_y' must be positive"));
    }

    const int64 TotalSamples = static_cast<int64>(CountX) * static_cast<int64>(CountY);
    if (TotalSamples > 4096)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Grid sample count exceeds the maximum of 4096 points"));
    }

    ALandscape* Landscape = FindLandscapeByName(LandscapeName);
    if (!Landscape)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(FString::Printf(TEXT("Landscape not found: %s"), *LandscapeName));
    }

    const FVector Origin = FUnrealAICommonUtils::GetVectorFromJson(Params, TEXT("origin"));
    const TArray<ULandscapeLayerInfoObject*> LandscapeLayerInfos = CollectLandscapeLayerInfos(Landscape);

    TArray<TSharedPtr<FJsonValue>> Samples;
    FString ErrorMessage;
    if (!TryBuildLandscapeGridSamples(Landscape, LandscapeLayerInfos, Origin, StepX, StepY, CountX, CountY, Samples, ErrorMessage))
    {
        return FUnrealAICommonUtils::CreateErrorResponse(ErrorMessage);
    }

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetBoolField(TEXT("success"), true);
    Result->SetStringField(TEXT("landscape_name"), Landscape->GetActorLabel());
    Result->SetStringField(TEXT("label"), Landscape->GetActorLabel());
    Result->SetStringField(TEXT("internal_name"), Landscape->GetName());
    AddNumberArray(Result, TEXT("origin"), {Origin.X, Origin.Y, Origin.Z});
    Result->SetNumberField(TEXT("step_x"), StepX);
    Result->SetNumberField(TEXT("step_y"), StepY);
    Result->SetNumberField(TEXT("count_x"), CountX);
    Result->SetNumberField(TEXT("count_y"), CountY);
    AddLandscapeTargetLayerSummary(Result, LandscapeLayerInfos);
    Result->SetArrayField(TEXT("samples"), Samples);
    Result->SetNumberField(TEXT("sample_count"), Samples.Num());
    return Result;
}

TSharedPtr<FJsonObject> FUnrealAILandscapeCommands::HandleSampleLandscapeRegion(const TSharedPtr<FJsonObject>& Params)
{
    FString LandscapeName;
    if (!Params->TryGetStringField(TEXT("landscape_name"), LandscapeName) || LandscapeName.IsEmpty())
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing 'landscape_name'"));
    }

    if (!Params->HasField(TEXT("min_corner")) || !Params->HasField(TEXT("max_corner")))
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing 'min_corner' or 'max_corner'"));
    }

    double StepX = 0.0;
    double StepY = 0.0;
    if (!Params->TryGetNumberField(TEXT("step_x"), StepX) || !Params->TryGetNumberField(TEXT("step_y"), StepY))
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing 'step_x' or 'step_y'"));
    }

    if (StepX <= 0.0 || StepY <= 0.0)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("'step_x' and 'step_y' must be positive"));
    }

    ALandscape* Landscape = FindLandscapeByName(LandscapeName);
    if (!Landscape)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(FString::Printf(TEXT("Landscape not found: %s"), *LandscapeName));
    }

    const FVector MinCorner = FUnrealAICommonUtils::GetVectorFromJson(Params, TEXT("min_corner"));
    const FVector MaxCorner = FUnrealAICommonUtils::GetVectorFromJson(Params, TEXT("max_corner"));
    if (MaxCorner.X < MinCorner.X || MaxCorner.Y < MinCorner.Y)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("'max_corner' must be greater than or equal to 'min_corner' on X and Y"));
    }

    const int32 CountX = FMath::FloorToInt(static_cast<float>((MaxCorner.X - MinCorner.X) / StepX)) + 1;
    const int32 CountY = FMath::FloorToInt(static_cast<float>((MaxCorner.Y - MinCorner.Y) / StepY)) + 1;
    if (CountX <= 0 || CountY <= 0)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Resolved region sample counts must be positive"));
    }

    const int64 TotalSamples = static_cast<int64>(CountX) * static_cast<int64>(CountY);
    if (TotalSamples > 4096)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Region sample count exceeds the maximum of 4096 points"));
    }

    const TArray<ULandscapeLayerInfoObject*> LandscapeLayerInfos = CollectLandscapeLayerInfos(Landscape);

    TArray<TSharedPtr<FJsonValue>> Samples;
    FString ErrorMessage;
    if (!TryBuildLandscapeGridSamples(Landscape, LandscapeLayerInfos, MinCorner, StepX, StepY, CountX, CountY, Samples, ErrorMessage))
    {
        return FUnrealAICommonUtils::CreateErrorResponse(ErrorMessage);
    }

    const FVector SampledMax(
        MinCorner.X + static_cast<float>((CountX - 1) * StepX),
        MinCorner.Y + static_cast<float>((CountY - 1) * StepY),
        MinCorner.Z);

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetBoolField(TEXT("success"), true);
    Result->SetStringField(TEXT("landscape_name"), Landscape->GetActorLabel());
    Result->SetStringField(TEXT("label"), Landscape->GetActorLabel());
    Result->SetStringField(TEXT("internal_name"), Landscape->GetName());
    AddNumberArray(Result, TEXT("min_corner"), {MinCorner.X, MinCorner.Y, MinCorner.Z});
    AddNumberArray(Result, TEXT("max_corner"), {MaxCorner.X, MaxCorner.Y, MaxCorner.Z});
    AddNumberArray(Result, TEXT("sampled_max_corner"), {SampledMax.X, SampledMax.Y, SampledMax.Z});
    Result->SetNumberField(TEXT("step_x"), StepX);
    Result->SetNumberField(TEXT("step_y"), StepY);
    Result->SetNumberField(TEXT("count_x"), CountX);
    Result->SetNumberField(TEXT("count_y"), CountY);
    AddLandscapeTargetLayerSummary(Result, LandscapeLayerInfos);
    Result->SetArrayField(TEXT("samples"), Samples);
    Result->SetNumberField(TEXT("sample_count"), Samples.Num());
    return Result;
}

TSharedPtr<FJsonObject> FUnrealAILandscapeCommands::HandleSampleLandscapeHeightRegion(const TSharedPtr<FJsonObject>& Params)
{
    FString LandscapeName;
    if (!Params->TryGetStringField(TEXT("landscape_name"), LandscapeName) || LandscapeName.IsEmpty())
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing 'landscape_name'"));
    }

    if (!Params->HasField(TEXT("min_corner")) || !Params->HasField(TEXT("max_corner")))
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing 'min_corner' or 'max_corner'"));
    }

    double StepX = 0.0;
    double StepY = 0.0;
    if (!Params->TryGetNumberField(TEXT("step_x"), StepX) || !Params->TryGetNumberField(TEXT("step_y"), StepY))
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing 'step_x' or 'step_y'"));
    }

    if (StepX <= 0.0 || StepY <= 0.0)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("'step_x' and 'step_y' must be positive"));
    }

    ALandscape* Landscape = FindLandscapeByName(LandscapeName);
    if (!Landscape)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(FString::Printf(TEXT("Landscape not found: %s"), *LandscapeName));
    }

    const FVector MinCorner = FUnrealAICommonUtils::GetVectorFromJson(Params, TEXT("min_corner"));
    const FVector MaxCorner = FUnrealAICommonUtils::GetVectorFromJson(Params, TEXT("max_corner"));
    if (MaxCorner.X < MinCorner.X || MaxCorner.Y < MinCorner.Y)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("'max_corner' must be greater than or equal to 'min_corner' on X and Y"));
    }

    const int32 CountX = FMath::FloorToInt(static_cast<float>((MaxCorner.X - MinCorner.X) / StepX)) + 1;
    const int32 CountY = FMath::FloorToInt(static_cast<float>((MaxCorner.Y - MinCorner.Y) / StepY)) + 1;
    if (CountX <= 0 || CountY <= 0)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Resolved height-region sample counts must be positive"));
    }

    const int64 TotalSamples = static_cast<int64>(CountX) * static_cast<int64>(CountY);
    if (TotalSamples > 16384)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Height-region sample count exceeds the maximum of 16384 points"));
    }

    TArray<TSharedPtr<FJsonValue>> HeightRows;
    double MinHeightWorld = 0.0;
    double MaxHeightWorld = 0.0;
    FString ErrorMessage;
    if (!TryBuildLandscapeHeightRows(Landscape, MinCorner, StepX, StepY, CountX, CountY, HeightRows, MinHeightWorld, MaxHeightWorld, ErrorMessage))
    {
        return FUnrealAICommonUtils::CreateErrorResponse(ErrorMessage);
    }

    const FVector SampledMax(
        MinCorner.X + static_cast<float>((CountX - 1) * StepX),
        MinCorner.Y + static_cast<float>((CountY - 1) * StepY),
        MinCorner.Z);

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetBoolField(TEXT("success"), true);
    Result->SetStringField(TEXT("landscape_name"), Landscape->GetActorLabel());
    Result->SetStringField(TEXT("label"), Landscape->GetActorLabel());
    Result->SetStringField(TEXT("internal_name"), Landscape->GetName());
    AddNumberArray(Result, TEXT("min_corner"), {MinCorner.X, MinCorner.Y, MinCorner.Z});
    AddNumberArray(Result, TEXT("max_corner"), {MaxCorner.X, MaxCorner.Y, MaxCorner.Z});
    AddNumberArray(Result, TEXT("sampled_max_corner"), {SampledMax.X, SampledMax.Y, SampledMax.Z});
    Result->SetNumberField(TEXT("step_x"), StepX);
    Result->SetNumberField(TEXT("step_y"), StepY);
    Result->SetNumberField(TEXT("count_x"), CountX);
    Result->SetNumberField(TEXT("count_y"), CountY);
    Result->SetNumberField(TEXT("sample_count"), TotalSamples);
    Result->SetNumberField(TEXT("height_min_world"), MinHeightWorld);
    Result->SetNumberField(TEXT("height_max_world"), MaxHeightWorld);
    Result->SetArrayField(TEXT("height_rows"), HeightRows);
    return Result;
}

TSharedPtr<FJsonObject> FUnrealAILandscapeCommands::HandleSampleLandscapeWeightRegion(const TSharedPtr<FJsonObject>& Params)
{
    FString LandscapeName;
    FString LayerName;
    if (!Params->TryGetStringField(TEXT("landscape_name"), LandscapeName) || LandscapeName.IsEmpty())
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing 'landscape_name'"));
    }
    if (!Params->TryGetStringField(TEXT("layer_name"), LayerName) || LayerName.IsEmpty())
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing 'layer_name'"));
    }
    if (!Params->HasField(TEXT("min_corner")) || !Params->HasField(TEXT("max_corner")))
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing 'min_corner' or 'max_corner'"));
    }

    double StepX = 0.0;
    double StepY = 0.0;
    if (!Params->TryGetNumberField(TEXT("step_x"), StepX) || !Params->TryGetNumberField(TEXT("step_y"), StepY))
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing 'step_x' or 'step_y'"));
    }
    if (StepX <= 0.0 || StepY <= 0.0)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("'step_x' and 'step_y' must be positive"));
    }

    ALandscape* Landscape = FindLandscapeByName(LandscapeName);
    if (!Landscape)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(FString::Printf(TEXT("Landscape not found: %s"), *LandscapeName));
    }

    ULandscapeLayerInfoObject* LayerInfo = ResolveLandscapeLayerInfo(Landscape, LayerName);
    if (!LayerInfo)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(FString::Printf(TEXT("Landscape layer not found: %s"), *LayerName));
    }

    const FVector MinCorner = FUnrealAICommonUtils::GetVectorFromJson(Params, TEXT("min_corner"));
    const FVector MaxCorner = FUnrealAICommonUtils::GetVectorFromJson(Params, TEXT("max_corner"));
    if (MaxCorner.X < MinCorner.X || MaxCorner.Y < MinCorner.Y)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("'max_corner' must be greater than or equal to 'min_corner' on X and Y"));
    }

    const int32 CountX = FMath::FloorToInt(static_cast<float>((MaxCorner.X - MinCorner.X) / StepX)) + 1;
    const int32 CountY = FMath::FloorToInt(static_cast<float>((MaxCorner.Y - MinCorner.Y) / StepY)) + 1;
    if (CountX <= 0 || CountY <= 0)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Resolved weight-region sample counts must be positive"));
    }

    const int64 TotalSamples = static_cast<int64>(CountX) * static_cast<int64>(CountY);
    if (TotalSamples > 16384)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Weight-region sample count exceeds the maximum of 16384 points"));
    }

    TArray<TSharedPtr<FJsonValue>> WeightRows;
    double WeightMin = 0.0;
    double WeightMax = 0.0;
    FString ErrorMessage;
    if (!TryBuildLandscapeWeightRows(Landscape, LayerInfo, MinCorner, StepX, StepY, CountX, CountY, WeightRows, WeightMin, WeightMax, ErrorMessage))
    {
        return FUnrealAICommonUtils::CreateErrorResponse(ErrorMessage);
    }

    const FVector SampledMax(
        MinCorner.X + static_cast<float>((CountX - 1) * StepX),
        MinCorner.Y + static_cast<float>((CountY - 1) * StepY),
        MinCorner.Z);

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetBoolField(TEXT("success"), true);
    Result->SetStringField(TEXT("landscape_name"), Landscape->GetActorLabel());
    Result->SetStringField(TEXT("label"), Landscape->GetActorLabel());
    Result->SetStringField(TEXT("internal_name"), Landscape->GetName());
    Result->SetStringField(TEXT("layer_name"), LayerInfo == ALandscapeProxy::VisibilityLayer ? TEXT("Visibility") : LayerInfo->GetLayerName().ToString());
    Result->SetStringField(TEXT("layer_object_name"), LayerInfo->GetName());
    Result->SetStringField(TEXT("layer_path"), LayerInfo->GetPathName());
    AddNumberArray(Result, TEXT("min_corner"), {MinCorner.X, MinCorner.Y, MinCorner.Z});
    AddNumberArray(Result, TEXT("max_corner"), {MaxCorner.X, MaxCorner.Y, MaxCorner.Z});
    AddNumberArray(Result, TEXT("sampled_max_corner"), {SampledMax.X, SampledMax.Y, SampledMax.Z});
    Result->SetNumberField(TEXT("step_x"), StepX);
    Result->SetNumberField(TEXT("step_y"), StepY);
    Result->SetNumberField(TEXT("count_x"), CountX);
    Result->SetNumberField(TEXT("count_y"), CountY);
    Result->SetNumberField(TEXT("sample_count"), TotalSamples);
    Result->SetNumberField(TEXT("weight_min"), WeightMin);
    Result->SetNumberField(TEXT("weight_max"), WeightMax);
    Result->SetArrayField(TEXT("weight_rows"), WeightRows);
    return Result;
}

TSharedPtr<FJsonObject> FUnrealAILandscapeCommands::HandlePaintLandscapeLayerRegion(const TSharedPtr<FJsonObject>& Params)
{
    FString LandscapeName;
    FString LayerName;
    if (!Params->TryGetStringField(TEXT("landscape_name"), LandscapeName) || LandscapeName.IsEmpty())
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing 'landscape_name'"));
    }
    if (!Params->TryGetStringField(TEXT("layer_name"), LayerName) || LayerName.IsEmpty())
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing 'layer_name'"));
    }
    if (!Params->HasField(TEXT("min_corner")) || !Params->HasField(TEXT("max_corner")))
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing 'min_corner' or 'max_corner'"));
    }

    double Weight = 1.0;
    Params->TryGetNumberField(TEXT("weight"), Weight);
    if (Weight < 0.0 || Weight > 1.0)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("'weight' must be between 0.0 and 1.0"));
    }

    ALandscape* Landscape = FindLandscapeByName(LandscapeName);
    if (!Landscape)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(FString::Printf(TEXT("Landscape not found: %s"), *LandscapeName));
    }

    ULandscapeLayerInfoObject* LayerInfo = ResolveLandscapeLayerInfo(Landscape, LayerName);
    if (!LayerInfo)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(FString::Printf(TEXT("Landscape layer not found: %s"), *LayerName));
    }

    const FVector MinCorner = FUnrealAICommonUtils::GetVectorFromJson(Params, TEXT("min_corner"));
    const FVector MaxCorner = FUnrealAICommonUtils::GetVectorFromJson(Params, TEXT("max_corner"));
    if (MaxCorner.X < MinCorner.X || MaxCorner.Y < MinCorner.Y)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("'max_corner' must be greater than or equal to 'min_corner' on X and Y"));
    }

    int32 MinX = 0;
    int32 MinY = 0;
    int32 MaxX = 0;
    int32 MaxY = 0;
    FString ErrorMessage;
    if (!TryResolveLandscapeRegionExtents(Landscape, MinCorner, MaxCorner, MinX, MinY, MaxX, MaxY, ErrorMessage))
    {
        return FUnrealAICommonUtils::CreateErrorResponse(ErrorMessage);
    }

    ULandscapeInfo* LandscapeInfo = Landscape->GetLandscapeInfo();
    if (!LandscapeInfo)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Unable to query landscape info"));
    }

    const int32 Width = MaxX - MinX + 1;
    const int32 Height = MaxY - MinY + 1;
    const uint8 WeightValue = static_cast<uint8>(FMath::Clamp(FMath::RoundToInt(Weight * 255.0), 0, 255));
    TArray<uint8> WeightData;
    WeightData.Init(WeightValue, Width * Height);

    FScopedTransaction Transaction(NSLOCTEXT("UnrealAI", "PaintLandscapeLayerRegion", "Paint Landscape Layer Region"));
    Landscape->Modify();

    TAlphamapAccessor<false> AlphamapAccessor(LandscapeInfo, LayerInfo);
    AlphamapAccessor.SetData(MinX, MinY, MaxX, MaxY, WeightData.GetData(), ELandscapeLayerPaintingRestriction::None);
    AlphamapAccessor.Flush();

    Landscape->ForceLayersFullUpdate();
    Landscape->MarkPackageDirty();

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetBoolField(TEXT("success"), true);
    Result->SetStringField(TEXT("landscape_name"), Landscape->GetActorLabel());
    Result->SetStringField(TEXT("layer_name"), LayerInfo == ALandscapeProxy::VisibilityLayer ? TEXT("Visibility") : LayerInfo->GetLayerName().ToString());
    Result->SetStringField(TEXT("layer_object_name"), LayerInfo->GetName());
    Result->SetStringField(TEXT("layer_path"), LayerInfo->GetPathName());
    Result->SetNumberField(TEXT("weight"), Weight);
    Result->SetNumberField(TEXT("weight_byte"), WeightValue);
    AddNumberArray(Result, TEXT("affected_extent"), {
        static_cast<double>(MinX),
        static_cast<double>(MinY),
        static_cast<double>(MaxX),
        static_cast<double>(MaxY),
    });
    Result->SetNumberField(TEXT("width"), Width);
    Result->SetNumberField(TEXT("height"), Height);
    return Result;
}

TSharedPtr<FJsonObject> FUnrealAILandscapeCommands::HandleSculptLandscapeHeightRegion(const TSharedPtr<FJsonObject>& Params)
{
    FString LandscapeName;
    if (!Params->TryGetStringField(TEXT("landscape_name"), LandscapeName) || LandscapeName.IsEmpty())
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing 'landscape_name'"));
    }
    if (!Params->HasField(TEXT("min_corner")) || !Params->HasField(TEXT("max_corner")))
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing 'min_corner' or 'max_corner'"));
    }

    double HeightWorld = 0.0;
    Params->TryGetNumberField(TEXT("height_world"), HeightWorld);

    ALandscape* Landscape = FindLandscapeByName(LandscapeName);
    if (!Landscape)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(FString::Printf(TEXT("Landscape not found: %s"), *LandscapeName));
    }

    const FVector MinCorner = FUnrealAICommonUtils::GetVectorFromJson(Params, TEXT("min_corner"));
    const FVector MaxCorner = FUnrealAICommonUtils::GetVectorFromJson(Params, TEXT("max_corner"));
    if (MaxCorner.X < MinCorner.X || MaxCorner.Y < MinCorner.Y)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("'max_corner' must be greater than or equal to 'min_corner' on X and Y"));
    }

    int32 MinX = 0;
    int32 MinY = 0;
    int32 MaxX = 0;
    int32 MaxY = 0;
    FString ErrorMessage;
    if (!TryResolveLandscapeRegionExtents(Landscape, MinCorner, MaxCorner, MinX, MinY, MaxX, MaxY, ErrorMessage))
    {
        return FUnrealAICommonUtils::CreateErrorResponse(ErrorMessage);
    }

    ULandscapeInfo* LandscapeInfo = Landscape->GetLandscapeInfo();
    if (!LandscapeInfo)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Unable to query landscape info"));
    }

    const int32 Width = MaxX - MinX + 1;
    const int32 Height = MaxY - MinY + 1;
    const FVector Scale = Landscape->GetActorScale3D();
    const float HeightLocal = Scale.Z == 0.0f ? 0.0f : static_cast<float>(HeightWorld / Scale.Z);
    const uint16 HeightValue = LandscapeDataAccess::GetTexHeight(HeightLocal);

    TArray<uint16> HeightData;
    HeightData.Init(HeightValue, Width * Height);

    FScopedTransaction Transaction(NSLOCTEXT("UnrealAI", "SculptLandscapeHeightRegion", "Sculpt Landscape Height Region"));
    Landscape->Modify();

    FHeightmapAccessor<false> HeightAccessor(LandscapeInfo);
    HeightAccessor.SetData(MinX, MinY, MaxX, MaxY, HeightData.GetData());
    HeightAccessor.Flush();

    Landscape->ForceLayersFullUpdate();
    Landscape->MarkPackageDirty();

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetBoolField(TEXT("success"), true);
    Result->SetStringField(TEXT("landscape_name"), Landscape->GetActorLabel());
    Result->SetNumberField(TEXT("height_world"), HeightWorld);
    AddNumberArray(Result, TEXT("affected_extent"), {
        static_cast<double>(MinX),
        static_cast<double>(MinY),
        static_cast<double>(MaxX),
        static_cast<double>(MaxY),
    });
    Result->SetNumberField(TEXT("width"), Width);
    Result->SetNumberField(TEXT("height"), Height);
    return Result;
}

TSharedPtr<FJsonObject> FUnrealAILandscapeCommands::HandleRebuildLandscape(const TSharedPtr<FJsonObject>& Params)
{
    FString LandscapeName;
    if (!Params->TryGetStringField(TEXT("landscape_name"), LandscapeName) || LandscapeName.IsEmpty())
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing 'landscape_name'"));
    }

    ALandscape* Landscape = FindLandscapeByName(LandscapeName);
    if (!Landscape)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(FString::Printf(TEXT("Landscape not found: %s"), *LandscapeName));
    }

    FScopedTransaction Transaction(NSLOCTEXT("UnrealAI", "RebuildLandscape", "Rebuild Landscape"));
    Landscape->Modify();
    Landscape->ForceLayersFullUpdate();
    Landscape->MarkPackageDirty();

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetBoolField(TEXT("success"), true);
    Result->SetStringField(TEXT("landscape_name"), Landscape->GetActorLabel());
    Result->SetBoolField(TEXT("rebuilt"), true);
    return Result;
}

TSharedPtr<FJsonObject> FUnrealAILandscapeCommands::HandleCreateLandscape(const TSharedPtr<FJsonObject>& Params)
{
    if (!GWorld)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("No active world"));
    }

    FString Name;
    Params->TryGetStringField(TEXT("name"), Name);
    if (!Name.IsEmpty() && FindLandscapeByName(Name))
    {
        return FUnrealAICommonUtils::CreateErrorResponse(FString::Printf(TEXT("Landscape already exists: %s"), *Name));
    }

    FVector Location = FVector::ZeroVector;
    if (Params->HasField(TEXT("location")))
    {
        Location = FUnrealAICommonUtils::GetVectorFromJson(Params, TEXT("location"));
    }

    FRotator Rotation = FRotator::ZeroRotator;
    if (Params->HasField(TEXT("rotation")))
    {
        Rotation = FUnrealAICommonUtils::GetRotatorFromJson(Params, TEXT("rotation"));
    }

    FVector Scale(100.0f, 100.0f, 100.0f);
    if (Params->HasField(TEXT("scale")))
    {
        Scale = FUnrealAICommonUtils::GetVectorFromJson(Params, TEXT("scale"));
    }

    int32 ComponentCountX = 4;
    int32 ComponentCountY = 4;
    int32 SectionsPerComponent = 1;
    int32 QuadsPerSection = 63;
    double BaseHeightWorld = 0.0;

    if (Params->HasField(TEXT("component_count_x"))) { ComponentCountX = Params->GetIntegerField(TEXT("component_count_x")); }
    if (Params->HasField(TEXT("component_count_y"))) { ComponentCountY = Params->GetIntegerField(TEXT("component_count_y")); }
    if (Params->HasField(TEXT("sections_per_component"))) { SectionsPerComponent = Params->GetIntegerField(TEXT("sections_per_component")); }
    if (Params->HasField(TEXT("quads_per_section"))) { QuadsPerSection = Params->GetIntegerField(TEXT("quads_per_section")); }
    Params->TryGetNumberField(TEXT("base_height_world"), BaseHeightWorld);

    if (ComponentCountX <= 0 || ComponentCountY <= 0 || SectionsPerComponent <= 0 || QuadsPerSection <= 0)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Landscape dimensions must be positive"));
    }

    FString MaterialPath;
    Params->TryGetStringField(TEXT("material_path"), MaterialPath);

    const int32 QuadsPerComponent = SectionsPerComponent * QuadsPerSection;
    const int32 SizeX = ComponentCountX * QuadsPerComponent + 1;
    const int32 SizeY = ComponentCountY * QuadsPerComponent + 1;
    const float BaseHeightLocal = Scale.Z == 0.0f ? 0.0f : static_cast<float>(BaseHeightWorld / Scale.Z);
    const uint16 HeightValue = LandscapeDataAccess::GetTexHeight(BaseHeightLocal);

    TArray<uint16> HeightData;
    HeightData.Init(HeightValue, SizeX * SizeY);

    TMap<FGuid, TArray<uint16>> HeightDataPerLayers;
    HeightDataPerLayers.Add(FGuid(), HeightData);

    TMap<FGuid, TArray<FLandscapeImportLayerInfo>> MaterialLayerDataPerLayers;
    MaterialLayerDataPerLayers.Add(FGuid(), TArray<FLandscapeImportLayerInfo>());

    FScopedTransaction Transaction(NSLOCTEXT("UnrealAI", "CreateLandscape", "Create Landscape"));

    const FVector Offset = FTransform(Rotation, FVector::ZeroVector, Scale).TransformVector(
        FVector(-ComponentCountX * QuadsPerComponent / 2.0, -ComponentCountY * QuadsPerComponent / 2.0, 0.0));

    ALandscape* Landscape = GWorld->SpawnActor<ALandscape>(Location + Offset, Rotation);
    if (!Landscape)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Failed to spawn landscape actor"));
    }

    if (!MaterialPath.IsEmpty())
    {
        if (UMaterialInterface* Material = LoadObject<UMaterialInterface>(nullptr, *MaterialPath))
        {
            Landscape->LandscapeMaterial = Material;
        }
        else
        {
            return FUnrealAICommonUtils::CreateErrorResponse(FString::Printf(TEXT("Unable to load material: %s"), *MaterialPath));
        }
    }

    Landscape->SetActorRelativeScale3D(Scale);
    Landscape->StaticLightingLOD = FMath::DivideAndRoundUp(FMath::CeilLogTwo((SizeX * SizeY) / (2048 * 2048) + 1), static_cast<uint32>(2));
    Landscape->Import(FGuid::NewGuid(), 0, 0, SizeX - 1, SizeY - 1, SectionsPerComponent, QuadsPerSection, HeightDataPerLayers, TEXT(""), MaterialLayerDataPerLayers, ELandscapeImportAlphamapType::Additive, TArrayView<const FLandscapeLayer>());

    if (!Name.IsEmpty())
    {
        Landscape->SetActorLabel(Name);
    }

    if (ULandscapeInfo* LandscapeInfo = Landscape->GetLandscapeInfo())
    {
        LandscapeInfo->UpdateLayerInfoMap(Landscape);
    }

    Landscape->ForceLayersFullUpdate();
    Landscape->MarkPackageDirty();

    TSharedPtr<FJsonObject> Result = LandscapeToJson(Landscape);
    Result->SetBoolField(TEXT("success"), true);
    Result->SetNumberField(TEXT("component_count_x"), ComponentCountX);
    Result->SetNumberField(TEXT("component_count_y"), ComponentCountY);
    Result->SetNumberField(TEXT("sections_per_component"), SectionsPerComponent);
    Result->SetNumberField(TEXT("quads_per_section"), QuadsPerSection);
    return Result;
}

TSharedPtr<FJsonObject> FUnrealAILandscapeCommands::HandleSetLandscapeFlatHeight(const TSharedPtr<FJsonObject>& Params)
{
    FString LandscapeName;
    if (!Params->TryGetStringField(TEXT("landscape_name"), LandscapeName))
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing 'landscape_name'"));
    }

    ALandscape* Landscape = FindLandscapeByName(LandscapeName);
    if (!Landscape)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(FString::Printf(TEXT("Landscape not found: %s"), *LandscapeName));
    }

    double HeightWorld = 0.0;
    Params->TryGetNumberField(TEXT("height_world"), HeightWorld);

    int32 MinX = 0;
    int32 MinY = 0;
    int32 MaxX = 0;
    int32 MaxY = 0;
    if (!GetLandscapeExtent(Landscape, MinX, MinY, MaxX, MaxY))
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Unable to query landscape extent"));
    }

    const int32 Width = MaxX - MinX + 1;
    const int32 Height = MaxY - MinY + 1;
    const FVector Scale = Landscape->GetActorScale3D();
    const float HeightLocal = Scale.Z == 0.0f ? 0.0f : static_cast<float>(HeightWorld / Scale.Z);
    const uint16 HeightValue = LandscapeDataAccess::GetTexHeight(HeightLocal);

    TArray<uint16> HeightData;
    HeightData.Init(HeightValue, Width * Height);

    FScopedTransaction Transaction(NSLOCTEXT("UnrealAI", "SetLandscapeFlatHeight", "Set Landscape Flat Height"));
    Landscape->Modify();

    if (!LandscapeEditorUtils::SetHeightmapData(Landscape, HeightData))
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("SetHeightmapData failed"));
    }

    Landscape->ForceLayersFullUpdate();
    Landscape->MarkPackageDirty();

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetBoolField(TEXT("success"), true);
    Result->SetStringField(TEXT("landscape_name"), Landscape->GetActorLabel());
    Result->SetNumberField(TEXT("height_world"), HeightWorld);
    Result->SetNumberField(TEXT("width"), Width);
    Result->SetNumberField(TEXT("height"), Height);
    return Result;
}

TSharedPtr<FJsonObject> FUnrealAILandscapeCommands::HandleImportLandscapeHeightmap(const TSharedPtr<FJsonObject>& Params)
{
    FString LandscapeName;
    FString SourcePath;
    bool bFlipYAxis = false;

    if (!Params->TryGetStringField(TEXT("landscape_name"), LandscapeName))
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing 'landscape_name'"));
    }
    if (!Params->TryGetStringField(TEXT("source_path"), SourcePath))
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing 'source_path'"));
    }
    Params->TryGetBoolField(TEXT("flip_y_axis"), bFlipYAxis);

    if (!FPaths::FileExists(SourcePath))
    {
        return FUnrealAICommonUtils::CreateErrorResponse(FString::Printf(TEXT("Heightmap file not found: %s"), *SourcePath));
    }

    ALandscape* Landscape = FindLandscapeByName(LandscapeName);
    if (!Landscape)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(FString::Printf(TEXT("Landscape not found: %s"), *LandscapeName));
    }

    int32 MinX = 0;
    int32 MinY = 0;
    int32 MaxX = 0;
    int32 MaxY = 0;
    if (!GetLandscapeExtent(Landscape, MinX, MinY, MaxX, MaxY))
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Unable to query landscape extent"));
    }

    const int32 Width = MaxX - MinX + 1;
    const int32 Height = MaxY - MinY + 1;

    FLandscapeImportDescriptor ImportDescriptor;
    FText Message;
    const ELandscapeImportResult DescriptorResult = FLandscapeImportHelper::GetHeightmapImportDescriptor(SourcePath, true, bFlipYAxis, ImportDescriptor, Message);
    if (DescriptorResult == ELandscapeImportResult::Error || ImportDescriptor.ImportResolutions.Num() == 0)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(FString::Printf(TEXT("Unable to read heightmap descriptor: %s"), *Message.ToString()));
    }

    int32 DescriptorIndex = ImportDescriptor.FindDescriptorIndex(Width, Height);
    if (DescriptorIndex == INDEX_NONE)
    {
        DescriptorIndex = 0;
    }

    TArray<uint16> ImportedData;
    const ELandscapeImportResult ImportResult = FLandscapeImportHelper::GetHeightmapImportData(ImportDescriptor, DescriptorIndex, ImportedData, Message);
    if (ImportResult == ELandscapeImportResult::Error)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(FString::Printf(TEXT("Unable to import heightmap data: %s"), *Message.ToString()));
    }

    TArray<uint16> FinalData;
    const FLandscapeImportResolution CurrentResolution = ImportDescriptor.ImportResolutions[DescriptorIndex];
    if (static_cast<int32>(CurrentResolution.Width) == Width && static_cast<int32>(CurrentResolution.Height) == Height)
    {
        FinalData = MoveTemp(ImportedData);
    }
    else
    {
        FLandscapeImportHelper::TransformHeightmapImportData(
            ImportedData,
            FinalData,
            CurrentResolution,
            FLandscapeImportResolution(Width, Height),
            ELandscapeImportTransformType::Resample);
    }

    FScopedTransaction Transaction(NSLOCTEXT("UnrealAI", "ImportLandscapeHeightmap", "Import Landscape Heightmap"));
    Landscape->Modify();

    if (!LandscapeEditorUtils::SetHeightmapData(Landscape, FinalData))
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("SetHeightmapData failed after import"));
    }

    Landscape->ForceLayersFullUpdate();
    Landscape->MarkPackageDirty();

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetBoolField(TEXT("success"), true);
    Result->SetStringField(TEXT("landscape_name"), Landscape->GetActorLabel());
    Result->SetStringField(TEXT("source_path"), SourcePath);
    Result->SetNumberField(TEXT("width"), Width);
    Result->SetNumberField(TEXT("height"), Height);
    Result->SetBoolField(TEXT("resampled"), static_cast<int32>(CurrentResolution.Width) != Width || static_cast<int32>(CurrentResolution.Height) != Height);
    return Result;
}