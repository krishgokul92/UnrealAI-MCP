"""Live-editor smoke tests for Phase 9g Wave 15 table validation helpers."""

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

    pytest.skip("No populated DataTable assets available for Phase 9g Wave 15 seed data")


def _make_seed_row_data(seed_row: Dict[str, Any], key_field_name: str) -> Dict[str, Any]:
    row_data = dict(seed_row)
    row_data.pop("Name", None)
    if key_field_name != "Name":
        row_data.pop(key_field_name, None)
    return row_data


def _issue_codes(result: Dict[str, Any]) -> set[str]:
    codes: set[str] = set()
    for issue in result.get("issues", []):
        if isinstance(issue, dict) and isinstance(issue.get("code"), str):
            codes.add(issue["code"])
    return codes


class TestPhase9gWave15TableValidation:
    def test_01_validate_table_row_import_helpers(self):
        row_struct_path, seed_row, key_field_name = _find_seed_row()

        suffix = time.time_ns()
        data_table_name = f"DT_Phase9G_Wave15_{suffix}"
        data_table_destination = "/Game/CopilotTests/DataTables"

        created_data_table = _ok(send_command("create_data_table_asset", {
            "data_table_name": data_table_name,
            "row_struct_path": row_struct_path,
            "destination_path": data_table_destination,
        }), "create_data_table_asset")
        data_table_asset_path = created_data_table["asset_path"]

        data_table_row_name = f"Phase9GWave15_Row_{suffix}"
        valid_data_table_row = _make_seed_row_data(seed_row, key_field_name)

        validated_data_table = _ok(send_command("validate_data_table_row_import", {
            "asset_path": data_table_asset_path,
            "row_name": data_table_row_name,
            "row_data": valid_data_table_row,
        }), "validate_data_table_row_import_valid")

        assert validated_data_table.get("success") is True, validated_data_table
        assert validated_data_table.get("is_valid") is True, validated_data_table
        assert validated_data_table.get("row_struct_path") == row_struct_path, validated_data_table
        assert validated_data_table.get("key_field_name") == key_field_name, validated_data_table

        normalized_data_table_row = validated_data_table.get("normalized_row_data")
        assert isinstance(normalized_data_table_row, dict), validated_data_table
        if key_field_name != "Name":
            assert normalized_data_table_row.get(key_field_name) == data_table_row_name, normalized_data_table_row
            assert "key_field_will_be_injected" in _issue_codes(validated_data_table), validated_data_table
        else:
            assert validated_data_table.get("warning_count") == 0, validated_data_table

        invalid_result = None
        invalid_codes: set[str] = set()
        if key_field_name != "Name":
            invalid_row_data = dict(valid_data_table_row)
            invalid_row_data[key_field_name] = f"Wrong_{suffix}"
            invalid_result = _ok(send_command("validate_data_table_row_import", {
                "asset_path": data_table_asset_path,
                "row_name": data_table_row_name,
                "row_data": invalid_row_data,
            }), "validate_data_table_row_import_invalid_key")
            invalid_codes = _issue_codes(invalid_result)
            assert "key_field_mismatch" in invalid_codes, invalid_result
        else:
            invalid_row_data = dict(valid_data_table_row)
            invalid_row_data["__phase9g_invalid_field__"] = 123
            invalid_result = _ok(send_command("validate_data_table_row_import", {
                "asset_path": data_table_asset_path,
                "row_name": data_table_row_name,
                "row_data": invalid_row_data,
            }), "validate_data_table_row_import_invalid_field")
            invalid_codes = _issue_codes(invalid_result)
            assert "unknown_field" in invalid_codes, invalid_result

        assert invalid_result is not None
        assert invalid_result.get("success") is True, invalid_result
        assert invalid_result.get("is_valid") is False, invalid_result
        assert invalid_result.get("error_count", 0) >= 1, invalid_result

        curve_table_name = f"CT_Phase9G_Wave15_{suffix}"
        curve_table_destination = "/Game/CopilotTests/CurveTables"
        created_curve_table = _ok(send_command("create_curve_table_asset", {
            "curve_table_name": curve_table_name,
            "curve_table_mode": "SimpleCurves",
            "destination_path": curve_table_destination,
        }), "create_curve_table_asset")
        curve_table_asset_path = created_curve_table["asset_path"]

        curve_row_name = f"Phase9GWave15_Curve_{suffix}"
        validated_curve_table = _ok(send_command("validate_curve_table_row_import", {
            "asset_path": curve_table_asset_path,
            "row_name": curve_row_name,
            "row_data": {
                "0": 1.5,
                "2.25": 4.0,
            },
        }), "validate_curve_table_row_import_valid")

        assert validated_curve_table.get("success") is True, validated_curve_table
        assert validated_curve_table.get("is_valid") is True, validated_curve_table
        assert validated_curve_table.get("curve_table_mode") == "SimpleCurves", validated_curve_table
        assert validated_curve_table.get("key_field_name") == "Name", validated_curve_table
        assert validated_curve_table.get("key_count") == 2, validated_curve_table
        normalized_curve_row = validated_curve_table.get("normalized_row_data")
        assert isinstance(normalized_curve_row, dict), validated_curve_table
        assert normalized_curve_row.get("Name") == curve_row_name, normalized_curve_row
        assert normalized_curve_row.get("0") == pytest.approx(1.5), normalized_curve_row
        assert normalized_curve_row.get("2.25") == pytest.approx(4.0), normalized_curve_row
        assert "row_name_will_be_injected" in _issue_codes(validated_curve_table), validated_curve_table

        invalid_curve_table = _ok(send_command("validate_curve_table_row_import", {
            "asset_path": curve_table_asset_path,
            "row_name": curve_row_name,
            "row_data": {
                "not_a_time": 1.0,
            },
        }), "validate_curve_table_row_import_invalid")

        assert invalid_curve_table.get("success") is True, invalid_curve_table
        assert invalid_curve_table.get("is_valid") is False, invalid_curve_table
        assert invalid_curve_table.get("error_count", 0) >= 1, invalid_curve_table
        assert "invalid_curve_time" in _issue_codes(invalid_curve_table), invalid_curve_table