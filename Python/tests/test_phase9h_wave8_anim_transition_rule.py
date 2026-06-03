"""Live-editor smoke tests for Phase 9h Wave 8 transition rule helpers."""

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


class TestPhase9hWave8AnimTransitionRule:
    def test_01_set_anim_transition_rules(self):
        skeleton_path = _find_first_asset("Skeleton")
        if not skeleton_path:
            pytest.skip("No Skeleton assets available for AnimBlueprint transition-rule test")

        suffix = time.time_ns() % 1_000_000_000
        anim_blueprint_name = f"ABP_Phase9H_Wave8_{suffix}"
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

        for state_name in ["Idle", "Walk", "Run", "Sprint", "Finish"]:
            _ok(send_command("create_anim_state", {
                "asset_path": asset_path,
                "state_machine_name": state_machine_name,
                "state_name": state_name,
            }), f"create_anim_state_{state_name}")

        for source_state_name, target_state_name in [
            ("Idle", "Walk"),
            ("Walk", "Run"),
            ("Run", "Sprint"),
            ("Sprint", "Finish"),
        ]:
            _ok(send_command("create_anim_transition", {
                "asset_path": asset_path,
                "state_machine_name": state_machine_name,
                "source_state_name": source_state_name,
                "target_state_name": target_state_name,
            }), f"create_anim_transition_{source_state_name}_{target_state_name}")

        _ok(send_command("create_variable", {
            "blueprint_name": anim_blueprint_name,
            "variable_name": "ShouldMove",
            "variable_type": "bool",
            "default_value": False,
        }), "create_bool_variable")
        _ok(send_command("create_variable", {
            "blueprint_name": anim_blueprint_name,
            "variable_name": "SpeedTier",
            "variable_type": "int",
            "default_value": 0,
        }), "create_int_variable")
        _ok(send_command("create_variable", {
            "blueprint_name": anim_blueprint_name,
            "variable_name": "BlendMode",
            "variable_type": "/Script/Engine.EMontagePlayReturnType",
        }), "create_enum_variable")

        always_true_rule = _ok(send_command("set_anim_transition_rule", {
            "asset_path": asset_path,
            "state_machine_name": state_machine_name,
            "source_state_name": "Idle",
            "target_state_name": "Walk",
            "rule_type": "always_true",
        }), "set_anim_transition_rule_always_true")
        assert always_true_rule.get("rule_type") == "always_true", always_true_rule
        assert always_true_rule.get("transition", {}).get("rule_type") == "always_true", always_true_rule

        bool_rule = _ok(send_command("set_anim_transition_rule", {
            "asset_path": asset_path,
            "state_machine_name": state_machine_name,
            "source_state_name": "Walk",
            "target_state_name": "Run",
            "rule_type": "bool_variable",
            "variable_name": "ShouldMove",
        }), "set_anim_transition_rule_bool_variable")
        assert bool_rule.get("rule_type") == "bool_variable", bool_rule
        assert bool_rule.get("transition", {}).get("rule_type") == "bool_variable", bool_rule
        assert bool_rule.get("transition", {}).get("rule_variable_name") == "ShouldMove", bool_rule

        int_rule = _ok(send_command("set_anim_transition_rule", {
            "asset_path": asset_path,
            "state_machine_name": state_machine_name,
            "source_state_name": "Run",
            "target_state_name": "Sprint",
            "rule_type": "int_equals",
            "variable_name": "SpeedTier",
            "expected_value": "2",
        }), "set_anim_transition_rule_int_equals")
        assert int_rule.get("rule_type") == "int_equals", int_rule
        assert int_rule.get("transition", {}).get("rule_type") == "int_equals", int_rule
        assert int_rule.get("transition", {}).get("rule_variable_name") == "SpeedTier", int_rule
        assert int_rule.get("transition", {}).get("rule_expected_value") == "2", int_rule

        enum_rule = _ok(send_command("set_anim_transition_rule", {
            "asset_path": asset_path,
            "state_machine_name": state_machine_name,
            "source_state_name": "Sprint",
            "target_state_name": "Finish",
            "rule_type": "enum_equals",
            "variable_name": "BlendMode",
            "expected_value": "MontageLength",
        }), "set_anim_transition_rule_enum_equals")
        assert enum_rule.get("rule_type") == "enum_equals", enum_rule
        assert enum_rule.get("transition", {}).get("rule_type") == "enum_equals", enum_rule
        assert enum_rule.get("transition", {}).get("rule_variable_name") == "BlendMode", enum_rule
        assert enum_rule.get("transition", {}).get("rule_property_kind") == "enum", enum_rule
        assert enum_rule.get("transition", {}).get("rule_enum_path") == "/Script/Engine.EMontagePlayReturnType", enum_rule