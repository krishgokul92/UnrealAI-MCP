"""Wrapper coverage for Phase 9i batch 4 Blackboard inspection and key lifecycle helpers."""

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


def test_read_blackboard_content_maps_payload(monkeypatch):
    captured: Dict[str, Any] = {}

    def fake_bridge(command: str, payload: Dict[str, Any]) -> str:
        captured["command"] = command
        captured["payload"] = payload
        return json.dumps({
            "status": "success",
            "result": {
                "asset_path": payload["asset_path"],
                "key_count": 3,
            },
        })

    monkeypatch.setattr(mcp, "_bridge", fake_bridge)

    response = mcp.read_blackboard_content("/Game/CopilotTests/Blackboards/BB_Test.BB_Test")
    payload = _decode(response)

    assert captured == {
        "command": "read_blackboard_content",
        "payload": {
            "asset_path": "/Game/CopilotTests/Blackboards/BB_Test.BB_Test",
        },
    }
    assert payload["key_count"] == 3


def test_create_blackboard_asset_maps_payload(monkeypatch):
    captured: Dict[str, Any] = {}

    def fake_bridge(command: str, payload: Dict[str, Any]) -> str:
        captured["command"] = command
        captured["payload"] = payload
        return json.dumps({
            "status": "success",
            "result": {
                "asset_path": f"{payload['destination_path']}/{payload['blackboard_name']}.{payload['blackboard_name']}",
                "created": True,
            },
        })

    monkeypatch.setattr(mcp, "_bridge", fake_bridge)

    response = mcp.create_blackboard_asset(
        blackboard_name="BB_Test",
        destination_path="/Game/CopilotTests/Blackboards",
        parent_blackboard_path="/Game/CopilotTests/Blackboards/BB_Parent.BB_Parent",
    )
    payload = _decode(response)

    assert captured == {
        "command": "create_blackboard_asset",
        "payload": {
            "blackboard_name": "BB_Test",
            "destination_path": "/Game/CopilotTests/Blackboards",
            "parent_blackboard_path": "/Game/CopilotTests/Blackboards/BB_Parent.BB_Parent",
        },
    }
    assert payload["created"] is True


def test_update_blackboard_keys_maps_payload(monkeypatch):
    captured: Dict[str, Any] = {}

    operations = [
        {"op": "add", "name": "BoolKey", "key_type": "bool", "default_value": True},
        {"op": "rename", "name": "BoolKey", "new_name": "StateFlag"},
    ]

    def fake_bridge(command: str, payload: Dict[str, Any]) -> str:
        captured["command"] = command
        captured["payload"] = payload
        return json.dumps({
            "status": "success",
            "result": {
                "asset_path": payload["asset_path"],
                "updated": True,
                "operation_count": len(payload["operations"]),
            },
        })

    monkeypatch.setattr(mcp, "_bridge", fake_bridge)

    response = mcp.update_blackboard_keys(
        asset_path="/Game/CopilotTests/Blackboards/BB_Test.BB_Test",
        operations=operations,
    )
    payload = _decode(response)

    assert captured == {
        "command": "update_blackboard_keys",
        "payload": {
            "asset_path": "/Game/CopilotTests/Blackboards/BB_Test.BB_Test",
            "operations": operations,
        },
    }
    assert payload["updated"] is True
    assert payload["operation_count"] == 2