"""Live-editor smoke tests for Phase 9f Wave 4 master-track section mutation."""

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
START_FRAME = 10
END_FRAME = 40


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


class TestPhase9fWave4MasterTrackSections:
    def test_01_add_section_to_master_track_in_level_sequence(self):
        suffix = int(time.time()) % 1000000
        sequence_name = f"LS_Phase9F_Wave4_{suffix}"

        create_payload = _ok(send_command("create_level_sequence", {
            "sequence_name": sequence_name,
            "destination_path": "/Game/Cinematics",
        }), "create_level_sequence")
        level_sequence_path = create_payload["level_sequence_path"]

        track_payload = _ok(send_command("add_master_track_to_level_sequence", {
            "level_sequence_path": level_sequence_path,
            "track_class": SHOT_TRACK_CLASS,
        }), "add_master_track_to_level_sequence")

        add_payload = _ok(send_command("add_section_to_master_track_in_level_sequence", {
            "level_sequence_path": level_sequence_path,
            "track_class": SHOT_TRACK_CLASS,
            "start_frame": START_FRAME,
            "end_frame": END_FRAME,
        }), "add_section_to_master_track_in_level_sequence")

        content = _ok(send_command("read_level_sequence_content", {
            "level_sequence_path": level_sequence_path,
        }), "read_level_sequence_content")

        assert track_payload.get("success") is True, track_payload
        assert add_payload.get("success") is True, add_payload
        assert add_payload.get("master_track_class") == SHOT_TRACK_CLASS, add_payload
        assert add_payload.get("master_track_path"), add_payload
        assert add_payload.get("section_path"), add_payload
        assert add_payload.get("section_class"), add_payload
        assert add_payload.get("master_track_count") == 1, add_payload
        assert len(add_payload.get("master_tracks", [])) == 1, add_payload
        assert add_payload["master_tracks"][0]["section_count"] == 1, add_payload

        section = add_payload["master_tracks"][0]["sections"][0]
        assert section["has_start_frame"] is True, add_payload
        assert section["has_end_frame"] is True, add_payload
        assert section["inclusive_start_frame"] == START_FRAME, add_payload
        assert section["exclusive_end_frame"] == END_FRAME, add_payload
        assert section["range"]["lower_bound_value"] == START_FRAME, add_payload
        assert section["range"]["upper_bound_value"] == END_FRAME, add_payload
        assert section["range"]["upper_bound_inclusive"] is False, add_payload

        assert content.get("success") is True, content
        assert content.get("level_sequence_path") == level_sequence_path, content
        assert content.get("master_track_count") == 1, content
        assert len(content.get("master_tracks", [])) == 1, content
        assert content["master_tracks"][0]["section_count"] == 1, content

        content_section = content["master_tracks"][0]["sections"][0]
        assert content_section["inclusive_start_frame"] == START_FRAME, content
        assert content_section["exclusive_end_frame"] == END_FRAME, content