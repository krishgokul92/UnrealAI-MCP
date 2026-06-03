"""Wrapper coverage for Phase 9f Wave 6 section removal helpers."""

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


def test_remove_section_from_master_track_in_level_sequence_maps_payload(monkeypatch):
    captured: Dict[str, Any] = {}

    def fake_bridge(command: str, payload: Dict[str, Any]) -> str:
        captured["command"] = command
        captured["payload"] = payload
        return json.dumps({
            "status": "success",
            "result": {
                "success": True,
                "master_track_count": 1,
                "removed_section_index": payload["section_index"],
            },
        })

    monkeypatch.setattr(mcp, "_bridge", fake_bridge)

    response = mcp.remove_section_from_master_track_in_level_sequence(
        "/Game/Cinematics/LS_RemoveSection",
        section_index=2,
        track_class="/Script/MovieSceneTracks.MovieSceneCinematicShotTrack",
    )
    payload = _decode(response)

    assert captured == {
        "command": "remove_section_from_master_track_in_level_sequence",
        "payload": {
            "level_sequence_path": "/Game/Cinematics/LS_RemoveSection",
            "track_class": "/Script/MovieSceneTracks.MovieSceneCinematicShotTrack",
            "section_index": 2,
        },
    }
    assert payload["success"] is True
    assert payload["removed_section_index"] == 2