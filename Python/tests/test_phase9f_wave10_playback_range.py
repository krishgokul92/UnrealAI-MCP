"""Live-editor smoke tests for Phase 9f Wave 10 playback-range mutation."""

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
START_FRAME = 100
END_FRAME = 220


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


class TestPhase9fWave10PlaybackRange:
	def test_01_set_level_sequence_playback_range(self):
		suffix = int(time.time()) % 1000000
		sequence_name = f"LS_Phase9F_Wave10_{suffix}"

		create_payload = _ok(send_command("create_level_sequence", {
			"sequence_name": sequence_name,
			"destination_path": "/Game/Cinematics",
		}), "create_level_sequence")
		level_sequence_path = create_payload["level_sequence_path"]

		playback_payload = _ok(send_command("set_level_sequence_playback_range", {
			"level_sequence_path": level_sequence_path,
			"start_frame": START_FRAME,
			"end_frame": END_FRAME,
		}), "set_level_sequence_playback_range")

		content = _ok(send_command("read_level_sequence_content", {
			"level_sequence_path": level_sequence_path,
		}), "read_level_sequence_content")

		assert playback_payload.get("success") is True, playback_payload
		assert playback_payload.get("start_frame") == START_FRAME, playback_payload
		assert playback_payload.get("end_frame") == END_FRAME, playback_payload
		assert playback_payload["playback_range"]["lower_bound_value"] == START_FRAME, playback_payload
		assert playback_payload["playback_range"]["upper_bound_value"] == END_FRAME, playback_payload

		assert content.get("success") is True, content
		assert content["playback_range"]["lower_bound_value"] == START_FRAME, content
		assert content["playback_range"]["upper_bound_value"] == END_FRAME, content