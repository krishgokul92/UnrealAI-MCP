"""Wrapper coverage for Phase 9j batch 3 PCG mutation helpers."""

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


def test_update_pcg_graph_node_settings_maps_payload(monkeypatch):
    captured: Dict[str, Any] = {}

    def fake_bridge(command: str, payload: Dict[str, Any]) -> str:
        captured["command"] = command
        captured["payload"] = payload
        return json.dumps({
            "status": "success",
            "result": {
                "updated": True,
                "updated_properties": list(payload["settings_patch"].keys()),
            },
        })

    monkeypatch.setattr(mcp, "_bridge", fake_bridge)

    response = mcp.update_pcg_graph_node_settings(
        asset_path="/Game/CopilotTests/PCG/PCG_Test.PCG_Test",
        node_path="/Game/CopilotTests/PCG/PCG_Test.PCG_Test:PCGNode_4",
        settings_patch={"Seed": 42, "bKeepZeroDensityPoints": True},
    )
    payload = _decode(response)

    assert captured == {
        "command": "update_pcg_graph_node_settings",
        "payload": {
            "asset_path": "/Game/CopilotTests/PCG/PCG_Test.PCG_Test",
            "node_path": "/Game/CopilotTests/PCG/PCG_Test.PCG_Test:PCGNode_4",
            "settings_patch": {"Seed": 42, "bKeepZeroDensityPoints": True},
        },
    }
    assert payload["updated"] is True


def test_set_pcg_graph_node_state_maps_payload(monkeypatch):
    captured: Dict[str, Any] = {}

    def fake_bridge(command: str, payload: Dict[str, Any]) -> str:
        captured["command"] = command
        captured["payload"] = payload
        return json.dumps({
            "status": "success",
            "result": {
                "updated": True,
                "updated_state_fields": [key for key in ("enabled", "debug") if key in payload],
            },
        })

    monkeypatch.setattr(mcp, "_bridge", fake_bridge)

    response = mcp.set_pcg_graph_node_state(
        asset_path="/Game/CopilotTests/PCG/PCG_Test.PCG_Test",
        node_name="PCGNode_1",
        enabled=False,
        debug=True,
    )
    payload = _decode(response)

    assert captured == {
        "command": "set_pcg_graph_node_state",
        "payload": {
            "asset_path": "/Game/CopilotTests/PCG/PCG_Test.PCG_Test",
            "node_name": "PCGNode_1",
            "enabled": False,
            "debug": True,
        },
    }
    assert payload["updated"] is True


def test_create_pcg_graph_parameter_maps_payload(monkeypatch):
    captured: Dict[str, Any] = {}

    def fake_bridge(command: str, payload: Dict[str, Any]) -> str:
        captured["command"] = command
        captured["payload"] = payload
        return json.dumps({
            "status": "success",
            "result": {
                "created": True,
                "parameter_name": payload["parameter_name"],
            },
        })

    monkeypatch.setattr(mcp, "_bridge", fake_bridge)

    response = mcp.create_pcg_graph_parameter(
        asset_path="/Game/CopilotTests/PCG/PCG_Test.PCG_Test",
        parameter_name="SpawnCount",
        parameter_type="Int32",
        default_value=7,
    )
    payload = _decode(response)

    assert captured == {
        "command": "create_pcg_graph_parameter",
        "payload": {
            "asset_path": "/Game/CopilotTests/PCG/PCG_Test.PCG_Test",
            "parameter_name": "SpawnCount",
            "parameter_type": "Int32",
            "default_value": 7,
        },
    }
    assert payload["created"] is True


def test_rename_pcg_graph_parameter_maps_payload(monkeypatch):
    captured: Dict[str, Any] = {}

    def fake_bridge(command: str, payload: Dict[str, Any]) -> str:
        captured["command"] = command
        captured["payload"] = payload
        return json.dumps({
            "status": "success",
            "result": {
                "renamed": True,
                "parameter_name": payload["new_name"],
            },
        })

    monkeypatch.setattr(mcp, "_bridge", fake_bridge)

    response = mcp.rename_pcg_graph_parameter(
        asset_path="/Game/CopilotTests/PCG/PCG_Test.PCG_Test",
        current_name="SpawnCount",
        new_name="SpawnCountRenamed",
    )
    payload = _decode(response)

    assert captured == {
        "command": "rename_pcg_graph_parameter",
        "payload": {
            "asset_path": "/Game/CopilotTests/PCG/PCG_Test.PCG_Test",
            "current_name": "SpawnCount",
            "new_name": "SpawnCountRenamed",
        },
    }
    assert payload["renamed"] is True


def test_set_pcg_graph_parameter_maps_payload(monkeypatch):
    captured: Dict[str, Any] = {}

    def fake_bridge(command: str, payload: Dict[str, Any]) -> str:
        captured["command"] = command
        captured["payload"] = payload
        return json.dumps({
            "status": "success",
            "result": {
                "updated": True,
                "parameter_name": payload["parameter_name"],
            },
        })

    monkeypatch.setattr(mcp, "_bridge", fake_bridge)

    response = mcp.set_pcg_graph_parameter(
        asset_path="/Game/CopilotTests/PCG/PCG_Test.PCG_Test_Instance.PCG_Test_Instance",
        parameter_name="SpawnCountRenamed",
        value=23,
    )
    payload = _decode(response)

    assert captured == {
        "command": "set_pcg_graph_parameter",
        "payload": {
            "asset_path": "/Game/CopilotTests/PCG/PCG_Test.PCG_Test_Instance.PCG_Test_Instance",
            "parameter_name": "SpawnCountRenamed",
            "value": 23,
        },
    }
    assert payload["updated"] is True


def test_reset_pcg_graph_parameter_override_maps_payload(monkeypatch):
    captured: Dict[str, Any] = {}

    def fake_bridge(command: str, payload: Dict[str, Any]) -> str:
        captured["command"] = command
        captured["payload"] = payload
        return json.dumps({
            "status": "success",
            "result": {
                "reset": True,
                "parameter_name": payload["parameter_name"],
            },
        })

    monkeypatch.setattr(mcp, "_bridge", fake_bridge)

    response = mcp.reset_pcg_graph_parameter_override(
        asset_path="/Game/CopilotTests/PCG/PCG_Test_Instance.PCG_Test_Instance",
        parameter_name="SpawnCountRenamed",
    )
    payload = _decode(response)

    assert captured == {
        "command": "reset_pcg_graph_parameter_override",
        "payload": {
            "asset_path": "/Game/CopilotTests/PCG/PCG_Test_Instance.PCG_Test_Instance",
            "parameter_name": "SpawnCountRenamed",
        },
    }
    assert payload["reset"] is True