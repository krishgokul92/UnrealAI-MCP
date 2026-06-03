"""Live-editor smoke tests for Phase 9g Wave 4 DataTable row upsert."""

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


def _find_seed_row() -> Tuple[str, Dict[str, Any], str]:
    find_payload = _ok(send_command("find_assets", {
        "class_name": "DataTable",
        "max_results": 25,
    }), "find_assets")

    assets = find_payload.get("assets", [])
    assert assets, find_payload

    for asset in assets:
        asset_path = asset["path"]
        content = _ok(send_command("read_data_table_content", {
            "asset_path": asset_path,
        }), f"read_data_table_content({asset_path})")

        row_names = content.get("row_names", [])
        if not row_names:
            continue

        row_name = row_names[0]
        row_payload = _ok(send_command("read_data_table_row", {
            "asset_path": asset_path,
            "row_name": row_name,
        }), f"read_data_table_row({asset_path}, {row_name})")

        row = row_payload.get("row")
        if isinstance(row, dict) and row:
            return content["row_struct_path"], row, row_payload.get("key_field_name", "Name")

    pytest.skip("No populated DataTable assets available for Phase 9g Wave 4 seed data")


class TestPhase9gWave4UpsertDataTableRow:
    def test_01_upsert_data_table_row(self):
        row_struct_path, seed_row, key_field_name = _find_seed_row()

        suffix = int(time.time()) % 1000000
        data_table_name = f"DT_Phase9G_Wave4_{suffix}"
        destination_path = "/Game/CopilotTests/DataTables"

        created = _ok(send_command("create_data_table_asset", {
            "data_table_name": data_table_name,
            "row_struct_path": row_struct_path,
            "destination_path": destination_path,
        }), "create_data_table_asset")

        asset_path = created["asset_path"]
        row_name = f"Phase9GWave4_Row_{suffix}"
        row_data = dict(seed_row)
        row_data.pop("Name", None)
        if key_field_name != "Name":
            row_data.pop(key_field_name, None)

        inserted = _ok(send_command("upsert_data_table_row", {
            "asset_path": asset_path,
            "row_name": row_name,
            "row_data": row_data,
        }), "upsert_data_table_row_insert")

        assert inserted.get("success") is True, inserted
        assert inserted.get("asset_path") == asset_path, inserted
        assert inserted.get("row_name") == row_name, inserted
        assert inserted.get("row_struct_path") == row_struct_path, inserted
        assert inserted.get("created") is True, inserted
        assert inserted.get("updated") is False, inserted
        assert inserted.get("row_existed") is False, inserted

        inserted_row = inserted.get("row")
        assert isinstance(inserted_row, dict), inserted
        assert inserted_row.get(key_field_name) == row_name, inserted_row

        updated = _ok(send_command("upsert_data_table_row", {
            "asset_path": asset_path,
            "row_name": row_name,
            "row_data": row_data,
        }), "upsert_data_table_row_update")

        assert updated.get("success") is True, updated
        assert updated.get("created") is False, updated
        assert updated.get("updated") is True, updated
        assert updated.get("row_existed") is True, updated

        content = _ok(send_command("read_data_table_content", {
            "asset_path": asset_path,
        }), "read_data_table_content_after_upsert")

        assert content.get("row_count") == 1, content
        assert content.get("row_names") == [row_name], content