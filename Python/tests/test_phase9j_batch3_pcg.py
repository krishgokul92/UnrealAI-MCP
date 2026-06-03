"""Live-editor smoke tests for Phase 9j batch 3 PCG mutation helpers."""

from __future__ import annotations

import json
import sys
import time
from pathlib import Path
from typing import Any, Dict, Optional

import pytest


_PY_ROOT = Path(__file__).resolve().parent.parent
if str(_PY_ROOT) not in sys.path:
    sys.path.insert(0, str(_PY_ROOT))

from ue_bridge import ping, send_command  # noqa: E402


SKIP_REASON = "UE bridge not reachable on 127.0.0.1:55557 (open the editor first)"
STARTUP_SETTLE_SECONDS = 10
SUPPORTED_SCALAR_KINDS = {"bool", "int32", "int64", "float", "double"}


@pytest.fixture(scope="module", autouse=True)
def require_bridge() -> None:
    for _ in range(30):
        if ping():
            time.sleep(STARTUP_SETTLE_SECONDS)
            _ok(send_command("new_blank_map", {
                "save_existing_map": False,
            }), "new_blank_map")
            return
        time.sleep(1)
    pytest.skip(SKIP_REASON)


def _unwrap(resp: Dict[str, Any]) -> Dict[str, Any]:
    if isinstance(resp, dict) and resp.get("status") == "success" and isinstance(resp.get("result"), dict):
        return resp["result"]
    return resp


def _ok(resp: Dict[str, Any], context: str) -> Dict[str, Any]:
    if not isinstance(resp, dict):
        pytest.fail(f"{context}: non-dict response: {resp!r}")
    if resp.get("status") == "error":
        pytest.fail(f"{context}: bridge error: {json.dumps(resp)[:1000]}")
    return _unwrap(resp)


def _find_scalar_property(node: Dict[str, Any]) -> Optional[Dict[str, Any]]:
    for prop in node.get("editable_properties") or []:
        if prop.get("property_kind") in SUPPORTED_SCALAR_KINDS:
            return prop
    return None


def _property_by_name(node: Dict[str, Any], property_name: str) -> Dict[str, Any]:
    for prop in node.get("editable_properties") or []:
        if prop.get("name") == property_name:
            return prop
    pytest.fail(f"Property {property_name!r} was not found in editable_properties")


def _next_scalar_value(prop: Dict[str, Any]) -> Any:
    kind = str(prop.get("property_kind") or "")
    current_text = str(prop.get("value_text") or "").strip()

    if kind == "bool":
        return current_text.lower() not in {"true", "1"}
    if kind in {"int32", "int64"}:
        try:
            current = int(float(current_text))
        except ValueError:
            current = 0
        return current + 1
    if kind in {"float", "double"}:
        try:
            current = float(current_text)
        except ValueError:
            current = 0.0
        return current + 1.25

    pytest.fail(f"Unsupported scalar property kind: {kind}")


def _parameter_by_name(graph_summary: Dict[str, Any], parameter_name: str) -> Dict[str, Any]:
    for parameter in graph_summary.get("user_parameters") or []:
        if parameter.get("name") == parameter_name:
            return parameter
    pytest.fail(f"Parameter {parameter_name!r} was not found")


class TestPhase9jBatch3PCG:
    SUFFIX = str(int(time.time()))[-6:]

    @classmethod
    def _name(cls, base: str) -> str:
        return f"{base}_{cls.SUFFIX}"

    def test_01_update_node_settings_and_state(self):
        graph_name = self._name("PCG_NodeMutations")
        destination_path = "/Game/CopilotTests/PCG"
        graph_asset_path = f"{destination_path}/{graph_name}.{graph_name}"

        listed_types = _ok(send_command("list_pcg_node_types", {
            "query": "surface",
            "max_results": 50,
        }), "list_pcg_node_types")
        node_types = listed_types.get("node_types") or []
        selected_type = next((node_type for node_type in node_types if node_type.get("class_path")), None)
        assert selected_type, listed_types

        created_graph = _ok(send_command("create_pcg_graph_asset", {
            "pcg_graph_name": graph_name,
            "destination_path": destination_path,
        }), "create_pcg_graph_asset")
        assert created_graph.get("asset_path") == graph_asset_path, created_graph

        added_node = _ok(send_command("add_pcg_graph_node", {
            "asset_path": graph_asset_path,
            "settings_class": selected_type["class_path"],
            "node_title": "Copilot Mutable Surface Node",
            "position_x": 320,
            "position_y": 160,
        }), "add_pcg_graph_node")
        assert added_node.get("node_path"), added_node

        read_node = _ok(send_command("read_pcg_graph_node", {
            "asset_path": graph_asset_path,
            "node_path": added_node["node_path"],
        }), "read_pcg_graph_node before update")

        assert isinstance(read_node.get("editable_properties"), list), read_node
        assert read_node.get("editable_property_count") == len(read_node.get("editable_properties") or []), read_node
        assert isinstance(read_node.get("overridable_params"), list), read_node
        assert read_node.get("overridable_param_count") == len(read_node.get("overridable_params") or []), read_node

        scalar_property = _find_scalar_property(read_node)
        assert scalar_property, read_node

        property_name = str(scalar_property["name"])
        new_value = _next_scalar_value(scalar_property)
        updated_node = _ok(send_command("update_pcg_graph_node_settings", {
            "asset_path": graph_asset_path,
            "node_path": added_node["node_path"],
            "settings_patch": {
                property_name: new_value,
            },
        }), "update_pcg_graph_node_settings")

        assert updated_node.get("updated") is True, updated_node
        assert property_name in (updated_node.get("updated_properties") or []), updated_node

        reread_node = _ok(send_command("read_pcg_graph_node", {
            "asset_path": graph_asset_path,
            "node_path": added_node["node_path"],
        }), "read_pcg_graph_node after settings update")
        updated_property = _property_by_name(reread_node, property_name)
        assert updated_property.get("value_text") != scalar_property.get("value_text"), reread_node

        state_payload: Dict[str, Any] = {
            "asset_path": graph_asset_path,
            "node_path": added_node["node_path"],
        }
        expected_field: Optional[str] = None
        expected_value: Optional[bool] = None
        if reread_node.get("can_be_disabled"):
            expected_field = "enabled"
            expected_value = not bool(reread_node.get("enabled"))
            state_payload[expected_field] = expected_value
        elif reread_node.get("can_be_debugged"):
            expected_field = "debug"
            expected_value = not bool(reread_node.get("debug"))
            state_payload[expected_field] = expected_value

        assert expected_field, reread_node
        state_result = _ok(send_command("set_pcg_graph_node_state", state_payload), "set_pcg_graph_node_state")
        assert state_result.get("updated") is True, state_result
        assert expected_field in (state_result.get("updated_state_fields") or []), state_result
        assert state_result.get(expected_field) is expected_value, state_result

    def test_02_create_rename_set_and_reset_graph_parameters(self):
        graph_name = self._name("PCG_GraphParameters")
        instance_name = self._name("PCG_GraphParameters_Instance")
        destination_path = "/Game/CopilotTests/PCG"
        graph_asset_path = f"{destination_path}/{graph_name}.{graph_name}"
        instance_asset_path = f"{destination_path}/{instance_name}.{instance_name}"

        created_graph = _ok(send_command("create_pcg_graph_asset", {
            "pcg_graph_name": graph_name,
            "destination_path": destination_path,
        }), "create_pcg_graph_asset for parameters")
        assert created_graph.get("asset_path") == graph_asset_path, created_graph

        created_parameter = _ok(send_command("create_pcg_graph_parameter", {
            "asset_path": graph_asset_path,
            "parameter_name": "SpawnCount",
            "parameter_type": "Int32",
            "default_value": 7,
        }), "create_pcg_graph_parameter")
        assert created_parameter.get("created") is True, created_parameter
        assert created_parameter.get("parameter_name") == "SpawnCount", created_parameter

        graph_readback = _ok(send_command("read_pcg_graph_content", {
            "asset_path": graph_asset_path,
        }), "read_pcg_graph_content after create parameter")
        created_summary = _parameter_by_name(graph_readback, "SpawnCount")
        assert created_summary.get("property_kind") == "int32", created_summary
        assert created_summary.get("value_type") == "Int32", created_summary
        assert created_summary.get("value_text") == "7", created_summary
        assert created_summary.get("overridden") is False, created_summary

        renamed_parameter = _ok(send_command("rename_pcg_graph_parameter", {
            "asset_path": graph_asset_path,
            "current_name": "SpawnCount",
            "new_name": "SpawnCountRenamed",
        }), "rename_pcg_graph_parameter")
        assert renamed_parameter.get("renamed") is True, renamed_parameter
        assert renamed_parameter.get("parameter_name") == "SpawnCountRenamed", renamed_parameter

        set_graph_parameter = _ok(send_command("set_pcg_graph_parameter", {
            "asset_path": graph_asset_path,
            "parameter_name": "SpawnCountRenamed",
            "value": 11,
        }), "set_pcg_graph_parameter on graph")
        assert set_graph_parameter.get("updated") is True, set_graph_parameter

        reread_graph = _ok(send_command("read_pcg_graph_content", {
            "asset_path": graph_asset_path,
        }), "read_pcg_graph_content after set graph parameter")
        renamed_summary = _parameter_by_name(reread_graph, "SpawnCountRenamed")
        assert renamed_summary.get("value_text") == "11", renamed_summary
        assert renamed_summary.get("overridden") is False, renamed_summary

        created_instance = _ok(send_command("create_pcg_graph_instance", {
            "instance_name": instance_name,
            "destination_path": destination_path,
            "parent_graph_path": graph_asset_path,
        }), "create_pcg_graph_instance")
        assert created_instance.get("asset_path") == instance_asset_path, created_instance

        set_instance_parameter = _ok(send_command("set_pcg_graph_parameter", {
            "asset_path": instance_asset_path,
            "parameter_name": "SpawnCountRenamed",
            "value": 23,
        }), "set_pcg_graph_parameter on instance")
        assert set_instance_parameter.get("updated") is True, set_instance_parameter

        instance_readback = _ok(send_command("read_pcg_graph_content", {
            "asset_path": instance_asset_path,
        }), "read_pcg_graph_content after set instance override")
        instance_summary = _parameter_by_name(instance_readback, "SpawnCountRenamed")
        assert instance_summary.get("value_text") == "23", instance_summary
        assert instance_summary.get("overridden") is True, instance_summary

        reset_override = _ok(send_command("reset_pcg_graph_parameter_override", {
            "asset_path": instance_asset_path,
            "parameter_name": "SpawnCountRenamed",
        }), "reset_pcg_graph_parameter_override")
        assert reset_override.get("reset") is True, reset_override
        assert reset_override.get("overridden_before") is True, reset_override
        assert reset_override.get("overridden_after") is False, reset_override

        reset_readback = _ok(send_command("read_pcg_graph_content", {
            "asset_path": instance_asset_path,
        }), "read_pcg_graph_content after reset instance override")
        reset_summary = _parameter_by_name(reset_readback, "SpawnCountRenamed")
        assert reset_summary.get("value_text") == "11", reset_summary
        assert reset_summary.get("overridden") is False, reset_summary