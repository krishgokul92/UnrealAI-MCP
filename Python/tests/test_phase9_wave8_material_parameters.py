"""Live-editor smoke tests for base material parameter-expression mutation."""

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

DEFAULT_TEXTURE_PATH = "/Engine/EngineResources/DefaultTexture"


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


def _create_material() -> str:
    suffix = int(time.time()) % 1000000
    name = f"M_Phase9_Wave8_{suffix}"
    payload = _ok(send_command("create_material_asset", {
        "material_name": name,
        "dest_path": "/Game/Materials",
    }), f"create_material_asset({name})")
    return payload["material_path"]


def _create_parameter_material() -> tuple[str, Dict[str, str]]:
    material_path = _create_material()

    scalar_expr = _ok(send_command("create_material_expression", {
        "material_path": material_path,
        "expression_class": "MaterialExpressionScalarParameter",
        "node_pos_x": -420,
        "node_pos_y": -220,
    }), "create_material_expression(scalar_parameter)")
    vector_expr = _ok(send_command("create_material_expression", {
        "material_path": material_path,
        "expression_class": "MaterialExpressionVectorParameter",
        "node_pos_x": -420,
        "node_pos_y": -40,
    }), "create_material_expression(vector_parameter)")
    _ok(send_command("create_material_expression", {
        "material_path": material_path,
        "expression_class": "MaterialExpressionTextureSampleParameter2D",
        "node_pos_x": -420,
        "node_pos_y": 140,
    }), "create_material_expression(texture_parameter)")
    _ok(send_command("create_material_expression", {
        "material_path": material_path,
        "expression_class": "MaterialExpressionStaticSwitchParameter",
        "node_pos_x": -420,
        "node_pos_y": 320,
    }), "create_material_expression(static_switch_parameter)")

    _ok(send_command("connect_material_property", {
        "material_path": material_path,
        "from_expression_id": vector_expr["expression"]["expression_id"],
        "property_name": "BaseColor",
    }), "connect_material_property(vector_parameter)")
    _ok(send_command("recompile_material", {
        "material_path": material_path,
    }), "recompile_material(parameter_material)")

    parameters = _ok(send_command("get_material_parameters", {
        "material_path": material_path,
    }), "get_material_parameters(before_update)")

    names_by_type: Dict[str, str] = {}
    for parameter in parameters.get("parameters", []):
        parameter_type = parameter.get("type")
        parameter_name = parameter.get("name")
        if parameter_type and parameter_name and parameter_type not in names_by_type:
            names_by_type[parameter_type] = parameter_name

    for required_type in ("scalar", "vector", "texture", "static_switch"):
        assert required_type in names_by_type, parameters

    return material_path, names_by_type


def _find_parameter(parameters: list[Dict[str, Any]], parameter_type: str, parameter_name: str) -> Dict[str, Any]:
    for parameter in parameters:
        if parameter.get("type") == parameter_type and parameter.get("name") == parameter_name:
            return parameter
    pytest.fail(f"Missing {parameter_type} parameter {parameter_name!r} in {parameters!r}")


@needs_bridge
class TestPhase9Wave8MaterialParameters:
    def test_01_set_material_parameter_defaults(self):
        material_path, parameter_names = _create_parameter_material()

        updated = _ok(send_command("set_material_parameters", {
            "material_path": material_path,
            "scalar_parameters": {
                parameter_names["scalar"]: 0.75,
            },
            "vector_parameters": {
                parameter_names["vector"]: [0.15, 0.35, 0.85, 1.0],
            },
            "texture_parameters": {
                parameter_names["texture"]: DEFAULT_TEXTURE_PATH,
            },
            "static_switch_parameters": {
                parameter_names["static_switch"]: True,
            },
        }), "set_material_parameters")
        parameters = _ok(send_command("get_material_parameters", {
            "material_path": material_path,
        }), "get_material_parameters(after_update)")

        scalar_parameter = _find_parameter(parameters.get("parameters", []), "scalar", parameter_names["scalar"])
        vector_parameter = _find_parameter(parameters.get("parameters", []), "vector", parameter_names["vector"])
        texture_parameter = _find_parameter(parameters.get("parameters", []), "texture", parameter_names["texture"])
        static_switch_parameter = _find_parameter(parameters.get("parameters", []), "static_switch", parameter_names["static_switch"])

        vector_default = vector_parameter.get("default_value") or {}
        texture_default = texture_parameter.get("default_value") or ""

        assert updated.get("success") is True, updated
        assert updated.get("updated_scalar_count") == 1, updated
        assert updated.get("updated_vector_count") == 1, updated
        assert updated.get("updated_texture_count") == 1, updated
        assert updated.get("updated_static_switch_count") == 1, updated
        assert scalar_parameter.get("default_value") == pytest.approx(0.75, abs=1e-3), scalar_parameter
        assert vector_default.get("r") == pytest.approx(0.15, abs=1e-3), vector_parameter
        assert vector_default.get("g") == pytest.approx(0.35, abs=1e-3), vector_parameter
        assert vector_default.get("b") == pytest.approx(0.85, abs=1e-3), vector_parameter
        assert vector_default.get("a") == pytest.approx(1.0, abs=1e-3), vector_parameter
        assert "DefaultTexture" in texture_default, texture_parameter
        assert static_switch_parameter.get("default_value") is True, static_switch_parameter
