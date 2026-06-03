"""Live-editor smoke tests for the first Phase 9e editor-context slice."""

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


SKIP_REASON = "UE bridge not reachable on 127.0.0.1:55557 (open the editor first)"
STARTUP_SETTLE_SECONDS = 10
VIEWPORT_TARGET_LOCATION = [1234.0, -5678.0, 910.0]


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


class TestPhase9eWave1EditorContext:
    def test_01_get_level_viewport_info_after_focus(self):
        _ok(send_command("focus_viewport", {
            "location": VIEWPORT_TARGET_LOCATION,
        }), "focus_viewport")

        payload = _ok(send_command("get_level_viewport_info", {}), "get_level_viewport_info")

        viewports = payload.get("viewports") or []
        assert payload.get("viewport_count") == len(viewports), payload
        assert len(viewports) >= 1, payload

        matched_target = False
        for index, viewport in enumerate(viewports):
            assert viewport.get("index") == index, viewport
            view_location = viewport.get("view_location")
            view_rotation = viewport.get("view_rotation")
            assert isinstance(view_location, list) and len(view_location) == 3, viewport
            assert isinstance(view_rotation, list) and len(view_rotation) == 3, viewport
            assert isinstance(viewport.get("is_perspective"), bool), viewport
            assert isinstance(viewport.get("is_realtime"), bool), viewport
            assert isinstance(viewport.get("fov"), (int, float)), viewport

            if all(abs(view_location[i] - VIEWPORT_TARGET_LOCATION[i]) <= 1.0 for i in range(3)):
                matched_target = True

        assert matched_target, payload

    def test_02_get_selected_actors_structure(self):
        payload = _ok(send_command("get_selected_actors", {}), "get_selected_actors")

        actors = payload.get("actors") or []
        assert payload.get("selected_count") == len(actors), payload
        assert isinstance(payload.get("current_level"), str) and payload.get("current_level"), payload

        for actor in actors:
            assert isinstance(actor.get("name"), str) and actor.get("name"), actor
            assert isinstance(actor.get("class"), str) and actor.get("class"), actor
            location = actor.get("location")
            assert isinstance(location, list) and len(location) == 3, actor