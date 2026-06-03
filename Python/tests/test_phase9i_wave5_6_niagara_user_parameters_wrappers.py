"""Wrapper coverage for Phase 9i batch 3 Niagara user-parameter mutation and validation helpers."""

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


def test_set_niagara_system_user_parameters_maps_payload(monkeypatch):
    captured: Dict[str, Any] = {}

    def fake_bridge(command: str, payload: Dict[str, Any]) -> str:
        captured["command"] = command
        captured["payload"] = payload
        return json.dumps({
            "status": "success",
            "result": {
                "asset_path": payload["asset_path"],
                "updated_parameter_count": len(payload["parameters"]),
            },
        })

    monkeypatch.setattr(mcp, "_bridge", fake_bridge)

    response = mcp.set_niagara_system_user_parameters(
        asset_path="/Game/CopilotTests/Niagara/NS_Test.NS_Test",
        create_if_missing=True,
        parameters=[
            {"name": "ScalarParam", "type": "float", "value": 1.25},
            {"name": "ColorParam", "type": "color", "value": [0.1, 0.2, 0.3, 1.0]},
        ],
    )
    payload = _decode(response)

    assert captured == {
        "command": "set_niagara_system_user_parameters",
        "payload": {
            "asset_path": "/Game/CopilotTests/Niagara/NS_Test.NS_Test",
            "parameters": [
                {"name": "ScalarParam", "type": "float", "value": 1.25},
                {"name": "ColorParam", "type": "color", "value": [0.1, 0.2, 0.3, 1.0]},
            ],
            "create_if_missing": True,
        },
    }
    assert payload["updated_parameter_count"] == 2


def test_validate_niagara_system_maps_payload(monkeypatch):
    captured: Dict[str, Any] = {}

    def fake_bridge(command: str, payload: Dict[str, Any]) -> str:
        captured["command"] = command
        captured["payload"] = payload
        return json.dumps({
            "status": "success",
            "result": {
                "asset_path": payload["asset_path"],
                "validated": True,
            },
        })

    monkeypatch.setattr(mcp, "_bridge", fake_bridge)

    response = mcp.validate_niagara_system("/Game/CopilotTests/Niagara/NS_Test.NS_Test")
    payload = _decode(response)

    assert captured == {
        "command": "validate_niagara_system",
        "payload": {
            "asset_path": "/Game/CopilotTests/Niagara/NS_Test.NS_Test",
        },
    }
    assert payload["validated"] is True