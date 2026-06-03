"""Live-editor smoke tests for the first read-only material graph tools.

These tests skip automatically when the UE bridge is unavailable.
"""

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


def _ensure_material_instance() -> str:
    suffix = int(time.time()) % 1000000
    name = f"MI_Phase9_Readonly_{suffix}"
    _ok(send_command("create_material_instance", {
        "parent_material": BASE_MATERIAL,
        "instance_name": name,
        "dest_path": "/Game/Materials",
        "vector_parameters": {
            "BaseColor": [0.2, 0.4, 0.8, 1.0],
        },
    }), f"create_material_instance({name})")
    return f"/Game/Materials/{name}"


@needs_bridge
class TestPhase9Wave2MaterialReadonly:
    def test_01_get_material_expressions(self):
        payload = _ok(send_command("get_material_expressions", {
            "material_path": BASE_MATERIAL,
        }), "get_material_expressions")

        assert payload.get("success") is True, payload
        assert payload.get("count", 0) > 0, payload
        assert payload.get("graph_material_path"), payload

        expr = payload["expressions"][0]
        assert expr.get("id"), expr
        assert expr.get("class"), expr
        assert isinstance(expr.get("editor_position"), list), expr

    def test_02_get_material_connections(self):
        payload = _ok(send_command("get_material_connections", {
            "material_path": BASE_MATERIAL,
        }), "get_material_connections")

        assert payload.get("success") is True, payload
        assert "connections" in payload, payload
        assert "property_inputs" in payload, payload
        assert payload.get("property_input_count", 0) >= 1, payload

    def test_03_get_material_parameters(self):
        material_instance_path = _ensure_material_instance()
        payload = _ok(send_command("get_material_parameters", {
            "material_path": material_instance_path,
        }), "get_material_parameters")

        assert payload.get("success") is True, payload
        assert payload.get("graph_material_path"), payload
        assert isinstance(payload.get("parameters"), list), payload
        assert payload.get("count", 0) >= 1, payload

        params_with_values = [param for param in payload["parameters"] if param.get("has_default_value")]
        assert params_with_values, payload

    def test_04_validate_material_graph(self):
        payload = _ok(send_command("validate_material_graph", {
            "material_path": BASE_MATERIAL,
        }), "validate_material_graph")

        assert payload.get("success") is True, payload
        assert payload.get("is_valid") is True, payload
        assert isinstance(payload.get("issues"), list), payload
        assert isinstance(payload.get("orphaned_expressions"), list), payload
        assert isinstance(payload.get("duplicate_parameters"), list), payload

        summary = payload.get("summary") or {}
        assert summary.get("expression_count", 0) > 0, payload
        assert summary.get("property_input_count", 0) >= 1, payload
        assert summary.get("error_count") == 0, payload
