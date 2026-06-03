"""Wrapper coverage for Phase 9f Wave 9 float-key helpers."""

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


def test_add_float_key_to_binding_track_in_level_sequence_maps_payload(monkeypatch):
    captured: Dict[str, Any] = {}

    def fake_bridge(command: str, payload: Dict[str, Any]) -> str:
        captured["command"] = command
        captured["payload"] = payload
        return json.dumps({
            "status": "success",
            "result": {
                "success": True,
                "key_created": True,
                "key_frame": payload["frame"],
                "key_value": payload["value"],
            },
        })

    monkeypatch.setattr(mcp, "_bridge", fake_bridge)

    response = mcp.add_float_key_to_binding_track_in_level_sequence(
        "/Game/Cinematics/LS_FloatKeys",
        binding_guid="guid-123",
        property_name="CustomTimeDilation",
        property_path="CustomTimeDilation",
        frame=25,
        value=2.5,
        section_start_frame=0,
        section_end_frame=60,
        interpolation="linear",
    )
    payload = _decode(response)

    assert captured == {
        "command": "add_float_key_to_binding_track_in_level_sequence",
        "payload": {
            "level_sequence_path": "/Game/Cinematics/LS_FloatKeys",
            "binding_guid": "guid-123",
            "property_name": "CustomTimeDilation",
            "property_path": "CustomTimeDilation",
            "frame": 25,
            "value": 2.5,
            "section_start_frame": 0,
            "section_end_frame": 60,
            "interpolation": "linear",
        },
    }
    assert payload["success"] is True
    assert payload["key_created"] is True
    assert payload["key_frame"] == 25
    assert payload["key_value"] == 2.5