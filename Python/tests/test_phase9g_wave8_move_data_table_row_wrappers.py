"""Wrapper coverage for Phase 9g Wave 8 DataTable row move."""

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


def test_move_data_table_row_maps_payload(monkeypatch):
    captured: Dict[str, Any] = {}

    def fake_bridge(command: str, payload: Dict[str, Any]) -> str:
        captured["command"] = command
        captured["payload"] = payload
        return json.dumps({
            "status": "success",
            "result": {
                "asset_path": payload["asset_path"],
                "row_name": payload["row_name"],
                "direction": payload["direction"],
                "num_rows_to_move_by": payload["num_rows_to_move_by"],
                "moved": True,
            },
        })

    monkeypatch.setattr(mcp, "_bridge", fake_bridge)

    response = mcp.move_data_table_row(
        asset_path="/Game/CopilotTests/DataTables/DT_TestWave8.DT_TestWave8",
        row_name="Phase9GWave8_C",
        direction="up",
        num_rows_to_move_by=2,
    )
    payload = _decode(response)

    assert captured == {
        "command": "move_data_table_row",
        "payload": {
            "asset_path": "/Game/CopilotTests/DataTables/DT_TestWave8.DT_TestWave8",
            "row_name": "Phase9GWave8_C",
            "direction": "up",
            "num_rows_to_move_by": 2,
        },
    }
    assert payload["asset_path"] == "/Game/CopilotTests/DataTables/DT_TestWave8.DT_TestWave8"
    assert payload["row_name"] == "Phase9GWave8_C"
    assert payload["direction"] == "up"
    assert payload["num_rows_to_move_by"] == 2
    assert payload["moved"] is True