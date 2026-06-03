"""Live-editor smoke tests for Phase 9c Wave 5 widget animation mutation helpers."""

from __future__ import annotations

import json
import sys
import time
from pathlib import Path
from typing import Any, Dict, Optional

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


def _create_widget_blueprint() -> str:
    suffix = int(time.time() * 1000) % 1000000
    widget_name = f"WBP_Phase9C_Wave5_Animations_{suffix}"
    last_error: Exception | None = None
    for _ in range(3):
        try:
            payload = _ok(send_command("create_widget_blueprint", {
                "widget_name": widget_name,
                "destination_path": "/Game/UI",
                "parent_class": "UserWidget",
                "root_widget_class": "CanvasPanel",
            }), f"create_widget_blueprint({widget_name})")
            return payload["data"]["widget_blueprint_path"] if "data" in payload else payload["widget_blueprint_path"]
        except BridgeError as exc:
            last_error = exc
            time.sleep(1)

    raise AssertionError(f"create_widget_blueprint({widget_name}) failed after retries: {last_error}")


def _read_widget_blueprint(widget_blueprint_path: str) -> Dict[str, Any]:
    payload = _ok(send_command("read_widget_blueprint_content", {
        "widget_blueprint_path": widget_blueprint_path,
    }), f"read_widget_blueprint_content({widget_blueprint_path})")
    return payload.get("data", payload)


def _find_animation(content: Dict[str, Any], animation_name: str) -> Optional[Dict[str, Any]]:
    for animation in content.get("animations") or []:
        if animation.get("name") == animation_name:
            return animation
    return None


class TestPhase9cWave5WidgetAnimations:
    def test_01_create_and_remove_widget_animation(self):
        widget_blueprint_path = _create_widget_blueprint()

        created = _ok(send_command("create_widget_animation_in_widget_blueprint", {
            "widget_blueprint_path": widget_blueprint_path,
            "animation_name": "Pulse",
        }), "create_widget_animation_in_widget_blueprint(Pulse)")
        created_data = created.get("data", created)
        actual_animation_name = created_data.get("animation_name") or "Pulse"

        content = _read_widget_blueprint(widget_blueprint_path)
        animation = _find_animation(content, actual_animation_name) or {}

        assert actual_animation_name.startswith("Pulse"), created_data
        assert created_data.get("animation_count") == 1, created_data
        assert created_data.get("requested_animation_name") == "Pulse", created_data
        assert content.get("animation_count") == 1, content
        assert animation.get("name") == actual_animation_name, animation

        removed = _ok(send_command("remove_widget_animation_from_widget_blueprint", {
            "widget_blueprint_path": widget_blueprint_path,
            "animation_name": actual_animation_name,
        }), "remove_widget_animation_from_widget_blueprint(Pulse)")
        removed_data = removed.get("data", removed)

        content_after_remove = _read_widget_blueprint(widget_blueprint_path)

        assert removed_data.get("animation_name") == actual_animation_name, removed_data
        assert removed_data.get("animation_count") == 0, removed_data
        assert content_after_remove.get("animation_count") == 0, content_after_remove
        assert _find_animation(content_after_remove, actual_animation_name) is None, content_after_remove