"""Pure-Python tests for newly exposed Phase 9 Wave 1 MCP wrappers.

These tests validate wrapper-to-bridge payload mapping without requiring a live
Unreal Editor connection.
"""

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


def test_create_blueprint_function_passes_optional_return_type(monkeypatch):
    calls = _capture_bridge(monkeypatch)

    result = mcp_module.create_blueprint_function(
        blueprint_name="BP_Test",
        function_name="ComputeValue",
        return_type="float",
    )

    assert result == '{"status":"ok"}'
    assert calls == [(
        "create_function",
        {
            "blueprint_name": "BP_Test",
            "function_name": "ComputeValue",
            "return_type": "float",
        },
    )]


def test_set_blueprint_variable_properties_maps_only_supplied_fields(monkeypatch):
    calls = _capture_bridge(monkeypatch)

    result = mcp_module.set_blueprint_variable_properties(
        blueprint_name="BP_Test",
        variable_name="Health",
        new_variable_name="MaxHealth",
        variable_type="float",
        default_value="150.0",
        is_public=True,
        expose_on_spawn=True,
        slider_range_min="0.0",
        slider_range_max="500.0",
        units="cm",
    )

    assert result == '{"status":"ok"}'
    assert calls == [(
        "set_blueprint_variable_properties",
        {
            "blueprint_name": "BP_Test",
            "variable_name": "Health",
            "var_name": "MaxHealth",
            "var_type": "float",
            "default_value": "150.0",
            "is_public": True,
            "expose_on_spawn": True,
            "slider_range_min": "0.0",
            "slider_range_max": "500.0",
            "units": "cm",
        },
    )]


def test_add_blueprint_function_input_maps_payload(monkeypatch):
    calls = _capture_bridge(monkeypatch)

    result = mcp_module.add_blueprint_function_input(
        blueprint_name="BP_Test",
        function_name="ComputeValue",
        param_name="DeltaTime",
        param_type="float",
        is_array=False,
    )

    assert result == '{"status":"ok"}'
    assert calls == [(
        "add_function_input",
        {
            "blueprint_name": "BP_Test",
            "function_name": "ComputeValue",
            "param_name": "DeltaTime",
            "param_type": "float",
            "is_array": False,
        },
    )]


def test_add_blueprint_function_output_maps_payload(monkeypatch):
    calls = _capture_bridge(monkeypatch)

    result = mcp_module.add_blueprint_function_output(
        blueprint_name="BP_Test",
        function_name="ComputeValue",
        param_name="Values",
        param_type="int",
        is_array=True,
    )

    assert result == '{"status":"ok"}'
    assert calls == [(
        "add_function_output",
        {
            "blueprint_name": "BP_Test",
            "function_name": "ComputeValue",
            "param_name": "Values",
            "param_type": "int",
            "is_array": True,
        },
    )]


def test_delete_and_rename_blueprint_function_map_payloads(monkeypatch):
    calls = _capture_bridge(monkeypatch)

    delete_result = mcp_module.delete_blueprint_function(
        blueprint_name="BP_Test",
        function_name="ObsoleteFunction",
    )
    rename_result = mcp_module.rename_blueprint_function(
        blueprint_name="BP_Test",
        old_function_name="OldName",
        new_function_name="NewName",
    )

    assert delete_result == '{"status":"ok"}'
    assert rename_result == '{"status":"ok"}'
    assert calls == [
        (
            "delete_function",
            {
                "blueprint_name": "BP_Test",
                "function_name": "ObsoleteFunction",
            },
        ),
        (
            "rename_function",
            {
                "blueprint_name": "BP_Test",
                "old_function_name": "OldName",
                "new_function_name": "NewName",
            },
        ),
    ]