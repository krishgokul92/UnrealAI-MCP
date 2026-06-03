"""Live-editor smoke tests for Phase 9f Wave 7 actor possessable bindings."""

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


class TestPhase9fWave7ActorPossessable:
	def test_01_add_actor_possessable_to_level_sequence(self):
		suffix = int(time.time()) % 1000000
		sequence_name = f"LS_Phase9F_Wave7_{suffix}"
		actor_name = f"Wave7SequencerActor_{suffix}"

		_spawn_cube(actor_name, [1400.0, 150.0, 120.0])

		create_payload = _ok(send_command("create_level_sequence", {
			"sequence_name": sequence_name,
			"destination_path": "/Game/Cinematics",
		}), "create_level_sequence")
		level_sequence_path = create_payload["level_sequence_path"]

		add_payload = _ok(send_command("add_actor_possessable_to_level_sequence", {
			"level_sequence_path": level_sequence_path,
			"actor_name": actor_name,
		}), "add_actor_possessable_to_level_sequence")

		repeat_payload = _ok(send_command("add_actor_possessable_to_level_sequence", {
			"level_sequence_path": level_sequence_path,
			"actor_name": actor_name,
		}), "add_actor_possessable_to_level_sequence_repeat")

		content = _ok(send_command("read_level_sequence_content", {
			"level_sequence_path": level_sequence_path,
		}), "read_level_sequence_content")

		assert add_payload.get("success") is True, add_payload
		assert add_payload.get("created") is True, add_payload
		assert add_payload.get("actor_name") == actor_name, add_payload
		assert add_payload.get("actor_label"), add_payload
		assert add_payload.get("binding_guid"), add_payload
		assert add_payload.get("binding_name") == add_payload.get("actor_label"), add_payload
		assert add_payload.get("binding_count") == 1, add_payload
		assert add_payload.get("possessable_count") == 1, add_payload
		assert len(add_payload.get("object_bindings", [])) == 1, add_payload
		assert add_payload["object_bindings"][0]["binding_kind"] == "possessable", add_payload
		assert add_payload["object_bindings"][0]["name"] == add_payload.get("binding_name"), add_payload

		assert repeat_payload.get("success") is True, repeat_payload
		assert repeat_payload.get("created") is False, repeat_payload
		assert repeat_payload.get("binding_guid") == add_payload.get("binding_guid"), repeat_payload
		assert repeat_payload.get("binding_count") == 1, repeat_payload
		assert repeat_payload.get("possessable_count") == 1, repeat_payload

		assert content.get("success") is True, content
		assert content.get("binding_count") == 1, content
		assert content.get("possessable_count") == 1, content
		assert len(content.get("object_bindings", [])) == 1, content
		assert content["object_bindings"][0]["binding_kind"] == "possessable", content
		assert content["object_bindings"][0]["name"] == add_payload.get("binding_name"), content