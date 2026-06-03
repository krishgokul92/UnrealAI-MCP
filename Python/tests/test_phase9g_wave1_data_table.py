"""Live-editor smoke tests for Phase 9g Wave 1 DataTable inspection."""

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


class TestPhase9gWave1DataTable:
	def test_01_read_data_table_content(self):
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

		assert content.get("asset_path") == asset_path, content
		assert content.get("name"), content
		assert content.get("row_struct_name"), content
		assert content.get("row_struct_path"), content
		assert isinstance(content.get("columns"), list), content
		assert content.get("column_count") == len(content.get("columns", [])), content
		assert content.get("column_count", 0) > 0, content
		assert content.get("row_count") == len(content.get("row_names", [])), content
		assert content.get("rows_json"), content
		if content.get("rows_parsed"):
			assert content.get("row_count") == len(content.get("rows", [])), content