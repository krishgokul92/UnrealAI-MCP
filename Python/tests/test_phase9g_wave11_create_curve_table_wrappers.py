"""Wrapper coverage for Phase 9g Wave 11 CurveTable asset creation."""

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


def test_create_curve_table_asset_maps_payload(monkeypatch):
    captured: Dict[str, Any] = {}

    def fake_bridge(command: str, payload: Dict[str, Any]) -> str:
        captured["command"] = command
        captured["payload"] = payload
        return json.dumps({
            "status": "success",
            "result": {
                "asset_path": f"{payload['destination_path']}/{payload['curve_table_name']}.{payload['curve_table_name']}",
                "curve_table_mode": payload["curve_table_mode"],
                "created": True,
            },
        })

    monkeypatch.setattr(mcp, "_bridge", fake_bridge)

    response = mcp.create_curve_table_asset(
        curve_table_name="CT_TestWave11",
        curve_table_mode="RichCurves",
        destination_path="/Game/CopilotTests/CurveTables",
    )
    payload = _decode(response)

    assert captured == {
        "command": "create_curve_table_asset",
        "payload": {
            "curve_table_name": "CT_TestWave11",
            "curve_table_mode": "RichCurves",
            "destination_path": "/Game/CopilotTests/CurveTables",
        },
    }
    assert payload["asset_path"] == "/Game/CopilotTests/CurveTables/CT_TestWave11.CT_TestWave11"
    assert payload["curve_table_mode"] == "RichCurves"
    assert payload["created"] is True