"""Live-editor smoke tests for the second Phase 9e editor-selection slice."""

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


class TestPhase9eWave2EditorSelection:
    SUFFIX = str(int(time.time()))[-6:]

    @classmethod
    def _name(cls, base: str) -> str:
        return f"{base}_{cls.SUFFIX}"

    def test_01_select_actors_replace_and_append(self):
        actor_a = self._name("SelectWave2_A")
        actor_b = self._name("SelectWave2_B")
        _spawn_cube(actor_a, [1400.0, 100.0, 80.0])
        _spawn_cube(actor_b, [1700.0, 100.0, 80.0])

        _ok(send_command("clear_actor_selection", {}), "clear_actor_selection setup")

        replace_payload = _ok(send_command("select_actors", {
            "actor_names": [actor_a],
            "replace_selection": True,
        }), "select_actors replace")
        selected_after_replace = _ok(send_command("get_selected_actors", {}), "get_selected_actors after replace")

        assert replace_payload.get("success") is True, replace_payload
        assert replace_payload.get("replace_selection") is True, replace_payload
        assert replace_payload.get("selected_count") == 1, replace_payload
        assert [actor.get("name") for actor in selected_after_replace.get("actors") or []] == [actor_a], selected_after_replace

        append_payload = _ok(send_command("select_actors", {
            "actor_names": [actor_b],
            "replace_selection": False,
        }), "select_actors append")
        selected_after_append = _ok(send_command("get_selected_actors", {}), "get_selected_actors after append")

        selected_names = sorted(actor.get("name") for actor in selected_after_append.get("actors") or [])
        assert append_payload.get("success") is True, append_payload
        assert append_payload.get("replace_selection") is False, append_payload
        assert append_payload.get("selected_count") == 1, append_payload
        assert selected_after_append.get("selected_count") == 2, selected_after_append
        assert selected_names == sorted([actor_a, actor_b]), selected_after_append

    def test_02_clear_actor_selection(self):
        actor_name = self._name("SelectWave2_Clear")
        _spawn_cube(actor_name, [2000.0, 100.0, 80.0])
        _ok(send_command("select_actors", {
            "actor_names": [actor_name],
            "replace_selection": True,
        }), "select_actors before clear")

        clear_payload = _ok(send_command("clear_actor_selection", {}), "clear_actor_selection")
        selected_after_clear = _ok(send_command("get_selected_actors", {}), "get_selected_actors after clear")

        assert clear_payload.get("success") is True, clear_payload
        assert clear_payload.get("previous_selected_count", 0) >= 1, clear_payload
        assert clear_payload.get("selected_count") == 0, clear_payload
        assert selected_after_clear.get("selected_count") == 0, selected_after_clear
        assert (selected_after_clear.get("actors") or []) == [], selected_after_clear