"""Live-editor smoke tests for Phase 9g Wave 12 CurveTable row upsert."""

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


class TestPhase9gWave12UpsertCurveTableRow:
    def test_01_upsert_curve_table_row(self):
        suffix = time.time_ns()
        destination_path = "/Game/CopilotTests/CurveTables"

        simple_name = f"CT_Phase9G_Wave12_Simple_{suffix}"
        simple_created = _ok(send_command("create_curve_table_asset", {
            "curve_table_name": simple_name,
            "curve_table_mode": "SimpleCurves",
            "destination_path": destination_path,
        }), "create_curve_table_asset_simple")
        simple_asset_path = simple_created["asset_path"]

        simple_row_name = f"Phase9GWave12_Simple_Row_{suffix}"
        simple_inserted = _ok(send_command("upsert_curve_table_row", {
            "asset_path": simple_asset_path,
            "row_name": simple_row_name,
            "row_data": {
                "0": 1.5,
                "1.5": 2.75,
            },
        }), "upsert_curve_table_row_simple_insert")

        assert simple_inserted.get("success") is True, simple_inserted
        assert simple_inserted.get("asset_path") == simple_asset_path, simple_inserted
        assert simple_inserted.get("row_name") == simple_row_name, simple_inserted
        assert simple_inserted.get("curve_table_mode") == "SimpleCurves", simple_inserted
        assert simple_inserted.get("created") is True, simple_inserted
        assert simple_inserted.get("updated") is False, simple_inserted
        assert simple_inserted.get("row_existed") is False, simple_inserted
        assert simple_inserted.get("key_count") == 2, simple_inserted

        simple_row = simple_inserted.get("row")
        assert isinstance(simple_row, dict), simple_inserted
        assert simple_row.get("Name") == simple_row_name, simple_row
        assert simple_row.get("0") == pytest.approx(1.5), simple_row
        assert simple_row.get("1.5") == pytest.approx(2.75), simple_row

        simple_updated = _ok(send_command("upsert_curve_table_row", {
            "asset_path": simple_asset_path,
            "row_name": simple_row_name,
            "row_data": {
                "Name": simple_row_name,
                "0.25": 4.0,
                "2": 8.5,
            },
        }), "upsert_curve_table_row_simple_update")

        assert simple_updated.get("success") is True, simple_updated
        assert simple_updated.get("curve_table_mode") == "SimpleCurves", simple_updated
        assert simple_updated.get("created") is False, simple_updated
        assert simple_updated.get("updated") is True, simple_updated
        assert simple_updated.get("row_existed") is True, simple_updated
        assert simple_updated.get("key_count") == 2, simple_updated

        simple_updated_row = simple_updated.get("row")
        assert simple_updated_row.get("Name") == simple_row_name, simple_updated_row
        assert simple_updated_row.get("0.25") == pytest.approx(4.0), simple_updated_row
        assert simple_updated_row.get("2") == pytest.approx(8.5), simple_updated_row
        assert "0" not in simple_updated_row, simple_updated_row

        simple_content = _ok(send_command("read_curve_table_content", {
            "asset_path": simple_asset_path,
        }), "read_curve_table_content_simple_after_upsert")
        assert simple_content.get("curve_table_mode") == "SimpleCurves", simple_content
        assert simple_content.get("row_count") == 1, simple_content
        assert simple_content.get("row_names") == [simple_row_name], simple_content

        rich_name = f"CT_Phase9G_Wave12_Rich_{suffix}"
        rich_created = _ok(send_command("create_curve_table_asset", {
            "curve_table_name": rich_name,
            "curve_table_mode": "RichCurves",
            "destination_path": destination_path,
        }), "create_curve_table_asset_rich")
        rich_asset_path = rich_created["asset_path"]

        rich_row_name = f"Phase9GWave12_Rich_Row_{suffix}"
        rich_inserted = _ok(send_command("upsert_curve_table_row", {
            "asset_path": rich_asset_path,
            "row_name": rich_row_name,
            "row_data": {
                "0": 3.0,
                "2": 9.5,
            },
        }), "upsert_curve_table_row_rich_insert")

        assert rich_inserted.get("success") is True, rich_inserted
        assert rich_inserted.get("asset_path") == rich_asset_path, rich_inserted
        assert rich_inserted.get("row_name") == rich_row_name, rich_inserted
        assert rich_inserted.get("curve_table_mode") == "RichCurves", rich_inserted
        assert rich_inserted.get("created") is True, rich_inserted
        assert rich_inserted.get("updated") is False, rich_inserted
        assert rich_inserted.get("row_existed") is False, rich_inserted
        assert rich_inserted.get("key_count") == 2, rich_inserted

        rich_row = rich_inserted.get("row")
        assert isinstance(rich_row, dict), rich_inserted
        assert rich_row.get("Name") == rich_row_name, rich_row
        assert rich_row.get("0") == pytest.approx(3.0), rich_row
        assert rich_row.get("2") == pytest.approx(9.5), rich_row

        rich_content = _ok(send_command("read_curve_table_content", {
            "asset_path": rich_asset_path,
        }), "read_curve_table_content_rich_after_upsert")
        assert rich_content.get("curve_table_mode") == "RichCurves", rich_content
        assert rich_content.get("row_count") == 1, rich_content
        assert rich_content.get("row_names") == [rich_row_name], rich_content