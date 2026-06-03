"""Phase 7 Wave 3b smoke tests — initial landscape helpers.

Tools exercised:
    get_landscapes, create_landscape, set_landscape_flat_height,
    import_landscape_heightmap

This file is scaffolded for the next live validation run. It is not executed in
this turn because the user asked not to rerun tests here.
"""

from __future__ import annotations

import json
import struct
import sys
import time
from pathlib import Path
from typing import Any, Dict

import pytest

_PY_ROOT = Path(__file__).resolve().parent.parent
if str(_PY_ROOT) not in sys.path:
    sys.path.insert(0, str(_PY_ROOT))

from ue_bridge import ping, send_command  # noqa: E402

BRIDGE_AVAILABLE = ping()
SKIP_REASON = "UE bridge not reachable on 127.0.0.1:55557 (open the editor first)"
needs_bridge = pytest.mark.skipif(not BRIDGE_AVAILABLE, reason=SKIP_REASON)


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


@needs_bridge
class TestPhase7Wave3bLandscape:
    """End-to-end tests for the first landscape helper slice."""

    SUFFIX = str(int(time.time()))[-6:]

    @classmethod
    def _name(cls, base: str) -> str:
        return f"{base}_{cls.SUFFIX}"

    @classmethod
    def _landscape_name(cls) -> str:
        return cls._name("Landscape_W3b")

    def test_01_get_landscapes(self):
        payload = _ok(send_command("get_landscapes", {}), "get_landscapes")
        assert isinstance(payload.get("landscapes"), list), payload
        assert payload.get("count", -1) >= 0, payload

    def test_02_create_landscape(self):
        payload = _ok(send_command("create_landscape", {
            "name": self._landscape_name(),
            "location": [4000.0, 0.0, 0.0],
            "component_count_x": 2,
            "component_count_y": 2,
            "sections_per_component": 1,
            "quads_per_section": 63,
            "base_height": 0.0,
        }), "create_landscape")
        assert payload.get("success") is True, payload
        assert payload.get("label") == self._landscape_name(), payload
        assert payload.get("width") == 127, payload
        assert payload.get("height") == 127, payload

    def test_03_set_landscape_flat_height(self):
        payload = _ok(send_command("set_landscape_flat_height", {
            "landscape_name": self._landscape_name(),
            "height_world": 256.0,
        }), "set_landscape_flat_height")
        assert payload.get("success") is True, payload
        assert payload.get("height_world") == 256.0, payload

    def test_04_import_landscape_heightmap(self):
        tmp_dir = Path("e:/unrealBP/_tmp")
        tmp_dir.mkdir(parents=True, exist_ok=True)
        file_path = tmp_dir / f"{self._landscape_name()}.r16"

        width = 127
        height = 127
        row = [32768 + ((index % 16) * 32) for index in range(width)]
        with file_path.open("wb") as handle:
            for _ in range(height):
                handle.write(struct.pack("<" + "H" * width, *row))

        payload = _ok(send_command("import_landscape_heightmap", {
            "landscape_name": self._landscape_name(),
            "source_path": str(file_path),
        }), "import_landscape_heightmap")
        assert payload.get("success") is True, payload
        assert payload.get("resampled") is False, payload