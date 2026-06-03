"""Live-editor smoke tests for Phase 9i batch 1 Niagara System inspection and creation."""

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
        pytest.fail(f"{context}: bridge error: {json.dumps(resp)[:600]}")
    return _unwrap(resp)


class TestPhase9iBatch1NiagaraSystem:
    def test_01_create_niagara_system_asset_and_readback(self):
        suffix = int(time.time()) % 1000000
        system_name = f"NS_Phase9I_Batch1_{suffix}"
        destination_path = "/Game/CopilotTests/Niagara"

        created = _ok(send_command("create_niagara_system_asset", {
            "niagara_system_name": system_name,
            "destination_path": destination_path,
        }), "create_niagara_system_asset")

        asset_path = f"{destination_path}/{system_name}.{system_name}"
        assert created.get("success") is True, created
        assert created.get("created") is True, created
        assert created.get("created_from_template") is False, created
        assert created.get("asset_path") == asset_path, created
        assert created.get("niagara_system_name") == system_name, created
        assert created.get("destination_path") == destination_path, created
        assert created.get("emitter_count") == 0, created
        assert created.get("enabled_emitter_count") == 0, created
        assert created.get("has_system_spawn_script") is True, created
        assert created.get("has_system_update_script") is True, created
        assert isinstance(created.get("exposed_user_parameters"), list), created
        assert isinstance(created.get("emitters"), list), created

        readback = _ok(send_command("read_niagara_system_content", {
            "asset_path": asset_path,
        }), "read_niagara_system_content")

        assert readback.get("asset_path") == asset_path, readback
        assert readback.get("name") == system_name, readback
        assert readback.get("emitter_count") == 0, readback
        assert readback.get("enabled_emitter_count") == 0, readback
        assert readback.get("has_system_spawn_script") is True, readback
        assert readback.get("has_system_update_script") is True, readback

    def test_02_create_niagara_system_asset_from_template(self):
        suffix = int(time.time()) % 1000000
        source_name = f"NS_Phase9I_TemplateSource_{suffix}"
        clone_name = f"NS_Phase9I_TemplateClone_{suffix}"
        destination_path = "/Game/CopilotTests/Niagara"

        source_created = _ok(send_command("create_niagara_system_asset", {
            "niagara_system_name": source_name,
            "destination_path": destination_path,
        }), "create_niagara_system_asset_source")

        source_asset_path = f"{destination_path}/{source_name}.{source_name}"
        assert source_created.get("asset_path") == source_asset_path, source_created

        cloned = _ok(send_command("create_niagara_system_asset", {
            "niagara_system_name": clone_name,
            "destination_path": destination_path,
            "template_asset_path": source_asset_path,
        }), "create_niagara_system_asset_clone")

        clone_asset_path = f"{destination_path}/{clone_name}.{clone_name}"
        assert cloned.get("success") is True, cloned
        assert cloned.get("created") is True, cloned
        assert cloned.get("created_from_template") is True, cloned
        assert cloned.get("template_asset_path") == source_asset_path, cloned
        assert cloned.get("asset_path") == clone_asset_path, cloned
        assert cloned.get("emitter_count") == source_created.get("emitter_count"), cloned
        assert cloned.get("exposed_user_parameter_count") == source_created.get("exposed_user_parameter_count"), cloned

        readback = _ok(send_command("read_niagara_system_content", {
            "asset_path": clone_asset_path,
        }), "read_niagara_system_content_clone")

        assert readback.get("asset_path") == clone_asset_path, readback
        assert readback.get("name") == clone_name, readback
        assert readback.get("emitter_count") == source_created.get("emitter_count"), readback
        assert readback.get("has_system_spawn_script") is True, readback
        assert readback.get("has_system_update_script") is True, readback