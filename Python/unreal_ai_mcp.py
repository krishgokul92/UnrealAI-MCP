"""
UnrealAI MCP Server
===================

Exposes the UE plugin's TCP bridge as an MCP server so VS Code Copilot,
Claude Desktop, Claude Code CLI, and any other MCP-aware host can drive the
Unreal Editor directly.

Architecture:

    VS Code Copilot (or any MCP host)
            │
            │  stdio (JSON-RPC)
            ▼
    unreal_ai_mcp.py  (this file, FastMCP)
            │
            │  TCP 55557
            ▼
    UE plugin (UnrealAIBridge.cpp)

Run standalone for smoke-testing:
    python unreal_ai_mcp.py

Or register it in VS Code via .vscode/mcp.json — see the workspace README.

This server reuses ue_bridge.py (TCP client) and the same handler pattern as
ue_tools.py. Tool schemas are generated automatically from the function
signatures and docstrings by FastMCP.
"""

from __future__ import annotations

import json
import logging
import sys
from pathlib import Path
from typing import Any, Dict, List, Literal, Optional

# stderr-only logging — stdout is reserved for MCP protocol traffic.
logging.basicConfig(
    level=logging.INFO,
    stream=sys.stderr,
    format="%(asctime)s [%(levelname)s] %(name)s: %(message)s",
)
log = logging.getLogger("UnrealAI.MCP")

from mcp.server.fastmcp import FastMCP

from ue_bridge import send_command, BridgeError
from unreal_ai_tooling import (
    ToolValidationError,
    filter_tool_registry,
    list_tool_categories,
    normalize_bridge_error,
    validate_bridge_command,
)
from t3d_validator import validate_t3d, expected_node_names
from t3d_snippet_loader import list_snippets, get_snippet


# --------------------------------------------------------------------------- #
# Server setup
# --------------------------------------------------------------------------- #

mcp = FastMCP(
    name="unrealai",
    instructions=(
        "Drives the Unreal Editor through the UnrealAI plugin's TCP bridge. "
        "Use these tools to inspect the level, spawn/move/delete actors, "
        "create and edit Blueprints, and paste Blueprint graph T3D. "
        "Always call ping first if you suspect the editor isn't connected."
    ),
)


# Per-tool truncation limits (mirrors ue_tools.py).
DEFAULT_TRUNCATE_BYTES = 8000
LARGE_TRUNCATE_BYTES = 16000
LARGE_RESPONSE_TOOLS = {
    "list_unreal_tools",
    "read_blueprint_content",
    "read_pcg_graph_content",
    "list_pcg_node_types",
    "read_pcg_graph_nodes",
    "read_pcg_graph_node",
    "create_pcg_graph_asset",
    "create_pcg_graph_instance",
    "add_pcg_graph_node",
    "delete_pcg_graph_node",
    "connect_pcg_graph_nodes",
    "disconnect_pcg_graph_nodes",
    "read_pcg_component_content",
    "add_pcg_component_to_actor",
    "create_pcg_volume",
    "read_blackboard_content",
    "create_blackboard_asset",
    "update_blackboard_keys",
    "read_niagara_system_content",
    "create_niagara_system_asset",
    "read_niagara_system_emitter",
    "set_niagara_system_user_parameters",
    "validate_niagara_system",
    "add_niagara_emitter_to_system",
    "duplicate_niagara_system_emitter",
    "rename_niagara_system_emitter",
    "remove_niagara_system_emitter",
    "read_anim_blueprint_content",
    "validate_anim_blueprint",
    "create_anim_blueprint_asset",
    "read_anim_state_machine",
    "create_anim_state_machine",
    "create_anim_state",
    "rename_anim_state",
    "delete_anim_state",
    "set_anim_state_sequence_player",
    "set_anim_state_blend_space_player",
    "set_anim_state_asset_player_parameters",
    "create_anim_transition",
    "delete_anim_transition",
    "set_anim_transition_rule",
    "read_widget_blueprint_content",
    "read_level_sequence_content",
    "read_curve_table_content",
    "create_curve_table_asset",
    "read_data_table_content",
    "move_data_table_row",
    "add_camera_cut_track_to_level_sequence",
    "add_actor_possessable_to_level_sequence",
    "add_track_to_binding_in_level_sequence",
    "add_float_key_to_binding_track_in_level_sequence",
    "set_level_sequence_playback_range",
    "add_master_track_to_level_sequence",
    "add_section_to_master_track_in_level_sequence",
    "set_section_range_in_master_track_in_level_sequence",
    "remove_section_from_master_track_in_level_sequence",
    "analyze_blueprint_graph",
    "read_behavior_tree_content",
    "create_behavior_tree_asset",
    "update_behavior_tree_subtree",
    "set_behavior_tree_node_properties",
    "validate_behavior_tree",
}


def _bridge(cmd: str, params: Optional[Dict[str, Any]] = None) -> str:
    """Send a command, return a JSON string suitable for an MCP tool result.

    Errors are returned as structured JSON instead of raising, so the model
    can react to them without the call appearing to crash.
    """
    payload = params or {}
    try:
        validate_bridge_command(cmd, payload)
        result = send_command(cmd, payload)
        if isinstance(result, dict) and result.get("status") == "error":
            result = normalize_bridge_error(cmd, result)
    except BridgeError as exc:
        result = normalize_bridge_error(cmd, {"error": str(exc)})
    except ToolValidationError as exc:
        result = exc.to_response(cmd)
    except Exception as exc:  # noqa: BLE001
        log.exception("Unexpected error calling %s", cmd)
        result = normalize_bridge_error(cmd, {"error": f"Unexpected: {exc}"})

    text = json.dumps(result, ensure_ascii=False)
    limit = LARGE_TRUNCATE_BYTES if cmd in LARGE_RESPONSE_TOOLS else DEFAULT_TRUNCATE_BYTES
    if len(text) > limit:
        text = (
            text[:limit]
            + f"\n\n[truncated at {limit} bytes — query a specific item by name to drill in]"
        )
    return text


def _registry_source_path() -> str:
    return str(Path(__file__))


def _json_tool_result(payload: Dict[str, Any]) -> str:
    text = json.dumps(payload, ensure_ascii=False)
    limit = LARGE_TRUNCATE_BYTES if len(text) > DEFAULT_TRUNCATE_BYTES else DEFAULT_TRUNCATE_BYTES
    if len(text) > limit:
        return text[:limit] + f"\n\n[truncated at {limit} bytes — narrow the category or query filter and retry]"
    return text


BLUEPRINT_FOLDER = "/Game/Blueprints"


def _bp_path(blueprint_name: str) -> str:
    normalized = blueprint_name.strip()
    if not normalized:
        return normalized

    if normalized.startswith("/"):
        if "." in normalized.rsplit("/", 1)[-1]:
            return normalized

        asset_name = normalized.rsplit("/", 1)[-1]
        return f"{normalized}.{asset_name}"

    return f"{BLUEPRINT_FOLDER}/{normalized}.{normalized}"


# --------------------------------------------------------------------------- #
# Level / actor tools
# --------------------------------------------------------------------------- #

@mcp.tool()
def ping() -> str:
    """Health-check the UE editor bridge. Call this first to verify the editor is connected."""
    return _bridge("ping")


@mcp.tool()
def get_unreal_tool_categories() -> str:
    """List UnrealAI tool categories with counts for read-only, mutating, and destructive tools."""
    categories = list_tool_categories(_registry_source_path())
    return _json_tool_result({
        "status": "success",
        "category_count": len(categories),
        "categories": categories,
    })


@mcp.tool()
def list_unreal_tools(
    category: str = "",
    query: str = "",
    include_read_only: bool = True,
    include_mutating: bool = True,
    include_destructive: bool = True,
) -> str:
    """List UnrealAI tools with category metadata and optional filtering.

    Args:
        category: Optional category name such as 'editor_level', 'materials', 'pcg', or 'widget_blueprints'.
        query: Optional case-insensitive substring matched against tool names, summaries, and categories.
        include_read_only: Include read-only tools.
        include_mutating: Include mutating tools.
        include_destructive: Include destructive tools such as delete/remove/reset helpers.
    """
    tools = filter_tool_registry(
        _registry_source_path(),
        category=category,
        query=query,
        include_read_only=include_read_only,
        include_mutating=include_mutating,
        include_destructive=include_destructive,
    )
    return _json_tool_result({
        "status": "success",
        "category": category.strip().lower(),
        "query": query,
        "tool_count": len(tools),
        "tools": tools,
    })


@mcp.tool()
def get_actors_in_level() -> str:
    """List every actor in the current editor level (name, class, location)."""
    return _bridge("get_actors_in_level")


@mcp.tool()
def get_selected_actors() -> str:
    """List the actors currently selected in the Unreal Editor outliner."""
    return _bridge("get_selected_actors")


@mcp.tool()
def get_level_viewport_info() -> str:
    """Inspect open level-editor viewports, including camera location, rotation, FOV, and realtime state."""
    return _bridge("get_level_viewport_info")


@mcp.tool()
def get_world_partition_info() -> str:
    """Inspect whether the current editor world uses World Partition and whether streaming is complete."""
    return _bridge("get_world_partition_info")


@mcp.tool()
def get_all_layers() -> str:
    """List all known editor layers with deterministic visibility and actor-count summaries."""
    return _bridge("get_all_layers")


@mcp.tool()
def create_layer(layer_name: str) -> str:
    """Create one new empty editor layer by name."""
    return _bridge("create_layer", {
        "layer_name": layer_name,
    })


@mcp.tool()
def rename_layer(layer_name: str, new_layer_name: str) -> str:
    """Rename one existing editor layer to a new unique name."""
    return _bridge("rename_layer", {
        "layer_name": layer_name,
        "new_layer_name": new_layer_name,
    })


@mcp.tool()
def delete_layer(layer_name: str) -> str:
    """Delete one editor layer and remove it from any actors that used it."""
    return _bridge("delete_layer", {
        "layer_name": layer_name,
    })


@mcp.tool()
def set_layer_visibility(layer_name: str, is_visible: bool) -> str:
    """Set the visibility of one existing editor layer by name."""
    return _bridge("set_layer_visibility", {
        "layer_name": layer_name,
        "is_visible": is_visible,
    })


@mcp.tool()
def toggle_layer_visibility(layer_name: str) -> str:
    """Toggle the visibility of one existing editor layer by name."""
    return _bridge("toggle_layer_visibility", {
        "layer_name": layer_name,
    })


@mcp.tool()
def make_all_layers_visible() -> str:
    """Set every known editor layer visible and return the updated layer catalog summary."""
    return _bridge("make_all_layers_visible")


@mcp.tool()
def get_actor_layers(actor_name: str) -> str:
    """List the editor layer names currently assigned to a specific actor by name or label."""
    return _bridge("get_actor_layers", {
        "actor_name": actor_name,
    })


@mcp.tool()
def get_actors_in_layer(layer_name: str) -> str:
    """List the actors currently assigned to a specific editor layer."""
    return _bridge("get_actors_in_layer", {
        "layer_name": layer_name,
    })


@mcp.tool()
def add_actor_to_layer(actor_name: str, layer_name: str) -> str:
    """Add one actor to one editor layer by name or label, creating the layer if needed."""
    return _bridge("add_actor_to_layer", {
        "actor_name": actor_name,
        "layer_name": layer_name,
    })


@mcp.tool()
def remove_actor_from_layer(actor_name: str, layer_name: str) -> str:
    """Remove one actor from one editor layer by name or label."""
    return _bridge("remove_actor_from_layer", {
        "actor_name": actor_name,
        "layer_name": layer_name,
    })


@mcp.tool()
def add_selected_actors_to_layer(layer_name: str) -> str:
    """Add the current editor selection to one editor layer, creating the layer if needed."""
    return _bridge("add_selected_actors_to_layer", {
        "layer_name": layer_name,
    })


@mcp.tool()
def remove_selected_actors_from_layer(layer_name: str) -> str:
    """Remove the current editor selection from one existing editor layer."""
    return _bridge("remove_selected_actors_from_layer", {
        "layer_name": layer_name,
    })


@mcp.tool()
def select_actors_in_layer(
    layer_name: str,
    replace_selection: bool = True,
    select_even_if_hidden: bool = False,
) -> str:
    """Select all actors assigned to one editor layer, optionally replacing the current selection first."""
    return _bridge("select_actors_in_layer", {
        "layer_name": layer_name,
        "replace_selection": replace_selection,
        "select_even_if_hidden": select_even_if_hidden,
    })


@mcp.tool()
def deselect_actors_in_layer(layer_name: str) -> str:
    """Deselect any currently selected actors assigned to one editor layer."""
    return _bridge("deselect_actors_in_layer", {
        "layer_name": layer_name,
    })


@mcp.tool()
def select_actors(actor_names: list[str], replace_selection: bool = True) -> str:
    """Select actors in the Unreal Editor outliner by name or label, optionally replacing the current selection."""
    return _bridge("select_actors", {
        "actor_names": actor_names,
        "replace_selection": replace_selection,
    })


@mcp.tool()
def clear_actor_selection() -> str:
    """Clear the current Unreal Editor actor selection."""
    return _bridge("clear_actor_selection")


@mcp.tool()
def find_actors_by_name(pattern: str) -> str:
    """Find actors whose label matches a wildcard pattern (e.g. 'Cube*', 'BP_Health*')."""
    return _bridge("find_actors_by_name", {"pattern": pattern})


@mcp.tool()
def spawn_actor(
    type: str,
    name: str,
    location: Optional[List[float]] = None,
    rotation: Optional[List[float]] = None,
) -> str:
    """Spawn a new actor in the current level.

    Args:
        type: Actor class name, e.g. 'StaticMeshActor', 'PointLight', 'CameraActor'.
        name: Unique label for the new actor.
        location: World location [X, Y, Z]. Defaults to origin.
        rotation: World rotation [Pitch, Yaw, Roll] in degrees. Defaults to zero.
    """
    return _bridge("spawn_actor", {
        "type": type,
        "name": name,
        "location": location or [0, 0, 0],
        "rotation": rotation or [0, 0, 0],
    })


@mcp.tool()
def delete_actor(name: str) -> str:
    """Delete an actor from the current level by its label."""
    return _bridge("delete_actor", {"name": name})


@mcp.tool()
def set_actor_transform(
    name: str,
    location: Optional[List[float]] = None,
    rotation: Optional[List[float]] = None,
    scale: Optional[List[float]] = None,
) -> str:
    """Move, rotate, or scale an existing actor. Pass only the fields you want to change."""
    payload: Dict[str, Any] = {"name": name}
    if location is not None:
        payload["location"] = location
    if rotation is not None:
        payload["rotation"] = rotation
    if scale is not None:
        payload["scale"] = scale
    return _bridge("set_actor_transform", payload)


# --------------------------------------------------------------------------- #
# Blueprint asset tools
# --------------------------------------------------------------------------- #

@mcp.tool()
def create_blueprint(name: str, parent_class: str = "Actor") -> str:
    """Create a new Blueprint asset.

    Args:
        name: Blueprint asset name.
        parent_class: Parent UClass, e.g. 'Actor', 'Pawn', 'Character'.
    """
    return _bridge("create_blueprint", {"name": name, "parent_class": parent_class})


@mcp.tool()
def create_widget_blueprint(
    widget_name: str,
    destination_path: str = "/Game/UI",
    parent_class: str = "UserWidget",
    root_widget_class: str = "CanvasPanel",
    add_default_child: bool = False,
    default_child_widget_class: str = "TextBlock",
) -> str:
    """Create a new Widget Blueprint asset.

    Args:
        widget_name: Widget Blueprint asset name.
        destination_path: Content Browser folder, e.g. '/Game/UI'.
        parent_class: Parent UUserWidget subclass name or class path.
        root_widget_class: Root panel widget class name, e.g. 'CanvasPanel'.
        add_default_child: If true, seed one child widget under the root panel.
        default_child_widget_class: Child widget class used when seeding a default child.
    """
    payload: Dict[str, Any] = {
        "widget_name": widget_name,
        "destination_path": destination_path,
        "parent_class": parent_class,
        "root_widget_class": root_widget_class,
    }
    if add_default_child:
        payload["add_default_child"] = True
        payload["default_child_widget_class"] = default_child_widget_class
    return _bridge("create_widget_blueprint", payload)


@mcp.tool()
def read_widget_blueprint_content(widget_blueprint_path: str) -> str:
    """Read a Widget Blueprint's editable widget tree, bindings, animations, and named-slot content."""
    return _bridge("read_widget_blueprint_content", {
        "widget_blueprint_path": widget_blueprint_path,
    })


@mcp.tool()
def create_level_sequence(
    sequence_name: str,
    destination_path: str = "/Game/Cinematics",
) -> str:
    """Create a new Level Sequence asset.

    Args:
        sequence_name: Level Sequence asset name.
        destination_path: Content Browser folder, e.g. '/Game/Cinematics'.
    """
    return _bridge("create_level_sequence", {
        "sequence_name": sequence_name,
        "destination_path": destination_path,
    })


@mcp.tool()
def read_level_sequence_content(level_sequence_path: str) -> str:
    """Read a Level Sequence's MovieScene summary, including bindings, tracks, sections, and playback metadata."""
    return _bridge("read_level_sequence_content", {
        "level_sequence_path": level_sequence_path,
    })


@mcp.tool()
def add_camera_cut_track_to_level_sequence(
    level_sequence_path: str,
    track_class: str = "/Script/MovieSceneTracks.MovieSceneCameraCutTrack",
) -> str:
    """Add the Level Sequence's camera-cut master track if it does not already exist."""
    return _bridge("add_camera_cut_track_to_level_sequence", {
        "level_sequence_path": level_sequence_path,
        "track_class": track_class,
    })


@mcp.tool()
def add_actor_possessable_to_level_sequence(
    level_sequence_path: str,
    actor_name: str,
) -> str:
    """Add an actor possessable binding to a Level Sequence if it is not already bound."""
    return _bridge("add_actor_possessable_to_level_sequence", {
        "level_sequence_path": level_sequence_path,
        "actor_name": actor_name,
    })


@mcp.tool()
def add_track_to_binding_in_level_sequence(
    level_sequence_path: str,
    binding_guid: str,
    track_class: str = "/Script/MovieSceneTracks.MovieSceneFloatTrack",
) -> str:
    """Add one bound track to an existing Level Sequence object binding if that track class is not already present."""
    return _bridge("add_track_to_binding_in_level_sequence", {
        "level_sequence_path": level_sequence_path,
        "binding_guid": binding_guid,
        "track_class": track_class,
    })


@mcp.tool()
def add_float_key_to_binding_track_in_level_sequence(
    level_sequence_path: str,
    binding_guid: str,
    property_name: str,
    property_path: str,
    frame: int,
    value: float,
    section_start_frame: int,
    section_end_frame: int,
    interpolation: str = "auto",
) -> str:
    """Add or update one float key on a bound float property track in a Level Sequence."""
    return _bridge("add_float_key_to_binding_track_in_level_sequence", {
        "level_sequence_path": level_sequence_path,
        "binding_guid": binding_guid,
        "property_name": property_name,
        "property_path": property_path,
        "frame": frame,
        "value": value,
        "section_start_frame": section_start_frame,
        "section_end_frame": section_end_frame,
        "interpolation": interpolation,
    })


@mcp.tool()
def set_level_sequence_playback_range(
    level_sequence_path: str,
    start_frame: int,
    end_frame: int,
) -> str:
    """Set the inclusive-start / exclusive-end playback range of a Level Sequence."""
    return _bridge("set_level_sequence_playback_range", {
        "level_sequence_path": level_sequence_path,
        "start_frame": start_frame,
        "end_frame": end_frame,
    })


@mcp.tool()
def add_master_track_to_level_sequence(
    level_sequence_path: str,
    track_class: str = "/Script/MovieSceneTracks.MovieSceneCinematicShotTrack",
) -> str:
    """Add an unbound master track to the Level Sequence if that track class is not already present."""
    return _bridge("add_master_track_to_level_sequence", {
        "level_sequence_path": level_sequence_path,
        "track_class": track_class,
    })


@mcp.tool()
def add_section_to_master_track_in_level_sequence(
    level_sequence_path: str,
    start_frame: int,
    end_frame: int,
    track_class: str = "/Script/MovieSceneTracks.MovieSceneCinematicShotTrack",
) -> str:
    """Add one section with an explicit frame range to an existing unbound master track in a Level Sequence."""
    return _bridge("add_section_to_master_track_in_level_sequence", {
        "level_sequence_path": level_sequence_path,
        "track_class": track_class,
        "start_frame": start_frame,
        "end_frame": end_frame,
    })


@mcp.tool()
def set_section_range_in_master_track_in_level_sequence(
    level_sequence_path: str,
    section_index: int,
    start_frame: int,
    end_frame: int,
    track_class: str = "/Script/MovieSceneTracks.MovieSceneCinematicShotTrack",
) -> str:
    """Update one existing section's frame range on an unbound master track in a Level Sequence."""
    return _bridge("set_section_range_in_master_track_in_level_sequence", {
        "level_sequence_path": level_sequence_path,
        "track_class": track_class,
        "section_index": section_index,
        "start_frame": start_frame,
        "end_frame": end_frame,
    })


@mcp.tool()
def remove_section_from_master_track_in_level_sequence(
    level_sequence_path: str,
    section_index: int,
    track_class: str = "/Script/MovieSceneTracks.MovieSceneCinematicShotTrack",
) -> str:
    """Remove one existing section by index from an unbound master track in a Level Sequence."""
    return _bridge("remove_section_from_master_track_in_level_sequence", {
        "level_sequence_path": level_sequence_path,
        "track_class": track_class,
        "section_index": section_index,
    })


@mcp.tool()
def set_widget_property_binding_in_widget_blueprint(
    widget_blueprint_path: str,
    widget_name: str,
    property_name: str,
    function_name: Optional[str] = None,
    source_property: Optional[str] = None,
    source_path: Optional[str] = None,
) -> str:
    """Bind a widget property or event delegate to a function or member-property path.

    Exactly one binding mode should be supplied:
    - function binding: pass `function_name`
    - property binding: pass `source_property` and optionally `source_path`
    """
    payload: Dict[str, Any] = {
        "widget_blueprint_path": widget_blueprint_path,
        "widget_name": widget_name,
        "property_name": property_name,
    }
    if function_name is not None:
        payload["function_name"] = function_name
    if source_property is not None:
        payload["source_property"] = source_property
    if source_path is not None:
        payload["source_path"] = source_path
    return _bridge("set_widget_property_binding_in_widget_blueprint", payload)


@mcp.tool()
def remove_widget_property_binding_from_widget_blueprint(
    widget_blueprint_path: str,
    widget_name: str,
    property_name: str,
) -> str:
    """Remove an existing widget property or event binding from a Widget Blueprint."""
    return _bridge("remove_widget_property_binding_from_widget_blueprint", {
        "widget_blueprint_path": widget_blueprint_path,
        "widget_name": widget_name,
        "property_name": property_name,
    })


@mcp.tool()
def create_widget_animation_in_widget_blueprint(
    widget_blueprint_path: str,
    animation_name: Optional[str] = None,
) -> str:
    """Create a Widget Blueprint animation with a fresh MovieScene."""
    payload: Dict[str, Any] = {
        "widget_blueprint_path": widget_blueprint_path,
    }
    if animation_name is not None:
        payload["animation_name"] = animation_name
    return _bridge("create_widget_animation_in_widget_blueprint", payload)


@mcp.tool()
def remove_widget_animation_from_widget_blueprint(
    widget_blueprint_path: str,
    animation_name: str,
) -> str:
    """Remove an existing Widget Blueprint animation by name."""
    return _bridge("remove_widget_animation_from_widget_blueprint", {
        "widget_blueprint_path": widget_blueprint_path,
        "animation_name": animation_name,
    })


@mcp.tool()
def add_widget_to_widget_blueprint(
    widget_blueprint_path: str,
    widget_class: str,
    parent_widget_name: Optional[str] = None,
    named_slot_name: Optional[str] = None,
    widget_name: Optional[str] = None,
    is_variable: bool = True,
) -> str:
    """Add a widget to a Widget Blueprint source tree under a panel parent or named slot host."""
    payload: Dict[str, Any] = {
        "widget_blueprint_path": widget_blueprint_path,
        "widget_class": widget_class,
        "is_variable": is_variable,
    }
    if parent_widget_name is not None:
        payload["parent_widget_name"] = parent_widget_name
    if named_slot_name is not None:
        payload["named_slot_name"] = named_slot_name
    if widget_name is not None:
        payload["widget_name"] = widget_name
    return _bridge("add_widget_to_widget_blueprint", payload)


@mcp.tool()
def remove_widget_from_widget_blueprint(widget_blueprint_path: str, widget_name: str) -> str:
    """Remove a widget and its subtree from a Widget Blueprint source tree."""
    return _bridge("remove_widget_from_widget_blueprint", {
        "widget_blueprint_path": widget_blueprint_path,
        "widget_name": widget_name,
    })


@mcp.tool()
def reparent_widget_in_widget_blueprint(
    widget_blueprint_path: str,
    widget_name: str,
    new_parent_widget_name: Optional[str] = None,
    new_named_slot_name: Optional[str] = None,
) -> str:
    """Move an existing widget to a different panel parent or named slot host in a Widget Blueprint source tree."""
    payload: Dict[str, Any] = {
        "widget_blueprint_path": widget_blueprint_path,
        "widget_name": widget_name,
    }
    if new_parent_widget_name is not None:
        payload["new_parent_widget_name"] = new_parent_widget_name
    if new_named_slot_name is not None:
        payload["new_named_slot_name"] = new_named_slot_name
    return _bridge("reparent_widget_in_widget_blueprint", payload)


@mcp.tool()
def set_widget_slot_layout_in_widget_blueprint(
    widget_blueprint_path: str,
    widget_name: str,
    anchors: Optional[Dict[str, Any]] = None,
    offsets: Optional[Dict[str, Any]] = None,
    size: Optional[Dict[str, Any]] = None,
    alignment: Optional[Dict[str, Any]] = None,
    z_order: Optional[int] = None,
    padding: Optional[Dict[str, Any]] = None,
    child_size: Optional[Dict[str, Any]] = None,
    horizontal_alignment: Optional[str] = None,
    vertical_alignment: Optional[str] = None,
    fill_empty_space: Optional[bool] = None,
    force_new_line: Optional[bool] = None,
    fill_span_when_less_than: Optional[float] = None,
    safe_area_scale: Optional[Dict[str, Any]] = None,
    is_title_safe: Optional[bool] = None,
    row: Optional[int] = None,
    row_span: Optional[int] = None,
    column: Optional[int] = None,
    column_span: Optional[int] = None,
    layer: Optional[int] = None,
    nudge: Optional[Dict[str, Any]] = None,
) -> str:
    """Mutate supported panel-slot layout fields for a widget inside a Widget Blueprint source tree.

    Current 9c-4 support covers Canvas, Grid, UniformGrid, HorizontalBox,
    VerticalBox, WrapBox, and SafeZone slots.
    """
    payload: Dict[str, Any] = {
        "widget_blueprint_path": widget_blueprint_path,
        "widget_name": widget_name,
    }
    if anchors is not None:
        payload["anchors"] = anchors
    if offsets is not None:
        payload["offsets"] = offsets
    if size is not None:
        payload["size"] = size
    if alignment is not None:
        payload["alignment"] = alignment
    if z_order is not None:
        payload["z_order"] = z_order
    if padding is not None:
        payload["padding"] = padding
    if child_size is not None:
        payload["child_size"] = child_size
    if horizontal_alignment is not None:
        payload["horizontal_alignment"] = horizontal_alignment
    if vertical_alignment is not None:
        payload["vertical_alignment"] = vertical_alignment
    if fill_empty_space is not None:
        payload["fill_empty_space"] = fill_empty_space
    if force_new_line is not None:
        payload["force_new_line"] = force_new_line
    if fill_span_when_less_than is not None:
        payload["fill_span_when_less_than"] = fill_span_when_less_than
    if safe_area_scale is not None:
        payload["safe_area_scale"] = safe_area_scale
    if is_title_safe is not None:
        payload["is_title_safe"] = is_title_safe
    if row is not None:
        payload["row"] = row
    if row_span is not None:
        payload["row_span"] = row_span
    if column is not None:
        payload["column"] = column
    if column_span is not None:
        payload["column_span"] = column_span
    if layer is not None:
        payload["layer"] = layer
    if nudge is not None:
        payload["nudge"] = nudge
    return _bridge("set_widget_slot_layout_in_widget_blueprint", payload)


@mcp.tool()
def compile_blueprint(name: str) -> str:
    """Compile a Blueprint by name. Always call after editing a Blueprint's graph or variables."""
    return _bridge("compile_blueprint", {"name": name})


@mcp.tool()
def add_component_to_blueprint(
    blueprint_name: str,
    component_type: str,
    component_name: str,
) -> str:
    """Add a component (e.g. 'StaticMeshComponent', 'PointLightComponent') to a Blueprint."""
    return _bridge("add_component_to_blueprint", {
        "blueprint_name": blueprint_name,
        "component_type": component_type,
        "component_name": component_name,
    })


@mcp.tool()
def get_available_materials() -> str:
    """List materials in the project's content browser."""
    return _bridge("get_available_materials")


@mcp.tool()
def create_material_asset(material_name: str, dest_path: str = "/Game/Materials") -> str:
    """Create a new blank material asset in the content browser."""
    return _bridge("create_material_asset", {
        "material_name": material_name,
        "dest_path": dest_path,
    })


@mcp.tool()
def create_material_function_asset(
    material_function_name: str,
    dest_path: str = "/Game/MaterialFunctions",
) -> str:
    """Create a new blank material function asset in the content browser."""
    return _bridge("create_material_function_asset", {
        "material_function_name": material_function_name,
        "dest_path": dest_path,
    })


@mcp.tool()
def get_material_expressions(material_path: str) -> str:
    """List expressions in a material or material-function graph with editor positions and pin names.

    Accepts a base material, material instance, or material function asset path.
    When a material instance is provided, the graph source is its resolved base
    material.
    """
    return _bridge("get_material_expressions", {"material_path": material_path})


@mcp.tool()
def get_material_connections(material_path: str) -> str:
    """List expression-to-expression edges in a material or material-function graph.

    The result also includes `property_inputs` for connected base-material
    outputs such as BaseColor or Roughness.
    """
    return _bridge("get_material_connections", {"material_path": material_path})


@mcp.tool()
def get_material_parameters(material_path: str) -> str:
    """List exposed material parameters on a material or material instance."""
    return _bridge("get_material_parameters", {"material_path": material_path})


@mcp.tool()
def set_material_parameters(
    material_path: str,
    scalar_parameters: Optional[Dict[str, float]] = None,
    vector_parameters: Optional[Dict[str, List[float]]] = None,
    texture_parameters: Optional[Dict[str, str]] = None,
    static_switch_parameters: Optional[Dict[str, bool]] = None,
) -> str:
    """Update default values on parameter expressions in a base material graph."""
    payload = {
        "material_path": material_path,
    }
    if scalar_parameters is not None:
        payload["scalar_parameters"] = scalar_parameters
    if vector_parameters is not None:
        payload["vector_parameters"] = vector_parameters
    if texture_parameters is not None:
        payload["texture_parameters"] = texture_parameters
    if static_switch_parameters is not None:
        payload["static_switch_parameters"] = static_switch_parameters
    return _bridge("set_material_parameters", payload)


@mcp.tool()
def set_material_instance_parameters(
    material_path: str,
    scalar_parameters: Optional[Dict[str, float]] = None,
    vector_parameters: Optional[Dict[str, List[float]]] = None,
    texture_parameters: Optional[Dict[str, str]] = None,
    static_switch_parameters: Optional[Dict[str, bool]] = None,
    clear_existing_overrides: bool = False,
) -> str:
    """Update scalar, vector, texture, or static-switch overrides on a material instance."""
    payload = {
        "material_path": material_path,
        "clear_existing_overrides": clear_existing_overrides,
    }
    if scalar_parameters is not None:
        payload["scalar_parameters"] = scalar_parameters
    if vector_parameters is not None:
        payload["vector_parameters"] = vector_parameters
    if texture_parameters is not None:
        payload["texture_parameters"] = texture_parameters
    if static_switch_parameters is not None:
        payload["static_switch_parameters"] = static_switch_parameters
    return _bridge("set_material_instance_parameters", payload)


@mcp.tool()
def validate_material_graph(material_path: str) -> str:
    """Analyze a material or material-function graph for dead-end expressions and output issues."""
    return _bridge("validate_material_graph", {"material_path": material_path})


@mcp.tool()
def create_material_expression(
    material_path: str,
    expression_class: str,
    node_pos_x: int = 0,
    node_pos_y: int = 0,
) -> str:
    """Create a material expression node on a base material or material function asset."""
    return _bridge("create_material_expression", {
        "material_path": material_path,
        "expression_class": expression_class,
        "node_pos_x": node_pos_x,
        "node_pos_y": node_pos_y,
    })


@mcp.tool()
def delete_material_expression(
    material_path: str,
    expression_id: Optional[str] = None,
    expression_name: Optional[str] = None,
) -> str:
    """Delete one material expression by GUID or node name from a material or material function graph."""
    payload = {"material_path": material_path}
    if expression_id is not None:
        payload["expression_id"] = expression_id
    if expression_name is not None:
        payload["expression_name"] = expression_name
    return _bridge("delete_material_expression", payload)


@mcp.tool()
def delete_material_expressions(
    material_path: str,
    expression_ids: Optional[List[str]] = None,
    expression_names: Optional[List[str]] = None,
) -> str:
    """Delete multiple expressions in one preflighted operation.

    This works for both base material graphs and material function graphs.
    Every requested expression is resolved before deletion starts, which avoids
    partial success caused by user-side loops.
    """
    payload: Dict[str, Any] = {"material_path": material_path}
    if expression_ids is not None:
        payload["expression_ids"] = expression_ids
    if expression_names is not None:
        payload["expression_names"] = expression_names
    return _bridge("delete_material_expressions", payload)


@mcp.tool()
def replace_material_expression(
    material_path: str,
    new_expression_class: str,
    expression_id: Optional[str] = None,
    expression_name: Optional[str] = None,
    node_pos_x: Optional[int] = None,
    node_pos_y: Optional[int] = None,
) -> str:
    """Replace one expression with a new class while preserving compatible graph connections."""
    payload: Dict[str, Any] = {
        "material_path": material_path,
        "new_expression_class": new_expression_class,
    }
    if expression_id is not None:
        payload["expression_id"] = expression_id
    if expression_name is not None:
        payload["expression_name"] = expression_name
    if node_pos_x is not None:
        payload["node_pos_x"] = node_pos_x
    if node_pos_y is not None:
        payload["node_pos_y"] = node_pos_y
    return _bridge("replace_material_expression", payload)


@mcp.tool()
def connect_material_expressions(
    material_path: str,
    from_expression_id: Optional[str] = None,
    from_expression_name: Optional[str] = None,
    from_output_name: str = "",
    to_expression_id: Optional[str] = None,
    to_expression_name: Optional[str] = None,
    to_input_name: str = "",
) -> str:
    """Connect one expression output into another expression input."""
    payload = {
        "material_path": material_path,
        "from_output_name": from_output_name,
        "to_input_name": to_input_name,
    }
    if from_expression_id is not None:
        payload["from_expression_id"] = from_expression_id
    if from_expression_name is not None:
        payload["from_expression_name"] = from_expression_name
    if to_expression_id is not None:
        payload["to_expression_id"] = to_expression_id
    if to_expression_name is not None:
        payload["to_expression_name"] = to_expression_name
    return _bridge("connect_material_expressions", payload)


@mcp.tool()
def disconnect_material_expressions(
    material_path: str,
    to_expression_id: Optional[str] = None,
    to_expression_name: Optional[str] = None,
    to_input_name: str = "",
    from_expression_id: Optional[str] = None,
    from_expression_name: Optional[str] = None,
    from_output_name: str = "",
) -> str:
    """Disconnect one expression input from its current source expression."""
    payload = {
        "material_path": material_path,
        "to_input_name": to_input_name,
        "from_output_name": from_output_name,
    }
    if to_expression_id is not None:
        payload["to_expression_id"] = to_expression_id
    if to_expression_name is not None:
        payload["to_expression_name"] = to_expression_name
    if from_expression_id is not None:
        payload["from_expression_id"] = from_expression_id
    if from_expression_name is not None:
        payload["from_expression_name"] = from_expression_name
    return _bridge("disconnect_material_expressions", payload)


@mcp.tool()
def connect_material_property(
    material_path: str,
    property_name: str,
    from_expression_id: Optional[str] = None,
    from_expression_name: Optional[str] = None,
    from_output_name: str = "",
) -> str:
    """Connect an expression output into a base-material output property such as BaseColor."""
    payload = {
        "material_path": material_path,
        "property_name": property_name,
        "from_output_name": from_output_name,
    }
    if from_expression_id is not None:
        payload["from_expression_id"] = from_expression_id
    if from_expression_name is not None:
        payload["from_expression_name"] = from_expression_name
    return _bridge("connect_material_property", payload)


@mcp.tool()
def disconnect_material_property(
    material_path: str,
    property_name: str,
    from_expression_id: Optional[str] = None,
    from_expression_name: Optional[str] = None,
    from_output_name: str = "",
) -> str:
    """Disconnect the current source expression from a base-material output property."""
    payload = {
        "material_path": material_path,
        "property_name": property_name,
        "from_output_name": from_output_name,
    }
    if from_expression_id is not None:
        payload["from_expression_id"] = from_expression_id
    if from_expression_name is not None:
        payload["from_expression_name"] = from_expression_name
    return _bridge("disconnect_material_property", payload)


@mcp.tool()
def recompile_material(material_path: str) -> str:
    """Trigger a material recompile or material-function update after graph edits."""
    return _bridge("recompile_material", {"material_path": material_path})


@mcp.tool()
def layout_material_expressions(material_path: str) -> str:
    """Auto-layout the expressions in a material or material-function graph."""
    return _bridge("layout_material_expressions", {"material_path": material_path})


# --------------------------------------------------------------------------- #
# Blueprint inspection tools
# --------------------------------------------------------------------------- #

@mcp.tool()
def read_blueprint_content(blueprint_name: str) -> str:
    """Dump full Blueprint structure: variables, functions, components, parent class.

    Call this BEFORE editing an existing Blueprint so you know what's there.
    """
    return _bridge("read_blueprint_content", {
        "blueprint_path": _bp_path(blueprint_name),
        "blueprint_name": blueprint_name,
    })


@mcp.tool()
def read_pcg_graph_content(asset_path: str) -> str:
    """Read one PCG graph or graph-instance asset, including template/library flags, tool metadata, node count, and user-parameter summaries."""
    return _bridge("read_pcg_graph_content", {
        "asset_path": asset_path,
    })


@mcp.tool()
def create_pcg_graph_asset(
    pcg_graph_name: str,
    destination_path: str = "/Game/PCG",
    template_asset_path: Optional[str] = None,
    is_template: Optional[bool] = None,
    expose_to_library: Optional[bool] = None,
    expose_generation_in_asset_explorer: Optional[bool] = None,
    title_override: Optional[str] = None,
    color_override: Optional[List[float]] = None,
) -> str:
    """Create a PCG graph asset, optionally duplicating a template graph and setting initial template or library-facing metadata."""
    payload: Dict[str, Any] = {
        "pcg_graph_name": pcg_graph_name,
        "destination_path": destination_path,
    }
    if template_asset_path is not None:
        payload["template_asset_path"] = template_asset_path
    if is_template is not None:
        payload["is_template"] = is_template
    if expose_to_library is not None:
        payload["expose_to_library"] = expose_to_library
    if expose_generation_in_asset_explorer is not None:
        payload["expose_generation_in_asset_explorer"] = expose_generation_in_asset_explorer
    if title_override is not None:
        payload["title_override"] = title_override
    if color_override is not None:
        payload["color_override"] = color_override
    return _bridge("create_pcg_graph_asset", payload)


@mcp.tool()
def create_pcg_graph_instance(
    instance_name: str,
    parent_graph_path: str,
    destination_path: str = "/Game/PCG",
) -> str:
    """Create a PCG graph-instance asset that points at an existing parent PCG graph or graph instance."""
    return _bridge("create_pcg_graph_instance", {
        "instance_name": instance_name,
        "parent_graph_path": parent_graph_path,
        "destination_path": destination_path,
    })


@mcp.tool()
def read_pcg_component_content(actor_name: str, component_name: Optional[str] = None) -> str:
    """Read one actor-owned PCG component, including assigned graph, generation trigger, partitioning, scheduling-policy, and owner transform context."""
    payload: Dict[str, Any] = {
        "actor_name": actor_name,
    }
    if component_name is not None:
        payload["component_name"] = component_name
    return _bridge("read_pcg_component_content", payload)


@mcp.tool()
def add_pcg_component_to_actor(
    actor_name: str,
    component_name: str = "PCGComponent",
    graph_asset_path: Optional[str] = None,
    generation_trigger: Optional[str] = None,
    is_partitioned: Optional[bool] = None,
    activated: Optional[bool] = None,
    seed: Optional[int] = None,
    generate_on_drop_when_trigger_on_demand: Optional[bool] = None,
) -> str:
    """Attach or reuse a PCG component on an existing actor and optionally assign its graph, trigger mode, partitioning, activation, and seed."""
    payload: Dict[str, Any] = {
        "actor_name": actor_name,
        "component_name": component_name,
    }
    if graph_asset_path is not None:
        payload["graph_asset_path"] = graph_asset_path
    if generation_trigger is not None:
        payload["generation_trigger"] = generation_trigger
    if is_partitioned is not None:
        payload["is_partitioned"] = is_partitioned
    if activated is not None:
        payload["activated"] = activated
    if seed is not None:
        payload["seed"] = seed
    if generate_on_drop_when_trigger_on_demand is not None:
        payload["generate_on_drop_when_trigger_on_demand"] = generate_on_drop_when_trigger_on_demand
    return _bridge("add_pcg_component_to_actor", payload)


@mcp.tool()
def create_pcg_volume(
    volume_name: str,
    location: Optional[List[float]] = None,
    rotation: Optional[List[float]] = None,
    scale: Optional[List[float]] = None,
    graph_asset_path: Optional[str] = None,
    generation_trigger: Optional[str] = None,
    is_partitioned: Optional[bool] = None,
    activated: Optional[bool] = None,
    seed: Optional[int] = None,
    generate_on_drop_when_trigger_on_demand: Optional[bool] = None,
) -> str:
    """Create a PCG volume actor, then optionally configure its default PCG component with graph assignment and basic generation settings."""
    payload: Dict[str, Any] = {
        "volume_name": volume_name,
    }
    if location is not None:
        payload["location"] = location
    if rotation is not None:
        payload["rotation"] = rotation
    if scale is not None:
        payload["scale"] = scale
    if graph_asset_path is not None:
        payload["graph_asset_path"] = graph_asset_path
    if generation_trigger is not None:
        payload["generation_trigger"] = generation_trigger
    if is_partitioned is not None:
        payload["is_partitioned"] = is_partitioned
    if activated is not None:
        payload["activated"] = activated
    if seed is not None:
        payload["seed"] = seed
    if generate_on_drop_when_trigger_on_demand is not None:
        payload["generate_on_drop_when_trigger_on_demand"] = generate_on_drop_when_trigger_on_demand
    return _bridge("create_pcg_volume", payload)


@mcp.tool()
def list_pcg_node_types(
    query: Optional[str] = None,
    settings_type: Optional[str] = None,
    max_results: int = 300,
) -> str:
    """List concrete PCG node settings classes, including default titles, aliases, and default pin metadata."""
    payload: Dict[str, Any] = {
        "max_results": max_results,
    }
    if query is not None:
        payload["query"] = query
    if settings_type is not None:
        payload["settings_type"] = settings_type
    return _bridge("list_pcg_node_types", payload)


@mcp.tool()
def read_pcg_graph_nodes(asset_path: str) -> str:
    """Read every node in a PCG graph asset, including node titles, pin metadata, position, and settings class details."""
    return _bridge("read_pcg_graph_nodes", {
        "asset_path": asset_path,
    })


@mcp.tool()
def read_pcg_graph_node(
    asset_path: str,
    node_path: Optional[str] = None,
    node_name: Optional[str] = None,
    node_title: Optional[str] = None,
) -> str:
    """Read one node from a PCG graph asset by node path, object name, or visible title."""
    payload: Dict[str, Any] = {
        "asset_path": asset_path,
    }
    if node_path is not None:
        payload["node_path"] = node_path
    if node_name is not None:
        payload["node_name"] = node_name
    if node_title is not None:
        payload["node_title"] = node_title
    return _bridge("read_pcg_graph_node", payload)


@mcp.tool()
def add_pcg_graph_node(
    asset_path: str,
    settings_class: str,
    node_title: Optional[str] = None,
    position_x: Optional[int] = None,
    position_y: Optional[int] = None,
    subgraph_asset_path: Optional[str] = None,
) -> str:
    """Add one default PCG node to a graph asset by settings class, optionally naming or positioning it and assigning a subgraph."""
    payload: Dict[str, Any] = {
        "asset_path": asset_path,
        "settings_class": settings_class,
    }
    if node_title is not None:
        payload["node_title"] = node_title
    if position_x is not None:
        payload["position_x"] = position_x
    if position_y is not None:
        payload["position_y"] = position_y
    if subgraph_asset_path is not None:
        payload["subgraph_asset_path"] = subgraph_asset_path
    return _bridge("add_pcg_graph_node", payload)


@mcp.tool()
def delete_pcg_graph_node(
    asset_path: str,
    node_path: Optional[str] = None,
    node_name: Optional[str] = None,
    node_title: Optional[str] = None,
) -> str:
    """Delete one non-input and non-output node from a PCG graph asset by node path, object name, or title."""
    payload: Dict[str, Any] = {
        "asset_path": asset_path,
    }
    if node_path is not None:
        payload["node_path"] = node_path
    if node_name is not None:
        payload["node_name"] = node_name
    if node_title is not None:
        payload["node_title"] = node_title
    return _bridge("delete_pcg_graph_node", payload)


@mcp.tool()
def connect_pcg_graph_nodes(
    asset_path: str,
    source_pin_name: str,
    target_pin_name: str,
    source_node_path: Optional[str] = None,
    source_node_name: Optional[str] = None,
    source_node_title: Optional[str] = None,
    target_node_path: Optional[str] = None,
    target_node_name: Optional[str] = None,
    target_node_title: Optional[str] = None,
) -> str:
    """Create one directed edge between two PCG graph nodes using node identifiers plus pin labels."""
    payload: Dict[str, Any] = {
        "asset_path": asset_path,
        "source_pin_name": source_pin_name,
        "target_pin_name": target_pin_name,
    }
    if source_node_path is not None:
        payload["source_node_path"] = source_node_path
    if source_node_name is not None:
        payload["source_node_name"] = source_node_name
    if source_node_title is not None:
        payload["source_node_title"] = source_node_title
    if target_node_path is not None:
        payload["target_node_path"] = target_node_path
    if target_node_name is not None:
        payload["target_node_name"] = target_node_name
    if target_node_title is not None:
        payload["target_node_title"] = target_node_title
    return _bridge("connect_pcg_graph_nodes", payload)


@mcp.tool()
def disconnect_pcg_graph_nodes(
    asset_path: str,
    source_pin_name: str,
    target_pin_name: str,
    source_node_path: Optional[str] = None,
    source_node_name: Optional[str] = None,
    source_node_title: Optional[str] = None,
    target_node_path: Optional[str] = None,
    target_node_name: Optional[str] = None,
    target_node_title: Optional[str] = None,
) -> str:
    """Remove one directed edge between two PCG graph nodes using node identifiers plus pin labels."""
    payload: Dict[str, Any] = {
        "asset_path": asset_path,
        "source_pin_name": source_pin_name,
        "target_pin_name": target_pin_name,
    }
    if source_node_path is not None:
        payload["source_node_path"] = source_node_path
    if source_node_name is not None:
        payload["source_node_name"] = source_node_name
    if source_node_title is not None:
        payload["source_node_title"] = source_node_title
    if target_node_path is not None:
        payload["target_node_path"] = target_node_path
    if target_node_name is not None:
        payload["target_node_name"] = target_node_name
    if target_node_title is not None:
        payload["target_node_title"] = target_node_title
    return _bridge("disconnect_pcg_graph_nodes", payload)


@mcp.tool()
def set_pcg_graph_node_position(
    asset_path: str,
    node_path: Optional[str] = None,
    node_name: Optional[str] = None,
    node_title: Optional[str] = None,
    position_x: Optional[int] = None,
    position_y: Optional[int] = None,
) -> str:
    """Move one PCG graph node to a new editor position by node path, object name, or visible title."""
    payload: Dict[str, Any] = {
        "asset_path": asset_path,
    }
    if node_path is not None:
        payload["node_path"] = node_path
    if node_name is not None:
        payload["node_name"] = node_name
    if node_title is not None:
        payload["node_title"] = node_title
    if position_x is not None:
        payload["position_x"] = position_x
    if position_y is not None:
        payload["position_y"] = position_y
    return _bridge("set_pcg_graph_node_position", payload)


@mcp.tool()
def set_pcg_subgraph_node_asset(
    asset_path: str,
    subgraph_asset_path: Optional[str] = None,
    node_path: Optional[str] = None,
    node_name: Optional[str] = None,
    node_title: Optional[str] = None,
    clear_subgraph: bool = False,
) -> str:
    """Assign or clear one PCG subgraph node's referenced subgraph asset."""
    payload: Dict[str, Any] = {
        "asset_path": asset_path,
    }
    if subgraph_asset_path is not None:
        payload["subgraph_asset_path"] = subgraph_asset_path
    if node_path is not None:
        payload["node_path"] = node_path
    if node_name is not None:
        payload["node_name"] = node_name
    if node_title is not None:
        payload["node_title"] = node_title
    if clear_subgraph:
        payload["clear_subgraph"] = True
    return _bridge("set_pcg_subgraph_node_asset", payload)


@mcp.tool()
def add_pcg_graph_comment(
    asset_path: str,
    comment_text: Optional[str] = None,
    position_x: Optional[int] = None,
    position_y: Optional[int] = None,
    width: Optional[int] = None,
    height: Optional[int] = None,
    comment_color: Optional[list[float]] = None,
    details: Optional[str] = None,
    font_size: Optional[int] = None,
    move_mode: Optional[int] = None,
    comment_depth: Optional[int] = None,
    comment_guid: Optional[str] = None,
    comment_bubble_visible_in_details_panel: Optional[bool] = None,
    color_comment_bubble: Optional[bool] = None,
    comment_bubble_pinned: Optional[bool] = None,
    comment_bubble_visible: Optional[bool] = None,
) -> str:
    """Create one PCG graph comment box with optional layout, color, and bubble settings."""
    payload: Dict[str, Any] = {
        "asset_path": asset_path,
    }
    if comment_text is not None:
        payload["comment_text"] = comment_text
    if position_x is not None:
        payload["position_x"] = position_x
    if position_y is not None:
        payload["position_y"] = position_y
    if width is not None:
        payload["width"] = width
    if height is not None:
        payload["height"] = height
    if comment_color is not None:
        payload["comment_color"] = comment_color
    if details is not None:
        payload["details"] = details
    if font_size is not None:
        payload["font_size"] = font_size
    if move_mode is not None:
        payload["move_mode"] = move_mode
    if comment_depth is not None:
        payload["comment_depth"] = comment_depth
    if comment_guid is not None:
        payload["comment_guid"] = comment_guid
    if comment_bubble_visible_in_details_panel is not None:
        payload["comment_bubble_visible_in_details_panel"] = comment_bubble_visible_in_details_panel
    if color_comment_bubble is not None:
        payload["color_comment_bubble"] = color_comment_bubble
    if comment_bubble_pinned is not None:
        payload["comment_bubble_pinned"] = comment_bubble_pinned
    if comment_bubble_visible is not None:
        payload["comment_bubble_visible"] = comment_bubble_visible
    return _bridge("add_pcg_graph_comment", payload)


@mcp.tool()
def update_pcg_graph_comment(
    asset_path: str,
    comment_guid: str,
    comment_text: Optional[str] = None,
    position_x: Optional[int] = None,
    position_y: Optional[int] = None,
    width: Optional[int] = None,
    height: Optional[int] = None,
    comment_color: Optional[list[float]] = None,
    details: Optional[str] = None,
    font_size: Optional[int] = None,
    move_mode: Optional[int] = None,
    comment_depth: Optional[int] = None,
    comment_bubble_visible_in_details_panel: Optional[bool] = None,
    color_comment_bubble: Optional[bool] = None,
    comment_bubble_pinned: Optional[bool] = None,
    comment_bubble_visible: Optional[bool] = None,
) -> str:
    """Update one existing PCG graph comment box by its comment GUID."""
    payload: Dict[str, Any] = {
        "asset_path": asset_path,
        "comment_guid": comment_guid,
    }
    if comment_text is not None:
        payload["comment_text"] = comment_text
    if position_x is not None:
        payload["position_x"] = position_x
    if position_y is not None:
        payload["position_y"] = position_y
    if width is not None:
        payload["width"] = width
    if height is not None:
        payload["height"] = height
    if comment_color is not None:
        payload["comment_color"] = comment_color
    if details is not None:
        payload["details"] = details
    if font_size is not None:
        payload["font_size"] = font_size
    if move_mode is not None:
        payload["move_mode"] = move_mode
    if comment_depth is not None:
        payload["comment_depth"] = comment_depth
    if comment_bubble_visible_in_details_panel is not None:
        payload["comment_bubble_visible_in_details_panel"] = comment_bubble_visible_in_details_panel
    if color_comment_bubble is not None:
        payload["color_comment_bubble"] = color_comment_bubble
    if comment_bubble_pinned is not None:
        payload["comment_bubble_pinned"] = comment_bubble_pinned
    if comment_bubble_visible is not None:
        payload["comment_bubble_visible"] = comment_bubble_visible
    return _bridge("update_pcg_graph_comment", payload)


@mcp.tool()
def delete_pcg_graph_comment(asset_path: str, comment_guid: str) -> str:
    """Delete one PCG graph comment box by its comment GUID."""
    return _bridge("delete_pcg_graph_comment", {
        "asset_path": asset_path,
        "comment_guid": comment_guid,
    })


@mcp.tool()
def add_pcg_graph_reroute(
    asset_path: str,
    reroute_kind: str,
    node_title: Optional[str] = None,
    position_x: Optional[int] = None,
    position_y: Optional[int] = None,
    declaration_node_path: Optional[str] = None,
    declaration_node_name: Optional[str] = None,
    declaration_node_title: Optional[str] = None,
) -> str:
    """Add one PCG reroute, named reroute declaration, or named reroute usage node; usage nodes can link to a declaration node."""
    payload: Dict[str, Any] = {
        "asset_path": asset_path,
        "reroute_kind": reroute_kind,
    }
    if node_title is not None:
        payload["node_title"] = node_title
    if position_x is not None:
        payload["position_x"] = position_x
    if position_y is not None:
        payload["position_y"] = position_y
    if declaration_node_path is not None:
        payload["declaration_node_path"] = declaration_node_path
    if declaration_node_name is not None:
        payload["declaration_node_name"] = declaration_node_name
    if declaration_node_title is not None:
        payload["declaration_node_title"] = declaration_node_title
    return _bridge("add_pcg_graph_reroute", payload)


@mcp.tool()
def update_pcg_graph_node_settings(
    asset_path: str,
    settings_patch: Dict[str, Any],
    node_path: Optional[str] = None,
    node_name: Optional[str] = None,
    node_title: Optional[str] = None,
) -> str:
    """Apply a partial JSON settings patch to one editable PCG graph node selected by path, object name, or visible title."""
    payload: Dict[str, Any] = {
        "asset_path": asset_path,
        "settings_patch": settings_patch,
    }
    if node_path is not None:
        payload["node_path"] = node_path
    if node_name is not None:
        payload["node_name"] = node_name
    if node_title is not None:
        payload["node_title"] = node_title
    return _bridge("update_pcg_graph_node_settings", payload)


@mcp.tool()
def set_pcg_graph_node_state(
    asset_path: str,
    node_path: Optional[str] = None,
    node_name: Optional[str] = None,
    node_title: Optional[str] = None,
    enabled: Optional[bool] = None,
    debug: Optional[bool] = None,
    inspecting: Optional[bool] = None,
) -> str:
    """Set one node's generic PCG state flags such as enabled, debug, or inspecting."""
    payload: Dict[str, Any] = {
        "asset_path": asset_path,
    }
    if node_path is not None:
        payload["node_path"] = node_path
    if node_name is not None:
        payload["node_name"] = node_name
    if node_title is not None:
        payload["node_title"] = node_title
    if enabled is not None:
        payload["enabled"] = enabled
    if debug is not None:
        payload["debug"] = debug
    if inspecting is not None:
        payload["inspecting"] = inspecting
    return _bridge("set_pcg_graph_node_state", payload)


@mcp.tool()
def create_pcg_graph_parameter(
    asset_path: str,
    parameter_name: str,
    parameter_type: str,
    default_value: Optional[Any] = None,
) -> str:
    """Create one PCG graph user parameter on a base graph, optionally assigning an initial default value."""
    payload: Dict[str, Any] = {
        "asset_path": asset_path,
        "parameter_name": parameter_name,
        "parameter_type": parameter_type,
    }
    if default_value is not None:
        payload["default_value"] = default_value
    return _bridge("create_pcg_graph_parameter", payload)


@mcp.tool()
def delete_pcg_graph_parameter(asset_path: str, parameter_name: str) -> str:
    """Delete one PCG graph user parameter from a base graph asset."""
    return _bridge("delete_pcg_graph_parameter", {
        "asset_path": asset_path,
        "parameter_name": parameter_name,
    })


@mcp.tool()
def rename_pcg_graph_parameter(asset_path: str, current_name: str, new_name: str) -> str:
    """Rename one PCG graph user parameter on a base graph."""
    return _bridge("rename_pcg_graph_parameter", {
        "asset_path": asset_path,
        "current_name": current_name,
        "new_name": new_name,
    })


@mcp.tool()
def set_pcg_graph_parameter(asset_path: str, parameter_name: str, value: Any) -> str:
    """Set one scalar PCG graph parameter value on either a base graph or a graph instance override."""
    return _bridge("set_pcg_graph_parameter", {
        "asset_path": asset_path,
        "parameter_name": parameter_name,
        "value": value,
    })


@mcp.tool()
def reset_pcg_graph_parameter_override(asset_path: str, parameter_name: str) -> str:
    """Reset one overridden PCG graph-instance user parameter back to its inherited graph default."""
    return _bridge("reset_pcg_graph_parameter_override", {
        "asset_path": asset_path,
        "parameter_name": parameter_name,
    })


@mcp.tool()
def read_behavior_tree_content(asset_path: str) -> str:
    """Read one Behavior Tree asset's linked Blackboard, root composite shape, node topology, and task/decorator/service summaries."""
    return _bridge("read_behavior_tree_content", {
        "asset_path": asset_path,
    })


@mcp.tool()
def create_behavior_tree_asset(
    behavior_tree_name: str,
    destination_path: str = "/Game/BehaviorTrees",
    blackboard_asset_path: Optional[str] = None,
) -> str:
    """Create a Behavior Tree asset with a default selector root and optional linked Blackboard assignment."""
    payload: Dict[str, Any] = {
        "behavior_tree_name": behavior_tree_name,
        "destination_path": destination_path,
    }
    if blackboard_asset_path is not None:
        payload["blackboard_asset_path"] = blackboard_asset_path
    return _bridge("create_behavior_tree_asset", payload)


@mcp.tool()
def update_behavior_tree_subtree(asset_path: str, operations: List[Dict[str, Any]]) -> str:
    """Apply ordered add/remove subtree edits for supported Behavior Tree composites, tasks, decorators, and services."""
    return _bridge("update_behavior_tree_subtree", {
        "asset_path": asset_path,
        "operations": operations,
    })


@mcp.tool()
def set_behavior_tree_node_properties(
    asset_path: str,
    topology_path: str,
    blackboard_key_name: Optional[str] = None,
    flow_abort_mode: Optional[str] = None,
    enabled_state: Optional[str] = None,
) -> str:
    """Mutate Behavior Tree selector bindings, decorator abort modes, and graph-backed node enabled state by topology path."""
    payload: Dict[str, Any] = {
        "asset_path": asset_path,
        "topology_path": topology_path,
    }
    if blackboard_key_name is not None:
        payload["blackboard_key_name"] = blackboard_key_name
    if flow_abort_mode is not None:
        payload["flow_abort_mode"] = flow_abort_mode
    if enabled_state is not None:
        payload["enabled_state"] = enabled_state
    return _bridge("set_behavior_tree_node_properties", payload)


@mcp.tool()
def validate_behavior_tree(asset_path: str) -> str:
    """Validate one Behavior Tree asset's runtime structure, selector bindings, abort modes, and graph-health diagnostics."""
    return _bridge("validate_behavior_tree", {
        "asset_path": asset_path,
    })


@mcp.tool()
def read_blackboard_content(asset_path: str) -> str:
    """Read one Blackboard asset's parent chain, key entries, key types, base-class filters, and default metadata."""
    return _bridge("read_blackboard_content", {
        "asset_path": asset_path,
    })


@mcp.tool()
def create_blackboard_asset(
    blackboard_name: str,
    destination_path: str = "/Game/Blackboards",
    parent_blackboard_path: Optional[str] = None,
) -> str:
    """Create a Blackboard asset, optionally assigning a parent Blackboard for inherited keys."""
    payload: Dict[str, Any] = {
        "blackboard_name": blackboard_name,
        "destination_path": destination_path,
    }
    if parent_blackboard_path is not None:
        payload["parent_blackboard_path"] = parent_blackboard_path
    return _bridge("create_blackboard_asset", payload)


@mcp.tool()
def update_blackboard_keys(asset_path: str, operations: List[Dict[str, Any]]) -> str:
    """Apply ordered Blackboard key add, update, rename, and delete operations with deterministic readback."""
    return _bridge("update_blackboard_keys", {
        "asset_path": asset_path,
        "operations": operations,
    })


@mcp.tool()
def read_niagara_system_content(asset_path: str) -> str:
    """Read one Niagara System asset's emitter handles, exposed user parameters, renderers, and compile-status metadata."""
    return _bridge("read_niagara_system_content", {
        "asset_path": asset_path,
    })


@mcp.tool()
def create_niagara_system_asset(
    niagara_system_name: str,
    destination_path: str = "/Game/NiagaraSystems",
    template_asset_path: Optional[str] = None,
) -> str:
    """Create a Niagara System asset from an empty baseline or an existing template system."""
    payload: Dict[str, Any] = {
        "niagara_system_name": niagara_system_name,
        "destination_path": destination_path,
    }
    if template_asset_path is not None:
        payload["template_asset_path"] = template_asset_path
    return _bridge("create_niagara_system_asset", payload)


@mcp.tool()
def read_niagara_system_emitter(
    asset_path: str,
    emitter_handle_id: Optional[str] = None,
    emitter_name: Optional[str] = None,
) -> str:
    """Read one Niagara System emitter handle by handle id or emitter name."""
    payload: Dict[str, Any] = {
        "asset_path": asset_path,
    }
    if emitter_handle_id is not None:
        payload["emitter_handle_id"] = emitter_handle_id
    if emitter_name is not None:
        payload["emitter_name"] = emitter_name
    return _bridge("read_niagara_system_emitter", payload)


@mcp.tool()
def set_niagara_system_user_parameters(
    asset_path: str,
    parameters: List[Dict[str, Any]],
    create_if_missing: bool = False,
) -> str:
    """Set common Niagara System exposed user-parameter defaults for float, bool, vector, color, and object-backed values."""
    return _bridge("set_niagara_system_user_parameters", {
        "asset_path": asset_path,
        "parameters": parameters,
        "create_if_missing": create_if_missing,
    })


@mcp.tool()
def validate_niagara_system(asset_path: str) -> str:
    """Validate one Niagara System for compile health, object-backed user-parameter references, unresolved renderer user-parameter bindings, and emitter enablement sanity."""
    return _bridge("validate_niagara_system", {
        "asset_path": asset_path,
    })


@mcp.tool()
def add_niagara_emitter_to_system(asset_path: str, emitter_asset_path: str) -> str:
    """Add an existing Niagara Emitter asset to a Niagara System and return the created emitter-handle readback."""
    return _bridge("add_niagara_emitter_to_system", {
        "asset_path": asset_path,
        "emitter_asset_path": emitter_asset_path,
    })


@mcp.tool()
def duplicate_niagara_system_emitter(
    asset_path: str,
    emitter_handle_id: Optional[str] = None,
    emitter_name: Optional[str] = None,
    new_emitter_name: Optional[str] = None,
) -> str:
    """Duplicate one Niagara System emitter handle by id or name, optionally renaming the new copy."""
    payload: Dict[str, Any] = {
        "asset_path": asset_path,
    }
    if emitter_handle_id is not None:
        payload["emitter_handle_id"] = emitter_handle_id
    if emitter_name is not None:
        payload["emitter_name"] = emitter_name
    if new_emitter_name is not None:
        payload["new_emitter_name"] = new_emitter_name
    return _bridge("duplicate_niagara_system_emitter", payload)


@mcp.tool()
def rename_niagara_system_emitter(
    asset_path: str,
    new_emitter_name: str,
    emitter_handle_id: Optional[str] = None,
    emitter_name: Optional[str] = None,
) -> str:
    """Rename one Niagara System emitter handle by id or current emitter name."""
    payload: Dict[str, Any] = {
        "asset_path": asset_path,
        "new_emitter_name": new_emitter_name,
    }
    if emitter_handle_id is not None:
        payload["emitter_handle_id"] = emitter_handle_id
    if emitter_name is not None:
        payload["emitter_name"] = emitter_name
    return _bridge("rename_niagara_system_emitter", payload)


@mcp.tool()
def remove_niagara_system_emitter(
    asset_path: str,
    emitter_handle_id: Optional[str] = None,
    emitter_name: Optional[str] = None,
) -> str:
    """Remove one Niagara System emitter handle by id or emitter name."""
    payload: Dict[str, Any] = {
        "asset_path": asset_path,
    }
    if emitter_handle_id is not None:
        payload["emitter_handle_id"] = emitter_handle_id
    if emitter_name is not None:
        payload["emitter_name"] = emitter_name
    return _bridge("remove_niagara_system_emitter", payload)


@mcp.tool()
def read_anim_blueprint_content(asset_path: str) -> str:
    """Read one AnimBlueprint asset's skeleton, preview mesh, layer graphs, and state-machine summaries."""
    return _bridge("read_anim_blueprint_content", {
        "asset_path": asset_path,
    })


@mcp.tool()
def validate_anim_blueprint(asset_path: str) -> str:
    """Validate one AnimBlueprint for compile/readback health, skeleton compatibility, unresolved player assets, and asset-player binding sanity."""
    return _bridge("validate_anim_blueprint", {
        "asset_path": asset_path,
    })


@mcp.tool()
def create_anim_blueprint_asset(
    anim_blueprint_name: str,
    skeleton_path: Optional[str] = None,
    destination_path: str = "/Game/AnimBlueprints",
    parent_class_path: Optional[str] = None,
    preview_skeletal_mesh_path: Optional[str] = None,
    is_template: bool = False,
) -> str:
    """Create an AnimBlueprint asset for a skeleton and return deterministic inspection readback.

    Provide `skeleton_path` for regular assets. Omit it only when `is_template=True`.
    """
    payload: Dict[str, Any] = {
        "anim_blueprint_name": anim_blueprint_name,
        "destination_path": destination_path,
        "is_template": is_template,
    }
    if skeleton_path is not None:
        payload["skeleton_path"] = skeleton_path
    if parent_class_path is not None:
        payload["parent_class_path"] = parent_class_path
    if preview_skeletal_mesh_path is not None:
        payload["preview_skeletal_mesh_path"] = preview_skeletal_mesh_path
    return _bridge("create_anim_blueprint_asset", payload)


@mcp.tool()
def read_anim_state_machine(asset_path: str, state_machine_name: str) -> str:
    """Read one named state machine from an AnimBlueprint, including state and transition summaries."""
    return _bridge("read_anim_state_machine", {
        "asset_path": asset_path,
        "state_machine_name": state_machine_name,
    })


@mcp.tool()
def create_anim_state_machine(
    asset_path: str,
    state_machine_name: str,
    graph_name: Optional[str] = None,
    pos_x: Optional[int] = None,
    pos_y: Optional[int] = None,
) -> str:
    """Create a state machine node inside an AnimBlueprint's top-level animation graph."""
    payload: Dict[str, Any] = {
        "asset_path": asset_path,
        "state_machine_name": state_machine_name,
    }
    if graph_name is not None:
        payload["graph_name"] = graph_name
    if pos_x is not None:
        payload["pos_x"] = pos_x
    if pos_y is not None:
        payload["pos_y"] = pos_y
    return _bridge("create_anim_state_machine", payload)


@mcp.tool()
def create_anim_state(
    asset_path: str,
    state_machine_name: str,
    state_name: str,
    pos_x: Optional[int] = None,
    pos_y: Optional[int] = None,
) -> str:
    """Create one state inside a named AnimBlueprint state machine."""
    payload: Dict[str, Any] = {
        "asset_path": asset_path,
        "state_machine_name": state_machine_name,
        "state_name": state_name,
    }
    if pos_x is not None:
        payload["pos_x"] = pos_x
    if pos_y is not None:
        payload["pos_y"] = pos_y
    return _bridge("create_anim_state", payload)


@mcp.tool()
def rename_anim_state(
    asset_path: str,
    state_machine_name: str,
    state_name: str,
    new_state_name: str,
) -> str:
    """Rename one existing state inside a named AnimBlueprint state machine."""
    return _bridge("rename_anim_state", {
        "asset_path": asset_path,
        "state_machine_name": state_machine_name,
        "state_name": state_name,
        "new_state_name": new_state_name,
    })


@mcp.tool()
def delete_anim_state(asset_path: str, state_machine_name: str, state_name: str) -> str:
    """Delete one existing state from a named AnimBlueprint state machine."""
    return _bridge("delete_anim_state", {
        "asset_path": asset_path,
        "state_machine_name": state_machine_name,
        "state_name": state_name,
    })


@mcp.tool()
def set_anim_state_sequence_player(
    asset_path: str,
    state_machine_name: str,
    state_name: str,
    sequence_path: str,
) -> str:
    """Replace one state's bound graph with a single sequence-player node wired into the state result."""
    return _bridge("set_anim_state_sequence_player", {
        "asset_path": asset_path,
        "state_machine_name": state_machine_name,
        "state_name": state_name,
        "sequence_path": sequence_path,
    })


@mcp.tool()
def set_anim_state_blend_space_player(
    asset_path: str,
    state_machine_name: str,
    state_name: str,
    blend_space_path: str,
) -> str:
    """Replace one state's bound graph with a single blend-space or aim-offset player wired into the state result."""
    return _bridge("set_anim_state_blend_space_player", {
        "asset_path": asset_path,
        "state_machine_name": state_machine_name,
        "state_name": state_name,
        "blend_space_path": blend_space_path,
    })


@mcp.tool()
def set_anim_state_asset_player_parameters(
    asset_path: str,
    state_machine_name: str,
    state_name: str,
    loop: Optional[bool] = None,
    play_rate: Optional[float] = None,
    start_position: Optional[float] = None,
    blend_space_x: Optional[float] = None,
    blend_space_y: Optional[float] = None,
    sync_group_name: Optional[str] = None,
    sync_group_role: Optional[str] = None,
    sync_group_method: Optional[str] = None,
    sync_group_override_position_when_joining_sync_group_as_leader: Optional[bool] = None,
) -> str:
    """Mutate supported settings on a state's existing single asset-player node without rebinding its animation asset."""
    payload: Dict[str, Any] = {
        "asset_path": asset_path,
        "state_machine_name": state_machine_name,
        "state_name": state_name,
    }
    if loop is not None:
        payload["loop"] = loop
    if play_rate is not None:
        payload["play_rate"] = play_rate
    if start_position is not None:
        payload["start_position"] = start_position
    if blend_space_x is not None:
        payload["blend_space_x"] = blend_space_x
    if blend_space_y is not None:
        payload["blend_space_y"] = blend_space_y
    if sync_group_name is not None:
        payload["sync_group_name"] = sync_group_name
    if sync_group_role is not None:
        payload["sync_group_role"] = sync_group_role
    if sync_group_method is not None:
        payload["sync_group_method"] = sync_group_method
    if sync_group_override_position_when_joining_sync_group_as_leader is not None:
        payload["sync_group_override_position_when_joining_sync_group_as_leader"] = (
            sync_group_override_position_when_joining_sync_group_as_leader
        )
    return _bridge("set_anim_state_asset_player_parameters", payload)


@mcp.tool()
def create_anim_transition(
    asset_path: str,
    state_machine_name: str,
    source_state_name: str,
    target_state_name: str,
) -> str:
    """Create one directed transition between two existing states in a named AnimBlueprint state machine."""
    return _bridge("create_anim_transition", {
        "asset_path": asset_path,
        "state_machine_name": state_machine_name,
        "source_state_name": source_state_name,
        "target_state_name": target_state_name,
    })


@mcp.tool()
def delete_anim_transition(
    asset_path: str,
    state_machine_name: str,
    source_state_name: str,
    target_state_name: str,
) -> str:
    """Delete directed transition(s) between two existing states in a named AnimBlueprint state machine."""
    return _bridge("delete_anim_transition", {
        "asset_path": asset_path,
        "state_machine_name": state_machine_name,
        "source_state_name": source_state_name,
        "target_state_name": target_state_name,
    })


@mcp.tool()
def set_anim_transition_rule(
    asset_path: str,
    state_machine_name: str,
    source_state_name: str,
    target_state_name: str,
    rule_type: str,
    variable_name: Optional[str] = None,
    expected_value: Optional[Any] = None,
) -> str:
    """Apply one supported guard pattern to a named AnimBlueprint transition.

    Supported rule types: `always_true`, `bool_variable`, `int_equals`, `enum_equals`.
    """
    payload: Dict[str, Any] = {
        "asset_path": asset_path,
        "state_machine_name": state_machine_name,
        "source_state_name": source_state_name,
        "target_state_name": target_state_name,
        "rule_type": rule_type,
    }
    if variable_name is not None:
        payload["variable_name"] = variable_name
    if expected_value is not None:
        payload["expected_value"] = expected_value
    return _bridge("set_anim_transition_rule", payload)


@mcp.tool()
def analyze_blueprint_graph(blueprint_name: str) -> str:
    """List every node and connection in a Blueprint's event graph.

    Use this to discover existing node_ids before connecting or deleting nodes.
    """
    return _bridge("analyze_blueprint_graph", {
        "blueprint_path": _bp_path(blueprint_name),
        "blueprint_name": blueprint_name,
    })


@mcp.tool()
def get_blueprint_variable_details(blueprint_name: str, variable_name: str) -> str:
    """Inspect one Blueprint variable's type, default value, and flags."""
    return _bridge("get_blueprint_variable_details", {
        "blueprint_path": _bp_path(blueprint_name),
        "blueprint_name": blueprint_name,
        "variable_name": variable_name,
    })


@mcp.tool()
def get_blueprint_function_details(blueprint_name: str, function_name: str) -> str:
    """Inspect one Blueprint function's inputs, outputs, and graph nodes."""
    return _bridge("get_blueprint_function_details", {
        "blueprint_path": _bp_path(blueprint_name),
        "blueprint_name": blueprint_name,
        "function_name": function_name,
    })


# --------------------------------------------------------------------------- #
# Blueprint graph authoring (T3D paste)
# --------------------------------------------------------------------------- #

@mcp.tool()
def paste_blueprint_graph(
    blueprint_name: str,
    t3d_text: str,
    graph_name: Optional[str] = None,
    auto_compile: bool = True,
    clear_graph: bool = False,
) -> str:
    """Paste T3D-format Blueprint clipboard text into a Blueprint graph.

    This is the primary tool for authoring Blueprint logic. Generate the T3D
    text yourself (Begin Object / End Object blocks with CustomProperties Pin
    lines), then call this tool ONCE per graph. It uses the same import path
    Ctrl+V uses inside the editor.

    Pre-flight validation runs first and returns a structured
    `validation_errors` array if the T3D is malformed (duplicate NodeGuid,
    duplicate PinId, unbalanced Begin/End Object, LinkedTo references to
    unknown nodes/pins). Fix every error and resend the FULL T3D — do not
    partial-patch.

    On success the response includes `expected_node_count`,
    `expected_node_names`, `pasted_count`, and `missing_node_count` so you
    can verify the editor accepted everything.

    Args:
        blueprint_name: Asset name (e.g. 'BP_HealthActor') or full path.
        t3d_text: Complete T3D text. Every NodeGuid and PinId must be unique.
            Pin connections use 'LinkedTo=(NodeName PinId,...)' inside CustomProperties Pin entries.
        graph_name: Target graph (defaults to 'EventGraph'). Pass a function name for function graphs.
        auto_compile: Compile the Blueprint after pasting. Default True.
        clear_graph: Delete all existing nodes in the target graph first. Default False.
    """
    # A1 — pre-flight validation.
    parsed, errors = validate_t3d(t3d_text)
    if errors:
        return json.dumps({
            "status": "validation_failed",
            "phase": "pre_flight",
            "validation_errors": errors,
            "expected_node_count": len(parsed.nodes),
            "hint": "Fix the listed validation_errors and resend the full T3D. Do not partial-patch.",
        }, ensure_ascii=False)

    payload: Dict[str, Any] = {
        "blueprint_name": blueprint_name,
        "t3d_text": t3d_text,
        "auto_compile": auto_compile,
        "clear_graph": clear_graph,
    }
    if graph_name:
        payload["graph_name"] = graph_name

    # Reuse the bridge but bypass _bridge()'s json.dumps so we can post-process.
    try:
        result = send_command("paste_nodes_to_blueprint", payload)
    except BridgeError as exc:
        result = {"error": str(exc), "command": "paste_nodes_to_blueprint"}
    except Exception as exc:  # noqa: BLE001
        log.exception("Unexpected error in paste_blueprint_graph")
        result = {"error": f"Unexpected: {exc}", "command": "paste_nodes_to_blueprint"}

    # A2 — post-paste verification.
    if isinstance(result, dict) and result.get("status") == "success":
        expected = expected_node_names(parsed)
        pasted_count = int(result.get("pasted_count", 0) or 0)
        result["expected_node_count"] = len(expected)
        result["expected_node_names"] = expected
        result["missing_node_count"] = max(0, len(expected) - pasted_count)
        result["expected_link_count"] = len(parsed.links)

    text = json.dumps(result, ensure_ascii=False)
    if len(text) > LARGE_TRUNCATE_BYTES:
        text = text[:LARGE_TRUNCATE_BYTES] + f"\n\n[truncated at {LARGE_TRUNCATE_BYTES} bytes]"
    return text


# --------------------------------------------------------------------------- #
# T3D snippet library (Phase 5 / A4)
# --------------------------------------------------------------------------- #

@mcp.tool()
def list_t3d_snippets() -> str:
    """List the hand-curated T3D snippets available for `paste_blueprint_graph`.

    Each entry includes `name`, `description`, the snippet's exec/data pin
    names, and tags. Use `get_t3d_snippet(name)` to fetch the actual T3D body
    (with fresh GUIDs) before pasting.

    Recommended workflow:
      1. Call `list_t3d_snippets` to see what's available.
      2. Call `get_t3d_snippet(name)` once per node you want.
      3. Concatenate the returned `t3d_text` blocks, edit `LinkedTo=(...)`
         entries to wire pins together using each snippet's `node_name` +
         the relevant `pin_names`, and call `paste_blueprint_graph`.
    """
    return json.dumps({"snippets": list_snippets()}, ensure_ascii=False)


@mcp.tool()
def get_t3d_snippet(name: str, fresh_guids: bool = True) -> str:
    """Fetch one T3D snippet (NodeGuid + PinId rewritten with fresh GUIDs by default).

    Args:
        name: snippet name from `list_t3d_snippets`.
        fresh_guids: rewrite every 32-char hex GUID with a fresh value so the
            output is paste-safe. Set False only to inspect the canonical
            template form.

    Returns JSON with `t3d_text`, `node_name` (the importer-visible Name=
    used by LinkedTo refs), `pin_names`, and the snippet's exec pin metadata.
    """
    return json.dumps(get_snippet(name, fresh_guids=fresh_guids), ensure_ascii=False)


# --------------------------------------------------------------------------- #
# Granular Blueprint graph editing (Phase 5 / Workstream B)
# Use these as a fallback when `paste_blueprint_graph` is awkward — for
# example, surgical edits to an existing graph or when you want one node at a
# time. All of these mutate a Blueprint and return the resulting node/pin
# identifiers so you can chain calls without guessing.
# --------------------------------------------------------------------------- #

@mcp.tool()
def add_blueprint_node(
    blueprint_name: str,
    node_type: str,
    function_name: Optional[str] = None,
    node_params: Optional[Dict[str, Any]] = None,
    node_position: Optional[List[float]] = None,
) -> str:
    """Add a single node to a Blueprint graph and return its identifiers.

    Recommended fallback path when T3D paste is too awkward for a small edit.

    Args:
        blueprint_name: Asset name (e.g. 'BP_HealthActor') or full path.
        node_type: One of: 'Branch', 'Comparison', 'Switch', 'SwitchEnum',
            'SwitchInteger', 'ExecutionSequence', 'VariableGet', 'VariableSet',
            'MakeArray', 'Print', 'CallFunction', 'Select', 'SpawnActor',
            'DynamicCast', 'ClassDynamicCast', 'CastByteToEnum', 'Timeline',
            'GetDataTableRow', 'AddComponentByClass', 'Self', 'ConstructObject'.
        function_name: Pass to target a function graph instead of EventGraph.
        node_params: Type-specific parameters (e.g. for CallFunction:
            {"target_function": "PrintString", "target_class": "/Script/Engine.KismetSystemLibrary"};
            for VariableGet/Set: {"variable_name": "MyVar"}). Place
            'function_name' inside this dict if your node belongs to a
            function graph.
        node_position: Optional [x, y].

    Returns the created node's name, GUID, and full pin list — feed this into
    `connect_blueprint_nodes` next.
    """
    np: Dict[str, Any] = dict(node_params or {})
    if function_name and "function_name" not in np:
        np["function_name"] = function_name
    if node_position and "node_position" not in np:
        np["node_position"] = list(node_position)
    return _bridge("add_blueprint_node", {
        "blueprint_name": blueprint_name,
        "node_type": node_type,
        "node_params": np,
    })


@mcp.tool()
def connect_blueprint_nodes(
    blueprint_name: str,
    source_node_id: str,
    source_pin_name: str,
    target_node_id: str,
    target_pin_name: str,
) -> str:
    """Wire one pin to another in a Blueprint graph.

    Use the `name` returned by `add_blueprint_node` (or visible in
    `analyze_blueprint_graph`) as the node ID. Pin names come from the same
    source — hex PinIds are NOT used here.
    """
    return _bridge("connect_nodes", {
        "blueprint_name": blueprint_name,
        "source_node_id": source_node_id,
        "source_pin_name": source_pin_name,
        "target_node_id": target_node_id,
        "target_pin_name": target_pin_name,
    })


@mcp.tool()
def delete_blueprint_node(blueprint_name: str, node_id: str) -> str:
    """Delete a single node from a Blueprint graph by its node name/ID."""
    return _bridge("delete_node", {
        "blueprint_name": blueprint_name,
        "node_id": node_id,
    })


@mcp.tool()
def add_event_node(blueprint_name: str, event_name: str) -> str:
    """Add an overridable event entry node (e.g. 'ReceiveBeginPlay', 'ReceiveTick').

    For class-specific events not on the parent class, use `add_blueprint_node`
    with `node_type='CallFunction'` instead.
    """
    return _bridge("add_event_node", {
        "blueprint_name": blueprint_name,
        "event_name": event_name,
    })


@mcp.tool()
def set_node_default(
    blueprint_name: str,
    node_id: str,
    pin_name: str,
    default_value: str,
) -> str:
    """Set the default value on an input pin of an existing node.

    Use the `set_pin_default_value` action under the hood. `default_value` must
    be the string form Unreal expects for the pin type (e.g. '"Hello"' for
    string, '1.5' for float, 'true' for bool, '(X=0,Y=0,Z=0)' for vector).
    """
    return _bridge("set_node_property", {
        "blueprint_name": blueprint_name,
        "node_id": node_id,
        "action": "set_pin_default_value",
        "pin_name": pin_name,
        "default_value": default_value,
    })


@mcp.tool()
def create_blueprint_variable(
    blueprint_name: str,
    variable_name: str,
    variable_type: str,
    default_value: Optional[str] = None,
    is_exposed: Optional[bool] = None,
) -> str:
    """Create a member variable on a Blueprint.

    Args:
        blueprint_name: Asset name.
        variable_name: New variable's name.
        variable_type: One of 'bool', 'int', 'float', 'string', 'name', 'text',
            'vector', 'rotator', 'transform', an enum path like
            '/Script/Engine.EMontagePlayReturnType', or an object/class path like
            '/Script/Engine.Actor'.
        default_value: Optional initial value (string form).
        is_exposed: Whether to expose the variable on the Details panel.
    """
    payload: Dict[str, Any] = {
        "blueprint_name": blueprint_name,
        "variable_name": variable_name,
        "variable_type": variable_type,
    }
    if default_value is not None:
        payload["default_value"] = default_value
    if is_exposed is not None:
        payload["is_exposed"] = is_exposed
    return _bridge("create_variable", payload)


@mcp.tool()
def create_blueprint_function(
    blueprint_name: str,
    function_name: str,
    return_type: Optional[str] = None,
) -> str:
    """Create a new user-defined function graph on a Blueprint.

    Args:
        blueprint_name: Asset name (e.g. 'BP_MyActor').
        function_name: Name for the new function (no spaces or special chars).
        return_type: Optional return type string. Defaults to void when omitted.
    """
    payload: Dict[str, Any] = {
        "blueprint_name": blueprint_name,
        "function_name": function_name,
    }
    if return_type is not None:
        payload["return_type"] = return_type
    return _bridge("create_function", payload)


@mcp.tool()
def set_blueprint_variable_properties(
    blueprint_name: str,
    variable_name: str,
    new_variable_name: Optional[str] = None,
    variable_type: Optional[str] = None,
    default_value: Optional[str] = None,
    is_blueprint_writable: Optional[bool] = None,
    is_public: Optional[bool] = None,
    is_editable_in_instance: Optional[bool] = None,
    is_config: Optional[bool] = None,
    friendly_name: Optional[str] = None,
    tooltip: Optional[str] = None,
    category: Optional[str] = None,
    replication_enabled: Optional[bool] = None,
    replication_condition: Optional[int] = None,
    is_private: Optional[bool] = None,
    expose_on_spawn: Optional[bool] = None,
    expose_to_cinematics: Optional[bool] = None,
    slider_range_min: Optional[str] = None,
    slider_range_max: Optional[str] = None,
    value_range_min: Optional[str] = None,
    value_range_max: Optional[str] = None,
    units: Optional[str] = None,
    bitmask: Optional[bool] = None,
    bitmask_enum: Optional[str] = None,
) -> str:
    """Update metadata and flags for an existing Blueprint member variable.

    Pass only the fields you want to change. This wrapper exposes the existing
    C++ variable-mutation handler without forcing callers to construct the raw
    JSON payload by hand.
    """
    payload: Dict[str, Any] = {
        "blueprint_name": blueprint_name,
        "variable_name": variable_name,
    }
    if new_variable_name is not None:
        payload["var_name"] = new_variable_name
    if variable_type is not None:
        payload["var_type"] = variable_type
    if default_value is not None:
        payload["default_value"] = default_value
    if is_blueprint_writable is not None:
        payload["is_blueprint_writable"] = is_blueprint_writable
    if is_public is not None:
        payload["is_public"] = is_public
    if is_editable_in_instance is not None:
        payload["is_editable_in_instance"] = is_editable_in_instance
    if is_config is not None:
        payload["is_config"] = is_config
    if friendly_name is not None:
        payload["friendly_name"] = friendly_name
    if tooltip is not None:
        payload["tooltip"] = tooltip
    if category is not None:
        payload["category"] = category
    if replication_enabled is not None:
        payload["replication_enabled"] = replication_enabled
    if replication_condition is not None:
        payload["replication_condition"] = replication_condition
    if is_private is not None:
        payload["is_private"] = is_private
    if expose_on_spawn is not None:
        payload["expose_on_spawn"] = expose_on_spawn
    if expose_to_cinematics is not None:
        payload["expose_to_cinematics"] = expose_to_cinematics
    if slider_range_min is not None:
        payload["slider_range_min"] = slider_range_min
    if slider_range_max is not None:
        payload["slider_range_max"] = slider_range_max
    if value_range_min is not None:
        payload["value_range_min"] = value_range_min
    if value_range_max is not None:
        payload["value_range_max"] = value_range_max
    if units is not None:
        payload["units"] = units
    if bitmask is not None:
        payload["bitmask"] = bitmask
    if bitmask_enum is not None:
        payload["bitmask_enum"] = bitmask_enum
    return _bridge("set_blueprint_variable_properties", payload)


@mcp.tool()
def add_blueprint_function_input(
    blueprint_name: str,
    function_name: str,
    param_name: str,
    param_type: str,
    is_array: bool = False,
) -> str:
    """Add one input parameter to an existing Blueprint function."""
    return _bridge("add_function_input", {
        "blueprint_name": blueprint_name,
        "function_name": function_name,
        "param_name": param_name,
        "param_type": param_type,
        "is_array": is_array,
    })


@mcp.tool()
def add_blueprint_function_output(
    blueprint_name: str,
    function_name: str,
    param_name: str,
    param_type: str,
    is_array: bool = False,
) -> str:
    """Add one output parameter to an existing Blueprint function."""
    return _bridge("add_function_output", {
        "blueprint_name": blueprint_name,
        "function_name": function_name,
        "param_name": param_name,
        "param_type": param_type,
        "is_array": is_array,
    })


@mcp.tool()
def delete_blueprint_function(
    blueprint_name: str,
    function_name: str,
) -> str:
    """Delete a user-defined Blueprint function graph by name."""
    return _bridge("delete_function", {
        "blueprint_name": blueprint_name,
        "function_name": function_name,
    })


@mcp.tool()
def rename_blueprint_function(
    blueprint_name: str,
    old_function_name: str,
    new_function_name: str,
) -> str:
    """Rename a user-defined Blueprint function graph."""
    return _bridge("rename_function", {
        "blueprint_name": blueprint_name,
        "old_function_name": old_function_name,
        "new_function_name": new_function_name,
    })


# --------------------------------------------------------------------------- #
# Phase 7 — Materials, Physics, Mesh, Spawning
# --------------------------------------------------------------------------- #

@mcp.tool()
def spawn_blueprint_actor(
    blueprint_name: str,
    actor_name: str,
    location: Optional[List[float]] = None,
    rotation: Optional[List[float]] = None,
) -> str:
    """Spawn an instance of a Blueprint into the current editor world.

    Args:
        blueprint_name: Blueprint asset name (e.g. 'BP_MyActor').
        actor_name: Label for the new actor in the level.
        location: Optional [X, Y, Z] world location.
        rotation: Optional [Pitch, Yaw, Roll] rotation in degrees.
    """
    payload: Dict[str, Any] = {
        "blueprint_name": blueprint_name,
        "actor_name": actor_name,
    }
    if location is not None:
        payload["location"] = location
    if rotation is not None:
        payload["rotation"] = rotation
    return _bridge("spawn_blueprint_actor", payload)


@mcp.tool()
def set_physics_properties(
    blueprint_name: str,
    component_name: str,
    simulate_physics: Optional[bool] = None,
    mass: Optional[float] = None,
    linear_damping: Optional[float] = None,
    angular_damping: Optional[float] = None,
) -> str:
    """Set physics properties on a primitive component inside a Blueprint.

    Pass only the properties you want to change.
    """
    payload: Dict[str, Any] = {
        "blueprint_name": blueprint_name,
        "component_name": component_name,
    }
    if simulate_physics is not None:
        payload["simulate_physics"] = simulate_physics
    if mass is not None:
        payload["mass"] = mass
    if linear_damping is not None:
        payload["linear_damping"] = linear_damping
    if angular_damping is not None:
        payload["angular_damping"] = angular_damping
    return _bridge("set_physics_properties", payload)


@mcp.tool()
def set_static_mesh_properties(
    blueprint_name: str,
    component_name: str,
    static_mesh: Optional[str] = None,
    material: Optional[str] = None,
) -> str:
    """Set the static mesh and/or first material of a StaticMeshComponent in a Blueprint.

    Args:
        blueprint_name: Blueprint asset name.
        component_name: Name of the StaticMeshComponent inside the BP.
        static_mesh: Asset path (e.g. '/Engine/BasicShapes/Cube').
        material: Material asset path applied to slot 0.
    """
    payload: Dict[str, Any] = {
        "blueprint_name": blueprint_name,
        "component_name": component_name,
    }
    if static_mesh is not None:
        payload["static_mesh"] = static_mesh
    if material is not None:
        payload["material"] = material
    return _bridge("set_static_mesh_properties", payload)


@mcp.tool()
def set_mesh_material_color(
    blueprint_name: str,
    component_name: str,
    color: List[float],
    material_slot: int = 0,
    parameter_name: str = "BaseColor",
    material_path: Optional[str] = None,
) -> str:
    """Create a dynamic material instance and tint a primitive component's slot.

    Args:
        color: [R, G, B, A] floats in 0..1.
        material_path: Optional source material; defaults to existing slot
            material or '/Engine/BasicShapes/BasicShapeMaterial'.
    """
    if len(color) != 4:
        return json.dumps({"status": "error",
                           "error": "color must be [R, G, B, A]"})
    payload: Dict[str, Any] = {
        "blueprint_name": blueprint_name,
        "component_name": component_name,
        "color": color,
        "material_slot": material_slot,
        "parameter_name": parameter_name,
    }
    if material_path is not None:
        payload["material_path"] = material_path
    return _bridge("set_mesh_material_color", payload)


@mcp.tool()
def apply_material_to_actor(
    actor_name: str,
    material_path: str,
    material_slot: int = 0,
) -> str:
    """Apply a material asset to every static mesh component of a level actor."""
    return _bridge("apply_material_to_actor", {
        "actor_name": actor_name,
        "material_path": material_path,
        "material_slot": material_slot,
    })


@mcp.tool()
def apply_material_to_blueprint(
    blueprint_name: str,
    component_name: str,
    material_path: str,
    material_slot: int = 0,
) -> str:
    """Apply a material asset to a primitive component inside a Blueprint."""
    return _bridge("apply_material_to_blueprint", {
        "blueprint_name": blueprint_name,
        "component_name": component_name,
        "material_path": material_path,
        "material_slot": material_slot,
    })


@mcp.tool()
def get_actor_material_info(actor_name: str) -> str:
    """Return material slots of all static mesh components on a level actor."""
    return _bridge("get_actor_material_info", {"actor_name": actor_name})


@mcp.tool()
def get_blueprint_material_info(
    blueprint_name: str,
    component_name: str,
) -> str:
    """Return material slot info for a primitive component inside a Blueprint."""
    return _bridge("get_blueprint_material_info", {
        "blueprint_name": blueprint_name,
        "component_name": component_name,
    })


# --------------------------------------------------------------------------- #
# Phase 7 Wave 2 \u2014 Asset registry, material instances, asset import
# --------------------------------------------------------------------------- #

@mcp.tool()
def find_assets(
    query: str = "",
    class_name: str = "",
    path: str = "/Game",
    recursive: bool = True,
    max_results: int = 200,
) -> str:
    """Search the editor's asset registry.

    Args:
        query: Substring matched case-insensitively against asset names. Empty
            returns everything in the path/class filter.
        class_name: Optional UObject class short name (e.g. 'Material',
            'StaticMesh', 'Blueprint', 'Texture2D').
        path: Content-browser folder to scan (default '/Game').
        recursive: Recurse into subfolders.
        max_results: Cap on returned items (default 200).
    """
    return _bridge("find_assets", {
        "query": query,
        "class_name": class_name,
        "path": path,
        "recursive": recursive,
        "max_results": max_results,
    })


@mcp.tool()
def read_data_table_content(asset_path: str) -> str:
    """Read a DataTable asset's row struct, columns, row names, and exported row data."""
    return _bridge("read_data_table_content", {
        "asset_path": asset_path,
    })


@mcp.tool()
def read_data_table_row(asset_path: str, row_name: str) -> str:
    """Read one DataTable row by name, including the exported row payload and resolved key field."""
    return _bridge("read_data_table_row", {
        "asset_path": asset_path,
        "row_name": row_name,
    })


@mcp.tool()
def read_curve_table_content(asset_path: str) -> str:
    """Read a CurveTable asset's curve mode, row names, and exported curve payloads."""
    return _bridge("read_curve_table_content", {
        "asset_path": asset_path,
    })


@mcp.tool()
def read_curve_table_row(asset_path: str, row_name: str) -> str:
    """Read one CurveTable row by name, including the exported row payload."""
    return _bridge("read_curve_table_row", {
        "asset_path": asset_path,
        "row_name": row_name,
    })


@mcp.tool()
def create_curve_table_asset(
    curve_table_name: str,
    curve_table_mode: str,
    destination_path: str = "/Game/CurveTables",
) -> str:
    """Create a new empty CurveTable asset for an explicit simple or rich curve-table mode."""
    return _bridge("create_curve_table_asset", {
        "curve_table_name": curve_table_name,
        "curve_table_mode": curve_table_mode,
        "destination_path": destination_path,
    })


@mcp.tool()
def upsert_curve_table_row(asset_path: str, row_name: str, row_data: Dict[str, Any]) -> str:
    """Create or replace one CurveTable row using numeric time/value JSON fields."""
    return _bridge("upsert_curve_table_row", {
        "asset_path": asset_path,
        "row_name": row_name,
        "row_data": row_data,
    })


@mcp.tool()
def delete_curve_table_row(asset_path: str, row_name: str) -> str:
    """Delete one CurveTable row by name and return deterministic post-delete table state."""
    return _bridge("delete_curve_table_row", {
        "asset_path": asset_path,
        "row_name": row_name,
    })


@mcp.tool()
def rename_curve_table_row(asset_path: str, row_name: str, new_row_name: str) -> str:
    """Rename one CurveTable row by name and return deterministic post-rename row state."""
    return _bridge("rename_curve_table_row", {
        "asset_path": asset_path,
        "row_name": row_name,
        "new_row_name": new_row_name,
    })


@mcp.tool()
def validate_data_table_row_import(asset_path: str, row_name: str, row_data: Dict[str, Any]) -> str:
    """Validate DataTable row import payload shape without mutating the asset."""
    return _bridge("validate_data_table_row_import", {
        "asset_path": asset_path,
        "row_name": row_name,
        "row_data": row_data,
    })


@mcp.tool()
def validate_curve_table_row_import(asset_path: str, row_name: str, row_data: Dict[str, Any]) -> str:
    """Validate CurveTable row import payload shape without mutating the asset."""
    return _bridge("validate_curve_table_row_import", {
        "asset_path": asset_path,
        "row_name": row_name,
        "row_data": row_data,
    })


@mcp.tool()
def create_data_table_asset(
    data_table_name: str,
    row_struct_path: str,
    destination_path: str = "/Game/DataTables",
) -> str:
    """Create a new empty DataTable asset for a given row struct."""
    return _bridge("create_data_table_asset", {
        "data_table_name": data_table_name,
        "row_struct_path": row_struct_path,
        "destination_path": destination_path,
    })


@mcp.tool()
def upsert_data_table_row(asset_path: str, row_name: str, row_data: Dict[str, Any]) -> str:
    """Create or replace one DataTable row using a JSON-compatible row payload."""
    return _bridge("upsert_data_table_row", {
        "asset_path": asset_path,
        "row_name": row_name,
        "row_data": row_data,
    })


@mcp.tool()
def delete_data_table_row(asset_path: str, row_name: str) -> str:
    """Delete one DataTable row by name and return deterministic post-delete table state."""
    return _bridge("delete_data_table_row", {
        "asset_path": asset_path,
        "row_name": row_name,
    })


@mcp.tool()
def rename_data_table_row(asset_path: str, row_name: str, new_row_name: str) -> str:
    """Rename one DataTable row by name and return deterministic post-rename row state."""
    return _bridge("rename_data_table_row", {
        "asset_path": asset_path,
        "row_name": row_name,
        "new_row_name": new_row_name,
    })


@mcp.tool()
def duplicate_data_table_row(asset_path: str, source_row_name: str, new_row_name: str) -> str:
    """Duplicate one DataTable row and return deterministic readback for the new row."""
    return _bridge("duplicate_data_table_row", {
        "asset_path": asset_path,
        "source_row_name": source_row_name,
        "new_row_name": new_row_name,
    })


@mcp.tool()
def move_data_table_row(
    asset_path: str,
    row_name: str,
    direction: str,
    num_rows_to_move_by: int = 1,
) -> str:
    """Move one DataTable row up or down and return deterministic post-move table order."""
    return _bridge("move_data_table_row", {
        "asset_path": asset_path,
        "row_name": row_name,
        "direction": direction,
        "num_rows_to_move_by": num_rows_to_move_by,
    })


@mcp.tool()
def get_asset_dependencies(asset_path: str) -> str:
    """Return packages that the given asset depends on (hard + soft references)."""
    return _bridge("get_asset_dependencies", {"asset_path": asset_path})


@mcp.tool()
def get_referencers(asset_path: str) -> str:
    """Return packages that reference the given asset."""
    return _bridge("get_referencers", {"asset_path": asset_path})


@mcp.tool()
def create_material_instance(
    parent_material: str,
    instance_name: str,
    dest_path: str = "/Game/Materials",
    scalar_parameters: Optional[Dict[str, float]] = None,
    vector_parameters: Optional[Dict[str, List[float]]] = None,
) -> str:
    """Create a UMaterialInstanceConstant asset deriving from `parent_material`.

    Args:
        parent_material: Object path to the parent material (e.g.
            '/Engine/BasicShapes/BasicShapeMaterial').
        instance_name: Asset name for the new instance.
        dest_path: Content-browser folder to place it in.
        scalar_parameters: Optional {ParamName: float} overrides.
        vector_parameters: Optional {ParamName: [R,G,B,A]} overrides.
    """
    payload: Dict[str, Any] = {
        "parent_material": parent_material,
        "instance_name": instance_name,
        "dest_path": dest_path,
    }
    if scalar_parameters:
        payload["scalar_parameters"] = scalar_parameters
    if vector_parameters:
        payload["vector_parameters"] = vector_parameters
    return _bridge("create_material_instance", payload)


@mcp.tool()
def import_asset(source_path: str, dest_path: str) -> str:
    """Import a file (FBX, PNG, WAV, etc.) into the project content tree.

    Args:
        source_path: Absolute path on the local filesystem.
        dest_path: Content-browser destination folder (e.g. '/Game/Imported').
    """
    return _bridge("import_asset", {
        "source_path": source_path,
        "dest_path": dest_path,
    })


# --------------------------------------------------------------------------- #
# Phase 7 Wave 3a — level design helpers
# --------------------------------------------------------------------------- #

@mcp.tool()
def new_blank_map(save_existing_map: bool = False) -> str:
    """Create and switch to a new blank map in the editor."""
    return _bridge("new_blank_map", {
        "save_existing_map": save_existing_map,
    })


@mcp.tool()
def open_level(asset_path: str, save_current_level: bool = False) -> str:
    """Open an existing map asset in the editor.

    Args:
        asset_path: Long package path or object path for the map, e.g.
            '/Game/Maps/iam_underTesting'. If you only know the map name, use
            `find_assets(query=..., class_name="World")` first.
        save_current_level: When true, save dirty map packages before loading.
    """
    return _bridge("open_level", {
        "asset_path": asset_path,
        "save_current_level": save_current_level,
    })


@mcp.tool()
def snap_actors_to_grid(
    actor_names: list[str],
    grid_size: float = 100.0,
    snap_rotation: bool = False,
    rotation_grid: float = 15.0,
) -> str:
    """Snap actors' world locations (and optionally rotations) to a grid.

    Args:
        actor_names: List of actor labels or internal names.
        grid_size: World units per grid step (default 100).
        snap_rotation: When true, also rounds rotation to `rotation_grid` degrees.
        rotation_grid: Degrees per rotation step (default 15).
    """
    return _bridge("snap_actors_to_grid", {
        "actor_names": actor_names,
        "grid_size": grid_size,
        "snap_rotation": snap_rotation,
        "rotation_grid": rotation_grid,
    })


@mcp.tool()
def align_actors(actor_names: list[str], axis: str = "z", mode: str = "min") -> str:
    """Align two or more actors along a single world axis.

    Args:
        actor_names: At least two actor labels/names.
        axis: 'x', 'y', or 'z'.
        mode: 'min', 'max', 'center', or 'average'. Determines the target value
            on the chosen axis (e.g. 'min' aligns everything to the lowest Z).
    """
    return _bridge("align_actors", {
        "actor_names": actor_names,
        "axis": axis,
        "mode": mode,
    })


@mcp.tool()
def duplicate_actor(
    actor_name: str,
    new_name: str = "",
    offset_location: list[float] | None = None,
) -> str:
    """Duplicate an actor in the current level, optionally offset and renamed.

    Args:
        actor_name: Source actor label or internal name.
        new_name: Optional label for the duplicate (defaults to engine-assigned).
        offset_location: [x, y, z] world-space offset applied to the duplicate.
    """
    payload: dict = {"actor_name": actor_name}
    if new_name:
        payload["new_name"] = new_name
    if offset_location is not None:
        payload["offset_location"] = offset_location
    return _bridge("duplicate_actor", payload)


@mcp.tool()
def focus_viewport(actor_name: str = "", location: list[float] | None = None) -> str:
    """Move all level viewports to focus on an actor or a world location.

    Provide either `actor_name` (frames the actor) or `location` (just moves the
    camera). Exactly one must be supplied.
    """
    payload: dict = {}
    if actor_name:
        payload["actor_name"] = actor_name
    if location is not None:
        payload["location"] = location
    return _bridge("focus_viewport", payload)


@mcp.tool()
def save_level() -> str:
    """Save the current level package to disk."""
    return _bridge("save_level", {})


@mcp.tool()
def undo_last_action() -> str:
    """Undo the most recent editor transaction created through UnrealAI or the editor itself."""
    return _bridge("undo_last_action", {})


@mcp.tool()
def redo_last_action() -> str:
    """Redo the most recently undone editor transaction."""
    return _bridge("redo_last_action", {})


@mcp.tool()
def capture_viewport_screenshot(
    file_path: str = "",
    image_format: Literal["png"] = "png",
) -> str:
    """Capture the active editor viewport to a PNG file and return the saved path."""
    payload: Dict[str, Any] = {"image_format": image_format}
    if file_path:
        payload["file_path"] = file_path
    return _bridge("capture_viewport_screenshot", payload)


@mcp.tool()
def place_in_grid(
    actor_type: str,
    name_prefix: str,
    origin: list[float],
    count_x: int,
    step_x: float,
    count_y: int = 1,
    step_y: float = 200.0,
    rotation: list[float] | None = None,
    scale: list[float] | None = None,
    static_mesh: str = "",
) -> str:
    """Spawn a repeated actor pattern on a regular world-space grid."""
    payload: Dict[str, Any] = {
        "actor_type": actor_type,
        "name_prefix": name_prefix,
        "origin": origin,
        "count_x": count_x,
        "step_x": step_x,
        "count_y": count_y,
        "step_y": step_y,
    }
    if rotation is not None:
        payload["rotation"] = rotation
    if scale is not None:
        payload["scale"] = scale
    if static_mesh:
        payload["static_mesh"] = static_mesh
    return _bridge("place_in_grid", payload)


@mcp.tool()
def place_in_circle(
    actor_type: str,
    name_prefix: str,
    center: list[float],
    radius: float,
    count: int,
    start_angle_degrees: float = 0.0,
    rotation: list[float] | None = None,
    scale: list[float] | None = None,
    static_mesh: str = "",
) -> str:
    """Spawn actors around a circular pattern in world space."""
    payload: Dict[str, Any] = {
        "actor_type": actor_type,
        "name_prefix": name_prefix,
        "center": center,
        "radius": radius,
        "count": count,
        "start_angle_degrees": start_angle_degrees,
    }
    if rotation is not None:
        payload["rotation"] = rotation
    if scale is not None:
        payload["scale"] = scale
    if static_mesh:
        payload["static_mesh"] = static_mesh
    return _bridge("place_in_circle", payload)


@mcp.tool()
def place_along_spline(
    actor_type: str,
    name_prefix: str,
    spline_actor_name: str,
    count: int,
    orientation_mode: Literal["none", "yaw", "full"] = "yaw",
    rotation: list[float] | None = None,
    scale: list[float] | None = None,
    static_mesh: str = "",
) -> str:
    """Spawn actors at evenly spaced points along a spline actor's first spline component."""
    payload: Dict[str, Any] = {
        "actor_type": actor_type,
        "name_prefix": name_prefix,
        "spline_actor_name": spline_actor_name,
        "count": count,
        "orientation_mode": orientation_mode,
    }
    if rotation is not None:
        payload["rotation"] = rotation
    if scale is not None:
        payload["scale"] = scale
    if static_mesh:
        payload["static_mesh"] = static_mesh
    return _bridge("place_along_spline", payload)


@mcp.tool()
def scatter_in_area(
    actor_type: str,
    name_prefix: str,
    min_corner: list[float],
    max_corner: list[float],
    count: int,
    seed: int = 12345,
    shape: Literal["box", "ellipse"] = "box",
    rotation: list[float] | None = None,
    scale: list[float] | None = None,
    static_mesh: str = "",
) -> str:
    """Spawn actors at pseudo-random locations inside a box or ellipse-shaped area."""
    payload: Dict[str, Any] = {
        "actor_type": actor_type,
        "name_prefix": name_prefix,
        "min_corner": min_corner,
        "max_corner": max_corner,
        "count": count,
        "seed": seed,
        "shape": shape,
    }
    if rotation is not None:
        payload["rotation"] = rotation
    if scale is not None:
        payload["scale"] = scale
    if static_mesh:
        payload["static_mesh"] = static_mesh
    return _bridge("scatter_in_area", payload)


# --------------------------------------------------------------------------- #
# Phase 7 Wave 3b — landscape helpers (initial slice)
# --------------------------------------------------------------------------- #

@mcp.tool()
def get_landscapes() -> str:
    """List landscape actors in the current level with size and transform data."""
    return _bridge("get_landscapes", {})


@mcp.tool()
def read_landscape_content(landscape_name: str) -> str:
    """Inspect one landscape's edit-layer, target-layer, and layer-info metadata."""
    return _bridge("read_landscape_content", {
        "landscape_name": landscape_name,
    })


@mcp.tool()
def sample_landscape_point(landscape_name: str, location: list[float]) -> str:
    """Sample one landscape at a world location for height and layer weights."""
    return _bridge("sample_landscape_point", {
        "landscape_name": landscape_name,
        "location": location,
    })


@mcp.tool()
def sample_landscape_points(landscape_name: str, locations: list[list[float]]) -> str:
    """Sample one landscape at multiple world locations for height and layer weights."""
    return _bridge("sample_landscape_points", {
        "landscape_name": landscape_name,
        "locations": locations,
    })


@mcp.tool()
def sample_landscape_grid(
    landscape_name: str,
    origin: list[float],
    step_x: float,
    step_y: float,
    count_x: int,
    count_y: int,
) -> str:
    """Sample one landscape over a regular world-space grid."""
    return _bridge("sample_landscape_grid", {
        "landscape_name": landscape_name,
        "origin": origin,
        "step_x": step_x,
        "step_y": step_y,
        "count_x": count_x,
        "count_y": count_y,
    })


@mcp.tool()
def sample_landscape_region(
    landscape_name: str,
    min_corner: list[float],
    max_corner: list[float],
    step_x: float,
    step_y: float,
) -> str:
    """Sample one landscape over a bounded world-space region."""
    return _bridge("sample_landscape_region", {
        "landscape_name": landscape_name,
        "min_corner": min_corner,
        "max_corner": max_corner,
        "step_x": step_x,
        "step_y": step_y,
    })


@mcp.tool()
def sample_landscape_height_region(
    landscape_name: str,
    min_corner: list[float],
    max_corner: list[float],
    step_x: float,
    step_y: float,
) -> str:
    """Sample one landscape height raster over a bounded world-space region."""
    return _bridge("sample_landscape_height_region", {
        "landscape_name": landscape_name,
        "min_corner": min_corner,
        "max_corner": max_corner,
        "step_x": step_x,
        "step_y": step_y,
    })


@mcp.tool()
def sample_landscape_weight_region(
    landscape_name: str,
    layer_name: str,
    min_corner: list[float],
    max_corner: list[float],
    step_x: float,
    step_y: float,
) -> str:
    """Sample one landscape layer over a bounded region as a dense weight raster."""
    return _bridge("sample_landscape_weight_region", {
        "landscape_name": landscape_name,
        "layer_name": layer_name,
        "min_corner": min_corner,
        "max_corner": max_corner,
        "step_x": step_x,
        "step_y": step_y,
    })


@mcp.tool()
def paint_landscape_layer_region(
    landscape_name: str,
    layer_name: str,
    min_corner: list[float],
    max_corner: list[float],
    weight: float = 1.0,
) -> str:
    """Paint one landscape layer over a bounded region with a uniform weight."""
    return _bridge("paint_landscape_layer_region", {
        "landscape_name": landscape_name,
        "layer_name": layer_name,
        "min_corner": min_corner,
        "max_corner": max_corner,
        "weight": weight,
    })


@mcp.tool()
def sculpt_landscape_height_region(
    landscape_name: str,
    min_corner: list[float],
    max_corner: list[float],
    height_world: float,
) -> str:
    """Sculpt one landscape region to a uniform world height."""
    return _bridge("sculpt_landscape_height_region", {
        "landscape_name": landscape_name,
        "min_corner": min_corner,
        "max_corner": max_corner,
        "height_world": height_world,
    })


@mcp.tool()
def rebuild_landscape(landscape_name: str) -> str:
    """Force a landscape layer and render rebuild pass."""
    return _bridge("rebuild_landscape", {
        "landscape_name": landscape_name,
    })


@mcp.tool()
def create_landscape(
    name: str = "",
    location: list[float] | None = None,
    rotation: list[float] | None = None,
    scale: list[float] | None = None,
    component_count_x: int = 4,
    component_count_y: int = 4,
    sections_per_component: int = 1,
    quads_per_section: int = 63,
    base_height: float = 0.0,
    material_path: str = "",
) -> str:
    """Create a new flat landscape actor.

    Args:
        name: Optional landscape label.
        location: Optional [x, y, z] world location for the landscape center.
        rotation: Optional [pitch, yaw, roll] rotation.
        scale: Optional [x, y, z] scale. Default is [100, 100, 100].
        component_count_x: Number of components along X.
        component_count_y: Number of components along Y.
        sections_per_component: Usually 1 or 2.
        quads_per_section: Usually 63 or 127.
        base_height: Flat starting height in world units.
        material_path: Optional landscape material asset path.
    """
    payload: dict = {
        "component_count_x": component_count_x,
        "component_count_y": component_count_y,
        "sections_per_component": sections_per_component,
        "quads_per_section": quads_per_section,
        "base_height": base_height,
    }
    if name:
        payload["name"] = name
    if location is not None:
        payload["location"] = location
    if rotation is not None:
        payload["rotation"] = rotation
    if scale is not None:
        payload["scale"] = scale
    if material_path:
        payload["material_path"] = material_path
    return _bridge("create_landscape", payload)


@mcp.tool()
def set_landscape_flat_height(landscape_name: str, height_world: float = 0.0) -> str:
    """Set the entire landscape heightmap to one flat world-space height."""
    return _bridge("set_landscape_flat_height", {
        "landscape_name": landscape_name,
        "height_world": height_world,
    })


@mcp.tool()
def import_landscape_heightmap(
    landscape_name: str,
    source_path: str,
    flip_y_axis: bool = False,
) -> str:
    """Import a heightmap file into an existing landscape.

    Supports editor-recognized heightmap formats such as `.r16`, `.raw`, and
    other formats handled by UE's landscape import pipeline.
    """
    return _bridge("import_landscape_heightmap", {
        "landscape_name": landscape_name,
        "source_path": source_path,
        "flip_y_axis": flip_y_axis,
    })


# --------------------------------------------------------------------------- #
# Entry point
# --------------------------------------------------------------------------- #

def main() -> None:
    log.info("UnrealAI MCP server starting (stdio transport)")
    log.info("Bridge target: 127.0.0.1:55557 (UE plugin)")
    log.info("Tools registered: %d", len(mcp._tool_manager._tools)
             if hasattr(mcp, "_tool_manager") else "?")
    # FastMCP.run() defaults to stdio transport.
    mcp.run()


if __name__ == "__main__":
    main()
