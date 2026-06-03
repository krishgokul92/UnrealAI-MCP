"""Live-editor smoke tests for the first material graph mutation slice."""

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
    name = f"M_Phase9_Wave3_{suffix}"
    payload = _ok(send_command("create_material_asset", {
        "material_name": name,
        "dest_path": "/Game/Materials",
    }), f"create_material_asset({name})")
    return payload["material_path"]


@needs_bridge
class TestPhase9Wave3MaterialMutation:
    def test_01_create_connect_layout_recompile(self):
        material_path = _create_material()

        color_expr = _ok(send_command("create_material_expression", {
            "material_path": material_path,
            "expression_class": "MaterialExpressionConstant3Vector",
            "node_pos_x": -400,
            "node_pos_y": -100,
        }), "create_material_expression(color)")
        scalar_expr = _ok(send_command("create_material_expression", {
            "material_path": material_path,
            "expression_class": "MaterialExpressionConstant",
            "node_pos_x": -400,
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
        _ok(send_command("layout_material_expressions", {
            "material_path": material_path,
        }), "layout_material_expressions")
        _ok(send_command("recompile_material", {
            "material_path": material_path,
        }), "recompile_material")

        connections = _ok(send_command("get_material_connections", {
            "material_path": material_path,
        }), "get_material_connections")
        validation = _ok(send_command("validate_material_graph", {
            "material_path": material_path,
        }), "validate_material_graph")

        assert connections.get("connection_count", 0) >= 2, connections
        assert any(item.get("property") == "BaseColor" for item in connections.get("property_inputs", [])), connections
        assert validation.get("success") is True, validation
        assert validation.get("is_valid") is True, validation
        assert validation.get("summary", {}).get("error_count") == 0, validation

    def test_02_delete_material_expression(self):
        material_path = _create_material()

        created = _ok(send_command("create_material_expression", {
            "material_path": material_path,
            "expression_class": "MaterialExpressionConstant",
            "node_pos_x": -80,
            "node_pos_y": 40,
        }), "create_material_expression(delete)")
        expression_id = created["expression"]["expression_id"]

        before = _ok(send_command("get_material_expressions", {
            "material_path": material_path,
        }), "get_material_expressions(before)")

        deleted = _ok(send_command("delete_material_expression", {
            "material_path": material_path,
            "expression_id": expression_id,
        }), "delete_material_expression")

        after = _ok(send_command("get_material_expressions", {
            "material_path": material_path,
        }), "get_material_expressions(after)")

        assert deleted.get("success") is True, deleted
        assert before.get("count", 0) >= 1, before
        assert after.get("count") == before.get("count") - 1, (before, after)