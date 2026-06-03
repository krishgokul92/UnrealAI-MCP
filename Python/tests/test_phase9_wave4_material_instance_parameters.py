"""Live-editor smoke tests for material instance parameter mutation."""

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

BASE_MATERIAL = "/Engine/BasicShapes/BasicShapeMaterial"


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


def _create_material_instance() -> str:
    suffix = int(time.time()) % 1000000
    name = f"MI_Phase9_Wave4_{suffix}"
    _ok(send_command("create_material_instance", {
        "parent_material": BASE_MATERIAL,
        "instance_name": name,
        "dest_path": "/Game/Materials",
        "vector_parameters": {
            "Color": [0.2, 0.4, 0.8, 1.0],
        },
    }), f"create_material_instance({name})")
    return f"/Game/Materials/{name}"


def _create_static_switch_parent_material() -> tuple[str, str]:
    suffix = int(time.time()) % 1000000
    name = f"M_Phase9_Wave4_Static_{suffix}"
    created = _ok(send_command("create_material_asset", {
        "material_name": name,
        "dest_path": "/Game/Materials",
    }), f"create_material_asset({name})")
    material_path = created["material_path"]

    switch_expr = _ok(send_command("create_material_expression", {
        "material_path": material_path,
        "expression_class": "MaterialExpressionStaticSwitchParameter",
        "node_pos_x": -160,
        "node_pos_y": 0,
    }), "create_material_expression(static_switch)")
    true_expr = _ok(send_command("create_material_expression", {
        "material_path": material_path,
        "expression_class": "MaterialExpressionConstant3Vector",
        "node_pos_x": -420,
        "node_pos_y": -120,
    }), "create_material_expression(true_color)")
    false_expr = _ok(send_command("create_material_expression", {
        "material_path": material_path,
        "expression_class": "MaterialExpressionConstant3Vector",
        "node_pos_x": -420,
        "node_pos_y": 120,
    }), "create_material_expression(false_color)")

    switch_id = switch_expr["expression"]["expression_id"]
    true_id = true_expr["expression"]["expression_id"]
    false_id = false_expr["expression"]["expression_id"]

    _ok(send_command("connect_material_expressions", {
        "material_path": material_path,
        "from_expression_id": true_id,
        "to_expression_id": switch_id,
        "to_input_name": "True",
    }), "connect_material_expressions(static_switch.True)")
    _ok(send_command("connect_material_expressions", {
        "material_path": material_path,
        "from_expression_id": false_id,
        "to_expression_id": switch_id,
        "to_input_name": "False",
    }), "connect_material_expressions(static_switch.False)")
    _ok(send_command("connect_material_property", {
        "material_path": material_path,
        "from_expression_id": switch_id,
        "property_name": "BaseColor",
    }), "connect_material_property(static_switch)")
    _ok(send_command("recompile_material", {
        "material_path": material_path,
    }), "recompile_material(static_switch)")

    parameters = _ok(send_command("get_material_parameters", {
        "material_path": material_path,
    }), "get_material_parameters(static_switch_parent)")
    static_switches = [
        parameter
        for parameter in parameters.get("parameters", [])
        if parameter.get("type") == "static_switch"
    ]
    assert static_switches, parameters

    return material_path, static_switches[0]["name"]


def _find_vector_parameter(parameters: list[Dict[str, Any]], parameter_name: str) -> Dict[str, Any]:
    for parameter in parameters:
        if parameter.get("type") == "vector" and parameter.get("name") == parameter_name:
            return parameter
    pytest.fail(f"Missing vector parameter {parameter_name!r} in {parameters!r}")


@needs_bridge
class TestPhase9Wave4MaterialInstanceParameters:
    def test_01_set_vector_parameter_override(self):
        material_path = _create_material_instance()

        updated = _ok(send_command("set_material_instance_parameters", {
            "material_path": material_path,
            "vector_parameters": {
                "Color": [0.9, 0.1, 0.3, 1.0],
            },
        }), "set_material_instance_parameters")
        parameters = _ok(send_command("get_material_parameters", {
            "material_path": material_path,
        }), "get_material_parameters")

        base_color = _find_vector_parameter(parameters.get("parameters", []), "Color")
        default_value = base_color.get("default_value") or {}

        assert updated.get("success") is True, updated
        assert updated.get("updated_vector_count") == 1, updated
        assert base_color.get("is_override") is True, base_color
        assert default_value.get("r") == pytest.approx(0.9, abs=1e-3), base_color
        assert default_value.get("g") == pytest.approx(0.1, abs=1e-3), base_color
        assert default_value.get("b") == pytest.approx(0.3, abs=1e-3), base_color
        assert default_value.get("a") == pytest.approx(1.0, abs=1e-3), base_color

    def test_02_set_static_switch_parameter_override(self):
        parent_material_path, parameter_name = _create_static_switch_parent_material()

        suffix = int(time.time()) % 1000000
        instance_name = f"MI_Phase9_Wave4_Static_{suffix}"
        created = _ok(send_command("create_material_instance", {
            "parent_material": parent_material_path,
            "instance_name": instance_name,
            "dest_path": "/Game/Materials",
        }), f"create_material_instance({instance_name})")
        material_path = created.get("path") or f"/Game/Materials/{instance_name}"

        updated = _ok(send_command("set_material_instance_parameters", {
            "material_path": material_path,
            "static_switch_parameters": {
                parameter_name: True,
            },
        }), "set_material_instance_parameters(static_switch)")
        parameters = _ok(send_command("get_material_parameters", {
            "material_path": material_path,
        }), "get_material_parameters(static_switch_instance)")

        static_switch = next(
            parameter
            for parameter in parameters.get("parameters", [])
            if parameter.get("type") == "static_switch" and parameter.get("name") == parameter_name
        )

        assert updated.get("success") is True, updated
        assert updated.get("updated_static_switch_count") == 1, updated
        assert static_switch.get("default_value") is True, static_switch
        assert static_switch.get("is_override") is True, static_switch