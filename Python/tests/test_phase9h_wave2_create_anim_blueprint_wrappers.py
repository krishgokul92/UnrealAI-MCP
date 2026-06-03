"""Wrapper coverage for Phase 9h Wave 2 AnimBlueprint asset creation."""

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


def test_create_anim_blueprint_asset_maps_payload(monkeypatch):
    captured: Dict[str, Any] = {}

    def fake_bridge(command: str, payload: Dict[str, Any]) -> str:
        captured["command"] = command
        captured["payload"] = payload
        return json.dumps({
            "status": "success",
            "result": {
                "anim_blueprint_name": payload["anim_blueprint_name"],
                "skeleton_path": payload["skeleton_path"],
                "destination_path": payload["destination_path"],
                "created": True,
            },
        })

    monkeypatch.setattr(mcp, "_bridge", fake_bridge)

    response = mcp.create_anim_blueprint_asset(
        anim_blueprint_name="ABP_TestWave2",
        skeleton_path="/Game/Characters/Mannequins/Meshes/SK_Mannequin_Skeleton.SK_Mannequin_Skeleton",
        destination_path="/Game/CopilotTests/AnimBlueprints",
        parent_class_path="/Script/Engine.AnimInstance",
        preview_skeletal_mesh_path="/Game/Characters/Mannequins/Meshes/SKM_Manny.SKM_Manny",
    )
    payload = _decode(response)

    assert captured == {
        "command": "create_anim_blueprint_asset",
        "payload": {
            "anim_blueprint_name": "ABP_TestWave2",
            "skeleton_path": "/Game/Characters/Mannequins/Meshes/SK_Mannequin_Skeleton.SK_Mannequin_Skeleton",
            "destination_path": "/Game/CopilotTests/AnimBlueprints",
            "is_template": False,
            "parent_class_path": "/Script/Engine.AnimInstance",
            "preview_skeletal_mesh_path": "/Game/Characters/Mannequins/Meshes/SKM_Manny.SKM_Manny",
        },
    }
    assert payload["anim_blueprint_name"] == "ABP_TestWave2"
    assert payload["skeleton_path"] == "/Game/Characters/Mannequins/Meshes/SK_Mannequin_Skeleton.SK_Mannequin_Skeleton"
    assert payload["destination_path"] == "/Game/CopilotTests/AnimBlueprints"
    assert payload["created"] is True