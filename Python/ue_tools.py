"""
UnrealAI Tool Catalog
=====================

Each tool the model may call:
  1. A JSON schema (Ollama / OpenAI function-calling format)
  2. A Python handler that translates arguments → UE bridge command

Keep the catalog small and focused; add more over time.

Bridge command names come from UnrealAIBridge.cpp dispatch table.
"""

from __future__ import annotations

import json
import logging
from typing import Any, Callable, Dict, List, Set

from ue_bridge import send_command, BridgeError

log = logging.getLogger("UnrealAI.Tools")


# Per-tool truncation limit. Inspection tools that legitimately return large
# graphs are exempted via LARGE_RESPONSE_TOOLS below.
DEFAULT_TRUNCATE_BYTES = 8000
LARGE_TRUNCATE_BYTES = 16000
LARGE_RESPONSE_TOOLS: Set[str] = {
    "read_blueprint_content",
    "analyze_blueprint_graph",
}


# --------------------------------------------------------------------------- #
# Tool handlers
# --------------------------------------------------------------------------- #

def _bridge(cmd: str, params: Dict[str, Any] | None = None) -> Dict[str, Any]:
    """Thin wrapper that returns errors as structured dicts (never raises)."""
    try:
        return send_command(cmd, params or {})
    except BridgeError as exc:
        return {"error": str(exc), "command": cmd}


# --- Level / actor tools (existing) -------------------------------------- #

def tool_ping(_args: Dict[str, Any]) -> Dict[str, Any]:
    return _bridge("ping")


def tool_get_actors_in_level(_args: Dict[str, Any]) -> Dict[str, Any]:
    return _bridge("get_actors_in_level")


def tool_find_actors_by_name(args: Dict[str, Any]) -> Dict[str, Any]:
    return _bridge("find_actors_by_name", {"pattern": args.get("pattern", "")})


def tool_spawn_actor(args: Dict[str, Any]) -> Dict[str, Any]:
    params: Dict[str, Any] = {
        "type": args.get("type", "StaticMeshActor"),
        "name": args.get("name", ""),
        "location": args.get("location", [0, 0, 0]),
        "rotation": args.get("rotation", [0, 0, 0]),
    }
    return _bridge("spawn_actor", params)


def tool_delete_actor(args: Dict[str, Any]) -> Dict[str, Any]:
    return _bridge("delete_actor", {"name": args.get("name", "")})


def tool_set_actor_transform(args: Dict[str, Any]) -> Dict[str, Any]:
    return _bridge("set_actor_transform", {
        "name": args.get("name", ""),
        "location": args.get("location"),
        "rotation": args.get("rotation"),
        "scale": args.get("scale"),
    })


def tool_create_blueprint(args: Dict[str, Any]) -> Dict[str, Any]:
    return _bridge("create_blueprint", {
        "name": args.get("name", ""),
        "parent_class": args.get("parent_class", "Actor"),
    })


def tool_compile_blueprint(args: Dict[str, Any]) -> Dict[str, Any]:
    return _bridge("compile_blueprint", {"name": args.get("name", "")})


def tool_add_component_to_blueprint(args: Dict[str, Any]) -> Dict[str, Any]:
    return _bridge("add_component_to_blueprint", {
        "blueprint_name": args.get("blueprint_name", ""),
        "component_type": args.get("component_type", ""),
        "component_name": args.get("component_name", ""),
    })


def tool_get_available_materials(_args: Dict[str, Any]) -> Dict[str, Any]:
    return _bridge("get_available_materials")


# --- Phase 5 Wave 1: Blueprint inspection -------------------------------- #

def tool_read_blueprint_content(args: Dict[str, Any]) -> Dict[str, Any]:
    return _bridge("read_blueprint_content", {
        "blueprint_name": args.get("blueprint_name", ""),
    })


def tool_analyze_blueprint_graph(args: Dict[str, Any]) -> Dict[str, Any]:
    return _bridge("analyze_blueprint_graph", {
        "blueprint_name": args.get("blueprint_name", ""),
    })


def tool_get_blueprint_variable_details(args: Dict[str, Any]) -> Dict[str, Any]:
    return _bridge("get_blueprint_variable_details", {
        "blueprint_name": args.get("blueprint_name", ""),
        "variable_name": args.get("variable_name", ""),
    })


def tool_get_blueprint_function_details(args: Dict[str, Any]) -> Dict[str, Any]:
    return _bridge("get_blueprint_function_details", {
        "blueprint_name": args.get("blueprint_name", ""),
        "function_name": args.get("function_name", ""),
    })


# --- Phase 5 (revised): T3D paste workflow ------------------------------- #
# Single tool that lets the LLM author entire BP graphs in one shot by
# producing T3D clipboard text (the same text Ctrl+C/Ctrl+V uses inside the
# editor). The C++ side calls FEdGraphUtilities::ImportNodesFromText.
#
# Phase 5 / A1 + A2: pre-flight validation and post-paste verification so the
# model gets actionable feedback when the T3D is malformed or partially binds.

from t3d_validator import validate_t3d, expected_node_names  # noqa: E402


def tool_paste_blueprint_graph(args: Dict[str, Any]) -> Dict[str, Any]:
    t3d_text = args.get("t3d_text", "") or ""

    # A1: pre-flight validation (returns immediately on structural errors,
    # no UE round-trip needed).
    parsed, errors = validate_t3d(t3d_text)
    if errors:
        return {
            "status": "validation_failed",
            "phase": "pre_flight",
            "validation_errors": errors,
            "expected_node_count": len(parsed.nodes),
            "hint": "Fix the listed validation_errors and resend the full T3D. Do not partial-patch.",
        }

    payload: Dict[str, Any] = {
        "blueprint_name": args.get("blueprint_name", ""),
        "t3d_text": t3d_text,
    }
    for k in ("graph_name", "auto_compile", "clear_graph"):
        if k in args and args[k] is not None:
            payload[k] = args[k]

    bridge_response = _bridge("paste_nodes_to_blueprint", payload)

    # A2: post-paste verification. The C++ side returns class+GUID pairs in
    # `node_names`; we cross-reference the T3D's intent against what landed.
    if isinstance(bridge_response, dict) and bridge_response.get("status") == "success":
        expected = expected_node_names(parsed)
        pasted_count = int(bridge_response.get("pasted_count", 0) or 0)
        bridge_response["expected_node_count"] = len(expected)
        bridge_response["expected_node_names"] = expected
        bridge_response["missing_node_count"] = max(0, len(expected) - pasted_count)
        # Every LinkedTo we parsed should have a target; pre-flight already
        # ensured target node + pin exist in the T3D, so any remaining link
        # issues are UE-side rejections we surface verbatim.
        bridge_response["expected_link_count"] = len(parsed.links)

    return bridge_response


# --------------------------------------------------------------------------- #
# OpenAI / Ollama tool schemas
# --------------------------------------------------------------------------- #

# Reusable fragments
_VEC3 = {
    "type": "array",
    "items": {"type": "number"},
    "minItems": 3,
    "maxItems": 3,
}

TOOL_SCHEMAS: List[Dict[str, Any]] = [
    # ---- Level / actor tools ---- #
    {
        "type": "function",
        "function": {
            "name": "ping",
            "description": "Check that the Unreal Editor bridge is reachable.",
            "parameters": {"type": "object", "properties": {}, "required": []},
        },
    },
    {
        "type": "function",
        "function": {
            "name": "get_actors_in_level",
            "description": "List every actor in the current editor level.",
            "parameters": {"type": "object", "properties": {}, "required": []},
        },
    },
    {
        "type": "function",
        "function": {
            "name": "find_actors_by_name",
            "description": "Find actors whose label matches a wildcard pattern (e.g. 'Cube*').",
            "parameters": {
                "type": "object",
                "properties": {
                    "pattern": {"type": "string", "description": "Name or wildcard pattern to match actor labels."},
                },
                "required": ["pattern"],
            },
        },
    },
    {
        "type": "function",
        "function": {
            "name": "spawn_actor",
            "description": "Spawn a new actor in the current level at a specific world location.",
            "parameters": {
                "type": "object",
                "properties": {
                    "type": {
                        "type": "string",
                        "description": "Actor class name, e.g. 'StaticMeshActor', 'PointLight', 'CameraActor'.",
                    },
                    "name": {"type": "string", "description": "Unique actor label to assign."},
                    "location": {**_VEC3, "description": "World location [X, Y, Z]."},
                    "rotation": {**_VEC3, "description": "World rotation [Pitch, Yaw, Roll] in degrees."},
                },
                "required": ["type", "name"],
            },
        },
    },
    {
        "type": "function",
        "function": {
            "name": "delete_actor",
            "description": "Delete an actor from the current level by its label.",
            "parameters": {
                "type": "object",
                "properties": {"name": {"type": "string", "description": "Actor label to delete."}},
                "required": ["name"],
            },
        },
    },
    {
        "type": "function",
        "function": {
            "name": "set_actor_transform",
            "description": "Move, rotate or scale an existing actor by its label.",
            "parameters": {
                "type": "object",
                "properties": {
                    "name": {"type": "string"},
                    "location": _VEC3,
                    "rotation": _VEC3,
                    "scale": _VEC3,
                },
                "required": ["name"],
            },
        },
    },
    {
        "type": "function",
        "function": {
            "name": "create_blueprint",
            "description": "Create a new Blueprint asset in the project.",
            "parameters": {
                "type": "object",
                "properties": {
                    "name": {"type": "string", "description": "Blueprint asset name."},
                    "parent_class": {
                        "type": "string",
                        "description": "Parent UClass name, e.g. 'Actor', 'Pawn', 'Character'.",
                    },
                },
                "required": ["name"],
            },
        },
    },
    {
        "type": "function",
        "function": {
            "name": "compile_blueprint",
            "description": "Compile a Blueprint asset by name. Always call this after editing a Blueprint's graph or variables.",
            "parameters": {
                "type": "object",
                "properties": {"name": {"type": "string"}},
                "required": ["name"],
            },
        },
    },
    {
        "type": "function",
        "function": {
            "name": "add_component_to_blueprint",
            "description": "Add a component (StaticMesh, PointLight, etc.) to a Blueprint.",
            "parameters": {
                "type": "object",
                "properties": {
                    "blueprint_name": {"type": "string"},
                    "component_type": {"type": "string", "description": "Component class, e.g. 'StaticMeshComponent'."},
                    "component_name": {"type": "string"},
                },
                "required": ["blueprint_name", "component_type", "component_name"],
            },
        },
    },
    {
        "type": "function",
        "function": {
            "name": "get_available_materials",
            "description": "List materials available in the project's content browser.",
            "parameters": {"type": "object", "properties": {}, "required": []},
        },
    },

    # ---- Phase 5 Wave 1: BP inspection ---- #
    {
        "type": "function",
        "function": {
            "name": "read_blueprint_content",
            "description": (
                "Dump the full structure of a Blueprint: variables, functions, "
                "components, parent class. Call this BEFORE editing an existing Blueprint "
                "so you know what's there."
            ),
            "parameters": {
                "type": "object",
                "properties": {"blueprint_name": {"type": "string"}},
                "required": ["blueprint_name"],
            },
        },
    },
    {
        "type": "function",
        "function": {
            "name": "analyze_blueprint_graph",
            "description": (
                "List every node and connection in a Blueprint's event graph. "
                "Use this to discover existing node_ids before connecting or deleting nodes."
            ),
            "parameters": {
                "type": "object",
                "properties": {"blueprint_name": {"type": "string"}},
                "required": ["blueprint_name"],
            },
        },
    },
    {
        "type": "function",
        "function": {
            "name": "get_blueprint_variable_details",
            "description": "Inspect one Blueprint variable's type, default value, flags.",
            "parameters": {
                "type": "object",
                "properties": {
                    "blueprint_name": {"type": "string"},
                    "variable_name": {"type": "string"},
                },
                "required": ["blueprint_name", "variable_name"],
            },
        },
    },
    {
        "type": "function",
        "function": {
            "name": "get_blueprint_function_details",
            "description": "Inspect one Blueprint function's inputs, outputs, and graph nodes.",
            "parameters": {
                "type": "object",
                "properties": {
                    "blueprint_name": {"type": "string"},
                    "function_name": {"type": "string"},
                },
                "required": ["blueprint_name", "function_name"],
            },
        },
    },

    # ---- Phase 5 (revised): T3D paste workflow ---- #
    {
        "type": "function",
        "function": {
            "name": "paste_blueprint_graph",
            "description": (
                "Paste T3D-format Blueprint clipboard text into a Blueprint graph. "
                "This is THE primary tool for authoring Blueprint logic. Generate the "
                "T3D text yourself (Begin Object / End Object blocks with CustomProperties Pin lines) "
                "following the spec in your system prompt, then call this tool ONCE per graph. "
                "It is the same import path Ctrl+V uses inside the editor. "
                "Set clear_graph=true if you want to replace the entire existing graph; "
                "leave it false to add to existing nodes. auto_compile defaults to true."
            ),
            "parameters": {
                "type": "object",
                "properties": {
                    "blueprint_name": {
                        "type": "string",
                        "description": "Blueprint asset name (e.g. 'BP_HealthActor') or full path (e.g. '/Game/Blueprints/BP_HealthActor').",
                    },
                    "t3d_text": {
                        "type": "string",
                        "description": (
                            "Complete T3D clipboard text. Must contain one or more 'Begin Object Class=...' "
                            "blocks, each ended by 'End Object'. Every NodeGuid and PinId must be unique. "
                            "Pin connections are written via 'LinkedTo=(NodeName.PinId,...)' inside CustomProperties Pin entries."
                        ),
                    },
                    "graph_name": {
                        "type": "string",
                        "description": "Target graph name. Defaults to 'EventGraph'. Pass a function name to paste into a function graph.",
                    },
                    "auto_compile": {
                        "type": "boolean",
                        "description": "Compile the Blueprint after pasting. Default true.",
                    },
                    "clear_graph": {
                        "type": "boolean",
                        "description": "Delete all existing nodes in the target graph before pasting. Default false.",
                    },
                },
                "required": ["blueprint_name", "t3d_text"],
            },
        },
    },
]


# Map function name → handler
TOOL_HANDLERS: Dict[str, Callable[[Dict[str, Any]], Dict[str, Any]]] = {
    # Level / actor
    "ping": tool_ping,
    "get_actors_in_level": tool_get_actors_in_level,
    "find_actors_by_name": tool_find_actors_by_name,
    "spawn_actor": tool_spawn_actor,
    "delete_actor": tool_delete_actor,
    "set_actor_transform": tool_set_actor_transform,
    "create_blueprint": tool_create_blueprint,
    "compile_blueprint": tool_compile_blueprint,
    "add_component_to_blueprint": tool_add_component_to_blueprint,
    "get_available_materials": tool_get_available_materials,
    # Phase 5 Wave 1: BP inspection
    "read_blueprint_content": tool_read_blueprint_content,
    "analyze_blueprint_graph": tool_analyze_blueprint_graph,
    "get_blueprint_variable_details": tool_get_blueprint_variable_details,
    "get_blueprint_function_details": tool_get_blueprint_function_details,
    # Phase 5 (revised): T3D paste workflow
    "paste_blueprint_graph": tool_paste_blueprint_graph,
}


def dispatch_tool(name: str, raw_args: str | Dict[str, Any]) -> str:
    """Run a tool by name. `raw_args` is what Ollama sent (JSON string or dict).

    Returns a JSON string suitable for a `tool` role message.
    """
    if isinstance(raw_args, str):
        try:
            args = json.loads(raw_args) if raw_args.strip() else {}
        except json.JSONDecodeError:
            return json.dumps({"error": f"Invalid JSON arguments for {name}: {raw_args}"})
    else:
        args = raw_args or {}

    handler = TOOL_HANDLERS.get(name)
    if handler is None:
        return json.dumps({"error": f"Unknown tool: {name}"})

    log.info("Tool call: %s(%s)", name, json.dumps(args)[:200])
    try:
        result = handler(args)
    except Exception as exc:  # noqa: BLE001
        log.exception("Tool %s raised", name)
        return json.dumps({"error": f"Tool {name} raised: {exc}"})

    # Truncate very large payloads to keep context under control.
    text = json.dumps(result, ensure_ascii=False)
    limit = LARGE_TRUNCATE_BYTES if name in LARGE_RESPONSE_TOOLS else DEFAULT_TRUNCATE_BYTES
    if len(text) > limit:
        text = text[:limit] + f' …[truncated at {limit} bytes — ask for a specific variable/function/node by name to drill in]'
    return text

