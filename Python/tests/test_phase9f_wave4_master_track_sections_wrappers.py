"""Pure-Python wrapper tests for Phase 9f Wave 4 master-track section helpers."""

from __future__ import annotations

import sys
from pathlib import Path


_PY_ROOT = Path(__file__).resolve().parent.parent
if str(_PY_ROOT) not in sys.path:
    sys.path.insert(0, str(_PY_ROOT))

import unreal_ai_mcp as mcp_module  # noqa: E402


def _capture_bridge(monkeypatch):
    calls = []

    def fake_bridge(cmd, params=None):
        calls.append((cmd, params))
        return '{"status":"ok"}'

    monkeypatch.setattr(mcp_module, "_bridge", fake_bridge)
    return calls


def test_add_section_to_master_track_in_level_sequence_maps_payload(monkeypatch):
    calls = _capture_bridge(monkeypatch)

    result = mcp_module.add_section_to_master_track_in_level_sequence(
        level_sequence_path="/Game/Cinematics/LS_TestIntro",
        track_class="/Script/MovieSceneTracks.MovieSceneCinematicShotTrack",
        start_frame=10,
        end_frame=40,
    )

    assert result == '{"status":"ok"}'
    assert calls == [(
        "add_section_to_master_track_in_level_sequence",
        {
            "level_sequence_path": "/Game/Cinematics/LS_TestIntro",
            "track_class": "/Script/MovieSceneTracks.MovieSceneCinematicShotTrack",
            "start_frame": 10,
            "end_frame": 40,
        },
    )]