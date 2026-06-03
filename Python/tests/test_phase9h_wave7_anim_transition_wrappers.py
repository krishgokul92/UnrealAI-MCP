"""Wrapper coverage for Phase 9h Wave 7 transition creation and deletion."""

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


def test_create_anim_transition_maps_payload(monkeypatch):
    captured: Dict[str, Any] = {}

    def fake_bridge(command: str, payload: Dict[str, Any]) -> str:
        captured["command"] = command
        captured["payload"] = payload
        return json.dumps({
            "status": "success",
            "result": {
                "source_state_name": payload["source_state_name"],
                "target_state_name": payload["target_state_name"],
                "created": True,
            },
        })

    monkeypatch.setattr(mcp, "_bridge", fake_bridge)

    response = mcp.create_anim_transition(
        asset_path="/Game/CopilotTests/AnimBlueprints/ABP_Test.ABP_Test",
        state_machine_name="Traversal",
        source_state_name="Idle",
        target_state_name="Walk",
    )
    payload = _decode(response)

    assert captured == {
        "command": "create_anim_transition",
        "payload": {
            "asset_path": "/Game/CopilotTests/AnimBlueprints/ABP_Test.ABP_Test",
            "state_machine_name": "Traversal",
            "source_state_name": "Idle",
            "target_state_name": "Walk",
        },
    }
    assert payload["source_state_name"] == "Idle"
    assert payload["target_state_name"] == "Walk"
    assert payload["created"] is True


def test_delete_anim_transition_maps_payload(monkeypatch):
    captured: Dict[str, Any] = {}

    def fake_bridge(command: str, payload: Dict[str, Any]) -> str:
        captured["command"] = command
        captured["payload"] = payload
        return json.dumps({
            "status": "success",
            "result": {
                "source_state_name": payload["source_state_name"],
                "target_state_name": payload["target_state_name"],
                "deleted": True,
            },
        })

    monkeypatch.setattr(mcp, "_bridge", fake_bridge)

    response = mcp.delete_anim_transition(
        asset_path="/Game/CopilotTests/AnimBlueprints/ABP_Test.ABP_Test",
        state_machine_name="Traversal",
        source_state_name="Idle",
        target_state_name="Walk",
    )
    payload = _decode(response)

    assert captured == {
        "command": "delete_anim_transition",
        "payload": {
            "asset_path": "/Game/CopilotTests/AnimBlueprints/ABP_Test.ABP_Test",
            "state_machine_name": "Traversal",
            "source_state_name": "Idle",
            "target_state_name": "Walk",
        },
    }
    assert payload["source_state_name"] == "Idle"
    assert payload["target_state_name"] == "Walk"
    assert payload["deleted"] is True