"""Phase 7 Wave 3a smoke tests \u2014 level design helpers.

Tools exercised:
    snap_actors_to_grid, align_actors, duplicate_actor, focus_viewport
"""

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


def _spawn_cube(name: str, location):
    resp = send_command("spawn_actor", {
        "name": name,
        "type": "StaticMeshActor",
        "location": location,
    })
    # Tolerate "already exists".
    if isinstance(resp, dict) and resp.get("status") == "error":
        if "already exists" in json.dumps(resp).lower():
            return
        pytest.fail(f"spawn_actor({name}): {resp}")


@needs_bridge
class TestPhase7Wave3a:
    """End-to-end tests for the four level-design helpers."""

    SUFFIX = str(int(time.time()))[-6:]

    @classmethod
    def _name(cls, base: str) -> str:
        return f"{base}_{cls.SUFFIX}"

    def test_01_snap_actors_to_grid(self):
        n1 = self._name("Snap_A")
        n2 = self._name("Snap_B")
        _spawn_cube(n1, [137.5, 212.3, 47.9])
        _spawn_cube(n2, [-83.1, 99.99, 250.5])
        resp = send_command("snap_actors_to_grid", {
            "actor_names": [n1, n2],
            "grid_size": 100.0,
        })
        payload = _ok(resp, "snap_actors_to_grid")
        assert payload.get("snapped_count") == 2, payload
        for entry in payload.get("results", []):
            loc = entry.get("new_location")
            assert loc is not None, entry
            for v in loc:
                assert abs(round(v / 100.0) * 100.0 - v) < 1e-3, entry

    def test_02_align_actors_z_min(self):
        n1 = self._name("Align_A")
        n2 = self._name("Align_B")
        n3 = self._name("Align_C")
        _spawn_cube(n1, [0, 0, 100])
        _spawn_cube(n2, [200, 0, 350])
        _spawn_cube(n3, [400, 0, 75])
        resp = send_command("align_actors", {
            "actor_names": [n1, n2, n3],
            "axis": "z",
            "mode": "min",
        })
        payload = _ok(resp, "align_actors min")
        assert payload.get("aligned_count") == 3, payload
        assert abs(payload.get("target_value", -1) - 75.0) < 1e-3, payload

    def test_03_align_actors_invalid_axis(self):
        resp = send_command("align_actors", {
            "actor_names": [self._name("Align_A"), self._name("Align_B")],
            "axis": "w",
        })
        # Expect explicit error envelope.
        assert isinstance(resp, dict)
        assert resp.get("status") == "error", resp

    def test_04_duplicate_actor(self):
        src = self._name("Dup_Src")
        _spawn_cube(src, [500, 500, 100])
        new_name = self._name("Dup_New")
        resp = send_command("duplicate_actor", {
            "actor_name": src,
            "new_name": new_name,
            "offset_location": [200.0, 0.0, 0.0],
        })
        payload = _ok(resp, "duplicate_actor")
        assert payload.get("success") is True, payload
        loc = payload.get("location")
        assert loc is not None, payload
        assert abs(loc[0] - 700.0) < 1e-3, payload

    def test_05_focus_viewport_on_actor(self):
        n = self._name("Focus_A")
        _spawn_cube(n, [1000, 1000, 200])
        resp = send_command("focus_viewport", {"actor_name": n})
        payload = _ok(resp, "focus_viewport actor")
        assert payload.get("success") is True, payload
        assert payload.get("focused_on"), payload

    def test_06_focus_viewport_at_location(self):
        resp = send_command("focus_viewport", {"location": [0.0, 0.0, 500.0]})
        payload = _ok(resp, "focus_viewport location")
        assert payload.get("success") is True, payload
        assert payload.get("focused_on_location") == [0.0, 0.0, 500.0], payload

    def test_07_focus_viewport_no_args(self):
        resp = send_command("focus_viewport", {})
        assert isinstance(resp, dict)
        assert resp.get("status") == "error", resp
