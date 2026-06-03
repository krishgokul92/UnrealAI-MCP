"""Live-editor smoke tests for the tenth Phase 9e selected-actor layer slice."""

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
        actor_name = f"Wave10LayerProbe_{probe_suffix}_{attempt}"
        layer_name = f"Wave10LayerProbeLayer_{probe_suffix}"

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


class TestPhase9eWave10SelectedActorLayers:
    SUFFIX = str(int(time.time()))[-6:]

    @classmethod
    def _name(cls, base: str) -> str:
        return f"{base}_{cls.SUFFIX}"

    def test_01_add_and_remove_selected_actors_from_layer(self):
        layer_name = self._name("SelectedLayer")
        actor_a = self._name("SelectedLayer_A")
        actor_b = self._name("SelectedLayer_B")
        actor_outside = self._name("SelectedLayer_Outside")

        _spawn_cube(actor_a, [8200.0, 100.0, 80.0])
        _spawn_cube(actor_b, [8500.0, 100.0, 80.0])
        _spawn_cube(actor_outside, [8800.0, 100.0, 80.0])

        _ok(send_command("select_actors", {
            "actor_names": [actor_a, actor_b],
            "replace_selection": True,
        }), "select_actors setup")

        add_payload = _ok(send_command("add_selected_actors_to_layer", {
            "layer_name": layer_name,
        }), "add_selected_actors_to_layer")
        actors_in_layer_after_add = _ok(send_command("get_actors_in_layer", {
            "layer_name": layer_name,
        }), "get_actors_in_layer after add")

        _ok(send_command("add_actor_to_layer", {
            "actor_name": actor_outside,
            "layer_name": layer_name,
        }), "add_actor_to_layer outsider")

        remove_payload = _ok(send_command("remove_selected_actors_from_layer", {
            "layer_name": layer_name,
        }), "remove_selected_actors_from_layer")
        actors_in_layer_after_remove = _ok(send_command("get_actors_in_layer", {
            "layer_name": layer_name,
        }), "get_actors_in_layer after remove")
        remove_noop = _ok(send_command("remove_selected_actors_from_layer", {
            "layer_name": layer_name,
        }), "remove_selected_actors_from_layer noop")

        add_names = sorted(actor.get("name") for actor in actors_in_layer_after_add.get("actors") or [])
        remove_names = sorted(actor.get("name") for actor in actors_in_layer_after_remove.get("actors") or [])

        assert add_payload.get("success") is True, add_payload
        assert add_payload.get("changed") is True, add_payload
        assert add_payload.get("selected_count") == 2, add_payload
        assert add_payload.get("layer_created") is True, add_payload
        assert add_names == sorted([actor_a, actor_b]), actors_in_layer_after_add

        assert remove_payload.get("success") is True, remove_payload
        assert remove_payload.get("changed") is True, remove_payload
        assert remove_payload.get("selected_count") == 2, remove_payload
        assert remove_names == [actor_outside], actors_in_layer_after_remove

        assert remove_noop.get("success") is True, remove_noop
        assert remove_noop.get("changed") is False, remove_noop