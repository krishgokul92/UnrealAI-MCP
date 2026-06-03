"""Live-editor smoke tests for material-function support and bulk graph deletes."""

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
    name = f"M_Phase9_Wave7_{suffix}"
    payload = _ok(send_command("create_material_asset", {
        "material_name": name,
        "dest_path": "/Game/Materials",
    }), f"create_material_asset({name})")
    return payload["material_path"]


def _create_material_function() -> str:
    suffix = int(time.time()) % 1000000
    name = f"MF_Phase9_Wave7_{suffix}"
    payload = _ok(send_command("create_material_function_asset", {
        "material_function_name": name,
        "dest_path": "/Game/MaterialFunctions",
    }), f"create_material_function_asset({name})")
    return payload["material_function_path"]


@needs_bridge
class TestPhase9Wave7MaterialFunctionsBulkDelete:
    def test_01_material_function_graph_authoring(self):
        function_path = _create_material_function()

        function_input = _ok(send_command("create_material_expression", {
            "material_path": function_path,
            "expression_class": "MaterialExpressionFunctionInput",
            "node_pos_x": -260,
            "node_pos_y": 0,
        }), "create_material_expression(function_input)")
        function_output = _ok(send_command("create_material_expression", {
            "material_path": function_path,
            "expression_class": "MaterialExpressionFunctionOutput",
            "node_pos_x": 0,
            "node_pos_y": 0,
        }), "create_material_expression(function_output)")

        input_id = function_input["expression"]["expression_id"]
        output_id = function_output["expression"]["expression_id"]

        _ok(send_command("connect_material_expressions", {
            "material_path": function_path,
            "from_expression_id": input_id,
            "to_expression_id": output_id,
        }), "connect_material_expressions(function_input_to_output)")
        _ok(send_command("layout_material_expressions", {
            "material_path": function_path,
        }), "layout_material_expressions(function)")
        _ok(send_command("recompile_material", {
            "material_path": function_path,
        }), "recompile_material(function)")

        expressions = _ok(send_command("get_material_expressions", {
            "material_path": function_path,
        }), "get_material_expressions(function)")
        connections = _ok(send_command("get_material_connections", {
            "material_path": function_path,
        }), "get_material_connections(function)")
        validation = _ok(send_command("validate_material_graph", {
            "material_path": function_path,
        }), "validate_material_graph(function)")

        assert expressions.get("count") == 2, expressions
        assert expressions.get("graph_asset_type") == "material_function", expressions
        assert connections.get("connection_count") == 1, connections
        assert connections.get("property_input_count") == 0, connections
        assert validation.get("success") is True, validation
        assert validation.get("is_valid") is True, validation
        assert validation.get("summary", {}).get("function_output_count") == 1, validation
        assert validation.get("summary", {}).get("connected_function_output_count") == 1, validation
        assert validation.get("summary", {}).get("error_count") == 0, validation

    def test_02_delete_material_expressions_bulk(self):
        material_path = _create_material()

        left = _ok(send_command("create_material_expression", {
            "material_path": material_path,
            "expression_class": "MaterialExpressionConstant",
            "node_pos_x": -240,
            "node_pos_y": -80,
        }), "create_material_expression(left)")
        right = _ok(send_command("create_material_expression", {
            "material_path": material_path,
            "expression_class": "MaterialExpressionConstant",
            "node_pos_x": -240,
            "node_pos_y": 80,
        }), "create_material_expression(right)")
        survivor = _ok(send_command("create_material_expression", {
            "material_path": material_path,
            "expression_class": "MaterialExpressionAdd",
            "node_pos_x": -40,
            "node_pos_y": 0,
        }), "create_material_expression(survivor)")

        left_id = left["expression"]["expression_id"]
        right_id = right["expression"]["expression_id"]
        survivor_id = survivor["expression"]["expression_id"]

        before = _ok(send_command("get_material_expressions", {
            "material_path": material_path,
        }), "get_material_expressions(before_bulk_delete)")
        deleted = _ok(send_command("delete_material_expressions", {
            "material_path": material_path,
            "expression_ids": [left_id, right_id],
        }), "delete_material_expressions")
        after = _ok(send_command("get_material_expressions", {
            "material_path": material_path,
        }), "get_material_expressions(after_bulk_delete)")

        remaining_ids = {expr.get("id") for expr in after.get("expressions", [])}

        assert deleted.get("success") is True, deleted
        assert deleted.get("deleted_count") == 2, deleted
        assert before.get("count") == after.get("count") + 2, (before, after)
        assert left_id not in remaining_ids, after
        assert right_id not in remaining_ids, after
        assert survivor_id in remaining_ids, after
