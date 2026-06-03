"""Wrapper coverage for Phase 9g Wave 9 CurveTable inspection."""

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


def test_read_curve_table_content_maps_payload(monkeypatch):
    captured: Dict[str, Any] = {}

    def fake_bridge(command: str, payload: Dict[str, Any]) -> str:
        captured["command"] = command
        captured["payload"] = payload
        return json.dumps({
            "status": "success",
            "result": {
                "asset_path": payload["asset_path"],
                "curve_table_mode": "RichCurves",
                "row_count": 2,
            },
        })

    monkeypatch.setattr(mcp, "_bridge", fake_bridge)

    response = mcp.read_curve_table_content(
        asset_path="/Engine/Fake/CT_Test.CT_Test",
    )
    payload = _decode(response)

    assert captured == {
        "command": "read_curve_table_content",
        "payload": {
            "asset_path": "/Engine/Fake/CT_Test.CT_Test",
        },
    }
    assert payload["asset_path"] == "/Engine/Fake/CT_Test.CT_Test"
    assert payload["curve_table_mode"] == "RichCurves"
    assert payload["row_count"] == 2