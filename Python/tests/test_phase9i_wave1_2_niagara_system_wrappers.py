"""Wrapper coverage for Phase 9i batch 1 Niagara System inspection and creation."""

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


def test_read_niagara_system_content_maps_payload(monkeypatch):
    captured: Dict[str, Any] = {}

    def fake_bridge(command: str, payload: Dict[str, Any]) -> str:
        captured["command"] = command
        captured["payload"] = payload
        return json.dumps({
            "status": "success",
            "result": {
                "asset_path": payload["asset_path"],
                "emitter_count": 0,
                "exposed_user_parameter_count": 0,
            },
        })

    monkeypatch.setattr(mcp, "_bridge", fake_bridge)

    asset_path = "/Game/CopilotTests/Niagara/NS_TestWave1.NS_TestWave1"
    response = mcp.read_niagara_system_content(asset_path)
    payload = _decode(response)

    assert captured == {
        "command": "read_niagara_system_content",
        "payload": {
            "asset_path": asset_path,
        },
    }
    assert payload["asset_path"] == asset_path
    assert payload["emitter_count"] == 0
    assert payload["exposed_user_parameter_count"] == 0


def test_create_niagara_system_asset_maps_payload(monkeypatch):
    captured: Dict[str, Any] = {}

    def fake_bridge(command: str, payload: Dict[str, Any]) -> str:
        captured["command"] = command
        captured["payload"] = payload
        return json.dumps({
            "status": "success",
            "result": {
                "niagara_system_name": payload["niagara_system_name"],
                "destination_path": payload["destination_path"],
                "template_asset_path": payload.get("template_asset_path", ""),
                "created": True,
            },
        })

    monkeypatch.setattr(mcp, "_bridge", fake_bridge)

    response = mcp.create_niagara_system_asset(
        niagara_system_name="NS_TestWave2",
        destination_path="/Game/CopilotTests/Niagara",
        template_asset_path="/Game/CopilotTests/Niagara/NS_Template.NS_Template",
    )
    payload = _decode(response)

    assert captured == {
        "command": "create_niagara_system_asset",
        "payload": {
            "niagara_system_name": "NS_TestWave2",
            "destination_path": "/Game/CopilotTests/Niagara",
            "template_asset_path": "/Game/CopilotTests/Niagara/NS_Template.NS_Template",
        },
    }
    assert payload["niagara_system_name"] == "NS_TestWave2"
    assert payload["destination_path"] == "/Game/CopilotTests/Niagara"
    assert payload["template_asset_path"] == "/Game/CopilotTests/Niagara/NS_Template.NS_Template"
    assert payload["created"] is True