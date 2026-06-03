"""Live-editor smoke tests for Phase 9h Wave 3 named state-machine inspection."""

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


def _create_temp_anim_blueprint(skeleton_path: str, suffix: int) -> Dict[str, str]:
    anim_blueprint_name = f"ABP_Phase9H_Wave3_{suffix}"
    destination_path = "/Game/CopilotTests/AnimBlueprints"
    asset_path = f"{destination_path}/{anim_blueprint_name}.{anim_blueprint_name}"

    created = _ok(send_command("create_anim_blueprint_asset", {
        "anim_blueprint_name": anim_blueprint_name,
        "skeleton_path": skeleton_path,
        "destination_path": destination_path,
    }), "create_anim_blueprint_asset")

    assert created.get("asset_path") == asset_path, created
    return {
        "anim_blueprint_name": anim_blueprint_name,
        "asset_path": asset_path,
    }


class TestPhase9hWave3AnimStateMachine:
    def test_01_read_anim_state_machine(self):
        skeleton_path = _find_first_asset("Skeleton")
        if not skeleton_path:
            pytest.skip("No Skeleton assets available for AnimBlueprint state-machine inspection test")

        temp_asset = _create_temp_anim_blueprint(skeleton_path, time.time_ns() % 1_000_000_000)
        state_machine_name = "Locomotion"

        created_state_machine = _ok(send_command("create_anim_state_machine", {
            "asset_path": temp_asset["asset_path"],
            "state_machine_name": state_machine_name,
        }), "create_anim_state_machine")

        assert created_state_machine.get("state_machine_name") == state_machine_name, created_state_machine

        state_machine = _ok(send_command("read_anim_state_machine", {
            "asset_path": temp_asset["asset_path"],
            "state_machine_name": state_machine_name,
        }), "read_anim_state_machine")

        assert state_machine.get("asset_path") == temp_asset["asset_path"], state_machine
        assert state_machine.get("anim_blueprint_name") == temp_asset["anim_blueprint_name"], state_machine
        assert state_machine.get("state_machine_name") == state_machine_name, state_machine
        assert state_machine.get("graph_name") in (None, "AnimGraph") or state_machine.get("owner_graph_name") == "AnimGraph", state_machine
        assert state_machine.get("owner_graph_name") == "AnimGraph", state_machine
        assert state_machine.get("state_count") == 0, state_machine
        assert state_machine.get("transition_count") == 0, state_machine
        assert state_machine.get("states") == [], state_machine
        assert state_machine.get("transitions") == [], state_machine
        assert isinstance(state_machine.get("state_machine"), dict), state_machine