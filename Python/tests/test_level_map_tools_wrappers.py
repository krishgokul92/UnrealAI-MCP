"""Wrapper coverage for MCP level-opening helpers."""

from __future__ import annotations

import sys
import types
from pathlib import Path


_PY_ROOT = Path(__file__).resolve().parent.parent
if str(_PY_ROOT) not in sys.path:
    sys.path.insert(0, str(_PY_ROOT))

if "mcp.server.fastmcp" not in sys.modules:
    mcp_module = types.ModuleType("mcp")
    server_module = types.ModuleType("mcp.server")
    fastmcp_module = types.ModuleType("mcp.server.fastmcp")

    class _FakeFastMCP:
        def __init__(self, *args, **kwargs):
            pass

        def tool(self, *args, **kwargs):
            def decorator(func):
                return func

            return decorator

    fastmcp_module.FastMCP = _FakeFastMCP
    sys.modules["mcp"] = mcp_module
    sys.modules["mcp.server"] = server_module
    sys.modules["mcp.server.fastmcp"] = fastmcp_module

import unreal_ai_mcp as mcp_module  # noqa: E402


def _capture_bridge(monkeypatch):
    calls = []

    def fake_bridge(cmd, params=None):
        calls.append((cmd, params))
        return '{"status":"ok"}'

    monkeypatch.setattr(mcp_module, "_bridge", fake_bridge)
    return calls


def test_new_blank_map_wrapper_maps_payload(monkeypatch):
    calls = _capture_bridge(monkeypatch)

    result = mcp_module.new_blank_map(save_existing_map=True)

    assert result == '{"status":"ok"}'
    assert calls == [(
        "new_blank_map",
        {"save_existing_map": True},
    )]


def test_open_level_wrapper_maps_payload(monkeypatch):
    calls = _capture_bridge(monkeypatch)

    result = mcp_module.open_level(
        asset_path="/Game/Maps/iam_underTesting",
        save_current_level=True,
    )

    assert result == '{"status":"ok"}'
    assert calls == [(
        "open_level",
        {
            "asset_path": "/Game/Maps/iam_underTesting",
            "save_current_level": True,
        },
    )]