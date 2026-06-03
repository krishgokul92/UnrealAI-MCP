"""Live-editor smoke tests for Phase 9j batch 2 PCG node catalog and graph editing helpers."""

from __future__ import annotations

import json
import sys
import time
from pathlib import Path
from typing import Any, Dict, Iterable

import pytest


_PY_ROOT = Path(__file__).resolve().parent.parent
if str(_PY_ROOT) not in sys.path:
    sys.path.insert(0, str(_PY_ROOT))

from ue_bridge import ping, send_command  # noqa: E402


SKIP_REASON = "UE bridge not reachable on 127.0.0.1:55557 (open the editor first)"
STARTUP_SETTLE_SECONDS = 10


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


def _first_pin_label(pins: Iterable[Dict[str, Any]]) -> str:
    for pin in pins:
        label = pin.get("label")
        if label:
            return str(label)
    pytest.fail("No usable pin label was returned")


def _pick_editable_node_type(node_types: list[Dict[str, Any]]) -> Dict[str, Any]:
    for node_type in node_types:
        if node_type.get("settings_type") == "InputOutput":
            continue
        if node_type.get("default_input_pins") and node_type.get("default_output_pins"):
            return node_type
    pytest.fail("No editable PCG node type with both input and output pins was returned")


class TestPhase9jBatch2PCG:
    SUFFIX = str(int(time.time()))[-6:]

    @classmethod
    def _name(cls, base: str) -> str:
        return f"{base}_{cls.SUFFIX}"

    def test_01_list_node_types_and_read_added_node(self):
        graph_name = self._name("PCG_GraphNodes")
        destination_path = "/Game/CopilotTests/PCG"
        graph_asset_path = f"{destination_path}/{graph_name}.{graph_name}"

        listed_types = _ok(send_command("list_pcg_node_types", {
            "query": "surface",
            "max_results": 50,
        }), "list_pcg_node_types")

        assert listed_types.get("count", 0) > 0, listed_types
        node_types = listed_types.get("node_types") or []
        assert isinstance(node_types, list) and node_types, listed_types
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
            "node_title": "Copilot Surface Node",
            "position_x": 240,
            "position_y": 120,
        }), "add_pcg_graph_node")

        assert added_node.get("created") is True, added_node
        assert added_node.get("settings_class_path") == selected_type["class_path"], added_node
        assert added_node.get("position_x") == 240, added_node
        assert added_node.get("position_y") == 120, added_node
        assert added_node.get("node_path"), added_node

        read_node = _ok(send_command("read_pcg_graph_node", {
            "asset_path": graph_asset_path,
            "node_path": added_node["node_path"],
        }), "read_pcg_graph_node")

        assert read_node.get("node_path") == added_node["node_path"], read_node
        assert read_node.get("settings_class_path") == selected_type["class_path"], read_node
        assert isinstance(read_node.get("input_pins"), list), read_node
        assert isinstance(read_node.get("output_pins"), list), read_node

    def test_02_connect_and_disconnect_graph_nodes(self):
        graph_name = self._name("PCG_GraphEdges")
        destination_path = "/Game/CopilotTests/PCG"
        graph_asset_path = f"{destination_path}/{graph_name}.{graph_name}"

        catalog = _ok(send_command("list_pcg_node_types", {
            "max_results": 400,
        }), "list_pcg_node_types for graph editing")
        node_type = _pick_editable_node_type(catalog.get("node_types") or [])

        created_graph = _ok(send_command("create_pcg_graph_asset", {
            "pcg_graph_name": graph_name,
            "destination_path": destination_path,
        }), "create_pcg_graph_asset for graph editing")
        assert created_graph.get("asset_path") == graph_asset_path, created_graph

        added_node = _ok(send_command("add_pcg_graph_node", {
            "asset_path": graph_asset_path,
            "settings_class": node_type["class_path"],
            "node_title": "Copilot Editable Node",
            "position_x": 480,
            "position_y": 180,
        }), "add_pcg_graph_node for graph editing")

        graph_nodes = _ok(send_command("read_pcg_graph_nodes", {
            "asset_path": graph_asset_path,
        }), "read_pcg_graph_nodes")
        nodes = graph_nodes.get("nodes") or []
        assert len(nodes) >= 3, graph_nodes

        input_node = next(
            (
                node for node in nodes
                if node.get("settings_type") == "InputOutput" and node.get("default_title") == "Input Node"
            ),
            None,
        )
        output_node = next(
            (
                node for node in nodes
                if node.get("settings_type") == "InputOutput" and node.get("default_title") == "Output Node"
            ),
            None,
        )
        assert input_node, graph_nodes
        assert output_node, graph_nodes

        input_to_added = _ok(send_command("connect_pcg_graph_nodes", {
            "asset_path": graph_asset_path,
            "source_node_path": input_node["node_path"],
            "source_pin_name": _first_pin_label(input_node["output_pins"]),
            "target_node_path": added_node["node_path"],
            "target_pin_name": _first_pin_label(added_node["input_pins"]),
        }), "connect_pcg_graph_nodes input->added")
        assert input_to_added.get("connected") is True, input_to_added

        added_to_output = _ok(send_command("connect_pcg_graph_nodes", {
            "asset_path": graph_asset_path,
            "source_node_path": added_node["node_path"],
            "source_pin_name": _first_pin_label(added_node["output_pins"]),
            "target_node_path": output_node["node_path"],
            "target_pin_name": _first_pin_label(output_node["input_pins"]),
        }), "connect_pcg_graph_nodes added->output")
        assert added_to_output.get("connected") is True, added_to_output

        read_node = _ok(send_command("read_pcg_graph_node", {
            "asset_path": graph_asset_path,
            "node_path": added_node["node_path"],
        }), "read_pcg_graph_node after connect")

        assert any(pin.get("connected") for pin in read_node.get("input_pins", [])), read_node
        assert any(pin.get("connected") for pin in read_node.get("output_pins", [])), read_node

        removed = _ok(send_command("disconnect_pcg_graph_nodes", {
            "asset_path": graph_asset_path,
            "source_node_path": added_node["node_path"],
            "source_pin_name": _first_pin_label(added_node["output_pins"]),
            "target_node_path": output_node["node_path"],
            "target_pin_name": _first_pin_label(output_node["input_pins"]),
        }), "disconnect_pcg_graph_nodes")
        assert removed.get("removed") is True, removed