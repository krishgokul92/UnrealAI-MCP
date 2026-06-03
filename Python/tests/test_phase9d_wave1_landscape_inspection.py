"""Live-editor smoke tests for the first Phase 9d landscape inspection slice."""

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


@pytest.fixture(scope="module", autouse=True)
def require_bridge() -> None:
    for _ in range(30):
        if ping():
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
    landscape_name = f"Landscape_Phase9D_Wave1_{suffix}"
    last_error: Exception | None = None
    for _ in range(3):
        try:
            payload = _ok(send_command("create_landscape", {
                "name": landscape_name,
                "location": [7000.0, 0.0, 0.0],
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


class TestPhase9dWave1LandscapeInspection:
    def test_01_read_landscape_content_after_create(self):
        landscape_name = _create_landscape()

        payload = _ok(send_command("read_landscape_content", {
            "landscape_name": landscape_name,
        }), f"read_landscape_content({landscape_name})")

        assert payload.get("label") == landscape_name, payload
        assert payload.get("edit_layer_count", -1) >= 1, payload
        assert isinstance(payload.get("edit_layers"), list), payload
        assert isinstance(payload.get("target_layer_names"), list), payload
        assert isinstance(payload.get("layer_info_objects"), list), payload

        first_layer = (payload.get("edit_layers") or [{}])[0]
        assert first_layer.get("index") == 0, first_layer
        assert "brush_count" in first_layer, first_layer
        assert "used_paint_layers" in first_layer, first_layer