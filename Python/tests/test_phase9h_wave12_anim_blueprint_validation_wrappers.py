"""Wrapper coverage for Phase 9h Wave 12 AnimBlueprint validation."""

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


def test_validate_anim_blueprint_maps_payload(monkeypatch):
    captured: Dict[str, Any] = {}

    def fake_bridge(command: str, payload: Dict[str, Any]) -> str:
        captured["command"] = command
        captured["payload"] = payload
        return json.dumps({
            "status": "success",
            "result": {
                "asset_path": payload["asset_path"],
                "is_valid": True,
                "compile_healthy": True,
            },
        })

    monkeypatch.setattr(mcp, "_bridge", fake_bridge)

    response = mcp.validate_anim_blueprint(
        asset_path="/Game/CopilotTests/AnimBlueprints/ABP_TestWave12.ABP_TestWave12",
    )
    payload = _decode(response)

    assert captured == {
        "command": "validate_anim_blueprint",
        "payload": {
            "asset_path": "/Game/CopilotTests/AnimBlueprints/ABP_TestWave12.ABP_TestWave12",
        },
    }
    assert payload["asset_path"] == "/Game/CopilotTests/AnimBlueprints/ABP_TestWave12.ABP_TestWave12"
    assert payload["is_valid"] is True
    assert payload["compile_healthy"] is True