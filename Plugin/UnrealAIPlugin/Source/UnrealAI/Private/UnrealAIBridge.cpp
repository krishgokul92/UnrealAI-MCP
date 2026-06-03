#include "UnrealAIBridge.h"
#include "MCPServerRunnable.h"
#include "Sockets.h"
#include "SocketSubsystem.h"
#include "HAL/RunnableThread.h"
#include "Interfaces/IPv4/IPv4Address.h"
#include "Interfaces/IPv4/IPv4Endpoint.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonWriter.h"
#include "ScopedTransaction.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/DirectionalLight.h"
#include "Engine/PointLight.h"
#include "Engine/SpotLight.h"
#include "Camera/CameraActor.h"
#include "EditorAssetLibrary.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "JsonObjectConverter.h"
#include "GameFramework/Actor.h"
#include "Engine/Selection.h"
#include "Kismet/GameplayStatics.h"
#include "Async/Async.h"
// Add Blueprint related includes
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Factories/BlueprintFactory.h"
#include "EdGraphSchema_K2.h"
#include "K2Node_Event.h"
#include "K2Node_VariableGet.h"
#include "K2Node_VariableSet.h"
#include "Components/StaticMeshComponent.h"
#include "Components/BoxComponent.h"
#include "Components/SphereComponent.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
// UE5.5 correct includes
#include "Engine/SimpleConstructionScript.h"
#include "Engine/SCS_Node.h"
#include "UObject/Field.h"
#include "UObject/FieldPath.h"
// Blueprint Graph specific includes
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "K2Node_CallFunction.h"
#include "K2Node_InputAction.h"
#include "K2Node_Self.h"
#include "GameFramework/InputSettings.h"
#include "EditorSubsystem.h"
#include "Subsystems/EditorActorSubsystem.h"
// Include our new command handler classes
#include "Commands/UnrealAIEditorCommands.h"
#include "Commands/UnrealAIBlueprintCommands.h"
#include "Commands/UnrealAIBlueprintGraphCommands.h"
#include "Commands/UnrealAIAssetCommands.h"
#include "Commands/UnrealAIMaterialCommands.h"
#include "Commands/UnrealAISequencerCommands.h"
#include "Commands/UnrealAIWidgetCommands.h"
#include "Commands/UnrealAILevelCommands.h"
#include "Commands/UnrealAILandscapeCommands.h"
#include "Commands/UnrealAICommonUtils.h"

// Default settings
#define MCP_SERVER_HOST "127.0.0.1"
#define MCP_SERVER_PORT 55557

UUnrealAIBridge::UUnrealAIBridge()
{
    EditorCommands = MakeShared<FUnrealAIEditorCommands>();
    BlueprintCommands = MakeShared<FUnrealAIBlueprintCommands>();
    BlueprintGraphCommands = MakeShared<FUnrealAIBlueprintGraphCommands>();
    AssetCommands = MakeShared<FUnrealAIAssetCommands>();
    MaterialCommands = MakeShared<FUnrealAIMaterialCommands>();
    SequencerCommands = MakeShared<FUnrealAISequencerCommands>();
    WidgetCommands = MakeShared<FUnrealAIWidgetCommands>();
    LevelCommands = MakeShared<FUnrealAILevelCommands>();
    LandscapeCommands = MakeShared<FUnrealAILandscapeCommands>();
}

UUnrealAIBridge::~UUnrealAIBridge()
{
    EditorCommands.Reset();
    BlueprintCommands.Reset();
    BlueprintGraphCommands.Reset();
    AssetCommands.Reset();
    MaterialCommands.Reset();
    SequencerCommands.Reset();
    WidgetCommands.Reset();
    LevelCommands.Reset();
    LandscapeCommands.Reset();
}

// Initialize subsystem
void UUnrealAIBridge::Initialize(FSubsystemCollectionBase& Collection)
{
    UE_LOG(LogTemp, Display, TEXT("UnrealAIBridge: Initializing"));
    
    bIsRunning = false;
    ListenerSocket = nullptr;
    ConnectionSocket = nullptr;
    ServerThread = nullptr;
    Port = MCP_SERVER_PORT;
    FIPv4Address::Parse(MCP_SERVER_HOST, ServerAddress);

    // Start the server automatically
    StartServer();
}

// Clean up resources when subsystem is destroyed
void UUnrealAIBridge::Deinitialize()
{
    UE_LOG(LogTemp, Display, TEXT("UnrealAIBridge: Shutting down"));
    StopServer();
}

// Start the MCP server
void UUnrealAIBridge::StartServer()
{
    if (bIsRunning)
    {
        UE_LOG(LogTemp, Warning, TEXT("UnrealAIBridge: Server is already running"));
        return;
    }

    // Create socket subsystem
    ISocketSubsystem* SocketSubsystem = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
    if (!SocketSubsystem)
    {
        UE_LOG(LogTemp, Error, TEXT("UnrealAIBridge: Failed to get socket subsystem"));
        return;
    }

    // Create listener socket
    TSharedPtr<FSocket> NewListenerSocket = MakeShareable(SocketSubsystem->CreateSocket(NAME_Stream, TEXT("UnrealAIListener"), false));
    if (!NewListenerSocket.IsValid())
    {
        UE_LOG(LogTemp, Error, TEXT("UnrealAIBridge: Failed to create listener socket"));
        return;
    }

    // Allow address reuse for quick restarts
    NewListenerSocket->SetReuseAddr(true);
    NewListenerSocket->SetNonBlocking(true);

    // Bind to address
    FIPv4Endpoint Endpoint(ServerAddress, Port);
    if (!NewListenerSocket->Bind(*Endpoint.ToInternetAddr()))
    {
        UE_LOG(LogTemp, Error, TEXT("UnrealAIBridge: Failed to bind listener socket to %s:%d"), *ServerAddress.ToString(), Port);
        return;
    }

    // Start listening
    if (!NewListenerSocket->Listen(5))
    {
        UE_LOG(LogTemp, Error, TEXT("UnrealAIBridge: Failed to start listening"));
        return;
    }

    ListenerSocket = NewListenerSocket;
    bIsRunning = true;
    UE_LOG(LogTemp, Display, TEXT("UnrealAIBridge: Server started on %s:%d"), *ServerAddress.ToString(), Port);

    // Start server thread
    ServerThread = FRunnableThread::Create(
        new FMCPServerRunnable(this, ListenerSocket),
        TEXT("UnrealAIServerThread"),
        0, TPri_Normal
    );

    if (!ServerThread)
    {
        UE_LOG(LogTemp, Error, TEXT("UnrealAIBridge: Failed to create server thread"));
        StopServer();
        return;
    }
}

// Stop the MCP server
void UUnrealAIBridge::StopServer()
{
    if (!bIsRunning)
    {
        return;
    }

    bIsRunning = false;

    // Clean up thread
    if (ServerThread)
    {
        ServerThread->Kill(true);
        delete ServerThread;
        ServerThread = nullptr;
    }

    // Close sockets
    if (ConnectionSocket.IsValid())
    {
        ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->DestroySocket(ConnectionSocket.Get());
        ConnectionSocket.Reset();
    }

    if (ListenerSocket.IsValid())
    {
        ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->DestroySocket(ListenerSocket.Get());
        ListenerSocket.Reset();
    }

    UE_LOG(LogTemp, Display, TEXT("UnrealAIBridge: Server stopped"));
}

// Execute a command received from a client
FString UUnrealAIBridge::ExecuteCommand(const FString& CommandType, const TSharedPtr<FJsonObject>& Params)
{
    UE_LOG(LogTemp, Display, TEXT("UnrealAIBridge: Executing command: %s"), *CommandType);
    
    // Create a promise to wait for the result
    TPromise<FString> Promise;
    TFuture<FString> Future = Promise.GetFuture();
    
    // Queue execution on Game Thread
    AsyncTask(ENamedThreads::GameThread, [this, CommandType, Params, Promise = MoveTemp(Promise)]() mutable
    {
        TSharedPtr<FJsonObject> ResponseJson = MakeShareable(new FJsonObject);

        // Determine if this command mutates state and should be undoable.
        static const TSet<FString> ReadOnlyCommands = {
            TEXT("ping"),
            TEXT("get_actors_in_level"),
            TEXT("capture_viewport_screenshot"),
            TEXT("get_selected_actors"),
            TEXT("get_level_viewport_info"),
            TEXT("get_world_partition_info"),
            TEXT("get_all_layers"),
            TEXT("get_actor_layers"),
            TEXT("get_actors_in_layer"),
            TEXT("find_actors_by_name"),
            TEXT("read_blueprint_content"),
            TEXT("read_widget_blueprint_content"),
            TEXT("analyze_blueprint_graph"),
            TEXT("get_blueprint_variable_details"),
            TEXT("get_blueprint_function_details"),
            TEXT("get_available_materials"),
            TEXT("get_actor_material_info"),
            TEXT("get_blueprint_material_info"),
            TEXT("get_material_expressions"),
            TEXT("get_material_connections"),
            TEXT("get_material_parameters"),
            TEXT("validate_material_graph"),
            TEXT("read_landscape_content"),
            TEXT("sample_landscape_point"),
            TEXT("sample_landscape_points"),
            TEXT("sample_landscape_grid"),
            TEXT("sample_landscape_region"),
            TEXT("sample_landscape_height_region"),
            TEXT("sample_landscape_weight_region"),
            TEXT("read_pcg_graph_content"),
            TEXT("list_pcg_node_types"),
            TEXT("read_pcg_graph_nodes"),
            TEXT("read_pcg_graph_node"),
            TEXT("read_pcg_component_content"),
            TEXT("read_behavior_tree_content"),
            TEXT("validate_behavior_tree"),
            TEXT("read_blackboard_content"),
            TEXT("read_niagara_system_content"),
            TEXT("read_niagara_system_emitter"),
            TEXT("validate_niagara_system"),
            TEXT("read_anim_blueprint_content"),
            TEXT("validate_anim_blueprint"),
            TEXT("read_anim_state_machine"),
            TEXT("read_curve_table_content"),
            TEXT("read_curve_table_row"),
            TEXT("validate_data_table_row_import"),
            TEXT("validate_curve_table_row_import"),
            TEXT("read_data_table_content"),
            TEXT("read_data_table_row"),
            TEXT("read_level_sequence_content"),
        };

        static const TSet<FString> NonTransactionalCommands = {
            TEXT("save_level"),
            TEXT("undo_last_action"),
            TEXT("redo_last_action"),
        };
        const bool bIsMutation = !ReadOnlyCommands.Contains(CommandType);
        const bool bShouldCreateTransaction = bIsMutation && !NonTransactionalCommands.Contains(CommandType);

        // Wrap mutations in a transaction so Ctrl+Z undoes them.
        TUniquePtr<FScopedTransaction> Transaction;
        if (bShouldCreateTransaction)
        {
            Transaction = MakeUnique<FScopedTransaction>(
                FText::FromString(FString::Printf(TEXT("UnrealAI: %s"), *CommandType)));
        }
        
        try
        {
            TSharedPtr<FJsonObject> ResultJson;
            
            if (CommandType == TEXT("ping"))
            {
                ResultJson = MakeShareable(new FJsonObject);
                ResultJson->SetStringField(TEXT("message"), TEXT("pong"));
            }
            // Editor Commands (including actor manipulation)
            else if (CommandType == TEXT("get_actors_in_level") || 
                     CommandType == TEXT("get_selected_actors") ||
                     CommandType == TEXT("get_level_viewport_info") ||
                     CommandType == TEXT("get_world_partition_info") ||
                     CommandType == TEXT("get_all_layers") ||
                     CommandType == TEXT("create_layer") ||
                     CommandType == TEXT("rename_layer") ||
                     CommandType == TEXT("delete_layer") ||
                     CommandType == TEXT("set_layer_visibility") ||
                     CommandType == TEXT("toggle_layer_visibility") ||
                     CommandType == TEXT("make_all_layers_visible") ||
                     CommandType == TEXT("get_actor_layers") ||
                     CommandType == TEXT("get_actors_in_layer") ||
                     CommandType == TEXT("add_actor_to_layer") ||
                     CommandType == TEXT("remove_actor_from_layer") ||
                     CommandType == TEXT("add_selected_actors_to_layer") ||
                     CommandType == TEXT("remove_selected_actors_from_layer") ||
                     CommandType == TEXT("select_actors_in_layer") ||
                     CommandType == TEXT("deselect_actors_in_layer") ||
                     CommandType == TEXT("find_actors_by_name") ||
                     CommandType == TEXT("select_actors") ||
                     CommandType == TEXT("clear_actor_selection") ||
                     CommandType == TEXT("spawn_actor") ||
                     CommandType == TEXT("save_level") ||
                     CommandType == TEXT("undo_last_action") ||
                     CommandType == TEXT("redo_last_action") ||
                     CommandType == TEXT("capture_viewport_screenshot") ||
                     CommandType == TEXT("place_in_grid") ||
                     CommandType == TEXT("place_in_circle") ||
                     CommandType == TEXT("place_along_spline") ||
                     CommandType == TEXT("scatter_in_area") ||
                     CommandType == TEXT("delete_actor") || 
                     CommandType == TEXT("set_actor_transform") ||
                     CommandType == TEXT("spawn_blueprint_actor"))
            {
                ResultJson = EditorCommands->HandleCommand(CommandType, Params);
            }
            // Blueprint Commands
            else if (CommandType == TEXT("create_blueprint") ||
                     CommandType == TEXT("add_component_to_blueprint") ||
                     CommandType == TEXT("set_physics_properties") ||
                     CommandType == TEXT("compile_blueprint") ||
                     CommandType == TEXT("set_static_mesh_properties") ||
                     CommandType == TEXT("set_mesh_material_color") ||
                     CommandType == TEXT("get_available_materials") ||
                     CommandType == TEXT("apply_material_to_actor") ||
                     CommandType == TEXT("apply_material_to_blueprint") ||
                     CommandType == TEXT("get_actor_material_info") ||
                     CommandType == TEXT("get_blueprint_material_info") ||
                     CommandType == TEXT("read_blueprint_content") ||
                     CommandType == TEXT("analyze_blueprint_graph") ||
                     CommandType == TEXT("get_blueprint_variable_details") ||
                     CommandType == TEXT("get_blueprint_function_details"))
            {
                ResultJson = BlueprintCommands->HandleCommand(CommandType, Params);
            }
            else if (CommandType == TEXT("create_level_sequence") ||
                     CommandType == TEXT("read_level_sequence_content") ||
                     CommandType == TEXT("add_camera_cut_track_to_level_sequence") ||
                     CommandType == TEXT("add_actor_possessable_to_level_sequence") ||
                     CommandType == TEXT("add_track_to_binding_in_level_sequence") ||
                     CommandType == TEXT("add_float_key_to_binding_track_in_level_sequence") ||
                     CommandType == TEXT("set_level_sequence_playback_range") ||
                     CommandType == TEXT("add_master_track_to_level_sequence") ||
                     CommandType == TEXT("add_section_to_master_track_in_level_sequence") ||
                     CommandType == TEXT("set_section_range_in_master_track_in_level_sequence") ||
                     CommandType == TEXT("remove_section_from_master_track_in_level_sequence"))
            {
                ResultJson = SequencerCommands->HandleCommand(CommandType, Params);
            }
            // Blueprint Graph Commands
            else if (CommandType == TEXT("add_blueprint_node") ||
                     CommandType == TEXT("connect_nodes") ||
                     CommandType == TEXT("create_variable") ||
                     CommandType == TEXT("set_blueprint_variable_properties") ||
                     CommandType == TEXT("add_event_node") ||
                     CommandType == TEXT("delete_node") ||
                     CommandType == TEXT("set_node_property") ||
                     CommandType == TEXT("create_function") ||
                     CommandType == TEXT("add_function_input") ||
                     CommandType == TEXT("add_function_output") ||
                     CommandType == TEXT("delete_function") ||
                     CommandType == TEXT("rename_function") ||
                     CommandType == TEXT("paste_nodes_to_blueprint"))
            {
                ResultJson = BlueprintGraphCommands->HandleCommand(CommandType, Params);
            }
            // Asset Commands (Phase 7 Wave 2)
            else if (CommandType == TEXT("find_assets") ||
                     CommandType == TEXT("read_pcg_graph_content") ||
                     CommandType == TEXT("list_pcg_node_types") ||
                     CommandType == TEXT("read_pcg_graph_nodes") ||
                     CommandType == TEXT("read_pcg_graph_node") ||
                     CommandType == TEXT("create_pcg_graph_asset") ||
                     CommandType == TEXT("create_pcg_graph_instance") ||
                     CommandType == TEXT("add_pcg_graph_node") ||
                     CommandType == TEXT("delete_pcg_graph_node") ||
                     CommandType == TEXT("connect_pcg_graph_nodes") ||
                     CommandType == TEXT("disconnect_pcg_graph_nodes") ||
                     CommandType == TEXT("set_pcg_graph_node_position") ||
                     CommandType == TEXT("set_pcg_subgraph_node_asset") ||
                     CommandType == TEXT("add_pcg_graph_comment") ||
                     CommandType == TEXT("update_pcg_graph_comment") ||
                     CommandType == TEXT("delete_pcg_graph_comment") ||
                     CommandType == TEXT("add_pcg_graph_reroute") ||
                     CommandType == TEXT("update_pcg_graph_node_settings") ||
                     CommandType == TEXT("set_pcg_graph_node_state") ||
                     CommandType == TEXT("create_pcg_graph_parameter") ||
                     CommandType == TEXT("delete_pcg_graph_parameter") ||
                     CommandType == TEXT("rename_pcg_graph_parameter") ||
                     CommandType == TEXT("set_pcg_graph_parameter") ||
                     CommandType == TEXT("reset_pcg_graph_parameter_override") ||
                     CommandType == TEXT("read_pcg_component_content") ||
                     CommandType == TEXT("add_pcg_component_to_actor") ||
                     CommandType == TEXT("create_pcg_volume") ||
                     CommandType == TEXT("read_behavior_tree_content") ||
                     CommandType == TEXT("create_behavior_tree_asset") ||
                     CommandType == TEXT("update_behavior_tree_subtree") ||
                     CommandType == TEXT("set_behavior_tree_node_properties") ||
                     CommandType == TEXT("validate_behavior_tree") ||
                     CommandType == TEXT("read_blackboard_content") ||
                     CommandType == TEXT("create_blackboard_asset") ||
                     CommandType == TEXT("update_blackboard_keys") ||
                     CommandType == TEXT("read_niagara_system_content") ||
                     CommandType == TEXT("read_niagara_system_emitter") ||
                     CommandType == TEXT("create_niagara_system_asset") ||
                     CommandType == TEXT("set_niagara_system_user_parameters") ||
                     CommandType == TEXT("validate_niagara_system") ||
                     CommandType == TEXT("add_niagara_emitter_to_system") ||
                     CommandType == TEXT("duplicate_niagara_system_emitter") ||
                     CommandType == TEXT("rename_niagara_system_emitter") ||
                     CommandType == TEXT("remove_niagara_system_emitter") ||
                     CommandType == TEXT("read_anim_blueprint_content") ||
                     CommandType == TEXT("validate_anim_blueprint") ||
                     CommandType == TEXT("read_anim_state_machine") ||
                     CommandType == TEXT("create_anim_blueprint_asset") ||
                     CommandType == TEXT("create_anim_state_machine") ||
                     CommandType == TEXT("create_anim_state") ||
                     CommandType == TEXT("rename_anim_state") ||
                     CommandType == TEXT("delete_anim_state") ||
                     CommandType == TEXT("set_anim_state_sequence_player") ||
                     CommandType == TEXT("set_anim_state_blend_space_player") ||
                     CommandType == TEXT("set_anim_state_asset_player_parameters") ||
                     CommandType == TEXT("create_anim_transition") ||
                     CommandType == TEXT("delete_anim_transition") ||
                     CommandType == TEXT("set_anim_transition_rule") ||
                     CommandType == TEXT("read_curve_table_content") ||
                     CommandType == TEXT("read_curve_table_row") ||
                     CommandType == TEXT("read_data_table_content") ||
                     CommandType == TEXT("read_data_table_row") ||
                     CommandType == TEXT("create_curve_table_asset") ||
                     CommandType == TEXT("upsert_curve_table_row") ||
                     CommandType == TEXT("delete_curve_table_row") ||
                     CommandType == TEXT("rename_curve_table_row") ||
                     CommandType == TEXT("validate_data_table_row_import") ||
                     CommandType == TEXT("validate_curve_table_row_import") ||
                     CommandType == TEXT("upsert_data_table_row") ||
                     CommandType == TEXT("delete_data_table_row") ||
                     CommandType == TEXT("rename_data_table_row") ||
                     CommandType == TEXT("duplicate_data_table_row") ||
                     CommandType == TEXT("move_data_table_row") ||
                     CommandType == TEXT("create_data_table_asset") ||
                     CommandType == TEXT("get_asset_dependencies") ||
                     CommandType == TEXT("get_referencers") ||
                     CommandType == TEXT("create_material_instance") ||
                     CommandType == TEXT("import_asset"))
            {
                ResultJson = AssetCommands->HandleCommand(CommandType, Params);
            }
            // Material Commands (Phase 9 Wave 2 / first read-only slice)
            else if (CommandType == TEXT("create_material_asset") ||
                     CommandType == TEXT("create_material_function_asset") ||
                     CommandType == TEXT("get_material_expressions") ||
                     CommandType == TEXT("get_material_connections") ||
                     CommandType == TEXT("get_material_parameters") ||
                     CommandType == TEXT("set_material_parameters") ||
                     CommandType == TEXT("set_material_instance_parameters") ||
                     CommandType == TEXT("validate_material_graph") ||
                     CommandType == TEXT("create_material_expression") ||
                     CommandType == TEXT("delete_material_expression") ||
                     CommandType == TEXT("delete_material_expressions") ||
                     CommandType == TEXT("replace_material_expression") ||
                     CommandType == TEXT("connect_material_expressions") ||
                     CommandType == TEXT("disconnect_material_expressions") ||
                     CommandType == TEXT("connect_material_property") ||
                     CommandType == TEXT("disconnect_material_property") ||
                     CommandType == TEXT("recompile_material") ||
                     CommandType == TEXT("layout_material_expressions"))
            {
                ResultJson = MaterialCommands->HandleCommand(CommandType, Params);
            }
            // Widget Blueprint Commands (Phase 9c Wave 1)
            else if (CommandType == TEXT("create_widget_blueprint") ||
                     CommandType == TEXT("read_widget_blueprint_content") ||
                     CommandType == TEXT("add_widget_to_widget_blueprint") ||
                     CommandType == TEXT("remove_widget_from_widget_blueprint") ||
                     CommandType == TEXT("reparent_widget_in_widget_blueprint") ||
				     CommandType == TEXT("set_widget_property_binding_in_widget_blueprint") ||
				     CommandType == TEXT("remove_widget_property_binding_from_widget_blueprint") ||
                     CommandType == TEXT("create_widget_animation_in_widget_blueprint") ||
                     CommandType == TEXT("remove_widget_animation_from_widget_blueprint") ||
                     CommandType == TEXT("set_widget_slot_layout_in_widget_blueprint"))
            {
                ResultJson = WidgetCommands->HandleCommand(CommandType, Params);
            }
            // Level Commands (Phase 7 Wave 3a)
            else if (CommandType == TEXT("new_blank_map") ||
                     CommandType == TEXT("open_level") ||
                     CommandType == TEXT("snap_actors_to_grid") ||
                     CommandType == TEXT("align_actors") ||
                     CommandType == TEXT("duplicate_actor") ||
                     CommandType == TEXT("focus_viewport"))
            {
                ResultJson = LevelCommands->HandleCommand(CommandType, Params);
            }
            // Landscape Commands (Phase 7 Wave 3b)
            else if (CommandType == TEXT("get_landscapes") ||
                     CommandType == TEXT("read_landscape_content") ||
                     CommandType == TEXT("sample_landscape_point") ||
                     CommandType == TEXT("sample_landscape_points") ||
                     CommandType == TEXT("sample_landscape_grid") ||
                     CommandType == TEXT("sample_landscape_region") ||
                     CommandType == TEXT("sample_landscape_height_region") ||
                     CommandType == TEXT("sample_landscape_weight_region") ||
                     CommandType == TEXT("paint_landscape_layer_region") ||
                     CommandType == TEXT("sculpt_landscape_height_region") ||
                     CommandType == TEXT("rebuild_landscape") ||
                     CommandType == TEXT("create_landscape") ||
                     CommandType == TEXT("set_landscape_flat_height") ||
                     CommandType == TEXT("import_landscape_heightmap"))
            {
                ResultJson = LandscapeCommands->HandleCommand(CommandType, Params);
            }
            else
            {
                ResponseJson->SetStringField(TEXT("status"), TEXT("error"));
                ResponseJson->SetStringField(TEXT("error"), FString::Printf(TEXT("Unknown command: %s"), *CommandType));
                
                FString ResultString;
                TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&ResultString);
                FJsonSerializer::Serialize(ResponseJson.ToSharedRef(), Writer);
                Promise.SetValue(ResultString);
                return;
            }
            
            // Check if the result contains an error
            bool bSuccess = true;
            FString ErrorMessage;
            
            if (ResultJson->HasField(TEXT("success")))
            {
                bSuccess = ResultJson->GetBoolField(TEXT("success"));
                if (!bSuccess && ResultJson->HasField(TEXT("error")))
                {
                    ErrorMessage = ResultJson->GetStringField(TEXT("error"));
                }
            }
            
            if (bSuccess)
            {
                // Set success status and include the result
                ResponseJson->SetStringField(TEXT("status"), TEXT("success"));
                ResponseJson->SetObjectField(TEXT("result"), ResultJson);
            }
            else
            {
                // Set error status and include the error message
                ResponseJson->SetStringField(TEXT("status"), TEXT("error"));
                ResponseJson->SetStringField(TEXT("error"), ErrorMessage);
            }
        }
        catch (const std::exception& e)
        {
            ResponseJson->SetStringField(TEXT("status"), TEXT("error"));
            ResponseJson->SetStringField(TEXT("error"), UTF8_TO_TCHAR(e.what()));
        }
        
        FString ResultString;
        TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&ResultString);
        FJsonSerializer::Serialize(ResponseJson.ToSharedRef(), Writer);
        Promise.SetValue(ResultString);
    });
    
    return Future.Get();
}