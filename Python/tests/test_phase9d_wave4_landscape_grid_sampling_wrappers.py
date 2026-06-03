"""Pure-Python wrapper tests for the Phase 9d landscape grid sampling slice."""

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


def test_sample_landscape_grid_maps_payload(monkeypatch):
    calls = _capture_bridge(monkeypatch)

    result = mcp_module.sample_landscape_grid(
        "Landscape_Main",
        [100.0, 200.0, 300.0],
        256.0,
        512.0,
        3,
        2,
    )

    assert result == '{"status":"ok"}'
    assert calls == [(
        "sample_landscape_grid",
        {
            "landscape_name": "Landscape_Main",
            "origin": [100.0, 200.0, 300.0],
            "step_x": 256.0,
            "step_y": 512.0,
            "count_x": 3,
            "count_y": 2,
        },
    )]