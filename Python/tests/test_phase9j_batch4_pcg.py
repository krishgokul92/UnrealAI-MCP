"""Live-editor smoke tests for the remaining Phase 9j PCG graph helpers."""

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


def _comment_by_guid(graph_summary: Dict[str, Any], comment_guid: str) -> Dict[str, Any]:
    for comment in graph_summary.get("comment_nodes") or []:
        if comment.get("comment_guid") == comment_guid:
            return comment
    pytest.fail(f"Comment {comment_guid!r} was not found")


def _parameter_by_name(graph_summary: Dict[str, Any], parameter_name: str) -> Optional[Dict[str, Any]]:
    for parameter in graph_summary.get("user_parameters") or []:
        if parameter.get("name") == parameter_name:
            return parameter
    return None


def _find_subgraph_node_type() -> Dict[str, Any]:
    listed_types = _ok(send_command("list_pcg_node_types", {
        "max_results": 400,
    }), "list_pcg_node_types for subgraph")
    node_types = listed_types.get("node_types") or []
    node_type = next(
        (
            candidate for candidate in node_types
            if str(candidate.get("class_name") or "").endswith("PCGSubgraphSettings")
        ),
        None,
    )
    assert node_type and node_type.get("class_path"), listed_types
    return node_type


def _active_subgraph_path(node_summary: Dict[str, Any]) -> str:
    if "subgraph_graph_path" in node_summary:
        return str(node_summary.get("subgraph_graph_path") or "")
    return str(
        node_summary.get("subgraph_override_path")
        or node_summary.get("subgraph_instance_path")
        or ""
    )


class TestPhase9jBatch4PCG:
    SUFFIX = str(int(time.time()))[-6:]

    @classmethod
    def _name(cls, base: str) -> str:
        return f"{base}_{cls.SUFFIX}"

    def test_01_comment_boxes_named_reroutes_and_node_layout(self):
        graph_name = self._name("PCG_CommentsAndReroutes")
        destination_path = "/Game/CopilotTests/PCG"
        graph_asset_path = f"{destination_path}/{graph_name}.{graph_name}"

        created_graph = _ok(send_command("create_pcg_graph_asset", {
            "pcg_graph_name": graph_name,
            "destination_path": destination_path,
        }), "create_pcg_graph_asset for comments/reroutes")
        assert created_graph.get("asset_path") == graph_asset_path, created_graph

        created_comment = _ok(send_command("add_pcg_graph_comment", {
            "asset_path": graph_asset_path,
            "comment_text": "Copilot comment box",
            "position_x": 96,
            "position_y": 144,
            "width": 420,
            "height": 180,
            "comment_color": [0.2, 0.45, 0.8, 1.0],
            "font_size": 20,
        }), "add_pcg_graph_comment")
        comment_guid = str(created_comment.get("comment_guid") or "")
        assert comment_guid, created_comment

        graph_readback = _ok(send_command("read_pcg_graph_content", {
            "asset_path": graph_asset_path,
        }), "read_pcg_graph_content after comment create")
        assert graph_readback.get("comment_count") == 1, graph_readback
        created_comment_summary = _comment_by_guid(graph_readback, comment_guid)
        assert created_comment_summary.get("comment_text") == "Copilot comment box", created_comment_summary
        assert created_comment_summary.get("width") == 420, created_comment_summary

        updated_comment = _ok(send_command("update_pcg_graph_comment", {
            "asset_path": graph_asset_path,
            "comment_guid": comment_guid,
            "comment_text": "Updated comment box",
            "position_x": 160,
            "width": 512,
        }), "update_pcg_graph_comment")
        assert updated_comment.get("updated") is True, updated_comment

        reread_graph = _ok(send_command("read_pcg_graph_content", {
            "asset_path": graph_asset_path,
        }), "read_pcg_graph_content after comment update")
        updated_comment_summary = _comment_by_guid(reread_graph, comment_guid)
        assert updated_comment_summary.get("comment_text") == "Updated comment box", updated_comment_summary
        assert updated_comment_summary.get("position_x") == 160, updated_comment_summary
        assert updated_comment_summary.get("width") == 512, updated_comment_summary

        declaration_node = _ok(send_command("add_pcg_graph_reroute", {
            "asset_path": graph_asset_path,
            "reroute_kind": "named_declaration",
            "node_title": "Copilot Named Flow",
            "position_x": 320,
            "position_y": 120,
        }), "add_pcg_graph_reroute declaration")
        assert declaration_node.get("reroute_kind") == "named_declaration", declaration_node

        usage_node = _ok(send_command("add_pcg_graph_reroute", {
            "asset_path": graph_asset_path,
            "reroute_kind": "named_usage",
            "declaration_node_path": declaration_node["node_path"],
            "position_x": 560,
            "position_y": 120,
        }), "add_pcg_graph_reroute usage")
        assert usage_node.get("reroute_kind") == "named_usage", usage_node
        assert usage_node.get("declaration_node_path") == declaration_node.get("node_path"), usage_node

        declaration_readback = _ok(send_command("read_pcg_graph_node", {
            "asset_path": graph_asset_path,
            "node_path": declaration_node["node_path"],
        }), "read_pcg_graph_node declaration")
        assert declaration_readback.get("usage_node_count") == 1, declaration_readback
        usage_nodes = declaration_readback.get("usage_nodes") or []
        assert usage_nodes and usage_nodes[0].get("node_path") == usage_node.get("node_path"), declaration_readback

        moved_usage = _ok(send_command("set_pcg_graph_node_position", {
            "asset_path": graph_asset_path,
            "node_path": usage_node["node_path"],
            "position_x": 704,
            "position_y": 256,
        }), "set_pcg_graph_node_position")
        assert moved_usage.get("position_x") == 704, moved_usage
        assert moved_usage.get("position_y") == 256, moved_usage

        deleted_comment = _ok(send_command("delete_pcg_graph_comment", {
            "asset_path": graph_asset_path,
            "comment_guid": comment_guid,
        }), "delete_pcg_graph_comment")
        assert deleted_comment.get("deleted") is True, deleted_comment

        final_graph = _ok(send_command("read_pcg_graph_content", {
            "asset_path": graph_asset_path,
        }), "read_pcg_graph_content after comment delete")
        assert final_graph.get("comment_count") == 0, final_graph

    def test_02_subgraph_reassignment_and_parameter_delete(self):
        parent_graph_name = self._name("PCG_SubgraphParent")
        subgraph_a_name = self._name("PCG_SubgraphA")
        subgraph_b_name = self._name("PCG_SubgraphB")
        destination_path = "/Game/CopilotTests/PCG"
        parent_graph_path = f"{destination_path}/{parent_graph_name}.{parent_graph_name}"
        subgraph_a_path = f"{destination_path}/{subgraph_a_name}.{subgraph_a_name}"
        subgraph_b_path = f"{destination_path}/{subgraph_b_name}.{subgraph_b_name}"

        subgraph_type = _find_subgraph_node_type()

        for graph_name in (parent_graph_name, subgraph_a_name, subgraph_b_name):
            created_graph = _ok(send_command("create_pcg_graph_asset", {
                "pcg_graph_name": graph_name,
                "destination_path": destination_path,
            }), f"create_pcg_graph_asset {graph_name}")
            assert created_graph.get("asset_path"), created_graph

        added_subgraph_node = _ok(send_command("add_pcg_graph_node", {
            "asset_path": parent_graph_path,
            "settings_class": subgraph_type["class_path"],
            "node_title": "Copilot Subgraph Carrier",
            "position_x": 360,
            "position_y": 200,
            "subgraph_asset_path": subgraph_a_path,
        }), "add_pcg_graph_node subgraph")
        assert _active_subgraph_path(added_subgraph_node) == subgraph_a_path, added_subgraph_node

        reassigned_subgraph = _ok(send_command("set_pcg_subgraph_node_asset", {
            "asset_path": parent_graph_path,
            "node_path": added_subgraph_node["node_path"],
            "subgraph_asset_path": subgraph_b_path,
        }), "set_pcg_subgraph_node_asset assign")
        assert _active_subgraph_path(reassigned_subgraph) == subgraph_b_path, reassigned_subgraph

        cleared_subgraph = _ok(send_command("set_pcg_subgraph_node_asset", {
            "asset_path": parent_graph_path,
            "node_path": added_subgraph_node["node_path"],
            "clear_subgraph": True,
        }), "set_pcg_subgraph_node_asset clear")
        assert _active_subgraph_path(cleared_subgraph) == "", cleared_subgraph

        created_parameter = _ok(send_command("create_pcg_graph_parameter", {
            "asset_path": parent_graph_path,
            "parameter_name": "DeleteMe",
            "parameter_type": "Int32",
            "default_value": 5,
        }), "create_pcg_graph_parameter for delete")
        assert created_parameter.get("created") is True, created_parameter

        graph_with_parameter = _ok(send_command("read_pcg_graph_content", {
            "asset_path": parent_graph_path,
        }), "read_pcg_graph_content before delete parameter")
        assert _parameter_by_name(graph_with_parameter, "DeleteMe"), graph_with_parameter

        deleted_parameter = _ok(send_command("delete_pcg_graph_parameter", {
            "asset_path": parent_graph_path,
            "parameter_name": "DeleteMe",
        }), "delete_pcg_graph_parameter")
        assert deleted_parameter.get("deleted") is True, deleted_parameter

        graph_without_parameter = _ok(send_command("read_pcg_graph_content", {
            "asset_path": parent_graph_path,
        }), "read_pcg_graph_content after delete parameter")
        assert _parameter_by_name(graph_without_parameter, "DeleteMe") is None, graph_without_parameter