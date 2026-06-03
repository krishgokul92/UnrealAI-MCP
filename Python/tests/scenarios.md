# Phase 5 Acceptance Scenarios

End-to-end smoke scenarios that an LLM driving the MCP server should be able to
satisfy on first attempt with Claude Sonnet/Opus. Each scenario maps to one
test in [test_blueprint_authoring.py](test_blueprint_authoring.py).

The harness asserts the final graph shape via `analyze_blueprint_graph` rather
than comparing exact node names — small permutations (renames, ordering) are
fine as long as the required node classes and connections exist.

| # | Name | Prompt to the model | Expected graph shape |
|---|------|---------------------|----------------------|
| 1 | `create_empty_actor_bp` | Create a Blueprint named `BP_Smoke_Empty` derived from `Actor`. | Asset exists, EventGraph compiles. |
| 2 | `add_static_mesh_component` | Create `BP_Smoke_Cube` (Actor) and add a StaticMesh component named `Mesh`. | Component `Mesh` of class `StaticMeshComponent` present. |
| 3 | `beginplay_print_string` | In `BP_Smoke_BeginPrint` (Actor), wire BeginPlay → PrintString("Hello"). | Nodes: `K2Node_Event` (BeginPlay), `K2Node_CallFunction` (PrintString); exec wired BeginPlay→PrintString. |
| 4 | `beginplay_branch_print` | In `BP_Smoke_Branch` (Actor) on BeginPlay, branch on a new `bool` variable `bIsReady` and print "Ready" on True / "NotReady" on False. | Nodes: BeginPlay, Branch, two PrintStrings, VariableGet `bIsReady`. Exec: BeginPlay→Branch; Branch True→PrintString A; Branch False→PrintString B. Variable `bIsReady` exists. |
| 5 | `tick_increment_counter` | In `BP_Smoke_Counter` (Actor) on Tick, increment an `int` variable `Counter` by 1. | Tick→Set Counter; Counter+1 wired into Set. Variable `Counter` exists (int). |
| 6 | `set_variable_default` | In `BP_Smoke_VarDefault` create string variable `Greeting` with default "Hi". | Variable present, default string equals "Hi". |
| 7 | `paste_snippet_beginplay_print` | Create `BP_Smoke_SnippetBP`, then paste the canned `event_begin_play` T3D snippet via `paste_blueprint_graph`. | EventGraph contains a BeginPlay event node; `paste_blueprint_graph` returns `expected_node_count >= 1` and no `validation_errors`. Validates A1+A2+A4 path. |
| 8 | `granular_chain_replaces_paste` | Same outcome as scenario 3 but built node-by-node using only granular tools (`add_event_node`, `add_blueprint_node`, `connect_blueprint_nodes`). Asset: `BP_Smoke_Granular`. | Same shape as scenario 3. Validates Workstream B end-to-end. |
| 9 | `delete_node_then_recompile` | Build scenario 3, then delete the PrintString node and recompile. | EventGraph contains BeginPlay only, no PrintString. Compile success. |
| 10 | `inspection_returns_pin_types` | After scenario 4, call `analyze_blueprint_graph` and verify pin metadata is present. | Returned nodes include `node_guid`; pins include `pin_id` + `category`; connections include `from_pin_id` and `to_pin_id`. Validates C1. |

## Running

```powershell
e:\unrealBP\.venv\Scripts\python.exe -m pytest e:\unrealBP\UnrealAI\Python\tests -v
```

Tests are skipped automatically when the UE bridge on `127.0.0.1:55557` is
unreachable. The harness creates Blueprints under `/Game/_PhaseFiveSmoke/` and
does NOT clean up — re-running the suite recreates each asset under the same
name (the C++ side overwrites the existing asset).

## What "pass" means for Phase 5 acceptance

All 10 scenarios pass on first invocation against a freshly opened editor. Any
scenario that requires a manual retry counts as a fail.
