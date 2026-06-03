"""Live-editor smoke tests for Phase 9g Wave 8 DataTable row move."""

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

    pytest.skip("No populated DataTable assets available for Phase 9g Wave 8 seed data")


class TestPhase9gWave8MoveDataTableRow:
    def test_01_move_data_table_row(self):
        row_struct_path, seed_row, key_field_name = _find_seed_row()

        suffix = int(time.time()) % 1000000
        data_table_name = f"DT_Phase9G_Wave8_{suffix}"
        destination_path = "/Game/CopilotTests/DataTables"

        created = _ok(send_command("create_data_table_asset", {
            "data_table_name": data_table_name,
            "row_struct_path": row_struct_path,
            "destination_path": destination_path,
        }), "create_data_table_asset")

        asset_path = created["asset_path"]
        row_names = [
            f"Phase9GWave8_A_{suffix}",
            f"Phase9GWave8_B_{suffix}",
            f"Phase9GWave8_C_{suffix}",
        ]

        base_row = dict(seed_row)
        base_row.pop("Name", None)
        if key_field_name != "Name":
            base_row.pop(key_field_name, None)

        for row_name in row_names:
            inserted = _ok(send_command("upsert_data_table_row", {
                "asset_path": asset_path,
                "row_name": row_name,
                "row_data": dict(base_row),
            }), f"upsert_data_table_row({row_name})")
            assert inserted.get("created") is True, inserted

        initial = _ok(send_command("read_data_table_content", {
            "asset_path": asset_path,
        }), "read_data_table_content_before_move")
        assert initial.get("row_names") == row_names, initial

        moved = _ok(send_command("move_data_table_row", {
            "asset_path": asset_path,
            "row_name": row_names[2],
            "direction": "up",
            "num_rows_to_move_by": 2,
        }), "move_data_table_row")

        assert moved.get("success") is True, moved
        assert moved.get("asset_path") == asset_path, moved
        assert moved.get("row_name") == row_names[2], moved
        assert moved.get("direction") == "up", moved
        assert moved.get("num_rows_to_move_by") == 2, moved
        assert moved.get("row_index_before") == 2, moved
        assert moved.get("row_index_after") == 0, moved
        assert moved.get("moved") is True, moved

        expected_order = [row_names[2], row_names[0], row_names[1]]
        assert moved.get("row_names") == expected_order, moved

        readback = _ok(send_command("read_data_table_content", {
            "asset_path": asset_path,
        }), "read_data_table_content_after_move")
        assert readback.get("row_names") == expected_order, readback
        assert readback.get("row_count") == 3, readback