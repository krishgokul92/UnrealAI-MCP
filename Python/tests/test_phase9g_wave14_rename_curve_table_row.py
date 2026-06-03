"""Live-editor smoke tests for Phase 9g Wave 14 CurveTable row rename."""

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


class TestPhase9gWave14RenameCurveTableRow:
    def test_01_rename_curve_table_row(self):
        suffix = time.time_ns()
        destination_path = "/Game/CopilotTests/CurveTables"
        curve_table_name = f"CT_Phase9G_Wave14_{suffix}"

        created = _ok(send_command("create_curve_table_asset", {
            "curve_table_name": curve_table_name,
            "curve_table_mode": "RichCurves",
            "destination_path": destination_path,
        }), "create_curve_table_asset")

        asset_path = created["asset_path"]
        old_row_name = f"Phase9GWave14_Old_{suffix}"
        new_row_name = f"Phase9GWave14_New_{suffix}"

        inserted = _ok(send_command("upsert_curve_table_row", {
            "asset_path": asset_path,
            "row_name": old_row_name,
            "row_data": {
                "0": 3.0,
                "2": 9.5,
            },
        }), "upsert_curve_table_row")
        assert inserted.get("created") is True, inserted

        renamed = _ok(send_command("rename_curve_table_row", {
            "asset_path": asset_path,
            "row_name": old_row_name,
            "new_row_name": new_row_name,
        }), "rename_curve_table_row")

        assert renamed.get("success") is True, renamed
        assert renamed.get("asset_path") == asset_path, renamed
        assert renamed.get("old_row_name") == old_row_name, renamed
        assert renamed.get("new_row_name") == new_row_name, renamed
        assert renamed.get("row_name") == new_row_name, renamed
        assert renamed.get("curve_table_mode") == "RichCurves", renamed
        assert renamed.get("renamed") is True, renamed
        assert renamed.get("changed") is True, renamed

        renamed_row = renamed.get("row")
        assert isinstance(renamed_row, dict), renamed
        assert renamed_row.get("Name") == new_row_name, renamed_row

        content = _ok(send_command("read_curve_table_content", {
            "asset_path": asset_path,
        }), "read_curve_table_content_after_rename")
        assert content.get("curve_table_mode") == "RichCurves", content
        assert content.get("row_count") == 1, content
        assert content.get("row_names") == [new_row_name], content

        missing_old = send_command("read_curve_table_row", {
            "asset_path": asset_path,
            "row_name": old_row_name,
        })
        assert isinstance(missing_old, dict), missing_old
        assert missing_old.get("status") == "error", missing_old
        assert old_row_name in json.dumps(missing_old), missing_old

        read_new = _ok(send_command("read_curve_table_row", {
            "asset_path": asset_path,
            "row_name": new_row_name,
        }), "read_curve_table_row_after_rename")
        assert read_new.get("row_name") == new_row_name, read_new
        assert read_new.get("row", {}).get("Name") == new_row_name, read_new