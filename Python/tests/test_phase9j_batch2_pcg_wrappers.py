"""Wrapper coverage for Phase 9j batch 2 PCG node catalog and graph editing helpers."""

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


def test_list_pcg_node_types_maps_payload(monkeypatch):
    captured: Dict[str, Any] = {}

    def fake_bridge(command: str, payload: Dict[str, Any]) -> str:
        captured["command"] = command
        captured["payload"] = payload
        return json.dumps({
            "status": "success",
            "result": {
                "count": 1,
                "node_types": [{"class_name": "PCGSurfaceSamplerSettings"}],
            },
        })

    monkeypatch.setattr(mcp, "_bridge", fake_bridge)

    response = mcp.list_pcg_node_types(query="surface", settings_type="Spatial", max_results=25)
    payload = _decode(response)

    assert captured == {
        "command": "list_pcg_node_types",
        "payload": {
            "query": "surface",
            "settings_type": "Spatial",
            "max_results": 25,
        },
    }
    assert payload["count"] == 1


def test_read_pcg_graph_nodes_maps_payload(monkeypatch):
    captured: Dict[str, Any] = {}

    def fake_bridge(command: str, payload: Dict[str, Any]) -> str:
        captured["command"] = command
        captured["payload"] = payload
        return json.dumps({
            "status": "success",
            "result": {
                "asset_path": payload["asset_path"],
                "count": 2,
            },
        })

    monkeypatch.setattr(mcp, "_bridge", fake_bridge)

    response = mcp.read_pcg_graph_nodes("/Game/CopilotTests/PCG/PCG_Test.PCG_Test")
    payload = _decode(response)

    assert captured == {
        "command": "read_pcg_graph_nodes",
        "payload": {
            "asset_path": "/Game/CopilotTests/PCG/PCG_Test.PCG_Test",
        },
    }
    assert payload["count"] == 2


def test_read_pcg_graph_node_maps_payload(monkeypatch):
    captured: Dict[str, Any] = {}

    def fake_bridge(command: str, payload: Dict[str, Any]) -> str:
        captured["command"] = command
        captured["payload"] = payload
        return json.dumps({
            "status": "success",
            "result": {
                "asset_path": payload["asset_path"],
                "node_name": payload.get("node_name", "SurfaceSampler"),
            },
        })

    monkeypatch.setattr(mcp, "_bridge", fake_bridge)

    response = mcp.read_pcg_graph_node(
        asset_path="/Game/CopilotTests/PCG/PCG_Test.PCG_Test",
        node_name="PCGNode_1",
    )
    payload = _decode(response)

    assert captured == {
        "command": "read_pcg_graph_node",
        "payload": {
            "asset_path": "/Game/CopilotTests/PCG/PCG_Test.PCG_Test",
            "node_name": "PCGNode_1",
        },
    }
    assert payload["node_name"] == "PCGNode_1"


def test_add_pcg_graph_node_maps_payload(monkeypatch):
    captured: Dict[str, Any] = {}

    def fake_bridge(command: str, payload: Dict[str, Any]) -> str:
        captured["command"] = command
        captured["payload"] = payload
        return json.dumps({
            "status": "success",
            "result": {
                "created": True,
                "node_name": "PCGSurfaceSamplerSettings_0",
            },
        })

    monkeypatch.setattr(mcp, "_bridge", fake_bridge)

    response = mcp.add_pcg_graph_node(
        asset_path="/Game/CopilotTests/PCG/PCG_Test.PCG_Test",
        settings_class="/Script/PCG.PCGSurfaceSamplerSettings",
        node_title="Surface Sampler A",
        position_x=240,
        position_y=120,
    )
    payload = _decode(response)

    assert captured == {
        "command": "add_pcg_graph_node",
        "payload": {
            "asset_path": "/Game/CopilotTests/PCG/PCG_Test.PCG_Test",
            "settings_class": "/Script/PCG.PCGSurfaceSamplerSettings",
            "node_title": "Surface Sampler A",
            "position_x": 240,
            "position_y": 120,
        },
    }
    assert payload["created"] is True


def test_delete_pcg_graph_node_maps_payload(monkeypatch):
    captured: Dict[str, Any] = {}

    def fake_bridge(command: str, payload: Dict[str, Any]) -> str:
        captured["command"] = command
        captured["payload"] = payload
        return json.dumps({
            "status": "success",
            "result": {
                "deleted": True,
                "node_path": payload.get("node_path", ""),
            },
        })

    monkeypatch.setattr(mcp, "_bridge", fake_bridge)

    response = mcp.delete_pcg_graph_node(
        asset_path="/Game/CopilotTests/PCG/PCG_Test.PCG_Test",
        node_path="/Game/CopilotTests/PCG/PCG_Test.PCG_Test:PCGNode_4",
    )
    payload = _decode(response)

    assert captured == {
        "command": "delete_pcg_graph_node",
        "payload": {
            "asset_path": "/Game/CopilotTests/PCG/PCG_Test.PCG_Test",
            "node_path": "/Game/CopilotTests/PCG/PCG_Test.PCG_Test:PCGNode_4",
        },
    }
    assert payload["deleted"] is True


def test_connect_pcg_graph_nodes_maps_payload(monkeypatch):
    captured: Dict[str, Any] = {}

    def fake_bridge(command: str, payload: Dict[str, Any]) -> str:
        captured["command"] = command
        captured["payload"] = payload
        return json.dumps({
            "status": "success",
            "result": {
                "connected": True,
                "source_pin_name": payload["source_pin_name"],
                "target_pin_name": payload["target_pin_name"],
            },
        })

    monkeypatch.setattr(mcp, "_bridge", fake_bridge)

    response = mcp.connect_pcg_graph_nodes(
        asset_path="/Game/CopilotTests/PCG/PCG_Test.PCG_Test",
        source_node_name="PCGNode_Surface",
        source_pin_name="Output",
        target_node_name="PCGNode_Spawn",
        target_pin_name="In",
    )
    payload = _decode(response)

    assert captured == {
        "command": "connect_pcg_graph_nodes",
        "payload": {
            "asset_path": "/Game/CopilotTests/PCG/PCG_Test.PCG_Test",
            "source_pin_name": "Output",
            "target_pin_name": "In",
            "source_node_name": "PCGNode_Surface",
            "target_node_name": "PCGNode_Spawn",
        },
    }
    assert payload["connected"] is True


def test_disconnect_pcg_graph_nodes_maps_payload(monkeypatch):
    captured: Dict[str, Any] = {}

    def fake_bridge(command: str, payload: Dict[str, Any]) -> str:
        captured["command"] = command
        captured["payload"] = payload
        return json.dumps({
            "status": "success",
            "result": {
                "removed": True,
                "source_pin_name": payload["source_pin_name"],
                "target_pin_name": payload["target_pin_name"],
            },
        })

    monkeypatch.setattr(mcp, "_bridge", fake_bridge)

    response = mcp.disconnect_pcg_graph_nodes(
        asset_path="/Game/CopilotTests/PCG/PCG_Test.PCG_Test",
        source_node_title="Surface Sampler A",
        source_pin_name="Output",
        target_node_title="Static Mesh Spawner",
        target_pin_name="In",
    )
    payload = _decode(response)

    assert captured == {
        "command": "disconnect_pcg_graph_nodes",
        "payload": {
            "asset_path": "/Game/CopilotTests/PCG/PCG_Test.PCG_Test",
            "source_pin_name": "Output",
            "target_pin_name": "In",
            "source_node_title": "Surface Sampler A",
            "target_node_title": "Static Mesh Spawner",
        },
    }
    assert payload["removed"] is True