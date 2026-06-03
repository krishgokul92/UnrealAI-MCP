"""Wrapper coverage for Phase 9g Wave 7 DataTable row duplicate."""

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


def test_duplicate_data_table_row_maps_payload(monkeypatch):
    captured: Dict[str, Any] = {}

    def fake_bridge(command: str, payload: Dict[str, Any]) -> str:
        captured["command"] = command
        captured["payload"] = payload
        return json.dumps({
            "status": "success",
            "result": {
                "asset_path": payload["asset_path"],
                "source_row_name": payload["source_row_name"],
                "new_row_name": payload["new_row_name"],
                "duplicated": True,
            },
        })

    monkeypatch.setattr(mcp, "_bridge", fake_bridge)

    response = mcp.duplicate_data_table_row(
        asset_path="/Game/CopilotTests/DataTables/DT_TestWave7.DT_TestWave7",
        source_row_name="Phase9GWave7_Source",
        new_row_name="Phase9GWave7_Copy",
    )
    payload = _decode(response)

    assert captured == {
        "command": "duplicate_data_table_row",
        "payload": {
            "asset_path": "/Game/CopilotTests/DataTables/DT_TestWave7.DT_TestWave7",
            "source_row_name": "Phase9GWave7_Source",
            "new_row_name": "Phase9GWave7_Copy",
        },
    }
    assert payload["asset_path"] == "/Game/CopilotTests/DataTables/DT_TestWave7.DT_TestWave7"
    assert payload["source_row_name"] == "Phase9GWave7_Source"
    assert payload["new_row_name"] == "Phase9GWave7_Copy"
    assert payload["duplicated"] is True