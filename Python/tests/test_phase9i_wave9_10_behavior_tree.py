"""Live-editor smoke tests for Phase 9i batch 5 Behavior Tree inspection and creation helpers."""

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


SKIP_REASON = "UE bridge not reachable on 127.0.0.1:55557 (open the editor first)"
STARTUP_SETTLE_SECONDS = 10


@pytest.fixture(scope="module", autouse=True)
def require_bridge() -> None:
    for _ in range(30):
        if ping():
            time.sleep(STARTUP_SETTLE_SECONDS)
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
        pytest.fail(f"{context}: bridge error: {json.dumps(resp)[:800]}")
    return _unwrap(resp)


class TestPhase9iBatch5BehaviorTree:
    def test_01_create_behavior_tree_with_blackboard_link_and_root_scaffold(self):
        suffix = int(time.time()) % 1000000
        blackboard_name = f"BB_Phase9I_Batch5_{suffix}"
        behavior_tree_name = f"BT_Phase9I_Batch5_{suffix}"
        blackboard_destination = "/Game/CopilotTests/Blackboards"
        behavior_tree_destination = "/Game/CopilotTests/BehaviorTrees"
        blackboard_asset_path = f"{blackboard_destination}/{blackboard_name}.{blackboard_name}"
        behavior_tree_asset_path = f"{behavior_tree_destination}/{behavior_tree_name}.{behavior_tree_name}"

        blackboard = _ok(send_command("create_blackboard_asset", {
            "blackboard_name": blackboard_name,
            "destination_path": blackboard_destination,
        }), "create_blackboard_asset")
        assert blackboard.get("asset_path") == blackboard_asset_path, blackboard

        created = _ok(send_command("create_behavior_tree_asset", {
            "behavior_tree_name": behavior_tree_name,
            "destination_path": behavior_tree_destination,
            "blackboard_asset_path": blackboard_asset_path,
        }), "create_behavior_tree_asset")

        assert created.get("created") is True, created
        assert created.get("asset_path") == behavior_tree_asset_path, created
        assert created.get("has_blackboard") is True, created
        assert created.get("blackboard_path") == blackboard_asset_path, created
        assert created.get("has_root_node") is True, created
        assert created.get("root_node_type") == "selector", created
        assert created.get("root_node_class_name") == "BTComposite_Selector", created
        assert created.get("node_count") == 1, created
        assert created.get("composite_count") == 1, created
        assert created.get("task_count") == 0, created
        assert created.get("decorator_count") == 0, created
        assert created.get("service_count") == 0, created
        assert created.get("root_decorator_count") == 0, created
        assert created.get("max_depth") == 0, created

        root_node = created.get("root_node")
        assert isinstance(root_node, dict), created
        assert root_node.get("topology_path") == "root", root_node
        assert root_node.get("depth") == 0, root_node
        assert root_node.get("node_kind") == "composite", root_node
        assert root_node.get("node_type") == "selector", root_node
        assert root_node.get("child_count") == 0, root_node
        assert root_node.get("service_count") == 0, root_node
        assert root_node.get("children") == [], root_node
        assert root_node.get("services") == [], root_node

        nodes = created.get("nodes")
        assert isinstance(nodes, list) and len(nodes) == 1, created
        assert nodes[0].get("topology_path") == "root", nodes
        assert nodes[0].get("node_kind") == "composite", nodes
        assert nodes[0].get("node_type") == "selector", nodes

    def test_02_read_behavior_tree_content_for_unlinked_tree(self):
        suffix = int(time.time()) % 1000000
        behavior_tree_name = f"BT_Phase9I_Batch5_Unlinked_{suffix}"
        behavior_tree_destination = "/Game/CopilotTests/BehaviorTrees"
        behavior_tree_asset_path = f"{behavior_tree_destination}/{behavior_tree_name}.{behavior_tree_name}"

        created = _ok(send_command("create_behavior_tree_asset", {
            "behavior_tree_name": behavior_tree_name,
            "destination_path": behavior_tree_destination,
        }), "create_behavior_tree_asset_unlinked")
        assert created.get("asset_path") == behavior_tree_asset_path, created

        readback = _ok(send_command("read_behavior_tree_content", {
            "asset_path": behavior_tree_asset_path,
        }), "read_behavior_tree_content")

        assert readback.get("asset_path") == behavior_tree_asset_path, readback
        assert readback.get("has_blackboard") is False, readback
        assert readback.get("blackboard_path") == "", readback
        assert readback.get("has_root_node") is True, readback
        assert readback.get("root_node_type") == "selector", readback
        assert readback.get("node_count") == 1, readback
        assert readback.get("composite_count") == 1, readback
        assert readback.get("task_count") == 0, readback
        assert readback.get("decorator_count") == 0, readback
        assert readback.get("service_count") == 0, readback
        assert readback.get("root_decorators") == [], readback
        assert isinstance(readback.get("root_node"), dict), readback