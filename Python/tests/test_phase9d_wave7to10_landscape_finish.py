"""Live-editor smoke tests for the remaining Phase 9d landscape slice."""

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

from ue_bridge import BridgeError, ping, send_command  # noqa: E402


SKIP_REASON = "UE bridge not reachable on 127.0.0.1:55557 (open the editor first)"
STARTUP_SETTLE_SECONDS = 20
WEIGHT_CREATE_LOCATION = [54000.0, 0.0, 0.0]
REGION_WIDTH_WORLD = 3200.0
REGION_HEIGHT_WORLD = 1600.0
WEIGHT_STEP_X = 1600.0
WEIGHT_STEP_Y = 1600.0
SCULPT_CREATE_LOCATION = [62000.0, 0.0, 0.0]
SCULPT_TARGET_HEIGHT = 1024.0
SCULPT_STEP_X = 1600.0
SCULPT_STEP_Y = 1600.0


@pytest.fixture(scope="module", autouse=True)
def require_bridge() -> None:
    for _ in range(30):
        if ping():
            time.sleep(STARTUP_SETTLE_SECONDS)
            return
        time.sleep(1)
    pytest.skip(SKIP_REASON)


def _unwrap(resp: Dict[str, Any]) -> Dict[str, Any]:
    if isinstance(resp, dict) and resp.get("status") == "success" and isinstance(resp.get("result"), dict):
        return resp["result"]
    return resp


def _ok(resp: Dict[str, Any], context: str) -> Dict[str, Any]:
    if not isinstance(resp, dict):
        pytest.fail(f"{context}: non-dict response: {resp!r}")
    if resp.get("status") == "error":
        pytest.fail(f"{context}: bridge error: {json.dumps(resp)[:600]}")
    return _unwrap(resp)


def _create_landscape(prefix: str, location: list[float]) -> str:
    suffix = int(time.time() * 1000) % 1000000
    landscape_name = f"{prefix}_{suffix}"
    last_error: Exception | None = None
    for _ in range(3):
        try:
            payload = _ok(send_command("create_landscape", {
                "name": landscape_name,
                "location": location,
                "component_count_x": 2,
                "component_count_y": 2,
                "sections_per_component": 1,
                "quads_per_section": 63,
                "base_height": 0.0,
            }), f"create_landscape({landscape_name})")
            actual_name = payload.get("label") or payload.get("name") or landscape_name
            return actual_name
        except BridgeError as exc:
            last_error = exc
            time.sleep(1)

    raise AssertionError(f"create_landscape({landscape_name}) failed after retries: {last_error}")


def _get_landscape_region(landscape_name: str) -> tuple[list[float], list[float]]:
    payload = _ok(send_command("read_landscape_content", {
        "landscape_name": landscape_name,
    }), f"read_landscape_content({landscape_name})")

    location = payload.get("location") or [0.0, 0.0, 0.0]
    scale = payload.get("scale") or [100.0, 100.0, 100.0]
    extent = payload.get("extent") or [0.0, 0.0, 0.0, 0.0]
    min_corner = [
        location[0] + extent[0] * scale[0],
        location[1] + extent[1] * scale[1],
        location[2],
    ]
    max_corner = [
        min_corner[0] + REGION_WIDTH_WORLD,
        min_corner[1] + REGION_HEIGHT_WORLD,
        min_corner[2],
    ]
    return min_corner, max_corner


class TestPhase9dWave7to10LandscapeFinish:
    def test_01_paint_visibility_region_and_sample_weight_region(self):
        landscape_name = _create_landscape("Landscape_Phase9D_Wave7", WEIGHT_CREATE_LOCATION)
        region_min, region_max = _get_landscape_region(landscape_name)

        before = _ok(send_command("sample_landscape_weight_region", {
            "landscape_name": landscape_name,
            "layer_name": "Visibility",
            "min_corner": region_min,
            "max_corner": region_max,
            "step_x": WEIGHT_STEP_X,
            "step_y": WEIGHT_STEP_Y,
        }), f"sample_landscape_weight_region_before({landscape_name})")

        paint = _ok(send_command("paint_landscape_layer_region", {
            "landscape_name": landscape_name,
            "layer_name": "Visibility",
            "min_corner": region_min,
            "max_corner": region_max,
            "weight": 1.0,
        }), f"paint_landscape_layer_region({landscape_name})")

        rebuild = _ok(send_command("rebuild_landscape", {
            "landscape_name": landscape_name,
        }), f"rebuild_landscape({landscape_name})")

        after = _ok(send_command("sample_landscape_weight_region", {
            "landscape_name": landscape_name,
            "layer_name": "Visibility",
            "min_corner": region_min,
            "max_corner": region_max,
            "step_x": WEIGHT_STEP_X,
            "step_y": WEIGHT_STEP_Y,
        }), f"sample_landscape_weight_region_after({landscape_name})")

        assert before.get("label") == landscape_name, before
        assert before.get("layer_name") == "Visibility", before
        assert before.get("sample_count") == 6, before
        assert before.get("count_x") == 3, before
        assert before.get("count_y") == 2, before
        assert paint.get("layer_name") == "Visibility", paint
        assert paint.get("weight_byte") == 255, paint
        assert paint.get("width") == 33, paint
        assert paint.get("height") == 17, paint
        assert rebuild.get("rebuilt") is True, rebuild
        assert after.get("label") == landscape_name, after
        assert after.get("layer_name") == "Visibility", after
        assert after.get("sample_count") == 6, after
        assert after.get("count_x") == 3, after
        assert after.get("count_y") == 2, after
        assert after.get("weight_max", 0.0) >= before.get("weight_max", 0.0), (before, after)

    def test_02_sculpt_landscape_height_region(self):
        landscape_name = _create_landscape("Landscape_Phase9D_Wave8", SCULPT_CREATE_LOCATION)
        region_min, region_max = _get_landscape_region(landscape_name)

        sculpt = _ok(send_command("sculpt_landscape_height_region", {
            "landscape_name": landscape_name,
            "min_corner": region_min,
            "max_corner": region_max,
            "height_world": SCULPT_TARGET_HEIGHT,
        }), f"sculpt_landscape_height_region({landscape_name})")

        sampled = _ok(send_command("sample_landscape_height_region", {
            "landscape_name": landscape_name,
            "min_corner": region_min,
            "max_corner": region_max,
            "step_x": SCULPT_STEP_X,
            "step_y": SCULPT_STEP_Y,
        }), f"sample_landscape_height_region({landscape_name})")

        assert sculpt.get("landscape_name") == landscape_name, sculpt
        assert sampled.get("label") == landscape_name, sampled
        assert sampled.get("count_x") == 3, sampled
        assert sampled.get("count_y") == 2, sampled
        assert abs(sampled.get("height_min_world", -99999.0) - SCULPT_TARGET_HEIGHT) <= 4.0, sampled
        assert abs(sampled.get("height_max_world", -99999.0) - SCULPT_TARGET_HEIGHT) <= 4.0, sampled
        height_rows = sampled.get("height_rows") or []
        assert len(height_rows) == 2, sampled
        for row in height_rows:
            assert len(row) == 3, row
            for height_value in row:
                assert abs(height_value - SCULPT_TARGET_HEIGHT) <= 4.0, row

    def test_03_rebuild_landscape_on_fresh_landscape(self):
        landscape_name = _create_landscape("Landscape_Phase9D_Wave10", [70000.0, 0.0, 0.0])

        payload = _ok(send_command("rebuild_landscape", {
            "landscape_name": landscape_name,
        }), f"rebuild_landscape({landscape_name})")

        assert payload.get("landscape_name") == landscape_name, payload
        assert payload.get("rebuilt") is True, payload