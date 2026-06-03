"""Pure-Python wrapper tests for the fourth Phase 9e actor-layer mutation slice."""

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


def test_add_actor_to_layer_maps_payload(monkeypatch):
    calls = _capture_bridge(monkeypatch)

    result = mcp_module.add_actor_to_layer("LayeredCube_A", "Gameplay")

    assert result == '{"status":"ok"}'
    assert calls == [(
        "add_actor_to_layer",
        {
            "actor_name": "LayeredCube_A",
            "layer_name": "Gameplay",
        },
    )]


def test_remove_actor_from_layer_maps_payload(monkeypatch):
    calls = _capture_bridge(monkeypatch)

    result = mcp_module.remove_actor_from_layer("LayeredCube_A", "Gameplay")

    assert result == '{"status":"ok"}'
    assert calls == [(
        "remove_actor_from_layer",
        {
            "actor_name": "LayeredCube_A",
            "layer_name": "Gameplay",
        },
    )]