"""Live-editor smoke tests for Phase 9c Wave 1 widget blueprint inspection."""

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
        pytest.fail(f"{context}: bridge error: {json.dumps(resp)[:600]}")
    return _unwrap(resp)


def _create_widget_blueprint() -> str:
    suffix = int(time.time()) % 1000000
    widget_name = f"WBP_Phase9C_Wave1_{suffix}"
    payload = _ok(send_command("create_widget_blueprint", {
        "widget_name": widget_name,
        "destination_path": "/Game/UI",
        "parent_class": "UserWidget",
        "root_widget_class": "CanvasPanel",
    }), f"create_widget_blueprint({widget_name})")
    return payload["data"]["widget_blueprint_path"] if "data" in payload else payload["widget_blueprint_path"]


@needs_bridge
class TestPhase9cWave1WidgetBlueprints:
    def test_01_create_and_read_widget_blueprint_content(self):
        widget_blueprint_path = _create_widget_blueprint()

        content = _ok(send_command("read_widget_blueprint_content", {
            "widget_blueprint_path": widget_blueprint_path,
        }), "read_widget_blueprint_content")
        data = content.get("data", content)
        root_widget = data.get("root_widget") or {}

        assert data.get("widget_blueprint_path") == widget_blueprint_path, data
        assert data.get("parent_class") == "UserWidget", data
        assert data.get("widget_count") == 1, data
        assert data.get("binding_count") == 0, data
        assert data.get("animation_count") == 0, data
        assert root_widget.get("class") == "CanvasPanel", root_widget
        assert root_widget.get("is_variable") is True, root_widget