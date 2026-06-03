"""Live-editor smoke tests for Phase 9g Wave 13 CurveTable row delete."""

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


class TestPhase9gWave13DeleteCurveTableRow:
    def test_01_delete_curve_table_row(self):
        suffix = time.time_ns()
        destination_path = "/Game/CopilotTests/CurveTables"
        curve_table_name = f"CT_Phase9G_Wave13_{suffix}"

        created = _ok(send_command("create_curve_table_asset", {
            "curve_table_name": curve_table_name,
            "curve_table_mode": "SimpleCurves",
            "destination_path": destination_path,
        }), "create_curve_table_asset")

        asset_path = created["asset_path"]
        row_name = f"Phase9GWave13_Row_{suffix}"

        inserted = _ok(send_command("upsert_curve_table_row", {
            "asset_path": asset_path,
            "row_name": row_name,
            "row_data": {
                "0": 1.0,
                "1.5": 2.5,
            },
        }), "upsert_curve_table_row")
        assert inserted.get("created") is True, inserted

        deleted = _ok(send_command("delete_curve_table_row", {
            "asset_path": asset_path,
            "row_name": row_name,
        }), "delete_curve_table_row")

        assert deleted.get("success") is True, deleted
        assert deleted.get("asset_path") == asset_path, deleted
        assert deleted.get("row_name") == row_name, deleted
        assert deleted.get("deleted") is True, deleted
        assert deleted.get("curve_table_mode") == "SimpleCurves", deleted
        assert deleted.get("row_count") == 0, deleted
        assert deleted.get("row_names") == [], deleted
        assert deleted.get("rows_json") == "[]", deleted
        assert deleted.get("rows") == [], deleted

        missing = send_command("read_curve_table_row", {
            "asset_path": asset_path,
            "row_name": row_name,
        })
        assert isinstance(missing, dict), missing
        assert missing.get("status") == "error", missing
        assert row_name in json.dumps(missing), missing