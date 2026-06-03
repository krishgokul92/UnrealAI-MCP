"""Pure-Python wrapper tests for the second Phase 9e editor-selection slice."""

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


def test_select_actors_maps_payload(monkeypatch):
    calls = _capture_bridge(monkeypatch)

    result = mcp_module.select_actors(["Actor_A", "Actor_B"], replace_selection=False)

    assert result == '{"status":"ok"}'
    assert calls == [(
        "select_actors",
        {
            "actor_names": ["Actor_A", "Actor_B"],
            "replace_selection": False,
        },
    )]


def test_clear_actor_selection_maps_payload(monkeypatch):
    calls = _capture_bridge(monkeypatch)

    result = mcp_module.clear_actor_selection()

    assert result == '{"status":"ok"}'
    assert calls == [(
        "clear_actor_selection",
        None,
    )]