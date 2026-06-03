"""Live-editor smoke tests for Phase 9h Wave 7 transition creation and deletion."""

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


class TestPhase9hWave7AnimTransition:
    def test_01_create_and_delete_anim_transition(self):
        skeleton_path = _find_first_asset("Skeleton")
        if not skeleton_path:
            pytest.skip("No Skeleton assets available for AnimBlueprint transition test")

        suffix = time.time_ns() % 1_000_000_000
        anim_blueprint_name = f"ABP_Phase9H_Wave7_{suffix}"
        destination_path = "/Game/CopilotTests/AnimBlueprints"
        asset_path = f"{destination_path}/{anim_blueprint_name}.{anim_blueprint_name}"

        created = _ok(send_command("create_anim_blueprint_asset", {
            "anim_blueprint_name": anim_blueprint_name,
            "skeleton_path": skeleton_path,
            "destination_path": destination_path,
        }), "create_anim_blueprint_asset")
        assert created.get("asset_path") == asset_path, created

        state_machine_name = "Traversal"
        _ok(send_command("create_anim_state_machine", {
            "asset_path": asset_path,
            "state_machine_name": state_machine_name,
        }), "create_anim_state_machine")

        _ok(send_command("create_anim_state", {
            "asset_path": asset_path,
            "state_machine_name": state_machine_name,
            "state_name": "Idle",
        }), "create_anim_state_idle")
        _ok(send_command("create_anim_state", {
            "asset_path": asset_path,
            "state_machine_name": state_machine_name,
            "state_name": "Walk",
        }), "create_anim_state_walk")

        transition = _ok(send_command("create_anim_transition", {
            "asset_path": asset_path,
            "state_machine_name": state_machine_name,
            "source_state_name": "Idle",
            "target_state_name": "Walk",
        }), "create_anim_transition")

        assert transition.get("success") is True, transition
        assert transition.get("created") is True, transition
        assert transition.get("transition_count") == 1, transition
        assert transition.get("transition", {}).get("source_state_name") == "Idle", transition
        assert transition.get("transition", {}).get("target_state_name") == "Walk", transition
        assert transition.get("transition", {}).get("rule_type") in ("always_false", "custom_graph"), transition

        deleted = _ok(send_command("delete_anim_transition", {
            "asset_path": asset_path,
            "state_machine_name": state_machine_name,
            "source_state_name": "Idle",
            "target_state_name": "Walk",
        }), "delete_anim_transition")

        assert deleted.get("success") is True, deleted
        assert deleted.get("deleted") is True, deleted
        assert deleted.get("deleted_transition_count") == 1, deleted
        assert deleted.get("transition_count") == 0, deleted
        assert deleted.get("transitions") == [], deleted