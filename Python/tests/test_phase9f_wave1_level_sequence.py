"""Live-editor smoke tests for Phase 9f Wave 1 Level Sequence inspection."""

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


class TestPhase9fWave1LevelSequence:
    def test_01_create_and_read_level_sequence_content(self):
        suffix = int(time.time()) % 1000000
        sequence_name = f"LS_Phase9F_Wave1_{suffix}"

        create_payload = _ok(send_command("create_level_sequence", {
            "sequence_name": sequence_name,
            "destination_path": "/Game/Cinematics",
        }), "create_level_sequence")
        level_sequence_path = create_payload["level_sequence_path"]

        content = _ok(send_command("read_level_sequence_content", {
            "level_sequence_path": level_sequence_path,
        }), "read_level_sequence_content")

        assert create_payload.get("success") is True, create_payload
        assert create_payload.get("created") is True, create_payload
        assert create_payload.get("level_sequence_path") == f"/Game/Cinematics/{sequence_name}", create_payload
        assert create_payload.get("level_sequence_name") == sequence_name, create_payload
        assert create_payload.get("movie_scene_name"), create_payload

        assert content.get("success") is True, content
        assert content.get("level_sequence_path") == level_sequence_path, content
        assert content.get("level_sequence_name") == sequence_name, content
        assert content.get("binding_count") == 0, content
        assert content.get("possessable_count") == 0, content
        assert content.get("spawnable_count") == 0, content
        assert content.get("master_track_count") == 0, content
        assert content.get("marked_frame_count") == 0, content
        assert content.get("has_camera_cut_track") is False, content
        assert content.get("object_bindings") == [], content
        assert content.get("master_tracks") == [], content
        assert content.get("spawnables") == [], content
        assert content.get("possessables") == [], content
        assert content.get("display_rate", {}).get("numerator", 0) > 0, content
        assert content.get("display_rate", {}).get("denominator", 0) > 0, content
        assert content.get("tick_resolution", {}).get("numerator", 0) > 0, content
        assert content.get("tick_resolution", {}).get("denominator", 0) > 0, content