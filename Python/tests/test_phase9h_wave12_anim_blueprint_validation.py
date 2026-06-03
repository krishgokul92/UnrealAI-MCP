"""Live-editor smoke tests for Phase 9h Wave 12 AnimBlueprint validation."""

from __future__ import annotations

import json
import sys
import time
from pathlib import Path
from typing import Any, Dict, List, Optional, Tuple

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


def _find_assets(class_name: str, max_results: int = 60) -> List[str]:
    payload = _ok(send_command("find_assets", {
        "class_name": class_name,
        "max_results": max_results,
    }), f"find_assets({class_name})")

    asset_paths: List[str] = []
    for asset in payload.get("assets", []):
        if isinstance(asset, dict) and isinstance(asset.get("path"), str) and asset["path"]:
            asset_paths.append(asset["path"])
    return asset_paths


def _issue_codes(result: Dict[str, Any]) -> set[str]:
    codes: set[str] = set()
    for issue in result.get("issues", []):
        if isinstance(issue, dict) and isinstance(issue.get("code"), str):
            codes.add(issue["code"])
    return codes


def _create_seed_anim_blueprint(
    skeleton_path: str,
    name_prefix: str,
    preview_skeletal_mesh_path: Optional[str] = None,
    seed_state_machine: bool = True,
) -> Tuple[str, str, str]:
    suffix = time.time_ns() % 1_000_000_000
    anim_blueprint_name = f"{name_prefix}_{suffix}"
    destination_path = "/Game/CopilotTests/AnimBlueprints"
    asset_path = f"{destination_path}/{anim_blueprint_name}.{anim_blueprint_name}"

    create_payload: Dict[str, Any] = {
        "anim_blueprint_name": anim_blueprint_name,
        "skeleton_path": skeleton_path,
        "destination_path": destination_path,
    }
    if preview_skeletal_mesh_path is not None:
        create_payload["preview_skeletal_mesh_path"] = preview_skeletal_mesh_path

    created = _ok(send_command("create_anim_blueprint_asset", create_payload), "create_anim_blueprint_asset")
    assert created.get("asset_path") == asset_path, created

    state_machine_name = "Traversal"
    state_name = "Idle"
    if not seed_state_machine:
        return asset_path, state_machine_name, state_name

    _ok(send_command("create_anim_state_machine", {
        "asset_path": asset_path,
        "state_machine_name": state_machine_name,
    }), "create_anim_state_machine")
    _ok(send_command("create_anim_state", {
        "asset_path": asset_path,
        "state_machine_name": state_machine_name,
        "state_name": state_name,
    }), "create_anim_state")

    return asset_path, state_machine_name, state_name


def _bind_sequence_and_validate(
    asset_path: str,
    state_machine_name: str,
    state_name: str,
    sequence_path: str,
) -> Dict[str, Any]:
    _ok(send_command("set_anim_state_sequence_player", {
        "asset_path": asset_path,
        "state_machine_name": state_machine_name,
        "state_name": state_name,
        "sequence_path": sequence_path,
    }), f"set_anim_state_sequence_player({sequence_path})")

    return _ok(send_command("validate_anim_blueprint", {
        "asset_path": asset_path,
    }), f"validate_anim_blueprint({asset_path})")


class TestPhase9hWave12AnimBlueprintValidation:
    def test_01_validate_anim_blueprint_reports_healthy_bound_state(self):
        skeleton_paths = _find_assets("Skeleton", max_results=20)
        if not skeleton_paths:
            pytest.skip("No Skeleton assets available for AnimBlueprint validation test")

        sequence_paths = _find_assets("AnimSequence", max_results=60)
        if not sequence_paths:
            pytest.skip("No AnimSequence assets available for AnimBlueprint validation test")

        target_skeleton_path = skeleton_paths[0]
        asset_path, state_machine_name, state_name = _create_seed_anim_blueprint(
            target_skeleton_path,
            "ABP_Phase9H_Wave12Healthy",
        )

        validated: Optional[Dict[str, Any]] = None
        validated_sequence_path: Optional[str] = None
        for sequence_path in sequence_paths:
            candidate = _bind_sequence_and_validate(asset_path, state_machine_name, state_name, sequence_path)
            codes = _issue_codes(candidate)
            if "incompatible_asset_skeleton" in codes:
                continue
            if "missing_animation_asset_skeleton" in codes:
                continue
            if "invalid_animation_asset" in codes:
                continue
            validated = candidate
            validated_sequence_path = sequence_path
            break

        if not validated or not validated_sequence_path:
            pytest.skip("No compatible AnimSequence assets available for AnimBlueprint validation test")

        state_machines = validated.get("state_machines", [])
        assert validated.get("success") is True, validated
        assert validated.get("is_valid") is True, validated
        assert validated.get("compile_healthy") is True, validated
        assert validated.get("readback_healthy") is True, validated
        assert validated.get("generated_class_present") is True, validated
        assert validated.get("target_skeleton_path") == target_skeleton_path, validated
        assert validated.get("checked_asset_player_count", 0) >= 1, validated
        assert validated.get("compile_status") in {"UpToDate", "UpToDateWithWarnings"}, validated
        assert validated.get("error_count") == 0, validated
        assert isinstance(state_machines, list) and state_machines, validated
        state = state_machines[0].get("states", [])[0]
        assert state.get("name") == state_name, validated
        assert state.get("animation_asset_path") == validated_sequence_path, validated

    def test_02_validate_anim_blueprint_reports_incompatible_sequence(self):
        skeleton_paths = _find_assets("Skeleton", max_results=20)
        if not skeleton_paths:
            pytest.skip("No Skeleton assets available for incompatible-sequence validation test")

        sequence_paths = _find_assets("AnimSequence", max_results=80)
        preview_mesh_paths = _find_assets("SkeletalMesh", max_results=40)
        if not sequence_paths and not preview_mesh_paths:
            pytest.skip("No AnimSequence or SkeletalMesh assets available for negative AnimBlueprint validation test")

        target_skeleton_path = skeleton_paths[0]
        asset_path, state_machine_name, state_name = _create_seed_anim_blueprint(
            target_skeleton_path,
            "ABP_Phase9H_Wave12Mismatch",
        )

        validated: Optional[Dict[str, Any]] = None
        validated_sequence_path: Optional[str] = None
        for sequence_path in sequence_paths:
            candidate = _bind_sequence_and_validate(asset_path, state_machine_name, state_name, sequence_path)
            codes = _issue_codes(candidate)
            if "incompatible_asset_skeleton" not in codes:
                continue
            validated = candidate
            validated_sequence_path = sequence_path
            break

        preview_mesh_path: Optional[str] = None
        if not validated:
            for candidate_preview_mesh_path in preview_mesh_paths:
                preview_asset_path, _, _ = _create_seed_anim_blueprint(
                    target_skeleton_path,
                    "ABP_Phase9H_Wave12PreviewMismatch",
                    preview_skeletal_mesh_path=candidate_preview_mesh_path,
                    seed_state_machine=False,
                )
                candidate = _ok(send_command("validate_anim_blueprint", {
                    "asset_path": preview_asset_path,
                }), f"validate_anim_blueprint({preview_asset_path})")
                codes = _issue_codes(candidate)
                if "preview_mesh_skeleton_mismatch" not in codes:
                    continue
                validated = candidate
                preview_mesh_path = candidate_preview_mesh_path
                break

        if not validated:
            pytest.skip("No incompatible AnimSequence or preview SkeletalMesh assets available for AnimBlueprint validation test")

        codes = _issue_codes(validated)
        assert validated.get("success") is True, validated
        assert validated.get("is_valid") is False, validated
        assert validated.get("error_count", 0) >= 1, validated
        assert validated.get("readback_healthy") is True, validated
        if validated_sequence_path is not None:
            state_machines = validated.get("state_machines", [])
            assert validated.get("checked_asset_player_count", 0) >= 1, validated
            assert "incompatible_asset_skeleton" in codes, validated
            assert isinstance(state_machines, list) and state_machines, validated
            state = state_machines[0].get("states", [])[0]
            assert state.get("name") == state_name, validated
            assert state.get("animation_asset_path") == validated_sequence_path, validated
        else:
            assert preview_mesh_path is not None
            assert "preview_mesh_skeleton_mismatch" in codes, validated
            assert validated.get("preview_skeletal_mesh_path") == preview_mesh_path, validated