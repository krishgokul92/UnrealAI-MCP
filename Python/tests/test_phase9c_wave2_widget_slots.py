"""Live-editor smoke tests for Phase 9c Wave 2 widget slot inspection."""

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


def _create_widget_blueprint(root_widget_class: str) -> str:
    suffix = int(time.time() * 1000) % 1000000
    widget_name = f"WBP_Phase9C_Wave2_{root_widget_class}_{suffix}"
    payload = _ok(send_command("create_widget_blueprint", {
        "widget_name": widget_name,
        "destination_path": "/Game/UI",
        "parent_class": "UserWidget",
        "root_widget_class": root_widget_class,
        "add_default_child": True,
        "default_child_widget_class": "TextBlock",
    }), f"create_widget_blueprint({widget_name})")
    return payload["data"]["widget_blueprint_path"] if "data" in payload else payload["widget_blueprint_path"]


def _read_widget_blueprint(widget_blueprint_path: str) -> Dict[str, Any]:
    payload = _ok(send_command("read_widget_blueprint_content", {
        "widget_blueprint_path": widget_blueprint_path,
    }), f"read_widget_blueprint_content({widget_blueprint_path})")
    return payload.get("data", payload)


def _get_seeded_child_slot(data: Dict[str, Any]) -> Dict[str, Any]:
    root_widget = data.get("root_widget") or {}
    children = root_widget.get("children") or []
    assert len(children) == 1, data
    child = children[0]
    slot = child.get("slot") or {}
    assert slot, child
    return slot


@needs_bridge
@pytest.mark.parametrize(
    ("root_widget_class", "expected_slot_type", "expected_fields"),
    [
        ("Button", "button", {"padding", "horizontal_alignment", "vertical_alignment"}),
        ("HorizontalBox", "horizontal_box", {"padding", "horizontal_alignment", "vertical_alignment", "child_size"}),
        ("GridPanel", "grid", {"padding", "row", "row_span", "column", "column_span", "layer", "nudge", "horizontal_alignment", "vertical_alignment"}),
        ("SafeZone", "safe_zone", {"padding", "safe_area_scale", "is_title_safe", "horizontal_alignment", "vertical_alignment"}),
        ("WrapBox", "wrap_box", {"padding", "fill_empty_space", "force_new_line", "fill_span_when_less_than", "horizontal_alignment", "vertical_alignment"}),
    ],
)
def test_read_widget_blueprint_slot_metadata(root_widget_class: str, expected_slot_type: str, expected_fields: set[str]):
    widget_blueprint_path = _create_widget_blueprint(root_widget_class)
    data = _read_widget_blueprint(widget_blueprint_path)
    slot = _get_seeded_child_slot(data)

    assert data.get("widget_count") == 2, data
    assert slot.get("slot_type") == expected_slot_type, slot
    assert expected_fields.issubset(slot.keys()), slot

    if "child_size" in expected_fields:
        child_size = slot.get("child_size") or {}
        assert {"size_rule", "value"}.issubset(child_size.keys()), child_size

    if "nudge" in expected_fields:
        nudge = slot.get("nudge") or {}
        assert {"x", "y"}.issubset(nudge.keys()), nudge

    if "padding" in expected_fields:
        padding = slot.get("padding") or {}
        assert {"left", "top", "right", "bottom"}.issubset(padding.keys()), padding

    if "safe_area_scale" in expected_fields:
        safe_area_scale = slot.get("safe_area_scale") or {}
        assert {"left", "top", "right", "bottom"}.issubset(safe_area_scale.keys()), safe_area_scale