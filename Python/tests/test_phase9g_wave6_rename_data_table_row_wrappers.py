"""Wrapper coverage for Phase 9g Wave 6 DataTable row rename."""

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


def test_rename_data_table_row_maps_payload(monkeypatch):
    captured: Dict[str, Any] = {}

    def fake_bridge(command: str, payload: Dict[str, Any]) -> str:
        captured["command"] = command
        captured["payload"] = payload
        return json.dumps({
            "status": "success",
            "result": {
                "asset_path": payload["asset_path"],
                "old_row_name": payload["row_name"],
                "new_row_name": payload["new_row_name"],
                "renamed": True,
            },
        })

    monkeypatch.setattr(mcp, "_bridge", fake_bridge)

    response = mcp.rename_data_table_row(
        asset_path="/Game/CopilotTests/DataTables/DT_TestWave6.DT_TestWave6",
        row_name="Phase9GWave6_Old",
        new_row_name="Phase9GWave6_New",
    )
    payload = _decode(response)

    assert captured == {
        "command": "rename_data_table_row",
        "payload": {
            "asset_path": "/Game/CopilotTests/DataTables/DT_TestWave6.DT_TestWave6",
            "row_name": "Phase9GWave6_Old",
            "new_row_name": "Phase9GWave6_New",
        },
    }
    assert payload["asset_path"] == "/Game/CopilotTests/DataTables/DT_TestWave6.DT_TestWave6"
    assert payload["old_row_name"] == "Phase9GWave6_Old"
    assert payload["new_row_name"] == "Phase9GWave6_New"
    assert payload["renamed"] is True