"""Live-editor smoke tests for the fourth Phase 9e actor-layer mutation slice."""

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


class TestPhase9eWave4ActorLayerMutation:
    SUFFIX = str(int(time.time()))[-6:]

    @classmethod
    def _name(cls, base: str) -> str:
        return f"{base}_{cls.SUFFIX}"

    def test_01_add_actor_to_layer_creates_and_reads_back(self):
        actor_name = self._name("LayerWave4_Add")
        layer_name = self._name("Gameplay_Add")
        _spawn_cube(actor_name, [2600.0, 100.0, 80.0])

        add_payload = _ok(send_command("add_actor_to_layer", {
            "actor_name": actor_name,
            "layer_name": layer_name,
        }), "add_actor_to_layer")
        actor_layers = _ok(send_command("get_actor_layers", {
            "actor_name": actor_name,
        }), "get_actor_layers after add")
        actors_in_layer = _ok(send_command("get_actors_in_layer", {
            "layer_name": layer_name,
        }), "get_actors_in_layer after add")

        assert add_payload.get("success") is True, add_payload
        assert add_payload.get("changed") is True, add_payload
        assert add_payload.get("already_in_layer") is False, add_payload
        assert add_payload.get("layer_created") is True, add_payload
        assert layer_name in (add_payload.get("layers") or []), add_payload
        assert actor_layers.get("layers") == [layer_name], actor_layers
        assert actor_layers.get("layer_count") == 1, actor_layers
        assert actors_in_layer.get("layer_name") == layer_name, actors_in_layer
        assert actors_in_layer.get("actor_count") == 1, actors_in_layer
        assert [actor.get("name") for actor in actors_in_layer.get("actors") or []] == [actor_name], actors_in_layer

    def test_02_remove_actor_from_layer_roundtrip_and_noop(self):
        actor_name = self._name("LayerWave4_Remove")
        layer_name = self._name("Gameplay_Remove")
        _spawn_cube(actor_name, [2900.0, 100.0, 80.0])

        _ok(send_command("add_actor_to_layer", {
            "actor_name": actor_name,
            "layer_name": layer_name,
        }), "add_actor_to_layer setup")

        remove_payload = _ok(send_command("remove_actor_from_layer", {
            "actor_name": actor_name,
            "layer_name": layer_name,
        }), "remove_actor_from_layer")
        actor_layers = _ok(send_command("get_actor_layers", {
            "actor_name": actor_name,
        }), "get_actor_layers after remove")
        actors_in_layer = _ok(send_command("get_actors_in_layer", {
            "layer_name": layer_name,
        }), "get_actors_in_layer after remove")
        remove_noop = _ok(send_command("remove_actor_from_layer", {
            "actor_name": actor_name,
            "layer_name": layer_name,
        }), "remove_actor_from_layer noop")

        assert remove_payload.get("success") is True, remove_payload
        assert remove_payload.get("changed") is True, remove_payload
        assert remove_payload.get("was_in_layer") is True, remove_payload
        assert actor_layers.get("layers") == [], actor_layers
        assert actor_layers.get("layer_count") == 0, actor_layers
        assert actors_in_layer.get("layer_name") == layer_name, actors_in_layer
        assert actors_in_layer.get("actor_count") == 0, actors_in_layer
        assert (actors_in_layer.get("actors") or []) == [], actors_in_layer
        assert remove_noop.get("success") is True, remove_noop
        assert remove_noop.get("changed") is False, remove_noop
        assert remove_noop.get("was_in_layer") is False, remove_noop