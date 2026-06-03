"""Live-editor smoke tests for the first Phase 9c Wave 5 widget binding mutation slice."""

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
    widget_name = f"WBP_Phase9C_Wave5_Bindings_{suffix}"
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


def _add_widget(widget_blueprint_path: str, widget_class: str, widget_name: str) -> None:
    _ok(send_command("add_widget_to_widget_blueprint", {
        "widget_blueprint_path": widget_blueprint_path,
        "widget_class": widget_class,
        "parent_widget_name": "RootWidget",
        "widget_name": widget_name,
    }), f"add_widget_to_widget_blueprint({widget_name})")


def _create_function(widget_blueprint_path: str, function_name: str) -> None:
    _ok(send_command("create_function", {
        "blueprint_name": widget_blueprint_path,
        "function_name": function_name,
    }), f"create_function({function_name})")


def _create_variable(widget_blueprint_path: str, variable_name: str, variable_type: str) -> None:
    _ok(send_command("create_variable", {
        "blueprint_name": widget_blueprint_path,
        "variable_name": variable_name,
        "variable_type": variable_type,
    }), f"create_variable({variable_name})")


def _find_binding(content: Dict[str, Any], widget_name: str, property_name: str) -> Optional[Dict[str, Any]]:
    for binding in content.get("bindings") or []:
        if binding.get("object_name") == widget_name and binding.get("property_name") == property_name:
            return binding
    return None


class TestPhase9cWave5WidgetBindings:
    def test_01_add_and_remove_button_event_binding(self):
        widget_blueprint_path = _create_widget_blueprint()

        _add_widget(widget_blueprint_path, "Button", "PrimaryButton")
        _create_function(widget_blueprint_path, "HandlePrimaryButtonClicked")

        updated = _ok(send_command("set_widget_property_binding_in_widget_blueprint", {
            "widget_blueprint_path": widget_blueprint_path,
            "widget_name": "PrimaryButton",
            "property_name": "OnClicked",
            "function_name": "HandlePrimaryButtonClicked",
        }), "set_widget_property_binding_in_widget_blueprint(OnClicked)")
        updated_data = updated.get("data", updated)

        content = _read_widget_blueprint(widget_blueprint_path)
        binding = _find_binding(content, "PrimaryButton", "OnClicked") or {}

        assert updated_data.get("widget_name") == "PrimaryButton", updated_data
        assert updated_data.get("property_name") == "OnClicked", updated_data
        assert updated_data.get("function_name") == "HandlePrimaryButtonClicked", updated_data
        assert updated_data.get("binding_kind") == "function", updated_data
        assert content.get("binding_count") == 1, content
        assert binding.get("object_name") == "PrimaryButton", binding
        assert binding.get("property_name") == "OnClicked", binding
        assert binding.get("function_name") == "HandlePrimaryButtonClicked", binding
        assert str(binding.get("kind", "")).lower().endswith("function"), binding

        removed = _ok(send_command("remove_widget_property_binding_from_widget_blueprint", {
            "widget_blueprint_path": widget_blueprint_path,
            "widget_name": "PrimaryButton",
            "property_name": "OnClicked",
        }), "remove_widget_property_binding_from_widget_blueprint(OnClicked)")
        removed_data = removed.get("data", removed)

        content_after_remove = _read_widget_blueprint(widget_blueprint_path)

        assert removed_data.get("widget_name") == "PrimaryButton", removed_data
        assert removed_data.get("property_name") == "OnClicked", removed_data
        assert removed_data.get("binding_count") == 0, removed_data
        assert content_after_remove.get("binding_count") == 0, content_after_remove
        assert _find_binding(content_after_remove, "PrimaryButton", "OnClicked") is None, content_after_remove

    def test_02_add_and_remove_text_property_binding(self):
        widget_blueprint_path = _create_widget_blueprint()

        _add_widget(widget_blueprint_path, "TextBlock", "StatusLabel")
        _create_variable(widget_blueprint_path, "StatusText", "text")

        updated = _ok(send_command("set_widget_property_binding_in_widget_blueprint", {
            "widget_blueprint_path": widget_blueprint_path,
            "widget_name": "StatusLabel",
            "property_name": "Text",
            "source_property": "StatusText",
        }), "set_widget_property_binding_in_widget_blueprint(Text)")
        updated_data = updated.get("data", updated)

        content = _read_widget_blueprint(widget_blueprint_path)
        binding = _find_binding(content, "StatusLabel", "Text") or {}

        assert updated_data.get("widget_name") == "StatusLabel", updated_data
        assert updated_data.get("property_name") == "Text", updated_data
        assert updated_data.get("binding_kind") == "property", updated_data
        assert updated_data.get("source_property") == "StatusText", updated_data
        assert updated_data.get("source_path") == "StatusText", updated_data
        assert content.get("binding_count") == 1, content
        assert binding.get("object_name") == "StatusLabel", binding
        assert binding.get("property_name") == "Text", binding
        assert binding.get("source_property") == "StatusText", binding
        assert binding.get("source_path") == "StatusText", binding
        assert str(binding.get("kind", "")).lower().endswith("property"), binding

        removed = _ok(send_command("remove_widget_property_binding_from_widget_blueprint", {
            "widget_blueprint_path": widget_blueprint_path,
            "widget_name": "StatusLabel",
            "property_name": "Text",
        }), "remove_widget_property_binding_from_widget_blueprint(Text)")
        removed_data = removed.get("data", removed)

        content_after_remove = _read_widget_blueprint(widget_blueprint_path)

        assert removed_data.get("widget_name") == "StatusLabel", removed_data
        assert removed_data.get("property_name") == "Text", removed_data
        assert removed_data.get("binding_count") == 0, removed_data
        assert content_after_remove.get("binding_count") == 0, content_after_remove
        assert _find_binding(content_after_remove, "StatusLabel", "Text") is None, content_after_remove