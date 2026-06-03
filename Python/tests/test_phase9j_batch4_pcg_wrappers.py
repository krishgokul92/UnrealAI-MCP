"""Wrapper coverage for the remaining Phase 9j PCG graph helpers."""

from __future__ import annotations

import json
import sys
from pathlib import Path
from typing import Any, Dict


_PY_ROOT = Path(__file__).resolve().parent.parent
if str(_PY_ROOT) not in sys.path:
    sys.path.insert(0, str(_PY_ROOT))

import unreal_ai_mcp as mcp  # noqa: E402


def _decode(response: str) -> Dict[str, Any]:
    payload = json.loads(response)
    assert payload["status"] == "success", payload
    assert isinstance(payload.get("result"), dict), payload
    return payload["result"]


def test_set_pcg_graph_node_position_maps_payload(monkeypatch):
    captured: Dict[str, Any] = {}

    def fake_bridge(command: str, payload: Dict[str, Any]) -> str:
        captured["command"] = command
        captured["payload"] = payload
        return json.dumps({
            "status": "success",
            "result": {
                "updated": True,
                "position_x": payload["position_x"],
                "position_y": payload["position_y"],
            },
        })

    monkeypatch.setattr(mcp, "_bridge", fake_bridge)

    response = mcp.set_pcg_graph_node_position(
        asset_path="/Game/CopilotTests/PCG/PCG_Test.PCG_Test",
        node_title="Copilot Surface Node",
        position_x=640,
        position_y=320,
    )
    payload = _decode(response)

    assert captured == {
        "command": "set_pcg_graph_node_position",
        "payload": {
            "asset_path": "/Game/CopilotTests/PCG/PCG_Test.PCG_Test",
            "node_title": "Copilot Surface Node",
            "position_x": 640,
            "position_y": 320,
        },
    }
    assert payload["updated"] is True


def test_set_pcg_subgraph_node_asset_maps_payload(monkeypatch):
    captured: Dict[str, Any] = {}

    def fake_bridge(command: str, payload: Dict[str, Any]) -> str:
        captured["command"] = command
        captured["payload"] = payload
        return json.dumps({
            "status": "success",
            "result": {
                "updated": True,
                "subgraph_asset_path": payload.get("subgraph_asset_path", ""),
            },
        })

    monkeypatch.setattr(mcp, "_bridge", fake_bridge)

    response = mcp.set_pcg_subgraph_node_asset(
        asset_path="/Game/CopilotTests/PCG/PCG_Test.PCG_Test",
        node_path="/Game/CopilotTests/PCG/PCG_Test.PCG_Test:PCGNode_4",
        subgraph_asset_path="/Game/CopilotTests/PCG/Subgraph_A.Subgraph_A",
    )
    payload = _decode(response)

    assert captured == {
        "command": "set_pcg_subgraph_node_asset",
        "payload": {
            "asset_path": "/Game/CopilotTests/PCG/PCG_Test.PCG_Test",
            "node_path": "/Game/CopilotTests/PCG/PCG_Test.PCG_Test:PCGNode_4",
            "subgraph_asset_path": "/Game/CopilotTests/PCG/Subgraph_A.Subgraph_A",
        },
    }
    assert payload["updated"] is True


def test_add_pcg_graph_comment_maps_payload(monkeypatch):
    captured: Dict[str, Any] = {}

    def fake_bridge(command: str, payload: Dict[str, Any]) -> str:
        captured["command"] = command
        captured["payload"] = payload
        return json.dumps({
            "status": "success",
            "result": {
                "created": True,
                "comment_guid": payload["comment_guid"],
            },
        })

    monkeypatch.setattr(mcp, "_bridge", fake_bridge)

    response = mcp.add_pcg_graph_comment(
        asset_path="/Game/CopilotTests/PCG/PCG_Test.PCG_Test",
        comment_guid="12345678-1234-1234-1234-1234567890ab",
        comment_text="Copilot comment",
        position_x=128,
        position_y=256,
        width=320,
        height=160,
        comment_color=[0.25, 0.5, 0.75, 1.0],
        details="graph note",
        font_size=20,
        move_mode=1,
        comment_depth=4,
        comment_bubble_visible=False,
    )
    payload = _decode(response)

    assert captured == {
        "command": "add_pcg_graph_comment",
        "payload": {
            "asset_path": "/Game/CopilotTests/PCG/PCG_Test.PCG_Test",
            "comment_guid": "12345678-1234-1234-1234-1234567890ab",
            "comment_text": "Copilot comment",
            "position_x": 128,
            "position_y": 256,
            "width": 320,
            "height": 160,
            "comment_color": [0.25, 0.5, 0.75, 1.0],
            "details": "graph note",
            "font_size": 20,
            "move_mode": 1,
            "comment_depth": 4,
            "comment_bubble_visible": False,
        },
    }
    assert payload["created"] is True


def test_update_pcg_graph_comment_maps_payload(monkeypatch):
    captured: Dict[str, Any] = {}

    def fake_bridge(command: str, payload: Dict[str, Any]) -> str:
        captured["command"] = command
        captured["payload"] = payload
        return json.dumps({
            "status": "success",
            "result": {
                "updated": True,
                "comment_guid": payload["comment_guid"],
            },
        })

    monkeypatch.setattr(mcp, "_bridge", fake_bridge)

    response = mcp.update_pcg_graph_comment(
        asset_path="/Game/CopilotTests/PCG/PCG_Test.PCG_Test",
        comment_guid="12345678-1234-1234-1234-1234567890ab",
        comment_text="Updated comment",
        width=512,
        comment_bubble_pinned=True,
    )
    payload = _decode(response)

    assert captured == {
        "command": "update_pcg_graph_comment",
        "payload": {
            "asset_path": "/Game/CopilotTests/PCG/PCG_Test.PCG_Test",
            "comment_guid": "12345678-1234-1234-1234-1234567890ab",
            "comment_text": "Updated comment",
            "width": 512,
            "comment_bubble_pinned": True,
        },
    }
    assert payload["updated"] is True


def test_delete_pcg_graph_comment_maps_payload(monkeypatch):
    captured: Dict[str, Any] = {}

    def fake_bridge(command: str, payload: Dict[str, Any]) -> str:
        captured["command"] = command
        captured["payload"] = payload
        return json.dumps({
            "status": "success",
            "result": {
                "deleted": True,
                "comment_guid": payload["comment_guid"],
            },
        })

    monkeypatch.setattr(mcp, "_bridge", fake_bridge)

    response = mcp.delete_pcg_graph_comment(
        asset_path="/Game/CopilotTests/PCG/PCG_Test.PCG_Test",
        comment_guid="12345678-1234-1234-1234-1234567890ab",
    )
    payload = _decode(response)

    assert captured == {
        "command": "delete_pcg_graph_comment",
        "payload": {
            "asset_path": "/Game/CopilotTests/PCG/PCG_Test.PCG_Test",
            "comment_guid": "12345678-1234-1234-1234-1234567890ab",
        },
    }
    assert payload["deleted"] is True


def test_add_pcg_graph_reroute_maps_payload(monkeypatch):
    captured: Dict[str, Any] = {}

    def fake_bridge(command: str, payload: Dict[str, Any]) -> str:
        captured["command"] = command
        captured["payload"] = payload
        return json.dumps({
            "status": "success",
            "result": {
                "created": True,
                "reroute_kind": payload["reroute_kind"],
            },
        })

    monkeypatch.setattr(mcp, "_bridge", fake_bridge)

    response = mcp.add_pcg_graph_reroute(
        asset_path="/Game/CopilotTests/PCG/PCG_Test.PCG_Test",
        reroute_kind="named_usage",
        node_title="Usage Reroute",
        position_x=512,
        position_y=384,
        declaration_node_title="Declaration Reroute",
    )
    payload = _decode(response)

    assert captured == {
        "command": "add_pcg_graph_reroute",
        "payload": {
            "asset_path": "/Game/CopilotTests/PCG/PCG_Test.PCG_Test",
            "reroute_kind": "named_usage",
            "node_title": "Usage Reroute",
            "position_x": 512,
            "position_y": 384,
            "declaration_node_title": "Declaration Reroute",
        },
    }
    assert payload["created"] is True


def test_delete_pcg_graph_parameter_maps_payload(monkeypatch):
    captured: Dict[str, Any] = {}

    def fake_bridge(command: str, payload: Dict[str, Any]) -> str:
        captured["command"] = command
        captured["payload"] = payload
        return json.dumps({
            "status": "success",
            "result": {
                "deleted": True,
                "parameter_name": payload["parameter_name"],
            },
        })

    monkeypatch.setattr(mcp, "_bridge", fake_bridge)

    response = mcp.delete_pcg_graph_parameter(
        asset_path="/Game/CopilotTests/PCG/PCG_Test.PCG_Test",
        parameter_name="SpawnCount",
    )
    payload = _decode(response)

    assert captured == {
        "command": "delete_pcg_graph_parameter",
        "payload": {
            "asset_path": "/Game/CopilotTests/PCG/PCG_Test.PCG_Test",
            "parameter_name": "SpawnCount",
        },
    }
    assert payload["deleted"] is True