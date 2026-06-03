"""Wrapper coverage for Phase 9g Wave 3 DataTable asset creation."""

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


def test_create_data_table_asset_maps_payload(monkeypatch):
    captured: Dict[str, Any] = {}

    def fake_bridge(command: str, payload: Dict[str, Any]) -> str:
        captured["command"] = command
        captured["payload"] = payload
        return json.dumps({
            "status": "success",
            "result": {
                "data_table_name": payload["data_table_name"],
                "row_struct_path": payload["row_struct_path"],
                "destination_path": payload["destination_path"],
                "created": True,
            },
        })

    monkeypatch.setattr(mcp, "_bridge", fake_bridge)

    response = mcp.create_data_table_asset(
        data_table_name="DT_TestWave3",
        row_struct_path="/Script/playtesting.WeaponData",
        destination_path="/Game/CopilotTests/DataTables",
    )
    payload = _decode(response)

    assert captured == {
        "command": "create_data_table_asset",
        "payload": {
            "data_table_name": "DT_TestWave3",
            "row_struct_path": "/Script/playtesting.WeaponData",
            "destination_path": "/Game/CopilotTests/DataTables",
        },
    }
    assert payload["data_table_name"] == "DT_TestWave3"
    assert payload["row_struct_path"] == "/Script/playtesting.WeaponData"
    assert payload["destination_path"] == "/Game/CopilotTests/DataTables"
    assert payload["created"] is True