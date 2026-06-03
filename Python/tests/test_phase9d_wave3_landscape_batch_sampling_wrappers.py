"""Pure-Python wrapper tests for the Phase 9d batched landscape sampling slice."""

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


def test_sample_landscape_points_maps_payload(monkeypatch):
    calls = _capture_bridge(monkeypatch)

    result = mcp_module.sample_landscape_points("Landscape_Main", [
        [100.0, 200.0, 300.0],
        [400.0, 500.0, 600.0],
    ])

    assert result == '{"status":"ok"}'
    assert calls == [(
        "sample_landscape_points",
        {
            "landscape_name": "Landscape_Main",
            "locations": [
                [100.0, 200.0, 300.0],
                [400.0, 500.0, 600.0],
            ],
        },
    )]