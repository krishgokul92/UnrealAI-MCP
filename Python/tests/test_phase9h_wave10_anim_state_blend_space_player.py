"""Live-editor smoke tests for Phase 9h Wave 10 state blend-space binding."""

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


class TestPhase9hWave10AnimStateBlendSpacePlayer:
    def test_01_set_anim_state_blend_space_player(self):
        skeleton_path = _find_first_asset("Skeleton")
        if not skeleton_path:
            pytest.skip("No Skeleton assets available for AnimBlueprint blend-space test")

        blend_space_path, expected_binding_type, expected_node_class = _find_first_blend_space_asset()
        if not blend_space_path or not expected_binding_type or not expected_node_class:
            pytest.skip("No BlendSpace-family assets available for state blend-space test")

        sequence_path = _find_first_asset("AnimSequence")

        suffix = time.time_ns() % 1_000_000_000
        anim_blueprint_name = f"ABP_Phase9H_Wave10_{suffix}"
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

        if sequence_path:
            seeded = _ok(send_command("set_anim_state_sequence_player", {
                "asset_path": asset_path,
                "state_machine_name": state_machine_name,
                "state_name": state_name,
                "sequence_path": sequence_path,
            }), "seed_anim_state_sequence_player")
            assert seeded.get("binding_type") == "sequence_player", seeded

        rebound = _ok(send_command("set_anim_state_blend_space_player", {
            "asset_path": asset_path,
            "state_machine_name": state_machine_name,
            "state_name": state_name,
            "blend_space_path": blend_space_path,
        }), "set_anim_state_blend_space_player")

        state = rebound.get("state", {})
        assert rebound.get("success") is True, rebound
        assert rebound.get("updated") is True, rebound
        assert rebound.get("binding_type") == expected_binding_type, rebound
        assert rebound.get("animation_asset_path") == blend_space_path, rebound
        assert state.get("name") == state_name, rebound
        assert state.get("asset_player_binding_type") == expected_binding_type, rebound
        assert state.get("animation_asset_path") == blend_space_path, rebound
        assert state.get("asset_player_node_class") == expected_node_class, rebound
        assert state.get("asset_player_count") == 1, rebound
        assert state.get("asset_player_is_supported_pattern") is True, rebound
        assert state.get("asset_player_is_connected") is True, rebound
        assert state.get("bound_graph_node_count") == 2, rebound