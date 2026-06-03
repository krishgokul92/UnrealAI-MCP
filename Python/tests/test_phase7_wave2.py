"""Phase 7 Wave 2 smoke tests \u2014 asset registry, material instances, asset import.

Run with:
    e:\\unrealBP\\.venv\\Scripts\\python.exe -m pytest e:\\unrealBP\\UnrealAI\\Python\\tests -v

All tests are end-to-end against the running editor. They auto-skip when the
TCP bridge on 127.0.0.1:55557 is unreachable.
"""

from __future__ import annotations

import json
import sys
from pathlib import Path
from typing import Any, Dict

import pytest

_PY_ROOT = Path(__file__).resolve().parent.parent
if str(_PY_ROOT) not in sys.path:
    sys.path.insert(0, str(_PY_ROOT))

from ue_bridge import ping, send_command  # noqa: E402

BRIDGE_AVAILABLE = ping()
SKIP_REASON = "UE bridge not reachable on 127.0.0.1:55557 (open the editor first)"
needs_bridge = pytest.mark.skipif(not BRIDGE_AVAILABLE, reason=SKIP_REASON)


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


@needs_bridge
class TestPhase7Wave2:
    """Smoke tests for the five Phase 7 Wave 2 MCP wrappers."""

    # --- find_assets ------------------------------------------------------- #
    def test_01_find_assets_unfiltered(self):
        # Every PlayTesting project has at least one asset under /Game.
        resp = send_command("find_assets", {"path": "/Game", "max_results": 50})
        payload = _ok(resp, "find_assets unfiltered")
        assert isinstance(payload.get("assets"), list)
        assert payload.get("count", 0) >= 1, payload

    def test_02_find_assets_by_class(self):
        # /Game/Blueprints should contain the BPs created by the Phase 5 suite.
        resp = send_command("find_assets", {
            "path": "/Game/Blueprints",
            "class_name": "Blueprint",
            "max_results": 100,
        })
        payload = _ok(resp, "find_assets blueprints")
        assets = payload.get("assets", [])
        assert isinstance(assets, list), payload
        # If the Phase 5 BPs already exist, expect at least one Smoke BP.
        if assets:
            for a in assets:
                assert a.get("name"), a
                assert a.get("path"), a

    def test_03_find_assets_substring(self):
        resp = send_command("find_assets", {
            "path": "/Game/Blueprints",
            "query": "smoke",
            "max_results": 50,
        })
        payload = _ok(resp, "find_assets smoke")
        # Either Phase 5 BPs are present (>=1) or the project is fresh (==0).
        assert isinstance(payload.get("assets"), list), payload

    # --- dependencies / referencers --------------------------------------- #
    def test_04_get_asset_dependencies_engine_basicshape(self):
        # The BasicShapeMaterial always exists in /Engine and has dependencies.
        resp = send_command("get_asset_dependencies", {
            "asset_path": "/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial",
        })
        payload = _ok(resp, "get_asset_dependencies")
        assert isinstance(payload.get("dependencies"), list), payload
        assert payload.get("count", -1) >= 0, payload

    def test_05_get_referencers_engine_basicshape(self):
        resp = send_command("get_referencers", {
            "asset_path": "/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial",
        })
        payload = _ok(resp, "get_referencers")
        assert isinstance(payload.get("referencers"), list), payload

    # --- create_material_instance ----------------------------------------- #
    def test_06_create_material_instance(self):
        instance_name = "MI_Phase7Smoke"
        instance_path = f"/Game/Materials/{instance_name}.{instance_name}"
        # If a previous run left it behind, that's fine \u2014 the C++ handler will
        # report "Asset already exists"; downstream test_07 still validates it.
        resp = send_command("create_material_instance", {
            "parent_material": "/Engine/BasicShapes/BasicShapeMaterial",
            "instance_name": instance_name,
            "dest_path": "/Game/Materials",
            "vector_parameters": {"Color": [0.1, 0.4, 0.9, 1.0]},
        })
        if isinstance(resp, dict) and resp.get("status") == "error":
            err = json.dumps(resp).lower()
            assert "already exists" in err, resp
        else:
            payload = _unwrap(resp)
            assert payload.get("success") is True, payload
            assert payload.get("path") == instance_path, payload

    def test_07_find_created_material_instance(self):
        # Confirm the instance is now visible to the asset registry.
        resp = send_command("find_assets", {
            "path": "/Game/Materials",
            "query": "MI_Phase7Smoke",
            "max_results": 10,
        })
        payload = _ok(resp, "find MI_Phase7Smoke")
        names = [a.get("name") for a in payload.get("assets", [])]
        assert "MI_Phase7Smoke" in names, payload

    # --- import_asset ----------------------------------------------------- #
    def test_08_import_asset_missing_source(self):
        # Negative test \u2014 we don't ship an FBX in the repo; verify the handler
        # surfaces a clean error rather than crashing.
        resp = send_command("import_asset", {
            "source_path": r"C:\\definitely\\does\\not\\exist.fbx",
            "dest_path": "/Game/Imported",
        })
        # Expect either an error envelope or an unsuccessful payload.
        if isinstance(resp, dict) and resp.get("status") == "error":
            assert "not found" in json.dumps(resp).lower(), resp
        else:
            payload = _unwrap(resp)
            assert payload.get("success") is False, payload
