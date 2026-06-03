"""Focused wrapper coverage for release-oriented registry and validation helpers."""

from __future__ import annotations

import json
import sys
from pathlib import Path


_PY_ROOT = Path(__file__).resolve().parent.parent
if str(_PY_ROOT) not in sys.path:
    sys.path.insert(0, str(_PY_ROOT))

import unreal_ai_mcp as mcp_module  # noqa: E402


def test_get_unreal_tool_categories_returns_category_counts():
    payload = json.loads(mcp_module.get_unreal_tool_categories())

    assert payload["status"] == "success", payload
    assert payload["category_count"] >= 8, payload

    categories = {entry["category"] for entry in payload["categories"]}
    assert "editor_level" in categories, payload
    assert "pcg" in categories, payload
    assert "materials" in categories, payload


def test_list_unreal_tools_filters_by_category_and_query():
    payload = json.loads(mcp_module.list_unreal_tools(category="pcg", query="graph"))

    assert payload["status"] == "success", payload
    assert payload["category"] == "pcg", payload
    assert payload["tool_count"] > 0, payload
    assert all(entry["category"] == "pcg" for entry in payload["tools"]), payload
    assert any(entry["name"] == "read_pcg_graph_content" for entry in payload["tools"]), payload


def test_list_unreal_tools_surfaces_level_map_helpers_under_editor_level():
    payload = json.loads(mcp_module.list_unreal_tools(category="editor_level", query="level"))

    assert payload["status"] == "success", payload
    tool_names = {entry["name"] for entry in payload["tools"]}
    assert "open_level" in tool_names, payload
    assert "save_level" in tool_names, payload


def test_bridge_returns_structured_asset_path_validation_error(monkeypatch):
    calls = []

    def fake_send_command(*args, **kwargs):
        calls.append((args, kwargs))
        return {"status": "success"}

    monkeypatch.setattr(mcp_module, "send_command", fake_send_command)

    payload = json.loads(
        mcp_module._bridge(
            "read_behavior_tree_content",
            {"asset_path": "Game/Invalid/BT_Test"},
        )
    )

    assert calls == []
    assert payload["status"] == "error", payload
    assert payload["error_code"] == "INVALID_ASSET_PATH", payload
    assert payload["field"] == "asset_path", payload
    assert "hint" in payload, payload


def test_bridge_validates_enum_values_before_send(monkeypatch):
    calls = []

    def fake_send_command(*args, **kwargs):
        calls.append((args, kwargs))
        return {"status": "success"}

    monkeypatch.setattr(mcp_module, "send_command", fake_send_command)

    payload = json.loads(
        mcp_module._bridge(
            "align_actors",
            {"actor_names": ["ActorA", "ActorB"], "axis": "w", "mode": "min"},
        )
    )

    assert calls == []
    assert payload["status"] == "error", payload
    assert payload["error_code"] == "INVALID_ENUM_VALUE", payload
    assert payload["field"] == "axis", payload
    assert payload["details"]["allowed_values"] == ["x", "y", "z"], payload


def test_bridge_augments_actor_not_found_error(monkeypatch):
    def fake_send_command(command, params=None):
        return {"status": "error", "error": "Actor not found: MissingActor"}

    monkeypatch.setattr(mcp_module, "send_command", fake_send_command)

    payload = json.loads(mcp_module._bridge("delete_actor", {"name": "MissingActor"}))

    assert payload["status"] == "error", payload
    assert payload["error_code"] == "ACTOR_NOT_FOUND", payload
    assert "find_actors_by_name" in payload["hint"], payload
    assert payload["command"] == "delete_actor", payload