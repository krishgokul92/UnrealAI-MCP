"""Live-editor smoke tests for Phase 9f Wave 9 float-key mutation."""

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
FLOAT_TRACK_CLASS = "/Script/MovieSceneTracks.MovieSceneFloatTrack"
PROPERTY_NAME = "CustomTimeDilation"
PROPERTY_PATH = "CustomTimeDilation"
SECTION_START_FRAME = 0
SECTION_END_FRAME = 60
KEY_FRAME = 25
INITIAL_VALUE = 1.25
UPDATED_VALUE = 2.5


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


class TestPhase9fWave9FloatKeys:
	def test_01_add_float_key_to_binding_track_in_level_sequence(self):
		suffix = int(time.time()) % 1000000
		sequence_name = f"LS_Phase9F_Wave9_{suffix}"
		actor_name = f"Wave9SequencerActor_{suffix}"

		_spawn_cube(actor_name, [1700.0, 350.0, 120.0])

		create_payload = _ok(send_command("create_level_sequence", {
			"sequence_name": sequence_name,
			"destination_path": "/Game/Cinematics",
		}), "create_level_sequence")
		level_sequence_path = create_payload["level_sequence_path"]

		binding_payload = _ok(send_command("add_actor_possessable_to_level_sequence", {
			"level_sequence_path": level_sequence_path,
			"actor_name": actor_name,
		}), "add_actor_possessable_to_level_sequence")
		binding_guid = binding_payload["binding_guid"]

		track_payload = _ok(send_command("add_track_to_binding_in_level_sequence", {
			"level_sequence_path": level_sequence_path,
			"binding_guid": binding_guid,
			"track_class": FLOAT_TRACK_CLASS,
		}), "add_track_to_binding_in_level_sequence")

		first_key_payload = _ok(send_command("add_float_key_to_binding_track_in_level_sequence", {
			"level_sequence_path": level_sequence_path,
			"binding_guid": binding_guid,
			"property_name": PROPERTY_NAME,
			"property_path": PROPERTY_PATH,
			"frame": KEY_FRAME,
			"value": INITIAL_VALUE,
			"section_start_frame": SECTION_START_FRAME,
			"section_end_frame": SECTION_END_FRAME,
			"interpolation": "linear",
		}), "add_float_key_to_binding_track_in_level_sequence")

		second_key_payload = _ok(send_command("add_float_key_to_binding_track_in_level_sequence", {
			"level_sequence_path": level_sequence_path,
			"binding_guid": binding_guid,
			"property_name": PROPERTY_NAME,
			"property_path": PROPERTY_PATH,
			"frame": KEY_FRAME,
			"value": UPDATED_VALUE,
			"section_start_frame": SECTION_START_FRAME,
			"section_end_frame": SECTION_END_FRAME,
			"interpolation": "linear",
		}), "add_float_key_to_binding_track_in_level_sequence_repeat")

		content = _ok(send_command("read_level_sequence_content", {
			"level_sequence_path": level_sequence_path,
		}), "read_level_sequence_content")

		assert track_payload.get("success") is True, track_payload
		assert first_key_payload.get("success") is True, first_key_payload
		assert first_key_payload.get("section_created") is True, first_key_payload
		assert first_key_payload.get("key_created") is True, first_key_payload
		assert first_key_payload.get("property_name") == PROPERTY_NAME, first_key_payload
		assert first_key_payload.get("property_path") == PROPERTY_PATH, first_key_payload
		assert len(first_key_payload.get("object_bindings", [])) == 1, first_key_payload
		binding_track = first_key_payload["object_bindings"][0]["tracks"][0]
		assert binding_track.get("property_name") == PROPERTY_NAME, first_key_payload
		assert binding_track.get("property_path") == PROPERTY_PATH, first_key_payload
		assert binding_track.get("section_count") == 1, first_key_payload
		section = binding_track["sections"][0]
		assert section.get("float_key_count") == 1, first_key_payload
		assert section["float_keys"][0]["frame"] == KEY_FRAME, first_key_payload
		assert section["float_keys"][0]["value"] == INITIAL_VALUE, first_key_payload

		assert second_key_payload.get("success") is True, second_key_payload
		assert second_key_payload.get("section_created") is False, second_key_payload
		assert second_key_payload.get("key_created") is False, second_key_payload
		updated_section = second_key_payload["object_bindings"][0]["tracks"][0]["sections"][0]
		assert updated_section.get("float_key_count") == 1, second_key_payload
		assert updated_section["float_keys"][0]["frame"] == KEY_FRAME, second_key_payload
		assert updated_section["float_keys"][0]["value"] == UPDATED_VALUE, second_key_payload

		assert content.get("success") is True, content
		assert len(content.get("object_bindings", [])) == 1, content
		content_track = content["object_bindings"][0]["tracks"][0]
		assert content_track.get("property_name") == PROPERTY_NAME, content
		assert content_track.get("property_path") == PROPERTY_PATH, content
		assert content_track.get("section_count") == 1, content
		content_section = content_track["sections"][0]
		assert content_section.get("float_key_count") == 1, content
		assert content_section["float_keys"][0]["frame"] == KEY_FRAME, content
		assert content_section["float_keys"][0]["value"] == UPDATED_VALUE, content