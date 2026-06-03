"""Live-editor smoke tests for material graph replace helpers."""

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


def _create_material() -> str:
    suffix = int(time.time()) % 1000000
    name = f"M_Phase9_Wave6_{suffix}"
    payload = _ok(send_command("create_material_asset", {
        "material_name": name,
        "dest_path": "/Game/Materials",
    }), f"create_material_asset({name})")
    return payload["material_path"]


def _create_replaceable_material() -> tuple[str, str, str, str]:
    material_path = _create_material()

    color_expr = _ok(send_command("create_material_expression", {
        "material_path": material_path,
        "expression_class": "MaterialExpressionConstant3Vector",
        "node_pos_x": -420,
        "node_pos_y": -100,
    }), "create_material_expression(color)")
    scalar_expr = _ok(send_command("create_material_expression", {
        "material_path": material_path,
        "expression_class": "MaterialExpressionConstant",
        "node_pos_x": -420,
        "node_pos_y": 120,
    }), "create_material_expression(scalar)")
    multiply_expr = _ok(send_command("create_material_expression", {
        "material_path": material_path,
        "expression_class": "MaterialExpressionMultiply",
        "node_pos_x": -120,
        "node_pos_y": 0,
    }), "create_material_expression(multiply)")

    color_id = color_expr["expression"]["expression_id"]
    scalar_id = scalar_expr["expression"]["expression_id"]
    multiply_id = multiply_expr["expression"]["expression_id"]

    _ok(send_command("connect_material_expressions", {
        "material_path": material_path,
        "from_expression_id": color_id,
        "to_expression_id": multiply_id,
        "to_input_name": "A",
    }), "connect_material_expressions(A)")
    _ok(send_command("connect_material_expressions", {
        "material_path": material_path,
        "from_expression_id": scalar_id,
        "to_expression_id": multiply_id,
        "to_input_name": "B",
    }), "connect_material_expressions(B)")
    _ok(send_command("connect_material_property", {
        "material_path": material_path,
        "from_expression_id": multiply_id,
        "property_name": "BaseColor",
    }), "connect_material_property(BaseColor)")

    return material_path, color_id, scalar_id, multiply_id


@needs_bridge
class TestPhase9Wave6MaterialReplace:
    def test_01_replace_material_expression_preserves_connections(self):
        material_path, color_id, scalar_id, multiply_id = _create_replaceable_material()

        before_expressions = _ok(send_command("get_material_expressions", {
            "material_path": material_path,
        }), "get_material_expressions(before_replace)")
        replaced = _ok(send_command("replace_material_expression", {
            "material_path": material_path,
            "expression_id": multiply_id,
            "new_expression_class": "MaterialExpressionAdd",
        }), "replace_material_expression")
        after_expressions = _ok(send_command("get_material_expressions", {
            "material_path": material_path,
        }), "get_material_expressions(after_replace)")
        connections = _ok(send_command("get_material_connections", {
            "material_path": material_path,
        }), "get_material_connections(after_replace)")
        validation = _ok(send_command("validate_material_graph", {
            "material_path": material_path,
        }), "validate_material_graph(after_replace)")

        replacement = replaced["replacement_expression"]
        replacement_id = replacement["expression_id"]

        assert replaced.get("success") is True, replaced
        assert replaced.get("reconnected_input_count") == 2, replaced
        assert replaced.get("reconnected_output_count") == 0, replaced
        assert replaced.get("reconnected_property_count") == 1, replaced
        assert replacement.get("expression_class") == "MaterialExpressionAdd", replaced
        assert before_expressions.get("count") == after_expressions.get("count"), (before_expressions, after_expressions)
        assert not any(expr.get("id") == multiply_id for expr in after_expressions.get("expressions", [])), after_expressions
        assert any(expr.get("id") == replacement_id for expr in after_expressions.get("expressions", [])), after_expressions
        assert any(
            connection.get("from_expression_id") == color_id and
            connection.get("to_expression_id") == replacement_id and
            connection.get("to_input_name") == "A"
            for connection in connections.get("connections", [])
        ), connections
        assert any(
            connection.get("from_expression_id") == scalar_id and
            connection.get("to_expression_id") == replacement_id and
            connection.get("to_input_name") == "B"
            for connection in connections.get("connections", [])
        ), connections
        assert any(
            item.get("property") == "BaseColor" and item.get("expression_id") == replacement_id
            for item in connections.get("property_inputs", [])
        ), connections
        assert validation.get("is_valid") is True, validation