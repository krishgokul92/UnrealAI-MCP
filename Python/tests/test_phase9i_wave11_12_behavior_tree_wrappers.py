"""Wrapper coverage for Phase 9i batch 6 Behavior Tree subtree, property, and validation helpers."""

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


def test_update_behavior_tree_subtree_maps_payload(monkeypatch):
    captured: Dict[str, Any] = {}
    operations = [
        {"op": "add", "parent_path": "root", "node_kind": "composite", "node_type": "sequence"},
        {"op": "remove", "target_path": "root/0"},
    ]

    def fake_bridge(command: str, payload: Dict[str, Any]) -> str:
        captured["command"] = command
        captured["payload"] = payload
        return json.dumps({
            "status": "success",
            "result": {
                "asset_path": payload["asset_path"],
                "updated": True,
                "applied_operation_count": len(payload["operations"]),
            },
        })

    monkeypatch.setattr(mcp, "_bridge", fake_bridge)

    response = mcp.update_behavior_tree_subtree(
        asset_path="/Game/CopilotTests/BehaviorTrees/BT_Test.BT_Test",
        operations=operations,
    )
    payload = _decode(response)

    assert captured == {
        "command": "update_behavior_tree_subtree",
        "payload": {
            "asset_path": "/Game/CopilotTests/BehaviorTrees/BT_Test.BT_Test",
            "operations": operations,
        },
    }
    assert payload["updated"] is True
    assert payload["applied_operation_count"] == 2


def test_set_behavior_tree_node_properties_maps_payload(monkeypatch):
    captured: Dict[str, Any] = {}

    def fake_bridge(command: str, payload: Dict[str, Any]) -> str:
        captured["command"] = command
        captured["payload"] = payload
        return json.dumps({
            "status": "success",
            "result": {
                "asset_path": payload["asset_path"],
                "topology_path": payload["topology_path"],
                "updated": True,
            },
        })

    monkeypatch.setattr(mcp, "_bridge", fake_bridge)

    response = mcp.set_behavior_tree_node_properties(
        asset_path="/Game/CopilotTests/BehaviorTrees/BT_Test.BT_Test",
        topology_path="root/0/decorators/0",
        blackboard_key_name="TargetActor",
        flow_abort_mode="both",
        enabled_state="disabled",
    )
    payload = _decode(response)

    assert captured == {
        "command": "set_behavior_tree_node_properties",
        "payload": {
            "asset_path": "/Game/CopilotTests/BehaviorTrees/BT_Test.BT_Test",
            "topology_path": "root/0/decorators/0",
            "blackboard_key_name": "TargetActor",
            "flow_abort_mode": "both",
            "enabled_state": "disabled",
        },
    }
    assert payload["updated"] is True
    assert payload["topology_path"] == "root/0/decorators/0"


def test_validate_behavior_tree_maps_payload(monkeypatch):
    captured: Dict[str, Any] = {}

    def fake_bridge(command: str, payload: Dict[str, Any]) -> str:
        captured["command"] = command
        captured["payload"] = payload
        return json.dumps({
            "status": "success",
            "result": {
                "asset_path": payload["asset_path"],
                "validated": True,
                "error_count": 0,
            },
        })

    monkeypatch.setattr(mcp, "_bridge", fake_bridge)

    response = mcp.validate_behavior_tree("/Game/CopilotTests/BehaviorTrees/BT_Test.BT_Test")
    payload = _decode(response)

    assert captured == {
        "command": "validate_behavior_tree",
        "payload": {
            "asset_path": "/Game/CopilotTests/BehaviorTrees/BT_Test.BT_Test",
        },
    }
    assert payload["validated"] is True
    assert payload["error_count"] == 0