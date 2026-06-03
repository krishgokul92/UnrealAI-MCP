"""Pure-Python wrapper tests for the first Phase 9d landscape sampling slice."""

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


def test_sample_landscape_point_maps_payload(monkeypatch):
    calls = _capture_bridge(monkeypatch)

    result = mcp_module.sample_landscape_point("Landscape_Main", [100.0, 200.0, 300.0])

    assert result == '{"status":"ok"}'
    assert calls == [(
        "sample_landscape_point",
        {
            "landscape_name": "Landscape_Main",
            "location": [100.0, 200.0, 300.0],
        },
    )]