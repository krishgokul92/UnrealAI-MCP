"""Pure-Python wrapper tests for Phase 9f Wave 1 Level Sequence tools."""

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


def test_create_level_sequence_maps_payload(monkeypatch):
    calls = _capture_bridge(monkeypatch)

    result = mcp_module.create_level_sequence(
        sequence_name="LS_TestIntro",
        destination_path="/Game/Cinematics/Test",
    )

    assert result == '{"status":"ok"}'
    assert calls == [(
        "create_level_sequence",
        {
            "sequence_name": "LS_TestIntro",
            "destination_path": "/Game/Cinematics/Test",
        },
    )]


def test_read_level_sequence_content_maps_payload(monkeypatch):
    calls = _capture_bridge(monkeypatch)

    result = mcp_module.read_level_sequence_content("/Game/Cinematics/LS_TestIntro")

    assert result == '{"status":"ok"}'
    assert calls == [(
        "read_level_sequence_content",
        {
            "level_sequence_path": "/Game/Cinematics/LS_TestIntro",
        },
    )]