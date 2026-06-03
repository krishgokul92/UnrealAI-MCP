"""Wrapper coverage for Phase 9h Wave 1 AnimBlueprint inspection."""

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


def test_read_anim_blueprint_content_maps_payload(monkeypatch):
    captured: Dict[str, Any] = {}

    def fake_bridge(command: str, payload: Dict[str, Any]) -> str:
        captured["command"] = command
        captured["payload"] = payload
        return json.dumps({
            "status": "success",
            "result": {
                "asset_path": payload["asset_path"],
                "state_machine_count": 0,
            },
        })

    monkeypatch.setattr(mcp, "_bridge", fake_bridge)

    asset_path = "/Game/CopilotTests/AnimBlueprints/ABP_TestWave1.ABP_TestWave1"
    response = mcp.read_anim_blueprint_content(asset_path)
    payload = _decode(response)

    assert captured == {
        "command": "read_anim_blueprint_content",
        "payload": {
            "asset_path": asset_path,
        },
    }
    assert payload["asset_path"] == asset_path
    assert payload["state_machine_count"] == 0