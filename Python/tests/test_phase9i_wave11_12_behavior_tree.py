"""Live-editor smoke tests for Phase 9i batch 6 Behavior Tree subtree, property, and validation helpers."""

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


def _issue_codes(readback: Dict[str, Any]) -> set[str]:
    return {
        issue.get("code")
        for issue in readback.get("issues", [])
        if isinstance(issue, dict) and isinstance(issue.get("code"), str)
    }


class TestPhase9iBatch6BehaviorTree:
    def test_01_author_properties_and_validate_behavior_tree(self):
        suffix = int(time.time()) % 1000000
        blackboard_name = f"BB_Phase9I_Batch6_{suffix}"
        behavior_tree_name = f"BT_Phase9I_Batch6_{suffix}"
        blackboard_destination = "/Game/CopilotTests/Blackboards"
        behavior_tree_destination = "/Game/CopilotTests/BehaviorTrees"
        blackboard_asset_path = f"{blackboard_destination}/{blackboard_name}.{blackboard_name}"
        behavior_tree_asset_path = f"{behavior_tree_destination}/{behavior_tree_name}.{behavior_tree_name}"

        created_blackboard = _ok(send_command("create_blackboard_asset", {
            "blackboard_name": blackboard_name,
            "destination_path": blackboard_destination,
        }), "create_blackboard_asset")
        assert created_blackboard.get("asset_path") == blackboard_asset_path, created_blackboard

        seeded_blackboard = _ok(send_command("update_blackboard_keys", {
            "asset_path": blackboard_asset_path,
            "operations": [
                {
                    "op": "add",
                    "name": "TargetActor",
                    "key_type": "object",
                    "base_class_path": "/Script/Engine.Actor",
                },
            ],
        }), "seed_blackboard")
        assert seeded_blackboard.get("added_key_count") == 1, seeded_blackboard

        created_tree = _ok(send_command("create_behavior_tree_asset", {
            "behavior_tree_name": behavior_tree_name,
            "destination_path": behavior_tree_destination,
            "blackboard_asset_path": blackboard_asset_path,
        }), "create_behavior_tree_asset")
        assert created_tree.get("asset_path") == behavior_tree_asset_path, created_tree

        authored = _ok(send_command("update_behavior_tree_subtree", {
            "asset_path": behavior_tree_asset_path,
            "operations": [
                {
                    "op": "add",
                    "parent_path": "root",
                    "node_kind": "composite",
                    "node_type": "sequence",
                },
                {
                    "op": "add",
                    "parent_path": "root/0",
                    "node_kind": "task",
                    "node_type": "rotate_to_face_bb_entry",
                },
                {
                    "op": "add",
                    "parent_path": "root/0",
                    "node_kind": "service",
                    "node_type": "default_focus",
                },
                {
                    "op": "add",
                    "parent_path": "root/0/0",
                    "node_kind": "decorator",
                    "node_type": "blackboard",
                },
            ],
        }), "update_behavior_tree_subtree_author")

        assert authored.get("updated") is True, authored
        assert authored.get("has_graph") is True, authored
        assert authored.get("applied_operation_count") == 4, authored
        assert authored.get("node_count") == 3, authored
        assert authored.get("composite_count") == 2, authored
        assert authored.get("task_count") == 1, authored
        assert authored.get("decorator_count") == 1, authored
        assert authored.get("service_count") == 1, authored

        _ok(send_command("set_behavior_tree_node_properties", {
            "asset_path": behavior_tree_asset_path,
            "topology_path": "root/0/services/0",
            "blackboard_key_name": "TargetActor",
            "enabled_state": "disabled",
        }), "set_service_properties")

        _ok(send_command("set_behavior_tree_node_properties", {
            "asset_path": behavior_tree_asset_path,
            "topology_path": "root/0/0",
            "blackboard_key_name": "TargetActor",
            "enabled_state": "development_only",
        }), "set_task_properties")

        _ok(send_command("set_behavior_tree_node_properties", {
            "asset_path": behavior_tree_asset_path,
            "topology_path": "root/0/0/decorators/0",
            "blackboard_key_name": "TargetActor",
            "flow_abort_mode": "self",
        }), "set_decorator_properties")

        readback = _ok(send_command("read_behavior_tree_content", {
            "asset_path": behavior_tree_asset_path,
        }), "read_behavior_tree_content")

        root_node = readback.get("root_node")
        assert isinstance(root_node, dict), readback
        first_child = root_node.get("children", [])[0]
        assert isinstance(first_child, dict), root_node
        sequence_node = first_child.get("node")
        assert isinstance(sequence_node, dict), first_child
        task_child = sequence_node.get("children", [])[0]
        assert isinstance(task_child, dict), sequence_node
        task_node = task_child.get("node")
        assert isinstance(task_node, dict), task_child
        service_node = sequence_node.get("services", [])[0]
        decorator_node = task_child.get("decorators", [])[0]

        assert sequence_node.get("topology_path") == "root/0", sequence_node
        assert service_node.get("topology_path") == "root/0/services/0", service_node
        assert service_node.get("selected_blackboard_key_name") == "TargetActor", service_node
        assert service_node.get("enabled_state") == "disabled", service_node
        assert task_node.get("topology_path") == "root/0/0", task_node
        assert task_node.get("selected_blackboard_key_name") == "TargetActor", task_node
        assert task_node.get("enabled_state") == "development_only", task_node
        assert decorator_node.get("topology_path") == "root/0/0/decorators/0", decorator_node
        assert decorator_node.get("selected_blackboard_key_name") == "TargetActor", decorator_node
        assert decorator_node.get("flow_abort_mode") == "self", decorator_node

        validated = _ok(send_command("validate_behavior_tree", {
            "asset_path": behavior_tree_asset_path,
        }), "validate_behavior_tree")

        codes = _issue_codes(validated)
        assert validated.get("validated") is True, validated
        assert validated.get("error_count") == 0, validated
        assert validated.get("warning_count") >= 2, validated
        assert "disabled_node" in codes, validated
        assert "development_only_node" in codes, validated

    def test_02_remove_behavior_tree_subtree_nodes(self):
        suffix = int(time.time()) % 1000000
        behavior_tree_name = f"BT_Phase9I_Batch6_Remove_{suffix}"
        destination_path = "/Game/CopilotTests/BehaviorTrees"
        asset_path = f"{destination_path}/{behavior_tree_name}.{behavior_tree_name}"

        created = _ok(send_command("create_behavior_tree_asset", {
            "behavior_tree_name": behavior_tree_name,
            "destination_path": destination_path,
        }), "create_behavior_tree_asset_remove")
        assert created.get("asset_path") == asset_path, created

        seeded = _ok(send_command("update_behavior_tree_subtree", {
            "asset_path": asset_path,
            "operations": [
                {
                    "op": "add",
                    "parent_path": "root",
                    "node_kind": "composite",
                    "node_type": "sequence",
                },
                {
                    "op": "add",
                    "parent_path": "root/0",
                    "node_kind": "task",
                    "node_type": "wait",
                },
                {
                    "op": "add",
                    "parent_path": "root/0",
                    "node_kind": "service",
                    "node_type": "default_focus",
                },
                {
                    "op": "add",
                    "parent_path": "root/0/0",
                    "node_kind": "decorator",
                    "node_type": "blackboard",
                },
            ],
        }), "seed_behavior_tree_subtree")
        assert seeded.get("node_count") == 3, seeded

        removed = _ok(send_command("update_behavior_tree_subtree", {
            "asset_path": asset_path,
            "operations": [
                {
                    "op": "remove",
                    "target_path": "root/0/0/decorators/0",
                },
                {
                    "op": "remove",
                    "target_path": "root/0/services/0",
                },
                {
                    "op": "remove",
                    "target_path": "root/0/0",
                },
                {
                    "op": "remove",
                    "target_path": "root/0",
                },
            ],
        }), "remove_behavior_tree_subtree")

        assert removed.get("updated") is True, removed
        assert removed.get("applied_operation_count") == 4, removed
        assert removed.get("node_count") == 1, removed
        assert removed.get("composite_count") == 1, removed
        assert removed.get("task_count") == 0, removed
        assert removed.get("decorator_count") == 0, removed
        assert removed.get("service_count") == 0, removed

        root_node = removed.get("root_node")
        assert isinstance(root_node, dict), removed
        assert root_node.get("child_count") == 0, root_node
        assert root_node.get("children") == [], root_node