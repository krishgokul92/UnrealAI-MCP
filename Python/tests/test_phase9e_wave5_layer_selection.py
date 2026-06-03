"""Live-editor smoke tests for the fifth Phase 9e layer-selection slice."""

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
        actor_name = f"Wave5LayerProbe_{probe_suffix}_{attempt}"
        layer_name = f"Wave5LayerProbeLayer_{probe_suffix}"

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


class TestPhase9eWave5LayerSelection:
    SUFFIX = str(int(time.time()))[-6:]

    @classmethod
    def _name(cls, base: str) -> str:
        return f"{base}_{cls.SUFFIX}"

    def test_01_select_actors_in_layer_replace_and_append(self):
        layer_name = self._name("SelectLayer")
        actor_a = self._name("SelectLayer_A")
        actor_b = self._name("SelectLayer_B")
        actor_outside = self._name("SelectLayer_Outside")

        _spawn_cube(actor_a, [3200.0, 100.0, 80.0])
        _spawn_cube(actor_b, [3500.0, 100.0, 80.0])
        _spawn_cube(actor_outside, [3800.0, 100.0, 80.0])

        _ok(send_command("add_actor_to_layer", {
            "actor_name": actor_a,
            "layer_name": layer_name,
        }), "add_actor_to_layer actor_a")
        _ok(send_command("add_actor_to_layer", {
            "actor_name": actor_b,
            "layer_name": layer_name,
        }), "add_actor_to_layer actor_b")

        _ok(send_command("clear_actor_selection", {}), "clear_actor_selection setup")
        _ok(send_command("select_actors", {
            "actor_names": [actor_outside],
            "replace_selection": True,
        }), "select_actors setup outsider")

        append_payload = _ok(send_command("select_actors_in_layer", {
            "layer_name": layer_name,
            "replace_selection": False,
        }), "select_actors_in_layer append")
        selected_after_append = _ok(send_command("get_selected_actors", {}), "get_selected_actors after append")

        append_names = sorted(actor.get("name") for actor in selected_after_append.get("actors") or [])
        assert append_payload.get("success") is True, append_payload
        assert append_payload.get("replace_selection") is False, append_payload
        assert append_payload.get("changed") is True, append_payload
        assert append_payload.get("selected_count") == 3, append_payload
        assert append_names == sorted([actor_a, actor_b, actor_outside]), selected_after_append

        replace_payload = _ok(send_command("select_actors_in_layer", {
            "layer_name": layer_name,
            "replace_selection": True,
        }), "select_actors_in_layer replace")
        selected_after_replace = _ok(send_command("get_selected_actors", {}), "get_selected_actors after replace")

        replace_names = sorted(actor.get("name") for actor in selected_after_replace.get("actors") or [])
        assert replace_payload.get("success") is True, replace_payload
        assert replace_payload.get("replace_selection") is True, replace_payload
        assert replace_payload.get("changed") is True, replace_payload
        assert replace_payload.get("previous_selected_count") == 3, replace_payload
        assert replace_payload.get("selected_count") == 2, replace_payload
        assert replace_names == sorted([actor_a, actor_b]), selected_after_replace

    def test_02_deselect_actors_in_layer_preserves_other_selection(self):
        layer_name = self._name("DeselectLayer")
        actor_a = self._name("DeselectLayer_A")
        actor_b = self._name("DeselectLayer_B")
        actor_outside = self._name("DeselectLayer_Outside")

        _spawn_cube(actor_a, [4100.0, 100.0, 80.0])
        _spawn_cube(actor_b, [4400.0, 100.0, 80.0])
        _spawn_cube(actor_outside, [4700.0, 100.0, 80.0])

        _ok(send_command("add_actor_to_layer", {
            "actor_name": actor_a,
            "layer_name": layer_name,
        }), "add_actor_to_layer actor_a setup")
        _ok(send_command("add_actor_to_layer", {
            "actor_name": actor_b,
            "layer_name": layer_name,
        }), "add_actor_to_layer actor_b setup")

        _ok(send_command("select_actors", {
            "actor_names": [actor_a, actor_b, actor_outside],
            "replace_selection": True,
        }), "select_actors setup mixed selection")

        deselect_payload = _ok(send_command("deselect_actors_in_layer", {
            "layer_name": layer_name,
        }), "deselect_actors_in_layer")
        selected_after_deselect = _ok(send_command("get_selected_actors", {}), "get_selected_actors after deselect")
        deselect_noop = _ok(send_command("deselect_actors_in_layer", {
            "layer_name": layer_name,
        }), "deselect_actors_in_layer noop")

        remaining_names = sorted(actor.get("name") for actor in selected_after_deselect.get("actors") or [])
        assert deselect_payload.get("success") is True, deselect_payload
        assert deselect_payload.get("changed") is True, deselect_payload
        assert deselect_payload.get("previous_selected_count") == 3, deselect_payload
        assert deselect_payload.get("selected_count") == 1, deselect_payload
        assert remaining_names == [actor_outside], selected_after_deselect
        assert deselect_noop.get("success") is True, deselect_noop
        assert deselect_noop.get("changed") is False, deselect_noop
        assert deselect_noop.get("selected_count") == 1, deselect_noop