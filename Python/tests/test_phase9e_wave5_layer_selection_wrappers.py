"""Pure-Python wrapper tests for the fifth Phase 9e layer-selection slice."""

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


def test_select_actors_in_layer_maps_payload(monkeypatch):
    calls = _capture_bridge(monkeypatch)

    result = mcp_module.select_actors_in_layer(
        "Gameplay",
        replace_selection=False,
        select_even_if_hidden=True,
    )

    assert result == '{"status":"ok"}'
    assert calls == [(
        "select_actors_in_layer",
        {
            "layer_name": "Gameplay",
            "replace_selection": False,
            "select_even_if_hidden": True,
        },
    )]


def test_deselect_actors_in_layer_maps_payload(monkeypatch):
    calls = _capture_bridge(monkeypatch)

    result = mcp_module.deselect_actors_in_layer("Gameplay")

    assert result == '{"status":"ok"}'
    assert calls == [(
        "deselect_actors_in_layer",
        {
            "layer_name": "Gameplay",
        },
    )]