"""Wrapper coverage for Phase 9h Wave 9 state sequence-player binding."""

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


def test_set_anim_state_sequence_player_maps_payload(monkeypatch):
    captured: Dict[str, Any] = {}

    def fake_bridge(command: str, payload: Dict[str, Any]) -> str:
        captured["command"] = command
        captured["payload"] = payload
        return json.dumps({
            "status": "success",
            "result": {
                "state_name": payload["state_name"],
                "binding_type": "sequence_player",
                "updated": True,
            },
        })

    monkeypatch.setattr(mcp, "_bridge", fake_bridge)

    response = mcp.set_anim_state_sequence_player(
        asset_path="/Game/CopilotTests/AnimBlueprints/ABP_Test.ABP_Test",
        state_machine_name="Traversal",
        state_name="Idle",
        sequence_path="/Game/Animations/Idle.Idle",
    )
    payload = _decode(response)

    assert captured == {
        "command": "set_anim_state_sequence_player",
        "payload": {
            "asset_path": "/Game/CopilotTests/AnimBlueprints/ABP_Test.ABP_Test",
            "state_machine_name": "Traversal",
            "state_name": "Idle",
            "sequence_path": "/Game/Animations/Idle.Idle",
        },
    }
    assert payload["state_name"] == "Idle"
    assert payload["binding_type"] == "sequence_player"
    assert payload["updated"] is True