"""Live-editor smoke tests for Phase 9h Wave 5 state creation."""

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


class TestPhase9hWave5CreateAnimState:
    def test_01_create_anim_state(self):
        skeleton_path = _find_first_asset("Skeleton")
        if not skeleton_path:
            pytest.skip("No Skeleton assets available for AnimBlueprint state creation test")

        suffix = time.time_ns() % 1_000_000_000
        anim_blueprint_name = f"ABP_Phase9H_Wave5_{suffix}"
        destination_path = "/Game/CopilotTests/AnimBlueprints"
        asset_path = f"{destination_path}/{anim_blueprint_name}.{anim_blueprint_name}"

        created = _ok(send_command("create_anim_blueprint_asset", {
            "anim_blueprint_name": anim_blueprint_name,
            "skeleton_path": skeleton_path,
            "destination_path": destination_path,
        }), "create_anim_blueprint_asset")

        assert created.get("asset_path") == asset_path, created

        state_machine_name = "Traversal"
        created_state_machine = _ok(send_command("create_anim_state_machine", {
            "asset_path": asset_path,
            "state_machine_name": state_machine_name,
        }), "create_anim_state_machine")

        assert created_state_machine.get("state_machine_name") == state_machine_name, created_state_machine

        state_name = "Idle"
        state = _ok(send_command("create_anim_state", {
            "asset_path": asset_path,
            "state_machine_name": state_machine_name,
            "state_name": state_name,
            "pos_x": 640,
            "pos_y": 128,
        }), "create_anim_state")

        assert state.get("success") is True, state
        assert state.get("created") is True, state
        assert state.get("asset_path") == asset_path, state
        assert state.get("state_machine_name") == state_machine_name, state
        assert state.get("state_name") == state_name, state
        assert state.get("entry_connected") is True, state
        assert state.get("entry_state_name") == state_name, state
        assert state.get("state_count") == 1, state
        assert state.get("transition_count") == 0, state
        assert state.get("node_pos_x") == 640, state
        assert state.get("node_pos_y") == 128, state
        assert isinstance(state.get("state"), dict), state
        assert state.get("state", {}).get("name") == state_name, state
        assert state.get("state", {}).get("bound_graph_name") == state_name, state
        assert state.get("states", [])[0].get("name") == state_name, state