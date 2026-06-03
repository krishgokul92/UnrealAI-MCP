"""Wrapper tests for the eleventh Phase 9e layer-lifecycle slice."""

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


def test_rename_layer_maps_payload(monkeypatch):
    mcp_module = _reload_module()
    calls: list[tuple[str, dict | None]] = []

    def fake_bridge(command: str, params: dict | None = None):
        calls.append((command, params))
        return {"status": "success"}

    monkeypatch.setattr(mcp_module, "_bridge", fake_bridge)

    result = mcp_module.rename_layer("Old", "New")

    assert result == {"status": "success"}
    assert calls == [
        ("rename_layer", {"layer_name": "Old", "new_layer_name": "New"}),
    ]


def test_delete_layer_maps_payload(monkeypatch):
    mcp_module = _reload_module()
    calls: list[tuple[str, dict | None]] = []

    def fake_bridge(command: str, params: dict | None = None):
        calls.append((command, params))
        return {"status": "success"}

    monkeypatch.setattr(mcp_module, "_bridge", fake_bridge)

    result = mcp_module.delete_layer("Old")

    assert result == {"status": "success"}
    assert calls == [
        ("delete_layer", {"layer_name": "Old"}),
    ]