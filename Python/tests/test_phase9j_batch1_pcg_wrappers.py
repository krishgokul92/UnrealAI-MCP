"""Wrapper coverage for Phase 9j batch 1 PCG graph, component, and volume helpers."""

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


def test_read_pcg_graph_content_maps_payload(monkeypatch):
    captured: Dict[str, Any] = {}

    def fake_bridge(command: str, payload: Dict[str, Any]) -> str:
        captured["command"] = command
        captured["payload"] = payload
        return json.dumps({
            "status": "success",
            "result": {
                "asset_path": payload["asset_path"],
                "node_count": 2,
            },
        })

    monkeypatch.setattr(mcp, "_bridge", fake_bridge)

    response = mcp.read_pcg_graph_content("/Game/CopilotTests/PCG/PCG_Test.PCG_Test")
    payload = _decode(response)

    assert captured == {
        "command": "read_pcg_graph_content",
        "payload": {
            "asset_path": "/Game/CopilotTests/PCG/PCG_Test.PCG_Test",
        },
    }
    assert payload["node_count"] == 2


def test_create_pcg_graph_asset_maps_payload(monkeypatch):
    captured: Dict[str, Any] = {}

    def fake_bridge(command: str, payload: Dict[str, Any]) -> str:
        captured["command"] = command
        captured["payload"] = payload
        return json.dumps({
            "status": "success",
            "result": {
                "asset_path": f"{payload['destination_path']}/{payload['pcg_graph_name']}.{payload['pcg_graph_name']}",
                "created": True,
            },
        })

    monkeypatch.setattr(mcp, "_bridge", fake_bridge)

    response = mcp.create_pcg_graph_asset(
        pcg_graph_name="PCG_Test",
        destination_path="/Game/CopilotTests/PCG",
        template_asset_path="/Game/CopilotTests/PCG/Templates/PCG_Template.PCG_Template",
        is_template=True,
        expose_to_library=True,
        expose_generation_in_asset_explorer=False,
        title_override="Test Graph",
        color_override=[0.1, 0.2, 0.3, 1.0],
    )
    payload = _decode(response)

    assert captured == {
        "command": "create_pcg_graph_asset",
        "payload": {
            "pcg_graph_name": "PCG_Test",
            "destination_path": "/Game/CopilotTests/PCG",
            "template_asset_path": "/Game/CopilotTests/PCG/Templates/PCG_Template.PCG_Template",
            "is_template": True,
            "expose_to_library": True,
            "expose_generation_in_asset_explorer": False,
            "title_override": "Test Graph",
            "color_override": [0.1, 0.2, 0.3, 1.0],
        },
    }
    assert payload["created"] is True


def test_create_pcg_graph_instance_maps_payload(monkeypatch):
    captured: Dict[str, Any] = {}

    def fake_bridge(command: str, payload: Dict[str, Any]) -> str:
        captured["command"] = command
        captured["payload"] = payload
        return json.dumps({
            "status": "success",
            "result": {
                "created": True,
                "asset_path": f"{payload['destination_path']}/{payload['instance_name']}.{payload['instance_name']}",
            },
        })

    monkeypatch.setattr(mcp, "_bridge", fake_bridge)

    response = mcp.create_pcg_graph_instance(
        instance_name="PCG_Instance_Test",
        parent_graph_path="/Game/CopilotTests/PCG/PCG_Test.PCG_Test",
        destination_path="/Game/CopilotTests/PCG/Instances",
    )
    payload = _decode(response)

    assert captured == {
        "command": "create_pcg_graph_instance",
        "payload": {
            "instance_name": "PCG_Instance_Test",
            "parent_graph_path": "/Game/CopilotTests/PCG/PCG_Test.PCG_Test",
            "destination_path": "/Game/CopilotTests/PCG/Instances",
        },
    }
    assert payload["created"] is True


def test_read_pcg_component_content_maps_payload(monkeypatch):
    captured: Dict[str, Any] = {}

    def fake_bridge(command: str, payload: Dict[str, Any]) -> str:
        captured["command"] = command
        captured["payload"] = payload
        return json.dumps({
            "status": "success",
            "result": {
                "actor_label": payload["actor_name"],
                "component_name": payload.get("component_name", "PCGComponent"),
            },
        })

    monkeypatch.setattr(mcp, "_bridge", fake_bridge)

    response = mcp.read_pcg_component_content("PCG_Volume_Test", component_name="PCGComponent0")
    payload = _decode(response)

    assert captured == {
        "command": "read_pcg_component_content",
        "payload": {
            "actor_name": "PCG_Volume_Test",
            "component_name": "PCGComponent0",
        },
    }
    assert payload["component_name"] == "PCGComponent0"


def test_add_pcg_component_to_actor_maps_payload(monkeypatch):
    captured: Dict[str, Any] = {}

    def fake_bridge(command: str, payload: Dict[str, Any]) -> str:
        captured["command"] = command
        captured["payload"] = payload
        return json.dumps({
            "status": "success",
            "result": {
                "created": True,
                "component_name": payload["component_name"],
            },
        })

    monkeypatch.setattr(mcp, "_bridge", fake_bridge)

    response = mcp.add_pcg_component_to_actor(
        actor_name="SM_TestActor",
        component_name="PCGComponent_Main",
        graph_asset_path="/Game/CopilotTests/PCG/PCG_Test.PCG_Test",
        generation_trigger="GenerateOnDemand",
        is_partitioned=True,
        activated=False,
        seed=99,
        generate_on_drop_when_trigger_on_demand=True,
    )
    payload = _decode(response)

    assert captured == {
        "command": "add_pcg_component_to_actor",
        "payload": {
            "actor_name": "SM_TestActor",
            "component_name": "PCGComponent_Main",
            "graph_asset_path": "/Game/CopilotTests/PCG/PCG_Test.PCG_Test",
            "generation_trigger": "GenerateOnDemand",
            "is_partitioned": True,
            "activated": False,
            "seed": 99,
            "generate_on_drop_when_trigger_on_demand": True,
        },
    }
    assert payload["component_name"] == "PCGComponent_Main"


def test_create_pcg_volume_maps_payload(monkeypatch):
    captured: Dict[str, Any] = {}

    def fake_bridge(command: str, payload: Dict[str, Any]) -> str:
        captured["command"] = command
        captured["payload"] = payload
        return json.dumps({
            "status": "success",
            "result": {
                "created": True,
                "volume_name": payload["volume_name"],
            },
        })

    monkeypatch.setattr(mcp, "_bridge", fake_bridge)

    response = mcp.create_pcg_volume(
        volume_name="PCG_Volume_Test",
        location=[100.0, 200.0, 300.0],
        rotation=[0.0, 90.0, 0.0],
        scale=[2.0, 2.0, 4.0],
        graph_asset_path="/Game/CopilotTests/PCG/PCG_Test.PCG_Test",
        generation_trigger="GenerateAtRuntime",
        is_partitioned=True,
        activated=True,
        seed=123,
        generate_on_drop_when_trigger_on_demand=False,
    )
    payload = _decode(response)

    assert captured == {
        "command": "create_pcg_volume",
        "payload": {
            "volume_name": "PCG_Volume_Test",
            "location": [100.0, 200.0, 300.0],
            "rotation": [0.0, 90.0, 0.0],
            "scale": [2.0, 2.0, 4.0],
            "graph_asset_path": "/Game/CopilotTests/PCG/PCG_Test.PCG_Test",
            "generation_trigger": "GenerateAtRuntime",
            "is_partitioned": True,
            "activated": True,
            "seed": 123,
            "generate_on_drop_when_trigger_on_demand": False,
        },
    }
    assert payload["volume_name"] == "PCG_Volume_Test"