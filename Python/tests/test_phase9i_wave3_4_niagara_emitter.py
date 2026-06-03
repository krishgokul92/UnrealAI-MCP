"""Live-editor smoke tests for Phase 9i batch 2 Niagara emitter inspection and lifecycle helpers."""

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


class TestPhase9iBatch2NiagaraEmitter:
    def test_01_add_read_duplicate_rename_remove_niagara_emitter(self):
        emitter_asset_path = _find_niagara_emitter_asset()
        if not emitter_asset_path:
            pytest.skip("No NiagaraEmitter assets available for batch 2 validation")

        suffix = int(time.time()) % 1000000
        system_name = f"NS_Phase9I_Batch2_{suffix}"
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

        added_handle_id = added.get("added_emitter_handle_id")
        added_name = added.get("name")
        assert added.get("success") is True, added
        assert added.get("added") is True, added
        assert added.get("asset_path") == system_asset_path, added
        assert added.get("emitter_asset_path") == emitter_asset_path, added
        assert added_handle_id, added
        assert added_name, added
        assert added.get("script_count", 0) >= 1, added
        assert isinstance(added.get("scripts"), list), added
        assert isinstance(added.get("renderers"), list), added
        assert isinstance(added.get("renderer_binding_parameters"), list), added

        emitter_readback = _ok(send_command("read_niagara_system_emitter", {
            "asset_path": system_asset_path,
            "emitter_handle_id": added_handle_id,
        }), "read_niagara_system_emitter")

        assert emitter_readback.get("handle_id") == added_handle_id, emitter_readback
        assert emitter_readback.get("name") == added_name, emitter_readback
        assert emitter_readback.get("asset_path") == system_asset_path, emitter_readback
        assert emitter_readback.get("system_name") == system_name, emitter_readback

        duplicate_name = f"{added_name}_Copy"
        duplicated = _ok(send_command("duplicate_niagara_system_emitter", {
            "asset_path": system_asset_path,
            "emitter_handle_id": added_handle_id,
            "new_emitter_name": duplicate_name,
        }), "duplicate_niagara_system_emitter")

        duplicated_handle_id = duplicated.get("duplicated_emitter_handle_id")
        assert duplicated.get("success") is True, duplicated
        assert duplicated.get("duplicated") is True, duplicated
        assert duplicated.get("source_emitter_handle_id") == added_handle_id, duplicated
        assert duplicated_handle_id and duplicated_handle_id != added_handle_id, duplicated
        assert duplicated.get("name") == duplicate_name, duplicated

        renamed_name = f"{duplicate_name}_Renamed"
        renamed = _ok(send_command("rename_niagara_system_emitter", {
            "asset_path": system_asset_path,
            "emitter_handle_id": duplicated_handle_id,
            "new_emitter_name": renamed_name,
        }), "rename_niagara_system_emitter")

        assert renamed.get("success") is True, renamed
        assert renamed.get("renamed") is True, renamed
        assert renamed.get("old_emitter_name") == duplicate_name, renamed
        assert renamed.get("new_emitter_name") == renamed_name, renamed
        assert renamed.get("name") == renamed_name, renamed

        removed = _ok(send_command("remove_niagara_system_emitter", {
            "asset_path": system_asset_path,
            "emitter_handle_id": duplicated_handle_id,
        }), "remove_niagara_system_emitter")

        assert removed.get("success") is True, removed
        assert removed.get("removed") is True, removed
        assert removed.get("removed_emitter_handle_id") == duplicated_handle_id, removed
        remaining_handles = {emitter.get("handle_id") for emitter in removed.get("emitters", [])}
        assert duplicated_handle_id not in remaining_handles, removed
        assert added_handle_id in remaining_handles, removed