"""Live-editor smoke tests for the first material graph disconnect helpers."""

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
    name = f"M_Phase9_Wave5_{suffix}"
    payload = _ok(send_command("create_material_asset", {
        "material_name": name,
        "dest_path": "/Game/Materials",
    }), f"create_material_asset({name})")
    return payload["material_path"]


def _create_connected_material() -> tuple[str, str, str, str]:
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
class TestPhase9Wave5MaterialDisconnect:
    def test_01_disconnect_material_expression_input(self):
        material_path, color_id, scalar_id, multiply_id = _create_connected_material()

        before = _ok(send_command("get_material_connections", {
            "material_path": material_path,
        }), "get_material_connections(before_disconnect_expression)")
        disconnected = _ok(send_command("disconnect_material_expressions", {
            "material_path": material_path,
            "from_expression_id": color_id,
            "to_expression_id": multiply_id,
            "to_input_name": "A",
        }), "disconnect_material_expressions")
        after = _ok(send_command("get_material_connections", {
            "material_path": material_path,
        }), "get_material_connections(after_disconnect_expression)")

        assert disconnected.get("success") is True, disconnected
        assert disconnected.get("to_input_name") == "A", disconnected
        assert before.get("connection_count") == 2, before
        assert after.get("connection_count") == 1, after
        assert not any(
            connection.get("to_expression_id") == multiply_id and connection.get("to_input_name") == "A"
            for connection in after.get("connections", [])
        ), after
        assert any(
            connection.get("to_expression_id") == multiply_id and connection.get("to_input_name") == "B"
            for connection in after.get("connections", [])
        ), after

    def test_02_disconnect_material_property(self):
        material_path, _color_id, _scalar_id, multiply_id = _create_connected_material()

        before = _ok(send_command("get_material_connections", {
            "material_path": material_path,
        }), "get_material_connections(before_disconnect_property)")
        disconnected = _ok(send_command("disconnect_material_property", {
            "material_path": material_path,
            "property_name": "BaseColor",
            "from_expression_id": multiply_id,
        }), "disconnect_material_property")
        after = _ok(send_command("get_material_connections", {
            "material_path": material_path,
        }), "get_material_connections(after_disconnect_property)")
        validation = _ok(send_command("validate_material_graph", {
            "material_path": material_path,
        }), "validate_material_graph(after_disconnect_property)")

        assert disconnected.get("success") is True, disconnected
        assert disconnected.get("property_name") == "BaseColor", disconnected
        assert any(item.get("property") == "BaseColor" for item in before.get("property_inputs", [])), before
        assert not any(item.get("property") == "BaseColor" for item in after.get("property_inputs", [])), after
        assert validation.get("is_valid") is False, validation
        assert any(issue.get("code") == "missing_output_binding" for issue in validation.get("issues", [])), validation