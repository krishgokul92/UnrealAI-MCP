"""Live-editor smoke tests for the third Phase 9e editor-layer slice."""

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


class TestPhase9eWave3ActorLayers:
    SUFFIX = str(int(time.time()))[-6:]

    @classmethod
    def _name(cls, base: str) -> str:
        return f"{base}_{cls.SUFFIX}"

    def test_01_get_actor_layers_on_fresh_actor(self):
        actor_name = self._name("LayerWave3_Actor")
        _spawn_cube(actor_name, [2300.0, 100.0, 80.0])

        payload = _ok(send_command("get_actor_layers", {
            "actor_name": actor_name,
        }), "get_actor_layers")

        actor = payload.get("actor") or {}
        assert actor.get("name") == actor_name, payload
        assert payload.get("layer_count") == 0, payload
        assert payload.get("layers") == [], payload
        assert isinstance(payload.get("current_level"), str) and payload.get("current_level"), payload

    def test_02_get_actors_in_layer_missing_layer_errors(self):
        layer_name = self._name("MissingLayer")
        response = send_command("get_actors_in_layer", {
            "layer_name": layer_name,
        })

        assert isinstance(response, dict), response
        assert response.get("status") == "error", response
        assert layer_name in json.dumps(response), response