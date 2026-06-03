"""Live-editor smoke tests for the twelfth Phase 9e world-partition inspection slice."""

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
            _ok(send_command("new_blank_map", {
                "save_existing_map": False,
            }), "new_blank_map")
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


def test_01_get_world_partition_info_reports_blank_world_state():
    payload = _ok(send_command("get_world_partition_info", {}), "get_world_partition_info")

    assert payload.get("success") is True, payload
    assert isinstance(payload.get("is_partitioned_world"), bool), payload
    assert isinstance(payload.get("world_partition_subsystem_available"), bool), payload
    assert isinstance(payload.get("is_all_streaming_completed"), bool), payload
    assert isinstance(payload.get("current_level"), str) and payload.get("current_level"), payload

    if payload.get("is_partitioned_world"):
        assert payload.get("world_partition_name"), payload
        assert payload.get("world_partition_path"), payload
        assert payload.get("world_partition_class"), payload
    else:
        assert payload.get("world_partition_name") == "", payload
        assert payload.get("world_partition_path") == "", payload
        assert payload.get("world_partition_class") == "", payload