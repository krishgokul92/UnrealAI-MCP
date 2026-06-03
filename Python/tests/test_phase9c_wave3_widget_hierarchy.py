"""Live-editor smoke tests for Phase 9c Wave 3 widget hierarchy mutation."""

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

from ue_bridge import ping, send_command  # noqa: E402


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
    widget_name = f"WBP_Phase9C_Wave3_{suffix}"
    payload = _ok(send_command("create_widget_blueprint", {
        "widget_name": widget_name,
        "destination_path": "/Game/UI",
        "parent_class": "UserWidget",
        "root_widget_class": "CanvasPanel",
    }), f"create_widget_blueprint({widget_name})")
    return payload["data"]["widget_blueprint_path"] if "data" in payload else payload["widget_blueprint_path"]


def _read_widget_blueprint(widget_blueprint_path: str) -> Dict[str, Any]:
    payload = _ok(send_command("read_widget_blueprint_content", {
        "widget_blueprint_path": widget_blueprint_path,
    }), f"read_widget_blueprint_content({widget_blueprint_path})")
    return payload.get("data", payload)


def _find_widget(node: Dict[str, Any], widget_name: str) -> Optional[Dict[str, Any]]:
    if node.get("name") == widget_name:
        return node

    for child in node.get("children") or []:
        found = _find_widget(child, widget_name)
        if found is not None:
            return found

    for named_slot_child in node.get("named_slot_children") or []:
        found = _find_widget(named_slot_child.get("widget") or {}, widget_name)
        if found is not None:
            return found

    return None


class TestPhase9cWave3WidgetHierarchy:
    def test_01_add_widget_to_widget_blueprint(self):
        widget_blueprint_path = _create_widget_blueprint()

        added = _ok(send_command("add_widget_to_widget_blueprint", {
            "widget_blueprint_path": widget_blueprint_path,
            "widget_class": "Button",
            "parent_widget_name": "RootWidget",
            "widget_name": "PrimaryButton",
        }), "add_widget_to_widget_blueprint(button)")
        data = added.get("data", added)
        content = _read_widget_blueprint(widget_blueprint_path)
        root = content.get("root_widget") or {}
        button = _find_widget(root, "PrimaryButton") or {}

        assert data.get("widget_name") == "PrimaryButton", data
        assert data.get("parent_widget_name") == "RootWidget", data
        assert content.get("widget_count") == 2, content
        assert button.get("class") == "Button", button
        assert (button.get("slot") or {}).get("slot_type") == "canvas", button

    def test_02_reparent_widget_in_widget_blueprint(self):
        widget_blueprint_path = _create_widget_blueprint()

        _ok(send_command("add_widget_to_widget_blueprint", {
            "widget_blueprint_path": widget_blueprint_path,
            "widget_class": "HorizontalBox",
            "parent_widget_name": "RootWidget",
            "widget_name": "LeftColumn",
        }), "add_widget_to_widget_blueprint(left_column)")
        _ok(send_command("add_widget_to_widget_blueprint", {
            "widget_blueprint_path": widget_blueprint_path,
            "widget_class": "VerticalBox",
            "parent_widget_name": "RootWidget",
            "widget_name": "RightColumn",
        }), "add_widget_to_widget_blueprint(right_column)")
        _ok(send_command("add_widget_to_widget_blueprint", {
            "widget_blueprint_path": widget_blueprint_path,
            "widget_class": "TextBlock",
            "parent_widget_name": "LeftColumn",
            "widget_name": "StatusLabel",
        }), "add_widget_to_widget_blueprint(status_label)")

        reparented = _ok(send_command("reparent_widget_in_widget_blueprint", {
            "widget_blueprint_path": widget_blueprint_path,
            "widget_name": "StatusLabel",
            "new_parent_widget_name": "RightColumn",
        }), "reparent_widget_in_widget_blueprint(status_label)")
        data = reparented.get("data", reparented)
        content = _read_widget_blueprint(widget_blueprint_path)
        root = content.get("root_widget") or {}
        left_column = _find_widget(root, "LeftColumn") or {}
        right_column = _find_widget(root, "RightColumn") or {}

        assert data.get("previous_parent_widget_name") == "LeftColumn", data
        assert data.get("new_parent_widget_name") == "RightColumn", data
        assert _find_widget(left_column, "StatusLabel") is None, left_column
        assert (_find_widget(right_column, "StatusLabel") or {}).get("class") == "TextBlock", right_column

    def test_03_remove_widget_from_widget_blueprint(self):
        widget_blueprint_path = _create_widget_blueprint()

        _ok(send_command("add_widget_to_widget_blueprint", {
            "widget_blueprint_path": widget_blueprint_path,
            "widget_class": "Button",
            "parent_widget_name": "RootWidget",
            "widget_name": "PrimaryButton",
        }), "add_widget_to_widget_blueprint(button)")
        _ok(send_command("add_widget_to_widget_blueprint", {
            "widget_blueprint_path": widget_blueprint_path,
            "widget_class": "TextBlock",
            "parent_widget_name": "PrimaryButton",
            "widget_name": "ButtonLabel",
        }), "add_widget_to_widget_blueprint(button_label)")

        removed = _ok(send_command("remove_widget_from_widget_blueprint", {
            "widget_blueprint_path": widget_blueprint_path,
            "widget_name": "PrimaryButton",
        }), "remove_widget_from_widget_blueprint(primary_button)")
        data = removed.get("data", removed)
        content = _read_widget_blueprint(widget_blueprint_path)
        root = content.get("root_widget") or {}

        assert data.get("removed_widget_name") == "PrimaryButton", data
        assert set(data.get("removed_widget_names") or []) == {"PrimaryButton", "ButtonLabel"}, data
        assert content.get("widget_count") == 1, content
        assert _find_widget(root, "PrimaryButton") is None, root
        assert _find_widget(root, "ButtonLabel") is None, root