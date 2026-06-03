"""Live-editor smoke tests for Phase 9f Wave 3 generic master-track mutation."""

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


class TestPhase9fWave3MasterTrack:
    def test_01_add_master_track_to_level_sequence(self):
        suffix = int(time.time()) % 1000000
        sequence_name = f"LS_Phase9F_Wave3_{suffix}"

        create_payload = _ok(send_command("create_level_sequence", {
            "sequence_name": sequence_name,
            "destination_path": "/Game/Cinematics",
        }), "create_level_sequence")
        level_sequence_path = create_payload["level_sequence_path"]

        add_payload = _ok(send_command("add_master_track_to_level_sequence", {
            "level_sequence_path": level_sequence_path,
            "track_class": SHOT_TRACK_CLASS,
        }), "add_master_track_to_level_sequence")

        second_add_payload = _ok(send_command("add_master_track_to_level_sequence", {
            "level_sequence_path": level_sequence_path,
            "track_class": SHOT_TRACK_CLASS,
        }), "add_master_track_to_level_sequence second call")

        content = _ok(send_command("read_level_sequence_content", {
            "level_sequence_path": level_sequence_path,
        }), "read_level_sequence_content")

        assert add_payload.get("success") is True, add_payload
        assert add_payload.get("created") is True, add_payload
        assert add_payload.get("has_camera_cut_track") is False, add_payload
        assert add_payload.get("master_track_count") == 1, add_payload
        assert len(add_payload.get("master_tracks", [])) == 1, add_payload
        assert add_payload.get("master_track_class") == SHOT_TRACK_CLASS, add_payload
        assert add_payload.get("master_track_path"), add_payload
        assert add_payload["master_tracks"][0]["class"] == "MovieSceneCinematicShotTrack", add_payload
        assert add_payload["master_tracks"][0]["section_count"] == 0, add_payload

        assert second_add_payload.get("success") is True, second_add_payload
        assert second_add_payload.get("created") is False, second_add_payload
        assert second_add_payload.get("master_track_count") == 1, second_add_payload

        assert content.get("success") is True, content
        assert content.get("level_sequence_path") == level_sequence_path, content
        assert content.get("has_camera_cut_track") is False, content
        assert content.get("master_track_count") == 1, content
        assert len(content.get("master_tracks", [])) == 1, content
        assert content["master_tracks"][0]["class"] == "MovieSceneCinematicShotTrack", content
        assert content["master_tracks"][0]["section_count"] == 0, content