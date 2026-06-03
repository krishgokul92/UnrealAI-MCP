"""Live-editor smoke tests for Phase 9h Wave 11 state asset-player parameter mutation."""

from __future__ import annotations

import json
import sys
import time
from pathlib import Path
from typing import Any, Dict, Optional, Tuple

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
        pytest.fail(f"{context}: bridge error: {json.dumps(resp)[:700]}")
    return _unwrap(resp)


def _find_first_asset(class_name: str) -> Optional[str]:
    payload = _ok(send_command("find_assets", {
        "class_name": class_name,
        "max_results": 20,
    }), f"find_assets({class_name})")

    assets = payload.get("assets", [])
    if not assets:
        return None
    return assets[0].get("path")


def _find_first_blend_space_asset() -> Tuple[Optional[str], Optional[str], Optional[str]]:
    for class_name, expected_binding_type, expected_node_class in [
        ("AimOffsetBlendSpace", "aim_offset_player", "AnimGraphNode_RotationOffsetBlendSpace"),
        ("AimOffsetBlendSpace1D", "aim_offset_player", "AnimGraphNode_RotationOffsetBlendSpace"),
        ("BlendSpace", "blend_space_player", "AnimGraphNode_BlendSpacePlayer"),
    ]:
        asset_path = _find_first_asset(class_name)
        if asset_path:
            return asset_path, expected_binding_type, expected_node_class
    return None, None, None


class TestPhase9hWave11AnimStateAssetPlayerParameters:
    def test_01_mutate_sequence_player_parameters(self):
        skeleton_path = _find_first_asset("Skeleton")
        if not skeleton_path:
            pytest.skip("No Skeleton assets available for AnimBlueprint sequence-parameter test")

        sequence_path = _find_first_asset("AnimSequence")
        if not sequence_path:
            pytest.skip("No AnimSequence assets available for sequence-parameter test")

        suffix = time.time_ns() % 1_000_000_000
        anim_blueprint_name = f"ABP_Phase9H_Wave11Seq_{suffix}"
        destination_path = "/Game/CopilotTests/AnimBlueprints"
        asset_path = f"{destination_path}/{anim_blueprint_name}.{anim_blueprint_name}"

        created = _ok(send_command("create_anim_blueprint_asset", {
            "anim_blueprint_name": anim_blueprint_name,
            "skeleton_path": skeleton_path,
            "destination_path": destination_path,
        }), "create_anim_blueprint_asset")
        assert created.get("asset_path") == asset_path, created

        state_machine_name = "Traversal"
        state_name = "Idle"
        _ok(send_command("create_anim_state_machine", {
            "asset_path": asset_path,
            "state_machine_name": state_machine_name,
        }), "create_anim_state_machine")
        _ok(send_command("create_anim_state", {
            "asset_path": asset_path,
            "state_machine_name": state_machine_name,
            "state_name": state_name,
        }), "create_anim_state")
        _ok(send_command("set_anim_state_sequence_player", {
            "asset_path": asset_path,
            "state_machine_name": state_machine_name,
            "state_name": state_name,
            "sequence_path": sequence_path,
        }), "set_anim_state_sequence_player")

        mutated = _ok(send_command("set_anim_state_asset_player_parameters", {
            "asset_path": asset_path,
            "state_machine_name": state_machine_name,
            "state_name": state_name,
            "loop": False,
            "play_rate": 1.75,
            "start_position": 0.25,
            "sync_group_name": "Locomotion",
            "sync_group_role": "CanBeLeader",
            "sync_group_method": "SyncGroup",
            "sync_group_override_position_when_joining_sync_group_as_leader": True,
        }), "set_anim_state_asset_player_parameters_sequence")

        state = mutated.get("state", {})
        summary = mutated.get("asset_player_summary", {})
        assert mutated.get("success") is True, mutated
        assert mutated.get("updated") is True, mutated
        assert mutated.get("binding_type") == "sequence_player", mutated
        assert mutated.get("loop") is False, mutated
        assert mutated.get("play_rate") == pytest.approx(1.75), mutated
        assert mutated.get("start_position") == pytest.approx(0.25), mutated
        assert mutated.get("sync_group_name") == "Locomotion", mutated
        assert mutated.get("sync_group_role") == "CanBeLeader", mutated
        assert mutated.get("sync_group_method") == "SyncGroup", mutated
        assert mutated.get("sync_group_override_position_when_joining_sync_group_as_leader") is True, mutated
        assert state.get("asset_player_loop") is False, mutated
        assert state.get("asset_player_play_rate") == pytest.approx(1.75), mutated
        assert state.get("asset_player_start_position") == pytest.approx(0.25), mutated
        assert state.get("asset_player_sync_group_name") == "Locomotion", mutated
        assert state.get("asset_player_sync_group_role") == "CanBeLeader", mutated
        assert state.get("asset_player_sync_group_method") == "SyncGroup", mutated
        assert state.get("asset_player_sync_group_override_position_when_joining_sync_group_as_leader") is True, mutated
        assert summary.get("play_rate") == pytest.approx(1.75), mutated
        assert summary.get("start_position") == pytest.approx(0.25), mutated

    def test_02_mutate_blend_space_player_parameters(self):
        skeleton_path = _find_first_asset("Skeleton")
        if not skeleton_path:
            pytest.skip("No Skeleton assets available for AnimBlueprint blend-space-parameter test")

        blend_space_path, expected_binding_type, expected_node_class = _find_first_blend_space_asset()
        if not blend_space_path or not expected_binding_type or not expected_node_class:
            pytest.skip("No BlendSpace-family assets available for blend-space-parameter test")

        suffix = time.time_ns() % 1_000_000_000
        anim_blueprint_name = f"ABP_Phase9H_Wave11Blend_{suffix}"
        destination_path = "/Game/CopilotTests/AnimBlueprints"
        asset_path = f"{destination_path}/{anim_blueprint_name}.{anim_blueprint_name}"

        created = _ok(send_command("create_anim_blueprint_asset", {
            "anim_blueprint_name": anim_blueprint_name,
            "skeleton_path": skeleton_path,
            "destination_path": destination_path,
        }), "create_anim_blueprint_asset")
        assert created.get("asset_path") == asset_path, created

        state_machine_name = "Traversal"
        state_name = "Locomotion"
        _ok(send_command("create_anim_state_machine", {
            "asset_path": asset_path,
            "state_machine_name": state_machine_name,
        }), "create_anim_state_machine")
        _ok(send_command("create_anim_state", {
            "asset_path": asset_path,
            "state_machine_name": state_machine_name,
            "state_name": state_name,
        }), "create_anim_state")
        _ok(send_command("set_anim_state_blend_space_player", {
            "asset_path": asset_path,
            "state_machine_name": state_machine_name,
            "state_name": state_name,
            "blend_space_path": blend_space_path,
        }), "set_anim_state_blend_space_player")

        mutated = _ok(send_command("set_anim_state_asset_player_parameters", {
            "asset_path": asset_path,
            "state_machine_name": state_machine_name,
            "state_name": state_name,
            "loop": False,
            "play_rate": 0.75,
            "start_position": 0.4,
            "blend_space_x": 0.5,
            "blend_space_y": -0.25,
            "sync_group_name": "AimOrMove",
            "sync_group_role": "CanBeLeader",
            "sync_group_method": "SyncGroup",
            "sync_group_override_position_when_joining_sync_group_as_leader": True,
        }), "set_anim_state_asset_player_parameters_blend_space")

        state = mutated.get("state", {})
        summary = mutated.get("asset_player_summary", {})
        assert mutated.get("success") is True, mutated
        assert mutated.get("updated") is True, mutated
        assert mutated.get("binding_type") == expected_binding_type, mutated
        assert mutated.get("asset_player_node_class") == expected_node_class, mutated
        assert mutated.get("loop") is False, mutated
        assert mutated.get("play_rate") == pytest.approx(0.75), mutated
        assert mutated.get("start_position") == pytest.approx(0.4), mutated
        assert mutated.get("blend_space_x") == pytest.approx(0.5), mutated
        assert mutated.get("blend_space_y") == pytest.approx(-0.25), mutated
        assert mutated.get("sync_group_name") == "AimOrMove", mutated
        assert mutated.get("sync_group_role") == "CanBeLeader", mutated
        assert mutated.get("sync_group_method") == "SyncGroup", mutated
        assert mutated.get("sync_group_override_position_when_joining_sync_group_as_leader") is True, mutated
        assert state.get("asset_player_loop") is False, mutated
        assert state.get("asset_player_play_rate") == pytest.approx(0.75), mutated
        assert state.get("asset_player_start_position") == pytest.approx(0.4), mutated
        assert state.get("asset_player_blend_space_x") == pytest.approx(0.5), mutated
        assert state.get("asset_player_blend_space_y") == pytest.approx(-0.25), mutated
        assert state.get("asset_player_sync_group_name") == "AimOrMove", mutated
        assert state.get("asset_player_sync_group_role") == "CanBeLeader", mutated
        assert state.get("asset_player_sync_group_method") == "SyncGroup", mutated
        assert state.get("asset_player_sync_group_override_position_when_joining_sync_group_as_leader") is True, mutated
        assert summary.get("play_rate") == pytest.approx(0.75), mutated
        assert summary.get("start_position") == pytest.approx(0.4), mutated
        assert summary.get("blend_space_x") == pytest.approx(0.5), mutated
        assert summary.get("blend_space_y") == pytest.approx(-0.25), mutated