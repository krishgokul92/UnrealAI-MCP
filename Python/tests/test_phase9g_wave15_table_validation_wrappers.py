"""Wrapper coverage for Phase 9g Wave 15 table validation helpers."""

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


def test_validate_data_table_row_import_maps_payload(monkeypatch):
    captured: Dict[str, Any] = {}
    row_data = {"Damage": 42}

    def fake_bridge(command: str, payload: Dict[str, Any]) -> str:
        captured["command"] = command
        captured["payload"] = payload
        return json.dumps({
            "status": "success",
            "result": {
                "asset_path": payload["asset_path"],
                "row_name": payload["row_name"],
                "is_valid": True,
            },
        })

    monkeypatch.setattr(mcp, "_bridge", fake_bridge)

    response = mcp.validate_data_table_row_import(
        asset_path="/Game/CopilotTests/DataTables/DT_TestWave15.DT_TestWave15",
        row_name="Phase9GWave15_Row",
        row_data=row_data,
    )
    payload = _decode(response)

    assert captured == {
        "command": "validate_data_table_row_import",
        "payload": {
            "asset_path": "/Game/CopilotTests/DataTables/DT_TestWave15.DT_TestWave15",
            "row_name": "Phase9GWave15_Row",
            "row_data": row_data,
        },
    }
    assert payload["asset_path"] == "/Game/CopilotTests/DataTables/DT_TestWave15.DT_TestWave15"
    assert payload["row_name"] == "Phase9GWave15_Row"
    assert payload["is_valid"] is True


def test_validate_curve_table_row_import_maps_payload(monkeypatch):
    captured: Dict[str, Any] = {}
    row_data = {"0": 1.5, "1": 3.0}

    def fake_bridge(command: str, payload: Dict[str, Any]) -> str:
        captured["command"] = command
        captured["payload"] = payload
        return json.dumps({
            "status": "success",
            "result": {
                "asset_path": payload["asset_path"],
                "row_name": payload["row_name"],
                "is_valid": True,
            },
        })

    monkeypatch.setattr(mcp, "_bridge", fake_bridge)

    response = mcp.validate_curve_table_row_import(
        asset_path="/Game/CopilotTests/CurveTables/CT_TestWave15.CT_TestWave15",
        row_name="Phase9GWave15_Curve",
        row_data=row_data,
    )
    payload = _decode(response)

    assert captured == {
        "command": "validate_curve_table_row_import",
        "payload": {
            "asset_path": "/Game/CopilotTests/CurveTables/CT_TestWave15.CT_TestWave15",
            "row_name": "Phase9GWave15_Curve",
            "row_data": row_data,
        },
    }
    assert payload["asset_path"] == "/Game/CopilotTests/CurveTables/CT_TestWave15.CT_TestWave15"
    assert payload["row_name"] == "Phase9GWave15_Curve"
    assert payload["is_valid"] is True