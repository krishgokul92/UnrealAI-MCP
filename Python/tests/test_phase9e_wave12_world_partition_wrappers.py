"""Wrapper tests for the twelfth Phase 9e world-partition inspection slice."""

from __future__ import annotations

import importlib
import sys
from pathlib import Path


_PY_ROOT = Path(__file__).resolve().parent.parent
if str(_PY_ROOT) not in sys.path:
    sys.path.insert(0, str(_PY_ROOT))


def _reload_module():
    if "unreal_ai_mcp" in sys.modules:
        return importlib.reload(sys.modules["unreal_ai_mcp"])
    return importlib.import_module("unreal_ai_mcp")


def test_get_world_partition_info_uses_bridge_command(monkeypatch):
    mcp_module = _reload_module()
    calls: list[tuple[str, dict | None]] = []

    def fake_bridge(command: str, params: dict | None = None):
        calls.append((command, params))
        return {"status": "success"}

    monkeypatch.setattr(mcp_module, "_bridge", fake_bridge)

    result = mcp_module.get_world_partition_info()

    assert result == {"status": "success"}
    assert calls == [
        ("get_world_partition_info", None),
    ]