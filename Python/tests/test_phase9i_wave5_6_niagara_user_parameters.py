"""Live-editor smoke tests for Phase 9i batch 3 Niagara user-parameter mutation and validation helpers."""

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
        pytest.fail(f"{context}: bridge error: {json.dumps(resp)[:600]}")
    return _unwrap(resp)


def _find_niagara_emitter_asset() -> Optional[str]:
    for query in ["Emitter", "VectorField", "Particle"]:
        payload = _ok(send_command("find_assets", {
            "path": "/Niagara",
            "query": query,
            "max_results": 100,
        }), f"find_assets({query})")
        for asset in payload.get("assets", []):
            if "NiagaraEmitter" in asset.get("class", ""):
                return asset.get("path")
    return None


def _get_parameter_by_display_name(readback: Dict[str, Any], display_name: str) -> Dict[str, Any]:
    for parameter in readback.get("exposed_user_parameters", []):
        if parameter.get("display_name") == display_name:
            return parameter
    pytest.fail(f"Missing exposed user parameter '{display_name}' in readback: {json.dumps(readback)[:800]}")


class TestPhase9iBatch3NiagaraUserParameters:
    def test_01_set_niagara_system_user_parameters_and_validate(self):
        emitter_asset_path = _find_niagara_emitter_asset()
        if not emitter_asset_path:
            pytest.skip("No NiagaraEmitter assets available for batch 3 validation")

        suffix = int(time.time()) % 1000000
        system_name = f"NS_Phase9I_Batch3_{suffix}"
        destination_path = "/Game/CopilotTests/Niagara"

        created = _ok(send_command("create_niagara_system_asset", {
            "niagara_system_name": system_name,
            "destination_path": destination_path,
        }), "create_niagara_system_asset")

        system_asset_path = f"{destination_path}/{system_name}.{system_name}"
        assert created.get("asset_path") == system_asset_path, created

        added = _ok(send_command("add_niagara_emitter_to_system", {
            "asset_path": system_asset_path,
            "emitter_asset_path": emitter_asset_path,
        }), "add_niagara_emitter_to_system")
        assert added.get("added") is True, added

        updated = _ok(send_command("set_niagara_system_user_parameters", {
            "asset_path": system_asset_path,
            "create_if_missing": True,
            "parameters": [
                {"name": "ScalarParam", "type": "float", "value": 1.25},
                {"name": "BoolParam", "type": "bool", "value": True},
                {"name": "VectorParam", "type": "vector", "value": [1.0, 2.0, 3.0]},
                {"name": "ColorParam", "type": "color", "value": [0.1, 0.2, 0.3, 1.0]},
                {"name": "ObjectParam", "type": "object", "object_path": emitter_asset_path},
            ],
        }), "set_niagara_system_user_parameters")

        assert updated.get("success") is True, updated
        assert updated.get("updated") is True, updated
        assert updated.get("created_parameter_count") == 5, updated
        assert updated.get("updated_parameter_count") == 5, updated
        assert updated.get("asset_path") == system_asset_path, updated

        scalar_param = _get_parameter_by_display_name(updated, "ScalarParam")
        bool_param = _get_parameter_by_display_name(updated, "BoolParam")
        vector_param = _get_parameter_by_display_name(updated, "VectorParam")
        color_param = _get_parameter_by_display_name(updated, "ColorParam")
        object_param = _get_parameter_by_display_name(updated, "ObjectParam")

        assert scalar_param.get("value_kind") == "float", scalar_param
        assert scalar_param.get("value") == pytest.approx(1.25), scalar_param
        assert bool_param.get("value_kind") == "bool", bool_param
        assert bool_param.get("value") is True, bool_param
        assert vector_param.get("value_kind") == "vector", vector_param
        assert vector_param.get("value") == pytest.approx([1.0, 2.0, 3.0]), vector_param
        assert color_param.get("value_kind") == "color", color_param
        assert color_param.get("value") == pytest.approx([0.1, 0.2, 0.3, 1.0]), color_param
        assert object_param.get("value_kind") == "object", object_param
        assert object_param.get("object_path") == emitter_asset_path, object_param
        assert object_param.get("is_null_object_value") is False, object_param

        validated = _ok(send_command("validate_niagara_system", {
            "asset_path": system_asset_path,
        }), "validate_niagara_system")

        issue_codes = {issue.get("code") for issue in validated.get("issues", [])}
        assert validated.get("validated") is True, validated
        assert validated.get("error_count") == 0, validated
        assert "no_emitters" not in issue_codes, validated
        assert "all_emitters_disabled" not in issue_codes, validated

    def test_02_validate_niagara_system_warns_when_no_emitters_exist(self):
        suffix = int(time.time()) % 1000000
        system_name = f"NS_Phase9I_Batch3_Validate_{suffix}"
        destination_path = "/Game/CopilotTests/Niagara"

        created = _ok(send_command("create_niagara_system_asset", {
            "niagara_system_name": system_name,
            "destination_path": destination_path,
        }), "create_niagara_system_asset_blank")

        system_asset_path = f"{destination_path}/{system_name}.{system_name}"
        assert created.get("asset_path") == system_asset_path, created

        validated = _ok(send_command("validate_niagara_system", {
            "asset_path": system_asset_path,
        }), "validate_niagara_system_blank")

        issue_codes = {issue.get("code") for issue in validated.get("issues", [])}
        assert validated.get("validated") is True, validated
        assert validated.get("warning_count", 0) >= 1, validated
        assert "no_emitters" in issue_codes, validated