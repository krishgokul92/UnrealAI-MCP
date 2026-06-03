#pragma once

#include "CoreMinimal.h"
#include "Json.h"

/**
 * Handler class for Widget Blueprint-related MCP commands.
 */
class FUnrealAIWidgetCommands
{
public:
	FUnrealAIWidgetCommands();

	TSharedPtr<FJsonObject> HandleCommand(const FString& CommandType, const TSharedPtr<FJsonObject>& Params);

private:
	TSharedPtr<FJsonObject> HandleCreateWidgetBlueprint(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleReadWidgetBlueprintContent(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleAddWidgetToWidgetBlueprint(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleRemoveWidgetFromWidgetBlueprint(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleReparentWidgetInWidgetBlueprint(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleSetWidgetSlotLayoutInWidgetBlueprint(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleSetWidgetPropertyBindingInWidgetBlueprint(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleRemoveWidgetPropertyBindingFromWidgetBlueprint(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleCreateWidgetAnimationInWidgetBlueprint(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleRemoveWidgetAnimationFromWidgetBlueprint(const TSharedPtr<FJsonObject>& Params);
};