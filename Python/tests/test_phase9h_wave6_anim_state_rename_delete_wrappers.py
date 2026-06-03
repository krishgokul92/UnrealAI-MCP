"""Wrapper coverage for Phase 9h Wave 6 state rename and delete."""

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


def test_rename_anim_state_maps_payload(monkeypatch):
    captured: Dict[str, Any] = {}

    def fake_bridge(command: str, payload: Dict[str, Any]) -> str:
        captured["command"] = command
        captured["payload"] = payload
        return json.dumps({
            "status": "success",
            "result": {
                "asset_path": payload["asset_path"],
                "state_machine_name": payload["state_machine_name"],
                "state_name": payload["new_state_name"],
                "renamed": True,
            },
        })

    monkeypatch.setattr(mcp, "_bridge", fake_bridge)

    response = mcp.rename_anim_state(
        asset_path="/Game/CopilotTests/AnimBlueprints/ABP_Test.ABP_Test",
        state_machine_name="Locomotion",
        state_name="Idle",
        new_state_name="IdleLoop",
    )
    payload = _decode(response)

    assert captured == {
        "command": "rename_anim_state",
        "payload": {
            "asset_path": "/Game/CopilotTests/AnimBlueprints/ABP_Test.ABP_Test",
            "state_machine_name": "Locomotion",
            "state_name": "Idle",
            "new_state_name": "IdleLoop",
        },
    }
    assert payload["asset_path"] == "/Game/CopilotTests/AnimBlueprints/ABP_Test.ABP_Test"
    assert payload["state_machine_name"] == "Locomotion"
    assert payload["state_name"] == "IdleLoop"
    assert payload["renamed"] is True


def test_delete_anim_state_maps_payload(monkeypatch):
    captured: Dict[str, Any] = {}

    def fake_bridge(command: str, payload: Dict[str, Any]) -> str:
        captured["command"] = command
        captured["payload"] = payload
        return json.dumps({
            "status": "success",
            "result": {
                "asset_path": payload["asset_path"],
                "state_machine_name": payload["state_machine_name"],
                "deleted_state_name": payload["state_name"],
                "deleted": True,
            },
        })

    monkeypatch.setattr(mcp, "_bridge", fake_bridge)

    response = mcp.delete_anim_state(
        asset_path="/Game/CopilotTests/AnimBlueprints/ABP_Test.ABP_Test",
        state_machine_name="Locomotion",
        state_name="IdleLoop",
    )
    payload = _decode(response)

    assert captured == {
        "command": "delete_anim_state",
        "payload": {
            "asset_path": "/Game/CopilotTests/AnimBlueprints/ABP_Test.ABP_Test",
            "state_machine_name": "Locomotion",
            "state_name": "IdleLoop",
        },
    }
    assert payload["asset_path"] == "/Game/CopilotTests/AnimBlueprints/ABP_Test.ABP_Test"
    assert payload["state_machine_name"] == "Locomotion"
    assert payload["deleted_state_name"] == "IdleLoop"
    assert payload["deleted"] is True