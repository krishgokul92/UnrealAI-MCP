"""Live-editor smoke tests for Phase 9g Wave 3 DataTable asset creation."""

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


class TestPhase9gWave3CreateDataTable:
	def test_01_create_data_table_asset(self):
		find_payload = _ok(send_command("find_assets", {
			"class_name": "DataTable",
			"max_results": 10,
		}), "find_assets")

		assets = find_payload.get("assets", [])
		assert assets, find_payload

		asset_path = next(
			(asset["path"] for asset in assets if asset.get("name") == "DT_WeaponData"),
			assets[0]["path"],
		)

		content = _ok(send_command("read_data_table_content", {
			"asset_path": asset_path,
		}), "read_data_table_content")

		row_struct_path = content.get("row_struct_path")
		assert row_struct_path, content

		suffix = int(time.time()) % 1000000
		data_table_name = f"DT_Phase9G_Wave3_{suffix}"
		destination_path = "/Game/CopilotTests/DataTables"

		created = _ok(send_command("create_data_table_asset", {
			"data_table_name": data_table_name,
			"row_struct_path": row_struct_path,
			"destination_path": destination_path,
		}), "create_data_table_asset")

		full_path = f"{destination_path}/{data_table_name}.{data_table_name}"
		assert created.get("success") is True, created
		assert created.get("created") is True, created
		assert created.get("asset_path") == full_path, created
		assert created.get("data_table_name") == data_table_name, created
		assert created.get("destination_path") == destination_path, created
		assert created.get("row_struct_path") == row_struct_path, created
		assert created.get("row_count") == 0, created
		assert created.get("column_count") == content.get("column_count"), created
		assert created.get("rows_json") == "[]", created
		assert created.get("rows") == [], created

		readback = _ok(send_command("read_data_table_content", {
			"asset_path": full_path,
		}), "read_data_table_content_created")

		assert readback.get("asset_path") == full_path, readback
		assert readback.get("row_struct_path") == row_struct_path, readback
		assert readback.get("row_count") == 0, readback