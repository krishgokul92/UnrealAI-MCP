"""Pure-Python wrapper tests for release-focused editor utility helpers."""

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


def test_save_and_history_helpers_map_empty_payloads(monkeypatch):
    calls = _capture_bridge(monkeypatch)

    assert mcp_module.save_level() == '{"status":"ok"}'
    assert mcp_module.undo_last_action() == '{"status":"ok"}'
    assert mcp_module.redo_last_action() == '{"status":"ok"}'
    assert calls == [
        ("save_level", {}),
        ("undo_last_action", {}),
        ("redo_last_action", {}),
    ]


def test_capture_viewport_screenshot_maps_optional_file_path(monkeypatch):
    calls = _capture_bridge(monkeypatch)

    result = mcp_module.capture_viewport_screenshot(file_path="Saved/UnrealAI/TestShot")

    assert result == '{"status":"ok"}'
    assert calls == [(
        "capture_viewport_screenshot",
        {
            "image_format": "png",
            "file_path": "Saved/UnrealAI/TestShot",
        },
    )]


def test_place_in_grid_maps_payload(monkeypatch):
    calls = _capture_bridge(monkeypatch)

    result = mcp_module.place_in_grid(
        actor_type="StaticMeshActor",
        name_prefix="GridRock",
        origin=[0.0, 0.0, 50.0],
        count_x=4,
        step_x=200.0,
        count_y=2,
        step_y=150.0,
        rotation=[0.0, 90.0, 0.0],
        scale=[1.0, 1.0, 1.0],
        static_mesh="/Engine/BasicShapes/Cube.Cube",
    )

    assert result == '{"status":"ok"}'
    assert calls == [(
        "place_in_grid",
        {
            "actor_type": "StaticMeshActor",
            "name_prefix": "GridRock",
            "origin": [0.0, 0.0, 50.0],
            "count_x": 4,
            "step_x": 200.0,
            "count_y": 2,
            "step_y": 150.0,
            "rotation": [0.0, 90.0, 0.0],
            "scale": [1.0, 1.0, 1.0],
            "static_mesh": "/Engine/BasicShapes/Cube.Cube",
        },
    )]


def test_place_in_circle_maps_payload(monkeypatch):
    calls = _capture_bridge(monkeypatch)

    result = mcp_module.place_in_circle(
        actor_type="PointLight",
        name_prefix="CircleLight",
        center=[100.0, 200.0, 300.0],
        radius=500.0,
        count=6,
        start_angle_degrees=30.0,
    )

    assert result == '{"status":"ok"}'
    assert calls == [(
        "place_in_circle",
        {
            "actor_type": "PointLight",
            "name_prefix": "CircleLight",
            "center": [100.0, 200.0, 300.0],
            "radius": 500.0,
            "count": 6,
            "start_angle_degrees": 30.0,
        },
    )]


def test_place_along_spline_maps_payload(monkeypatch):
    calls = _capture_bridge(monkeypatch)

    result = mcp_module.place_along_spline(
        actor_type="StaticMeshActor",
        name_prefix="SplineFence",
        spline_actor_name="RoadSplineActor",
        count=8,
        orientation_mode="full",
        static_mesh="/Engine/BasicShapes/Cylinder.Cylinder",
    )

    assert result == '{"status":"ok"}'
    assert calls == [(
        "place_along_spline",
        {
            "actor_type": "StaticMeshActor",
            "name_prefix": "SplineFence",
            "spline_actor_name": "RoadSplineActor",
            "count": 8,
            "orientation_mode": "full",
            "static_mesh": "/Engine/BasicShapes/Cylinder.Cylinder",
        },
    )]


def test_scatter_in_area_maps_payload(monkeypatch):
    calls = _capture_bridge(monkeypatch)

    result = mcp_module.scatter_in_area(
        actor_type="StaticMeshActor",
        name_prefix="ScatterTree",
        min_corner=[-500.0, -250.0, 0.0],
        max_corner=[500.0, 250.0, 0.0],
        count=10,
        seed=99,
        shape="ellipse",
    )

    assert result == '{"status":"ok"}'
    assert calls == [(
        "scatter_in_area",
        {
            "actor_type": "StaticMeshActor",
            "name_prefix": "ScatterTree",
            "min_corner": [-500.0, -250.0, 0.0],
            "max_corner": [500.0, 250.0, 0.0],
            "count": 10,
            "seed": 99,
            "shape": "ellipse",
        },
    )]