#include "Commands/UnrealAIWidgetCommands.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetBlueprintGeneratedClass.h"
#include "Blueprint/WidgetTree.h"
#include "Commands/UnrealAICommonUtils.h"
#include "Components/BackgroundBlurSlot.h"
#include "Components/BorderSlot.h"
#include "Components/ButtonSlot.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/GridSlot.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/NamedSlotInterface.h"
#include "Components/OverlaySlot.h"
#include "Components/PanelSlot.h"
#include "Components/PanelWidget.h"
#include "Components/SafeZoneSlot.h"
#include "Components/ScaleBoxSlot.h"
#include "Components/ScrollBoxSlot.h"
#include "Components/SizeBoxSlot.h"
#include "Components/SlateWrapperTypes.h"
#include "Components/StackBoxSlot.h"
#include "Components/UniformGridSlot.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/Widget.h"
#include "Components/WidgetSwitcherSlot.h"
#include "Components/WindowTitleBarAreaSlot.h"
#include "Components/WrapBoxSlot.h"
#include "EditorAssetLibrary.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "MovieScene.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/UObjectIterator.h"
#include "UObject/UnrealType.h"
#include "WidgetBlueprintEditorUtils.h"
#include "Widgets/Layout/Anchors.h"
#include "Animation/WidgetAnimation.h"
#include "WidgetBlueprint.h"

namespace
{
	FString SanitizePackageFolder(const FString& InFolder)
	{
		FString Folder = InFolder;
		if (Folder.IsEmpty())
		{
			Folder = TEXT("/Game/UI");
		}

		Folder.ReplaceInline(TEXT("\\"), TEXT("/"));
		while (Folder.EndsWith(TEXT("/")) && Folder.Len() > 1)
		{
			Folder.LeftChopInline(1, EAllowShrinking::No);
		}

		if (!Folder.StartsWith(TEXT("/")))
		{
			Folder = TEXT("/") + Folder;
		}

		if (!Folder.StartsWith(TEXT("/Game")))
		{
			Folder = TEXT("/Game/") + Folder.RightChop(1);
		}

		return Folder;
	}

	FString EnumValueToString(const UEnum* Enum, int64 Value)
	{
		if (!Enum)
		{
			return FString::FromInt(static_cast<int32>(Value));
		}
		return Enum->GetNameStringByValue(Value);
	}

	template <typename TBaseClass>
	UClass* ResolveClassReference(const FString& RequestedClass, const TArray<FString>& ScriptModules)
	{
		if (RequestedClass.IsEmpty())
		{
			return nullptr;
		}

		auto TryLoad = [](const FString& CandidatePath) -> UClass*
		{
			return LoadClass<TBaseClass>(nullptr, *CandidatePath);
		};

		if (RequestedClass.Contains(TEXT("/")) || RequestedClass.Contains(TEXT(".")))
		{
			if (UClass* LoadedClass = TryLoad(RequestedClass))
			{
				return LoadedClass;
			}
		}

		FString ShortName = RequestedClass;
		if (ShortName.StartsWith(TEXT("U")) || ShortName.StartsWith(TEXT("A")))
		{
			ShortName.RightChopInline(1, EAllowShrinking::No);
		}

		for (const FString& ModuleName : ScriptModules)
		{
			const FString ScriptPath = FString::Printf(TEXT("/Script/%s.%s"), *ModuleName, *ShortName);
			if (UClass* LoadedClass = TryLoad(ScriptPath))
			{
				return LoadedClass;
			}
		}

		for (TObjectIterator<UClass> It; It; ++It)
		{
			UClass* CandidateClass = *It;
			if (!CandidateClass->IsChildOf(TBaseClass::StaticClass()))
			{
				continue;
			}

			const FString CandidateName = CandidateClass->GetName();
			if (CandidateName == RequestedClass || CandidateName == ShortName || CandidateName == (TEXT("U") + ShortName))
			{
				return CandidateClass;
			}
		}

		return nullptr;
	}

	TSharedPtr<FJsonObject> MakeVector2Json(const FVector2D& Value)
	{
		TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetNumberField(TEXT("x"), Value.X);
		Result->SetNumberField(TEXT("y"), Value.Y);
		return Result;
	}

	TSharedPtr<FJsonObject> MakeMarginJson(const FMargin& Value)
	{
		TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetNumberField(TEXT("left"), Value.Left);
		Result->SetNumberField(TEXT("top"), Value.Top);
		Result->SetNumberField(TEXT("right"), Value.Right);
		Result->SetNumberField(TEXT("bottom"), Value.Bottom);
		return Result;
	}

	TSharedPtr<FJsonObject> MakeAnchorsJson(const FAnchors& Anchors)
	{
		TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetObjectField(TEXT("minimum"), MakeVector2Json(Anchors.Minimum));
		Result->SetObjectField(TEXT("maximum"), MakeVector2Json(Anchors.Maximum));
		return Result;
	}

	bool TryGetObjectParam(
		const TSharedPtr<FJsonObject>& Params,
		const TCHAR* FieldName,
		TSharedPtr<FJsonObject>& OutObject,
		FString& OutError)
	{
		const TSharedPtr<FJsonObject>* ObjectValue = nullptr;
		if (!Params->TryGetObjectField(FieldName, ObjectValue) || !ObjectValue || !ObjectValue->IsValid())
		{
			OutError = FString::Printf(TEXT("Field '%s' must be an object"), FieldName);
			return false;
		}

		OutObject = *ObjectValue;
		return true;
	}

	bool ParseVector2Json(const TSharedPtr<FJsonObject>& JsonObject, FVector2D& OutValue, const FString& Context, FString& OutError)
	{
		if (!JsonObject.IsValid())
		{
			OutError = FString::Printf(TEXT("%s must be an object"), *Context);
			return false;
		}

		double X = 0.0;
		double Y = 0.0;
		if (!JsonObject->TryGetNumberField(TEXT("x"), X) || !JsonObject->TryGetNumberField(TEXT("y"), Y))
		{
			OutError = FString::Printf(TEXT("%s must include numeric 'x' and 'y' fields"), *Context);
			return false;
		}

		OutValue = FVector2D(X, Y);
		return true;
	}

	bool ParseMarginJson(const TSharedPtr<FJsonObject>& JsonObject, FMargin& OutValue, const FString& Context, FString& OutError)
	{
		if (!JsonObject.IsValid())
		{
			OutError = FString::Printf(TEXT("%s must be an object"), *Context);
			return false;
		}

		double Left = 0.0;
		double Top = 0.0;
		double Right = 0.0;
		double Bottom = 0.0;
		if (!JsonObject->TryGetNumberField(TEXT("left"), Left) ||
			!JsonObject->TryGetNumberField(TEXT("top"), Top) ||
			!JsonObject->TryGetNumberField(TEXT("right"), Right) ||
			!JsonObject->TryGetNumberField(TEXT("bottom"), Bottom))
		{
			OutError = FString::Printf(TEXT("%s must include numeric left/top/right/bottom fields"), *Context);
			return false;
		}

		OutValue = FMargin(Left, Top, Right, Bottom);
		return true;
	}

	bool ParseAnchorsJson(const TSharedPtr<FJsonObject>& JsonObject, FAnchors& OutAnchors, FString& OutError)
	{
		if (!JsonObject.IsValid())
		{
			OutError = TEXT("Field 'anchors' must be an object");
			return false;
		}

		const TSharedPtr<FJsonObject>* MinimumObject = nullptr;
		const TSharedPtr<FJsonObject>* MaximumObject = nullptr;
		if (!JsonObject->TryGetObjectField(TEXT("minimum"), MinimumObject) || !MinimumObject || !MinimumObject->IsValid() ||
			!JsonObject->TryGetObjectField(TEXT("maximum"), MaximumObject) || !MaximumObject || !MaximumObject->IsValid())
		{
			OutError = TEXT("Field 'anchors' must include object fields 'minimum' and 'maximum'");
			return false;
		}

		FVector2D Minimum = FVector2D::ZeroVector;
		FVector2D Maximum = FVector2D::ZeroVector;
		if (!ParseVector2Json(*MinimumObject, Minimum, TEXT("anchors.minimum"), OutError) ||
			!ParseVector2Json(*MaximumObject, Maximum, TEXT("anchors.maximum"), OutError))
		{
			return false;
		}

		OutAnchors = FAnchors(Minimum.X, Minimum.Y, Maximum.X, Maximum.Y);
		return true;
	}

	bool ParseSlateChildSizeJson(const TSharedPtr<FJsonObject>& JsonObject, FSlateChildSize& OutChildSize, FString& OutError)
	{
		if (!JsonObject.IsValid())
		{
			OutError = TEXT("Field 'child_size' must be an object");
			return false;
		}

		FString SizeRuleValue;
		double Value = 0.0;
		if (!JsonObject->TryGetStringField(TEXT("size_rule"), SizeRuleValue) ||
			!JsonObject->TryGetNumberField(TEXT("value"), Value))
		{
			OutError = TEXT("Field 'child_size' must include string 'size_rule' and numeric 'value' fields");
			return false;
		}

		FString NormalizedSizeRule = SizeRuleValue;
		NormalizedSizeRule.TrimStartAndEndInline();
		NormalizedSizeRule.ToLowerInline();
		NormalizedSizeRule.ReplaceInline(TEXT("_"), TEXT(""));
		NormalizedSizeRule.ReplaceInline(TEXT("-"), TEXT(""));
		NormalizedSizeRule.ReplaceInline(TEXT(" "), TEXT(""));
		if (NormalizedSizeRule == TEXT("automatic"))
		{
			OutChildSize.SizeRule = ESlateSizeRule::Automatic;
		}
		else if (NormalizedSizeRule == TEXT("fill"))
		{
			OutChildSize.SizeRule = ESlateSizeRule::Fill;
		}
		else
		{
			OutError = TEXT("Field 'child_size.size_rule' must be 'automatic' or 'fill'");
			return false;
		}

		OutChildSize.Value = static_cast<float>(Value);
		return true;
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

	TArray<FString> CollectWidgetLayoutFieldNames(const TSharedPtr<FJsonObject>& Params)
	{
		TArray<FString> FieldNames;
		for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : Params->Values)
		{
			if (Pair.Key != TEXT("widget_blueprint_path") && Pair.Key != TEXT("widget_name"))
			{
				FieldNames.Add(Pair.Key);
			}
		}

		FieldNames.Sort();
		return FieldNames;
	}

	FString JoinFieldNames(const TArray<FString>& FieldNames)
	{
		return FString::Join(FieldNames, TEXT(", "));
	}

	FString SlateSizeRuleToString(ESlateSizeRule::Type SizeRule)
	{
		switch (SizeRule)
		{
		case ESlateSizeRule::Automatic:
			return TEXT("automatic");
		case ESlateSizeRule::Fill:
			return TEXT("fill");
		default:
			return FString::FromInt(static_cast<int32>(SizeRule));
		}
	}

	void AddPaddingField(const TSharedPtr<FJsonObject>& SlotJson, const FMargin& Padding)
	{
		SlotJson->SetObjectField(TEXT("padding"), MakeMarginJson(Padding));
	}

	void AddAlignmentFields(
		const TSharedPtr<FJsonObject>& SlotJson,
		EHorizontalAlignment HorizontalAlignment,
		EVerticalAlignment VerticalAlignment)
	{
		SlotJson->SetStringField(
			TEXT("horizontal_alignment"),
			EnumValueToString(StaticEnum<EHorizontalAlignment>(), static_cast<int64>(HorizontalAlignment)));
		SlotJson->SetStringField(
			TEXT("vertical_alignment"),
			EnumValueToString(StaticEnum<EVerticalAlignment>(), static_cast<int64>(VerticalAlignment)));
	}

	void AddPaddingAlignmentFields(
		const TSharedPtr<FJsonObject>& SlotJson,
		const FMargin& Padding,
		EHorizontalAlignment HorizontalAlignment,
		EVerticalAlignment VerticalAlignment)
	{
		AddPaddingField(SlotJson, Padding);
		AddAlignmentFields(SlotJson, HorizontalAlignment, VerticalAlignment);
	}

	TSharedPtr<FJsonObject> MakeChildSizeJson(const FSlateChildSize& ChildSize)
	{
		TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetStringField(TEXT("size_rule"), SlateSizeRuleToString(ChildSize.SizeRule));
		Result->SetNumberField(TEXT("value"), ChildSize.Value);
		return Result;
	}

	TSharedPtr<FJsonObject> MakeSlotJson(UPanelSlot* Slot)
	{
		TSharedPtr<FJsonObject> SlotJson = MakeShared<FJsonObject>();
		SlotJson->SetStringField(TEXT("slot_class"), Slot->GetClass()->GetName());

		if (const UCanvasPanelSlot* CanvasSlot = Cast<UCanvasPanelSlot>(Slot))
		{
			SlotJson->SetStringField(TEXT("slot_type"), TEXT("canvas"));
			SlotJson->SetObjectField(TEXT("anchors"), MakeAnchorsJson(CanvasSlot->GetAnchors()));
			SlotJson->SetObjectField(TEXT("size"), MakeVector2Json(CanvasSlot->GetSize()));
			SlotJson->SetObjectField(TEXT("offsets"), MakeMarginJson(CanvasSlot->GetOffsets()));
			SlotJson->SetObjectField(TEXT("alignment"), MakeVector2Json(CanvasSlot->GetAlignment()));
			SlotJson->SetNumberField(TEXT("z_order"), CanvasSlot->GetZOrder());
		}
		else if (const UGridSlot* GridSlot = Cast<UGridSlot>(Slot))
		{
			SlotJson->SetStringField(TEXT("slot_type"), TEXT("grid"));
			AddPaddingAlignmentFields(
				SlotJson,
				GridSlot->GetPadding(),
				GridSlot->GetHorizontalAlignment(),
				GridSlot->GetVerticalAlignment());
			SlotJson->SetNumberField(TEXT("row"), GridSlot->GetRow());
			SlotJson->SetNumberField(TEXT("row_span"), GridSlot->GetRowSpan());
			SlotJson->SetNumberField(TEXT("column"), GridSlot->GetColumn());
			SlotJson->SetNumberField(TEXT("column_span"), GridSlot->GetColumnSpan());
			SlotJson->SetNumberField(TEXT("layer"), GridSlot->GetLayer());
			SlotJson->SetObjectField(TEXT("nudge"), MakeVector2Json(GridSlot->GetNudge()));
		}
		else if (const UUniformGridSlot* UniformGridSlot = Cast<UUniformGridSlot>(Slot))
		{
			SlotJson->SetStringField(TEXT("slot_type"), TEXT("uniform_grid"));
			AddAlignmentFields(
				SlotJson,
				UniformGridSlot->GetHorizontalAlignment(),
				UniformGridSlot->GetVerticalAlignment());
			SlotJson->SetNumberField(TEXT("row"), UniformGridSlot->GetRow());
			SlotJson->SetNumberField(TEXT("column"), UniformGridSlot->GetColumn());
		}
		else if (const UHorizontalBoxSlot* HorizontalBoxSlot = Cast<UHorizontalBoxSlot>(Slot))
		{
			SlotJson->SetStringField(TEXT("slot_type"), TEXT("horizontal_box"));
			AddPaddingAlignmentFields(
				SlotJson,
				HorizontalBoxSlot->GetPadding(),
				HorizontalBoxSlot->GetHorizontalAlignment(),
				HorizontalBoxSlot->GetVerticalAlignment());
			SlotJson->SetObjectField(TEXT("child_size"), MakeChildSizeJson(HorizontalBoxSlot->GetSize()));
		}
		else if (const UVerticalBoxSlot* VerticalBoxSlot = Cast<UVerticalBoxSlot>(Slot))
		{
			SlotJson->SetStringField(TEXT("slot_type"), TEXT("vertical_box"));
			AddPaddingAlignmentFields(
				SlotJson,
				VerticalBoxSlot->GetPadding(),
				VerticalBoxSlot->GetHorizontalAlignment(),
				VerticalBoxSlot->GetVerticalAlignment());
			SlotJson->SetObjectField(TEXT("child_size"), MakeChildSizeJson(VerticalBoxSlot->GetSize()));
		}
		else if (const UScrollBoxSlot* ScrollBoxSlot = Cast<UScrollBoxSlot>(Slot))
		{
			SlotJson->SetStringField(TEXT("slot_type"), TEXT("scroll_box"));
			AddPaddingAlignmentFields(
				SlotJson,
				ScrollBoxSlot->GetPadding(),
				ScrollBoxSlot->GetHorizontalAlignment(),
				ScrollBoxSlot->GetVerticalAlignment());
			SlotJson->SetObjectField(TEXT("child_size"), MakeChildSizeJson(ScrollBoxSlot->GetSize()));
		}
		else if (const UStackBoxSlot* StackBoxSlot = Cast<UStackBoxSlot>(Slot))
		{
			SlotJson->SetStringField(TEXT("slot_type"), TEXT("stack_box"));
			AddPaddingAlignmentFields(
				SlotJson,
				StackBoxSlot->GetPadding(),
				StackBoxSlot->GetHorizontalAlignment(),
				StackBoxSlot->GetVerticalAlignment());
			SlotJson->SetObjectField(TEXT("child_size"), MakeChildSizeJson(StackBoxSlot->GetSize()));
		}
		else if (const UWrapBoxSlot* WrapBoxSlot = Cast<UWrapBoxSlot>(Slot))
		{
			SlotJson->SetStringField(TEXT("slot_type"), TEXT("wrap_box"));
			AddPaddingAlignmentFields(
				SlotJson,
				WrapBoxSlot->GetPadding(),
				WrapBoxSlot->GetHorizontalAlignment(),
				WrapBoxSlot->GetVerticalAlignment());
			SlotJson->SetBoolField(TEXT("fill_empty_space"), WrapBoxSlot->DoesFillEmptySpace());
			SlotJson->SetBoolField(TEXT("force_new_line"), WrapBoxSlot->DoesForceNewLine());
			SlotJson->SetNumberField(TEXT("fill_span_when_less_than"), WrapBoxSlot->GetFillSpanWhenLessThan());
		}
		else if (const USafeZoneSlot* SafeZoneSlot = Cast<USafeZoneSlot>(Slot))
		{
			SlotJson->SetStringField(TEXT("slot_type"), TEXT("safe_zone"));
			AddPaddingAlignmentFields(
				SlotJson,
				SafeZoneSlot->GetPadding(),
				SafeZoneSlot->GetHorizontalAlignment(),
				SafeZoneSlot->GetVerticalAlignment());
			SlotJson->SetBoolField(TEXT("is_title_safe"), SafeZoneSlot->IsTitleSafe());
			SlotJson->SetObjectField(TEXT("safe_area_scale"), MakeMarginJson(SafeZoneSlot->GetSafeAreaScale()));
		}
		else if (const UScaleBoxSlot* ScaleBoxSlot = Cast<UScaleBoxSlot>(Slot))
		{
			SlotJson->SetStringField(TEXT("slot_type"), TEXT("scale_box"));
			AddAlignmentFields(
				SlotJson,
				ScaleBoxSlot->GetHorizontalAlignment(),
				ScaleBoxSlot->GetVerticalAlignment());
		}
		else if (const UBackgroundBlurSlot* BackgroundBlurSlot = Cast<UBackgroundBlurSlot>(Slot))
		{
			SlotJson->SetStringField(TEXT("slot_type"), TEXT("background_blur"));
			AddPaddingAlignmentFields(
				SlotJson,
				BackgroundBlurSlot->GetPadding(),
				BackgroundBlurSlot->GetHorizontalAlignment(),
				BackgroundBlurSlot->GetVerticalAlignment());
		}
		else if (const UBorderSlot* BorderSlot = Cast<UBorderSlot>(Slot))
		{
			SlotJson->SetStringField(TEXT("slot_type"), TEXT("border"));
			AddPaddingAlignmentFields(
				SlotJson,
				BorderSlot->GetPadding(),
				BorderSlot->GetHorizontalAlignment(),
				BorderSlot->GetVerticalAlignment());
		}
		else if (const UButtonSlot* ButtonSlot = Cast<UButtonSlot>(Slot))
		{
			SlotJson->SetStringField(TEXT("slot_type"), TEXT("button"));
			AddPaddingAlignmentFields(
				SlotJson,
				ButtonSlot->GetPadding(),
				ButtonSlot->GetHorizontalAlignment(),
				ButtonSlot->GetVerticalAlignment());
		}
		else if (const UOverlaySlot* OverlaySlot = Cast<UOverlaySlot>(Slot))
		{
			SlotJson->SetStringField(TEXT("slot_type"), TEXT("overlay"));
			AddPaddingAlignmentFields(
				SlotJson,
				OverlaySlot->GetPadding(),
				OverlaySlot->GetHorizontalAlignment(),
				OverlaySlot->GetVerticalAlignment());
		}
		else if (const USizeBoxSlot* SizeBoxSlot = Cast<USizeBoxSlot>(Slot))
		{
			SlotJson->SetStringField(TEXT("slot_type"), TEXT("size_box"));
			AddPaddingAlignmentFields(
				SlotJson,
				SizeBoxSlot->GetPadding(),
				SizeBoxSlot->GetHorizontalAlignment(),
				SizeBoxSlot->GetVerticalAlignment());
		}
		else if (const UWidgetSwitcherSlot* WidgetSwitcherSlot = Cast<UWidgetSwitcherSlot>(Slot))
		{
			SlotJson->SetStringField(TEXT("slot_type"), TEXT("widget_switcher"));
			AddPaddingAlignmentFields(
				SlotJson,
				WidgetSwitcherSlot->GetPadding(),
				WidgetSwitcherSlot->GetHorizontalAlignment(),
				WidgetSwitcherSlot->GetVerticalAlignment());
		}
		else if (const UWindowTitleBarAreaSlot* WindowTitleBarAreaSlot = Cast<UWindowTitleBarAreaSlot>(Slot))
		{
			SlotJson->SetStringField(TEXT("slot_type"), TEXT("window_title_bar_area"));
			AddPaddingAlignmentFields(
				SlotJson,
				WindowTitleBarAreaSlot->GetPadding(),
				WindowTitleBarAreaSlot->GetHorizontalAlignment(),
				WindowTitleBarAreaSlot->GetVerticalAlignment());
		}

		return SlotJson;
	}

	struct FWidgetTreeStats
	{
		int32 WidgetCount = 0;
		int32 NamedSlotContentCount = 0;
	};

	TSharedPtr<FJsonObject> BuildWidgetNode(UWidget* Widget, int32 ChildIndex, const FString& ParentName, const FString& ParentNamedSlot, FWidgetTreeStats& Stats)
	{
		TSharedPtr<FJsonObject> NodeJson = MakeShared<FJsonObject>();
		NodeJson->SetStringField(TEXT("name"), Widget->GetName());
		NodeJson->SetStringField(TEXT("display_name"), Widget->GetDisplayLabel());
		NodeJson->SetStringField(TEXT("class"), Widget->GetClass()->GetName());
		NodeJson->SetBoolField(TEXT("is_variable"), Widget->bIsVariable);
		NodeJson->SetStringField(
			TEXT("visibility"),
			EnumValueToString(StaticEnum<ESlateVisibility>(), static_cast<int64>(Widget->GetVisibility())));
		NodeJson->SetObjectField(TEXT("render_transform_pivot"), MakeVector2Json(Widget->GetRenderTransformPivot()));

		const FString ToolTipText = Widget->GetToolTipText().ToString();
		if (!ToolTipText.IsEmpty())
		{
			NodeJson->SetStringField(TEXT("tooltip_text"), ToolTipText);
		}

		if (!ParentName.IsEmpty())
		{
			NodeJson->SetStringField(TEXT("parent_name"), ParentName);
		}

		if (ChildIndex >= 0)
		{
			NodeJson->SetNumberField(TEXT("child_index"), ChildIndex);
		}

		if (!ParentNamedSlot.IsEmpty())
		{
			NodeJson->SetStringField(TEXT("parent_named_slot"), ParentNamedSlot);
		}

		if (Widget->Slot)
		{
			NodeJson->SetObjectField(TEXT("slot"), MakeSlotJson(Widget->Slot));
		}

		++Stats.WidgetCount;

		TArray<TSharedPtr<FJsonValue>> Children;
		if (const UPanelWidget* PanelWidget = Cast<UPanelWidget>(Widget))
		{
			for (int32 Index = 0; Index < PanelWidget->GetChildrenCount(); ++Index)
			{
				if (UWidget* ChildWidget = PanelWidget->GetChildAt(Index))
				{
					Children.Add(MakeShared<FJsonValueObject>(
						BuildWidgetNode(ChildWidget, Index, Widget->GetName(), FString(), Stats)));
				}
			}
		}

		if (Children.Num() > 0)
		{
			NodeJson->SetArrayField(TEXT("children"), Children);
		}

		if (const INamedSlotInterface* NamedSlotOwner = Cast<INamedSlotInterface>(Widget))
		{
			TArray<FName> SlotNames;
			NamedSlotOwner->GetSlotNames(SlotNames);
			if (SlotNames.Num() > 0)
			{
				TArray<TSharedPtr<FJsonValue>> NamedSlotChildren;
				for (const FName& SlotName : SlotNames)
				{
					if (UWidget* SlotContent = NamedSlotOwner->GetContentForSlot(SlotName))
					{
						TSharedPtr<FJsonObject> SlotChildJson = MakeShared<FJsonObject>();
						SlotChildJson->SetStringField(TEXT("slot_name"), SlotName.ToString());
						SlotChildJson->SetObjectField(
							TEXT("widget"),
							BuildWidgetNode(SlotContent, INDEX_NONE, Widget->GetName(), SlotName.ToString(), Stats));
						NamedSlotChildren.Add(MakeShared<FJsonValueObject>(SlotChildJson));
						++Stats.NamedSlotContentCount;
					}
				}

				if (NamedSlotChildren.Num() > 0)
				{
					NodeJson->SetArrayField(TEXT("named_slot_children"), NamedSlotChildren);
				}
			}
		}

		return NodeJson;
	}

	TSharedPtr<FJsonObject> MakeBindingJson(const FDelegateEditorBinding& Binding)
	{
		TSharedPtr<FJsonObject> BindingJson = MakeShared<FJsonObject>();
		BindingJson->SetStringField(TEXT("object_name"), Binding.ObjectName);
		BindingJson->SetStringField(TEXT("property_name"), Binding.PropertyName.ToString());
		BindingJson->SetStringField(TEXT("kind"), EnumValueToString(StaticEnum<EBindingKind>(), static_cast<int64>(Binding.Kind)));

		if (!Binding.FunctionName.IsNone())
		{
			BindingJson->SetStringField(TEXT("function_name"), Binding.FunctionName.ToString());
		}

		if (!Binding.SourceProperty.IsNone())
		{
			BindingJson->SetStringField(TEXT("source_property"), Binding.SourceProperty.ToString());
		}

		if (!Binding.SourcePath.IsEmpty())
		{
			TArray<FString> SourcePathSegments;
			for (const FEditorPropertyPathSegment& Segment : Binding.SourcePath.Segments)
			{
				const FName SegmentName = Segment.GetMemberName();
				if (!SegmentName.IsNone())
				{
					SourcePathSegments.Add(SegmentName.ToString());
				}
			}

			if (SourcePathSegments.Num() > 0)
			{
				TArray<TSharedPtr<FJsonValue>> SourcePathSegmentValues;
				for (const FString& SourcePathSegment : SourcePathSegments)
				{
					SourcePathSegmentValues.Add(MakeShared<FJsonValueString>(SourcePathSegment));
				}

				BindingJson->SetStringField(TEXT("source_path"), FString::Join(SourcePathSegments, TEXT(".")));
				BindingJson->SetArrayField(TEXT("source_path_segments"), SourcePathSegmentValues);

				const FString SourcePathDisplay = Binding.SourcePath.GetDisplayText().ToString();
				if (!SourcePathDisplay.IsEmpty() && SourcePathDisplay != BindingJson->GetStringField(TEXT("source_path")))
				{
					BindingJson->SetStringField(TEXT("source_path_display"), SourcePathDisplay);
				}
			}
			else
			{
				BindingJson->SetStringField(TEXT("source_path"), Binding.SourcePath.GetDisplayText().ToString());
			}
		}

		if (Binding.MemberGuid.IsValid())
		{
			BindingJson->SetStringField(TEXT("member_guid"), Binding.MemberGuid.ToString(EGuidFormats::DigitsWithHyphens));
		}

		return BindingJson;
	}

	TSharedPtr<FJsonObject> MakeAnimationJson(const UWidgetAnimation* Animation)
	{
		TSharedPtr<FJsonObject> AnimationJson = MakeShared<FJsonObject>();
		AnimationJson->SetStringField(TEXT("name"), Animation->GetName());
		AnimationJson->SetStringField(TEXT("path"), Animation->GetPathName());

#if WITH_EDITOR
		AnimationJson->SetStringField(TEXT("display_label"), Animation->GetDisplayLabel());
#endif

		if (const UMovieScene* MovieScene = Animation->GetMovieScene())
		{
			AnimationJson->SetStringField(TEXT("movie_scene_name"), MovieScene->GetName());
		}

		return AnimationJson;
	}

	UWidgetBlueprint* LoadWidgetBlueprint(const FString& AssetPath)
	{
		if (AssetPath.IsEmpty())
		{
			return nullptr;
		}

		return Cast<UWidgetBlueprint>(UEditorAssetLibrary::LoadAsset(AssetPath));
	}

	bool ParseBindingSourcePathSegments(const TSharedPtr<FJsonObject>& Params, TArray<FString>& OutSegments, FString& OutError)
	{
		OutSegments.Reset();

		const TArray<TSharedPtr<FJsonValue>>* SourcePathSegmentValues = nullptr;
		if (Params->TryGetArrayField(TEXT("source_path_segments"), SourcePathSegmentValues))
		{
			for (const TSharedPtr<FJsonValue>& SourcePathSegmentValue : *SourcePathSegmentValues)
			{
				if (!SourcePathSegmentValue.IsValid() || SourcePathSegmentValue->Type != EJson::String)
				{
					OutError = TEXT("Field 'source_path_segments' must contain only strings");
					return false;
				}

				FString SourcePathSegment = SourcePathSegmentValue->AsString();
				SourcePathSegment.TrimStartAndEndInline();
				if (SourcePathSegment.IsEmpty())
				{
					OutError = TEXT("Field 'source_path_segments' cannot contain empty values");
					return false;
				}

				OutSegments.Add(SourcePathSegment);
			}

			return true;
		}

		FString SourcePathString;
		if (Params->TryGetStringField(TEXT("source_path"), SourcePathString) && !SourcePathString.IsEmpty())
		{
			SourcePathString.ParseIntoArray(OutSegments, TEXT("."), true);

			for (FString& SourcePathSegment : OutSegments)
			{
				SourcePathSegment.TrimStartAndEndInline();
			}

			OutSegments.RemoveAll([](const FString& SourcePathSegment) {
				return SourcePathSegment.IsEmpty();
			});

			if (OutSegments.Num() == 0)
			{
				OutError = TEXT("Field 'source_path' must include at least one property segment");
				return false;
			}
		}

		return true;
	}

	FProperty* FindPropertyByPathSegment(UStruct* OwnerStruct, const FString& RequestedSegment)
	{
		if (!OwnerStruct || RequestedSegment.IsEmpty())
		{
			return nullptr;
		}

		if (FProperty* Property = OwnerStruct->FindPropertyByName(*RequestedSegment))
		{
			return Property;
		}

		for (TFieldIterator<FProperty> It(OwnerStruct, EFieldIteratorFlags::IncludeSuper); It; ++It)
		{
			FProperty* CandidateProperty = *It;
			if (CandidateProperty->GetName().Equals(RequestedSegment, ESearchCase::IgnoreCase) ||
				CandidateProperty->GetDisplayNameText().ToString().Equals(RequestedSegment, ESearchCase::IgnoreCase))
			{
				return CandidateProperty;
			}
		}

		return nullptr;
	}

	bool ResolveBindingSourcePath(
		UWidgetBlueprint* WidgetBlueprint,
		const TArray<FString>& RequestedSegments,
		FEditorPropertyPath& OutSourcePath,
		FProperty*& OutLeafProperty,
		FString& OutError)
	{
		OutSourcePath = FEditorPropertyPath();
		OutLeafProperty = nullptr;

		if (!WidgetBlueprint)
		{
			OutError = TEXT("Invalid Widget Blueprint for source-path resolution");
			return false;
		}

		if (RequestedSegments.Num() == 0)
		{
			OutError = TEXT("Missing source path segments");
			return false;
		}

		UClass* BindingClass = WidgetBlueprint->SkeletonGeneratedClass ? WidgetBlueprint->SkeletonGeneratedClass : WidgetBlueprint->GeneratedClass;
		if (!BindingClass)
		{
			OutError = TEXT("Widget Blueprint does not have a generated class available for source-path resolution");
			return false;
		}

		TArray<FFieldVariant> FieldChain;
		UStruct* CurrentStruct = BindingClass;
		for (int32 SegmentIndex = 0; SegmentIndex < RequestedSegments.Num(); ++SegmentIndex)
		{
			const FString& RequestedSegment = RequestedSegments[SegmentIndex];
			FProperty* SegmentProperty = FindPropertyByPathSegment(CurrentStruct, RequestedSegment);
			if (!SegmentProperty)
			{
				OutError = FString::Printf(
					TEXT("Source path segment '%s' was not found on '%s'"),
					*RequestedSegment,
					CurrentStruct ? *CurrentStruct->GetName() : TEXT("<null>"));
				return false;
			}

			FieldChain.Add(SegmentProperty);

			const bool bIsLastSegment = SegmentIndex == RequestedSegments.Num() - 1;
			if (bIsLastSegment)
			{
				OutLeafProperty = SegmentProperty;
				break;
			}

			if (FStructProperty* StructProperty = CastField<FStructProperty>(SegmentProperty))
			{
				CurrentStruct = StructProperty->Struct;
			}
			else if (FObjectPropertyBase* ObjectProperty = CastField<FObjectPropertyBase>(SegmentProperty))
			{
				CurrentStruct = ObjectProperty->PropertyClass;
			}
			else
			{
				OutError = FString::Printf(
					TEXT("Source path segment '%s' is not traversable; only struct and object properties can appear before the final segment"),
					*RequestedSegment);
				return false;
			}

			if (!CurrentStruct)
			{
				OutError = FString::Printf(TEXT("Source path segment '%s' did not resolve to a valid type"), *RequestedSegment);
				return false;
			}
		}

		OutSourcePath = FEditorPropertyPath(FieldChain);
		return OutLeafProperty != nullptr;
	}

	FDelegateProperty* FindWidgetPropertyBindingDelegate(UWidget* Widget, const FName& PropertyName)
	{
		if (!Widget)
		{
			return nullptr;
		}

		const FName PropertyDelegateName(*(PropertyName.ToString() + TEXT("Delegate")));
		if (FDelegateProperty* PropertyDelegate = FindFProperty<FDelegateProperty>(Widget->GetClass(), PropertyDelegateName))
		{
			return PropertyDelegate;
		}

		return nullptr;
	}

	UFunction* FindWidgetBindingSignatureFunction(UWidget* Widget, const FName& PropertyName, bool& bOutUsesPropertyBindingDelegate)
	{
		bOutUsesPropertyBindingDelegate = false;
		if (!Widget)
		{
			return nullptr;
		}

		if (FDelegateProperty* PropertyDelegate = FindWidgetPropertyBindingDelegate(Widget, PropertyName))
		{
			bOutUsesPropertyBindingDelegate = true;
			return PropertyDelegate->SignatureFunction;
		}

		if (FMulticastDelegateProperty* MulticastDelegateProperty = FindFProperty<FMulticastDelegateProperty>(Widget->GetClass(), PropertyName))
		{
			return MulticastDelegateProperty->SignatureFunction;
		}

		if (FDelegateProperty* DelegateProperty = FindFProperty<FDelegateProperty>(Widget->GetClass(), PropertyName))
		{
			return DelegateProperty->SignatureFunction;
		}

		return nullptr;
	}

	UWidget* FindWidgetByName(UWidgetBlueprint* WidgetBlueprint, const FString& WidgetName)
	{
		if (!WidgetBlueprint || !WidgetBlueprint->WidgetTree || WidgetName.IsEmpty())
		{
			return nullptr;
		}

		return WidgetBlueprint->WidgetTree->FindWidget(*WidgetName);
	}

	UWidgetAnimation* FindWidgetAnimationByName(UWidgetBlueprint* WidgetBlueprint, const FString& AnimationName)
	{
		if (!WidgetBlueprint || AnimationName.IsEmpty())
		{
			return nullptr;
		}

		for (UWidgetAnimation* WidgetAnimation : WidgetBlueprint->Animations)
		{
			if (!WidgetAnimation)
			{
				continue;
			}

			if (WidgetAnimation->GetName().Equals(AnimationName, ESearchCase::IgnoreCase))
			{
				return WidgetAnimation;
			}

#if WITH_EDITOR
			if (WidgetAnimation->GetDisplayLabel().Equals(AnimationName, ESearchCase::IgnoreCase))
			{
				return WidgetAnimation;
			}
#endif
		}

		return nullptr;
	}

	FString MakeUniqueAnimationName(UWidgetBlueprint* WidgetBlueprint, const FString& RequestedName)
	{
		const FString BaseAnimationName = RequestedName.IsEmpty() ? TEXT("NewAnimation") : RequestedName;

		auto IsAnimationNameAvailable = [WidgetBlueprint](const FString& CandidateName) -> bool
		{
			if (!WidgetBlueprint)
			{
				return false;
			}

			return FindWidgetAnimationByName(WidgetBlueprint, CandidateName) == nullptr &&
				StaticFindObjectFast(UObject::StaticClass(), WidgetBlueprint, *CandidateName) == nullptr;
		};

		if (IsAnimationNameAvailable(BaseAnimationName))
		{
			return BaseAnimationName;
		}

		for (int32 Suffix = 1; Suffix < 10000; ++Suffix)
		{
			const FString CandidateName = FString::Printf(TEXT("%s_%d"), *BaseAnimationName, Suffix);
			if (IsAnimationNameAvailable(CandidateName))
			{
				return CandidateName;
			}
		}

		return FString::Printf(TEXT("%s_%d"), *BaseAnimationName, FMath::RandRange(10000, 99999));
	}

	FString GetDefaultWidgetName(UClass* WidgetClass)
	{
		if (!WidgetClass)
		{
			return TEXT("Widget");
		}

		FString WidgetClassName = WidgetClass->GetName();
		if (WidgetClassName.StartsWith(TEXT("U")) && WidgetClassName.Len() > 1)
		{
			WidgetClassName.RightChopInline(1, EAllowShrinking::No);
		}

		return WidgetClassName;
	}

	FString MakeUniqueWidgetName(UWidgetBlueprint* WidgetBlueprint, const FString& BaseName)
	{
		if (!WidgetBlueprint || !WidgetBlueprint->WidgetTree)
		{
			return BaseName;
		}

		if (!WidgetBlueprint->WidgetTree->FindWidget(*BaseName))
		{
			return BaseName;
		}

		for (int32 Suffix = 1; Suffix < 10000; ++Suffix)
		{
			const FString CandidateName = FString::Printf(TEXT("%s_%d"), *BaseName, Suffix);
			if (!WidgetBlueprint->WidgetTree->FindWidget(*CandidateName))
			{
				return CandidateName;
			}
		}

		return FString::Printf(TEXT("%s_%d"), *BaseName, FMath::RandRange(10000, 99999));
	}

	void CollectWidgetSubtree(UWidget* RootWidget, TArray<UWidget*>& OutWidgets)
	{
		OutWidgets.Reset();
		if (!RootWidget)
		{
			return;
		}

		OutWidgets.Add(RootWidget);
		UWidgetTree::ForWidgetAndChildren(RootWidget, [&OutWidgets](UWidget* Widget) {
			if (Widget && Widget != OutWidgets[0])
			{
				OutWidgets.Add(Widget);
			}
		});
	}

	bool IsWidgetInSubtree(UWidget* RootWidget, UWidget* CandidateWidget)
	{
		if (!RootWidget || !CandidateWidget)
		{
			return false;
		}

		bool bFound = false;
		UWidgetTree::ForEachWidgetAndChildrenUntil(RootWidget, [CandidateWidget, &bFound](UWidget* Widget) {
			bFound = Widget == CandidateWidget;
			return !bFound;
		});
		return bFound;
	}

	FString FindNamedSlotNameForContent(const TScriptInterface<INamedSlotInterface>& NamedSlotHost, UWidget* ContentWidget)
	{
		if (!NamedSlotHost || !ContentWidget)
		{
			return FString();
		}

		TArray<FName> SlotNames;
		NamedSlotHost->GetSlotNames(SlotNames);
		for (const FName& SlotName : SlotNames)
		{
			if (NamedSlotHost->GetContentForSlot(SlotName) == ContentWidget)
			{
				return SlotName.ToString();
			}
		}

		return FString();
	}

	bool ResolveNamedSlotHost(
		UWidgetBlueprint* WidgetBlueprint,
		const FString& ParentWidgetName,
		const FString& NamedSlotName,
		TScriptInterface<INamedSlotInterface>& OutNamedSlotHost,
		UObject*& OutHostObject,
		FString& OutHostName,
		FString& OutError)
	{
		if (!WidgetBlueprint || !WidgetBlueprint->WidgetTree)
		{
			OutError = TEXT("Widget Blueprint does not have a valid widget tree");
			return false;
		}

		if (NamedSlotName.IsEmpty())
		{
			OutError = TEXT("Missing named slot name");
			return false;
		}

		if (ParentWidgetName.IsEmpty())
		{
			OutHostObject = WidgetBlueprint->WidgetTree;
			OutNamedSlotHost.SetObject(WidgetBlueprint->WidgetTree);
			OutNamedSlotHost.SetInterface(Cast<INamedSlotInterface>(WidgetBlueprint->WidgetTree));
			OutHostName = TEXT("WidgetTree");
		}
		else
		{
			UWidget* HostWidget = FindWidgetByName(WidgetBlueprint, ParentWidgetName);
			if (!HostWidget)
			{
				OutError = FString::Printf(TEXT("Named slot host widget '%s' was not found"), *ParentWidgetName);
				return false;
			}

			INamedSlotInterface* NamedSlotInterface = Cast<INamedSlotInterface>(HostWidget);
			if (!NamedSlotInterface)
			{
				OutError = FString::Printf(TEXT("Widget '%s' does not expose named slots"), *ParentWidgetName);
				return false;
			}

			OutHostObject = HostWidget;
			OutNamedSlotHost.SetObject(HostWidget);
			OutNamedSlotHost.SetInterface(NamedSlotInterface);
			OutHostName = HostWidget->GetName();
		}

		if (!OutNamedSlotHost)
		{
			OutError = TEXT("Failed to resolve named slot host");
			return false;
		}

		TArray<FName> SlotNames;
		OutNamedSlotHost->GetSlotNames(SlotNames);
		if (!SlotNames.Contains(*NamedSlotName))
		{
			OutError = FString::Printf(TEXT("Named slot '%s' was not found on host '%s'"), *NamedSlotName, *OutHostName);
			return false;
		}

		return true;
	}

	bool DetachWidgetFromHierarchy(
		UWidgetBlueprint* WidgetBlueprint,
		UWidget* Widget,
		bool bAllowRootDetachment,
		FString& OutParentWidgetName,
		FString& OutNamedSlotName,
		FString& OutNamedSlotHostName,
		FString& OutError)
	{
		if (!WidgetBlueprint || !WidgetBlueprint->WidgetTree || !Widget)
		{
			OutError = TEXT("Invalid widget detach request");
			return false;
		}

		if (TScriptInterface<INamedSlotInterface> NamedSlotHost = FWidgetBlueprintEditorUtils::FindNamedSlotHostForContent(Widget, WidgetBlueprint->WidgetTree))
		{
			UObject* HostObject = NamedSlotHost.GetObject();
			OutNamedSlotName = FindNamedSlotNameForContent(NamedSlotHost, Widget);
			OutNamedSlotHostName = HostObject == WidgetBlueprint->WidgetTree ? TEXT("WidgetTree") : HostObject->GetName();

			if (HostObject)
			{
				HostObject->SetFlags(RF_Transactional);
				HostObject->Modify();
			}

			if (!FWidgetBlueprintEditorUtils::RemoveNamedSlotHostContent(Widget, NamedSlotHost))
			{
				OutError = FString::Printf(TEXT("Failed to detach widget '%s' from named slot host '%s'"), *Widget->GetName(), *OutNamedSlotHostName);
				return false;
			}

			return true;
		}

		int32 ChildIndex = INDEX_NONE;
		if (UPanelWidget* ParentWidget = WidgetBlueprint->WidgetTree->FindWidgetParent(Widget, ChildIndex))
		{
			OutParentWidgetName = ParentWidget->GetName();
			ParentWidget->SetFlags(RF_Transactional);
			ParentWidget->Modify();
			if (!ParentWidget->RemoveChild(Widget))
			{
				OutError = FString::Printf(TEXT("Failed to detach widget '%s' from parent '%s'"), *Widget->GetName(), *OutParentWidgetName);
				return false;
			}
			return true;
		}

		if (Widget == WidgetBlueprint->WidgetTree->RootWidget)
		{
			if (!bAllowRootDetachment)
			{
				OutError = TEXT("Reparenting the root widget is not supported in this wave");
				return false;
			}

			WidgetBlueprint->WidgetTree->SetFlags(RF_Transactional);
			WidgetBlueprint->WidgetTree->Modify();
			WidgetBlueprint->WidgetTree->RootWidget = nullptr;
			return true;
		}

		OutError = FString::Printf(TEXT("Widget '%s' is not attached to the WidgetTree"), *Widget->GetName());
		return false;
	}

	bool AttachWidgetToHierarchy(
		UWidgetBlueprint* WidgetBlueprint,
		UWidget* Widget,
		const FString& ParentWidgetName,
		const FString& NamedSlotName,
		FString& OutParentWidgetName,
		FString& OutNamedSlotName,
		FString& OutNamedSlotHostName,
		FString& OutError)
	{
		if (!WidgetBlueprint || !WidgetBlueprint->WidgetTree || !Widget)
		{
			OutError = TEXT("Invalid widget attach request");
			return false;
		}

		if (!NamedSlotName.IsEmpty())
		{
			TScriptInterface<INamedSlotInterface> NamedSlotHost;
			UObject* HostObject = nullptr;
			FString HostName;
			if (!ResolveNamedSlotHost(WidgetBlueprint, ParentWidgetName, NamedSlotName, NamedSlotHost, HostObject, HostName, OutError))
			{
				return false;
			}

			if (UWidget* ExistingContent = NamedSlotHost->GetContentForSlot(*NamedSlotName))
			{
				if (ExistingContent != Widget)
				{
					OutError = FString::Printf(TEXT("Named slot '%s' on '%s' already contains widget '%s'"), *NamedSlotName, *HostName, *ExistingContent->GetName());
					return false;
				}
			}

			if (HostObject)
			{
				HostObject->SetFlags(RF_Transactional);
				HostObject->Modify();
			}

			NamedSlotHost->SetContentForSlot(*NamedSlotName, Widget);
			OutNamedSlotName = NamedSlotName;
			OutNamedSlotHostName = HostName;
			if (!ParentWidgetName.IsEmpty())
			{
				OutParentWidgetName = ParentWidgetName;
			}
			return true;
		}

		if (ParentWidgetName.IsEmpty())
		{
			if (WidgetBlueprint->WidgetTree->RootWidget && WidgetBlueprint->WidgetTree->RootWidget != Widget)
			{
				OutError = TEXT("WidgetTree already has a root widget; provide a parent_widget_name or named_slot_name");
				return false;
			}

			WidgetBlueprint->WidgetTree->SetFlags(RF_Transactional);
			WidgetBlueprint->WidgetTree->Modify();
			WidgetBlueprint->WidgetTree->RootWidget = Widget;
			return true;
		}

		UWidget* ParentWidgetObject = FindWidgetByName(WidgetBlueprint, ParentWidgetName);
		if (!ParentWidgetObject)
		{
			OutError = FString::Printf(TEXT("Parent widget '%s' was not found"), *ParentWidgetName);
			return false;
		}

		UPanelWidget* ParentPanelWidget = Cast<UPanelWidget>(ParentWidgetObject);
		if (!ParentPanelWidget)
		{
			OutError = FString::Printf(TEXT("Widget '%s' is not a panel widget and cannot accept children"), *ParentWidgetName);
			return false;
		}

		ParentPanelWidget->SetFlags(RF_Transactional);
		ParentPanelWidget->Modify();
		if (!ParentPanelWidget->AddChild(Widget))
		{
			OutError = FString::Printf(TEXT("Failed to add widget '%s' under parent '%s'"), *Widget->GetName(), *ParentWidgetName);
			return false;
		}

		OutParentWidgetName = ParentWidgetName;
		return true;
	}

	void FinalizeWidgetBlueprintMutation(UWidgetBlueprint* WidgetBlueprint)
	{
		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(WidgetBlueprint);
		FKismetEditorUtilities::CompileBlueprint(WidgetBlueprint);
		UEditorAssetLibrary::SaveAsset(WidgetBlueprint->GetOutermost()->GetName(), false);
	}
}

FUnrealAIWidgetCommands::FUnrealAIWidgetCommands()
{
}

TSharedPtr<FJsonObject> FUnrealAIWidgetCommands::HandleCommand(const FString& CommandType, const TSharedPtr<FJsonObject>& Params)
{
	if (CommandType == TEXT("create_widget_blueprint"))
	{
		return HandleCreateWidgetBlueprint(Params);
	}
	else if (CommandType == TEXT("read_widget_blueprint_content"))
	{
		return HandleReadWidgetBlueprintContent(Params);
	}
	else if (CommandType == TEXT("add_widget_to_widget_blueprint"))
	{
		return HandleAddWidgetToWidgetBlueprint(Params);
	}
	else if (CommandType == TEXT("remove_widget_from_widget_blueprint"))
	{
		return HandleRemoveWidgetFromWidgetBlueprint(Params);
	}
	else if (CommandType == TEXT("reparent_widget_in_widget_blueprint"))
	{
		return HandleReparentWidgetInWidgetBlueprint(Params);
	}
	else if (CommandType == TEXT("set_widget_property_binding_in_widget_blueprint"))
	{
		return HandleSetWidgetPropertyBindingInWidgetBlueprint(Params);
	}
	else if (CommandType == TEXT("remove_widget_property_binding_from_widget_blueprint"))
	{
		return HandleRemoveWidgetPropertyBindingFromWidgetBlueprint(Params);
	}
	else if (CommandType == TEXT("create_widget_animation_in_widget_blueprint"))
	{
		return HandleCreateWidgetAnimationInWidgetBlueprint(Params);
	}
	else if (CommandType == TEXT("remove_widget_animation_from_widget_blueprint"))
	{
		return HandleRemoveWidgetAnimationFromWidgetBlueprint(Params);
	}
	else if (CommandType == TEXT("set_widget_slot_layout_in_widget_blueprint"))
	{
		return HandleSetWidgetSlotLayoutInWidgetBlueprint(Params);
	}

	return FUnrealAICommonUtils::CreateErrorResponse(FString::Printf(TEXT("Unknown widget command: %s"), *CommandType));
}

TSharedPtr<FJsonObject> FUnrealAIWidgetCommands::HandleCreateWidgetBlueprint(const TSharedPtr<FJsonObject>& Params)
{
	FString WidgetName;
	if (!Params->TryGetStringField(TEXT("widget_name"), WidgetName) || WidgetName.IsEmpty())
	{
		return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing 'widget_name' parameter"));
	}

	FString DestinationPath = TEXT("/Game/UI");
	Params->TryGetStringField(TEXT("destination_path"), DestinationPath);
	DestinationPath = SanitizePackageFolder(DestinationPath);

	const FString WidgetAssetPath = DestinationPath + TEXT("/") + WidgetName;
	if (UEditorAssetLibrary::DoesAssetExist(WidgetAssetPath))
	{
		return FUnrealAICommonUtils::CreateErrorResponse(FString::Printf(TEXT("Widget Blueprint already exists: %s"), *WidgetAssetPath));
	}

	FString ParentClassName = TEXT("UserWidget");
	Params->TryGetStringField(TEXT("parent_class"), ParentClassName);
	UClass* ParentClass = ResolveClassReference<UUserWidget>(ParentClassName, {TEXT("UMG")});
	if (!ParentClass)
	{
		return FUnrealAICommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("Unable to resolve widget parent class '%s'"), *ParentClassName));
	}

	FString RootWidgetClassName = TEXT("CanvasPanel");
	Params->TryGetStringField(TEXT("root_widget_class"), RootWidgetClassName);
	UClass* RootWidgetClass = nullptr;
	if (!RootWidgetClassName.IsEmpty())
	{
		RootWidgetClass = ResolveClassReference<UPanelWidget>(RootWidgetClassName, {TEXT("UMG")});
		if (!RootWidgetClass)
		{
			return FUnrealAICommonUtils::CreateErrorResponse(
				FString::Printf(TEXT("Unable to resolve root widget class '%s'"), *RootWidgetClassName));
		}
	}

	bool bAddDefaultChild = false;
	Params->TryGetBoolField(TEXT("add_default_child"), bAddDefaultChild);

	FString DefaultChildWidgetClassName = TEXT("TextBlock");
	Params->TryGetStringField(TEXT("default_child_widget_class"), DefaultChildWidgetClassName);
	UClass* DefaultChildWidgetClass = nullptr;
	if (bAddDefaultChild)
	{
		DefaultChildWidgetClass = ResolveClassReference<UWidget>(DefaultChildWidgetClassName, {TEXT("UMG")});
		if (!DefaultChildWidgetClass)
		{
			return FUnrealAICommonUtils::CreateErrorResponse(
				FString::Printf(TEXT("Unable to resolve default child widget class '%s'"), *DefaultChildWidgetClassName));
		}
	}

	UPackage* Package = CreatePackage(*WidgetAssetPath);
	if (!Package)
	{
		return FUnrealAICommonUtils::CreateErrorResponse(FString::Printf(TEXT("Failed to create package '%s'"), *WidgetAssetPath));
	}

	UWidgetBlueprint* NewWidgetBlueprint = Cast<UWidgetBlueprint>(
		FKismetEditorUtilities::CreateBlueprint(
			ParentClass,
			Package,
			*WidgetName,
			BPTYPE_Normal,
			UWidgetBlueprint::StaticClass(),
			UWidgetBlueprintGeneratedClass::StaticClass(),
			NAME_None));
	if (!NewWidgetBlueprint)
	{
		return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Failed to create Widget Blueprint asset"));
	}

	if (RootWidgetClass && NewWidgetBlueprint->WidgetTree && !NewWidgetBlueprint->WidgetTree->RootWidget)
	{
		UWidget* RootWidget = NewWidgetBlueprint->WidgetTree->ConstructWidget<UWidget>(RootWidgetClass, FName(TEXT("RootWidget")));
		if (RootWidget)
		{
			RootWidget->bIsVariable = true;
			NewWidgetBlueprint->WidgetTree->RootWidget = RootWidget;
			NewWidgetBlueprint->OnVariableAdded(RootWidget->GetFName());

			if (bAddDefaultChild)
			{
				UPanelWidget* RootPanelWidget = Cast<UPanelWidget>(RootWidget);
				if (!RootPanelWidget)
				{
					return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Root widget is not a panel and cannot accept a default child"));
				}

				UWidget* DefaultChildWidget = NewWidgetBlueprint->WidgetTree->ConstructWidget<UWidget>(
					DefaultChildWidgetClass,
					FName(TEXT("RootChild")));
				if (!DefaultChildWidget)
				{
					return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Failed to construct default child widget"));
				}

				DefaultChildWidget->bIsVariable = true;
				if (!RootPanelWidget->AddChild(DefaultChildWidget))
				{
					return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Failed to add default child widget to root panel"));
				}

				NewWidgetBlueprint->OnVariableAdded(DefaultChildWidget->GetFName());
			}
		}
	}

	FAssetRegistryModule::AssetCreated(NewWidgetBlueprint);
	Package->MarkPackageDirty();
	FKismetEditorUtilities::CompileBlueprint(NewWidgetBlueprint);
	UEditorAssetLibrary::SaveAsset(WidgetAssetPath, false);

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("widget_name"), WidgetName);
	Data->SetStringField(TEXT("widget_blueprint_path"), WidgetAssetPath);
	Data->SetStringField(TEXT("parent_class"), ParentClass->GetName());
	if (RootWidgetClass)
	{
		Data->SetStringField(TEXT("root_widget_class"), RootWidgetClass->GetName());
	}
	Data->SetBoolField(TEXT("add_default_child"), bAddDefaultChild);
	if (bAddDefaultChild && DefaultChildWidgetClass)
	{
		Data->SetStringField(TEXT("default_child_widget_class"), DefaultChildWidgetClass->GetName());
	}

	return FUnrealAICommonUtils::CreateSuccessResponse(Data);
}

TSharedPtr<FJsonObject> FUnrealAIWidgetCommands::HandleSetWidgetPropertyBindingInWidgetBlueprint(const TSharedPtr<FJsonObject>& Params)
{
	FString WidgetBlueprintPath;
	if (!Params->TryGetStringField(TEXT("widget_blueprint_path"), WidgetBlueprintPath) || WidgetBlueprintPath.IsEmpty())
	{
		return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing 'widget_blueprint_path' parameter"));
	}

	FString WidgetName;
	if (!Params->TryGetStringField(TEXT("widget_name"), WidgetName) || WidgetName.IsEmpty())
	{
		return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing 'widget_name' parameter"));
	}

	FString PropertyNameString;
	if (!Params->TryGetStringField(TEXT("property_name"), PropertyNameString) || PropertyNameString.IsEmpty())
	{
		return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing 'property_name' parameter"));
	}

	FString FunctionNameString;
	const bool bHasFunctionName = Params->TryGetStringField(TEXT("function_name"), FunctionNameString) && !FunctionNameString.IsEmpty();

	FString SourcePropertyString;
	const bool bHasSourceProperty = Params->TryGetStringField(TEXT("source_property"), SourcePropertyString) && !SourcePropertyString.IsEmpty();

	TArray<FString> SourcePathSegments;
	FString SourcePathParseError;
	if (!ParseBindingSourcePathSegments(Params, SourcePathSegments, SourcePathParseError))
	{
		return FUnrealAICommonUtils::CreateErrorResponse(SourcePathParseError);
	}
	const bool bHasSourcePath = SourcePathSegments.Num() > 0;

	if (!bHasFunctionName && !bHasSourceProperty && !bHasSourcePath)
	{
		return FUnrealAICommonUtils::CreateErrorResponse(
			TEXT("Provide either 'function_name' or a property binding via 'source_property'/'source_path'"));
	}

	if (bHasFunctionName && (bHasSourceProperty || bHasSourcePath))
	{
		return FUnrealAICommonUtils::CreateErrorResponse(
			TEXT("'function_name' cannot be combined with 'source_property' or 'source_path'"));
	}

	UWidgetBlueprint* WidgetBlueprint = LoadWidgetBlueprint(WidgetBlueprintPath);
	if (!WidgetBlueprint)
	{
		return FUnrealAICommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("Failed to load Widget Blueprint '%s'"), *WidgetBlueprintPath));
	}

	UWidget* Widget = FindWidgetByName(WidgetBlueprint, WidgetName);
	if (!Widget)
	{
		return FUnrealAICommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("Widget '%s' was not found in '%s'"), *WidgetName, *WidgetBlueprintPath));
	}

	if (!Widget->bIsVariable)
	{
		return FUnrealAICommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("Widget '%s' must be marked as a variable before it can participate in Widget Blueprint bindings"), *WidgetName));
	}

	const FName PropertyName(*PropertyNameString);
	if (!Widget->GetClass()->FindPropertyByName(PropertyName))
	{
		return FUnrealAICommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("Widget property '%s' was not found on widget class '%s'"), *PropertyNameString, *Widget->GetClass()->GetName()));
	}

	const FDelegateProperty* PropertyBindingDelegate = FindWidgetPropertyBindingDelegate(Widget, PropertyName);
	bool bUsesPropertyBindingDelegate = false;
	UFunction* BindingSignatureFunction = FindWidgetBindingSignatureFunction(Widget, PropertyName, bUsesPropertyBindingDelegate);
	if (!BindingSignatureFunction)
	{
		return FUnrealAICommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("Widget property '%s' does not expose a bindable delegate or event"), *PropertyNameString));
	}

	UClass* BindingClass = WidgetBlueprint->SkeletonGeneratedClass ? WidgetBlueprint->SkeletonGeneratedClass : WidgetBlueprint->GeneratedClass;
	if (!BindingClass)
	{
		return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Widget Blueprint does not have a generated class available for binding lookup"));
	}

	FDelegateEditorBinding Binding;
	Binding.ObjectName = Widget->GetName();
	Binding.PropertyName = PropertyName;

	const FString BoundWidgetName = Widget->GetName();
	FString BoundFunctionName;
	FString BoundSourcePropertyName;
	TArray<FString> BoundSourcePathSegments;

	if (bHasFunctionName)
	{
		const FName FunctionName(*FunctionNameString);
		UFunction* BindingFunction = BindingClass->FindFunctionByName(FunctionName);
		if (!BindingFunction)
		{
			return FUnrealAICommonUtils::CreateErrorResponse(
				FString::Printf(TEXT("Binding function '%s' was not found on Widget Blueprint '%s'"), *FunctionNameString, *WidgetBlueprint->GetName()));
		}

		if (!BindingFunction->IsSignatureCompatibleWith(
			BindingSignatureFunction,
			UFunction::GetDefaultIgnoredSignatureCompatibilityFlags() | CPF_ReturnParm))
		{
			return FUnrealAICommonUtils::CreateErrorResponse(
				FString::Printf(TEXT("Binding function '%s' is not compatible with widget property '%s'"), *FunctionNameString, *PropertyNameString));
		}

		Binding.FunctionName = BindingFunction->GetFName();
		Binding.Kind = EBindingKind::Function;
		UBlueprint::GetGuidFromClassByFieldName<UFunction>(BindingFunction->GetOwnerClass(), BindingFunction->GetFName(), Binding.MemberGuid);
		BoundFunctionName = Binding.FunctionName.ToString();
	}
	else
	{
		if (!bHasSourceProperty && bHasSourcePath)
		{
			SourcePropertyString = SourcePathSegments.Last();
		}

		if (!bHasSourcePath)
		{
			SourcePathSegments.Add(SourcePropertyString);
		}
		else if (bHasSourceProperty && !SourcePathSegments.Last().Equals(SourcePropertyString, ESearchCase::IgnoreCase))
		{
			return FUnrealAICommonUtils::CreateErrorResponse(
				TEXT("'source_property' must match the last segment of 'source_path' when both are supplied"));
		}

		if (!PropertyBindingDelegate || !bUsesPropertyBindingDelegate)
		{
			return FUnrealAICommonUtils::CreateErrorResponse(
				FString::Printf(TEXT("Widget property '%s' does not support source-property bindings"), *PropertyNameString));
		}

		FEditorPropertyPath SourcePath;
		FProperty* SourceProperty = nullptr;
		FString SourcePathResolveError;
		if (!ResolveBindingSourcePath(WidgetBlueprint, SourcePathSegments, SourcePath, SourceProperty, SourcePathResolveError))
		{
			return FUnrealAICommonUtils::CreateErrorResponse(SourcePathResolveError);
		}

		FText SourcePathValidationError;
		if (!SourcePath.Validate(const_cast<FDelegateProperty*>(PropertyBindingDelegate), SourcePathValidationError))
		{
			return FUnrealAICommonUtils::CreateErrorResponse(SourcePathValidationError.ToString());
		}

		Binding.SourceProperty = SourceProperty->GetFName();
		Binding.SourcePath = SourcePath;
		Binding.Kind = EBindingKind::Property;
		if (UClass* SourceOwnerClass = SourceProperty->GetOwnerClass())
		{
			UBlueprint::GetGuidFromClassByFieldName<FProperty>(SourceOwnerClass, Binding.SourceProperty, Binding.MemberGuid);
		}

		BoundSourcePropertyName = Binding.SourceProperty.ToString();
		for (const FEditorPropertyPathSegment& Segment : Binding.SourcePath.Segments)
		{
			const FName SegmentName = Segment.GetMemberName();
			if (!SegmentName.IsNone())
			{
				BoundSourcePathSegments.Add(SegmentName.ToString());
			}
		}
	}

	TSharedPtr<FJsonObject> BindingJson = MakeBindingJson(Binding);

	const int32 ExistingBindingIndex = WidgetBlueprint->Bindings.IndexOfByPredicate([&Binding](const FDelegateEditorBinding& ExistingBinding) {
		return ExistingBinding == Binding;
	});
	const bool bReplacedExistingBinding = ExistingBindingIndex != INDEX_NONE;
	TSharedPtr<FJsonObject> PreviousBindingJson;
	if (bReplacedExistingBinding)
	{
		PreviousBindingJson = MakeBindingJson(WidgetBlueprint->Bindings[ExistingBindingIndex]);
	}

	WidgetBlueprint->SetFlags(RF_Transactional);
	WidgetBlueprint->Modify();
	WidgetBlueprint->Bindings.Remove(Binding);
	WidgetBlueprint->Bindings.AddUnique(Binding);

	FinalizeWidgetBlueprintMutation(WidgetBlueprint);

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("widget_blueprint_path"), WidgetBlueprint->GetOutermost()->GetName());
	Data->SetStringField(TEXT("widget_name"), BoundWidgetName);
	Data->SetStringField(TEXT("property_name"), PropertyName.ToString());
	Data->SetStringField(TEXT("binding_kind"), Binding.Kind == EBindingKind::Function ? TEXT("function") : TEXT("property"));
	if (!BoundFunctionName.IsEmpty())
	{
		Data->SetStringField(TEXT("function_name"), BoundFunctionName);
	}
	if (!BoundSourcePropertyName.IsEmpty())
	{
		Data->SetStringField(TEXT("source_property"), BoundSourcePropertyName);
	}
	if (BoundSourcePathSegments.Num() > 0)
	{
		Data->SetStringField(TEXT("source_path"), FString::Join(BoundSourcePathSegments, TEXT(".")));
		TArray<TSharedPtr<FJsonValue>> SourcePathSegmentValues;
		for (const FString& BoundSourcePathSegment : BoundSourcePathSegments)
		{
			SourcePathSegmentValues.Add(MakeShared<FJsonValueString>(BoundSourcePathSegment));
		}
		Data->SetArrayField(TEXT("source_path_segments"), SourcePathSegmentValues);
	}
	Data->SetBoolField(TEXT("replaced_existing_binding"), bReplacedExistingBinding);
	Data->SetObjectField(TEXT("binding"), BindingJson);
	Data->SetNumberField(TEXT("binding_count"), WidgetBlueprint->Bindings.Num());
	if (PreviousBindingJson.IsValid())
	{
		Data->SetObjectField(TEXT("previous_binding"), PreviousBindingJson);
	}

	return FUnrealAICommonUtils::CreateSuccessResponse(Data);
}

TSharedPtr<FJsonObject> FUnrealAIWidgetCommands::HandleCreateWidgetAnimationInWidgetBlueprint(const TSharedPtr<FJsonObject>& Params)
{
	FString WidgetBlueprintPath;
	if (!Params->TryGetStringField(TEXT("widget_blueprint_path"), WidgetBlueprintPath) || WidgetBlueprintPath.IsEmpty())
	{
		return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing 'widget_blueprint_path' parameter"));
	}

	FString AnimationName;
	Params->TryGetStringField(TEXT("animation_name"), AnimationName);

	UWidgetBlueprint* WidgetBlueprint = LoadWidgetBlueprint(WidgetBlueprintPath);
	if (!WidgetBlueprint)
	{
		return FUnrealAICommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("Failed to load Widget Blueprint '%s'"), *WidgetBlueprintPath));
	}

	const FString UniqueAnimationName = MakeUniqueAnimationName(WidgetBlueprint, AnimationName);
	const FName UniqueAnimationFName(*UniqueAnimationName);

	UWidgetAnimation* NewAnimation = NewObject<UWidgetAnimation>(WidgetBlueprint, FName(), RF_Transactional);
	if (!NewAnimation)
	{
		return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Failed to create Widget Animation object"));
	}

	NewAnimation->SetFlags(RF_Transactional);
	NewAnimation->Modify();
	NewAnimation->SetDisplayLabel(UniqueAnimationName);
	NewAnimation->Rename(*UniqueAnimationName);

	NewAnimation->MovieScene = NewObject<UMovieScene>(NewAnimation, UniqueAnimationFName, RF_Transactional);
	if (!NewAnimation->MovieScene)
	{
		return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Failed to create MovieScene for Widget Animation"));
	}

	const float InTime = 0.0f;
	const float OutTime = 5.0f;
	NewAnimation->MovieScene->SetDisplayRate(FFrameRate(20, 1));
	const FFrameTime InFrame = InTime * NewAnimation->MovieScene->GetTickResolution();
	const FFrameTime OutFrame = OutTime * NewAnimation->MovieScene->GetTickResolution();
	NewAnimation->MovieScene->SetPlaybackRange(TRange<FFrameNumber>(InFrame.FrameNumber, OutFrame.FrameNumber + 1));
	NewAnimation->MovieScene->GetEditorData().WorkStart = InTime;
	NewAnimation->MovieScene->GetEditorData().WorkEnd = OutTime;

	WidgetBlueprint->SetFlags(RF_Transactional);
	WidgetBlueprint->Modify();
	WidgetBlueprint->Animations.Add(NewAnimation);
	WidgetBlueprint->OnVariableAdded(NewAnimation->GetFName());

	TSharedPtr<FJsonObject> AnimationJson = MakeAnimationJson(NewAnimation);
	const bool bRenamedRequestedAnimation = !AnimationName.IsEmpty() && !AnimationName.Equals(UniqueAnimationName, ESearchCase::CaseSensitive);

	FinalizeWidgetBlueprintMutation(WidgetBlueprint);

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("widget_blueprint_path"), WidgetBlueprint->GetOutermost()->GetName());
	Data->SetStringField(TEXT("animation_name"), UniqueAnimationName);
	Data->SetObjectField(TEXT("animation"), AnimationJson);
	Data->SetNumberField(TEXT("animation_count"), WidgetBlueprint->Animations.Num());
	Data->SetBoolField(TEXT("renamed_requested_animation"), bRenamedRequestedAnimation);
	if (!AnimationName.IsEmpty())
	{
		Data->SetStringField(TEXT("requested_animation_name"), AnimationName);
	}

	return FUnrealAICommonUtils::CreateSuccessResponse(Data);
}

TSharedPtr<FJsonObject> FUnrealAIWidgetCommands::HandleRemoveWidgetAnimationFromWidgetBlueprint(const TSharedPtr<FJsonObject>& Params)
{
	FString WidgetBlueprintPath;
	if (!Params->TryGetStringField(TEXT("widget_blueprint_path"), WidgetBlueprintPath) || WidgetBlueprintPath.IsEmpty())
	{
		return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing 'widget_blueprint_path' parameter"));
	}

	FString AnimationName;
	if (!Params->TryGetStringField(TEXT("animation_name"), AnimationName) || AnimationName.IsEmpty())
	{
		return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing 'animation_name' parameter"));
	}

	UWidgetBlueprint* WidgetBlueprint = LoadWidgetBlueprint(WidgetBlueprintPath);
	if (!WidgetBlueprint)
	{
		return FUnrealAICommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("Failed to load Widget Blueprint '%s'"), *WidgetBlueprintPath));
	}

	UWidgetAnimation* WidgetAnimation = FindWidgetAnimationByName(WidgetBlueprint, AnimationName);
	if (!WidgetAnimation)
	{
		return FUnrealAICommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("Widget animation '%s' was not found in '%s'"), *AnimationName, *WidgetBlueprintPath));
	}

	const FName RemovedAnimationName = WidgetAnimation->GetFName();
	TSharedPtr<FJsonObject> RemovedAnimationJson = MakeAnimationJson(WidgetAnimation);

	WidgetBlueprint->SetFlags(RF_Transactional);
	WidgetBlueprint->Modify();
	WidgetAnimation->Rename(nullptr, GetTransientPackage());
	WidgetBlueprint->Animations.Remove(WidgetAnimation);
	WidgetBlueprint->OnVariableRemoved(RemovedAnimationName);

	FinalizeWidgetBlueprintMutation(WidgetBlueprint);

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("widget_blueprint_path"), WidgetBlueprint->GetOutermost()->GetName());
	Data->SetStringField(TEXT("animation_name"), RemovedAnimationName.ToString());
	Data->SetObjectField(TEXT("removed_animation"), RemovedAnimationJson);
	Data->SetNumberField(TEXT("animation_count"), WidgetBlueprint->Animations.Num());

	return FUnrealAICommonUtils::CreateSuccessResponse(Data);
}

TSharedPtr<FJsonObject> FUnrealAIWidgetCommands::HandleRemoveWidgetPropertyBindingFromWidgetBlueprint(const TSharedPtr<FJsonObject>& Params)
{
	FString WidgetBlueprintPath;
	if (!Params->TryGetStringField(TEXT("widget_blueprint_path"), WidgetBlueprintPath) || WidgetBlueprintPath.IsEmpty())
	{
		return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing 'widget_blueprint_path' parameter"));
	}

	FString WidgetName;
	if (!Params->TryGetStringField(TEXT("widget_name"), WidgetName) || WidgetName.IsEmpty())
	{
		return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing 'widget_name' parameter"));
	}

	FString PropertyNameString;
	if (!Params->TryGetStringField(TEXT("property_name"), PropertyNameString) || PropertyNameString.IsEmpty())
	{
		return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing 'property_name' parameter"));
	}

	UWidgetBlueprint* WidgetBlueprint = LoadWidgetBlueprint(WidgetBlueprintPath);
	if (!WidgetBlueprint)
	{
		return FUnrealAICommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("Failed to load Widget Blueprint '%s'"), *WidgetBlueprintPath));
	}

	UWidget* Widget = FindWidgetByName(WidgetBlueprint, WidgetName);
	if (!Widget)
	{
		return FUnrealAICommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("Widget '%s' was not found in '%s'"), *WidgetName, *WidgetBlueprintPath));
	}

	const FName PropertyName(*PropertyNameString);
	const int32 BindingIndex = WidgetBlueprint->Bindings.IndexOfByPredicate([&Widget, &PropertyName](const FDelegateEditorBinding& ExistingBinding) {
		return ExistingBinding.ObjectName == Widget->GetName() && ExistingBinding.PropertyName == PropertyName;
	});
	if (BindingIndex == INDEX_NONE)
	{
		return FUnrealAICommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("No binding was found for widget '%s' property '%s'"), *WidgetName, *PropertyNameString));
	}

	const FDelegateEditorBinding RemovedBinding = WidgetBlueprint->Bindings[BindingIndex];
	const FString RemovedWidgetName = Widget->GetName();
	TSharedPtr<FJsonObject> RemovedBindingJson = MakeBindingJson(RemovedBinding);

	WidgetBlueprint->SetFlags(RF_Transactional);
	WidgetBlueprint->Modify();
	WidgetBlueprint->Bindings.RemoveAt(BindingIndex);

	FinalizeWidgetBlueprintMutation(WidgetBlueprint);

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("widget_blueprint_path"), WidgetBlueprint->GetOutermost()->GetName());
	Data->SetStringField(TEXT("widget_name"), RemovedWidgetName);
	Data->SetStringField(TEXT("property_name"), PropertyName.ToString());
	Data->SetObjectField(TEXT("removed_binding"), RemovedBindingJson);
	Data->SetNumberField(TEXT("binding_count"), WidgetBlueprint->Bindings.Num());

	return FUnrealAICommonUtils::CreateSuccessResponse(Data);
}

TSharedPtr<FJsonObject> FUnrealAIWidgetCommands::HandleReadWidgetBlueprintContent(const TSharedPtr<FJsonObject>& Params)
{
	FString WidgetBlueprintPath;
	if (!Params->TryGetStringField(TEXT("widget_blueprint_path"), WidgetBlueprintPath) || WidgetBlueprintPath.IsEmpty())
	{
		return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing 'widget_blueprint_path' parameter"));
	}

	UWidgetBlueprint* WidgetBlueprint = LoadWidgetBlueprint(WidgetBlueprintPath);
	if (!WidgetBlueprint)
	{
		return FUnrealAICommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("Failed to load Widget Blueprint '%s'"), *WidgetBlueprintPath));
	}

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("widget_blueprint_path"), WidgetBlueprint->GetOutermost()->GetName());
	Data->SetStringField(TEXT("asset_name"), WidgetBlueprint->GetName());
	if (WidgetBlueprint->ParentClass)
	{
		Data->SetStringField(TEXT("parent_class"), WidgetBlueprint->ParentClass->GetName());
	}

	FWidgetTreeStats TreeStats;
	if (WidgetBlueprint->WidgetTree && WidgetBlueprint->WidgetTree->RootWidget)
	{
		Data->SetObjectField(
			TEXT("root_widget"),
			BuildWidgetNode(WidgetBlueprint->WidgetTree->RootWidget, INDEX_NONE, FString(), FString(), TreeStats));
	}

	Data->SetNumberField(TEXT("widget_count"), TreeStats.WidgetCount);
	Data->SetNumberField(TEXT("named_slot_content_count"), TreeStats.NamedSlotContentCount);

	TArray<TSharedPtr<FJsonValue>> WidgetTreeNamedSlots;
	if (WidgetBlueprint->WidgetTree)
	{
		TArray<FName> SlotNames;
		WidgetBlueprint->WidgetTree->GetSlotNames(SlotNames);
		for (const FName& SlotName : SlotNames)
		{
			WidgetTreeNamedSlots.Add(MakeShared<FJsonValueString>(SlotName.ToString()));
		}
	}
	Data->SetArrayField(TEXT("widget_tree_named_slots"), WidgetTreeNamedSlots);

	TArray<TSharedPtr<FJsonValue>> Bindings;
	for (const FDelegateEditorBinding& Binding : WidgetBlueprint->Bindings)
	{
		Bindings.Add(MakeShared<FJsonValueObject>(MakeBindingJson(Binding)));
	}
	Data->SetArrayField(TEXT("bindings"), Bindings);
	Data->SetNumberField(TEXT("binding_count"), Bindings.Num());

	TArray<TSharedPtr<FJsonValue>> Animations;
	for (const UWidgetAnimation* Animation : WidgetBlueprint->Animations)
	{
		if (Animation)
		{
			Animations.Add(MakeShared<FJsonValueObject>(MakeAnimationJson(Animation)));
		}
	}
	Data->SetArrayField(TEXT("animations"), Animations);
	Data->SetNumberField(TEXT("animation_count"), Animations.Num());

	return FUnrealAICommonUtils::CreateSuccessResponse(Data);
}

TSharedPtr<FJsonObject> FUnrealAIWidgetCommands::HandleAddWidgetToWidgetBlueprint(const TSharedPtr<FJsonObject>& Params)
{
	FString WidgetBlueprintPath;
	if (!Params->TryGetStringField(TEXT("widget_blueprint_path"), WidgetBlueprintPath) || WidgetBlueprintPath.IsEmpty())
	{
		return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing 'widget_blueprint_path' parameter"));
	}

	FString WidgetClassName;
	if (!Params->TryGetStringField(TEXT("widget_class"), WidgetClassName) || WidgetClassName.IsEmpty())
	{
		return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing 'widget_class' parameter"));
	}

	UWidgetBlueprint* WidgetBlueprint = LoadWidgetBlueprint(WidgetBlueprintPath);
	if (!WidgetBlueprint)
	{
		return FUnrealAICommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("Failed to load Widget Blueprint '%s'"), *WidgetBlueprintPath));
	}

	UClass* WidgetClass = ResolveClassReference<UWidget>(WidgetClassName, {TEXT("UMG")});
	if (!WidgetClass)
	{
		return FUnrealAICommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("Unable to resolve widget class '%s'"), *WidgetClassName));
	}

	FString WidgetName;
	Params->TryGetStringField(TEXT("widget_name"), WidgetName);
	if (WidgetName.IsEmpty())
	{
		WidgetName = MakeUniqueWidgetName(WidgetBlueprint, GetDefaultWidgetName(WidgetClass));
	}
	else if (FindWidgetByName(WidgetBlueprint, WidgetName))
	{
		return FUnrealAICommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("A widget named '%s' already exists in '%s'"), *WidgetName, *WidgetBlueprintPath));
	}

	FString ParentWidgetName;
	Params->TryGetStringField(TEXT("parent_widget_name"), ParentWidgetName);

	FString NamedSlotName;
	Params->TryGetStringField(TEXT("named_slot_name"), NamedSlotName);

	bool bIsVariable = true;
	Params->TryGetBoolField(TEXT("is_variable"), bIsVariable);

	WidgetBlueprint->SetFlags(RF_Transactional);
	WidgetBlueprint->Modify();
	WidgetBlueprint->WidgetTree->SetFlags(RF_Transactional);
	WidgetBlueprint->WidgetTree->Modify();

	UWidget* NewWidget = WidgetBlueprint->WidgetTree->ConstructWidget<UWidget>(WidgetClass, FName(*WidgetName));
	if (!NewWidget)
	{
		return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Failed to construct widget instance"));
	}

	NewWidget->bIsVariable = bIsVariable;
	NewWidget->SetFlags(RF_Transactional);
	NewWidget->Modify();

	FString ResolvedParentWidgetName;
	FString ResolvedNamedSlotName;
	FString ResolvedNamedSlotHostName;
	FString AttachError;
	if (!AttachWidgetToHierarchy(
			WidgetBlueprint,
			NewWidget,
			ParentWidgetName,
			NamedSlotName,
			ResolvedParentWidgetName,
			ResolvedNamedSlotName,
			ResolvedNamedSlotHostName,
			AttachError))
	{
		return FUnrealAICommonUtils::CreateErrorResponse(AttachError);
	}

	if (bIsVariable)
	{
		WidgetBlueprint->OnVariableAdded(NewWidget->GetFName());
	}

	FinalizeWidgetBlueprintMutation(WidgetBlueprint);

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("widget_blueprint_path"), WidgetBlueprint->GetOutermost()->GetName());
	Data->SetStringField(TEXT("widget_name"), NewWidget->GetName());
	Data->SetStringField(TEXT("widget_class"), NewWidget->GetClass()->GetName());
	Data->SetBoolField(TEXT("is_variable"), NewWidget->bIsVariable);
	if (!ResolvedParentWidgetName.IsEmpty())
	{
		Data->SetStringField(TEXT("parent_widget_name"), ResolvedParentWidgetName);
	}
	if (!ResolvedNamedSlotName.IsEmpty())
	{
		Data->SetStringField(TEXT("named_slot_name"), ResolvedNamedSlotName);
		Data->SetStringField(TEXT("named_slot_host_name"), ResolvedNamedSlotHostName);
	}
	Data->SetBoolField(TEXT("is_root_widget"), WidgetBlueprint->WidgetTree->RootWidget == NewWidget);

	return FUnrealAICommonUtils::CreateSuccessResponse(Data);
}

TSharedPtr<FJsonObject> FUnrealAIWidgetCommands::HandleRemoveWidgetFromWidgetBlueprint(const TSharedPtr<FJsonObject>& Params)
{
	FString WidgetBlueprintPath;
	if (!Params->TryGetStringField(TEXT("widget_blueprint_path"), WidgetBlueprintPath) || WidgetBlueprintPath.IsEmpty())
	{
		return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing 'widget_blueprint_path' parameter"));
	}

	FString WidgetName;
	if (!Params->TryGetStringField(TEXT("widget_name"), WidgetName) || WidgetName.IsEmpty())
	{
		return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing 'widget_name' parameter"));
	}

	UWidgetBlueprint* WidgetBlueprint = LoadWidgetBlueprint(WidgetBlueprintPath);
	if (!WidgetBlueprint)
	{
		return FUnrealAICommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("Failed to load Widget Blueprint '%s'"), *WidgetBlueprintPath));
	}

	UWidget* WidgetToRemove = FindWidgetByName(WidgetBlueprint, WidgetName);
	if (!WidgetToRemove)
	{
		return FUnrealAICommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("Widget '%s' was not found in '%s'"), *WidgetName, *WidgetBlueprintPath));
	}

	WidgetBlueprint->SetFlags(RF_Transactional);
	WidgetBlueprint->Modify();
	WidgetBlueprint->WidgetTree->SetFlags(RF_Transactional);
	WidgetBlueprint->WidgetTree->Modify();

	TArray<UWidget*> RemovedWidgets;
	CollectWidgetSubtree(WidgetToRemove, RemovedWidgets);
	TArray<FString> RemovedWidgetOriginalNames;
	for (UWidget* RemovedWidget : RemovedWidgets)
	{
		if (RemovedWidget)
		{
			RemovedWidgetOriginalNames.Add(RemovedWidget->GetName());
		}
	}

	FString ParentWidgetName;
	FString NamedSlotName;
	FString NamedSlotHostName;
	FString DetachError;
	if (!DetachWidgetFromHierarchy(
			WidgetBlueprint,
			WidgetToRemove,
			true,
			ParentWidgetName,
			NamedSlotName,
			NamedSlotHostName,
			DetachError))
	{
		return FUnrealAICommonUtils::CreateErrorResponse(DetachError);
	}

	for (UWidget* RemovedWidget : RemovedWidgets)
	{
		if (RemovedWidget && RemovedWidget->bIsVariable)
		{
			WidgetBlueprint->OnVariableRemoved(RemovedWidget->GetFName());
		}
	}

	FinalizeWidgetBlueprintMutation(WidgetBlueprint);

	TArray<TSharedPtr<FJsonValue>> RemovedWidgetNames;
	for (const FString& RemovedWidgetName : RemovedWidgetOriginalNames)
	{
		RemovedWidgetNames.Add(MakeShared<FJsonValueString>(RemovedWidgetName));
	}

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("widget_blueprint_path"), WidgetBlueprint->GetOutermost()->GetName());
	Data->SetStringField(TEXT("removed_widget_name"), WidgetName);
	Data->SetArrayField(TEXT("removed_widget_names"), RemovedWidgetNames);
	Data->SetNumberField(TEXT("removed_widget_count"), RemovedWidgetNames.Num());
	if (!ParentWidgetName.IsEmpty())
	{
		Data->SetStringField(TEXT("previous_parent_widget_name"), ParentWidgetName);
	}
	if (!NamedSlotName.IsEmpty())
	{
		Data->SetStringField(TEXT("previous_named_slot_name"), NamedSlotName);
		Data->SetStringField(TEXT("previous_named_slot_host_name"), NamedSlotHostName);
	}
	Data->SetBoolField(TEXT("removed_root_widget"), WidgetBlueprint->WidgetTree->RootWidget == nullptr && WidgetToRemove->GetName() == WidgetName);

	return FUnrealAICommonUtils::CreateSuccessResponse(Data);
}

TSharedPtr<FJsonObject> FUnrealAIWidgetCommands::HandleReparentWidgetInWidgetBlueprint(const TSharedPtr<FJsonObject>& Params)
{
	FString WidgetBlueprintPath;
	if (!Params->TryGetStringField(TEXT("widget_blueprint_path"), WidgetBlueprintPath) || WidgetBlueprintPath.IsEmpty())
	{
		return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing 'widget_blueprint_path' parameter"));
	}

	FString WidgetName;
	if (!Params->TryGetStringField(TEXT("widget_name"), WidgetName) || WidgetName.IsEmpty())
	{
		return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing 'widget_name' parameter"));
	}

	UWidgetBlueprint* WidgetBlueprint = LoadWidgetBlueprint(WidgetBlueprintPath);
	if (!WidgetBlueprint)
	{
		return FUnrealAICommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("Failed to load Widget Blueprint '%s'"), *WidgetBlueprintPath));
	}

	UWidget* WidgetToMove = FindWidgetByName(WidgetBlueprint, WidgetName);
	if (!WidgetToMove)
	{
		return FUnrealAICommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("Widget '%s' was not found in '%s'"), *WidgetName, *WidgetBlueprintPath));
	}

	if (WidgetToMove == WidgetBlueprint->WidgetTree->RootWidget)
	{
		return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Reparenting the root widget is not supported in this wave"));
	}

	FString NewParentWidgetName;
	Params->TryGetStringField(TEXT("new_parent_widget_name"), NewParentWidgetName);

	FString NewNamedSlotName;
	Params->TryGetStringField(TEXT("new_named_slot_name"), NewNamedSlotName);

	if (!NewParentWidgetName.IsEmpty())
	{
		UWidget* NewParentWidget = FindWidgetByName(WidgetBlueprint, NewParentWidgetName);
		if (!NewParentWidget)
		{
			return FUnrealAICommonUtils::CreateErrorResponse(
				FString::Printf(TEXT("New parent widget '%s' was not found"), *NewParentWidgetName));
		}

		if (NewParentWidget == WidgetToMove || IsWidgetInSubtree(WidgetToMove, NewParentWidget))
		{
			return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Cannot reparent a widget into itself or one of its descendants"));
		}
	}

	WidgetBlueprint->SetFlags(RF_Transactional);
	WidgetBlueprint->Modify();
	WidgetBlueprint->WidgetTree->SetFlags(RF_Transactional);
	WidgetBlueprint->WidgetTree->Modify();

	FString PreviousParentWidgetName;
	FString PreviousNamedSlotName;
	FString PreviousNamedSlotHostName;
	FString DetachError;
	if (!DetachWidgetFromHierarchy(
			WidgetBlueprint,
			WidgetToMove,
			false,
			PreviousParentWidgetName,
			PreviousNamedSlotName,
			PreviousNamedSlotHostName,
			DetachError))
	{
		return FUnrealAICommonUtils::CreateErrorResponse(DetachError);
	}

	FString ResolvedParentWidgetName;
	FString ResolvedNamedSlotName;
	FString ResolvedNamedSlotHostName;
	FString AttachError;
	if (!AttachWidgetToHierarchy(
			WidgetBlueprint,
			WidgetToMove,
			NewParentWidgetName,
			NewNamedSlotName,
			ResolvedParentWidgetName,
			ResolvedNamedSlotName,
			ResolvedNamedSlotHostName,
			AttachError))
	{
		FString RollbackParentWidgetName;
		FString RollbackNamedSlotName;
		FString RollbackNamedSlotHostName;
		FString RollbackError;
		AttachWidgetToHierarchy(
			WidgetBlueprint,
			WidgetToMove,
			PreviousParentWidgetName,
			PreviousNamedSlotName,
			RollbackParentWidgetName,
			RollbackNamedSlotName,
			RollbackNamedSlotHostName,
			RollbackError);
		return FUnrealAICommonUtils::CreateErrorResponse(AttachError);
	}

	FinalizeWidgetBlueprintMutation(WidgetBlueprint);

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("widget_blueprint_path"), WidgetBlueprint->GetOutermost()->GetName());
	Data->SetStringField(TEXT("widget_name"), WidgetToMove->GetName());
	if (!PreviousParentWidgetName.IsEmpty())
	{
		Data->SetStringField(TEXT("previous_parent_widget_name"), PreviousParentWidgetName);
	}
	if (!PreviousNamedSlotName.IsEmpty())
	{
		Data->SetStringField(TEXT("previous_named_slot_name"), PreviousNamedSlotName);
		Data->SetStringField(TEXT("previous_named_slot_host_name"), PreviousNamedSlotHostName);
	}
	if (!ResolvedParentWidgetName.IsEmpty())
	{
		Data->SetStringField(TEXT("new_parent_widget_name"), ResolvedParentWidgetName);
	}
	if (!ResolvedNamedSlotName.IsEmpty())
	{
		Data->SetStringField(TEXT("new_named_slot_name"), ResolvedNamedSlotName);
		Data->SetStringField(TEXT("new_named_slot_host_name"), ResolvedNamedSlotHostName);
	}

	return FUnrealAICommonUtils::CreateSuccessResponse(Data);
}

TSharedPtr<FJsonObject> FUnrealAIWidgetCommands::HandleSetWidgetSlotLayoutInWidgetBlueprint(const TSharedPtr<FJsonObject>& Params)
{
	FString WidgetBlueprintPath;
	if (!Params->TryGetStringField(TEXT("widget_blueprint_path"), WidgetBlueprintPath) || WidgetBlueprintPath.IsEmpty())
	{
		return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing 'widget_blueprint_path' parameter"));
	}

	FString WidgetName;
	if (!Params->TryGetStringField(TEXT("widget_name"), WidgetName) || WidgetName.IsEmpty())
	{
		return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Missing 'widget_name' parameter"));
	}

	const TArray<FString> ProvidedFields = CollectWidgetLayoutFieldNames(Params);
	if (ProvidedFields.Num() == 0)
	{
		return FUnrealAICommonUtils::CreateErrorResponse(TEXT("No layout fields were provided"));
	}

	UWidgetBlueprint* WidgetBlueprint = LoadWidgetBlueprint(WidgetBlueprintPath);
	if (!WidgetBlueprint)
	{
		return FUnrealAICommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("Failed to load Widget Blueprint '%s'"), *WidgetBlueprintPath));
	}

	UWidget* Widget = FindWidgetByName(WidgetBlueprint, WidgetName);
	if (!Widget)
	{
		return FUnrealAICommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("Widget '%s' was not found in '%s'"), *WidgetName, *WidgetBlueprintPath));
	}

	if (!Widget->Slot)
	{
		return FUnrealAICommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("Widget '%s' does not currently have a panel slot to mutate"), *WidgetName));
	}

	TArray<FString> AllowedFields;
	FString SlotTypeLabel;
	UCanvasPanelSlot* CanvasSlot = Cast<UCanvasPanelSlot>(Widget->Slot);
	UGridSlot* GridSlot = Cast<UGridSlot>(Widget->Slot);
	UUniformGridSlot* UniformGridSlot = Cast<UUniformGridSlot>(Widget->Slot);
	UHorizontalBoxSlot* HorizontalBoxSlot = Cast<UHorizontalBoxSlot>(Widget->Slot);
	UVerticalBoxSlot* VerticalBoxSlot = Cast<UVerticalBoxSlot>(Widget->Slot);
	UScrollBoxSlot* ScrollBoxSlot = Cast<UScrollBoxSlot>(Widget->Slot);
	UStackBoxSlot* StackBoxSlot = Cast<UStackBoxSlot>(Widget->Slot);
	UWrapBoxSlot* WrapBoxSlot = Cast<UWrapBoxSlot>(Widget->Slot);
	USafeZoneSlot* SafeZoneSlot = Cast<USafeZoneSlot>(Widget->Slot);
	UScaleBoxSlot* ScaleBoxSlot = Cast<UScaleBoxSlot>(Widget->Slot);
	UBackgroundBlurSlot* BackgroundBlurSlot = Cast<UBackgroundBlurSlot>(Widget->Slot);
	UBorderSlot* BorderSlot = Cast<UBorderSlot>(Widget->Slot);
	UButtonSlot* ButtonSlot = Cast<UButtonSlot>(Widget->Slot);
	UOverlaySlot* OverlaySlot = Cast<UOverlaySlot>(Widget->Slot);
	USizeBoxSlot* SizeBoxSlot = Cast<USizeBoxSlot>(Widget->Slot);
	UWidgetSwitcherSlot* WidgetSwitcherSlot = Cast<UWidgetSwitcherSlot>(Widget->Slot);
	UWindowTitleBarAreaSlot* WindowTitleBarAreaSlot = Cast<UWindowTitleBarAreaSlot>(Widget->Slot);
	if (CanvasSlot)
	{
		SlotTypeLabel = TEXT("canvas");
		AllowedFields = {TEXT("alignment"), TEXT("anchors"), TEXT("offsets"), TEXT("size"), TEXT("z_order")};
	}
	else if (GridSlot)
	{
		SlotTypeLabel = TEXT("grid");
		AllowedFields = {
			TEXT("column"),
			TEXT("column_span"),
			TEXT("horizontal_alignment"),
			TEXT("layer"),
			TEXT("nudge"),
			TEXT("padding"),
			TEXT("row"),
			TEXT("row_span"),
			TEXT("vertical_alignment")};
	}
	else if (UniformGridSlot)
	{
		SlotTypeLabel = TEXT("uniform_grid");
		AllowedFields = {TEXT("column"), TEXT("horizontal_alignment"), TEXT("row"), TEXT("vertical_alignment")};
	}
	else if (HorizontalBoxSlot)
	{
		SlotTypeLabel = TEXT("horizontal_box");
		AllowedFields = {TEXT("child_size"), TEXT("horizontal_alignment"), TEXT("padding"), TEXT("vertical_alignment")};
	}
	else if (VerticalBoxSlot)
	{
		SlotTypeLabel = TEXT("vertical_box");
		AllowedFields = {TEXT("child_size"), TEXT("horizontal_alignment"), TEXT("padding"), TEXT("vertical_alignment")};
	}
	else if (ScrollBoxSlot)
	{
		SlotTypeLabel = TEXT("scroll_box");
		AllowedFields = {TEXT("child_size"), TEXT("horizontal_alignment"), TEXT("padding"), TEXT("vertical_alignment")};
	}
	else if (StackBoxSlot)
	{
		SlotTypeLabel = TEXT("stack_box");
		AllowedFields = {TEXT("child_size"), TEXT("horizontal_alignment"), TEXT("padding"), TEXT("vertical_alignment")};
	}
	else if (WrapBoxSlot)
	{
		SlotTypeLabel = TEXT("wrap_box");
		AllowedFields = {
			TEXT("fill_empty_space"),
			TEXT("fill_span_when_less_than"),
			TEXT("force_new_line"),
			TEXT("horizontal_alignment"),
			TEXT("padding"),
			TEXT("vertical_alignment")};
	}
	else if (SafeZoneSlot)
	{
		SlotTypeLabel = TEXT("safe_zone");
		AllowedFields = {
			TEXT("horizontal_alignment"),
			TEXT("is_title_safe"),
			TEXT("padding"),
			TEXT("safe_area_scale"),
			TEXT("vertical_alignment")};
	}
	else if (ScaleBoxSlot)
	{
		SlotTypeLabel = TEXT("scale_box");
		AllowedFields = {TEXT("horizontal_alignment"), TEXT("vertical_alignment")};
	}
	else if (BackgroundBlurSlot)
	{
		SlotTypeLabel = TEXT("background_blur");
		AllowedFields = {TEXT("horizontal_alignment"), TEXT("padding"), TEXT("vertical_alignment")};
	}
	else if (BorderSlot)
	{
		SlotTypeLabel = TEXT("border");
		AllowedFields = {TEXT("horizontal_alignment"), TEXT("padding"), TEXT("vertical_alignment")};
	}
	else if (ButtonSlot)
	{
		SlotTypeLabel = TEXT("button");
		AllowedFields = {TEXT("horizontal_alignment"), TEXT("padding"), TEXT("vertical_alignment")};
	}
	else if (OverlaySlot)
	{
		SlotTypeLabel = TEXT("overlay");
		AllowedFields = {TEXT("horizontal_alignment"), TEXT("padding"), TEXT("vertical_alignment")};
	}
	else if (SizeBoxSlot)
	{
		SlotTypeLabel = TEXT("size_box");
		AllowedFields = {TEXT("horizontal_alignment"), TEXT("padding"), TEXT("vertical_alignment")};
	}
	else if (WidgetSwitcherSlot)
	{
		SlotTypeLabel = TEXT("widget_switcher");
		AllowedFields = {TEXT("horizontal_alignment"), TEXT("padding"), TEXT("vertical_alignment")};
	}
	else if (WindowTitleBarAreaSlot)
	{
		SlotTypeLabel = TEXT("window_title_bar_area");
		AllowedFields = {TEXT("horizontal_alignment"), TEXT("padding"), TEXT("vertical_alignment")};
	}
	else
	{
		return FUnrealAICommonUtils::CreateErrorResponse(
			FString::Printf(
				TEXT("Slot type '%s' is not supported by the current 9c-4 layout mutation slice"),
				*Widget->Slot->GetClass()->GetName()));
	}

	TArray<FString> UnsupportedFields;
	for (const FString& FieldName : ProvidedFields)
	{
		if (!AllowedFields.Contains(FieldName))
		{
			UnsupportedFields.Add(FieldName);
		}
	}

	if (UnsupportedFields.Num() > 0)
	{
		UnsupportedFields.Sort();
		return FUnrealAICommonUtils::CreateErrorResponse(
			FString::Printf(
				TEXT("Unsupported layout fields for slot type '%s': %s"),
				*SlotTypeLabel,
				*JoinFieldNames(UnsupportedFields)));
	}

	WidgetBlueprint->SetFlags(RF_Transactional);
	WidgetBlueprint->Modify();
	WidgetBlueprint->WidgetTree->SetFlags(RF_Transactional);
	WidgetBlueprint->WidgetTree->Modify();
	Widget->SetFlags(RF_Transactional);
	Widget->Modify();
	Widget->Slot->SetFlags(RF_Transactional);
		Widget->Slot->Modify();

	FString ParseError;
	if (CanvasSlot)
	{
		if (Params->HasField(TEXT("anchors")))
		{
			TSharedPtr<FJsonObject> AnchorsObject;
			FAnchors Anchors;
			if (!TryGetObjectParam(Params, TEXT("anchors"), AnchorsObject, ParseError) ||
				!ParseAnchorsJson(AnchorsObject, Anchors, ParseError))
			{
				return FUnrealAICommonUtils::CreateErrorResponse(ParseError);
			}
			CanvasSlot->SetAnchors(Anchors);
		}

		if (Params->HasField(TEXT("offsets")))
		{
			TSharedPtr<FJsonObject> OffsetsObject;
			FMargin Offsets;
			if (!TryGetObjectParam(Params, TEXT("offsets"), OffsetsObject, ParseError) ||
				!ParseMarginJson(OffsetsObject, Offsets, TEXT("offsets"), ParseError))
			{
				return FUnrealAICommonUtils::CreateErrorResponse(ParseError);
			}
			CanvasSlot->SetOffsets(Offsets);
		}

		if (Params->HasField(TEXT("size")))
		{
			TSharedPtr<FJsonObject> SizeObject;
			FVector2D Size = FVector2D::ZeroVector;
			if (!TryGetObjectParam(Params, TEXT("size"), SizeObject, ParseError) ||
				!ParseVector2Json(SizeObject, Size, TEXT("size"), ParseError))
			{
				return FUnrealAICommonUtils::CreateErrorResponse(ParseError);
			}
			CanvasSlot->SetSize(Size);
		}

		if (Params->HasField(TEXT("alignment")))
		{
			TSharedPtr<FJsonObject> AlignmentObject;
			FVector2D Alignment = FVector2D::ZeroVector;
			if (!TryGetObjectParam(Params, TEXT("alignment"), AlignmentObject, ParseError) ||
				!ParseVector2Json(AlignmentObject, Alignment, TEXT("alignment"), ParseError))
			{
				return FUnrealAICommonUtils::CreateErrorResponse(ParseError);
			}
			CanvasSlot->SetAlignment(Alignment);
		}

		if (Params->HasField(TEXT("z_order")))
		{
			double ZOrder = 0.0;
			if (!Params->TryGetNumberField(TEXT("z_order"), ZOrder))
			{
				return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Field 'z_order' must be numeric"));
			}
			CanvasSlot->SetZOrder(FMath::RoundToInt(ZOrder));
		}
	}
	else if (GridSlot)
	{
		if (Params->HasField(TEXT("padding")))
		{
			TSharedPtr<FJsonObject> PaddingObject;
			FMargin Padding;
			if (!TryGetObjectParam(Params, TEXT("padding"), PaddingObject, ParseError) ||
				!ParseMarginJson(PaddingObject, Padding, TEXT("padding"), ParseError))
			{
				return FUnrealAICommonUtils::CreateErrorResponse(ParseError);
			}
			GridSlot->SetPadding(Padding);
		}

		if (Params->HasField(TEXT("horizontal_alignment")))
		{
			FString HorizontalAlignmentValue;
			EHorizontalAlignment HorizontalAlignment = HAlign_Fill;
			if (!Params->TryGetStringField(TEXT("horizontal_alignment"), HorizontalAlignmentValue) ||
				!TryParseEnumValue(HorizontalAlignmentValue, StaticEnum<EHorizontalAlignment>(), HorizontalAlignment))
			{
				return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Field 'horizontal_alignment' must be a valid horizontal alignment string"));
			}
			GridSlot->SetHorizontalAlignment(HorizontalAlignment);
		}

		if (Params->HasField(TEXT("vertical_alignment")))
		{
			FString VerticalAlignmentValue;
			EVerticalAlignment VerticalAlignment = VAlign_Fill;
			if (!Params->TryGetStringField(TEXT("vertical_alignment"), VerticalAlignmentValue) ||
				!TryParseEnumValue(VerticalAlignmentValue, StaticEnum<EVerticalAlignment>(), VerticalAlignment))
			{
				return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Field 'vertical_alignment' must be a valid vertical alignment string"));
			}
			GridSlot->SetVerticalAlignment(VerticalAlignment);
		}

		if (Params->HasField(TEXT("row")))
		{
			double Row = 0.0;
			if (!Params->TryGetNumberField(TEXT("row"), Row))
			{
				return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Field 'row' must be numeric"));
			}
			GridSlot->SetRow(FMath::RoundToInt(Row));
		}

		if (Params->HasField(TEXT("row_span")))
		{
			double RowSpan = 0.0;
			if (!Params->TryGetNumberField(TEXT("row_span"), RowSpan))
			{
				return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Field 'row_span' must be numeric"));
			}
			GridSlot->SetRowSpan(FMath::Max(1, FMath::RoundToInt(RowSpan)));
		}

		if (Params->HasField(TEXT("column")))
		{
			double Column = 0.0;
			if (!Params->TryGetNumberField(TEXT("column"), Column))
			{
				return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Field 'column' must be numeric"));
			}
			GridSlot->SetColumn(FMath::RoundToInt(Column));
		}

		if (Params->HasField(TEXT("column_span")))
		{
			double ColumnSpan = 0.0;
			if (!Params->TryGetNumberField(TEXT("column_span"), ColumnSpan))
			{
				return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Field 'column_span' must be numeric"));
			}
			GridSlot->SetColumnSpan(FMath::Max(1, FMath::RoundToInt(ColumnSpan)));
		}

		if (Params->HasField(TEXT("layer")))
		{
			double Layer = 0.0;
			if (!Params->TryGetNumberField(TEXT("layer"), Layer))
			{
				return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Field 'layer' must be numeric"));
			}
			GridSlot->SetLayer(FMath::RoundToInt(Layer));
		}

		if (Params->HasField(TEXT("nudge")))
		{
			TSharedPtr<FJsonObject> NudgeObject;
			FVector2D Nudge = FVector2D::ZeroVector;
			if (!TryGetObjectParam(Params, TEXT("nudge"), NudgeObject, ParseError) ||
				!ParseVector2Json(NudgeObject, Nudge, TEXT("nudge"), ParseError))
			{
				return FUnrealAICommonUtils::CreateErrorResponse(ParseError);
			}
			GridSlot->SetNudge(Nudge);
		}
	}
	else if (UniformGridSlot)
	{
		if (Params->HasField(TEXT("horizontal_alignment")))
		{
			FString HorizontalAlignmentValue;
			EHorizontalAlignment HorizontalAlignment = HAlign_Fill;
			if (!Params->TryGetStringField(TEXT("horizontal_alignment"), HorizontalAlignmentValue) ||
				!TryParseEnumValue(HorizontalAlignmentValue, StaticEnum<EHorizontalAlignment>(), HorizontalAlignment))
			{
				return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Field 'horizontal_alignment' must be a valid horizontal alignment string"));
			}
			UniformGridSlot->SetHorizontalAlignment(HorizontalAlignment);
		}

		if (Params->HasField(TEXT("vertical_alignment")))
		{
			FString VerticalAlignmentValue;
			EVerticalAlignment VerticalAlignment = VAlign_Fill;
			if (!Params->TryGetStringField(TEXT("vertical_alignment"), VerticalAlignmentValue) ||
				!TryParseEnumValue(VerticalAlignmentValue, StaticEnum<EVerticalAlignment>(), VerticalAlignment))
			{
				return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Field 'vertical_alignment' must be a valid vertical alignment string"));
			}
			UniformGridSlot->SetVerticalAlignment(VerticalAlignment);
		}

		if (Params->HasField(TEXT("row")))
		{
			double Row = 0.0;
			if (!Params->TryGetNumberField(TEXT("row"), Row))
			{
				return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Field 'row' must be numeric"));
			}
			UniformGridSlot->SetRow(FMath::RoundToInt(Row));
		}

		if (Params->HasField(TEXT("column")))
		{
			double Column = 0.0;
			if (!Params->TryGetNumberField(TEXT("column"), Column))
			{
				return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Field 'column' must be numeric"));
			}
			UniformGridSlot->SetColumn(FMath::RoundToInt(Column));
		}
	}
	else if (HorizontalBoxSlot)
	{
		if (Params->HasField(TEXT("padding")))
		{
			TSharedPtr<FJsonObject> PaddingObject;
			FMargin Padding;
			if (!TryGetObjectParam(Params, TEXT("padding"), PaddingObject, ParseError) ||
				!ParseMarginJson(PaddingObject, Padding, TEXT("padding"), ParseError))
			{
				return FUnrealAICommonUtils::CreateErrorResponse(ParseError);
			}
			HorizontalBoxSlot->SetPadding(Padding);
		}

		if (Params->HasField(TEXT("horizontal_alignment")))
		{
			FString HorizontalAlignmentValue;
			EHorizontalAlignment HorizontalAlignment = HAlign_Fill;
			if (!Params->TryGetStringField(TEXT("horizontal_alignment"), HorizontalAlignmentValue) ||
				!TryParseEnumValue(HorizontalAlignmentValue, StaticEnum<EHorizontalAlignment>(), HorizontalAlignment))
			{
				return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Field 'horizontal_alignment' must be a valid horizontal alignment string"));
			}
			HorizontalBoxSlot->SetHorizontalAlignment(HorizontalAlignment);
		}

		if (Params->HasField(TEXT("vertical_alignment")))
		{
			FString VerticalAlignmentValue;
			EVerticalAlignment VerticalAlignment = VAlign_Fill;
			if (!Params->TryGetStringField(TEXT("vertical_alignment"), VerticalAlignmentValue) ||
				!TryParseEnumValue(VerticalAlignmentValue, StaticEnum<EVerticalAlignment>(), VerticalAlignment))
			{
				return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Field 'vertical_alignment' must be a valid vertical alignment string"));
			}
			HorizontalBoxSlot->SetVerticalAlignment(VerticalAlignment);
		}

		if (Params->HasField(TEXT("child_size")))
		{
			TSharedPtr<FJsonObject> ChildSizeObject;
			FSlateChildSize ChildSize;
			if (!TryGetObjectParam(Params, TEXT("child_size"), ChildSizeObject, ParseError) ||
				!ParseSlateChildSizeJson(ChildSizeObject, ChildSize, ParseError))
			{
				return FUnrealAICommonUtils::CreateErrorResponse(ParseError);
			}
			HorizontalBoxSlot->SetSize(ChildSize);
		}
	}
	else if (VerticalBoxSlot)
	{
		if (Params->HasField(TEXT("padding")))
		{
			TSharedPtr<FJsonObject> PaddingObject;
			FMargin Padding;
			if (!TryGetObjectParam(Params, TEXT("padding"), PaddingObject, ParseError) ||
				!ParseMarginJson(PaddingObject, Padding, TEXT("padding"), ParseError))
			{
				return FUnrealAICommonUtils::CreateErrorResponse(ParseError);
			}
			VerticalBoxSlot->SetPadding(Padding);
		}

		if (Params->HasField(TEXT("horizontal_alignment")))
		{
			FString HorizontalAlignmentValue;
			EHorizontalAlignment HorizontalAlignment = HAlign_Fill;
			if (!Params->TryGetStringField(TEXT("horizontal_alignment"), HorizontalAlignmentValue) ||
				!TryParseEnumValue(HorizontalAlignmentValue, StaticEnum<EHorizontalAlignment>(), HorizontalAlignment))
			{
				return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Field 'horizontal_alignment' must be a valid horizontal alignment string"));
			}
			VerticalBoxSlot->SetHorizontalAlignment(HorizontalAlignment);
		}

		if (Params->HasField(TEXT("vertical_alignment")))
		{
			FString VerticalAlignmentValue;
			EVerticalAlignment VerticalAlignment = VAlign_Fill;
			if (!Params->TryGetStringField(TEXT("vertical_alignment"), VerticalAlignmentValue) ||
				!TryParseEnumValue(VerticalAlignmentValue, StaticEnum<EVerticalAlignment>(), VerticalAlignment))
			{
				return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Field 'vertical_alignment' must be a valid vertical alignment string"));
			}
			VerticalBoxSlot->SetVerticalAlignment(VerticalAlignment);
		}

		if (Params->HasField(TEXT("child_size")))
		{
			TSharedPtr<FJsonObject> ChildSizeObject;
			FSlateChildSize ChildSize;
			if (!TryGetObjectParam(Params, TEXT("child_size"), ChildSizeObject, ParseError) ||
				!ParseSlateChildSizeJson(ChildSizeObject, ChildSize, ParseError))
			{
				return FUnrealAICommonUtils::CreateErrorResponse(ParseError);
			}
			VerticalBoxSlot->SetSize(ChildSize);
		}
	}
	else if (ScrollBoxSlot)
	{
		if (Params->HasField(TEXT("padding")))
		{
			TSharedPtr<FJsonObject> PaddingObject;
			FMargin Padding;
			if (!TryGetObjectParam(Params, TEXT("padding"), PaddingObject, ParseError) ||
				!ParseMarginJson(PaddingObject, Padding, TEXT("padding"), ParseError))
			{
				return FUnrealAICommonUtils::CreateErrorResponse(ParseError);
			}
			ScrollBoxSlot->SetPadding(Padding);
		}

		if (Params->HasField(TEXT("horizontal_alignment")))
		{
			FString HorizontalAlignmentValue;
			EHorizontalAlignment HorizontalAlignment = HAlign_Fill;
			if (!Params->TryGetStringField(TEXT("horizontal_alignment"), HorizontalAlignmentValue) ||
				!TryParseEnumValue(HorizontalAlignmentValue, StaticEnum<EHorizontalAlignment>(), HorizontalAlignment))
			{
				return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Field 'horizontal_alignment' must be a valid horizontal alignment string"));
			}
			ScrollBoxSlot->SetHorizontalAlignment(HorizontalAlignment);
		}

		if (Params->HasField(TEXT("vertical_alignment")))
		{
			FString VerticalAlignmentValue;
			EVerticalAlignment VerticalAlignment = VAlign_Fill;
			if (!Params->TryGetStringField(TEXT("vertical_alignment"), VerticalAlignmentValue) ||
				!TryParseEnumValue(VerticalAlignmentValue, StaticEnum<EVerticalAlignment>(), VerticalAlignment))
			{
				return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Field 'vertical_alignment' must be a valid vertical alignment string"));
			}
			ScrollBoxSlot->SetVerticalAlignment(VerticalAlignment);
		}

		if (Params->HasField(TEXT("child_size")))
		{
			TSharedPtr<FJsonObject> ChildSizeObject;
			FSlateChildSize ChildSize;
			if (!TryGetObjectParam(Params, TEXT("child_size"), ChildSizeObject, ParseError) ||
				!ParseSlateChildSizeJson(ChildSizeObject, ChildSize, ParseError))
			{
				return FUnrealAICommonUtils::CreateErrorResponse(ParseError);
			}
			ScrollBoxSlot->SetSize(ChildSize);
		}
	}
	else if (StackBoxSlot)
	{
		if (Params->HasField(TEXT("padding")))
		{
			TSharedPtr<FJsonObject> PaddingObject;
			FMargin Padding;
			if (!TryGetObjectParam(Params, TEXT("padding"), PaddingObject, ParseError) ||
				!ParseMarginJson(PaddingObject, Padding, TEXT("padding"), ParseError))
			{
				return FUnrealAICommonUtils::CreateErrorResponse(ParseError);
			}
			StackBoxSlot->SetPadding(Padding);
		}

		if (Params->HasField(TEXT("horizontal_alignment")))
		{
			FString HorizontalAlignmentValue;
			EHorizontalAlignment HorizontalAlignment = HAlign_Fill;
			if (!Params->TryGetStringField(TEXT("horizontal_alignment"), HorizontalAlignmentValue) ||
				!TryParseEnumValue(HorizontalAlignmentValue, StaticEnum<EHorizontalAlignment>(), HorizontalAlignment))
			{
				return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Field 'horizontal_alignment' must be a valid horizontal alignment string"));
			}
			StackBoxSlot->SetHorizontalAlignment(HorizontalAlignment);
		}

		if (Params->HasField(TEXT("vertical_alignment")))
		{
			FString VerticalAlignmentValue;
			EVerticalAlignment VerticalAlignment = VAlign_Fill;
			if (!Params->TryGetStringField(TEXT("vertical_alignment"), VerticalAlignmentValue) ||
				!TryParseEnumValue(VerticalAlignmentValue, StaticEnum<EVerticalAlignment>(), VerticalAlignment))
			{
				return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Field 'vertical_alignment' must be a valid vertical alignment string"));
			}
			StackBoxSlot->SetVerticalAlignment(VerticalAlignment);
		}

		if (Params->HasField(TEXT("child_size")))
		{
			TSharedPtr<FJsonObject> ChildSizeObject;
			FSlateChildSize ChildSize;
			if (!TryGetObjectParam(Params, TEXT("child_size"), ChildSizeObject, ParseError) ||
				!ParseSlateChildSizeJson(ChildSizeObject, ChildSize, ParseError))
			{
				return FUnrealAICommonUtils::CreateErrorResponse(ParseError);
			}
			StackBoxSlot->SetSize(ChildSize);
		}
	}
	else if (WrapBoxSlot)
	{
		if (Params->HasField(TEXT("padding")))
		{
			TSharedPtr<FJsonObject> PaddingObject;
			FMargin Padding;
			if (!TryGetObjectParam(Params, TEXT("padding"), PaddingObject, ParseError) ||
				!ParseMarginJson(PaddingObject, Padding, TEXT("padding"), ParseError))
			{
				return FUnrealAICommonUtils::CreateErrorResponse(ParseError);
			}
			WrapBoxSlot->SetPadding(Padding);
		}

		if (Params->HasField(TEXT("horizontal_alignment")))
		{
			FString HorizontalAlignmentValue;
			EHorizontalAlignment HorizontalAlignment = HAlign_Fill;
			if (!Params->TryGetStringField(TEXT("horizontal_alignment"), HorizontalAlignmentValue) ||
				!TryParseEnumValue(HorizontalAlignmentValue, StaticEnum<EHorizontalAlignment>(), HorizontalAlignment))
			{
				return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Field 'horizontal_alignment' must be a valid horizontal alignment string"));
			}
			WrapBoxSlot->SetHorizontalAlignment(HorizontalAlignment);
		}

		if (Params->HasField(TEXT("vertical_alignment")))
		{
			FString VerticalAlignmentValue;
			EVerticalAlignment VerticalAlignment = VAlign_Fill;
			if (!Params->TryGetStringField(TEXT("vertical_alignment"), VerticalAlignmentValue) ||
				!TryParseEnumValue(VerticalAlignmentValue, StaticEnum<EVerticalAlignment>(), VerticalAlignment))
			{
				return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Field 'vertical_alignment' must be a valid vertical alignment string"));
			}
			WrapBoxSlot->SetVerticalAlignment(VerticalAlignment);
		}

		if (Params->HasField(TEXT("fill_empty_space")))
		{
			bool bFillEmptySpace = false;
			if (!Params->TryGetBoolField(TEXT("fill_empty_space"), bFillEmptySpace))
			{
				return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Field 'fill_empty_space' must be boolean"));
			}
			WrapBoxSlot->SetFillEmptySpace(bFillEmptySpace);
		}

		if (Params->HasField(TEXT("force_new_line")))
		{
			bool bForceNewLine = false;
			if (!Params->TryGetBoolField(TEXT("force_new_line"), bForceNewLine))
			{
				return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Field 'force_new_line' must be boolean"));
			}
			WrapBoxSlot->SetNewLine(bForceNewLine);
		}

		if (Params->HasField(TEXT("fill_span_when_less_than")))
		{
			double FillSpanWhenLessThan = 0.0;
			if (!Params->TryGetNumberField(TEXT("fill_span_when_less_than"), FillSpanWhenLessThan))
			{
				return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Field 'fill_span_when_less_than' must be numeric"));
			}
			WrapBoxSlot->SetFillSpanWhenLessThan(static_cast<float>(FillSpanWhenLessThan));
		}
	}
	else if (SafeZoneSlot)
	{
		if (Params->HasField(TEXT("padding")))
		{
			TSharedPtr<FJsonObject> PaddingObject;
			FMargin Padding;
			if (!TryGetObjectParam(Params, TEXT("padding"), PaddingObject, ParseError) ||
				!ParseMarginJson(PaddingObject, Padding, TEXT("padding"), ParseError))
			{
				return FUnrealAICommonUtils::CreateErrorResponse(ParseError);
			}
			SafeZoneSlot->SetPadding(Padding);
		}

		if (Params->HasField(TEXT("horizontal_alignment")))
		{
			FString HorizontalAlignmentValue;
			EHorizontalAlignment HorizontalAlignment = HAlign_Fill;
			if (!Params->TryGetStringField(TEXT("horizontal_alignment"), HorizontalAlignmentValue) ||
				!TryParseEnumValue(HorizontalAlignmentValue, StaticEnum<EHorizontalAlignment>(), HorizontalAlignment))
			{
				return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Field 'horizontal_alignment' must be a valid horizontal alignment string"));
			}
			SafeZoneSlot->SetHorizontalAlignment(HorizontalAlignment);
		}

		if (Params->HasField(TEXT("vertical_alignment")))
		{
			FString VerticalAlignmentValue;
			EVerticalAlignment VerticalAlignment = VAlign_Fill;
			if (!Params->TryGetStringField(TEXT("vertical_alignment"), VerticalAlignmentValue) ||
				!TryParseEnumValue(VerticalAlignmentValue, StaticEnum<EVerticalAlignment>(), VerticalAlignment))
			{
				return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Field 'vertical_alignment' must be a valid vertical alignment string"));
			}
			SafeZoneSlot->SetVerticalAlignment(VerticalAlignment);
		}

		if (Params->HasField(TEXT("safe_area_scale")))
		{
			TSharedPtr<FJsonObject> SafeAreaScaleObject;
			FMargin SafeAreaScale;
			if (!TryGetObjectParam(Params, TEXT("safe_area_scale"), SafeAreaScaleObject, ParseError) ||
				!ParseMarginJson(SafeAreaScaleObject, SafeAreaScale, TEXT("safe_area_scale"), ParseError))
			{
				return FUnrealAICommonUtils::CreateErrorResponse(ParseError);
			}
			SafeZoneSlot->SetSafeAreaScale(SafeAreaScale);
		}

		if (Params->HasField(TEXT("is_title_safe")))
		{
			bool bIsTitleSafe = false;
			if (!Params->TryGetBoolField(TEXT("is_title_safe"), bIsTitleSafe))
			{
				return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Field 'is_title_safe' must be boolean"));
			}
			SafeZoneSlot->SetIsTitleSafe(bIsTitleSafe);
		}
	}
	else if (ScaleBoxSlot)
	{
		if (Params->HasField(TEXT("horizontal_alignment")))
		{
			FString HorizontalAlignmentValue;
			EHorizontalAlignment HorizontalAlignment = HAlign_Fill;
			if (!Params->TryGetStringField(TEXT("horizontal_alignment"), HorizontalAlignmentValue) ||
				!TryParseEnumValue(HorizontalAlignmentValue, StaticEnum<EHorizontalAlignment>(), HorizontalAlignment))
			{
				return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Field 'horizontal_alignment' must be a valid horizontal alignment string"));
			}
			ScaleBoxSlot->SetHorizontalAlignment(HorizontalAlignment);
		}

		if (Params->HasField(TEXT("vertical_alignment")))
		{
			FString VerticalAlignmentValue;
			EVerticalAlignment VerticalAlignment = VAlign_Fill;
			if (!Params->TryGetStringField(TEXT("vertical_alignment"), VerticalAlignmentValue) ||
				!TryParseEnumValue(VerticalAlignmentValue, StaticEnum<EVerticalAlignment>(), VerticalAlignment))
			{
				return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Field 'vertical_alignment' must be a valid vertical alignment string"));
			}
			ScaleBoxSlot->SetVerticalAlignment(VerticalAlignment);
		}
	}
	else if (BackgroundBlurSlot)
	{
		if (Params->HasField(TEXT("padding")))
		{
			TSharedPtr<FJsonObject> PaddingObject;
			FMargin Padding;
			if (!TryGetObjectParam(Params, TEXT("padding"), PaddingObject, ParseError) ||
				!ParseMarginJson(PaddingObject, Padding, TEXT("padding"), ParseError))
			{
				return FUnrealAICommonUtils::CreateErrorResponse(ParseError);
			}
			BackgroundBlurSlot->SetPadding(Padding);
		}

		if (Params->HasField(TEXT("horizontal_alignment")))
		{
			FString HorizontalAlignmentValue;
			EHorizontalAlignment HorizontalAlignment = HAlign_Fill;
			if (!Params->TryGetStringField(TEXT("horizontal_alignment"), HorizontalAlignmentValue) ||
				!TryParseEnumValue(HorizontalAlignmentValue, StaticEnum<EHorizontalAlignment>(), HorizontalAlignment))
			{
				return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Field 'horizontal_alignment' must be a valid horizontal alignment string"));
			}
			BackgroundBlurSlot->SetHorizontalAlignment(HorizontalAlignment);
		}

		if (Params->HasField(TEXT("vertical_alignment")))
		{
			FString VerticalAlignmentValue;
			EVerticalAlignment VerticalAlignment = VAlign_Fill;
			if (!Params->TryGetStringField(TEXT("vertical_alignment"), VerticalAlignmentValue) ||
				!TryParseEnumValue(VerticalAlignmentValue, StaticEnum<EVerticalAlignment>(), VerticalAlignment))
			{
				return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Field 'vertical_alignment' must be a valid vertical alignment string"));
			}
			BackgroundBlurSlot->SetVerticalAlignment(VerticalAlignment);
		}
	}
	else if (BorderSlot)
	{
		if (Params->HasField(TEXT("padding")))
		{
			TSharedPtr<FJsonObject> PaddingObject;
			FMargin Padding;
			if (!TryGetObjectParam(Params, TEXT("padding"), PaddingObject, ParseError) ||
				!ParseMarginJson(PaddingObject, Padding, TEXT("padding"), ParseError))
			{
				return FUnrealAICommonUtils::CreateErrorResponse(ParseError);
			}
			BorderSlot->SetPadding(Padding);
		}

		if (Params->HasField(TEXT("horizontal_alignment")))
		{
			FString HorizontalAlignmentValue;
			EHorizontalAlignment HorizontalAlignment = HAlign_Fill;
			if (!Params->TryGetStringField(TEXT("horizontal_alignment"), HorizontalAlignmentValue) ||
				!TryParseEnumValue(HorizontalAlignmentValue, StaticEnum<EHorizontalAlignment>(), HorizontalAlignment))
			{
				return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Field 'horizontal_alignment' must be a valid horizontal alignment string"));
			}
			BorderSlot->SetHorizontalAlignment(HorizontalAlignment);
		}

		if (Params->HasField(TEXT("vertical_alignment")))
		{
			FString VerticalAlignmentValue;
			EVerticalAlignment VerticalAlignment = VAlign_Fill;
			if (!Params->TryGetStringField(TEXT("vertical_alignment"), VerticalAlignmentValue) ||
				!TryParseEnumValue(VerticalAlignmentValue, StaticEnum<EVerticalAlignment>(), VerticalAlignment))
			{
				return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Field 'vertical_alignment' must be a valid vertical alignment string"));
			}
			BorderSlot->SetVerticalAlignment(VerticalAlignment);
		}
	}
	else if (ButtonSlot)
	{
		if (Params->HasField(TEXT("padding")))
		{
			TSharedPtr<FJsonObject> PaddingObject;
			FMargin Padding;
			if (!TryGetObjectParam(Params, TEXT("padding"), PaddingObject, ParseError) ||
				!ParseMarginJson(PaddingObject, Padding, TEXT("padding"), ParseError))
			{
				return FUnrealAICommonUtils::CreateErrorResponse(ParseError);
			}
			ButtonSlot->SetPadding(Padding);
		}

		if (Params->HasField(TEXT("horizontal_alignment")))
		{
			FString HorizontalAlignmentValue;
			EHorizontalAlignment HorizontalAlignment = HAlign_Fill;
			if (!Params->TryGetStringField(TEXT("horizontal_alignment"), HorizontalAlignmentValue) ||
				!TryParseEnumValue(HorizontalAlignmentValue, StaticEnum<EHorizontalAlignment>(), HorizontalAlignment))
			{
				return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Field 'horizontal_alignment' must be a valid horizontal alignment string"));
			}
			ButtonSlot->SetHorizontalAlignment(HorizontalAlignment);
		}

		if (Params->HasField(TEXT("vertical_alignment")))
		{
			FString VerticalAlignmentValue;
			EVerticalAlignment VerticalAlignment = VAlign_Fill;
			if (!Params->TryGetStringField(TEXT("vertical_alignment"), VerticalAlignmentValue) ||
				!TryParseEnumValue(VerticalAlignmentValue, StaticEnum<EVerticalAlignment>(), VerticalAlignment))
			{
				return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Field 'vertical_alignment' must be a valid vertical alignment string"));
			}
			ButtonSlot->SetVerticalAlignment(VerticalAlignment);
		}
	}
	else if (OverlaySlot)
	{
		if (Params->HasField(TEXT("padding")))
		{
			TSharedPtr<FJsonObject> PaddingObject;
			FMargin Padding;
			if (!TryGetObjectParam(Params, TEXT("padding"), PaddingObject, ParseError) ||
				!ParseMarginJson(PaddingObject, Padding, TEXT("padding"), ParseError))
			{
				return FUnrealAICommonUtils::CreateErrorResponse(ParseError);
			}
			OverlaySlot->SetPadding(Padding);
		}

		if (Params->HasField(TEXT("horizontal_alignment")))
		{
			FString HorizontalAlignmentValue;
			EHorizontalAlignment HorizontalAlignment = HAlign_Fill;
			if (!Params->TryGetStringField(TEXT("horizontal_alignment"), HorizontalAlignmentValue) ||
				!TryParseEnumValue(HorizontalAlignmentValue, StaticEnum<EHorizontalAlignment>(), HorizontalAlignment))
			{
				return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Field 'horizontal_alignment' must be a valid horizontal alignment string"));
			}
			OverlaySlot->SetHorizontalAlignment(HorizontalAlignment);
		}

		if (Params->HasField(TEXT("vertical_alignment")))
		{
			FString VerticalAlignmentValue;
			EVerticalAlignment VerticalAlignment = VAlign_Fill;
			if (!Params->TryGetStringField(TEXT("vertical_alignment"), VerticalAlignmentValue) ||
				!TryParseEnumValue(VerticalAlignmentValue, StaticEnum<EVerticalAlignment>(), VerticalAlignment))
			{
				return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Field 'vertical_alignment' must be a valid vertical alignment string"));
			}
			OverlaySlot->SetVerticalAlignment(VerticalAlignment);
		}
	}
	else if (SizeBoxSlot)
	{
		if (Params->HasField(TEXT("padding")))
		{
			TSharedPtr<FJsonObject> PaddingObject;
			FMargin Padding;
			if (!TryGetObjectParam(Params, TEXT("padding"), PaddingObject, ParseError) ||
				!ParseMarginJson(PaddingObject, Padding, TEXT("padding"), ParseError))
			{
				return FUnrealAICommonUtils::CreateErrorResponse(ParseError);
			}
			SizeBoxSlot->SetPadding(Padding);
		}

		if (Params->HasField(TEXT("horizontal_alignment")))
		{
			FString HorizontalAlignmentValue;
			EHorizontalAlignment HorizontalAlignment = HAlign_Fill;
			if (!Params->TryGetStringField(TEXT("horizontal_alignment"), HorizontalAlignmentValue) ||
				!TryParseEnumValue(HorizontalAlignmentValue, StaticEnum<EHorizontalAlignment>(), HorizontalAlignment))
			{
				return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Field 'horizontal_alignment' must be a valid horizontal alignment string"));
			}
			SizeBoxSlot->SetHorizontalAlignment(HorizontalAlignment);
		}

		if (Params->HasField(TEXT("vertical_alignment")))
		{
			FString VerticalAlignmentValue;
			EVerticalAlignment VerticalAlignment = VAlign_Fill;
			if (!Params->TryGetStringField(TEXT("vertical_alignment"), VerticalAlignmentValue) ||
				!TryParseEnumValue(VerticalAlignmentValue, StaticEnum<EVerticalAlignment>(), VerticalAlignment))
			{
				return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Field 'vertical_alignment' must be a valid vertical alignment string"));
			}
			SizeBoxSlot->SetVerticalAlignment(VerticalAlignment);
		}
	}
	else if (WidgetSwitcherSlot)
	{
		if (Params->HasField(TEXT("padding")))
		{
			TSharedPtr<FJsonObject> PaddingObject;
			FMargin Padding;
			if (!TryGetObjectParam(Params, TEXT("padding"), PaddingObject, ParseError) ||
				!ParseMarginJson(PaddingObject, Padding, TEXT("padding"), ParseError))
			{
				return FUnrealAICommonUtils::CreateErrorResponse(ParseError);
			}
			WidgetSwitcherSlot->SetPadding(Padding);
		}

		if (Params->HasField(TEXT("horizontal_alignment")))
		{
			FString HorizontalAlignmentValue;
			EHorizontalAlignment HorizontalAlignment = HAlign_Fill;
			if (!Params->TryGetStringField(TEXT("horizontal_alignment"), HorizontalAlignmentValue) ||
				!TryParseEnumValue(HorizontalAlignmentValue, StaticEnum<EHorizontalAlignment>(), HorizontalAlignment))
			{
				return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Field 'horizontal_alignment' must be a valid horizontal alignment string"));
			}
			WidgetSwitcherSlot->SetHorizontalAlignment(HorizontalAlignment);
		}

		if (Params->HasField(TEXT("vertical_alignment")))
		{
			FString VerticalAlignmentValue;
			EVerticalAlignment VerticalAlignment = VAlign_Fill;
			if (!Params->TryGetStringField(TEXT("vertical_alignment"), VerticalAlignmentValue) ||
				!TryParseEnumValue(VerticalAlignmentValue, StaticEnum<EVerticalAlignment>(), VerticalAlignment))
			{
				return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Field 'vertical_alignment' must be a valid vertical alignment string"));
			}
			WidgetSwitcherSlot->SetVerticalAlignment(VerticalAlignment);
		}
	}
	else if (WindowTitleBarAreaSlot)
	{
		if (Params->HasField(TEXT("padding")))
		{
			TSharedPtr<FJsonObject> PaddingObject;
			FMargin Padding;
			if (!TryGetObjectParam(Params, TEXT("padding"), PaddingObject, ParseError) ||
				!ParseMarginJson(PaddingObject, Padding, TEXT("padding"), ParseError))
			{
				return FUnrealAICommonUtils::CreateErrorResponse(ParseError);
			}
			WindowTitleBarAreaSlot->SetPadding(Padding);
		}

		if (Params->HasField(TEXT("horizontal_alignment")))
		{
			FString HorizontalAlignmentValue;
			EHorizontalAlignment HorizontalAlignment = HAlign_Fill;
			if (!Params->TryGetStringField(TEXT("horizontal_alignment"), HorizontalAlignmentValue) ||
				!TryParseEnumValue(HorizontalAlignmentValue, StaticEnum<EHorizontalAlignment>(), HorizontalAlignment))
			{
				return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Field 'horizontal_alignment' must be a valid horizontal alignment string"));
			}
			WindowTitleBarAreaSlot->SetHorizontalAlignment(HorizontalAlignment);
		}

		if (Params->HasField(TEXT("vertical_alignment")))
		{
			FString VerticalAlignmentValue;
			EVerticalAlignment VerticalAlignment = VAlign_Fill;
			if (!Params->TryGetStringField(TEXT("vertical_alignment"), VerticalAlignmentValue) ||
				!TryParseEnumValue(VerticalAlignmentValue, StaticEnum<EVerticalAlignment>(), VerticalAlignment))
			{
				return FUnrealAICommonUtils::CreateErrorResponse(TEXT("Field 'vertical_alignment' must be a valid vertical alignment string"));
			}
			WindowTitleBarAreaSlot->SetVerticalAlignment(VerticalAlignment);
		}
	}

	FinalizeWidgetBlueprintMutation(WidgetBlueprint);

	TArray<TSharedPtr<FJsonValue>> UpdatedFields;
	for (const FString& FieldName : ProvidedFields)
	{
		UpdatedFields.Add(MakeShared<FJsonValueString>(FieldName));
	}

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("widget_blueprint_path"), WidgetBlueprint->GetOutermost()->GetName());
	Data->SetStringField(TEXT("widget_name"), Widget->GetName());
	Data->SetStringField(TEXT("slot_type"), SlotTypeLabel);
	Data->SetObjectField(TEXT("slot"), MakeSlotJson(Widget->Slot));
	Data->SetArrayField(TEXT("updated_fields"), UpdatedFields);

	return FUnrealAICommonUtils::CreateSuccessResponse(Data);
}