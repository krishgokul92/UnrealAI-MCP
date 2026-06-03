#include "Commands/UnrealAIAssetCommands.h"
#include "Commands/UnrealAICommonUtils.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "AssetRegistry/AssetData.h"
#include "AssetRegistry/ARFilter.h"

#include "AssetToolsModule.h"
#include "IAssetTools.h"

#include "Animation/AnimBlueprint.h"
#include "Animation/AnimBlueprintGeneratedClass.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimationAsset.h"
#include "Animation/AnimSequenceBase.h"
#include "Animation/BlendSpace.h"
#include "Animation/AimOffsetBlendSpace.h"
#include "Animation/AimOffsetBlendSpace1D.h"
#include "Animation/Skeleton.h"
#include "Animation/AnimTypes.h"
#include "AnimationGraph.h"
#include "AnimationGraphSchema.h"
#include "AnimationStateGraph.h"
#include "AnimationTransitionGraph.h"
#include "AnimationTransitionSchema.h"
#include "AnimGraphNode_BlendSpacePlayer.h"
#include "AnimGraphNode_RotationOffsetBlendSpace.h"
#include "AnimGraphNode_SequencePlayer.h"
#include "AnimGraphNode_StateResult.h"
#include "AnimGraphNode_StateMachine.h"
#include "AnimGraphNode_StateMachineBase.h"
#include "AnimGraphNode_TransitionResult.h"
#include "AnimationStateMachineGraph.h"
#include "AnimStateEntryNode.h"
#include "AnimStateNode.h"
#include "AnimStateNodeBase.h"
#include "AnimStateTransitionNode.h"
#include "BehaviorTree/BehaviorTree.h"
#include "BehaviorTree/BTCompositeNode.h"
#include "BehaviorTree/BTDecorator.h"
#include "BehaviorTree/BTService.h"
#include "BehaviorTree/BTTaskNode.h"
#include "BehaviorTree/Blackboard/BlackboardKeyAllTypes.h"
#include "BehaviorTree/BlackboardAssetProvider.h"
#include "BehaviorTree/BlackboardData.h"
#include "BehaviorTree/BehaviorTreeTypes.h"
#include "BehaviorTree/Composites/BTComposite_Selector.h"
#include "BehaviorTree/Composites/BTComposite_Sequence.h"
#include "BehaviorTree/Composites/BTComposite_SimpleParallel.h"
#include "BehaviorTree/Decorators/BTDecorator_Blackboard.h"
#include "BehaviorTree/Services/BTService_DefaultFocus.h"
#include "BehaviorTree/Tasks/BTTask_MoveTo.h"
#include "BehaviorTree/Tasks/BTTask_RotateToFaceBBEntry.h"
#include "BehaviorTree/Tasks/BTTask_Wait.h"
#include "BehaviorTreeGraph.h"
#include "BehaviorTreeGraphNode.h"
#include "BehaviorTreeGraphNode_Composite.h"
#include "BehaviorTreeGraphNode_Decorator.h"
#include "BehaviorTreeGraphNode_Root.h"
#include "BehaviorTreeGraphNode_Service.h"
#include "BehaviorTreeGraphNode_SimpleParallel.h"
#include "BehaviorTreeGraphNode_Task.h"
#include "BehaviorTreeFactory.h"
#include "BlackboardDataFactory.h"
#include "DataTableEditorUtils.h"

#include "ActorFactories/ActorFactory.h"
#include "Editor.h"
#include "EngineUtils.h"
#include "Factories/Factory.h"
#include "GameFramework/Actor.h"

#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"

#include "K2Node_PromotableOperator.h"
#include "K2Node_VariableGet.h"

#include "Engine/CurveTable.h"
#include "Engine/DataTable.h"
#include "Engine/SkeletalMesh.h"
#include "JsonObjectConverter.h"
#include "Kismet/KismetMathLibrary.h"
#include "EditorAssetLibrary.h"
#include "Factories/AnimBlueprintFactory.h"
#include "Factories/DataTableFactory.h"
#include "Factories/MaterialInstanceConstantFactoryNew.h"
#include "NiagaraConstants.h"
#include "NiagaraEmitter.h"
#include "NiagaraParameterStore.h"
#include "NiagaraRendererProperties.h"
#include "NiagaraScript.h"
#include "NiagaraSimulationStageBase.h"
#include "NiagaraSystem.h"
#include "NiagaraSystemFactoryNew.h"
#include "NiagaraEditorUtilities.h"
#include "PCGComponent.h"
#include "Elements/PCGReroute.h"
#include "PCGGraph.h"
#include "PCGNode.h"
#include "PCGPin.h"
#include "PCGSettings.h"
#include "PCGSubgraph.h"
#include "PCGVolume.h"
#include "RuntimeGen/SchedulingPolicies/PCGSchedulingPolicyBase.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Materials/MaterialInterface.h"
#include "Materials/Material.h"

#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphSchema.h"
#include "EdGraphSchema_BehaviorTree.h"
#include "EdGraphSchema_K2.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "UObject/UObjectHash.h"
#include "UObject/UObjectIterator.h"
#include "UObject/UnrealType.h"
#include "Misc/DefaultValueHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"

FUnrealAIAssetCommands::FUnrealAIAssetCommands()
{
}

namespace
{
    struct FCurveTableImportedKey
    {
        float Time = 0.0f;
        float Value = 0.0f;
    };

    FString GetDataTableKeyFieldName(const UDataTable* DataTable)
    {
        if (!DataTable || DataTable->ImportKeyField.IsEmpty())
        {
            return TEXT("Name");
        }

        return DataTable->ImportKeyField;
    }

    FString GetCurveTableModeName(const UCurveTable* CurveTable)
    {
        if (!CurveTable)
        {
            return TEXT("Empty");
        }

        switch (CurveTable->GetCurveTableMode())
        {
        case ECurveTableMode::SimpleCurves:
            return TEXT("SimpleCurves");
        case ECurveTableMode::RichCurves:
            return TEXT("RichCurves");
        default:
            return TEXT("Empty");
        }
    }

    bool TryParseCurveTableModeName(
        const FString& InCurveTableMode,
        ECurveTableMode& OutCurveTableMode,
        FString& OutCanonicalModeName,
        FString& OutErrorMessage)
    {
        OutErrorMessage.Reset();
        OutCanonicalModeName.Reset();

        const FString NormalizedMode = InCurveTableMode.TrimStartAndEnd().ToLower();
        if (NormalizedMode == TEXT("simple") || NormalizedMode == TEXT("simplecurve") || NormalizedMode == TEXT("simplecurves"))
        {
            OutCurveTableMode = ECurveTableMode::SimpleCurves;
            OutCanonicalModeName = TEXT("SimpleCurves");
            return true;
        }

        if (NormalizedMode == TEXT("rich") || NormalizedMode == TEXT("richcurve") || NormalizedMode == TEXT("richcurves"))
        {
            OutCurveTableMode = ECurveTableMode::RichCurves;
            OutCanonicalModeName = TEXT("RichCurves");
            return true;
        }

        OutErrorMessage = TEXT("'curve_table_mode' must be one of: SimpleCurves, simple, RichCurves, rich");
        return false;
    }

    FString GetGraphSchemaClassName(const UEdGraph* Graph)
    {
        if (!Graph)
        {
            return TEXT("None");
        }

        const UEdGraphSchema* Schema = Graph->GetSchema();
        return Schema ? Schema->GetClass()->GetName() : TEXT("None");
    }

    FString GetNiagaraEmitterModeName(ENiagaraEmitterMode EmitterMode)
    {
        switch (EmitterMode)
        {
        case ENiagaraEmitterMode::Standard:
            return TEXT("Standard");
        case ENiagaraEmitterMode::Stateless:
            return TEXT("Stateless");
        default:
            return TEXT("Unknown");
        }
    }

    FString GetEnumValueName(const UEnum* Enum, int64 Value)
    {
        if (!Enum)
        {
            return TEXT("Unknown");
        }

        FString Name = Enum->GetNameStringByValue(Value);
        return Name.IsEmpty() ? TEXT("Unknown") : Name;
    }

    FString GetNiagaraSimTargetName(ENiagaraSimTarget SimTarget)
    {
        return GetEnumValueName(StaticEnum<ENiagaraSimTarget>(), static_cast<int64>(SimTarget));
    }

    FString GetNiagaraEmitterCalculateBoundModeName(ENiagaraEmitterCalculateBoundMode CalculateBoundMode)
    {
        return GetEnumValueName(StaticEnum<ENiagaraEmitterCalculateBoundMode>(), static_cast<int64>(CalculateBoundMode));
    }

    FString GetNiagaraScriptUsageName(ENiagaraScriptUsage Usage)
    {
        return GetEnumValueName(StaticEnum<ENiagaraScriptUsage>(), static_cast<int64>(Usage));
    }

    FString GetNiagaraScriptCompileStatusName(ENiagaraScriptCompileStatus Status)
    {
        return GetEnumValueName(StaticEnum<ENiagaraScriptCompileStatus>(), static_cast<int64>(Status));
    }

    FString GetNiagaraScriptExecutionModeName(EScriptExecutionMode ExecutionMode)
    {
        return GetEnumValueName(StaticEnum<EScriptExecutionMode>(), static_cast<int64>(ExecutionMode));
    }

    FString GetNiagaraUserParameterDisplayName(const FNiagaraVariable& Parameter)
    {
        FString DisplayName = Parameter.GetName().ToString();
        static const FString UserPrefix = TEXT("User.");
        if (DisplayName.StartsWith(UserPrefix, ESearchCase::IgnoreCase))
        {
            DisplayName.RightChopInline(UserPrefix.Len(), EAllowShrinking::No);
        }
        return DisplayName;
    }

    FString GetNiagaraUserParameterValueKind(const FNiagaraTypeDefinition& TypeDefinition)
    {
        if (TypeDefinition == FNiagaraTypeDefinition::GetFloatDef())
        {
            return TEXT("float");
        }

        if (TypeDefinition == FNiagaraTypeDefinition::GetBoolDef())
        {
            return TEXT("bool");
        }

        if (TypeDefinition == FNiagaraTypeDefinition::GetVec3Def() || TypeDefinition == FNiagaraTypeDefinition::GetPositionDef())
        {
            return TEXT("vector");
        }

        if (TypeDefinition == FNiagaraTypeDefinition::GetColorDef())
        {
            return TEXT("color");
        }

        if (TypeDefinition.IsUObject() && !TypeDefinition.IsDataInterface())
        {
            return TEXT("object");
        }

        if (TypeDefinition.IsDataInterface())
        {
            return TEXT("data_interface");
        }

        return TEXT("unsupported");
    }

    TArray<TSharedPtr<FJsonValue>> BuildNumberArray(std::initializer_list<double> Values)
    {
        TArray<TSharedPtr<FJsonValue>> NumberArray;
        for (double Value : Values)
        {
            NumberArray.Add(MakeShared<FJsonValueNumber>(Value));
        }
        return NumberArray;
    }

    TSharedPtr<FJsonObject> BuildNiagaraParameterSummary(
        const FNiagaraVariable& Parameter,
        const FNiagaraParameterStore* ParameterStore = nullptr)
    {
        TSharedPtr<FJsonObject> ParameterObject = MakeShared<FJsonObject>();
        const FNiagaraTypeDefinition& TypeDefinition = Parameter.GetType();
        const FString ValueKind = GetNiagaraUserParameterValueKind(TypeDefinition);

        ParameterObject->SetStringField(TEXT("name"), Parameter.GetName().ToString());
        ParameterObject->SetStringField(TEXT("display_name"), GetNiagaraUserParameterDisplayName(Parameter));
        ParameterObject->SetStringField(TEXT("type_name"), TypeDefinition.GetNameText().ToString());

        const UClass* TypeClass = TypeDefinition.GetClass();
        ParameterObject->SetStringField(TEXT("type_class_name"), TypeClass ? TypeClass->GetName() : TEXT(""));
        ParameterObject->SetStringField(TEXT("type_class_path"), TypeClass ? TypeClass->GetPathName() : TEXT(""));
        ParameterObject->SetStringField(TEXT("value_kind"), ValueKind);
        ParameterObject->SetBoolField(TEXT("supports_default_value_mutation"),
            ValueKind == TEXT("float") ||
            ValueKind == TEXT("bool") ||
            ValueKind == TEXT("vector") ||
            ValueKind == TEXT("color") ||
            ValueKind == TEXT("object"));

        if (!ParameterStore)
        {
            return ParameterObject;
        }

        if (ValueKind == TEXT("float"))
        {
            ParameterObject->SetBoolField(TEXT("has_value"), true);
            ParameterObject->SetNumberField(TEXT("value"), ParameterStore->GetParameterValue<float>(Parameter));
        }
        else if (ValueKind == TEXT("bool"))
        {
            ParameterObject->SetBoolField(TEXT("has_value"), true);
            ParameterObject->SetBoolField(TEXT("value"), ParameterStore->GetParameterValue<FNiagaraBool>(Parameter).GetValue());
        }
        else if (ValueKind == TEXT("vector"))
        {
            ParameterObject->SetBoolField(TEXT("has_value"), true);
            if (TypeDefinition == FNiagaraTypeDefinition::GetPositionDef())
            {
                const FNiagaraPosition PositionValue = ParameterStore->GetParameterValue<FNiagaraPosition>(Parameter);
                ParameterObject->SetArrayField(TEXT("value"), BuildNumberArray({ PositionValue.X, PositionValue.Y, PositionValue.Z }));
            }
            else
            {
                const FVector3f VectorValue = ParameterStore->GetParameterValue<FVector3f>(Parameter);
                ParameterObject->SetArrayField(TEXT("value"), BuildNumberArray({ VectorValue.X, VectorValue.Y, VectorValue.Z }));
            }
        }
        else if (ValueKind == TEXT("color"))
        {
            const FLinearColor ColorValue = ParameterStore->GetParameterValue<FLinearColor>(Parameter);
            ParameterObject->SetBoolField(TEXT("has_value"), true);
            ParameterObject->SetArrayField(TEXT("value"), BuildNumberArray({ ColorValue.R, ColorValue.G, ColorValue.B, ColorValue.A }));
        }
        else if (ValueKind == TEXT("object"))
        {
            const TObjectPtr<UObject> ObjectValue = ParameterStore->GetUObject(Parameter);
            ParameterObject->SetBoolField(TEXT("has_value"), ObjectValue != nullptr);
            ParameterObject->SetBoolField(TEXT("is_null_object_value"), ObjectValue == nullptr);
            ParameterObject->SetStringField(TEXT("object_name"), ObjectValue ? ObjectValue->GetName() : TEXT(""));
            ParameterObject->SetStringField(TEXT("object_path"), ObjectValue ? ObjectValue->GetPathName() : TEXT(""));
            ParameterObject->SetStringField(TEXT("object_class_name"), ObjectValue ? ObjectValue->GetClass()->GetName() : TEXT(""));
            ParameterObject->SetStringField(TEXT("object_class_path"), ObjectValue ? ObjectValue->GetClass()->GetPathName() : TEXT(""));
        }

        return ParameterObject;
    }

    TArray<TSharedPtr<FJsonValue>> BuildNiagaraParameterArray(
        const TArray<FNiagaraVariable>& Parameters,
        const FNiagaraParameterStore* ParameterStore = nullptr)
    {
        TArray<TSharedPtr<FJsonValue>> ParameterArray;
        for (const FNiagaraVariable& Parameter : Parameters)
        {
            ParameterArray.Add(MakeShared<FJsonValueObject>(BuildNiagaraParameterSummary(Parameter, ParameterStore)));
        }

        return ParameterArray;
    }

    bool TryResolveSupportedNiagaraUserParameterType(
        const FString& InTypeName,
        FNiagaraTypeDefinition& OutTypeDefinition,
        FString& OutCanonicalTypeName)
    {
        const FString NormalizedTypeName = InTypeName.TrimStartAndEnd().ToLower();
        if (NormalizedTypeName == TEXT("float") || NormalizedTypeName == TEXT("number") || NormalizedTypeName == TEXT("scalar"))
        {
            OutTypeDefinition = FNiagaraTypeDefinition::GetFloatDef();
            OutCanonicalTypeName = TEXT("float");
            return true;
        }

        if (NormalizedTypeName == TEXT("bool") || NormalizedTypeName == TEXT("boolean"))
        {
            OutTypeDefinition = FNiagaraTypeDefinition::GetBoolDef();
            OutCanonicalTypeName = TEXT("bool");
            return true;
        }

        if (NormalizedTypeName == TEXT("vector") || NormalizedTypeName == TEXT("vec3") || NormalizedTypeName == TEXT("float3") || NormalizedTypeName == TEXT("position"))
        {
            OutTypeDefinition = FNiagaraTypeDefinition::GetVec3Def();
            OutCanonicalTypeName = TEXT("vector");
            return true;
        }

        if (NormalizedTypeName == TEXT("color") || NormalizedTypeName == TEXT("linear_color"))
        {
            OutTypeDefinition = FNiagaraTypeDefinition::GetColorDef();
            OutCanonicalTypeName = TEXT("color");
            return true;
        }

        if (NormalizedTypeName == TEXT("object") || NormalizedTypeName == TEXT("uobject") || NormalizedTypeName == TEXT("asset"))
        {
            OutTypeDefinition = FNiagaraTypeDefinition::GetUObjectDef();
            OutCanonicalTypeName = TEXT("object");
            return true;
        }

        OutCanonicalTypeName.Reset();
        return false;
    }

    bool ResolveNiagaraUserParameter(
        const FNiagaraUserRedirectionParameterStore& ParameterStore,
        const FString& ParameterName,
        FNiagaraVariable& OutParameter)
    {
        TArray<FNiagaraVariable> Parameters;
        ParameterStore.GetParameters(Parameters);

        const FString TrimmedParameterName = ParameterName.TrimStartAndEnd();
        for (const FNiagaraVariable& Parameter : Parameters)
        {
            const FString FullName = Parameter.GetName().ToString();
            const FString DisplayName = GetNiagaraUserParameterDisplayName(Parameter);
            if (FullName.Equals(TrimmedParameterName, ESearchCase::IgnoreCase) ||
                DisplayName.Equals(TrimmedParameterName, ESearchCase::IgnoreCase))
            {
                OutParameter = Parameter;
                return true;
            }
        }

        return false;
    }

    bool TryReadJsonNumberArrayField(
        const TSharedPtr<FJsonObject>& JsonObject,
        const FString& FieldName,
        int32 ExpectedCount,
        TArray<double>& OutValues)
    {
        OutValues.Reset();

        const TArray<TSharedPtr<FJsonValue>>* JsonArray = nullptr;
        if (!JsonObject->TryGetArrayField(FieldName, JsonArray) || !JsonArray || JsonArray->Num() != ExpectedCount)
        {
            return false;
        }

        for (const TSharedPtr<FJsonValue>& JsonValue : *JsonArray)
        {
            if (!JsonValue.IsValid() || JsonValue->Type != EJson::Number)
            {
                OutValues.Reset();
                return false;
            }

            OutValues.Add(JsonValue->AsNumber());
        }

        return true;
    }

    bool IsNiagaraCompileStatusHealthy(const FString& CompileStatus)
    {
        return CompileStatus == TEXT("NCS_UpToDate") ||
            CompileStatus == TEXT("NCS_UpToDateWithWarnings") ||
            CompileStatus == TEXT("NCS_ComputeUpToDateWithWarnings");
    }

    bool IsNiagaraCompileStatusWarningOnly(const FString& CompileStatus)
    {
        return CompileStatus == TEXT("NCS_UpToDateWithWarnings") ||
            CompileStatus == TEXT("NCS_ComputeUpToDateWithWarnings") ||
            CompileStatus == TEXT("NCS_Unknown") ||
            CompileStatus == TEXT("NCS_Dirty") ||
            CompileStatus == TEXT("NCS_BeingCreated");
    }

    TArray<TSharedPtr<FJsonValue>> BuildStringArray(const TArray<FString>& Strings)
    {
        TArray<TSharedPtr<FJsonValue>> JsonArray;
        for (const FString& Value : Strings)
        {
            JsonArray.Add(MakeShared<FJsonValueString>(Value));
        }
        return JsonArray;
    }

    FString GetBlackboardKeyValueKind(const UBlackboardKeyType* KeyType)
    {
        if (!KeyType)
        {
            return TEXT("none");
        }

        if (KeyType->IsA<UBlackboardKeyType_Bool>())
        {
            return TEXT("bool");
        }

        if (KeyType->IsA<UBlackboardKeyType_Int>())
        {
            return TEXT("int");
        }

        if (KeyType->IsA<UBlackboardKeyType_Float>())
        {
            return TEXT("float");
        }

        if (KeyType->IsA<UBlackboardKeyType_Name>())
        {
            return TEXT("name");
        }

        if (KeyType->IsA<UBlackboardKeyType_String>())
        {
            return TEXT("string");
        }

        if (KeyType->IsA<UBlackboardKeyType_Vector>())
        {
            return TEXT("vector");
        }

        if (KeyType->IsA<UBlackboardKeyType_Object>())
        {
            return TEXT("object");
        }

        if (KeyType->IsA<UBlackboardKeyType_Class>())
        {
            return TEXT("class");
        }

        if (KeyType->IsA<UBlackboardKeyType_Enum>() || KeyType->IsA<UBlackboardKeyType_NativeEnum>())
        {
            return TEXT("enum");
        }

        return TEXT("unsupported");
    }

    FString GetObjectNameOrEmpty(const UObject* Object)
    {
        return Object ? Object->GetName() : TEXT("");
    }

    FString GetObjectPathOrEmpty(const UObject* Object)
    {
        return Object ? Object->GetPathName() : TEXT("");
    }

    UClass* ResolveClassPath(const FString& RequestedPath, FString& OutErrorMessage)
    {
        OutErrorMessage.Reset();

        const FString TrimmedPath = RequestedPath.TrimStartAndEnd();
        if (TrimmedPath.IsEmpty())
        {
            return nullptr;
        }

        if (UClass* LoadedClass = FindObject<UClass>(nullptr, *TrimmedPath))
        {
            return LoadedClass;
        }

        if (UClass* LoadedClass = LoadObject<UClass>(nullptr, *TrimmedPath))
        {
            return LoadedClass;
        }

        OutErrorMessage = FString::Printf(TEXT("Failed to resolve class path: %s"), *TrimmedPath);
        return nullptr;
    }

    UObject* ResolveObjectPath(const FString& RequestedPath, FString& OutErrorMessage)
    {
        OutErrorMessage.Reset();

        const FString TrimmedPath = RequestedPath.TrimStartAndEnd();
        if (TrimmedPath.IsEmpty())
        {
            return nullptr;
        }

        if (UObject* LoadedAsset = UEditorAssetLibrary::LoadAsset(TrimmedPath))
        {
            return LoadedAsset;
        }

        if (UObject* LoadedObject = StaticLoadObject(UObject::StaticClass(), nullptr, *TrimmedPath))
        {
            return LoadedObject;
        }

        OutErrorMessage = FString::Printf(TEXT("Failed to resolve object path: %s"), *TrimmedPath);
        return nullptr;
    }

    bool TryResolveSupportedBlackboardKeyTypeClass(
        const FString& RequestedTypeName,
        UClass*& OutKeyTypeClass,
        FString& OutCanonicalTypeName,
        FString& OutErrorMessage)
    {
        OutKeyTypeClass = nullptr;
        OutCanonicalTypeName.Reset();
        OutErrorMessage.Reset();

        const FString NormalizedTypeName = RequestedTypeName.TrimStartAndEnd().ToLower();
        if (NormalizedTypeName.IsEmpty())
        {
            OutErrorMessage = TEXT("Missing or empty 'key_type' value");
            return false;
        }

        if (NormalizedTypeName == TEXT("bool") || NormalizedTypeName == TEXT("boolean"))
        {
            OutKeyTypeClass = UBlackboardKeyType_Bool::StaticClass();
            OutCanonicalTypeName = TEXT("bool");
            return true;
        }

        if (NormalizedTypeName == TEXT("int") || NormalizedTypeName == TEXT("integer"))
        {
            OutKeyTypeClass = UBlackboardKeyType_Int::StaticClass();
            OutCanonicalTypeName = TEXT("int");
            return true;
        }

        if (NormalizedTypeName == TEXT("float") || NormalizedTypeName == TEXT("number"))
        {
            OutKeyTypeClass = UBlackboardKeyType_Float::StaticClass();
            OutCanonicalTypeName = TEXT("float");
            return true;
        }

        if (NormalizedTypeName == TEXT("name"))
        {
            OutKeyTypeClass = UBlackboardKeyType_Name::StaticClass();
            OutCanonicalTypeName = TEXT("name");
            return true;
        }

        if (NormalizedTypeName == TEXT("string") || NormalizedTypeName == TEXT("text"))
        {
            OutKeyTypeClass = UBlackboardKeyType_String::StaticClass();
            OutCanonicalTypeName = TEXT("string");
            return true;
        }

        if (NormalizedTypeName == TEXT("vector"))
        {
            OutKeyTypeClass = UBlackboardKeyType_Vector::StaticClass();
            OutCanonicalTypeName = TEXT("vector");
            return true;
        }

        if (NormalizedTypeName == TEXT("object"))
        {
            OutKeyTypeClass = UBlackboardKeyType_Object::StaticClass();
            OutCanonicalTypeName = TEXT("object");
            return true;
        }

        if (NormalizedTypeName == TEXT("class"))
        {
            OutKeyTypeClass = UBlackboardKeyType_Class::StaticClass();
            OutCanonicalTypeName = TEXT("class");
            return true;
        }

        OutErrorMessage = TEXT("'key_type' must be one of: bool, int, float, name, string, vector, object, class");
        return false;
    }

    int32 FindLocalBlackboardKeyIndex(const UBlackboardData& BlackboardData, const FName KeyName)
    {
        for (int32 Index = 0; Index < BlackboardData.Keys.Num(); ++Index)
        {
            if (BlackboardData.Keys[Index].EntryName == KeyName)
            {
                return Index;
            }
        }

        return INDEX_NONE;
    }

    bool DoesBlackboardKeyNameExistInParentChain(const UBlackboardData& BlackboardData, const FName KeyName)
    {
        const UBlackboardData* Parent = BlackboardData.Parent;
        while (Parent)
        {
            for (const FBlackboardEntry& ParentEntry : Parent->Keys)
            {
                if (ParentEntry.EntryName == KeyName)
                {
                    return true;
                }
            }

            Parent = Parent->Parent;
        }

        return false;
    }

    void AppendBlackboardParentChain(const UBlackboardData* BlackboardData, TArray<TSharedPtr<FJsonValue>>& OutParentChain)
    {
        if (!BlackboardData || !BlackboardData->Parent)
        {
            return;
        }

        AppendBlackboardParentChain(BlackboardData->Parent, OutParentChain);

        TSharedPtr<FJsonObject> ParentObject = MakeShared<FJsonObject>();
        ParentObject->SetStringField(TEXT("name"), BlackboardData->Parent->GetName());
        ParentObject->SetStringField(TEXT("asset_path"), BlackboardData->Parent->GetPathName());
        ParentObject->SetStringField(TEXT("class_name"), BlackboardData->Parent->GetClass()->GetName());
        OutParentChain.Add(MakeShared<FJsonValueObject>(ParentObject));
    }

    TSharedPtr<FJsonObject> BuildBlackboardEntrySummary(
        const UBlackboardData& BlackboardContext,
        const UBlackboardData& SourceBlackboard,
        const FBlackboardEntry& Entry,
        bool bIsInherited)
    {
        TSharedPtr<FJsonObject> EntryObject = MakeShared<FJsonObject>();
        const int32 KeyId = static_cast<int32>(BlackboardContext.GetKeyID(Entry.EntryName));
        EntryObject->SetStringField(TEXT("name"), Entry.EntryName.ToString());
        EntryObject->SetNumberField(TEXT("key_id"), KeyId);
        EntryObject->SetBoolField(TEXT("is_inherited"), bIsInherited);
        EntryObject->SetStringField(TEXT("source_blackboard_name"), SourceBlackboard.GetName());
        EntryObject->SetStringField(TEXT("source_blackboard_path"), SourceBlackboard.GetPathName());
        EntryObject->SetBoolField(TEXT("instance_synced"), Entry.bInstanceSynced != 0);
#if WITH_EDITORONLY_DATA
        EntryObject->SetStringField(TEXT("description"), Entry.EntryDescription);
        EntryObject->SetStringField(TEXT("category"), Entry.EntryCategory.ToString());
#else
        EntryObject->SetStringField(TEXT("description"), TEXT(""));
        EntryObject->SetStringField(TEXT("category"), TEXT(""));
#endif

        const UBlackboardKeyType* KeyType = Entry.KeyType;
        const UClass* KeyTypeClass = KeyType ? KeyType->GetClass() : nullptr;
        EntryObject->SetStringField(TEXT("type_name"), KeyTypeClass ? KeyTypeClass->GetDisplayNameText().ToString() : TEXT("None"));
        EntryObject->SetStringField(TEXT("key_type_class_name"), KeyTypeClass ? KeyTypeClass->GetName() : TEXT("None"));
        EntryObject->SetStringField(TEXT("key_type_class_path"), KeyTypeClass ? KeyTypeClass->GetPathName() : TEXT(""));

        const FString ValueKind = GetBlackboardKeyValueKind(KeyType);
        EntryObject->SetStringField(TEXT("value_kind"), ValueKind);
        EntryObject->SetBoolField(TEXT("supports_default_value_mutation"),
            ValueKind == TEXT("bool") ||
            ValueKind == TEXT("int") ||
            ValueKind == TEXT("float") ||
            ValueKind == TEXT("name") ||
            ValueKind == TEXT("string") ||
            ValueKind == TEXT("vector") ||
            ValueKind == TEXT("object") ||
            ValueKind == TEXT("class"));
        EntryObject->SetBoolField(TEXT("supports_base_class_filter"), ValueKind == TEXT("object") || ValueKind == TEXT("class"));
        EntryObject->SetBoolField(TEXT("supports_enum_metadata"), ValueKind == TEXT("enum"));
        EntryObject->SetBoolField(TEXT("has_default_value"), false);
        EntryObject->SetBoolField(TEXT("uses_default_value"), false);

        if (const UBlackboardKeyType_Bool* BoolKey = Cast<UBlackboardKeyType_Bool>(KeyType))
        {
            EntryObject->SetBoolField(TEXT("default_value"), BoolKey->bDefaultValue);
            EntryObject->SetBoolField(TEXT("has_default_value"), true);
            EntryObject->SetBoolField(TEXT("uses_default_value"), true);
        }
        else if (const UBlackboardKeyType_Int* IntKey = Cast<UBlackboardKeyType_Int>(KeyType))
        {
            EntryObject->SetNumberField(TEXT("default_value"), IntKey->DefaultValue);
            EntryObject->SetBoolField(TEXT("has_default_value"), true);
            EntryObject->SetBoolField(TEXT("uses_default_value"), true);
        }
        else if (const UBlackboardKeyType_Float* FloatKey = Cast<UBlackboardKeyType_Float>(KeyType))
        {
            EntryObject->SetNumberField(TEXT("default_value"), FloatKey->DefaultValue);
            EntryObject->SetBoolField(TEXT("has_default_value"), true);
            EntryObject->SetBoolField(TEXT("uses_default_value"), true);
        }
        else if (const UBlackboardKeyType_Name* NameKey = Cast<UBlackboardKeyType_Name>(KeyType))
        {
            EntryObject->SetStringField(TEXT("default_value"), NameKey->DefaultValue.ToString());
            EntryObject->SetBoolField(TEXT("has_default_value"), !NameKey->DefaultValue.IsNone());
            EntryObject->SetBoolField(TEXT("uses_default_value"), !NameKey->DefaultValue.IsNone());
        }
        else if (const UBlackboardKeyType_String* StringKey = Cast<UBlackboardKeyType_String>(KeyType))
        {
            EntryObject->SetStringField(TEXT("default_value"), StringKey->DefaultValue);
            EntryObject->SetBoolField(TEXT("has_default_value"), !StringKey->DefaultValue.IsEmpty());
            EntryObject->SetBoolField(TEXT("uses_default_value"), !StringKey->DefaultValue.IsEmpty());
        }
        else if (const UBlackboardKeyType_Vector* VectorKey = Cast<UBlackboardKeyType_Vector>(KeyType))
        {
            EntryObject->SetArrayField(TEXT("default_value"), BuildNumberArray({ VectorKey->DefaultValue.X, VectorKey->DefaultValue.Y, VectorKey->DefaultValue.Z }));
            EntryObject->SetBoolField(TEXT("has_default_value"), VectorKey->bUseDefaultValue);
            EntryObject->SetBoolField(TEXT("uses_default_value"), VectorKey->bUseDefaultValue);
        }
        else if (const UBlackboardKeyType_Object* ObjectKey = Cast<UBlackboardKeyType_Object>(KeyType))
        {
            EntryObject->SetStringField(TEXT("base_class_name"), GetObjectNameOrEmpty(ObjectKey->BaseClass));
            EntryObject->SetStringField(TEXT("base_class_path"), GetObjectPathOrEmpty(ObjectKey->BaseClass));
            EntryObject->SetStringField(TEXT("default_value_name"), GetObjectNameOrEmpty(ObjectKey->DefaultValue));
            EntryObject->SetStringField(TEXT("default_value_path"), GetObjectPathOrEmpty(ObjectKey->DefaultValue));
            EntryObject->SetBoolField(TEXT("has_default_value"), ObjectKey->DefaultValue != nullptr);
            EntryObject->SetBoolField(TEXT("uses_default_value"), ObjectKey->DefaultValue != nullptr);
            EntryObject->SetBoolField(TEXT("is_null_default_value"), ObjectKey->DefaultValue == nullptr);
        }
        else if (const UBlackboardKeyType_Class* ClassKey = Cast<UBlackboardKeyType_Class>(KeyType))
        {
            EntryObject->SetStringField(TEXT("base_class_name"), GetObjectNameOrEmpty(ClassKey->BaseClass));
            EntryObject->SetStringField(TEXT("base_class_path"), GetObjectPathOrEmpty(ClassKey->BaseClass));
            EntryObject->SetStringField(TEXT("default_value_name"), GetObjectNameOrEmpty(ClassKey->DefaultValue));
            EntryObject->SetStringField(TEXT("default_value_path"), GetObjectPathOrEmpty(ClassKey->DefaultValue));
            EntryObject->SetBoolField(TEXT("has_default_value"), ClassKey->DefaultValue != nullptr);
            EntryObject->SetBoolField(TEXT("uses_default_value"), ClassKey->DefaultValue != nullptr);
            EntryObject->SetBoolField(TEXT("is_null_default_value"), ClassKey->DefaultValue == nullptr);
        }
        else if (const UBlackboardKeyType_Enum* EnumKey = Cast<UBlackboardKeyType_Enum>(KeyType))
        {
            EntryObject->SetStringField(TEXT("enum_type_name"), GetObjectNameOrEmpty(EnumKey->EnumType));
            EntryObject->SetStringField(TEXT("enum_type_path"), GetObjectPathOrEmpty(EnumKey->EnumType));
            EntryObject->SetStringField(TEXT("enum_name_override"), EnumKey->EnumName);
            EntryObject->SetBoolField(TEXT("is_enum_name_valid"), EnumKey->bIsEnumNameValid != 0);
            EntryObject->SetNumberField(TEXT("default_value"), EnumKey->DefaultValue);
            EntryObject->SetStringField(TEXT("default_value_name"), EnumKey->EnumType ? EnumKey->EnumType->GetNameStringByValue(EnumKey->DefaultValue) : TEXT(""));
            EntryObject->SetBoolField(TEXT("has_default_value"), EnumKey->EnumType != nullptr || !EnumKey->EnumName.IsEmpty());
        }
        else if (const UBlackboardKeyType_NativeEnum* NativeEnumKey = Cast<UBlackboardKeyType_NativeEnum>(KeyType))
        {
            EntryObject->SetStringField(TEXT("enum_type_name"), GetObjectNameOrEmpty(NativeEnumKey->EnumType));
            EntryObject->SetStringField(TEXT("enum_type_path"), GetObjectPathOrEmpty(NativeEnumKey->EnumType));
            EntryObject->SetStringField(TEXT("enum_name_override"), NativeEnumKey->EnumName);
        }

        return EntryObject;
    }

    void AppendInheritedBlackboardEntries(
        const UBlackboardData& BlackboardContext,
        const UBlackboardData* ParentBlackboard,
        TArray<TSharedPtr<FJsonValue>>& OutKeys)
    {
        if (!ParentBlackboard)
        {
            return;
        }

        AppendInheritedBlackboardEntries(BlackboardContext, ParentBlackboard->Parent, OutKeys);

        for (const FBlackboardEntry& Entry : ParentBlackboard->Keys)
        {
            OutKeys.Add(MakeShared<FJsonValueObject>(BuildBlackboardEntrySummary(BlackboardContext, *ParentBlackboard, Entry, true)));
        }
    }

    TSharedPtr<FJsonObject> BuildBlackboardContentSummary(UBlackboardData& BlackboardData, const FString& AssetPath)
    {
        TArray<TSharedPtr<FJsonValue>> ParentChain;
        AppendBlackboardParentChain(&BlackboardData, ParentChain);

        TArray<TSharedPtr<FJsonValue>> InheritedKeys;
        AppendInheritedBlackboardEntries(BlackboardData, BlackboardData.Parent, InheritedKeys);

        TArray<TSharedPtr<FJsonValue>> LocalKeys;
        for (const FBlackboardEntry& Entry : BlackboardData.Keys)
        {
            LocalKeys.Add(MakeShared<FJsonValueObject>(BuildBlackboardEntrySummary(BlackboardData, BlackboardData, Entry, false)));
        }

        TArray<TSharedPtr<FJsonValue>> AllKeys = InheritedKeys;
        AllKeys.Append(LocalKeys);

        TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
        Result->SetStringField(TEXT("asset_path"), AssetPath);
        Result->SetStringField(TEXT("blackboard_name"), BlackboardData.GetName());
        Result->SetStringField(TEXT("class_name"), BlackboardData.GetClass()->GetName());
        Result->SetStringField(TEXT("class_path"), BlackboardData.GetClass()->GetPathName());
        Result->SetBoolField(TEXT("has_parent"), BlackboardData.Parent != nullptr);
        Result->SetStringField(TEXT("parent_blackboard_name"), BlackboardData.Parent ? BlackboardData.Parent->GetName() : TEXT(""));
        Result->SetStringField(TEXT("parent_blackboard_path"), BlackboardData.Parent ? BlackboardData.Parent->GetPathName() : TEXT(""));
        Result->SetBoolField(TEXT("is_valid"), BlackboardData.IsValid());
        Result->SetArrayField(TEXT("parent_chain"), ParentChain);
        Result->SetArrayField(TEXT("keys"), AllKeys);
        Result->SetArrayField(TEXT("local_keys"), LocalKeys);
        Result->SetArrayField(TEXT("inherited_keys"), InheritedKeys);
        Result->SetNumberField(TEXT("key_count"), AllKeys.Num());
        Result->SetNumberField(TEXT("local_key_count"), LocalKeys.Num());
        Result->SetNumberField(TEXT("inherited_key_count"), InheritedKeys.Num());
        return Result;
    }

    void GetBlackboardOwnerClasses(TArray<const UClass*>& OutBlackboardOwnerClasses)
    {
        for (TObjectIterator<UClass> ClassIt; ClassIt; ++ClassIt)
        {
            UClass* Class = *ClassIt;
            if (Class && Class->ImplementsInterface(UBlackboardAssetProvider::StaticClass()))
            {
                OutBlackboardOwnerClasses.Add(Class);
            }
        }
    }

    void LoadBlackboardReferencerAssets(const UBlackboardData& BlackboardData, TArray<UObject*>& OutReferencerAssets)
    {
        TArray<const UClass*> BlackboardOwnerClasses;
        GetBlackboardOwnerClasses(BlackboardOwnerClasses);

        IAssetRegistry& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();

        TArray<FName> ReferencerPackages;
        AssetRegistry.GetReferencers(
            BlackboardData.GetOutermost()->GetFName(),
            ReferencerPackages,
            UE::AssetRegistry::EDependencyCategory::Package,
            UE::AssetRegistry::EDependencyQuery::Hard);

        for (const FName& ReferencerPackage : ReferencerPackages)
        {
            TArray<FAssetData> Assets;
            AssetRegistry.GetAssetsByPackageName(ReferencerPackage, Assets);

            for (const FAssetData& Asset : Assets)
            {
                if (BlackboardOwnerClasses.Find(Asset.GetClass()) == INDEX_NONE)
                {
                    continue;
                }

                UObject* AssetObject = Asset.GetAsset();
                const IBlackboardAssetProvider* BlackboardProvider = Cast<const IBlackboardAssetProvider>(AssetObject);
                if (BlackboardProvider && BlackboardProvider->GetBlackboardAsset() == &BlackboardData)
                {
                    OutReferencerAssets.Add(AssetObject);
                }
            }
        }
    }

    void UpdateExternalBlackboardKeyReferences(
        const FName OldKeyName,
        const FName NewKeyName,
        const TArray<UObject*>& ReferencerAssets)
    {
        for (UObject* Asset : ReferencerAssets)
        {
            if (!Asset)
            {
                continue;
            }

            TArray<UObject*> Objects;
            GetObjectsWithOuter(Asset->GetOutermost(), Objects);
            for (UObject* SubObject : Objects)
            {
                if (!SubObject)
                {
                    continue;
                }

                for (TFieldIterator<FStructProperty> It(SubObject->GetClass()); It; ++It)
                {
                    if (!It->GetCPPType(nullptr, CPPF_None).Contains(TEXT("FBlackboardKeySelector")))
                    {
                        continue;
                    }

                    FBlackboardKeySelector* PropertyValue = reinterpret_cast<FBlackboardKeySelector*>(It->ContainerPtrToValuePtr<uint8>(SubObject));
                    if (PropertyValue && PropertyValue->SelectedKeyName == OldKeyName)
                    {
                        SubObject->Modify();
                        PropertyValue->SelectedKeyName = NewKeyName;
                    }
                }
            }
        }
    }

    bool TryApplyBlackboardEntryMutation(
        UBlackboardData& BlackboardData,
        FBlackboardEntry& Entry,
        const TSharedPtr<FJsonObject>& OperationObject,
        const bool bRequireKeyType,
        FString& OutCanonicalTypeName,
        FString& OutErrorMessage)
    {
        OutCanonicalTypeName.Reset();
        OutErrorMessage.Reset();

        FString RequestedKeyType;
        const bool bHasRequestedKeyType = OperationObject->TryGetStringField(TEXT("key_type"), RequestedKeyType) && !RequestedKeyType.TrimStartAndEnd().IsEmpty();
        if (bRequireKeyType && !bHasRequestedKeyType && !Entry.KeyType)
        {
            OutErrorMessage = TEXT("Missing or empty 'key_type' for Blackboard key create/upsert operation");
            return false;
        }

        if (bHasRequestedKeyType)
        {
            UClass* KeyTypeClass = nullptr;
            if (!TryResolveSupportedBlackboardKeyTypeClass(RequestedKeyType, KeyTypeClass, OutCanonicalTypeName, OutErrorMessage))
            {
                return false;
            }

            if (!Entry.KeyType || Entry.KeyType->GetClass() != KeyTypeClass)
            {
                Entry.KeyType = NewObject<UBlackboardKeyType>(&BlackboardData, KeyTypeClass);
            }
        }
        else if (Entry.KeyType)
        {
            OutCanonicalTypeName = GetBlackboardKeyValueKind(Entry.KeyType);
        }

        if (!Entry.KeyType)
        {
            OutErrorMessage = TEXT("Blackboard key mutation could not resolve a supported key type");
            return false;
        }

        bool bInstanceSynced = false;
        if (OperationObject->TryGetBoolField(TEXT("instance_synced"), bInstanceSynced))
        {
            Entry.bInstanceSynced = bInstanceSynced ? 1 : 0;
        }

#if WITH_EDITORONLY_DATA
        FString Description;
        if (OperationObject->TryGetStringField(TEXT("description"), Description))
        {
            Entry.EntryDescription = Description;
        }

        FString Category;
        if (OperationObject->TryGetStringField(TEXT("category"), Category))
        {
            Category = Category.TrimStartAndEnd();
            Entry.EntryCategory = Category.IsEmpty() ? NAME_None : FName(*Category);
        }
#endif

        if (UBlackboardKeyType_Bool* BoolKey = Cast<UBlackboardKeyType_Bool>(Entry.KeyType))
        {
            bool DefaultValue = false;
            if (OperationObject->TryGetBoolField(TEXT("default_value"), DefaultValue))
            {
                BoolKey->bDefaultValue = DefaultValue;
            }
            return true;
        }

        if (UBlackboardKeyType_Int* IntKey = Cast<UBlackboardKeyType_Int>(Entry.KeyType))
        {
            double NumberValue = 0.0;
            if (OperationObject->TryGetNumberField(TEXT("default_value"), NumberValue))
            {
                if (NumberValue < static_cast<double>(MIN_int32) || NumberValue > static_cast<double>(MAX_int32))
                {
                    OutErrorMessage = TEXT("Int Blackboard keys require a 'default_value' within int32 range");
                    return false;
                }

                const int32 IntValue = static_cast<int32>(NumberValue);
                if (!FMath::IsNearlyEqual(NumberValue, static_cast<double>(IntValue)))
                {
                    OutErrorMessage = TEXT("Int Blackboard keys require an integer 'default_value'");
                    return false;
                }

                IntKey->DefaultValue = IntValue;
            }
            return true;
        }

        if (UBlackboardKeyType_Float* FloatKey = Cast<UBlackboardKeyType_Float>(Entry.KeyType))
        {
            double NumberValue = 0.0;
            if (OperationObject->TryGetNumberField(TEXT("default_value"), NumberValue))
            {
                FloatKey->DefaultValue = static_cast<float>(NumberValue);
            }
            return true;
        }

        if (UBlackboardKeyType_Name* NameKey = Cast<UBlackboardKeyType_Name>(Entry.KeyType))
        {
            FString DefaultValue;
            if (OperationObject->TryGetStringField(TEXT("default_value"), DefaultValue))
            {
                DefaultValue = DefaultValue.TrimStartAndEnd();
                NameKey->DefaultValue = DefaultValue.IsEmpty() ? NAME_None : FName(*DefaultValue);
            }
            return true;
        }

        if (UBlackboardKeyType_String* StringKey = Cast<UBlackboardKeyType_String>(Entry.KeyType))
        {
            FString DefaultValue;
            if (OperationObject->TryGetStringField(TEXT("default_value"), DefaultValue))
            {
                StringKey->DefaultValue = DefaultValue;
            }
            return true;
        }

        if (UBlackboardKeyType_Vector* VectorKey = Cast<UBlackboardKeyType_Vector>(Entry.KeyType))
        {
            TArray<double> VectorValues;
            if (TryReadJsonNumberArrayField(OperationObject, TEXT("default_value"), 3, VectorValues))
            {
                VectorKey->DefaultValue = FVector(
                    static_cast<float>(VectorValues[0]),
                    static_cast<float>(VectorValues[1]),
                    static_cast<float>(VectorValues[2]));
                VectorKey->bUseDefaultValue = true;
            }

            bool bUseDefaultValue = false;
            if (OperationObject->TryGetBoolField(TEXT("use_default_value"), bUseDefaultValue) && !bUseDefaultValue)
            {
                VectorKey->DefaultValue = FVector::ZeroVector;
                VectorKey->bUseDefaultValue = false;
            }
            return true;
        }

        if (UBlackboardKeyType_Object* ObjectKey = Cast<UBlackboardKeyType_Object>(Entry.KeyType))
        {
            FString BaseClassPath;
            if (OperationObject->TryGetStringField(TEXT("base_class_path"), BaseClassPath))
            {
                BaseClassPath = BaseClassPath.TrimStartAndEnd();
                if (BaseClassPath.IsEmpty())
                {
                    ObjectKey->BaseClass = UObject::StaticClass();
                }
                else
                {
                    FString ResolveError;
                    UClass* BaseClass = ResolveClassPath(BaseClassPath, ResolveError);
                    if (!BaseClass)
                    {
                        OutErrorMessage = ResolveError;
                        return false;
                    }

                    ObjectKey->BaseClass = BaseClass;
                }
            }

            FString DefaultValuePath;
            if (OperationObject->TryGetStringField(TEXT("default_value_path"), DefaultValuePath))
            {
                DefaultValuePath = DefaultValuePath.TrimStartAndEnd();
                if (DefaultValuePath.IsEmpty())
                {
                    ObjectKey->DefaultValue = nullptr;
                }
                else
                {
                    FString ResolveError;
                    UObject* DefaultValueObject = ResolveObjectPath(DefaultValuePath, ResolveError);
                    if (!DefaultValueObject)
                    {
                        OutErrorMessage = ResolveError;
                        return false;
                    }

                    if (ObjectKey->BaseClass && !DefaultValueObject->IsA(ObjectKey->BaseClass))
                    {
                        OutErrorMessage = FString::Printf(
                            TEXT("Object default value '%s' is not compatible with base class '%s'"),
                            *DefaultValuePath,
                            *ObjectKey->BaseClass->GetPathName());
                        return false;
                    }

                    ObjectKey->DefaultValue = DefaultValueObject;
                }
            }
            return true;
        }

        if (UBlackboardKeyType_Class* ClassKey = Cast<UBlackboardKeyType_Class>(Entry.KeyType))
        {
            FString BaseClassPath;
            if (OperationObject->TryGetStringField(TEXT("base_class_path"), BaseClassPath))
            {
                BaseClassPath = BaseClassPath.TrimStartAndEnd();
                if (BaseClassPath.IsEmpty())
                {
                    ClassKey->BaseClass = UObject::StaticClass();
                }
                else
                {
                    FString ResolveError;
                    UClass* BaseClass = ResolveClassPath(BaseClassPath, ResolveError);
                    if (!BaseClass)
                    {
                        OutErrorMessage = ResolveError;
                        return false;
                    }

                    ClassKey->BaseClass = BaseClass;
                }
            }

            FString DefaultValuePath;
            if (OperationObject->TryGetStringField(TEXT("default_value_path"), DefaultValuePath))
            {
                DefaultValuePath = DefaultValuePath.TrimStartAndEnd();
                if (DefaultValuePath.IsEmpty())
                {
                    ClassKey->DefaultValue = nullptr;
                }
                else
                {
                    FString ResolveError;
                    UClass* DefaultValueClass = ResolveClassPath(DefaultValuePath, ResolveError);
                    if (!DefaultValueClass)
                    {
                        OutErrorMessage = ResolveError;
                        return false;
                    }

                    if (ClassKey->BaseClass && !DefaultValueClass->IsChildOf(ClassKey->BaseClass))
                    {
                        OutErrorMessage = FString::Printf(
                            TEXT("Class default value '%s' is not a child of base class '%s'"),
                            *DefaultValuePath,
                            *ClassKey->BaseClass->GetPathName());
                        return false;
                    }

                    ClassKey->DefaultValue = DefaultValueClass;
                }
            }
            return true;
        }

        OutErrorMessage = FString::Printf(
            TEXT("Blackboard key type '%s' is not supported by this mutation helper"),
            *Entry.KeyType->GetClass()->GetName());
        return false;
    }

    TSharedPtr<FJsonObject> BuildNiagaraScriptSummary(const UNiagaraScript* Script)
    {
        TSharedPtr<FJsonObject> ScriptObject = MakeShared<FJsonObject>();
        if (!Script)
        {
            ScriptObject->SetStringField(TEXT("name"), TEXT(""));
            ScriptObject->SetStringField(TEXT("path"), TEXT(""));
            ScriptObject->SetStringField(TEXT("usage"), TEXT("Unknown"));
            ScriptObject->SetStringField(TEXT("usage_id"), TEXT(""));
            ScriptObject->SetBoolField(TEXT("is_compilable"), false);
            ScriptObject->SetStringField(TEXT("compile_status"), TEXT("Unknown"));
            ScriptObject->SetStringField(TEXT("compile_status_text"), TEXT("Unknown"));
            ScriptObject->SetBoolField(TEXT("script_and_source_synchronized"), false);
            return ScriptObject;
        }

        const ENiagaraScriptCompileStatus CompileStatus = Script->GetLastCompileStatus();
        ScriptObject->SetStringField(TEXT("name"), Script->GetName());
        ScriptObject->SetStringField(TEXT("path"), Script->GetPathName());
        ScriptObject->SetStringField(TEXT("usage"), GetNiagaraScriptUsageName(Script->GetUsage()));
        ScriptObject->SetStringField(TEXT("usage_id"), Script->GetUsageId().ToString());
        ScriptObject->SetBoolField(TEXT("is_compilable"), Script->IsCompilable());
        ScriptObject->SetStringField(TEXT("compile_status"), GetNiagaraScriptCompileStatusName(CompileStatus));
        ScriptObject->SetStringField(
            TEXT("compile_status_text"),
            StaticEnum<ENiagaraScriptCompileStatus>()
            ? StaticEnum<ENiagaraScriptCompileStatus>()->GetDisplayNameTextByValue(static_cast<int64>(CompileStatus)).ToString()
            : GetNiagaraScriptCompileStatusName(CompileStatus));
#if WITH_EDITORONLY_DATA
        ScriptObject->SetBoolField(TEXT("script_and_source_synchronized"), Script->AreScriptAndSourceSynchronized());
#else
        ScriptObject->SetBoolField(TEXT("script_and_source_synchronized"), false);
#endif
        return ScriptObject;
    }

    TSharedPtr<FJsonObject> BuildNiagaraEventHandlerSummary(const FNiagaraEventScriptProperties& EventHandler)
    {
        TSharedPtr<FJsonObject> EventHandlerObject = MakeShared<FJsonObject>();
        EventHandlerObject->SetStringField(TEXT("script_name"), EventHandler.Script ? EventHandler.Script->GetName() : TEXT(""));
        EventHandlerObject->SetStringField(TEXT("script_path"), EventHandler.Script ? EventHandler.Script->GetPathName() : TEXT(""));
        EventHandlerObject->SetStringField(TEXT("execution_mode"), GetNiagaraScriptExecutionModeName(EventHandler.ExecutionMode));
        EventHandlerObject->SetStringField(TEXT("source_emitter_id"), EventHandler.SourceEmitterID.ToString());
        EventHandlerObject->SetStringField(TEXT("source_event_name"), EventHandler.SourceEventName.ToString());
        EventHandlerObject->SetNumberField(TEXT("spawn_number"), EventHandler.SpawnNumber);
        EventHandlerObject->SetNumberField(TEXT("min_spawn_number"), EventHandler.MinSpawnNumber);
        EventHandlerObject->SetNumberField(TEXT("max_events_per_frame"), EventHandler.MaxEventsPerFrame);
        EventHandlerObject->SetBoolField(TEXT("random_spawn_number"), EventHandler.bRandomSpawnNumber);
        EventHandlerObject->SetBoolField(TEXT("update_attribute_initial_values"), EventHandler.UpdateAttributeInitialValues);
        return EventHandlerObject;
    }

    TSharedPtr<FJsonObject> BuildNiagaraSimulationStageSummary(const UNiagaraSimulationStageBase* SimulationStage, int32 Index)
    {
        TSharedPtr<FJsonObject> SimulationStageObject = MakeShared<FJsonObject>();
        SimulationStageObject->SetNumberField(TEXT("index"), Index);
        SimulationStageObject->SetStringField(TEXT("name"), SimulationStage ? SimulationStage->GetName() : TEXT(""));
        SimulationStageObject->SetStringField(TEXT("path"), SimulationStage ? SimulationStage->GetPathName() : TEXT(""));
        SimulationStageObject->SetStringField(TEXT("class_name"), SimulationStage ? SimulationStage->GetClass()->GetName() : TEXT(""));
        SimulationStageObject->SetStringField(TEXT("class_path"), SimulationStage ? SimulationStage->GetClass()->GetPathName() : TEXT(""));
        return SimulationStageObject;
    }

    bool ResolveNiagaraEmitterHandleIndex(
        const TSharedPtr<FJsonObject>& Params,
        UNiagaraSystem& NiagaraSystem,
        int32& OutEmitterIndex,
        FString& OutError)
    {
        OutEmitterIndex = INDEX_NONE;
        OutError.Reset();

        FString EmitterHandleIdString;
        Params->TryGetStringField(TEXT("emitter_handle_id"), EmitterHandleIdString);
        EmitterHandleIdString = EmitterHandleIdString.TrimStartAndEnd();

        FString EmitterName;
        Params->TryGetStringField(TEXT("emitter_name"), EmitterName);
        EmitterName = EmitterName.TrimStartAndEnd();

        if (EmitterHandleIdString.IsEmpty() && EmitterName.IsEmpty())
        {
            OutError = TEXT("Provide either 'emitter_handle_id' or 'emitter_name'");
            return false;
        }

        FGuid EmitterHandleId;
        const bool bHasEmitterHandleId = !EmitterHandleIdString.IsEmpty();
        if (bHasEmitterHandleId && !FGuid::Parse(EmitterHandleIdString, EmitterHandleId))
        {
            OutError = TEXT("'emitter_handle_id' must be a valid GUID string");
            return false;
        }

        TArray<FNiagaraEmitterHandle>& EmitterHandles = NiagaraSystem.GetEmitterHandles();
        for (int32 Index = 0; Index < EmitterHandles.Num(); ++Index)
        {
            const FNiagaraEmitterHandle& EmitterHandle = EmitterHandles[Index];
            if (!EmitterHandle.IsValid())
            {
                continue;
            }

            if (bHasEmitterHandleId && EmitterHandle.GetId() == EmitterHandleId)
            {
                OutEmitterIndex = Index;
                return true;
            }

            if (!EmitterName.IsEmpty() && EmitterHandle.GetName().ToString().Equals(EmitterName, ESearchCase::IgnoreCase))
            {
                OutEmitterIndex = Index;
                return true;
            }
        }

        OutError = bHasEmitterHandleId
            ? FString::Printf(TEXT("Failed to resolve Niagara emitter handle id: %s"), *EmitterHandleIdString)
            : FString::Printf(TEXT("Failed to resolve Niagara emitter name: %s"), *EmitterName);
        return false;
    }

    TSharedPtr<FJsonObject> BuildNiagaraEmitterSummary(UNiagaraSystem& NiagaraSystem, const FNiagaraEmitterHandle& EmitterHandle)
    {
        TSharedPtr<FJsonObject> EmitterObject = MakeShared<FJsonObject>();

        const bool bEmitterEnabled = EmitterHandle.GetIsEnabled();
        const FVersionedNiagaraEmitter VersionedEmitter = EmitterHandle.GetInstance();
        const FVersionedNiagaraEmitterData* EmitterData = EmitterHandle.GetEmitterData();
        const UNiagaraEmitterBase* EmitterBase = EmitterHandle.GetEmitterBase();

        EmitterObject->SetStringField(TEXT("name"), EmitterHandle.GetName().ToString());
        EmitterObject->SetStringField(TEXT("handle_id"), EmitterHandle.GetId().ToString());
        EmitterObject->SetStringField(TEXT("id_name"), EmitterHandle.GetIdName().ToString());
        EmitterObject->SetStringField(TEXT("unique_instance_name"), EmitterHandle.GetUniqueInstanceName());
        EmitterObject->SetBoolField(TEXT("is_enabled"), bEmitterEnabled);
        EmitterObject->SetStringField(TEXT("emitter_mode"), GetNiagaraEmitterModeName(EmitterHandle.GetEmitterMode()));
        EmitterObject->SetStringField(TEXT("version_guid"), VersionedEmitter.Version.ToString());

#if WITH_EDITORONLY_DATA
        EmitterObject->SetBoolField(TEXT("needs_recompile"), EmitterHandle.NeedsRecompile());
#else
        EmitterObject->SetBoolField(TEXT("needs_recompile"), false);
#endif

        EmitterObject->SetStringField(TEXT("source_emitter_name"), EmitterBase ? EmitterBase->GetName() : TEXT(""));
        EmitterObject->SetStringField(TEXT("source_emitter_path"), EmitterBase ? EmitterBase->GetPathName() : TEXT(""));
        EmitterObject->SetStringField(TEXT("source_emitter_class"), EmitterBase ? EmitterBase->GetClass()->GetName() : TEXT(""));

        if (EmitterData)
        {
            EmitterObject->SetBoolField(TEXT("local_space"), EmitterData->bLocalSpace);
            EmitterObject->SetBoolField(TEXT("determinism"), EmitterData->bDeterminism);
            EmitterObject->SetStringField(TEXT("sim_target"), GetNiagaraSimTargetName(EmitterData->SimTarget));
            EmitterObject->SetStringField(TEXT("calculate_bounds_mode"), GetNiagaraEmitterCalculateBoundModeName(EmitterData->CalculateBoundsMode));
            EmitterObject->SetStringField(TEXT("version_change_description"), EmitterData->VersionChangeDescription.ToString());

            TSharedPtr<FJsonObject> FixedBoundsObject = MakeShared<FJsonObject>();
            FixedBoundsObject->SetArrayField(TEXT("min"), {
                MakeShared<FJsonValueNumber>(EmitterData->FixedBounds.Min.X),
                MakeShared<FJsonValueNumber>(EmitterData->FixedBounds.Min.Y),
                MakeShared<FJsonValueNumber>(EmitterData->FixedBounds.Min.Z),
            });
            FixedBoundsObject->SetArrayField(TEXT("max"), {
                MakeShared<FJsonValueNumber>(EmitterData->FixedBounds.Max.X),
                MakeShared<FJsonValueNumber>(EmitterData->FixedBounds.Max.Y),
                MakeShared<FJsonValueNumber>(EmitterData->FixedBounds.Max.Z),
            });
            EmitterObject->SetObjectField(TEXT("fixed_bounds"), FixedBoundsObject);

            TArray<TSharedPtr<FJsonValue>> ScriptArray;
            TArray<UNiagaraScript*> Scripts;
            EmitterData->GetScripts(Scripts, /*bCompilableOnly=*/false, /*bEnabledOnly=*/false);
            for (UNiagaraScript* Script : Scripts)
            {
                ScriptArray.Add(MakeShared<FJsonValueObject>(BuildNiagaraScriptSummary(Script)));
            }
            EmitterObject->SetArrayField(TEXT("scripts"), ScriptArray);
            EmitterObject->SetNumberField(TEXT("script_count"), ScriptArray.Num());

            TArray<TSharedPtr<FJsonValue>> RendererArray;
            int32 EnabledRendererCount = 0;
            const TArray<UNiagaraRendererProperties*>& Renderers = EmitterData->GetRenderers();
            for (int32 RendererIndex = 0; RendererIndex < Renderers.Num(); ++RendererIndex)
            {
                const UNiagaraRendererProperties* Renderer = Renderers[RendererIndex];
                if (!Renderer)
                {
                    continue;
                }

                const bool bRendererEnabled = Renderer->GetIsEnabled();
                if (bRendererEnabled)
                {
                    ++EnabledRendererCount;
                }

                TSharedPtr<FJsonObject> RendererObject = MakeShared<FJsonObject>();
                RendererObject->SetNumberField(TEXT("index"), RendererIndex);
                RendererObject->SetStringField(TEXT("name"), Renderer->GetName());
                RendererObject->SetStringField(TEXT("class_name"), Renderer->GetClass()->GetName());
                RendererObject->SetStringField(TEXT("class_path"), Renderer->GetClass()->GetPathName());
                RendererObject->SetBoolField(TEXT("is_enabled"), bRendererEnabled);
                RendererArray.Add(MakeShared<FJsonValueObject>(RendererObject));
            }
            EmitterObject->SetArrayField(TEXT("renderers"), RendererArray);
            EmitterObject->SetNumberField(TEXT("renderer_count"), RendererArray.Num());
            EmitterObject->SetNumberField(TEXT("enabled_renderer_count"), EnabledRendererCount);

            TArray<FNiagaraVariable> RendererBindingParameters;
            EmitterData->RendererBindings.GetParameters(RendererBindingParameters);
            TArray<TSharedPtr<FJsonValue>> RendererBindingParameterArray = BuildNiagaraParameterArray(RendererBindingParameters);
            EmitterObject->SetArrayField(TEXT("renderer_binding_parameters"), RendererBindingParameterArray);
            EmitterObject->SetNumberField(TEXT("renderer_binding_parameter_count"), RendererBindingParameterArray.Num());

            TArray<TSharedPtr<FJsonValue>> EventHandlerArray;
            for (const FNiagaraEventScriptProperties& EventHandler : EmitterData->GetEventHandlers())
            {
                EventHandlerArray.Add(MakeShared<FJsonValueObject>(BuildNiagaraEventHandlerSummary(EventHandler)));
            }
            EmitterObject->SetArrayField(TEXT("event_handlers"), EventHandlerArray);
            EmitterObject->SetNumberField(TEXT("event_handler_count"), EventHandlerArray.Num());

            TArray<TSharedPtr<FJsonValue>> SimulationStageArray;
            const TArray<UNiagaraSimulationStageBase*>& SimulationStages = EmitterData->GetSimulationStages();
            for (int32 SimulationStageIndex = 0; SimulationStageIndex < SimulationStages.Num(); ++SimulationStageIndex)
            {
                SimulationStageArray.Add(MakeShared<FJsonValueObject>(BuildNiagaraSimulationStageSummary(SimulationStages[SimulationStageIndex], SimulationStageIndex)));
            }
            EmitterObject->SetArrayField(TEXT("simulation_stages"), SimulationStageArray);
            EmitterObject->SetNumberField(TEXT("simulation_stage_count"), SimulationStageArray.Num());
        }
        else
        {
            EmitterObject->SetBoolField(TEXT("local_space"), false);
            EmitterObject->SetBoolField(TEXT("determinism"), false);
            EmitterObject->SetStringField(TEXT("sim_target"), TEXT("Unknown"));
            EmitterObject->SetStringField(TEXT("calculate_bounds_mode"), TEXT("Unknown"));
            EmitterObject->SetStringField(TEXT("version_change_description"), TEXT(""));
            EmitterObject->SetArrayField(TEXT("scripts"), {});
            EmitterObject->SetNumberField(TEXT("script_count"), 0);
            EmitterObject->SetArrayField(TEXT("renderers"), {});
            EmitterObject->SetNumberField(TEXT("renderer_count"), 0);
            EmitterObject->SetNumberField(TEXT("enabled_renderer_count"), 0);
            EmitterObject->SetArrayField(TEXT("renderer_binding_parameters"), {});
            EmitterObject->SetNumberField(TEXT("renderer_binding_parameter_count"), 0);
            EmitterObject->SetArrayField(TEXT("event_handlers"), {});
            EmitterObject->SetNumberField(TEXT("event_handler_count"), 0);
            EmitterObject->SetArrayField(TEXT("simulation_stages"), {});
            EmitterObject->SetNumberField(TEXT("simulation_stage_count"), 0);
        }

        return EmitterObject;
    }

    FString GetAnimStateTypeName(const UAnimStateNode* StateNode)
    {
        if (!StateNode)
        {
            return TEXT("Unknown");
        }

        switch (StateNode->StateType)
        {
        case AST_SingleAnimation:
            return TEXT("SingleAnimation");
        case AST_BlendGraph:
            return TEXT("BlendGraph");
        default:
            return TEXT("Unknown");
        }
    }

    FString GetBlueprintStatusName(EBlueprintStatus Status)
    {
        switch (Status)
        {
        case BS_Unknown:
            return TEXT("Unknown");
        case BS_Dirty:
            return TEXT("Dirty");
        case BS_Error:
            return TEXT("Error");
        case BS_UpToDate:
            return TEXT("UpToDate");
        case BS_BeingCreated:
            return TEXT("BeingCreated");
        case BS_UpToDateWithWarnings:
            return TEXT("UpToDateWithWarnings");
        default:
            return TEXT("Unknown");
        }
    }

    bool IsBlueprintCompileHealthy(EBlueprintStatus Status)
    {
        return Status == BS_UpToDate || Status == BS_UpToDateWithWarnings;
    }

    bool TryGetMatchingAnimStateMachine(
        const TSharedPtr<FJsonObject>& AnimBlueprintContent,
        const FString& RequestedStateMachineName,
        TSharedPtr<FJsonObject>& OutStateMachineObject,
        TArray<FString>* OutAvailableStateMachineNames = nullptr)
    {
        OutStateMachineObject.Reset();

        const TArray<TSharedPtr<FJsonValue>>* StateMachines = nullptr;
        if (!AnimBlueprintContent.IsValid() || !AnimBlueprintContent->TryGetArrayField(TEXT("state_machines"), StateMachines) || !StateMachines)
        {
            return false;
        }

        for (const TSharedPtr<FJsonValue>& StateMachineValue : *StateMachines)
        {
            const TSharedPtr<FJsonObject>* StateMachineObject = nullptr;
            if (!StateMachineValue.IsValid() || !StateMachineValue->TryGetObject(StateMachineObject) || !StateMachineObject || !StateMachineObject->IsValid())
            {
                continue;
            }

            FString CandidateName;
            if (!(*StateMachineObject)->TryGetStringField(TEXT("name"), CandidateName))
            {
                continue;
            }

            if (OutAvailableStateMachineNames)
            {
                OutAvailableStateMachineNames->Add(CandidateName);
            }

            if (CandidateName.Equals(RequestedStateMachineName, ESearchCase::IgnoreCase))
            {
                OutStateMachineObject = *StateMachineObject;
                return true;
            }
        }

        return false;
    }

    bool TryGetMatchingAnimState(
        const TSharedPtr<FJsonObject>& StateMachineContent,
        const FString& RequestedStateName,
        TSharedPtr<FJsonObject>& OutStateObject,
        TArray<FString>* OutAvailableStateNames = nullptr)
    {
        OutStateObject.Reset();

        const TArray<TSharedPtr<FJsonValue>>* States = nullptr;
        if (!StateMachineContent.IsValid() || !StateMachineContent->TryGetArrayField(TEXT("states"), States) || !States)
        {
            return false;
        }

        for (const TSharedPtr<FJsonValue>& StateValue : *States)
        {
            const TSharedPtr<FJsonObject>* StateObject = nullptr;
            if (!StateValue.IsValid() || !StateValue->TryGetObject(StateObject) || !StateObject || !StateObject->IsValid())
            {
                continue;
            }

            FString CandidateName;
            if (!(*StateObject)->TryGetStringField(TEXT("name"), CandidateName))
            {
                continue;
            }

            if (OutAvailableStateNames)
            {
                OutAvailableStateNames->Add(CandidateName);
            }

            if (CandidateName.Equals(RequestedStateName, ESearchCase::IgnoreCase))
            {
                OutStateObject = *StateObject;
                return true;
            }
        }

        return false;
    }

    UAnimationGraph* ResolveTargetAnimationGraph(UAnimBlueprint* AnimBlueprint, const FString& RequestedGraphName, FString& OutErrorMessage)
    {
        OutErrorMessage.Reset();
        if (!AnimBlueprint)
        {
            OutErrorMessage = TEXT("AnimBlueprint is null");
            return nullptr;
        }

        const FString NormalizedRequestedGraphName = RequestedGraphName.TrimStartAndEnd();
        UAnimationGraph* FirstAnimationGraph = nullptr;

        for (UEdGraph* Graph : AnimBlueprint->FunctionGraphs)
        {
            UAnimationGraph* AnimationGraph = Cast<UAnimationGraph>(Graph);
            if (!AnimationGraph)
            {
                continue;
            }

            if (!FirstAnimationGraph)
            {
                FirstAnimationGraph = AnimationGraph;
            }

            if (!NormalizedRequestedGraphName.IsEmpty() && AnimationGraph->GetName().Equals(NormalizedRequestedGraphName, ESearchCase::IgnoreCase))
            {
                return AnimationGraph;
            }
        }

        if (!NormalizedRequestedGraphName.IsEmpty())
        {
            OutErrorMessage = FString::Printf(TEXT("Animation graph not found in AnimBlueprint: %s"), *NormalizedRequestedGraphName);
            return nullptr;
        }

        if (!FirstAnimationGraph)
        {
            OutErrorMessage = TEXT("AnimBlueprint does not contain a top-level animation graph");
            return nullptr;
        }

        return FirstAnimationGraph;
    }

    bool DoesAnimBlueprintGraphNameCollide(UAnimBlueprint* AnimBlueprint, const FString& RequestedGraphName)
    {
        if (!AnimBlueprint)
        {
            return false;
        }

        const FString NormalizedRequestedGraphName = RequestedGraphName.TrimStartAndEnd();
        if (NormalizedRequestedGraphName.IsEmpty())
        {
            return false;
        }

        TArray<UEdGraph*> BlueprintGraphs;
        BlueprintGraphs.Append(AnimBlueprint->FunctionGraphs);
        BlueprintGraphs.Append(AnimBlueprint->UbergraphPages);

        for (UEdGraph* Graph : BlueprintGraphs)
        {
            if (Graph && Graph->GetName().Equals(NormalizedRequestedGraphName, ESearchCase::IgnoreCase))
            {
                return true;
            }
        }

        return false;
    }

    UAnimationStateMachineGraph* ResolveTargetStateMachineGraph(
        UAnimBlueprint* AnimBlueprint,
        const FString& RequestedStateMachineName,
        FString& OutErrorMessage,
        UAnimGraphNode_StateMachineBase** OutStateMachineNode = nullptr,
        UEdGraph** OutOwnerGraph = nullptr)
    {
        OutErrorMessage.Reset();
        if (OutStateMachineNode)
        {
            *OutStateMachineNode = nullptr;
        }
        if (OutOwnerGraph)
        {
            *OutOwnerGraph = nullptr;
        }

        if (!AnimBlueprint)
        {
            OutErrorMessage = TEXT("AnimBlueprint is null");
            return nullptr;
        }

        const FString NormalizedRequestedStateMachineName = RequestedStateMachineName.TrimStartAndEnd();
        if (NormalizedRequestedStateMachineName.IsEmpty())
        {
            OutErrorMessage = TEXT("Missing or empty 'state_machine_name' parameter");
            return nullptr;
        }

        TArray<FString> AvailableStateMachineNames;
        TArray<UEdGraph*> TopLevelGraphs;
        TopLevelGraphs.Append(AnimBlueprint->FunctionGraphs);
        TopLevelGraphs.Append(AnimBlueprint->UbergraphPages);

        for (UEdGraph* TopLevelGraph : TopLevelGraphs)
        {
            if (!TopLevelGraph)
            {
                continue;
            }

            for (UEdGraphNode* Node : TopLevelGraph->Nodes)
            {
                UAnimGraphNode_StateMachineBase* StateMachineNode = Cast<UAnimGraphNode_StateMachineBase>(Node);
                if (!StateMachineNode)
                {
                    continue;
                }

                FString CandidateName = StateMachineNode->GetStateMachineName();
                if (CandidateName.IsEmpty())
                {
                    CandidateName = StateMachineNode->GetNodeTitle(ENodeTitleType::ListView).ToString();
                }

                if (!CandidateName.IsEmpty())
                {
                    AvailableStateMachineNames.AddUnique(CandidateName);
                }

                if (!CandidateName.Equals(NormalizedRequestedStateMachineName, ESearchCase::IgnoreCase))
                {
                    continue;
                }

                if (!StateMachineNode->EditorStateMachineGraph)
                {
                    OutErrorMessage = FString::Printf(TEXT("State machine '%s' is missing its editor graph"), *CandidateName);
                    return nullptr;
                }

                if (OutStateMachineNode)
                {
                    *OutStateMachineNode = StateMachineNode;
                }
                if (OutOwnerGraph)
                {
                    *OutOwnerGraph = TopLevelGraph;
                }
                return StateMachineNode->EditorStateMachineGraph;
            }
        }

        const FString AvailableNamesMessage = AvailableStateMachineNames.Num() > 0
            ? FString::Join(AvailableStateMachineNames, TEXT(", "))
            : TEXT("none");
        OutErrorMessage = FString::Printf(TEXT("State machine not found in AnimBlueprint: %s (available: %s)"),
            *NormalizedRequestedStateMachineName,
            *AvailableNamesMessage);
        return nullptr;
    }

    UAnimStateNode* ResolveTargetAnimStateNode(
        UAnimationStateMachineGraph* StateMachineGraph,
        const FString& RequestedStateName,
        FString& OutErrorMessage,
        TArray<FString>* OutAvailableStateNames = nullptr)
    {
        OutErrorMessage.Reset();
        if (OutAvailableStateNames)
        {
            OutAvailableStateNames->Reset();
        }

        if (!StateMachineGraph)
        {
            OutErrorMessage = TEXT("State machine graph is null");
            return nullptr;
        }

        const FString NormalizedRequestedStateName = RequestedStateName.TrimStartAndEnd();
        if (NormalizedRequestedStateName.IsEmpty())
        {
            OutErrorMessage = TEXT("Missing or empty 'state_name' parameter");
            return nullptr;
        }

        for (UEdGraphNode* Node : StateMachineGraph->Nodes)
        {
            UAnimStateNode* StateNode = Cast<UAnimStateNode>(Node);
            if (!StateNode)
            {
                continue;
            }

            FString CandidateName = StateNode->GetStateName();
            if (CandidateName.IsEmpty())
            {
                CandidateName = StateNode->GetNodeTitle(ENodeTitleType::ListView).ToString();
            }

            if (OutAvailableStateNames && !CandidateName.IsEmpty())
            {
                OutAvailableStateNames->Add(CandidateName);
            }

            if (CandidateName.Equals(NormalizedRequestedStateName, ESearchCase::IgnoreCase))
            {
                return StateNode;
            }
        }

        const FString AvailableNamesMessage = (OutAvailableStateNames && OutAvailableStateNames->Num() > 0)
            ? FString::Join(*OutAvailableStateNames, TEXT(", "))
            : TEXT("none");
        OutErrorMessage = FString::Printf(TEXT("State not found in state machine: %s (available: %s)"),
            *NormalizedRequestedStateName,
            *AvailableNamesMessage);
        return nullptr;
    }

    int32 CountAnimStates(const UAnimationStateMachineGraph* StateMachineGraph)
    {
        if (!StateMachineGraph)
        {
            return 0;
        }

        int32 StateCount = 0;
        for (UEdGraphNode* Node : StateMachineGraph->Nodes)
        {
            if (Cast<UAnimStateNode>(Node))
            {
                ++StateCount;
            }
        }

        return StateCount;
    }

    void CollectTransitionsForState(
        UAnimationStateMachineGraph* StateMachineGraph,
        const UAnimStateNodeBase* TargetState,
        TArray<UAnimStateTransitionNode*>& OutTransitions)
    {
        OutTransitions.Reset();
        if (!StateMachineGraph || !TargetState)
        {
            return;
        }

        for (UEdGraphNode* Node : StateMachineGraph->Nodes)
        {
            UAnimStateTransitionNode* TransitionNode = Cast<UAnimStateTransitionNode>(Node);
            if (!TransitionNode)
            {
                continue;
            }

            if (TransitionNode->GetPreviousState() == TargetState || TransitionNode->GetNextState() == TargetState)
            {
                OutTransitions.Add(TransitionNode);
            }
        }
    }

    void CollectTransitionsBetweenStates(
        UAnimationStateMachineGraph* StateMachineGraph,
        const UAnimStateNodeBase* SourceState,
        const UAnimStateNodeBase* TargetState,
        TArray<UAnimStateTransitionNode*>& OutTransitions)
    {
        OutTransitions.Reset();
        if (!StateMachineGraph || !SourceState || !TargetState)
        {
            return;
        }

        for (UEdGraphNode* Node : StateMachineGraph->Nodes)
        {
            UAnimStateTransitionNode* TransitionNode = Cast<UAnimStateTransitionNode>(Node);
            if (!TransitionNode)
            {
                continue;
            }

            if (TransitionNode->GetPreviousState() == SourceState && TransitionNode->GetNextState() == TargetState)
            {
                OutTransitions.Add(TransitionNode);
            }
        }
    }

    bool TryGetMatchingAnimTransition(
        const TSharedPtr<FJsonObject>& StateMachineContent,
        const FString& RequestedSourceStateName,
        const FString& RequestedTargetStateName,
        TSharedPtr<FJsonObject>& OutTransitionObject,
        int32* OutMatchCount = nullptr)
    {
        OutTransitionObject.Reset();
        if (OutMatchCount)
        {
            *OutMatchCount = 0;
        }

        const TArray<TSharedPtr<FJsonValue>>* Transitions = nullptr;
        if (!StateMachineContent.IsValid() || !StateMachineContent->TryGetArrayField(TEXT("transitions"), Transitions) || !Transitions)
        {
            return false;
        }

        for (const TSharedPtr<FJsonValue>& TransitionValue : *Transitions)
        {
            const TSharedPtr<FJsonObject>* TransitionObject = nullptr;
            if (!TransitionValue.IsValid() || !TransitionValue->TryGetObject(TransitionObject) || !TransitionObject || !TransitionObject->IsValid())
            {
                continue;
            }

            FString CandidateSourceStateName;
            FString CandidateTargetStateName;
            if (!(*TransitionObject)->TryGetStringField(TEXT("source_state_name"), CandidateSourceStateName) ||
                !(*TransitionObject)->TryGetStringField(TEXT("target_state_name"), CandidateTargetStateName))
            {
                continue;
            }

            if (CandidateSourceStateName.Equals(RequestedSourceStateName, ESearchCase::IgnoreCase) &&
                CandidateTargetStateName.Equals(RequestedTargetStateName, ESearchCase::IgnoreCase))
            {
                if (OutMatchCount)
                {
                    ++(*OutMatchCount);
                }
                if (!OutTransitionObject.IsValid())
                {
                    OutTransitionObject = *TransitionObject;
                }
            }
        }

        return OutTransitionObject.IsValid();
    }

    FProperty* ResolveAnimBlueprintProperty(UAnimBlueprint* AnimBlueprint, const FString& VariableName)
    {
        if (!AnimBlueprint || VariableName.TrimStartAndEnd().IsEmpty())
        {
            return nullptr;
        }

        const FName VarName(*VariableName.TrimStartAndEnd());

        if (AnimBlueprint->GeneratedClass)
        {
            if (FProperty* Property = FindFProperty<FProperty>(AnimBlueprint->GeneratedClass, VarName))
            {
                return Property;
            }
        }

        if (AnimBlueprint->SkeletonGeneratedClass)
        {
            if (FProperty* Property = FindFProperty<FProperty>(AnimBlueprint->SkeletonGeneratedClass, VarName))
            {
                return Property;
            }
        }

        if (AnimBlueprint->ParentClass)
        {
            if (FProperty* Property = FindFProperty<FProperty>(AnimBlueprint->ParentClass, VarName))
            {
                return Property;
            }
        }

        return nullptr;
    }

    FString GetAnimTransitionRulePropertyKind(const FProperty* Property)
    {
        if (!Property)
        {
            return TEXT("unknown");
        }

        if (CastField<const FBoolProperty>(Property))
        {
            return TEXT("bool");
        }

        if (CastField<const FIntProperty>(Property))
        {
            return TEXT("int");
        }

        if (const FEnumProperty* EnumProperty = CastField<const FEnumProperty>(Property))
        {
            return EnumProperty->GetEnum() ? TEXT("enum") : TEXT("int");
        }

        if (const FByteProperty* ByteProperty = CastField<const FByteProperty>(Property))
        {
            return ByteProperty->Enum ? TEXT("enum") : TEXT("byte");
        }

        return TEXT("other");
    }

    UEnum* ResolveAnimTransitionRuleEnum(const FProperty* Property)
    {
        if (const FEnumProperty* EnumProperty = CastField<const FEnumProperty>(Property))
        {
            return EnumProperty->GetEnum();
        }
        if (const FByteProperty* ByteProperty = CastField<const FByteProperty>(Property))
        {
            return ByteProperty->Enum;
        }
        return nullptr;
    }

    UEdGraphPin* FindFirstNonExecOutputPin(UEdGraphNode* Node)
    {
        if (!Node)
        {
            return nullptr;
        }

        for (UEdGraphPin* Pin : Node->Pins)
        {
            if (Pin && Pin->Direction == EGPD_Output && Pin->PinType.PinCategory != UEdGraphSchema_K2::PC_Exec)
            {
                return Pin;
            }
        }

        return nullptr;
    }

    UEdGraphPin* FindFirstBoolOutputPin(UEdGraphNode* Node)
    {
        if (!Node)
        {
            return nullptr;
        }

        for (UEdGraphPin* Pin : Node->Pins)
        {
            if (Pin && Pin->Direction == EGPD_Output && Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Boolean)
            {
                return Pin;
            }
        }

        return nullptr;
    }

    UEdGraphPin* FindTransitionRuleResultPin(UAnimationTransitionGraph* TransitionRuleGraph)
    {
        if (!TransitionRuleGraph)
        {
            return nullptr;
        }

        UAnimGraphNode_TransitionResult* ResultNode = TransitionRuleGraph->GetResultNode();
        if (!ResultNode)
        {
            ResultNode = TransitionRuleGraph->MyResultNode;
        }

        if (!ResultNode)
        {
            return nullptr;
        }

        return ResultNode->FindPin(TEXT("bCanEnterTransition"));
    }

    void ResetTransitionRuleGraph(UAnimBlueprint* AnimBlueprint, UAnimationTransitionGraph* TransitionRuleGraph)
    {
        if (!AnimBlueprint || !TransitionRuleGraph)
        {
            return;
        }

        UAnimGraphNode_TransitionResult* ResultNode = TransitionRuleGraph->GetResultNode();
        if (!ResultNode)
        {
            ResultNode = TransitionRuleGraph->MyResultNode;
        }

        UEdGraphPin* ResultPin = ResultNode ? ResultNode->FindPin(TEXT("bCanEnterTransition")) : nullptr;
        if (ResultPin)
        {
            ResultPin->BreakAllPinLinks();
            ResultPin->DefaultValue = TEXT("false");
            ResultPin->AutogeneratedDefaultValue = TEXT("false");
        }
        if (ResultNode)
        {
            ResultNode->Node.bCanEnterTransition = false;
            ResultNode->Modify();
        }

        TArray<UEdGraphNode*> NodesToRemove;
        for (UEdGraphNode* Node : TransitionRuleGraph->Nodes)
        {
            if (!Node || Node == ResultNode)
            {
                continue;
            }

            NodesToRemove.Add(Node);
        }

        for (UEdGraphNode* Node : NodesToRemove)
        {
            FBlueprintEditorUtils::RemoveNode(AnimBlueprint, Node, /*bDontRecompile=*/true);
        }

        TransitionRuleGraph->NotifyGraphChanged();
    }

    bool ConnectPinsOnGraph(UEdGraph* Graph, UEdGraphPin* SourcePin, UEdGraphPin* TargetPin)
    {
        if (!Graph || !SourcePin || !TargetPin)
        {
            return false;
        }

        const UEdGraphSchema* Schema = Graph->GetSchema();
        if (!Schema)
        {
            return false;
        }

        return Schema->TryCreateConnection(SourcePin, TargetPin);
    }

    UEdGraphPin* FindFirstVisiblePin(UEdGraphNode* Node, EEdGraphPinDirection Direction)
    {
        if (!Node)
        {
            return nullptr;
        }

        UEdGraphPin* FirstMatchingPin = nullptr;
        for (UEdGraphPin* Pin : Node->Pins)
        {
            if (!Pin || Pin->Direction != Direction)
            {
                continue;
            }

            if (!FirstMatchingPin)
            {
                FirstMatchingPin = Pin;
            }

            if (!Pin->bHidden)
            {
                return Pin;
            }
        }

        return FirstMatchingPin;
    }

    FString NormalizeEnumToken(const FString& Value)
    {
        FString Normalized = Value;
        Normalized.TrimStartAndEndInline();
        Normalized.ToLowerInline();
        Normalized.ReplaceInline(TEXT("_"), TEXT(""));
        Normalized.ReplaceInline(TEXT("-"), TEXT(""));
        Normalized.ReplaceInline(TEXT(" "), TEXT(""));
        return Normalized;
    }

    template <typename TEnum>
    bool TryParseEnumValue(const FString& RawValue, const UEnum* EnumDefinition, TEnum& OutValue)
    {
        if (!EnumDefinition)
        {
            return false;
        }

        const int64 ExactValue = EnumDefinition->GetValueByNameString(RawValue);
        if (ExactValue != INDEX_NONE)
        {
            OutValue = static_cast<TEnum>(ExactValue);
            return true;
        }

        const FString NormalizedInput = NormalizeEnumToken(RawValue);
        for (int32 EnumIndex = 0; EnumIndex < EnumDefinition->NumEnums(); ++EnumIndex)
        {
            const int64 CandidateValue = EnumDefinition->GetValueByIndex(EnumIndex);
            if (CandidateValue == INDEX_NONE)
            {
                continue;
            }

            const FString CandidateName = EnumDefinition->GetNameStringByIndex(EnumIndex);
            if (NormalizeEnumToken(CandidateName) == NormalizedInput)
            {
                OutValue = static_cast<TEnum>(CandidateValue);
                return true;
            }

            FString Prefix;
            FString Suffix;
            if (CandidateName.Split(TEXT("_"), &Prefix, &Suffix) && NormalizeEnumToken(Suffix) == NormalizedInput)
            {
                OutValue = static_cast<TEnum>(CandidateValue);
                return true;
            }
        }

        return false;
    }

    FString EnumValueToString(const UEnum* EnumDefinition, int64 Value)
    {
        if (!EnumDefinition)
        {
            return FString::FromInt(static_cast<int32>(Value));
        }

        return EnumDefinition->GetNameStringByValue(Value);
    }

    bool SetStructFloatPropertyValue(UScriptStruct* StructDefinition, void* StructData, const TCHAR* PropertyName, float Value)
    {
        if (!StructDefinition || !StructData)
        {
            return false;
        }

        FFloatProperty* FloatProperty = FindFProperty<FFloatProperty>(StructDefinition, FName(PropertyName));
        if (!FloatProperty)
        {
            return false;
        }

        void* PropertyAddress = FloatProperty->ContainerPtrToValuePtr<void>(StructData);
        FloatProperty->SetPropertyValue(PropertyAddress, Value);
        return true;
    }

    UAnimationStateGraph* ResolveTargetStateBoundGraph(UAnimStateNode* StateNode, FString& OutErrorMessage);
    UAnimGraphNode_StateResult* ResolveAnimStateResultNode(UAnimationStateGraph* StateGraph);

    bool TryDescribeAnimStateAssetPlayerNode(
        UEdGraphNode* Node,
        FString& OutBindingType,
        UAnimationAsset*& OutAnimationAsset,
        FAnimNode_AssetPlayerBase*& OutAssetPlayerNode)
    {
        OutBindingType.Reset();
        OutAnimationAsset = nullptr;
        OutAssetPlayerNode = nullptr;

        if (UAnimGraphNode_SequencePlayer* SequencePlayerNode = Cast<UAnimGraphNode_SequencePlayer>(Node))
        {
            OutBindingType = TEXT("sequence_player");
            OutAnimationAsset = SequencePlayerNode->GetAnimationAsset();
            OutAssetPlayerNode = &SequencePlayerNode->Node;
            return true;
        }

        if (UAnimGraphNode_RotationOffsetBlendSpace* AimOffsetNode = Cast<UAnimGraphNode_RotationOffsetBlendSpace>(Node))
        {
            OutBindingType = TEXT("aim_offset_player");
            OutAnimationAsset = AimOffsetNode->GetAnimationAsset();
            OutAssetPlayerNode = &AimOffsetNode->Node;
            return true;
        }

        if (UAnimGraphNode_BlendSpacePlayer* BlendSpaceNode = Cast<UAnimGraphNode_BlendSpacePlayer>(Node))
        {
            OutBindingType = TEXT("blend_space_player");
            OutAnimationAsset = BlendSpaceNode->GetAnimationAsset();
            OutAssetPlayerNode = &BlendSpaceNode->Node;
            return true;
        }

        return false;
    }

    struct FResolvedAnimStateAssetPlayerNode
    {
        UAnimationStateGraph* StateGraph = nullptr;
        UAnimGraphNode_StateResult* ResultNode = nullptr;
        UEdGraphPin* ResultPin = nullptr;
        UEdGraphNode* AssetPlayerGraphNode = nullptr;
        FAnimNode_AssetPlayerBase* AssetPlayerNode = nullptr;
        UEdGraphPin* AssetPlayerOutputPin = nullptr;
        FString BindingType;
    };

    bool ResolveSupportedAnimStateAssetPlayerNode(
        UAnimStateNode* StateNode,
        FResolvedAnimStateAssetPlayerNode& OutResolvedNode,
        FString& OutErrorMessage)
    {
        OutResolvedNode = FResolvedAnimStateAssetPlayerNode();
        OutErrorMessage.Reset();

        UAnimationStateGraph* StateGraph = ResolveTargetStateBoundGraph(StateNode, OutErrorMessage);
        if (!StateGraph)
        {
            return false;
        }

        UAnimGraphNode_StateResult* ResultNode = ResolveAnimStateResultNode(StateGraph);
        if (!ResultNode)
        {
            OutErrorMessage = TEXT("Animation state graph is missing its result node");
            return false;
        }

        UEdGraphPin* ResultPin = FindFirstVisiblePin(ResultNode, EGPD_Input);
        if (!ResultPin)
        {
            OutErrorMessage = TEXT("Animation state graph result node is missing its input pose pin");
            return false;
        }

        int32 NonResultNodeCount = 0;
        int32 AssetPlayerCount = 0;

        for (UEdGraphNode* GraphNode : StateGraph->Nodes)
        {
            if (!GraphNode || GraphNode == ResultNode)
            {
                continue;
            }

            ++NonResultNodeCount;

            FString CandidateBindingType;
            UAnimationAsset* CandidateAnimationAsset = nullptr;
            FAnimNode_AssetPlayerBase* CandidateAssetPlayerNode = nullptr;
            if (!TryDescribeAnimStateAssetPlayerNode(
                GraphNode,
                CandidateBindingType,
                CandidateAnimationAsset,
                CandidateAssetPlayerNode))
            {
                continue;
            }

            ++AssetPlayerCount;
            if (!OutResolvedNode.AssetPlayerGraphNode)
            {
                OutResolvedNode.StateGraph = StateGraph;
                OutResolvedNode.ResultNode = ResultNode;
                OutResolvedNode.ResultPin = ResultPin;
                OutResolvedNode.AssetPlayerGraphNode = GraphNode;
                OutResolvedNode.AssetPlayerNode = CandidateAssetPlayerNode;
                OutResolvedNode.AssetPlayerOutputPin = FindFirstVisiblePin(GraphNode, EGPD_Output);
                OutResolvedNode.BindingType = CandidateBindingType;
            }
        }

        if (!OutResolvedNode.AssetPlayerGraphNode)
        {
            OutErrorMessage = NonResultNodeCount == 0
                ? FString::Printf(TEXT("Animation state '%s' does not contain an asset-player node"), *StateNode->GetStateName())
                : FString::Printf(TEXT("Animation state '%s' does not use a supported asset-player graph pattern"), *StateNode->GetStateName());
            return false;
        }

        if (AssetPlayerCount != 1 || NonResultNodeCount != 1)
        {
            OutErrorMessage = FString::Printf(
                TEXT("Animation state '%s' does not use the supported single asset-player graph pattern"),
                *StateNode->GetStateName());
            return false;
        }

        if (!OutResolvedNode.AssetPlayerOutputPin)
        {
            OutErrorMessage = TEXT("Animation state asset-player node is missing its output pose pin");
            return false;
        }

        bool bConnectedToResult = false;
        for (UEdGraphPin* LinkedPin : OutResolvedNode.ResultPin->LinkedTo)
        {
            if (LinkedPin == OutResolvedNode.AssetPlayerOutputPin)
            {
                bConnectedToResult = true;
                break;
            }
        }

        if (!bConnectedToResult)
        {
            OutErrorMessage = FString::Printf(
                TEXT("Animation state '%s' asset-player node is not connected to the state result"),
                *StateNode->GetStateName());
            return false;
        }

        return true;
    }

    void PopulateAnimStateAssetPlayerParameterSummary(
        UEdGraphNode* AssetPlayerGraphNode,
        FAnimNode_AssetPlayerBase* AssetPlayerNode,
        TSharedPtr<FJsonObject>& AssetPlayerSummary)
    {
        AssetPlayerSummary->SetBoolField(TEXT("loop"), false);
        AssetPlayerSummary->SetNumberField(TEXT("play_rate"), 0.0);
        AssetPlayerSummary->SetNumberField(TEXT("start_position"), 0.0);
        AssetPlayerSummary->SetNumberField(TEXT("blend_space_x"), 0.0);
        AssetPlayerSummary->SetNumberField(TEXT("blend_space_y"), 0.0);
        AssetPlayerSummary->SetStringField(TEXT("sync_group_name"), TEXT(""));
        AssetPlayerSummary->SetStringField(TEXT("sync_group_role"), TEXT(""));
        AssetPlayerSummary->SetStringField(TEXT("sync_group_method"), TEXT(""));
        AssetPlayerSummary->SetBoolField(
            TEXT("sync_group_override_position_when_joining_sync_group_as_leader"),
            false);

        if (!AssetPlayerGraphNode || !AssetPlayerNode)
        {
            return;
        }

        AssetPlayerSummary->SetBoolField(TEXT("loop"), AssetPlayerNode->IsLooping());
        AssetPlayerSummary->SetStringField(TEXT("sync_group_name"), AssetPlayerNode->GetGroupName().ToString());
        AssetPlayerSummary->SetStringField(
            TEXT("sync_group_role"),
            EnumValueToString(StaticEnum<EAnimGroupRole::Type>(), static_cast<int64>(AssetPlayerNode->GetGroupRole())));
        AssetPlayerSummary->SetStringField(
            TEXT("sync_group_method"),
            EnumValueToString(StaticEnum<EAnimSyncMethod>(), static_cast<int64>(AssetPlayerNode->GetGroupMethod())));
        AssetPlayerSummary->SetBoolField(
            TEXT("sync_group_override_position_when_joining_sync_group_as_leader"),
            AssetPlayerNode->GetOverridePositionWhenJoiningSyncGroupAsLeader());

        if (UAnimGraphNode_SequencePlayer* SequencePlayerNode = Cast<UAnimGraphNode_SequencePlayer>(AssetPlayerGraphNode))
        {
            AssetPlayerSummary->SetNumberField(TEXT("play_rate"), SequencePlayerNode->Node.GetPlayRate());
            AssetPlayerSummary->SetNumberField(TEXT("start_position"), SequencePlayerNode->Node.GetStartPosition());
            return;
        }

        if (UAnimGraphNode_BlendSpacePlayer* BlendSpaceNode = Cast<UAnimGraphNode_BlendSpacePlayer>(AssetPlayerGraphNode))
        {
            const FVector Position = BlendSpaceNode->Node.GetPosition();
            AssetPlayerSummary->SetNumberField(TEXT("play_rate"), BlendSpaceNode->Node.GetPlayRate());
            AssetPlayerSummary->SetNumberField(TEXT("start_position"), BlendSpaceNode->Node.GetStartPosition());
            AssetPlayerSummary->SetNumberField(TEXT("blend_space_x"), Position.X);
            AssetPlayerSummary->SetNumberField(TEXT("blend_space_y"), Position.Y);
            return;
        }

        if (UAnimGraphNode_RotationOffsetBlendSpace* AimOffsetNode = Cast<UAnimGraphNode_RotationOffsetBlendSpace>(AssetPlayerGraphNode))
        {
            const FVector Position = AimOffsetNode->Node.GetPosition();
            AssetPlayerSummary->SetNumberField(TEXT("play_rate"), AimOffsetNode->Node.GetPlayRate());
            AssetPlayerSummary->SetNumberField(TEXT("start_position"), AimOffsetNode->Node.GetStartPosition());
            AssetPlayerSummary->SetNumberField(TEXT("blend_space_x"), Position.X);
            AssetPlayerSummary->SetNumberField(TEXT("blend_space_y"), Position.Y);
        }
    }

    void AppendAnimStateAssetPlayerResponseFields(
        const TSharedPtr<FJsonObject>& Result,
        const TSharedPtr<FJsonObject>& UpdatedState)
    {
        if (!Result.IsValid() || !UpdatedState.IsValid())
        {
            return;
        }

        Result->SetStringField(TEXT("binding_type"), UpdatedState->GetStringField(TEXT("asset_player_binding_type")));
        Result->SetStringField(TEXT("animation_asset_path"), UpdatedState->GetStringField(TEXT("animation_asset_path")));
        Result->SetStringField(TEXT("animation_asset_name"), UpdatedState->GetStringField(TEXT("animation_asset_name")));
        Result->SetStringField(TEXT("animation_asset_class"), UpdatedState->GetStringField(TEXT("animation_asset_class")));
        Result->SetStringField(TEXT("asset_player_node_class"), UpdatedState->GetStringField(TEXT("asset_player_node_class")));
        Result->SetBoolField(TEXT("asset_player_is_supported_pattern"), UpdatedState->GetBoolField(TEXT("asset_player_is_supported_pattern")));
        Result->SetBoolField(TEXT("asset_player_is_connected"), UpdatedState->GetBoolField(TEXT("asset_player_is_connected")));
        Result->SetBoolField(TEXT("loop"), UpdatedState->GetBoolField(TEXT("asset_player_loop")));
        Result->SetNumberField(TEXT("play_rate"), UpdatedState->GetNumberField(TEXT("asset_player_play_rate")));
        Result->SetNumberField(TEXT("start_position"), UpdatedState->GetNumberField(TEXT("asset_player_start_position")));
        Result->SetNumberField(TEXT("blend_space_x"), UpdatedState->GetNumberField(TEXT("asset_player_blend_space_x")));
        Result->SetNumberField(TEXT("blend_space_y"), UpdatedState->GetNumberField(TEXT("asset_player_blend_space_y")));
        Result->SetStringField(TEXT("sync_group_name"), UpdatedState->GetStringField(TEXT("asset_player_sync_group_name")));
        Result->SetStringField(TEXT("sync_group_role"), UpdatedState->GetStringField(TEXT("asset_player_sync_group_role")));
        Result->SetStringField(TEXT("sync_group_method"), UpdatedState->GetStringField(TEXT("asset_player_sync_group_method")));
        Result->SetBoolField(
            TEXT("sync_group_override_position_when_joining_sync_group_as_leader"),
            UpdatedState->GetBoolField(TEXT("asset_player_sync_group_override_position_when_joining_sync_group_as_leader")));

        const TSharedPtr<FJsonObject>* AssetPlayerSummary = nullptr;
        if (UpdatedState->TryGetObjectField(TEXT("asset_player_summary"), AssetPlayerSummary) && AssetPlayerSummary && AssetPlayerSummary->IsValid())
        {
            Result->SetObjectField(TEXT("asset_player_summary"), (*AssetPlayerSummary).ToSharedRef());
        }
    }

    UAnimationStateGraph* ResolveTargetStateBoundGraph(UAnimStateNode* StateNode, FString& OutErrorMessage)
    {
        OutErrorMessage.Reset();

        if (!StateNode)
        {
            OutErrorMessage = TEXT("Animation state node is null");
            return nullptr;
        }

        if (!StateNode->BoundGraph)
        {
            OutErrorMessage = FString::Printf(TEXT("Animation state '%s' is missing its bound graph"), *StateNode->GetStateName());
            return nullptr;
        }

        UAnimationStateGraph* StateGraph = Cast<UAnimationStateGraph>(StateNode->BoundGraph);
        if (!StateGraph)
        {
            OutErrorMessage = FString::Printf(
                TEXT("Animation state '%s' bound graph is not a UAnimationStateGraph"),
                *StateNode->GetStateName());
            return nullptr;
        }

        return StateGraph;
    }

    UAnimGraphNode_StateResult* ResolveAnimStateResultNode(UAnimationStateGraph* StateGraph)
    {
        if (!StateGraph)
        {
            return nullptr;
        }

        UAnimGraphNode_StateResult* ResultNode = StateGraph->GetResultNode();
        if (!ResultNode)
        {
            ResultNode = StateGraph->MyResultNode;
        }

        return ResultNode;
    }

    void ResetAnimStateBoundGraph(UAnimBlueprint* AnimBlueprint, UAnimationStateGraph* StateGraph)
    {
        if (!AnimBlueprint || !StateGraph)
        {
            return;
        }

        UAnimGraphNode_StateResult* ResultNode = ResolveAnimStateResultNode(StateGraph);
        UEdGraphPin* ResultPin = FindFirstVisiblePin(ResultNode, EGPD_Input);
        if (ResultPin)
        {
            ResultPin->BreakAllPinLinks();
        }

        TArray<UEdGraphNode*> NodesToRemove;
        for (UEdGraphNode* Node : StateGraph->Nodes)
        {
            if (!Node || Node == ResultNode)
            {
                continue;
            }

            NodesToRemove.Add(Node);
        }

        for (UEdGraphNode* Node : NodesToRemove)
        {
            FBlueprintEditorUtils::RemoveNode(AnimBlueprint, Node, /*bDontRecompile=*/true);
        }

        StateGraph->NotifyGraphChanged();
    }

    template <typename TNodeType>
    TNodeType* CreateAnimStateGraphNode(UAnimationStateGraph* StateGraph, const FVector2D& Position)
    {
        if (!StateGraph)
        {
            return nullptr;
        }

        FGraphNodeCreator<TNodeType> NodeCreator(*StateGraph);
        TNodeType* Node = NodeCreator.CreateNode(/*bSelectNewNode=*/false);
        if (!Node)
        {
            return nullptr;
        }

        Node->NodePosX = static_cast<int32>(Position.X);
        Node->NodePosY = static_cast<int32>(Position.Y);
        NodeCreator.Finalize();
        return Node;
    }

    UEdGraphNode* CreateAnimStateAssetPlayerNode(
        UAnimationStateGraph* StateGraph,
        UAnimationAsset* AnimationAsset,
        const FVector2D& Position,
        FString& OutBindingType,
        FString& OutErrorMessage)
    {
        OutBindingType.Reset();
        OutErrorMessage.Reset();

        if (!StateGraph)
        {
            OutErrorMessage = TEXT("Animation state graph is null");
            return nullptr;
        }

        if (!AnimationAsset)
        {
            OutErrorMessage = TEXT("Animation asset is null");
            return nullptr;
        }

        if (UAnimSequenceBase* SequenceAsset = Cast<UAnimSequenceBase>(AnimationAsset))
        {
            UAnimGraphNode_SequencePlayer* SequencePlayerNode = CreateAnimStateGraphNode<UAnimGraphNode_SequencePlayer>(StateGraph, Position);
            if (!SequencePlayerNode)
            {
                OutErrorMessage = TEXT("Failed to create sequence player node in animation state graph");
                return nullptr;
            }

            SequencePlayerNode->SetAnimationAsset(SequenceAsset);
            SequencePlayerNode->ReconstructNode();
            OutBindingType = TEXT("sequence_player");
            return SequencePlayerNode;
        }

        if (UBlendSpace* BlendSpaceAsset = Cast<UBlendSpace>(AnimationAsset))
        {
            const bool bIsAimOffset = BlendSpaceAsset->IsA(UAimOffsetBlendSpace::StaticClass())
                || BlendSpaceAsset->IsA(UAimOffsetBlendSpace1D::StaticClass());

            if (bIsAimOffset)
            {
                UAnimGraphNode_RotationOffsetBlendSpace* AimOffsetNode =
                    CreateAnimStateGraphNode<UAnimGraphNode_RotationOffsetBlendSpace>(StateGraph, Position);
                if (!AimOffsetNode)
                {
                    OutErrorMessage = TEXT("Failed to create aim offset player node in animation state graph");
                    return nullptr;
                }

                AimOffsetNode->SetAnimationAsset(BlendSpaceAsset);
                AimOffsetNode->ReconstructNode();
                OutBindingType = TEXT("aim_offset_player");
                return AimOffsetNode;
            }

            UAnimGraphNode_BlendSpacePlayer* BlendSpaceNode =
                CreateAnimStateGraphNode<UAnimGraphNode_BlendSpacePlayer>(StateGraph, Position);
            if (!BlendSpaceNode)
            {
                OutErrorMessage = TEXT("Failed to create blend space player node in animation state graph");
                return nullptr;
            }

            BlendSpaceNode->SetAnimationAsset(BlendSpaceAsset);
            BlendSpaceNode->ReconstructNode();
            OutBindingType = TEXT("blend_space_player");
            return BlendSpaceNode;
        }

        OutErrorMessage = FString::Printf(
            TEXT("Unsupported animation asset class for state binding: %s"),
            *AnimationAsset->GetClass()->GetName());
        return nullptr;
    }

    bool ApplyAnimStateAssetBinding(
        UAnimBlueprint* AnimBlueprint,
        UAnimStateNode* StateNode,
        UAnimationAsset* AnimationAsset,
        FString& OutBindingType,
        FString& OutErrorMessage)
    {
        OutBindingType.Reset();
        OutErrorMessage.Reset();

        if (!AnimBlueprint)
        {
            OutErrorMessage = TEXT("AnimBlueprint is null");
            return false;
        }

        UAnimationStateGraph* StateGraph = ResolveTargetStateBoundGraph(StateNode, OutErrorMessage);
        if (!StateGraph)
        {
            return false;
        }

        UAnimGraphNode_StateResult* ResultNode = ResolveAnimStateResultNode(StateGraph);
        if (!ResultNode)
        {
            OutErrorMessage = TEXT("Animation state graph is missing its result node");
            return false;
        }

        UEdGraphPin* ResultPin = FindFirstVisiblePin(ResultNode, EGPD_Input);
        if (!ResultPin)
        {
            OutErrorMessage = TEXT("Animation state graph result node is missing its input pose pin");
            return false;
        }

        AnimBlueprint->Modify();
        StateNode->Modify();
        StateGraph->Modify();
        ResultNode->Modify();

        ResetAnimStateBoundGraph(AnimBlueprint, StateGraph);

        const FVector2D NodePosition(
            static_cast<float>(ResultNode->NodePosX - 320),
            static_cast<float>(ResultNode->NodePosY));
        UEdGraphNode* AssetPlayerNode = CreateAnimStateAssetPlayerNode(
            StateGraph,
            AnimationAsset,
            NodePosition,
            OutBindingType,
            OutErrorMessage);
        if (!AssetPlayerNode)
        {
            return false;
        }

        UEdGraphPin* OutputPin = FindFirstVisiblePin(AssetPlayerNode, EGPD_Output);
        if (!OutputPin)
        {
            OutErrorMessage = TEXT("Animation state asset player node is missing its output pose pin");
            return false;
        }

        if (!ConnectPinsOnGraph(StateGraph, OutputPin, ResultPin))
        {
            OutErrorMessage = TEXT("Failed to connect animation state asset player output into state result");
            return false;
        }

        StateGraph->NotifyGraphChanged();
        return true;
    }

    TSharedPtr<FJsonObject> BuildAnimStateAssetPlayerSummary(const UAnimStateNode* StateNode)
    {
        TSharedPtr<FJsonObject> AssetPlayerSummary = MakeShared<FJsonObject>();
        AssetPlayerSummary->SetStringField(TEXT("binding_type"), TEXT("custom_graph"));
        AssetPlayerSummary->SetBoolField(TEXT("is_supported_pattern"), false);
        AssetPlayerSummary->SetBoolField(TEXT("is_connected"), false);
        AssetPlayerSummary->SetStringField(TEXT("asset_name"), TEXT(""));
        AssetPlayerSummary->SetStringField(TEXT("asset_path"), TEXT(""));
        AssetPlayerSummary->SetStringField(TEXT("asset_class"), TEXT(""));
        AssetPlayerSummary->SetStringField(TEXT("node_class"), TEXT(""));
        AssetPlayerSummary->SetStringField(TEXT("node_name"), TEXT(""));
        AssetPlayerSummary->SetStringField(TEXT("node_title"), TEXT(""));
        AssetPlayerSummary->SetNumberField(TEXT("graph_node_count"), 0);
        AssetPlayerSummary->SetNumberField(TEXT("asset_player_count"), 0);
        AssetPlayerSummary->SetNumberField(TEXT("non_result_node_count"), 0);
        AssetPlayerSummary->SetBoolField(TEXT("loop"), false);
        AssetPlayerSummary->SetNumberField(TEXT("play_rate"), 0.0);
        AssetPlayerSummary->SetNumberField(TEXT("start_position"), 0.0);
        AssetPlayerSummary->SetNumberField(TEXT("blend_space_x"), 0.0);
        AssetPlayerSummary->SetNumberField(TEXT("blend_space_y"), 0.0);
        AssetPlayerSummary->SetStringField(TEXT("sync_group_name"), TEXT(""));
        AssetPlayerSummary->SetStringField(TEXT("sync_group_role"), TEXT(""));
        AssetPlayerSummary->SetStringField(TEXT("sync_group_method"), TEXT(""));
        AssetPlayerSummary->SetBoolField(
            TEXT("sync_group_override_position_when_joining_sync_group_as_leader"),
            false);

        if (!StateNode || !StateNode->BoundGraph)
        {
            AssetPlayerSummary->SetStringField(TEXT("binding_type"), TEXT("missing_graph"));
            return AssetPlayerSummary;
        }

        UAnimationStateGraph* StateGraph = Cast<UAnimationStateGraph>(StateNode->BoundGraph);
        if (!StateGraph)
        {
            AssetPlayerSummary->SetStringField(TEXT("binding_type"), TEXT("unsupported_graph"));
            return AssetPlayerSummary;
        }

        AssetPlayerSummary->SetNumberField(TEXT("graph_node_count"), StateGraph->Nodes.Num());

        UAnimGraphNode_StateResult* ResultNode = ResolveAnimStateResultNode(StateGraph);
        if (!ResultNode)
        {
            AssetPlayerSummary->SetStringField(TEXT("binding_type"), TEXT("missing_result_node"));
            return AssetPlayerSummary;
        }

        UEdGraphPin* ResultPin = FindFirstVisiblePin(ResultNode, EGPD_Input);
        if (!ResultPin)
        {
            AssetPlayerSummary->SetStringField(TEXT("binding_type"), TEXT("missing_result_pin"));
            return AssetPlayerSummary;
        }

        UEdGraphNode* PrimaryAssetPlayerNode = nullptr;
        UEdGraphPin* PrimaryOutputPin = nullptr;
        UAnimationAsset* PrimaryAnimationAsset = nullptr;
        FString BindingType;
        int32 NonResultNodeCount = 0;
        int32 AssetPlayerCount = 0;

        for (UEdGraphNode* Node : StateGraph->Nodes)
        {
            if (!Node || Node == ResultNode)
            {
                continue;
            }

            ++NonResultNodeCount;

            FString CandidateBindingType;
            UAnimationAsset* CandidateAnimationAsset = nullptr;
            if (UAnimGraphNode_SequencePlayer* SequencePlayerNode = Cast<UAnimGraphNode_SequencePlayer>(Node))
            {
                CandidateBindingType = TEXT("sequence_player");
                CandidateAnimationAsset = SequencePlayerNode->GetAnimationAsset();
            }
            else if (UAnimGraphNode_BlendSpacePlayer* BlendSpaceNode = Cast<UAnimGraphNode_BlendSpacePlayer>(Node))
            {
                CandidateBindingType = TEXT("blend_space_player");
                CandidateAnimationAsset = BlendSpaceNode->GetAnimationAsset();
            }
            else if (UAnimGraphNode_RotationOffsetBlendSpace* AimOffsetNode = Cast<UAnimGraphNode_RotationOffsetBlendSpace>(Node))
            {
                CandidateBindingType = TEXT("aim_offset_player");
                CandidateAnimationAsset = AimOffsetNode->GetAnimationAsset();
            }

            if (CandidateBindingType.IsEmpty())
            {
                continue;
            }

            ++AssetPlayerCount;
            if (!PrimaryAssetPlayerNode)
            {
                PrimaryAssetPlayerNode = Node;
                PrimaryOutputPin = FindFirstVisiblePin(Node, EGPD_Output);
                PrimaryAnimationAsset = CandidateAnimationAsset;
                BindingType = CandidateBindingType;
            }
        }

        AssetPlayerSummary->SetNumberField(TEXT("asset_player_count"), AssetPlayerCount);
        AssetPlayerSummary->SetNumberField(TEXT("non_result_node_count"), NonResultNodeCount);

        if (!PrimaryAssetPlayerNode)
        {
            AssetPlayerSummary->SetStringField(
                TEXT("binding_type"),
                NonResultNodeCount == 0 ? TEXT("empty_state") : TEXT("custom_graph"));
            return AssetPlayerSummary;
        }

        AssetPlayerSummary->SetStringField(TEXT("binding_type"), BindingType);
        AssetPlayerSummary->SetStringField(TEXT("node_class"), PrimaryAssetPlayerNode->GetClass()->GetName());
        AssetPlayerSummary->SetStringField(TEXT("node_name"), PrimaryAssetPlayerNode->GetName());
        AssetPlayerSummary->SetStringField(
            TEXT("node_title"),
            PrimaryAssetPlayerNode->GetNodeTitle(ENodeTitleType::ListView).ToString());

        if (PrimaryAnimationAsset)
        {
            AssetPlayerSummary->SetStringField(TEXT("asset_name"), PrimaryAnimationAsset->GetName());
            AssetPlayerSummary->SetStringField(TEXT("asset_path"), PrimaryAnimationAsset->GetPathName());
            AssetPlayerSummary->SetStringField(TEXT("asset_class"), PrimaryAnimationAsset->GetClass()->GetName());
        }

        bool bConnectedToResult = false;
        if (PrimaryOutputPin)
        {
            for (UEdGraphPin* LinkedPin : ResultPin->LinkedTo)
            {
                if (LinkedPin == PrimaryOutputPin)
                {
                    bConnectedToResult = true;
                    break;
                }
            }
        }

        AssetPlayerSummary->SetBoolField(TEXT("is_connected"), bConnectedToResult);
        AssetPlayerSummary->SetBoolField(
            TEXT("is_supported_pattern"),
            AssetPlayerCount == 1 && NonResultNodeCount == 1 && bConnectedToResult);

        FAnimNode_AssetPlayerBase* PrimaryAssetPlayerRuntime = nullptr;
        UAnimationAsset* IgnoredAnimationAsset = nullptr;
        FString IgnoredBindingType;
        TryDescribeAnimStateAssetPlayerNode(
            PrimaryAssetPlayerNode,
            IgnoredBindingType,
            IgnoredAnimationAsset,
            PrimaryAssetPlayerRuntime);
        PopulateAnimStateAssetPlayerParameterSummary(
            PrimaryAssetPlayerNode,
            PrimaryAssetPlayerRuntime,
            AssetPlayerSummary);
        return AssetPlayerSummary;
    }

    UK2Node_CallFunction* CreateEqualityComparisonNode(UEdGraph* Graph, const FString& PropertyKind, const FVector2D& Position)
    {
        if (!Graph)
        {
            return nullptr;
        }

        UFunction* EqualityFunction = nullptr;
        if (PropertyKind == TEXT("int"))
        {
            EqualityFunction = UKismetMathLibrary::StaticClass()->FindFunctionByName(TEXT("EqualEqual_IntInt"));
        }
        else if (PropertyKind == TEXT("enum") || PropertyKind == TEXT("byte"))
        {
            EqualityFunction = UKismetMathLibrary::StaticClass()->FindFunctionByName(TEXT("EqualEqual_ByteByte"));
        }

        if (!EqualityFunction)
        {
            return nullptr;
        }

        return FUnrealAICommonUtils::CreateFunctionCallNode(Graph, EqualityFunction, Position);
    }

    TSharedPtr<FJsonObject> BuildAnimTransitionRuleSummary(const UAnimStateTransitionNode* TransitionNode)
    {
        TSharedPtr<FJsonObject> RuleSummary = MakeShared<FJsonObject>();
        RuleSummary->SetStringField(TEXT("rule_type"), TEXT("custom_graph"));
        RuleSummary->SetBoolField(TEXT("is_supported_pattern"), false);
        RuleSummary->SetStringField(TEXT("variable_name"), TEXT(""));
        RuleSummary->SetStringField(TEXT("expected_value"), TEXT(""));
        RuleSummary->SetStringField(TEXT("property_kind"), TEXT("unknown"));
        RuleSummary->SetStringField(TEXT("enum_path"), TEXT(""));
        RuleSummary->SetBoolField(TEXT("is_connected"), false);

        if (!TransitionNode || !TransitionNode->BoundGraph)
        {
            RuleSummary->SetStringField(TEXT("rule_type"), TEXT("missing_graph"));
            return RuleSummary;
        }

        UAnimationTransitionGraph* TransitionRuleGraph = Cast<UAnimationTransitionGraph>(TransitionNode->BoundGraph);
        if (!TransitionRuleGraph)
        {
            return RuleSummary;
        }

        RuleSummary->SetNumberField(TEXT("graph_node_count"), TransitionRuleGraph->Nodes.Num());

        UAnimGraphNode_TransitionResult* ResultNode = TransitionRuleGraph->GetResultNode();
        if (!ResultNode)
        {
            ResultNode = TransitionRuleGraph->MyResultNode;
        }
        if (!ResultNode)
        {
            RuleSummary->SetStringField(TEXT("rule_type"), TEXT("missing_result_node"));
            return RuleSummary;
        }

        UEdGraphPin* ResultPin = ResultNode->FindPin(TEXT("bCanEnterTransition"));
        if (!ResultPin)
        {
            RuleSummary->SetStringField(TEXT("rule_type"), TEXT("missing_result_pin"));
            return RuleSummary;
        }

        const bool bHasConnections = ResultPin->LinkedTo.Num() > 0;
        RuleSummary->SetBoolField(TEXT("is_connected"), bHasConnections);

        if (!bHasConnections)
        {
            const bool bCanEnterTransition = ResultPin->DefaultValue.Equals(TEXT("true"), ESearchCase::IgnoreCase) || ResultNode->Node.bCanEnterTransition;
            RuleSummary->SetStringField(TEXT("rule_type"), bCanEnterTransition ? TEXT("always_true") : TEXT("always_false"));
            RuleSummary->SetBoolField(TEXT("is_supported_pattern"), true);
            RuleSummary->SetBoolField(TEXT("constant_value"), bCanEnterTransition);
            return RuleSummary;
        }

        UEdGraphPin* IncomingPin = ResultPin->LinkedTo[0];
        UEdGraphNode* IncomingNode = IncomingPin ? IncomingPin->GetOwningNode() : nullptr;
        if (!IncomingNode)
        {
            return RuleSummary;
        }

        if (const UK2Node_VariableGet* VariableGetNode = Cast<UK2Node_VariableGet>(IncomingNode))
        {
            UEdGraphPin* VariableOutputPin = FindFirstNonExecOutputPin(const_cast<UK2Node_VariableGet*>(VariableGetNode));
            if (VariableOutputPin && VariableOutputPin->PinType.PinCategory == UEdGraphSchema_K2::PC_Boolean)
            {
                RuleSummary->SetStringField(TEXT("rule_type"), TEXT("bool_variable"));
                RuleSummary->SetBoolField(TEXT("is_supported_pattern"), true);
                RuleSummary->SetStringField(TEXT("variable_name"), VariableGetNode->VariableReference.GetMemberName().ToString());
                RuleSummary->SetStringField(TEXT("property_kind"), TEXT("bool"));
                return RuleSummary;
            }
        }

        if (const UK2Node_CallFunction* ComparisonFunctionNode = Cast<UK2Node_CallFunction>(IncomingNode))
        {
            const UFunction* TargetFunction = ComparisonFunctionNode->GetTargetFunction();
            const FName TargetFunctionName = TargetFunction ? TargetFunction->GetFName() : NAME_None;
            if (TargetFunctionName == TEXT("EqualEqual_IntInt") || TargetFunctionName == TEXT("EqualEqual_ByteByte"))
            {
                UEdGraphPin* InputAPin = ComparisonFunctionNode->FindPin(TEXT("A"));
                UEdGraphPin* InputBPin = ComparisonFunctionNode->FindPin(TEXT("B"));
                if (InputAPin && InputBPin)
                {
                    UEdGraphPin* VariableInputPin = nullptr;
                    UEdGraphPin* ConstantInputPin = nullptr;
                    if (InputAPin->LinkedTo.Num() > 0 && InputBPin->LinkedTo.Num() == 0)
                    {
                        VariableInputPin = InputAPin;
                        ConstantInputPin = InputBPin;
                    }
                    else if (InputBPin->LinkedTo.Num() > 0 && InputAPin->LinkedTo.Num() == 0)
                    {
                        VariableInputPin = InputBPin;
                        ConstantInputPin = InputAPin;
                    }

                    if (VariableInputPin && ConstantInputPin)
                    {
                        UEdGraphPin* VariableOutputPin = VariableInputPin->LinkedTo[0];
                        UEdGraphNode* VariableNode = VariableOutputPin ? VariableOutputPin->GetOwningNode() : nullptr;
                        const UK2Node_VariableGet* VariableGetNode = Cast<UK2Node_VariableGet>(VariableNode);
                        if (VariableGetNode && VariableOutputPin)
                        {
                            const FString VariableName = VariableGetNode->VariableReference.GetMemberName().ToString();
                            FString ExpectedValue = ConstantInputPin->DefaultValue;
                            const UEnum* Enum = Cast<UEnum>(VariableOutputPin->PinType.PinSubCategoryObject.Get());

                            RuleSummary->SetStringField(TEXT("variable_name"), VariableName);
                            RuleSummary->SetStringField(TEXT("enum_path"), Enum ? Enum->GetPathName() : TEXT(""));

                            if (TargetFunctionName == TEXT("EqualEqual_IntInt"))
                            {
                                RuleSummary->SetStringField(TEXT("rule_type"), TEXT("int_equals"));
                                RuleSummary->SetStringField(TEXT("property_kind"), TEXT("int"));
                                RuleSummary->SetStringField(TEXT("expected_value"), ExpectedValue);
                                RuleSummary->SetBoolField(TEXT("is_supported_pattern"), true);
                                return RuleSummary;
                            }

                            if (TargetFunctionName == TEXT("EqualEqual_ByteByte") && Enum)
                            {
                                int64 EnumNumericValue = 0;
                                if (FDefaultValueHelper::ParseInt64(ExpectedValue, EnumNumericValue))
                                {
                                    const FString EnumName = Enum->GetAuthoredNameStringByValue(EnumNumericValue);
                                    if (!EnumName.IsEmpty())
                                    {
                                        ExpectedValue = EnumName;
                                    }
                                }

                                RuleSummary->SetStringField(TEXT("rule_type"), TEXT("enum_equals"));
                                RuleSummary->SetStringField(TEXT("property_kind"), TEXT("enum"));
                                RuleSummary->SetStringField(TEXT("expected_value"), ExpectedValue);
                                RuleSummary->SetBoolField(TEXT("is_supported_pattern"), true);
                                return RuleSummary;
                            }
                        }
                    }
                }
            }
        }

        if (const UK2Node_PromotableOperator* ComparisonNode = Cast<UK2Node_PromotableOperator>(IncomingNode))
        {
            UEdGraphPin* InputAPin = ComparisonNode->FindPin(TEXT("A"));
            UEdGraphPin* InputBPin = ComparisonNode->FindPin(TEXT("B"));
            if (InputAPin && InputBPin)
            {
                UEdGraphPin* VariableInputPin = nullptr;
                UEdGraphPin* ConstantInputPin = nullptr;
                if (InputAPin->LinkedTo.Num() > 0 && InputBPin->LinkedTo.Num() == 0)
                {
                    VariableInputPin = InputAPin;
                    ConstantInputPin = InputBPin;
                }
                else if (InputBPin->LinkedTo.Num() > 0 && InputAPin->LinkedTo.Num() == 0)
                {
                    VariableInputPin = InputBPin;
                    ConstantInputPin = InputAPin;
                }

                if (VariableInputPin && ConstantInputPin)
                {
                    UEdGraphNode* VariableNode = VariableInputPin->LinkedTo[0] ? VariableInputPin->LinkedTo[0]->GetOwningNode() : nullptr;
                    const UK2Node_VariableGet* VariableGetNode = Cast<UK2Node_VariableGet>(VariableNode);
                    if (VariableGetNode)
                    {
                        const FString VariableName = VariableGetNode->VariableReference.GetMemberName().ToString();
                        const FString ExpectedValue = ConstantInputPin->DefaultValue;
                        const FString PinCategory = VariableInputPin->PinType.PinCategory.ToString();
                        const UEnum* Enum = Cast<UEnum>(VariableInputPin->PinType.PinSubCategoryObject.Get());

                        RuleSummary->SetStringField(TEXT("variable_name"), VariableName);
                        RuleSummary->SetStringField(TEXT("expected_value"), ExpectedValue);
                        RuleSummary->SetStringField(TEXT("enum_path"), Enum ? Enum->GetPathName() : TEXT(""));

                        if (VariableInputPin->PinType.PinCategory == UEdGraphSchema_K2::PC_Int)
                        {
                            RuleSummary->SetStringField(TEXT("rule_type"), TEXT("int_equals"));
                            RuleSummary->SetStringField(TEXT("property_kind"), TEXT("int"));
                            RuleSummary->SetBoolField(TEXT("is_supported_pattern"), true);
                            return RuleSummary;
                        }

                        if ((VariableInputPin->PinType.PinCategory == UEdGraphSchema_K2::PC_Byte || VariableInputPin->PinType.PinCategory == UEdGraphSchema_K2::PC_Enum) && Enum)
                        {
                            RuleSummary->SetStringField(TEXT("rule_type"), TEXT("enum_equals"));
                            RuleSummary->SetStringField(TEXT("property_kind"), TEXT("enum"));
                            RuleSummary->SetBoolField(TEXT("is_supported_pattern"), true);
                            return RuleSummary;
                        }

                        RuleSummary->SetStringField(TEXT("property_kind"), PinCategory);
                    }
                }
            }
        }

        return RuleSummary;
    }

    bool ApplyAnimTransitionRule(
        UAnimBlueprint* AnimBlueprint,
        UAnimStateTransitionNode* TransitionNode,
        const FString& RuleType,
        const FString& VariableName,
        const FString& ExpectedValue,
        FString& OutErrorMessage)
    {
        OutErrorMessage.Reset();

        if (!AnimBlueprint || !TransitionNode || !TransitionNode->BoundGraph)
        {
            OutErrorMessage = TEXT("Transition node or bound graph is invalid");
            return false;
        }

        UAnimationTransitionGraph* TransitionRuleGraph = Cast<UAnimationTransitionGraph>(TransitionNode->BoundGraph);
        if (!TransitionRuleGraph)
        {
            OutErrorMessage = TEXT("Transition node bound graph is not a transition rule graph");
            return false;
        }

        ResetTransitionRuleGraph(AnimBlueprint, TransitionRuleGraph);
        TransitionNode->Modify();
        TransitionNode->bAutomaticRuleBasedOnSequencePlayerInState = false;

        UEdGraphPin* ResultPin = FindTransitionRuleResultPin(TransitionRuleGraph);
        UAnimGraphNode_TransitionResult* ResultNode = TransitionRuleGraph->GetResultNode();
        if (!ResultNode)
        {
            ResultNode = TransitionRuleGraph->MyResultNode;
        }
        if (!ResultPin || !ResultNode)
        {
            OutErrorMessage = TEXT("Transition rule graph is missing its result node or result pin");
            return false;
        }

        const FString NormalizedRuleType = RuleType.TrimStartAndEnd().ToLower();
        if (NormalizedRuleType == TEXT("always_true"))
        {
            ResultPin->DefaultValue = TEXT("true");
            ResultPin->AutogeneratedDefaultValue = TEXT("false");
            ResultNode->Node.bCanEnterTransition = true;
            TransitionRuleGraph->NotifyGraphChanged();
            return true;
        }

        if (VariableName.TrimStartAndEnd().IsEmpty())
        {
            OutErrorMessage = TEXT("Missing or empty 'variable_name' parameter for transition rule");
            return false;
        }

        FProperty* VariableProperty = ResolveAnimBlueprintProperty(AnimBlueprint, VariableName);
        if (!VariableProperty)
        {
            OutErrorMessage = FString::Printf(TEXT("AnimBlueprint variable not found: %s"), *VariableName);
            return false;
        }

        const FString PropertyKind = GetAnimTransitionRulePropertyKind(VariableProperty);
        UK2Node_VariableGet* VariableGetNode = FUnrealAICommonUtils::CreateVariableGetNode(
            TransitionRuleGraph,
            AnimBlueprint,
            VariableName,
            FVector2D(-320.0f, 0.0f));
        if (!VariableGetNode)
        {
            OutErrorMessage = FString::Printf(TEXT("Failed to create variable get node for transition rule variable: %s"), *VariableName);
            return false;
        }

        UEdGraphPin* VariableOutputPin = FindFirstNonExecOutputPin(VariableGetNode);
        if (!VariableOutputPin)
        {
            OutErrorMessage = TEXT("Created variable get node is missing its data output pin");
            return false;
        }

        if (NormalizedRuleType == TEXT("bool_variable"))
        {
            if (PropertyKind != TEXT("bool"))
            {
                OutErrorMessage = FString::Printf(TEXT("Transition rule 'bool_variable' requires a bool variable, but '%s' is %s"), *VariableName, *PropertyKind);
                return false;
            }

            if (!ConnectPinsOnGraph(TransitionRuleGraph, VariableOutputPin, ResultPin))
            {
                OutErrorMessage = TEXT("Failed to connect bool variable rule into the transition result pin");
                return false;
            }

            TransitionRuleGraph->NotifyGraphChanged();
            return true;
        }

        if (NormalizedRuleType != TEXT("int_equals") && NormalizedRuleType != TEXT("enum_equals"))
        {
            OutErrorMessage = TEXT("'rule_type' must be one of: always_true, bool_variable, int_equals, enum_equals");
            return false;
        }

        if (ExpectedValue.TrimStartAndEnd().IsEmpty())
        {
            OutErrorMessage = TEXT("Missing or empty 'expected_value' parameter for transition equality rule");
            return false;
        }

        if (NormalizedRuleType == TEXT("int_equals") && PropertyKind != TEXT("int"))
        {
            OutErrorMessage = FString::Printf(TEXT("Transition rule 'int_equals' requires an int variable, but '%s' is %s"), *VariableName, *PropertyKind);
            return false;
        }

        if (NormalizedRuleType == TEXT("enum_equals") && PropertyKind != TEXT("enum"))
        {
            OutErrorMessage = FString::Printf(TEXT("Transition rule 'enum_equals' requires an enum variable, but '%s' is %s"), *VariableName, *PropertyKind);
            return false;
        }

        UK2Node_CallFunction* ComparisonNode = CreateEqualityComparisonNode(TransitionRuleGraph, PropertyKind, FVector2D(-40.0f, 0.0f));
        if (!ComparisonNode)
        {
            OutErrorMessage = TEXT("Failed to create equality comparison node for transition rule");
            return false;
        }

        UEdGraphPin* ComparisonInputAPin = FUnrealAICommonUtils::FindPin(ComparisonNode, TEXT("A"), EGPD_Input);
        UEdGraphPin* ComparisonInputBPin = FUnrealAICommonUtils::FindPin(ComparisonNode, TEXT("B"), EGPD_Input);
        if (!ComparisonInputAPin || !ComparisonInputBPin)
        {
            OutErrorMessage = TEXT("Created comparison node is missing its expected input pins");
            return false;
        }

        if (!ConnectPinsOnGraph(TransitionRuleGraph, VariableOutputPin, ComparisonInputAPin))
        {
            OutErrorMessage = TEXT("Failed to connect variable output into comparison node");
            return false;
        }

        ComparisonNode->ReconstructNode();
        TransitionRuleGraph->NotifyGraphChanged();

        ComparisonInputBPin = FUnrealAICommonUtils::FindPin(ComparisonNode, TEXT("B"), EGPD_Input);

        FString ComparisonDefaultValue = ExpectedValue;
        if (NormalizedRuleType == TEXT("enum_equals"))
        {
            UEnum* Enum = ResolveAnimTransitionRuleEnum(VariableProperty);
            if (!Enum)
            {
                OutErrorMessage = FString::Printf(TEXT("Transition rule 'enum_equals' could not resolve enum metadata for '%s'"), *VariableName);
                return false;
            }

            int64 EnumNumericValue = INDEX_NONE;
            if (!FDefaultValueHelper::ParseInt64(ExpectedValue, EnumNumericValue))
            {
                EnumNumericValue = Enum->GetValueByNameString(ExpectedValue);
                if (EnumNumericValue == INDEX_NONE && ExpectedValue.Contains(TEXT("::")))
                {
                    FString ScopedExpectedValue = ExpectedValue;
                    FString Scope;
                    if (ScopedExpectedValue.Split(TEXT("::"), &Scope, &ScopedExpectedValue, ESearchCase::IgnoreCase, ESearchDir::FromEnd))
                    {
                        EnumNumericValue = Enum->GetValueByNameString(ScopedExpectedValue);
                    }
                }
            }

            if (EnumNumericValue == INDEX_NONE)
            {
                OutErrorMessage = FString::Printf(TEXT("Unknown enum value '%s' for %s"), *ExpectedValue, *Enum->GetPathName());
                return false;
            }

            ComparisonDefaultValue = LexToString(EnumNumericValue);
        }

        UEdGraphPin* ComparisonOutputPin = FUnrealAICommonUtils::FindPin(ComparisonNode, UEdGraphSchema_K2::PN_ReturnValue.ToString(), EGPD_Output);
        if (!ComparisonInputBPin || !ComparisonOutputPin)
        {
            OutErrorMessage = TEXT("Comparison node is missing its expected input or bool output pins");
            return false;
        }

        const UEdGraphSchema* TransitionRuleSchema = TransitionRuleGraph->GetSchema();
        if (!TransitionRuleSchema)
        {
            OutErrorMessage = TEXT("Transition rule graph is missing its schema");
            return false;
        }

        TransitionRuleSchema->TrySetDefaultValue(*ComparisonInputBPin, ComparisonDefaultValue);
        if (!ConnectPinsOnGraph(TransitionRuleGraph, ComparisonOutputPin, ResultPin))
        {
            OutErrorMessage = TEXT("Failed to connect comparison node output into the transition result pin");
            return false;
        }

        TransitionRuleGraph->NotifyGraphChanged();
        return true;
    }

    TSharedPtr<FJsonObject> CopyJsonObject(const TSharedPtr<FJsonObject>& SourceObject)
    {
        TSharedPtr<FJsonObject> CopiedObject = MakeShared<FJsonObject>();
        if (SourceObject.IsValid())
        {
            CopiedObject->Values = SourceObject->Values;
        }
        return CopiedObject;
    }

    void AddNumberArrayField(const TSharedPtr<FJsonObject>& JsonObject, const FString& FieldName, const TArray<double>& Values)
    {
        TArray<TSharedPtr<FJsonValue>> JsonValues;
        JsonValues.Reserve(Values.Num());
        for (double Value : Values)
        {
            JsonValues.Add(MakeShared<FJsonValueNumber>(Value));
        }

        JsonObject->SetArrayField(FieldName, JsonValues);
    }

    void AddVectorArrayField(const TSharedPtr<FJsonObject>& JsonObject, const FString& FieldName, const FVector& Value)
    {
        AddNumberArrayField(JsonObject, FieldName, { Value.X, Value.Y, Value.Z });
    }

    void AddRotatorArrayField(const TSharedPtr<FJsonObject>& JsonObject, const FString& FieldName, const FRotator& Value)
    {
        AddNumberArrayField(JsonObject, FieldName, { Value.Pitch, Value.Yaw, Value.Roll });
    }

    void AddColorArrayField(const TSharedPtr<FJsonObject>& JsonObject, const FString& FieldName, const FLinearColor& Value)
    {
        AddNumberArrayField(JsonObject, FieldName, { Value.R, Value.G, Value.B, Value.A });
    }

    TArray<TSharedPtr<FJsonValue>> StringArrayToJson(const TArray<FString>& Values)
    {
        TArray<TSharedPtr<FJsonValue>> JsonValues;
        JsonValues.Reserve(Values.Num());
        for (const FString& Value : Values)
        {
            JsonValues.Add(MakeShared<FJsonValueString>(Value));
        }

        return JsonValues;
    }

    FString BuildAssetObjectPath(const FString& DestinationPath, const FString& AssetName)
    {
        return FString::Printf(TEXT("%s/%s.%s"), *DestinationPath, *AssetName, *AssetName);
    }

    UWorld* GetEditorWorld()
    {
        return GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
    }

    AActor* FindActorByNameInWorld(UWorld* World, const FString& ActorName)
    {
        if (!World)
        {
            return nullptr;
        }

        const FString TrimmedActorName = ActorName.TrimStartAndEnd();
        for (TActorIterator<AActor> It(World); It; ++It)
        {
            AActor* Actor = *It;
            if (Actor && (Actor->GetName() == TrimmedActorName || Actor->GetActorLabel() == TrimmedActorName))
            {
                return Actor;
            }
        }

        return nullptr;
    }

    UPCGComponent* FindPCGComponentOnActor(AActor* Actor, const FString& ComponentName)
    {
        if (!Actor)
        {
            return nullptr;
        }

        TInlineComponentArray<UPCGComponent*> Components;
        Actor->GetComponents(Components);
        if (Components.Num() == 0)
        {
            return nullptr;
        }

        const FString TrimmedComponentName = ComponentName.TrimStartAndEnd();
        if (TrimmedComponentName.IsEmpty())
        {
            return Components[0];
        }

        for (UPCGComponent* Component : Components)
        {
            if (Component && Component->GetName() == TrimmedComponentName)
            {
                return Component;
            }
        }

        if (Components.Num() == 1 && Actor->IsA<APCGVolume>())
        {
            return Components[0];
        }

        return nullptr;
    }

    UFactory* CreateFactoryByClassPath(const TCHAR* FactoryClassPath, FString& OutError)
    {
        OutError.Reset();

        UClass* FactoryClass = StaticLoadClass(UFactory::StaticClass(), nullptr, FactoryClassPath);
        if (!FactoryClass)
        {
            OutError = FString::Printf(TEXT("Failed to load factory class: %s"), FactoryClassPath);
            return nullptr;
        }

        UFactory* Factory = NewObject<UFactory>(GetTransientPackage(), FactoryClass);
        if (!Factory)
        {
            OutError = FString::Printf(TEXT("Failed to instantiate factory class: %s"), FactoryClassPath);
            return nullptr;
        }

        Factory->bEditAfterNew = false;
        return Factory;
    }

    bool SetBoolPropertyValue(UObject* Object, const TCHAR* PropertyName, bool bValue)
    {
        if (!Object)
        {
            return false;
        }

        if (FBoolProperty* BoolProperty = FindFProperty<FBoolProperty>(Object->GetClass(), PropertyName))
        {
            BoolProperty->SetPropertyValue_InContainer(Object, bValue);
            return true;
        }

        return false;
    }

    bool SetObjectPropertyValue(UObject* Object, const TCHAR* PropertyName, UObject* Value)
    {
        if (!Object)
        {
            return false;
        }

        if (FObjectPropertyBase* ObjectProperty = FindFProperty<FObjectPropertyBase>(Object->GetClass(), PropertyName))
        {
            ObjectProperty->SetObjectPropertyValue_InContainer(Object, Value);
            return true;
        }

        return false;
    }

    FString GetPCGComponentGenerationTriggerName(EPCGComponentGenerationTrigger Trigger)
    {
        return GetEnumValueName(StaticEnum<EPCGComponentGenerationTrigger>(), static_cast<int64>(Trigger));
    }

    FString GetPCGPropertyKindName(const FProperty* Property)
    {
        if (!Property)
        {
            return TEXT("unknown");
        }

        if (CastField<FBoolProperty>(Property))
        {
            return TEXT("bool");
        }
        if (CastField<FEnumProperty>(Property))
        {
            return TEXT("enum");
        }
        if (CastField<FByteProperty>(Property))
        {
            return TEXT("byte");
        }
        if (CastField<FIntProperty>(Property))
        {
            return TEXT("int32");
        }
        if (CastField<FInt64Property>(Property))
        {
            return TEXT("int64");
        }
        if (CastField<FFloatProperty>(Property))
        {
            return TEXT("float");
        }
        if (CastField<FDoubleProperty>(Property))
        {
            return TEXT("double");
        }
        if (CastField<FNameProperty>(Property))
        {
            return TEXT("name");
        }
        if (CastField<FStrProperty>(Property))
        {
            return TEXT("string");
        }
        if (CastField<FTextProperty>(Property))
        {
            return TEXT("text");
        }
        if (CastField<FSoftClassProperty>(Property))
        {
            return TEXT("soft_class");
        }
        if (CastField<FSoftObjectProperty>(Property))
        {
            return TEXT("soft_object");
        }
        if (CastField<FClassProperty>(Property))
        {
            return TEXT("class");
        }
        if (CastField<FObjectPropertyBase>(Property))
        {
            return TEXT("object");
        }
        if (CastField<FStructProperty>(Property))
        {
            return TEXT("struct");
        }
        if (CastField<FArrayProperty>(Property))
        {
            return TEXT("array");
        }
        if (CastField<FSetProperty>(Property))
        {
            return TEXT("set");
        }
        if (CastField<FMapProperty>(Property))
        {
            return TEXT("map");
        }

        return TEXT("unknown");
    }

    FString ExportPCGPropertyValueText(const FProperty* Property, const void* Container)
    {
        if (!Property || !Container)
        {
            return TEXT("");
        }

        FString ValueText;
        Property->ExportText_InContainer(0, ValueText, Container, Container, nullptr, PPF_None);
        return ValueText;
    }

    bool IsPCGEditableSettingsProperty(const FProperty* Property)
    {
        if (!Property)
        {
            return false;
        }

        if (!Property->HasAnyPropertyFlags(CPF_Edit) ||
            Property->HasAnyPropertyFlags(CPF_Transient | CPF_Deprecated | CPF_DisableEditOnInstance))
        {
            return false;
        }

        const FName PropertyName = Property->GetFName();
        return PropertyName != GET_MEMBER_NAME_CHECKED(UPCGSettingsInterface, bEnabled) &&
            PropertyName != GET_MEMBER_NAME_CHECKED(UPCGSettingsInterface, bDebug) &&
            PropertyName != GET_MEMBER_NAME_CHECKED(UPCGSettingsInterface, DebugSettings) &&
            PropertyName != GET_MEMBER_NAME_CHECKED(UPCGSettingsInterface, bBreakDebugger) &&
            PropertyName != GET_MEMBER_NAME_CHECKED(UPCGSettingsInterface, bDisplayDebuggingProperties);
    }

    TSharedPtr<FJsonObject> BuildPCGEditablePropertySummary(UObject* Object, const FProperty* Property)
    {
        TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
        if (!Object || !Property)
        {
            return Result;
        }

        Result->SetStringField(TEXT("name"), Property->GetName());
        Result->SetStringField(TEXT("display_name"), Property->GetDisplayNameText().ToString());
        Result->SetStringField(TEXT("cpp_type"), Property->GetCPPType());
        Result->SetStringField(TEXT("property_class"), Property->GetClass()->GetName());
        Result->SetStringField(TEXT("property_kind"), GetPCGPropertyKindName(Property));
        Result->SetStringField(TEXT("owner_class"), Property->GetOwnerStruct() ? Property->GetOwnerStruct()->GetName() : TEXT(""));
        Result->SetStringField(TEXT("value_text"), ExportPCGPropertyValueText(Property, Object));
        Result->SetBoolField(TEXT("is_advanced"), Property->HasAnyPropertyFlags(CPF_AdvancedDisplay));
        Result->SetBoolField(TEXT("is_editable"), true);
        return Result;
    }

    TArray<TSharedPtr<FJsonValue>> BuildPCGEditablePropertySummaryArray(UObject* Object)
    {
        TArray<TSharedPtr<FJsonValue>> Result;
        if (!Object)
        {
            return Result;
        }

        for (TFieldIterator<FProperty> PropertyIt(Object->GetClass()); PropertyIt; ++PropertyIt)
        {
            const FProperty* Property = *PropertyIt;
            if (!IsPCGEditableSettingsProperty(Property))
            {
                continue;
            }

            Result.Add(MakeShared<FJsonValueObject>(BuildPCGEditablePropertySummary(Object, Property)));
        }

        return Result;
    }

    TSharedPtr<FJsonObject> BuildPCGOverridableParamSummary(const FPCGSettingsOverridableParam& Param)
    {
        TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
        TArray<FString> PropertyNames;
        for (const FName& PropertyName : Param.PropertiesNames)
        {
            if (!PropertyName.IsNone())
            {
                PropertyNames.Add(PropertyName.ToString());
            }
        }

        Result->SetStringField(TEXT("label"), Param.Label.ToString());
        Result->SetStringField(TEXT("property_path"), FString::Join(PropertyNames, TEXT(".")));
        Result->SetStringField(TEXT("property_class_name"), Param.PropertyClass ? Param.PropertyClass->GetName() : TEXT(""));
        Result->SetStringField(TEXT("property_class_path"), Param.PropertyClass ? Param.PropertyClass->GetPathName() : TEXT(""));
        Result->SetBoolField(TEXT("has_name_clash"), Param.bHasNameClash);
        Result->SetBoolField(TEXT("has_aliases"), !Param.MapOfAliases.IsEmpty());
        Result->SetBoolField(TEXT("supports_gpu"), Param.bSupportsGPU);
        Result->SetBoolField(TEXT("requires_gpu_readback"), Param.bRequiresGPUReadback);
        Result->SetBoolField(TEXT("is_hard_reference"), Param.IsHardReferenceOverride());

        Result->SetArrayField(TEXT("property_names"), StringArrayToJson(PropertyNames));

        TArray<FString> Aliases;
        for (const TPair<int32, FPCGPropertyAliases>& AliasEntry : Param.MapOfAliases)
        {
            for (const FName& Alias : AliasEntry.Value.Aliases)
            {
                if (!Alias.IsNone())
                {
                    Aliases.AddUnique(Alias.ToString());
                }
            }
        }
        Result->SetArrayField(TEXT("aliases"), StringArrayToJson(Aliases));

#if WITH_EDITOR
        Result->SetStringField(TEXT("display_property_path"), FString::Join(PropertyNames, TEXT(".")));
#endif

        return Result;
    }

    TArray<TSharedPtr<FJsonValue>> BuildPCGOverridableParamSummaryArray(const UPCGSettings* Settings)
    {
        TArray<TSharedPtr<FJsonValue>> Result;
        if (!Settings)
        {
            return Result;
        }

        for (const FPCGSettingsOverridableParam& Param : Settings->OverridableParams())
        {
            Result.Add(MakeShared<FJsonValueObject>(BuildPCGOverridableParamSummary(Param)));
        }

        return Result;
    }

    FString GetPropertyBagPropertyTypeName(EPropertyBagPropertyType PropertyType)
    {
        return GetEnumValueName(StaticEnum<EPropertyBagPropertyType>(), static_cast<int64>(PropertyType));
    }

    FString GetPropertyBagContainerTypeName(EPropertyBagContainerType ContainerType)
    {
        return GetEnumValueName(StaticEnum<EPropertyBagContainerType>(), static_cast<int64>(ContainerType));
    }

    TArray<TSharedPtr<FJsonValue>> BuildPropertyBagContainerTypeArray(const FPropertyBagContainerTypes& ContainerTypes)
    {
        TArray<TSharedPtr<FJsonValue>> Result;
        for (const EPropertyBagContainerType ContainerType : ContainerTypes)
        {
            if (ContainerType != EPropertyBagContainerType::None)
            {
                Result.Add(MakeShared<FJsonValueString>(GetPropertyBagContainerTypeName(ContainerType)));
            }
        }

        return Result;
    }

    bool TryParsePCGGraphParameterType(
        const FString& InType,
        EPropertyBagPropertyType& OutType,
        FString& OutCanonicalType,
        FString& OutError)
    {
        const FString Normalized = InType.TrimStartAndEnd().ToLower().Replace(TEXT("_"), TEXT(""));
        if (Normalized.IsEmpty())
        {
            OutError = TEXT("Missing or empty 'parameter_type' parameter");
            return false;
        }

        if (Normalized == TEXT("bool") || Normalized == TEXT("boolean"))
        {
            OutType = EPropertyBagPropertyType::Bool;
        }
        else if (Normalized == TEXT("int") || Normalized == TEXT("int32") || Normalized == TEXT("integer"))
        {
            OutType = EPropertyBagPropertyType::Int32;
        }
        else if (Normalized == TEXT("int64") || Normalized == TEXT("long"))
        {
            OutType = EPropertyBagPropertyType::Int64;
        }
        else if (Normalized == TEXT("float"))
        {
            OutType = EPropertyBagPropertyType::Float;
        }
        else if (Normalized == TEXT("double"))
        {
            OutType = EPropertyBagPropertyType::Double;
        }
        else if (Normalized == TEXT("name"))
        {
            OutType = EPropertyBagPropertyType::Name;
        }
        else if (Normalized == TEXT("string"))
        {
            OutType = EPropertyBagPropertyType::String;
        }
        else
        {
            OutError = TEXT("'parameter_type' must be one of: Bool, Int32, Int64, Float, Double, Name, String");
            return false;
        }

        OutCanonicalType = GetPropertyBagPropertyTypeName(OutType);
        return true;
    }

    bool TrySetPCGGraphParameterValue(
        UPCGGraphInterface* GraphInterface,
        const FPropertyBagPropertyDesc* PropertyDesc,
        const TSharedPtr<FJsonValue>& JsonValue,
        FString& OutError)
    {
        OutError.Reset();

        if (!GraphInterface || !PropertyDesc)
        {
            OutError = TEXT("Graph parameter target is invalid");
            return false;
        }

        if (!JsonValue.IsValid())
        {
            OutError = TEXT("Missing 'value' parameter");
            return false;
        }

        if (!PropertyDesc->ContainerTypes.IsEmpty())
        {
            OutError = TEXT("Container graph parameters are not supported by this first 9j-5 mutation slice");
            return false;
        }

        const FName PropertyName = PropertyDesc->Name;
        const EPropertyBagPropertyType ValueType = PropertyDesc->ValueType;
        EPropertyBagResult SetResult = EPropertyBagResult::PropertyNotFound;

        switch (ValueType)
        {
        case EPropertyBagPropertyType::Bool:
        {
            bool bValue = false;
            if (!JsonValue->TryGetBool(bValue))
            {
                OutError = FString::Printf(TEXT("Graph parameter '%s' expects a boolean value"), *PropertyName.ToString());
                return false;
            }

            SetResult = GraphInterface->SetGraphParameter(PropertyName, bValue);
            break;
        }
        case EPropertyBagPropertyType::Int32:
        {
            double NumericValue = 0.0;
            if (!JsonValue->TryGetNumber(NumericValue) || !FMath::IsNearlyEqual(NumericValue, FMath::RoundToDouble(NumericValue)))
            {
                OutError = FString::Printf(TEXT("Graph parameter '%s' expects an Int32 numeric value"), *PropertyName.ToString());
                return false;
            }

            SetResult = GraphInterface->SetGraphParameter(PropertyName, static_cast<int32>(NumericValue));
            break;
        }
        case EPropertyBagPropertyType::Int64:
        {
            double NumericValue = 0.0;
            if (!JsonValue->TryGetNumber(NumericValue) || !FMath::IsNearlyEqual(NumericValue, FMath::RoundToDouble(NumericValue)))
            {
                OutError = FString::Printf(TEXT("Graph parameter '%s' expects an Int64 numeric value"), *PropertyName.ToString());
                return false;
            }

            SetResult = GraphInterface->SetGraphParameter(PropertyName, static_cast<int64>(NumericValue));
            break;
        }
        case EPropertyBagPropertyType::Float:
        {
            double NumericValue = 0.0;
            if (!JsonValue->TryGetNumber(NumericValue))
            {
                OutError = FString::Printf(TEXT("Graph parameter '%s' expects a Float numeric value"), *PropertyName.ToString());
                return false;
            }

            SetResult = GraphInterface->SetGraphParameter(PropertyName, static_cast<float>(NumericValue));
            break;
        }
        case EPropertyBagPropertyType::Double:
        {
            double NumericValue = 0.0;
            if (!JsonValue->TryGetNumber(NumericValue))
            {
                OutError = FString::Printf(TEXT("Graph parameter '%s' expects a Double numeric value"), *PropertyName.ToString());
                return false;
            }

            SetResult = GraphInterface->SetGraphParameter(PropertyName, NumericValue);
            break;
        }
        case EPropertyBagPropertyType::Name:
        {
            FString StringValue;
            if (!JsonValue->TryGetString(StringValue))
            {
                OutError = FString::Printf(TEXT("Graph parameter '%s' expects a string value for Name"), *PropertyName.ToString());
                return false;
            }

            SetResult = GraphInterface->SetGraphParameter(PropertyName, FName(*StringValue));
            break;
        }
        case EPropertyBagPropertyType::String:
        {
            FString StringValue;
            if (!JsonValue->TryGetString(StringValue))
            {
                OutError = FString::Printf(TEXT("Graph parameter '%s' expects a string value"), *PropertyName.ToString());
                return false;
            }

            SetResult = GraphInterface->SetGraphParameter(PropertyName, StringValue);
            break;
        }
        default:
            OutError = FString::Printf(TEXT("Graph parameter '%s' uses unsupported value type '%s' in this first 9j-5 mutation slice"),
                *PropertyName.ToString(),
                *GetPropertyBagPropertyTypeName(ValueType));
            return false;
        }

        if (SetResult != EPropertyBagResult::Success)
        {
            OutError = FString::Printf(TEXT("Failed to set graph parameter '%s' (result: %s)"),
                *PropertyName.ToString(),
                *GetEnumValueName(StaticEnum<EPropertyBagResult>(), static_cast<int64>(SetResult)));
            return false;
        }

        return true;
    }

    bool TryParsePCGComponentGenerationTrigger(
        const FString& InTrigger,
        EPCGComponentGenerationTrigger& OutTrigger,
        FString& OutCanonicalTrigger,
        FString& OutError)
    {
        OutCanonicalTrigger.Reset();
        OutError.Reset();

        const FString Normalized = InTrigger.TrimStartAndEnd().ToLower();
        if (Normalized == TEXT("onload") ||
            Normalized == TEXT("load") ||
            Normalized == TEXT("generateonload") ||
            Normalized == TEXT("generate_on_load"))
        {
            OutTrigger = EPCGComponentGenerationTrigger::GenerateOnLoad;
        }
        else if (Normalized == TEXT("ondemand") ||
            Normalized == TEXT("demand") ||
            Normalized == TEXT("generateondemand") ||
            Normalized == TEXT("generate_on_demand"))
        {
            OutTrigger = EPCGComponentGenerationTrigger::GenerateOnDemand;
        }
        else if (Normalized == TEXT("runtime") ||
            Normalized == TEXT("generateatruntime") ||
            Normalized == TEXT("generate_at_runtime"))
        {
            OutTrigger = EPCGComponentGenerationTrigger::GenerateAtRuntime;
        }
        else
        {
            OutError = TEXT("'generation_trigger' must be one of: GenerateOnLoad, GenerateOnDemand, GenerateAtRuntime");
            return false;
        }

        OutCanonicalTrigger = GetPCGComponentGenerationTriggerName(OutTrigger);
        return true;
    }

    TArray<TSharedPtr<FJsonValue>> BuildPCGUserParameterArray(const UPCGGraphInterface* GraphInterface)
    {
        TArray<TSharedPtr<FJsonValue>> ParametersArray;
        if (!GraphInterface)
        {
            return ParametersArray;
        }

        const FInstancedPropertyBag* UserParameters = GraphInterface->GetUserParametersStruct();
        if (!UserParameters)
        {
            return ParametersArray;
        }

        const UStruct* ParameterStruct = UserParameters->GetPropertyBagStruct();
        if (!ParameterStruct)
        {
            return ParametersArray;
        }

        for (TFieldIterator<FProperty> PropertyIt(ParameterStruct); PropertyIt; ++PropertyIt)
        {
            const FProperty* Property = *PropertyIt;
            if (!Property)
            {
                continue;
            }

            const FPropertyBagPropertyDesc* ParameterDesc = UserParameters->FindPropertyDescByName(Property->GetFName());
            const TValueOrError<FString, EPropertyBagResult> SerializedValue = UserParameters->GetValueSerializedString(Property->GetFName());
            bool bOverridden = false;
            if (const UPCGGraphInstance* GraphInstance = Cast<UPCGGraphInstance>(GraphInterface))
            {
                bOverridden = ParameterDesc && ParameterDesc->CachedProperty && GraphInstance->IsPropertyOverridden(ParameterDesc->CachedProperty);
            }
            else
            {
                bOverridden = GraphInterface->IsGraphParameterOverridden(Property->GetFName());
            }

            TSharedPtr<FJsonObject> ParameterObject = MakeShared<FJsonObject>();
            ParameterObject->SetStringField(TEXT("name"), Property->GetName());
            ParameterObject->SetStringField(TEXT("cpp_type"), Property->GetCPPType());
            ParameterObject->SetStringField(TEXT("property_class"), Property->GetClass()->GetName());
            ParameterObject->SetStringField(TEXT("property_kind"), GetPCGPropertyKindName(Property));
            ParameterObject->SetStringField(TEXT("value_text"), SerializedValue.IsValid() ? SerializedValue.GetValue() : TEXT(""));
            ParameterObject->SetStringField(TEXT("value_type"), ParameterDesc ? GetPropertyBagPropertyTypeName(ParameterDesc->ValueType) : TEXT(""));
            ParameterObject->SetStringField(TEXT("value_type_object_path"), ParameterDesc && ParameterDesc->ValueTypeObject ? ParameterDesc->ValueTypeObject->GetPathName() : TEXT(""));
            ParameterObject->SetArrayField(TEXT("container_types"), ParameterDesc ? BuildPropertyBagContainerTypeArray(ParameterDesc->ContainerTypes) : TArray<TSharedPtr<FJsonValue>>());
            ParameterObject->SetBoolField(TEXT("overridden"), bOverridden);
            ParametersArray.Add(MakeShared<FJsonValueObject>(ParameterObject));
        }

        return ParametersArray;
    }

    FString GetPCGRerouteKindName(const UPCGSettings* Settings)
    {
        if (Cast<UPCGNamedRerouteUsageSettings>(Settings))
        {
            return TEXT("named_usage");
        }
        if (Cast<UPCGNamedRerouteDeclarationSettings>(Settings))
        {
            return TEXT("named_declaration");
        }
        if (Cast<UPCGRerouteSettings>(Settings))
        {
            return TEXT("reroute");
        }

        return TEXT("");
    }

    UPCGNode* FindPCGNodeForSettings(const UPCGGraph* Graph, const UPCGSettingsInterface* SettingsInterface)
    {
        if (!Graph || !SettingsInterface)
        {
            return nullptr;
        }

        for (UPCGNode* CandidateNode : Graph->GetNodes())
        {
            if (!CandidateNode)
            {
                continue;
            }

            if (CandidateNode->GetSettingsInterface() == SettingsInterface || CandidateNode->GetSettings() == SettingsInterface)
            {
                return CandidateNode;
            }
        }

        return nullptr;
    }

    TSharedPtr<FJsonObject> BuildPCGGraphCommentSummary(const FPCGGraphCommentNodeData& CommentNode)
    {
        TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
#if WITH_EDITORONLY_DATA
        Result->SetStringField(TEXT("comment_guid"), CommentNode.GUID.ToString(EGuidFormats::DigitsWithHyphens));
        Result->SetStringField(TEXT("comment_text"), CommentNode.NodeComment);
        Result->SetStringField(TEXT("details"), CommentNode.NodeDetails.ToString());
        Result->SetNumberField(TEXT("position_x"), CommentNode.NodePosX);
        Result->SetNumberField(TEXT("position_y"), CommentNode.NodePosY);
        Result->SetNumberField(TEXT("width"), CommentNode.NodeWidth);
        Result->SetNumberField(TEXT("height"), CommentNode.NodeHeight);
        Result->SetNumberField(TEXT("font_size"), CommentNode.FontSize);
        Result->SetNumberField(TEXT("move_mode"), CommentNode.MoveMode);
        Result->SetNumberField(TEXT("comment_depth"), CommentNode.CommentDepth);
        Result->SetBoolField(TEXT("comment_bubble_visible_in_details_panel"), CommentNode.bCommentBubbleVisible_InDetailsPanel != 0);
        Result->SetBoolField(TEXT("color_comment_bubble"), CommentNode.bColorCommentBubble != 0);
        Result->SetBoolField(TEXT("comment_bubble_pinned"), CommentNode.bCommentBubblePinned != 0);
        Result->SetBoolField(TEXT("comment_bubble_visible"), CommentNode.bCommentBubbleVisible != 0);
        AddColorArrayField(Result, TEXT("comment_color"), CommentNode.CommentColor);
#endif
        return Result;
    }

    TArray<TSharedPtr<FJsonValue>> BuildPCGGraphCommentSummaryArray(const UPCGGraph* Graph)
    {
        TArray<TSharedPtr<FJsonValue>> Result;
        if (!Graph)
        {
            return Result;
        }

#if WITH_EDITORONLY_DATA
        const TArray<FPCGGraphCommentNodeData>& CommentNodes = Graph->GetCommentNodes();
        Result.Reserve(CommentNodes.Num());
        for (const FPCGGraphCommentNodeData& CommentNode : CommentNodes)
        {
            Result.Add(MakeShared<FJsonValueObject>(BuildPCGGraphCommentSummary(CommentNode)));
        }
#endif

        return Result;
    }

    bool TryGetPCGCommentGuid(
        const TSharedPtr<FJsonObject>& Params,
        const FString& FieldName,
        FGuid& OutGuid,
        FString& OutError)
    {
        OutGuid.Invalidate();
        OutError.Reset();

        FString GuidText;
        if (!Params->TryGetStringField(FieldName, GuidText) || GuidText.TrimStartAndEnd().IsEmpty())
        {
            OutError = FString::Printf(TEXT("Missing or empty '%s' parameter"), *FieldName);
            return false;
        }

        GuidText = GuidText.TrimStartAndEnd();
        if (!FGuid::Parse(GuidText, OutGuid))
        {
            OutError = FString::Printf(TEXT("'%s' must be a valid GUID string"), *FieldName);
            return false;
        }

        return true;
    }

    bool TryApplyPCGCommentNodePatch(
        const TSharedPtr<FJsonObject>& Params,
        FPCGGraphCommentNodeData& CommentNode,
        FString& OutError)
    {
        OutError.Reset();

#if !WITH_EDITORONLY_DATA
        OutError = TEXT("PCG graph comments require editor-only data");
        return false;
#else
        if (!Params.IsValid())
        {
            OutError = TEXT("PCG graph comment patch payload is invalid");
            return false;
        }

        FString StringValue;
        if (Params->TryGetStringField(TEXT("comment_text"), StringValue))
        {
            CommentNode.NodeComment = StringValue;
        }

        if (Params->TryGetStringField(TEXT("details"), StringValue))
        {
            CommentNode.NodeDetails = FText::FromString(StringValue);
        }

        double NumericValue = 0.0;
        if (Params->TryGetNumberField(TEXT("position_x"), NumericValue))
        {
            CommentNode.NodePosX = FMath::RoundToInt(NumericValue);
        }
        if (Params->TryGetNumberField(TEXT("position_y"), NumericValue))
        {
            CommentNode.NodePosY = FMath::RoundToInt(NumericValue);
        }
        if (Params->TryGetNumberField(TEXT("width"), NumericValue))
        {
            CommentNode.NodeWidth = FMath::Max(1, FMath::RoundToInt(NumericValue));
        }
        if (Params->TryGetNumberField(TEXT("height"), NumericValue))
        {
            CommentNode.NodeHeight = FMath::Max(1, FMath::RoundToInt(NumericValue));
        }
        if (Params->TryGetNumberField(TEXT("font_size"), NumericValue))
        {
            CommentNode.FontSize = FMath::Max(1, FMath::RoundToInt(NumericValue));
        }
        if (Params->TryGetNumberField(TEXT("move_mode"), NumericValue))
        {
            CommentNode.MoveMode = static_cast<uint8>(FMath::Max(0, FMath::RoundToInt(NumericValue)));
        }
        if (Params->TryGetNumberField(TEXT("comment_depth"), NumericValue))
        {
            CommentNode.CommentDepth = FMath::RoundToInt(NumericValue);
        }

        bool bBoolValue = false;
        if (Params->TryGetBoolField(TEXT("comment_bubble_visible_in_details_panel"), bBoolValue))
        {
            CommentNode.bCommentBubbleVisible_InDetailsPanel = bBoolValue ? 1 : 0;
        }
        if (Params->TryGetBoolField(TEXT("color_comment_bubble"), bBoolValue))
        {
            CommentNode.bColorCommentBubble = bBoolValue ? 1 : 0;
        }
        if (Params->TryGetBoolField(TEXT("comment_bubble_pinned"), bBoolValue))
        {
            CommentNode.bCommentBubblePinned = bBoolValue ? 1 : 0;
        }
        if (Params->TryGetBoolField(TEXT("comment_bubble_visible"), bBoolValue))
        {
            CommentNode.bCommentBubbleVisible = bBoolValue ? 1 : 0;
        }

        TArray<double> ColorValues;
        if (TryReadJsonNumberArrayField(Params, TEXT("comment_color"), 4, ColorValues))
        {
            CommentNode.CommentColor = FLinearColor(
                static_cast<float>(ColorValues[0]),
                static_cast<float>(ColorValues[1]),
                static_cast<float>(ColorValues[2]),
                static_cast<float>(ColorValues[3]));
        }
        else if (Params->HasField(TEXT("comment_color")))
        {
            OutError = TEXT("'comment_color' must be a 4-number array [r, g, b, a]");
            return false;
        }

        return true;
#endif
    }

    void NotifyPCGGraphEdited(UPCGGraph* Graph, EPCGChangeType ChangeType)
    {
#if WITH_EDITOR
        if (Graph)
        {
            Graph->ForceNotificationForEditor(ChangeType);
        }
#else
        static_cast<void>(Graph);
        static_cast<void>(ChangeType);
#endif
    }

    TSharedPtr<FJsonObject> BuildPCGGraphSummary(UPCGGraphInterface* GraphInterface, const FString& AssetPathHint = FString())
    {
        TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
        if (!GraphInterface)
        {
            return Result;
        }

        const UPCGGraph* BaseGraph = GraphInterface->GetGraph();
        Result->SetStringField(TEXT("object_name"), GraphInterface->GetName());
        Result->SetStringField(TEXT("object_path"), GraphInterface->GetPathName());
        Result->SetStringField(TEXT("object_class"), GraphInterface->GetClass()->GetName());
        Result->SetStringField(TEXT("asset_path"), AssetPathHint.IsEmpty() ? GraphInterface->GetPathName() : AssetPathHint);
        Result->SetBoolField(TEXT("is_asset"), GraphInterface->IsAsset());
        Result->SetBoolField(TEXT("is_instance"), GraphInterface->IsInstance());
        Result->SetStringField(TEXT("graph_name"), BaseGraph ? BaseGraph->GetName() : TEXT(""));
        Result->SetStringField(TEXT("graph_asset_path"), BaseGraph ? BaseGraph->GetPathName() : TEXT(""));
        Result->SetNumberField(TEXT("node_count"), BaseGraph ? BaseGraph->GetNodes().Num() : 0);
        Result->SetBoolField(TEXT("has_input_node"), BaseGraph && BaseGraph->GetInputNode() != nullptr);
        Result->SetBoolField(TEXT("has_output_node"), BaseGraph && BaseGraph->GetOutputNode() != nullptr);
        Result->SetBoolField(TEXT("hierarchical_generation_enabled"), BaseGraph && BaseGraph->IsHierarchicalGenerationEnabled());
        Result->SetBoolField(TEXT("use_2d_grid"), BaseGraph && BaseGraph->Use2DGrid());
        Result->SetNumberField(TEXT("default_grid_size"), BaseGraph ? static_cast<double>(BaseGraph->GetDefaultGridSize()) : 0.0);

    #if WITH_EDITORONLY_DATA
        const TArray<TSharedPtr<FJsonValue>> CommentNodes = BuildPCGGraphCommentSummaryArray(BaseGraph);
        Result->SetArrayField(TEXT("comment_nodes"), CommentNodes);
        Result->SetNumberField(TEXT("comment_count"), CommentNodes.Num());
    #endif

#if WITH_EDITOR
        Result->SetBoolField(TEXT("is_standalone_graph"), GraphInterface->IsStandaloneGraph());

        const TOptional<FText> TitleOverride = GraphInterface->GetTitleOverride();
        Result->SetBoolField(TEXT("has_title_override"), TitleOverride.IsSet());
        Result->SetStringField(TEXT("title_override"), TitleOverride.IsSet() ? TitleOverride.GetValue().ToString() : TEXT(""));

        const TOptional<FLinearColor> ColorOverride = GraphInterface->GetColorOverride();
        Result->SetBoolField(TEXT("has_color_override"), ColorOverride.IsSet());
        if (ColorOverride.IsSet())
        {
            AddColorArrayField(Result, TEXT("color_override"), ColorOverride.GetValue());
        }

        if (TOptional<FPCGGraphToolData> ToolData = GraphInterface->GetGraphToolData(); ToolData.IsSet())
        {
            TSharedPtr<FJsonObject> ToolDataObject = MakeShared<FJsonObject>();
            ToolDataObject->SetStringField(TEXT("display_name"), ToolData->DisplayName.ToString());
            ToolDataObject->SetStringField(TEXT("tooltip"), ToolData->Tooltip.ToString());
            ToolDataObject->SetArrayField(TEXT("compatible_tool_tags"), StringArrayToJson(ToolData->CompatibleToolTags));
            ToolDataObject->SetStringField(TEXT("initial_actor_class_name"), ToolData->InitialActorClassToSpawn ? ToolData->InitialActorClassToSpawn->GetName() : TEXT(""));
            ToolDataObject->SetStringField(TEXT("initial_actor_class_path"), ToolData->InitialActorClassToSpawn ? ToolData->InitialActorClassToSpawn->GetPathName() : TEXT(""));
            ToolDataObject->SetStringField(TEXT("new_actor_label"), ToolData->NewActorLabel.ToString());
            ToolDataObject->SetBoolField(TEXT("is_preset"), ToolData->bIsPreset);
            Result->SetObjectField(TEXT("tool_data"), ToolDataObject);
        }
#endif

        if (const UPCGGraph* Graph = Cast<UPCGGraph>(GraphInterface))
        {
#if WITH_EDITORONLY_DATA
            Result->SetBoolField(TEXT("is_template"), Graph->bIsTemplate);
            Result->SetBoolField(TEXT("expose_to_library"), Graph->bExposeToLibrary);
            Result->SetBoolField(TEXT("expose_generation_in_asset_explorer"), Graph->bExposeGenerationInAssetExplorer);
            Result->SetStringField(TEXT("category"), Graph->Category.ToString());
            Result->SetStringField(TEXT("description"), Graph->Description.ToString());
            Result->SetBoolField(TEXT("is_standalone_graph_asset"), Graph->bIsStandaloneGraph);
#endif
        }

        if (const UPCGGraphInstance* GraphInstance = Cast<UPCGGraphInstance>(GraphInterface))
        {
            Result->SetStringField(TEXT("parent_graph_path"), GraphInstance->Graph ? GraphInstance->Graph->GetPathName() : TEXT(""));
            Result->SetStringField(TEXT("parent_graph_name"), GraphInstance->Graph ? GraphInstance->Graph->GetName() : TEXT(""));
#if WITH_EDITORONLY_DATA
            Result->SetBoolField(TEXT("override_description"), GraphInstance->bOverrideDescription);
            Result->SetStringField(TEXT("description"), GraphInstance->Description.ToString());
            Result->SetBoolField(TEXT("override_category"), GraphInstance->bOverrideCategory);
            Result->SetStringField(TEXT("category"), GraphInstance->Category.ToString());

            TSharedPtr<FJsonObject> ToolOverrideObject = MakeShared<FJsonObject>();
            ToolOverrideObject->SetStringField(TEXT("display_name"), GraphInstance->ToolDataOverrides.DisplayName.ToString());
            ToolOverrideObject->SetStringField(TEXT("tooltip"), GraphInstance->ToolDataOverrides.Tooltip.ToString());
            ToolOverrideObject->SetBoolField(TEXT("is_preset"), GraphInstance->ToolDataOverrides.bIsPreset);
            Result->SetObjectField(TEXT("tool_data_overrides"), ToolOverrideObject);
#endif
        }

        TArray<TSharedPtr<FJsonValue>> UserParameters = BuildPCGUserParameterArray(GraphInterface);
        Result->SetArrayField(TEXT("user_parameters"), UserParameters);
        Result->SetNumberField(TEXT("user_parameter_count"), UserParameters.Num());
        return Result;
    }

    TSharedPtr<FJsonObject> BuildPCGComponentSummary(UPCGComponent* Component)
    {
        TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
        if (!Component)
        {
            return Result;
        }

        AActor* Owner = Component->GetOwner();
        UPCGGraphInterface* AssignedGraph = Component->GetGraphInstance()
            ? Cast<UPCGGraphInterface>(Component->GetGraphInstance())
            : Cast<UPCGGraphInterface>(Component->GetGraph());

        Result->SetStringField(TEXT("component_name"), Component->GetName());
        Result->SetStringField(TEXT("component_class"), Component->GetClass()->GetName());
        Result->SetBoolField(TEXT("activated"), Component->bActivated);
        Result->SetNumberField(TEXT("seed"), Component->Seed);
        Result->SetBoolField(TEXT("is_partitioned"), Component->bIsComponentPartitioned);
        Result->SetStringField(TEXT("generation_trigger"), GetPCGComponentGenerationTriggerName(Component->GenerationTrigger));
        Result->SetBoolField(TEXT("generate_on_drop_when_trigger_on_demand"), Component->bGenerateOnDropWhenTriggerOnDemand);
        Result->SetBoolField(TEXT("override_generation_radii"), Component->bOverrideGenerationRadii);
        Result->SetStringField(TEXT("scheduling_policy_class"), Component->SchedulingPolicyClass ? Component->SchedulingPolicyClass->GetName() : TEXT(""));
        Result->SetStringField(TEXT("scheduling_policy_path"), Component->SchedulingPolicyClass ? Component->SchedulingPolicyClass->GetPathName() : TEXT(""));
        Result->SetStringField(TEXT("scheduling_policy_instance_class"), Component->GetRuntimeGenSchedulingPolicy() ? Component->GetRuntimeGenSchedulingPolicy()->GetClass()->GetName() : TEXT(""));
        Result->SetBoolField(TEXT("has_graph_instance"), Component->GetGraphInstance() != nullptr);
        Result->SetStringField(TEXT("graph_path"), AssignedGraph ? AssignedGraph->GetPathName() : TEXT(""));
        Result->SetStringField(TEXT("graph_asset_path"), Component->GetGraph() ? Component->GetGraph()->GetPathName() : TEXT(""));
        Result->SetNumberField(TEXT("tool_data_entry_count"), Component->ToolDataContainer.ToolData.Num());

        if (Owner)
        {
            Result->SetStringField(TEXT("actor_name"), Owner->GetName());
            Result->SetStringField(TEXT("actor_label"), Owner->GetActorLabel());
            Result->SetStringField(TEXT("actor_class"), Owner->GetClass()->GetName());
            Result->SetStringField(TEXT("actor_path"), Owner->GetPathName());
            AddVectorArrayField(Result, TEXT("actor_location"), Owner->GetActorLocation());
            AddRotatorArrayField(Result, TEXT("actor_rotation"), Owner->GetActorRotation());
            AddVectorArrayField(Result, TEXT("actor_scale"), Owner->GetActorScale3D());
        }

        if (AssignedGraph)
        {
            Result->SetObjectField(TEXT("graph"), BuildPCGGraphSummary(AssignedGraph));
        }

        return Result;
    }

    bool TryApplyPCGComponentSettings(const TSharedPtr<FJsonObject>& Params, UPCGComponent* Component, FString& OutError)
    {
        OutError.Reset();

        if (!Component)
        {
            OutError = TEXT("PCG component is null");
            return false;
        }

        FString GraphAssetPath;
        Params->TryGetStringField(TEXT("graph_asset_path"), GraphAssetPath);
        GraphAssetPath = GraphAssetPath.TrimStartAndEnd();

        FString GenerationTrigger;
        Params->TryGetStringField(TEXT("generation_trigger"), GenerationTrigger);
        GenerationTrigger = GenerationTrigger.TrimStartAndEnd();
        if (!GenerationTrigger.IsEmpty())
        {
            EPCGComponentGenerationTrigger ParsedTrigger = EPCGComponentGenerationTrigger::GenerateOnLoad;
            FString CanonicalTrigger;
            if (!TryParsePCGComponentGenerationTrigger(GenerationTrigger, ParsedTrigger, CanonicalTrigger, OutError))
            {
                return false;
            }

            Component->GenerationTrigger = ParsedTrigger;
        }

        bool bBoolValue = false;
        if (Params->TryGetBoolField(TEXT("is_partitioned"), bBoolValue))
        {
            Component->SetIsPartitioned(bBoolValue);
        }

        if (Params->TryGetBoolField(TEXT("activated"), bBoolValue))
        {
            Component->bActivated = bBoolValue;
        }

        if (Params->TryGetBoolField(TEXT("generate_on_drop_when_trigger_on_demand"), bBoolValue))
        {
            Component->bGenerateOnDropWhenTriggerOnDemand = bBoolValue;
        }

        int32 SeedValue = 0;
        if (Params->TryGetNumberField(TEXT("seed"), SeedValue))
        {
            Component->Seed = SeedValue;
        }

        if (!GraphAssetPath.IsEmpty())
        {
            UPCGGraphInterface* GraphInterface = Cast<UPCGGraphInterface>(UEditorAssetLibrary::LoadAsset(GraphAssetPath));
            if (!GraphInterface)
            {
                OutError = FString::Printf(TEXT("Failed to load PCG graph asset: %s"), *GraphAssetPath);
                return false;
            }

            // Establish runtime-generation state first so graph assignment does not queue a stale editor refresh.
            Component->SetGraphLocal(GraphInterface);
        }

        Component->Modify();
        Component->MarkPackageDirty();
        if (AActor* Owner = Component->GetOwner())
        {
            Owner->Modify();
            Owner->MarkPackageDirty();
        }

        return true;
    }

    FString GetPCGSettingsTypeName(EPCGSettingsType SettingsType)
    {
        return GetEnumValueName(StaticEnum<EPCGSettingsType>(), static_cast<int64>(SettingsType));
    }

    FString GetPCGPinUsageName(EPCGPinUsage PinUsage)
    {
        return GetEnumValueName(StaticEnum<EPCGPinUsage>(), static_cast<int64>(PinUsage));
    }

    FString GetPCGPinStatusName(EPCGPinStatus PinStatus)
    {
        return GetEnumValueName(StaticEnum<EPCGPinStatus>(), static_cast<int64>(PinStatus));
    }

    TSharedPtr<FJsonObject> BuildPCGPinSummary(const FPCGPinProperties& PinProperties, bool bIsConnected)
    {
        TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
        Result->SetStringField(TEXT("label"), PinProperties.Label.ToString());
        Result->SetStringField(TEXT("usage"), GetPCGPinUsageName(PinProperties.Usage));
        Result->SetStringField(TEXT("status"), GetPCGPinStatusName(PinProperties.PinStatus));
        Result->SetStringField(TEXT("allowed_types"), PinProperties.AllowedTypes.ToString());
        Result->SetBoolField(TEXT("allow_multiple_data"), PinProperties.bAllowMultipleData);
        Result->SetBoolField(TEXT("allow_multiple_connections"), PinProperties.AllowsMultipleConnections());
        Result->SetBoolField(TEXT("advanced"), PinProperties.IsAdvancedPin());
        Result->SetBoolField(TEXT("required"), PinProperties.IsRequiredPin());
        Result->SetBoolField(TEXT("invisible"), PinProperties.bInvisiblePin);
        Result->SetBoolField(TEXT("connected"), bIsConnected);
#if WITH_EDITORONLY_DATA
        Result->SetStringField(TEXT("tooltip"), PinProperties.Tooltip.ToString());
#endif
        return Result;
    }

    TArray<TSharedPtr<FJsonValue>> BuildPCGPinSummaryArray(const TArray<FPCGPinProperties>& PinPropertiesArray, TFunctionRef<bool(const FName&)> IsConnected)
    {
        TArray<TSharedPtr<FJsonValue>> Result;
        Result.Reserve(PinPropertiesArray.Num());
        for (const FPCGPinProperties& PinProperties : PinPropertiesArray)
        {
            Result.Add(MakeShared<FJsonValueObject>(BuildPCGPinSummary(PinProperties, IsConnected(PinProperties.Label))));
        }

        return Result;
    }

    TSharedPtr<FJsonObject> BuildPCGNodeTypeSummary(UClass* SettingsClass)
    {
        TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
        if (!SettingsClass || !SettingsClass->IsChildOf(UPCGSettings::StaticClass()))
        {
            return Result;
        }

        const UPCGSettings* Settings = Cast<UPCGSettings>(SettingsClass->GetDefaultObject());
        if (!Settings)
        {
            return Result;
        }

        Result->SetStringField(TEXT("class_name"), SettingsClass->GetName());
        Result->SetStringField(TEXT("class_path"), SettingsClass->GetPathName());
        Result->SetStringField(TEXT("default_node_name"), Settings->GetDefaultNodeName().ToString());
        Result->SetStringField(TEXT("default_node_title"), Settings->GetDefaultNodeTitle().ToString());
        Result->SetStringField(TEXT("settings_type"), GetPCGSettingsTypeName(Settings->GetType()));
        Result->SetBoolField(TEXT("can_be_disabled"), Settings->CanBeDisabled());
        Result->SetBoolField(TEXT("can_be_debugged"), Settings->CanBeDebugged());
        Result->SetBoolField(TEXT("has_dynamic_pins"), Settings->HasDynamicPins());
        Result->SetBoolField(TEXT("output_pins_can_be_deactivated"), Settings->OutputPinsCanBeDeactivated());
        Result->SetBoolField(TEXT("uses_seed"), Settings->UseSeed());

        TArray<FString> Aliases;
#if WITH_EDITOR
        for (const FText& Alias : Settings->GetNodeTitleAliases())
        {
            Aliases.Add(Alias.ToString());
        }

        Result->SetStringField(TEXT("tooltip"), Settings->GetNodeTooltipText().ToString());
        AddColorArrayField(Result, TEXT("title_color"), Settings->GetNodeTitleColor());
#endif
        Result->SetArrayField(TEXT("aliases"), StringArrayToJson(Aliases));
        Result->SetArrayField(TEXT("default_input_pins"), BuildPCGPinSummaryArray(Settings->DefaultInputPinProperties(), [](const FName&) { return false; }));
        Result->SetArrayField(TEXT("default_output_pins"), BuildPCGPinSummaryArray(Settings->DefaultOutputPinProperties(), [](const FName&) { return false; }));
        return Result;
    }

    TSharedPtr<FJsonObject> BuildPCGNodeSummary(UPCGNode* Node)
    {
        TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
        if (!Node)
        {
            return Result;
        }

        UPCGSettingsInterface* SettingsInterface = Node->GetSettingsInterface();
        UPCGSettings* Settings = Node->GetSettings();

        Result->SetStringField(TEXT("node_name"), Node->GetName());
        Result->SetStringField(TEXT("node_path"), Node->GetPathName());
        Result->SetStringField(TEXT("graph_path"), Node->GetGraph() ? Node->GetGraph()->GetPathName() : TEXT(""));
        Result->SetBoolField(TEXT("is_instance"), Node->IsInstance());
        Result->SetStringField(TEXT("authored_title"), Node->HasAuthoredTitle() ? Node->GetAuthoredTitleLine().ToString() : TEXT(""));
        Result->SetStringField(TEXT("default_title"), Node->GetDefaultTitle().ToString());
        Result->SetStringField(TEXT("generated_title"), Node->GetGeneratedTitleLine().ToString());
        Result->SetStringField(TEXT("settings_class_name"), Settings ? Settings->GetClass()->GetName() : TEXT(""));
        Result->SetStringField(TEXT("settings_class_path"), Settings ? Settings->GetClass()->GetPathName() : TEXT(""));
        Result->SetStringField(TEXT("settings_type"), Settings ? GetPCGSettingsTypeName(Settings->GetType()) : TEXT("Unknown"));
        Result->SetStringField(TEXT("reroute_kind"), GetPCGRerouteKindName(Settings));
        Result->SetBoolField(TEXT("enabled"), SettingsInterface ? SettingsInterface->bEnabled : false);
        Result->SetBoolField(TEXT("debug"), SettingsInterface ? SettingsInterface->bDebug : false);
        Result->SetBoolField(TEXT("can_be_disabled"), Settings ? Settings->CanBeDisabled() : false);
        Result->SetBoolField(TEXT("can_be_debugged"), Settings ? Settings->CanBeDebugged() : false);
        Result->SetBoolField(TEXT("has_dynamic_pins"), Settings ? Settings->HasDynamicPins() : false);
        Result->SetBoolField(TEXT("output_pins_can_be_deactivated"), Settings ? Settings->OutputPinsCanBeDeactivated() : false);
        Result->SetBoolField(TEXT("uses_seed"), Settings ? Settings->UseSeed() : false);

        bool bInspecting = false;
    #if WITH_EDITOR
        bInspecting = SettingsInterface ? SettingsInterface->bIsInspecting : false;
    #endif
        Result->SetBoolField(TEXT("inspecting"), bInspecting);
        Result->SetBoolField(TEXT("has_inbound_edges"), Node->HasInboundEdges());
        Result->SetNumberField(TEXT("inbound_edge_count"), Node->GetInboundEdgesNum());

        int32 PositionX = 0;
        int32 PositionY = 0;
#if WITH_EDITOR
        Node->GetNodePosition(PositionX, PositionY);
        Result->SetStringField(TEXT("tooltip"), Node->GetNodeTooltipText().ToString());
        Result->SetBoolField(TEXT("hidden"), Node->IsHidden());
#endif
        Result->SetNumberField(TEXT("position_x"), PositionX);
        Result->SetNumberField(TEXT("position_y"), PositionY);
#if WITH_EDITORONLY_DATA
        Result->SetStringField(TEXT("comment"), Node->NodeComment);
#endif

        Result->SetArrayField(TEXT("input_pins"), BuildPCGPinSummaryArray(Node->InputPinProperties(), [Node](const FName& Label)
        {
            return Node->IsInputPinConnected(Label);
        }));
        Result->SetArrayField(TEXT("output_pins"), BuildPCGPinSummaryArray(Node->OutputPinProperties(), [Node](const FName& Label)
        {
            return Node->IsOutputPinConnected(Label);
        }));

        if (const UPCGSubgraphSettings* SubgraphSettings = Cast<UPCGSubgraphSettings>(Settings))
        {
            Result->SetStringField(TEXT("subgraph_graph_path"), SubgraphSettings->GetSubgraph() ? SubgraphSettings->GetSubgraph()->GetPathName() : TEXT(""));
            Result->SetStringField(TEXT("subgraph_interface_path"), SubgraphSettings->GetSubgraphInterface() ? SubgraphSettings->GetSubgraphInterface()->GetPathName() : TEXT(""));
            Result->SetStringField(TEXT("subgraph_override_path"), SubgraphSettings->SubgraphOverride ? SubgraphSettings->SubgraphOverride->GetPathName() : TEXT(""));
            Result->SetStringField(TEXT("subgraph_instance_path"), SubgraphSettings->SubgraphInstance ? SubgraphSettings->SubgraphInstance->GetPathName() : TEXT(""));
        }

        if (const UPCGNamedRerouteUsageSettings* NamedUsageSettings = Cast<UPCGNamedRerouteUsageSettings>(Settings))
        {
            const UPCGNode* DeclarationNode = FindPCGNodeForSettings(Node->GetGraph(), NamedUsageSettings->Declaration);
            Result->SetStringField(TEXT("declaration_settings_path"), NamedUsageSettings->Declaration ? NamedUsageSettings->Declaration->GetPathName() : TEXT(""));
            Result->SetStringField(TEXT("declaration_node_path"), DeclarationNode ? DeclarationNode->GetPathName() : TEXT(""));
            Result->SetStringField(TEXT("declaration_node_name"), DeclarationNode ? DeclarationNode->GetName() : TEXT(""));
            Result->SetStringField(TEXT("declaration_node_title"), DeclarationNode ? DeclarationNode->GetGeneratedTitleLine().ToString() : TEXT(""));
        }
        else if (const UPCGNamedRerouteDeclarationSettings* NamedDeclarationSettings = Cast<UPCGNamedRerouteDeclarationSettings>(Settings))
        {
            TArray<TSharedPtr<FJsonValue>> UsageNodeArray;
            if (const UPCGGraph* Graph = Node->GetGraph())
            {
                for (UPCGNode* CandidateNode : Graph->GetNodes())
                {
                    const UPCGNamedRerouteUsageSettings* CandidateUsageSettings = CandidateNode ? Cast<UPCGNamedRerouteUsageSettings>(CandidateNode->GetSettings()) : nullptr;
                    if (!CandidateUsageSettings || CandidateUsageSettings->Declaration != NamedDeclarationSettings)
                    {
                        continue;
                    }

                    TSharedPtr<FJsonObject> UsageNodeObject = MakeShared<FJsonObject>();
                    UsageNodeObject->SetStringField(TEXT("node_path"), CandidateNode->GetPathName());
                    UsageNodeObject->SetStringField(TEXT("node_name"), CandidateNode->GetName());
                    UsageNodeObject->SetStringField(TEXT("node_title"), CandidateNode->GetGeneratedTitleLine().ToString());
                    UsageNodeArray.Add(MakeShared<FJsonValueObject>(UsageNodeObject));
                }
            }

            Result->SetArrayField(TEXT("usage_nodes"), UsageNodeArray);
            Result->SetNumberField(TEXT("usage_node_count"), UsageNodeArray.Num());
        }

        const TArray<TSharedPtr<FJsonValue>> EditableProperties = BuildPCGEditablePropertySummaryArray(Settings);
        Result->SetArrayField(TEXT("editable_properties"), EditableProperties);
        Result->SetNumberField(TEXT("editable_property_count"), EditableProperties.Num());

        const TArray<TSharedPtr<FJsonValue>> OverridableParams = BuildPCGOverridableParamSummaryArray(Settings);
        Result->SetArrayField(TEXT("overridable_params"), OverridableParams);
        Result->SetNumberField(TEXT("overridable_param_count"), OverridableParams.Num());

        return Result;
    }

    UPCGGraphInterface* LoadPCGGraphInterfaceAsset(const TSharedPtr<FJsonObject>& Params, FString& OutAssetPath, FString& OutError)
    {
        OutAssetPath.Reset();
        OutError.Reset();

        if (!Params->TryGetStringField(TEXT("asset_path"), OutAssetPath) || OutAssetPath.TrimStartAndEnd().IsEmpty())
        {
            OutError = TEXT("Missing or empty 'asset_path' parameter");
            return nullptr;
        }
        OutAssetPath = OutAssetPath.TrimStartAndEnd();

        UObject* LoadedObject = UEditorAssetLibrary::LoadAsset(OutAssetPath);
        UPCGGraphInterface* GraphInterface = Cast<UPCGGraphInterface>(LoadedObject);
        if (!GraphInterface)
        {
            OutError = FString::Printf(TEXT("PCG graph asset not found or wrong class: %s"), *OutAssetPath);
            return nullptr;
        }

        return GraphInterface;
    }

    UPCGGraph* LoadPCGEditableGraphAsset(const TSharedPtr<FJsonObject>& Params, FString& OutAssetPath, FString& OutError)
    {
        OutAssetPath.Reset();
        OutError.Reset();

        if (!Params->TryGetStringField(TEXT("asset_path"), OutAssetPath) || OutAssetPath.TrimStartAndEnd().IsEmpty())
        {
            OutError = TEXT("Missing or empty 'asset_path' parameter");
            return nullptr;
        }
        OutAssetPath = OutAssetPath.TrimStartAndEnd();

        UObject* LoadedObject = UEditorAssetLibrary::LoadAsset(OutAssetPath);
        UPCGGraph* Graph = Cast<UPCGGraph>(LoadedObject);
        if (!Graph)
        {
            OutError = FString::Printf(TEXT("PCG graph asset not found or not editable as a UPCGGraph: %s"), *OutAssetPath);
            return nullptr;
        }

        return Graph;
    }

    const FPropertyBagPropertyDesc* ResolvePCGGraphParameterDesc(
        const UPCGGraphInterface* GraphInterface,
        const FString& ParameterName,
        FString& OutError)
    {
        OutError.Reset();

        if (!GraphInterface)
        {
            OutError = TEXT("PCG graph interface is null");
            return nullptr;
        }

        const FString TrimmedName = ParameterName.TrimStartAndEnd();
        if (TrimmedName.IsEmpty())
        {
            OutError = TEXT("Missing or empty 'parameter_name' parameter");
            return nullptr;
        }

        const FInstancedPropertyBag* UserParameters = GraphInterface->GetUserParametersStruct();
        if (!UserParameters)
        {
            OutError = TEXT("PCG graph interface does not expose user parameters");
            return nullptr;
        }

        const FPropertyBagPropertyDesc* PropertyDesc = UserParameters->FindPropertyDescByName(FName(*TrimmedName));
        if (!PropertyDesc)
        {
            OutError = FString::Printf(TEXT("Graph parameter not found: %s"), *TrimmedName);
            return nullptr;
        }

        return PropertyDesc;
    }

    UClass* ResolvePCGSettingsClass(const FString& Identifier)
    {
        const FString TrimmedIdentifier = Identifier.TrimStartAndEnd();
        if (TrimmedIdentifier.IsEmpty())
        {
            return nullptr;
        }

        if (UClass* LoadedClass = StaticLoadClass(UPCGSettings::StaticClass(), nullptr, *TrimmedIdentifier))
        {
            if (LoadedClass->IsChildOf(UPCGSettings::StaticClass()))
            {
                return LoadedClass;
            }
        }

        TArray<UClass*> DerivedClasses;
        GetDerivedClasses(UPCGSettings::StaticClass(), DerivedClasses, true);
        for (UClass* SettingsClass : DerivedClasses)
        {
            if (!SettingsClass || SettingsClass->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists))
            {
                continue;
            }

            const UPCGSettings* Settings = Cast<UPCGSettings>(SettingsClass->GetDefaultObject());
            if (!Settings)
            {
                continue;
            }

            if (SettingsClass->GetName().Equals(TrimmedIdentifier, ESearchCase::IgnoreCase) ||
                SettingsClass->GetPathName().Equals(TrimmedIdentifier, ESearchCase::IgnoreCase) ||
                Settings->GetDefaultNodeName().ToString().Equals(TrimmedIdentifier, ESearchCase::IgnoreCase) ||
                Settings->GetDefaultNodeTitle().ToString().Equals(TrimmedIdentifier, ESearchCase::IgnoreCase))
            {
                return SettingsClass;
            }

#if WITH_EDITOR
            for (const FText& Alias : Settings->GetNodeTitleAliases())
            {
                if (Alias.ToString().Equals(TrimmedIdentifier, ESearchCase::IgnoreCase))
                {
                    return SettingsClass;
                }
            }
#endif
        }

        return nullptr;
    }

    TArray<UPCGNode*> GetInspectablePCGGraphNodes(UPCGGraph* Graph)
    {
        TArray<UPCGNode*> Nodes;
        if (!Graph)
        {
            return Nodes;
        }

        auto AddNodeIfMissing = [&Nodes](UPCGNode* Node)
        {
            if (Node && !Nodes.Contains(Node))
            {
                Nodes.Add(Node);
            }
        };

        AddNodeIfMissing(Graph->GetInputNode());
        for (UPCGNode* Node : Graph->GetNodes())
        {
            AddNodeIfMissing(Node);
        }
        AddNodeIfMissing(Graph->GetOutputNode());
        return Nodes;
    }

    UPCGNode* ResolvePCGGraphNode(UPCGGraph* Graph, const TSharedPtr<FJsonObject>& Params, FString& OutError)
    {
        OutError.Reset();

        if (!Graph)
        {
            OutError = TEXT("PCG graph is null");
            return nullptr;
        }

        FString NodePath;
        FString NodeName;
        FString NodeTitle;
        Params->TryGetStringField(TEXT("node_path"), NodePath);
        Params->TryGetStringField(TEXT("node_name"), NodeName);
        Params->TryGetStringField(TEXT("node_title"), NodeTitle);
        NodePath = NodePath.TrimStartAndEnd();
        NodeName = NodeName.TrimStartAndEnd();
        NodeTitle = NodeTitle.TrimStartAndEnd();

        if (NodePath.IsEmpty() && NodeName.IsEmpty() && NodeTitle.IsEmpty())
        {
            OutError = TEXT("One of 'node_path', 'node_name', or 'node_title' is required");
            return nullptr;
        }

        for (UPCGNode* Node : GetInspectablePCGGraphNodes(Graph))
        {
            if (!Node)
            {
                continue;
            }

            if (!NodePath.IsEmpty() && Node->GetPathName().Equals(NodePath, ESearchCase::IgnoreCase))
            {
                return Node;
            }

            if (!NodeName.IsEmpty() && Node->GetName().Equals(NodeName, ESearchCase::IgnoreCase))
            {
                return Node;
            }

            if (!NodeTitle.IsEmpty())
            {
                const FString AuthoredTitle = Node->HasAuthoredTitle() ? Node->GetAuthoredTitleLine().ToString() : FString();
                const FString DefaultTitle = Node->GetDefaultTitle().ToString();
                const FString GeneratedTitle = Node->GetGeneratedTitleLine().ToString();
                if (AuthoredTitle.Equals(NodeTitle, ESearchCase::IgnoreCase) ||
                    DefaultTitle.Equals(NodeTitle, ESearchCase::IgnoreCase) ||
                    GeneratedTitle.Equals(NodeTitle, ESearchCase::IgnoreCase))
                {
                    return Node;
                }
            }
        }

        OutError = TEXT("Failed to resolve PCG graph node from the supplied identifier");
        return nullptr;
    }

    TSharedPtr<FJsonObject> BuildValidationIssue(
        const FString& Severity,
        const FString& Code,
        const FString& Message,
        const FString& FieldName = FString())
    {
        TSharedPtr<FJsonObject> Issue = MakeShared<FJsonObject>();
        Issue->SetStringField(TEXT("severity"), Severity);
        Issue->SetStringField(TEXT("code"), Code);
        Issue->SetStringField(TEXT("message"), Message);
        if (!FieldName.IsEmpty())
        {
            Issue->SetStringField(TEXT("field"), FieldName);
        }
        return Issue;
    }

    void AddValidationIssue(
        TArray<TSharedPtr<FJsonValue>>& Issues,
        int32& ErrorCount,
        int32& WarningCount,
        const FString& Severity,
        const FString& Code,
        const FString& Message,
        const FString& FieldName = FString())
    {
        Issues.Add(MakeShared<FJsonValueObject>(BuildValidationIssue(Severity, Code, Message, FieldName)));
        if (Severity == TEXT("error"))
        {
            ++ErrorCount;
        }
        else if (Severity == TEXT("warning"))
        {
            ++WarningCount;
        }
    }

    bool TryWriteJsonObjectToString(const TSharedPtr<FJsonObject>& JsonObject, FString& OutJson)
    {
        OutJson.Reset();
        if (!JsonObject.IsValid())
        {
            return false;
        }

        TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutJson);
        const bool bSerialized = FJsonSerializer::Serialize(JsonObject.ToSharedRef(), Writer);
        Writer->Close();
        return bSerialized;
    }

    TSharedPtr<FJsonObject> BuildNormalizedCurveTableRowData(
        const FString& RowName,
        const TArray<FCurveTableImportedKey>& ImportedKeys)
    {
        TSharedPtr<FJsonObject> NormalizedRowData = MakeShared<FJsonObject>();
        NormalizedRowData->SetStringField(TEXT("Name"), RowName);
        for (const FCurveTableImportedKey& ImportedKey : ImportedKeys)
        {
            NormalizedRowData->SetNumberField(FString::SanitizeFloat(ImportedKey.Time, 0), ImportedKey.Value);
        }
        return NormalizedRowData;
    }

    bool TryParseCurveTableRowData(
        const TSharedPtr<FJsonObject>& RowDataObject,
        const FString& DesiredRowName,
        TArray<FCurveTableImportedKey>& OutCurveKeys,
        FString& OutErrorCode,
        FString& OutErrorMessage)
    {
        OutCurveKeys.Reset();
        OutErrorCode.Reset();
        OutErrorMessage.Reset();

        if (!RowDataObject.IsValid())
        {
            OutErrorCode = TEXT("invalid_row_data");
            OutErrorMessage = TEXT("Missing or invalid 'row_data' object parameter");
            return false;
        }

        FString ExistingRowName;
        if (RowDataObject->TryGetStringField(TEXT("Name"), ExistingRowName) && ExistingRowName != DesiredRowName)
        {
            OutErrorCode = TEXT("row_name_mismatch");
            OutErrorMessage = FString::Printf(
                TEXT("row_data.Name must match row_name '%s' for this CurveTable"),
                *DesiredRowName);
            return false;
        }

        for (const TPair<FString, TSharedPtr<FJsonValue>>& FieldPair : RowDataObject->Values)
        {
            if (FieldPair.Key == TEXT("Name"))
            {
                continue;
            }

            float ParsedTime = 0.0f;
            if (!FDefaultValueHelper::ParseFloat(FieldPair.Key, ParsedTime))
            {
                OutErrorCode = TEXT("invalid_curve_time");
                OutErrorMessage = FString::Printf(
                    TEXT("row_data field '%s' is not a valid numeric curve time"),
                    *FieldPair.Key);
                return false;
            }

            double ParsedValue = 0.0;
            if (!FieldPair.Value.IsValid() || !FieldPair.Value->TryGetNumber(ParsedValue))
            {
                OutErrorCode = TEXT("non_numeric_curve_value");
                OutErrorMessage = FString::Printf(
                    TEXT("row_data.%s must be a numeric curve value"),
                    *FieldPair.Key);
                return false;
            }

            for (const FCurveTableImportedKey& ExistingKey : OutCurveKeys)
            {
                if (ExistingKey.Time == ParsedTime)
                {
                    OutErrorCode = TEXT("duplicate_curve_time");
                    OutErrorMessage = FString::Printf(
                        TEXT("row_data contains duplicate curve times that normalize to %g"),
                        ParsedTime);
                    return false;
                }
            }

            FCurveTableImportedKey& ImportedKey = OutCurveKeys.AddDefaulted_GetRef();
            ImportedKey.Time = ParsedTime;
            ImportedKey.Value = static_cast<float>(ParsedValue);
        }

        if (OutCurveKeys.Num() == 0)
        {
            OutErrorCode = TEXT("missing_curve_keys");
            OutErrorMessage = TEXT("row_data must include at least one numeric curve time/value field");
            return false;
        }

        OutCurveKeys.Sort([](const FCurveTableImportedKey& A, const FCurveTableImportedKey& B)
        {
            return A.Time < B.Time;
        });

        return true;
    }

    void PopulateSimpleCurveFromImportedKeys(FSimpleCurve& Curve, const TArray<FCurveTableImportedKey>& ImportedKeys)
    {
        Curve.SetKeyInterpMode(RCIM_Linear);
        for (const FCurveTableImportedKey& ImportedKey : ImportedKeys)
        {
            Curve.AddKey(ImportedKey.Time, ImportedKey.Value);
        }
    }

    void PopulateRichCurveFromImportedKeys(FRichCurve& Curve, const TArray<FCurveTableImportedKey>& ImportedKeys)
    {
        for (const FCurveTableImportedKey& ImportedKey : ImportedKeys)
        {
            const FKeyHandle KeyHandle = Curve.AddKey(ImportedKey.Time, ImportedKey.Value);
            Curve.SetKeyInterpMode(KeyHandle, RCIM_Linear);
        }
    }

    bool TryParseTableRowsJson(const FString& TableJson, TArray<TSharedPtr<FJsonValue>>& OutRows)
    {
        OutRows.Reset();
        if (TableJson.IsEmpty())
        {
            return false;
        }

        TSharedRef<TJsonReader<>> ArrayReader = TJsonReaderFactory<>::Create(TableJson);
        return FJsonSerializer::Deserialize(ArrayReader, OutRows);
    }

    bool TrySyncDataTableKeyFieldValue(const UDataTable* DataTable, uint8* RowData, const FString& DesiredRowName, FString& OutErrorMessage)
    {
        OutErrorMessage.Reset();

        if (!DataTable || !RowData)
        {
            OutErrorMessage = TEXT("DataTable row data is unavailable");
            return false;
        }

        const FString KeyFieldName = GetDataTableKeyFieldName(DataTable);
        if (KeyFieldName == TEXT("Name"))
        {
            return true;
        }

        const UScriptStruct* RowStruct = DataTable->GetRowStruct();
        if (!RowStruct)
        {
            OutErrorMessage = TEXT("DataTable is missing its row struct");
            return false;
        }

        if (RowStruct->FindPropertyByName(FName(*KeyFieldName)) == nullptr)
        {
            OutErrorMessage = FString::Printf(TEXT("DataTable key field '%s' is not present on row struct: %s"),
                *KeyFieldName,
                *RowStruct->GetPathName());
            return false;
        }

        TSharedPtr<FJsonObject> KeyFieldPatch = MakeShared<FJsonObject>();
        KeyFieldPatch->SetStringField(KeyFieldName, DesiredRowName);

        FText ImportFailureReason;
        const bool bImported = FJsonObjectConverter::JsonObjectToUStruct(
            KeyFieldPatch.ToSharedRef(),
            RowStruct,
            RowData,
            0,
            0,
            false,
            &ImportFailureReason);

        if (!bImported)
        {
            const FString FailureMessage = ImportFailureReason.IsEmpty()
                ? TEXT("Unknown key-field synchronization failure")
                : ImportFailureReason.ToString();
            OutErrorMessage = FString::Printf(TEXT("Failed to synchronize DataTable key field '%s': %s"),
                *KeyFieldName,
                *FailureMessage);
            return false;
        }

        return true;
    }

    struct FBehaviorTreeSummaryCounts
    {
        int32 NodeCount = 0;
        int32 CompositeCount = 0;
        int32 TaskCount = 0;
        int32 DecoratorCount = 0;
        int32 ServiceCount = 0;
        int32 MaxDepth = 0;
    };

    struct FBehaviorTreeGraphDiagnostics
    {
        TMap<const UObject*, ENodeEnabledState> EnabledStates;
        TMap<const UObject*, FString> ErrorMessages;
        int32 GraphNodeCount = 0;
    };

    struct FBehaviorTreeResolvedPath
    {
        enum class ETargetKind
        {
            None,
            Composite,
            Task,
            Decorator,
            RootDecorator,
            Service
        };

        ETargetKind Kind = ETargetKind::None;
        UBTNode* Node = nullptr;
        UBTCompositeNode* Composite = nullptr;
        UBTTaskNode* Task = nullptr;
        UBTDecorator* Decorator = nullptr;
        UBTService* Service = nullptr;
        UBTCompositeNode* ParentComposite = nullptr;
        int32 ChildIndex = INDEX_NONE;

        UObject* GetRuntimeObject() const
        {
            switch (Kind)
            {
            case ETargetKind::Composite:
                return Composite;
            case ETargetKind::Task:
                return Task;
            case ETargetKind::Decorator:
            case ETargetKind::RootDecorator:
                return Decorator;
            case ETargetKind::Service:
                return Service;
            default:
                return Node;
            }
        }
    };

    FString NormalizeBehaviorTreeToken(const FString& Token)
    {
        FString Normalized = Token.TrimStartAndEnd().ToLower();
        Normalized.ReplaceInline(TEXT("-"), TEXT("_"));
        Normalized.ReplaceInline(TEXT(" "), TEXT("_"));
        return Normalized;
    }

    FString GetBehaviorTreeCompositeKind(const UBTCompositeNode* CompositeNode)
    {
        if (!CompositeNode)
        {
            return TEXT("");
        }

        const UClass* NodeClass = CompositeNode->GetClass();
        if (NodeClass == UBTComposite_Selector::StaticClass())
        {
            return TEXT("selector");
        }

        if (NodeClass == UBTComposite_Sequence::StaticClass())
        {
            return TEXT("sequence");
        }

        if (NodeClass == UBTComposite_SimpleParallel::StaticClass())
        {
            return TEXT("simple_parallel");
        }

        return NodeClass ? NodeClass->GetName() : TEXT("");
    }

    FString GetBehaviorTreeFlowAbortModeName(const EBTFlowAbortMode::Type AbortMode)
    {
        switch (AbortMode)
        {
        case EBTFlowAbortMode::None:
            return TEXT("none");
        case EBTFlowAbortMode::LowerPriority:
            return TEXT("lower_priority");
        case EBTFlowAbortMode::Self:
            return TEXT("self");
        case EBTFlowAbortMode::Both:
            return TEXT("both");
        default:
            return TEXT("unknown");
        }
    }

    FString GetBehaviorTreeEnabledStateName(const ENodeEnabledState EnabledState)
    {
        switch (EnabledState)
        {
        case ENodeEnabledState::Enabled:
            return TEXT("enabled");
        case ENodeEnabledState::Disabled:
            return TEXT("disabled");
        case ENodeEnabledState::DevelopmentOnly:
            return TEXT("development_only");
        default:
            return TEXT("unknown");
        }
    }

    bool TryParseBehaviorTreeEnabledState(const FString& Value, ENodeEnabledState& OutEnabledState)
    {
        const FString Normalized = NormalizeBehaviorTreeToken(Value);
        if (Normalized == TEXT("enabled"))
        {
            OutEnabledState = ENodeEnabledState::Enabled;
            return true;
        }

        if (Normalized == TEXT("disabled"))
        {
            OutEnabledState = ENodeEnabledState::Disabled;
            return true;
        }

        if (Normalized == TEXT("development_only"))
        {
            OutEnabledState = ENodeEnabledState::DevelopmentOnly;
            return true;
        }

        return false;
    }

    bool TryParseBehaviorTreeFlowAbortMode(const FString& Value, EBTFlowAbortMode::Type& OutAbortMode)
    {
        const FString Normalized = NormalizeBehaviorTreeToken(Value);
        if (Normalized == TEXT("none"))
        {
            OutAbortMode = EBTFlowAbortMode::None;
            return true;
        }

        if (Normalized == TEXT("lower_priority"))
        {
            OutAbortMode = EBTFlowAbortMode::LowerPriority;
            return true;
        }

        if (Normalized == TEXT("self"))
        {
            OutAbortMode = EBTFlowAbortMode::Self;
            return true;
        }

        if (Normalized == TEXT("both"))
        {
            OutAbortMode = EBTFlowAbortMode::Both;
            return true;
        }

        return false;
    }

    bool TryGetBehaviorTreeBlackboardKeySelector(const UObject& NodeObject, const FBlackboardKeySelector*& OutSelector)
    {
        OutSelector = nullptr;

        const FStructProperty* BlackboardKeyProperty = FindFProperty<FStructProperty>(NodeObject.GetClass(), TEXT("BlackboardKey"));
        if (!BlackboardKeyProperty || BlackboardKeyProperty->Struct != FBlackboardKeySelector::StaticStruct())
        {
            return false;
        }

        OutSelector = BlackboardKeyProperty->ContainerPtrToValuePtr<FBlackboardKeySelector>(&NodeObject);
        return OutSelector != nullptr;
    }

    bool TryGetBehaviorTreeBlackboardKeySelector(UObject& NodeObject, FBlackboardKeySelector*& OutSelector)
    {
        OutSelector = nullptr;

        FStructProperty* BlackboardKeyProperty = FindFProperty<FStructProperty>(NodeObject.GetClass(), TEXT("BlackboardKey"));
        if (!BlackboardKeyProperty || BlackboardKeyProperty->Struct != FBlackboardKeySelector::StaticStruct())
        {
            return false;
        }

        OutSelector = BlackboardKeyProperty->ContainerPtrToValuePtr<FBlackboardKeySelector>(&NodeObject);
        return OutSelector != nullptr;
    }

    void CollectBehaviorTreeGraphDiagnostics(const UBehaviorTree& BehaviorTree, FBehaviorTreeGraphDiagnostics& OutDiagnostics)
    {
        const UBehaviorTreeGraph* BehaviorTreeGraph = Cast<UBehaviorTreeGraph>(BehaviorTree.BTGraph);
        if (!BehaviorTreeGraph)
        {
            return;
        }

        for (UEdGraphNode* EdGraphNode : BehaviorTreeGraph->Nodes)
        {
            const UBehaviorTreeGraphNode* GraphNode = Cast<UBehaviorTreeGraphNode>(EdGraphNode);
            if (!GraphNode)
            {
                continue;
            }

            ++OutDiagnostics.GraphNodeCount;
            if (GraphNode->NodeInstance)
            {
                OutDiagnostics.EnabledStates.Add(GraphNode->NodeInstance, GraphNode->GetDesiredEnabledState());
                if (!GraphNode->ErrorMessage.IsEmpty())
                {
                    OutDiagnostics.ErrorMessages.Add(GraphNode->NodeInstance, GraphNode->ErrorMessage);
                }
            }

            for (const UBehaviorTreeGraphNode* DecoratorNode : GraphNode->Decorators)
            {
                if (!DecoratorNode)
                {
                    continue;
                }

                ++OutDiagnostics.GraphNodeCount;
                if (DecoratorNode->NodeInstance)
                {
                    OutDiagnostics.EnabledStates.Add(DecoratorNode->NodeInstance, DecoratorNode->GetDesiredEnabledState());
                    if (!DecoratorNode->ErrorMessage.IsEmpty())
                    {
                        OutDiagnostics.ErrorMessages.Add(DecoratorNode->NodeInstance, DecoratorNode->ErrorMessage);
                    }
                }
            }

            for (const UBehaviorTreeGraphNode* ServiceNode : GraphNode->Services)
            {
                if (!ServiceNode)
                {
                    continue;
                }

                ++OutDiagnostics.GraphNodeCount;
                if (ServiceNode->NodeInstance)
                {
                    OutDiagnostics.EnabledStates.Add(ServiceNode->NodeInstance, ServiceNode->GetDesiredEnabledState());
                    if (!ServiceNode->ErrorMessage.IsEmpty())
                    {
                        OutDiagnostics.ErrorMessages.Add(ServiceNode->NodeInstance, ServiceNode->ErrorMessage);
                    }
                }
            }
        }
    }

    void AppendBehaviorTreeEnabledStateSummary(
        const UObject& NodeObject,
        const FBehaviorTreeGraphDiagnostics& Diagnostics,
        const TSharedPtr<FJsonObject>& NodeSummary)
    {
        const ENodeEnabledState* EnabledState = Diagnostics.EnabledStates.Find(&NodeObject);
        const ENodeEnabledState EffectiveState = EnabledState ? *EnabledState : ENodeEnabledState::Enabled;
        NodeSummary->SetStringField(TEXT("enabled_state"), GetBehaviorTreeEnabledStateName(EffectiveState));
        NodeSummary->SetBoolField(TEXT("is_enabled"), EffectiveState != ENodeEnabledState::Disabled);
    }

    void AppendBehaviorTreeBlackboardSelectorSummary(const UObject& NodeObject, const TSharedPtr<FJsonObject>& NodeSummary)
    {
        const FBlackboardKeySelector* BlackboardSelector = nullptr;
        const bool bHasBlackboardSelector = TryGetBehaviorTreeBlackboardKeySelector(NodeObject, BlackboardSelector) && BlackboardSelector;

        NodeSummary->SetBoolField(TEXT("has_blackboard_selector"), bHasBlackboardSelector);
        if (!bHasBlackboardSelector)
        {
            NodeSummary->SetStringField(TEXT("selected_blackboard_key_name"), TEXT(""));
            NodeSummary->SetStringField(TEXT("selected_blackboard_key_type_name"), TEXT(""));
            NodeSummary->SetStringField(TEXT("selected_blackboard_key_type_path"), TEXT(""));
            NodeSummary->SetNumberField(TEXT("selected_blackboard_key_id"), static_cast<double>(static_cast<int32>(FBlackboard::InvalidKey)));
            NodeSummary->SetBoolField(TEXT("selected_blackboard_key_needs_resolving"), false);
            NodeSummary->SetBoolField(TEXT("is_selected_blackboard_key_set"), false);
            return;
        }

        NodeSummary->SetStringField(TEXT("selected_blackboard_key_name"), BlackboardSelector->SelectedKeyName.ToString());
        NodeSummary->SetStringField(TEXT("selected_blackboard_key_type_name"), BlackboardSelector->SelectedKeyType ? BlackboardSelector->SelectedKeyType->GetName() : TEXT(""));
        NodeSummary->SetStringField(TEXT("selected_blackboard_key_type_path"), BlackboardSelector->SelectedKeyType ? BlackboardSelector->SelectedKeyType->GetPathName() : TEXT(""));
        NodeSummary->SetNumberField(TEXT("selected_blackboard_key_id"), static_cast<double>(static_cast<int32>(BlackboardSelector->GetSelectedKeyID())));
        NodeSummary->SetBoolField(TEXT("selected_blackboard_key_needs_resolving"), BlackboardSelector->NeedsResolving());
        NodeSummary->SetBoolField(TEXT("is_selected_blackboard_key_set"), BlackboardSelector->GetSelectedKeyID() != FBlackboard::InvalidKey);
    }

    FString GetBehaviorTreeDecoratorLogicOperationName(const EBTDecoratorLogic::Type Operation)
    {
        switch (Operation)
        {
        case EBTDecoratorLogic::Invalid:
            return TEXT("invalid");
        case EBTDecoratorLogic::Test:
            return TEXT("test");
        case EBTDecoratorLogic::And:
            return TEXT("and");
        case EBTDecoratorLogic::Or:
            return TEXT("or");
        case EBTDecoratorLogic::Not:
            return TEXT("not");
        default:
            return TEXT("unknown");
        }
    }

    TSharedPtr<FJsonObject> BuildBehaviorTreeNodeSummaryBase(
        const UBTNode& Node,
        const FString& TopologyPath,
        const int32 Depth,
        const FString& NodeKind,
        const FString& NodeType,
        const FBehaviorTreeGraphDiagnostics& Diagnostics)
    {
        TSharedPtr<FJsonObject> NodeObject = MakeShared<FJsonObject>();
        NodeObject->SetStringField(TEXT("topology_path"), TopologyPath);
        NodeObject->SetNumberField(TEXT("depth"), Depth);
        NodeObject->SetStringField(TEXT("node_kind"), NodeKind);
        NodeObject->SetStringField(TEXT("node_type"), NodeType);
        NodeObject->SetStringField(TEXT("node_name"), Node.GetNodeName());
        NodeObject->SetStringField(TEXT("class_name"), Node.GetClass()->GetName());
        NodeObject->SetStringField(TEXT("class_path"), Node.GetClass()->GetPathName());
        NodeObject->SetStringField(TEXT("static_description"), Node.GetStaticDescription());
        AppendBehaviorTreeEnabledStateSummary(Node, Diagnostics, NodeObject);
        AppendBehaviorTreeBlackboardSelectorSummary(Node, NodeObject);
        return NodeObject;
    }

    void AppendBehaviorTreeFlatNodeSummary(
        const UBTNode& Node,
        const FString& TopologyPath,
        const int32 Depth,
        const FString& NodeKind,
        const FString& NodeType,
        const int32 ChildCount,
        const int32 ServiceCount,
        const FBehaviorTreeGraphDiagnostics& Diagnostics,
        TArray<TSharedPtr<FJsonValue>>& OutNodes)
    {
        TSharedPtr<FJsonObject> FlatNode = BuildBehaviorTreeNodeSummaryBase(Node, TopologyPath, Depth, NodeKind, NodeType, Diagnostics);
        FlatNode->SetNumberField(TEXT("child_count"), ChildCount);
        FlatNode->SetNumberField(TEXT("service_count"), ServiceCount);
        OutNodes.Add(MakeShared<FJsonValueObject>(FlatNode));
    }

    TSharedPtr<FJsonObject> BuildBehaviorTreeServiceSummary(
        const UBTService& Service,
        const FString& AttachmentPath,
        const int32 Depth,
        const FBehaviorTreeGraphDiagnostics& Diagnostics)
    {
        return BuildBehaviorTreeNodeSummaryBase(
            Service,
            AttachmentPath,
            Depth,
            TEXT("service"),
            Service.GetClass()->GetName(),
            Diagnostics);
    }

    TSharedPtr<FJsonObject> BuildBehaviorTreeDecoratorSummary(
        const UBTDecorator& Decorator,
        const FString& AttachmentPath,
        const int32 Depth,
        const FBehaviorTreeGraphDiagnostics& Diagnostics)
    {
        TSharedPtr<FJsonObject> DecoratorObject = BuildBehaviorTreeNodeSummaryBase(
            Decorator,
            AttachmentPath,
            Depth,
            TEXT("decorator"),
            Decorator.GetClass()->GetName(),
            Diagnostics);
        DecoratorObject->SetStringField(TEXT("flow_abort_mode"), GetBehaviorTreeFlowAbortModeName(Decorator.GetFlowAbortMode()));
        DecoratorObject->SetBoolField(TEXT("is_inversed"), Decorator.IsInversed());
        return DecoratorObject;
    }

    TArray<TSharedPtr<FJsonValue>> BuildBehaviorTreeDecoratorLogicSummary(const TArray<FBTDecoratorLogic>& DecoratorOps)
    {
        TArray<TSharedPtr<FJsonValue>> LogicArray;
        for (const FBTDecoratorLogic& DecoratorOp : DecoratorOps)
        {
            TSharedPtr<FJsonObject> LogicObject = MakeShared<FJsonObject>();
            LogicObject->SetStringField(TEXT("operation"), GetBehaviorTreeDecoratorLogicOperationName(DecoratorOp.Operation));
            LogicObject->SetNumberField(TEXT("number"), static_cast<int32>(DecoratorOp.Number));
            LogicArray.Add(MakeShared<FJsonValueObject>(LogicObject));
        }

        return LogicArray;
    }

    TSharedPtr<FJsonObject> BuildBehaviorTreeTaskSummary(
        UBTTaskNode& TaskNode,
        const FString& TopologyPath,
        const int32 Depth,
        FBehaviorTreeSummaryCounts& Counts,
        const FBehaviorTreeGraphDiagnostics& Diagnostics,
        TArray<TSharedPtr<FJsonValue>>& OutNodes)
    {
        Counts.NodeCount += 1;
        Counts.TaskCount += 1;
        Counts.MaxDepth = FMath::Max(Counts.MaxDepth, Depth);

        TArray<TSharedPtr<FJsonValue>> Services;
        for (int32 ServiceIndex = 0; ServiceIndex < TaskNode.Services.Num(); ++ServiceIndex)
        {
            UBTService* Service = TaskNode.Services[ServiceIndex];
            if (!Service)
            {
                continue;
            }

            Counts.ServiceCount += 1;
            Services.Add(MakeShared<FJsonValueObject>(BuildBehaviorTreeServiceSummary(
                *Service,
                FString::Printf(TEXT("%s/services/%d"), *TopologyPath, ServiceIndex),
                Depth,
                Diagnostics)));
        }

        TSharedPtr<FJsonObject> TaskObject = BuildBehaviorTreeNodeSummaryBase(
            TaskNode,
            TopologyPath,
            Depth,
            TEXT("task"),
            TaskNode.GetClass()->GetName(),
            Diagnostics);
        TaskObject->SetArrayField(TEXT("services"), Services);
        TaskObject->SetNumberField(TEXT("service_count"), Services.Num());
        TaskObject->SetNumberField(TEXT("child_count"), 0);

        AppendBehaviorTreeFlatNodeSummary(
            TaskNode,
            TopologyPath,
            Depth,
            TEXT("task"),
            TaskNode.GetClass()->GetName(),
            0,
            Services.Num(),
            Diagnostics,
            OutNodes);

        return TaskObject;
    }

    TSharedPtr<FJsonObject> BuildBehaviorTreeCompositeSummary(
        UBTCompositeNode& CompositeNode,
        const FString& TopologyPath,
        const int32 Depth,
        FBehaviorTreeSummaryCounts& Counts,
        const FBehaviorTreeGraphDiagnostics& Diagnostics,
        TArray<TSharedPtr<FJsonValue>>& OutNodes)
    {
        Counts.NodeCount += 1;
        Counts.CompositeCount += 1;
        Counts.MaxDepth = FMath::Max(Counts.MaxDepth, Depth);

        TArray<TSharedPtr<FJsonValue>> Services;
        for (int32 ServiceIndex = 0; ServiceIndex < CompositeNode.Services.Num(); ++ServiceIndex)
        {
            UBTService* Service = CompositeNode.Services[ServiceIndex];
            if (!Service)
            {
                continue;
            }

            Counts.ServiceCount += 1;
            Services.Add(MakeShared<FJsonValueObject>(BuildBehaviorTreeServiceSummary(
                *Service,
                FString::Printf(TEXT("%s/services/%d"), *TopologyPath, ServiceIndex),
                Depth,
                Diagnostics)));
        }

        TArray<TSharedPtr<FJsonValue>> Children;
        for (int32 ChildIndex = 0; ChildIndex < CompositeNode.Children.Num(); ++ChildIndex)
        {
            const FBTCompositeChild& Child = CompositeNode.Children[ChildIndex];
            TSharedPtr<FJsonObject> ChildObject = MakeShared<FJsonObject>();
            const FString ChildTopologyPath = FString::Printf(TEXT("%s/%d"), *TopologyPath, ChildIndex);

            TArray<TSharedPtr<FJsonValue>> Decorators;
            for (int32 DecoratorIndex = 0; DecoratorIndex < Child.Decorators.Num(); ++DecoratorIndex)
            {
                UBTDecorator* Decorator = Child.Decorators[DecoratorIndex];
                if (!Decorator)
                {
                    continue;
                }

                Counts.DecoratorCount += 1;
                Decorators.Add(MakeShared<FJsonValueObject>(BuildBehaviorTreeDecoratorSummary(
                    *Decorator,
                    FString::Printf(TEXT("%s/decorators/%d"), *ChildTopologyPath, DecoratorIndex),
                    Depth + 1,
                    Diagnostics)));
            }

            ChildObject->SetNumberField(TEXT("child_index"), ChildIndex);
            ChildObject->SetArrayField(TEXT("decorators"), Decorators);
            ChildObject->SetNumberField(TEXT("decorator_count"), Decorators.Num());
            ChildObject->SetArrayField(TEXT("decorator_logic"), BuildBehaviorTreeDecoratorLogicSummary(Child.DecoratorOps));
            ChildObject->SetStringField(TEXT("child_topology_path"), ChildTopologyPath);

            if (Child.ChildComposite)
            {
                ChildObject->SetStringField(TEXT("child_kind"), TEXT("composite"));
                ChildObject->SetObjectField(TEXT("node"), BuildBehaviorTreeCompositeSummary(*Child.ChildComposite, ChildTopologyPath, Depth + 1, Counts, Diagnostics, OutNodes));
            }
            else if (Child.ChildTask)
            {
                ChildObject->SetStringField(TEXT("child_kind"), TEXT("task"));
                ChildObject->SetObjectField(TEXT("node"), BuildBehaviorTreeTaskSummary(*Child.ChildTask, ChildTopologyPath, Depth + 1, Counts, Diagnostics, OutNodes));
            }
            else
            {
                ChildObject->SetStringField(TEXT("child_kind"), TEXT("none"));
            }

            Children.Add(MakeShared<FJsonValueObject>(ChildObject));
        }

        const FString CompositeKind = GetBehaviorTreeCompositeKind(&CompositeNode);
        TSharedPtr<FJsonObject> CompositeObject = BuildBehaviorTreeNodeSummaryBase(
            CompositeNode,
            TopologyPath,
            Depth,
            TEXT("composite"),
            CompositeKind,
            Diagnostics);
        CompositeObject->SetBoolField(TEXT("apply_decorator_scope"), CompositeNode.IsApplyingDecoratorScope());
        CompositeObject->SetArrayField(TEXT("services"), Services);
        CompositeObject->SetNumberField(TEXT("service_count"), Services.Num());
        CompositeObject->SetArrayField(TEXT("children"), Children);
        CompositeObject->SetNumberField(TEXT("child_count"), Children.Num());

        AppendBehaviorTreeFlatNodeSummary(
            CompositeNode,
            TopologyPath,
            Depth,
            TEXT("composite"),
            CompositeKind,
            Children.Num(),
            Services.Num(),
            Diagnostics,
            OutNodes);

        return CompositeObject;
    }

    TSharedPtr<FJsonObject> BuildBehaviorTreeContentSummary(UBehaviorTree& BehaviorTree, const FString& AssetPath)
    {
        FBehaviorTreeSummaryCounts Counts;
        FBehaviorTreeGraphDiagnostics Diagnostics;
        CollectBehaviorTreeGraphDiagnostics(BehaviorTree, Diagnostics);

        TArray<TSharedPtr<FJsonValue>> RootDecorators;
        for (int32 DecoratorIndex = 0; DecoratorIndex < BehaviorTree.RootDecorators.Num(); ++DecoratorIndex)
        {
            UBTDecorator* Decorator = BehaviorTree.RootDecorators[DecoratorIndex];
            if (!Decorator)
            {
                continue;
            }

            Counts.DecoratorCount += 1;
            RootDecorators.Add(MakeShared<FJsonValueObject>(BuildBehaviorTreeDecoratorSummary(
                *Decorator,
                FString::Printf(TEXT("root/root_decorators/%d"), DecoratorIndex),
                0,
                Diagnostics)));
        }

        TArray<TSharedPtr<FJsonValue>> Nodes;
        TSharedPtr<FJsonObject> RootNodeSummary;
        FString RootNodeType;
        if (BehaviorTree.RootNode)
        {
            RootNodeType = GetBehaviorTreeCompositeKind(BehaviorTree.RootNode);
            RootNodeSummary = BuildBehaviorTreeCompositeSummary(*BehaviorTree.RootNode, TEXT("root"), 0, Counts, Diagnostics, Nodes);
        }

        TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
        Result->SetStringField(TEXT("asset_path"), AssetPath);
        Result->SetStringField(TEXT("behavior_tree_name"), BehaviorTree.GetName());
        Result->SetStringField(TEXT("class_name"), BehaviorTree.GetClass()->GetName());
        Result->SetStringField(TEXT("class_path"), BehaviorTree.GetClass()->GetPathName());
        Result->SetBoolField(TEXT("has_blackboard"), BehaviorTree.BlackboardAsset != nullptr);
        Result->SetStringField(TEXT("blackboard_name"), GetObjectNameOrEmpty(BehaviorTree.BlackboardAsset));
        Result->SetStringField(TEXT("blackboard_path"), GetObjectPathOrEmpty(BehaviorTree.BlackboardAsset));
        Result->SetBoolField(TEXT("has_graph"), Cast<UBehaviorTreeGraph>(BehaviorTree.BTGraph) != nullptr);
        Result->SetNumberField(TEXT("graph_node_count"), Diagnostics.GraphNodeCount);
        Result->SetBoolField(TEXT("has_root_node"), BehaviorTree.RootNode != nullptr);
        Result->SetStringField(TEXT("root_node_type"), RootNodeType);
        Result->SetStringField(TEXT("root_node_class_name"), BehaviorTree.RootNode ? BehaviorTree.RootNode->GetClass()->GetName() : TEXT(""));
        Result->SetStringField(TEXT("root_node_class_path"), BehaviorTree.RootNode ? BehaviorTree.RootNode->GetClass()->GetPathName() : TEXT(""));
        if (RootNodeSummary.IsValid())
        {
            Result->SetObjectField(TEXT("root_node"), RootNodeSummary);
        }
        Result->SetArrayField(TEXT("root_decorators"), RootDecorators);
        Result->SetArrayField(TEXT("root_decorator_logic"), BuildBehaviorTreeDecoratorLogicSummary(BehaviorTree.RootDecoratorOps));
        Result->SetNumberField(TEXT("root_decorator_count"), RootDecorators.Num());
        Result->SetArrayField(TEXT("nodes"), Nodes);
        Result->SetNumberField(TEXT("node_count"), Counts.NodeCount);
        Result->SetNumberField(TEXT("composite_count"), Counts.CompositeCount);
        Result->SetNumberField(TEXT("task_count"), Counts.TaskCount);
        Result->SetNumberField(TEXT("decorator_count"), Counts.DecoratorCount);
        Result->SetNumberField(TEXT("service_count"), Counts.ServiceCount);
        Result->SetNumberField(TEXT("max_depth"), Counts.MaxDepth);
        Result->SetBoolField(TEXT("is_valid"), BehaviorTree.RootNode != nullptr);
        return Result;
    }

    bool TryParseBehaviorTreePathIndex(const FString& Segment, int32& OutIndex)
    {
        if (!LexTryParseString(OutIndex, *Segment))
        {
            return false;
        }

        return OutIndex >= 0;
    }

    bool TryResolveBehaviorTreeTopologyPath(
        UBehaviorTree& BehaviorTree,
        const FString& TopologyPath,
        FBehaviorTreeResolvedPath& OutResolvedPath,
        FString& OutError)
    {
        OutResolvedPath = FBehaviorTreeResolvedPath();

        TArray<FString> Segments;
        TopologyPath.ParseIntoArray(Segments, TEXT("/"), true);
        if (Segments.Num() == 0 || Segments[0] != TEXT("root"))
        {
            OutError = FString::Printf(TEXT("Invalid Behavior Tree topology path: %s"), *TopologyPath);
            return false;
        }

        UBTCompositeNode* CurrentComposite = BehaviorTree.RootNode;
        UBTNode* CurrentNode = CurrentComposite;
        UBTCompositeNode* LastParentComposite = nullptr;
        int32 LastChildIndex = INDEX_NONE;

        if (!CurrentComposite)
        {
            OutError = TEXT("Behavior Tree has no root node");
            return false;
        }

        if (Segments.Num() == 1)
        {
            OutResolvedPath.Kind = FBehaviorTreeResolvedPath::ETargetKind::Composite;
            OutResolvedPath.Node = CurrentComposite;
            OutResolvedPath.Composite = CurrentComposite;
            return true;
        }

        for (int32 SegmentIndex = 1; SegmentIndex < Segments.Num(); ++SegmentIndex)
        {
            const FString& Segment = Segments[SegmentIndex];
            if (Segment == TEXT("root_decorators"))
            {
                if (SegmentIndex + 1 >= Segments.Num())
                {
                    OutError = FString::Printf(TEXT("Missing root decorator index in path: %s"), *TopologyPath);
                    return false;
                }

                int32 DecoratorIndex = INDEX_NONE;
                if (!TryParseBehaviorTreePathIndex(Segments[++SegmentIndex], DecoratorIndex) || !BehaviorTree.RootDecorators.IsValidIndex(DecoratorIndex) || !BehaviorTree.RootDecorators[DecoratorIndex])
                {
                    OutError = FString::Printf(TEXT("Invalid root decorator path: %s"), *TopologyPath);
                    return false;
                }

                OutResolvedPath.Kind = FBehaviorTreeResolvedPath::ETargetKind::RootDecorator;
                OutResolvedPath.Decorator = BehaviorTree.RootDecorators[DecoratorIndex];
                return true;
            }

            if (Segment == TEXT("services"))
            {
                if (SegmentIndex + 1 >= Segments.Num())
                {
                    OutError = FString::Printf(TEXT("Missing service index in path: %s"), *TopologyPath);
                    return false;
                }

                int32 ServiceIndex = INDEX_NONE;
                if (!TryParseBehaviorTreePathIndex(Segments[++SegmentIndex], ServiceIndex))
                {
                    OutError = FString::Printf(TEXT("Invalid service index in path: %s"), *TopologyPath);
                    return false;
                }

                UBTService* ResolvedService = nullptr;
                if (UBTCompositeNode* CompositeNode = Cast<UBTCompositeNode>(CurrentNode))
                {
                    if (CompositeNode->Services.IsValidIndex(ServiceIndex))
                    {
                        ResolvedService = CompositeNode->Services[ServiceIndex];
                    }
                }
                else if (UBTTaskNode* TaskNode = Cast<UBTTaskNode>(CurrentNode))
                {
                    if (TaskNode->Services.IsValidIndex(ServiceIndex))
                    {
                        ResolvedService = TaskNode->Services[ServiceIndex];
                    }
                }

                if (!ResolvedService)
                {
                    OutError = FString::Printf(TEXT("Invalid service path: %s"), *TopologyPath);
                    return false;
                }

                OutResolvedPath.Kind = FBehaviorTreeResolvedPath::ETargetKind::Service;
                OutResolvedPath.Service = ResolvedService;
                return true;
            }

            if (Segment == TEXT("decorators"))
            {
                if (!LastParentComposite || !LastParentComposite->Children.IsValidIndex(LastChildIndex))
                {
                    OutError = FString::Printf(TEXT("Decorator path does not target a valid child edge: %s"), *TopologyPath);
                    return false;
                }

                if (SegmentIndex + 1 >= Segments.Num())
                {
                    OutError = FString::Printf(TEXT("Missing decorator index in path: %s"), *TopologyPath);
                    return false;
                }

                int32 DecoratorIndex = INDEX_NONE;
                if (!TryParseBehaviorTreePathIndex(Segments[++SegmentIndex], DecoratorIndex) ||
                    !LastParentComposite->Children[LastChildIndex].Decorators.IsValidIndex(DecoratorIndex) ||
                    !LastParentComposite->Children[LastChildIndex].Decorators[DecoratorIndex])
                {
                    OutError = FString::Printf(TEXT("Invalid decorator path: %s"), *TopologyPath);
                    return false;
                }

                OutResolvedPath.Kind = FBehaviorTreeResolvedPath::ETargetKind::Decorator;
                OutResolvedPath.Decorator = LastParentComposite->Children[LastChildIndex].Decorators[DecoratorIndex];
                OutResolvedPath.ParentComposite = LastParentComposite;
                OutResolvedPath.ChildIndex = LastChildIndex;
                return true;
            }

            int32 ChildIndex = INDEX_NONE;
            if (!TryParseBehaviorTreePathIndex(Segment, ChildIndex))
            {
                OutError = FString::Printf(TEXT("Unexpected path segment '%s' in path: %s"), *Segment, *TopologyPath);
                return false;
            }

            if (!CurrentComposite || !CurrentComposite->Children.IsValidIndex(ChildIndex))
            {
                OutError = FString::Printf(TEXT("Child index out of range in path: %s"), *TopologyPath);
                return false;
            }

            UBTNode* ChildNode = CurrentComposite->GetChildNode(ChildIndex);
            if (!ChildNode)
            {
                OutError = FString::Printf(TEXT("Child node is missing in path: %s"), *TopologyPath);
                return false;
            }

            LastParentComposite = CurrentComposite;
            LastChildIndex = ChildIndex;
            CurrentNode = ChildNode;
            CurrentComposite = Cast<UBTCompositeNode>(ChildNode);
        }

        if (UBTCompositeNode* CompositeNode = Cast<UBTCompositeNode>(CurrentNode))
        {
            OutResolvedPath.Kind = FBehaviorTreeResolvedPath::ETargetKind::Composite;
            OutResolvedPath.Node = CompositeNode;
            OutResolvedPath.Composite = CompositeNode;
            OutResolvedPath.ParentComposite = LastParentComposite;
            OutResolvedPath.ChildIndex = LastChildIndex;
            return true;
        }

        if (UBTTaskNode* TaskNode = Cast<UBTTaskNode>(CurrentNode))
        {
            OutResolvedPath.Kind = FBehaviorTreeResolvedPath::ETargetKind::Task;
            OutResolvedPath.Node = TaskNode;
            OutResolvedPath.Task = TaskNode;
            OutResolvedPath.ParentComposite = LastParentComposite;
            OutResolvedPath.ChildIndex = LastChildIndex;
            return true;
        }

        OutError = FString::Printf(TEXT("Unsupported node type at path: %s"), *TopologyPath);
        return false;
    }

    UBehaviorTreeGraphNode_Root* FindBehaviorTreeGraphRootNode(UBehaviorTreeGraph& BehaviorTreeGraph)
    {
        for (UEdGraphNode* GraphNode : BehaviorTreeGraph.Nodes)
        {
            if (UBehaviorTreeGraphNode_Root* RootNode = Cast<UBehaviorTreeGraphNode_Root>(GraphNode))
            {
                return RootNode;
            }
        }

        return nullptr;
    }

    UBehaviorTreeGraphNode* FindBehaviorTreeGraphNodeByRuntimeObject(
        UBehaviorTreeGraph& BehaviorTreeGraph,
        const UObject& RuntimeObject,
        UBehaviorTreeGraphNode*& OutOwnerNode)
    {
        OutOwnerNode = nullptr;

        for (UEdGraphNode* EdGraphNode : BehaviorTreeGraph.Nodes)
        {
            UBehaviorTreeGraphNode* GraphNode = Cast<UBehaviorTreeGraphNode>(EdGraphNode);
            if (!GraphNode)
            {
                continue;
            }

            if (GraphNode->NodeInstance == &RuntimeObject)
            {
                OutOwnerNode = GraphNode;
                return GraphNode;
            }

            for (UBehaviorTreeGraphNode* DecoratorNode : GraphNode->Decorators)
            {
                if (DecoratorNode && DecoratorNode->NodeInstance == &RuntimeObject)
                {
                    OutOwnerNode = GraphNode;
                    return DecoratorNode;
                }
            }

            for (UBehaviorTreeGraphNode* ServiceNode : GraphNode->Services)
            {
                if (ServiceNode && ServiceNode->NodeInstance == &RuntimeObject)
                {
                    OutOwnerNode = GraphNode;
                    return ServiceNode;
                }
            }
        }

        return nullptr;
    }

    bool EnsureBehaviorTreeGraphForMutation(UBehaviorTree& BehaviorTree, UBehaviorTreeGraph*& OutGraph, FString& OutError)
    {
        OutGraph = Cast<UBehaviorTreeGraph>(BehaviorTree.BTGraph);
        if (!OutGraph)
        {
            BehaviorTree.BTGraph = FBlueprintEditorUtils::CreateNewGraph(
                &BehaviorTree,
                TEXT("BehaviorTreeGraph"),
                UBehaviorTreeGraph::StaticClass(),
                UEdGraphSchema_BehaviorTree::StaticClass());
            OutGraph = Cast<UBehaviorTreeGraph>(BehaviorTree.BTGraph);
            if (!OutGraph)
            {
                OutError = TEXT("Failed to create Behavior Tree editor graph");
                return false;
            }

            const UEdGraphSchema* GraphSchema = OutGraph->GetSchema();
            if (!GraphSchema)
            {
                OutError = TEXT("Behavior Tree editor graph schema was not available");
                return false;
            }

            GraphSchema->CreateDefaultNodesForGraph(*OutGraph);
            if (UBehaviorTreeGraphNode_Root* RootGraphNode = FindBehaviorTreeGraphRootNode(*OutGraph))
            {
                RootGraphNode->BlackboardAsset = BehaviorTree.BlackboardAsset;
            }

            OutGraph->OnCreated();
        }
        else
        {
            OutGraph->OnLoaded();
        }

        if (UBehaviorTreeGraphNode_Root* RootGraphNode = FindBehaviorTreeGraphRootNode(*OutGraph))
        {
            RootGraphNode->BlackboardAsset = BehaviorTree.BlackboardAsset;
            RootGraphNode->UpdateBlackboard();
        }

        OutGraph->Initialize();
        return true;
    }

    bool TryResolveBehaviorTreeGraphNodeForPath(
        UBehaviorTree& BehaviorTree,
        UBehaviorTreeGraph& BehaviorTreeGraph,
        const FString& TopologyPath,
        FBehaviorTreeResolvedPath& OutResolvedPath,
        UBehaviorTreeGraphNode*& OutGraphNode,
        UBehaviorTreeGraphNode*& OutOwnerNode,
        FString& OutError)
    {
        if (!TryResolveBehaviorTreeTopologyPath(BehaviorTree, TopologyPath, OutResolvedPath, OutError))
        {
            return false;
        }

        UObject* RuntimeObject = OutResolvedPath.GetRuntimeObject();
        if (!RuntimeObject)
        {
            OutError = FString::Printf(TEXT("No runtime node resolved for path: %s"), *TopologyPath);
            return false;
        }

        OutGraphNode = FindBehaviorTreeGraphNodeByRuntimeObject(BehaviorTreeGraph, *RuntimeObject, OutOwnerNode);
        if (!OutGraphNode)
        {
            OutError = FString::Printf(TEXT("Failed to resolve Behavior Tree graph node for path: %s"), *TopologyPath);
            return false;
        }

        return true;
    }

    bool TryResolveSupportedBehaviorTreeCompositeType(const FString& RequestedType, UClass*& OutRuntimeClass, FString& OutCanonicalType)
    {
        const FString Normalized = NormalizeBehaviorTreeToken(RequestedType);
        if (Normalized == TEXT("selector") || Normalized == TEXT("btcomposite_selector"))
        {
            OutRuntimeClass = UBTComposite_Selector::StaticClass();
            OutCanonicalType = TEXT("selector");
            return true;
        }

        if (Normalized == TEXT("sequence") || Normalized == TEXT("btcomposite_sequence"))
        {
            OutRuntimeClass = UBTComposite_Sequence::StaticClass();
            OutCanonicalType = TEXT("sequence");
            return true;
        }

        if (Normalized == TEXT("simple_parallel") || Normalized == TEXT("btcomposite_simpleparallel"))
        {
            OutRuntimeClass = UBTComposite_SimpleParallel::StaticClass();
            OutCanonicalType = TEXT("simple_parallel");
            return true;
        }

        return false;
    }

    bool TryResolveSupportedBehaviorTreeTaskType(const FString& RequestedType, UClass*& OutRuntimeClass, FString& OutCanonicalType)
    {
        const FString Normalized = NormalizeBehaviorTreeToken(RequestedType);
        if (Normalized == TEXT("wait") || Normalized == TEXT("bttask_wait"))
        {
            OutRuntimeClass = UBTTask_Wait::StaticClass();
            OutCanonicalType = TEXT("wait");
            return true;
        }

        if (Normalized == TEXT("move_to") || Normalized == TEXT("bttask_moveto"))
        {
            OutRuntimeClass = UBTTask_MoveTo::StaticClass();
            OutCanonicalType = TEXT("move_to");
            return true;
        }

        if (Normalized == TEXT("rotate_to_face_bb_entry") || Normalized == TEXT("bttask_rotatetofacebbentry"))
        {
            OutRuntimeClass = UBTTask_RotateToFaceBBEntry::StaticClass();
            OutCanonicalType = TEXT("rotate_to_face_bb_entry");
            return true;
        }

        return false;
    }

    bool TryResolveSupportedBehaviorTreeDecoratorType(const FString& RequestedType, UClass*& OutRuntimeClass, FString& OutCanonicalType)
    {
        const FString Normalized = NormalizeBehaviorTreeToken(RequestedType);
        if (Normalized == TEXT("blackboard") || Normalized == TEXT("btdecorator_blackboard"))
        {
            OutRuntimeClass = UBTDecorator_Blackboard::StaticClass();
            OutCanonicalType = TEXT("blackboard");
            return true;
        }

        return false;
    }

    bool TryResolveSupportedBehaviorTreeServiceType(const FString& RequestedType, UClass*& OutRuntimeClass, FString& OutCanonicalType)
    {
        const FString Normalized = NormalizeBehaviorTreeToken(RequestedType);
        if (Normalized == TEXT("default_focus") || Normalized == TEXT("btservice_defaultfocus"))
        {
            OutRuntimeClass = UBTService_DefaultFocus::StaticClass();
            OutCanonicalType = TEXT("default_focus");
            return true;
        }

        return false;
    }

    template <typename GraphNodeType>
    GraphNodeType* CreateBehaviorTreeGraphChildNode(UBehaviorTreeGraph& BehaviorTreeGraph, UClass* RuntimeClass)
    {
        FGraphNodeCreator<GraphNodeType> NodeBuilder(BehaviorTreeGraph);
        GraphNodeType* NewGraphNode = NodeBuilder.CreateNode();
        NewGraphNode->ClassData = FGraphNodeClassData(RuntimeClass, FString());
        NodeBuilder.Finalize();
        return NewGraphNode;
    }

    UBehaviorTreeGraphNode_Decorator* CreateBehaviorTreeDecoratorSubNode(UBehaviorTreeGraph& BehaviorTreeGraph, UClass* RuntimeClass)
    {
        UBehaviorTreeGraphNode_Decorator* DecoratorNode = NewObject<UBehaviorTreeGraphNode_Decorator>(&BehaviorTreeGraph, UBehaviorTreeGraphNode_Decorator::StaticClass(), NAME_None, RF_Transactional);
        DecoratorNode->ClassData = FGraphNodeClassData(RuntimeClass, FString());
        return DecoratorNode;
    }

    UBehaviorTreeGraphNode_Service* CreateBehaviorTreeServiceSubNode(UBehaviorTreeGraph& BehaviorTreeGraph, UClass* RuntimeClass)
    {
        UBehaviorTreeGraphNode_Service* ServiceNode = NewObject<UBehaviorTreeGraphNode_Service>(&BehaviorTreeGraph, UBehaviorTreeGraphNode_Service::StaticClass(), NAME_None, RF_Transactional);
        ServiceNode->ClassData = FGraphNodeClassData(RuntimeClass, FString());
        return ServiceNode;
    }

    void PositionBehaviorTreeGraphChildNode(
        const UBehaviorTreeGraphNode& ParentGraphNode,
        UBehaviorTreeGraphNode& ChildGraphNode,
        const int32 OutputPinIndex)
    {
        int32 NextX = ParentGraphNode.NodePosX + 420;
        int32 NextY = ParentGraphNode.NodePosY + 180;

        if (UEdGraphPin* ParentOutputPin = ParentGraphNode.GetOutputPin(OutputPinIndex))
        {
            for (UEdGraphPin* LinkedPin : ParentOutputPin->LinkedTo)
            {
                if (!LinkedPin)
                {
                    continue;
                }

                if (const UBehaviorTreeGraphNode* LinkedNode = Cast<UBehaviorTreeGraphNode>(LinkedPin->GetOwningNode()))
                {
                    NextX = FMath::Max(NextX, LinkedNode->NodePosX + 420);
                    NextY = FMath::Max(NextY, LinkedNode->NodePosY + 120);
                }
            }
        }

        ChildGraphNode.NodePosX = NextX;
        ChildGraphNode.NodePosY = NextY;
    }

    void AddBehaviorTreeGraphErrorIssue(
        const UObject& RuntimeObject,
        const FString& TopologyPath,
        const FBehaviorTreeGraphDiagnostics& Diagnostics,
        TArray<TSharedPtr<FJsonValue>>& Issues,
        int32& ErrorCount,
        int32& WarningCount)
    {
        const FString* ErrorMessage = Diagnostics.ErrorMessages.Find(&RuntimeObject);
        if (!ErrorMessage || ErrorMessage->IsEmpty())
        {
            return;
        }

        AddValidationIssue(
            Issues,
            ErrorCount,
            WarningCount,
            TEXT("error"),
            TEXT("graph_node_error"),
            FString::Printf(TEXT("Behavior Tree graph node at '%s' reports: %s"), *TopologyPath, **ErrorMessage),
            TopologyPath);
    }

    void AddBehaviorTreeEnabledStateIssue(
        const UObject& RuntimeObject,
        const FString& TopologyPath,
        const FBehaviorTreeGraphDiagnostics& Diagnostics,
        TArray<TSharedPtr<FJsonValue>>& Issues,
        int32& ErrorCount,
        int32& WarningCount)
    {
        const ENodeEnabledState* EnabledState = Diagnostics.EnabledStates.Find(&RuntimeObject);
        if (!EnabledState)
        {
            return;
        }

        if (*EnabledState == ENodeEnabledState::Disabled)
        {
            AddValidationIssue(
                Issues,
                ErrorCount,
                WarningCount,
                TEXT("warning"),
                TEXT("disabled_node"),
                FString::Printf(TEXT("Behavior Tree node at '%s' is disabled"), *TopologyPath),
                TopologyPath);
        }
        else if (*EnabledState == ENodeEnabledState::DevelopmentOnly)
        {
            AddValidationIssue(
                Issues,
                ErrorCount,
                WarningCount,
                TEXT("warning"),
                TEXT("development_only_node"),
                FString::Printf(TEXT("Behavior Tree node at '%s' is development-only"), *TopologyPath),
                TopologyPath);
        }
    }

    void ValidateBehaviorTreeRuntimeObject(
        const UObject& RuntimeObject,
        const FString& TopologyPath,
        const UBehaviorTree& BehaviorTree,
        const FBehaviorTreeGraphDiagnostics& Diagnostics,
        TArray<TSharedPtr<FJsonValue>>& Issues,
        int32& ErrorCount,
        int32& WarningCount)
    {
        AddBehaviorTreeGraphErrorIssue(RuntimeObject, TopologyPath, Diagnostics, Issues, ErrorCount, WarningCount);
        AddBehaviorTreeEnabledStateIssue(RuntimeObject, TopologyPath, Diagnostics, Issues, ErrorCount, WarningCount);

        const FBlackboardKeySelector* BlackboardSelector = nullptr;
        if (TryGetBehaviorTreeBlackboardKeySelector(RuntimeObject, BlackboardSelector) && BlackboardSelector)
        {
            if (!BehaviorTree.BlackboardAsset)
            {
                AddValidationIssue(
                    Issues,
                    ErrorCount,
                    WarningCount,
                    TEXT("error"),
                    TEXT("missing_blackboard_asset"),
                    FString::Printf(TEXT("Behavior Tree node at '%s' requires a Blackboard asset but the tree has none"), *TopologyPath),
                    TopologyPath);
            }
            else if (!BlackboardSelector->SelectedKeyName.IsNone())
            {
                FBlackboardKeySelector ResolvedSelector = *BlackboardSelector;
                ResolvedSelector.InvalidateResolvedKey();
                ResolvedSelector.ResolveSelectedKey(*BehaviorTree.BlackboardAsset);
                if (ResolvedSelector.GetSelectedKeyID() == FBlackboard::InvalidKey)
                {
                    AddValidationIssue(
                        Issues,
                        ErrorCount,
                        WarningCount,
                        TEXT("error"),
                        TEXT("unresolved_blackboard_key"),
                        FString::Printf(TEXT("Behavior Tree node at '%s' references Blackboard key '%s' that could not be resolved"), *TopologyPath, *BlackboardSelector->SelectedKeyName.ToString()),
                        TopologyPath);
                }
            }
        }

        if (const UBTDecorator* Decorator = Cast<UBTDecorator>(&RuntimeObject))
        {
            if (!Decorator->IsFlowAbortModeValid())
            {
                AddValidationIssue(
                    Issues,
                    ErrorCount,
                    WarningCount,
                    TEXT("error"),
                    TEXT("invalid_flow_abort_mode"),
                    FString::Printf(TEXT("Behavior Tree decorator at '%s' has an invalid flow abort mode"), *TopologyPath),
                    TopologyPath);
            }
        }
    }

    void ValidateBehaviorTreeTask(
        UBTTaskNode& TaskNode,
        const FString& TopologyPath,
        const UBehaviorTree& BehaviorTree,
        const FBehaviorTreeGraphDiagnostics& Diagnostics,
        TArray<TSharedPtr<FJsonValue>>& Issues,
        int32& ErrorCount,
        int32& WarningCount);

    void ValidateBehaviorTreeComposite(
        UBTCompositeNode& CompositeNode,
        const FString& TopologyPath,
        const UBehaviorTree& BehaviorTree,
        const FBehaviorTreeGraphDiagnostics& Diagnostics,
        TArray<TSharedPtr<FJsonValue>>& Issues,
        int32& ErrorCount,
        int32& WarningCount);

    void ValidateBehaviorTreeTask(
        UBTTaskNode& TaskNode,
        const FString& TopologyPath,
        const UBehaviorTree& BehaviorTree,
        const FBehaviorTreeGraphDiagnostics& Diagnostics,
        TArray<TSharedPtr<FJsonValue>>& Issues,
        int32& ErrorCount,
        int32& WarningCount)
    {
        ValidateBehaviorTreeRuntimeObject(TaskNode, TopologyPath, BehaviorTree, Diagnostics, Issues, ErrorCount, WarningCount);

        for (int32 ServiceIndex = 0; ServiceIndex < TaskNode.Services.Num(); ++ServiceIndex)
        {
            UBTService* Service = TaskNode.Services[ServiceIndex];
            if (!Service)
            {
                continue;
            }

            ValidateBehaviorTreeRuntimeObject(
                *Service,
                FString::Printf(TEXT("%s/services/%d"), *TopologyPath, ServiceIndex),
                BehaviorTree,
                Diagnostics,
                Issues,
                ErrorCount,
                WarningCount);
        }
    }

    void ValidateBehaviorTreeComposite(
        UBTCompositeNode& CompositeNode,
        const FString& TopologyPath,
        const UBehaviorTree& BehaviorTree,
        const FBehaviorTreeGraphDiagnostics& Diagnostics,
        TArray<TSharedPtr<FJsonValue>>& Issues,
        int32& ErrorCount,
        int32& WarningCount)
    {
        ValidateBehaviorTreeRuntimeObject(CompositeNode, TopologyPath, BehaviorTree, Diagnostics, Issues, ErrorCount, WarningCount);

        if (CompositeNode.Children.Num() == 0)
        {
            AddValidationIssue(
                Issues,
                ErrorCount,
                WarningCount,
                TEXT("warning"),
                TEXT("empty_composite"),
                FString::Printf(TEXT("Composite node at '%s' has no children"), *TopologyPath),
                TopologyPath);
        }

        if (CompositeNode.GetClass() == UBTComposite_SimpleParallel::StaticClass() && CompositeNode.Children.Num() < 2)
        {
            AddValidationIssue(
                Issues,
                ErrorCount,
                WarningCount,
                TEXT("warning"),
                TEXT("simple_parallel_incomplete"),
                FString::Printf(TEXT("Simple Parallel node at '%s' does not have both main and background branches"), *TopologyPath),
                TopologyPath);
        }

        for (int32 ServiceIndex = 0; ServiceIndex < CompositeNode.Services.Num(); ++ServiceIndex)
        {
            UBTService* Service = CompositeNode.Services[ServiceIndex];
            if (!Service)
            {
                continue;
            }

            ValidateBehaviorTreeRuntimeObject(
                *Service,
                FString::Printf(TEXT("%s/services/%d"), *TopologyPath, ServiceIndex),
                BehaviorTree,
                Diagnostics,
                Issues,
                ErrorCount,
                WarningCount);
        }

        for (int32 ChildIndex = 0; ChildIndex < CompositeNode.Children.Num(); ++ChildIndex)
        {
            const FBTCompositeChild& Child = CompositeNode.Children[ChildIndex];
            const FString ChildTopologyPath = FString::Printf(TEXT("%s/%d"), *TopologyPath, ChildIndex);

            for (int32 DecoratorIndex = 0; DecoratorIndex < Child.Decorators.Num(); ++DecoratorIndex)
            {
                UBTDecorator* Decorator = Child.Decorators[DecoratorIndex];
                if (!Decorator)
                {
                    continue;
                }

                ValidateBehaviorTreeRuntimeObject(
                    *Decorator,
                    FString::Printf(TEXT("%s/decorators/%d"), *ChildTopologyPath, DecoratorIndex),
                    BehaviorTree,
                    Diagnostics,
                    Issues,
                    ErrorCount,
                    WarningCount);
            }

            if (Child.ChildComposite)
            {
                ValidateBehaviorTreeComposite(*Child.ChildComposite, ChildTopologyPath, BehaviorTree, Diagnostics, Issues, ErrorCount, WarningCount);
            }
            else if (Child.ChildTask)
            {
                ValidateBehaviorTreeTask(*Child.ChildTask, ChildTopologyPath, BehaviorTree, Diagnostics, Issues, ErrorCount, WarningCount);
            }
            else
            {
                AddValidationIssue(
                    Issues,
                    ErrorCount,
                    WarningCount,
                    TEXT("error"),
                    TEXT("missing_child_node"),
                    FString::Printf(TEXT("Composite child slot '%s' has no task or composite node"), *ChildTopologyPath),
                    ChildTopologyPath);
            }
        }
    }
}

TSharedPtr<FJsonObject> FUnrealAIAssetCommands::HandleCommand(const FString& CommandType, const TSharedPtr<FJsonObject>& Params)
{
    if (CommandType == TEXT("find_assets"))
    {
        return HandleFindAssets(Params);
    }
    if (CommandType == TEXT("read_pcg_graph_content"))
    {
        return HandleReadPCGGraphContent(Params);
    }
    if (CommandType == TEXT("create_pcg_graph_asset"))
    {
        return HandleCreatePCGGraphAsset(Params);
    }
    if (CommandType == TEXT("create_pcg_graph_instance"))
    {
        return HandleCreatePCGGraphInstance(Params);
    }
    if (CommandType == TEXT("read_pcg_component_content"))
    {
        return HandleReadPCGComponentContent(Params);
    }
    if (CommandType == TEXT("add_pcg_component_to_actor"))
    {
        return HandleAddPCGComponentToActor(Params);
    }
    if (CommandType == TEXT("create_pcg_volume"))
    {
        return HandleCreatePCGVolume(Params);
    }
    if (CommandType == TEXT("list_pcg_node_types"))
    {
        return HandleListPCGNodeTypes(Params);
    }
    if (CommandType == TEXT("read_pcg_graph_nodes"))
    {
        return HandleReadPCGGraphNodes(Params);
    }
    if (CommandType == TEXT("read_pcg_graph_node"))
    {
        return HandleReadPCGGraphNode(Params);
    }
    if (CommandType == TEXT("add_pcg_graph_node"))
    {
        return HandleAddPCGGraphNode(Params);
    }
    if (CommandType == TEXT("delete_pcg_graph_node"))
    {
        return HandleDeletePCGGraphNode(Params);
    }
    if (CommandType == TEXT("connect_pcg_graph_nodes"))
    {
        return HandleConnectPCGGraphNodes(Params);
    }
    if (CommandType == TEXT("disconnect_pcg_graph_nodes"))
    {
        return HandleDisconnectPCGGraphNodes(Params);
    }
    if (CommandType == TEXT("set_pcg_graph_node_position"))
    {
        return HandleSetPCGGraphNodePosition(Params);
    }
    if (CommandType == TEXT("set_pcg_subgraph_node_asset"))
    {
        return HandleSetPCGSubgraphNodeAsset(Params);
    }
    if (CommandType == TEXT("add_pcg_graph_comment"))
    {
        return HandleAddPCGGraphComment(Params);
    }
    if (CommandType == TEXT("update_pcg_graph_comment"))
    {
        return HandleUpdatePCGGraphComment(Params);
    }
    if (CommandType == TEXT("delete_pcg_graph_comment"))
    {
        return HandleDeletePCGGraphComment(Params);
    }
    if (CommandType == TEXT("add_pcg_graph_reroute"))
    {
        return HandleAddPCGGraphReroute(Params);
    }
    if (CommandType == TEXT("update_pcg_graph_node_settings"))
    {
        return HandleUpdatePCGGraphNodeSettings(Params);
    }
    if (CommandType == TEXT("set_pcg_graph_node_state"))
    {
        return HandleSetPCGGraphNodeState(Params);
    }
    if (CommandType == TEXT("create_pcg_graph_parameter"))
    {
        return HandleCreatePCGGraphParameter(Params);
    }
    if (CommandType == TEXT("delete_pcg_graph_parameter"))
    {
        return HandleDeletePCGGraphParameter(Params);
    }
    if (CommandType == TEXT("rename_pcg_graph_parameter"))
    {
        return HandleRenamePCGGraphParameter(Params);
    }
    if (CommandType == TEXT("set_pcg_graph_parameter"))
    {
        return HandleSetPCGGraphParameter(Params);
    }
    if (CommandType == TEXT("reset_pcg_graph_parameter_override"))
    {
        return HandleResetPCGGraphParameterOverride(Params);
    }
    if (CommandType == TEXT("read_behavior_tree_content"))
    {
        return HandleReadBehaviorTreeContent(Params);
    }
    if (CommandType == TEXT("create_behavior_tree_asset"))
    {
        return HandleCreateBehaviorTreeAsset(Params);
    }
    if (CommandType == TEXT("update_behavior_tree_subtree"))
    {
        return HandleUpdateBehaviorTreeSubtree(Params);
    }
    if (CommandType == TEXT("set_behavior_tree_node_properties"))
    {
        return HandleSetBehaviorTreeNodeProperties(Params);
    }
    if (CommandType == TEXT("validate_behavior_tree"))
    {
        return HandleValidateBehaviorTree(Params);
    }
    if (CommandType == TEXT("read_blackboard_content"))
    {
        return HandleReadBlackboardContent(Params);
    }
    if (CommandType == TEXT("create_blackboard_asset"))
    {
        return HandleCreateBlackboardAsset(Params);
    }
    if (CommandType == TEXT("update_blackboard_keys"))
    {
        return HandleUpdateBlackboardKeys(Params);
    }
    if (CommandType == TEXT("read_niagara_system_content"))
    {
        return HandleReadNiagaraSystemContent(Params);
    }
    if (CommandType == TEXT("create_niagara_system_asset"))
    {
        return HandleCreateNiagaraSystemAsset(Params);
    }
    if (CommandType == TEXT("set_niagara_system_user_parameters"))
    {
        return HandleSetNiagaraSystemUserParameters(Params);
    }
    if (CommandType == TEXT("validate_niagara_system"))
    {
        return HandleValidateNiagaraSystem(Params);
    }
    if (CommandType == TEXT("read_niagara_system_emitter"))
    {
        return HandleReadNiagaraSystemEmitter(Params);
    }
    if (CommandType == TEXT("add_niagara_emitter_to_system"))
    {
        return HandleAddNiagaraEmitterToSystem(Params);
    }
    if (CommandType == TEXT("duplicate_niagara_system_emitter"))
    {
        return HandleDuplicateNiagaraSystemEmitter(Params);
    }
    if (CommandType == TEXT("rename_niagara_system_emitter"))
    {
        return HandleRenameNiagaraSystemEmitter(Params);
    }
    if (CommandType == TEXT("remove_niagara_system_emitter"))
    {
        return HandleRemoveNiagaraSystemEmitter(Params);
    }
    if (CommandType == TEXT("read_anim_blueprint_content"))
    {
        return HandleReadAnimBlueprintContent(Params);
    }
    if (CommandType == TEXT("validate_anim_blueprint"))
    {
        return HandleValidateAnimBlueprint(Params);
    }
    if (CommandType == TEXT("read_anim_state_machine"))
    {
        return HandleReadAnimStateMachine(Params);
    }
    if (CommandType == TEXT("create_anim_blueprint_asset"))
    {
        return HandleCreateAnimBlueprintAsset(Params);
    }
    if (CommandType == TEXT("create_anim_state_machine"))
    {
        return HandleCreateAnimStateMachine(Params);
    }
    if (CommandType == TEXT("create_anim_state"))
    {
        return HandleCreateAnimState(Params);
    }
    if (CommandType == TEXT("rename_anim_state"))
    {
        return HandleRenameAnimState(Params);
    }
    if (CommandType == TEXT("delete_anim_state"))
    {
        return HandleDeleteAnimState(Params);
    }
    if (CommandType == TEXT("set_anim_state_sequence_player"))
    {
        return HandleSetAnimStateSequencePlayer(Params);
    }
    if (CommandType == TEXT("set_anim_state_blend_space_player"))
    {
        return HandleSetAnimStateBlendSpacePlayer(Params);
    }
    if (CommandType == TEXT("set_anim_state_asset_player_parameters"))
    {
        return HandleSetAnimStateAssetPlayerParameters(Params);
    }
    if (CommandType == TEXT("create_anim_transition"))
    {
        return HandleCreateAnimTransition(Params);
    }
    if (CommandType == TEXT("delete_anim_transition"))
    {
        return HandleDeleteAnimTransition(Params);
    }
    if (CommandType == TEXT("set_anim_transition_rule"))
    {
        return HandleSetAnimTransitionRule(Params);
    }
    if (CommandType == TEXT("read_curve_table_content"))
    {
        return HandleReadCurveTableContent(Params);
    }
    if (CommandType == TEXT("read_curve_table_row"))
    {
        return HandleReadCurveTableRow(Params);
    }
    if (CommandType == TEXT("create_curve_table_asset"))
    {
        return HandleCreateCurveTableAsset(Params);
    }
    if (CommandType == TEXT("upsert_curve_table_row"))
    {
        return HandleUpsertCurveTableRow(Params);
    }
    if (CommandType == TEXT("delete_curve_table_row"))
    {
        return HandleDeleteCurveTableRow(Params);
    }
    if (CommandType == TEXT("rename_curve_table_row"))
    {
        return HandleRenameCurveTableRow(Params);
    }
    if (CommandType == TEXT("validate_data_table_row_import"))
    {
        return HandleValidateDataTableRowImport(Params);
    }
    if (CommandType == TEXT("validate_curve_table_row_import"))
    {
        return HandleValidateCurveTableRowImport(Params);
    }
    if (CommandType == TEXT("read_data_table_content"))
    {
        return HandleReadDataTableContent(Params);
    }
    if (CommandType == TEXT("read_data_table_row"))
    {
        return HandleReadDataTableRow(Params);
    }
    if (CommandType == TEXT("upsert_data_table_row"))
    {
        return HandleUpsertDataTableRow(Params);
    }
    if (CommandType == TEXT("delete_data_table_row"))
    {
        return HandleDeleteDataTableRow(Params);
    }
    if (CommandType == TEXT("rename_data_table_row"))
    {
        return HandleRenameDataTableRow(Params);
    }
    if (CommandType == TEXT("duplicate_data_table_row"))
    {
        return HandleDuplicateDataTableRow(Params);
    }
    if (CommandType == TEXT("move_data_table_row"))
    {
        return HandleMoveDataTableRow(Params);
    }
    if (CommandType == TEXT("create_data_table_asset"))
    {
        return HandleCreateDataTableAsset(Params);
    }
    if (CommandType == TEXT("get_asset_dependencies"))
    {
        return HandleGetAssetDependencies(Params);
    }
    if (CommandType == TEXT("get_referencers"))
    {
        return HandleGetReferencers(Params);
    }
    if (CommandType == TEXT("create_material_instance"))
    {
        return HandleCreateMaterialInstance(Params);
    }
    if (CommandType == TEXT("import_asset"))
    {
        return HandleImportAsset(Params);
    }
    return FUnrealAICommonUtils::CreateErrorResponse(
        FString::Printf(TEXT("Unknown asset command: %s"), *CommandType));
}

// --------------------------------------------------------------------------- //
// find_assets
// --------------------------------------------------------------------------- //
TSharedPtr<FJsonObject> FUnrealAIAssetCommands::HandleFindAssets(const TSharedPtr<FJsonObject>& Params)
{
    FString Query;          // substring to match on AssetName (case-insensitive)
    FString ClassName;      // optional, e.g. "Material", "Blueprint", "StaticMesh"
    FString PathFilter;     // optional, e.g. "/Game/Blueprints"
    bool bRecursivePaths = true;
    int32 MaxResults = 200;

    Params->TryGetStringField(TEXT("query"), Query);
    Params->TryGetStringField(TEXT("class_name"), ClassName);
    Params->TryGetStringField(TEXT("path"), PathFilter);
    if (Params->HasField(TEXT("recursive"))) { bRecursivePaths = Params->GetBoolField(TEXT("recursive")); }
    if (Params->HasField(TEXT("max_results"))) { MaxResults = Params->GetIntegerField(TEXT("max_results")); }

    FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
    IAssetRegistry& Registry = AssetRegistryModule.Get();

    FARFilter Filter;
    Filter.bRecursivePaths = bRecursivePaths;
    Filter.bRecursiveClasses = true;
    if (!PathFilter.IsEmpty())
    {
        Filter.PackagePaths.Add(*PathFilter);
    }
    else
    {
        Filter.PackagePaths.Add(TEXT("/Game"));
    }
    if (!ClassName.IsEmpty())
    {
        // ClassPaths is the modern API on UE 5.1+
        Filter.ClassPaths.Add(FTopLevelAssetPath(TEXT("/Script/Engine"), *ClassName));
    }

    TArray<FAssetData> Assets;
    Registry.GetAssets(Filter, Assets);

    TArray<TSharedPtr<FJsonValue>> ResultArray;
    int32 Matched = 0;
    const FString QueryLower = Query.ToLower();

    for (const FAssetData& Data : Assets)
    {
        if (!QueryLower.IsEmpty())
        {
            const FString NameLower = Data.AssetName.ToString().ToLower();
            if (!NameLower.Contains(QueryLower))
            {
                continue;
            }
        }

        TSharedPtr<FJsonObject> AssetObj = MakeShared<FJsonObject>();
        AssetObj->SetStringField(TEXT("name"), Data.AssetName.ToString());
        AssetObj->SetStringField(TEXT("path"), Data.GetObjectPathString());
        AssetObj->SetStringField(TEXT("package_path"), Data.PackagePath.ToString());
        AssetObj->SetStringField(TEXT("class"), Data.AssetClassPath.ToString());
        ResultArray.Add(MakeShared<FJsonValueObject>(AssetObj));

        if (++Matched >= MaxResults)
        {
            break;
        }
    }

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetArrayField(TEXT("assets"), ResultArray);
    Result->SetNumberField(TEXT("count"), ResultArray.Num());
    Result->SetBoolField(TEXT("truncated"), Matched >= MaxResults);
    return Result;
}

// --------------------------------------------------------------------------- //
// read_pcg_graph_content
// --------------------------------------------------------------------------- //
TSharedPtr<FJsonObject> FUnrealAIAssetCommands::HandleReadPCGGraphContent(const TSharedPtr<FJsonObject>& Params)
{
    FString AssetPath;
    if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath) || AssetPath.TrimStartAndEnd().IsEmpty())
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing or empty 'asset_path' parameter"));
    }
    AssetPath = AssetPath.TrimStartAndEnd();

    UPCGGraphInterface* GraphInterface = Cast<UPCGGraphInterface>(UEditorAssetLibrary::LoadAsset(AssetPath));
    if (!GraphInterface)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("PCG graph asset not found or wrong class: %s"), *AssetPath));
    }

    TSharedPtr<FJsonObject> Result = BuildPCGGraphSummary(GraphInterface, AssetPath);
    Result->SetBoolField(TEXT("success"), true);
    return Result;
}

// --------------------------------------------------------------------------- //
// create_pcg_graph_asset
// --------------------------------------------------------------------------- //
TSharedPtr<FJsonObject> FUnrealAIAssetCommands::HandleCreatePCGGraphAsset(const TSharedPtr<FJsonObject>& Params)
{
    FString GraphName;
    if (!Params->TryGetStringField(TEXT("pcg_graph_name"), GraphName) || GraphName.TrimStartAndEnd().IsEmpty())
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing or empty 'pcg_graph_name' parameter"));
    }
    GraphName = GraphName.TrimStartAndEnd();

    FString DestinationPath = TEXT("/Game/PCG");
    Params->TryGetStringField(TEXT("destination_path"), DestinationPath);
    DestinationPath = DestinationPath.TrimStartAndEnd();
    if (DestinationPath.IsEmpty())
    {
        DestinationPath = TEXT("/Game/PCG");
    }

    FString TemplateAssetPath;
    Params->TryGetStringField(TEXT("template_asset_path"), TemplateAssetPath);
    TemplateAssetPath = TemplateAssetPath.TrimStartAndEnd();

    if (!UEditorAssetLibrary::DoesDirectoryExist(DestinationPath))
    {
        UEditorAssetLibrary::MakeDirectory(DestinationPath);
    }

    const FString FullObjectPath = BuildAssetObjectPath(DestinationPath, GraphName);
    if (UEditorAssetLibrary::DoesAssetExist(FullObjectPath))
    {
        return FUnrealAICommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Asset already exists: %s"), *FullObjectPath));
    }

    UPCGGraph* TemplateGraph = nullptr;
    if (!TemplateAssetPath.IsEmpty())
    {
        TemplateGraph = Cast<UPCGGraph>(UEditorAssetLibrary::LoadAsset(TemplateAssetPath));
        if (!TemplateGraph)
        {
            return FUnrealAICommonUtils::CreateErrorResponse(
                FString::Printf(TEXT("Failed to load PCG template graph: %s"), *TemplateAssetPath));
        }
    }

    FString FactoryError;
    UFactory* Factory = CreateFactoryByClassPath(TEXT("/Script/PCGEditor.PCGGraphFactory"), FactoryError);
    if (!Factory)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(FactoryError);
    }

    SetBoolPropertyValue(Factory, TEXT("bSkipTemplateSelection"), true);
    if (TemplateGraph)
    {
        SetObjectPropertyValue(Factory, TEXT("TemplateGraph"), TemplateGraph);
    }

    FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools"));
    IAssetTools& AssetTools = AssetToolsModule.Get();

    UObject* NewAsset = AssetTools.CreateAsset(GraphName, DestinationPath, UPCGGraph::StaticClass(), Factory);
    UPCGGraph* Graph = Cast<UPCGGraph>(NewAsset);
    if (!Graph)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Failed to create PCG graph asset"));
    }

    Graph->SetFlags(RF_Transactional);
    Graph->Modify();

    bool bIsTemplate = false;
    if (Params->TryGetBoolField(TEXT("is_template"), bIsTemplate))
    {
        Graph->bIsTemplate = bIsTemplate;
    }

    bool bExposeToLibrary = false;
    if (Params->TryGetBoolField(TEXT("expose_to_library"), bExposeToLibrary))
    {
        Graph->bExposeToLibrary = bExposeToLibrary;
    }

    bool bExposeGenerationInAssetExplorer = false;
    if (Params->TryGetBoolField(TEXT("expose_generation_in_asset_explorer"), bExposeGenerationInAssetExplorer))
    {
        Graph->bExposeGenerationInAssetExplorer = bExposeGenerationInAssetExplorer;
    }

    FString TitleOverride;
    Params->TryGetStringField(TEXT("title_override"), TitleOverride);
    TitleOverride = TitleOverride.TrimStartAndEnd();
    if (!TitleOverride.IsEmpty())
    {
        Graph->bOverrideTitle = true;
        Graph->Title = FText::FromString(TitleOverride);
    }

    TArray<double> ColorOverride;
    if (TryReadJsonNumberArrayField(Params, TEXT("color_override"), 4, ColorOverride))
    {
        Graph->bOverrideColor = true;
        Graph->Color = FLinearColor(
            static_cast<float>(ColorOverride[0]),
            static_cast<float>(ColorOverride[1]),
            static_cast<float>(ColorOverride[2]),
            static_cast<float>(ColorOverride[3]));
    }

    Graph->MarkPackageDirty();
    UEditorAssetLibrary::SaveAsset(FullObjectPath, /*bOnlyIfIsDirty=*/false);

    TSharedPtr<FJsonObject> ReadParams = MakeShared<FJsonObject>();
    ReadParams->SetStringField(TEXT("asset_path"), FullObjectPath);
    TSharedPtr<FJsonObject> Result = HandleReadPCGGraphContent(ReadParams);
    Result->SetStringField(TEXT("pcg_graph_name"), GraphName);
    Result->SetStringField(TEXT("destination_path"), DestinationPath);
    Result->SetStringField(TEXT("template_asset_path"), TemplateAssetPath);
    Result->SetBoolField(TEXT("created"), true);
    Result->SetBoolField(TEXT("success"), true);
    return Result;
}

// --------------------------------------------------------------------------- //
// create_pcg_graph_instance
// --------------------------------------------------------------------------- //
TSharedPtr<FJsonObject> FUnrealAIAssetCommands::HandleCreatePCGGraphInstance(const TSharedPtr<FJsonObject>& Params)
{
    FString InstanceName;
    if (!Params->TryGetStringField(TEXT("instance_name"), InstanceName) || InstanceName.TrimStartAndEnd().IsEmpty())
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing or empty 'instance_name' parameter"));
    }
    InstanceName = InstanceName.TrimStartAndEnd();

    FString ParentGraphPath;
    if (!Params->TryGetStringField(TEXT("parent_graph_path"), ParentGraphPath) || ParentGraphPath.TrimStartAndEnd().IsEmpty())
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing or empty 'parent_graph_path' parameter"));
    }
    ParentGraphPath = ParentGraphPath.TrimStartAndEnd();

    FString DestinationPath = TEXT("/Game/PCG");
    Params->TryGetStringField(TEXT("destination_path"), DestinationPath);
    DestinationPath = DestinationPath.TrimStartAndEnd();
    if (DestinationPath.IsEmpty())
    {
        DestinationPath = TEXT("/Game/PCG");
    }

    UPCGGraphInterface* ParentGraph = Cast<UPCGGraphInterface>(UEditorAssetLibrary::LoadAsset(ParentGraphPath));
    if (!ParentGraph)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Failed to load parent PCG graph: %s"), *ParentGraphPath));
    }

    if (!UEditorAssetLibrary::DoesDirectoryExist(DestinationPath))
    {
        UEditorAssetLibrary::MakeDirectory(DestinationPath);
    }

    const FString FullObjectPath = BuildAssetObjectPath(DestinationPath, InstanceName);
    if (UEditorAssetLibrary::DoesAssetExist(FullObjectPath))
    {
        return FUnrealAICommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Asset already exists: %s"), *FullObjectPath));
    }

    FString FactoryError;
    UFactory* Factory = CreateFactoryByClassPath(TEXT("/Script/PCGEditor.PCGGraphInstanceFactory"), FactoryError);
    if (!Factory)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(FactoryError);
    }

    SetObjectPropertyValue(Factory, TEXT("ParentGraph"), ParentGraph);

    FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools"));
    IAssetTools& AssetTools = AssetToolsModule.Get();

    UObject* NewAsset = AssetTools.CreateAsset(InstanceName, DestinationPath, UPCGGraphInstance::StaticClass(), Factory);
    UPCGGraphInstance* GraphInstance = Cast<UPCGGraphInstance>(NewAsset);
    if (!GraphInstance)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Failed to create PCG graph instance asset"));
    }

    GraphInstance->SetFlags(RF_Transactional);
    GraphInstance->Modify();
    GraphInstance->MarkPackageDirty();
    UEditorAssetLibrary::SaveAsset(FullObjectPath, /*bOnlyIfIsDirty=*/false);

    TSharedPtr<FJsonObject> ReadParams = MakeShared<FJsonObject>();
    ReadParams->SetStringField(TEXT("asset_path"), FullObjectPath);
    TSharedPtr<FJsonObject> Result = HandleReadPCGGraphContent(ReadParams);
    Result->SetStringField(TEXT("instance_name"), InstanceName);
    Result->SetStringField(TEXT("parent_graph_path"), ParentGraphPath);
    Result->SetStringField(TEXT("destination_path"), DestinationPath);
    Result->SetBoolField(TEXT("created"), true);
    Result->SetBoolField(TEXT("success"), true);
    return Result;
}

// --------------------------------------------------------------------------- //
// read_pcg_component_content
// --------------------------------------------------------------------------- //
TSharedPtr<FJsonObject> FUnrealAIAssetCommands::HandleReadPCGComponentContent(const TSharedPtr<FJsonObject>& Params)
{
    FString ActorName;
    if (!Params->TryGetStringField(TEXT("actor_name"), ActorName) || ActorName.TrimStartAndEnd().IsEmpty())
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing or empty 'actor_name' parameter"));
    }
    ActorName = ActorName.TrimStartAndEnd();

    FString ComponentName;
    Params->TryGetStringField(TEXT("component_name"), ComponentName);
    ComponentName = ComponentName.TrimStartAndEnd();

    UWorld* World = GetEditorWorld();
    AActor* Actor = FindActorByNameInWorld(World, ActorName);
    if (!Actor)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Actor not found: %s"), *ActorName));
    }

    UPCGComponent* PCGComponent = FindPCGComponentOnActor(Actor, ComponentName);
    if (!PCGComponent)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(
            ComponentName.IsEmpty()
                ? FString::Printf(TEXT("Actor '%s' does not have a PCG component"), *ActorName)
                : FString::Printf(TEXT("PCG component '%s' not found on actor '%s'"), *ComponentName, *ActorName));
    }

    TSharedPtr<FJsonObject> Result = BuildPCGComponentSummary(PCGComponent);
    Result->SetBoolField(TEXT("success"), true);
    return Result;
}

// --------------------------------------------------------------------------- //
// add_pcg_component_to_actor
// --------------------------------------------------------------------------- //
TSharedPtr<FJsonObject> FUnrealAIAssetCommands::HandleAddPCGComponentToActor(const TSharedPtr<FJsonObject>& Params)
{
    FString ActorName;
    if (!Params->TryGetStringField(TEXT("actor_name"), ActorName) || ActorName.TrimStartAndEnd().IsEmpty())
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing or empty 'actor_name' parameter"));
    }
    ActorName = ActorName.TrimStartAndEnd();

    FString ComponentName = TEXT("PCGComponent");
    Params->TryGetStringField(TEXT("component_name"), ComponentName);
    ComponentName = ComponentName.TrimStartAndEnd();
    if (ComponentName.IsEmpty())
    {
        ComponentName = TEXT("PCGComponent");
    }

    UWorld* World = GetEditorWorld();
    AActor* Actor = FindActorByNameInWorld(World, ActorName);
    if (!Actor)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Actor not found: %s"), *ActorName));
    }

    UPCGComponent* PCGComponent = FindPCGComponentOnActor(Actor, ComponentName);
    const bool bCreated = (PCGComponent == nullptr);
    if (!PCGComponent)
    {
        Actor->Modify();
        PCGComponent = NewObject<UPCGComponent>(Actor, UPCGComponent::StaticClass(), *ComponentName, RF_Transactional);
        if (!PCGComponent)
        {
            return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Failed to create PCG component"));
        }

        PCGComponent->SetFlags(RF_Transactional);
        Actor->AddInstanceComponent(PCGComponent);
        PCGComponent->OnComponentCreated();
        PCGComponent->RegisterComponent();
    }

    FString ApplyError;
    if (!TryApplyPCGComponentSettings(Params, PCGComponent, ApplyError))
    {
        return FUnrealAICommonUtils::CreateErrorResponse(ApplyError);
    }

    TSharedPtr<FJsonObject> ReadParams = MakeShared<FJsonObject>();
    ReadParams->SetStringField(TEXT("actor_name"), Actor->GetActorLabel());
    ReadParams->SetStringField(TEXT("component_name"), PCGComponent->GetName());
    TSharedPtr<FJsonObject> Result = HandleReadPCGComponentContent(ReadParams);
    Result->SetBoolField(TEXT("created"), bCreated);
    Result->SetBoolField(TEXT("success"), true);
    return Result;
}

// --------------------------------------------------------------------------- //
// create_pcg_volume
// --------------------------------------------------------------------------- //
TSharedPtr<FJsonObject> FUnrealAIAssetCommands::HandleCreatePCGVolume(const TSharedPtr<FJsonObject>& Params)
{
    FString VolumeName;
    if (!Params->TryGetStringField(TEXT("volume_name"), VolumeName) || VolumeName.TrimStartAndEnd().IsEmpty())
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing or empty 'volume_name' parameter"));
    }
    VolumeName = VolumeName.TrimStartAndEnd();

    UWorld* World = GetEditorWorld();
    if (!World)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Failed to resolve editor world"));
    }

    FVector Location = FVector::ZeroVector;
    FRotator Rotation = FRotator::ZeroRotator;
    FVector Scale = FVector::OneVector;

    TArray<double> TransformValues;
    if (TryReadJsonNumberArrayField(Params, TEXT("location"), 3, TransformValues))
    {
        Location = FVector(TransformValues[0], TransformValues[1], TransformValues[2]);
    }
    if (TryReadJsonNumberArrayField(Params, TEXT("rotation"), 3, TransformValues))
    {
        Rotation = FRotator(TransformValues[0], TransformValues[1], TransformValues[2]);
    }
    if (TryReadJsonNumberArrayField(Params, TEXT("scale"), 3, TransformValues))
    {
        Scale = FVector(TransformValues[0], TransformValues[1], TransformValues[2]);
    }

    FTransform SpawnTransform(Rotation, Location, Scale);
    APCGVolume* VolumeActor = nullptr;

    if (GEditor)
    {
        TSubclassOf<APCGVolume> PCGVolumeClass = APCGVolume::StaticClass();
        UActorFactory* PCGVolumeFactory = GEditor->FindActorFactoryForActorClass(PCGVolumeClass);
        if (PCGVolumeFactory && GCurrentLevelEditingViewportClient)
        {
            VolumeActor = Cast<APCGVolume>(GEditor->UseActorFactory(PCGVolumeFactory, FAssetData(PCGVolumeClass), &SpawnTransform));
        }
    }

    if (!VolumeActor)
    {
        VolumeActor = World->SpawnActor<APCGVolume>(APCGVolume::StaticClass(), SpawnTransform);
    }

    if (!VolumeActor)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Failed to create PCG volume actor"));
    }

    VolumeActor->SetActorLabel(VolumeName);
    VolumeActor->Modify();
    VolumeActor->MarkPackageDirty();

    UPCGComponent* PCGComponent = VolumeActor->FindComponentByClass<UPCGComponent>();
    if (!PCGComponent)
    {
        PCGComponent = NewObject<UPCGComponent>(VolumeActor, UPCGComponent::StaticClass(), TEXT("PCGComponent"), RF_Transactional);
        if (!PCGComponent)
        {
            return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Failed to create PCG component on PCG volume"));
        }

        PCGComponent->SetFlags(RF_Transactional);
        VolumeActor->AddInstanceComponent(PCGComponent);
        PCGComponent->OnComponentCreated();
        PCGComponent->RegisterComponent();
    }

    FString ApplyError;
    if (!TryApplyPCGComponentSettings(Params, PCGComponent, ApplyError))
    {
        return FUnrealAICommonUtils::CreateErrorResponse(ApplyError);
    }

    TSharedPtr<FJsonObject> Result = BuildPCGComponentSummary(PCGComponent);
    Result->SetStringField(TEXT("volume_name"), VolumeActor->GetActorLabel());
    Result->SetBoolField(TEXT("created"), true);
    Result->SetBoolField(TEXT("success"), true);
    return Result;
}

// --------------------------------------------------------------------------- //
// list_pcg_node_types
// --------------------------------------------------------------------------- //
TSharedPtr<FJsonObject> FUnrealAIAssetCommands::HandleListPCGNodeTypes(const TSharedPtr<FJsonObject>& Params)
{
    FString Query;
    FString SettingsTypeFilter;
    int32 MaxResults = 300;
    Params->TryGetStringField(TEXT("query"), Query);
    Params->TryGetStringField(TEXT("settings_type"), SettingsTypeFilter);
    if (Params->HasField(TEXT("max_results")))
    {
        MaxResults = FMath::Clamp(Params->GetIntegerField(TEXT("max_results")), 1, 1000);
    }

    Query = Query.TrimStartAndEnd().ToLower();
    SettingsTypeFilter = SettingsTypeFilter.TrimStartAndEnd();

    TArray<UClass*> DerivedClasses;
    GetDerivedClasses(UPCGSettings::StaticClass(), DerivedClasses, true);
    DerivedClasses.Sort([](const UClass& Left, const UClass& Right)
    {
        return Left.GetName() < Right.GetName();
    });

    TArray<TSharedPtr<FJsonValue>> NodeTypes;
    NodeTypes.Reserve(FMath::Min(DerivedClasses.Num(), MaxResults));

    for (UClass* SettingsClass : DerivedClasses)
    {
        if (!SettingsClass || SettingsClass->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists))
        {
            continue;
        }

        const UPCGSettings* Settings = Cast<UPCGSettings>(SettingsClass->GetDefaultObject());
        if (!Settings)
        {
            continue;
        }

        const FString TypeName = GetPCGSettingsTypeName(Settings->GetType());
        if (!SettingsTypeFilter.IsEmpty() && !TypeName.Equals(SettingsTypeFilter, ESearchCase::IgnoreCase))
        {
            continue;
        }

        if (!Query.IsEmpty())
        {
            const FString ClassNameLower = SettingsClass->GetName().ToLower();
            const FString ClassPathLower = SettingsClass->GetPathName().ToLower();
            const FString DefaultNodeNameLower = Settings->GetDefaultNodeName().ToString().ToLower();
            const FString DefaultNodeTitleLower = Settings->GetDefaultNodeTitle().ToString().ToLower();

            bool bMatchesQuery = ClassNameLower.Contains(Query)
                || ClassPathLower.Contains(Query)
                || DefaultNodeNameLower.Contains(Query)
                || DefaultNodeTitleLower.Contains(Query);

#if WITH_EDITOR
            if (!bMatchesQuery)
            {
                for (const FText& Alias : Settings->GetNodeTitleAliases())
                {
                    if (Alias.ToString().ToLower().Contains(Query))
                    {
                        bMatchesQuery = true;
                        break;
                    }
                }
            }
#endif

            if (!bMatchesQuery)
            {
                continue;
            }
        }

        NodeTypes.Add(MakeShared<FJsonValueObject>(BuildPCGNodeTypeSummary(SettingsClass)));
        if (NodeTypes.Num() >= MaxResults)
        {
            break;
        }
    }

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetArrayField(TEXT("node_types"), NodeTypes);
    Result->SetNumberField(TEXT("count"), NodeTypes.Num());
    Result->SetBoolField(TEXT("truncated"), NodeTypes.Num() >= MaxResults);
    Result->SetBoolField(TEXT("success"), true);
    return Result;
}

// --------------------------------------------------------------------------- //
// read_pcg_graph_nodes
// --------------------------------------------------------------------------- //
TSharedPtr<FJsonObject> FUnrealAIAssetCommands::HandleReadPCGGraphNodes(const TSharedPtr<FJsonObject>& Params)
{
    FString AssetPath;
    FString Error;
    UPCGGraph* Graph = LoadPCGEditableGraphAsset(Params, AssetPath, Error);
    if (!Graph)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(Error);
    }

    TArray<UPCGNode*> Nodes = GetInspectablePCGGraphNodes(Graph);
    Nodes.Sort([](const UPCGNode& Left, const UPCGNode& Right)
    {
        return Left.GetPathName() < Right.GetPathName();
    });

    TArray<TSharedPtr<FJsonValue>> NodeArray;
    NodeArray.Reserve(Nodes.Num());
    for (UPCGNode* Node : Nodes)
    {
        NodeArray.Add(MakeShared<FJsonValueObject>(BuildPCGNodeSummary(Node)));
    }

    TSharedPtr<FJsonObject> Result = BuildPCGGraphSummary(Graph, AssetPath);
    Result->SetArrayField(TEXT("nodes"), NodeArray);
    Result->SetNumberField(TEXT("count"), NodeArray.Num());
    Result->SetBoolField(TEXT("success"), true);
    return Result;
}

// --------------------------------------------------------------------------- //
// read_pcg_graph_node
// --------------------------------------------------------------------------- //
TSharedPtr<FJsonObject> FUnrealAIAssetCommands::HandleReadPCGGraphNode(const TSharedPtr<FJsonObject>& Params)
{
    FString AssetPath;
    FString Error;
    UPCGGraph* Graph = LoadPCGEditableGraphAsset(Params, AssetPath, Error);
    if (!Graph)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(Error);
    }

    UPCGNode* Node = ResolvePCGGraphNode(Graph, Params, Error);
    if (!Node)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(Error);
    }

    TSharedPtr<FJsonObject> Result = BuildPCGNodeSummary(Node);
    Result->SetStringField(TEXT("asset_path"), AssetPath);
    Result->SetBoolField(TEXT("success"), true);
    return Result;
}

// --------------------------------------------------------------------------- //
// add_pcg_graph_node
// --------------------------------------------------------------------------- //
TSharedPtr<FJsonObject> FUnrealAIAssetCommands::HandleAddPCGGraphNode(const TSharedPtr<FJsonObject>& Params)
{
    FString AssetPath;
    FString Error;
    UPCGGraph* Graph = LoadPCGEditableGraphAsset(Params, AssetPath, Error);
    if (!Graph)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(Error);
    }

    FString SettingsClassIdentifier;
    if (!Params->TryGetStringField(TEXT("settings_class"), SettingsClassIdentifier) || SettingsClassIdentifier.TrimStartAndEnd().IsEmpty())
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing or empty 'settings_class' parameter"));
    }
    SettingsClassIdentifier = SettingsClassIdentifier.TrimStartAndEnd();

    UClass* SettingsClass = ResolvePCGSettingsClass(SettingsClassIdentifier);
    if (!SettingsClass)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Failed to resolve PCG settings class: %s"), *SettingsClassIdentifier));
    }

    UPCGSettings* DefaultSettings = nullptr;
    Graph->Modify();
    UPCGNode* Node = Graph->AddNodeOfType(SettingsClass, DefaultSettings);
    if (!Node || !DefaultSettings)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Failed to add PCG node to graph"));
    }

    Node->Modify();

    FString NodeTitle;
    Params->TryGetStringField(TEXT("node_title"), NodeTitle);
    NodeTitle = NodeTitle.TrimStartAndEnd();
#if WITH_EDITOR
    if (!NodeTitle.IsEmpty())
    {
        Node->SetNodeTitle(FName(*NodeTitle), true);
    }

    double PositionX = 0.0;
    double PositionY = 0.0;
    bool bHasPositionX = Params->TryGetNumberField(TEXT("position_x"), PositionX);
    bool bHasPositionY = Params->TryGetNumberField(TEXT("position_y"), PositionY);
    if (bHasPositionX || bHasPositionY)
    {
        int32 CurrentX = 0;
        int32 CurrentY = 0;
        Node->GetNodePosition(CurrentX, CurrentY);
        Node->SetNodePosition(
            bHasPositionX ? static_cast<int32>(PositionX) : CurrentX,
            bHasPositionY ? static_cast<int32>(PositionY) : CurrentY);
    }
#endif

    FString SubgraphAssetPath;
    Params->TryGetStringField(TEXT("subgraph_asset_path"), SubgraphAssetPath);
    SubgraphAssetPath = SubgraphAssetPath.TrimStartAndEnd();
    if (!SubgraphAssetPath.IsEmpty())
    {
        UPCGSubgraphSettings* SubgraphSettings = Cast<UPCGSubgraphSettings>(DefaultSettings);
        if (!SubgraphSettings)
        {
            return FUnrealAICommonUtils::CreateErrorResponse(
                TEXT("'subgraph_asset_path' is only valid when 'settings_class' resolves to UPCGSubgraphSettings"));
        }

        UPCGGraphInterface* Subgraph = Cast<UPCGGraphInterface>(UEditorAssetLibrary::LoadAsset(SubgraphAssetPath));
        if (!Subgraph)
        {
            return FUnrealAICommonUtils::CreateErrorResponse(
                FString::Printf(TEXT("Failed to load subgraph asset: %s"), *SubgraphAssetPath));
        }

        SubgraphSettings->SetSubgraph(Subgraph);
    }

    Graph->MarkPackageDirty();
    UEditorAssetLibrary::SaveAsset(AssetPath, /*bOnlyIfIsDirty=*/false);

    TSharedPtr<FJsonObject> Result = BuildPCGNodeSummary(Node);
    Result->SetStringField(TEXT("asset_path"), AssetPath);
    Result->SetStringField(TEXT("resolved_settings_class_path"), SettingsClass->GetPathName());
    Result->SetBoolField(TEXT("created"), true);
    Result->SetBoolField(TEXT("success"), true);
    return Result;
}

// --------------------------------------------------------------------------- //
// delete_pcg_graph_node
// --------------------------------------------------------------------------- //
TSharedPtr<FJsonObject> FUnrealAIAssetCommands::HandleDeletePCGGraphNode(const TSharedPtr<FJsonObject>& Params)
{
    FString AssetPath;
    FString Error;
    UPCGGraph* Graph = LoadPCGEditableGraphAsset(Params, AssetPath, Error);
    if (!Graph)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(Error);
    }

    UPCGNode* Node = ResolvePCGGraphNode(Graph, Params, Error);
    if (!Node)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(Error);
    }

    if (Node == Graph->GetInputNode() || Node == Graph->GetOutputNode())
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Input and output nodes cannot be deleted"));
    }

    TSharedPtr<FJsonObject> DeletedNodeSummary = BuildPCGNodeSummary(Node);
    Graph->Modify();
    Graph->RemoveNode(Node);
    Graph->MarkPackageDirty();
    UEditorAssetLibrary::SaveAsset(AssetPath, /*bOnlyIfIsDirty=*/false);

    DeletedNodeSummary->SetStringField(TEXT("asset_path"), AssetPath);
    DeletedNodeSummary->SetBoolField(TEXT("deleted"), true);
    DeletedNodeSummary->SetBoolField(TEXT("success"), true);
    return DeletedNodeSummary;
}

// --------------------------------------------------------------------------- //
// connect_pcg_graph_nodes
// --------------------------------------------------------------------------- //
TSharedPtr<FJsonObject> FUnrealAIAssetCommands::HandleConnectPCGGraphNodes(const TSharedPtr<FJsonObject>& Params)
{
    FString AssetPath;
    FString Error;
    UPCGGraph* Graph = LoadPCGEditableGraphAsset(Params, AssetPath, Error);
    if (!Graph)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(Error);
    }

    const TSharedPtr<FJsonObject> SourceNodeParams = MakeShared<FJsonObject>();
    if (Params->HasTypedField<EJson::String>(TEXT("source_node_path")))
    {
        SourceNodeParams->SetStringField(TEXT("node_path"), Params->GetStringField(TEXT("source_node_path")));
    }
    if (Params->HasTypedField<EJson::String>(TEXT("source_node_name")))
    {
        SourceNodeParams->SetStringField(TEXT("node_name"), Params->GetStringField(TEXT("source_node_name")));
    }
    if (Params->HasTypedField<EJson::String>(TEXT("source_node_title")))
    {
        SourceNodeParams->SetStringField(TEXT("node_title"), Params->GetStringField(TEXT("source_node_title")));
    }

    const TSharedPtr<FJsonObject> TargetNodeParams = MakeShared<FJsonObject>();
    if (Params->HasTypedField<EJson::String>(TEXT("target_node_path")))
    {
        TargetNodeParams->SetStringField(TEXT("node_path"), Params->GetStringField(TEXT("target_node_path")));
    }
    if (Params->HasTypedField<EJson::String>(TEXT("target_node_name")))
    {
        TargetNodeParams->SetStringField(TEXT("node_name"), Params->GetStringField(TEXT("target_node_name")));
    }
    if (Params->HasTypedField<EJson::String>(TEXT("target_node_title")))
    {
        TargetNodeParams->SetStringField(TEXT("node_title"), Params->GetStringField(TEXT("target_node_title")));
    }

    UPCGNode* SourceNode = ResolvePCGGraphNode(Graph, SourceNodeParams, Error);
    if (!SourceNode)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(FString::Printf(TEXT("Failed to resolve source node: %s"), *Error));
    }

    UPCGNode* TargetNode = ResolvePCGGraphNode(Graph, TargetNodeParams, Error);
    if (!TargetNode)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(FString::Printf(TEXT("Failed to resolve target node: %s"), *Error));
    }

    FString SourcePinName;
    FString TargetPinName;
    if (!Params->TryGetStringField(TEXT("source_pin_name"), SourcePinName) || SourcePinName.TrimStartAndEnd().IsEmpty())
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing or empty 'source_pin_name' parameter"));
    }
    if (!Params->TryGetStringField(TEXT("target_pin_name"), TargetPinName) || TargetPinName.TrimStartAndEnd().IsEmpty())
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing or empty 'target_pin_name' parameter"));
    }

    SourcePinName = SourcePinName.TrimStartAndEnd();
    TargetPinName = TargetPinName.TrimStartAndEnd();
    if (!SourceNode->GetOutputPin(FName(*SourcePinName)))
    {
        return FUnrealAICommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Output pin '%s' not found on source node"), *SourcePinName));
    }
    if (!TargetNode->GetInputPin(FName(*TargetPinName)))
    {
        return FUnrealAICommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Input pin '%s' not found on target node"), *TargetPinName));
    }

    Graph->Modify();
    Graph->AddEdge(SourceNode, FName(*SourcePinName), TargetNode, FName(*TargetPinName));
    Graph->MarkPackageDirty();
    UEditorAssetLibrary::SaveAsset(AssetPath, /*bOnlyIfIsDirty=*/false);

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("asset_path"), AssetPath);
    Result->SetStringField(TEXT("source_pin_name"), SourcePinName);
    Result->SetStringField(TEXT("target_pin_name"), TargetPinName);
    Result->SetObjectField(TEXT("source_node"), BuildPCGNodeSummary(SourceNode));
    Result->SetObjectField(TEXT("target_node"), BuildPCGNodeSummary(TargetNode));
    Result->SetBoolField(TEXT("connected"), true);
    Result->SetBoolField(TEXT("success"), true);
    return Result;
}

// --------------------------------------------------------------------------- //
// disconnect_pcg_graph_nodes
// --------------------------------------------------------------------------- //
TSharedPtr<FJsonObject> FUnrealAIAssetCommands::HandleDisconnectPCGGraphNodes(const TSharedPtr<FJsonObject>& Params)
{
    FString AssetPath;
    FString Error;
    UPCGGraph* Graph = LoadPCGEditableGraphAsset(Params, AssetPath, Error);
    if (!Graph)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(Error);
    }

    const TSharedPtr<FJsonObject> SourceNodeParams = MakeShared<FJsonObject>();
    if (Params->HasTypedField<EJson::String>(TEXT("source_node_path")))
    {
        SourceNodeParams->SetStringField(TEXT("node_path"), Params->GetStringField(TEXT("source_node_path")));
    }
    if (Params->HasTypedField<EJson::String>(TEXT("source_node_name")))
    {
        SourceNodeParams->SetStringField(TEXT("node_name"), Params->GetStringField(TEXT("source_node_name")));
    }
    if (Params->HasTypedField<EJson::String>(TEXT("source_node_title")))
    {
        SourceNodeParams->SetStringField(TEXT("node_title"), Params->GetStringField(TEXT("source_node_title")));
    }

    const TSharedPtr<FJsonObject> TargetNodeParams = MakeShared<FJsonObject>();
    if (Params->HasTypedField<EJson::String>(TEXT("target_node_path")))
    {
        TargetNodeParams->SetStringField(TEXT("node_path"), Params->GetStringField(TEXT("target_node_path")));
    }
    if (Params->HasTypedField<EJson::String>(TEXT("target_node_name")))
    {
        TargetNodeParams->SetStringField(TEXT("node_name"), Params->GetStringField(TEXT("target_node_name")));
    }
    if (Params->HasTypedField<EJson::String>(TEXT("target_node_title")))
    {
        TargetNodeParams->SetStringField(TEXT("node_title"), Params->GetStringField(TEXT("target_node_title")));
    }

    UPCGNode* SourceNode = ResolvePCGGraphNode(Graph, SourceNodeParams, Error);
    if (!SourceNode)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(FString::Printf(TEXT("Failed to resolve source node: %s"), *Error));
    }

    UPCGNode* TargetNode = ResolvePCGGraphNode(Graph, TargetNodeParams, Error);
    if (!TargetNode)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(FString::Printf(TEXT("Failed to resolve target node: %s"), *Error));
    }

    FString SourcePinName;
    FString TargetPinName;
    if (!Params->TryGetStringField(TEXT("source_pin_name"), SourcePinName) || SourcePinName.TrimStartAndEnd().IsEmpty())
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing or empty 'source_pin_name' parameter"));
    }
    if (!Params->TryGetStringField(TEXT("target_pin_name"), TargetPinName) || TargetPinName.TrimStartAndEnd().IsEmpty())
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing or empty 'target_pin_name' parameter"));
    }

    SourcePinName = SourcePinName.TrimStartAndEnd();
    TargetPinName = TargetPinName.TrimStartAndEnd();
    const bool bRemoved = Graph->RemoveEdge(SourceNode, FName(*SourcePinName), TargetNode, FName(*TargetPinName));
    if (bRemoved)
    {
        Graph->Modify();
        Graph->MarkPackageDirty();
        UEditorAssetLibrary::SaveAsset(AssetPath, /*bOnlyIfIsDirty=*/false);
    }

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("asset_path"), AssetPath);
    Result->SetStringField(TEXT("source_pin_name"), SourcePinName);
    Result->SetStringField(TEXT("target_pin_name"), TargetPinName);
    Result->SetObjectField(TEXT("source_node"), BuildPCGNodeSummary(SourceNode));
    Result->SetObjectField(TEXT("target_node"), BuildPCGNodeSummary(TargetNode));
    Result->SetBoolField(TEXT("removed"), bRemoved);
    Result->SetBoolField(TEXT("success"), true);
    return Result;
}

// --------------------------------------------------------------------------- //
// set_pcg_graph_node_position
// --------------------------------------------------------------------------- //
TSharedPtr<FJsonObject> FUnrealAIAssetCommands::HandleSetPCGGraphNodePosition(const TSharedPtr<FJsonObject>& Params)
{
    FString AssetPath;
    FString LoadError;
    UPCGGraph* Graph = LoadPCGEditableGraphAsset(Params, AssetPath, LoadError);
    if (!Graph)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(LoadError);
    }

#if !WITH_EDITOR
    return FUnrealAICommonUtils::CreateErrorResponse(TEXT("set_pcg_graph_node_position requires editor support"));
#else
    FString ResolveError;
    UPCGNode* Node = ResolvePCGGraphNode(Graph, Params, ResolveError);
    if (!Node)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(ResolveError);
    }

    double PositionXValue = 0.0;
    double PositionYValue = 0.0;
    const bool bHasPositionX = Params->TryGetNumberField(TEXT("position_x"), PositionXValue);
    const bool bHasPositionY = Params->TryGetNumberField(TEXT("position_y"), PositionYValue);
    if (!bHasPositionX && !bHasPositionY)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Provide at least one of: position_x, position_y"));
    }

    int32 CurrentX = 0;
    int32 CurrentY = 0;
    Node->GetNodePosition(CurrentX, CurrentY);

    Graph->Modify();
    Node->Modify();
    Node->SetNodePosition(
        bHasPositionX ? FMath::RoundToInt(PositionXValue) : CurrentX,
        bHasPositionY ? FMath::RoundToInt(PositionYValue) : CurrentY);

    NotifyPCGGraphEdited(Graph, EPCGChangeType::Cosmetic);
    Graph->MarkPackageDirty();
    UEditorAssetLibrary::SaveAsset(AssetPath, /*bOnlyIfIsDirty=*/false);

    TSharedPtr<FJsonObject> Result = BuildPCGNodeSummary(Node);
    Result->SetStringField(TEXT("asset_path"), AssetPath);
    Result->SetBoolField(TEXT("updated"), true);
    Result->SetBoolField(TEXT("success"), true);
    return Result;
#endif
}

// --------------------------------------------------------------------------- //
// set_pcg_subgraph_node_asset
// --------------------------------------------------------------------------- //
TSharedPtr<FJsonObject> FUnrealAIAssetCommands::HandleSetPCGSubgraphNodeAsset(const TSharedPtr<FJsonObject>& Params)
{
    FString AssetPath;
    FString LoadError;
    UPCGGraph* Graph = LoadPCGEditableGraphAsset(Params, AssetPath, LoadError);
    if (!Graph)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(LoadError);
    }

    FString ResolveError;
    UPCGNode* Node = ResolvePCGGraphNode(Graph, Params, ResolveError);
    if (!Node)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(ResolveError);
    }

    UPCGSubgraphSettings* SubgraphSettings = Cast<UPCGSubgraphSettings>(Node->GetSettings());
    if (!SubgraphSettings)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Resolved node is not a PCG subgraph node"));
    }

    bool bClearSubgraph = false;
    Params->TryGetBoolField(TEXT("clear_subgraph"), bClearSubgraph);

    FString SubgraphAssetPath;
    const bool bHasSubgraphAssetPath = Params->TryGetStringField(TEXT("subgraph_asset_path"), SubgraphAssetPath);
    SubgraphAssetPath = SubgraphAssetPath.TrimStartAndEnd();
    if (!bHasSubgraphAssetPath && !bClearSubgraph)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Provide 'subgraph_asset_path' or set 'clear_subgraph' to true"));
    }

    UPCGGraphInterface* SubgraphAsset = nullptr;
    if (!bClearSubgraph && !SubgraphAssetPath.IsEmpty())
    {
        SubgraphAsset = Cast<UPCGGraphInterface>(UEditorAssetLibrary::LoadAsset(SubgraphAssetPath));
        if (!SubgraphAsset)
        {
            return FUnrealAICommonUtils::CreateErrorResponse(
                FString::Printf(TEXT("Failed to load PCG subgraph asset: %s"), *SubgraphAssetPath));
        }
    }

    Graph->Modify();
    Node->Modify();
    SubgraphSettings->Modify();
    SubgraphSettings->SetSubgraph(SubgraphAsset);

#if WITH_EDITOR
    FProperty* ChangedProperty = SubgraphSettings->GetClass()->FindPropertyByName(FName(TEXT("SubgraphOverride")));
    FPropertyChangedEvent PropertyChangedEvent(ChangedProperty, EPropertyChangeType::ValueSet);
    static_cast<UObject*>(SubgraphSettings)->PostEditChangeProperty(PropertyChangedEvent);
#endif

    NotifyPCGGraphEdited(Graph, EPCGChangeType::Structural);
    Graph->MarkPackageDirty();
    UEditorAssetLibrary::SaveAsset(AssetPath, /*bOnlyIfIsDirty=*/false);

    TSharedPtr<FJsonObject> Result = BuildPCGNodeSummary(Node);
    Result->SetStringField(TEXT("asset_path"), AssetPath);
    Result->SetStringField(TEXT("subgraph_asset_path"), SubgraphSettings->GetSubgraph() ? SubgraphSettings->GetSubgraph()->GetPathName() : TEXT(""));
    Result->SetBoolField(TEXT("updated"), true);
    Result->SetBoolField(TEXT("success"), true);
    return Result;
}

// --------------------------------------------------------------------------- //
// add_pcg_graph_comment
// --------------------------------------------------------------------------- //
TSharedPtr<FJsonObject> FUnrealAIAssetCommands::HandleAddPCGGraphComment(const TSharedPtr<FJsonObject>& Params)
{
    FString AssetPath;
    FString LoadError;
    UPCGGraph* Graph = LoadPCGEditableGraphAsset(Params, AssetPath, LoadError);
    if (!Graph)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(LoadError);
    }

#if !WITH_EDITORONLY_DATA
    return FUnrealAICommonUtils::CreateErrorResponse(TEXT("add_pcg_graph_comment requires editor-only PCG graph data"));
#else
    TArray<FPCGGraphCommentNodeData> CommentNodes = Graph->GetCommentNodes();
    FPCGGraphCommentNodeData NewCommentNode;

    FString GuidError;
    if (Params->HasTypedField<EJson::String>(TEXT("comment_guid")))
    {
        if (!TryGetPCGCommentGuid(Params, TEXT("comment_guid"), NewCommentNode.GUID, GuidError))
        {
            return FUnrealAICommonUtils::CreateErrorResponse(GuidError);
        }
    }
    else
    {
        NewCommentNode.GUID = FGuid::NewGuid();
    }

    for (const FPCGGraphCommentNodeData& ExistingCommentNode : CommentNodes)
    {
        if (ExistingCommentNode.GUID == NewCommentNode.GUID)
        {
            return FUnrealAICommonUtils::CreateErrorResponse(
                FString::Printf(TEXT("PCG graph comment GUID already exists: %s"), *NewCommentNode.GUID.ToString(EGuidFormats::DigitsWithHyphens)));
        }
    }

    FString PatchError;
    if (!TryApplyPCGCommentNodePatch(Params, NewCommentNode, PatchError))
    {
        return FUnrealAICommonUtils::CreateErrorResponse(PatchError);
    }

    CommentNodes.Add(NewCommentNode);
    Graph->Modify();
    Graph->SetCommentNodes(MoveTemp(CommentNodes));
    NotifyPCGGraphEdited(Graph, EPCGChangeType::Cosmetic);
    Graph->MarkPackageDirty();
    UEditorAssetLibrary::SaveAsset(AssetPath, /*bOnlyIfIsDirty=*/false);

    TSharedPtr<FJsonObject> Result = BuildPCGGraphCommentSummary(NewCommentNode);
    Result->SetStringField(TEXT("asset_path"), AssetPath);
    Result->SetBoolField(TEXT("created"), true);
    Result->SetBoolField(TEXT("success"), true);
    return Result;
#endif
}

// --------------------------------------------------------------------------- //
// update_pcg_graph_comment
// --------------------------------------------------------------------------- //
TSharedPtr<FJsonObject> FUnrealAIAssetCommands::HandleUpdatePCGGraphComment(const TSharedPtr<FJsonObject>& Params)
{
    FString AssetPath;
    FString LoadError;
    UPCGGraph* Graph = LoadPCGEditableGraphAsset(Params, AssetPath, LoadError);
    if (!Graph)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(LoadError);
    }

#if !WITH_EDITORONLY_DATA
    return FUnrealAICommonUtils::CreateErrorResponse(TEXT("update_pcg_graph_comment requires editor-only PCG graph data"));
#else
    FGuid CommentGuid;
    FString GuidError;
    if (!TryGetPCGCommentGuid(Params, TEXT("comment_guid"), CommentGuid, GuidError))
    {
        return FUnrealAICommonUtils::CreateErrorResponse(GuidError);
    }

    TArray<FPCGGraphCommentNodeData> CommentNodes = Graph->GetCommentNodes();
    FPCGGraphCommentNodeData* CommentNode = CommentNodes.FindByPredicate([CommentGuid](const FPCGGraphCommentNodeData& ExistingCommentNode)
    {
        return ExistingCommentNode.GUID == CommentGuid;
    });
    if (!CommentNode)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("PCG graph comment not found: %s"), *CommentGuid.ToString(EGuidFormats::DigitsWithHyphens)));
    }

    FString PatchError;
    if (!TryApplyPCGCommentNodePatch(Params, *CommentNode, PatchError))
    {
        return FUnrealAICommonUtils::CreateErrorResponse(PatchError);
    }

    Graph->Modify();
    Graph->SetCommentNodes(MoveTemp(CommentNodes));
    NotifyPCGGraphEdited(Graph, EPCGChangeType::Cosmetic);
    Graph->MarkPackageDirty();
    UEditorAssetLibrary::SaveAsset(AssetPath, /*bOnlyIfIsDirty=*/false);

    TSharedPtr<FJsonObject> Result = BuildPCGGraphCommentSummary(*CommentNode);
    Result->SetStringField(TEXT("asset_path"), AssetPath);
    Result->SetBoolField(TEXT("updated"), true);
    Result->SetBoolField(TEXT("success"), true);
    return Result;
#endif
}

// --------------------------------------------------------------------------- //
// delete_pcg_graph_comment
// --------------------------------------------------------------------------- //
TSharedPtr<FJsonObject> FUnrealAIAssetCommands::HandleDeletePCGGraphComment(const TSharedPtr<FJsonObject>& Params)
{
    FString AssetPath;
    FString LoadError;
    UPCGGraph* Graph = LoadPCGEditableGraphAsset(Params, AssetPath, LoadError);
    if (!Graph)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(LoadError);
    }

#if !WITH_EDITORONLY_DATA
    return FUnrealAICommonUtils::CreateErrorResponse(TEXT("delete_pcg_graph_comment requires editor-only PCG graph data"));
#else
    FGuid CommentGuid;
    FString GuidError;
    if (!TryGetPCGCommentGuid(Params, TEXT("comment_guid"), CommentGuid, GuidError))
    {
        return FUnrealAICommonUtils::CreateErrorResponse(GuidError);
    }

    const FPCGGraphCommentNodeData* CommentNode = Graph->GetCommentNodes().FindByPredicate([CommentGuid](const FPCGGraphCommentNodeData& ExistingCommentNode)
    {
        return ExistingCommentNode.GUID == CommentGuid;
    });
    if (!CommentNode)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("PCG graph comment not found: %s"), *CommentGuid.ToString(EGuidFormats::DigitsWithHyphens)));
    }

    TSharedPtr<FJsonObject> Result = BuildPCGGraphCommentSummary(*CommentNode);
    Graph->Modify();
    Graph->RemoveCommentNode(CommentGuid);
    NotifyPCGGraphEdited(Graph, EPCGChangeType::Cosmetic);
    Graph->MarkPackageDirty();
    UEditorAssetLibrary::SaveAsset(AssetPath, /*bOnlyIfIsDirty=*/false);

    Result->SetStringField(TEXT("asset_path"), AssetPath);
    Result->SetBoolField(TEXT("deleted"), true);
    Result->SetBoolField(TEXT("success"), true);
    return Result;
#endif
}

// --------------------------------------------------------------------------- //
// add_pcg_graph_reroute
// --------------------------------------------------------------------------- //
TSharedPtr<FJsonObject> FUnrealAIAssetCommands::HandleAddPCGGraphReroute(const TSharedPtr<FJsonObject>& Params)
{
    FString AssetPath;
    FString LoadError;
    UPCGGraph* Graph = LoadPCGEditableGraphAsset(Params, AssetPath, LoadError);
    if (!Graph)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(LoadError);
    }

    FString RerouteKind;
    if (!Params->TryGetStringField(TEXT("reroute_kind"), RerouteKind) || RerouteKind.TrimStartAndEnd().IsEmpty())
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing or empty 'reroute_kind' parameter"));
    }

    const FString NormalizedRerouteKind = RerouteKind.TrimStartAndEnd().ToLower().Replace(TEXT("_"), TEXT("")).Replace(TEXT("-"), TEXT("")).Replace(TEXT(" "), TEXT(""));

    UClass* SettingsClass = nullptr;
    bool bRequiresDeclaration = false;
    if (NormalizedRerouteKind == TEXT("reroute"))
    {
        SettingsClass = UPCGRerouteSettings::StaticClass();
    }
    else if (NormalizedRerouteKind == TEXT("nameddeclaration") || NormalizedRerouteKind == TEXT("declaration"))
    {
        SettingsClass = UPCGNamedRerouteDeclarationSettings::StaticClass();
    }
    else if (NormalizedRerouteKind == TEXT("namedusage") || NormalizedRerouteKind == TEXT("usage"))
    {
        SettingsClass = UPCGNamedRerouteUsageSettings::StaticClass();
        bRequiresDeclaration = true;
    }
    else
    {
        return FUnrealAICommonUtils::CreateErrorResponse(
            TEXT("'reroute_kind' must be one of: reroute, named_declaration, named_usage"));
    }

    UPCGNode* DeclarationNode = nullptr;
    if (bRequiresDeclaration)
    {
        const TSharedPtr<FJsonObject> DeclarationParams = MakeShared<FJsonObject>();
        if (Params->HasTypedField<EJson::String>(TEXT("declaration_node_path")))
        {
            DeclarationParams->SetStringField(TEXT("node_path"), Params->GetStringField(TEXT("declaration_node_path")));
        }
        if (Params->HasTypedField<EJson::String>(TEXT("declaration_node_name")))
        {
            DeclarationParams->SetStringField(TEXT("node_name"), Params->GetStringField(TEXT("declaration_node_name")));
        }
        if (Params->HasTypedField<EJson::String>(TEXT("declaration_node_title")))
        {
            DeclarationParams->SetStringField(TEXT("node_title"), Params->GetStringField(TEXT("declaration_node_title")));
        }
        if (DeclarationParams->Values.IsEmpty())
        {
            return FUnrealAICommonUtils::CreateErrorResponse(
                TEXT("named_usage reroutes require declaration_node_path, declaration_node_name, or declaration_node_title"));
        }

        FString ResolveError;
        DeclarationNode = ResolvePCGGraphNode(Graph, DeclarationParams, ResolveError);
        if (!DeclarationNode)
        {
            return FUnrealAICommonUtils::CreateErrorResponse(
                FString::Printf(TEXT("Failed to resolve named reroute declaration node: %s"), *ResolveError));
        }
        if (!Cast<UPCGNamedRerouteDeclarationSettings>(DeclarationNode->GetSettings()))
        {
            return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Resolved declaration node is not a named reroute declaration"));
        }
    }

    UPCGSettings* DefaultSettings = nullptr;
    Graph->Modify();
    UPCGNode* Node = Graph->AddNodeOfType(SettingsClass, DefaultSettings);
    if (!Node || !DefaultSettings)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Failed to add PCG reroute node to graph"));
    }

    Node->Modify();
    DefaultSettings->Modify();

    FString NodeTitle;
    Params->TryGetStringField(TEXT("node_title"), NodeTitle);
    NodeTitle = NodeTitle.TrimStartAndEnd();
    if (NodeTitle.IsEmpty() && DeclarationNode)
    {
        NodeTitle = DeclarationNode->GetGeneratedTitleLine().ToString();
    }

#if WITH_EDITOR
    if (!NodeTitle.IsEmpty())
    {
        Node->SetNodeTitle(FName(*NodeTitle), true);
    }

    double PositionX = 0.0;
    double PositionY = 0.0;
    const bool bHasPositionX = Params->TryGetNumberField(TEXT("position_x"), PositionX);
    const bool bHasPositionY = Params->TryGetNumberField(TEXT("position_y"), PositionY);
    if (bHasPositionX || bHasPositionY)
    {
        int32 CurrentX = 0;
        int32 CurrentY = 0;
        Node->GetNodePosition(CurrentX, CurrentY);
        Node->SetNodePosition(
            bHasPositionX ? FMath::RoundToInt(PositionX) : CurrentX,
            bHasPositionY ? FMath::RoundToInt(PositionY) : CurrentY);
    }
#endif

    if (UPCGNamedRerouteUsageSettings* UsageSettings = Cast<UPCGNamedRerouteUsageSettings>(DefaultSettings))
    {
        const UPCGNamedRerouteDeclarationSettings* DeclarationSettings = Cast<UPCGNamedRerouteDeclarationSettings>(DeclarationNode ? DeclarationNode->GetSettings() : nullptr);
        UsageSettings->Declaration = DeclarationSettings;

#if WITH_EDITOR
        FProperty* DeclarationProperty = UsageSettings->GetClass()->FindPropertyByName(FName(TEXT("Declaration")));
        FPropertyChangedEvent PropertyChangedEvent(DeclarationProperty, EPropertyChangeType::ValueSet);
        static_cast<UObject*>(UsageSettings)->PostEditChangeProperty(PropertyChangedEvent);
#endif
    }

    NotifyPCGGraphEdited(Graph, EPCGChangeType::Structural);
    Graph->MarkPackageDirty();
    UEditorAssetLibrary::SaveAsset(AssetPath, /*bOnlyIfIsDirty=*/false);

    TSharedPtr<FJsonObject> Result = BuildPCGNodeSummary(Node);
    Result->SetStringField(TEXT("asset_path"), AssetPath);
    Result->SetBoolField(TEXT("created"), true);
    Result->SetBoolField(TEXT("success"), true);
    return Result;
}

// --------------------------------------------------------------------------- //
// update_pcg_graph_node_settings
// --------------------------------------------------------------------------- //
TSharedPtr<FJsonObject> FUnrealAIAssetCommands::HandleUpdatePCGGraphNodeSettings(const TSharedPtr<FJsonObject>& Params)
{
    FString AssetPath;
    FString LoadError;
    UPCGGraph* Graph = LoadPCGEditableGraphAsset(Params, AssetPath, LoadError);
    if (!Graph)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(LoadError);
    }

    const TSharedPtr<FJsonObject>* SettingsPatch = nullptr;
    if (!Params->TryGetObjectField(TEXT("settings_patch"), SettingsPatch) || !SettingsPatch || !(*SettingsPatch).IsValid())
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing or invalid 'settings_patch' object parameter"));
    }

    FString ResolveError;
    UPCGNode* Node = ResolvePCGGraphNode(Graph, Params, ResolveError);
    if (!Node)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(ResolveError);
    }

    UPCGSettingsInterface* SettingsInterface = Node->GetSettingsInterface();
    UPCGSettings* Settings = Node->GetSettings();
    if (!SettingsInterface || !Settings)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Resolved node has no mutable PCG settings"));
    }

    TArray<FString> UpdatedProperties;
    for (const TPair<FString, TSharedPtr<FJsonValue>>& Entry : (*SettingsPatch)->Values)
    {
        const FName PropertyName(*Entry.Key);
        const FProperty* Property = Settings->GetClass()->FindPropertyByName(PropertyName);
        if (!IsPCGEditableSettingsProperty(Property))
        {
            return FUnrealAICommonUtils::CreateErrorResponse(
                FString::Printf(TEXT("'settings_patch.%s' is not an editable PCG settings property on %s"),
                    *Entry.Key,
                    *Settings->GetClass()->GetPathName()));
        }

        UpdatedProperties.Add(Entry.Key);
    }

    if (UpdatedProperties.IsEmpty())
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("'settings_patch' must contain at least one editable property"));
    }

    Settings->Modify();
    Node->Modify();
    Graph->Modify();

    FText ImportFailureReason;
    const bool bImported = FJsonObjectConverter::JsonObjectToUStruct(
        (*SettingsPatch).ToSharedRef(),
        Settings->GetClass(),
        Settings,
        0,
        0,
        false,
        &ImportFailureReason);

    if (!bImported)
    {
        const FString FailureMessage = ImportFailureReason.IsEmpty()
            ? TEXT("Unknown PCG settings deserialization failure")
            : ImportFailureReason.ToString();
        return FUnrealAICommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Failed to apply settings_patch to node '%s': %s"),
                *Node->GetPathName(),
                *FailureMessage));
    }

    if (UpdatedProperties.Num() == 1)
    {
        FProperty* ChangedProperty = Settings->GetClass()->FindPropertyByName(FName(*UpdatedProperties[0]));
        FPropertyChangedEvent PropertyChangedEvent(ChangedProperty, EPropertyChangeType::ValueSet);
        static_cast<UObject*>(Settings)->PostEditChangeProperty(PropertyChangedEvent);
    }
    else
    {
        FPropertyChangedEvent PropertyChangedEvent(nullptr, EPropertyChangeType::ValueSet);
        static_cast<UObject*>(Settings)->PostEditChangeProperty(PropertyChangedEvent);
    }

    Settings->MarkPackageDirty();
    Graph->MarkPackageDirty();
    UEditorAssetLibrary::SaveAsset(AssetPath, /*bOnlyIfIsDirty=*/false);

    TSharedPtr<FJsonObject> Result = BuildPCGNodeSummary(Node);
    Result->SetArrayField(TEXT("updated_properties"), StringArrayToJson(UpdatedProperties));
    Result->SetBoolField(TEXT("updated"), true);
    Result->SetBoolField(TEXT("success"), true);
    return Result;
}

// --------------------------------------------------------------------------- //
// set_pcg_graph_node_state
// --------------------------------------------------------------------------- //
TSharedPtr<FJsonObject> FUnrealAIAssetCommands::HandleSetPCGGraphNodeState(const TSharedPtr<FJsonObject>& Params)
{
    FString AssetPath;
    FString LoadError;
    UPCGGraph* Graph = LoadPCGEditableGraphAsset(Params, AssetPath, LoadError);
    if (!Graph)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(LoadError);
    }

    FString ResolveError;
    UPCGNode* Node = ResolvePCGGraphNode(Graph, Params, ResolveError);
    if (!Node)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(ResolveError);
    }

    UPCGSettingsInterface* SettingsInterface = Node->GetSettingsInterface();
    UPCGSettings* Settings = Node->GetSettings();
    if (!SettingsInterface || !Settings)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Resolved node has no mutable PCG settings"));
    }

    const bool bHasEnabled = Params->HasField(TEXT("enabled"));
    const bool bHasDebug = Params->HasField(TEXT("debug"));
    const bool bHasInspecting = Params->HasField(TEXT("inspecting"));
    if (!bHasEnabled && !bHasDebug && !bHasInspecting)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Provide at least one of: enabled, debug, inspecting"));
    }

    SettingsInterface->Modify();
    Node->Modify();
    Graph->Modify();
    Settings->Modify();

    TArray<FString> UpdatedStateFields;

    if (bHasEnabled)
    {
        bool bEnabled = false;
        if (!Params->TryGetBoolField(TEXT("enabled"), bEnabled))
        {
            return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Field 'enabled' must be a boolean"));
        }

        if (!bEnabled && !Settings->CanBeDisabled())
        {
            return FUnrealAICommonUtils::CreateErrorResponse(
                FString::Printf(TEXT("Node '%s' cannot be disabled"), *Node->GetPathName()));
        }

        SettingsInterface->SetEnabled(bEnabled);
        UpdatedStateFields.Add(TEXT("enabled"));
    }

    if (bHasDebug)
    {
        bool bDebug = false;
        if (!Params->TryGetBoolField(TEXT("debug"), bDebug))
        {
            return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Field 'debug' must be a boolean"));
        }

        if (bDebug && !Settings->CanBeDebugged())
        {
            return FUnrealAICommonUtils::CreateErrorResponse(
                FString::Printf(TEXT("Node '%s' cannot be put in debug mode"), *Node->GetPathName()));
        }

        if (SettingsInterface->bDebug != bDebug)
        {
            SettingsInterface->bDebug = bDebug;
            FProperty* DebugProperty = SettingsInterface->GetClass()->FindPropertyByName(
                GET_MEMBER_NAME_CHECKED(UPCGSettingsInterface, bDebug));
            FPropertyChangedEvent PropertyChangedEvent(DebugProperty, EPropertyChangeType::ValueSet);
            static_cast<UObject*>(SettingsInterface)->PostEditChangeProperty(PropertyChangedEvent);
        }

        UpdatedStateFields.Add(TEXT("debug"));
    }

#if WITH_EDITOR
    if (bHasInspecting)
    {
        bool bInspecting = false;
        if (!Params->TryGetBoolField(TEXT("inspecting"), bInspecting))
        {
            return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Field 'inspecting' must be a boolean"));
        }

        if (bInspecting && !Settings->CanBeDebugged())
        {
            return FUnrealAICommonUtils::CreateErrorResponse(
                FString::Printf(TEXT("Node '%s' cannot be inspected"), *Node->GetPathName()));
        }

        SettingsInterface->bIsInspecting = bInspecting;
        UpdatedStateFields.Add(TEXT("inspecting"));
    }
#endif

    Settings->MarkPackageDirty();
    Graph->MarkPackageDirty();
    UEditorAssetLibrary::SaveAsset(AssetPath, /*bOnlyIfIsDirty=*/false);

    TSharedPtr<FJsonObject> Result = BuildPCGNodeSummary(Node);
    Result->SetArrayField(TEXT("updated_state_fields"), StringArrayToJson(UpdatedStateFields));
    Result->SetBoolField(TEXT("updated"), true);
    Result->SetBoolField(TEXT("success"), true);
    return Result;
}

// --------------------------------------------------------------------------- //
// create_pcg_graph_parameter
// --------------------------------------------------------------------------- //
TSharedPtr<FJsonObject> FUnrealAIAssetCommands::HandleCreatePCGGraphParameter(const TSharedPtr<FJsonObject>& Params)
{
    FString AssetPath;
    FString LoadError;
    UPCGGraph* Graph = LoadPCGEditableGraphAsset(Params, AssetPath, LoadError);
    if (!Graph)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(LoadError);
    }

    FString ParameterName;
    if (!Params->TryGetStringField(TEXT("parameter_name"), ParameterName) || ParameterName.TrimStartAndEnd().IsEmpty())
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing or empty 'parameter_name' parameter"));
    }
    ParameterName = ParameterName.TrimStartAndEnd();

    FString ParameterType;
    if (!Params->TryGetStringField(TEXT("parameter_type"), ParameterType) || ParameterType.TrimStartAndEnd().IsEmpty())
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing or empty 'parameter_type' parameter"));
    }

    EPropertyBagPropertyType ParsedType = EPropertyBagPropertyType::None;
    FString CanonicalType;
    FString ParseError;
    if (!TryParsePCGGraphParameterType(ParameterType, ParsedType, CanonicalType, ParseError))
    {
        return FUnrealAICommonUtils::CreateErrorResponse(ParseError);
    }

    Graph->Modify();
    const EPropertyBagAlterationResult AddResult = Graph->AddUserParameters({
        FPropertyBagPropertyDesc(FName(*ParameterName), ParsedType)
    });
    if (AddResult != EPropertyBagAlterationResult::Success)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Failed to create graph parameter '%s' (result: %s)"),
                *ParameterName,
                *GetEnumValueName(StaticEnum<EPropertyBagAlterationResult>(), static_cast<int64>(AddResult))));
    }

    if (const TSharedPtr<FJsonValue>* DefaultValue = Params->Values.Find(TEXT("default_value")))
    {
        FString SetError;
        const FPropertyBagPropertyDesc* PropertyDesc = ResolvePCGGraphParameterDesc(Graph, ParameterName, SetError);
        if (!PropertyDesc || !TrySetPCGGraphParameterValue(Graph, PropertyDesc, *DefaultValue, SetError))
        {
            return FUnrealAICommonUtils::CreateErrorResponse(SetError);
        }
    }

    Graph->MarkPackageDirty();
    UEditorAssetLibrary::SaveAsset(AssetPath, /*bOnlyIfIsDirty=*/false);

    TSharedPtr<FJsonObject> Result = BuildPCGGraphSummary(Graph, AssetPath);
    Result->SetStringField(TEXT("parameter_name"), ParameterName);
    Result->SetStringField(TEXT("parameter_type"), CanonicalType);
    Result->SetBoolField(TEXT("created"), true);
    Result->SetBoolField(TEXT("success"), true);
    return Result;
}

// --------------------------------------------------------------------------- //
// delete_pcg_graph_parameter
// --------------------------------------------------------------------------- //
TSharedPtr<FJsonObject> FUnrealAIAssetCommands::HandleDeletePCGGraphParameter(const TSharedPtr<FJsonObject>& Params)
{
    FString AssetPath;
    FString LoadError;
    UPCGGraph* Graph = LoadPCGEditableGraphAsset(Params, AssetPath, LoadError);
    if (!Graph)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(LoadError);
    }

    FString ParameterName;
    if (!Params->TryGetStringField(TEXT("parameter_name"), ParameterName) || ParameterName.TrimStartAndEnd().IsEmpty())
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing or empty 'parameter_name' parameter"));
    }
    ParameterName = ParameterName.TrimStartAndEnd();

    FString ResolveError;
    if (!ResolvePCGGraphParameterDesc(Graph, ParameterName, ResolveError))
    {
        return FUnrealAICommonUtils::CreateErrorResponse(ResolveError);
    }

    Graph->Modify();
    EPropertyBagAlterationResult DeleteResult = EPropertyBagAlterationResult::InternalError;
    Graph->UpdateUserParametersStruct([&ParameterName, &DeleteResult](FInstancedPropertyBag& UserParameters)
    {
        DeleteResult = UserParameters.RemovePropertyByName(FName(*ParameterName));
    });

    if (DeleteResult != EPropertyBagAlterationResult::Success)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Failed to delete graph parameter '%s' (result: %s)"),
                *ParameterName,
                *GetEnumValueName(StaticEnum<EPropertyBagAlterationResult>(), static_cast<int64>(DeleteResult))));
    }

    Graph->MarkPackageDirty();
    UEditorAssetLibrary::SaveAsset(AssetPath, /*bOnlyIfIsDirty=*/false);

    TSharedPtr<FJsonObject> Result = BuildPCGGraphSummary(Graph, AssetPath);
    Result->SetStringField(TEXT("parameter_name"), ParameterName);
    Result->SetBoolField(TEXT("deleted"), true);
    Result->SetBoolField(TEXT("success"), true);
    return Result;
}

// --------------------------------------------------------------------------- //
// rename_pcg_graph_parameter
// --------------------------------------------------------------------------- //
TSharedPtr<FJsonObject> FUnrealAIAssetCommands::HandleRenamePCGGraphParameter(const TSharedPtr<FJsonObject>& Params)
{
    FString AssetPath;
    FString LoadError;
    UPCGGraph* Graph = LoadPCGEditableGraphAsset(Params, AssetPath, LoadError);
    if (!Graph)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(LoadError);
    }

    FString CurrentName;
    if (!Params->TryGetStringField(TEXT("current_name"), CurrentName) || CurrentName.TrimStartAndEnd().IsEmpty())
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing or empty 'current_name' parameter"));
    }
    CurrentName = CurrentName.TrimStartAndEnd();

    FString NewName;
    if (!Params->TryGetStringField(TEXT("new_name"), NewName) || NewName.TrimStartAndEnd().IsEmpty())
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing or empty 'new_name' parameter"));
    }
    NewName = NewName.TrimStartAndEnd();

    Graph->Modify();
    const EPropertyBagAlterationResult RenameResult = Graph->RenameUserParameter(FName(*CurrentName), FName(*NewName));
    if (RenameResult != EPropertyBagAlterationResult::Success)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Failed to rename graph parameter '%s' to '%s' (result: %s)"),
                *CurrentName,
                *NewName,
                *GetEnumValueName(StaticEnum<EPropertyBagAlterationResult>(), static_cast<int64>(RenameResult))));
    }

    Graph->MarkPackageDirty();
    UEditorAssetLibrary::SaveAsset(AssetPath, /*bOnlyIfIsDirty=*/false);

    TSharedPtr<FJsonObject> Result = BuildPCGGraphSummary(Graph, AssetPath);
    Result->SetStringField(TEXT("previous_name"), CurrentName);
    Result->SetStringField(TEXT("parameter_name"), NewName);
    Result->SetBoolField(TEXT("renamed"), true);
    Result->SetBoolField(TEXT("success"), true);
    return Result;
}

// --------------------------------------------------------------------------- //
// set_pcg_graph_parameter
// --------------------------------------------------------------------------- //
TSharedPtr<FJsonObject> FUnrealAIAssetCommands::HandleSetPCGGraphParameter(const TSharedPtr<FJsonObject>& Params)
{
    FString AssetPath;
    FString LoadError;
    UPCGGraphInterface* GraphInterface = LoadPCGGraphInterfaceAsset(Params, AssetPath, LoadError);
    if (!GraphInterface)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(LoadError);
    }

    FString ParameterName;
    if (!Params->TryGetStringField(TEXT("parameter_name"), ParameterName) || ParameterName.TrimStartAndEnd().IsEmpty())
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing or empty 'parameter_name' parameter"));
    }
    ParameterName = ParameterName.TrimStartAndEnd();

    const TSharedPtr<FJsonValue>* ValueField = Params->Values.Find(TEXT("value"));
    if (!ValueField || !ValueField->IsValid())
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing 'value' parameter"));
    }

    FString ResolveError;
    const FPropertyBagPropertyDesc* PropertyDesc = ResolvePCGGraphParameterDesc(GraphInterface, ParameterName, ResolveError);
    if (!PropertyDesc)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(ResolveError);
    }

    GraphInterface->Modify();
    if (UPCGGraphInstance* GraphInstance = Cast<UPCGGraphInstance>(GraphInterface))
    {
        if (PropertyDesc->CachedProperty && !GraphInstance->IsPropertyOverridden(PropertyDesc->CachedProperty))
        {
            GraphInstance->UpdatePropertyOverride(PropertyDesc->CachedProperty, true);
        }
    }

    FString SetError;
    if (!TrySetPCGGraphParameterValue(GraphInterface, PropertyDesc, *ValueField, SetError))
    {
        return FUnrealAICommonUtils::CreateErrorResponse(SetError);
    }

    GraphInterface->MarkPackageDirty();
    UEditorAssetLibrary::SaveAsset(AssetPath, /*bOnlyIfIsDirty=*/false);

    TSharedPtr<FJsonObject> Result = BuildPCGGraphSummary(GraphInterface, AssetPath);
    Result->SetStringField(TEXT("parameter_name"), ParameterName);
    Result->SetBoolField(TEXT("updated"), true);
    Result->SetBoolField(TEXT("success"), true);
    return Result;
}

// --------------------------------------------------------------------------- //
// reset_pcg_graph_parameter_override
// --------------------------------------------------------------------------- //
TSharedPtr<FJsonObject> FUnrealAIAssetCommands::HandleResetPCGGraphParameterOverride(const TSharedPtr<FJsonObject>& Params)
{
    FString AssetPath;
    FString LoadError;
    UPCGGraphInterface* GraphInterface = LoadPCGGraphInterfaceAsset(Params, AssetPath, LoadError);
    if (!GraphInterface)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(LoadError);
    }

    UPCGGraphInstance* GraphInstance = Cast<UPCGGraphInstance>(GraphInterface);
    if (!GraphInstance)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("reset_pcg_graph_parameter_override requires a PCGGraphInstance asset_path"));
    }

    FString ParameterName;
    if (!Params->TryGetStringField(TEXT("parameter_name"), ParameterName) || ParameterName.TrimStartAndEnd().IsEmpty())
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing or empty 'parameter_name' parameter"));
    }
    ParameterName = ParameterName.TrimStartAndEnd();

    FString ResolveError;
    const FPropertyBagPropertyDesc* PropertyDesc = ResolvePCGGraphParameterDesc(GraphInstance, ParameterName, ResolveError);
    if (!PropertyDesc)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(ResolveError);
    }

    const bool bWasOverridden = PropertyDesc->CachedProperty && GraphInstance->IsPropertyOverridden(PropertyDesc->CachedProperty);
    GraphInstance->Modify();
    GraphInstance->UpdatePropertyOverride(PropertyDesc->CachedProperty, false);
    GraphInstance->MarkPackageDirty();
    UEditorAssetLibrary::SaveAsset(AssetPath, /*bOnlyIfIsDirty=*/false);

    TSharedPtr<FJsonObject> Result = BuildPCGGraphSummary(GraphInstance, AssetPath);
    Result->SetStringField(TEXT("parameter_name"), ParameterName);
    Result->SetBoolField(TEXT("overridden_before"), bWasOverridden);
    Result->SetBoolField(TEXT("overridden_after"), PropertyDesc->CachedProperty && GraphInstance->IsPropertyOverridden(PropertyDesc->CachedProperty));
    Result->SetBoolField(TEXT("reset"), true);
    Result->SetBoolField(TEXT("success"), true);
    return Result;
}

// --------------------------------------------------------------------------- //
// read_niagara_system_content
// --------------------------------------------------------------------------- //
TSharedPtr<FJsonObject> FUnrealAIAssetCommands::HandleReadNiagaraSystemContent(const TSharedPtr<FJsonObject>& Params)
{
    FString AssetPath;
    if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath) || AssetPath.TrimStartAndEnd().IsEmpty())
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing or empty 'asset_path' parameter"));
    }
    AssetPath = AssetPath.TrimStartAndEnd();

    UObject* LoadedObject = UEditorAssetLibrary::LoadAsset(AssetPath);
    UNiagaraSystem* NiagaraSystem = Cast<UNiagaraSystem>(LoadedObject);
    if (!NiagaraSystem)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Niagara System not found or wrong class: %s"), *AssetPath));
    }

    const UNiagaraScript* SystemSpawnScript = NiagaraSystem->GetSystemSpawnScript();
    const UNiagaraScript* SystemUpdateScript = NiagaraSystem->GetSystemUpdateScript();

    TArray<TSharedPtr<FJsonValue>> ExposedParameterArray;
    TArray<FNiagaraVariable> ExposedParameters;
    NiagaraSystem->GetExposedParameters().GetParameters(ExposedParameters);
    ExposedParameterArray = BuildNiagaraParameterArray(ExposedParameters, &NiagaraSystem->GetExposedParameters());

    TArray<TSharedPtr<FJsonValue>> EmitterArray;
    int32 EnabledEmitterCount = 0;
    for (const FNiagaraEmitterHandle& EmitterHandle : NiagaraSystem->GetEmitterHandles())
    {
        if (!EmitterHandle.IsValid())
        {
            continue;
        }

        const bool bEmitterEnabled = EmitterHandle.GetIsEnabled();
        if (bEmitterEnabled)
        {
            ++EnabledEmitterCount;
        }
        EmitterArray.Add(MakeShared<FJsonValueObject>(BuildNiagaraEmitterSummary(*NiagaraSystem, EmitterHandle)));
    }

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("asset_path"), AssetPath);
    Result->SetStringField(TEXT("name"), NiagaraSystem->GetName());
    Result->SetStringField(TEXT("package_name"), NiagaraSystem->GetOutermost() ? NiagaraSystem->GetOutermost()->GetName() : TEXT(""));
    Result->SetBoolField(TEXT("is_ready_to_run"), NiagaraSystem->IsReadyToRun());
    Result->SetBoolField(TEXT("needs_request_compile"), NiagaraSystem->NeedsRequestCompile());
    Result->SetBoolField(TEXT("has_system_spawn_script"), SystemSpawnScript != nullptr);
    Result->SetBoolField(TEXT("has_system_update_script"), SystemUpdateScript != nullptr);
    Result->SetStringField(TEXT("system_spawn_script_name"), SystemSpawnScript ? SystemSpawnScript->GetName() : TEXT(""));
    Result->SetStringField(TEXT("system_spawn_script_path"), SystemSpawnScript ? SystemSpawnScript->GetPathName() : TEXT(""));
    Result->SetStringField(TEXT("system_update_script_name"), SystemUpdateScript ? SystemUpdateScript->GetName() : TEXT(""));
    Result->SetStringField(TEXT("system_update_script_path"), SystemUpdateScript ? SystemUpdateScript->GetPathName() : TEXT(""));
    Result->SetObjectField(TEXT("system_spawn_script"), BuildNiagaraScriptSummary(SystemSpawnScript).ToSharedRef());
    Result->SetObjectField(TEXT("system_update_script"), BuildNiagaraScriptSummary(SystemUpdateScript).ToSharedRef());
    Result->SetArrayField(TEXT("exposed_user_parameters"), ExposedParameterArray);
    Result->SetNumberField(TEXT("exposed_user_parameter_count"), ExposedParameterArray.Num());
    Result->SetArrayField(TEXT("emitters"), EmitterArray);
    Result->SetNumberField(TEXT("emitter_count"), EmitterArray.Num());
    Result->SetNumberField(TEXT("enabled_emitter_count"), EnabledEmitterCount);
    return Result;
}

// --------------------------------------------------------------------------- //
// set_niagara_system_user_parameters
// --------------------------------------------------------------------------- //
TSharedPtr<FJsonObject> FUnrealAIAssetCommands::HandleSetNiagaraSystemUserParameters(const TSharedPtr<FJsonObject>& Params)
{
    FString AssetPath;
    if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath) || AssetPath.TrimStartAndEnd().IsEmpty())
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing or empty 'asset_path' parameter"));
    }
    AssetPath = AssetPath.TrimStartAndEnd();

    const TArray<TSharedPtr<FJsonValue>>* ParameterUpdates = nullptr;
    if (!Params->TryGetArrayField(TEXT("parameters"), ParameterUpdates) || !ParameterUpdates || ParameterUpdates->Num() == 0)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing or empty 'parameters' array parameter"));
    }

    bool bCreateIfMissing = false;
    Params->TryGetBoolField(TEXT("create_if_missing"), bCreateIfMissing);

    UNiagaraSystem* NiagaraSystem = Cast<UNiagaraSystem>(UEditorAssetLibrary::LoadAsset(AssetPath));
    if (!NiagaraSystem)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Niagara System not found or wrong class: %s"), *AssetPath));
    }

    NiagaraSystem->Modify();
    FNiagaraUserRedirectionParameterStore& ExposedParameters = NiagaraSystem->GetExposedParameters();

    TArray<FString> UpdatedParameterNames;
    TArray<FString> CreatedParameterNames;

    for (int32 ParameterIndex = 0; ParameterIndex < ParameterUpdates->Num(); ++ParameterIndex)
    {
        const TSharedPtr<FJsonValue>& ParameterUpdateValue = (*ParameterUpdates)[ParameterIndex];
        if (!ParameterUpdateValue.IsValid() || ParameterUpdateValue->Type != EJson::Object)
        {
            return FUnrealAICommonUtils::CreateErrorResponse(
                FString::Printf(TEXT("Parameter update at index %d must be a JSON object"), ParameterIndex));
        }

        const TSharedPtr<FJsonObject> ParameterUpdateObject = ParameterUpdateValue->AsObject();
        if (!ParameterUpdateObject.IsValid())
        {
            return FUnrealAICommonUtils::CreateErrorResponse(
                FString::Printf(TEXT("Parameter update at index %d must be a JSON object"), ParameterIndex));
        }

        FString ParameterName;
        if (!ParameterUpdateObject->TryGetStringField(TEXT("name"), ParameterName) || ParameterName.TrimStartAndEnd().IsEmpty())
        {
            return FUnrealAICommonUtils::CreateErrorResponse(
                FString::Printf(TEXT("Parameter update at index %d is missing a non-empty 'name' field"), ParameterIndex));
        }
        ParameterName = ParameterName.TrimStartAndEnd();

        FString RequestedTypeName;
        ParameterUpdateObject->TryGetStringField(TEXT("type"), RequestedTypeName);
        RequestedTypeName = RequestedTypeName.TrimStartAndEnd();

        FNiagaraVariable ParameterVariable;
        const bool bParameterExists = ResolveNiagaraUserParameter(ExposedParameters, ParameterName, ParameterVariable);
        if (!bParameterExists)
        {
            if (!bCreateIfMissing)
            {
                return FUnrealAICommonUtils::CreateErrorResponse(
                    FString::Printf(TEXT("Failed to resolve exposed Niagara user parameter: %s"), *ParameterName));
            }

            if (RequestedTypeName.IsEmpty())
            {
                return FUnrealAICommonUtils::CreateErrorResponse(
                    FString::Printf(TEXT("Parameter '%s' is missing from the system; provide 'type' when create_if_missing is true"), *ParameterName));
            }

            FNiagaraTypeDefinition CreatedTypeDefinition;
            FString CanonicalTypeName;
            if (!TryResolveSupportedNiagaraUserParameterType(RequestedTypeName, CreatedTypeDefinition, CanonicalTypeName))
            {
                return FUnrealAICommonUtils::CreateErrorResponse(
                    FString::Printf(TEXT("Unsupported Niagara user parameter type for '%s': %s"), *ParameterName, *RequestedTypeName));
            }

            ParameterVariable = FNiagaraVariable(CreatedTypeDefinition, FName(*ParameterName));
            FNiagaraUserRedirectionParameterStore::MakeUserVariable(ParameterVariable);
            if (!ExposedParameters.AddParameter(ParameterVariable, /*bInitialize=*/true, /*bTriggerRebind=*/true))
            {
                return FUnrealAICommonUtils::CreateErrorResponse(
                    FString::Printf(TEXT("Failed to create Niagara user parameter: %s"), *ParameterName));
            }
            CreatedParameterNames.Add(ParameterName);
        }
        else if (!RequestedTypeName.IsEmpty())
        {
            FNiagaraTypeDefinition RequestedTypeDefinition;
            FString CanonicalTypeName;
            if (!TryResolveSupportedNiagaraUserParameterType(RequestedTypeName, RequestedTypeDefinition, CanonicalTypeName))
            {
                return FUnrealAICommonUtils::CreateErrorResponse(
                    FString::Printf(TEXT("Unsupported Niagara user parameter type for '%s': %s"), *ParameterName, *RequestedTypeName));
            }

            const FString ExistingValueKind = GetNiagaraUserParameterValueKind(ParameterVariable.GetType());
            if (ExistingValueKind != CanonicalTypeName && !(ExistingValueKind == TEXT("vector") && CanonicalTypeName == TEXT("vector")))
            {
                return FUnrealAICommonUtils::CreateErrorResponse(
                    FString::Printf(TEXT("Parameter '%s' exists with type '%s', not requested type '%s'"), *ParameterName, *ExistingValueKind, *CanonicalTypeName));
            }
        }

        const FString ValueKind = GetNiagaraUserParameterValueKind(ParameterVariable.GetType());
        if (ValueKind == TEXT("unsupported") || ValueKind == TEXT("data_interface"))
        {
            return FUnrealAICommonUtils::CreateErrorResponse(
                FString::Printf(TEXT("Parameter '%s' has unsupported Niagara user parameter type '%s'"), *ParameterName, *ParameterVariable.GetType().GetNameText().ToString()));
        }

        if (ValueKind == TEXT("float"))
        {
            double NumericValue = 0.0;
            if (!ParameterUpdateObject->TryGetNumberField(TEXT("value"), NumericValue))
            {
                return FUnrealAICommonUtils::CreateErrorResponse(
                    FString::Printf(TEXT("Parameter '%s' requires a numeric 'value' field"), *ParameterName));
            }

            ExposedParameters.SetParameterValue<float>(static_cast<float>(NumericValue), ParameterVariable, /*bAdd=*/false);
        }
        else if (ValueKind == TEXT("bool"))
        {
            bool bBoolValue = false;
            if (!ParameterUpdateObject->TryGetBoolField(TEXT("value"), bBoolValue))
            {
                return FUnrealAICommonUtils::CreateErrorResponse(
                    FString::Printf(TEXT("Parameter '%s' requires a boolean 'value' field"), *ParameterName));
            }

            ExposedParameters.SetParameterValue<FNiagaraBool>(FNiagaraBool(bBoolValue), ParameterVariable, /*bAdd=*/false);
        }
        else if (ValueKind == TEXT("vector"))
        {
            TArray<double> VectorValues;
            if (!TryReadJsonNumberArrayField(ParameterUpdateObject, TEXT("value"), 3, VectorValues))
            {
                return FUnrealAICommonUtils::CreateErrorResponse(
                    FString::Printf(TEXT("Parameter '%s' requires a 3-number array 'value' field"), *ParameterName));
            }

            if (ParameterVariable.GetType() == FNiagaraTypeDefinition::GetPositionDef())
            {
                ExposedParameters.SetParameterValue<FNiagaraPosition>(
                    FNiagaraPosition(static_cast<float>(VectorValues[0]), static_cast<float>(VectorValues[1]), static_cast<float>(VectorValues[2])),
                    ParameterVariable,
                    /*bAdd=*/false);
            }
            else
            {
                ExposedParameters.SetParameterValue<FVector3f>(
                    FVector3f(static_cast<float>(VectorValues[0]), static_cast<float>(VectorValues[1]), static_cast<float>(VectorValues[2])),
                    ParameterVariable,
                    /*bAdd=*/false);
            }
        }
        else if (ValueKind == TEXT("color"))
        {
            TArray<double> ColorValues;
            if (!TryReadJsonNumberArrayField(ParameterUpdateObject, TEXT("value"), 4, ColorValues))
            {
                return FUnrealAICommonUtils::CreateErrorResponse(
                    FString::Printf(TEXT("Parameter '%s' requires a 4-number array 'value' field"), *ParameterName));
            }

            ExposedParameters.SetParameterValue<FLinearColor>(
                FLinearColor(
                    static_cast<float>(ColorValues[0]),
                    static_cast<float>(ColorValues[1]),
                    static_cast<float>(ColorValues[2]),
                    static_cast<float>(ColorValues[3])),
                ParameterVariable,
                /*bAdd=*/false);
        }
        else if (ValueKind == TEXT("object"))
        {
            FString ObjectPath;
            if (!ParameterUpdateObject->TryGetStringField(TEXT("object_path"), ObjectPath) || ObjectPath.TrimStartAndEnd().IsEmpty())
            {
                return FUnrealAICommonUtils::CreateErrorResponse(
                    FString::Printf(TEXT("Parameter '%s' requires a non-empty 'object_path' field"), *ParameterName));
            }
            ObjectPath = ObjectPath.TrimStartAndEnd();

            UObject* ObjectValue = UEditorAssetLibrary::LoadAsset(ObjectPath);
            if (!ObjectValue)
            {
                return FUnrealAICommonUtils::CreateErrorResponse(
                    FString::Printf(TEXT("Failed to load object asset for parameter '%s': %s"), *ParameterName, *ObjectPath));
            }

            const UClass* ExpectedClass = ParameterVariable.GetType().GetClass();
            if (ExpectedClass && !ObjectValue->IsA(ExpectedClass))
            {
                return FUnrealAICommonUtils::CreateErrorResponse(
                    FString::Printf(TEXT("Object asset for parameter '%s' has class '%s' but expected '%s'"), *ParameterName, *ObjectValue->GetClass()->GetName(), *ExpectedClass->GetName()));
            }

            ExposedParameters.SetUObject(ObjectValue, ParameterVariable);
        }

        UpdatedParameterNames.Add(ParameterName);
    }

    NiagaraSystem->RequestCompile(false);
    NiagaraSystem->WaitForCompilationComplete(false, false);
    NiagaraSystem->MarkPackageDirty();
    UEditorAssetLibrary::SaveAsset(AssetPath, /*bOnlyIfIsDirty=*/false);

    TSharedPtr<FJsonObject> ReadParams = MakeShared<FJsonObject>();
    ReadParams->SetStringField(TEXT("asset_path"), AssetPath);

    TSharedPtr<FJsonObject> Result = HandleReadNiagaraSystemContent(ReadParams);
    Result->SetStringField(TEXT("asset_path"), AssetPath);
    Result->SetBoolField(TEXT("success"), true);
    Result->SetBoolField(TEXT("updated"), true);
    Result->SetBoolField(TEXT("create_if_missing"), bCreateIfMissing);
    Result->SetNumberField(TEXT("updated_parameter_count"), UpdatedParameterNames.Num());
    Result->SetNumberField(TEXT("created_parameter_count"), CreatedParameterNames.Num());
    Result->SetArrayField(TEXT("updated_parameter_names"), BuildStringArray(UpdatedParameterNames));
    Result->SetArrayField(TEXT("created_parameter_names"), BuildStringArray(CreatedParameterNames));
    return Result;
}

// --------------------------------------------------------------------------- //
// validate_niagara_system
// --------------------------------------------------------------------------- //
TSharedPtr<FJsonObject> FUnrealAIAssetCommands::HandleValidateNiagaraSystem(const TSharedPtr<FJsonObject>& Params)
{
    FString AssetPath;
    if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath) || AssetPath.TrimStartAndEnd().IsEmpty())
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing or empty 'asset_path' parameter"));
    }
    AssetPath = AssetPath.TrimStartAndEnd();

    UNiagaraSystem* NiagaraSystem = Cast<UNiagaraSystem>(UEditorAssetLibrary::LoadAsset(AssetPath));
    if (!NiagaraSystem)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Niagara System not found or wrong class: %s"), *AssetPath));
    }

    NiagaraSystem->RequestCompile(false);
    NiagaraSystem->WaitForCompilationComplete(false, false);

    TSharedPtr<FJsonObject> ReadParams = MakeShared<FJsonObject>();
    ReadParams->SetStringField(TEXT("asset_path"), AssetPath);

    TSharedPtr<FJsonObject> Readback = HandleReadNiagaraSystemContent(ReadParams);
    if (!Readback.IsValid())
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Failed to read Niagara System after validation compile"));
    }
    if (Readback->HasField(TEXT("error")))
    {
        return Readback;
    }

    TSharedPtr<FJsonObject> Result = CopyJsonObject(Readback);
    TArray<TSharedPtr<FJsonValue>> Issues;
    int32 ErrorCount = 0;
    int32 WarningCount = 0;

    if (!Result->GetBoolField(TEXT("has_system_spawn_script")))
    {
        AddValidationIssue(Issues, ErrorCount, WarningCount, TEXT("error"), TEXT("missing_system_spawn_script"), TEXT("Niagara System is missing a system spawn script"), TEXT("system_spawn_script"));
    }
    if (!Result->GetBoolField(TEXT("has_system_update_script")))
    {
        AddValidationIssue(Issues, ErrorCount, WarningCount, TEXT("error"), TEXT("missing_system_update_script"), TEXT("Niagara System is missing a system update script"), TEXT("system_update_script"));
    }

    auto AddScriptIssues = [&](const TCHAR* FieldName, const TCHAR* Label)
    {
        const TSharedPtr<FJsonObject>* ScriptObject = nullptr;
        if (!Result->TryGetObjectField(FieldName, ScriptObject) || !ScriptObject || !ScriptObject->IsValid())
        {
            return;
        }

        const FString CompileStatus = (*ScriptObject)->GetStringField(TEXT("compile_status"));
        if (CompileStatus == TEXT("NCS_Error"))
        {
            AddValidationIssue(
                Issues,
                ErrorCount,
                WarningCount,
                TEXT("error"),
                TEXT("script_compile_error"),
                FString::Printf(TEXT("%s compile status is %s"), Label, *CompileStatus),
                FieldName);
        }
        else if (!IsNiagaraCompileStatusHealthy(CompileStatus) || IsNiagaraCompileStatusWarningOnly(CompileStatus))
        {
            AddValidationIssue(
                Issues,
                ErrorCount,
                WarningCount,
                TEXT("warning"),
                TEXT("script_compile_status_not_clean"),
                FString::Printf(TEXT("%s compile status is %s"), Label, *CompileStatus),
                FieldName);
        }

        bool bScriptAndSourceSynchronized = true;
        if ((*ScriptObject)->TryGetBoolField(TEXT("script_and_source_synchronized"), bScriptAndSourceSynchronized) && !bScriptAndSourceSynchronized)
        {
            AddValidationIssue(
                Issues,
                ErrorCount,
                WarningCount,
                TEXT("warning"),
                TEXT("script_source_out_of_sync"),
                FString::Printf(TEXT("%s is not synchronized with its source graph"), Label),
                FieldName);
        }
    };

    AddScriptIssues(TEXT("system_spawn_script"), TEXT("System spawn script"));
    AddScriptIssues(TEXT("system_update_script"), TEXT("System update script"));

    if (!Result->GetBoolField(TEXT("is_ready_to_run")))
    {
        AddValidationIssue(Issues, ErrorCount, WarningCount, TEXT("error"), TEXT("system_not_ready_to_run"), TEXT("Niagara System is not ready to run after compile"), TEXT("is_ready_to_run"));
    }
    if (Result->GetBoolField(TEXT("needs_request_compile")))
    {
        AddValidationIssue(Issues, ErrorCount, WarningCount, TEXT("warning"), TEXT("system_needs_request_compile"), TEXT("Niagara System still reports that it needs a compile request after validation"), TEXT("needs_request_compile"));
    }

    const int32 EmitterCount = static_cast<int32>(Result->GetNumberField(TEXT("emitter_count")));
    const int32 EnabledEmitterCount = static_cast<int32>(Result->GetNumberField(TEXT("enabled_emitter_count")));
    if (EmitterCount == 0)
    {
        AddValidationIssue(Issues, ErrorCount, WarningCount, TEXT("warning"), TEXT("no_emitters"), TEXT("Niagara System has no emitter handles"), TEXT("emitters"));
    }
    else if (EnabledEmitterCount == 0)
    {
        AddValidationIssue(Issues, ErrorCount, WarningCount, TEXT("warning"), TEXT("all_emitters_disabled"), TEXT("Niagara System has emitter handles but none are enabled"), TEXT("emitters"));
    }

    TSet<FString> ExposedParameterNames;
    const TArray<TSharedPtr<FJsonValue>>* ExposedParameters = nullptr;
    if (Result->TryGetArrayField(TEXT("exposed_user_parameters"), ExposedParameters) && ExposedParameters)
    {
        for (const TSharedPtr<FJsonValue>& ExposedParameterValue : *ExposedParameters)
        {
            if (!ExposedParameterValue.IsValid() || ExposedParameterValue->Type != EJson::Object)
            {
                continue;
            }

            const TSharedPtr<FJsonObject> ExposedParameterObject = ExposedParameterValue->AsObject();
            const FString FullName = ExposedParameterObject->GetStringField(TEXT("name")).ToLower();
            const FString DisplayName = ExposedParameterObject->GetStringField(TEXT("display_name")).ToLower();
            ExposedParameterNames.Add(FullName);
            if (!DisplayName.IsEmpty())
            {
                ExposedParameterNames.Add(DisplayName);
            }

            const FString ValueKind = ExposedParameterObject->GetStringField(TEXT("value_kind"));
            if (ValueKind == TEXT("object"))
            {
                const bool bIsNullObjectValue = ExposedParameterObject->GetBoolField(TEXT("is_null_object_value"));
                const FString DisplayParameterName = ExposedParameterObject->GetStringField(TEXT("display_name"));
                const FString ObjectPath = ExposedParameterObject->GetStringField(TEXT("object_path"));
                if (bIsNullObjectValue)
                {
                    AddValidationIssue(
                        Issues,
                        ErrorCount,
                        WarningCount,
                        TEXT("error"),
                        TEXT("missing_object_reference"),
                        FString::Printf(TEXT("Object-backed user parameter '%s' does not currently reference an asset"), *DisplayParameterName),
                        TEXT("exposed_user_parameters"));
                }
                else if (!ObjectPath.IsEmpty() && !UEditorAssetLibrary::DoesAssetExist(ObjectPath))
                {
                    AddValidationIssue(
                        Issues,
                        ErrorCount,
                        WarningCount,
                        TEXT("error"),
                        TEXT("stale_object_reference"),
                        FString::Printf(TEXT("Object-backed user parameter '%s' references a missing asset: %s"), *DisplayParameterName, *ObjectPath),
                        TEXT("exposed_user_parameters"));
                }
            }
        }
    }

    const TArray<TSharedPtr<FJsonValue>>* Emitters = nullptr;
    if (Result->TryGetArrayField(TEXT("emitters"), Emitters) && Emitters)
    {
        for (const TSharedPtr<FJsonValue>& EmitterValue : *Emitters)
        {
            if (!EmitterValue.IsValid() || EmitterValue->Type != EJson::Object)
            {
                continue;
            }

            const TSharedPtr<FJsonObject> EmitterObject = EmitterValue->AsObject();
            const FString EmitterName = EmitterObject->GetStringField(TEXT("name"));
            if (EmitterObject->GetBoolField(TEXT("needs_recompile")))
            {
                AddValidationIssue(
                    Issues,
                    ErrorCount,
                    WarningCount,
                    TEXT("warning"),
                    TEXT("emitter_needs_recompile"),
                    FString::Printf(TEXT("Emitter '%s' still reports that it needs recompile after validation"), *EmitterName),
                    TEXT("emitters"));
            }

            const TArray<TSharedPtr<FJsonValue>>* RendererBindings = nullptr;
            if (!EmitterObject->TryGetArrayField(TEXT("renderer_binding_parameters"), RendererBindings) || !RendererBindings)
            {
                continue;
            }

            for (const TSharedPtr<FJsonValue>& RendererBindingValue : *RendererBindings)
            {
                if (!RendererBindingValue.IsValid() || RendererBindingValue->Type != EJson::Object)
                {
                    continue;
                }

                const TSharedPtr<FJsonObject> RendererBindingObject = RendererBindingValue->AsObject();
                const FString BindingName = RendererBindingObject->GetStringField(TEXT("name"));
                if (!BindingName.StartsWith(TEXT("User."), ESearchCase::IgnoreCase))
                {
                    continue;
                }

                const FString BindingFullName = BindingName.ToLower();
                const FString BindingDisplayName = BindingName.RightChop(5).ToLower();
                if (!ExposedParameterNames.Contains(BindingFullName) && !ExposedParameterNames.Contains(BindingDisplayName))
                {
                    AddValidationIssue(
                        Issues,
                        ErrorCount,
                        WarningCount,
                        TEXT("error"),
                        TEXT("unresolved_parameter_binding"),
                        FString::Printf(TEXT("Emitter '%s' has a renderer binding for unresolved user parameter '%s'"), *EmitterName, *BindingName),
                        TEXT("emitters"));
                }
            }
        }
    }

    Result->SetArrayField(TEXT("issues"), Issues);
    Result->SetNumberField(TEXT("error_count"), ErrorCount);
    Result->SetNumberField(TEXT("warning_count"), WarningCount);
    Result->SetBoolField(TEXT("is_valid"), ErrorCount == 0);
    Result->SetBoolField(TEXT("validated"), true);
    Result->SetBoolField(TEXT("success"), true);
    return Result;
}

// --------------------------------------------------------------------------- //
// read_behavior_tree_content
// --------------------------------------------------------------------------- //
TSharedPtr<FJsonObject> FUnrealAIAssetCommands::HandleReadBehaviorTreeContent(const TSharedPtr<FJsonObject>& Params)
{
    FString AssetPath;
    if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath) || AssetPath.TrimStartAndEnd().IsEmpty())
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing or empty 'asset_path' parameter"));
    }
    AssetPath = AssetPath.TrimStartAndEnd();

    UObject* LoadedObject = UEditorAssetLibrary::LoadAsset(AssetPath);
    UBehaviorTree* BehaviorTree = Cast<UBehaviorTree>(LoadedObject);
    if (!BehaviorTree)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Behavior Tree asset not found or wrong class: %s"), *AssetPath));
    }

    TSharedPtr<FJsonObject> Result = BuildBehaviorTreeContentSummary(*BehaviorTree, AssetPath);
    Result->SetBoolField(TEXT("success"), true);
    return Result;
}

// --------------------------------------------------------------------------- //
// create_behavior_tree_asset
// --------------------------------------------------------------------------- //
TSharedPtr<FJsonObject> FUnrealAIAssetCommands::HandleCreateBehaviorTreeAsset(const TSharedPtr<FJsonObject>& Params)
{
    FString BehaviorTreeName;
    if (!Params->TryGetStringField(TEXT("behavior_tree_name"), BehaviorTreeName) || BehaviorTreeName.TrimStartAndEnd().IsEmpty())
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing or empty 'behavior_tree_name' parameter"));
    }
    BehaviorTreeName = BehaviorTreeName.TrimStartAndEnd();

    FString DestinationPath = TEXT("/Game/BehaviorTrees");
    Params->TryGetStringField(TEXT("destination_path"), DestinationPath);
    DestinationPath = DestinationPath.TrimStartAndEnd();
    if (DestinationPath.IsEmpty())
    {
        DestinationPath = TEXT("/Game/BehaviorTrees");
    }

    FString BlackboardAssetPath;
    Params->TryGetStringField(TEXT("blackboard_asset_path"), BlackboardAssetPath);
    BlackboardAssetPath = BlackboardAssetPath.TrimStartAndEnd();

    UBlackboardData* BlackboardAsset = nullptr;
    if (!BlackboardAssetPath.IsEmpty())
    {
        BlackboardAsset = Cast<UBlackboardData>(UEditorAssetLibrary::LoadAsset(BlackboardAssetPath));
        if (!BlackboardAsset)
        {
            return FUnrealAICommonUtils::CreateErrorResponse(
                FString::Printf(TEXT("Failed to load Blackboard asset: %s"), *BlackboardAssetPath));
        }
    }

    if (!UEditorAssetLibrary::DoesDirectoryExist(DestinationPath))
    {
        UEditorAssetLibrary::MakeDirectory(DestinationPath);
    }

    const FString FullObjectPath = FString::Printf(TEXT("%s/%s.%s"), *DestinationPath, *BehaviorTreeName, *BehaviorTreeName);
    if (UEditorAssetLibrary::DoesAssetExist(FullObjectPath))
    {
        return FUnrealAICommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Asset already exists: %s"), *FullObjectPath));
    }

    FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools"));
    IAssetTools& AssetTools = AssetToolsModule.Get();

    UBehaviorTreeFactory* Factory = NewObject<UBehaviorTreeFactory>();
    Factory->bEditAfterNew = false;

    UObject* NewAsset = AssetTools.CreateAsset(BehaviorTreeName, DestinationPath, UBehaviorTree::StaticClass(), Factory);
    UBehaviorTree* BehaviorTree = Cast<UBehaviorTree>(NewAsset);
    if (!BehaviorTree)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Failed to create Behavior Tree asset"));
    }

    BehaviorTree->SetFlags(RF_Transactional);
    BehaviorTree->Modify();

    if (BlackboardAsset)
    {
        FProperty* BlackboardProperty = FindFProperty<FProperty>(UBehaviorTree::StaticClass(), GET_MEMBER_NAME_CHECKED(UBehaviorTree, BlackboardAsset));
        BehaviorTree->PreEditChange(BlackboardProperty);
        BehaviorTree->BlackboardAsset = BlackboardAsset;
        FPropertyChangedEvent BlackboardChangedEvent(BlackboardProperty, EPropertyChangeType::ValueSet);
        BehaviorTree->PostEditChangeProperty(BlackboardChangedEvent);
    }

    FProperty* RootNodeProperty = FindFProperty<FProperty>(UBehaviorTree::StaticClass(), GET_MEMBER_NAME_CHECKED(UBehaviorTree, RootNode));
    BehaviorTree->PreEditChange(RootNodeProperty);
    UBTComposite_Selector* RootNode = NewObject<UBTComposite_Selector>(BehaviorTree, UBTComposite_Selector::StaticClass(), NAME_None, RF_Transactional);
    RootNode->SetFlags(RF_Transactional);
    BehaviorTree->RootNode = RootNode;
    RootNode->InitializeNode(nullptr, 0, 0, 0);
    RootNode->InitializeFromAsset(*BehaviorTree);
    FPropertyChangedEvent RootNodeChangedEvent(RootNodeProperty, EPropertyChangeType::ValueSet);
    BehaviorTree->PostEditChangeProperty(RootNodeChangedEvent);

    BehaviorTree->MarkPackageDirty();
    UEditorAssetLibrary::SaveAsset(FullObjectPath, /*bOnlyIfIsDirty=*/false);

    TSharedPtr<FJsonObject> ReadParams = MakeShared<FJsonObject>();
    ReadParams->SetStringField(TEXT("asset_path"), FullObjectPath);
    TSharedPtr<FJsonObject> Result = HandleReadBehaviorTreeContent(ReadParams);
    Result->SetStringField(TEXT("behavior_tree_name"), BehaviorTreeName);
    Result->SetStringField(TEXT("destination_path"), DestinationPath);
    Result->SetStringField(TEXT("asset_path"), FullObjectPath);
    Result->SetStringField(TEXT("blackboard_asset_path"), BlackboardAssetPath);
    Result->SetBoolField(TEXT("created"), true);
    Result->SetBoolField(TEXT("success"), true);
    return Result;
}

// --------------------------------------------------------------------------- //
// update_behavior_tree_subtree
// --------------------------------------------------------------------------- //
TSharedPtr<FJsonObject> FUnrealAIAssetCommands::HandleUpdateBehaviorTreeSubtree(const TSharedPtr<FJsonObject>& Params)
{
    FString AssetPath;
    if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath) || AssetPath.TrimStartAndEnd().IsEmpty())
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing or empty 'asset_path' parameter"));
    }
    AssetPath = AssetPath.TrimStartAndEnd();

    const TArray<TSharedPtr<FJsonValue>>* Operations = nullptr;
    if (!Params->TryGetArrayField(TEXT("operations"), Operations) || !Operations || Operations->Num() == 0)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing or empty 'operations' array parameter"));
    }

    UBehaviorTree* BehaviorTree = Cast<UBehaviorTree>(UEditorAssetLibrary::LoadAsset(AssetPath));
    if (!BehaviorTree)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Behavior Tree asset not found or wrong class: %s"), *AssetPath));
    }

    UBehaviorTreeGraph* BehaviorTreeGraph = nullptr;
    FString GraphError;
    if (!EnsureBehaviorTreeGraphForMutation(*BehaviorTree, BehaviorTreeGraph, GraphError))
    {
        return FUnrealAICommonUtils::CreateErrorResponse(GraphError);
    }

    TArray<TSharedPtr<FJsonValue>> AppliedOperations;

    for (int32 OperationIndex = 0; OperationIndex < Operations->Num(); ++OperationIndex)
    {
        const TSharedPtr<FJsonValue>& OperationValue = (*Operations)[OperationIndex];
        if (!OperationValue.IsValid() || OperationValue->Type != EJson::Object)
        {
            return FUnrealAICommonUtils::CreateErrorResponse(
                FString::Printf(TEXT("Operation at index %d must be a JSON object"), OperationIndex));
        }

        const TSharedPtr<FJsonObject> OperationObject = OperationValue->AsObject();
        if (!OperationObject.IsValid())
        {
            return FUnrealAICommonUtils::CreateErrorResponse(
                FString::Printf(TEXT("Operation at index %d must be a JSON object"), OperationIndex));
        }

        FString OperationType;
        if (!OperationObject->TryGetStringField(TEXT("op"), OperationType) || OperationType.TrimStartAndEnd().IsEmpty())
        {
            return FUnrealAICommonUtils::CreateErrorResponse(
                FString::Printf(TEXT("Operation at index %d is missing a non-empty 'op' field"), OperationIndex));
        }
        OperationType = NormalizeBehaviorTreeToken(OperationType);

        TSharedPtr<FJsonObject> AppliedOperation = MakeShared<FJsonObject>();
        AppliedOperation->SetNumberField(TEXT("index"), OperationIndex);
        AppliedOperation->SetStringField(TEXT("op"), OperationType);

        if (OperationType == TEXT("add"))
        {
            FString ParentPath;
            if (!OperationObject->TryGetStringField(TEXT("parent_path"), ParentPath) || ParentPath.TrimStartAndEnd().IsEmpty())
            {
                return FUnrealAICommonUtils::CreateErrorResponse(
                    FString::Printf(TEXT("Add operation at index %d is missing a non-empty 'parent_path' field"), OperationIndex));
            }
            ParentPath = ParentPath.TrimStartAndEnd();

            FString NodeKind;
            if (!OperationObject->TryGetStringField(TEXT("node_kind"), NodeKind) || NodeKind.TrimStartAndEnd().IsEmpty())
            {
                return FUnrealAICommonUtils::CreateErrorResponse(
                    FString::Printf(TEXT("Add operation at index %d is missing a non-empty 'node_kind' field"), OperationIndex));
            }
            NodeKind = NormalizeBehaviorTreeToken(NodeKind);

            FString NodeType;
            if (!OperationObject->TryGetStringField(TEXT("node_type"), NodeType) || NodeType.TrimStartAndEnd().IsEmpty())
            {
                return FUnrealAICommonUtils::CreateErrorResponse(
                    FString::Printf(TEXT("Add operation at index %d is missing a non-empty 'node_type' field"), OperationIndex));
            }
            NodeType = NodeType.TrimStartAndEnd();

            int32 ChildSlot = 0;
            if (const TSharedPtr<FJsonValue>* ChildSlotValue = OperationObject->Values.Find(TEXT("child_slot")))
            {
                if (!(*ChildSlotValue).IsValid() || ((*ChildSlotValue)->Type != EJson::Number))
                {
                    return FUnrealAICommonUtils::CreateErrorResponse(
                        FString::Printf(TEXT("Add operation at index %d has a non-numeric 'child_slot' field"), OperationIndex));
                }
                ChildSlot = static_cast<int32>((*ChildSlotValue)->AsNumber());
                if (ChildSlot < 0)
                {
                    return FUnrealAICommonUtils::CreateErrorResponse(
                        FString::Printf(TEXT("Add operation at index %d has an invalid negative 'child_slot' field"), OperationIndex));
                }
            }

            UClass* RuntimeClass = nullptr;
            FString CanonicalNodeType;
            const bool bSupportedType =
                (NodeKind == TEXT("composite") && TryResolveSupportedBehaviorTreeCompositeType(NodeType, RuntimeClass, CanonicalNodeType)) ||
                (NodeKind == TEXT("task") && TryResolveSupportedBehaviorTreeTaskType(NodeType, RuntimeClass, CanonicalNodeType)) ||
                (NodeKind == TEXT("decorator") && TryResolveSupportedBehaviorTreeDecoratorType(NodeType, RuntimeClass, CanonicalNodeType)) ||
                (NodeKind == TEXT("service") && TryResolveSupportedBehaviorTreeServiceType(NodeType, RuntimeClass, CanonicalNodeType));
            if (!bSupportedType || !RuntimeClass)
            {
                return FUnrealAICommonUtils::CreateErrorResponse(
                    FString::Printf(TEXT("Unsupported Behavior Tree %s type at operation %d: %s"), *NodeKind, OperationIndex, *NodeType));
            }

            FBehaviorTreeResolvedPath ResolvedPath;
            UBehaviorTreeGraphNode* ParentGraphNode = nullptr;
            UBehaviorTreeGraphNode* ParentOwnerNode = nullptr;
            FString ResolveError;
            if (!TryResolveBehaviorTreeGraphNodeForPath(*BehaviorTree, *BehaviorTreeGraph, ParentPath, ResolvedPath, ParentGraphNode, ParentOwnerNode, ResolveError))
            {
                return FUnrealAICommonUtils::CreateErrorResponse(ResolveError);
            }

            if (NodeKind == TEXT("composite") || NodeKind == TEXT("task"))
            {
                if (ResolvedPath.Kind != FBehaviorTreeResolvedPath::ETargetKind::Composite)
                {
                    return FUnrealAICommonUtils::CreateErrorResponse(
                        FString::Printf(TEXT("Add child operation at index %d requires a composite 'parent_path': %s"), OperationIndex, *ParentPath));
                }

                UBehaviorTreeGraphNode* NewGraphNode = nullptr;
                if (NodeKind == TEXT("task"))
                {
                    NewGraphNode = CreateBehaviorTreeGraphChildNode<UBehaviorTreeGraphNode_Task>(*BehaviorTreeGraph, RuntimeClass);
                }
                else if (CanonicalNodeType == TEXT("simple_parallel"))
                {
                    NewGraphNode = CreateBehaviorTreeGraphChildNode<UBehaviorTreeGraphNode_SimpleParallel>(*BehaviorTreeGraph, RuntimeClass);
                }
                else
                {
                    NewGraphNode = CreateBehaviorTreeGraphChildNode<UBehaviorTreeGraphNode_Composite>(*BehaviorTreeGraph, RuntimeClass);
                }

                UEdGraphPin* ParentOutputPin = ParentGraphNode ? ParentGraphNode->GetOutputPin(ChildSlot) : nullptr;
                UEdGraphPin* ChildInputPin = NewGraphNode ? NewGraphNode->GetInputPin() : nullptr;
                if (!ParentOutputPin || !ChildInputPin)
                {
                    return FUnrealAICommonUtils::CreateErrorResponse(
                        FString::Printf(TEXT("Failed to resolve graph pins for add child operation at index %d"), OperationIndex));
                }

                PositionBehaviorTreeGraphChildNode(*ParentGraphNode, *NewGraphNode, ChildSlot);
                const UEdGraphSchema* GraphSchema = BehaviorTreeGraph->GetSchema();
                if (!GraphSchema || !GraphSchema->TryCreateConnection(ParentOutputPin, ChildInputPin))
                {
                    return FUnrealAICommonUtils::CreateErrorResponse(
                        FString::Printf(TEXT("Failed to connect new Behavior Tree node for operation %d"), OperationIndex));
                }

                BehaviorTreeGraph->UpdateAsset();
            }
            else if (NodeKind == TEXT("decorator"))
            {
                UBehaviorTreeGraphNode* DecoratorOwnerNode = nullptr;
                if (ParentPath == TEXT("root"))
                {
                    DecoratorOwnerNode = FindBehaviorTreeGraphRootNode(*BehaviorTreeGraph);
                }
                else if (ResolvedPath.Kind == FBehaviorTreeResolvedPath::ETargetKind::Composite || ResolvedPath.Kind == FBehaviorTreeResolvedPath::ETargetKind::Task)
                {
                    DecoratorOwnerNode = ParentGraphNode;
                }

                if (!DecoratorOwnerNode)
                {
                    return FUnrealAICommonUtils::CreateErrorResponse(
                        FString::Printf(TEXT("Decorator add operation at index %d requires 'parent_path' to resolve to 'root', a task, or a composite"), OperationIndex));
                }

                UBehaviorTreeGraphNode_Decorator* DecoratorNode = CreateBehaviorTreeDecoratorSubNode(*BehaviorTreeGraph, RuntimeClass);
                DecoratorOwnerNode->AddSubNode(DecoratorNode, BehaviorTreeGraph);
                BehaviorTreeGraph->UpdateAsset();
            }
            else if (NodeKind == TEXT("service"))
            {
                if (ResolvedPath.Kind != FBehaviorTreeResolvedPath::ETargetKind::Composite && ResolvedPath.Kind != FBehaviorTreeResolvedPath::ETargetKind::Task)
                {
                    return FUnrealAICommonUtils::CreateErrorResponse(
                        FString::Printf(TEXT("Service add operation at index %d requires a task or composite 'parent_path': %s"), OperationIndex, *ParentPath));
                }

                UBehaviorTreeGraphNode_Service* ServiceNode = CreateBehaviorTreeServiceSubNode(*BehaviorTreeGraph, RuntimeClass);
                ParentGraphNode->AddSubNode(ServiceNode, BehaviorTreeGraph);
                BehaviorTreeGraph->UpdateAsset();
            }
            else
            {
                return FUnrealAICommonUtils::CreateErrorResponse(
                    FString::Printf(TEXT("Unsupported add operation node_kind at index %d: %s"), OperationIndex, *NodeKind));
            }

            AppliedOperation->SetStringField(TEXT("parent_path"), ParentPath);
            AppliedOperation->SetStringField(TEXT("node_kind"), NodeKind);
            AppliedOperation->SetStringField(TEXT("node_type"), CanonicalNodeType);
            AppliedOperation->SetNumberField(TEXT("child_slot"), ChildSlot);
        }
        else if (OperationType == TEXT("remove"))
        {
            FString TargetPath;
            if (!OperationObject->TryGetStringField(TEXT("target_path"), TargetPath) || TargetPath.TrimStartAndEnd().IsEmpty())
            {
                return FUnrealAICommonUtils::CreateErrorResponse(
                    FString::Printf(TEXT("Remove operation at index %d is missing a non-empty 'target_path' field"), OperationIndex));
            }
            TargetPath = TargetPath.TrimStartAndEnd();

            if (TargetPath == TEXT("root"))
            {
                return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Removing the root Behavior Tree node is not supported"));
            }

            FBehaviorTreeResolvedPath ResolvedPath;
            UBehaviorTreeGraphNode* GraphNode = nullptr;
            UBehaviorTreeGraphNode* OwnerNode = nullptr;
            FString ResolveError;
            if (!TryResolveBehaviorTreeGraphNodeForPath(*BehaviorTree, *BehaviorTreeGraph, TargetPath, ResolvedPath, GraphNode, OwnerNode, ResolveError))
            {
                return FUnrealAICommonUtils::CreateErrorResponse(ResolveError);
            }

            if (!GraphNode)
            {
                return FUnrealAICommonUtils::CreateErrorResponse(
                    FString::Printf(TEXT("Failed to resolve graph node for remove operation at index %d"), OperationIndex));
            }

            GraphNode->DestroyNode();
            BehaviorTreeGraph->UpdateAsset();

            AppliedOperation->SetStringField(TEXT("target_path"), TargetPath);
        }
        else
        {
            return FUnrealAICommonUtils::CreateErrorResponse(
                FString::Printf(TEXT("Unsupported Behavior Tree subtree operation at index %d: %s"), OperationIndex, *OperationType));
        }

        AppliedOperations.Add(MakeShared<FJsonValueObject>(AppliedOperation));
    }

    BehaviorTree->MarkPackageDirty();
    UEditorAssetLibrary::SaveAsset(AssetPath, /*bOnlyIfIsDirty=*/false);

    TSharedPtr<FJsonObject> Result = BuildBehaviorTreeContentSummary(*BehaviorTree, AssetPath);
    Result->SetArrayField(TEXT("applied_operations"), AppliedOperations);
    Result->SetNumberField(TEXT("applied_operation_count"), AppliedOperations.Num());
    Result->SetBoolField(TEXT("updated"), true);
    Result->SetBoolField(TEXT("success"), true);
    return Result;
}

// --------------------------------------------------------------------------- //
// set_behavior_tree_node_properties
// --------------------------------------------------------------------------- //
TSharedPtr<FJsonObject> FUnrealAIAssetCommands::HandleSetBehaviorTreeNodeProperties(const TSharedPtr<FJsonObject>& Params)
{
    FString AssetPath;
    if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath) || AssetPath.TrimStartAndEnd().IsEmpty())
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing or empty 'asset_path' parameter"));
    }
    AssetPath = AssetPath.TrimStartAndEnd();

    FString TopologyPath;
    if (!Params->TryGetStringField(TEXT("topology_path"), TopologyPath) || TopologyPath.TrimStartAndEnd().IsEmpty())
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing or empty 'topology_path' parameter"));
    }
    TopologyPath = TopologyPath.TrimStartAndEnd();

    FString BlackboardKeyName;
    const bool bHasBlackboardKeyName = Params->TryGetStringField(TEXT("blackboard_key_name"), BlackboardKeyName);
    BlackboardKeyName = BlackboardKeyName.TrimStartAndEnd();

    FString FlowAbortModeValue;
    const bool bHasFlowAbortMode = Params->TryGetStringField(TEXT("flow_abort_mode"), FlowAbortModeValue);
    FlowAbortModeValue = FlowAbortModeValue.TrimStartAndEnd();

    FString EnabledStateValue;
    const bool bHasEnabledState = Params->TryGetStringField(TEXT("enabled_state"), EnabledStateValue);
    EnabledStateValue = EnabledStateValue.TrimStartAndEnd();

    if (!bHasBlackboardKeyName && !bHasFlowAbortMode && !bHasEnabledState)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(
            TEXT("Provide at least one of 'blackboard_key_name', 'flow_abort_mode', or 'enabled_state'"));
    }

    UBehaviorTree* BehaviorTree = Cast<UBehaviorTree>(UEditorAssetLibrary::LoadAsset(AssetPath));
    if (!BehaviorTree)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Behavior Tree asset not found or wrong class: %s"), *AssetPath));
    }

    UBehaviorTreeGraph* BehaviorTreeGraph = nullptr;
    FString GraphError;
    if (!EnsureBehaviorTreeGraphForMutation(*BehaviorTree, BehaviorTreeGraph, GraphError))
    {
        return FUnrealAICommonUtils::CreateErrorResponse(GraphError);
    }

    FBehaviorTreeResolvedPath ResolvedPath;
    UBehaviorTreeGraphNode* GraphNode = nullptr;
    UBehaviorTreeGraphNode* OwnerNode = nullptr;
    FString ResolveError;
    if (!TryResolveBehaviorTreeGraphNodeForPath(*BehaviorTree, *BehaviorTreeGraph, TopologyPath, ResolvedPath, GraphNode, OwnerNode, ResolveError))
    {
        return FUnrealAICommonUtils::CreateErrorResponse(ResolveError);
    }

    UObject* RuntimeObject = ResolvedPath.GetRuntimeObject();
    TArray<TSharedPtr<FJsonValue>> UpdatedFields;

    if (bHasBlackboardKeyName)
    {
        if (!RuntimeObject)
        {
            return FUnrealAICommonUtils::CreateErrorResponse(
                FString::Printf(TEXT("Path '%s' does not resolve to a Behavior Tree runtime node"), *TopologyPath));
        }

        FBlackboardKeySelector* BlackboardSelector = nullptr;
        if (!TryGetBehaviorTreeBlackboardKeySelector(*RuntimeObject, BlackboardSelector) || !BlackboardSelector)
        {
            return FUnrealAICommonUtils::CreateErrorResponse(
                FString::Printf(TEXT("Behavior Tree node at '%s' does not expose a Blackboard selector"), *TopologyPath));
        }

        if (!BehaviorTree->BlackboardAsset)
        {
            return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Behavior Tree has no Blackboard asset to resolve selector changes against"));
        }

        RuntimeObject->Modify();
        if (BlackboardKeyName.IsEmpty() || NormalizeBehaviorTreeToken(BlackboardKeyName) == TEXT("none"))
        {
            BlackboardSelector->SelectedKeyName = NAME_None;
            BlackboardSelector->InvalidateResolvedKey();
        }
        else
        {
            const FName KeyName(*BlackboardKeyName);
            if (BehaviorTree->BlackboardAsset->GetKeyID(KeyName) == FBlackboard::InvalidKey)
            {
                return FUnrealAICommonUtils::CreateErrorResponse(
                    FString::Printf(TEXT("Blackboard key '%s' was not found on Blackboard asset '%s'"), *BlackboardKeyName, *BehaviorTree->BlackboardAsset->GetPathName()));
            }

            BlackboardSelector->SelectedKeyName = KeyName;
            BlackboardSelector->InvalidateResolvedKey();
            BlackboardSelector->ResolveSelectedKey(*BehaviorTree->BlackboardAsset);
        }

        UpdatedFields.Add(MakeShared<FJsonValueString>(TEXT("blackboard_key_name")));
    }

    if (bHasFlowAbortMode)
    {
        UBTDecorator* Decorator = Cast<UBTDecorator>(RuntimeObject);
        if (!Decorator)
        {
            return FUnrealAICommonUtils::CreateErrorResponse(
                FString::Printf(TEXT("Behavior Tree node at '%s' is not a decorator"), *TopologyPath));
        }

        EBTFlowAbortMode::Type FlowAbortMode = EBTFlowAbortMode::None;
        if (!TryParseBehaviorTreeFlowAbortMode(FlowAbortModeValue, FlowAbortMode))
        {
            return FUnrealAICommonUtils::CreateErrorResponse(
                FString::Printf(TEXT("Unsupported Behavior Tree flow_abort_mode value: %s"), *FlowAbortModeValue));
        }

        Decorator->Modify();
        FByteProperty* FlowAbortProperty = FindFProperty<FByteProperty>(UBTDecorator::StaticClass(), TEXT("FlowAbortMode"));
        if (!FlowAbortProperty)
        {
            return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Failed to resolve Behavior Tree FlowAbortMode property"));
        }

        void* FlowAbortValue = FlowAbortProperty->ContainerPtrToValuePtr<void>(Decorator);
        FlowAbortProperty->SetIntPropertyValue(FlowAbortValue, static_cast<int64>(FlowAbortMode));
        UpdatedFields.Add(MakeShared<FJsonValueString>(TEXT("flow_abort_mode")));
    }

    if (bHasEnabledState)
    {
        ENodeEnabledState EnabledState = ENodeEnabledState::Enabled;
        if (!TryParseBehaviorTreeEnabledState(EnabledStateValue, EnabledState))
        {
            return FUnrealAICommonUtils::CreateErrorResponse(
                FString::Printf(TEXT("Unsupported Behavior Tree enabled_state value: %s"), *EnabledStateValue));
        }

        GraphNode->Modify();
        GraphNode->SetEnabledState(EnabledState);
        UpdatedFields.Add(MakeShared<FJsonValueString>(TEXT("enabled_state")));
    }

    BehaviorTreeGraph->UpdateAsset();
    BehaviorTree->MarkPackageDirty();
    UEditorAssetLibrary::SaveAsset(AssetPath, /*bOnlyIfIsDirty=*/false);

    TSharedPtr<FJsonObject> Result = BuildBehaviorTreeContentSummary(*BehaviorTree, AssetPath);
    Result->SetStringField(TEXT("topology_path"), TopologyPath);
    Result->SetArrayField(TEXT("updated_fields"), UpdatedFields);
    Result->SetNumberField(TEXT("updated_field_count"), UpdatedFields.Num());
    Result->SetBoolField(TEXT("updated"), true);
    Result->SetBoolField(TEXT("success"), true);
    return Result;
}

// --------------------------------------------------------------------------- //
// validate_behavior_tree
// --------------------------------------------------------------------------- //
TSharedPtr<FJsonObject> FUnrealAIAssetCommands::HandleValidateBehaviorTree(const TSharedPtr<FJsonObject>& Params)
{
    FString AssetPath;
    if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath) || AssetPath.TrimStartAndEnd().IsEmpty())
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing or empty 'asset_path' parameter"));
    }
    AssetPath = AssetPath.TrimStartAndEnd();

    UBehaviorTree* BehaviorTree = Cast<UBehaviorTree>(UEditorAssetLibrary::LoadAsset(AssetPath));
    if (!BehaviorTree)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Behavior Tree asset not found or wrong class: %s"), *AssetPath));
    }

    TSharedPtr<FJsonObject> Result = BuildBehaviorTreeContentSummary(*BehaviorTree, AssetPath);

    TArray<TSharedPtr<FJsonValue>> Issues;
    int32 ErrorCount = 0;
    int32 WarningCount = 0;

    FBehaviorTreeGraphDiagnostics Diagnostics;
    CollectBehaviorTreeGraphDiagnostics(*BehaviorTree, Diagnostics);

    if (!Cast<UBehaviorTreeGraph>(BehaviorTree->BTGraph))
    {
        AddValidationIssue(
            Issues,
            ErrorCount,
            WarningCount,
            TEXT("warning"),
            TEXT("missing_editor_graph"),
            TEXT("Behavior Tree asset has no editor graph; enabled-state and graph diagnostics are unavailable until a mutation helper restores it"),
            TEXT("asset_path"));
    }

    if (!BehaviorTree->RootNode)
    {
        AddValidationIssue(
            Issues,
            ErrorCount,
            WarningCount,
            TEXT("error"),
            TEXT("missing_root_node"),
            TEXT("Behavior Tree asset has no root node"),
            TEXT("root"));
    }
    else
    {
        for (int32 DecoratorIndex = 0; DecoratorIndex < BehaviorTree->RootDecorators.Num(); ++DecoratorIndex)
        {
            UBTDecorator* Decorator = BehaviorTree->RootDecorators[DecoratorIndex];
            if (!Decorator)
            {
                continue;
            }

            ValidateBehaviorTreeRuntimeObject(
                *Decorator,
                FString::Printf(TEXT("root/root_decorators/%d"), DecoratorIndex),
                *BehaviorTree,
                Diagnostics,
                Issues,
                ErrorCount,
                WarningCount);
        }

        ValidateBehaviorTreeComposite(*BehaviorTree->RootNode, TEXT("root"), *BehaviorTree, Diagnostics, Issues, ErrorCount, WarningCount);
    }

    Result->SetArrayField(TEXT("issues"), Issues);
    Result->SetNumberField(TEXT("error_count"), ErrorCount);
    Result->SetNumberField(TEXT("warning_count"), WarningCount);
    Result->SetBoolField(TEXT("is_valid"), ErrorCount == 0);
    Result->SetBoolField(TEXT("validated"), true);
    Result->SetBoolField(TEXT("success"), true);
    return Result;
}

// --------------------------------------------------------------------------- //
// read_blackboard_content
// --------------------------------------------------------------------------- //
TSharedPtr<FJsonObject> FUnrealAIAssetCommands::HandleReadBlackboardContent(const TSharedPtr<FJsonObject>& Params)
{
    FString AssetPath;
    if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath) || AssetPath.TrimStartAndEnd().IsEmpty())
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing or empty 'asset_path' parameter"));
    }
    AssetPath = AssetPath.TrimStartAndEnd();

    UObject* LoadedObject = UEditorAssetLibrary::LoadAsset(AssetPath);
    UBlackboardData* BlackboardData = Cast<UBlackboardData>(LoadedObject);
    if (!BlackboardData)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Blackboard asset not found or wrong class: %s"), *AssetPath));
    }

    TSharedPtr<FJsonObject> Result = BuildBlackboardContentSummary(*BlackboardData, AssetPath);
    Result->SetBoolField(TEXT("success"), true);
    return Result;
}

// --------------------------------------------------------------------------- //
// create_blackboard_asset
// --------------------------------------------------------------------------- //
TSharedPtr<FJsonObject> FUnrealAIAssetCommands::HandleCreateBlackboardAsset(const TSharedPtr<FJsonObject>& Params)
{
    FString BlackboardName;
    if (!Params->TryGetStringField(TEXT("blackboard_name"), BlackboardName) || BlackboardName.TrimStartAndEnd().IsEmpty())
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing or empty 'blackboard_name' parameter"));
    }
    BlackboardName = BlackboardName.TrimStartAndEnd();

    FString DestinationPath = TEXT("/Game/Blackboards");
    Params->TryGetStringField(TEXT("destination_path"), DestinationPath);
    DestinationPath = DestinationPath.TrimStartAndEnd();
    if (DestinationPath.IsEmpty())
    {
        DestinationPath = TEXT("/Game/Blackboards");
    }

    FString ParentBlackboardPath;
    Params->TryGetStringField(TEXT("parent_blackboard_path"), ParentBlackboardPath);
    ParentBlackboardPath = ParentBlackboardPath.TrimStartAndEnd();

    UBlackboardData* ParentBlackboard = nullptr;
    if (!ParentBlackboardPath.IsEmpty())
    {
        ParentBlackboard = Cast<UBlackboardData>(UEditorAssetLibrary::LoadAsset(ParentBlackboardPath));
        if (!ParentBlackboard)
        {
            return FUnrealAICommonUtils::CreateErrorResponse(
                FString::Printf(TEXT("Failed to load parent Blackboard asset: %s"), *ParentBlackboardPath));
        }
    }

    if (!UEditorAssetLibrary::DoesDirectoryExist(DestinationPath))
    {
        UEditorAssetLibrary::MakeDirectory(DestinationPath);
    }

    const FString FullObjectPath = FString::Printf(TEXT("%s/%s.%s"), *DestinationPath, *BlackboardName, *BlackboardName);
    if (UEditorAssetLibrary::DoesAssetExist(FullObjectPath))
    {
        return FUnrealAICommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Asset already exists: %s"), *FullObjectPath));
    }

    FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools"));
    IAssetTools& AssetTools = AssetToolsModule.Get();

    UBlackboardDataFactory* Factory = NewObject<UBlackboardDataFactory>();
    Factory->bEditAfterNew = false;

    UObject* NewAsset = AssetTools.CreateAsset(BlackboardName, DestinationPath, UBlackboardData::StaticClass(), Factory);
    UBlackboardData* BlackboardData = Cast<UBlackboardData>(NewAsset);
    if (!BlackboardData)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Failed to create Blackboard asset"));
    }

    if (ParentBlackboard)
    {
        FProperty* ParentProperty = FindFProperty<FProperty>(UBlackboardData::StaticClass(), GET_MEMBER_NAME_CHECKED(UBlackboardData, Parent));
        BlackboardData->PreEditChange(ParentProperty);
        BlackboardData->Parent = ParentBlackboard;
        FPropertyChangedEvent PropertyChangedEvent(ParentProperty, EPropertyChangeType::ValueSet);
        BlackboardData->PostEditChangeProperty(PropertyChangedEvent);
    }

    BlackboardData->MarkPackageDirty();
    UEditorAssetLibrary::SaveAsset(FullObjectPath, /*bOnlyIfIsDirty=*/false);

    TSharedPtr<FJsonObject> ReadParams = MakeShared<FJsonObject>();
    ReadParams->SetStringField(TEXT("asset_path"), FullObjectPath);

    TSharedPtr<FJsonObject> Result = HandleReadBlackboardContent(ReadParams);
    Result->SetStringField(TEXT("blackboard_name"), BlackboardName);
    Result->SetStringField(TEXT("destination_path"), DestinationPath);
    Result->SetStringField(TEXT("asset_path"), FullObjectPath);
    Result->SetStringField(TEXT("parent_blackboard_path"), ParentBlackboardPath);
    Result->SetBoolField(TEXT("created"), true);
    Result->SetBoolField(TEXT("success"), true);
    return Result;
}

// --------------------------------------------------------------------------- //
// update_blackboard_keys
// --------------------------------------------------------------------------- //
TSharedPtr<FJsonObject> FUnrealAIAssetCommands::HandleUpdateBlackboardKeys(const TSharedPtr<FJsonObject>& Params)
{
    FString AssetPath;
    if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath) || AssetPath.TrimStartAndEnd().IsEmpty())
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing or empty 'asset_path' parameter"));
    }
    AssetPath = AssetPath.TrimStartAndEnd();

    UObject* LoadedObject = UEditorAssetLibrary::LoadAsset(AssetPath);
    UBlackboardData* BlackboardData = Cast<UBlackboardData>(LoadedObject);
    if (!BlackboardData)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Blackboard asset not found or wrong class: %s"), *AssetPath));
    }

    const TArray<TSharedPtr<FJsonValue>>* Operations = nullptr;
    if (!Params->TryGetArrayField(TEXT("operations"), Operations) || !Operations || Operations->Num() == 0)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing or empty 'operations' array"));
    }

    FProperty* KeysProperty = FindFProperty<FProperty>(UBlackboardData::StaticClass(), GET_MEMBER_NAME_CHECKED(UBlackboardData, Keys));
    FProperty* EntryNameProperty = FindFProperty<FProperty>(FBlackboardEntry::StaticStruct(), GET_MEMBER_NAME_CHECKED(FBlackboardEntry, EntryName));

    BlackboardData->SetFlags(RF_Transactional);
    BlackboardData->Modify();

    int32 AddedCount = 0;
    int32 UpdatedCount = 0;
    int32 RenamedCount = 0;
    int32 DeletedCount = 0;
    TArray<TSharedPtr<FJsonValue>> AppliedOperations;

    for (const TSharedPtr<FJsonValue>& OperationValue : *Operations)
    {
        if (!OperationValue.IsValid() || OperationValue->Type != EJson::Object)
        {
            return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Each 'operations' entry must be a JSON object"));
        }

        const TSharedPtr<FJsonObject> OperationObject = OperationValue->AsObject();

        FString OperationName = TEXT("upsert");
        OperationObject->TryGetStringField(TEXT("op"), OperationName);
        OperationName = OperationName.TrimStartAndEnd().ToLower();
        if (OperationName.IsEmpty())
        {
            OperationName = TEXT("upsert");
        }

        FString KeyNameString;
        if (!OperationObject->TryGetStringField(TEXT("name"), KeyNameString) || KeyNameString.TrimStartAndEnd().IsEmpty())
        {
            return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Each Blackboard key operation requires a non-empty 'name'"));
        }
        KeyNameString = KeyNameString.TrimStartAndEnd();
        const FName KeyName(*KeyNameString);

        TSharedPtr<FJsonObject> AppliedOperation = MakeShared<FJsonObject>();
        AppliedOperation->SetStringField(TEXT("requested_op"), OperationName);
        AppliedOperation->SetStringField(TEXT("name"), KeyNameString);

        if (OperationName == TEXT("delete") || OperationName == TEXT("remove"))
        {
            const int32 LocalKeyIndex = FindLocalBlackboardKeyIndex(*BlackboardData, KeyName);
            if (LocalKeyIndex == INDEX_NONE)
            {
                return FUnrealAICommonUtils::CreateErrorResponse(
                    FString::Printf(TEXT("Blackboard key '%s' was not found as a local key"), *KeyNameString));
            }

            BlackboardData->PreEditChange(KeysProperty);
            BlackboardData->Keys.RemoveAt(LocalKeyIndex);
            FPropertyChangedEvent PropertyChangedEvent(KeysProperty, EPropertyChangeType::ArrayRemove);
            BlackboardData->PostEditChangeProperty(PropertyChangedEvent);

            ++DeletedCount;
            AppliedOperation->SetStringField(TEXT("applied_op"), TEXT("delete"));
            AppliedOperations.Add(MakeShared<FJsonValueObject>(AppliedOperation));
            continue;
        }

        if (OperationName == TEXT("rename"))
        {
            FString NewNameString;
            if (!OperationObject->TryGetStringField(TEXT("new_name"), NewNameString) || NewNameString.TrimStartAndEnd().IsEmpty())
            {
                return FUnrealAICommonUtils::CreateErrorResponse(
                    FString::Printf(TEXT("Rename operation for Blackboard key '%s' requires a non-empty 'new_name'"), *KeyNameString));
            }
            NewNameString = NewNameString.TrimStartAndEnd();
            const FName NewKeyName(*NewNameString);

            const int32 LocalKeyIndex = FindLocalBlackboardKeyIndex(*BlackboardData, KeyName);
            if (LocalKeyIndex == INDEX_NONE)
            {
                return FUnrealAICommonUtils::CreateErrorResponse(
                    FString::Printf(TEXT("Blackboard key '%s' was not found as a local key"), *KeyNameString));
            }

            if (KeyName != NewKeyName)
            {
                const int32 ExistingLocalKeyIndex = FindLocalBlackboardKeyIndex(*BlackboardData, NewKeyName);
                if (ExistingLocalKeyIndex != INDEX_NONE || DoesBlackboardKeyNameExistInParentChain(*BlackboardData, NewKeyName))
                {
                    return FUnrealAICommonUtils::CreateErrorResponse(
                        FString::Printf(TEXT("Cannot rename Blackboard key '%s' to '%s' because that name already exists"), *KeyNameString, *NewNameString));
                }

                TArray<UObject*> ReferencerAssets;
                LoadBlackboardReferencerAssets(*BlackboardData, ReferencerAssets);

                FBlackboardEntry& Entry = BlackboardData->Keys[LocalKeyIndex];
                FEditPropertyChain PropertyChain;
                PropertyChain.AddHead(KeysProperty);
                PropertyChain.AddTail(EntryNameProperty);
                PropertyChain.SetActiveMemberPropertyNode(KeysProperty);
                PropertyChain.SetActivePropertyNode(EntryNameProperty);

                Entry.EntryName = NewKeyName;
                BlackboardData->PreEditChange(PropertyChain);
                UpdateExternalBlackboardKeyReferences(KeyName, NewKeyName, ReferencerAssets);

                FPropertyChangedEvent PropertyChangedEvent(EntryNameProperty, EPropertyChangeType::ValueSet);
                FPropertyChangedChainEvent PropertyChangedChainEvent(PropertyChain, PropertyChangedEvent);
                BlackboardData->PostEditChangeChainProperty(PropertyChangedChainEvent);

                for (UObject* ReferencerAsset : ReferencerAssets)
                {
                    if (ReferencerAsset)
                    {
                        UEditorAssetLibrary::SaveLoadedAsset(ReferencerAsset, /*bOnlyIfIsDirty=*/false);
                    }
                }
            }

            ++RenamedCount;
            AppliedOperation->SetStringField(TEXT("applied_op"), TEXT("rename"));
            AppliedOperation->SetStringField(TEXT("new_name"), NewNameString);
            AppliedOperations.Add(MakeShared<FJsonValueObject>(AppliedOperation));
            continue;
        }

        if (OperationName != TEXT("add") && OperationName != TEXT("create") && OperationName != TEXT("update") && OperationName != TEXT("upsert"))
        {
            return FUnrealAICommonUtils::CreateErrorResponse(
                FString::Printf(TEXT("Unsupported Blackboard key operation: %s"), *OperationName));
        }

        const int32 LocalKeyIndex = FindLocalBlackboardKeyIndex(*BlackboardData, KeyName);
        const bool bKeyExistsLocally = LocalKeyIndex != INDEX_NONE;
        const bool bIsAddOnly = OperationName == TEXT("add") || OperationName == TEXT("create");
        const bool bIsUpdateOnly = OperationName == TEXT("update");
        const bool bIsUpsert = OperationName == TEXT("upsert");

        if (bIsAddOnly && (bKeyExistsLocally || DoesBlackboardKeyNameExistInParentChain(*BlackboardData, KeyName)))
        {
            return FUnrealAICommonUtils::CreateErrorResponse(
                FString::Printf(TEXT("Cannot add Blackboard key '%s' because that name already exists"), *KeyNameString));
        }

        if (bIsUpdateOnly && !bKeyExistsLocally)
        {
            return FUnrealAICommonUtils::CreateErrorResponse(
                FString::Printf(TEXT("Cannot update Blackboard key '%s' because it does not exist locally"), *KeyNameString));
        }

        if (!bKeyExistsLocally && !bIsAddOnly && !bIsUpsert)
        {
            return FUnrealAICommonUtils::CreateErrorResponse(
                FString::Printf(TEXT("Blackboard key '%s' was not found as a local key"), *KeyNameString));
        }

        if (!bKeyExistsLocally && DoesBlackboardKeyNameExistInParentChain(*BlackboardData, KeyName))
        {
            return FUnrealAICommonUtils::CreateErrorResponse(
                FString::Printf(TEXT("Cannot create or upsert Blackboard key '%s' because that name already exists in the parent chain"), *KeyNameString));
        }

        FString CanonicalTypeName;
        FString MutationError;
        if (!bKeyExistsLocally)
        {
            BlackboardData->PreEditChange(KeysProperty);

            FBlackboardEntry NewEntry;
            NewEntry.EntryName = KeyName;
            if (!TryApplyBlackboardEntryMutation(*BlackboardData, NewEntry, OperationObject, /*bRequireKeyType=*/true, CanonicalTypeName, MutationError))
            {
                return FUnrealAICommonUtils::CreateErrorResponse(MutationError);
            }

            BlackboardData->Keys.Add(NewEntry);
            FPropertyChangedEvent PropertyChangedEvent(KeysProperty, EPropertyChangeType::ArrayAdd);
            BlackboardData->PostEditChangeProperty(PropertyChangedEvent);

            ++AddedCount;
            AppliedOperation->SetStringField(TEXT("applied_op"), TEXT("add"));
            AppliedOperation->SetStringField(TEXT("key_type"), CanonicalTypeName);
            AppliedOperations.Add(MakeShared<FJsonValueObject>(AppliedOperation));
            continue;
        }

        BlackboardData->PreEditChange(KeysProperty);
        if (!TryApplyBlackboardEntryMutation(*BlackboardData, BlackboardData->Keys[LocalKeyIndex], OperationObject, /*bRequireKeyType=*/false, CanonicalTypeName, MutationError))
        {
            return FUnrealAICommonUtils::CreateErrorResponse(MutationError);
        }

        FPropertyChangedEvent PropertyChangedEvent(KeysProperty, EPropertyChangeType::ValueSet);
        BlackboardData->PostEditChangeProperty(PropertyChangedEvent);

        ++UpdatedCount;
        AppliedOperation->SetStringField(TEXT("applied_op"), TEXT("update"));
        if (!CanonicalTypeName.IsEmpty())
        {
            AppliedOperation->SetStringField(TEXT("key_type"), CanonicalTypeName);
        }
        AppliedOperations.Add(MakeShared<FJsonValueObject>(AppliedOperation));
    }

    BlackboardData->MarkPackageDirty();
    UEditorAssetLibrary::SaveAsset(AssetPath, /*bOnlyIfIsDirty=*/false);

    TSharedPtr<FJsonObject> ReadParams = MakeShared<FJsonObject>();
    ReadParams->SetStringField(TEXT("asset_path"), AssetPath);
    TSharedPtr<FJsonObject> Result = HandleReadBlackboardContent(ReadParams);
    Result->SetArrayField(TEXT("applied_operations"), AppliedOperations);
    Result->SetNumberField(TEXT("added_key_count"), AddedCount);
    Result->SetNumberField(TEXT("updated_key_count"), UpdatedCount);
    Result->SetNumberField(TEXT("renamed_key_count"), RenamedCount);
    Result->SetNumberField(TEXT("deleted_key_count"), DeletedCount);
    Result->SetBoolField(TEXT("updated"), AddedCount + UpdatedCount + RenamedCount + DeletedCount > 0);
    Result->SetBoolField(TEXT("success"), true);
    return Result;
}

// --------------------------------------------------------------------------- //
// read_niagara_system_emitter
// --------------------------------------------------------------------------- //
TSharedPtr<FJsonObject> FUnrealAIAssetCommands::HandleReadNiagaraSystemEmitter(const TSharedPtr<FJsonObject>& Params)
{
    FString AssetPath;
    if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath) || AssetPath.TrimStartAndEnd().IsEmpty())
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing or empty 'asset_path' parameter"));
    }
    AssetPath = AssetPath.TrimStartAndEnd();

    UObject* LoadedObject = UEditorAssetLibrary::LoadAsset(AssetPath);
    UNiagaraSystem* NiagaraSystem = Cast<UNiagaraSystem>(LoadedObject);
    if (!NiagaraSystem)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Niagara System not found or wrong class: %s"), *AssetPath));
    }

    int32 EmitterIndex = INDEX_NONE;
    FString ResolveError;
    if (!ResolveNiagaraEmitterHandleIndex(Params, *NiagaraSystem, EmitterIndex, ResolveError))
    {
        return FUnrealAICommonUtils::CreateErrorResponse(ResolveError);
    }

    TArray<FNiagaraEmitterHandle>& EmitterHandles = NiagaraSystem->GetEmitterHandles();
    if (!EmitterHandles.IsValidIndex(EmitterIndex))
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Resolved Niagara emitter index is out of bounds"));
    }

    TSharedPtr<FJsonObject> Result = BuildNiagaraEmitterSummary(*NiagaraSystem, EmitterHandles[EmitterIndex]);
    Result->SetStringField(TEXT("asset_path"), AssetPath);
    Result->SetStringField(TEXT("system_name"), NiagaraSystem->GetName());
    Result->SetStringField(TEXT("system_path"), AssetPath);
    Result->SetBoolField(TEXT("success"), true);
    return Result;
}

// --------------------------------------------------------------------------- //
// create_niagara_system_asset
// --------------------------------------------------------------------------- //
TSharedPtr<FJsonObject> FUnrealAIAssetCommands::HandleCreateNiagaraSystemAsset(const TSharedPtr<FJsonObject>& Params)
{
    FString NiagaraSystemName;
    if (!Params->TryGetStringField(TEXT("niagara_system_name"), NiagaraSystemName) || NiagaraSystemName.TrimStartAndEnd().IsEmpty())
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing or empty 'niagara_system_name' parameter"));
    }
    NiagaraSystemName = NiagaraSystemName.TrimStartAndEnd();

    FString DestinationPath = TEXT("/Game/NiagaraSystems");
    Params->TryGetStringField(TEXT("destination_path"), DestinationPath);
    DestinationPath = DestinationPath.TrimStartAndEnd();
    if (DestinationPath.IsEmpty())
    {
        DestinationPath = TEXT("/Game/NiagaraSystems");
    }

    FString TemplateAssetPath;
    Params->TryGetStringField(TEXT("template_asset_path"), TemplateAssetPath);
    TemplateAssetPath = TemplateAssetPath.TrimStartAndEnd();

    UNiagaraSystem* TemplateSystem = nullptr;
    if (!TemplateAssetPath.IsEmpty())
    {
        UObject* LoadedTemplateObject = UEditorAssetLibrary::LoadAsset(TemplateAssetPath);
        TemplateSystem = Cast<UNiagaraSystem>(LoadedTemplateObject);
        if (!TemplateSystem)
        {
            return FUnrealAICommonUtils::CreateErrorResponse(
                FString::Printf(TEXT("Failed to load Niagara System template asset: %s"), *TemplateAssetPath));
        }
    }

    if (!UEditorAssetLibrary::DoesDirectoryExist(DestinationPath))
    {
        UEditorAssetLibrary::MakeDirectory(DestinationPath);
    }

    const FString FullObjectPath = FString::Printf(TEXT("%s/%s.%s"), *DestinationPath, *NiagaraSystemName, *NiagaraSystemName);
    if (UEditorAssetLibrary::DoesAssetExist(FullObjectPath))
    {
        return FUnrealAICommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Asset already exists: %s"), *FullObjectPath));
    }

    FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools"));
    IAssetTools& AssetTools = AssetToolsModule.Get();

    UNiagaraSystemFactoryNew* Factory = NewObject<UNiagaraSystemFactoryNew>();
    Factory->SystemToCopy = TemplateSystem;
    Factory->bEditAfterNew = false;

    UObject* NewAsset = AssetTools.CreateAsset(NiagaraSystemName, DestinationPath, UNiagaraSystem::StaticClass(), Factory);
    UNiagaraSystem* NiagaraSystem = Cast<UNiagaraSystem>(NewAsset);
    if (!NiagaraSystem)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Failed to create Niagara System asset"));
    }

    NiagaraSystem->MarkPackageDirty();
    UEditorAssetLibrary::SaveAsset(FullObjectPath, /*bOnlyIfIsDirty=*/false);

    TSharedPtr<FJsonObject> ReadParams = MakeShared<FJsonObject>();
    ReadParams->SetStringField(TEXT("asset_path"), FullObjectPath);

    TSharedPtr<FJsonObject> Result = HandleReadNiagaraSystemContent(ReadParams);
    Result->SetStringField(TEXT("niagara_system_name"), NiagaraSystemName);
    Result->SetStringField(TEXT("destination_path"), DestinationPath);
    Result->SetStringField(TEXT("asset_path"), FullObjectPath);
    Result->SetStringField(TEXT("template_asset_path"), TemplateAssetPath);
    Result->SetBoolField(TEXT("created"), true);
    Result->SetBoolField(TEXT("created_from_template"), !TemplateAssetPath.IsEmpty());
    Result->SetBoolField(TEXT("success"), true);
    return Result;
}

// --------------------------------------------------------------------------- //
// add_niagara_emitter_to_system
// --------------------------------------------------------------------------- //
TSharedPtr<FJsonObject> FUnrealAIAssetCommands::HandleAddNiagaraEmitterToSystem(const TSharedPtr<FJsonObject>& Params)
{
    FString AssetPath;
    if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath) || AssetPath.TrimStartAndEnd().IsEmpty())
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing or empty 'asset_path' parameter"));
    }
    AssetPath = AssetPath.TrimStartAndEnd();

    FString EmitterAssetPath;
    if (!Params->TryGetStringField(TEXT("emitter_asset_path"), EmitterAssetPath) || EmitterAssetPath.TrimStartAndEnd().IsEmpty())
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing or empty 'emitter_asset_path' parameter"));
    }
    EmitterAssetPath = EmitterAssetPath.TrimStartAndEnd();

    UNiagaraSystem* NiagaraSystem = Cast<UNiagaraSystem>(UEditorAssetLibrary::LoadAsset(AssetPath));
    if (!NiagaraSystem)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Niagara System not found or wrong class: %s"), *AssetPath));
    }

    UNiagaraEmitter* NiagaraEmitter = Cast<UNiagaraEmitter>(UEditorAssetLibrary::LoadAsset(EmitterAssetPath));
    if (!NiagaraEmitter)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Niagara Emitter asset not found or wrong class: %s"), *EmitterAssetPath));
    }

    const FGuid AddedEmitterHandleId = FNiagaraEditorUtilities::AddEmitterToSystem(
        *NiagaraSystem,
        *NiagaraEmitter,
        NiagaraEmitter->GetExposedVersion().VersionGuid,
        /*bCreateCopy=*/true);

    NiagaraSystem->RequestCompile(false);
    NiagaraSystem->WaitForCompilationComplete(false, false);
    NiagaraSystem->MarkPackageDirty();
    UEditorAssetLibrary::SaveAsset(AssetPath, /*bOnlyIfIsDirty=*/false);

    TSharedPtr<FJsonObject> ReadParams = MakeShared<FJsonObject>();
    ReadParams->SetStringField(TEXT("asset_path"), AssetPath);
    ReadParams->SetStringField(TEXT("emitter_handle_id"), AddedEmitterHandleId.ToString());

    TSharedPtr<FJsonObject> Result = HandleReadNiagaraSystemEmitter(ReadParams);
    Result->SetStringField(TEXT("asset_path"), AssetPath);
    Result->SetStringField(TEXT("emitter_asset_path"), EmitterAssetPath);
    Result->SetStringField(TEXT("added_emitter_handle_id"), AddedEmitterHandleId.ToString());
    Result->SetBoolField(TEXT("added"), true);
    Result->SetBoolField(TEXT("success"), true);
    return Result;
}

// --------------------------------------------------------------------------- //
// duplicate_niagara_system_emitter
// --------------------------------------------------------------------------- //
TSharedPtr<FJsonObject> FUnrealAIAssetCommands::HandleDuplicateNiagaraSystemEmitter(const TSharedPtr<FJsonObject>& Params)
{
    FString AssetPath;
    if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath) || AssetPath.TrimStartAndEnd().IsEmpty())
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing or empty 'asset_path' parameter"));
    }
    AssetPath = AssetPath.TrimStartAndEnd();

    UNiagaraSystem* NiagaraSystem = Cast<UNiagaraSystem>(UEditorAssetLibrary::LoadAsset(AssetPath));
    if (!NiagaraSystem)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Niagara System not found or wrong class: %s"), *AssetPath));
    }

    int32 EmitterIndex = INDEX_NONE;
    FString ResolveError;
    if (!ResolveNiagaraEmitterHandleIndex(Params, *NiagaraSystem, EmitterIndex, ResolveError))
    {
        return FUnrealAICommonUtils::CreateErrorResponse(ResolveError);
    }

    TArray<FNiagaraEmitterHandle>& EmitterHandles = NiagaraSystem->GetEmitterHandles();
    if (!EmitterHandles.IsValidIndex(EmitterIndex))
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Resolved Niagara emitter index is out of bounds"));
    }

    const FNiagaraEmitterHandle& SourceEmitterHandle = EmitterHandles[EmitterIndex];
    const FVersionedNiagaraEmitter SourceVersionedEmitter = SourceEmitterHandle.GetInstance();
    if (!SourceVersionedEmitter.Emitter)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Resolved Niagara emitter handle does not have a valid emitter instance"));
    }

    const FGuid DuplicatedEmitterHandleId = FNiagaraEditorUtilities::AddEmitterToSystem(
        *NiagaraSystem,
        *SourceVersionedEmitter.Emitter,
        SourceVersionedEmitter.Version,
        /*bCreateCopy=*/true);

    FString NewEmitterName;
    Params->TryGetStringField(TEXT("new_emitter_name"), NewEmitterName);
    NewEmitterName = NewEmitterName.TrimStartAndEnd();
    if (!NewEmitterName.IsEmpty())
    {
        int32 NewEmitterIndex = INDEX_NONE;
        TSharedPtr<FJsonObject> ResolveDuplicatedParams = MakeShared<FJsonObject>();
        ResolveDuplicatedParams->SetStringField(TEXT("emitter_handle_id"), DuplicatedEmitterHandleId.ToString());
        if (!ResolveNiagaraEmitterHandleIndex(ResolveDuplicatedParams, *NiagaraSystem, NewEmitterIndex, ResolveError))
        {
            return FUnrealAICommonUtils::CreateErrorResponse(ResolveError);
        }

        NiagaraSystem->Modify();
        NiagaraSystem->GetEmitterHandles()[NewEmitterIndex].SetName(*NewEmitterName, *NiagaraSystem);
    }

    NiagaraSystem->RequestCompile(false);
    NiagaraSystem->WaitForCompilationComplete(false, false);
    NiagaraSystem->MarkPackageDirty();
    UEditorAssetLibrary::SaveAsset(AssetPath, /*bOnlyIfIsDirty=*/false);

    TSharedPtr<FJsonObject> ReadParams = MakeShared<FJsonObject>();
    ReadParams->SetStringField(TEXT("asset_path"), AssetPath);
    ReadParams->SetStringField(TEXT("emitter_handle_id"), DuplicatedEmitterHandleId.ToString());

    TSharedPtr<FJsonObject> Result = HandleReadNiagaraSystemEmitter(ReadParams);
    Result->SetStringField(TEXT("asset_path"), AssetPath);
    Result->SetStringField(TEXT("source_emitter_handle_id"), SourceEmitterHandle.GetId().ToString());
    Result->SetStringField(TEXT("duplicated_emitter_handle_id"), DuplicatedEmitterHandleId.ToString());
    Result->SetStringField(TEXT("requested_new_emitter_name"), NewEmitterName);
    Result->SetBoolField(TEXT("duplicated"), true);
    Result->SetBoolField(TEXT("success"), true);
    return Result;
}

// --------------------------------------------------------------------------- //
// rename_niagara_system_emitter
// --------------------------------------------------------------------------- //
TSharedPtr<FJsonObject> FUnrealAIAssetCommands::HandleRenameNiagaraSystemEmitter(const TSharedPtr<FJsonObject>& Params)
{
    FString AssetPath;
    if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath) || AssetPath.TrimStartAndEnd().IsEmpty())
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing or empty 'asset_path' parameter"));
    }
    AssetPath = AssetPath.TrimStartAndEnd();

    FString NewEmitterName;
    if (!Params->TryGetStringField(TEXT("new_emitter_name"), NewEmitterName) || NewEmitterName.TrimStartAndEnd().IsEmpty())
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing or empty 'new_emitter_name' parameter"));
    }
    NewEmitterName = NewEmitterName.TrimStartAndEnd();

    UNiagaraSystem* NiagaraSystem = Cast<UNiagaraSystem>(UEditorAssetLibrary::LoadAsset(AssetPath));
    if (!NiagaraSystem)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Niagara System not found or wrong class: %s"), *AssetPath));
    }

    int32 EmitterIndex = INDEX_NONE;
    FString ResolveError;
    if (!ResolveNiagaraEmitterHandleIndex(Params, *NiagaraSystem, EmitterIndex, ResolveError))
    {
        return FUnrealAICommonUtils::CreateErrorResponse(ResolveError);
    }

    TArray<FNiagaraEmitterHandle>& EmitterHandles = NiagaraSystem->GetEmitterHandles();
    if (!EmitterHandles.IsValidIndex(EmitterIndex))
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Resolved Niagara emitter index is out of bounds"));
    }

    const FString OldEmitterName = EmitterHandles[EmitterIndex].GetName().ToString();
    if (OldEmitterName.Equals(NewEmitterName, ESearchCase::CaseSensitive))
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("'new_emitter_name' must differ from the current emitter name"));
    }

    NiagaraSystem->Modify();
    EmitterHandles[EmitterIndex].SetName(*NewEmitterName, *NiagaraSystem);
    NiagaraSystem->RequestCompile(false);
    NiagaraSystem->WaitForCompilationComplete(false, false);
    NiagaraSystem->MarkPackageDirty();
    UEditorAssetLibrary::SaveAsset(AssetPath, /*bOnlyIfIsDirty=*/false);

    TSharedPtr<FJsonObject> ReadParams = MakeShared<FJsonObject>();
    ReadParams->SetStringField(TEXT("asset_path"), AssetPath);
    ReadParams->SetStringField(TEXT("emitter_handle_id"), EmitterHandles[EmitterIndex].GetId().ToString());

    TSharedPtr<FJsonObject> Result = HandleReadNiagaraSystemEmitter(ReadParams);
    Result->SetStringField(TEXT("asset_path"), AssetPath);
    Result->SetStringField(TEXT("old_emitter_name"), OldEmitterName);
    Result->SetStringField(TEXT("new_emitter_name"), NewEmitterName);
    Result->SetBoolField(TEXT("renamed"), true);
    Result->SetBoolField(TEXT("success"), true);
    return Result;
}

// --------------------------------------------------------------------------- //
// remove_niagara_system_emitter
// --------------------------------------------------------------------------- //
TSharedPtr<FJsonObject> FUnrealAIAssetCommands::HandleRemoveNiagaraSystemEmitter(const TSharedPtr<FJsonObject>& Params)
{
    FString AssetPath;
    if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath) || AssetPath.TrimStartAndEnd().IsEmpty())
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing or empty 'asset_path' parameter"));
    }
    AssetPath = AssetPath.TrimStartAndEnd();

    UNiagaraSystem* NiagaraSystem = Cast<UNiagaraSystem>(UEditorAssetLibrary::LoadAsset(AssetPath));
    if (!NiagaraSystem)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Niagara System not found or wrong class: %s"), *AssetPath));
    }

    int32 EmitterIndex = INDEX_NONE;
    FString ResolveError;
    if (!ResolveNiagaraEmitterHandleIndex(Params, *NiagaraSystem, EmitterIndex, ResolveError))
    {
        return FUnrealAICommonUtils::CreateErrorResponse(ResolveError);
    }

    TArray<FNiagaraEmitterHandle>& EmitterHandles = NiagaraSystem->GetEmitterHandles();
    if (!EmitterHandles.IsValidIndex(EmitterIndex))
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Resolved Niagara emitter index is out of bounds"));
    }

    const FString RemovedEmitterHandleId = EmitterHandles[EmitterIndex].GetId().ToString();
    const FString RemovedEmitterName = EmitterHandles[EmitterIndex].GetName().ToString();

    TSet<FGuid> RemovedEmitterHandleIds;
    RemovedEmitterHandleIds.Add(EmitterHandles[EmitterIndex].GetId());
    NiagaraSystem->Modify();
    NiagaraSystem->RemoveEmitterHandlesById(RemovedEmitterHandleIds);

    NiagaraSystem->RequestCompile(false);
    NiagaraSystem->WaitForCompilationComplete(false, false);
    NiagaraSystem->MarkPackageDirty();
    UEditorAssetLibrary::SaveAsset(AssetPath, /*bOnlyIfIsDirty=*/false);

    TSharedPtr<FJsonObject> ReadParams = MakeShared<FJsonObject>();
    ReadParams->SetStringField(TEXT("asset_path"), AssetPath);
    TSharedPtr<FJsonObject> Result = HandleReadNiagaraSystemContent(ReadParams);
    Result->SetStringField(TEXT("asset_path"), AssetPath);
    Result->SetStringField(TEXT("removed_emitter_handle_id"), RemovedEmitterHandleId);
    Result->SetStringField(TEXT("removed_emitter_name"), RemovedEmitterName);
    Result->SetBoolField(TEXT("removed"), true);
    Result->SetBoolField(TEXT("success"), true);
    return Result;
}

// --------------------------------------------------------------------------- //
// read_anim_blueprint_content
// --------------------------------------------------------------------------- //
TSharedPtr<FJsonObject> FUnrealAIAssetCommands::HandleReadAnimBlueprintContent(const TSharedPtr<FJsonObject>& Params)
{
    FString AssetPath;
    if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath) || AssetPath.TrimStartAndEnd().IsEmpty())
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing or empty 'asset_path' parameter"));
    }
    AssetPath = AssetPath.TrimStartAndEnd();

    UObject* LoadedObject = UEditorAssetLibrary::LoadAsset(AssetPath);
    UAnimBlueprint* AnimBlueprint = Cast<UAnimBlueprint>(LoadedObject);
    if (!AnimBlueprint)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("AnimBlueprint not found or wrong class: %s"), *AssetPath));
    }

    const UAnimBlueprintGeneratedClass* GeneratedClass = AnimBlueprint->GetAnimBlueprintGeneratedClass();
    const USkeleton* TargetSkeleton = AnimBlueprint->TargetSkeleton;
    const USkeletalMesh* PreviewMesh = AnimBlueprint->GetPreviewMesh();

    TArray<TSharedPtr<FJsonValue>> VariableArray;
    for (const FBPVariableDescription& Variable : AnimBlueprint->NewVariables)
    {
        TSharedPtr<FJsonObject> VariableObject = MakeShared<FJsonObject>();
        VariableObject->SetStringField(TEXT("name"), Variable.VarName.ToString());
        VariableObject->SetStringField(TEXT("type"), Variable.VarType.PinCategory.ToString());
        VariableObject->SetStringField(TEXT("default_value"), Variable.DefaultValue);
        VariableObject->SetBoolField(TEXT("is_editable"), (Variable.PropertyFlags & CPF_Edit) != 0);
        VariableArray.Add(MakeShared<FJsonValueObject>(VariableObject));
    }

    TArray<TSharedPtr<FJsonValue>> AnimLayerGraphArray;
    for (UEdGraph* Graph : AnimBlueprint->FunctionGraphs)
    {
        if (!Graph)
        {
            continue;
        }

        bool bContainsStateMachine = false;
        for (UEdGraphNode* Node : Graph->Nodes)
        {
            if (Cast<UAnimGraphNode_StateMachineBase>(Node))
            {
                bContainsStateMachine = true;
                break;
            }
        }

        TSharedPtr<FJsonObject> GraphObject = MakeShared<FJsonObject>();
        GraphObject->SetStringField(TEXT("name"), Graph->GetName());
        GraphObject->SetStringField(TEXT("path"), Graph->GetPathName());
        GraphObject->SetStringField(TEXT("schema_class"), GetGraphSchemaClassName(Graph));
        GraphObject->SetNumberField(TEXT("node_count"), Graph->Nodes.Num());
        GraphObject->SetBoolField(TEXT("contains_state_machine"), bContainsStateMachine);
        AnimLayerGraphArray.Add(MakeShared<FJsonValueObject>(GraphObject));
    }

    TArray<UEdGraph*> TopLevelGraphs;
    TopLevelGraphs.Append(AnimBlueprint->FunctionGraphs);
    TopLevelGraphs.Append(AnimBlueprint->UbergraphPages);

    TSet<const UAnimGraphNode_StateMachineBase*> SeenStateMachineNodes;
    TArray<TSharedPtr<FJsonValue>> StateMachineArray;

    for (UEdGraph* TopLevelGraph : TopLevelGraphs)
    {
        if (!TopLevelGraph)
        {
            continue;
        }

        for (UEdGraphNode* Node : TopLevelGraph->Nodes)
        {
            UAnimGraphNode_StateMachineBase* StateMachineNode = Cast<UAnimGraphNode_StateMachineBase>(Node);
            if (!StateMachineNode || SeenStateMachineNodes.Contains(StateMachineNode))
            {
                continue;
            }

            SeenStateMachineNodes.Add(StateMachineNode);

            UAnimationStateMachineGraph* StateMachineGraph = StateMachineNode->EditorStateMachineGraph;

            TSharedPtr<FJsonObject> StateMachineObject = MakeShared<FJsonObject>();
            FString StateMachineName = StateMachineNode->GetStateMachineName();
            if (StateMachineName.IsEmpty())
            {
                StateMachineName = StateMachineNode->GetNodeTitle(ENodeTitleType::ListView).ToString();
            }

            StateMachineObject->SetStringField(TEXT("name"), StateMachineName);
            StateMachineObject->SetStringField(TEXT("owner_graph_name"), TopLevelGraph->GetName());
            StateMachineObject->SetStringField(TEXT("owner_graph_path"), TopLevelGraph->GetPathName());
            StateMachineObject->SetStringField(TEXT("owner_graph_schema_class"), GetGraphSchemaClassName(TopLevelGraph));
            StateMachineObject->SetStringField(TEXT("state_machine_graph_name"), StateMachineGraph ? StateMachineGraph->GetName() : TEXT(""));
            StateMachineObject->SetStringField(TEXT("state_machine_graph_path"), StateMachineGraph ? StateMachineGraph->GetPathName() : TEXT(""));

            FString EntryStateName;
            int32 StateCount = 0;
            int32 TransitionCount = 0;
            TArray<TSharedPtr<FJsonValue>> StateArray;
            TArray<TSharedPtr<FJsonValue>> TransitionArray;

            if (StateMachineGraph)
            {
                if (StateMachineGraph->EntryNode)
                {
                    if (UEdGraphNode* EntryOutputNode = StateMachineGraph->EntryNode->GetOutputNode())
                    {
                        if (UAnimStateNodeBase* EntryStateNode = Cast<UAnimStateNodeBase>(EntryOutputNode))
                        {
                            EntryStateName = EntryStateNode->GetStateName();
                        }
                        else
                        {
                            EntryStateName = EntryOutputNode->GetNodeTitle(ENodeTitleType::ListView).ToString();
                        }
                    }
                }

                for (UEdGraphNode* StateMachineGraphNode : StateMachineGraph->Nodes)
                {
                    if (UAnimStateNode* StateNode = Cast<UAnimStateNode>(StateMachineGraphNode))
                    {
                        ++StateCount;

                        TSharedPtr<FJsonObject> StateObject = MakeShared<FJsonObject>();
                        StateObject->SetStringField(TEXT("name"), StateNode->GetStateName());
                        StateObject->SetStringField(TEXT("node_name"), StateNode->GetName());
                        StateObject->SetStringField(TEXT("title"), StateNode->GetNodeTitle(ENodeTitleType::ListView).ToString());
                        StateObject->SetStringField(TEXT("state_type"), GetAnimStateTypeName(StateNode));
                        StateObject->SetBoolField(TEXT("always_reset_on_entry"), StateNode->bAlwaysResetOnEntry);
                        StateObject->SetStringField(TEXT("bound_graph_name"), StateNode->BoundGraph ? StateNode->BoundGraph->GetName() : TEXT(""));
                        StateObject->SetStringField(TEXT("bound_graph_path"), StateNode->BoundGraph ? StateNode->BoundGraph->GetPathName() : TEXT(""));
                        StateObject->SetStringField(TEXT("bound_graph_schema_class"), GetGraphSchemaClassName(StateNode->BoundGraph));
                        StateObject->SetNumberField(TEXT("bound_graph_node_count"), StateNode->BoundGraph ? StateNode->BoundGraph->Nodes.Num() : 0);

                        TSharedPtr<FJsonObject> AssetPlayerSummary = BuildAnimStateAssetPlayerSummary(StateNode);
                        StateObject->SetObjectField(TEXT("asset_player_summary"), AssetPlayerSummary.ToSharedRef());
                        StateObject->SetStringField(TEXT("asset_player_binding_type"), AssetPlayerSummary->GetStringField(TEXT("binding_type")));
                        StateObject->SetStringField(TEXT("animation_asset_name"), AssetPlayerSummary->GetStringField(TEXT("asset_name")));
                        StateObject->SetStringField(TEXT("animation_asset_path"), AssetPlayerSummary->GetStringField(TEXT("asset_path")));
                        StateObject->SetStringField(TEXT("animation_asset_class"), AssetPlayerSummary->GetStringField(TEXT("asset_class")));
                        StateObject->SetStringField(TEXT("asset_player_node_class"), AssetPlayerSummary->GetStringField(TEXT("node_class")));
                        StateObject->SetStringField(TEXT("asset_player_node_name"), AssetPlayerSummary->GetStringField(TEXT("node_name")));
                        StateObject->SetStringField(TEXT("asset_player_node_title"), AssetPlayerSummary->GetStringField(TEXT("node_title")));
                        StateObject->SetNumberField(TEXT("asset_player_count"), AssetPlayerSummary->GetNumberField(TEXT("asset_player_count")));
                        StateObject->SetBoolField(TEXT("asset_player_is_supported_pattern"), AssetPlayerSummary->GetBoolField(TEXT("is_supported_pattern")));
                        StateObject->SetBoolField(TEXT("asset_player_is_connected"), AssetPlayerSummary->GetBoolField(TEXT("is_connected")));
                        StateObject->SetBoolField(TEXT("asset_player_loop"), AssetPlayerSummary->GetBoolField(TEXT("loop")));
                        StateObject->SetNumberField(TEXT("asset_player_play_rate"), AssetPlayerSummary->GetNumberField(TEXT("play_rate")));
                        StateObject->SetNumberField(TEXT("asset_player_start_position"), AssetPlayerSummary->GetNumberField(TEXT("start_position")));
                        StateObject->SetNumberField(TEXT("asset_player_blend_space_x"), AssetPlayerSummary->GetNumberField(TEXT("blend_space_x")));
                        StateObject->SetNumberField(TEXT("asset_player_blend_space_y"), AssetPlayerSummary->GetNumberField(TEXT("blend_space_y")));
                        StateObject->SetStringField(TEXT("asset_player_sync_group_name"), AssetPlayerSummary->GetStringField(TEXT("sync_group_name")));
                        StateObject->SetStringField(TEXT("asset_player_sync_group_role"), AssetPlayerSummary->GetStringField(TEXT("sync_group_role")));
                        StateObject->SetStringField(TEXT("asset_player_sync_group_method"), AssetPlayerSummary->GetStringField(TEXT("sync_group_method")));
                        StateObject->SetBoolField(
                            TEXT("asset_player_sync_group_override_position_when_joining_sync_group_as_leader"),
                            AssetPlayerSummary->GetBoolField(TEXT("sync_group_override_position_when_joining_sync_group_as_leader")));
                        StateArray.Add(MakeShared<FJsonValueObject>(StateObject));
                    }
                    else if (UAnimStateTransitionNode* TransitionNode = Cast<UAnimStateTransitionNode>(StateMachineGraphNode))
                    {
                        ++TransitionCount;

                        const UAnimStateNodeBase* PreviousState = TransitionNode->GetPreviousState();
                        const UAnimStateNodeBase* NextState = TransitionNode->GetNextState();

                        TSharedPtr<FJsonObject> TransitionObject = MakeShared<FJsonObject>();
                        TransitionObject->SetStringField(TEXT("name"), TransitionNode->GetNodeTitle(ENodeTitleType::ListView).ToString());
                        TransitionObject->SetStringField(TEXT("source_state_name"), PreviousState ? PreviousState->GetStateName() : TEXT(""));
                        TransitionObject->SetStringField(TEXT("target_state_name"), NextState ? NextState->GetStateName() : TEXT(""));
                        TransitionObject->SetNumberField(TEXT("priority_order"), TransitionNode->PriorityOrder);
                        TransitionObject->SetNumberField(TEXT("crossfade_duration"), TransitionNode->CrossfadeDuration);
                        TransitionObject->SetBoolField(TEXT("automatic_rule_based_on_sequence_player_in_state"), TransitionNode->bAutomaticRuleBasedOnSequencePlayerInState);
                        TransitionObject->SetNumberField(TEXT("automatic_rule_trigger_time"), TransitionNode->AutomaticRuleTriggerTime);
                        TransitionObject->SetBoolField(TEXT("bidirectional"), TransitionNode->Bidirectional);
                        TransitionObject->SetBoolField(TEXT("disabled"), TransitionNode->bDisabled);
                        TransitionObject->SetNumberField(TEXT("logic_type_value"), static_cast<int32>(TransitionNode->LogicType.GetValue()));
                        TransitionObject->SetStringField(TEXT("bound_graph_name"), TransitionNode->BoundGraph ? TransitionNode->BoundGraph->GetName() : TEXT(""));
                        TransitionObject->SetStringField(TEXT("bound_graph_path"), TransitionNode->BoundGraph ? TransitionNode->BoundGraph->GetPathName() : TEXT(""));
                        TransitionObject->SetStringField(TEXT("bound_graph_schema_class"), GetGraphSchemaClassName(TransitionNode->BoundGraph));
                        TransitionObject->SetNumberField(TEXT("bound_graph_node_count"), TransitionNode->BoundGraph ? TransitionNode->BoundGraph->Nodes.Num() : 0);
                        TransitionObject->SetStringField(TEXT("custom_transition_graph_name"), TransitionNode->GetCustomTransitionGraph() ? TransitionNode->GetCustomTransitionGraph()->GetName() : TEXT(""));
                        TransitionObject->SetStringField(TEXT("custom_transition_graph_path"), TransitionNode->GetCustomTransitionGraph() ? TransitionNode->GetCustomTransitionGraph()->GetPathName() : TEXT(""));
                        TransitionObject->SetNumberField(TEXT("custom_transition_graph_node_count"), TransitionNode->GetCustomTransitionGraph() ? TransitionNode->GetCustomTransitionGraph()->Nodes.Num() : 0);

                        TSharedPtr<FJsonObject> RuleSummary = BuildAnimTransitionRuleSummary(TransitionNode);
                        TransitionObject->SetObjectField(TEXT("rule_summary"), RuleSummary.ToSharedRef());
                        TransitionObject->SetStringField(TEXT("rule_type"), RuleSummary->GetStringField(TEXT("rule_type")));
                        TransitionObject->SetStringField(TEXT("rule_variable_name"), RuleSummary->GetStringField(TEXT("variable_name")));
                        TransitionObject->SetStringField(TEXT("rule_expected_value"), RuleSummary->GetStringField(TEXT("expected_value")));
                        TransitionObject->SetStringField(TEXT("rule_property_kind"), RuleSummary->GetStringField(TEXT("property_kind")));
                        TransitionObject->SetStringField(TEXT("rule_enum_path"), RuleSummary->GetStringField(TEXT("enum_path")));
                        TransitionObject->SetBoolField(TEXT("rule_is_supported_pattern"), RuleSummary->GetBoolField(TEXT("is_supported_pattern")));
                        TransitionObject->SetBoolField(TEXT("rule_is_connected"), RuleSummary->GetBoolField(TEXT("is_connected")));
                        TransitionArray.Add(MakeShared<FJsonValueObject>(TransitionObject));
                    }
                }
            }

            StateMachineObject->SetStringField(TEXT("entry_state_name"), EntryStateName);
            StateMachineObject->SetNumberField(TEXT("state_count"), StateCount);
            StateMachineObject->SetNumberField(TEXT("transition_count"), TransitionCount);
            StateMachineObject->SetArrayField(TEXT("states"), StateArray);
            StateMachineObject->SetArrayField(TEXT("transitions"), TransitionArray);
            StateMachineArray.Add(MakeShared<FJsonValueObject>(StateMachineObject));
        }
    }

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("asset_path"), AssetPath);
    Result->SetStringField(TEXT("name"), AnimBlueprint->GetName());
    Result->SetStringField(TEXT("parent_class_name"), AnimBlueprint->ParentClass ? AnimBlueprint->ParentClass->GetName() : TEXT(""));
    Result->SetStringField(TEXT("parent_class_path"), AnimBlueprint->ParentClass ? AnimBlueprint->ParentClass->GetPathName() : TEXT(""));
    Result->SetStringField(TEXT("generated_class_name"), GeneratedClass ? GeneratedClass->GetName() : TEXT(""));
    Result->SetStringField(TEXT("generated_class_path"), GeneratedClass ? GeneratedClass->GetPathName() : TEXT(""));
    Result->SetStringField(TEXT("target_skeleton_name"), TargetSkeleton ? TargetSkeleton->GetName() : TEXT(""));
    Result->SetStringField(TEXT("target_skeleton_path"), TargetSkeleton ? TargetSkeleton->GetPathName() : TEXT(""));
    Result->SetStringField(TEXT("preview_skeletal_mesh_name"), PreviewMesh ? PreviewMesh->GetName() : TEXT(""));
    Result->SetStringField(TEXT("preview_skeletal_mesh_path"), PreviewMesh ? PreviewMesh->GetPathName() : TEXT(""));
    Result->SetBoolField(TEXT("is_template"), AnimBlueprint->bIsTemplate);
    Result->SetBoolField(TEXT("supports_anim_layers"), AnimBlueprint->SupportsAnimLayers());
    Result->SetBoolField(TEXT("supports_event_graphs"), AnimBlueprint->SupportsEventGraphs());
    Result->SetBoolField(TEXT("supports_macros"), AnimBlueprint->SupportsMacros());
    Result->SetBoolField(TEXT("supports_input_events"), AnimBlueprint->SupportsInputEvents());
    Result->SetArrayField(TEXT("variables"), VariableArray);
    Result->SetNumberField(TEXT("variable_count"), VariableArray.Num());
    Result->SetArrayField(TEXT("anim_layer_graphs"), AnimLayerGraphArray);
    Result->SetNumberField(TEXT("anim_layer_graph_count"), AnimLayerGraphArray.Num());
    Result->SetArrayField(TEXT("state_machines"), StateMachineArray);
    Result->SetNumberField(TEXT("state_machine_count"), StateMachineArray.Num());
    return Result;
}

// --------------------------------------------------------------------------- //
// create_anim_blueprint_asset
// --------------------------------------------------------------------------- //
TSharedPtr<FJsonObject> FUnrealAIAssetCommands::HandleCreateAnimBlueprintAsset(const TSharedPtr<FJsonObject>& Params)
{
    FString AnimBlueprintName;
    if (!Params->TryGetStringField(TEXT("anim_blueprint_name"), AnimBlueprintName) || AnimBlueprintName.TrimStartAndEnd().IsEmpty())
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing or empty 'anim_blueprint_name' parameter"));
    }
    AnimBlueprintName = AnimBlueprintName.TrimStartAndEnd();

    bool bIsTemplate = false;
    Params->TryGetBoolField(TEXT("is_template"), bIsTemplate);

    FString SkeletonPath;
    Params->TryGetStringField(TEXT("skeleton_path"), SkeletonPath);
    SkeletonPath = SkeletonPath.TrimStartAndEnd();

    if (bIsTemplate && !SkeletonPath.IsEmpty())
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Template AnimBlueprint assets cannot specify 'skeleton_path'"));
    }
    if (!bIsTemplate && SkeletonPath.IsEmpty())
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing or empty 'skeleton_path' parameter"));
    }

    FString DestinationPath = TEXT("/Game/AnimBlueprints");
    Params->TryGetStringField(TEXT("destination_path"), DestinationPath);
    DestinationPath = DestinationPath.TrimStartAndEnd();
    if (DestinationPath.IsEmpty())
    {
        DestinationPath = TEXT("/Game/AnimBlueprints");
    }

    UClass* ParentClass = UAnimInstance::StaticClass();
    FString ParentClassInput;
    if (!Params->TryGetStringField(TEXT("parent_class_path"), ParentClassInput) || ParentClassInput.TrimStartAndEnd().IsEmpty())
    {
        Params->TryGetStringField(TEXT("parent_class"), ParentClassInput);
    }
    ParentClassInput = ParentClassInput.TrimStartAndEnd();

    if (!ParentClassInput.IsEmpty())
    {
        TArray<FString> CandidatePaths;
        CandidatePaths.Add(ParentClassInput);
        if (!ParentClassInput.StartsWith(TEXT("/")))
        {
            FString EngineClassName = ParentClassInput;
            if (!EngineClassName.StartsWith(TEXT("U")))
            {
                EngineClassName = TEXT("U") + EngineClassName;
            }
            CandidatePaths.Add(FString::Printf(TEXT("/Script/Engine.%s"), *EngineClassName));
        }

        ParentClass = nullptr;
        for (const FString& CandidatePath : CandidatePaths)
        {
            ParentClass = FindObject<UClass>(nullptr, *CandidatePath);
            if (!ParentClass)
            {
                ParentClass = LoadClass<UAnimInstance>(nullptr, *CandidatePath);
            }
            if (ParentClass)
            {
                break;
            }
        }

        if (!ParentClass)
        {
            return FUnrealAICommonUtils::CreateErrorResponse(
                FString::Printf(TEXT("Failed to load AnimBlueprint parent class: %s"), *ParentClassInput));
        }
        if (!ParentClass->IsChildOf(UAnimInstance::StaticClass()))
        {
            return FUnrealAICommonUtils::CreateErrorResponse(
                FString::Printf(TEXT("AnimBlueprint parent class must derive from UAnimInstance: %s"), *ParentClass->GetPathName()));
        }
    }

    USkeleton* TargetSkeleton = nullptr;
    if (!SkeletonPath.IsEmpty())
    {
        UObject* LoadedSkeletonObject = UEditorAssetLibrary::LoadAsset(SkeletonPath);
        TargetSkeleton = Cast<USkeleton>(LoadedSkeletonObject);
        if (!TargetSkeleton)
        {
            return FUnrealAICommonUtils::CreateErrorResponse(
                FString::Printf(TEXT("Failed to load skeleton asset: %s"), *SkeletonPath));
        }
    }

    FString PreviewSkeletalMeshPath;
    Params->TryGetStringField(TEXT("preview_skeletal_mesh_path"), PreviewSkeletalMeshPath);
    PreviewSkeletalMeshPath = PreviewSkeletalMeshPath.TrimStartAndEnd();

    USkeletalMesh* PreviewSkeletalMesh = nullptr;
    if (!PreviewSkeletalMeshPath.IsEmpty())
    {
        UObject* LoadedPreviewMeshObject = UEditorAssetLibrary::LoadAsset(PreviewSkeletalMeshPath);
        PreviewSkeletalMesh = Cast<USkeletalMesh>(LoadedPreviewMeshObject);
        if (!PreviewSkeletalMesh)
        {
            return FUnrealAICommonUtils::CreateErrorResponse(
                FString::Printf(TEXT("Failed to load preview skeletal mesh asset: %s"), *PreviewSkeletalMeshPath));
        }
    }

    if (!UEditorAssetLibrary::DoesDirectoryExist(DestinationPath))
    {
        UEditorAssetLibrary::MakeDirectory(DestinationPath);
    }

    const FString FullObjectPath = FString::Printf(TEXT("%s/%s.%s"), *DestinationPath, *AnimBlueprintName, *AnimBlueprintName);
    if (UEditorAssetLibrary::DoesAssetExist(FullObjectPath))
    {
        return FUnrealAICommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Asset already exists: %s"), *FullObjectPath));
    }

    FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools"));
    IAssetTools& AssetTools = AssetToolsModule.Get();

    UAnimBlueprintFactory* Factory = NewObject<UAnimBlueprintFactory>();
    Factory->BlueprintType = BPTYPE_Normal;
    Factory->ParentClass = ParentClass;
    Factory->TargetSkeleton = TargetSkeleton;
    Factory->PreviewSkeletalMesh = PreviewSkeletalMesh;
    Factory->bTemplate = bIsTemplate;

    UObject* NewAsset = AssetTools.CreateAsset(AnimBlueprintName, DestinationPath, UAnimBlueprint::StaticClass(), Factory);
    UAnimBlueprint* AnimBlueprint = Cast<UAnimBlueprint>(NewAsset);
    if (!AnimBlueprint)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Failed to create AnimBlueprint asset"));
    }

    AnimBlueprint->MarkPackageDirty();
    UEditorAssetLibrary::SaveAsset(FullObjectPath, /*bOnlyIfIsDirty=*/false);

    TSharedPtr<FJsonObject> ReadParams = MakeShared<FJsonObject>();
    ReadParams->SetStringField(TEXT("asset_path"), FullObjectPath);

    TSharedPtr<FJsonObject> Result = HandleReadAnimBlueprintContent(ReadParams);
    if (!Result.IsValid())
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Failed to read back AnimBlueprint after create"));
    }
    if (Result->HasField(TEXT("error")))
    {
        return Result;
    }

    Result->SetStringField(TEXT("anim_blueprint_name"), AnimBlueprintName);
    Result->SetStringField(TEXT("destination_path"), DestinationPath);
    Result->SetStringField(TEXT("asset_path"), FullObjectPath);
    Result->SetStringField(TEXT("requested_skeleton_path"), SkeletonPath);
    Result->SetStringField(TEXT("requested_parent_class_path"), ParentClass ? ParentClass->GetPathName() : TEXT(""));
    Result->SetStringField(TEXT("requested_preview_skeletal_mesh_path"), PreviewSkeletalMeshPath);
    Result->SetBoolField(TEXT("created"), true);
    Result->SetBoolField(TEXT("success"), true);
    return Result;
}

// --------------------------------------------------------------------------- //
// validate_anim_blueprint
// --------------------------------------------------------------------------- //
TSharedPtr<FJsonObject> FUnrealAIAssetCommands::HandleValidateAnimBlueprint(const TSharedPtr<FJsonObject>& Params)
{
    FString AssetPath;
    if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath) || AssetPath.TrimStartAndEnd().IsEmpty())
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing or empty 'asset_path' parameter"));
    }
    AssetPath = AssetPath.TrimStartAndEnd();

    UObject* LoadedObject = UEditorAssetLibrary::LoadAsset(AssetPath);
    UAnimBlueprint* AnimBlueprint = Cast<UAnimBlueprint>(LoadedObject);
    if (!AnimBlueprint)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("AnimBlueprint not found or wrong class: %s"), *AssetPath));
    }

    FKismetEditorUtilities::CompileBlueprint(AnimBlueprint);

    TSharedPtr<FJsonObject> ReadParams = MakeShared<FJsonObject>();
    ReadParams->SetStringField(TEXT("asset_path"), AssetPath);

    TSharedPtr<FJsonObject> Readback = HandleReadAnimBlueprintContent(ReadParams);

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    if (Readback.IsValid() && !Readback->HasField(TEXT("error")))
    {
        Result = CopyJsonObject(Readback);
    }

    Result->SetStringField(TEXT("asset_path"), AssetPath);
    Result->SetStringField(TEXT("validation_type"), TEXT("AnimBlueprint"));
    Result->SetStringField(TEXT("name"), AnimBlueprint->GetName());

    const USkeleton* TargetSkeleton = AnimBlueprint->TargetSkeleton;
    const USkeletalMesh* PreviewMesh = AnimBlueprint->GetPreviewMesh();
    Result->SetStringField(TEXT("target_skeleton_name"), TargetSkeleton ? TargetSkeleton->GetName() : TEXT(""));
    Result->SetStringField(TEXT("target_skeleton_path"), TargetSkeleton ? TargetSkeleton->GetPathName() : TEXT(""));
    Result->SetStringField(TEXT("preview_skeletal_mesh_name"), PreviewMesh ? PreviewMesh->GetName() : TEXT(""));
    Result->SetStringField(TEXT("preview_skeletal_mesh_path"), PreviewMesh ? PreviewMesh->GetPathName() : TEXT(""));

    TArray<TSharedPtr<FJsonValue>> Issues;
    int32 ErrorCount = 0;
    int32 WarningCount = 0;
    int32 CheckedAssetPlayerCount = 0;

    const EBlueprintStatus CompileStatus = AnimBlueprint->Status;
    const FString CompileStatusName = GetBlueprintStatusName(CompileStatus);
    Result->SetStringField(TEXT("compile_status"), CompileStatusName);
    Result->SetBoolField(TEXT("compile_invoked"), true);
    Result->SetBoolField(TEXT("compile_healthy"), IsBlueprintCompileHealthy(CompileStatus));

    if (CompileStatus == BS_Error)
    {
        AddValidationIssue(
            Issues,
            ErrorCount,
            WarningCount,
            TEXT("error"),
            TEXT("compile_failed"),
            TEXT("AnimBlueprint compile finished with errors"));
    }
    else if (CompileStatus == BS_UpToDateWithWarnings)
    {
        AddValidationIssue(
            Issues,
            ErrorCount,
            WarningCount,
            TEXT("warning"),
            TEXT("compile_warnings"),
            TEXT("AnimBlueprint compile finished with warnings"));
    }
    else if (!IsBlueprintCompileHealthy(CompileStatus))
    {
        AddValidationIssue(
            Issues,
            ErrorCount,
            WarningCount,
            TEXT("error"),
            TEXT("compile_not_up_to_date"),
            FString::Printf(TEXT("AnimBlueprint compile status is not healthy: %s"), *CompileStatusName));
    }

    Result->SetStringField(
        TEXT("generated_class_path"),
        AnimBlueprint->GeneratedClass ? AnimBlueprint->GeneratedClass->GetPathName() : TEXT(""));
    Result->SetBoolField(TEXT("generated_class_present"), AnimBlueprint->GeneratedClass != nullptr);
    if (!AnimBlueprint->GeneratedClass)
    {
        AddValidationIssue(
            Issues,
            ErrorCount,
            WarningCount,
            TEXT("error"),
            TEXT("missing_generated_class"),
            TEXT("AnimBlueprint is missing its generated class after compile"),
            TEXT("generated_class_path"));
    }

    if (!TargetSkeleton)
    {
        AddValidationIssue(
            Issues,
            ErrorCount,
            WarningCount,
            TEXT("error"),
            TEXT("missing_target_skeleton"),
            TEXT("AnimBlueprint is missing its target skeleton"),
            TEXT("target_skeleton_path"));
    }

    bool bPreviewMeshMatchesTargetSkeleton = false;
    if (PreviewMesh && TargetSkeleton)
    {
        const USkeleton* PreviewMeshSkeleton = PreviewMesh->GetSkeleton();
        bPreviewMeshMatchesTargetSkeleton = PreviewMeshSkeleton == TargetSkeleton;
        if (!PreviewMeshSkeleton)
        {
            AddValidationIssue(
                Issues,
                ErrorCount,
                WarningCount,
                TEXT("error"),
                TEXT("preview_mesh_missing_skeleton"),
                FString::Printf(TEXT("Preview skeletal mesh '%s' is missing a skeleton"), *PreviewMesh->GetPathName()),
                TEXT("preview_skeletal_mesh_path"));
        }
        else if (!bPreviewMeshMatchesTargetSkeleton)
        {
            AddValidationIssue(
                Issues,
                ErrorCount,
                WarningCount,
                TEXT("error"),
                TEXT("preview_mesh_skeleton_mismatch"),
                FString::Printf(TEXT("Preview skeletal mesh skeleton '%s' does not match AnimBlueprint target skeleton '%s'"),
                    *PreviewMeshSkeleton->GetPathName(),
                    *TargetSkeleton->GetPathName()),
                TEXT("preview_skeletal_mesh_path"));
        }
    }
    Result->SetBoolField(TEXT("preview_mesh_matches_target_skeleton"), bPreviewMeshMatchesTargetSkeleton);

    const bool bReadbackHealthy = Readback.IsValid() && !Readback->HasField(TEXT("error"));
    Result->SetBoolField(TEXT("readback_healthy"), bReadbackHealthy);
    if (!bReadbackHealthy)
    {
        const FString ReadbackError = (Readback.IsValid() && Readback->HasField(TEXT("error")))
            ? Readback->GetStringField(TEXT("error"))
            : TEXT("Unknown AnimBlueprint readback failure");
        AddValidationIssue(
            Issues,
            ErrorCount,
            WarningCount,
            TEXT("error"),
            TEXT("readback_failed"),
            FString::Printf(TEXT("AnimBlueprint readback failed after compile: %s"), *ReadbackError));
    }

    if (bReadbackHealthy)
    {
        const TArray<TSharedPtr<FJsonValue>>* StateMachines = nullptr;
        if (Result->TryGetArrayField(TEXT("state_machines"), StateMachines) && StateMachines)
        {
            for (const TSharedPtr<FJsonValue>& StateMachineValue : *StateMachines)
            {
                const TSharedPtr<FJsonObject>* StateMachineObject = nullptr;
                if (!StateMachineValue.IsValid() || !StateMachineValue->TryGetObject(StateMachineObject) || !StateMachineObject || !(*StateMachineObject).IsValid())
                {
                    continue;
                }

                FString StateMachineName;
                (*StateMachineObject)->TryGetStringField(TEXT("name"), StateMachineName);

                const TArray<TSharedPtr<FJsonValue>>* States = nullptr;
                if (!(*StateMachineObject)->TryGetArrayField(TEXT("states"), States) || !States)
                {
                    continue;
                }

                for (const TSharedPtr<FJsonValue>& StateValue : *States)
                {
                    const TSharedPtr<FJsonObject>* StateObject = nullptr;
                    if (!StateValue.IsValid() || !StateValue->TryGetObject(StateObject) || !StateObject || !(*StateObject).IsValid())
                    {
                        continue;
                    }

                    FString StateName;
                    (*StateObject)->TryGetStringField(TEXT("name"), StateName);
                    const FString StateField = FString::Printf(TEXT("state_machines.%s.states.%s"), *StateMachineName, *StateName);

                    FString BindingType;
                    (*StateObject)->TryGetStringField(TEXT("asset_player_binding_type"), BindingType);

                    double AssetPlayerCountValue = 0.0;
                    (*StateObject)->TryGetNumberField(TEXT("asset_player_count"), AssetPlayerCountValue);
                    const int32 AssetPlayerCount = static_cast<int32>(AssetPlayerCountValue);

                    bool bSupportedPattern = false;
                    (*StateObject)->TryGetBoolField(TEXT("asset_player_is_supported_pattern"), bSupportedPattern);
                    bool bConnectedPattern = false;
                    (*StateObject)->TryGetBoolField(TEXT("asset_player_is_connected"), bConnectedPattern);

                    if (AssetPlayerCount > 0 && !bSupportedPattern)
                    {
                        AddValidationIssue(
                            Issues,
                            ErrorCount,
                            WarningCount,
                            TEXT("warning"),
                            TEXT("unsupported_asset_player_pattern"),
                            FString::Printf(TEXT("State '%s' in state machine '%s' does not use the supported single asset-player graph pattern"),
                                *StateName,
                                *StateMachineName),
                            StateField);
                    }

                    if (AssetPlayerCount > 0 && !bConnectedPattern)
                    {
                        AddValidationIssue(
                            Issues,
                            ErrorCount,
                            WarningCount,
                            TEXT("warning"),
                            TEXT("disconnected_asset_player"),
                            FString::Printf(TEXT("State '%s' in state machine '%s' has an asset-player node that is not connected to the state result"),
                                *StateName,
                                *StateMachineName),
                            StateField);
                    }

                    if (BindingType != TEXT("sequence_player") &&
                        BindingType != TEXT("blend_space_player") &&
                        BindingType != TEXT("aim_offset_player"))
                    {
                        continue;
                    }

                    ++CheckedAssetPlayerCount;

                    FString AnimationAssetPath;
                    (*StateObject)->TryGetStringField(TEXT("animation_asset_path"), AnimationAssetPath);
                    AnimationAssetPath = AnimationAssetPath.TrimStartAndEnd();

                    if (AnimationAssetPath.IsEmpty())
                    {
                        AddValidationIssue(
                            Issues,
                            ErrorCount,
                            WarningCount,
                            TEXT("error"),
                            TEXT("unresolved_asset_player_asset"),
                            FString::Printf(TEXT("State '%s' in state machine '%s' has a '%s' node with no resolved animation asset"),
                                *StateName,
                                *StateMachineName,
                                *BindingType),
                            StateField);
                        continue;
                    }

                    UObject* StateAssetObject = UEditorAssetLibrary::LoadAsset(AnimationAssetPath);
                    UAnimationAsset* AnimationAsset = Cast<UAnimationAsset>(StateAssetObject);
                    if (!AnimationAsset)
                    {
                        AddValidationIssue(
                            Issues,
                            ErrorCount,
                            WarningCount,
                            TEXT("error"),
                            TEXT("invalid_animation_asset"),
                            FString::Printf(TEXT("State '%s' in state machine '%s' resolves to a non-animation asset: %s"),
                                *StateName,
                                *StateMachineName,
                                *AnimationAssetPath),
                            StateField);
                        continue;
                    }

                    bool bBindingTypeMatchesAssetClass = false;
                    if (BindingType == TEXT("sequence_player"))
                    {
                        bBindingTypeMatchesAssetClass = Cast<UAnimSequenceBase>(AnimationAsset) != nullptr;
                    }
                    else if (BindingType == TEXT("blend_space_player"))
                    {
                        bBindingTypeMatchesAssetClass =
                            Cast<UBlendSpace>(AnimationAsset) != nullptr &&
                            Cast<UAimOffsetBlendSpace>(AnimationAsset) == nullptr &&
                            Cast<UAimOffsetBlendSpace1D>(AnimationAsset) == nullptr;
                    }
                    else if (BindingType == TEXT("aim_offset_player"))
                    {
                        bBindingTypeMatchesAssetClass =
                            Cast<UAimOffsetBlendSpace>(AnimationAsset) != nullptr ||
                            Cast<UAimOffsetBlendSpace1D>(AnimationAsset) != nullptr;
                    }

                    if (!bBindingTypeMatchesAssetClass)
                    {
                        AddValidationIssue(
                            Issues,
                            ErrorCount,
                            WarningCount,
                            TEXT("error"),
                            TEXT("binding_type_asset_class_mismatch"),
                            FString::Printf(TEXT("State '%s' in state machine '%s' uses binding type '%s' with incompatible asset class '%s'"),
                                *StateName,
                                *StateMachineName,
                                *BindingType,
                                *AnimationAsset->GetClass()->GetPathName()),
                            StateField);
                    }

                    if (TargetSkeleton)
                    {
                        const USkeleton* AnimationAssetSkeleton = AnimationAsset->GetSkeleton();
                        if (!AnimationAssetSkeleton)
                        {
                            AddValidationIssue(
                                Issues,
                                ErrorCount,
                                WarningCount,
                                TEXT("error"),
                                TEXT("missing_animation_asset_skeleton"),
                                FString::Printf(TEXT("Animation asset '%s' used by state '%s' in state machine '%s' is missing a skeleton"),
                                    *AnimationAssetPath,
                                    *StateName,
                                    *StateMachineName),
                                StateField);
                        }
                        else if (AnimationAssetSkeleton != TargetSkeleton)
                        {
                            AddValidationIssue(
                                Issues,
                                ErrorCount,
                                WarningCount,
                                TEXT("error"),
                                TEXT("incompatible_asset_skeleton"),
                                FString::Printf(TEXT("Animation asset skeleton '%s' does not match AnimBlueprint target skeleton '%s' for state '%s' in state machine '%s'"),
                                    *AnimationAssetSkeleton->GetPathName(),
                                    *TargetSkeleton->GetPathName(),
                                    *StateName,
                                    *StateMachineName),
                                StateField);
                        }
                    }
                }
            }
        }
    }

    Result->SetNumberField(TEXT("checked_asset_player_count"), CheckedAssetPlayerCount);
    Result->SetBoolField(TEXT("target_skeleton_present"), TargetSkeleton != nullptr);
    Result->SetBoolField(TEXT("preview_skeletal_mesh_present"), PreviewMesh != nullptr);
    Result->SetArrayField(TEXT("issues"), Issues);
    Result->SetNumberField(TEXT("error_count"), ErrorCount);
    Result->SetNumberField(TEXT("warning_count"), WarningCount);
    Result->SetBoolField(TEXT("skeleton_healthy"), TargetSkeleton != nullptr && ErrorCount == 0);
    Result->SetBoolField(TEXT("asset_player_health_checked"), bReadbackHealthy);
    Result->SetBoolField(TEXT("asset_player_healthy"), bReadbackHealthy && ErrorCount == 0);
    Result->SetBoolField(TEXT("is_valid"), ErrorCount == 0);
    Result->SetBoolField(TEXT("success"), true);
    return Result;
}

// --------------------------------------------------------------------------- //
// read_anim_state_machine
// --------------------------------------------------------------------------- //
TSharedPtr<FJsonObject> FUnrealAIAssetCommands::HandleReadAnimStateMachine(const TSharedPtr<FJsonObject>& Params)
{
    FString AssetPath;
    if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath) || AssetPath.TrimStartAndEnd().IsEmpty())
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing or empty 'asset_path' parameter"));
    }
    AssetPath = AssetPath.TrimStartAndEnd();

    FString StateMachineName;
    if (!Params->TryGetStringField(TEXT("state_machine_name"), StateMachineName) || StateMachineName.TrimStartAndEnd().IsEmpty())
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing or empty 'state_machine_name' parameter"));
    }
    StateMachineName = StateMachineName.TrimStartAndEnd();

    TSharedPtr<FJsonObject> ReadParams = MakeShared<FJsonObject>();
    ReadParams->SetStringField(TEXT("asset_path"), AssetPath);

    TSharedPtr<FJsonObject> AnimBlueprintContent = HandleReadAnimBlueprintContent(ReadParams);
    if (!AnimBlueprintContent.IsValid())
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Failed to read AnimBlueprint content"));
    }
    if (AnimBlueprintContent->HasField(TEXT("error")))
    {
        return AnimBlueprintContent;
    }

    TArray<FString> AvailableStateMachineNames;
    TSharedPtr<FJsonObject> MatchingStateMachine;
    if (!TryGetMatchingAnimStateMachine(AnimBlueprintContent, StateMachineName, MatchingStateMachine, &AvailableStateMachineNames) || !MatchingStateMachine.IsValid())
    {
        FString AvailableNamesMessage = TEXT("none");
        if (AvailableStateMachineNames.Num() > 0)
        {
            AvailableNamesMessage = FString::Join(AvailableStateMachineNames, TEXT(", "));
        }

        return FUnrealAICommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("State machine not found in AnimBlueprint: %s (available: %s)"),
                *StateMachineName,
                *AvailableNamesMessage));
    }

    TSharedPtr<FJsonObject> Result = CopyJsonObject(MatchingStateMachine);
    Result->SetStringField(TEXT("asset_path"), AssetPath);
    Result->SetStringField(TEXT("anim_blueprint_name"), AnimBlueprintContent->GetStringField(TEXT("name")));
    Result->SetStringField(TEXT("anim_blueprint_parent_class_name"), AnimBlueprintContent->GetStringField(TEXT("parent_class_name")));
    Result->SetStringField(TEXT("target_skeleton_path"), AnimBlueprintContent->GetStringField(TEXT("target_skeleton_path")));
    Result->SetStringField(TEXT("state_machine_name"), MatchingStateMachine->GetStringField(TEXT("name")));
    Result->SetObjectField(TEXT("state_machine"), MatchingStateMachine.ToSharedRef());
    return Result;
}

// --------------------------------------------------------------------------- //
// create_anim_state_machine
// --------------------------------------------------------------------------- //
TSharedPtr<FJsonObject> FUnrealAIAssetCommands::HandleCreateAnimStateMachine(const TSharedPtr<FJsonObject>& Params)
{
    FString AssetPath;
    if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath) || AssetPath.TrimStartAndEnd().IsEmpty())
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing or empty 'asset_path' parameter"));
    }
    AssetPath = AssetPath.TrimStartAndEnd();

    FString StateMachineName;
    if (!Params->TryGetStringField(TEXT("state_machine_name"), StateMachineName) || StateMachineName.TrimStartAndEnd().IsEmpty())
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing or empty 'state_machine_name' parameter"));
    }
    StateMachineName = StateMachineName.TrimStartAndEnd();

    FString GraphName;
    Params->TryGetStringField(TEXT("graph_name"), GraphName);
    GraphName = GraphName.TrimStartAndEnd();

    UObject* LoadedObject = UEditorAssetLibrary::LoadAsset(AssetPath);
    UAnimBlueprint* AnimBlueprint = Cast<UAnimBlueprint>(LoadedObject);
    if (!AnimBlueprint)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("AnimBlueprint not found or wrong class: %s"), *AssetPath));
    }

    FString ResolveGraphError;
    UAnimationGraph* TargetGraph = ResolveTargetAnimationGraph(AnimBlueprint, GraphName, ResolveGraphError);
    if (!TargetGraph)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(ResolveGraphError);
    }

    const FString EffectiveGraphName = TargetGraph->GetName();

    for (UEdGraph* Graph : AnimBlueprint->FunctionGraphs)
    {
        if (Graph && Graph->GetName().Equals(StateMachineName, ESearchCase::IgnoreCase))
        {
            return FUnrealAICommonUtils::CreateErrorResponse(
                FString::Printf(TEXT("State machine name collides with existing AnimBlueprint graph: %s"), *StateMachineName));
        }
    }

    for (UEdGraphNode* Node : TargetGraph->Nodes)
    {
        UAnimGraphNode_StateMachineBase* ExistingStateMachineNode = Cast<UAnimGraphNode_StateMachineBase>(Node);
        if (!ExistingStateMachineNode)
        {
            continue;
        }

        const FString ExistingStateMachineName = ExistingStateMachineNode->GetStateMachineName();
        if (ExistingStateMachineName.Equals(StateMachineName, ESearchCase::IgnoreCase))
        {
            return FUnrealAICommonUtils::CreateErrorResponse(
                FString::Printf(TEXT("State machine already exists in animation graph '%s': %s"),
                    *EffectiveGraphName,
                    *StateMachineName));
        }
    }

    int32 NodePosX = 0;
    int32 NodePosY = 0;
    double NodePosXInput = 0.0;
    double NodePosYInput = 0.0;
    const bool bHasExplicitPosX = Params->TryGetNumberField(TEXT("pos_x"), NodePosXInput);
    const bool bHasExplicitPosY = Params->TryGetNumberField(TEXT("pos_y"), NodePosYInput);
    if (bHasExplicitPosX)
    {
        NodePosX = static_cast<int32>(NodePosXInput);
    }
    if (bHasExplicitPosY)
    {
        NodePosY = static_cast<int32>(NodePosYInput);
    }
    if (!bHasExplicitPosX && !bHasExplicitPosY)
    {
        int32 ExistingStateMachineCount = 0;
        for (UEdGraphNode* Node : TargetGraph->Nodes)
        {
            if (Cast<UAnimGraphNode_StateMachineBase>(Node))
            {
                ++ExistingStateMachineCount;
            }
        }

        NodePosX = ExistingStateMachineCount * 400;
        NodePosY = 0;
    }

    AnimBlueprint->Modify();
    TargetGraph->Modify();

    FGraphNodeCreator<UAnimGraphNode_StateMachine> NodeCreator(*TargetGraph);
    UAnimGraphNode_StateMachine* StateMachineNode = NodeCreator.CreateNode(/*bSelectNewNode=*/false);
    if (!StateMachineNode)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Failed to create state machine node in animation graph"));
    }

    StateMachineNode->NodePosX = NodePosX;
    StateMachineNode->NodePosY = NodePosY;
    NodeCreator.Finalize();

    if (!StateMachineNode->EditorStateMachineGraph)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Created state machine node is missing its editor graph"));
    }

    FBlueprintEditorUtils::RenameGraph(StateMachineNode->EditorStateMachineGraph, *StateMachineName);
    TargetGraph->NotifyGraphChanged();
    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(AnimBlueprint);
    UEditorAssetLibrary::SaveAsset(AssetPath, /*bOnlyIfIsDirty=*/false);

    TSharedPtr<FJsonObject> ReadParams = MakeShared<FJsonObject>();
    ReadParams->SetStringField(TEXT("asset_path"), AssetPath);
    ReadParams->SetStringField(TEXT("state_machine_name"), StateMachineName);

    TSharedPtr<FJsonObject> Result = HandleReadAnimStateMachine(ReadParams);
    if (!Result.IsValid())
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Failed to read back state machine after create"));
    }
    if (Result->HasField(TEXT("error")))
    {
        return Result;
    }

    Result->SetStringField(TEXT("graph_name"), EffectiveGraphName);
    Result->SetNumberField(TEXT("node_pos_x"), StateMachineNode->NodePosX);
    Result->SetNumberField(TEXT("node_pos_y"), StateMachineNode->NodePosY);
    Result->SetBoolField(TEXT("created"), true);
    Result->SetBoolField(TEXT("success"), true);
    return Result;
}

// --------------------------------------------------------------------------- //
// create_anim_state
// --------------------------------------------------------------------------- //
TSharedPtr<FJsonObject> FUnrealAIAssetCommands::HandleCreateAnimState(const TSharedPtr<FJsonObject>& Params)
{
    FString AssetPath;
    if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath) || AssetPath.TrimStartAndEnd().IsEmpty())
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing or empty 'asset_path' parameter"));
    }
    AssetPath = AssetPath.TrimStartAndEnd();

    FString StateMachineName;
    if (!Params->TryGetStringField(TEXT("state_machine_name"), StateMachineName) || StateMachineName.TrimStartAndEnd().IsEmpty())
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing or empty 'state_machine_name' parameter"));
    }
    StateMachineName = StateMachineName.TrimStartAndEnd();

    FString StateName;
    if (!Params->TryGetStringField(TEXT("state_name"), StateName) || StateName.TrimStartAndEnd().IsEmpty())
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing or empty 'state_name' parameter"));
    }
    StateName = StateName.TrimStartAndEnd();

    UObject* LoadedObject = UEditorAssetLibrary::LoadAsset(AssetPath);
    UAnimBlueprint* AnimBlueprint = Cast<UAnimBlueprint>(LoadedObject);
    if (!AnimBlueprint)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("AnimBlueprint not found or wrong class: %s"), *AssetPath));
    }

    FString ResolveStateMachineError;
    UAnimationStateMachineGraph* StateMachineGraph = ResolveTargetStateMachineGraph(AnimBlueprint, StateMachineName, ResolveStateMachineError);
    if (!StateMachineGraph)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(ResolveStateMachineError);
    }

    if (DoesAnimBlueprintGraphNameCollide(AnimBlueprint, StateName))
    {
        return FUnrealAICommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("State name collides with existing AnimBlueprint graph: %s"), *StateName));
    }

    TArray<FString> AvailableStateNames;
    FString ResolveStateError;
    if (ResolveTargetAnimStateNode(StateMachineGraph, StateName, ResolveStateError, &AvailableStateNames))
    {
        return FUnrealAICommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("State already exists in state machine '%s': %s"), *StateMachineName, *StateName));
    }

    int32 NodePosX = 400;
    int32 NodePosY = 0;
    double NodePosXInput = 0.0;
    double NodePosYInput = 0.0;
    const bool bHasExplicitPosX = Params->TryGetNumberField(TEXT("pos_x"), NodePosXInput);
    const bool bHasExplicitPosY = Params->TryGetNumberField(TEXT("pos_y"), NodePosYInput);
    if (bHasExplicitPosX)
    {
        NodePosX = static_cast<int32>(NodePosXInput);
    }
    if (bHasExplicitPosY)
    {
        NodePosY = static_cast<int32>(NodePosYInput);
    }
    if (!bHasExplicitPosX && !bHasExplicitPosY)
    {
        NodePosX = 400 + (CountAnimStates(StateMachineGraph) * 400);
        NodePosY = 0;
    }

    AnimBlueprint->Modify();
    StateMachineGraph->Modify();

    FGraphNodeCreator<UAnimStateNode> NodeCreator(*StateMachineGraph);
    UAnimStateNode* StateNode = NodeCreator.CreateNode(/*bSelectNewNode=*/false);
    if (!StateNode)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Failed to create animation state node in state machine"));
    }

    StateNode->NodePosX = NodePosX;
    StateNode->NodePosY = NodePosY;
    NodeCreator.Finalize();

    if (!StateNode->BoundGraph)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Created animation state node is missing its bound graph"));
    }

    StateNode->OnRenameNode(StateName);

    bool bConnectedEntry = false;
    if (StateMachineGraph->EntryNode && !StateMachineGraph->EntryNode->GetOutputNode())
    {
        const UEdGraphSchema* GraphSchema = StateMachineGraph->GetSchema();
        if (GraphSchema)
        {
            bConnectedEntry = GraphSchema->TryCreateConnection(StateMachineGraph->EntryNode->GetOutputPin(), StateNode->GetInputPin());
        }
    }

    StateMachineGraph->NotifyGraphChanged();
    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(AnimBlueprint);
    UEditorAssetLibrary::SaveAsset(AssetPath, /*bOnlyIfIsDirty=*/false);

    TSharedPtr<FJsonObject> ReadParams = MakeShared<FJsonObject>();
    ReadParams->SetStringField(TEXT("asset_path"), AssetPath);
    ReadParams->SetStringField(TEXT("state_machine_name"), StateMachineName);

    TSharedPtr<FJsonObject> Result = HandleReadAnimStateMachine(ReadParams);
    if (!Result.IsValid())
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Failed to read back state machine after state create"));
    }
    if (Result->HasField(TEXT("error")))
    {
        return Result;
    }

    TSharedPtr<FJsonObject> CreatedState;
    if (!TryGetMatchingAnimState(Result, StateName, CreatedState) || !CreatedState.IsValid())
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Failed to resolve created state from post-create readback"));
    }

    Result->SetStringField(TEXT("state_name"), CreatedState->GetStringField(TEXT("name")));
    Result->SetObjectField(TEXT("state"), CreatedState.ToSharedRef());
    Result->SetNumberField(TEXT("node_pos_x"), StateNode->NodePosX);
    Result->SetNumberField(TEXT("node_pos_y"), StateNode->NodePosY);
    Result->SetBoolField(TEXT("entry_connected"), bConnectedEntry);
    Result->SetBoolField(TEXT("created"), true);
    Result->SetBoolField(TEXT("success"), true);
    return Result;
}

// --------------------------------------------------------------------------- //
// rename_anim_state
// --------------------------------------------------------------------------- //
TSharedPtr<FJsonObject> FUnrealAIAssetCommands::HandleRenameAnimState(const TSharedPtr<FJsonObject>& Params)
{
    FString AssetPath;
    if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath) || AssetPath.TrimStartAndEnd().IsEmpty())
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing or empty 'asset_path' parameter"));
    }
    AssetPath = AssetPath.TrimStartAndEnd();

    FString StateMachineName;
    if (!Params->TryGetStringField(TEXT("state_machine_name"), StateMachineName) || StateMachineName.TrimStartAndEnd().IsEmpty())
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing or empty 'state_machine_name' parameter"));
    }
    StateMachineName = StateMachineName.TrimStartAndEnd();

    FString StateName;
    if (!Params->TryGetStringField(TEXT("state_name"), StateName) || StateName.TrimStartAndEnd().IsEmpty())
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing or empty 'state_name' parameter"));
    }
    StateName = StateName.TrimStartAndEnd();

    FString NewStateName;
    if (!Params->TryGetStringField(TEXT("new_state_name"), NewStateName) || NewStateName.TrimStartAndEnd().IsEmpty())
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing or empty 'new_state_name' parameter"));
    }
    NewStateName = NewStateName.TrimStartAndEnd();

    if (StateName.Equals(NewStateName, ESearchCase::IgnoreCase))
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("'new_state_name' must differ from 'state_name'"));
    }

    UObject* LoadedObject = UEditorAssetLibrary::LoadAsset(AssetPath);
    UAnimBlueprint* AnimBlueprint = Cast<UAnimBlueprint>(LoadedObject);
    if (!AnimBlueprint)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("AnimBlueprint not found or wrong class: %s"), *AssetPath));
    }

    FString ResolveStateMachineError;
    UAnimationStateMachineGraph* StateMachineGraph = ResolveTargetStateMachineGraph(AnimBlueprint, StateMachineName, ResolveStateMachineError);
    if (!StateMachineGraph)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(ResolveStateMachineError);
    }

    TArray<FString> AvailableStateNames;
    FString ResolveStateError;
    UAnimStateNode* StateNode = ResolveTargetAnimStateNode(StateMachineGraph, StateName, ResolveStateError, &AvailableStateNames);
    if (!StateNode)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(ResolveStateError);
    }

    if (DoesAnimBlueprintGraphNameCollide(AnimBlueprint, NewStateName))
    {
        return FUnrealAICommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("State name collides with existing AnimBlueprint graph: %s"), *NewStateName));
    }

    for (const FString& ExistingStateName : AvailableStateNames)
    {
        if (ExistingStateName.Equals(NewStateName, ESearchCase::IgnoreCase))
        {
            return FUnrealAICommonUtils::CreateErrorResponse(
                FString::Printf(TEXT("State already exists in state machine '%s': %s"), *StateMachineName, *NewStateName));
        }
    }

    AnimBlueprint->Modify();
    StateMachineGraph->Modify();
    StateNode->Modify();
    StateNode->OnRenameNode(NewStateName);

    StateMachineGraph->NotifyGraphChanged();
    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(AnimBlueprint);
    UEditorAssetLibrary::SaveAsset(AssetPath, /*bOnlyIfIsDirty=*/false);

    TSharedPtr<FJsonObject> ReadParams = MakeShared<FJsonObject>();
    ReadParams->SetStringField(TEXT("asset_path"), AssetPath);
    ReadParams->SetStringField(TEXT("state_machine_name"), StateMachineName);

    TSharedPtr<FJsonObject> Result = HandleReadAnimStateMachine(ReadParams);
    if (!Result.IsValid())
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Failed to read back state machine after state rename"));
    }
    if (Result->HasField(TEXT("error")))
    {
        return Result;
    }

    TSharedPtr<FJsonObject> RenamedState;
    if (!TryGetMatchingAnimState(Result, NewStateName, RenamedState) || !RenamedState.IsValid())
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Failed to resolve renamed state from post-rename readback"));
    }

    Result->SetStringField(TEXT("old_state_name"), StateName);
    Result->SetStringField(TEXT("new_state_name"), NewStateName);
    Result->SetStringField(TEXT("state_name"), RenamedState->GetStringField(TEXT("name")));
    Result->SetObjectField(TEXT("state"), RenamedState.ToSharedRef());
    Result->SetBoolField(TEXT("renamed"), true);
    Result->SetBoolField(TEXT("success"), true);
    return Result;
}

// --------------------------------------------------------------------------- //
// delete_anim_state
// --------------------------------------------------------------------------- //
TSharedPtr<FJsonObject> FUnrealAIAssetCommands::HandleDeleteAnimState(const TSharedPtr<FJsonObject>& Params)
{
    FString AssetPath;
    if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath) || AssetPath.TrimStartAndEnd().IsEmpty())
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing or empty 'asset_path' parameter"));
    }
    AssetPath = AssetPath.TrimStartAndEnd();

    FString StateMachineName;
    if (!Params->TryGetStringField(TEXT("state_machine_name"), StateMachineName) || StateMachineName.TrimStartAndEnd().IsEmpty())
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing or empty 'state_machine_name' parameter"));
    }
    StateMachineName = StateMachineName.TrimStartAndEnd();

    FString StateName;
    if (!Params->TryGetStringField(TEXT("state_name"), StateName) || StateName.TrimStartAndEnd().IsEmpty())
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing or empty 'state_name' parameter"));
    }
    StateName = StateName.TrimStartAndEnd();

    UObject* LoadedObject = UEditorAssetLibrary::LoadAsset(AssetPath);
    UAnimBlueprint* AnimBlueprint = Cast<UAnimBlueprint>(LoadedObject);
    if (!AnimBlueprint)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("AnimBlueprint not found or wrong class: %s"), *AssetPath));
    }

    FString ResolveStateMachineError;
    UAnimationStateMachineGraph* StateMachineGraph = ResolveTargetStateMachineGraph(AnimBlueprint, StateMachineName, ResolveStateMachineError);
    if (!StateMachineGraph)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(ResolveStateMachineError);
    }

    FString ResolveStateError;
    UAnimStateNode* StateNode = ResolveTargetAnimStateNode(StateMachineGraph, StateName, ResolveStateError);
    if (!StateNode)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(ResolveStateError);
    }

    const int32 StateCountBefore = CountAnimStates(StateMachineGraph);

    TArray<UAnimStateTransitionNode*> TransitionsToDelete;
    CollectTransitionsForState(StateMachineGraph, StateNode, TransitionsToDelete);
    const int32 DeletedTransitionCount = TransitionsToDelete.Num();

    AnimBlueprint->Modify();
    StateMachineGraph->Modify();

    for (UAnimStateTransitionNode* TransitionNode : TransitionsToDelete)
    {
        if (!TransitionNode)
        {
            continue;
        }

        TransitionNode->Modify();
        TransitionNode->DestroyNode();
    }

    StateNode->Modify();
    StateNode->DestroyNode();

    StateMachineGraph->NotifyGraphChanged();
    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(AnimBlueprint);
    UEditorAssetLibrary::SaveAsset(AssetPath, /*bOnlyIfIsDirty=*/false);

    TSharedPtr<FJsonObject> ReadParams = MakeShared<FJsonObject>();
    ReadParams->SetStringField(TEXT("asset_path"), AssetPath);
    ReadParams->SetStringField(TEXT("state_machine_name"), StateMachineName);

    TSharedPtr<FJsonObject> Result = HandleReadAnimStateMachine(ReadParams);
    if (!Result.IsValid())
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Failed to read back state machine after state delete"));
    }
    if (Result->HasField(TEXT("error")))
    {
        return Result;
    }

    Result->SetStringField(TEXT("deleted_state_name"), StateName);
    Result->SetNumberField(TEXT("state_count_before"), StateCountBefore);
    Result->SetNumberField(TEXT("deleted_transition_count"), DeletedTransitionCount);
    Result->SetBoolField(TEXT("deleted"), true);
    Result->SetBoolField(TEXT("success"), true);
    return Result;
}

// --------------------------------------------------------------------------- //
// create_anim_transition
// --------------------------------------------------------------------------- //
TSharedPtr<FJsonObject> FUnrealAIAssetCommands::HandleSetAnimStateSequencePlayer(const TSharedPtr<FJsonObject>& Params)
{
    FString AssetPath;
    if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath) || AssetPath.TrimStartAndEnd().IsEmpty())
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing or empty 'asset_path' parameter"));
    }
    AssetPath = AssetPath.TrimStartAndEnd();

    FString StateMachineName;
    if (!Params->TryGetStringField(TEXT("state_machine_name"), StateMachineName) || StateMachineName.TrimStartAndEnd().IsEmpty())
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing or empty 'state_machine_name' parameter"));
    }
    StateMachineName = StateMachineName.TrimStartAndEnd();

    FString StateName;
    if (!Params->TryGetStringField(TEXT("state_name"), StateName) || StateName.TrimStartAndEnd().IsEmpty())
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing or empty 'state_name' parameter"));
    }
    StateName = StateName.TrimStartAndEnd();

    FString SequencePath;
    if (!Params->TryGetStringField(TEXT("sequence_path"), SequencePath) || SequencePath.TrimStartAndEnd().IsEmpty())
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing or empty 'sequence_path' parameter"));
    }
    SequencePath = SequencePath.TrimStartAndEnd();

    UObject* LoadedObject = UEditorAssetLibrary::LoadAsset(AssetPath);
    UAnimBlueprint* AnimBlueprint = Cast<UAnimBlueprint>(LoadedObject);
    if (!AnimBlueprint)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("AnimBlueprint not found or wrong class: %s"), *AssetPath));
    }

    FString ResolveStateMachineError;
    UAnimationStateMachineGraph* StateMachineGraph = ResolveTargetStateMachineGraph(AnimBlueprint, StateMachineName, ResolveStateMachineError);
    if (!StateMachineGraph)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(ResolveStateMachineError);
    }

    FString ResolveStateError;
    UAnimStateNode* StateNode = ResolveTargetAnimStateNode(StateMachineGraph, StateName, ResolveStateError);
    if (!StateNode)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(ResolveStateError);
    }

    UObject* SequenceObject = UEditorAssetLibrary::LoadAsset(SequencePath);
    UAnimSequenceBase* SequenceAsset = Cast<UAnimSequenceBase>(SequenceObject);
    if (!SequenceAsset)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Animation sequence asset not found or wrong class: %s"), *SequencePath));
    }

    FString BindingType;
    FString ApplyError;
    if (!ApplyAnimStateAssetBinding(AnimBlueprint, StateNode, SequenceAsset, BindingType, ApplyError))
    {
        return FUnrealAICommonUtils::CreateErrorResponse(ApplyError);
    }

    StateMachineGraph->NotifyGraphChanged();
    if (StateNode->BoundGraph)
    {
        StateNode->BoundGraph->NotifyGraphChanged();
    }

    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(AnimBlueprint);
    UEditorAssetLibrary::SaveAsset(AssetPath, /*bOnlyIfIsDirty=*/false);

    TSharedPtr<FJsonObject> ReadParams = MakeShared<FJsonObject>();
    ReadParams->SetStringField(TEXT("asset_path"), AssetPath);
    ReadParams->SetStringField(TEXT("state_machine_name"), StateMachineName);

    TSharedPtr<FJsonObject> Result = HandleReadAnimStateMachine(ReadParams);
    if (!Result.IsValid())
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Failed to read back state machine after sequence-player bind"));
    }
    if (Result->HasField(TEXT("error")))
    {
        return Result;
    }

    TSharedPtr<FJsonObject> UpdatedState;
    if (!TryGetMatchingAnimState(Result, StateName, UpdatedState) || !UpdatedState.IsValid())
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Failed to resolve updated state from post-bind readback"));
    }

    Result->SetStringField(TEXT("state_name"), UpdatedState->GetStringField(TEXT("name")));
    Result->SetObjectField(TEXT("state"), UpdatedState.ToSharedRef());
    AppendAnimStateAssetPlayerResponseFields(Result, UpdatedState);
    Result->SetStringField(TEXT("sequence_path"), SequencePath);
    Result->SetBoolField(TEXT("updated"), true);
    Result->SetBoolField(TEXT("success"), true);
    return Result;
}

// --------------------------------------------------------------------------- //
// set_anim_state_blend_space_player
// --------------------------------------------------------------------------- //
TSharedPtr<FJsonObject> FUnrealAIAssetCommands::HandleSetAnimStateBlendSpacePlayer(const TSharedPtr<FJsonObject>& Params)
{
    FString AssetPath;
    if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath) || AssetPath.TrimStartAndEnd().IsEmpty())
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing or empty 'asset_path' parameter"));
    }
    AssetPath = AssetPath.TrimStartAndEnd();

    FString StateMachineName;
    if (!Params->TryGetStringField(TEXT("state_machine_name"), StateMachineName) || StateMachineName.TrimStartAndEnd().IsEmpty())
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing or empty 'state_machine_name' parameter"));
    }
    StateMachineName = StateMachineName.TrimStartAndEnd();

    FString StateName;
    if (!Params->TryGetStringField(TEXT("state_name"), StateName) || StateName.TrimStartAndEnd().IsEmpty())
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing or empty 'state_name' parameter"));
    }
    StateName = StateName.TrimStartAndEnd();

    FString BlendSpacePath;
    if (!Params->TryGetStringField(TEXT("blend_space_path"), BlendSpacePath) || BlendSpacePath.TrimStartAndEnd().IsEmpty())
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing or empty 'blend_space_path' parameter"));
    }
    BlendSpacePath = BlendSpacePath.TrimStartAndEnd();

    UObject* LoadedObject = UEditorAssetLibrary::LoadAsset(AssetPath);
    UAnimBlueprint* AnimBlueprint = Cast<UAnimBlueprint>(LoadedObject);
    if (!AnimBlueprint)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("AnimBlueprint not found or wrong class: %s"), *AssetPath));
    }

    FString ResolveStateMachineError;
    UAnimationStateMachineGraph* StateMachineGraph = ResolveTargetStateMachineGraph(AnimBlueprint, StateMachineName, ResolveStateMachineError);
    if (!StateMachineGraph)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(ResolveStateMachineError);
    }

    FString ResolveStateError;
    UAnimStateNode* StateNode = ResolveTargetAnimStateNode(StateMachineGraph, StateName, ResolveStateError);
    if (!StateNode)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(ResolveStateError);
    }

    UObject* BlendSpaceObject = UEditorAssetLibrary::LoadAsset(BlendSpacePath);
    UBlendSpace* BlendSpaceAsset = Cast<UBlendSpace>(BlendSpaceObject);
    if (!BlendSpaceAsset)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Blend space asset not found or wrong class: %s"), *BlendSpacePath));
    }

    FString BindingType;
    FString ApplyError;
    if (!ApplyAnimStateAssetBinding(AnimBlueprint, StateNode, BlendSpaceAsset, BindingType, ApplyError))
    {
        return FUnrealAICommonUtils::CreateErrorResponse(ApplyError);
    }

    StateMachineGraph->NotifyGraphChanged();
    if (StateNode->BoundGraph)
    {
        StateNode->BoundGraph->NotifyGraphChanged();
    }

    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(AnimBlueprint);
    UEditorAssetLibrary::SaveAsset(AssetPath, /*bOnlyIfIsDirty=*/false);

    TSharedPtr<FJsonObject> ReadParams = MakeShared<FJsonObject>();
    ReadParams->SetStringField(TEXT("asset_path"), AssetPath);
    ReadParams->SetStringField(TEXT("state_machine_name"), StateMachineName);

    TSharedPtr<FJsonObject> Result = HandleReadAnimStateMachine(ReadParams);
    if (!Result.IsValid())
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Failed to read back state machine after blend-space bind"));
    }
    if (Result->HasField(TEXT("error")))
    {
        return Result;
    }

    TSharedPtr<FJsonObject> UpdatedState;
    if (!TryGetMatchingAnimState(Result, StateName, UpdatedState) || !UpdatedState.IsValid())
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Failed to resolve updated state from post-bind readback"));
    }

    Result->SetStringField(TEXT("state_name"), UpdatedState->GetStringField(TEXT("name")));
    Result->SetObjectField(TEXT("state"), UpdatedState.ToSharedRef());
    AppendAnimStateAssetPlayerResponseFields(Result, UpdatedState);
    Result->SetStringField(TEXT("blend_space_path"), BlendSpacePath);
    Result->SetBoolField(TEXT("updated"), true);
    Result->SetBoolField(TEXT("success"), true);
    return Result;
}

// --------------------------------------------------------------------------- //
// set_anim_state_asset_player_parameters
// --------------------------------------------------------------------------- //
TSharedPtr<FJsonObject> FUnrealAIAssetCommands::HandleSetAnimStateAssetPlayerParameters(const TSharedPtr<FJsonObject>& Params)
{
    FString AssetPath;
    if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath) || AssetPath.TrimStartAndEnd().IsEmpty())
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing or empty 'asset_path' parameter"));
    }
    AssetPath = AssetPath.TrimStartAndEnd();

    FString StateMachineName;
    if (!Params->TryGetStringField(TEXT("state_machine_name"), StateMachineName) || StateMachineName.TrimStartAndEnd().IsEmpty())
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing or empty 'state_machine_name' parameter"));
    }
    StateMachineName = StateMachineName.TrimStartAndEnd();

    FString StateName;
    if (!Params->TryGetStringField(TEXT("state_name"), StateName) || StateName.TrimStartAndEnd().IsEmpty())
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing or empty 'state_name' parameter"));
    }
    StateName = StateName.TrimStartAndEnd();

    bool bHasLoop = false;
    bool bLoop = false;
    if (Params->HasField(TEXT("loop")))
    {
        if (!Params->TryGetBoolField(TEXT("loop"), bLoop))
        {
            return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Field 'loop' must be a boolean"));
        }
        bHasLoop = true;
    }

    bool bHasPlayRate = false;
    double PlayRateValue = 0.0;
    if (Params->HasField(TEXT("play_rate")))
    {
        if (!Params->TryGetNumberField(TEXT("play_rate"), PlayRateValue))
        {
            return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Field 'play_rate' must be numeric"));
        }
        bHasPlayRate = true;
    }

    bool bHasStartPosition = false;
    double StartPositionValue = 0.0;
    if (Params->HasField(TEXT("start_position")))
    {
        if (!Params->TryGetNumberField(TEXT("start_position"), StartPositionValue))
        {
            return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Field 'start_position' must be numeric"));
        }
        bHasStartPosition = true;
    }

    bool bHasBlendSpaceX = false;
    double BlendSpaceXValue = 0.0;
    if (Params->HasField(TEXT("blend_space_x")))
    {
        if (!Params->TryGetNumberField(TEXT("blend_space_x"), BlendSpaceXValue))
        {
            return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Field 'blend_space_x' must be numeric"));
        }
        bHasBlendSpaceX = true;
    }

    bool bHasBlendSpaceY = false;
    double BlendSpaceYValue = 0.0;
    if (Params->HasField(TEXT("blend_space_y")))
    {
        if (!Params->TryGetNumberField(TEXT("blend_space_y"), BlendSpaceYValue))
        {
            return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Field 'blend_space_y' must be numeric"));
        }
        bHasBlendSpaceY = true;
    }

    bool bHasSyncGroupName = false;
    FString SyncGroupName;
    if (Params->HasField(TEXT("sync_group_name")))
    {
        if (!Params->TryGetStringField(TEXT("sync_group_name"), SyncGroupName))
        {
            return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Field 'sync_group_name' must be a string"));
        }
        SyncGroupName = SyncGroupName.TrimStartAndEnd();
        bHasSyncGroupName = true;
    }

    bool bHasSyncGroupRole = false;
    EAnimGroupRole::Type SyncGroupRole = EAnimGroupRole::CanBeLeader;
    if (Params->HasField(TEXT("sync_group_role")))
    {
        FString SyncGroupRoleValue;
        if (!Params->TryGetStringField(TEXT("sync_group_role"), SyncGroupRoleValue) || SyncGroupRoleValue.TrimStartAndEnd().IsEmpty())
        {
            return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Field 'sync_group_role' must be a non-empty string"));
        }

        if (!TryParseEnumValue(SyncGroupRoleValue, StaticEnum<EAnimGroupRole::Type>(), SyncGroupRole))
        {
            return FUnrealAICommonUtils::CreateErrorResponse(
                FString::Printf(TEXT("Unknown sync group role: %s"), *SyncGroupRoleValue));
        }

        bHasSyncGroupRole = true;
    }

    bool bHasSyncGroupMethod = false;
    EAnimSyncMethod SyncGroupMethod = EAnimSyncMethod::DoNotSync;
    if (Params->HasField(TEXT("sync_group_method")))
    {
        FString SyncGroupMethodValue;
        if (!Params->TryGetStringField(TEXT("sync_group_method"), SyncGroupMethodValue) || SyncGroupMethodValue.TrimStartAndEnd().IsEmpty())
        {
            return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Field 'sync_group_method' must be a non-empty string"));
        }

        if (!TryParseEnumValue(SyncGroupMethodValue, StaticEnum<EAnimSyncMethod>(), SyncGroupMethod))
        {
            return FUnrealAICommonUtils::CreateErrorResponse(
                FString::Printf(TEXT("Unknown sync group method: %s"), *SyncGroupMethodValue));
        }

        bHasSyncGroupMethod = true;
    }

    bool bHasSyncGroupOverride = false;
    bool bSyncGroupOverride = false;
    if (Params->HasField(TEXT("sync_group_override_position_when_joining_sync_group_as_leader")))
    {
        if (!Params->TryGetBoolField(
            TEXT("sync_group_override_position_when_joining_sync_group_as_leader"),
            bSyncGroupOverride))
        {
            return FUnrealAICommonUtils::CreateErrorResponse(
                TEXT("Field 'sync_group_override_position_when_joining_sync_group_as_leader' must be a boolean"));
        }
        bHasSyncGroupOverride = true;
    }

    if (!bHasLoop &&
        !bHasPlayRate &&
        !bHasStartPosition &&
        !bHasBlendSpaceX &&
        !bHasBlendSpaceY &&
        !bHasSyncGroupName &&
        !bHasSyncGroupRole &&
        !bHasSyncGroupMethod &&
        !bHasSyncGroupOverride)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(
            TEXT("At least one asset-player parameter must be provided"));
    }

    UObject* LoadedObject = UEditorAssetLibrary::LoadAsset(AssetPath);
    UAnimBlueprint* AnimBlueprint = Cast<UAnimBlueprint>(LoadedObject);
    if (!AnimBlueprint)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("AnimBlueprint not found or wrong class: %s"), *AssetPath));
    }

    FString ResolveStateMachineError;
    UAnimationStateMachineGraph* StateMachineGraph = ResolveTargetStateMachineGraph(AnimBlueprint, StateMachineName, ResolveStateMachineError);
    if (!StateMachineGraph)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(ResolveStateMachineError);
    }

    FString ResolveStateError;
    UAnimStateNode* StateNode = ResolveTargetAnimStateNode(StateMachineGraph, StateName, ResolveStateError);
    if (!StateNode)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(ResolveStateError);
    }

    FResolvedAnimStateAssetPlayerNode ResolvedNode;
    FString ResolveAssetPlayerError;
    if (!ResolveSupportedAnimStateAssetPlayerNode(StateNode, ResolvedNode, ResolveAssetPlayerError))
    {
        return FUnrealAICommonUtils::CreateErrorResponse(ResolveAssetPlayerError);
    }

    if (ResolvedNode.BindingType == TEXT("sequence_player") && (bHasBlendSpaceX || bHasBlendSpaceY))
    {
        return FUnrealAICommonUtils::CreateErrorResponse(
            TEXT("Blend-space coordinate parameters are not supported for sequence-player states"));
    }

    AnimBlueprint->Modify();
    StateMachineGraph->Modify();
    StateNode->Modify();
    if (ResolvedNode.StateGraph)
    {
        ResolvedNode.StateGraph->Modify();
    }
    if (ResolvedNode.AssetPlayerGraphNode)
    {
        ResolvedNode.AssetPlayerGraphNode->Modify();
    }

    if (ResolvedNode.BindingType == TEXT("sequence_player"))
    {
        UAnimGraphNode_SequencePlayer* SequencePlayerNode = Cast<UAnimGraphNode_SequencePlayer>(ResolvedNode.AssetPlayerGraphNode);
        if (!SequencePlayerNode)
        {
            return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Failed to resolve sequence-player node for mutation"));
        }

        if (bHasLoop && !SequencePlayerNode->Node.SetLoopAnimation(bLoop))
        {
            return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Failed to update sequence-player loop setting"));
        }
        if (bHasPlayRate && !SequencePlayerNode->Node.SetPlayRate(static_cast<float>(PlayRateValue)))
        {
            return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Failed to update sequence-player play rate"));
        }
        if (bHasStartPosition && !SequencePlayerNode->Node.SetStartPosition(static_cast<float>(StartPositionValue)))
        {
            return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Failed to update sequence-player start position"));
        }
        if (bHasSyncGroupName && !SequencePlayerNode->Node.SetGroupName(FName(*SyncGroupName)))
        {
            return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Failed to update sequence-player sync group name"));
        }
        if (bHasSyncGroupRole && !SequencePlayerNode->Node.SetGroupRole(SyncGroupRole))
        {
            return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Failed to update sequence-player sync group role"));
        }
        if (bHasSyncGroupMethod && !SequencePlayerNode->Node.SetGroupMethod(SyncGroupMethod))
        {
            return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Failed to update sequence-player sync group method"));
        }
        if (bHasSyncGroupOverride && !SequencePlayerNode->Node.SetOverridePositionWhenJoiningSyncGroupAsLeader(bSyncGroupOverride))
        {
            return FUnrealAICommonUtils::CreateErrorResponse(
                TEXT("Failed to update sequence-player sync-group leader override"));
        }
    }
    else
    {
        FAnimNode_BlendSpacePlayerBase* BlendSpacePlayerNode = nullptr;
        void* BlendSpacePlayerStructData = nullptr;
        UScriptStruct* BlendSpacePlayerStruct = nullptr;

        if (ResolvedNode.BindingType == TEXT("blend_space_player"))
        {
            UAnimGraphNode_BlendSpacePlayer* BlendSpaceGraphNode = Cast<UAnimGraphNode_BlendSpacePlayer>(ResolvedNode.AssetPlayerGraphNode);
            if (!BlendSpaceGraphNode)
            {
                return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Failed to resolve blend-space player node for mutation"));
            }

            FStructProperty* BlendSpaceNodeProperty = CastField<FStructProperty>(
                BlendSpaceGraphNode->GetClass()->FindPropertyByName(
                    GET_MEMBER_NAME_CHECKED(UAnimGraphNode_BlendSpacePlayer, Node)));
            BlendSpacePlayerNode = &BlendSpaceGraphNode->Node;
            BlendSpacePlayerStructData = BlendSpaceNodeProperty
                ? BlendSpaceNodeProperty->ContainerPtrToValuePtr<void>(BlendSpaceGraphNode)
                : nullptr;
            BlendSpacePlayerStruct = BlendSpaceNodeProperty ? BlendSpaceNodeProperty->Struct : nullptr;
        }
        else if (ResolvedNode.BindingType == TEXT("aim_offset_player"))
        {
            UAnimGraphNode_RotationOffsetBlendSpace* AimOffsetGraphNode = Cast<UAnimGraphNode_RotationOffsetBlendSpace>(ResolvedNode.AssetPlayerGraphNode);
            if (!AimOffsetGraphNode)
            {
                return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Failed to resolve aim-offset player node for mutation"));
            }

            FStructProperty* AimOffsetNodeProperty = CastField<FStructProperty>(
                AimOffsetGraphNode->GetClass()->FindPropertyByName(
                    GET_MEMBER_NAME_CHECKED(UAnimGraphNode_RotationOffsetBlendSpace, Node)));
            BlendSpacePlayerNode = &AimOffsetGraphNode->Node;
            BlendSpacePlayerStructData = AimOffsetNodeProperty
                ? AimOffsetNodeProperty->ContainerPtrToValuePtr<void>(AimOffsetGraphNode)
                : nullptr;
            BlendSpacePlayerStruct = AimOffsetNodeProperty ? AimOffsetNodeProperty->Struct : nullptr;
        }

        if (!BlendSpacePlayerNode || !BlendSpacePlayerStructData || !BlendSpacePlayerStruct)
        {
            return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Failed to resolve blend-space asset-player node for mutation"));
        }

        if (bHasLoop && !BlendSpacePlayerNode->SetLoop(bLoop))
        {
            return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Failed to update blend-space loop setting"));
        }
        if (bHasPlayRate && !BlendSpacePlayerNode->SetPlayRate(static_cast<float>(PlayRateValue)))
        {
            return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Failed to update blend-space play rate"));
        }
        if (bHasStartPosition && !SetStructFloatPropertyValue(
            BlendSpacePlayerStruct,
            BlendSpacePlayerStructData,
            TEXT("StartPosition"),
            static_cast<float>(StartPositionValue)))
        {
            return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Failed to update blend-space start position"));
        }
        if (bHasBlendSpaceX || bHasBlendSpaceY)
        {
            FVector BlendSpacePosition = BlendSpacePlayerNode->GetPosition();
            if (bHasBlendSpaceX)
            {
                BlendSpacePosition.X = static_cast<float>(BlendSpaceXValue);
            }
            if (bHasBlendSpaceY)
            {
                BlendSpacePosition.Y = static_cast<float>(BlendSpaceYValue);
            }

            if (!BlendSpacePlayerNode->SetPosition(BlendSpacePosition))
            {
                return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Failed to update blend-space coordinates"));
            }
        }
        if (bHasSyncGroupName && !BlendSpacePlayerNode->SetGroupName(FName(*SyncGroupName)))
        {
            return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Failed to update blend-space sync group name"));
        }
        if (bHasSyncGroupRole && !BlendSpacePlayerNode->SetGroupRole(SyncGroupRole))
        {
            return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Failed to update blend-space sync group role"));
        }
        if (bHasSyncGroupMethod && !BlendSpacePlayerNode->SetGroupMethod(SyncGroupMethod))
        {
            return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Failed to update blend-space sync group method"));
        }
        if (bHasSyncGroupOverride && !BlendSpacePlayerNode->SetOverridePositionWhenJoiningSyncGroupAsLeader(bSyncGroupOverride))
        {
            return FUnrealAICommonUtils::CreateErrorResponse(
                TEXT("Failed to update blend-space sync-group leader override"));
        }
    }

    StateMachineGraph->NotifyGraphChanged();
    if (StateNode->BoundGraph)
    {
        StateNode->BoundGraph->NotifyGraphChanged();
    }

    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(AnimBlueprint);
    UEditorAssetLibrary::SaveAsset(AssetPath, /*bOnlyIfIsDirty=*/false);

    TSharedPtr<FJsonObject> ReadParams = MakeShared<FJsonObject>();
    ReadParams->SetStringField(TEXT("asset_path"), AssetPath);
    ReadParams->SetStringField(TEXT("state_machine_name"), StateMachineName);

    TSharedPtr<FJsonObject> Result = HandleReadAnimStateMachine(ReadParams);
    if (!Result.IsValid())
    {
        return FUnrealAICommonUtils::CreateErrorResponse(
            TEXT("Failed to read back state machine after asset-player parameter mutation"));
    }
    if (Result->HasField(TEXT("error")))
    {
        return Result;
    }

    TSharedPtr<FJsonObject> UpdatedState;
    if (!TryGetMatchingAnimState(Result, StateName, UpdatedState) || !UpdatedState.IsValid())
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Failed to resolve updated state from post-mutation readback"));
    }

    Result->SetStringField(TEXT("state_name"), UpdatedState->GetStringField(TEXT("name")));
    Result->SetObjectField(TEXT("state"), UpdatedState.ToSharedRef());
    AppendAnimStateAssetPlayerResponseFields(Result, UpdatedState);
    Result->SetBoolField(TEXT("updated"), true);
    Result->SetBoolField(TEXT("success"), true);
    return Result;
}

// --------------------------------------------------------------------------- //
// create_anim_transition
// --------------------------------------------------------------------------- //
TSharedPtr<FJsonObject> FUnrealAIAssetCommands::HandleCreateAnimTransition(const TSharedPtr<FJsonObject>& Params)
{
    FString AssetPath;
    if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath) || AssetPath.TrimStartAndEnd().IsEmpty())
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing or empty 'asset_path' parameter"));
    }
    AssetPath = AssetPath.TrimStartAndEnd();

    FString StateMachineName;
    if (!Params->TryGetStringField(TEXT("state_machine_name"), StateMachineName) || StateMachineName.TrimStartAndEnd().IsEmpty())
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing or empty 'state_machine_name' parameter"));
    }
    StateMachineName = StateMachineName.TrimStartAndEnd();

    FString SourceStateName;
    if (!Params->TryGetStringField(TEXT("source_state_name"), SourceStateName) || SourceStateName.TrimStartAndEnd().IsEmpty())
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing or empty 'source_state_name' parameter"));
    }
    SourceStateName = SourceStateName.TrimStartAndEnd();

    FString TargetStateName;
    if (!Params->TryGetStringField(TEXT("target_state_name"), TargetStateName) || TargetStateName.TrimStartAndEnd().IsEmpty())
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing or empty 'target_state_name' parameter"));
    }
    TargetStateName = TargetStateName.TrimStartAndEnd();

    UObject* LoadedObject = UEditorAssetLibrary::LoadAsset(AssetPath);
    UAnimBlueprint* AnimBlueprint = Cast<UAnimBlueprint>(LoadedObject);
    if (!AnimBlueprint)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("AnimBlueprint not found or wrong class: %s"), *AssetPath));
    }

    FString ResolveStateMachineError;
    UAnimationStateMachineGraph* StateMachineGraph = ResolveTargetStateMachineGraph(AnimBlueprint, StateMachineName, ResolveStateMachineError);
    if (!StateMachineGraph)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(ResolveStateMachineError);
    }

    FString ResolveSourceStateError;
    UAnimStateNode* SourceStateNode = ResolveTargetAnimStateNode(StateMachineGraph, SourceStateName, ResolveSourceStateError);
    if (!SourceStateNode)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(FString::Printf(TEXT("Source %s"), *ResolveSourceStateError));
    }

    FString ResolveTargetStateError;
    UAnimStateNode* TargetStateNode = ResolveTargetAnimStateNode(StateMachineGraph, TargetStateName, ResolveTargetStateError);
    if (!TargetStateNode)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(FString::Printf(TEXT("Target %s"), *ResolveTargetStateError));
    }

    TArray<UAnimStateTransitionNode*> ExistingTransitions;
    CollectTransitionsBetweenStates(StateMachineGraph, SourceStateNode, TargetStateNode, ExistingTransitions);
    if (ExistingTransitions.Num() > 0)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Transition already exists in state machine '%s': %s -> %s"),
                *StateMachineName,
                *SourceStateName,
                *TargetStateName));
    }

    TSet<const UAnimStateTransitionNode*> TransitionNodesBefore;
    for (UEdGraphNode* Node : StateMachineGraph->Nodes)
    {
        if (const UAnimStateTransitionNode* TransitionNode = Cast<UAnimStateTransitionNode>(Node))
        {
            TransitionNodesBefore.Add(TransitionNode);
        }
    }

    AnimBlueprint->Modify();
    StateMachineGraph->Modify();

    const UEdGraphSchema* StateMachineSchema = StateMachineGraph->GetSchema();
    if (!StateMachineSchema || !StateMachineSchema->TryCreateConnection(SourceStateNode->GetOutputPin(), TargetStateNode->GetInputPin()))
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Failed to create transition between the requested states"));
    }

    UAnimStateTransitionNode* CreatedTransitionNode = nullptr;
    TArray<UAnimStateTransitionNode*> CreatedTransitions;
    CollectTransitionsBetweenStates(StateMachineGraph, SourceStateNode, TargetStateNode, CreatedTransitions);
    for (UAnimStateTransitionNode* TransitionNode : CreatedTransitions)
    {
        if (TransitionNode && !TransitionNodesBefore.Contains(TransitionNode))
        {
            CreatedTransitionNode = TransitionNode;
            break;
        }
    }
    if (!CreatedTransitionNode && CreatedTransitions.Num() > 0)
    {
        CreatedTransitionNode = CreatedTransitions[0];
    }
    if (!CreatedTransitionNode)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Transition create succeeded but the new transition node could not be resolved"));
    }

    StateMachineGraph->NotifyGraphChanged();
    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(AnimBlueprint);
    UEditorAssetLibrary::SaveAsset(AssetPath, /*bOnlyIfIsDirty=*/false);

    TSharedPtr<FJsonObject> ReadParams = MakeShared<FJsonObject>();
    ReadParams->SetStringField(TEXT("asset_path"), AssetPath);
    ReadParams->SetStringField(TEXT("state_machine_name"), StateMachineName);

    TSharedPtr<FJsonObject> Result = HandleReadAnimStateMachine(ReadParams);
    if (!Result.IsValid())
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Failed to read back state machine after transition create"));
    }
    if (Result->HasField(TEXT("error")))
    {
        return Result;
    }

    TSharedPtr<FJsonObject> CreatedTransition;
    if (!TryGetMatchingAnimTransition(Result, SourceStateName, TargetStateName, CreatedTransition) || !CreatedTransition.IsValid())
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Failed to resolve created transition from post-create readback"));
    }

    Result->SetStringField(TEXT("source_state_name"), SourceStateName);
    Result->SetStringField(TEXT("target_state_name"), TargetStateName);
    Result->SetObjectField(TEXT("transition"), CreatedTransition.ToSharedRef());
    Result->SetBoolField(TEXT("created"), true);
    Result->SetBoolField(TEXT("success"), true);
    return Result;
}

// --------------------------------------------------------------------------- //
// delete_anim_transition
// --------------------------------------------------------------------------- //
TSharedPtr<FJsonObject> FUnrealAIAssetCommands::HandleDeleteAnimTransition(const TSharedPtr<FJsonObject>& Params)
{
    FString AssetPath;
    if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath) || AssetPath.TrimStartAndEnd().IsEmpty())
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing or empty 'asset_path' parameter"));
    }
    AssetPath = AssetPath.TrimStartAndEnd();

    FString StateMachineName;
    if (!Params->TryGetStringField(TEXT("state_machine_name"), StateMachineName) || StateMachineName.TrimStartAndEnd().IsEmpty())
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing or empty 'state_machine_name' parameter"));
    }
    StateMachineName = StateMachineName.TrimStartAndEnd();

    FString SourceStateName;
    if (!Params->TryGetStringField(TEXT("source_state_name"), SourceStateName) || SourceStateName.TrimStartAndEnd().IsEmpty())
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing or empty 'source_state_name' parameter"));
    }
    SourceStateName = SourceStateName.TrimStartAndEnd();

    FString TargetStateName;
    if (!Params->TryGetStringField(TEXT("target_state_name"), TargetStateName) || TargetStateName.TrimStartAndEnd().IsEmpty())
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing or empty 'target_state_name' parameter"));
    }
    TargetStateName = TargetStateName.TrimStartAndEnd();

    UObject* LoadedObject = UEditorAssetLibrary::LoadAsset(AssetPath);
    UAnimBlueprint* AnimBlueprint = Cast<UAnimBlueprint>(LoadedObject);
    if (!AnimBlueprint)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("AnimBlueprint not found or wrong class: %s"), *AssetPath));
    }

    FString ResolveStateMachineError;
    UAnimationStateMachineGraph* StateMachineGraph = ResolveTargetStateMachineGraph(AnimBlueprint, StateMachineName, ResolveStateMachineError);
    if (!StateMachineGraph)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(ResolveStateMachineError);
    }

    FString ResolveSourceStateError;
    UAnimStateNode* SourceStateNode = ResolveTargetAnimStateNode(StateMachineGraph, SourceStateName, ResolveSourceStateError);
    if (!SourceStateNode)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(FString::Printf(TEXT("Source %s"), *ResolveSourceStateError));
    }

    FString ResolveTargetStateError;
    UAnimStateNode* TargetStateNode = ResolveTargetAnimStateNode(StateMachineGraph, TargetStateName, ResolveTargetStateError);
    if (!TargetStateNode)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(FString::Printf(TEXT("Target %s"), *ResolveTargetStateError));
    }

    TArray<UAnimStateTransitionNode*> TransitionsToDelete;
    CollectTransitionsBetweenStates(StateMachineGraph, SourceStateNode, TargetStateNode, TransitionsToDelete);
    if (TransitionsToDelete.Num() == 0)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Transition not found in state machine '%s': %s -> %s"),
                *StateMachineName,
                *SourceStateName,
                *TargetStateName));
    }

    AnimBlueprint->Modify();
    StateMachineGraph->Modify();
    const int32 DeletedTransitionCount = TransitionsToDelete.Num();
    for (UAnimStateTransitionNode* TransitionNode : TransitionsToDelete)
    {
        if (!TransitionNode)
        {
            continue;
        }

        TransitionNode->Modify();
        TransitionNode->DestroyNode();
    }

    StateMachineGraph->NotifyGraphChanged();
    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(AnimBlueprint);
    UEditorAssetLibrary::SaveAsset(AssetPath, /*bOnlyIfIsDirty=*/false);

    TSharedPtr<FJsonObject> ReadParams = MakeShared<FJsonObject>();
    ReadParams->SetStringField(TEXT("asset_path"), AssetPath);
    ReadParams->SetStringField(TEXT("state_machine_name"), StateMachineName);

    TSharedPtr<FJsonObject> Result = HandleReadAnimStateMachine(ReadParams);
    if (!Result.IsValid())
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Failed to read back state machine after transition delete"));
    }
    if (Result->HasField(TEXT("error")))
    {
        return Result;
    }

    Result->SetStringField(TEXT("source_state_name"), SourceStateName);
    Result->SetStringField(TEXT("target_state_name"), TargetStateName);
    Result->SetNumberField(TEXT("deleted_transition_count"), DeletedTransitionCount);
    Result->SetBoolField(TEXT("deleted"), true);
    Result->SetBoolField(TEXT("success"), true);
    return Result;
}

// --------------------------------------------------------------------------- //
// set_anim_transition_rule
// --------------------------------------------------------------------------- //
TSharedPtr<FJsonObject> FUnrealAIAssetCommands::HandleSetAnimTransitionRule(const TSharedPtr<FJsonObject>& Params)
{
    FString AssetPath;
    if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath) || AssetPath.TrimStartAndEnd().IsEmpty())
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing or empty 'asset_path' parameter"));
    }
    AssetPath = AssetPath.TrimStartAndEnd();

    FString StateMachineName;
    if (!Params->TryGetStringField(TEXT("state_machine_name"), StateMachineName) || StateMachineName.TrimStartAndEnd().IsEmpty())
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing or empty 'state_machine_name' parameter"));
    }
    StateMachineName = StateMachineName.TrimStartAndEnd();

    FString SourceStateName;
    if (!Params->TryGetStringField(TEXT("source_state_name"), SourceStateName) || SourceStateName.TrimStartAndEnd().IsEmpty())
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing or empty 'source_state_name' parameter"));
    }
    SourceStateName = SourceStateName.TrimStartAndEnd();

    FString TargetStateName;
    if (!Params->TryGetStringField(TEXT("target_state_name"), TargetStateName) || TargetStateName.TrimStartAndEnd().IsEmpty())
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing or empty 'target_state_name' parameter"));
    }
    TargetStateName = TargetStateName.TrimStartAndEnd();

    FString RuleType;
    if (!Params->TryGetStringField(TEXT("rule_type"), RuleType) || RuleType.TrimStartAndEnd().IsEmpty())
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing or empty 'rule_type' parameter"));
    }
    RuleType = RuleType.TrimStartAndEnd();

    FString VariableName;
    Params->TryGetStringField(TEXT("variable_name"), VariableName);
    VariableName = VariableName.TrimStartAndEnd();

    FString ExpectedValue;
    Params->TryGetStringField(TEXT("expected_value"), ExpectedValue);
    if (ExpectedValue.TrimStartAndEnd().IsEmpty())
    {
        double NumericExpectedValue = 0.0;
        if (Params->TryGetNumberField(TEXT("expected_value"), NumericExpectedValue))
        {
            ExpectedValue = FString::SanitizeFloat(NumericExpectedValue);
        }
    }
    ExpectedValue = ExpectedValue.TrimStartAndEnd();

    UObject* LoadedObject = UEditorAssetLibrary::LoadAsset(AssetPath);
    UAnimBlueprint* AnimBlueprint = Cast<UAnimBlueprint>(LoadedObject);
    if (!AnimBlueprint)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("AnimBlueprint not found or wrong class: %s"), *AssetPath));
    }

    FString ResolveStateMachineError;
    UAnimationStateMachineGraph* StateMachineGraph = ResolveTargetStateMachineGraph(AnimBlueprint, StateMachineName, ResolveStateMachineError);
    if (!StateMachineGraph)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(ResolveStateMachineError);
    }

    FString ResolveSourceStateError;
    UAnimStateNode* SourceStateNode = ResolveTargetAnimStateNode(StateMachineGraph, SourceStateName, ResolveSourceStateError);
    if (!SourceStateNode)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(FString::Printf(TEXT("Source %s"), *ResolveSourceStateError));
    }

    FString ResolveTargetStateError;
    UAnimStateNode* TargetStateNode = ResolveTargetAnimStateNode(StateMachineGraph, TargetStateName, ResolveTargetStateError);
    if (!TargetStateNode)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(FString::Printf(TEXT("Target %s"), *ResolveTargetStateError));
    }

    TArray<UAnimStateTransitionNode*> MatchingTransitions;
    CollectTransitionsBetweenStates(StateMachineGraph, SourceStateNode, TargetStateNode, MatchingTransitions);
    if (MatchingTransitions.Num() == 0)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Transition not found in state machine '%s': %s -> %s"),
                *StateMachineName,
                *SourceStateName,
                *TargetStateName));
    }
    if (MatchingTransitions.Num() > 1)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Transition rule update is ambiguous because multiple transitions match: %s -> %s"),
                *SourceStateName,
                *TargetStateName));
    }

    AnimBlueprint->Modify();
    StateMachineGraph->Modify();

    FString ApplyRuleError;
    if (!ApplyAnimTransitionRule(AnimBlueprint, MatchingTransitions[0], RuleType, VariableName, ExpectedValue, ApplyRuleError))
    {
        return FUnrealAICommonUtils::CreateErrorResponse(ApplyRuleError);
    }

    StateMachineGraph->NotifyGraphChanged();
    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(AnimBlueprint);
    UEditorAssetLibrary::SaveAsset(AssetPath, /*bOnlyIfIsDirty=*/false);

    TSharedPtr<FJsonObject> ReadParams = MakeShared<FJsonObject>();
    ReadParams->SetStringField(TEXT("asset_path"), AssetPath);
    ReadParams->SetStringField(TEXT("state_machine_name"), StateMachineName);

    TSharedPtr<FJsonObject> Result = HandleReadAnimStateMachine(ReadParams);
    if (!Result.IsValid())
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Failed to read back state machine after transition rule update"));
    }
    if (Result->HasField(TEXT("error")))
    {
        return Result;
    }

    TSharedPtr<FJsonObject> UpdatedTransition;
    int32 MatchCount = 0;
    if (!TryGetMatchingAnimTransition(Result, SourceStateName, TargetStateName, UpdatedTransition, &MatchCount) || !UpdatedTransition.IsValid())
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Failed to resolve updated transition from post-rule readback"));
    }

    Result->SetStringField(TEXT("source_state_name"), SourceStateName);
    Result->SetStringField(TEXT("target_state_name"), TargetStateName);
    Result->SetStringField(TEXT("rule_type"), UpdatedTransition->GetStringField(TEXT("rule_type")));
    Result->SetStringField(TEXT("requested_rule_type"), RuleType);
    Result->SetStringField(TEXT("variable_name"), UpdatedTransition->GetStringField(TEXT("rule_variable_name")));
    Result->SetStringField(TEXT("expected_value"), UpdatedTransition->GetStringField(TEXT("rule_expected_value")));
    Result->SetObjectField(TEXT("transition"), UpdatedTransition.ToSharedRef());
    Result->SetBoolField(TEXT("updated"), true);
    Result->SetBoolField(TEXT("success"), true);
    return Result;
}

// --------------------------------------------------------------------------- //
// read_data_table_content
// --------------------------------------------------------------------------- //
TSharedPtr<FJsonObject> FUnrealAIAssetCommands::HandleReadDataTableContent(const TSharedPtr<FJsonObject>& Params)
{
    FString AssetPath;
    if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath) || AssetPath.TrimStartAndEnd().IsEmpty())
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing or empty 'asset_path' parameter"));
    }
    AssetPath = AssetPath.TrimStartAndEnd();

    UObject* LoadedObject = UEditorAssetLibrary::LoadAsset(AssetPath);
    UDataTable* DataTable = Cast<UDataTable>(LoadedObject);
    if (!DataTable)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("DataTable not found or wrong class: %s"), *AssetPath));
    }

    const UScriptStruct* RowStruct = DataTable->GetRowStruct();
    if (!RowStruct)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("DataTable is missing its row struct"));
    }

    TArray<TSharedPtr<FJsonValue>> ColumnArray;
    for (TFieldIterator<FProperty> PropertyIt(RowStruct); PropertyIt; ++PropertyIt)
    {
        const FProperty* Property = *PropertyIt;
        TSharedPtr<FJsonObject> ColumnObject = MakeShared<FJsonObject>();
        ColumnObject->SetStringField(TEXT("name"), Property->GetName());
        ColumnObject->SetStringField(TEXT("cpp_type"), Property->GetCPPType());
        ColumnObject->SetStringField(TEXT("property_class"), Property->GetClass()->GetName());
        ColumnArray.Add(MakeShared<FJsonValueObject>(ColumnObject));
    }

    TArray<TSharedPtr<FJsonValue>> RowNameArray;
    const TArray<FName> RowNames = DataTable->GetRowNames();
    RowNameArray.Reserve(RowNames.Num());
    for (const FName& RowName : RowNames)
    {
        RowNameArray.Add(MakeShared<FJsonValueString>(RowName.ToString()));
    }

    const FString KeyFieldName = GetDataTableKeyFieldName(DataTable);
    const FString TableJson = DataTable->GetTableAsJSON();
    TArray<TSharedPtr<FJsonValue>> ParsedRows;
    const bool bRowsParsed = TryParseTableRowsJson(TableJson, ParsedRows);

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("asset_path"), AssetPath);
    Result->SetStringField(TEXT("name"), DataTable->GetName());
    Result->SetStringField(TEXT("key_field_name"), KeyFieldName);
    Result->SetStringField(TEXT("row_struct_name"), RowStruct->GetName());
    Result->SetStringField(TEXT("row_struct_path"), RowStruct->GetPathName());
    Result->SetArrayField(TEXT("columns"), ColumnArray);
    Result->SetNumberField(TEXT("column_count"), ColumnArray.Num());
    Result->SetArrayField(TEXT("row_names"), RowNameArray);
    Result->SetNumberField(TEXT("row_count"), RowNames.Num());
    Result->SetStringField(TEXT("rows_json"), TableJson);
    Result->SetBoolField(TEXT("rows_parsed"), bRowsParsed);
    Result->SetArrayField(TEXT("rows"), ParsedRows);
    return Result;
}

// --------------------------------------------------------------------------- //
// read_data_table_row
// --------------------------------------------------------------------------- //
TSharedPtr<FJsonObject> FUnrealAIAssetCommands::HandleReadDataTableRow(const TSharedPtr<FJsonObject>& Params)
{
    FString AssetPath;
    if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath) || AssetPath.TrimStartAndEnd().IsEmpty())
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing or empty 'asset_path' parameter"));
    }
    AssetPath = AssetPath.TrimStartAndEnd();

    FString RowNameString;
    if (!Params->TryGetStringField(TEXT("row_name"), RowNameString) || RowNameString.TrimStartAndEnd().IsEmpty())
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing or empty 'row_name' parameter"));
    }
    RowNameString = RowNameString.TrimStartAndEnd();

    UObject* LoadedObject = UEditorAssetLibrary::LoadAsset(AssetPath);
    UDataTable* DataTable = Cast<UDataTable>(LoadedObject);
    if (!DataTable)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("DataTable not found or wrong class: %s"), *AssetPath));
    }

    const UScriptStruct* RowStruct = DataTable->GetRowStruct();
    if (!RowStruct)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("DataTable is missing its row struct"));
    }

    const FName RowName(*RowNameString);
    if (DataTable->FindRowUnchecked(RowName) == nullptr)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Row not found in DataTable: %s"), *RowNameString));
    }

    const FString KeyFieldName = GetDataTableKeyFieldName(DataTable);
    const FString TableJson = DataTable->GetTableAsJSON();
    TArray<TSharedPtr<FJsonValue>> ParsedRows;
    const bool bRowsParsed = TryParseTableRowsJson(TableJson, ParsedRows);

    TSharedPtr<FJsonObject> MatchingRow;
    if (bRowsParsed)
    {
        for (const TSharedPtr<FJsonValue>& RowValue : ParsedRows)
        {
            const TSharedPtr<FJsonObject>* RowObject = nullptr;
            if (!RowValue.IsValid() || !RowValue->TryGetObject(RowObject) || !RowObject || !RowObject->IsValid())
            {
                continue;
            }

            FString CandidateRowName;
            if ((*RowObject)->TryGetStringField(KeyFieldName, CandidateRowName) && CandidateRowName == RowNameString)
            {
                MatchingRow = *RowObject;
                break;
            }
        }
    }

    if (!MatchingRow.IsValid())
    {
        return FUnrealAICommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Row export payload not found in DataTable JSON: %s"), *RowNameString));
    }

    FString RowJson;
    {
        TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&RowJson);
        FJsonSerializer::Serialize(MatchingRow.ToSharedRef(), Writer);
        Writer->Close();
    }

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("asset_path"), AssetPath);
    Result->SetStringField(TEXT("name"), DataTable->GetName());
    Result->SetStringField(TEXT("row_name"), RowNameString);
    Result->SetStringField(TEXT("key_field_name"), KeyFieldName);
    Result->SetStringField(TEXT("row_struct_name"), RowStruct->GetName());
    Result->SetStringField(TEXT("row_struct_path"), RowStruct->GetPathName());
    Result->SetBoolField(TEXT("rows_parsed"), bRowsParsed);
    Result->SetStringField(TEXT("row_json"), RowJson);
    Result->SetObjectField(TEXT("row"), MatchingRow.ToSharedRef());
    return Result;
}

// --------------------------------------------------------------------------- //
// read_curve_table_content
// --------------------------------------------------------------------------- //
TSharedPtr<FJsonObject> FUnrealAIAssetCommands::HandleReadCurveTableContent(const TSharedPtr<FJsonObject>& Params)
{
    FString AssetPath;
    if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath) || AssetPath.TrimStartAndEnd().IsEmpty())
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing or empty 'asset_path' parameter"));
    }
    AssetPath = AssetPath.TrimStartAndEnd();

    UObject* LoadedObject = UEditorAssetLibrary::LoadAsset(AssetPath);
    UCurveTable* CurveTable = Cast<UCurveTable>(LoadedObject);
    if (!CurveTable)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("CurveTable not found or wrong class: %s"), *AssetPath));
    }

    TArray<FName> RowNames;
    CurveTable->GetRowMap().GenerateKeyArray(RowNames);

    TArray<TSharedPtr<FJsonValue>> RowNameArray;
    RowNameArray.Reserve(RowNames.Num());
    for (const FName& RowName : RowNames)
    {
        RowNameArray.Add(MakeShared<FJsonValueString>(RowName.ToString()));
    }

    FString TableJson = TEXT("[]");
    TArray<TSharedPtr<FJsonValue>> ParsedRows;
    bool bRowsParsed = true;
    if (RowNames.Num() > 0)
    {
        TableJson = CurveTable->GetTableAsJSON();
        bRowsParsed = TryParseTableRowsJson(TableJson, ParsedRows);
    }

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("asset_path"), AssetPath);
    Result->SetStringField(TEXT("name"), CurveTable->GetName());
    Result->SetStringField(TEXT("key_field_name"), TEXT("Name"));
    Result->SetStringField(TEXT("curve_table_mode"), GetCurveTableModeName(CurveTable));
    Result->SetArrayField(TEXT("row_names"), RowNameArray);
    Result->SetNumberField(TEXT("row_count"), RowNames.Num());
    Result->SetStringField(TEXT("rows_json"), TableJson);
    Result->SetBoolField(TEXT("rows_parsed"), bRowsParsed);
    Result->SetArrayField(TEXT("rows"), ParsedRows);
    return Result;
}

// --------------------------------------------------------------------------- //
// read_curve_table_row
// --------------------------------------------------------------------------- //
TSharedPtr<FJsonObject> FUnrealAIAssetCommands::HandleReadCurveTableRow(const TSharedPtr<FJsonObject>& Params)
{
    FString AssetPath;
    if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath) || AssetPath.TrimStartAndEnd().IsEmpty())
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing or empty 'asset_path' parameter"));
    }
    AssetPath = AssetPath.TrimStartAndEnd();

    FString RowNameString;
    if (!Params->TryGetStringField(TEXT("row_name"), RowNameString) || RowNameString.TrimStartAndEnd().IsEmpty())
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing or empty 'row_name' parameter"));
    }
    RowNameString = RowNameString.TrimStartAndEnd();

    UObject* LoadedObject = UEditorAssetLibrary::LoadAsset(AssetPath);
    UCurveTable* CurveTable = Cast<UCurveTable>(LoadedObject);
    if (!CurveTable)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("CurveTable not found or wrong class: %s"), *AssetPath));
    }

    const FName RowName(*RowNameString);
    if (CurveTable->FindCurveUnchecked(RowName) == nullptr)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Row not found in CurveTable: %s"), *RowNameString));
    }

    const FString TableJson = CurveTable->GetTableAsJSON();
    TArray<TSharedPtr<FJsonValue>> ParsedRows;
    const bool bRowsParsed = TryParseTableRowsJson(TableJson, ParsedRows);

    TSharedPtr<FJsonObject> MatchingRow;
    if (bRowsParsed)
    {
        for (const TSharedPtr<FJsonValue>& RowValue : ParsedRows)
        {
            const TSharedPtr<FJsonObject>* RowObject = nullptr;
            if (!RowValue.IsValid() || !RowValue->TryGetObject(RowObject) || !RowObject || !RowObject->IsValid())
            {
                continue;
            }

            FString CandidateRowName;
            if ((*RowObject)->TryGetStringField(TEXT("Name"), CandidateRowName) && CandidateRowName == RowNameString)
            {
                MatchingRow = *RowObject;
                break;
            }
        }
    }

    if (!MatchingRow.IsValid())
    {
        return FUnrealAICommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Row export payload not found in CurveTable JSON: %s"), *RowNameString));
    }

    FString RowJson;
    {
        TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&RowJson);
        FJsonSerializer::Serialize(MatchingRow.ToSharedRef(), Writer);
        Writer->Close();
    }

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("asset_path"), AssetPath);
    Result->SetStringField(TEXT("name"), CurveTable->GetName());
    Result->SetStringField(TEXT("row_name"), RowNameString);
    Result->SetStringField(TEXT("key_field_name"), TEXT("Name"));
    Result->SetStringField(TEXT("curve_table_mode"), GetCurveTableModeName(CurveTable));
    Result->SetBoolField(TEXT("rows_parsed"), bRowsParsed);
    Result->SetStringField(TEXT("row_json"), RowJson);
    Result->SetObjectField(TEXT("row"), MatchingRow.ToSharedRef());
    return Result;
}

// --------------------------------------------------------------------------- //
// create_curve_table_asset
// --------------------------------------------------------------------------- //
TSharedPtr<FJsonObject> FUnrealAIAssetCommands::HandleCreateCurveTableAsset(const TSharedPtr<FJsonObject>& Params)
{
    FString CurveTableName;
    if (!Params->TryGetStringField(TEXT("curve_table_name"), CurveTableName) || CurveTableName.TrimStartAndEnd().IsEmpty())
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing or empty 'curve_table_name' parameter"));
    }
    CurveTableName = CurveTableName.TrimStartAndEnd();

    FString RequestedCurveTableMode;
    if (!Params->TryGetStringField(TEXT("curve_table_mode"), RequestedCurveTableMode) || RequestedCurveTableMode.TrimStartAndEnd().IsEmpty())
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing or empty 'curve_table_mode' parameter"));
    }
    RequestedCurveTableMode = RequestedCurveTableMode.TrimStartAndEnd();

    FString DestinationPath = TEXT("/Game/CurveTables");
    Params->TryGetStringField(TEXT("destination_path"), DestinationPath);
    DestinationPath = DestinationPath.TrimStartAndEnd();
    if (DestinationPath.IsEmpty())
    {
        DestinationPath = TEXT("/Game/CurveTables");
    }

    ECurveTableMode CurveTableMode = ECurveTableMode::Empty;
    FString CanonicalCurveTableMode;
    FString CurveTableModeError;
    if (!TryParseCurveTableModeName(RequestedCurveTableMode, CurveTableMode, CanonicalCurveTableMode, CurveTableModeError))
    {
        return FUnrealAICommonUtils::CreateErrorResponse(CurveTableModeError);
    }

    if (!UEditorAssetLibrary::DoesDirectoryExist(DestinationPath))
    {
        UEditorAssetLibrary::MakeDirectory(DestinationPath);
    }

    const FString FullObjectPath = FString::Printf(TEXT("%s/%s.%s"), *DestinationPath, *CurveTableName, *CurveTableName);
    if (UEditorAssetLibrary::DoesAssetExist(FullObjectPath))
    {
        return FUnrealAICommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Asset already exists: %s"), *FullObjectPath));
    }

    const FString PackageName = FString::Printf(TEXT("%s/%s"), *DestinationPath, *CurveTableName);
    UPackage* Package = CreatePackage(*PackageName);
    if (!Package)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Failed to create package for CurveTable asset: %s"), *PackageName));
    }

    UCurveTable* CurveTable = NewObject<UCurveTable>(Package, *CurveTableName, RF_Public | RF_Standalone);
    if (!CurveTable)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Failed to create curve table asset"));
    }

    FName SeedRowName(TEXT("Curve"));
    if (CurveTableMode == ECurveTableMode::SimpleCurves)
    {
        FSimpleCurve& SeedCurve = CurveTable->AddSimpleCurve(SeedRowName);
        SeedCurve.SetKeyInterpMode(ERichCurveInterpMode::RCIM_Linear);
    }
    else
    {
        CurveTable->AddRichCurve(SeedRowName);
    }
    CurveTable->DeleteRow(SeedRowName);

    FAssetRegistryModule::AssetCreated(CurveTable);
    CurveTable->MarkPackageDirty();
    Package->MarkPackageDirty();
    UEditorAssetLibrary::SaveAsset(FullObjectPath, /*bOnlyIfIsDirty=*/false);

    TSharedPtr<FJsonObject> ReadParams = MakeShared<FJsonObject>();
    ReadParams->SetStringField(TEXT("asset_path"), FullObjectPath);

    TSharedPtr<FJsonObject> Result = HandleReadCurveTableContent(ReadParams);
    if (!Result.IsValid())
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Failed to read back CurveTable after create"));
    }
    if (Result->HasField(TEXT("error")))
    {
        return Result;
    }

    Result->SetStringField(TEXT("curve_table_name"), CurveTableName);
    Result->SetStringField(TEXT("destination_path"), DestinationPath);
    Result->SetStringField(TEXT("asset_path"), FullObjectPath);
    Result->SetStringField(TEXT("requested_curve_table_mode"), CanonicalCurveTableMode);
    Result->SetBoolField(TEXT("created"), true);
    Result->SetBoolField(TEXT("success"), true);
    return Result;
}

// --------------------------------------------------------------------------- //
// upsert_curve_table_row
// --------------------------------------------------------------------------- //
TSharedPtr<FJsonObject> FUnrealAIAssetCommands::HandleUpsertCurveTableRow(const TSharedPtr<FJsonObject>& Params)
{
    FString AssetPath;
    if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath) || AssetPath.TrimStartAndEnd().IsEmpty())
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing or empty 'asset_path' parameter"));
    }
    AssetPath = AssetPath.TrimStartAndEnd();

    FString RowNameString;
    if (!Params->TryGetStringField(TEXT("row_name"), RowNameString) || RowNameString.TrimStartAndEnd().IsEmpty())
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing or empty 'row_name' parameter"));
    }
    RowNameString = RowNameString.TrimStartAndEnd();

    const TSharedPtr<FJsonObject>* InputRowData = nullptr;
    if (!Params->TryGetObjectField(TEXT("row_data"), InputRowData) || !InputRowData || !(*InputRowData).IsValid())
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing or invalid 'row_data' object parameter"));
    }

    UObject* LoadedObject = UEditorAssetLibrary::LoadAsset(AssetPath);
    UCurveTable* CurveTable = Cast<UCurveTable>(LoadedObject);
    if (!CurveTable)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("CurveTable not found or wrong class: %s"), *AssetPath));
    }

    const ECurveTableMode CurveTableMode = CurveTable->GetCurveTableMode();
    if (CurveTableMode == ECurveTableMode::Empty)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(
            TEXT("CurveTable mode is Empty; create the asset with an explicit curve_table_mode before upserting rows"));
    }

    TArray<FCurveTableImportedKey> ImportedKeys;
    FString ParseErrorCode;
    FString ParseErrorMessage;
    if (!TryParseCurveTableRowData(*InputRowData, RowNameString, ImportedKeys, ParseErrorCode, ParseErrorMessage))
    {
        return FUnrealAICommonUtils::CreateErrorResponse(ParseErrorMessage);
    }

    const FName RowName(*RowNameString);
    const bool bRowExisted = CurveTable->FindCurveUnchecked(RowName) != nullptr;

    CurveTable->Modify(true);
    if (CurveTableMode == ECurveTableMode::SimpleCurves)
    {
        FSimpleCurve& Curve = CurveTable->AddSimpleCurve(RowName);
        PopulateSimpleCurveFromImportedKeys(Curve, ImportedKeys);
    }
    else
    {
        FRichCurve& Curve = CurveTable->AddRichCurve(RowName);
        PopulateRichCurveFromImportedKeys(Curve, ImportedKeys);
    }

    UCurveTable::InvalidateAllCachedCurves();
    CurveTable->OnCurveTableChanged().Broadcast();
    CurveTable->MarkPackageDirty();
    UEditorAssetLibrary::SaveAsset(AssetPath, /*bOnlyIfIsDirty=*/false);

    TSharedPtr<FJsonObject> ReadParams = MakeShared<FJsonObject>();
    ReadParams->SetStringField(TEXT("asset_path"), AssetPath);
    ReadParams->SetStringField(TEXT("row_name"), RowNameString);

    TSharedPtr<FJsonObject> Result = HandleReadCurveTableRow(ReadParams);
    if (!Result.IsValid())
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Failed to read back CurveTable row after upsert"));
    }
    if (Result->HasField(TEXT("error")))
    {
        return Result;
    }

    Result->SetNumberField(TEXT("key_count"), ImportedKeys.Num());
    Result->SetBoolField(TEXT("row_existed"), bRowExisted);
    Result->SetBoolField(TEXT("created"), !bRowExisted);
    Result->SetBoolField(TEXT("updated"), bRowExisted);
    Result->SetBoolField(TEXT("upserted"), true);
    Result->SetBoolField(TEXT("success"), true);
    return Result;
}

// --------------------------------------------------------------------------- //
// delete_curve_table_row
// --------------------------------------------------------------------------- //
TSharedPtr<FJsonObject> FUnrealAIAssetCommands::HandleDeleteCurveTableRow(const TSharedPtr<FJsonObject>& Params)
{
    FString AssetPath;
    if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath) || AssetPath.TrimStartAndEnd().IsEmpty())
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing or empty 'asset_path' parameter"));
    }
    AssetPath = AssetPath.TrimStartAndEnd();

    FString RowNameString;
    if (!Params->TryGetStringField(TEXT("row_name"), RowNameString) || RowNameString.TrimStartAndEnd().IsEmpty())
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing or empty 'row_name' parameter"));
    }
    RowNameString = RowNameString.TrimStartAndEnd();

    UObject* LoadedObject = UEditorAssetLibrary::LoadAsset(AssetPath);
    UCurveTable* CurveTable = Cast<UCurveTable>(LoadedObject);
    if (!CurveTable)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("CurveTable not found or wrong class: %s"), *AssetPath));
    }

    FName RowName(*RowNameString);
    if (CurveTable->FindCurveUnchecked(RowName) == nullptr)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Row not found in CurveTable: %s"), *RowNameString));
    }

    CurveTable->Modify(true);
    CurveTable->DeleteRow(RowName);
    if (CurveTable->FindCurveUnchecked(RowName) != nullptr)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Failed to delete CurveTable row: %s"), *RowNameString));
    }

    UCurveTable::InvalidateAllCachedCurves();
    CurveTable->OnCurveTableChanged().Broadcast();
    CurveTable->MarkPackageDirty();
    UEditorAssetLibrary::SaveAsset(AssetPath, /*bOnlyIfIsDirty=*/false);

    TSharedPtr<FJsonObject> ReadParams = MakeShared<FJsonObject>();
    ReadParams->SetStringField(TEXT("asset_path"), AssetPath);

    TSharedPtr<FJsonObject> Result = HandleReadCurveTableContent(ReadParams);
    if (!Result.IsValid())
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Failed to read back CurveTable after delete"));
    }
    if (Result->HasField(TEXT("error")))
    {
        return Result;
    }

    Result->SetStringField(TEXT("row_name"), RowNameString);
    Result->SetBoolField(TEXT("deleted"), true);
    Result->SetBoolField(TEXT("success"), true);
    return Result;
}

// --------------------------------------------------------------------------- //
// rename_curve_table_row
// --------------------------------------------------------------------------- //
TSharedPtr<FJsonObject> FUnrealAIAssetCommands::HandleRenameCurveTableRow(const TSharedPtr<FJsonObject>& Params)
{
    FString AssetPath;
    if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath) || AssetPath.TrimStartAndEnd().IsEmpty())
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing or empty 'asset_path' parameter"));
    }
    AssetPath = AssetPath.TrimStartAndEnd();

    FString OldRowNameString;
    if (!Params->TryGetStringField(TEXT("row_name"), OldRowNameString) || OldRowNameString.TrimStartAndEnd().IsEmpty())
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing or empty 'row_name' parameter"));
    }
    OldRowNameString = OldRowNameString.TrimStartAndEnd();

    FString NewRowNameString;
    if (!Params->TryGetStringField(TEXT("new_row_name"), NewRowNameString) || NewRowNameString.TrimStartAndEnd().IsEmpty())
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing or empty 'new_row_name' parameter"));
    }
    NewRowNameString = NewRowNameString.TrimStartAndEnd();

    UObject* LoadedObject = UEditorAssetLibrary::LoadAsset(AssetPath);
    UCurveTable* CurveTable = Cast<UCurveTable>(LoadedObject);
    if (!CurveTable)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("CurveTable not found or wrong class: %s"), *AssetPath));
    }

    FName OldRowName(*OldRowNameString);
    if (CurveTable->FindCurveUnchecked(OldRowName) == nullptr)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Row not found in CurveTable: %s"), *OldRowNameString));
    }

    if (OldRowNameString == NewRowNameString)
    {
        TSharedPtr<FJsonObject> ReadParams = MakeShared<FJsonObject>();
        ReadParams->SetStringField(TEXT("asset_path"), AssetPath);
        ReadParams->SetStringField(TEXT("row_name"), OldRowNameString);

        TSharedPtr<FJsonObject> Result = HandleReadCurveTableRow(ReadParams);
        if (!Result.IsValid())
        {
            return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Failed to read back CurveTable row after rename no-op"));
        }
        if (Result->HasField(TEXT("error")))
        {
            return Result;
        }

        Result->SetStringField(TEXT("old_row_name"), OldRowNameString);
        Result->SetStringField(TEXT("new_row_name"), NewRowNameString);
        Result->SetBoolField(TEXT("changed"), false);
        Result->SetBoolField(TEXT("renamed"), false);
        Result->SetBoolField(TEXT("success"), true);
        return Result;
    }

    FName NewRowName(*NewRowNameString);
    if (CurveTable->FindCurveUnchecked(NewRowName) != nullptr)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Row already exists in CurveTable: %s"), *NewRowNameString));
    }

    CurveTable->Modify(true);
    CurveTable->RenameRow(OldRowName, NewRowName);
    if (CurveTable->FindCurveUnchecked(NewRowName) == nullptr || CurveTable->FindCurveUnchecked(OldRowName) != nullptr)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Failed to rename CurveTable row '%s' to '%s'"),
                *OldRowNameString,
                *NewRowNameString));
    }

    UCurveTable::InvalidateAllCachedCurves();
    CurveTable->OnCurveTableChanged().Broadcast();
    CurveTable->MarkPackageDirty();
    UEditorAssetLibrary::SaveAsset(AssetPath, /*bOnlyIfIsDirty=*/false);

    TSharedPtr<FJsonObject> ReadParams = MakeShared<FJsonObject>();
    ReadParams->SetStringField(TEXT("asset_path"), AssetPath);
    ReadParams->SetStringField(TEXT("row_name"), NewRowNameString);

    TSharedPtr<FJsonObject> Result = HandleReadCurveTableRow(ReadParams);
    if (!Result.IsValid())
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Failed to read back CurveTable row after rename"));
    }
    if (Result->HasField(TEXT("error")))
    {
        return Result;
    }

    Result->SetStringField(TEXT("old_row_name"), OldRowNameString);
    Result->SetStringField(TEXT("new_row_name"), NewRowNameString);
    Result->SetBoolField(TEXT("changed"), true);
    Result->SetBoolField(TEXT("renamed"), true);
    Result->SetBoolField(TEXT("success"), true);
    return Result;
}

// --------------------------------------------------------------------------- //
// validate_data_table_row_import
// --------------------------------------------------------------------------- //
TSharedPtr<FJsonObject> FUnrealAIAssetCommands::HandleValidateDataTableRowImport(const TSharedPtr<FJsonObject>& Params)
{
    FString AssetPath;
    if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath) || AssetPath.TrimStartAndEnd().IsEmpty())
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing or empty 'asset_path' parameter"));
    }
    AssetPath = AssetPath.TrimStartAndEnd();

    FString RowNameString;
    if (!Params->TryGetStringField(TEXT("row_name"), RowNameString) || RowNameString.TrimStartAndEnd().IsEmpty())
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing or empty 'row_name' parameter"));
    }
    RowNameString = RowNameString.TrimStartAndEnd();

    const TSharedPtr<FJsonObject>* InputRowData = nullptr;
    if (!Params->TryGetObjectField(TEXT("row_data"), InputRowData) || !InputRowData || !(*InputRowData).IsValid())
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing or invalid 'row_data' object parameter"));
    }

    UObject* LoadedObject = UEditorAssetLibrary::LoadAsset(AssetPath);
    UDataTable* DataTable = Cast<UDataTable>(LoadedObject);
    if (!DataTable)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("DataTable not found or wrong class: %s"), *AssetPath));
    }

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("asset_path"), AssetPath);
    Result->SetStringField(TEXT("row_name"), RowNameString);
    Result->SetStringField(TEXT("table_type"), TEXT("DataTable"));

    TArray<TSharedPtr<FJsonValue>> Issues;
    int32 ErrorCount = 0;
    int32 WarningCount = 0;

    const FString KeyFieldName = GetDataTableKeyFieldName(DataTable);
    Result->SetStringField(TEXT("key_field_name"), KeyFieldName);

    const UScriptStruct* RowStruct = DataTable->GetRowStruct();
    TSharedPtr<FJsonObject> NormalizedRowData = CopyJsonObject(*InputRowData);
    Result->SetNumberField(TEXT("row_data_field_count"), NormalizedRowData->Values.Num());

    if (!RowStruct)
    {
        AddValidationIssue(
            Issues,
            ErrorCount,
            WarningCount,
            TEXT("error"),
            TEXT("missing_row_struct"),
            TEXT("DataTable is missing its row struct"));
    }
    else
    {
        Result->SetStringField(TEXT("row_struct_path"), RowStruct->GetPathName());

        if (KeyFieldName != TEXT("Name"))
        {
            if (RowStruct->FindPropertyByName(FName(*KeyFieldName)) == nullptr)
            {
                AddValidationIssue(
                    Issues,
                    ErrorCount,
                    WarningCount,
                    TEXT("error"),
                    TEXT("missing_key_field_property"),
                    FString::Printf(TEXT("DataTable key field '%s' is not present on row struct: %s"),
                        *KeyFieldName,
                        *RowStruct->GetPathName()),
                    KeyFieldName);
            }
            else
            {
                FString ExistingKeyFieldValue;
                if (NormalizedRowData->TryGetStringField(KeyFieldName, ExistingKeyFieldValue))
                {
                    if (ExistingKeyFieldValue != RowNameString)
                    {
                        AddValidationIssue(
                            Issues,
                            ErrorCount,
                            WarningCount,
                            TEXT("error"),
                            TEXT("key_field_mismatch"),
                            FString::Printf(TEXT("row_data.%s must match row_name '%s' for this DataTable"),
                                *KeyFieldName,
                                *RowNameString),
                            KeyFieldName);
                    }
                }
                else
                {
                    NormalizedRowData->SetStringField(KeyFieldName, RowNameString);
                    AddValidationIssue(
                        Issues,
                        ErrorCount,
                        WarningCount,
                        TEXT("warning"),
                        TEXT("key_field_will_be_injected"),
                        FString::Printf(TEXT("row_data is missing key field '%s'; it will be injected from row_name"),
                            *KeyFieldName),
                        KeyFieldName);
                }
            }
        }

        for (const TPair<FString, TSharedPtr<FJsonValue>>& FieldPair : NormalizedRowData->Values)
        {
            const FString& FieldName = FieldPair.Key;
            if (FieldName == TEXT("Name") && KeyFieldName == TEXT("Name"))
            {
                continue;
            }
            if (FieldName == KeyFieldName)
            {
                continue;
            }
            if (RowStruct->FindPropertyByName(FName(*FieldName)) == nullptr)
            {
                AddValidationIssue(
                    Issues,
                    ErrorCount,
                    WarningCount,
                    TEXT("error"),
                    TEXT("unknown_field"),
                    FString::Printf(TEXT("row_data field '%s' is not present on row struct: %s"),
                        *FieldName,
                        *RowStruct->GetPathName()),
                    FieldName);
            }
        }

        if (ErrorCount == 0)
        {
            TArray<uint8> RowDataBuffer;
            RowDataBuffer.SetNumZeroed(RowStruct->GetStructureSize());
            RowStruct->InitializeStruct(RowDataBuffer.GetData());

            FText ImportFailureReason;
            const bool bImported = FJsonObjectConverter::JsonObjectToUStruct(
                NormalizedRowData.ToSharedRef(),
                RowStruct,
                RowDataBuffer.GetData(),
                0,
                0,
                false,
                &ImportFailureReason);

            RowStruct->DestroyStruct(RowDataBuffer.GetData());

            if (!bImported)
            {
                const FString FailureMessage = ImportFailureReason.IsEmpty()
                    ? TEXT("Unknown row deserialization failure")
                    : ImportFailureReason.ToString();
                AddValidationIssue(
                    Issues,
                    ErrorCount,
                    WarningCount,
                    TEXT("error"),
                    TEXT("deserialization_failed"),
                    FString::Printf(TEXT("Failed to deserialize row_data into '%s': %s"),
                        *RowStruct->GetPathName(),
                        *FailureMessage));
            }
        }
    }

    Result->SetNumberField(TEXT("row_data_field_count"), NormalizedRowData->Values.Num());
    if (ErrorCount == 0)
    {
        FString NormalizedRowJson;
        if (TryWriteJsonObjectToString(NormalizedRowData, NormalizedRowJson))
        {
            Result->SetObjectField(TEXT("normalized_row_data"), NormalizedRowData.ToSharedRef());
            Result->SetStringField(TEXT("normalized_row_json"), NormalizedRowJson);
        }
    }

    Result->SetArrayField(TEXT("issues"), Issues);
    Result->SetNumberField(TEXT("error_count"), ErrorCount);
    Result->SetNumberField(TEXT("warning_count"), WarningCount);
    Result->SetBoolField(TEXT("is_valid"), ErrorCount == 0);
    Result->SetBoolField(TEXT("import_shape_valid"), ErrorCount == 0);
    Result->SetBoolField(TEXT("success"), true);
    return Result;
}

// --------------------------------------------------------------------------- //
// validate_curve_table_row_import
// --------------------------------------------------------------------------- //
TSharedPtr<FJsonObject> FUnrealAIAssetCommands::HandleValidateCurveTableRowImport(const TSharedPtr<FJsonObject>& Params)
{
    FString AssetPath;
    if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath) || AssetPath.TrimStartAndEnd().IsEmpty())
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing or empty 'asset_path' parameter"));
    }
    AssetPath = AssetPath.TrimStartAndEnd();

    FString RowNameString;
    if (!Params->TryGetStringField(TEXT("row_name"), RowNameString) || RowNameString.TrimStartAndEnd().IsEmpty())
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing or empty 'row_name' parameter"));
    }
    RowNameString = RowNameString.TrimStartAndEnd();

    const TSharedPtr<FJsonObject>* InputRowData = nullptr;
    if (!Params->TryGetObjectField(TEXT("row_data"), InputRowData) || !InputRowData || !(*InputRowData).IsValid())
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing or invalid 'row_data' object parameter"));
    }

    UObject* LoadedObject = UEditorAssetLibrary::LoadAsset(AssetPath);
    UCurveTable* CurveTable = Cast<UCurveTable>(LoadedObject);
    if (!CurveTable)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("CurveTable not found or wrong class: %s"), *AssetPath));
    }

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("asset_path"), AssetPath);
    Result->SetStringField(TEXT("row_name"), RowNameString);
    Result->SetStringField(TEXT("table_type"), TEXT("CurveTable"));
    Result->SetStringField(TEXT("key_field_name"), TEXT("Name"));
    Result->SetStringField(TEXT("curve_table_mode"), GetCurveTableModeName(CurveTable));

    TArray<TSharedPtr<FJsonValue>> Issues;
    int32 ErrorCount = 0;
    int32 WarningCount = 0;

    if (CurveTable->GetCurveTableMode() == ECurveTableMode::Empty)
    {
        AddValidationIssue(
            Issues,
            ErrorCount,
            WarningCount,
            TEXT("error"),
            TEXT("empty_curve_table_mode"),
            TEXT("CurveTable mode is Empty; create the asset with an explicit curve_table_mode before importing rows"));
    }
    else
    {
        TArray<FCurveTableImportedKey> ImportedKeys;
        FString ParseErrorCode;
        FString ParseErrorMessage;
        if (!TryParseCurveTableRowData(*InputRowData, RowNameString, ImportedKeys, ParseErrorCode, ParseErrorMessage))
        {
            AddValidationIssue(
                Issues,
                ErrorCount,
                WarningCount,
                TEXT("error"),
                ParseErrorCode,
                ParseErrorMessage);
        }
        else
        {
            if (!(*InputRowData)->HasField(TEXT("Name")))
            {
                AddValidationIssue(
                    Issues,
                    ErrorCount,
                    WarningCount,
                    TEXT("warning"),
                    TEXT("row_name_will_be_injected"),
                    TEXT("row_data is missing Name; it will be synthesized from row_name"),
                    TEXT("Name"));
            }

            TSharedPtr<FJsonObject> NormalizedRowData = BuildNormalizedCurveTableRowData(RowNameString, ImportedKeys);
            Result->SetNumberField(TEXT("key_count"), ImportedKeys.Num());
            Result->SetNumberField(TEXT("row_data_field_count"), NormalizedRowData->Values.Num());

            FString NormalizedRowJson;
            if (TryWriteJsonObjectToString(NormalizedRowData, NormalizedRowJson))
            {
                Result->SetObjectField(TEXT("normalized_row_data"), NormalizedRowData.ToSharedRef());
                Result->SetStringField(TEXT("normalized_row_json"), NormalizedRowJson);
            }
        }
    }

    Result->SetArrayField(TEXT("issues"), Issues);
    Result->SetNumberField(TEXT("error_count"), ErrorCount);
    Result->SetNumberField(TEXT("warning_count"), WarningCount);
    Result->SetBoolField(TEXT("is_valid"), ErrorCount == 0);
    Result->SetBoolField(TEXT("import_shape_valid"), ErrorCount == 0);
    Result->SetBoolField(TEXT("success"), true);
    return Result;
}

// --------------------------------------------------------------------------- //
// upsert_data_table_row
// --------------------------------------------------------------------------- //
TSharedPtr<FJsonObject> FUnrealAIAssetCommands::HandleUpsertDataTableRow(const TSharedPtr<FJsonObject>& Params)
{
    FString AssetPath;
    if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath) || AssetPath.TrimStartAndEnd().IsEmpty())
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing or empty 'asset_path' parameter"));
    }
    AssetPath = AssetPath.TrimStartAndEnd();

    FString RowNameString;
    if (!Params->TryGetStringField(TEXT("row_name"), RowNameString) || RowNameString.TrimStartAndEnd().IsEmpty())
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing or empty 'row_name' parameter"));
    }
    RowNameString = RowNameString.TrimStartAndEnd();

    const TSharedPtr<FJsonObject>* InputRowData = nullptr;
    if (!Params->TryGetObjectField(TEXT("row_data"), InputRowData) || !InputRowData || !(*InputRowData).IsValid())
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing or invalid 'row_data' object parameter"));
    }

    UObject* LoadedObject = UEditorAssetLibrary::LoadAsset(AssetPath);
    UDataTable* DataTable = Cast<UDataTable>(LoadedObject);
    if (!DataTable)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("DataTable not found or wrong class: %s"), *AssetPath));
    }

    const UScriptStruct* RowStruct = DataTable->GetRowStruct();
    if (!RowStruct)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("DataTable is missing its row struct"));
    }

    TSharedPtr<FJsonObject> RowDataObject = *InputRowData;
    const FString KeyFieldName = GetDataTableKeyFieldName(DataTable);
    if (KeyFieldName != TEXT("Name"))
    {
        if (RowStruct->FindPropertyByName(FName(*KeyFieldName)) == nullptr)
        {
            return FUnrealAICommonUtils::CreateErrorResponse(
                FString::Printf(TEXT("DataTable key field '%s' is not present on row struct: %s"),
                    *KeyFieldName,
                    *RowStruct->GetPathName()));
        }

        FString ExistingKeyFieldValue;
        if (RowDataObject->TryGetStringField(KeyFieldName, ExistingKeyFieldValue))
        {
            if (ExistingKeyFieldValue != RowNameString)
            {
                return FUnrealAICommonUtils::CreateErrorResponse(
                    FString::Printf(TEXT("row_data.%s must match row_name '%s' for this DataTable"),
                        *KeyFieldName,
                        *RowNameString));
            }
        }
        else
        {
            RowDataObject->SetStringField(KeyFieldName, RowNameString);
        }
    }

    TArray<uint8> RowDataBuffer;
    RowDataBuffer.SetNumZeroed(RowStruct->GetStructureSize());
    RowStruct->InitializeStruct(RowDataBuffer.GetData());

    FText ImportFailureReason;
    const bool bImported = FJsonObjectConverter::JsonObjectToUStruct(
        RowDataObject.ToSharedRef(),
        RowStruct,
        RowDataBuffer.GetData(),
        0,
        0,
        false,
        &ImportFailureReason);

    if (!bImported)
    {
        RowStruct->DestroyStruct(RowDataBuffer.GetData());
        const FString FailureMessage = ImportFailureReason.IsEmpty()
            ? TEXT("Unknown row deserialization failure")
            : ImportFailureReason.ToString();
        return FUnrealAICommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Failed to deserialize row_data into '%s': %s"),
                *RowStruct->GetPathName(),
                *FailureMessage));
    }

    const FName RowName(*RowNameString);
    const bool bRowExisted = DataTable->FindRowUnchecked(RowName) != nullptr;

    DataTable->Modify();
    DataTable->AddRow(RowName, RowDataBuffer.GetData(), RowStruct);
    RowStruct->DestroyStruct(RowDataBuffer.GetData());

    DataTable->MarkPackageDirty();
    UEditorAssetLibrary::SaveAsset(AssetPath, /*bOnlyIfIsDirty=*/false);

    TSharedPtr<FJsonObject> ReadParams = MakeShared<FJsonObject>();
    ReadParams->SetStringField(TEXT("asset_path"), AssetPath);
    ReadParams->SetStringField(TEXT("row_name"), RowNameString);

    TSharedPtr<FJsonObject> Result = HandleReadDataTableRow(ReadParams);
    if (!Result.IsValid())
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Failed to read back DataTable row after upsert"));
    }
    if (Result->HasField(TEXT("error")))
    {
        return Result;
    }

    Result->SetBoolField(TEXT("row_existed"), bRowExisted);
    Result->SetBoolField(TEXT("created"), !bRowExisted);
    Result->SetBoolField(TEXT("updated"), bRowExisted);
    Result->SetBoolField(TEXT("upserted"), true);
    Result->SetBoolField(TEXT("success"), true);
    return Result;
}

// --------------------------------------------------------------------------- //
// delete_data_table_row
// --------------------------------------------------------------------------- //
TSharedPtr<FJsonObject> FUnrealAIAssetCommands::HandleDeleteDataTableRow(const TSharedPtr<FJsonObject>& Params)
{
    FString AssetPath;
    if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath) || AssetPath.TrimStartAndEnd().IsEmpty())
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing or empty 'asset_path' parameter"));
    }
    AssetPath = AssetPath.TrimStartAndEnd();

    FString RowNameString;
    if (!Params->TryGetStringField(TEXT("row_name"), RowNameString) || RowNameString.TrimStartAndEnd().IsEmpty())
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing or empty 'row_name' parameter"));
    }
    RowNameString = RowNameString.TrimStartAndEnd();

    UObject* LoadedObject = UEditorAssetLibrary::LoadAsset(AssetPath);
    UDataTable* DataTable = Cast<UDataTable>(LoadedObject);
    if (!DataTable)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("DataTable not found or wrong class: %s"), *AssetPath));
    }

    const FName RowName(*RowNameString);
    if (DataTable->FindRowUnchecked(RowName) == nullptr)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Row not found in DataTable: %s"), *RowNameString));
    }

    DataTable->Modify();
    DataTable->RemoveRow(RowName);
    DataTable->MarkPackageDirty();
    UEditorAssetLibrary::SaveAsset(AssetPath, /*bOnlyIfIsDirty=*/false);

    TSharedPtr<FJsonObject> ReadParams = MakeShared<FJsonObject>();
    ReadParams->SetStringField(TEXT("asset_path"), AssetPath);

    TSharedPtr<FJsonObject> Result = HandleReadDataTableContent(ReadParams);
    if (!Result.IsValid())
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Failed to read back DataTable after delete"));
    }
    if (Result->HasField(TEXT("error")))
    {
        return Result;
    }

    Result->SetStringField(TEXT("row_name"), RowNameString);
    Result->SetBoolField(TEXT("deleted"), true);
    Result->SetBoolField(TEXT("success"), true);
    return Result;
}

// --------------------------------------------------------------------------- //
// rename_data_table_row
// --------------------------------------------------------------------------- //
TSharedPtr<FJsonObject> FUnrealAIAssetCommands::HandleRenameDataTableRow(const TSharedPtr<FJsonObject>& Params)
{
    FString AssetPath;
    if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath) || AssetPath.TrimStartAndEnd().IsEmpty())
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing or empty 'asset_path' parameter"));
    }
    AssetPath = AssetPath.TrimStartAndEnd();

    FString OldRowNameString;
    if (!Params->TryGetStringField(TEXT("row_name"), OldRowNameString) || OldRowNameString.TrimStartAndEnd().IsEmpty())
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing or empty 'row_name' parameter"));
    }
    OldRowNameString = OldRowNameString.TrimStartAndEnd();

    FString NewRowNameString;
    if (!Params->TryGetStringField(TEXT("new_row_name"), NewRowNameString) || NewRowNameString.TrimStartAndEnd().IsEmpty())
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing or empty 'new_row_name' parameter"));
    }
    NewRowNameString = NewRowNameString.TrimStartAndEnd();

    UObject* LoadedObject = UEditorAssetLibrary::LoadAsset(AssetPath);
    UDataTable* DataTable = Cast<UDataTable>(LoadedObject);
    if (!DataTable)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("DataTable not found or wrong class: %s"), *AssetPath));
    }

    const FName OldRowName(*OldRowNameString);
    uint8* OldRowData = DataTable->FindRowUnchecked(OldRowName);
    if (OldRowData == nullptr)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Row not found in DataTable: %s"), *OldRowNameString));
    }

    if (OldRowNameString == NewRowNameString)
    {
        TSharedPtr<FJsonObject> ReadParams = MakeShared<FJsonObject>();
        ReadParams->SetStringField(TEXT("asset_path"), AssetPath);
        ReadParams->SetStringField(TEXT("row_name"), OldRowNameString);

        TSharedPtr<FJsonObject> Result = HandleReadDataTableRow(ReadParams);
        if (!Result.IsValid())
        {
            return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Failed to read back DataTable row after rename no-op"));
        }
        if (Result->HasField(TEXT("error")))
        {
            return Result;
        }

        Result->SetStringField(TEXT("old_row_name"), OldRowNameString);
        Result->SetStringField(TEXT("new_row_name"), NewRowNameString);
        Result->SetBoolField(TEXT("changed"), false);
        Result->SetBoolField(TEXT("renamed"), false);
        Result->SetBoolField(TEXT("success"), true);
        return Result;
    }

    const FName NewRowName(*NewRowNameString);
    if (DataTable->FindRowUnchecked(NewRowName) != nullptr)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Row already exists in DataTable: %s"), *NewRowNameString));
    }

    FString KeyFieldSyncError;
    if (!TrySyncDataTableKeyFieldValue(DataTable, OldRowData, NewRowNameString, KeyFieldSyncError))
    {
        return FUnrealAICommonUtils::CreateErrorResponse(KeyFieldSyncError);
    }

    if (!FDataTableEditorUtils::RenameRow(DataTable, OldRowName, NewRowName))
    {
        FString RollbackError;
        TrySyncDataTableKeyFieldValue(DataTable, OldRowData, OldRowNameString, RollbackError);
        return FUnrealAICommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Failed to rename DataTable row '%s' to '%s'"),
                *OldRowNameString,
                *NewRowNameString));
    }

    DataTable->MarkPackageDirty();
    UEditorAssetLibrary::SaveAsset(AssetPath, /*bOnlyIfIsDirty=*/false);

    TSharedPtr<FJsonObject> ReadParams = MakeShared<FJsonObject>();
    ReadParams->SetStringField(TEXT("asset_path"), AssetPath);
    ReadParams->SetStringField(TEXT("row_name"), NewRowNameString);

    TSharedPtr<FJsonObject> Result = HandleReadDataTableRow(ReadParams);
    if (!Result.IsValid())
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Failed to read back DataTable row after rename"));
    }
    if (Result->HasField(TEXT("error")))
    {
        return Result;
    }

    Result->SetStringField(TEXT("old_row_name"), OldRowNameString);
    Result->SetStringField(TEXT("new_row_name"), NewRowNameString);
    Result->SetBoolField(TEXT("changed"), true);
    Result->SetBoolField(TEXT("renamed"), true);
    Result->SetBoolField(TEXT("success"), true);
    return Result;
}

// --------------------------------------------------------------------------- //
// duplicate_data_table_row
// --------------------------------------------------------------------------- //
TSharedPtr<FJsonObject> FUnrealAIAssetCommands::HandleDuplicateDataTableRow(const TSharedPtr<FJsonObject>& Params)
{
    FString AssetPath;
    if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath) || AssetPath.TrimStartAndEnd().IsEmpty())
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing or empty 'asset_path' parameter"));
    }
    AssetPath = AssetPath.TrimStartAndEnd();

    FString SourceRowNameString;
    if (!Params->TryGetStringField(TEXT("source_row_name"), SourceRowNameString) || SourceRowNameString.TrimStartAndEnd().IsEmpty())
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing or empty 'source_row_name' parameter"));
    }
    SourceRowNameString = SourceRowNameString.TrimStartAndEnd();

    FString NewRowNameString;
    if (!Params->TryGetStringField(TEXT("new_row_name"), NewRowNameString) || NewRowNameString.TrimStartAndEnd().IsEmpty())
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing or empty 'new_row_name' parameter"));
    }
    NewRowNameString = NewRowNameString.TrimStartAndEnd();

    UObject* LoadedObject = UEditorAssetLibrary::LoadAsset(AssetPath);
    UDataTable* DataTable = Cast<UDataTable>(LoadedObject);
    if (!DataTable)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("DataTable not found or wrong class: %s"), *AssetPath));
    }

    const FName SourceRowName(*SourceRowNameString);
    if (DataTable->FindRowUnchecked(SourceRowName) == nullptr)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Row not found in DataTable: %s"), *SourceRowNameString));
    }

    const FName NewRowName(*NewRowNameString);
    if (DataTable->FindRowUnchecked(NewRowName) != nullptr)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Row already exists in DataTable: %s"), *NewRowNameString));
    }

    uint8* NewRowData = FDataTableEditorUtils::DuplicateRow(DataTable, SourceRowName, NewRowName);
    if (NewRowData == nullptr)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Failed to duplicate DataTable row '%s' to '%s'"),
                *SourceRowNameString,
                *NewRowNameString));
    }

    FString KeySyncError;
    if (!TrySyncDataTableKeyFieldValue(DataTable, NewRowData, NewRowNameString, KeySyncError))
    {
        FDataTableEditorUtils::RemoveRow(DataTable, NewRowName);
        return FUnrealAICommonUtils::CreateErrorResponse(KeySyncError);
    }

    DataTable->HandleDataTableChanged(NewRowName);
    DataTable->MarkPackageDirty();
    UEditorAssetLibrary::SaveAsset(AssetPath, /*bOnlyIfIsDirty=*/false);

    TSharedPtr<FJsonObject> ReadParams = MakeShared<FJsonObject>();
    ReadParams->SetStringField(TEXT("asset_path"), AssetPath);
    ReadParams->SetStringField(TEXT("row_name"), NewRowNameString);

    TSharedPtr<FJsonObject> Result = HandleReadDataTableRow(ReadParams);
    if (!Result.IsValid())
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Failed to read back DataTable row after duplicate"));
    }
    if (Result->HasField(TEXT("error")))
    {
        return Result;
    }

    Result->SetStringField(TEXT("source_row_name"), SourceRowNameString);
    Result->SetStringField(TEXT("new_row_name"), NewRowNameString);
    Result->SetBoolField(TEXT("duplicated"), true);
    Result->SetBoolField(TEXT("success"), true);
    return Result;
}

// --------------------------------------------------------------------------- //
// move_data_table_row
// --------------------------------------------------------------------------- //
TSharedPtr<FJsonObject> FUnrealAIAssetCommands::HandleMoveDataTableRow(const TSharedPtr<FJsonObject>& Params)
{
    FString AssetPath;
    if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath) || AssetPath.TrimStartAndEnd().IsEmpty())
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing or empty 'asset_path' parameter"));
    }
    AssetPath = AssetPath.TrimStartAndEnd();

    FString RowNameString;
    if (!Params->TryGetStringField(TEXT("row_name"), RowNameString) || RowNameString.TrimStartAndEnd().IsEmpty())
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing or empty 'row_name' parameter"));
    }
    RowNameString = RowNameString.TrimStartAndEnd();

    FString DirectionString;
    if (!Params->TryGetStringField(TEXT("direction"), DirectionString) || DirectionString.TrimStartAndEnd().IsEmpty())
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing or empty 'direction' parameter"));
    }
    DirectionString = DirectionString.TrimStartAndEnd().ToLower();

    int32 NumRowsToMoveBy = 1;
    if (Params->HasField(TEXT("num_rows_to_move_by")))
    {
        NumRowsToMoveBy = Params->GetIntegerField(TEXT("num_rows_to_move_by"));
    }
    if (NumRowsToMoveBy < 1)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("'num_rows_to_move_by' must be >= 1"));
    }

    FDataTableEditorUtils::ERowMoveDirection Direction;
    if (DirectionString == TEXT("up"))
    {
        Direction = FDataTableEditorUtils::ERowMoveDirection::Up;
    }
    else if (DirectionString == TEXT("down"))
    {
        Direction = FDataTableEditorUtils::ERowMoveDirection::Down;
    }
    else
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("'direction' must be either 'up' or 'down'"));
    }

    UObject* LoadedObject = UEditorAssetLibrary::LoadAsset(AssetPath);
    UDataTable* DataTable = Cast<UDataTable>(LoadedObject);
    if (!DataTable)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("DataTable not found or wrong class: %s"), *AssetPath));
    }

    const FName RowName(*RowNameString);
    if (DataTable->FindRowUnchecked(RowName) == nullptr)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Row not found in DataTable: %s"), *RowNameString));
    }

    const TArray<FName> RowNamesBefore = DataTable->GetRowNames();
    const int32 RowIndexBefore = RowNamesBefore.IndexOfByKey(RowName);
    if (RowIndexBefore == INDEX_NONE)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Row order entry not found in DataTable: %s"), *RowNameString));
    }

    const int32 LastRowIndex = RowNamesBefore.Num() - 1;
    const int32 RequestedIndex = Direction == FDataTableEditorUtils::ERowMoveDirection::Up
        ? FMath::Max(0, RowIndexBefore - NumRowsToMoveBy)
        : FMath::Min(LastRowIndex, RowIndexBefore + NumRowsToMoveBy);

    TSharedPtr<FJsonObject> ReadParams = MakeShared<FJsonObject>();
    ReadParams->SetStringField(TEXT("asset_path"), AssetPath);

    if (RequestedIndex == RowIndexBefore)
    {
        TSharedPtr<FJsonObject> Result = HandleReadDataTableContent(ReadParams);
        if (!Result.IsValid())
        {
            return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Failed to read back DataTable after move no-op"));
        }
        if (Result->HasField(TEXT("error")))
        {
            return Result;
        }

        Result->SetStringField(TEXT("row_name"), RowNameString);
        Result->SetStringField(TEXT("direction"), DirectionString);
        Result->SetNumberField(TEXT("num_rows_to_move_by"), NumRowsToMoveBy);
        Result->SetNumberField(TEXT("row_index_before"), RowIndexBefore);
        Result->SetNumberField(TEXT("row_index_after"), RowIndexBefore);
        Result->SetBoolField(TEXT("changed"), false);
        Result->SetBoolField(TEXT("moved"), false);
        Result->SetBoolField(TEXT("success"), true);
        return Result;
    }

    DataTable->Modify();
    if (!FDataTableEditorUtils::MoveRow(DataTable, RowName, Direction, NumRowsToMoveBy))
    {
        return FUnrealAICommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Failed to move DataTable row '%s' %s by %d"),
                *RowNameString,
                *DirectionString,
                NumRowsToMoveBy));
    }

    DataTable->MarkPackageDirty();
    UEditorAssetLibrary::SaveAsset(AssetPath, /*bOnlyIfIsDirty=*/false);

    TSharedPtr<FJsonObject> Result = HandleReadDataTableContent(ReadParams);
    if (!Result.IsValid())
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Failed to read back DataTable after move"));
    }
    if (Result->HasField(TEXT("error")))
    {
        return Result;
    }

    const TArray<TSharedPtr<FJsonValue>>& RowNamesAfter = Result->GetArrayField(TEXT("row_names"));
    int32 RowIndexAfter = INDEX_NONE;
    for (int32 Index = 0; Index < RowNamesAfter.Num(); ++Index)
    {
        FString CandidateRowName;
        if (RowNamesAfter[Index].IsValid() && RowNamesAfter[Index]->TryGetString(CandidateRowName) && CandidateRowName == RowNameString)
        {
            RowIndexAfter = Index;
            break;
        }
    }

    if (RowIndexAfter == INDEX_NONE)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Moved row missing from DataTable readback: %s"), *RowNameString));
    }

    Result->SetStringField(TEXT("row_name"), RowNameString);
    Result->SetStringField(TEXT("direction"), DirectionString);
    Result->SetNumberField(TEXT("num_rows_to_move_by"), NumRowsToMoveBy);
    Result->SetNumberField(TEXT("row_index_before"), RowIndexBefore);
    Result->SetNumberField(TEXT("row_index_after"), RowIndexAfter);
    Result->SetBoolField(TEXT("changed"), RowIndexAfter != RowIndexBefore);
    Result->SetBoolField(TEXT("moved"), RowIndexAfter != RowIndexBefore);
    Result->SetBoolField(TEXT("success"), true);
    return Result;
}

// --------------------------------------------------------------------------- //
// create_data_table_asset
// --------------------------------------------------------------------------- //
TSharedPtr<FJsonObject> FUnrealAIAssetCommands::HandleCreateDataTableAsset(const TSharedPtr<FJsonObject>& Params)
{
    FString DataTableName;
    if (!Params->TryGetStringField(TEXT("data_table_name"), DataTableName) || DataTableName.TrimStartAndEnd().IsEmpty())
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing or empty 'data_table_name' parameter"));
    }
    DataTableName = DataTableName.TrimStartAndEnd();

    FString RowStructPath;
    if (!Params->TryGetStringField(TEXT("row_struct_path"), RowStructPath) || RowStructPath.TrimStartAndEnd().IsEmpty())
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing or empty 'row_struct_path' parameter"));
    }
    RowStructPath = RowStructPath.TrimStartAndEnd();

    FString DestinationPath = TEXT("/Game/DataTables");
    Params->TryGetStringField(TEXT("destination_path"), DestinationPath);
    DestinationPath = DestinationPath.TrimStartAndEnd();
    if (DestinationPath.IsEmpty())
    {
        DestinationPath = TEXT("/Game/DataTables");
    }

    const UScriptStruct* RowStruct = FindObject<UScriptStruct>(nullptr, *RowStructPath);
    if (!RowStruct)
    {
        RowStruct = LoadObject<UScriptStruct>(nullptr, *RowStructPath);
    }
    if (!RowStruct)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Failed to load row struct: %s"), *RowStructPath));
    }

    if (!UEditorAssetLibrary::DoesDirectoryExist(DestinationPath))
    {
        UEditorAssetLibrary::MakeDirectory(DestinationPath);
    }

    const FString FullObjectPath = FString::Printf(TEXT("%s/%s.%s"), *DestinationPath, *DataTableName, *DataTableName);
    if (UEditorAssetLibrary::DoesAssetExist(FullObjectPath))
    {
        return FUnrealAICommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Asset already exists: %s"), *FullObjectPath));
    }

    FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools"));
    IAssetTools& AssetTools = AssetToolsModule.Get();

    UDataTableFactory* Factory = NewObject<UDataTableFactory>();
    Factory->Struct = RowStruct;

    UObject* NewAsset = AssetTools.CreateAsset(DataTableName, DestinationPath, UDataTable::StaticClass(), Factory);
    UDataTable* DataTable = Cast<UDataTable>(NewAsset);
    if (!DataTable)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Failed to create data table asset"));
    }

    DataTable->MarkPackageDirty();
    UEditorAssetLibrary::SaveAsset(FullObjectPath, /*bOnlyIfIsDirty=*/false);

    TSharedPtr<FJsonObject> ReadParams = MakeShared<FJsonObject>();
    ReadParams->SetStringField(TEXT("asset_path"), FullObjectPath);

    TSharedPtr<FJsonObject> Result = HandleReadDataTableContent(ReadParams);
    Result->SetStringField(TEXT("data_table_name"), DataTableName);
    Result->SetStringField(TEXT("destination_path"), DestinationPath);
    Result->SetStringField(TEXT("asset_path"), FullObjectPath);
    Result->SetBoolField(TEXT("created"), true);
    Result->SetBoolField(TEXT("success"), true);
    return Result;
}

// --------------------------------------------------------------------------- //
// dependencies / referencers (shared helper)
// --------------------------------------------------------------------------- //
static TSharedPtr<FJsonObject> RunDepsOrRefs(const TSharedPtr<FJsonObject>& Params, bool bReferencers)
{
    FString AssetPath;
    if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing 'asset_path' parameter"));
    }

    FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
    IAssetRegistry& Registry = AssetRegistryModule.Get();

    // Convert object path or package path to package FName.
    FString PackagePath = AssetPath;
    if (PackagePath.Contains(TEXT(".")))
    {
        PackagePath = FPackageName::ObjectPathToPackageName(AssetPath);
    }

    TArray<FName> Out;
    if (bReferencers)
    {
        Registry.GetReferencers(*PackagePath, Out);
    }
    else
    {
        Registry.GetDependencies(*PackagePath, Out);
    }

    TArray<TSharedPtr<FJsonValue>> Items;
    for (const FName& N : Out)
    {
        Items.Add(MakeShared<FJsonValueString>(N.ToString()));
    }

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("asset_path"), AssetPath);
    Result->SetArrayField(bReferencers ? TEXT("referencers") : TEXT("dependencies"), Items);
    Result->SetNumberField(TEXT("count"), Items.Num());
    return Result;
}

TSharedPtr<FJsonObject> FUnrealAIAssetCommands::HandleGetAssetDependencies(const TSharedPtr<FJsonObject>& Params)
{
    return RunDepsOrRefs(Params, /*bReferencers=*/false);
}

TSharedPtr<FJsonObject> FUnrealAIAssetCommands::HandleGetReferencers(const TSharedPtr<FJsonObject>& Params)
{
    return RunDepsOrRefs(Params, /*bReferencers=*/true);
}

// --------------------------------------------------------------------------- //
// create_material_instance
// --------------------------------------------------------------------------- //
TSharedPtr<FJsonObject> FUnrealAIAssetCommands::HandleCreateMaterialInstance(const TSharedPtr<FJsonObject>& Params)
{
    FString ParentPath;
    if (!Params->TryGetStringField(TEXT("parent_material"), ParentPath))
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing 'parent_material' parameter"));
    }
    FString InstanceName;
    if (!Params->TryGetStringField(TEXT("instance_name"), InstanceName))
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing 'instance_name' parameter"));
    }
    FString DestPath = TEXT("/Game/Materials");
    Params->TryGetStringField(TEXT("dest_path"), DestPath);

    UMaterialInterface* Parent = Cast<UMaterialInterface>(UEditorAssetLibrary::LoadAsset(ParentPath));
    if (!Parent)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Failed to load parent material: %s"), *ParentPath));
    }

    // Make sure dest folder exists in content browser tree.
    if (!UEditorAssetLibrary::DoesDirectoryExist(DestPath))
    {
        UEditorAssetLibrary::MakeDirectory(DestPath);
    }

    const FString FullObjectPath = FString::Printf(TEXT("%s/%s.%s"), *DestPath, *InstanceName, *InstanceName);
    if (UEditorAssetLibrary::DoesAssetExist(FullObjectPath))
    {
        return FUnrealAICommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Asset already exists: %s"), *FullObjectPath));
    }

    FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools"));
    IAssetTools& AssetTools = AssetToolsModule.Get();

    UMaterialInstanceConstantFactoryNew* Factory = NewObject<UMaterialInstanceConstantFactoryNew>();
    Factory->InitialParent = Parent;

    UObject* NewAsset = AssetTools.CreateAsset(InstanceName, DestPath, UMaterialInstanceConstant::StaticClass(), Factory);
    UMaterialInstanceConstant* MIC = Cast<UMaterialInstanceConstant>(NewAsset);
    if (!MIC)
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Failed to create material instance"));
    }

    // Optional scalar parameters.
    const TSharedPtr<FJsonObject>* ScalarParams = nullptr;
    if (Params->TryGetObjectField(TEXT("scalar_parameters"), ScalarParams) && ScalarParams && (*ScalarParams).IsValid())
    {
        for (const auto& Pair : (*ScalarParams)->Values)
        {
            const float Value = static_cast<float>(Pair.Value->AsNumber());
            MIC->SetScalarParameterValueEditorOnly(FName(*Pair.Key), Value);
        }
    }

    // Optional vector parameters: { "name": [r,g,b,a] }
    const TSharedPtr<FJsonObject>* VectorParams = nullptr;
    if (Params->TryGetObjectField(TEXT("vector_parameters"), VectorParams) && VectorParams && (*VectorParams).IsValid())
    {
        for (const auto& Pair : (*VectorParams)->Values)
        {
            const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
            if (Pair.Value->TryGetArray(Arr) && Arr && Arr->Num() == 4)
            {
                FLinearColor C(
                    static_cast<float>((*Arr)[0]->AsNumber()),
                    static_cast<float>((*Arr)[1]->AsNumber()),
                    static_cast<float>((*Arr)[2]->AsNumber()),
                    static_cast<float>((*Arr)[3]->AsNumber()));
                MIC->SetVectorParameterValueEditorOnly(FName(*Pair.Key), C);
            }
        }
    }

    MIC->PostEditChange();
    MIC->MarkPackageDirty();
    UEditorAssetLibrary::SaveAsset(FullObjectPath, /*bOnlyIfIsDirty=*/false);

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("instance_name"), InstanceName);
    Result->SetStringField(TEXT("path"), FullObjectPath);
    Result->SetStringField(TEXT("parent_material"), ParentPath);
    Result->SetBoolField(TEXT("success"), true);
    return Result;
}

// --------------------------------------------------------------------------- //
// import_asset
// --------------------------------------------------------------------------- //
TSharedPtr<FJsonObject> FUnrealAIAssetCommands::HandleImportAsset(const TSharedPtr<FJsonObject>& Params)
{
    FString SourcePath;
    if (!Params->TryGetStringField(TEXT("source_path"), SourcePath))
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing 'source_path' parameter"));
    }
    FString DestPath;
    if (!Params->TryGetStringField(TEXT("dest_path"), DestPath))
    {
        return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing 'dest_path' parameter (e.g. /Game/Imported)"));
    }

    if (!FPaths::FileExists(SourcePath))
    {
        return FUnrealAICommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Source file not found: %s"), *SourcePath));
    }

    FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools"));
    IAssetTools& AssetTools = AssetToolsModule.Get();

    TArray<FString> Files;
    Files.Add(SourcePath);

    TArray<UObject*> Imported = AssetTools.ImportAssets(Files, DestPath);

    TArray<TSharedPtr<FJsonValue>> ImportedJson;
    for (UObject* Obj : Imported)
    {
        if (!Obj) continue;
        TSharedPtr<FJsonObject> A = MakeShared<FJsonObject>();
        A->SetStringField(TEXT("name"), Obj->GetName());
        A->SetStringField(TEXT("path"), Obj->GetPathName());
        A->SetStringField(TEXT("class"), Obj->GetClass()->GetName());
        ImportedJson.Add(MakeShared<FJsonValueObject>(A));

        // Persist to disk.
        UEditorAssetLibrary::SaveAsset(Obj->GetPathName(), /*bOnlyIfIsDirty=*/false);
    }

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("source_path"), SourcePath);
    Result->SetStringField(TEXT("dest_path"), DestPath);
    Result->SetArrayField(TEXT("imported"), ImportedJson);
    Result->SetNumberField(TEXT("count"), ImportedJson.Num());
    Result->SetBoolField(TEXT("success"), Imported.Num() > 0);
    if (Imported.Num() == 0)
    {
        Result->SetStringField(TEXT("error"), TEXT("ImportAssets returned no objects \u2014 unsupported file type or import failed"));
    }
    return Result;
}
