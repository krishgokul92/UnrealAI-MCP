"""Focused live-editor coverage for release-oriented editor utility helpers."""

from __future__ import annotations

import json
import sys
import time
from pathlib import Path
from typing import Any, Dict

import pytest


_PY_ROOT = Path(__file__).resolve().parent.parent
if str(_PY_ROOT) not in sys.path:
    sys.path.insert(0, str(_PY_ROOT))

from ue_bridge import ping, send_command  # noqa: E402


BRIDGE_AVAILABLE = ping()
SKIP_REASON = "UE bridge not reachable on 127.0.0.1:55557 (open the editor first)"
needs_bridge = pytest.mark.skipif(not BRIDGE_AVAILABLE, reason=SKIP_REASON)


def _unwrap(resp: Dict[str, Any]) -> Dict[str, Any]:
    if isinstance(resp, dict) and resp.get("status") == "success" and isinstance(resp.get("result"), dict):
        return resp["result"]
    return resp


def _ok(resp: Dict[str, Any], context: str) -> Dict[str, Any]:
    if not isinstance(resp, dict):
        pytest.fail(f"{context}: non-dict response: {resp!r}")
    if resp.get("status") == "error":
        pytest.fail(f"{context}: bridge error: {json.dumps(resp)[:800]}")
    return _unwrap(resp)


def _find_count(pattern: str) -> int:
    payload = _ok(send_command("find_actors_by_name", {"pattern": pattern}), f"find_actors_by_name({pattern})")
    return len(payload.get("actors", []))


@needs_bridge
class TestReleaseEditorHelpersLive:
    SUFFIX = str(int(time.time()))[-6:]

    @classmethod
    def _name(cls, base: str) -> str:
        return f"{base}_{cls.SUFFIX}"

    def test_01_grid_placement_and_undo_redo(self):
        _ok(send_command("new_blank_map", {"save_existing_map": False}), "new_blank_map")
        prefix = self._name("GridPlacement")

        placement = _ok(
            send_command(
                "place_in_grid",
                {
                    "actor_type": "StaticMeshActor",
                    "name_prefix": prefix,
                    "origin": [0.0, 0.0, 50.0],
                    "count_x": 3,
                    "step_x": 200.0,
                    "count_y": 2,
                    "step_y": 150.0,
                    "static_mesh": "/Engine/BasicShapes/Cube.Cube",
                },
            ),
            "place_in_grid",
        )

        assert placement.get("spawned_count") == 6, placement
        assert _find_count(prefix) == 6

        undo = _ok(send_command("undo_last_action", {}), "undo_last_action")
        assert undo.get("undid") is True, undo
        assert _find_count(prefix) == 0

        redo = _ok(send_command("redo_last_action", {}), "redo_last_action")
        assert redo.get("redid") is True, redo
        assert _find_count(prefix) == 6

    def test_02_circle_scatter_and_screenshot(self):
        _ok(send_command("new_blank_map", {"save_existing_map": False}), "new_blank_map")
        circle_prefix = self._name("CirclePlacement")
        scatter_prefix = self._name("ScatterPlacement")

        circle = _ok(
            send_command(
                "place_in_circle",
                {
                    "actor_type": "PointLight",
                    "name_prefix": circle_prefix,
                    "center": [0.0, 0.0, 200.0],
                    "radius": 500.0,
                    "count": 5,
                },
            ),
            "place_in_circle",
        )
        assert circle.get("spawned_count") == 5, circle
        assert _find_count(circle_prefix) == 5

        scatter = _ok(
            send_command(
                "scatter_in_area",
                {
                    "actor_type": "StaticMeshActor",
                    "name_prefix": scatter_prefix,
                    "min_corner": [-300.0, -300.0, 50.0],
                    "max_corner": [300.0, 300.0, 50.0],
                    "count": 4,
                    "seed": 77,
                    "shape": "ellipse",
                    "static_mesh": "/Engine/BasicShapes/Sphere.Sphere",
                },
            ),
            "scatter_in_area",
        )
        assert scatter.get("spawned_count") == 4, scatter
        assert _find_count(scatter_prefix) == 4

        screenshot = _ok(
            send_command(
                "capture_viewport_screenshot",
                {
                    "file_path": f"Saved/UnrealAI/pytest_release_{self.SUFFIX}",
                    "image_format": "png",
                },
            ),
            "capture_viewport_screenshot",
        )
        assert screenshot.get("file_path", "").lower().endswith(".png"), screenshot
        assert screenshot.get("width", 0) > 0, screenshot
        assert screenshot.get("height", 0) > 0, screenshot