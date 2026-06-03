"""Wrapper tests for the seventh Phase 9e layer-visibility slice."""

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


def test_set_layer_visibility_maps_payload(monkeypatch):
    mcp_module = _reload_module()
    calls: list[tuple[str, dict | None]] = []

    def fake_bridge(command: str, params: dict | None = None):
        calls.append((command, params))
        return {"status": "success"}

    monkeypatch.setattr(mcp_module, "_bridge", fake_bridge)

    result = mcp_module.set_layer_visibility("Gameplay", False)

    assert result == {"status": "success"}
    assert calls == [
        ("set_layer_visibility", {
            "layer_name": "Gameplay",
            "is_visible": False,
        }),
    ]