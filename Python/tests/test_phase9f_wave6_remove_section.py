"""Live-editor smoke tests for Phase 9f Wave 6 section removal."""

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
SHOT_TRACK_CLASS = "/Script/MovieSceneTracks.MovieSceneCinematicShotTrack"
FIRST_START_FRAME = 10
FIRST_END_FRAME = 40
SECOND_START_FRAME = 50
SECOND_END_FRAME = 80


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


class TestPhase9fWave6RemoveSection:
	def test_01_remove_section_from_master_track_in_level_sequence(self):
		suffix = int(time.time()) % 1000000
		sequence_name = f"LS_Phase9F_Wave6_{suffix}"

		create_payload = _ok(send_command("create_level_sequence", {
			"sequence_name": sequence_name,
			"destination_path": "/Game/Cinematics",
		}), "create_level_sequence")
		level_sequence_path = create_payload["level_sequence_path"]

		track_payload = _ok(send_command("add_master_track_to_level_sequence", {
			"level_sequence_path": level_sequence_path,
			"track_class": SHOT_TRACK_CLASS,
		}), "add_master_track_to_level_sequence")

		first_section_payload = _ok(send_command("add_section_to_master_track_in_level_sequence", {
			"level_sequence_path": level_sequence_path,
			"track_class": SHOT_TRACK_CLASS,
			"start_frame": FIRST_START_FRAME,
			"end_frame": FIRST_END_FRAME,
		}), "add_section_to_master_track_in_level_sequence")

		second_section_payload = _ok(send_command("add_section_to_master_track_in_level_sequence", {
			"level_sequence_path": level_sequence_path,
			"track_class": SHOT_TRACK_CLASS,
			"start_frame": SECOND_START_FRAME,
			"end_frame": SECOND_END_FRAME,
		}), "add_section_to_master_track_in_level_sequence")

		remove_payload = _ok(send_command("remove_section_from_master_track_in_level_sequence", {
			"level_sequence_path": level_sequence_path,
			"track_class": SHOT_TRACK_CLASS,
			"section_index": 0,
		}), "remove_section_from_master_track_in_level_sequence")

		content = _ok(send_command("read_level_sequence_content", {
			"level_sequence_path": level_sequence_path,
		}), "read_level_sequence_content")

		assert track_payload.get("success") is True, track_payload
		assert first_section_payload.get("success") is True, first_section_payload
		assert second_section_payload.get("success") is True, second_section_payload
		assert remove_payload.get("success") is True, remove_payload
		assert remove_payload.get("removed_section_index") == 0, remove_payload
		assert remove_payload.get("master_track_class") == SHOT_TRACK_CLASS, remove_payload
		assert remove_payload.get("master_track_count") == 1, remove_payload
		assert len(remove_payload.get("master_tracks", [])) == 1, remove_payload
		assert remove_payload["master_tracks"][0]["section_count"] == 1, remove_payload
		assert remove_payload.get("removed_section_path") == first_section_payload.get("section_path"), remove_payload

		remaining_section = remove_payload["master_tracks"][0]["sections"][0]
		assert remaining_section["inclusive_start_frame"] == SECOND_START_FRAME, remove_payload
		assert remaining_section["exclusive_end_frame"] == SECOND_END_FRAME, remove_payload

		assert content.get("success") is True, content
		assert content.get("level_sequence_path") == level_sequence_path, content
		assert content.get("master_track_count") == 1, content
		assert len(content.get("master_tracks", [])) == 1, content
		assert content["master_tracks"][0]["section_count"] == 1, content

		content_section = content["master_tracks"][0]["sections"][0]
		assert content_section["inclusive_start_frame"] == SECOND_START_FRAME, content
		assert content_section["exclusive_end_frame"] == SECOND_END_FRAME, content