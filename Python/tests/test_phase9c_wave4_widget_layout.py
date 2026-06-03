"""Live-editor smoke tests for the initial Phase 9c Wave 4 widget layout mutation slice."""

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


def _create_widget_blueprint(root_widget_class: str) -> str:
    suffix = int(time.time() * 1000) % 1000000
    widget_name = f"WBP_Phase9C_Wave4_{root_widget_class}_{suffix}"
    last_error: Exception | None = None
    for _ in range(3):
        try:
            payload = _ok(send_command("create_widget_blueprint", {
                "widget_name": widget_name,
                "destination_path": "/Game/UI",
                "parent_class": "UserWidget",
                "root_widget_class": root_widget_class,
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


class TestPhase9cWave4WidgetLayout:
    def test_01_mutate_canvas_slot_layout(self):
        widget_blueprint_path = _create_widget_blueprint("CanvasPanel")

        _add_widget(widget_blueprint_path, "Button", "PrimaryButton")

        updated = _ok(send_command("set_widget_slot_layout_in_widget_blueprint", {
            "widget_blueprint_path": widget_blueprint_path,
            "widget_name": "PrimaryButton",
            "anchors": {
                "minimum": {"x": 0.25, "y": 0.25},
                "maximum": {"x": 0.25, "y": 0.25},
            },
            "offsets": {"left": 12, "top": 24, "right": 180, "bottom": 96},
            "alignment": {"x": 0.5, "y": 1.0},
            "z_order": 7,
        }), "set_widget_slot_layout_in_widget_blueprint(canvas)")
        data = updated.get("data", updated)
        content = _read_widget_blueprint(widget_blueprint_path)
        button = _find_widget(content.get("root_widget") or {}, "PrimaryButton") or {}
        slot = button.get("slot") or {}

        assert data.get("slot_type") == "canvas", data
        assert set(data.get("updated_fields") or []) == {"anchors", "offsets", "alignment", "z_order"}, data
        assert slot.get("anchors") == {
            "minimum": {"x": 0.25, "y": 0.25},
            "maximum": {"x": 0.25, "y": 0.25},
        }, slot
        assert slot.get("offsets") == {"left": 12.0, "top": 24.0, "right": 180.0, "bottom": 96.0}, slot
        assert slot.get("alignment") == {"x": 0.5, "y": 1.0}, slot
        assert slot.get("z_order") == 7, slot

    def test_02_mutate_grid_slot_layout(self):
        widget_blueprint_path = _create_widget_blueprint("GridPanel")

        _add_widget(widget_blueprint_path, "TextBlock", "GridChild")

        updated = _ok(send_command("set_widget_slot_layout_in_widget_blueprint", {
            "widget_blueprint_path": widget_blueprint_path,
            "widget_name": "GridChild",
            "padding": {"left": 4, "top": 8, "right": 12, "bottom": 16},
            "horizontal_alignment": "HAlign_Right",
            "vertical_alignment": "VAlign_Bottom",
            "row": 2,
            "row_span": 3,
            "column": 1,
            "column_span": 2,
            "layer": 4,
            "nudge": {"x": 9, "y": -3},
        }), "set_widget_slot_layout_in_widget_blueprint(grid)")
        data = updated.get("data", updated)
        content = _read_widget_blueprint(widget_blueprint_path)
        grid_child = _find_widget(content.get("root_widget") or {}, "GridChild") or {}
        slot = grid_child.get("slot") or {}

        assert data.get("slot_type") == "grid", data
        assert set(data.get("updated_fields") or []) == {
            "padding",
            "horizontal_alignment",
            "vertical_alignment",
            "row",
            "row_span",
            "column",
            "column_span",
            "layer",
            "nudge",
        }, data
        assert slot.get("padding") == {"left": 4.0, "top": 8.0, "right": 12.0, "bottom": 16.0}, slot
        assert slot.get("horizontal_alignment") == "HAlign_Right", slot
        assert slot.get("vertical_alignment") == "VAlign_Bottom", slot
        assert slot.get("row") == 2, slot
        assert slot.get("row_span") == 3, slot
        assert slot.get("column") == 1, slot
        assert slot.get("column_span") == 2, slot
        assert slot.get("layer") == 4, slot
        assert slot.get("nudge") == {"x": 9.0, "y": -3.0}, slot

    @pytest.mark.parametrize(
        ("root_widget_class", "widget_name", "slot_type"),
        [
            ("HorizontalBox", "HorizontalChild", "horizontal_box"),
            ("VerticalBox", "VerticalChild", "vertical_box"),
        ],
    )
    def test_03_mutate_box_slot_layout(self, root_widget_class: str, widget_name: str, slot_type: str):
        widget_blueprint_path = _create_widget_blueprint(root_widget_class)
        _add_widget(widget_blueprint_path, "TextBlock", widget_name)

        updated = _ok(send_command("set_widget_slot_layout_in_widget_blueprint", {
            "widget_blueprint_path": widget_blueprint_path,
            "widget_name": widget_name,
            "padding": {"left": 5, "top": 10, "right": 15, "bottom": 20},
            "child_size": {"size_rule": "fill", "value": 1.75},
            "horizontal_alignment": "HAlign_Center",
            "vertical_alignment": "VAlign_Bottom",
        }), f"set_widget_slot_layout_in_widget_blueprint({slot_type})")
        data = updated.get("data", updated)
        content = _read_widget_blueprint(widget_blueprint_path)
        child = _find_widget(content.get("root_widget") or {}, widget_name) or {}
        slot = child.get("slot") or {}

        assert data.get("slot_type") == slot_type, data
        assert set(data.get("updated_fields") or []) == {
            "padding",
            "child_size",
            "horizontal_alignment",
            "vertical_alignment",
        }, data
        assert slot.get("padding") == {"left": 5.0, "top": 10.0, "right": 15.0, "bottom": 20.0}, slot
        assert slot.get("child_size") == {"size_rule": "fill", "value": 1.75}, slot
        assert slot.get("horizontal_alignment") == "HAlign_Center", slot
        assert slot.get("vertical_alignment") == "VAlign_Bottom", slot

    def test_04_mutate_wrap_box_slot_layout(self):
        widget_blueprint_path = _create_widget_blueprint("WrapBox")
        _add_widget(widget_blueprint_path, "TextBlock", "WrapChild")

        updated = _ok(send_command("set_widget_slot_layout_in_widget_blueprint", {
            "widget_blueprint_path": widget_blueprint_path,
            "widget_name": "WrapChild",
            "padding": {"left": 6, "top": 12, "right": 18, "bottom": 24},
            "horizontal_alignment": "HAlign_Right",
            "vertical_alignment": "VAlign_Center",
            "fill_empty_space": True,
            "force_new_line": True,
            "fill_span_when_less_than": 320.0,
        }), "set_widget_slot_layout_in_widget_blueprint(wrap_box)")
        data = updated.get("data", updated)
        content = _read_widget_blueprint(widget_blueprint_path)
        child = _find_widget(content.get("root_widget") or {}, "WrapChild") or {}
        slot = child.get("slot") or {}

        assert data.get("slot_type") == "wrap_box", data
        assert set(data.get("updated_fields") or []) == {
            "padding",
            "horizontal_alignment",
            "vertical_alignment",
            "fill_empty_space",
            "force_new_line",
            "fill_span_when_less_than",
        }, data
        assert slot.get("padding") == {"left": 6.0, "top": 12.0, "right": 18.0, "bottom": 24.0}, slot
        assert slot.get("horizontal_alignment") == "HAlign_Right", slot
        assert slot.get("vertical_alignment") == "VAlign_Center", slot
        assert slot.get("fill_empty_space") is True, slot
        assert slot.get("force_new_line") is True, slot
        assert slot.get("fill_span_when_less_than") == 320.0, slot

    def test_05_mutate_safe_zone_slot_layout(self):
        widget_blueprint_path = _create_widget_blueprint("SafeZone")
        _add_widget(widget_blueprint_path, "TextBlock", "SafeChild")

        updated = _ok(send_command("set_widget_slot_layout_in_widget_blueprint", {
            "widget_blueprint_path": widget_blueprint_path,
            "widget_name": "SafeChild",
            "padding": {"left": 7, "top": 14, "right": 21, "bottom": 28},
            "horizontal_alignment": "HAlign_Left",
            "vertical_alignment": "VAlign_Center",
            "safe_area_scale": {"left": 0.1, "top": 0.2, "right": 0.3, "bottom": 0.4},
            "is_title_safe": False,
        }), "set_widget_slot_layout_in_widget_blueprint(safe_zone)")
        data = updated.get("data", updated)
        content = _read_widget_blueprint(widget_blueprint_path)
        child = _find_widget(content.get("root_widget") or {}, "SafeChild") or {}
        slot = child.get("slot") or {}

        assert data.get("slot_type") == "safe_zone", data
        assert set(data.get("updated_fields") or []) == {
            "padding",
            "horizontal_alignment",
            "vertical_alignment",
            "safe_area_scale",
            "is_title_safe",
        }, data
        assert slot.get("padding") == {"left": 7.0, "top": 14.0, "right": 21.0, "bottom": 28.0}, slot
        assert slot.get("horizontal_alignment") == "HAlign_Left", slot
        assert slot.get("vertical_alignment") == "VAlign_Center", slot
        safe_area_scale = slot.get("safe_area_scale") or {}
        assert safe_area_scale == pytest.approx({"left": 0.1, "top": 0.2, "right": 0.3, "bottom": 0.4}), slot
        assert slot.get("is_title_safe") is False, slot

    def test_06_mutate_uniform_grid_slot_layout(self):
        widget_blueprint_path = _create_widget_blueprint("UniformGridPanel")
        _add_widget(widget_blueprint_path, "TextBlock", "UniformChild")

        updated = _ok(send_command("set_widget_slot_layout_in_widget_blueprint", {
            "widget_blueprint_path": widget_blueprint_path,
            "widget_name": "UniformChild",
            "horizontal_alignment": "HAlign_Fill",
            "vertical_alignment": "VAlign_Bottom",
            "row": 3,
            "column": 4,
        }), "set_widget_slot_layout_in_widget_blueprint(uniform_grid)")
        data = updated.get("data", updated)
        content = _read_widget_blueprint(widget_blueprint_path)
        child = _find_widget(content.get("root_widget") or {}, "UniformChild") or {}
        slot = child.get("slot") or {}

        assert data.get("slot_type") == "uniform_grid", data
        assert set(data.get("updated_fields") or []) == {
            "horizontal_alignment",
            "vertical_alignment",
            "row",
            "column",
        }, data
        assert slot.get("horizontal_alignment") == "HAlign_Fill", slot
        assert slot.get("vertical_alignment") == "VAlign_Bottom", slot
        assert slot.get("row") == 3, slot
        assert slot.get("column") == 4, slot

    @pytest.mark.parametrize(
        ("root_widget_class", "widget_name", "slot_type"),
        [
            ("ScrollBox", "ScrollChild", "scroll_box"),
            ("StackBox", "StackChild", "stack_box"),
        ],
    )
    def test_07_mutate_scroll_and_stack_slot_layout(self, root_widget_class: str, widget_name: str, slot_type: str):
        widget_blueprint_path = _create_widget_blueprint(root_widget_class)
        _add_widget(widget_blueprint_path, "TextBlock", widget_name)

        updated = _ok(send_command("set_widget_slot_layout_in_widget_blueprint", {
            "widget_blueprint_path": widget_blueprint_path,
            "widget_name": widget_name,
            "padding": {"left": 8, "top": 16, "right": 24, "bottom": 32},
            "child_size": {"size_rule": "automatic", "value": 2.0},
            "horizontal_alignment": "HAlign_Right",
            "vertical_alignment": "VAlign_Top",
        }), f"set_widget_slot_layout_in_widget_blueprint({slot_type})")
        data = updated.get("data", updated)
        content = _read_widget_blueprint(widget_blueprint_path)
        child = _find_widget(content.get("root_widget") or {}, widget_name) or {}
        slot = child.get("slot") or {}

        assert data.get("slot_type") == slot_type, data
        assert set(data.get("updated_fields") or []) == {
            "padding",
            "child_size",
            "horizontal_alignment",
            "vertical_alignment",
        }, data
        assert slot.get("padding") == {"left": 8.0, "top": 16.0, "right": 24.0, "bottom": 32.0}, slot
        assert slot.get("child_size") == {"size_rule": "automatic", "value": 2.0}, slot
        assert slot.get("horizontal_alignment") == "HAlign_Right", slot
        assert slot.get("vertical_alignment") == "VAlign_Top", slot

    def test_08_mutate_scale_box_slot_layout(self):
        widget_blueprint_path = _create_widget_blueprint("ScaleBox")
        _add_widget(widget_blueprint_path, "TextBlock", "ScaleChild")

        updated = _ok(send_command("set_widget_slot_layout_in_widget_blueprint", {
            "widget_blueprint_path": widget_blueprint_path,
            "widget_name": "ScaleChild",
            "horizontal_alignment": "HAlign_Center",
            "vertical_alignment": "VAlign_Fill",
        }), "set_widget_slot_layout_in_widget_blueprint(scale_box)")
        data = updated.get("data", updated)
        content = _read_widget_blueprint(widget_blueprint_path)
        child = _find_widget(content.get("root_widget") or {}, "ScaleChild") or {}
        slot = child.get("slot") or {}

        assert data.get("slot_type") == "scale_box", data
        assert set(data.get("updated_fields") or []) == {"horizontal_alignment", "vertical_alignment"}, data
        assert slot.get("horizontal_alignment") == "HAlign_Center", slot
        assert slot.get("vertical_alignment") == "VAlign_Fill", slot

    @pytest.mark.parametrize(
        ("root_widget_class", "widget_name", "slot_type"),
        [
            ("BackgroundBlur", "BlurChild", "background_blur"),
            ("Border", "BorderChild", "border"),
            ("Button", "ButtonChild", "button"),
            ("Overlay", "OverlayChild", "overlay"),
            ("SizeBox", "SizeChild", "size_box"),
            ("WidgetSwitcher", "SwitcherChild", "widget_switcher"),
            ("WindowTitleBarArea", "TitleBarChild", "window_title_bar_area"),
        ],
    )
    def test_09_mutate_remaining_padding_alignment_slots(self, root_widget_class: str, widget_name: str, slot_type: str):
        widget_blueprint_path = _create_widget_blueprint(root_widget_class)
        _add_widget(widget_blueprint_path, "TextBlock", widget_name)

        updated = _ok(send_command("set_widget_slot_layout_in_widget_blueprint", {
            "widget_blueprint_path": widget_blueprint_path,
            "widget_name": widget_name,
            "padding": {"left": 11, "top": 22, "right": 33, "bottom": 44},
            "horizontal_alignment": "HAlign_Left",
            "vertical_alignment": "VAlign_Bottom",
        }), f"set_widget_slot_layout_in_widget_blueprint({slot_type})")
        data = updated.get("data", updated)
        content = _read_widget_blueprint(widget_blueprint_path)
        child = _find_widget(content.get("root_widget") or {}, widget_name) or {}
        slot = child.get("slot") or {}

        assert data.get("slot_type") == slot_type, data
        assert set(data.get("updated_fields") or []) == {"padding", "horizontal_alignment", "vertical_alignment"}, data
        assert slot.get("padding") == {"left": 11.0, "top": 22.0, "right": 33.0, "bottom": 44.0}, slot
        assert slot.get("horizontal_alignment") == "HAlign_Left", slot
        assert slot.get("vertical_alignment") == "VAlign_Bottom", slot