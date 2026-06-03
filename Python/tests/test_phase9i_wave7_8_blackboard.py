"""Live-editor smoke tests for Phase 9i batch 4 Blackboard inspection and key lifecycle helpers."""

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


def _key_by_name(readback: Dict[str, Any], name: str) -> Dict[str, Any]:
    for key in readback.get("keys", []):
        if key.get("name") == name:
            return key
    pytest.fail(f"Missing Blackboard key '{name}' in readback: {json.dumps(readback)[:1200]}")


def _user_authored_keys(readback: Dict[str, Any]) -> list[Dict[str, Any]]:
    return [key for key in readback.get("keys", []) if key.get("name") != "SelfActor"]


def _user_authored_local_keys(readback: Dict[str, Any]) -> list[Dict[str, Any]]:
    return [key for key in readback.get("local_keys", []) if key.get("name") != "SelfActor"]


def _user_authored_inherited_keys(readback: Dict[str, Any]) -> list[Dict[str, Any]]:
    return [key for key in readback.get("inherited_keys", []) if key.get("name") != "SelfActor"]


class TestPhase9iBatch4Blackboard:
    def test_01_create_and_mutate_blackboard_keys(self):
        suffix = int(time.time()) % 1000000
        blackboard_name = f"BB_Phase9I_Batch4_{suffix}"
        destination_path = "/Game/CopilotTests/Blackboards"
        asset_path = f"{destination_path}/{blackboard_name}.{blackboard_name}"

        created = _ok(send_command("create_blackboard_asset", {
            "blackboard_name": blackboard_name,
            "destination_path": destination_path,
        }), "create_blackboard_asset")

        assert created.get("created") is True, created
        assert created.get("asset_path") == asset_path, created

        seeded = _ok(send_command("update_blackboard_keys", {
            "asset_path": asset_path,
            "operations": [
                {
                    "op": "add",
                    "name": "BoolKey",
                    "key_type": "bool",
                    "default_value": True,
                    "instance_synced": True,
                    "description": "Boolean gate",
                },
                {
                    "op": "add",
                    "name": "FloatKey",
                    "key_type": "float",
                    "default_value": 1.25,
                },
                {
                    "op": "add",
                    "name": "NameKey",
                    "key_type": "name",
                    "default_value": "Idle",
                },
                {
                    "op": "add",
                    "name": "StringKey",
                    "key_type": "string",
                    "default_value": "Patrol",
                },
                {
                    "op": "add",
                    "name": "VectorKey",
                    "key_type": "vector",
                    "default_value": [1.0, 2.0, 3.0],
                },
                {
                    "op": "add",
                    "name": "ObjectKey",
                    "key_type": "object",
                    "base_class_path": "/Script/CoreUObject.Object",
                    "default_value_path": asset_path,
                },
                {
                    "op": "add",
                    "name": "ClassKey",
                    "key_type": "class",
                    "base_class_path": "/Script/Engine.Actor",
                    "default_value_path": "/Script/Engine.Actor",
                },
            ],
        }), "update_blackboard_keys_seed")

        assert seeded.get("updated") is True, seeded
        assert seeded.get("added_key_count") == 7, seeded
        assert len(_user_authored_keys(seeded)) == 7, seeded
        assert _key_by_name(seeded, "SelfActor").get("name") == "SelfActor", seeded

        bool_key = _key_by_name(seeded, "BoolKey")
        float_key = _key_by_name(seeded, "FloatKey")
        vector_key = _key_by_name(seeded, "VectorKey")
        object_key = _key_by_name(seeded, "ObjectKey")
        class_key = _key_by_name(seeded, "ClassKey")

        assert bool_key.get("value_kind") == "bool", bool_key
        assert bool_key.get("default_value") is True, bool_key
        assert bool_key.get("instance_synced") is True, bool_key
        assert float_key.get("value_kind") == "float", float_key
        assert float_key.get("default_value") == pytest.approx(1.25), float_key
        assert vector_key.get("value_kind") == "vector", vector_key
        assert vector_key.get("default_value") == pytest.approx([1.0, 2.0, 3.0]), vector_key
        assert vector_key.get("uses_default_value") is True, vector_key
        assert object_key.get("value_kind") == "object", object_key
        assert object_key.get("base_class_path") == "/Script/CoreUObject.Object", object_key
        assert object_key.get("default_value_path") == asset_path, object_key
        assert class_key.get("value_kind") == "class", class_key
        assert class_key.get("base_class_path") == "/Script/Engine.Actor", class_key
        assert class_key.get("default_value_path") == "/Script/Engine.Actor", class_key

        mutated = _ok(send_command("update_blackboard_keys", {
            "asset_path": asset_path,
            "operations": [
                {
                    "op": "rename",
                    "name": "NameKey",
                    "new_name": "StateName",
                },
                {
                    "op": "update",
                    "name": "FloatKey",
                    "default_value": 2.5,
                    "instance_synced": True,
                    "description": "Updated float value",
                },
                {
                    "op": "delete",
                    "name": "StringKey",
                },
                {
                    "op": "upsert",
                    "name": "IntKey",
                    "key_type": "int",
                    "default_value": 42,
                    "category": "Numbers",
                },
            ],
        }), "update_blackboard_keys_mutate")

        assert mutated.get("updated") is True, mutated
        assert mutated.get("added_key_count") == 1, mutated
        assert mutated.get("updated_key_count") == 1, mutated
        assert mutated.get("renamed_key_count") == 1, mutated
        assert mutated.get("deleted_key_count") == 1, mutated
        assert len(_user_authored_keys(mutated)) == 7, mutated

        state_name_key = _key_by_name(mutated, "StateName")
        updated_float_key = _key_by_name(mutated, "FloatKey")
        int_key = _key_by_name(mutated, "IntKey")

        assert state_name_key.get("value_kind") == "name", state_name_key
        assert state_name_key.get("default_value") == "Idle", state_name_key
        assert updated_float_key.get("default_value") == pytest.approx(2.5), updated_float_key
        assert updated_float_key.get("instance_synced") is True, updated_float_key
        assert updated_float_key.get("description") == "Updated float value", updated_float_key
        assert int_key.get("value_kind") == "int", int_key
        assert int_key.get("default_value") == 42, int_key
        assert int_key.get("category") == "Numbers", int_key
        assert all(key.get("name") != "StringKey" for key in mutated.get("keys", [])), mutated

    def test_02_child_blackboard_inherits_parent_chain_and_local_keys(self):
        suffix = int(time.time()) % 1000000
        destination_path = "/Game/CopilotTests/Blackboards"
        parent_name = f"BB_Phase9I_Batch4_Parent_{suffix}"
        child_name = f"BB_Phase9I_Batch4_Child_{suffix}"
        parent_asset_path = f"{destination_path}/{parent_name}.{parent_name}"
        child_asset_path = f"{destination_path}/{child_name}.{child_name}"

        parent_created = _ok(send_command("create_blackboard_asset", {
            "blackboard_name": parent_name,
            "destination_path": destination_path,
        }), "create_parent_blackboard")
        assert parent_created.get("asset_path") == parent_asset_path, parent_created

        parent_seeded = _ok(send_command("update_blackboard_keys", {
            "asset_path": parent_asset_path,
            "operations": [
                {
                    "op": "add",
                    "name": "ParentFloat",
                    "key_type": "float",
                    "default_value": 3.5,
                },
                {
                    "op": "add",
                    "name": "ParentState",
                    "key_type": "string",
                    "default_value": "Searching",
                },
            ],
        }), "seed_parent_blackboard")
        assert parent_seeded.get("added_key_count") == 2, parent_seeded

        child_created = _ok(send_command("create_blackboard_asset", {
            "blackboard_name": child_name,
            "destination_path": destination_path,
            "parent_blackboard_path": parent_asset_path,
        }), "create_child_blackboard")
        assert child_created.get("asset_path") == child_asset_path, child_created
        assert child_created.get("has_parent") is True, child_created
        assert child_created.get("parent_blackboard_path") == parent_asset_path, child_created

        child_seeded = _ok(send_command("update_blackboard_keys", {
            "asset_path": child_asset_path,
            "operations": [
                {
                    "op": "add",
                    "name": "ChildFlag",
                    "key_type": "bool",
                    "default_value": False,
                },
            ],
        }), "seed_child_blackboard")
        assert child_seeded.get("added_key_count") == 1, child_seeded

        readback = _ok(send_command("read_blackboard_content", {
            "asset_path": child_asset_path,
        }), "read_child_blackboard")

        assert readback.get("has_parent") is True, readback
        assert readback.get("parent_blackboard_path") == parent_asset_path, readback
        assert readback.get("parent_chain") == [
            {
                "name": parent_name,
                "asset_path": parent_asset_path,
                "class_name": "BlackboardData",
            },
        ], readback
        assert len(_user_authored_keys(readback)) == 3, readback
        assert len(_user_authored_local_keys(readback)) == 1, readback
        assert len(_user_authored_inherited_keys(readback)) == 2, readback
        assert _key_by_name(readback, "SelfActor").get("name") == "SelfActor", readback

        parent_float = _key_by_name(readback, "ParentFloat")
        parent_state = _key_by_name(readback, "ParentState")
        child_flag = _key_by_name(readback, "ChildFlag")

        assert parent_float.get("is_inherited") is True, parent_float
        assert parent_float.get("source_blackboard_path") == parent_asset_path, parent_float
        assert parent_float.get("default_value") == pytest.approx(3.5), parent_float
        assert parent_state.get("is_inherited") is True, parent_state
        assert parent_state.get("default_value") == "Searching", parent_state
        assert child_flag.get("is_inherited") is False, child_flag
        assert child_flag.get("source_blackboard_path") == child_asset_path, child_flag