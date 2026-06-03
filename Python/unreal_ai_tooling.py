from __future__ import annotations

import ast
import re
from functools import lru_cache
from pathlib import Path
from typing import Any, Dict, Iterable, List, Tuple


UE_OBJECT_PATH_RE = re.compile(r"^/[A-Za-z0-9_]+(?:/[A-Za-z0-9_]+)*(?:\.[A-Za-z0-9_]+)?$")
UE_CONTENT_PATH_RE = re.compile(r"^/[A-Za-z0-9_]+(?:/[A-Za-z0-9_]+)*$")
GUID_RE = re.compile(
    r"^[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[1-5][0-9a-fA-F]{3}-[89abAB][0-9a-fA-F]{3}-[0-9a-fA-F]{12}$"
)


class ToolValidationError(ValueError):
    def __init__(
        self,
        code: str,
        message: str,
        hint: str,
        *,
        field: str | None = None,
        details: Dict[str, Any] | None = None,
    ) -> None:
        super().__init__(message)
        self.code = code
        self.message = message
        self.hint = hint
        self.field = field
        self.details = details or {}

    def to_response(self, command: str) -> Dict[str, Any]:
        response: Dict[str, Any] = {
            "status": "error",
            "error": self.message,
            "error_code": self.code,
            "hint": self.hint,
            "command": command,
        }
        if self.field:
            response["field"] = self.field
        if self.details:
            response["details"] = self.details
        return response


_CATEGORY_PREFIX_RULES: List[Tuple[str, Tuple[str, ...]]] = [
    ("registry", ("list_unreal_tools", "get_unreal_tool_categories")),
    (
        "editor_level",
        (
            "ping",
            "get_actors_in_level",
            "get_selected_actors",
            "get_level_viewport_info",
            "get_world_partition_info",
            "get_all_layers",
            "create_layer",
            "rename_layer",
            "delete_layer",
            "get_actor_layers",
            "get_actors_in_layer",
            "add_actor_to_layer",
            "remove_actor_from_layer",
            "add_selected_actors_to_layer",
            "remove_selected_actors_from_layer",
            "select_actors_in_layer",
            "deselect_actors_in_layer",
            "set_layer_visibility",
            "toggle_layer_visibility",
            "make_all_layers_visible",
            "select_actors",
            "clear_actor_selection",
            "find_actors_by_name",
            "spawn_actor",
            "delete_actor",
            "set_actor_transform",
            "new_blank_map",
            "open_level",
            "snap_actors_to_grid",
            "align_actors",
            "duplicate_actor",
            "focus_viewport",
            "save_level",
            "undo_last_action",
            "redo_last_action",
            "capture_viewport_screenshot",
            "place_in_grid",
            "place_in_circle",
            "place_along_spline",
            "scatter_in_area",
        ),
    ),
    (
        "blueprint_assets",
        (
            "create_blueprint",
            "compile_blueprint",
            "add_component_to_blueprint",
            "get_available_materials",
            "create_blueprint_function",
            "read_blueprint_content",
            "analyze_blueprint_graph",
            "get_blueprint_variable_details",
            "get_blueprint_function_details",
        ),
    ),
    (
        "blueprint_graph",
        (
            "paste_blueprint_graph",
            "list_t3d_snippets",
            "get_t3d_snippet",
            "add_blueprint_node",
            "connect_blueprint_nodes",
            "delete_blueprint_node",
            "add_event_node",
            "set_node_default",
            "create_blueprint_variable",
            "set_blueprint_variable_properties",
            "add_blueprint_function_input",
            "add_blueprint_function_output",
            "delete_blueprint_function",
            "rename_blueprint_function",
        ),
    ),
    (
        "blueprint_instances",
        (
            "set_static_mesh_properties",
            "set_physics_properties",
            "set_mesh_material_color",
            "spawn_blueprint_actor",
            "apply_material_to_actor",
            "apply_material_to_blueprint",
            "get_actor_material_info",
            "get_blueprint_material_info",
        ),
    ),
    (
        "asset_registry",
        (
            "find_assets",
            "get_asset_dependencies",
            "get_referencers",
            "create_material_instance",
            "import_asset",
        ),
    ),
    ("materials", ("create_material_", "get_material_", "set_material_", "validate_material_", "delete_material_", "replace_material_", "connect_material_", "disconnect_material_", "recompile_material", "layout_material_")),
    ("widget_blueprints", ("create_widget_", "read_widget_", "add_widget_", "remove_widget_", "reparent_widget_", "set_widget_",)),
    ("level_sequences", ("create_level_sequence", "read_level_sequence_content", "add_camera_cut_track_to_level_sequence", "add_actor_possessable_to_level_sequence", "add_track_to_binding_in_level_sequence", "add_float_key_to_binding_track_in_level_sequence", "set_level_sequence_playback_range", "add_master_track_to_level_sequence", "add_section_to_master_track_in_level_sequence", "set_section_range_in_master_track_in_level_sequence", "remove_section_from_master_track_in_level_sequence")),
    ("landscape", ("get_landscapes", "read_landscape_content", "sample_landscape_", "paint_landscape_", "sculpt_landscape_", "rebuild_landscape", "create_landscape", "set_landscape_flat_height", "import_landscape_heightmap")),
    ("data_tables", ("read_data_table_", "create_data_table_", "upsert_data_table_", "delete_data_table_", "rename_data_table_", "duplicate_data_table_", "move_data_table_", "validate_data_table_", "read_curve_table_", "create_curve_table_", "upsert_curve_table_", "delete_curve_table_", "rename_curve_table_", "validate_curve_table_")),
    ("pcg", ("read_pcg_", "list_pcg_", "create_pcg_", "add_pcg_", "delete_pcg_", "connect_pcg_", "disconnect_pcg_", "set_pcg_", "update_pcg_", "rename_pcg_", "reset_pcg_")),
    ("niagara", ("read_niagara_", "create_niagara_", "set_niagara_", "validate_niagara_", "add_niagara_", "duplicate_niagara_", "rename_niagara_", "remove_niagara_")),
    ("blackboard", ("read_blackboard_", "create_blackboard_", "update_blackboard_")),
    ("behavior_tree", ("read_behavior_tree_", "create_behavior_tree_", "update_behavior_tree_", "set_behavior_tree_", "validate_behavior_tree")),
    ("anim_blueprints", ("read_anim_", "create_anim_", "rename_anim_", "delete_anim_", "set_anim_", "validate_anim_blueprint")),
]


_ENUM_RULES: Dict[Tuple[str, str], Tuple[str, ...]] = {
    ("align_actors", "axis"): ("x", "y", "z"),
    ("align_actors", "mode"): ("min", "max", "center", "average"),
    ("move_data_table_row", "direction"): ("up", "down"),
    ("capture_viewport_screenshot", "image_format"): ("png",),
    ("place_along_spline", "orientation_mode"): ("none", "yaw", "full"),
    ("scatter_in_area", "shape"): ("box", "ellipse"),
}


_OBJECT_PATH_FIELDS = {
    "asset_path",
    "blueprint_path",
    "material_path",
    "parent_material",
    "preview_mesh_path",
    "skeleton_path",
    "static_mesh",
    "subgraph_asset_path",
    "parent_blueprint_path",
}

_CONTENT_PATH_FIELDS = {
    "dest_path",
    "path",
}


def _is_tool_decorator(decorator: ast.AST) -> bool:
    if isinstance(decorator, ast.Call):
        decorator = decorator.func
    return isinstance(decorator, ast.Attribute) and decorator.attr == "tool"


def _matches_prefix(name: str, prefixes: Iterable[str]) -> bool:
    return any(name == prefix or name.startswith(prefix) for prefix in prefixes)


def infer_tool_category(name: str) -> str:
    for category, prefixes in _CATEGORY_PREFIX_RULES:
        if _matches_prefix(name, prefixes):
            return category
    return "other"


def is_read_only_tool(name: str) -> bool:
    return name == "ping" or name.startswith(("get_", "read_", "list_", "find_", "validate_"))


def is_destructive_tool(name: str) -> bool:
    return name.startswith(("delete_", "remove_", "clear_", "reset_", "unload_"))


@lru_cache(maxsize=4)
def load_tool_registry(source_path: str) -> List[Dict[str, Any]]:
    source = Path(source_path).read_text(encoding="utf-8")
    module = ast.parse(source)
    entries: List[Dict[str, Any]] = []

    for node in module.body:
        if not isinstance(node, (ast.FunctionDef, ast.AsyncFunctionDef)):
            continue
        if not any(_is_tool_decorator(decorator) for decorator in node.decorator_list):
            continue

        name = node.name
        docstring = ast.get_docstring(node) or ""
        summary = docstring.strip().splitlines()[0].strip() if docstring.strip() else ""
        entries.append(
            {
                "name": name,
                "category": infer_tool_category(name),
                "read_only": is_read_only_tool(name),
                "mutating": not is_read_only_tool(name),
                "destructive": is_destructive_tool(name),
                "summary": summary,
                "line": getattr(node, "lineno", 0),
            }
        )

    entries.sort(key=lambda entry: entry["name"])
    return entries


def list_tool_categories(source_path: str) -> List[Dict[str, Any]]:
    counts: Dict[str, Dict[str, int]] = {}
    for entry in load_tool_registry(source_path):
        category = entry["category"]
        bucket = counts.setdefault(
            category,
            {
                "tool_count": 0,
                "read_only_count": 0,
                "mutating_count": 0,
                "destructive_count": 0,
            },
        )
        bucket["tool_count"] += 1
        bucket["read_only_count"] += 1 if entry["read_only"] else 0
        bucket["mutating_count"] += 1 if entry["mutating"] else 0
        bucket["destructive_count"] += 1 if entry["destructive"] else 0

    categories = []
    for category, bucket in sorted(counts.items()):
        categories.append({"category": category, **bucket})
    return categories


def filter_tool_registry(
    source_path: str,
    *,
    category: str = "",
    query: str = "",
    include_read_only: bool = True,
    include_mutating: bool = True,
    include_destructive: bool = True,
) -> List[Dict[str, Any]]:
    normalized_category = category.strip().lower()
    normalized_query = query.strip().lower()
    results: List[Dict[str, Any]] = []
    for entry in load_tool_registry(source_path):
        if normalized_category and entry["category"] != normalized_category:
            continue
        if not include_read_only and entry["read_only"]:
            continue
        if not include_mutating and entry["mutating"]:
            continue
        if not include_destructive and entry["destructive"]:
            continue

        haystack = f"{entry['name']} {entry['summary']} {entry['category']}".lower()
        if normalized_query and normalized_query not in haystack:
            continue
        results.append(entry)
    return results


def _validate_actor_name(value: Any, field: str) -> None:
    if not isinstance(value, str) or not value.strip():
        raise ToolValidationError(
            "INVALID_ACTOR_NAME",
            f"'{field}' must be a non-empty actor label or internal actor name.",
            "Use find_actors_by_name or get_actors_in_level to discover a valid actor name before retrying.",
            field=field,
        )


def _validate_object_path(value: Any, field: str) -> None:
    if not isinstance(value, str) or not UE_OBJECT_PATH_RE.match(value.strip()):
        raise ToolValidationError(
            "INVALID_ASSET_PATH",
            f"'{field}' must be a valid Unreal asset path like '/Game/Folder/Asset' or '/Game/Folder/Asset.Asset'.",
            "Use a content-browser asset path that starts with /Game, /Engine, /Script, or another mounted content root.",
            field=field,
            details={"expected_format": "/Game/Folder/Asset or /Game/Folder/Asset.Asset"},
        )


def _validate_content_path(value: Any, field: str) -> None:
    if not isinstance(value, str) or not UE_CONTENT_PATH_RE.match(value.strip()):
        raise ToolValidationError(
            "INVALID_CONTENT_PATH",
            f"'{field}' must be a valid Unreal content folder path like '/Game/Folder'.",
            "Use a mounted content folder path that starts with /Game, /Engine, or another mounted content root.",
            field=field,
            details={"expected_format": "/Game/Folder"},
        )


def _validate_enum(command: str, field: str, value: Any) -> None:
    allowed = _ENUM_RULES.get((command, field))
    if not allowed or not isinstance(value, str):
        return
    normalized_value = value.strip().lower()
    if normalized_value not in allowed:
        raise ToolValidationError(
            "INVALID_ENUM_VALUE",
            f"'{field}' must be one of: {', '.join(allowed)}.",
            "Retry with one of the allowed values listed in details.allowed_values.",
            field=field,
            details={"allowed_values": list(allowed), "received": value},
        )


def _validate_guid(command: str, field: str, value: Any) -> None:
    if command == "read_niagara_system_emitter" and field == "emitter_handle_id":
        if isinstance(value, str) and value.strip() and not GUID_RE.match(value.strip()):
            raise ToolValidationError(
                "INVALID_ID",
                "'emitter_handle_id' must be a valid GUID string.",
                "Pass the exact emitter handle id returned by read_niagara_system_content.",
                field=field,
            )


def validate_bridge_command(command: str, params: Dict[str, Any]) -> None:
    if not params:
        return

    for field, value in params.items():
        if field in _OBJECT_PATH_FIELDS and value not in (None, ""):
            _validate_object_path(value, field)
        elif field in _CONTENT_PATH_FIELDS and value not in (None, ""):
            _validate_content_path(value, field)

        _validate_enum(command, field, value)
        _validate_guid(command, field, value)

        if field in {"actor_name", "spline_actor_name", "source_actor_name"}:
            _validate_actor_name(value, field)
        elif field == "actor_names":
            if not isinstance(value, list) or not value:
                raise ToolValidationError(
                    "INVALID_ACTOR_NAME",
                    "'actor_names' must contain at least one actor name.",
                    "Use find_actors_by_name or get_actors_in_level to discover valid actor names before retrying.",
                    field=field,
                )
            for actor_name in value:
                _validate_actor_name(actor_name, field)
        elif field == "name" and command in {"spawn_actor", "delete_actor", "set_actor_transform"}:
            _validate_actor_name(value, field)


def infer_error_code(message: str) -> str:
    lowered = message.lower()
    if "cannot reach ue bridge" in lowered:
        return "BRIDGE_UNREACHABLE"
    if "empty response" in lowered:
        return "BRIDGE_EMPTY_RESPONSE"
    if "invalid json" in lowered:
        return "BRIDGE_INVALID_JSON"
    if "unknown command" in lowered:
        return "UNKNOWN_COMMAND"
    if "actor not found" in lowered:
        return "ACTOR_NOT_FOUND"
    if "already exists" in lowered:
        return "ALREADY_EXISTS"
    if "missing '" in lowered or "missing or empty" in lowered or "provide either" in lowered:
        return "MISSING_PARAMETER"
    if "valid guid" in lowered:
        return "INVALID_ID"
    if "unknown actor type" in lowered or "must be one of" in lowered:
        return "INVALID_ENUM_VALUE"
    if "failed to resolve" in lowered or "failed to find" in lowered or "asset not found" in lowered:
        return "ASSET_OR_RESOURCE_NOT_FOUND"
    return "COMMAND_FAILED"


def infer_error_hint(command: str, code: str) -> str:
    hints = {
        "BRIDGE_UNREACHABLE": "Open the Unreal project with the UnrealAI plugin loaded, wait for the editor to finish startup, then call ping again.",
        "BRIDGE_EMPTY_RESPONSE": "Retry the command after the editor settles, or restart the Unreal Editor if the bridge stays silent.",
        "BRIDGE_INVALID_JSON": "Retry the command once. If it repeats, inspect the Unreal Output Log for bridge serialization errors.",
        "UNKNOWN_COMMAND": "Refresh the MCP server tool list or call list_unreal_tools to confirm the supported command surface.",
        "ACTOR_NOT_FOUND": "Call find_actors_by_name or get_actors_in_level to discover the exact actor label before retrying.",
        "ALREADY_EXISTS": "Choose a unique name or delete the existing resource before retrying.",
        "MISSING_PARAMETER": "Provide every required field shown in the tool schema and retry the full call.",
        "INVALID_ID": "Pass the exact identifier returned by the corresponding read or list tool.",
        "INVALID_ENUM_VALUE": "Use one of the allowed values listed in details.allowed_values or in the tool schema.",
        "ASSET_OR_RESOURCE_NOT_FOUND": "Use find_assets or the relevant read tool to verify the exact asset path before retrying.",
        "COMMAND_FAILED": f"Retry the full {command} call after checking the returned error details.",
    }
    return hints[code]


def normalize_bridge_error(command: str, result: Dict[str, Any]) -> Dict[str, Any]:
    message = str(result.get("error") or "Unknown command failure")
    code = str(result.get("error_code") or infer_error_code(message))
    normalized = dict(result)
    normalized.setdefault("status", "error")
    normalized["error"] = message
    normalized["error_code"] = code
    normalized.setdefault("hint", infer_error_hint(command, code))
    normalized.setdefault("command", command)
    return normalized