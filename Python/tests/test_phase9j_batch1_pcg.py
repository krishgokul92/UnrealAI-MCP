"""Live-editor smoke tests for Phase 9j batch 1 PCG graph/component inspection and bootstrap helpers."""

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


SKIP_REASON = "UE bridge not reachable on 127.0.0.1:55557 (open the editor first)"
STARTUP_SETTLE_SECONDS = 10


@pytest.fixture(scope="module", autouse=True)
def require_bridge() -> None:
    for _ in range(30):
        if ping():
            time.sleep(STARTUP_SETTLE_SECONDS)
            _ok(send_command("new_blank_map", {
                "save_existing_map": False,
            }), "new_blank_map")
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
        pytest.fail(f"{context}: bridge error: {json.dumps(resp)[:1000]}")
    return _unwrap(resp)


def _spawn_cube(name: str, location: list[float]) -> None:
    resp = send_command("spawn_actor", {
        "name": name,
        "type": "StaticMeshActor",
        "location": location,
    })
    if isinstance(resp, dict) and resp.get("status") == "error":
        if "already exists" in json.dumps(resp).lower():
            return
        pytest.fail(f"spawn_actor({name}): {resp}")


class TestPhase9jBatch1PCG:
    SUFFIX = str(int(time.time()))[-6:]

    @classmethod
    def _name(cls, base: str) -> str:
        return f"{base}_{cls.SUFFIX}"

    def test_01_create_and_read_pcg_graph_and_instance(self):
        graph_name = self._name("PCG_Graph")
        instance_name = self._name("PCG_Instance")
        destination_path = "/Game/CopilotTests/PCG"
        graph_asset_path = f"{destination_path}/{graph_name}.{graph_name}"
        instance_asset_path = f"{destination_path}/{instance_name}.{instance_name}"

        created_graph = _ok(send_command("create_pcg_graph_asset", {
            "pcg_graph_name": graph_name,
            "destination_path": destination_path,
            "expose_to_library": True,
            "title_override": "Copilot PCG Graph",
            "color_override": [0.2, 0.4, 0.6, 1.0],
        }), "create_pcg_graph_asset")

        assert created_graph.get("created") is True, created_graph
        assert created_graph.get("asset_path") == graph_asset_path, created_graph
        assert created_graph.get("object_class") == "PCGGraph", created_graph
        assert created_graph.get("is_asset") is True, created_graph
        assert created_graph.get("is_instance") is False, created_graph
        assert created_graph.get("expose_to_library") is True, created_graph
        assert created_graph.get("has_title_override") is True, created_graph
        assert created_graph.get("title_override") == "Copilot PCG Graph", created_graph
        assert created_graph.get("node_count") >= 0, created_graph
        assert created_graph.get("user_parameter_count") >= 0, created_graph

        graph_readback = _ok(send_command("read_pcg_graph_content", {
            "asset_path": graph_asset_path,
        }), "read_pcg_graph_content")

        assert graph_readback.get("asset_path") == graph_asset_path, graph_readback
        assert graph_readback.get("object_class") == "PCGGraph", graph_readback
        assert graph_readback.get("graph_asset_path") == graph_asset_path, graph_readback
        assert isinstance(graph_readback.get("user_parameters"), list), graph_readback

        created_instance = _ok(send_command("create_pcg_graph_instance", {
            "instance_name": instance_name,
            "destination_path": destination_path,
            "parent_graph_path": graph_asset_path,
        }), "create_pcg_graph_instance")

        assert created_instance.get("created") is True, created_instance
        assert created_instance.get("asset_path") == instance_asset_path, created_instance
        assert created_instance.get("is_instance") is True, created_instance
        assert created_instance.get("parent_graph_path") == graph_asset_path, created_instance
        assert created_instance.get("graph_asset_path") == graph_asset_path, created_instance

    def test_02_add_pcg_component_and_create_pcg_volume(self):
        graph_name = self._name("PCG_ComponentGraph")
        actor_name = self._name("PCG_TargetActor")
        component_name = self._name("PCGComponent")
        volume_name = self._name("PCG_Volume")
        destination_path = "/Game/CopilotTests/PCG"
        graph_asset_path = f"{destination_path}/{graph_name}.{graph_name}"

        created_graph = _ok(send_command("create_pcg_graph_asset", {
            "pcg_graph_name": graph_name,
            "destination_path": destination_path,
        }), "create_pcg_graph_asset for component")
        assert created_graph.get("asset_path") == graph_asset_path, created_graph

        _spawn_cube(actor_name, [2400.0, 300.0, 120.0])

        added_component = _ok(send_command("add_pcg_component_to_actor", {
            "actor_name": actor_name,
            "component_name": component_name,
            "graph_asset_path": graph_asset_path,
            "generation_trigger": "GenerateOnDemand",
            "is_partitioned": True,
            "activated": False,
            "seed": 77,
            "generate_on_drop_when_trigger_on_demand": True,
        }), "add_pcg_component_to_actor")

        assert added_component.get("success") is True, added_component
        assert added_component.get("component_name") == component_name, added_component
        assert added_component.get("actor_class") == "StaticMeshActor", added_component
        assert added_component.get("actor_path"), added_component
        assert added_component.get("graph_asset_path") == graph_asset_path, added_component
        assert added_component.get("generation_trigger") == "GenerateOnDemand", added_component
        assert added_component.get("is_partitioned") is True, added_component
        assert added_component.get("activated") is False, added_component
        assert added_component.get("seed") == 77, added_component
        assert added_component.get("has_graph_instance") is True, added_component
        assert isinstance(added_component.get("graph"), dict), added_component

        component_readback = _ok(send_command("read_pcg_component_content", {
            "actor_name": actor_name,
            "component_name": component_name,
        }), "read_pcg_component_content")

        assert component_readback.get("component_name") == component_name, component_readback
        assert component_readback.get("actor_class") == "StaticMeshActor", component_readback
        assert component_readback.get("actor_path"), component_readback
        assert component_readback.get("graph_asset_path") == graph_asset_path, component_readback
        assert component_readback.get("generation_trigger") == "GenerateOnDemand", component_readback
        assert component_readback.get("tool_data_entry_count") >= 0, component_readback

        created_volume = _ok(send_command("create_pcg_volume", {
            "volume_name": volume_name,
            "location": [3200.0, 500.0, 150.0],
            "rotation": [0.0, 45.0, 0.0],
            "scale": [2.0, 3.0, 4.0],
            "graph_asset_path": graph_asset_path,
            "generation_trigger": "GenerateAtRuntime",
            "is_partitioned": True,
            "activated": True,
            "seed": 123,
        }), "create_pcg_volume")

        assert created_volume.get("created") is True, created_volume
        assert created_volume.get("volume_name") == volume_name, created_volume
        assert created_volume.get("actor_label") == volume_name, created_volume
        assert created_volume.get("component_class") == "PCGComponent", created_volume
        assert created_volume.get("graph_asset_path") == graph_asset_path, created_volume
        assert created_volume.get("generation_trigger") == "GenerateAtRuntime", created_volume
        assert created_volume.get("is_partitioned") is True, created_volume
        assert created_volume.get("activated") is True, created_volume
        assert created_volume.get("seed") == 123, created_volume