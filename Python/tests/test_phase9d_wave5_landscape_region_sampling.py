"""Live-editor smoke tests for the Phase 9d landscape region sampling slice."""

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
CREATE_LOCATION = [38000.0, 0.0, 0.0]
REGION_MIN = CREATE_LOCATION
REGION_MAX = [CREATE_LOCATION[0] + 3200.0, CREATE_LOCATION[1] + 1600.0, CREATE_LOCATION[2]]
REGION_STEP_X = 1600.0
REGION_STEP_Y = 1600.0
EXPECTED_COUNT_X = 3
EXPECTED_COUNT_Y = 2
STARTUP_SETTLE_SECONDS = 20


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


def _create_landscape() -> str:
    suffix = int(time.time() * 1000) % 1000000
    landscape_name = f"Landscape_Phase9D_Wave5_{suffix}"
    last_error: Exception | None = None
    for _ in range(3):
        try:
            payload = _ok(send_command("create_landscape", {
                "name": landscape_name,
                "location": CREATE_LOCATION,
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


class TestPhase9dWave5LandscapeRegionSampling:
    def test_01_sample_landscape_region_after_flatten(self):
        landscape_name = _create_landscape()
        target_height = 768.0

        _ok(send_command("set_landscape_flat_height", {
            "landscape_name": landscape_name,
            "height_world": target_height,
        }), f"set_landscape_flat_height({landscape_name})")

        payload = _ok(send_command("sample_landscape_region", {
            "landscape_name": landscape_name,
            "min_corner": REGION_MIN,
            "max_corner": REGION_MAX,
            "step_x": REGION_STEP_X,
            "step_y": REGION_STEP_Y,
        }), f"sample_landscape_region({landscape_name})")

        assert payload.get("label") == landscape_name, payload
        assert payload.get("min_corner") == REGION_MIN, payload
        assert payload.get("max_corner") == REGION_MAX, payload
        assert payload.get("sampled_max_corner") == REGION_MAX, payload
        assert payload.get("count_x") == EXPECTED_COUNT_X, payload
        assert payload.get("count_y") == EXPECTED_COUNT_Y, payload
        assert payload.get("sample_count") == EXPECTED_COUNT_X * EXPECTED_COUNT_Y, payload
        assert payload.get("step_x") == REGION_STEP_X, payload
        assert payload.get("step_y") == REGION_STEP_Y, payload
        assert payload.get("target_layer_count") == 0, payload

        samples = payload.get("samples") or []
        assert len(samples) == EXPECTED_COUNT_X * EXPECTED_COUNT_Y, payload
        for sample in samples:
            assert sample.get("component_name"), sample
            assert isinstance(sample.get("component_extent"), list), sample
            assert isinstance(sample.get("layer_weights"), list), sample
            assert sample.get("layer_weight_count") == 0, sample
            assert 0 <= sample.get("grid_x", -1) < EXPECTED_COUNT_X, sample
            assert 0 <= sample.get("grid_y", -1) < EXPECTED_COUNT_Y, sample
            expected_location = [
                REGION_MIN[0] + sample["grid_x"] * REGION_STEP_X,
                REGION_MIN[1] + sample["grid_y"] * REGION_STEP_Y,
                REGION_MIN[2],
            ]
            assert sample.get("location") == expected_location, sample
            assert abs(sample.get("height_world", -99999.0) - target_height) <= 4.0, sample