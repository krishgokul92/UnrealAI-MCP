"""Wrapper coverage for Phase 9h Wave 4 state-machine creation."""

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


def test_create_anim_state_machine_maps_payload(monkeypatch):
    captured: Dict[str, Any] = {}

    def fake_bridge(command: str, payload: Dict[str, Any]) -> str:
        captured["command"] = command
        captured["payload"] = payload
        return json.dumps({
            "status": "success",
            "result": {
                "asset_path": payload["asset_path"],
                "state_machine_name": payload["state_machine_name"],
                "graph_name": payload["graph_name"],
                "created": True,
            },
        })

    monkeypatch.setattr(mcp, "_bridge", fake_bridge)

    response = mcp.create_anim_state_machine(
        asset_path="/Game/CopilotTests/AnimBlueprints/ABP_Test.ABP_Test",
        state_machine_name="Locomotion",
        graph_name="AnimGraph",
        pos_x=320,
        pos_y=96,
    )
    payload = _decode(response)

    assert captured == {
        "command": "create_anim_state_machine",
        "payload": {
            "asset_path": "/Game/CopilotTests/AnimBlueprints/ABP_Test.ABP_Test",
            "state_machine_name": "Locomotion",
            "graph_name": "AnimGraph",
            "pos_x": 320,
            "pos_y": 96,
        },
    }
    assert payload["asset_path"] == "/Game/CopilotTests/AnimBlueprints/ABP_Test.ABP_Test"
    assert payload["state_machine_name"] == "Locomotion"
    assert payload["graph_name"] == "AnimGraph"
    assert payload["created"] is True