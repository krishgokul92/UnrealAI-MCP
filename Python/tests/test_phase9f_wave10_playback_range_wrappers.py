"""Wrapper coverage for Phase 9f Wave 10 playback-range helpers."""

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


def test_set_level_sequence_playback_range_maps_payload(monkeypatch):
    captured: Dict[str, Any] = {}

    def fake_bridge(command: str, payload: Dict[str, Any]) -> str:
        captured["command"] = command
        captured["payload"] = payload
        return json.dumps({
            "status": "success",
            "result": {
                "success": True,
                "start_frame": payload["start_frame"],
                "end_frame": payload["end_frame"],
            },
        })

    monkeypatch.setattr(mcp, "_bridge", fake_bridge)

    response = mcp.set_level_sequence_playback_range(
        "/Game/Cinematics/LS_PlaybackRange",
        start_frame=100,
        end_frame=220,
    )
    payload = _decode(response)

    assert captured == {
        "command": "set_level_sequence_playback_range",
        "payload": {
            "level_sequence_path": "/Game/Cinematics/LS_PlaybackRange",
            "start_frame": 100,
            "end_frame": 220,
        },
    }
    assert payload["success"] is True
    assert payload["start_frame"] == 100
    assert payload["end_frame"] == 220