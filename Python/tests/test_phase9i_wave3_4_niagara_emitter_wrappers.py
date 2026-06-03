"""Wrapper coverage for Phase 9i batch 2 Niagara emitter inspection and lifecycle helpers."""

from __future__ import annotations

import json
import sys
from pathlib import Path
from typing import Any, Dict


_PY_ROOT = Path(__file__).resolve().parent.parent
if str(_PY_ROOT) not in sys.path:
    sys.path.insert(0, str(_PY_ROOT))

import unreal_ai_mcp as mcp  # noqa: E402


def _decode(response: str) -> Dict[str, Any]:
    payload = json.loads(response)
    assert payload["status"] == "success", payload
    assert isinstance(payload.get("result"), dict), payload
    return payload["result"]


def test_read_niagara_system_emitter_maps_payload(monkeypatch):
    captured: Dict[str, Any] = {}

    def fake_bridge(command: str, payload: Dict[str, Any]) -> str:
        captured["command"] = command
        captured["payload"] = payload
        return json.dumps({
            "status": "success",
            "result": {
                "asset_path": payload["asset_path"],
                "handle_id": payload["emitter_handle_id"],
            },
        })

    monkeypatch.setattr(mcp, "_bridge", fake_bridge)

    response = mcp.read_niagara_system_emitter(
        asset_path="/Game/CopilotTests/Niagara/NS_Test.NS_Test",
        emitter_handle_id="12345678-1234-1234-1234-1234567890ab",
    )
    payload = _decode(response)

    assert captured == {
        "command": "read_niagara_system_emitter",
        "payload": {
            "asset_path": "/Game/CopilotTests/Niagara/NS_Test.NS_Test",
            "emitter_handle_id": "12345678-1234-1234-1234-1234567890ab",
        },
    }
    assert payload["handle_id"] == "12345678-1234-1234-1234-1234567890ab"


def test_add_niagara_emitter_to_system_maps_payload(monkeypatch):
    captured: Dict[str, Any] = {}

    def fake_bridge(command: str, payload: Dict[str, Any]) -> str:
        captured["command"] = command
        captured["payload"] = payload
        return json.dumps({
            "status": "success",
            "result": {
                "asset_path": payload["asset_path"],
                "emitter_asset_path": payload["emitter_asset_path"],
                "added": True,
            },
        })

    monkeypatch.setattr(mcp, "_bridge", fake_bridge)

    response = mcp.add_niagara_emitter_to_system(
        asset_path="/Game/CopilotTests/Niagara/NS_Test.NS_Test",
        emitter_asset_path="/Niagara/VectorFields/VectorFieldArrowEmitter.VectorFieldArrowEmitter",
    )
    payload = _decode(response)

    assert captured == {
        "command": "add_niagara_emitter_to_system",
        "payload": {
            "asset_path": "/Game/CopilotTests/Niagara/NS_Test.NS_Test",
            "emitter_asset_path": "/Niagara/VectorFields/VectorFieldArrowEmitter.VectorFieldArrowEmitter",
        },
    }
    assert payload["added"] is True


def test_duplicate_rename_remove_niagara_system_emitter_map_payloads(monkeypatch):
    captured: list[tuple[str, Dict[str, Any]]] = []

    def fake_bridge(command: str, payload: Dict[str, Any]) -> str:
        captured.append((command, payload))
        return json.dumps({
            "status": "success",
            "result": {
                "command": command,
                "asset_path": payload["asset_path"],
            },
        })

    monkeypatch.setattr(mcp, "_bridge", fake_bridge)

    duplicate_payload = _decode(mcp.duplicate_niagara_system_emitter(
        asset_path="/Game/CopilotTests/Niagara/NS_Test.NS_Test",
        emitter_name="ArrowEmitter",
        new_emitter_name="ArrowEmitterCopy",
    ))
    rename_payload = _decode(mcp.rename_niagara_system_emitter(
        asset_path="/Game/CopilotTests/Niagara/NS_Test.NS_Test",
        emitter_handle_id="12345678-1234-1234-1234-1234567890ab",
        new_emitter_name="ArrowEmitterRenamed",
    ))
    remove_payload = _decode(mcp.remove_niagara_system_emitter(
        asset_path="/Game/CopilotTests/Niagara/NS_Test.NS_Test",
        emitter_name="ArrowEmitterRenamed",
    ))

    assert captured == [
        (
            "duplicate_niagara_system_emitter",
            {
                "asset_path": "/Game/CopilotTests/Niagara/NS_Test.NS_Test",
                "emitter_name": "ArrowEmitter",
                "new_emitter_name": "ArrowEmitterCopy",
            },
        ),
        (
            "rename_niagara_system_emitter",
            {
                "asset_path": "/Game/CopilotTests/Niagara/NS_Test.NS_Test",
                "new_emitter_name": "ArrowEmitterRenamed",
                "emitter_handle_id": "12345678-1234-1234-1234-1234567890ab",
            },
        ),
        (
            "remove_niagara_system_emitter",
            {
                "asset_path": "/Game/CopilotTests/Niagara/NS_Test.NS_Test",
                "emitter_name": "ArrowEmitterRenamed",
            },
        ),
    ]
    assert duplicate_payload["command"] == "duplicate_niagara_system_emitter"
    assert rename_payload["command"] == "rename_niagara_system_emitter"
    assert remove_payload["command"] == "remove_niagara_system_emitter"