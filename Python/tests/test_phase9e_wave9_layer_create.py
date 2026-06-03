"""Live-editor smoke tests for the ninth Phase 9e layer-create slice."""

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


def _prepare_layer_capable_world() -> None:
    _ok(send_command("new_blank_map", {
        "save_existing_map": False,
    }), "new_blank_map")

    probe_suffix = str(int(time.time() * 1000))[-8:]
    last_error: Dict[str, Any] | None = None

    for attempt in range(LAYER_WORLD_READY_RETRIES):
        actor_name = f"Wave9LayerProbe_{probe_suffix}_{attempt}"
        layer_name = f"Wave9LayerProbeLayer_{probe_suffix}"

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


class TestPhase9eWave9LayerCreate:
    SUFFIX = str(int(time.time()))[-6:]

    @classmethod
    def _name(cls, base: str) -> str:
        return f"{base}_{cls.SUFFIX}"

    def test_01_create_layer_and_reject_duplicate(self):
        layer_name = self._name("CreateLayer")

        create_payload = _ok(send_command("create_layer", {
            "layer_name": layer_name,
        }), "create_layer")
        catalog = _ok(send_command("get_all_layers", {}), "get_all_layers")
        duplicate_resp = send_command("create_layer", {
            "layer_name": layer_name,
        })

        entries = {entry.get("name"): entry for entry in catalog.get("layers") or []}

        assert create_payload.get("success") is True, create_payload
        assert create_payload.get("changed") is True, create_payload
        assert create_payload.get("is_visible") is True, create_payload
        assert entries[layer_name]["is_visible"] is True, catalog
        assert entries[layer_name]["actor_count"] == 0, catalog
        assert duplicate_resp.get("status") == "error", duplicate_resp
        assert "already exists" in json.dumps(duplicate_resp).lower(), duplicate_resp