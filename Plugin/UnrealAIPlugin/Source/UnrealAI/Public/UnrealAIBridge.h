#pragma once

#include "CoreMinimal.h"
#include "EditorSubsystem.h"
#include "Sockets.h"
#include "SocketSubsystem.h"
#include "Http.h"
#include "Json.h"
#include "Interfaces/IPv4/IPv4Address.h"
#include "Interfaces/IPv4/IPv4Endpoint.h"
#include "Commands/UnrealAIEditorCommands.h"
#include "Commands/UnrealAIBlueprintCommands.h"
#include "Commands/UnrealAIBlueprintGraphCommands.h"
#include "Commands/UnrealAIAssetCommands.h"
#include "Commands/UnrealAIMaterialCommands.h"
#include "Commands/UnrealAISequencerCommands.h"
#include "Commands/UnrealAIWidgetCommands.h"
#include "Commands/UnrealAILevelCommands.h"
#include "Commands/UnrealAILandscapeCommands.h"
#include "UnrealAIBridge.generated.h"

class FMCPServerRunnable;

/**
 * Editor subsystem for MCP Bridge
 * Handles communication between external tools and the Unreal Editor
 * through a TCP socket connection. Commands are received as JSON and
 * routed to appropriate command handlers.
 */
UCLASS()
class UNREALAI_API UUnrealAIBridge : public UEditorSubsystem
{
	GENERATED_BODY()

public:
	UUnrealAIBridge();
	virtual ~UUnrealAIBridge();

	// UEditorSubsystem implementation
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	// Server functions
	void StartServer();
	void StopServer();
	bool IsRunning() const { return bIsRunning; }

	// Command execution
	FString ExecuteCommand(const FString& CommandType, const TSharedPtr<FJsonObject>& Params);

private:
	// Server state
	bool bIsRunning;
	TSharedPtr<FSocket> ListenerSocket;
	TSharedPtr<FSocket> ConnectionSocket;
	FRunnableThread* ServerThread;

	// Server configuration
	FIPv4Address ServerAddress;
	uint16 Port;

	// Command handler instances
	TSharedPtr<FUnrealAIEditorCommands> EditorCommands;
	TSharedPtr<FUnrealAIBlueprintCommands> BlueprintCommands;
	TSharedPtr<FUnrealAIBlueprintGraphCommands> BlueprintGraphCommands;
	TSharedPtr<FUnrealAIAssetCommands> AssetCommands;
	TSharedPtr<FUnrealAIMaterialCommands> MaterialCommands;
	TSharedPtr<FUnrealAISequencerCommands> SequencerCommands;
	TSharedPtr<FUnrealAIWidgetCommands> WidgetCommands;
	TSharedPtr<FUnrealAILevelCommands> LevelCommands;
	TSharedPtr<FUnrealAILandscapeCommands> LandscapeCommands;
}; 