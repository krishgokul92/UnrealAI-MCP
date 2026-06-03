"""Wrapper coverage for Phase 9h Wave 11 state asset-player parameter mutation."""

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


def test_set_anim_state_asset_player_parameters_maps_payload(monkeypatch):
    captured: Dict[str, Any] = {}

    def fake_bridge(command: str, payload: Dict[str, Any]) -> str:
        captured["command"] = command
        captured["payload"] = payload
        return json.dumps({
            "status": "success",
            "result": {
                "state_name": payload["state_name"],
                "updated": True,
                "play_rate": payload["play_rate"],
            },
        })

    monkeypatch.setattr(mcp, "_bridge", fake_bridge)

    response = mcp.set_anim_state_asset_player_parameters(
        asset_path="/Game/CopilotTests/AnimBlueprints/ABP_Test.ABP_Test",
        state_machine_name="Traversal",
        state_name="Idle",
        loop=False,
        play_rate=1.75,
        start_position=0.25,
        blend_space_x=0.5,
        blend_space_y=-0.25,
        sync_group_name="Locomotion",
        sync_group_role="CanBeLeader",
        sync_group_method="SyncGroup",
        sync_group_override_position_when_joining_sync_group_as_leader=True,
    )
    payload = _decode(response)

    assert captured == {
        "command": "set_anim_state_asset_player_parameters",
        "payload": {
            "asset_path": "/Game/CopilotTests/AnimBlueprints/ABP_Test.ABP_Test",
            "state_machine_name": "Traversal",
            "state_name": "Idle",
            "loop": False,
            "play_rate": 1.75,
            "start_position": 0.25,
            "blend_space_x": 0.5,
            "blend_space_y": -0.25,
            "sync_group_name": "Locomotion",
            "sync_group_role": "CanBeLeader",
            "sync_group_method": "SyncGroup",
            "sync_group_override_position_when_joining_sync_group_as_leader": True,
        },
    }
    assert payload["state_name"] == "Idle"
    assert payload["updated"] is True
    assert payload["play_rate"] == 1.75