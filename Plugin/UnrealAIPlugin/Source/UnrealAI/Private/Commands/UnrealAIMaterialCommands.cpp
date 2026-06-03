#include "Commands/UnrealAIMaterialCommands.h"

#include "AssetToolsModule.h"
#include "Commands/UnrealAICommonUtils.h"
#include "EditorAssetLibrary.h"
#include "Factories/MaterialFunctionFactoryNew.h"
#include "Factories/MaterialFactoryNew.h"
#include "IAssetTools.h"
#include "MaterialEditingLibrary.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpression.h"
#include "Materials/MaterialExpressionComment.h"
#include "Materials/MaterialExpressionFunctionOutput.h"
#include "Materials/MaterialFunction.h"
#include "Materials/MaterialFunctionInterface.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Engine/Texture.h"
#include "UObject/UObjectIterator.h"

namespace
{
    struct FMaterialExpressionConnectionRef
    {
        TObjectPtr<UMaterialExpression> Expression;
        FString OutputName;
        FString InputName;
    };

    struct FMaterialPropertyConnectionRef
    {
        EMaterialProperty Property;
        FString PropertyName;
        FString OutputName;
    };

    struct FMaterialPropertyEntry
    {
        EMaterialProperty Property;
        const TCHAR* Name;
    };

    constexpr FMaterialPropertyEntry MaterialProperties[] = {
        {MP_BaseColor, TEXT("BaseColor")},
        {MP_Metallic, TEXT("Metallic")},
        {MP_Specular, TEXT("Specular")},
        {MP_Roughness, TEXT("Roughness")},
        {MP_Anisotropy, TEXT("Anisotropy")},
        {MP_EmissiveColor, TEXT("EmissiveColor")},
        {MP_Normal, TEXT("Normal")},
        {MP_Tangent, TEXT("Tangent")},
        {MP_WorldPositionOffset, TEXT("WorldPositionOffset")},
        {MP_SubsurfaceColor, TEXT("SubsurfaceColor")},
        {MP_CustomData0, TEXT("CustomData0")},
        {MP_CustomData1, TEXT("CustomData1")},
        {MP_AmbientOcclusion, TEXT("AmbientOcclusion")},
        {MP_Refraction, TEXT("Refraction")},
        {MP_Opacity, TEXT("Opacity")},
        {MP_OpacityMask, TEXT("OpacityMask")},
        {MP_PixelDepthOffset, TEXT("PixelDepthOffset")},
    };

    struct FResolvedMaterialGraph
    {
        FString RequestedPath;
        UMaterialInterface* MaterialInterface = nullptr;
        UMaterial* Material = nullptr;
        UMaterialFunction* MaterialFunction = nullptr;

        bool IsMaterial() const
        {
            return Material != nullptr;
        }

        bool IsMaterialFunction() const
        {
            return MaterialFunction != nullptr;
        }

        FString GetGraphAssetType() const
        {
            return IsMaterialFunction() ? TEXT("material_function") : TEXT("material");
        }

        FString GetGraphPath() const
        {
            if (MaterialFunction)
            {
                return MaterialFunction->GetPathName();
            }
            if (Material)
            {
                return Material->GetPathName();
            }
            return RequestedPath;
        }
    };

    FString GuidToString(const FGuid& Guid)
    {
        return Guid.ToString(EGuidFormats::DigitsWithHyphens);
    }

    FString ParameterAssociationToString(EMaterialParameterAssociation Association)
    {
        if (const UEnum* Enum = StaticEnum<EMaterialParameterAssociation>())
        {
            return Enum->GetNameStringByValue(static_cast<int64>(Association));
        }
        return FString::FromInt(static_cast<int32>(Association));
    }

    FString BuildParameterIdentityKey(const FMaterialParameterInfo& Info)
    {
        return FString::Printf(
            TEXT("%s|%d|%d"),
            *Info.Name.ToString(),
            static_cast<int32>(Info.Association),
            Info.Index);
    }

    bool ShouldIgnoreValidationExpression(UMaterialExpression* Expression)
    {
        return Expression && Expression->IsA<UMaterialExpressionComment>();
    }

    TSharedPtr<FJsonObject> BuildValidationIssue(
        const FString& Severity,
        const FString& Code,
        const FString& Message)
    {
        TSharedPtr<FJsonObject> Issue = MakeShared<FJsonObject>();
        Issue->SetStringField(TEXT("severity"), Severity);
        Issue->SetStringField(TEXT("code"), Code);
        Issue->SetStringField(TEXT("message"), Message);
        return Issue;
    }

    bool ResolveMaterialInputs(
        const TSharedPtr<FJsonObject>& Params,
        FString& OutRequestedPath,
        UMaterialInterface*& OutMaterialInterface,
        UMaterial*& OutMaterial,
        TSharedPtr<FJsonObject>& OutError)
    {
        OutMaterialInterface = nullptr;
        OutMaterial = nullptr;
        OutError.Reset();

        if (!Params->TryGetStringField(TEXT("material_path"), OutRequestedPath) || OutRequestedPath.IsEmpty())
        {
            OutError = FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing 'material_path' parameter"));
            return false;
        }

        UObject* LoadedAsset = UEditorAssetLibrary::LoadAsset(OutRequestedPath);
        if (Cast<UMaterialFunctionInterface>(LoadedAsset))
        {
            OutError = FUnrealAICommonUtils::CreateErrorResponse(
                FString::Printf(TEXT("Material function assets are not supported by get_material_parameters: %s"), *OutRequestedPath));
            return false;
        }

        OutMaterialInterface = Cast<UMaterialInterface>(LoadedAsset);
        if (!OutMaterialInterface)
        {
            OutError = FUnrealAICommonUtils::CreateErrorResponse(
                FString::Printf(TEXT("Failed to load material interface: %s"), *OutRequestedPath));
            return false;
        }

        OutMaterial = OutMaterialInterface->GetMaterial();
        if (!OutMaterial)
        {
            OutError = FUnrealAICommonUtils::CreateErrorResponse(
                FString::Printf(TEXT("Unable to resolve base material graph for: %s"), *OutRequestedPath));
            return false;
        }

        return true;
    }

    bool ResolveReadableMaterialGraph(
        const TSharedPtr<FJsonObject>& Params,
        FResolvedMaterialGraph& OutGraph,
        TSharedPtr<FJsonObject>& OutError)
    {
        OutGraph = FResolvedMaterialGraph();
        OutError.Reset();

        if (!Params->TryGetStringField(TEXT("material_path"), OutGraph.RequestedPath) || OutGraph.RequestedPath.IsEmpty())
        {
            OutError = FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing 'material_path' parameter"));
            return false;
        }

        UObject* LoadedAsset = UEditorAssetLibrary::LoadAsset(OutGraph.RequestedPath);
        if (UMaterialFunction* MaterialFunction = Cast<UMaterialFunction>(LoadedAsset))
        {
            OutGraph.MaterialFunction = MaterialFunction;
            return true;
        }

        OutGraph.MaterialInterface = Cast<UMaterialInterface>(LoadedAsset);
        if (!OutGraph.MaterialInterface)
        {
            OutError = FUnrealAICommonUtils::CreateErrorResponse(
                FString::Printf(TEXT("Failed to load material graph asset: %s"), *OutGraph.RequestedPath));
            return false;
        }

        OutGraph.Material = OutGraph.MaterialInterface->GetMaterial();
        if (!OutGraph.Material)
        {
            OutError = FUnrealAICommonUtils::CreateErrorResponse(
                FString::Printf(TEXT("Unable to resolve base material graph for: %s"), *OutGraph.RequestedPath));
            return false;
        }

        return true;
    }

    bool ResolveEditableMaterialGraph(
        const TSharedPtr<FJsonObject>& Params,
        FResolvedMaterialGraph& OutGraph,
        TSharedPtr<FJsonObject>& OutError)
    {
        OutGraph = FResolvedMaterialGraph();
        OutError.Reset();

        if (!Params->TryGetStringField(TEXT("material_path"), OutGraph.RequestedPath) || OutGraph.RequestedPath.IsEmpty())
        {
            OutError = FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing 'material_path' parameter"));
            return false;
        }

        UObject* LoadedAsset = UEditorAssetLibrary::LoadAsset(OutGraph.RequestedPath);
        OutGraph.Material = Cast<UMaterial>(LoadedAsset);
        if (OutGraph.Material)
        {
            OutGraph.MaterialInterface = OutGraph.Material;
            return true;
        }

        OutGraph.MaterialFunction = Cast<UMaterialFunction>(LoadedAsset);
        if (OutGraph.MaterialFunction)
        {
            return true;
        }

        OutError = FUnrealAICommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Expected a base material or material function asset, got: %s"), *OutGraph.RequestedPath));
        return false;
    }

    bool ResolveEditableMaterial(
        const TSharedPtr<FJsonObject>& Params,
        FString& OutRequestedPath,
        UMaterial*& OutMaterial,
        TSharedPtr<FJsonObject>& OutError)
    {
        OutMaterial = nullptr;
        OutError.Reset();

        if (!Params->TryGetStringField(TEXT("material_path"), OutRequestedPath) || OutRequestedPath.IsEmpty())
        {
            OutError = FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing 'material_path' parameter"));
            return false;
        }

        UObject* LoadedAsset = UEditorAssetLibrary::LoadAsset(OutRequestedPath);
        OutMaterial = Cast<UMaterial>(LoadedAsset);
        if (!OutMaterial)
        {
            OutError = FUnrealAICommonUtils::CreateErrorResponse(
                FString::Printf(TEXT("Expected a base material asset, got: %s"), *OutRequestedPath));
            return false;
        }

        return true;
    }

    bool ResolveEditableMaterialInstance(
        const TSharedPtr<FJsonObject>& Params,
        FString& OutRequestedPath,
        UMaterialInstanceConstant*& OutMaterialInstance,
        TSharedPtr<FJsonObject>& OutError)
    {
        OutMaterialInstance = nullptr;
        OutError.Reset();

        if (!Params->TryGetStringField(TEXT("material_path"), OutRequestedPath) || OutRequestedPath.IsEmpty())
        {
            OutError = FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing 'material_path' parameter"));
            return false;
        }

        UObject* LoadedAsset = UEditorAssetLibrary::LoadAsset(OutRequestedPath);
        OutMaterialInstance = Cast<UMaterialInstanceConstant>(LoadedAsset);
        if (!OutMaterialInstance)
        {
            OutError = FUnrealAICommonUtils::CreateErrorResponse(
                FString::Printf(TEXT("Expected a material instance constant asset, got: %s"), *OutRequestedPath));
            return false;
        }

        return true;
    }

    UClass* ResolveExpressionClass(const FString& ExpressionClassName)
    {
        for (TObjectIterator<UClass> It; It; ++It)
        {
            UClass* Candidate = *It;
            if (!Candidate->IsChildOf(UMaterialExpression::StaticClass()))
            {
                continue;
            }

            if (Candidate->GetName().Equals(ExpressionClassName, ESearchCase::IgnoreCase) ||
                Candidate->GetPathName().Equals(ExpressionClassName, ESearchCase::IgnoreCase))
            {
                return Candidate;
            }
        }

        return nullptr;
    }

    UMaterialExpression* ResolveMaterialExpression(
        const FResolvedMaterialGraph& Graph,
        const FString& ExpressionId,
        const FString& ExpressionName)
    {
        FGuid ParsedGuid;
        const bool bHasGuid = !ExpressionId.IsEmpty() && FGuid::Parse(ExpressionId, ParsedGuid);

        TArray<UMaterialExpression*> Expressions;
        if (Graph.MaterialFunction)
        {
            for (UMaterialExpression* Expression : Graph.MaterialFunction->GetExpressions())
            {
                if (Expression)
                {
                    Expressions.Add(Expression);
                }
            }
        }
        else if (Graph.Material)
        {
            for (UMaterialExpression* Expression : Graph.Material->GetExpressions())
            {
                if (Expression)
                {
                    Expressions.Add(Expression);
                }
            }
        }

        for (UMaterialExpression* Expression : Expressions)
        {
            if (bHasGuid && Expression->MaterialExpressionGuid == ParsedGuid)
            {
                return Expression;
            }

            if (!ExpressionName.IsEmpty() && Expression->GetName().Equals(ExpressionName, ESearchCase::IgnoreCase))
            {
                return Expression;
            }
        }

        return nullptr;
    }

    UMaterialExpression* ResolveMaterialExpression(
        const FResolvedMaterialGraph& Graph,
        const TSharedPtr<FJsonObject>& Params,
        const FString& IdField,
        const FString& NameField)
    {
        FString ExpressionId;
        FString ExpressionName;
        Params->TryGetStringField(*IdField, ExpressionId);
        Params->TryGetStringField(*NameField, ExpressionName);
        return ResolveMaterialExpression(Graph, ExpressionId, ExpressionName);
    }

    bool TryResolveMaterialProperty(const FString& PropertyName, EMaterialProperty& OutProperty)
    {
        for (const FMaterialPropertyEntry& Entry : MaterialProperties)
        {
            if (PropertyName.Equals(Entry.Name, ESearchCase::IgnoreCase))
            {
                OutProperty = Entry.Property;
                return true;
            }
        }

        return false;
    }

    TSharedPtr<FJsonObject> BuildMaterialExpressionRef(UMaterialExpression* Expression)
    {
        TSharedPtr<FJsonObject> Ref = MakeShared<FJsonObject>();
        Ref->SetStringField(TEXT("expression_id"), GuidToString(Expression->MaterialExpressionGuid));
        Ref->SetStringField(TEXT("expression_name"), Expression->GetName());
        Ref->SetStringField(TEXT("expression_class"), Expression->GetClass()->GetName());
        Ref->SetNumberField(TEXT("node_pos_x"), Expression->MaterialExpressionEditorX);
        Ref->SetNumberField(TEXT("node_pos_y"), Expression->MaterialExpressionEditorY);
        return Ref;
    }

    FString GetMaterialExpressionOutputName(UMaterialExpression* Expression, int32 OutputIndex)
    {
        if (!Expression)
        {
            return FString();
        }

        if (FExpressionOutput* Output = Expression->GetOutput(OutputIndex))
        {
            return Output->OutputName.ToString();
        }

        return FString();
    }

    void ClearMaterialExpressionInput(FExpressionInput* Input)
    {
        if (!Input)
        {
            return;
        }

        Input->Expression = nullptr;
        Input->OutputIndex = 0;
        Input->Mask = 0;
        Input->MaskR = 0;
        Input->MaskG = 0;
        Input->MaskB = 0;
        Input->MaskA = 0;
    }

    bool ResolveMaterialExpressionInput(
        UMaterialExpression* Expression,
        const FString& RequestedInputName,
        int32& OutInputIndex,
        FExpressionInput*& OutInput,
        FString& OutResolvedInputName,
        TSharedPtr<FJsonObject>& OutError)
    {
        OutInputIndex = INDEX_NONE;
        OutInput = nullptr;
        OutResolvedInputName.Reset();
        OutError.Reset();

        if (!Expression)
        {
            OutError = FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing destination material expression"));
            return false;
        }

        const int32 InputCount = Expression->CountInputs();
        if (InputCount <= 0)
        {
            OutError = FUnrealAICommonUtils::CreateErrorResponse(
                FString::Printf(TEXT("Expression '%s' has no editable inputs"), *Expression->GetName()));
            return false;
        }

        auto TryUseInputIndex = [&](int32 InputIndex) -> bool
        {
            FExpressionInput* Candidate = Expression->GetInput(InputIndex);
            if (!Candidate)
            {
                return false;
            }

            OutInputIndex = InputIndex;
            OutInput = Candidate;
            OutResolvedInputName = Expression->GetInputName(InputIndex).ToString();
            return true;
        };

        if (!RequestedInputName.IsEmpty())
        {
            for (int32 InputIndex = 0; InputIndex < InputCount; ++InputIndex)
            {
                if (!Expression->GetInputName(InputIndex).ToString().Equals(RequestedInputName, ESearchCase::IgnoreCase))
                {
                    continue;
                }

                if (TryUseInputIndex(InputIndex))
                {
                    return true;
                }

                OutError = FUnrealAICommonUtils::CreateErrorResponse(
                    FString::Printf(TEXT("Input '%s' is not editable on expression '%s'"), *RequestedInputName, *Expression->GetName()));
                return false;
            }

            OutError = FUnrealAICommonUtils::CreateErrorResponse(
                FString::Printf(TEXT("Expression '%s' has no input named '%s'"), *Expression->GetName(), *RequestedInputName));
            return false;
        }

        for (int32 InputIndex = 0; InputIndex < InputCount; ++InputIndex)
        {
            FExpressionInput* Candidate = Expression->GetInput(InputIndex);
            if (!Candidate || !Candidate->Expression)
            {
                continue;
            }

            OutInputIndex = InputIndex;
            OutInput = Candidate;
            OutResolvedInputName = Expression->GetInputName(InputIndex).ToString();
            return true;
        }

        for (int32 InputIndex = 0; InputIndex < InputCount; ++InputIndex)
        {
            if (TryUseInputIndex(InputIndex))
            {
                return true;
            }
        }

        OutError = FUnrealAICommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Expression '%s' has no editable inputs"), *Expression->GetName()));
        return false;
    }

    void SaveEditorAsset(UObject* Asset)
    {
        if (Asset)
        {
            UEditorAssetLibrary::SaveLoadedAsset(Asset, false);
        }
    }

    void GatherGraphExpressions(const FResolvedMaterialGraph& Graph, TArray<UMaterialExpression*>& OutExpressions)
    {
        OutExpressions.Reset();

        if (Graph.MaterialFunction)
        {
            for (UMaterialExpression* Expression : Graph.MaterialFunction->GetExpressions())
            {
                if (Expression)
                {
                    OutExpressions.Add(Expression);
                }
            }
            return;
        }

        if (Graph.Material)
        {
            for (UMaterialExpression* Expression : Graph.Material->GetExpressions())
            {
                if (Expression)
                {
                    OutExpressions.Add(Expression);
                }
            }
        }
    }

    UMaterialExpression* CreateGraphExpression(
        const FResolvedMaterialGraph& Graph,
        UClass* ExpressionClass,
        int32 NodePosX,
        int32 NodePosY)
    {
        if (Graph.MaterialFunction)
        {
            return UMaterialEditingLibrary::CreateMaterialExpressionInFunction(
                Graph.MaterialFunction,
                ExpressionClass,
                NodePosX,
                NodePosY);
        }

        if (Graph.Material)
        {
            return UMaterialEditingLibrary::CreateMaterialExpression(
                Graph.Material,
                ExpressionClass,
                NodePosX,
                NodePosY);
        }

        return nullptr;
    }

    void DeleteGraphExpression(const FResolvedMaterialGraph& Graph, UMaterialExpression* Expression)
    {
        if (!Expression)
        {
            return;
        }

        if (Graph.MaterialFunction)
        {
            UMaterialEditingLibrary::DeleteMaterialExpressionInFunction(Graph.MaterialFunction, Expression);
            return;
        }

        if (Graph.Material)
        {
            UMaterialEditingLibrary::DeleteMaterialExpression(Graph.Material, Expression);
        }
    }

    void FinalizeGraphEdit(const FResolvedMaterialGraph& Graph)
    {
        if (Graph.MaterialFunction)
        {
            UMaterialEditingLibrary::UpdateMaterialFunction(Graph.MaterialFunction);
            Graph.MaterialFunction->PostEditChange();
            Graph.MaterialFunction->MarkPackageDirty();
            SaveEditorAsset(Graph.MaterialFunction);
            return;
        }

        if (Graph.Material)
        {
            Graph.Material->PostEditChange();
            Graph.Material->MarkPackageDirty();
            SaveEditorAsset(Graph.Material);
        }
    }

    void AddResolvedGraphMetadata(const FResolvedMaterialGraph& Graph, const TSharedPtr<FJsonObject>& Result)
    {
        Result->SetStringField(TEXT("requested_material_path"), Graph.RequestedPath);
        Result->SetStringField(TEXT("graph_material_path"), Graph.GetGraphPath());
        Result->SetStringField(TEXT("graph_asset_type"), Graph.GetGraphAssetType());
    }

    TSharedPtr<FJsonObject> BuildExpressionJson(UMaterialExpression* Expression)
    {
        TSharedPtr<FJsonObject> ExpressionObject = MakeShared<FJsonObject>();
        ExpressionObject->SetStringField(TEXT("id"), GuidToString(Expression->MaterialExpressionGuid));
        ExpressionObject->SetStringField(TEXT("guid"), GuidToString(Expression->MaterialExpressionGuid));
        ExpressionObject->SetStringField(TEXT("name"), Expression->GetName());
        ExpressionObject->SetStringField(TEXT("class"), Expression->GetClass()->GetName());
        ExpressionObject->SetStringField(TEXT("description"), Expression->Desc);
        ExpressionObject->SetBoolField(TEXT("is_parameter_expression"), Expression->bIsParameterExpression != 0);

        TArray<TSharedPtr<FJsonValue>> Position;
        Position.Add(MakeShared<FJsonValueNumber>(Expression->MaterialExpressionEditorX));
        Position.Add(MakeShared<FJsonValueNumber>(Expression->MaterialExpressionEditorY));
        ExpressionObject->SetArrayField(TEXT("editor_position"), Position);

        TArray<TSharedPtr<FJsonValue>> InputNames;
        const int32 InputCount = Expression->CountInputs();
        for (int32 InputIndex = 0; InputIndex < InputCount; ++InputIndex)
        {
            InputNames.Add(MakeShared<FJsonValueString>(Expression->GetInputName(InputIndex).ToString()));
        }
        ExpressionObject->SetArrayField(TEXT("input_names"), InputNames);
        ExpressionObject->SetNumberField(TEXT("input_count"), InputCount);

        TArray<TSharedPtr<FJsonValue>> OutputNames;
        for (FExpressionOutput& Output : Expression->GetOutputs())
        {
            OutputNames.Add(MakeShared<FJsonValueString>(Output.OutputName.ToString()));
        }
        ExpressionObject->SetArrayField(TEXT("output_names"), OutputNames);
        ExpressionObject->SetNumberField(TEXT("output_count"), OutputNames.Num());

        return ExpressionObject;
    }

    TSharedPtr<FJsonValue> BuildParameterValueJson(EMaterialParameterType ParameterType, const FMaterialParameterValue& Value)
    {
        switch (ParameterType)
        {
        case EMaterialParameterType::Scalar:
            return MakeShared<FJsonValueNumber>(Value.AsScalar());

        case EMaterialParameterType::Vector:
        {
            const FLinearColor Color = Value.AsLinearColor();
            TSharedPtr<FJsonObject> ColorObject = MakeShared<FJsonObject>();
            ColorObject->SetNumberField(TEXT("r"), Color.R);
            ColorObject->SetNumberField(TEXT("g"), Color.G);
            ColorObject->SetNumberField(TEXT("b"), Color.B);
            ColorObject->SetNumberField(TEXT("a"), Color.A);
            return MakeShared<FJsonValueObject>(ColorObject);
        }

        case EMaterialParameterType::Texture:
        {
            if (UObject* TextureObject = Value.AsTextureObject())
            {
                return MakeShared<FJsonValueString>(TextureObject->GetPathName());
            }
            return MakeShared<FJsonValueNull>();
        }

        case EMaterialParameterType::StaticSwitch:
            return MakeShared<FJsonValueBoolean>(Value.AsStaticSwitch());

        default:
            return MakeShared<FJsonValueNull>();
        }
    }

    void AddParameterMetadata(
        UMaterialInterface* MaterialInterface,
        EMaterialParameterType ParameterType,
        const FMaterialParameterInfo& Info,
        const TSharedPtr<FJsonObject>& ParameterObject)
    {
        FMaterialParameterMetadata Metadata;
        const bool bHasValue = MaterialInterface->GetParameterValue(
            ParameterType,
            FMemoryImageMaterialParameterInfo(Info),
            Metadata);

        ParameterObject->SetBoolField(TEXT("has_default_value"), bHasValue);
        if (!bHasValue)
        {
            return;
        }

        ParameterObject->SetField(TEXT("default_value"), BuildParameterValueJson(ParameterType, Metadata.Value));

#if WITH_EDITORONLY_DATA
        ParameterObject->SetBoolField(TEXT("is_override"), Metadata.bOverride);

        if (!Metadata.Group.IsNone())
        {
            ParameterObject->SetStringField(TEXT("group"), Metadata.Group.ToString());
        }

        if (!Metadata.Description.IsEmpty())
        {
            ParameterObject->SetStringField(TEXT("description"), Metadata.Description);
        }

        if (!Metadata.AssetPath.IsEmpty())
        {
            ParameterObject->SetStringField(TEXT("source_asset_path"), Metadata.AssetPath);
        }

        if (Metadata.ExpressionGuid.IsValid())
        {
            ParameterObject->SetStringField(TEXT("expression_guid"), GuidToString(Metadata.ExpressionGuid));
        }

        if (ParameterType == EMaterialParameterType::Scalar)
        {
            TSharedPtr<FJsonObject> SliderRange = MakeShared<FJsonObject>();
            SliderRange->SetNumberField(TEXT("min"), Metadata.ScalarMin);
            SliderRange->SetNumberField(TEXT("max"), Metadata.ScalarMax);
            ParameterObject->SetObjectField(TEXT("slider_range"), SliderRange);
        }
#endif
    }

    void AddParameterInfos(
        UMaterialInterface* MaterialInterface,
        EMaterialParameterType ParameterType,
        const FString& TypeName,
        const TArray<FMaterialParameterInfo>& Infos,
        const TArray<FGuid>& Ids,
        TArray<TSharedPtr<FJsonValue>>& OutParameters)
    {
        for (int32 Index = 0; Index < Infos.Num(); ++Index)
        {
            const FMaterialParameterInfo& Info = Infos[Index];

            TSharedPtr<FJsonObject> ParameterObject = MakeShared<FJsonObject>();
            ParameterObject->SetStringField(TEXT("name"), Info.Name.ToString());
            ParameterObject->SetStringField(TEXT("type"), TypeName);
            ParameterObject->SetStringField(TEXT("association"), ParameterAssociationToString(Info.Association));
            ParameterObject->SetNumberField(TEXT("association_index"), static_cast<int32>(Info.Association));
            ParameterObject->SetNumberField(TEXT("layer_index"), Info.Index);
            if (Ids.IsValidIndex(Index))
            {
                ParameterObject->SetStringField(TEXT("id"), GuidToString(Ids[Index]));
            }

            AddParameterMetadata(MaterialInterface, ParameterType, Info, ParameterObject);

            OutParameters.Add(MakeShared<FJsonValueObject>(ParameterObject));
        }
    }

    void CollectMatchingParameterInfos(
        const TArray<FMaterialParameterInfo>& Infos,
        const FString& ParameterName,
        TArray<FMaterialParameterInfo>& OutInfos)
    {
        OutInfos.Reset();

        for (const FMaterialParameterInfo& Info : Infos)
        {
            if (Info.Name.ToString().Equals(ParameterName, ESearchCase::IgnoreCase))
            {
                OutInfos.Add(Info);
            }
        }
    }
}

FUnrealAIMaterialCommands::FUnrealAIMaterialCommands()
{
}

TSharedPtr<FJsonObject> FUnrealAIMaterialCommands::HandleCommand(const FString& CommandType, const TSharedPtr<FJsonObject>& Params)
{
    if (CommandType == TEXT("create_material_asset"))
    {
        return HandleCreateMaterialAsset(Params);
    }
    if (CommandType == TEXT("create_material_function_asset"))
    {
        return HandleCreateMaterialFunctionAsset(Params);
    }
    if (CommandType == TEXT("get_material_expressions"))
    {
        return HandleGetMaterialExpressions(Params);
    }
    if (CommandType == TEXT("get_material_connections"))
    {
        return HandleGetMaterialConnections(Params);
    }
    if (CommandType == TEXT("get_material_parameters"))
    {
        return HandleGetMaterialParameters(Params);
    }
    if (CommandType == TEXT("set_material_parameters"))
    {
        return HandleSetMaterialParameters(Params);
    }
    if (CommandType == TEXT("set_material_instance_parameters"))
    {
        return HandleSetMaterialInstanceParameters(Params);
    }
    if (CommandType == TEXT("validate_material_graph"))
    {
        return HandleValidateMaterialGraph(Params);
    }
    if (CommandType == TEXT("create_material_expression"))
    {
        return HandleCreateMaterialExpression(Params);
    }
    if (CommandType == TEXT("delete_material_expression"))
    {
        return HandleDeleteMaterialExpression(Params);
    }
    if (CommandType == TEXT("delete_material_expressions"))
    {
        return HandleDeleteMaterialExpressions(Params);
    }
    if (CommandType == TEXT("replace_material_expression"))
    {
        return HandleReplaceMaterialExpression(Params);
    }
    if (CommandType == TEXT("connect_material_expressions"))
    {
        return HandleConnectMaterialExpressions(Params);
    }
    if (CommandType == TEXT("disconnect_material_expressions"))
    {
        return HandleDisconnectMaterialExpressions(Params);
    }
    if (CommandType == TEXT("connect_material_property"))
    {
        return HandleConnectMaterialProperty(Params);
    }
    if (CommandType == TEXT("disconnect_material_property"))
    {
        return HandleDisconnectMaterialProperty(Params);
    }
    if (CommandType == TEXT("recompile_material"))
    {
        return HandleRecompileMaterial(Params);
    }
    if (CommandType == TEXT("layout_material_expressions"))
    {
        return HandleLayoutMaterialExpressions(Params);
    }

    return FUnrealAICommonUtils::CreateErrorResponse(
        FString::Printf(TEXT("Unknown material command: %s"), *CommandType));
}

TSharedPtr<FJsonObject> FUnrealAIMaterialCommands::HandleCreateMaterialAsset(const TSharedPtr<FJsonObject>& Params)
{
    FString MaterialName;
    if (!Params->TryGetStringField(TEXT("material_name"), MaterialName) || MaterialName.IsEmpty())
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing 'material_name' parameter"));
    }

    FString DestPath = TEXT("/Game/Materials");
    Params->TryGetStringField(TEXT("dest_path"), DestPath);

    if (!UEditorAssetLibrary::DoesDirectoryExist(DestPath))
    {
        UEditorAssetLibrary::MakeDirectory(DestPath);
    }

    const FString FullObjectPath = FString::Printf(TEXT("%s/%s.%s"), *DestPath, *MaterialName, *MaterialName);
    if (UEditorAssetLibrary::DoesAssetExist(FullObjectPath))
    {
        return FUnrealAICommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Asset already exists: %s"), *FullObjectPath));
    }

    FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools"));
    IAssetTools& AssetTools = AssetToolsModule.Get();

    UMaterialFactoryNew* Factory = NewObject<UMaterialFactoryNew>();
    UObject* NewAsset = AssetTools.CreateAsset(MaterialName, DestPath, UMaterial::StaticClass(), Factory);
    UMaterial* Material = Cast<UMaterial>(NewAsset);
    if (!Material)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Failed to create material asset"));
    }

    SaveEditorAsset(Material);

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetBoolField(TEXT("success"), true);
    Result->SetStringField(TEXT("material_path"), Material->GetPathName());
    Result->SetStringField(TEXT("material_name"), Material->GetName());
    return Result;
}

TSharedPtr<FJsonObject> FUnrealAIMaterialCommands::HandleCreateMaterialFunctionAsset(const TSharedPtr<FJsonObject>& Params)
{
    FString MaterialFunctionName;
    if (!Params->TryGetStringField(TEXT("material_function_name"), MaterialFunctionName) || MaterialFunctionName.IsEmpty())
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing 'material_function_name' parameter"));
    }

    FString DestPath = TEXT("/Game/MaterialFunctions");
    Params->TryGetStringField(TEXT("dest_path"), DestPath);

    if (!UEditorAssetLibrary::DoesDirectoryExist(DestPath))
    {
        UEditorAssetLibrary::MakeDirectory(DestPath);
    }

    const FString FullObjectPath = FString::Printf(TEXT("%s/%s.%s"), *DestPath, *MaterialFunctionName, *MaterialFunctionName);
    if (UEditorAssetLibrary::DoesAssetExist(FullObjectPath))
    {
        return FUnrealAICommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Asset already exists: %s"), *FullObjectPath));
    }

    FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools"));
    IAssetTools& AssetTools = AssetToolsModule.Get();

    UMaterialFunctionFactoryNew* Factory = NewObject<UMaterialFunctionFactoryNew>();
    UObject* NewAsset = AssetTools.CreateAsset(MaterialFunctionName, DestPath, UMaterialFunction::StaticClass(), Factory);
    UMaterialFunction* MaterialFunction = Cast<UMaterialFunction>(NewAsset);
    if (!MaterialFunction)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Failed to create material function asset"));
    }

    SaveEditorAsset(MaterialFunction);

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetBoolField(TEXT("success"), true);
    Result->SetStringField(TEXT("material_function_path"), MaterialFunction->GetPathName());
    Result->SetStringField(TEXT("material_function_name"), MaterialFunction->GetName());
    return Result;
}

TSharedPtr<FJsonObject> FUnrealAIMaterialCommands::HandleGetMaterialExpressions(const TSharedPtr<FJsonObject>& Params)
{
    FResolvedMaterialGraph Graph;
    TSharedPtr<FJsonObject> Error;
    if (!ResolveReadableMaterialGraph(Params, Graph, Error))
    {
        return Error;
    }

    TArray<TSharedPtr<FJsonValue>> Expressions;
    TArray<UMaterialExpression*> ResolvedExpressions;
    GatherGraphExpressions(Graph, ResolvedExpressions);
    for (UMaterialExpression* Expression : ResolvedExpressions)
    {
        Expressions.Add(MakeShared<FJsonValueObject>(BuildExpressionJson(Expression)));
    }

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetBoolField(TEXT("success"), true);
    AddResolvedGraphMetadata(Graph, Result);
    Result->SetArrayField(TEXT("expressions"), Expressions);
    Result->SetNumberField(TEXT("count"), Expressions.Num());
    return Result;
}

TSharedPtr<FJsonObject> FUnrealAIMaterialCommands::HandleGetMaterialConnections(const TSharedPtr<FJsonObject>& Params)
{
    FResolvedMaterialGraph Graph;
    TSharedPtr<FJsonObject> Error;
    if (!ResolveReadableMaterialGraph(Params, Graph, Error))
    {
        return Error;
    }

    TArray<TSharedPtr<FJsonValue>> Connections;
    TArray<UMaterialExpression*> Expressions;
    GatherGraphExpressions(Graph, Expressions);
    for (UMaterialExpression* Expression : Expressions)
    {
        const int32 InputCount = Expression->CountInputs();
        for (int32 InputIndex = 0; InputIndex < InputCount; ++InputIndex)
        {
            FExpressionInput* Input = Expression->GetInput(InputIndex);
            if (!Input || !Input->Expression)
            {
                continue;
            }

            TSharedPtr<FJsonObject> Connection = MakeShared<FJsonObject>();
            Connection->SetStringField(TEXT("from_expression_id"), GuidToString(Input->Expression->MaterialExpressionGuid));
            Connection->SetStringField(TEXT("from_expression_name"), Input->Expression->GetName());
            Connection->SetStringField(TEXT("to_expression_id"), GuidToString(Expression->MaterialExpressionGuid));
            Connection->SetStringField(TEXT("to_expression_name"), Expression->GetName());
            Connection->SetNumberField(TEXT("from_output_index"), Input->OutputIndex);
            Connection->SetNumberField(TEXT("to_input_index"), InputIndex);
            Connection->SetStringField(TEXT("to_input_name"), Expression->GetInputName(InputIndex).ToString());

            FString OutputName;
            if (FExpressionOutput* Output = Input->Expression->GetOutput(Input->OutputIndex))
            {
                OutputName = Output->OutputName.ToString();
            }
            Connection->SetStringField(TEXT("from_output_name"), OutputName);

            Connections.Add(MakeShared<FJsonValueObject>(Connection));
        }
    }

    TArray<TSharedPtr<FJsonValue>> PropertyInputs;
    if (Graph.Material)
    {
        for (const FMaterialPropertyEntry& PropertyEntry : MaterialProperties)
        {
            FExpressionInput* PropertyInput = Graph.Material->GetExpressionInputForProperty(PropertyEntry.Property);
            if (!PropertyInput || !PropertyInput->Expression)
            {
                continue;
            }

            TSharedPtr<FJsonObject> PropertyObject = MakeShared<FJsonObject>();
            PropertyObject->SetStringField(TEXT("property"), PropertyEntry.Name);
            PropertyObject->SetStringField(TEXT("expression_id"), GuidToString(PropertyInput->Expression->MaterialExpressionGuid));
            PropertyObject->SetStringField(TEXT("expression_name"), PropertyInput->Expression->GetName());
            PropertyObject->SetNumberField(TEXT("output_index"), PropertyInput->OutputIndex);

            FString OutputName;
            if (FExpressionOutput* Output = PropertyInput->Expression->GetOutput(PropertyInput->OutputIndex))
            {
                OutputName = Output->OutputName.ToString();
            }
            PropertyObject->SetStringField(TEXT("output_name"), OutputName);

            PropertyInputs.Add(MakeShared<FJsonValueObject>(PropertyObject));
        }
    }

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetBoolField(TEXT("success"), true);
    AddResolvedGraphMetadata(Graph, Result);
    Result->SetArrayField(TEXT("connections"), Connections);
    Result->SetNumberField(TEXT("connection_count"), Connections.Num());
    Result->SetArrayField(TEXT("property_inputs"), PropertyInputs);
    Result->SetNumberField(TEXT("property_input_count"), PropertyInputs.Num());
    return Result;
}

TSharedPtr<FJsonObject> FUnrealAIMaterialCommands::HandleGetMaterialParameters(const TSharedPtr<FJsonObject>& Params)
{
    FString RequestedPath;
    UMaterialInterface* MaterialInterface = nullptr;
    UMaterial* Material = nullptr;
    TSharedPtr<FJsonObject> Error;
    if (!ResolveMaterialInputs(Params, RequestedPath, MaterialInterface, Material, Error))
    {
        return Error;
    }

    TArray<TSharedPtr<FJsonValue>> Parameters;

    TArray<FMaterialParameterInfo> ScalarInfos;
    TArray<FGuid> ScalarIds;
    MaterialInterface->GetAllScalarParameterInfo(ScalarInfos, ScalarIds);
    AddParameterInfos(MaterialInterface, EMaterialParameterType::Scalar, TEXT("scalar"), ScalarInfos, ScalarIds, Parameters);

    TArray<FMaterialParameterInfo> VectorInfos;
    TArray<FGuid> VectorIds;
    MaterialInterface->GetAllVectorParameterInfo(VectorInfos, VectorIds);
    AddParameterInfos(MaterialInterface, EMaterialParameterType::Vector, TEXT("vector"), VectorInfos, VectorIds, Parameters);

    TArray<FMaterialParameterInfo> TextureInfos;
    TArray<FGuid> TextureIds;
    MaterialInterface->GetAllTextureParameterInfo(TextureInfos, TextureIds);
    AddParameterInfos(MaterialInterface, EMaterialParameterType::Texture, TEXT("texture"), TextureInfos, TextureIds, Parameters);

#if WITH_EDITORONLY_DATA
    TArray<FMaterialParameterInfo> StaticSwitchInfos;
    TArray<FGuid> StaticSwitchIds;
    MaterialInterface->GetAllStaticSwitchParameterInfo(StaticSwitchInfos, StaticSwitchIds);
    AddParameterInfos(MaterialInterface, EMaterialParameterType::StaticSwitch, TEXT("static_switch"), StaticSwitchInfos, StaticSwitchIds, Parameters);
#endif

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetBoolField(TEXT("success"), true);
    Result->SetStringField(TEXT("requested_material_path"), RequestedPath);
    Result->SetStringField(TEXT("graph_material_path"), Material->GetPathName());
    Result->SetArrayField(TEXT("parameters"), Parameters);
    Result->SetNumberField(TEXT("count"), Parameters.Num());
    return Result;
}

TSharedPtr<FJsonObject> FUnrealAIMaterialCommands::HandleSetMaterialParameters(const TSharedPtr<FJsonObject>& Params)
{
    FString RequestedPath;
    UMaterial* Material = nullptr;
    TSharedPtr<FJsonObject> Error;
    if (!ResolveEditableMaterial(Params, RequestedPath, Material, Error))
    {
        return Error;
    }

    int32 UpdatedScalarCount = 0;
    int32 UpdatedVectorCount = 0;
    int32 UpdatedTextureCount = 0;
    int32 UpdatedStaticSwitchCount = 0;

    TArray<FMaterialParameterInfo> ExistingScalarInfos;
    TArray<FGuid> ExistingScalarIds;
    Material->GetAllScalarParameterInfo(ExistingScalarInfos, ExistingScalarIds);

    TArray<FMaterialParameterInfo> ExistingVectorInfos;
    TArray<FGuid> ExistingVectorIds;
    Material->GetAllVectorParameterInfo(ExistingVectorInfos, ExistingVectorIds);

    TArray<FMaterialParameterInfo> ExistingTextureInfos;
    TArray<FGuid> ExistingTextureIds;
    Material->GetAllTextureParameterInfo(ExistingTextureInfos, ExistingTextureIds);

#if WITH_EDITORONLY_DATA
    TArray<FMaterialParameterInfo> ExistingStaticSwitchInfos;
    TArray<FGuid> ExistingStaticSwitchIds;
    Material->GetAllStaticSwitchParameterInfo(ExistingStaticSwitchInfos, ExistingStaticSwitchIds);
#endif

    const TSharedPtr<FJsonObject>* ScalarParams = nullptr;
    if (Params->TryGetObjectField(TEXT("scalar_parameters"), ScalarParams) && ScalarParams && (*ScalarParams).IsValid())
    {
        for (const auto& Pair : (*ScalarParams)->Values)
        {
            if (Pair.Value->Type != EJson::Number)
            {
                return FUnrealAICommonUtils::CreateErrorResponse(
                    FString::Printf(TEXT("Scalar parameter '%s' must be a number"), *Pair.Key));
            }

            TArray<FMaterialParameterInfo> MatchingInfos;
            CollectMatchingParameterInfos(ExistingScalarInfos, Pair.Key, MatchingInfos);
            if (MatchingInfos.Num() == 0)
            {
                return FUnrealAICommonUtils::CreateErrorResponse(
                    FString::Printf(TEXT("Unknown scalar parameter '%s'"), *Pair.Key));
            }

            if (!Material->SetScalarParameterValueEditorOnly(FName(*Pair.Key), static_cast<float>(Pair.Value->AsNumber())))
            {
                return FUnrealAICommonUtils::CreateErrorResponse(
                    FString::Printf(TEXT("Failed to update scalar parameter '%s'"), *Pair.Key));
            }

            ++UpdatedScalarCount;
        }
    }

    const TSharedPtr<FJsonObject>* VectorParams = nullptr;
    if (Params->TryGetObjectField(TEXT("vector_parameters"), VectorParams) && VectorParams && (*VectorParams).IsValid())
    {
        for (const auto& Pair : (*VectorParams)->Values)
        {
            const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
            if (!Pair.Value->TryGetArray(Values) || !Values || Values->Num() != 4)
            {
                return FUnrealAICommonUtils::CreateErrorResponse(
                    FString::Printf(TEXT("Vector parameter '%s' must be an array of 4 numbers"), *Pair.Key));
            }

            TArray<FMaterialParameterInfo> MatchingInfos;
            CollectMatchingParameterInfos(ExistingVectorInfos, Pair.Key, MatchingInfos);
            if (MatchingInfos.Num() == 0)
            {
                return FUnrealAICommonUtils::CreateErrorResponse(
                    FString::Printf(TEXT("Unknown vector parameter '%s'"), *Pair.Key));
            }

            FLinearColor Color(
                static_cast<float>((*Values)[0]->AsNumber()),
                static_cast<float>((*Values)[1]->AsNumber()),
                static_cast<float>((*Values)[2]->AsNumber()),
                static_cast<float>((*Values)[3]->AsNumber()));
            if (!Material->SetVectorParameterValueEditorOnly(FName(*Pair.Key), Color))
            {
                return FUnrealAICommonUtils::CreateErrorResponse(
                    FString::Printf(TEXT("Failed to update vector parameter '%s'"), *Pair.Key));
            }

            ++UpdatedVectorCount;
        }
    }

    const TSharedPtr<FJsonObject>* TextureParams = nullptr;
    if (Params->TryGetObjectField(TEXT("texture_parameters"), TextureParams) && TextureParams && (*TextureParams).IsValid())
    {
        for (const auto& Pair : (*TextureParams)->Values)
        {
            if (Pair.Value->Type != EJson::String)
            {
                return FUnrealAICommonUtils::CreateErrorResponse(
                    FString::Printf(TEXT("Texture parameter '%s' must be an asset path string"), *Pair.Key));
            }

            TArray<FMaterialParameterInfo> MatchingInfos;
            CollectMatchingParameterInfos(ExistingTextureInfos, Pair.Key, MatchingInfos);
            if (MatchingInfos.Num() == 0)
            {
                return FUnrealAICommonUtils::CreateErrorResponse(
                    FString::Printf(TEXT("Unknown texture parameter '%s'"), *Pair.Key));
            }

            const FString TexturePath = Pair.Value->AsString();
            UTexture* Texture = Cast<UTexture>(UEditorAssetLibrary::LoadAsset(TexturePath));
            if (!Texture)
            {
                return FUnrealAICommonUtils::CreateErrorResponse(
                    FString::Printf(TEXT("Failed to load texture asset for parameter '%s': %s"), *Pair.Key, *TexturePath));
            }

            if (!Material->SetTextureParameterValueEditorOnly(FName(*Pair.Key), Texture))
            {
                return FUnrealAICommonUtils::CreateErrorResponse(
                    FString::Printf(TEXT("Failed to update texture parameter '%s'"), *Pair.Key));
            }

            ++UpdatedTextureCount;
        }
    }

#if WITH_EDITORONLY_DATA
    const TSharedPtr<FJsonObject>* StaticSwitchParams = nullptr;
    if (Params->TryGetObjectField(TEXT("static_switch_parameters"), StaticSwitchParams) && StaticSwitchParams && (*StaticSwitchParams).IsValid())
    {
        for (const auto& Pair : (*StaticSwitchParams)->Values)
        {
            if (Pair.Value->Type != EJson::Boolean)
            {
                return FUnrealAICommonUtils::CreateErrorResponse(
                    FString::Printf(TEXT("Static switch parameter '%s' must be a boolean"), *Pair.Key));
            }

            TArray<FMaterialParameterInfo> MatchingInfos;
            CollectMatchingParameterInfos(ExistingStaticSwitchInfos, Pair.Key, MatchingInfos);
            if (MatchingInfos.Num() == 0)
            {
                return FUnrealAICommonUtils::CreateErrorResponse(
                    FString::Printf(TEXT("Unknown static switch parameter '%s'"), *Pair.Key));
            }

            for (const FMaterialParameterInfo& Info : MatchingInfos)
            {
                FMaterialParameterMetadata Metadata;
                const bool bHasMetadata = Material->GetParameterValue(
                    EMaterialParameterType::StaticSwitch,
                    FMemoryImageMaterialParameterInfo(Info),
                    Metadata);
                if (!bHasMetadata || !Metadata.ExpressionGuid.IsValid())
                {
                    return FUnrealAICommonUtils::CreateErrorResponse(
                        FString::Printf(TEXT("Failed to resolve static switch expression GUID for '%s'"), *Pair.Key));
                }

                if (!Material->SetStaticSwitchParameterValueEditorOnly(FName(*Pair.Key), Pair.Value->AsBool(), Metadata.ExpressionGuid))
                {
                    return FUnrealAICommonUtils::CreateErrorResponse(
                        FString::Printf(TEXT("Failed to update static switch parameter '%s'"), *Pair.Key));
                }
            }

            ++UpdatedStaticSwitchCount;
        }
    }
#endif

    const int32 UpdatedParameterCount = UpdatedScalarCount + UpdatedVectorCount + UpdatedTextureCount + UpdatedStaticSwitchCount;
    if (UpdatedParameterCount == 0)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(
            TEXT("No parameter defaults supplied; provide scalar_parameters, vector_parameters, texture_parameters, or static_switch_parameters"));
    }

    Material->PostEditChange();
    Material->MarkPackageDirty();
    SaveEditorAsset(Material);

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetBoolField(TEXT("success"), true);
    Result->SetStringField(TEXT("material_path"), RequestedPath);
    Result->SetNumberField(TEXT("updated_scalar_count"), UpdatedScalarCount);
    Result->SetNumberField(TEXT("updated_vector_count"), UpdatedVectorCount);
    Result->SetNumberField(TEXT("updated_texture_count"), UpdatedTextureCount);
    Result->SetNumberField(TEXT("updated_static_switch_count"), UpdatedStaticSwitchCount);
    Result->SetNumberField(TEXT("updated_parameter_count"), UpdatedParameterCount);
    return Result;
}

TSharedPtr<FJsonObject> FUnrealAIMaterialCommands::HandleSetMaterialInstanceParameters(const TSharedPtr<FJsonObject>& Params)
{
    FString RequestedPath;
    UMaterialInstanceConstant* MaterialInstance = nullptr;
    TSharedPtr<FJsonObject> Error;
    if (!ResolveEditableMaterialInstance(Params, RequestedPath, MaterialInstance, Error))
    {
        return Error;
    }

    bool bClearExistingOverrides = false;
    Params->TryGetBoolField(TEXT("clear_existing_overrides"), bClearExistingOverrides);

    int32 UpdatedScalarCount = 0;
    int32 UpdatedVectorCount = 0;
    int32 UpdatedTextureCount = 0;
    int32 UpdatedStaticSwitchCount = 0;

    TArray<FMaterialParameterInfo> ExistingStaticSwitchInfos;
    TArray<FGuid> ExistingStaticSwitchIds;
    MaterialInstance->GetAllStaticSwitchParameterInfo(ExistingStaticSwitchInfos, ExistingStaticSwitchIds);

    if (bClearExistingOverrides)
    {
        MaterialInstance->ClearParameterValuesEditorOnly();
    }

    const TSharedPtr<FJsonObject>* ScalarParams = nullptr;
    if (Params->TryGetObjectField(TEXT("scalar_parameters"), ScalarParams) && ScalarParams && (*ScalarParams).IsValid())
    {
        for (const auto& Pair : (*ScalarParams)->Values)
        {
            if (Pair.Value->Type != EJson::Number)
            {
                return FUnrealAICommonUtils::CreateErrorResponse(
                    FString::Printf(TEXT("Scalar parameter '%s' must be a number"), *Pair.Key));
            }

            MaterialInstance->SetScalarParameterValueEditorOnly(
                FMaterialParameterInfo(FName(*Pair.Key)),
                static_cast<float>(Pair.Value->AsNumber()));
            ++UpdatedScalarCount;
        }
    }

    const TSharedPtr<FJsonObject>* VectorParams = nullptr;
    if (Params->TryGetObjectField(TEXT("vector_parameters"), VectorParams) && VectorParams && (*VectorParams).IsValid())
    {
        for (const auto& Pair : (*VectorParams)->Values)
        {
            const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
            if (!Pair.Value->TryGetArray(Values) || !Values || Values->Num() != 4)
            {
                return FUnrealAICommonUtils::CreateErrorResponse(
                    FString::Printf(TEXT("Vector parameter '%s' must be an array of 4 numbers"), *Pair.Key));
            }

            FLinearColor Color(
                static_cast<float>((*Values)[0]->AsNumber()),
                static_cast<float>((*Values)[1]->AsNumber()),
                static_cast<float>((*Values)[2]->AsNumber()),
                static_cast<float>((*Values)[3]->AsNumber()));
            MaterialInstance->SetVectorParameterValueEditorOnly(FMaterialParameterInfo(FName(*Pair.Key)), Color);
            ++UpdatedVectorCount;
        }
    }

    const TSharedPtr<FJsonObject>* TextureParams = nullptr;
    if (Params->TryGetObjectField(TEXT("texture_parameters"), TextureParams) && TextureParams && (*TextureParams).IsValid())
    {
        for (const auto& Pair : (*TextureParams)->Values)
        {
            if (Pair.Value->Type != EJson::String)
            {
                return FUnrealAICommonUtils::CreateErrorResponse(
                    FString::Printf(TEXT("Texture parameter '%s' must be an asset path string"), *Pair.Key));
            }

            const FString TexturePath = Pair.Value->AsString();
            UTexture* Texture = Cast<UTexture>(UEditorAssetLibrary::LoadAsset(TexturePath));
            if (!Texture)
            {
                return FUnrealAICommonUtils::CreateErrorResponse(
                    FString::Printf(TEXT("Failed to load texture asset for parameter '%s': %s"), *Pair.Key, *TexturePath));
            }

            MaterialInstance->SetTextureParameterValueEditorOnly(FMaterialParameterInfo(FName(*Pair.Key)), Texture);
            ++UpdatedTextureCount;
        }
    }

    const TSharedPtr<FJsonObject>* StaticSwitchParams = nullptr;
    if (Params->TryGetObjectField(TEXT("static_switch_parameters"), StaticSwitchParams) && StaticSwitchParams && (*StaticSwitchParams).IsValid())
    {
        for (const auto& Pair : (*StaticSwitchParams)->Values)
        {
            if (Pair.Value->Type != EJson::Boolean)
            {
                return FUnrealAICommonUtils::CreateErrorResponse(
                    FString::Printf(TEXT("Static switch parameter '%s' must be a boolean"), *Pair.Key));
            }

            const bool bHasStaticSwitchParameter = ExistingStaticSwitchInfos.ContainsByPredicate(
                [&](const FMaterialParameterInfo& Info)
                {
                    return Info.Association == EMaterialParameterAssociation::GlobalParameter &&
                           Info.Name.ToString().Equals(Pair.Key, ESearchCase::IgnoreCase);
                });
            if (!bHasStaticSwitchParameter)
            {
                return FUnrealAICommonUtils::CreateErrorResponse(
                    FString::Printf(TEXT("Unknown static switch parameter '%s'"), *Pair.Key));
            }

            // UE 5.7's MaterialEditingLibrary setter does the update but leaves its bool return value false.
            UMaterialEditingLibrary::SetMaterialInstanceStaticSwitchParameterValue(
                MaterialInstance,
                FName(*Pair.Key),
                Pair.Value->AsBool(),
                EMaterialParameterAssociation::GlobalParameter,
                false);

            ++UpdatedStaticSwitchCount;
        }
    }

    const int32 UpdatedParameterCount = UpdatedScalarCount + UpdatedVectorCount + UpdatedTextureCount + UpdatedStaticSwitchCount;
    if (!bClearExistingOverrides && UpdatedParameterCount == 0)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(
            TEXT("No parameter overrides supplied; provide scalar_parameters, vector_parameters, texture_parameters, static_switch_parameters, or set clear_existing_overrides=true"));
    }

    if (UpdatedStaticSwitchCount > 0)
    {
        UMaterialEditingLibrary::UpdateMaterialInstance(MaterialInstance);
    }

    MaterialInstance->PostEditChange();
    MaterialInstance->MarkPackageDirty();
    SaveEditorAsset(MaterialInstance);

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetBoolField(TEXT("success"), true);
    Result->SetStringField(TEXT("material_path"), RequestedPath);
    Result->SetBoolField(TEXT("cleared_existing_overrides"), bClearExistingOverrides);
    Result->SetNumberField(TEXT("updated_scalar_count"), UpdatedScalarCount);
    Result->SetNumberField(TEXT("updated_vector_count"), UpdatedVectorCount);
    Result->SetNumberField(TEXT("updated_texture_count"), UpdatedTextureCount);
    Result->SetNumberField(TEXT("updated_static_switch_count"), UpdatedStaticSwitchCount);
    Result->SetNumberField(TEXT("updated_parameter_count"), UpdatedParameterCount);
    return Result;
}

TSharedPtr<FJsonObject> FUnrealAIMaterialCommands::HandleValidateMaterialGraph(const TSharedPtr<FJsonObject>& Params)
{
    FResolvedMaterialGraph Graph;
    TSharedPtr<FJsonObject> Error;
    if (!ResolveReadableMaterialGraph(Params, Graph, Error))
    {
        return Error;
    }

    TArray<UMaterialExpression*> Expressions;
    TSet<UMaterialExpression*> ReferencedExpressions;
    TSet<UMaterialExpression*> PropertyBoundExpressions;
    TSet<UMaterialExpression*> TerminalExpressions;
    int32 ConnectionCount = 0;
    int32 PropertyInputCount = 0;
    int32 FunctionOutputCount = 0;
    int32 ConnectedFunctionOutputCount = 0;

    GatherGraphExpressions(Graph, Expressions);
    for (int32 ExpressionIndex = Expressions.Num() - 1; ExpressionIndex >= 0; --ExpressionIndex)
    {
        UMaterialExpression* Expression = Expressions[ExpressionIndex];
        if (!Expression || ShouldIgnoreValidationExpression(Expression))
        {
            Expressions.RemoveAt(ExpressionIndex);
            continue;
        }

        if (Expression->IsA<UMaterialExpressionFunctionOutput>())
        {
            TerminalExpressions.Add(Expression);
            ++FunctionOutputCount;
        }

        const int32 InputCount = Expression->CountInputs();
        bool bHasConnectedInput = false;
        for (int32 InputIndex = 0; InputIndex < InputCount; ++InputIndex)
        {
            FExpressionInput* Input = Expression->GetInput(InputIndex);
            if (!Input || !Input->Expression)
            {
                continue;
            }

            bHasConnectedInput = true;
            ReferencedExpressions.Add(Input->Expression);
            ++ConnectionCount;
        }

        if (Expression->IsA<UMaterialExpressionFunctionOutput>() && bHasConnectedInput)
        {
            ++ConnectedFunctionOutputCount;
        }
    }

    if (Graph.Material)
    {
        for (const FMaterialPropertyEntry& PropertyEntry : MaterialProperties)
        {
            FExpressionInput* PropertyInput = Graph.Material->GetExpressionInputForProperty(PropertyEntry.Property);
            if (!PropertyInput || !PropertyInput->Expression)
            {
                continue;
            }

            PropertyBoundExpressions.Add(PropertyInput->Expression);
            ++PropertyInputCount;
        }
    }

    TArray<TSharedPtr<FJsonValue>> Issues;
    TArray<TSharedPtr<FJsonValue>> OrphanedExpressions;
    int32 ErrorCount = 0;
    int32 WarningCount = 0;

    if (Expressions.Num() == 0)
    {
        Issues.Add(MakeShared<FJsonValueObject>(BuildValidationIssue(
            TEXT("error"),
            TEXT("empty_graph"),
            FString::Printf(TEXT("Material graph '%s' contains no expressions"), *Graph.GetGraphPath()))));
        ++ErrorCount;
    }

    if (Graph.Material && PropertyInputCount == 0)
    {
        Issues.Add(MakeShared<FJsonValueObject>(BuildValidationIssue(
            TEXT("error"),
            TEXT("missing_output_binding"),
            FString::Printf(TEXT("Material graph '%s' is not connected to any material output property"), *Graph.Material->GetPathName()))));
        ++ErrorCount;
    }

    if (Graph.MaterialFunction && FunctionOutputCount == 0)
    {
        Issues.Add(MakeShared<FJsonValueObject>(BuildValidationIssue(
            TEXT("error"),
            TEXT("missing_function_output"),
            FString::Printf(TEXT("Material function graph '%s' contains no function output expressions"), *Graph.MaterialFunction->GetPathName()))));
        ++ErrorCount;
    }

    if (Graph.MaterialFunction && FunctionOutputCount > 0 && ConnectedFunctionOutputCount == 0)
    {
        Issues.Add(MakeShared<FJsonValueObject>(BuildValidationIssue(
            TEXT("error"),
            TEXT("missing_function_output_binding"),
            FString::Printf(TEXT("Material function graph '%s' has no connected function outputs"), *Graph.MaterialFunction->GetPathName()))));
        ++ErrorCount;
    }

    for (UMaterialExpression* Expression : Expressions)
    {
        if (ReferencedExpressions.Contains(Expression) || PropertyBoundExpressions.Contains(Expression) || TerminalExpressions.Contains(Expression))
        {
            continue;
        }

        TSharedPtr<FJsonObject> OrphanObject = MakeShared<FJsonObject>();
        OrphanObject->SetStringField(TEXT("expression_id"), GuidToString(Expression->MaterialExpressionGuid));
        OrphanObject->SetStringField(TEXT("expression_name"), Expression->GetName());
        OrphanObject->SetStringField(TEXT("expression_class"), Expression->GetClass()->GetName());
        OrphanedExpressions.Add(MakeShared<FJsonValueObject>(OrphanObject));

        TSharedPtr<FJsonObject> Issue = BuildValidationIssue(
            TEXT("warning"),
            TEXT("orphaned_expression"),
            FString::Printf(TEXT("Expression '%s' is not connected to another expression or a material output property"), *Expression->GetName()));
        Issue->SetStringField(TEXT("expression_id"), GuidToString(Expression->MaterialExpressionGuid));
        Issue->SetStringField(TEXT("expression_name"), Expression->GetName());
        Issue->SetStringField(TEXT("expression_class"), Expression->GetClass()->GetName());
        Issues.Add(MakeShared<FJsonValueObject>(Issue));
        ++WarningCount;
    }

    TMap<FString, TSet<FString>> ParameterIdentityTypes;
    TMap<FString, FMaterialParameterInfo> ParameterIdentityInfo;
    auto CollectParameterIdentity = [&](EMaterialParameterType ParameterType, const TCHAR* TypeName, const TArray<FMaterialParameterInfo>& Infos)
    {
        for (const FMaterialParameterInfo& Info : Infos)
        {
            const FString Key = BuildParameterIdentityKey(Info);
            ParameterIdentityTypes.FindOrAdd(Key).Add(TypeName);
            ParameterIdentityInfo.FindOrAdd(Key) = Info;
        }
    };

    TArray<FMaterialParameterInfo> ScalarInfos;
    TArray<FGuid> ScalarIds;
    TArray<FMaterialParameterInfo> VectorInfos;
    TArray<FGuid> VectorIds;
    TArray<FMaterialParameterInfo> TextureInfos;
    TArray<FGuid> TextureIds;
#if WITH_EDITORONLY_DATA
    TArray<FMaterialParameterInfo> StaticSwitchInfos;
    TArray<FGuid> StaticSwitchIds;
#endif

    if (Graph.MaterialInterface)
    {
        Graph.MaterialInterface->GetAllScalarParameterInfo(ScalarInfos, ScalarIds);
        CollectParameterIdentity(EMaterialParameterType::Scalar, TEXT("scalar"), ScalarInfos);

        Graph.MaterialInterface->GetAllVectorParameterInfo(VectorInfos, VectorIds);
        CollectParameterIdentity(EMaterialParameterType::Vector, TEXT("vector"), VectorInfos);

        Graph.MaterialInterface->GetAllTextureParameterInfo(TextureInfos, TextureIds);
        CollectParameterIdentity(EMaterialParameterType::Texture, TEXT("texture"), TextureInfos);

#if WITH_EDITORONLY_DATA
        Graph.MaterialInterface->GetAllStaticSwitchParameterInfo(StaticSwitchInfos, StaticSwitchIds);
        CollectParameterIdentity(EMaterialParameterType::StaticSwitch, TEXT("static_switch"), StaticSwitchInfos);
#endif
    }

    TArray<TSharedPtr<FJsonValue>> DuplicateParameters;
    for (const TPair<FString, TSet<FString>>& Entry : ParameterIdentityTypes)
    {
        if (Entry.Value.Num() <= 1)
        {
            continue;
        }

        const FMaterialParameterInfo* Info = ParameterIdentityInfo.Find(Entry.Key);
        if (!Info)
        {
            continue;
        }

        TArray<FString> SortedTypes = Entry.Value.Array();
        SortedTypes.Sort();

        TArray<TSharedPtr<FJsonValue>> TypeValues;
        for (const FString& TypeName : SortedTypes)
        {
            TypeValues.Add(MakeShared<FJsonValueString>(TypeName));
        }

        TSharedPtr<FJsonObject> DuplicateObject = MakeShared<FJsonObject>();
        DuplicateObject->SetStringField(TEXT("name"), Info->Name.ToString());
        DuplicateObject->SetStringField(TEXT("association"), ParameterAssociationToString(Info->Association));
        DuplicateObject->SetNumberField(TEXT("association_index"), static_cast<int32>(Info->Association));
        DuplicateObject->SetNumberField(TEXT("layer_index"), Info->Index);
        DuplicateObject->SetArrayField(TEXT("types"), TypeValues);
        DuplicateParameters.Add(MakeShared<FJsonValueObject>(DuplicateObject));

        TSharedPtr<FJsonObject> Issue = BuildValidationIssue(
            TEXT("warning"),
            TEXT("parameter_identity_collision"),
            FString::Printf(TEXT("Parameter '%s' appears with multiple parameter types"), *Info->Name.ToString()));
        Issue->SetStringField(TEXT("parameter_name"), Info->Name.ToString());
        Issue->SetStringField(TEXT("association"), ParameterAssociationToString(Info->Association));
        Issue->SetNumberField(TEXT("layer_index"), Info->Index);
        Issue->SetArrayField(TEXT("types"), TypeValues);
        Issues.Add(MakeShared<FJsonValueObject>(Issue));
        ++WarningCount;
    }

    TSharedPtr<FJsonObject> Summary = MakeShared<FJsonObject>();
    Summary->SetNumberField(TEXT("expression_count"), Expressions.Num());
    Summary->SetNumberField(TEXT("connection_count"), ConnectionCount);
    Summary->SetNumberField(TEXT("property_input_count"), PropertyInputCount);
    Summary->SetNumberField(TEXT("function_output_count"), FunctionOutputCount);
    Summary->SetNumberField(TEXT("connected_function_output_count"), ConnectedFunctionOutputCount);
    Summary->SetNumberField(TEXT("parameter_count"), ScalarInfos.Num() + VectorInfos.Num() + TextureInfos.Num()
#if WITH_EDITORONLY_DATA
        + StaticSwitchInfos.Num()
#endif
    );
    Summary->SetNumberField(TEXT("orphaned_expression_count"), OrphanedExpressions.Num());
    Summary->SetNumberField(TEXT("duplicate_parameter_count"), DuplicateParameters.Num());
    Summary->SetNumberField(TEXT("issue_count"), Issues.Num());
    Summary->SetNumberField(TEXT("error_count"), ErrorCount);
    Summary->SetNumberField(TEXT("warning_count"), WarningCount);

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetBoolField(TEXT("success"), true);
    Result->SetBoolField(TEXT("is_valid"), ErrorCount == 0);
    AddResolvedGraphMetadata(Graph, Result);
    Result->SetObjectField(TEXT("summary"), Summary);
    Result->SetArrayField(TEXT("issues"), Issues);
    Result->SetArrayField(TEXT("orphaned_expressions"), OrphanedExpressions);
    Result->SetArrayField(TEXT("duplicate_parameters"), DuplicateParameters);
    return Result;
}

TSharedPtr<FJsonObject> FUnrealAIMaterialCommands::HandleCreateMaterialExpression(const TSharedPtr<FJsonObject>& Params)
{
    FResolvedMaterialGraph Graph;
    TSharedPtr<FJsonObject> Error;
    if (!ResolveEditableMaterialGraph(Params, Graph, Error))
    {
        return Error;
    }

    FString ExpressionClassName;
    if (!Params->TryGetStringField(TEXT("expression_class"), ExpressionClassName) || ExpressionClassName.IsEmpty())
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing 'expression_class' parameter"));
    }

    UClass* ExpressionClass = ResolveExpressionClass(ExpressionClassName);
    if (!ExpressionClass)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Unknown material expression class: %s"), *ExpressionClassName));
    }

    int32 NodePosX = 0;
    int32 NodePosY = 0;
    if (Params->HasField(TEXT("node_pos_x")))
    {
        NodePosX = Params->GetIntegerField(TEXT("node_pos_x"));
    }
    if (Params->HasField(TEXT("node_pos_y")))
    {
        NodePosY = Params->GetIntegerField(TEXT("node_pos_y"));
    }

    UMaterialExpression* Expression = CreateGraphExpression(Graph, ExpressionClass, NodePosX, NodePosY);
    if (!Expression)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Failed to create material expression"));
    }

    FinalizeGraphEdit(Graph);

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetBoolField(TEXT("success"), true);
    Result->SetStringField(TEXT("material_path"), Graph.RequestedPath);
    Result->SetStringField(TEXT("graph_asset_type"), Graph.GetGraphAssetType());
    Result->SetObjectField(TEXT("expression"), BuildMaterialExpressionRef(Expression));
    return Result;
}

TSharedPtr<FJsonObject> FUnrealAIMaterialCommands::HandleDeleteMaterialExpression(const TSharedPtr<FJsonObject>& Params)
{
    FResolvedMaterialGraph Graph;
    TSharedPtr<FJsonObject> Error;
    if (!ResolveEditableMaterialGraph(Params, Graph, Error))
    {
        return Error;
    }

    UMaterialExpression* Expression = ResolveMaterialExpression(Graph, Params, TEXT("expression_id"), TEXT("expression_name"));
    if (!Expression)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Failed to resolve material expression by id or name"));
    }

    const TSharedPtr<FJsonObject> DeletedExpression = BuildMaterialExpressionRef(Expression);
    DeleteGraphExpression(Graph, Expression);
    FinalizeGraphEdit(Graph);

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetBoolField(TEXT("success"), true);
    Result->SetStringField(TEXT("material_path"), Graph.RequestedPath);
    Result->SetStringField(TEXT("graph_asset_type"), Graph.GetGraphAssetType());
    Result->SetObjectField(TEXT("deleted_expression"), DeletedExpression);
    return Result;
}

TSharedPtr<FJsonObject> FUnrealAIMaterialCommands::HandleDeleteMaterialExpressions(const TSharedPtr<FJsonObject>& Params)
{
    FResolvedMaterialGraph Graph;
    TSharedPtr<FJsonObject> Error;
    if (!ResolveEditableMaterialGraph(Params, Graph, Error))
    {
        return Error;
    }

    const TArray<TSharedPtr<FJsonValue>>* ExpressionIds = nullptr;
    const TArray<TSharedPtr<FJsonValue>>* ExpressionNames = nullptr;
    const bool bHasExpressionIds = Params->TryGetArrayField(TEXT("expression_ids"), ExpressionIds) && ExpressionIds && ExpressionIds->Num() > 0;
    const bool bHasExpressionNames = Params->TryGetArrayField(TEXT("expression_names"), ExpressionNames) && ExpressionNames && ExpressionNames->Num() > 0;
    if (!bHasExpressionIds && !bHasExpressionNames)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Provide 'expression_ids' and/or 'expression_names' with at least one entry"));
    }

    TArray<UMaterialExpression*> ExpressionsToDelete;
    TArray<TSharedPtr<FJsonValue>> DeletedExpressions;
    TSet<UMaterialExpression*> SeenExpressions;

    auto ResolveAndQueueExpression = [&](const FString& ExpressionId, const FString& ExpressionName, const FString& Label) -> TSharedPtr<FJsonObject>
    {
        UMaterialExpression* Expression = ResolveMaterialExpression(Graph, ExpressionId, ExpressionName);
        if (!Expression)
        {
            return FUnrealAICommonUtils::CreateErrorResponse(
                FString::Printf(TEXT("Failed to resolve material expression for %s: %s"), *Label, ExpressionId.IsEmpty() ? *ExpressionName : *ExpressionId));
        }

        if (SeenExpressions.Contains(Expression))
        {
            return FUnrealAICommonUtils::CreateErrorResponse(
                FString::Printf(TEXT("Duplicate material expression requested for deletion: %s"), *Expression->GetName()));
        }

        SeenExpressions.Add(Expression);
        ExpressionsToDelete.Add(Expression);
        DeletedExpressions.Add(MakeShared<FJsonValueObject>(BuildMaterialExpressionRef(Expression)));
        return nullptr;
    };

    if (bHasExpressionIds)
    {
        for (const TSharedPtr<FJsonValue>& Value : *ExpressionIds)
        {
            if (!Value.IsValid() || Value->Type != EJson::String || Value->AsString().IsEmpty())
            {
                return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Each entry in 'expression_ids' must be a non-empty string"));
            }

            if (TSharedPtr<FJsonObject> ResolveError = ResolveAndQueueExpression(Value->AsString(), FString(), TEXT("expression id")))
            {
                return ResolveError;
            }
        }
    }

    if (bHasExpressionNames)
    {
        for (const TSharedPtr<FJsonValue>& Value : *ExpressionNames)
        {
            if (!Value.IsValid() || Value->Type != EJson::String || Value->AsString().IsEmpty())
            {
                return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Each entry in 'expression_names' must be a non-empty string"));
            }

            if (TSharedPtr<FJsonObject> ResolveError = ResolveAndQueueExpression(FString(), Value->AsString(), TEXT("expression name")))
            {
                return ResolveError;
            }
        }
    }

    for (UMaterialExpression* Expression : ExpressionsToDelete)
    {
        DeleteGraphExpression(Graph, Expression);
    }

    FinalizeGraphEdit(Graph);

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetBoolField(TEXT("success"), true);
    Result->SetStringField(TEXT("material_path"), Graph.RequestedPath);
    Result->SetStringField(TEXT("graph_asset_type"), Graph.GetGraphAssetType());
    Result->SetArrayField(TEXT("deleted_expressions"), DeletedExpressions);
    Result->SetNumberField(TEXT("deleted_count"), DeletedExpressions.Num());
    return Result;
}

TSharedPtr<FJsonObject> FUnrealAIMaterialCommands::HandleReplaceMaterialExpression(const TSharedPtr<FJsonObject>& Params)
{
    FResolvedMaterialGraph Graph;
    TSharedPtr<FJsonObject> Error;
    if (!ResolveEditableMaterialGraph(Params, Graph, Error))
    {
        return Error;
    }

    UMaterialExpression* SourceExpression = ResolveMaterialExpression(Graph, Params, TEXT("expression_id"), TEXT("expression_name"));
    if (!SourceExpression)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Failed to resolve material expression by id or name"));
    }

    FString NewExpressionClassName;
    if (!Params->TryGetStringField(TEXT("new_expression_class"), NewExpressionClassName) || NewExpressionClassName.IsEmpty())
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing 'new_expression_class' parameter"));
    }

    UClass* NewExpressionClass = ResolveExpressionClass(NewExpressionClassName);
    if (!NewExpressionClass)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Unknown material expression class: %s"), *NewExpressionClassName));
    }

    int32 NodePosX = SourceExpression->MaterialExpressionEditorX;
    int32 NodePosY = SourceExpression->MaterialExpressionEditorY;
    if (Params->HasField(TEXT("node_pos_x")))
    {
        NodePosX = Params->GetIntegerField(TEXT("node_pos_x"));
    }
    if (Params->HasField(TEXT("node_pos_y")))
    {
        NodePosY = Params->GetIntegerField(TEXT("node_pos_y"));
    }

    TArray<FMaterialExpressionConnectionRef> IncomingConnections;
    const int32 SourceInputCount = SourceExpression->CountInputs();
    for (int32 InputIndex = 0; InputIndex < SourceInputCount; ++InputIndex)
    {
        FExpressionInput* Input = SourceExpression->GetInput(InputIndex);
        if (!Input || !Input->Expression)
        {
            continue;
        }

        FMaterialExpressionConnectionRef Connection;
        Connection.Expression = Input->Expression;
        Connection.OutputName = GetMaterialExpressionOutputName(Input->Expression, Input->OutputIndex);
        Connection.InputName = SourceExpression->GetInputName(InputIndex).ToString();
        IncomingConnections.Add(Connection);
    }

    TArray<FMaterialExpressionConnectionRef> OutgoingConnections;
    TArray<UMaterialExpression*> Expressions;
    GatherGraphExpressions(Graph, Expressions);
    for (UMaterialExpression* Candidate : Expressions)
    {
        if (!Candidate || Candidate == SourceExpression)
        {
            continue;
        }

        const int32 CandidateInputCount = Candidate->CountInputs();
        for (int32 InputIndex = 0; InputIndex < CandidateInputCount; ++InputIndex)
        {
            FExpressionInput* CandidateInput = Candidate->GetInput(InputIndex);
            if (!CandidateInput || CandidateInput->Expression != SourceExpression)
            {
                continue;
            }

            FMaterialExpressionConnectionRef Connection;
            Connection.Expression = Candidate;
            Connection.OutputName = GetMaterialExpressionOutputName(SourceExpression, CandidateInput->OutputIndex);
            Connection.InputName = Candidate->GetInputName(InputIndex).ToString();
            OutgoingConnections.Add(Connection);
        }
    }

    TArray<FMaterialPropertyConnectionRef> PropertyConnections;
    if (Graph.Material)
    {
        for (const FMaterialPropertyEntry& PropertyEntry : MaterialProperties)
        {
            FExpressionInput* PropertyInput = Graph.Material->GetExpressionInputForProperty(PropertyEntry.Property);
            if (!PropertyInput || PropertyInput->Expression != SourceExpression)
            {
                continue;
            }

            FMaterialPropertyConnectionRef Connection;
            Connection.Property = PropertyEntry.Property;
            Connection.PropertyName = PropertyEntry.Name;
            Connection.OutputName = GetMaterialExpressionOutputName(SourceExpression, PropertyInput->OutputIndex);
            PropertyConnections.Add(Connection);
        }
    }

    UMaterialExpression* ReplacementExpression = CreateGraphExpression(Graph, NewExpressionClass, NodePosX, NodePosY);
    if (!ReplacementExpression)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Failed to create replacement material expression"));
    }

    ReplacementExpression->Desc = SourceExpression->Desc;

    auto CleanupReplacement = [&]()
    {
        if (ReplacementExpression)
        {
            DeleteGraphExpression(Graph, ReplacementExpression);
            ReplacementExpression = nullptr;
        }
    };

    for (const FMaterialExpressionConnectionRef& Connection : IncomingConnections)
    {
        if (!UMaterialEditingLibrary::ConnectMaterialExpressions(
                Connection.Expression,
                Connection.OutputName,
                ReplacementExpression,
                Connection.InputName))
        {
            CleanupReplacement();
            return FUnrealAICommonUtils::CreateErrorResponse(
                FString::Printf(TEXT("Failed to reconnect replacement input '%s'"), *Connection.InputName));
        }
    }

    for (const FMaterialExpressionConnectionRef& Connection : OutgoingConnections)
    {
        if (!UMaterialEditingLibrary::ConnectMaterialExpressions(
                ReplacementExpression,
                Connection.OutputName,
                Connection.Expression,
                Connection.InputName))
        {
            CleanupReplacement();
            return FUnrealAICommonUtils::CreateErrorResponse(
                FString::Printf(TEXT("Failed to reconnect downstream input '%s'"), *Connection.InputName));
        }
    }

    for (const FMaterialPropertyConnectionRef& Connection : PropertyConnections)
    {
        if (!UMaterialEditingLibrary::ConnectMaterialProperty(
                ReplacementExpression,
                Connection.OutputName,
                Connection.Property))
        {
            CleanupReplacement();
            return FUnrealAICommonUtils::CreateErrorResponse(
                FString::Printf(TEXT("Failed to reconnect material property '%s'"), *Connection.PropertyName));
        }
    }

    const TSharedPtr<FJsonObject> ReplacedExpressionRef = BuildMaterialExpressionRef(SourceExpression);
    DeleteGraphExpression(Graph, SourceExpression);
    FinalizeGraphEdit(Graph);

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetBoolField(TEXT("success"), true);
    Result->SetStringField(TEXT("material_path"), Graph.RequestedPath);
    Result->SetStringField(TEXT("graph_asset_type"), Graph.GetGraphAssetType());
    Result->SetObjectField(TEXT("replaced_expression"), ReplacedExpressionRef);
    Result->SetObjectField(TEXT("replacement_expression"), BuildMaterialExpressionRef(ReplacementExpression));
    Result->SetNumberField(TEXT("reconnected_input_count"), IncomingConnections.Num());
    Result->SetNumberField(TEXT("reconnected_output_count"), OutgoingConnections.Num());
    Result->SetNumberField(TEXT("reconnected_property_count"), PropertyConnections.Num());
    return Result;
}

TSharedPtr<FJsonObject> FUnrealAIMaterialCommands::HandleConnectMaterialExpressions(const TSharedPtr<FJsonObject>& Params)
{
    FResolvedMaterialGraph Graph;
    TSharedPtr<FJsonObject> Error;
    if (!ResolveEditableMaterialGraph(Params, Graph, Error))
    {
        return Error;
    }

    UMaterialExpression* FromExpression = ResolveMaterialExpression(Graph, Params, TEXT("from_expression_id"), TEXT("from_expression_name"));
    UMaterialExpression* ToExpression = ResolveMaterialExpression(Graph, Params, TEXT("to_expression_id"), TEXT("to_expression_name"));
    if (!FromExpression || !ToExpression)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Failed to resolve from/to expressions by id or name"));
    }

    FString FromOutputName;
    FString ToInputName;
    Params->TryGetStringField(TEXT("from_output_name"), FromOutputName);
    Params->TryGetStringField(TEXT("to_input_name"), ToInputName);

    const bool bConnected = UMaterialEditingLibrary::ConnectMaterialExpressions(
        FromExpression,
        FromOutputName,
        ToExpression,
        ToInputName);
    if (!bConnected)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Failed to connect material expressions"));
    }

    FinalizeGraphEdit(Graph);

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetBoolField(TEXT("success"), true);
    Result->SetStringField(TEXT("material_path"), Graph.RequestedPath);
    Result->SetStringField(TEXT("graph_asset_type"), Graph.GetGraphAssetType());
    Result->SetObjectField(TEXT("from_expression"), BuildMaterialExpressionRef(FromExpression));
    Result->SetObjectField(TEXT("to_expression"), BuildMaterialExpressionRef(ToExpression));
    Result->SetStringField(TEXT("from_output_name"), FromOutputName);
    Result->SetStringField(TEXT("to_input_name"), ToInputName);
    return Result;
}

TSharedPtr<FJsonObject> FUnrealAIMaterialCommands::HandleDisconnectMaterialExpressions(const TSharedPtr<FJsonObject>& Params)
{
    FResolvedMaterialGraph Graph;
    TSharedPtr<FJsonObject> Error;
    if (!ResolveEditableMaterialGraph(Params, Graph, Error))
    {
        return Error;
    }

    UMaterialExpression* ToExpression = ResolveMaterialExpression(Graph, Params, TEXT("to_expression_id"), TEXT("to_expression_name"));
    if (!ToExpression)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Failed to resolve destination expression by id or name"));
    }

    FString RequestedInputName;
    Params->TryGetStringField(TEXT("to_input_name"), RequestedInputName);

    int32 InputIndex = INDEX_NONE;
    FExpressionInput* Input = nullptr;
    FString ResolvedInputName;
    if (!ResolveMaterialExpressionInput(ToExpression, RequestedInputName, InputIndex, Input, ResolvedInputName, Error))
    {
        return Error;
    }

    if (!Input || !Input->Expression)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Input '%s' on expression '%s' is not currently connected"), *ResolvedInputName, *ToExpression->GetName()));
    }

    FString RequestedFromExpressionId;
    FString RequestedFromExpressionName;
    Params->TryGetStringField(TEXT("from_expression_id"), RequestedFromExpressionId);
    Params->TryGetStringField(TEXT("from_expression_name"), RequestedFromExpressionName);

    UMaterialExpression* FromExpression = Input->Expression;
    if (!RequestedFromExpressionId.IsEmpty() || !RequestedFromExpressionName.IsEmpty())
    {
        UMaterialExpression* ExpectedFromExpression = ResolveMaterialExpression(Graph, Params, TEXT("from_expression_id"), TEXT("from_expression_name"));
        if (!ExpectedFromExpression)
        {
            return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Failed to resolve source expression by id or name"));
        }
        if (ExpectedFromExpression != FromExpression)
        {
            return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Destination input is connected to a different source expression"));
        }
    }

    const FString PreviousOutputName = GetMaterialExpressionOutputName(FromExpression, Input->OutputIndex);
    FString RequestedOutputName;
    Params->TryGetStringField(TEXT("from_output_name"), RequestedOutputName);
    if (!RequestedOutputName.IsEmpty() && !RequestedOutputName.Equals(PreviousOutputName, ESearchCase::IgnoreCase))
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Destination input is connected to a different source output"));
    }

    const int32 PreviousOutputIndex = Input->OutputIndex;
    ClearMaterialExpressionInput(Input);
    FinalizeGraphEdit(Graph);

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetBoolField(TEXT("success"), true);
    Result->SetStringField(TEXT("material_path"), Graph.RequestedPath);
    Result->SetStringField(TEXT("graph_asset_type"), Graph.GetGraphAssetType());
    Result->SetObjectField(TEXT("from_expression"), BuildMaterialExpressionRef(FromExpression));
    Result->SetObjectField(TEXT("to_expression"), BuildMaterialExpressionRef(ToExpression));
    Result->SetStringField(TEXT("from_output_name"), PreviousOutputName);
    Result->SetNumberField(TEXT("from_output_index"), PreviousOutputIndex);
    Result->SetStringField(TEXT("to_input_name"), ResolvedInputName);
    Result->SetNumberField(TEXT("to_input_index"), InputIndex);
    return Result;
}

TSharedPtr<FJsonObject> FUnrealAIMaterialCommands::HandleConnectMaterialProperty(const TSharedPtr<FJsonObject>& Params)
{
    FResolvedMaterialGraph Graph;
    TSharedPtr<FJsonObject> Error;
    if (!ResolveEditableMaterialGraph(Params, Graph, Error))
    {
        return Error;
    }

    if (!Graph.Material)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Material output property bindings are only supported on base material assets"));
    }

    UMaterialExpression* FromExpression = ResolveMaterialExpression(Graph, Params, TEXT("from_expression_id"), TEXT("from_expression_name"));
    if (!FromExpression)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Failed to resolve source expression by id or name"));
    }

    FString FromOutputName;
    FString PropertyName;
    Params->TryGetStringField(TEXT("from_output_name"), FromOutputName);
    if (!Params->TryGetStringField(TEXT("property_name"), PropertyName) || PropertyName.IsEmpty())
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing 'property_name' parameter"));
    }

    EMaterialProperty Property = MP_BaseColor;
    if (!TryResolveMaterialProperty(PropertyName, Property))
    {
        return FUnrealAICommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Unknown material property: %s"), *PropertyName));
    }

    const bool bConnected = UMaterialEditingLibrary::ConnectMaterialProperty(FromExpression, FromOutputName, Property);
    if (!bConnected)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Failed to connect expression to material property"));
    }

    FinalizeGraphEdit(Graph);

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetBoolField(TEXT("success"), true);
    Result->SetStringField(TEXT("material_path"), Graph.RequestedPath);
    Result->SetStringField(TEXT("graph_asset_type"), Graph.GetGraphAssetType());
    Result->SetObjectField(TEXT("from_expression"), BuildMaterialExpressionRef(FromExpression));
    Result->SetStringField(TEXT("property_name"), PropertyName);
    Result->SetStringField(TEXT("from_output_name"), FromOutputName);
    return Result;
}

TSharedPtr<FJsonObject> FUnrealAIMaterialCommands::HandleDisconnectMaterialProperty(const TSharedPtr<FJsonObject>& Params)
{
    FResolvedMaterialGraph Graph;
    TSharedPtr<FJsonObject> Error;
    if (!ResolveEditableMaterialGraph(Params, Graph, Error))
    {
        return Error;
    }

    if (!Graph.Material)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Material output property bindings are only supported on base material assets"));
    }

    FString PropertyName;
    if (!Params->TryGetStringField(TEXT("property_name"), PropertyName) || PropertyName.IsEmpty())
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing 'property_name' parameter"));
    }

    EMaterialProperty Property = MP_BaseColor;
    if (!TryResolveMaterialProperty(PropertyName, Property))
    {
        return FUnrealAICommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Unknown material property: %s"), *PropertyName));
    }

    FExpressionInput* PropertyInput = Graph.Material->GetExpressionInputForProperty(Property);
    if (!PropertyInput || !PropertyInput->Expression)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Material property '%s' is not currently connected"), *PropertyName));
    }

    FString RequestedFromExpressionId;
    FString RequestedFromExpressionName;
    Params->TryGetStringField(TEXT("from_expression_id"), RequestedFromExpressionId);
    Params->TryGetStringField(TEXT("from_expression_name"), RequestedFromExpressionName);

    UMaterialExpression* FromExpression = PropertyInput->Expression;
    if (!RequestedFromExpressionId.IsEmpty() || !RequestedFromExpressionName.IsEmpty())
    {
        UMaterialExpression* ExpectedFromExpression = ResolveMaterialExpression(Graph, Params, TEXT("from_expression_id"), TEXT("from_expression_name"));
        if (!ExpectedFromExpression)
        {
            return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Failed to resolve source expression by id or name"));
        }
        if (ExpectedFromExpression != FromExpression)
        {
            return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Material property is connected to a different source expression"));
        }
    }

    const FString PreviousOutputName = GetMaterialExpressionOutputName(FromExpression, PropertyInput->OutputIndex);
    FString RequestedOutputName;
    Params->TryGetStringField(TEXT("from_output_name"), RequestedOutputName);
    if (!RequestedOutputName.IsEmpty() && !RequestedOutputName.Equals(PreviousOutputName, ESearchCase::IgnoreCase))
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Material property is connected to a different source output"));
    }

    const int32 PreviousOutputIndex = PropertyInput->OutputIndex;
    ClearMaterialExpressionInput(PropertyInput);
    FinalizeGraphEdit(Graph);

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetBoolField(TEXT("success"), true);
    Result->SetStringField(TEXT("material_path"), Graph.RequestedPath);
    Result->SetStringField(TEXT("graph_asset_type"), Graph.GetGraphAssetType());
    Result->SetObjectField(TEXT("from_expression"), BuildMaterialExpressionRef(FromExpression));
    Result->SetStringField(TEXT("property_name"), PropertyName);
    Result->SetStringField(TEXT("from_output_name"), PreviousOutputName);
    Result->SetNumberField(TEXT("from_output_index"), PreviousOutputIndex);
    return Result;
}

TSharedPtr<FJsonObject> FUnrealAIMaterialCommands::HandleRecompileMaterial(const TSharedPtr<FJsonObject>& Params)
{
    FResolvedMaterialGraph Graph;
    TSharedPtr<FJsonObject> Error;
    if (!ResolveEditableMaterialGraph(Params, Graph, Error))
    {
        return Error;
    }

    if (Graph.Material)
    {
        UMaterialEditingLibrary::RecompileMaterial(Graph.Material);
    }

    FinalizeGraphEdit(Graph);

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetBoolField(TEXT("success"), true);
    Result->SetStringField(TEXT("material_path"), Graph.RequestedPath);
    Result->SetStringField(TEXT("graph_asset_type"), Graph.GetGraphAssetType());
    return Result;
}

TSharedPtr<FJsonObject> FUnrealAIMaterialCommands::HandleLayoutMaterialExpressions(const TSharedPtr<FJsonObject>& Params)
{
    FResolvedMaterialGraph Graph;
    TSharedPtr<FJsonObject> Error;
    if (!ResolveEditableMaterialGraph(Params, Graph, Error))
    {
        return Error;
    }

    if (Graph.MaterialFunction)
    {
        UMaterialEditingLibrary::LayoutMaterialFunctionExpressions(Graph.MaterialFunction);
    }
    else if (Graph.Material)
    {
        UMaterialEditingLibrary::LayoutMaterialExpressions(Graph.Material);
    }

    FinalizeGraphEdit(Graph);

    TArray<UMaterialExpression*> Expressions;
    GatherGraphExpressions(Graph, Expressions);

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetBoolField(TEXT("success"), true);
    Result->SetStringField(TEXT("material_path"), Graph.RequestedPath);
    Result->SetStringField(TEXT("graph_asset_type"), Graph.GetGraphAssetType());
    Result->SetNumberField(TEXT("expression_count"), Expressions.Num());
    return Result;
}