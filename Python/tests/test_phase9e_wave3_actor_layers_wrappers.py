"""Pure-Python wrapper tests for the third Phase 9e editor-layer slice."""

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


def test_get_actor_layers_maps_payload(monkeypatch):
    calls = _capture_bridge(monkeypatch)

    result = mcp_module.get_actor_layers("LayeredCube_A")

    assert result == '{"status":"ok"}'
    assert calls == [(
        "get_actor_layers",
        {
            "actor_name": "LayeredCube_A",
        },
    )]


def test_get_actors_in_layer_maps_payload(monkeypatch):
    calls = _capture_bridge(monkeypatch)

    result = mcp_module.get_actors_in_layer("Gameplay")

    assert result == '{"status":"ok"}'
    assert calls == [(
        "get_actors_in_layer",
        {
            "layer_name": "Gameplay",
        },
    )]