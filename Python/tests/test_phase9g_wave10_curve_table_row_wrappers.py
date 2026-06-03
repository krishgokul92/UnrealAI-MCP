"""Wrapper coverage for Phase 9g Wave 10 CurveTable row inspection."""

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


def test_read_curve_table_row_maps_payload(monkeypatch):
    captured: Dict[str, Any] = {}

    def fake_bridge(command: str, payload: Dict[str, Any]) -> str:
        captured["command"] = command
        captured["payload"] = payload
        return json.dumps({
            "status": "success",
            "result": {
                "asset_path": payload["asset_path"],
                "row_name": payload["row_name"],
                "key_field_name": "Name",
                "row": {
                    "Name": payload["row_name"],
                    "0": 1.0,
                },
            },
        })

    monkeypatch.setattr(mcp, "_bridge", fake_bridge)

    response = mcp.read_curve_table_row(
        asset_path="/Engine/Fake/CT_Test.CT_Test",
        row_name="CurveA",
    )
    payload = _decode(response)

    assert captured == {
        "command": "read_curve_table_row",
        "payload": {
            "asset_path": "/Engine/Fake/CT_Test.CT_Test",
            "row_name": "CurveA",
        },
    }
    assert payload["asset_path"] == "/Engine/Fake/CT_Test.CT_Test"
    assert payload["row_name"] == "CurveA"
    assert payload["key_field_name"] == "Name"
    assert payload["row"]["Name"] == "CurveA"