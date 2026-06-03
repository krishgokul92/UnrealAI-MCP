"""Live-editor smoke tests for Phase 9g Wave 11 CurveTable asset creation."""

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


class TestPhase9gWave11CreateCurveTable:
    def test_01_create_curve_table_asset(self):
        suffix = int(time.time()) % 1000000
        destination_path = "/Game/CopilotTests/CurveTables"

        simple_name = f"CT_Phase9G_Wave11_Simple_{suffix}"
        simple_created = _ok(send_command("create_curve_table_asset", {
            "curve_table_name": simple_name,
            "curve_table_mode": "SimpleCurves",
            "destination_path": destination_path,
        }), "create_curve_table_asset_simple")

        simple_full_path = f"{destination_path}/{simple_name}.{simple_name}"
        assert simple_created.get("success") is True, simple_created
        assert simple_created.get("created") is True, simple_created
        assert simple_created.get("asset_path") == simple_full_path, simple_created
        assert simple_created.get("curve_table_name") == simple_name, simple_created
        assert simple_created.get("destination_path") == destination_path, simple_created
        assert simple_created.get("curve_table_mode") == "SimpleCurves", simple_created
        assert simple_created.get("requested_curve_table_mode") == "SimpleCurves", simple_created
        assert simple_created.get("row_count") == 0, simple_created
        assert simple_created.get("row_names") == [], simple_created
        assert simple_created.get("rows_json") == "[]", simple_created
        assert simple_created.get("rows_parsed") is True, simple_created
        assert simple_created.get("rows") == [], simple_created

        simple_readback = _ok(send_command("read_curve_table_content", {
            "asset_path": simple_full_path,
        }), "read_curve_table_content_simple")
        assert simple_readback.get("curve_table_mode") == "SimpleCurves", simple_readback
        assert simple_readback.get("row_count") == 0, simple_readback
        assert simple_readback.get("rows_json") == "[]", simple_readback

        rich_name = f"CT_Phase9G_Wave11_Rich_{suffix}"
        rich_created = _ok(send_command("create_curve_table_asset", {
            "curve_table_name": rich_name,
            "curve_table_mode": "RichCurves",
            "destination_path": destination_path,
        }), "create_curve_table_asset_rich")

        rich_full_path = f"{destination_path}/{rich_name}.{rich_name}"
        assert rich_created.get("success") is True, rich_created
        assert rich_created.get("created") is True, rich_created
        assert rich_created.get("asset_path") == rich_full_path, rich_created
        assert rich_created.get("curve_table_name") == rich_name, rich_created
        assert rich_created.get("destination_path") == destination_path, rich_created
        assert rich_created.get("curve_table_mode") == "RichCurves", rich_created
        assert rich_created.get("requested_curve_table_mode") == "RichCurves", rich_created
        assert rich_created.get("row_count") == 0, rich_created
        assert rich_created.get("row_names") == [], rich_created
        assert rich_created.get("rows_json") == "[]", rich_created
        assert rich_created.get("rows_parsed") is True, rich_created
        assert rich_created.get("rows") == [], rich_created

        rich_readback = _ok(send_command("read_curve_table_content", {
            "asset_path": rich_full_path,
        }), "read_curve_table_content_rich")
        assert rich_readback.get("curve_table_mode") == "RichCurves", rich_readback
        assert rich_readback.get("row_count") == 0, rich_readback
        assert rich_readback.get("rows_json") == "[]", rich_readback