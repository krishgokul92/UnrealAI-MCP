"""Pure-Python wrapper tests for the remaining Phase 9d landscape slice."""

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


def test_sample_landscape_weight_region_maps_payload(monkeypatch):
    calls = _capture_bridge(monkeypatch)

    result = mcp_module.sample_landscape_weight_region(
        "Landscape_Main",
        "Visibility",
        [100.0, 200.0, 300.0],
        [740.0, 920.0, 300.0],
        320.0,
        360.0,
    )

    assert result == '{"status":"ok"}'
    assert calls == [(
        "sample_landscape_weight_region",
        {
            "landscape_name": "Landscape_Main",
            "layer_name": "Visibility",
            "min_corner": [100.0, 200.0, 300.0],
            "max_corner": [740.0, 920.0, 300.0],
            "step_x": 320.0,
            "step_y": 360.0,
        },
    )]


def test_paint_landscape_layer_region_maps_payload(monkeypatch):
    calls = _capture_bridge(monkeypatch)

    result = mcp_module.paint_landscape_layer_region(
        "Landscape_Main",
        "Visibility",
        [100.0, 200.0, 300.0],
        [740.0, 920.0, 300.0],
        0.75,
    )

    assert result == '{"status":"ok"}'
    assert calls == [(
        "paint_landscape_layer_region",
        {
            "landscape_name": "Landscape_Main",
            "layer_name": "Visibility",
            "min_corner": [100.0, 200.0, 300.0],
            "max_corner": [740.0, 920.0, 300.0],
            "weight": 0.75,
        },
    )]


def test_sculpt_landscape_height_region_maps_payload(monkeypatch):
    calls = _capture_bridge(monkeypatch)

    result = mcp_module.sculpt_landscape_height_region(
        "Landscape_Main",
        [100.0, 200.0, 300.0],
        [740.0, 920.0, 300.0],
        512.0,
    )

    assert result == '{"status":"ok"}'
    assert calls == [(
        "sculpt_landscape_height_region",
        {
            "landscape_name": "Landscape_Main",
            "min_corner": [100.0, 200.0, 300.0],
            "max_corner": [740.0, 920.0, 300.0],
            "height_world": 512.0,
        },
    )]


def test_rebuild_landscape_maps_payload(monkeypatch):
    calls = _capture_bridge(monkeypatch)

    result = mcp_module.rebuild_landscape("Landscape_Main")

    assert result == '{"status":"ok"}'
    assert calls == [(
        "rebuild_landscape",
        {
            "landscape_name": "Landscape_Main",
        },
    )]