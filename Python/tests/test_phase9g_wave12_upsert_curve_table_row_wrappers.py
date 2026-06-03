"""Wrapper coverage for Phase 9g Wave 12 CurveTable row upsert."""

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


def test_upsert_curve_table_row_maps_payload(monkeypatch):
    captured: Dict[str, Any] = {}
    row_data = {
        "0": 1.5,
        "1.5": 3.25,
    }

    def fake_bridge(command: str, payload: Dict[str, Any]) -> str:
        captured["command"] = command
        captured["payload"] = payload
        return json.dumps({
            "status": "success",
            "result": {
                "asset_path": payload["asset_path"],
                "row_name": payload["row_name"],
                "row": {"Name": payload["row_name"], **payload["row_data"]},
                "created": True,
            },
        })

    monkeypatch.setattr(mcp, "_bridge", fake_bridge)

    response = mcp.upsert_curve_table_row(
        asset_path="/Game/CopilotTests/CurveTables/CT_TestWave12.CT_TestWave12",
        row_name="Phase9GWave12_Row",
        row_data=row_data,
    )
    payload = _decode(response)

    assert captured == {
        "command": "upsert_curve_table_row",
        "payload": {
            "asset_path": "/Game/CopilotTests/CurveTables/CT_TestWave12.CT_TestWave12",
            "row_name": "Phase9GWave12_Row",
            "row_data": row_data,
        },
    }
    assert payload["asset_path"] == "/Game/CopilotTests/CurveTables/CT_TestWave12.CT_TestWave12"
    assert payload["row_name"] == "Phase9GWave12_Row"
    assert payload["row"] == {"Name": "Phase9GWave12_Row", **row_data}
    assert payload["created"] is True