"""Phase 7 smoke tests \u2014 materials, physics, mesh, blueprint spawning.

Run with:
    e:\\unrealBP\\.venv\\Scripts\\python.exe -m pytest e:\\unrealBP\\UnrealAI\\Python\\tests -v

All tests are end-to-end against the running editor. They auto-skip when the
TCP bridge on 127.0.0.1:55557 is unreachable.

Every Phase 7 MCP tool wraps a C++ handler that already shipped in earlier
phases; these tests verify the new Python wrappers send the right param names
and that the round-trip succeeds.
"""

from __future__ import annotations

import json
import sys
from pathlib import Path
from typing import Any, Dict, Optional

import pytest

_PY_ROOT = Path(__file__).resolve().parent.parent
if str(_PY_ROOT) not in sys.path:
    sys.path.insert(0, str(_PY_ROOT))

from ue_bridge import ping, send_command  # noqa: E402

BRIDGE_AVAILABLE = ping()
SKIP_REASON = "UE bridge not reachable on 127.0.0.1:55557 (open the editor first)"
needs_bridge = pytest.mark.skipif(not BRIDGE_AVAILABLE, reason=SKIP_REASON)


# --------------------------------------------------------------------------- #
# Helpers (mirroring test_blueprint_authoring.py)
# --------------------------------------------------------------------------- #

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


def _ensure_bp_with_mesh(name: str, component: str = "Mesh") -> None:
    resp = send_command("create_blueprint", {"name": name, "parent_class": "Actor"})
    if isinstance(resp, dict) and resp.get("status") == "error" and "already" not in str(resp).lower():
        pytest.fail(f"create_blueprint({name}): {resp}")
    add = send_command("add_component_to_blueprint", {
        "blueprint_name": name,
        "component_type": "/Script/Engine.StaticMeshComponent",
        "component_name": component,
    })
    if isinstance(add, dict) and add.get("status") == "error" and "already" not in str(add).lower():
        pytest.fail(f"add_component({component}): {add}")
    set_mesh = send_command("set_static_mesh_properties", {
        "blueprint_name": name,
        "component_name": component,
        "static_mesh": "/Engine/BasicShapes/Cube",
    })
    _ok(set_mesh, f"set_static_mesh_properties({component})")
    _ok(send_command("compile_blueprint", {"blueprint_name": name}),
        f"compile({name})")


@needs_bridge
class TestPhase7Tools:
    """Smoke tests for the eight Phase 7 MCP wrappers."""

    BP_NAME = "BP_Phase7_Smoke"
    COMPONENT = "Mesh"

    # --- 1. set_static_mesh_properties ------------------------------------- #
    def test_01_set_static_mesh_properties(self):
        _ensure_bp_with_mesh(self.BP_NAME, self.COMPONENT)
        # _ensure_bp_with_mesh already calls set_static_mesh_properties; here
        # we just round-trip with `material` too.
        resp = send_command("set_static_mesh_properties", {
            "blueprint_name": self.BP_NAME,
            "component_name": self.COMPONENT,
            "static_mesh": "/Engine/BasicShapes/Cube",
        })
        payload = _ok(resp, "set_static_mesh_properties")
        assert payload.get("component") == self.COMPONENT, payload

    # --- 2. set_physics_properties ----------------------------------------- #
    def test_02_set_physics_properties(self):
        _ensure_bp_with_mesh(self.BP_NAME, self.COMPONENT)
        resp = send_command("set_physics_properties", {
            "blueprint_name": self.BP_NAME,
            "component_name": self.COMPONENT,
            "simulate_physics": True,
            "mass": 25.0,
            "linear_damping": 0.05,
            "angular_damping": 0.05,
        })
        payload = _ok(resp, "set_physics_properties")
        assert payload.get("component") == self.COMPONENT, payload

    # --- 3. set_mesh_material_color ---------------------------------------- #
    def test_03_set_mesh_material_color(self):
        _ensure_bp_with_mesh(self.BP_NAME, self.COMPONENT)
        resp = send_command("set_mesh_material_color", {
            "blueprint_name": self.BP_NAME,
            "component_name": self.COMPONENT,
            "color": [0.2, 0.7, 0.9, 1.0],
            "material_slot": 0,
            "parameter_name": "BaseColor",
        })
        payload = _ok(resp, "set_mesh_material_color")
        assert payload.get("success") is True, payload
        assert payload.get("component") == self.COMPONENT, payload

    # --- 4. apply_material_to_blueprint ------------------------------------ #
    def test_04_apply_material_to_blueprint(self):
        _ensure_bp_with_mesh(self.BP_NAME, self.COMPONENT)
        resp = send_command("apply_material_to_blueprint", {
            "blueprint_name": self.BP_NAME,
            "component_name": self.COMPONENT,
            "material_path": "/Engine/BasicShapes/BasicShapeMaterial",
            "material_slot": 0,
        })
        payload = _ok(resp, "apply_material_to_blueprint")
        assert payload.get("success") is True, payload

    # --- 5. get_blueprint_material_info ------------------------------------ #
    def test_05_get_blueprint_material_info(self):
        _ensure_bp_with_mesh(self.BP_NAME, self.COMPONENT)
        resp = send_command("get_blueprint_material_info", {
            "blueprint_name": self.BP_NAME,
            "component_name": self.COMPONENT,
        })
        payload = _ok(resp, "get_blueprint_material_info")
        # Handler returns at minimum component_name + some material info.
        # Don't over-assert on shape \u2014 just confirm it didn't error.
        assert isinstance(payload, dict) and payload, payload

    # --- 6. spawn_blueprint_actor + apply_material_to_actor + get_actor_material_info -- #
    def test_06_spawn_and_actor_material_roundtrip(self):
        _ensure_bp_with_mesh(self.BP_NAME, self.COMPONENT)
        _ok(send_command("compile_blueprint", {"blueprint_name": self.BP_NAME}),
            "recompile before spawn")
        actor_label = "Phase7_SpawnedActor"
        # Best-effort cleanup of any prior instance.
        send_command("delete_actor", {"name": actor_label})
        spawn = send_command("spawn_blueprint_actor", {
            "blueprint_name": self.BP_NAME,
            "actor_name": actor_label,
            "location": [0.0, 0.0, 200.0],
            "rotation": [0.0, 0.0, 0.0],
        })
        payload = _ok(spawn, "spawn_blueprint_actor")
        # ActorToJsonObject returns a `name` field (the runtime actor name,
        # not the label). Use that for downstream lookups.
        spawned_name = payload.get("name") or actor_label
        assert spawned_name, payload

        # Apply material to the spawned actor.
        applied = send_command("apply_material_to_actor", {
            "actor_name": spawned_name,
            "material_path": "/Engine/BasicShapes/BasicShapeMaterial",
            "material_slot": 0,
        })
        applied_payload = _ok(applied, "apply_material_to_actor")
        assert applied_payload.get("success") is True, applied_payload

        # Read material info back.
        info = send_command("get_actor_material_info", {"actor_name": spawned_name})
        info_payload = _ok(info, "get_actor_material_info")
        slots = info_payload.get("material_slots", [])
        assert isinstance(slots, list) and len(slots) >= 1, info_payload
        # First slot should now reference the BasicShapeMaterial.
        assert any("BasicShape" in (s.get("material_path") or "") or
                   "BasicShape" in (s.get("material_name") or "")
                   for s in slots), slots
