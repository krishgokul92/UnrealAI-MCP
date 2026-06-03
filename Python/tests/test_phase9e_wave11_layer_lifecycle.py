"""Live-editor smoke tests for the eleventh Phase 9e layer-lifecycle slice."""

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
LAYER_WORLD_READY_RETRIES = 45
LAYER_WORLD_READY_INTERVAL_SECONDS = 1


@pytest.fixture(scope="module", autouse=True)
def require_bridge() -> None:
    for _ in range(30):
        if ping():
            time.sleep(STARTUP_SETTLE_SECONDS)
            _prepare_layer_capable_world()
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


def _spawn_cube(name: str, location: list[float]) -> None:
    resp = send_command("spawn_actor", {
        "name": name,
        "type": "StaticMeshActor",
        "location": location,
    })
    if isinstance(resp, dict) and resp.get("status") == "error":
        if "already exists" in json.dumps(resp).lower():
            return
        pytest.fail(f"spawn_actor({name}): {resp}")


def _prepare_layer_capable_world() -> None:
    _ok(send_command("new_blank_map", {
        "save_existing_map": False,
    }), "new_blank_map")

    probe_suffix = str(int(time.time() * 1000))[-8:]
    last_error: Dict[str, Any] | None = None

    for attempt in range(LAYER_WORLD_READY_RETRIES):
        actor_name = f"Wave11LayerProbe_{probe_suffix}_{attempt}"
        layer_name = f"Wave11LayerProbeLayer_{probe_suffix}"

        spawn_resp = send_command("spawn_actor", {
            "name": actor_name,
            "type": "StaticMeshActor",
            "location": [1200.0 + attempt * 25.0, 100.0, 80.0],
        })
        if isinstance(spawn_resp, dict) and spawn_resp.get("status") == "error":
            last_error = spawn_resp
            time.sleep(LAYER_WORLD_READY_INTERVAL_SECONDS)
            continue

        add_resp = send_command("add_actor_to_layer", {
            "actor_name": actor_name,
            "layer_name": layer_name,
        })
        if isinstance(add_resp, dict) and add_resp.get("status") == "success":
            send_command("remove_actor_from_layer", {
                "actor_name": actor_name,
                "layer_name": layer_name,
            })
            send_command("delete_actor", {"name": actor_name})
            return

        last_error = add_resp
        time.sleep(LAYER_WORLD_READY_INTERVAL_SECONDS)

    pytest.fail(f"Could not prepare a layer-capable blank world: {last_error}")


class TestPhase9eWave11LayerLifecycle:
    SUFFIX = str(int(time.time()))[-6:]

    @classmethod
    def _name(cls, base: str) -> str:
        return f"{base}_{cls.SUFFIX}"

    def test_01_rename_and_delete_layer(self):
        original_layer = self._name("LifecycleOld")
        renamed_layer = self._name("LifecycleNew")
        actor_name = self._name("LifecycleActor")

        _spawn_cube(actor_name, [9400.0, 100.0, 80.0])
        _ok(send_command("add_actor_to_layer", {
            "actor_name": actor_name,
            "layer_name": original_layer,
        }), "add_actor_to_layer setup")

        rename_payload = _ok(send_command("rename_layer", {
            "layer_name": original_layer,
            "new_layer_name": renamed_layer,
        }), "rename_layer")
        actor_layers_after_rename = _ok(send_command("get_actor_layers", {
            "actor_name": actor_name,
        }), "get_actor_layers after rename")
        old_layer_lookup = send_command("get_actors_in_layer", {
            "layer_name": original_layer,
        })

        delete_payload = _ok(send_command("delete_layer", {
            "layer_name": renamed_layer,
        }), "delete_layer")
        catalog_after_delete = _ok(send_command("get_all_layers", {}), "get_all_layers after delete")
        actor_layers_after_delete = _ok(send_command("get_actor_layers", {
            "actor_name": actor_name,
        }), "get_actor_layers after delete")

        layer_names_after_delete = {entry.get("name") for entry in catalog_after_delete.get("layers") or []}

        assert rename_payload.get("success") is True, rename_payload
        assert rename_payload.get("changed") is True, rename_payload
        assert rename_payload.get("new_layer_name") == renamed_layer, rename_payload
        assert actor_layers_after_rename.get("layers") == [renamed_layer], actor_layers_after_rename
        assert old_layer_lookup.get("status") == "error", old_layer_lookup
        assert "layer not found" in json.dumps(old_layer_lookup).lower(), old_layer_lookup

        assert delete_payload.get("success") is True, delete_payload
        assert delete_payload.get("changed") is True, delete_payload
        assert delete_payload.get("layer_exists_after_delete") is False, delete_payload
        assert delete_payload.get("actor_count_removed") == 1, delete_payload
        assert renamed_layer not in layer_names_after_delete, catalog_after_delete
        assert actor_layers_after_delete.get("layer_count") == 0, actor_layers_after_delete