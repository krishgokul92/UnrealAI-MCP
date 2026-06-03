"""Live-editor smoke tests for Phase 9g Wave 10 CurveTable row inspection."""

from __future__ import annotations

import json
import sys
import time
from pathlib import Path
from typing import Any, Dict, Tuple

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


def _find_nonempty_curve_table_asset() -> Tuple[str, Dict[str, Any]]:
    for root in ("/Game", "/Engine"):
        find_payload = _ok(send_command("find_assets", {
            "class_name": "CurveTable",
            "path": root,
            "max_results": 25,
        }), f"find_assets({root})")

        for asset in find_payload.get("assets", []):
            asset_path = asset["path"]
            content = _ok(send_command("read_curve_table_content", {
                "asset_path": asset_path,
            }), f"read_curve_table_content({asset_path})")
            if content.get("row_names"):
                return asset_path, content

    pytest.skip("No populated CurveTable assets available under /Game or /Engine for Phase 9g Wave 10")


class TestPhase9gWave10CurveTableRow:
    def test_01_read_curve_table_row(self):
        asset_path, content = _find_nonempty_curve_table_asset()

        row_names = content.get("row_names", [])
        assert row_names, content
        row_name = row_names[0]

        row_payload = _ok(send_command("read_curve_table_row", {
            "asset_path": asset_path,
            "row_name": row_name,
        }), "read_curve_table_row")

        assert row_payload.get("asset_path") == asset_path, row_payload
        assert row_payload.get("row_name") == row_name, row_payload
        assert row_payload.get("name"), row_payload
        assert row_payload.get("key_field_name") == "Name", row_payload
        assert row_payload.get("curve_table_mode") == content.get("curve_table_mode"), row_payload
        assert row_payload.get("row_json"), row_payload
        assert isinstance(row_payload.get("row"), dict), row_payload
        assert row_payload["row"][row_payload["key_field_name"]] == row_name, row_payload

        if content.get("rows_parsed"):
            matching_content_rows = [
                row for row in content.get("rows", [])
                if isinstance(row, dict) and row.get(row_payload["key_field_name"]) == row_name
            ]
            assert len(matching_content_rows) == 1, content
            assert matching_content_rows[0] == row_payload["row"], row_payload