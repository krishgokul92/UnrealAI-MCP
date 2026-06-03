"""Pure-Python wrapper tests for the first Phase 9d landscape inspection slice."""

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


def test_read_landscape_content_maps_payload(monkeypatch):
    calls = _capture_bridge(monkeypatch)

    result = mcp_module.read_landscape_content("Landscape_Main")

    assert result == '{"status":"ok"}'
    assert calls == [(
        "read_landscape_content",
        {
            "landscape_name": "Landscape_Main",
        },
    )]