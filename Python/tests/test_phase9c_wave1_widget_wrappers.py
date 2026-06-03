"""Pure-Python wrapper tests for Phase 9c widget blueprint tools."""

from __future__ import annotations

import sys
from pathlib import Path


_PY_ROOT = Path(__file__).resolve().parent.parent
if str(_PY_ROOT) not in sys.path:
    sys.path.insert(0, str(_PY_ROOT))

import unreal_ai_mcp as mcp_module  # noqa: E402


def _capture_bridge(monkeypatch):
    calls = []

    def fake_bridge(cmd, params=None):
        calls.append((cmd, params))
        return '{"status":"ok"}'

    monkeypatch.setattr(mcp_module, "_bridge", fake_bridge)
    return calls


def test_create_widget_blueprint_maps_payload(monkeypatch):
    calls = _capture_bridge(monkeypatch)

    result = mcp_module.create_widget_blueprint(
        widget_name="WBP_TestMenu",
        destination_path="/Game/UI/Test",
        parent_class="UserWidget",
        root_widget_class="CanvasPanel",
    )

    assert result == '{"status":"ok"}'
    assert calls == [(
        "create_widget_blueprint",
        {
            "widget_name": "WBP_TestMenu",
            "destination_path": "/Game/UI/Test",
            "parent_class": "UserWidget",
            "root_widget_class": "CanvasPanel",
        },
    )]


def test_create_widget_blueprint_maps_seed_child_payload(monkeypatch):
    calls = _capture_bridge(monkeypatch)

    result = mcp_module.create_widget_blueprint(
        widget_name="WBP_TestSlots",
        root_widget_class="HorizontalBox",
        add_default_child=True,
        default_child_widget_class="TextBlock",
    )

    assert result == '{"status":"ok"}'
    assert calls == [(
        "create_widget_blueprint",
        {
            "widget_name": "WBP_TestSlots",
            "destination_path": "/Game/UI",
            "parent_class": "UserWidget",
            "root_widget_class": "HorizontalBox",
            "add_default_child": True,
            "default_child_widget_class": "TextBlock",
        },
    )]


def test_read_widget_blueprint_content_maps_payload(monkeypatch):
    calls = _capture_bridge(monkeypatch)

    result = mcp_module.read_widget_blueprint_content("/Game/UI/WBP_TestMenu")

    assert result == '{"status":"ok"}'
    assert calls == [(
        "read_widget_blueprint_content",
        {
            "widget_blueprint_path": "/Game/UI/WBP_TestMenu",
        },
    )]


def test_set_widget_property_binding_in_widget_blueprint_maps_payload(monkeypatch):
    calls = _capture_bridge(monkeypatch)

    result = mcp_module.set_widget_property_binding_in_widget_blueprint(
        widget_blueprint_path="/Game/UI/WBP_TestMenu",
        widget_name="PrimaryButton",
        property_name="OnClicked",
        function_name="HandlePrimaryButtonClicked",
    )

    assert result == '{"status":"ok"}'
    assert calls == [(
        "set_widget_property_binding_in_widget_blueprint",
        {
            "widget_blueprint_path": "/Game/UI/WBP_TestMenu",
            "widget_name": "PrimaryButton",
            "property_name": "OnClicked",
            "function_name": "HandlePrimaryButtonClicked",
        },
    )]


def test_set_widget_property_binding_in_widget_blueprint_maps_property_payload(monkeypatch):
    calls = _capture_bridge(monkeypatch)

    result = mcp_module.set_widget_property_binding_in_widget_blueprint(
        widget_blueprint_path="/Game/UI/WBP_TestMenu",
        widget_name="StatusLabel",
        property_name="Text",
        source_property="StatusText",
        source_path="StatusText",
    )

    assert result == '{"status":"ok"}'
    assert calls == [(
        "set_widget_property_binding_in_widget_blueprint",
        {
            "widget_blueprint_path": "/Game/UI/WBP_TestMenu",
            "widget_name": "StatusLabel",
            "property_name": "Text",
            "source_property": "StatusText",
            "source_path": "StatusText",
        },
    )]


def test_remove_widget_property_binding_from_widget_blueprint_maps_payload(monkeypatch):
    calls = _capture_bridge(monkeypatch)

    result = mcp_module.remove_widget_property_binding_from_widget_blueprint(
        widget_blueprint_path="/Game/UI/WBP_TestMenu",
        widget_name="PrimaryButton",
        property_name="OnClicked",
    )

    assert result == '{"status":"ok"}'
    assert calls == [(
        "remove_widget_property_binding_from_widget_blueprint",
        {
            "widget_blueprint_path": "/Game/UI/WBP_TestMenu",
            "widget_name": "PrimaryButton",
            "property_name": "OnClicked",
        },
    )]


def test_create_widget_animation_in_widget_blueprint_maps_payload(monkeypatch):
    calls = _capture_bridge(monkeypatch)

    result = mcp_module.create_widget_animation_in_widget_blueprint(
        widget_blueprint_path="/Game/UI/WBP_TestMenu",
        animation_name="Pulse",
    )

    assert result == '{"status":"ok"}'
    assert calls == [(
        "create_widget_animation_in_widget_blueprint",
        {
            "widget_blueprint_path": "/Game/UI/WBP_TestMenu",
            "animation_name": "Pulse",
        },
    )]


def test_remove_widget_animation_from_widget_blueprint_maps_payload(monkeypatch):
    calls = _capture_bridge(monkeypatch)

    result = mcp_module.remove_widget_animation_from_widget_blueprint(
        widget_blueprint_path="/Game/UI/WBP_TestMenu",
        animation_name="Pulse",
    )

    assert result == '{"status":"ok"}'
    assert calls == [(
        "remove_widget_animation_from_widget_blueprint",
        {
            "widget_blueprint_path": "/Game/UI/WBP_TestMenu",
            "animation_name": "Pulse",
        },
    )]


def test_add_widget_to_widget_blueprint_maps_payload(monkeypatch):
    calls = _capture_bridge(monkeypatch)

    result = mcp_module.add_widget_to_widget_blueprint(
        widget_blueprint_path="/Game/UI/WBP_TestMenu",
        widget_class="Button",
        parent_widget_name="RootWidget",
        widget_name="PrimaryButton",
        is_variable=False,
    )

    assert result == '{"status":"ok"}'
    assert calls == [(
        "add_widget_to_widget_blueprint",
        {
            "widget_blueprint_path": "/Game/UI/WBP_TestMenu",
            "widget_class": "Button",
            "parent_widget_name": "RootWidget",
            "widget_name": "PrimaryButton",
            "is_variable": False,
        },
    )]


def test_remove_widget_from_widget_blueprint_maps_payload(monkeypatch):
    calls = _capture_bridge(monkeypatch)

    result = mcp_module.remove_widget_from_widget_blueprint(
        widget_blueprint_path="/Game/UI/WBP_TestMenu",
        widget_name="PrimaryButton",
    )

    assert result == '{"status":"ok"}'
    assert calls == [(
        "remove_widget_from_widget_blueprint",
        {
            "widget_blueprint_path": "/Game/UI/WBP_TestMenu",
            "widget_name": "PrimaryButton",
        },
    )]


def test_reparent_widget_in_widget_blueprint_maps_payload(monkeypatch):
    calls = _capture_bridge(monkeypatch)

    result = mcp_module.reparent_widget_in_widget_blueprint(
        widget_blueprint_path="/Game/UI/WBP_TestMenu",
        widget_name="PrimaryButton",
        new_parent_widget_name="Sidebar",
    )

    assert result == '{"status":"ok"}'
    assert calls == [(
        "reparent_widget_in_widget_blueprint",
        {
            "widget_blueprint_path": "/Game/UI/WBP_TestMenu",
            "widget_name": "PrimaryButton",
            "new_parent_widget_name": "Sidebar",
        },
    )]


def test_set_widget_slot_layout_in_widget_blueprint_maps_payload(monkeypatch):
    calls = _capture_bridge(monkeypatch)

    result = mcp_module.set_widget_slot_layout_in_widget_blueprint(
        widget_blueprint_path="/Game/UI/WBP_TestMenu",
        widget_name="PrimaryButton",
        anchors={
            "minimum": {"x": 0.25, "y": 0.25},
            "maximum": {"x": 0.25, "y": 0.25},
        },
        offsets={"left": 10, "top": 20, "right": 200, "bottom": 80},
        alignment={"x": 0.5, "y": 1.0},
        z_order=7,
    )

    assert result == '{"status":"ok"}'
    assert calls == [(
        "set_widget_slot_layout_in_widget_blueprint",
        {
            "widget_blueprint_path": "/Game/UI/WBP_TestMenu",
            "widget_name": "PrimaryButton",
            "anchors": {
                "minimum": {"x": 0.25, "y": 0.25},
                "maximum": {"x": 0.25, "y": 0.25},
            },
            "offsets": {"left": 10, "top": 20, "right": 200, "bottom": 80},
            "alignment": {"x": 0.5, "y": 1.0},
            "z_order": 7,
        },
    )]


def test_set_widget_slot_layout_in_widget_blueprint_maps_box_payload(monkeypatch):
    calls = _capture_bridge(monkeypatch)

    result = mcp_module.set_widget_slot_layout_in_widget_blueprint(
        widget_blueprint_path="/Game/UI/WBP_TestMenu",
        widget_name="PrimaryButton",
        padding={"left": 3, "top": 6, "right": 9, "bottom": 12},
        child_size={"size_rule": "fill", "value": 1.5},
        horizontal_alignment="HAlign_Center",
        vertical_alignment="VAlign_Bottom",
    )

    assert result == '{"status":"ok"}'
    assert calls == [(
        "set_widget_slot_layout_in_widget_blueprint",
        {
            "widget_blueprint_path": "/Game/UI/WBP_TestMenu",
            "widget_name": "PrimaryButton",
            "padding": {"left": 3, "top": 6, "right": 9, "bottom": 12},
            "child_size": {"size_rule": "fill", "value": 1.5},
            "horizontal_alignment": "HAlign_Center",
            "vertical_alignment": "VAlign_Bottom",
        },
    )]


def test_set_widget_slot_layout_in_widget_blueprint_maps_wrap_and_safe_payload(monkeypatch):
    calls = _capture_bridge(monkeypatch)

    result = mcp_module.set_widget_slot_layout_in_widget_blueprint(
        widget_blueprint_path="/Game/UI/WBP_TestMenu",
        widget_name="PrimaryButton",
        fill_empty_space=True,
        force_new_line=True,
        fill_span_when_less_than=320.0,
        safe_area_scale={"left": 0.2, "top": 0.3, "right": 0.4, "bottom": 0.5},
        is_title_safe=False,
    )

    assert result == '{"status":"ok"}'
    assert calls == [(
        "set_widget_slot_layout_in_widget_blueprint",
        {
            "widget_blueprint_path": "/Game/UI/WBP_TestMenu",
            "widget_name": "PrimaryButton",
            "fill_empty_space": True,
            "force_new_line": True,
            "fill_span_when_less_than": 320.0,
            "safe_area_scale": {"left": 0.2, "top": 0.3, "right": 0.4, "bottom": 0.5},
            "is_title_safe": False,
        },
    )]
