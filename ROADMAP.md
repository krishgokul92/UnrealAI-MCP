# UnrealAI — Project Roadmap & Status

**Last updated:** 2026-05-14 (Phase 9 in progress)
**Project root:** `e:\unrealBP\UnrealAI`
**UE project:** `E:\Unreal5\PlayTesting\playtesting\playtesting.uproject`
**UE version:** 5.7 (installed at `C:\Program Files\Epic Games\UE_5.7`)
**Hardware:** RTX 4090 24GB, 130GB RAM

**Active front-ends (parallel):**
1. **Slate Chat Panel** (in-editor, talks to local Python agent on :8765)
2. **VS Code Copilot** (agent mode, talks to MCP server `unreal_ai_mcp.py` over stdio) — **NEW in Phase 6.5**

Both share the same `ue_bridge.py` and the same 202 MCP tools, so anything the Slate panel can do, Copilot can do, and vice versa.

---

## Architecture Overview

```
UE5 Editor (Slate Chat Panel, port 55557 TCP)
     │
     │  HTTP POST /chat  (port 8765)
     ▼
Python Agent (FastAPI)  ──▶  LLM (Ollama :11434 / OpenRouter cloud)
     │                              │
     │  tool calls                  │  tool_calls or text-JSON
     ▼                              │
TCP Bridge Client  ◀────────────────┘
     │  (ue_bridge.py, port 55557)
     ▼
UE Plugin TCP Server (MCPServerRunnable)
     │  (GameThread dispatch)
     ▼
UnrealAIBridge::ExecuteCommand
     ├── EditorCommands      (actors, transforms)
     ├── BlueprintCommands   (create BP, compile, materials, inspection)
     └── BlueprintGraphCommands (nodes, variables, functions, T3D paste)
```

### Key Files

| Layer | File | Purpose |
|-------|------|---------|
| **Python Agent** | `Python/unreal_ai_agent.py` (~1050 lines) | FastAPI server, LLM dispatch, session mgmt, tool loop, streaming |
| **Python Tools** | `Python/ue_tools.py` (~480 lines) | Tool schemas + handlers, TCP bridge calls |
| **Python Bridge** | `Python/ue_bridge.py` (~85 lines) | Socket client for UE TCP bridge |
| **Python Config** | `Python/.env` | API keys (OpenRouter, future OpenAI/Anthropic) |
| **C++ Plugin Def** | `UnrealAIPlugin/UnrealAI.uplugin` | Editor-only module, Win64/Mac/Linux |
| **C++ Build** | `UnrealAIPlugin/Source/UnrealAI/UnrealAI.Build.cs` | Module dependencies |
| **C++ Module** | `Private/UnrealAIModule.cpp` | Tab registration, Window menu entry |
| **C++ Bridge** | `Private/UnrealAIBridge.cpp` (~320 lines) | TCP server lifecycle, command dispatch |
| **C++ TCP** | `Private/MCPServerRunnable.cpp` (~180 lines) | Socket read/write loop |
| **C++ UI** | `Private/UI/SUnrealAIChatPanel.cpp` (~1250 lines) | Slate chat panel (messages, model picker, code blocks, streaming, markdown, save/load) |
| **C++ Editor Cmds** | `Private/Commands/UnrealAIEditorCommands.cpp` (~300 lines) | Actor spawn/delete/transform |
| **C++ BP Cmds** | `Private/Commands/UnrealAIBlueprintCommands.cpp` (~700 lines) | BP create/compile/inspect, materials |
| **C++ BP Graph** | `Private/Commands/UnrealAIBlueprintGraphCommands.cpp` (~580 lines) | Nodes, variables, functions, T3D paste |
| **C++ Utils** | `Private/Commands/UnrealAICommonUtils.cpp` (~300 lines) | Blueprint lookup, JSON helpers, node factories |
| **C++ Graph Helpers** | `Private/Commands/BlueprintGraph/*.cpp` | 10+ sub-modules for node types |
| **Skill Prompt** | `claude-ue-skill/SKILL.md` (15KB) | UE development assistant persona |
| **T3D Spec** | `claude-ue-skill/references/blueprint_serialization.md` (35KB) | T3D clipboard format reference |

---

## Phase History

### Phase 1 — Plugin Scaffold ✅ DONE
- UE5 editor-only plugin: `UnrealAI.uplugin`, Build.cs, module registration
- TCP socket bridge (port 55557) with JSON wire format
- GameThread dispatch pattern via `AsyncTask` + `TPromise`/`TFuture`
- MCPServerRunnable on dedicated thread (single client, non-blocking)

### Phase 2 — Slate Chat Panel ✅ DONE
- `SUnrealAIChatPanel` compound widget
- Model dropdown (Ollama model list), status indicator, Clear button
- Scrollable message history with role-colored bubbles (User=blue, Assistant=green, System=yellow)
- Multi-line input box, Ctrl+Enter to send
- Registered under Window menu as "UnrealAI" tab

### Phase 3 — Python Agent + Ollama Integration ✅ DONE
- FastAPI server on port 8765 (`unreal_ai_agent.py`)
- Ollama `/api/chat` client with `stream=False`, temperature 0.2, context 16384
- Async job pattern: `/chat` returns job_id, panel polls `/chat/status/{id}` every 2s
- Session management (in-memory, per GUID, trimmed to 40 messages)
- Skill file injection (SKILL.md + blueprint_serialization.md) into system prompt
- `/models` endpoint merges Ollama + cloud models
- `/health` endpoint for diagnostics

### Phase 4 — Project Context + Tool Calling ✅ DONE
- `GatherProjectContext()` sends rich snapshot with every chat message:
  - project_name, project_dir, engine_version, current_level, actor_count
  - selected_actors (name, class, location), content_top_folders, source_modules, plugins
- Tool-calling loop (`_run_tool_loop`, max 5 iterations):
  - Native tool_calls (Ollama/OpenRouter)
  - Text-JSON fallback (`_extract_text_tool_calls`) for models that emit `{"name":..., "arguments":...}` as text
  - Nudge system message after text-mode tool calls to steer model back to prose
- Core tools registered:
  - `ping` — bridge health check
  - `get_actors_in_level` — list all actors
  - `find_actors_by_name` — wildcard pattern search
  - `spawn_actor` — create actor (StaticMesh, lights, camera)
  - `delete_actor` — remove by label
  - `set_actor_transform` — move/rotate/scale
  - `create_blueprint` — create BP asset
  - `compile_blueprint` — compile BP
  - `add_component_to_blueprint` — add component with transform
  - `get_available_materials` — search content browser
- Response truncation (8KB default, 16KB for inspection tools)

### Phase 5 — Blueprint Graph Authoring ✅ DONE (2026-05-08)
**Goal:** Let the LLM author Blueprint logic graphs (nodes, connections, variables).

**Why we're un-pausing:** With Phase 6.5 the user can now drive the bridge through **Claude Sonnet via VS Code Copilot** instead of being stuck on local 14B Ollama. Sonnet handles long structured outputs (T3D blocks) and multi-step inspection workflows that the 14B model couldn't. The C++ side has been ready since the original Phase 5 attempt — only the model side was the bottleneck.

**See [Phase 5 Re-activation Plan](#phase-5-re-activation-plan-2026-05-08) below for the concrete work plan.**

**Original status (kept for history):**

**What was built (C++ — all working in the bridge):**
- 13 granular graph commands fully implemented and routed:
  - `add_blueprint_node` → NodeManager
  - `connect_nodes` → BPConnector
  - `create_variable` → BPVariables
  - `set_blueprint_variable_properties` → BPVariables
  - `add_event_node` → EventManager
  - `delete_node` → NodeDeleter
  - `set_node_property` → NodePropertyManager
  - `create_function` → FunctionManager
  - `add_function_input/output` → FunctionIO
  - `delete_function` / `rename_function` → FunctionManager
  - `paste_nodes_to_blueprint` → inline FEdGraphUtilities::ImportNodesFromText
- 4 read-only inspection commands:
  - `read_blueprint_content` — full BP dump
  - `analyze_blueprint_graph` — nodes + connections
  - `get_blueprint_variable_details` — variable type/value/flags
  - `get_blueprint_function_details` — function inputs/outputs/nodes
- 10 specialized node type sub-modules in `BlueprintGraph/Nodes/`:
  - AnimationNodes, CastingNodes, ControlFlowNodes, DataNodes
  - ExecutionSequenceEditor, MakeArrayEditor, NodeCreatorUtils
  - SpecializedNodes, SwitchEnumEditor, UtilityNodes

**Approach A — T3D Paste (attempted, failed at model level):**
- Python tool: `paste_blueprint_graph` exposed with schema
- System prompt: 53KB with T3D generation rules + full format spec
- Guardrail: `_looks_like_unwrapped_t3d()` detects model emitting T3D in chat text
- Re-prompt loop: nudges model to use tool call instead of printing T3D
- **Result:** qwen2.5-coder:14b creates the BP asset via `create_blueprint` but either emits T3D as chat text (not as tool call) or generates malformed T3D. The 53KB system prompt is likely too large for the 14B model to follow reliably. The C++ paste handler works — the bottleneck is model capability.

**Approach B — Granular tools (attempted, same model-level failure):**
- 7 Python tool schemas were exposed: add_blueprint_node, add_event_node, connect_nodes, delete_node, set_node_property, create_variable, set_blueprint_variable_properties
- System prompt taught multi-step node-by-node workflow
- **Result:** qwen2.5-coder:14b couldn't reliably sequence 4-6 tool calls with correct pin IDs and node references. The model kept hallucinating node names and pin IDs that didn't exist.

**Status (as of 2026-04-20):** Both approaches blocked by model capability (14B too small). All C++ infrastructure is solid and tested via direct TCP calls. Python schemas removed (only `paste_blueprint_graph` remains). Paused pending stronger model or smaller-context approach.

**Status update (2026-05-08):** Phase 5 **complete**. All 16 tests in `Python/tests/test_blueprint_authoring.py` pass against the live editor (6 pure-Python validator/snippet tests + 10 end-to-end Blueprint authoring scenarios). Tool count grew 15 → 23. Acceptance criteria #1–#6 met. A3 (`replace_node_by_name`) and C2 (`list_node_classes`) deferred to backlog. Proceeding to Phase 7.

**Historical snapshot at Phase 5 completion:** the MCP surface was 23 tools at that point. Phase 7 expanded the live tool catalog to 44 tools, Phase 9 Wave 1 raised it to 50, the completed Phase 9b material tranche raised it to 68, the first two Phase 9c widget slices brought the total to 70, and Wave 9c-3 raises it to 73; see the Current MCP Tool Inventory section below for the current list.

### Phase 5.5 — OpenRouter BYOK ✅ DONE
- `.env` loader (custom, no python-dotenv dependency)
- `_openrouter_chat()`: OpenAI-compatible client at `https://openrouter.ai/api/v1`
- Temperature 0.2, max_tokens 8192, timeout 300s
- 429 retry with exponential backoff (5s/10s/20s, 4 attempts)
- 404 "no endpoints support tool use" → auto-retry without tools, cache model
- `_strip_think_blocks()` for `<think>...</think>` removal
- Tool_calls normalization from OpenAI format to internal format
- `/models` endpoint appends OpenRouter models when API key is set
- **Tested:** Auth works, routing works, error handling works. Free tier rate-limited upstream.

### Phase 5.5b — Chat Panel Code Block UI ✅ DONE
- `SplitMessageIntoSegments()` — parses markdown ``` fences into prose/code segments
- `CreateProseWidget()` — auto-wrapped STextBlock
- `CreateCodeBlockWidget()` — bordered dark box with:
  - Header: language label + line count
  - Copy button (clipboard via `FPlatformApplicationMisc::ClipboardCopy`)
  - Body: read-only `SMultiLineEditableTextBox`, monospace font, max 240px height with scroll
- Added `ApplicationCore` to Build.cs dependencies

---

## Phase 6 — UX Polish ✅ DONE

Improved the chat panel and agent UX.

| Feature | Description | Status |
|---------|-------------|--------|
| Streaming responses | Token-by-token display via polling (1s interval). Python streams from both Ollama (NDJSON) and OpenRouter (SSE). Job object carries `partial_text`. Panel shows live "AI (streaming...)" widget. | ✅ Done |
| Markdown rendering | Headers (`# ## ###`), bold lines (`**...**`), bullet/numbered lists (`- * 1.`), blockquotes (`>`), horizontal rules (`---`), empty-line spacing. Parsed per-line in `CreateProseWidget()`. | ✅ Done |
| Code block UI | ✅ **DONE early** (Phase 5.5b) — scrollable monospace box with Copy button | ✅ Done |
| Conversation save/load | `POST /session/save`, `POST /session/load`, `GET /sessions`. JSON files in `Python/sessions/`. Panel has Save/Load buttons. Load auto-loads most recent (session picker deferred to Phase 7). | ✅ Done |
| Undo integration | Mutation commands wrapped in `FScopedTransaction` in `ExecuteCommand()`. Read-only commands exempted. Ctrl+Z now undoes AI-triggered actor/BP changes. | ✅ Done |
| Tool-call log | `Job.tool_log` accumulates `{name, args, result}` per tool call. `/chat/status` returns `tool_log` array. Panel renders as compact italic summary below each response. | ✅ Done |

**Bugs fixed during Phase 6:**
- `FinalizeStreamingMessage` was wiping tool-log entries (called `RefreshMessageList` after tool log was added)
- `FinalizeStreamingMessage` caused unnecessary full widget rebuild when no streaming occurred
- Ollama `_do_stream` closure captured stale `effective_tools` from outer scope — could silently swallow 400 errors on retry
- Poll interval reduced from 2.0s to 1.0s for smoother streaming
- Path traversal guard added to `/session/load` endpoint

---

## Phase 6.5 — MCP / VS Code Integration ✅ DONE

Adds a second front-end so the user's **VS Code Copilot** subscription (Claude Sonnet 4.5 / GPT-5 / etc.) can drive the UE plugin directly via the [Model Context Protocol](https://modelcontextprotocol.io). The Slate chat panel and Ollama/OpenRouter path remain fully supported in parallel.

### Architecture

```
VS Code Copilot (agent mode)               UE Slate Chat Panel
            │                                       │
            │ stdio (JSON-RPC)                      │ HTTP /chat
            ▼                                       ▼
   unreal_ai_mcp.py  (FastMCP)            unreal_ai_agent.py (FastAPI)
            │                                       │
            └────────────┬──────────────────────────┘
                         │
                         ▼   (TCP 55557)
                ue_bridge.send_command()
                         │
                         ▼
              UE plugin (UnrealAIBridge.cpp)
```

Both front-ends share the **same** `ue_bridge.py` and the **same** 50 tool handlers. No code duplication.

### Files added

| File | Purpose |
|------|---------|
| `Python/unreal_ai_mcp.py` | FastMCP server. Wraps the current 129-tool MCP surface with `@mcp.tool()` decorators. Returns structured-error JSON instead of raising on `BridgeError`. Logs to stderr only (stdout reserved for protocol). |
| `.vscode/mcp.json` | Workspace-level MCP server registration for VS Code. Points stdio at `.venv/Scripts/python.exe` running `unreal_ai_mcp.py`. |
| `UnrealAI/scripts/test_mcp_smoke.py` | Standalone smoke test. Sends `initialize` + `tools/list`, prints registered tools and any server stderr. |

### Initial tools exposed (Phase 6.5 launch set; current total is 129)

`ping`, `get_actors_in_level`, `find_actors_by_name`, `spawn_actor`, `delete_actor`, `set_actor_transform`, `create_blueprint`, `compile_blueprint`, `add_component_to_blueprint`, `get_available_materials`, `read_blueprint_content`, `analyze_blueprint_graph`, `get_blueprint_variable_details`, `get_blueprint_function_details`, `paste_blueprint_graph`

### How to use from VS Code

1. Open the Unreal Editor with the UnrealAI plugin loaded (TCP bridge listens on 55557).
2. Open this workspace in VS Code with **GitHub Copilot — Agent mode**.
3. VS Code reads `.vscode/mcp.json` automatically. Confirm the `unrealai` server appears with 129 tools in the MCP panel.
4. Ask Copilot anything like *"Spawn a cube called Pillar at (0,0,200) and tell me what's in the level."* — it will pick the right tools.
5. The Python agent on port 8765 does **not** need to be running for the MCP path; only the UE editor needs to be open.

### Smoke test

```powershell
e:\unrealBP\.venv\Scripts\python.exe e:\unrealBP\UnrealAI\scripts\test_mcp_smoke.py
```
Expected: `Tools advertised: 129`.

### Why this unblocks Phase 5

The local `qwen2.5-coder:14b` couldn't reliably generate valid T3D for `paste_blueprint_graph`. With Claude Sonnet via VS Code, T3D authoring becomes tractable. Phase 5 is now being re-activated — see [Phase 5 Re-activation Plan](#phase-5-re-activation-plan-2026-05-08).

---

## Phase 6.5b — Per-Project VS Code Binding & Custom Agent ✅ DONE (2026-05-08)

Follow-up to Phase 6.5 to make the VS Code path usable across multiple Unreal projects and to give Copilot a scoped Unreal persona.

### Changes

| Change | Detail |
|---|---|
| **“Open in VS Code” toolbar button** | New button in `SUnrealAIChatPanel` toolbar. On click: resolves `FPaths::ProjectDir()`, provisions `<ProjectDir>/.vscode/mcp.json` (registers the `unrealai` MCP server pointing at the central venv + `unreal_ai_mcp.py`) and `<ProjectDir>/.claude/agents/unreal-mcp.md` (custom agent), then launches `code "<ProjectDir>" --new-window`. |
| **Per-project VS Code workspace** | Each UE project gets its own VS Code window bound to its folder → chat history, sessions and MCP scope persist per-project. |
| **Custom Copilot agent** | `unreal-mcp` agent file lives at `.claude/agents/unreal-mcp.md`. Frontmatter: `name`, `description`, `model: claude-sonnet-4.5`. System prompt teaches “talk first, act second”, inspect-before-mutate, compile-after-edit. |
| **Includes added** | `Misc/FileHelper.h`, `Misc/Paths.h`, `HAL/FileManager.h` in `SUnrealAIChatPanel.cpp`. |

### Lessons learned (recorded for future)

- VS Code 1.119 reads agents from `.claude/agents/*.md` (NOT `.github/chatmodes/*.chatmode.md`). The chatmode format is from an older spec and is silently ignored.
- Required frontmatter fields: `name` AND `model`. File is dropped if either missing.
- Picker location: bottom-of-chat-input **Agent dropdown** (not the top-of-panel mode selector).
- `robocopy /MIR` preserves source timestamps — UBT then thinks nothing changed and skips compilation. Touch `.cpp` after copy to force a rebuild.

---

## Phase 5 Re-activation Plan (2026-05-08)

**Premise:** With Claude Sonnet driving the bridge through MCP, the model side can now handle T3D generation and multi-step graph workflows. The C++ paste handler (`FEdGraphUtilities::ImportNodesFromText`) and 13 granular graph commands have been working since the original attempt. The plan below adds the missing scaffolding around them so the model has a high success rate.

### Goals (in priority order)

1. **Make T3D paste reliably succeed** for common Blueprint patterns (event handlers, simple branching, variable get/set, function calls).
2. **Provide a granular fallback path** when T3D fails so the model can still build graphs node-by-node.
3. **Tight feedback loop** — every authoring tool returns enough state for the model to verify success and self-correct on failure.
4. **Reusable patterns** — surface common Blueprint snippets as MCP **prompts** / **resources** so the model doesn't reinvent them every time.

### Workstreams

#### A. T3D paste hardening (Highest impact)

| Task | Detail | Owner | Status |
|---|---|---|---|
| **A1. Validation pre-flight** | Before calling `ImportNodesFromText`, run a Python-side validator that catches the top 5 model mistakes: duplicate `NodeGuid`, duplicate `PinId`, `LinkedTo` referring to a missing node/pin, missing `End Object`, mismatched braces. Return a structured `validation_errors` array so the model can fix and retry without round-tripping through UE. | Python | ✅ DONE — `t3d_validator.py`; 7 smoke cases pass |
| **A2. Post-paste verification** | After `ImportNodesFromText`, read back the graph and return: `pasted_node_count`, `pasted_node_names`, `unresolved_links` (any `LinkedTo` that didn't bind). | C++ + Python | ✅ DONE — `expected_node_count`, `missing_node_count`, `expected_link_count` returned |
| **A3. Append-vs-replace flag** | Tool already accepts `clear_graph`. Add `replace_node_by_name` so the model can swap one node's T3D without rebuilding the whole graph. | Python wrapper | ⬜ TODO |
| **A4. T3D snippet library** | Vendor 8–12 hand-curated, tested T3D snippets into `Python/t3d_snippets/`. Expose via `list_t3d_snippets` and `get_t3d_snippet(name)` MCP tools. | Python | ✅ DONE — 10 snippets, 2 MCP tools, fresh-GUID rewrite |
| **A5. Few-shot examples in agent prompt** | The custom `unreal-mcp` agent file gets a short “T3D cookbook” section with 2–3 minimal correct examples. | Markdown | ✅ DONE — `.claude/agents/unreal-mcp.md` + Slate `AgentBody` literal updated |

#### B. Granular fallback tools — ✅ DONE

All six tools wired to the existing C++ handlers (already routed in `UnrealAIBridge.cpp`); no C++ rebuild needed. Surfaced as MCP tools 18–23.

| Tool | Status |
|---|---|
| `add_blueprint_node` | ✅ Returns node name + GUID + pins |
| `connect_blueprint_nodes` | ✅ |
| `delete_blueprint_node` | ✅ |
| `set_node_default` | ✅ Wraps `set_node_property` action `set_pin_default_value` |
| `add_event_node` | ✅ |
| `create_blueprint_variable` | ✅ |

Key design held: every tool returns the resulting node’s identifiers (name + GUID + pin names + pin IDs) so the model can chain calls without guessing.

#### C. Inspection upgrades (Medium impact)

| Task | Detail | Status |
|---|---|---|
| **C1. `analyze_blueprint_graph` includes pin types** | Adds `node_guid`, `pin_id`, `sub_category`, `sub_category_object`, `container`, `is_reference`, `is_const`, `default_value`, `hidden`, `advanced`; connections gain `from_pin_id`/`to_pin_id`. | ✅ DONE — C++ rebuilt + deployed 2026-05-08. |
| **C2. `list_node_classes(filter)`** | Returns the set of node classes available in a graph context, filtered by category or substring. | ⬜ TODO |
| **C3. `dry_run_compile`** | Compile and return errors **without** committing. (Stretch.) | ⬜ Stretch |

#### D. MCP-native primitives (Stretch)

| Task | Detail |
|---|---|
| **D1. MCP resources** | Expose the T3D snippet library as MCP resources (`unrealai://snippets/{name}`) so Claude can read them with the resources API instead of calling tools. |
| **D2. MCP prompts** | Pre-built prompt templates: “Add BeginPlay handler that prints X”, “Expose variable Y on the details panel”, “Wire button click to function Z”. |
| **D3. Tool annotations** | Add `readOnlyHint` / `destructiveHint` to MCP tool annotations so VS Code can show the right confirmation UI. |

#### E. Validation harness (Required for confidence) — ✅ DONE

| Task | Detail | Status |
|---|---|---|
| **E1. Test BPs in repo** | `Python/tests/test_blueprint_authoring.py` — 16 tests: 6 pure-Python (validator + snippets) + 10 end-to-end scenarios. | ✅ DONE |
| **E2. Scenario catalogue** | `Python/tests/scenarios.md` documents all 10 target scenarios. | ✅ DONE |
| **E3. Regression run** | `pytest UnrealAI\Python\tests -v` — **16 passed in 17.59s** against live editor (2026-05-08). | ✅ DONE |

### Phase 5 Acceptance Criteria

Phase 5 is **DONE** when:

1. The 10 scenarios in E2 all succeed on first attempt with Claude Sonnet via the MCP front-end. — ✅ (16/16 pass on 2026-05-08)
2. T3D paste tool returns structured validation errors (A1) and post-paste verification (A2). — ✅
3. At least 4 granular fallback tools (B) are exposed so the model has a recovery path. — ✅ (6 exposed)
4. `analyze_blueprint_graph` returns pin types (C1). — ✅ (built + deployed)
5. The custom agent file references the T3D cookbook (A5). — ✅
6. Smoke test in E3 passes. — ✅ (16/16 green)

**Backlog (deferred, not blocking):** A3 `replace_node_by_name`, C2 `list_node_classes(filter)`, C3 `dry_run_compile`, Workstream D (MCP resources/prompts/annotations).

### Order of execution

1. **Week 1:** A1 (validation pre-flight) + A2 (post-paste verification) + A5 (cookbook). Quick wins.
2. **Week 1:** A4 (snippet library) — enables few-shot grounding.
3. **Week 2:** B (granular fallback tools) + C1 (pin types).
4. **Week 2:** E (validation harness) — prove it works.
5. **Stretch:** D (MCP-native primitives) once basic flow is proven.

### What we're explicitly NOT doing (to keep scope honest)

- Animation Blueprints, Behavior Trees, State Trees — future phases.
- Materials graph editing — different graph type, separate work.
- Niagara graphs — ditto.
- UMG widget graphs — maybe Phase 7.

---

## Phase 7 — More Editor Tools 🔄 IN PROGRESS (2026-05-08)

Expand the tool catalog beyond actors and basic BP creation. Many C++ handlers already exist; the work is exposing them as MCP tools.

**Wave 1 — material/physics/spawn wrappers ✅ DONE (2026-05-08).** Tool count now **23 → 31**. Six new pytest scenarios in `Python/tests/test_phase7_tools.py` exercise every wrapper end-to-end against the live editor; all 22 tests in the combined suite pass (`pytest UnrealAI\Python\tests` — 22 passed in 29.22s).

**Wave 2 — asset registry, material instances, asset import ✅ DONE (2026-05-08).** New C++ class `FUnrealAIAssetCommands` (header + ~290 LOC `.cpp`), wired through `UnrealAIBridge` (constructor, destructor, dispatch), `AssetTools` added to `UnrealAI.Build.cs`. Five new MCP tools added to `unreal_ai_mcp.py`. Tool count **31 → 36**. Eight new pytest scenarios in `Python/tests/test_phase7_wave2.py`; combined suite **30 passed in 24.76s**.

**Wave 3a — level design helpers ✅ DONE (2026-05-08).** New C++ class `FUnrealAILevelCommands` added and routed through `UnrealAIBridge`. Four new MCP tools added to `unreal_ai_mcp.py`: `snap_actors_to_grid`, `align_actors`, `duplicate_actor`, `focus_viewport`. New `Python/tests/test_phase7_wave3a.py` covers 7 end-to-end scenarios; user-reported live run: **7 passed in 4.94s**. Tool count **36 → 40**. No combined full-suite rerun recorded yet after Wave 3a.

**Wave 3b — landscape creation and height editing ✅ DONE (2026-05-08).** New `FUnrealAILandscapeCommands` shipped with bridge wiring and `Landscape` / `LandscapeEditor` module dependencies. Four new commands are exposed in source and MCP: `get_landscapes`, `create_landscape`, `set_landscape_flat_height`, `import_landscape_heightmap`. User-reported live validation of `Python/tests/test_phase7_wave3b_landscape.py` is **4 passed in 1.67s** after a small response-shape fix in `create_landscape` so the actor label is surfaced consistently. Tool count **40 → 44**. Landscape paint-layer authoring remains deferred to a later wave.

| Tool Area | Tools | C++ Status | MCP Wrapper |
|-----------|-------|------------|-------------|
| Material assignment | `apply_material_to_actor`, `apply_material_to_blueprint`, `get_actor_material_info`, `get_blueprint_material_info`, `set_mesh_material_color` | ✅ | ✅ DONE (Wave 1) |
| Physics & mesh | `set_physics_properties`, `set_static_mesh_properties` | ✅ | ✅ DONE (Wave 1) |
| Blueprint spawning | `spawn_blueprint_actor` | ✅ | ✅ DONE (Wave 1) |
| Asset search | `find_assets(query, class_name, path, recursive, max_results)`, `get_asset_dependencies(asset_path)`, `get_referencers(asset_path)` | ✅ DONE (Wave 2) | ✅ DONE (Wave 2) |
| Material creation | `create_material_instance(parent_material, instance_name, dest_path, scalar_parameters, vector_parameters)` | ✅ DONE (Wave 2) | ✅ DONE (Wave 2) |
| Asset import | `import_asset(source_path, dest_path)` for FBX/textures | ✅ DONE (Wave 2) | ✅ DONE (Wave 2) |
| Landscape ops | `get_landscapes`, `read_landscape_content`, `sample_landscape_point`, `sample_landscape_points`, `sample_landscape_grid`, `sample_landscape_region`, `sample_landscape_height_region`, `create_landscape`, `set_landscape_flat_height`, `import_landscape_heightmap` (paint/sculpt tools deferred to later 9d waves) | ✅ DONE (Wave 3b + 9d-6) | ✅ DONE (Wave 3b + 9d-6) |
| Level design helpers | `snap_actors_to_grid`, `align_actors`, `duplicate_actor`, `focus_viewport` | ✅ DONE (Wave 3a) | ✅ DONE (Wave 3a) |

### Historical MCP Tool Inventory (147 total at that checkpoint)

This section is a retained Phase 7-era snapshot of the MCP surface at that checkpoint. For the current live catalog, use `Guides/tools-reference.md` and the latest Phase 9 entries below.

| Area | What you can do | Tools |
|------|-----------------|-------|
| Bridge and level basics | Verify the editor bridge, inspect the current level, find actors, spawn actors, delete actors, and move/rotate/scale actors | `ping`, `get_actors_in_level`, `find_actors_by_name`, `spawn_actor`, `delete_actor`, `set_actor_transform` |
| Blueprint asset authoring | Create Blueprint assets, compile them, add components, create user-defined function graphs, and browse available materials | `create_blueprint`, `compile_blueprint`, `add_component_to_blueprint`, `get_available_materials`, `create_blueprint_function` |
| Blueprint inspection | Dump full Blueprint structure, inspect graphs, and inspect variable/function details before editing | `read_blueprint_content`, `analyze_blueprint_graph`, `get_blueprint_variable_details`, `get_blueprint_function_details` |
| AnimBlueprint inspection, asset creation, state-machine authoring, state lifecycle mutation, state-bound asset-player binding, transition mutation, and common transition-rule authoring | Read one AnimBlueprint asset's skeleton, preview mesh, exposed variables, layer-graph summaries, and state-machine summaries; inspect one named state machine; create empty AnimBlueprint assets, state machines, states, and directed transitions; rename/delete states; bind one state to a sequence, blend-space, or aim-offset player with deterministic state summary readback; delete transitions; and author deterministic `always_true`, bool-variable, int-equality, and enum-equality transition guards with rule-summary readback | `read_anim_blueprint_content`, `create_anim_blueprint_asset`, `read_anim_state_machine`, `create_anim_state_machine`, `create_anim_state`, `rename_anim_state`, `delete_anim_state`, `set_anim_state_sequence_player`, `set_anim_state_blend_space_player`, `create_anim_transition`, `delete_anim_transition`, `set_anim_transition_rule` |
| Widget Blueprint authoring, inspection, hierarchy mutation, layout mutation, binding mutation, and animation mutation | Create Widget Blueprint assets, inspect editable widget trees, bindings, animations, named-slot content, and common slot metadata, then add, remove, reparent, or mutate supported slot layout fields, function/property widget bindings, and source-asset widget animations | `create_widget_blueprint`, `read_widget_blueprint_content`, `add_widget_to_widget_blueprint`, `remove_widget_from_widget_blueprint`, `reparent_widget_in_widget_blueprint`, `set_widget_slot_layout_in_widget_blueprint`, `set_widget_property_binding_in_widget_blueprint`, `remove_widget_property_binding_from_widget_blueprint`, `create_widget_animation_in_widget_blueprint`, `remove_widget_animation_from_widget_blueprint` |
| Blueprint graph authoring | Paste validated T3D graphs, browse reusable snippets, add/connect/delete nodes, add events, set pin defaults, create or mutate member variables, and edit function signatures | `paste_blueprint_graph`, `list_t3d_snippets`, `get_t3d_snippet`, `add_blueprint_node`, `connect_blueprint_nodes`, `delete_blueprint_node`, `add_event_node`, `set_node_default`, `create_blueprint_variable`, `set_blueprint_variable_properties`, `add_blueprint_function_input`, `add_blueprint_function_output`, `delete_blueprint_function`, `rename_blueprint_function` |
| Blueprint spawn, mesh, physics, and materials | Spawn Blueprint instances into the level, configure physics, assign meshes, tint materials, apply materials, and inspect material slots | `spawn_blueprint_actor`, `set_physics_properties`, `set_static_mesh_properties`, `set_mesh_material_color`, `apply_material_to_actor`, `apply_material_to_blueprint`, `get_actor_material_info`, `get_blueprint_material_info` |
| Asset registry and import | Search assets, inspect dependency/reference graphs, create material instances, and import source files into content folders | `find_assets`, `get_asset_dependencies`, `get_referencers`, `create_material_instance`, `import_asset` |
| DataTable and CurveTable inspection, mutation, lifecycle mutation, validation, row reordering, and table asset creation | Read one DataTable asset's row struct, columns, row names, exported row payloads, and one resolved row by name, read one CurveTable asset's mode, row names, exported curve payloads, and one resolved curve row by name, then create empty DataTable and CurveTable assets, upsert DataTable and CurveTable rows, delete and rename both table row types, validate row import payloads, duplicate rows, and move rows with deterministic readback | `read_data_table_content`, `read_data_table_row`, `read_curve_table_content`, `read_curve_table_row`, `create_data_table_asset`, `create_curve_table_asset`, `upsert_data_table_row`, `upsert_curve_table_row`, `delete_data_table_row`, `delete_curve_table_row`, `rename_data_table_row`, `rename_curve_table_row`, `validate_data_table_row_import`, `validate_curve_table_row_import`, `duplicate_data_table_row`, `move_data_table_row` |
| Material graph inspection and authoring | Create blank materials and material functions, inspect graphs, validate them, update base-material parameter defaults or material-instance overrides, create/delete/bulk-delete/connect/disconnect/replace expressions, bind or disconnect base-material outputs, and run recompile/layout helpers | `create_material_asset`, `create_material_function_asset`, `get_material_expressions`, `get_material_connections`, `get_material_parameters`, `set_material_parameters`, `set_material_instance_parameters`, `validate_material_graph`, `create_material_expression`, `delete_material_expression`, `delete_material_expressions`, `replace_material_expression`, `connect_material_expressions`, `disconnect_material_expressions`, `connect_material_property`, `disconnect_material_property`, `recompile_material`, `layout_material_expressions` |
| Level design helpers | Snap actors to grid, align them, duplicate them, and move editor viewports to actors or locations | `snap_actors_to_grid`, `align_actors`, `duplicate_actor`, `focus_viewport` |
| Landscape inspection and mutation | List landscapes, inspect edit layers and layer info assets, sample world-space height and paint-layer weights at one point or over batches/grids/regions, create flat landscapes, flatten/import heightmaps, paint bounded weight regions, sculpt bounded height regions, and rebuild landscape data | `get_landscapes`, `read_landscape_content`, `sample_landscape_point`, `sample_landscape_points`, `sample_landscape_grid`, `sample_landscape_region`, `sample_landscape_height_region`, `sample_landscape_weight_region`, `create_landscape`, `set_landscape_flat_height`, `import_landscape_heightmap`, `paint_landscape_layer_region`, `sculpt_landscape_height_region`, `rebuild_landscape` |
| Editor orchestration | Inspect selection, viewports, layers, and world partition state; mutate actor selection; inspect or mutate actor-layer membership; and manage layer visibility and lifecycle | `get_selected_actors`, `get_level_viewport_info`, `get_world_partition_info`, `get_all_layers`, `create_layer`, `rename_layer`, `delete_layer`, `get_actor_layers`, `get_actors_in_layer`, `add_actor_to_layer`, `remove_actor_from_layer`, `add_selected_actors_to_layer`, `remove_selected_actors_from_layer`, `select_actors_in_layer`, `deselect_actors_in_layer`, `set_layer_visibility`, `toggle_layer_visibility`, `make_all_layers_visible`, `select_actors`, `clear_actor_selection` |
| Sequencer and MovieScene | Create Level Sequence assets, inspect playback/binding/track/section state, mutate camera-cut and generic master tracks, create/update/remove master-track sections, bind actors, add bound tracks, add float keys to bound property tracks, and update playback range | `create_level_sequence`, `read_level_sequence_content`, `add_camera_cut_track_to_level_sequence`, `add_master_track_to_level_sequence`, `add_section_to_master_track_in_level_sequence`, `set_section_range_in_master_track_in_level_sequence`, `remove_section_from_master_track_in_level_sequence`, `add_actor_possessable_to_level_sequence`, `add_track_to_binding_in_level_sequence`, `add_float_key_to_binding_track_in_level_sequence`, `set_level_sequence_playback_range` |

---

## Phase 8 — Multi-Model & Cloud APIs (PARTIALLY DONE)

Support cloud APIs alongside Ollama, model comparison, API key management.

| Feature | Status |
|---------|--------|
| Ollama local models | ✅ DONE (Phase 3) |
| OpenRouter BYOK | ✅ DONE (Phase 5.5) — with retry, tool fallback, think-block stripping |
| OpenAI API support | ❌ Not started — slot reserved in .env |
| Anthropic API support | ❌ Not started — slot reserved in .env |
| Model performance comparison | ❌ Not started |
| API key management in editor settings | ❌ Not started — currently uses .env file |
| Per-model context/token config | ❌ Not started |

---

## Phase 9 — Engine Surface Expansion & MCP Catalog 🔄 IN PROGRESS (2026-05-08)

Turn the UnrealAI MCP layer into a curated editor/runtime capability catalog by domain. The goal is not a one-tool-per-engine-symbol mirror; the goal is to expose stable tool families that map to how an agent actually works: inspect, analyze, mutate, compile/build, and verify.

**Wave 1 — Blueprint function/variable mutation parity ✅ DONE (2026-05-08).** Five previously routed-but-C++-only Blueprint mutation handlers are now exposed through `Python/unreal_ai_mcp.py`: `set_blueprint_variable_properties`, `add_blueprint_function_input`, `add_blueprint_function_output`, `delete_blueprint_function`, and `rename_blueprint_function`. `create_blueprint_function` also now forwards an optional `return_type`. Tool count **44 → 50**. Added focused pure-Python wrapper coverage in `Python/tests/test_phase9_wave1_mcp_wrappers.py`; **5 passed in 1.04s**.

**Wave 2 — Material graph read-only slice ✅ DONE (2026-05-11).** Added a new C++ handler family `FUnrealAIMaterialCommands` and exposed the first material graph inspection tools through `Python/unreal_ai_mcp.py`: `get_material_expressions`, `get_material_connections`, `get_material_parameters`, and `validate_material_graph`. The first slice intentionally uses runtime/public material APIs under `Engine` rather than taking an immediate dependency on `MaterialEditor`. `get_material_parameters` now returns current default values plus override/editor metadata when UE exposes it, and `validate_material_graph` flags empty graphs, missing output bindings, dead-end expressions, and parameter-identity collisions. Tool count **50 → 54**. Focused live-editor coverage in `Python/tests/test_phase9_wave2_material_readonly.py` now passes **4/4** against the rebuilt playtesting project plugin.

**Wave 3 — Material graph core mutation ✅ DONE (2026-05-11).** Added the first editor-backed material authoring tools using `MaterialEditor`'s `UMaterialEditingLibrary`: `create_material_asset`, `create_material_expression`, `delete_material_expression`, `connect_material_expressions`, `connect_material_property`, `recompile_material`, and `layout_material_expressions`. This is the first true write tranche for 9b and moves the domain beyond inspection into graph authoring. Tool count **54 → 61**. Focused live-editor coverage in `Python/tests/test_phase9_wave3_material_mutation.py` passes **2/2** against the rebuilt playtesting project plugin.

**Wave 4 — Material instance parameter override edits ✅ DONE (2026-05-11).** Added `set_material_instance_parameters` for mutating scalar, vector, texture, and static-switch overrides on existing `UMaterialInstanceConstant` assets. This completes the first 9b parameter-edit tranche without widening prematurely into base-material parameter-expression mutation. Tool count **61 → 62**. Focused live-editor coverage in `Python/tests/test_phase9_wave4_material_instance_parameters.py` passes **2/2**.

**Wave 5 — Material graph disconnect helpers ✅ DONE (2026-05-11).** Added the first 9b-5 ergonomics tools: `disconnect_material_expressions` and `disconnect_material_property`. These make graph surgery reversible without forcing delete-and-recreate flows and establish the first explicit disconnect helper family for material graphs. Tool count **62 → 64**. Focused live-editor coverage in `Python/tests/test_phase9_wave5_material_disconnect.py` passes **2/2**.

**Wave 6 — Material graph replace helper ✅ DONE (2026-05-11).** Added `replace_material_expression` to swap one node for another class while preserving incoming edges, downstream consumers, and material-property bindings when the replacement shape is compatible. This is the next 9b-5 ergonomics slice and removes much of the delete-recreate-rewire churn for common graph refactors. Tool count **64 → 65**. Focused live-editor coverage in `Python/tests/test_phase9_wave6_material_replace.py` passes **1/1**.

**Wave 7 — Material-function coverage and bulk delete helpers ✅ DONE (2026-05-11).** Added `create_material_function_asset` plus function-aware support across the existing graph inspection and mutation tools so the same MCP surface now works on both base materials and material functions. Added `delete_material_expressions` as the first preflighted bulk graph edit helper, which resolves every requested node before mutation starts and avoids partial user-side delete loops. Tool count **65 → 67**. Focused live-editor coverage in `Python/tests/test_phase9_wave7_material_functions_bulk_delete.py` exercises both material-function graph authoring and bulk deletes.

**Wave 8 — Base material parameter-expression mutation ✅ DONE (2026-05-11).** Added `set_material_parameters` for updating scalar, vector, texture, and static-switch defaults on parameter expressions in base material graphs. This closes the remaining 9b gap by exposing the source-material counterpart to the existing material-instance override helper. Tool count **67 → 68**. Focused live-editor coverage in `Python/tests/test_phase9_wave8_material_parameters.py` passes **1/1**.

**Wave 9c-1 — Widget Blueprint creation + read-only inspection ✅ DONE (2026-05-11).** Added a new C++ handler family `FUnrealAIWidgetCommands` plus the first widget blueprint MCP tools: `create_widget_blueprint` and `read_widget_blueprint_content`. The slice uses `UWidgetBlueprint`, `UWidgetTree`, `FDelegateEditorBinding`, `UWidgetAnimation`, and core runtime widget/slot metadata to expose editable widget hierarchy, Canvas layout data, bindings, animations, and named-slot content from the source asset. `UnrealAI.Build.cs` now includes both `UMG` and `UMGEditor`. Tool count **68 → 70**. Focused wrapper coverage in `Python/tests/test_phase9c_wave1_widget_wrappers.py` passes **2/2** and live-editor coverage in `Python/tests/test_phase9c_wave1_widget_blueprints.py` passes **1/1** against the rebuilt playtesting project plugin.

**Wave 9c-2 — Broader common slot/layout inspection ✅ DONE (2026-05-11).** Expanded `read_widget_blueprint_content` so it now serializes common panel-slot subclasses beyond Canvas, including box/content slots (`Button`, `Border`, `HorizontalBox`, `VerticalBox`, `ScrollBox`, `Overlay`, `WidgetSwitcher`, `WindowTitleBarArea`, `SizeBox`, `BackgroundBlur`), grid slots (`GridSlot`, `UniformGridSlot`), wrap slots, stack-box slots, and safe-zone slots. `create_widget_blueprint` also gained an optional seeded-child path so representative slot types can be exercised deterministically during validation. Tool count remains **70**. Focused wrapper coverage in `Python/tests/test_phase9c_wave1_widget_wrappers.py` passes **3/3** and live-editor coverage in `Python/tests/test_phase9c_wave2_widget_slots.py` passes **5/5**.

**Wave 9c-3 — Widget hierarchy mutation ✅ DONE (2026-05-11).** Added the first widget-tree write tranche: `add_widget_to_widget_blueprint`, `remove_widget_from_widget_blueprint`, and `reparent_widget_in_widget_blueprint`. The handlers use `UWidgetTree`, `UPanelWidget`, named-slot host resolution, and `FWidgetBlueprintEditorUtils`-style editor mutation patterns to add children under panel parents or named slots, move existing widgets between parents safely, and remove whole subtrees while preserving deterministic response metadata. Tool count **70 → 73**. Focused wrapper coverage in `Python/tests/test_phase9c_wave1_widget_wrappers.py` now passes **6/6**, and live-editor coverage in `Python/tests/test_phase9c_wave3_widget_hierarchy.py` passes **3/3**.

**Wave 9c-4 — Layout mutation complete ✅ DONE (2026-05-11).** Completed `set_widget_slot_layout_in_widget_blueprint` so it now mutates every slot family currently serialized by `read_widget_blueprint_content`: Canvas slots (`anchors`, `offsets`, `size`, `alignment`, `z_order`), Grid slots (`padding`, `horizontal_alignment`, `vertical_alignment`, `row`, `row_span`, `column`, `column_span`, `layer`, `nudge`), UniformGrid slots (`horizontal_alignment`, `vertical_alignment`, `row`, `column`), HorizontalBox/VerticalBox/ScrollBox/StackBox slots (`padding`, `horizontal_alignment`, `vertical_alignment`, `child_size`), WrapBox slots (`padding`, `horizontal_alignment`, `vertical_alignment`, `fill_empty_space`, `force_new_line`, `fill_span_when_less_than`), SafeZone slots (`padding`, `horizontal_alignment`, `vertical_alignment`, `safe_area_scale`, `is_title_safe`), ScaleBox slots (`horizontal_alignment`, `vertical_alignment`), and BackgroundBlur/Border/Button/Overlay/SizeBox/WidgetSwitcher/WindowTitleBarArea slots (`padding`, `horizontal_alignment`, `vertical_alignment`). Tool count **73 → 74**. Focused wrapper coverage in `Python/tests/test_phase9c_wave1_widget_wrappers.py` passes **9/9**, and live-editor coverage in `Python/tests/test_phase9c_wave4_widget_layout.py` now passes **17/17**.

**Wave 9c-5 — Binding and animation mutation complete ✅ DONE (2026-05-11).** Finished the remaining widget-mutation tranche by expanding `set_widget_property_binding_in_widget_blueprint` to support both function-based bindings and source-property/property-path bindings backed by `FEditorPropertyPath`, and by adding `create_widget_animation_in_widget_blueprint` plus `remove_widget_animation_from_widget_blueprint` for source-asset widget animation authoring backed by `UWidgetAnimation` and `MovieScene`. Tool count **74 → 78**. Focused wrapper coverage in `Python/tests/test_phase9c_wave1_widget_wrappers.py` now passes **14/14**, and live-editor coverage in `Python/tests/test_phase9c_wave5_widget_bindings.py` plus `Python/tests/test_phase9c_wave5_widget_animations.py` passes **3/3**.

**Wave 9d-1 — Landscape edit-layer inspection ✅ DONE (2026-05-12).** Added the first read-only landscape-advanced slice: `read_landscape_content`. The new handler serializes the selected edit-layer index, per-layer visibility/lock/alpha/brush metadata, used paint layers, deduplicated `ULandscapeLayerInfoObject` assets, and target layer names derived from those used paint layers. Tool count **78 → 79**. Focused wrapper coverage in `Python/tests/test_phase9d_wave1_landscape_wrappers.py` passes **1/1**, and live-editor coverage in `Python/tests/test_phase9d_wave1_landscape_inspection.py` passes **1/1** against the rebuilt playtesting project plugin. Live validation also exposed a TCP bridge read race during rapid reconnects; `MCPServerRunnable` now switches accepted client sockets to blocking mode so the live editor bridge no longer drops later commands as zero-byte disconnects.

**Wave 9d-2 — Landscape point sampling ✅ DONE (2026-05-12).** Added `sample_landscape_point` as the next read-only landscape-advanced helper. The new handler samples one world location on a named landscape, returns the resolved component metadata, returns the current world-space height from editor heightmap data, and reports per-layer weights for every deduplicated paint layer on that landscape. Tool count **79 → 80**. Focused wrapper coverage in `Python/tests/test_phase9d_wave2_landscape_sampling_wrappers.py` passes **1/1**, and live-editor coverage in `Python/tests/test_phase9d_wave2_landscape_sampling.py` passes **1/1 in 24.80s** after adding a short post-ping startup settle window to avoid the known early-editor `create_landscape` ensure. Live validation also showed that collision-backed `GetHeightAtLocation` stayed stale after `set_landscape_flat_height`, so the final implementation samples height directly from `FLandscapeComponentDataInterface` instead of collision heightfields.

**Wave 9d-3 — Landscape batch point sampling ✅ DONE (2026-05-12).** Added `sample_landscape_points` as the next read-only landscape-advanced helper. The new handler reuses the same direct editor heightmap sampling and layer-weight logic as `sample_landscape_point`, but accepts multiple world locations in one request and returns a sampled result object for each location along with shared target-layer metadata at the top level. Tool count **80 → 81**. Focused wrapper coverage in `Python/tests/test_phase9d_wave3_landscape_batch_sampling_wrappers.py` passes **1/1**, and live-editor coverage in `Python/tests/test_phase9d_wave3_landscape_batch_sampling.py` passes **1/1 in 31.94s**.

**Wave 9d-4 — Landscape grid sampling ✅ DONE (2026-05-12).** Added `sample_landscape_grid` as the next read-only landscape-advanced helper. The new handler builds a regular world-space sample grid from one origin plus `step_x`, `step_y`, `count_x`, and `count_y`, then reuses the same direct editor heightmap sampling and layer-weight path as the single-point and batched samplers for each grid point. Tool count **81 → 82**. Focused wrapper coverage in `Python/tests/test_phase9d_wave4_landscape_grid_sampling_wrappers.py` passes **1/1**, and live-editor coverage in `Python/tests/test_phase9d_wave4_landscape_grid_sampling.py` passes **1/1 in 21.32s**.

**Wave 9d-5 — Landscape region sampling ✅ DONE (2026-05-12).** Added `sample_landscape_region` as the next read-only landscape-advanced helper. The new handler accepts one bounded region via `min_corner`, `max_corner`, `step_x`, and `step_y`, resolves inclusive X/Y sample counts from those bounds, and reuses the same direct editor heightmap sampling and layer-weight path as the single-point, batch, and grid samplers for each generated point. Tool count **82 → 83**. Focused wrapper coverage in `Python/tests/test_phase9d_wave5_landscape_region_sampling_wrappers.py` passes **1/1**, and live-editor coverage in `Python/tests/test_phase9d_wave5_landscape_region_sampling.py` passes **1/1 in 20.86s**.

**Wave 9d-6 — Landscape height-region raster sampling ✅ DONE (2026-05-12).** Added `sample_landscape_height_region` as the next read-only landscape-advanced helper. The new handler accepts the same bounded region inputs as `sample_landscape_region`, but returns a denser height-only raster via `height_rows` plus `height_min_world` and `height_max_world`, allowing higher sample density without the per-point layer-weight payload. Tool count **83 → 84**. Focused wrapper coverage in `Python/tests/test_phase9d_wave6_landscape_height_region_wrappers.py` passes **1/1**, and live-editor coverage in `Python/tests/test_phase9d_wave6_landscape_height_region.py` passes **1/1 in 22.08s**.

**Wave 9d-7 — Landscape weight-region raster sampling ✅ DONE (2026-05-12).** Added `sample_landscape_weight_region` as the remaining read-only weight-query slice. The new handler accepts one bounded region plus `step_x`, `step_y`, and a target `layer_name`, then returns normalized `weight_rows` plus min/max summaries for that layer using editor-side landscape weight data rather than collision or runtime proxies. Tool count **84 → 85**. Focused wrapper coverage is part of `Python/tests/test_phase9d_wave7to10_landscape_finish_wrappers.py`, which passes **4/4**, and combined live-editor coverage in `Python/tests/test_phase9d_wave7to10_landscape_finish.py` passes **3/3**.

**Wave 9d-8 — Safe landscape layer paint-region mutation ✅ DONE (2026-05-12).** Added `paint_landscape_layer_region` for bounded, uniform layer painting over a named target layer or the built-in `Visibility` layer. The new handler resolves clamped local extents from world-space bounds and applies editor-backed alphamap writes through `TAlphamapAccessor<false>`, returning the affected extent plus the normalized and byte-scaled weight that was applied. Tool count **85 → 86**. Focused wrapper coverage is part of `Python/tests/test_phase9d_wave7to10_landscape_finish_wrappers.py` (**4/4 passed**), and combined live-editor coverage in `Python/tests/test_phase9d_wave7to10_landscape_finish.py` passes **3/3**.

**Wave 9d-9 — Safe landscape height-region sculpt mutation ✅ DONE (2026-05-12).** Added `sculpt_landscape_height_region` for bounded region flatten/sculpt edits to a uniform world-space height. The new handler converts world bounds to clamped landscape extents, converts the requested world height to landscape texture height units, and applies the edit through `FHeightmapAccessor<false>`. Tool count **86 → 87**. Focused wrapper coverage is part of `Python/tests/test_phase9d_wave7to10_landscape_finish_wrappers.py` (**4/4 passed**), and combined live-editor coverage in `Python/tests/test_phase9d_wave7to10_landscape_finish.py` passes **3/3**.

**Wave 9d-10 — Landscape rebuild helper ✅ DONE (2026-05-12).** Added `rebuild_landscape` as the explicit refresh helper for post-mutation workflows. The new handler forces a full landscape layer update and marks the asset dirty so agent-side edit flows can request a deterministic rebuild step after paint/sculpt operations. Tool count **87 → 88**. Focused wrapper coverage is part of `Python/tests/test_phase9d_wave7to10_landscape_finish_wrappers.py` (**4/4 passed**), and combined live-editor coverage in `Python/tests/test_phase9d_wave7to10_landscape_finish.py` passes **3/3**.

**Wave 9e-1 — Editor-context inspection ✅ DONE (2026-05-12).** Added the first read-only level/editor orchestration slice: `get_selected_actors` and `get_level_viewport_info`. The new editor command helpers expose current outliner selection via `GEditor->GetSelectedActors()` and open level-editor viewport camera state via `GEditor->GetLevelViewportClients()`, returning selected-actor metadata plus per-viewport location, rotation, FOV, and realtime/perspective flags. Tool count **88 → 90**. Focused wrapper coverage in `Python/tests/test_phase9e_wave1_editor_context_wrappers.py` passes **2/2**, and live-editor coverage in `Python/tests/test_phase9e_wave1_editor_context.py` passes **2/2** against the rebuilt playtesting project plugin.

**Wave 9e-2 — Editor selection mutation ✅ DONE (2026-05-12).** Added the first write tranche for level/editor orchestration: `select_actors` and `clear_actor_selection`. The new editor command helpers preflight every requested actor before mutating selection, support both replace and append flows, clear selection through `GEditor->SelectNone`, and expose deterministic readback through the existing `get_selected_actors` inspection helper. Tool count **90 → 92**. Focused wrapper coverage in `Python/tests/test_phase9e_wave2_editor_selection_wrappers.py` passes **2/2**, and live-editor coverage in `Python/tests/test_phase9e_wave2_editor_selection.py` passes **2/2** against the rebuilt playtesting project plugin.

**Wave 9e-3 — Actor layer membership inspection ✅ DONE (2026-05-12).** Added the next read-only level/editor orchestration slice: `get_actor_layers` and `get_actors_in_layer`. The new editor command helpers expose per-actor editor layer membership directly from `AActor::Layers` and resolve layer-to-actor membership through `ULayersSubsystem`, returning deterministic layer names, actor metadata, and count summaries while preflighting missing layers. Tool count **92 → 94**. Focused wrapper coverage in `Python/tests/test_phase9e_wave3_actor_layers_wrappers.py` passes **2/2**, and live-editor coverage in `Python/tests/test_phase9e_wave3_actor_layers.py` passes **2/2** against the rebuilt playtesting project plugin.

**Wave 9e-4 — Actor layer membership mutation ✅ DONE (2026-05-12).** Added the next write tranche for level/editor orchestration: `add_actor_to_layer` and `remove_actor_from_layer`. The new editor command helpers reuse `ULayersSubsystem` mutation semantics so add requests can create missing layers automatically while remove requests no-op cleanly when the actor is no longer assigned, and both return deterministic post-mutation layer readback from `AActor::Layers`. Tool count **94 → 96**. Focused wrapper coverage in `Python/tests/test_phase9e_wave4_actor_layer_mutation_wrappers.py` passes **2/2**, and live-editor coverage in `Python/tests/test_phase9e_wave4_actor_layer_mutation.py` passes **2/2** against the rebuilt playtesting project plugin.

**Wave 9e-5 — Layer-based selection mutation ✅ DONE (2026-05-12).** Added the next write tranche for level/editor orchestration: `select_actors_in_layer` and `deselect_actors_in_layer`. The new editor command helpers reuse `ULayersSubsystem::SelectActorsInLayer` for both select and deselect flows, preserve replace-vs-append semantics, and compute deterministic `changed` state by comparing the selected-actor set before and after each mutation. Tool count **96 → 98**. Focused wrapper coverage in `Python/tests/test_phase9e_wave5_layer_selection_wrappers.py` passes **2/2**, and live-editor coverage in `Python/tests/test_phase9e_wave5_layer_selection.py` passes **2/2** against the rebuilt playtesting project plugin.

**Wave 9e-6 — Layer catalog inspection ✅ DONE (2026-05-12).** Added the next read-only level/editor orchestration slice: `get_all_layers`. The new editor command helper enumerates every known editor layer through `ULayersSubsystem::AddAllLayersTo`, returns deterministic layer-name ordering, exposes `ULayer::IsVisible()` state, and includes per-layer actor counts for quick catalog and visibility inspection. Tool count **98 → 99**. Focused wrapper coverage in `Python/tests/test_phase9e_wave6_layer_catalog_wrappers.py` passes **1/1**, and live-editor coverage in `Python/tests/test_phase9e_wave6_layer_catalog.py` passes **1/1** against the rebuilt playtesting project plugin.

**Wave 9e-7 — Layer visibility mutation ✅ DONE (2026-05-12).** Added the next write slice for level/editor orchestration: `set_layer_visibility`. The new editor command helper preflights existing layer names, applies visibility mutation through `ULayersSubsystem::SetLayerVisibility`, and returns deterministic pre/post visibility readback plus the updated layer catalog entry. Tool count **99 → 100**. Focused wrapper coverage in `Python/tests/test_phase9e_wave7_layer_visibility_wrappers.py` passes **1/1**, and live-editor coverage in `Python/tests/test_phase9e_wave7_layer_visibility.py` passes **1/1** against the rebuilt playtesting project plugin.

**Wave 9e-8 — Layer visibility convenience helpers ✅ DONE (2026-05-12).** Added the next write slice for level/editor orchestration: `toggle_layer_visibility` and `make_all_layers_visible`. The new editor command helpers preflight existing layer names for single-layer toggles, reuse `ULayersSubsystem::ToggleLayerVisibility` and `ULayersSubsystem::MakeAllLayersVisible`, and return deterministic post-mutation layer catalog summaries so hidden-layer recovery is observable in one round-trip. Tool count **100 → 102**. Focused wrapper coverage in `Python/tests/test_phase9e_wave8_layer_visibility_convenience_wrappers.py` passes **2/2**, and live-editor coverage in `Python/tests/test_phase9e_wave8_layer_visibility_convenience.py` passes **1/1** against the rebuilt playtesting project plugin.

**Wave 9e-9 — Explicit layer creation ✅ DONE (2026-05-12).** Added the next write slice for level/editor orchestration: `create_layer`. The new editor command helper preflights duplicate layer names, applies explicit empty-layer creation through `ULayersSubsystem::CreateLayer`, and returns deterministic readback for the new visible empty layer through the existing layer-catalog serializer. Tool count **102 → 103**. Focused wrapper coverage in `Python/tests/test_phase9e_wave9_layer_create_wrappers.py` passes **1/1**, and live-editor coverage in `Python/tests/test_phase9e_wave9_layer_create.py` passes **1/1** against the rebuilt playtesting project plugin.

**Wave 9e-10 — Selected-actor layer mutation ✅ DONE (2026-05-12).** Added the next write slice for level/editor orchestration: `add_selected_actors_to_layer` and `remove_selected_actors_from_layer`. The new editor command helpers reuse `ULayersSubsystem::AddSelectedActorsToLayer` and `ULayersSubsystem::RemoveSelectedActorsFromLayer`, preserve the current editor selection, surface deterministic selected-count and layer-membership summaries, and preflight empty selection or missing-layer cases so the bridge does not mutate ambiguously. Tool count **103 → 105**. Focused wrapper coverage in `Python/tests/test_phase9e_wave10_selected_actor_layers_wrappers.py` passes **2/2**, and live-editor coverage in `Python/tests/test_phase9e_wave10_selected_actor_layers.py` passes **1/1** against the rebuilt playtesting project plugin.

**Wave 9e-11 — Layer lifecycle helpers ✅ DONE (2026-05-12).** Added the final layer-mutation slice for level/editor orchestration: `rename_layer` and `delete_layer`. The new editor command helpers preflight duplicate rename targets, keep same-name renames deterministic no-ops, reuse `ULayersSubsystem::RenameLayer` and `ULayersSubsystem::DeleteLayer`, and return stable post-mutation snapshots including removed-actor summaries on delete. Tool count **105 → 107**. Focused wrapper coverage in `Python/tests/test_phase9e_wave11_layer_lifecycle_wrappers.py` passes **2/2**, and live-editor coverage in `Python/tests/test_phase9e_wave11_layer_lifecycle.py` passes **1/1** against the rebuilt playtesting project plugin.

**Wave 9e-12 — World Partition inspection ✅ DONE (2026-05-12).** Added the last read-only editor-orchestration slice: `get_world_partition_info`. The new helper reports whether the current editor world is partitioned, whether the `UWorldPartitionSubsystem` is available, whether streaming is complete, and the current `UWorldPartition` object identity when present, which closes the remaining world-partition inspection gap in the tranche definition. Tool count **107 → 108**. Focused wrapper coverage in `Python/tests/test_phase9e_wave12_world_partition_wrappers.py` passes **1/1**, and live-editor coverage in `Python/tests/test_phase9e_wave12_world_partition.py` passes **1/1** against the rebuilt playtesting project plugin. Phase **9e** is now complete.

### 9b Wave Plan

| Wave | Scope | Status |
|------|-------|--------|
| 9b-1 | Graph inspection + parameter/default inspection | ✅ DONE |
| 9b-2 | Graph validation (empty graphs, missing outputs, dead-end expressions, parameter collisions) | ✅ DONE |
| 9b-3 | Core graph mutation: create material assets, create/delete expressions, connect graph edges, connect material properties, recompile/layout | ✅ DONE |
| 9b-4 | Parameter edits: base-material default scalar/vector/texture/static-switch writes plus material-instance override helpers | ✅ DONE |
| 9b-5 | Graph ergonomics and higher-level edits: disconnect/replace helpers, safer bulk edits, material-function coverage | ✅ DONE |

### 9c Wave Plan

| Wave | Scope | Primary UE module families | Status |
|------|-------|----------------------------|--------|
| 9c-1 | Widget asset creation plus editable tree, binding, animation, and named-slot inspection | `UMGEditor` (`UWidgetBlueprint`), runtime `UMG` (`UWidgetTree`, `UWidget`, `UPanelWidget`, `CanvasPanelSlot`) | ✅ DONE |
| 9c-2 | Broader slot/layout inspection across common panel-slot subclasses | runtime `UMG` slot classes (`CanvasPanelSlot`, box/grid/overlay/wrap/safe-zone slots) | ✅ DONE |
| 9c-3 | Hierarchy mutation: add/remove/reparent widgets safely inside source trees | `UMGEditor`, `WidgetTree`, `FWidgetBlueprintEditorUtils` | ✅ DONE |
| 9c-4 | Layout mutation: anchors, offsets, alignment, z-order, and slot-specific settings | runtime `UMG` slot setter APIs | ✅ DONE |
| 9c-5 | Binding and animation mutation helpers | `FDelegateEditorBinding`, `UWidgetAnimation`, `MovieScene` | ✅ DONE |

### 9d Wave Plan

| Wave | Scope | Primary UE module families | Status |
|------|-------|----------------------------|--------|
| 9d-1 | Read-only landscape content inspection: selected edit layer, edit-layer metadata, used paint layers, and layer info assets | runtime `Landscape` (`ALandscape`, `FLandscapeLayer`, `ULandscapeLayerInfoObject`) | ✅ DONE |
| 9d-2 | Read-only point sampling: world-space height plus per-layer weights at a location | runtime `Landscape` height/weight APIs plus editor-side `FLandscapeComponentDataInterface` | ✅ DONE |
| 9d-3 | Read-only batch point sampling: multiple sampled locations in one request | runtime `Landscape` height/weight APIs plus editor-side `FLandscapeComponentDataInterface` | ✅ DONE |
| 9d-4 | Read-only grid sampling: regular world-space sample grids over one landscape | runtime `Landscape` height/weight APIs plus editor-side `FLandscapeComponentDataInterface` | ✅ DONE |
| 9d-5 | Read-only region sampling: bounded rectangular areas resolved into inclusive sample grids | runtime `Landscape` height/weight APIs plus editor-side `FLandscapeComponentDataInterface` | ✅ DONE |
| 9d-6 | Read-only height-raster region sampling: bounded rectangular areas returned as dense height rows | runtime `Landscape` editor height sampling via `FLandscapeComponentDataInterface` | ✅ DONE |
| 9d-7 | Read-only weight-raster region sampling: bounded rectangular areas returned as dense normalized weight rows for one target layer | editor `LandscapeEdit` weight access via `FLandscapeEditDataInterface` | ✅ DONE |
| 9d-8 | Safe paint-region mutation: bounded uniform layer writes over one target layer | editor `LandscapeEdit` alphamap writes via `TAlphamapAccessor<false>` | ✅ DONE |
| 9d-9 | Safe height-region sculpt mutation: bounded uniform world-height writes | editor `LandscapeEdit` height writes via `FHeightmapAccessor<false>` | ✅ DONE |
| 9d-10 | Explicit rebuild helper after landscape edit-layer mutation | `ALandscape::ForceLayersFullUpdate` and package-dirty editor refresh | ✅ DONE |

### 9e Wave Plan

| Wave | Scope | Primary UE module families | Status |
|------|-------|----------------------------|--------|
| 9e-1 | Read-only editor-context inspection: selected actors and open level-editor viewport camera state | `UnrealEd`, `LevelEditor`, `Engine/Selection` | ✅ DONE |
| 9e-2 | Selection mutation: replace, append, and clear actor selection in the editor outliner | `UnrealEd`, `Engine/Selection` | ✅ DONE |
| 9e-3 | Read-only actor layer membership inspection: inspect one actor's editor layers or list actors in one editor layer | `UnrealEd`, `Layers`, `AActor::Layers`, `ULayersSubsystem` | ✅ DONE |
| 9e-4 | Actor layer membership mutation: add or remove one actor from one editor layer with deterministic readback | `UnrealEd`, `Layers`, `AActor::Layers`, `ULayersSubsystem` | ✅ DONE |
| 9e-5 | Layer-based selection mutation: select or deselect all actors in one editor layer with deterministic selection-state readback | `UnrealEd`, `Layers`, `Engine/Selection`, `ULayersSubsystem` | ✅ DONE |
| 9e-6 | Read-only layer catalog inspection: list all known editor layers with visibility and actor-count summaries | `UnrealEd`, `Layers`, `ULayer`, `ULayersSubsystem` | ✅ DONE |
| 9e-7 | Layer visibility mutation: set one existing editor layer visible or hidden with deterministic readback | `UnrealEd`, `Layers`, `ULayer`, `ULayersSubsystem` | ✅ DONE |
| 9e-8 | Layer visibility convenience helpers: toggle one existing layer or restore all layers visible with deterministic catalog readback | `UnrealEd`, `Layers`, `ULayer`, `ULayersSubsystem` | ✅ DONE |
| 9e-9 | Explicit layer creation: create one empty visible editor layer with deterministic catalog readback | `UnrealEd`, `Layers`, `ULayer`, `ULayersSubsystem` | ✅ DONE |
| 9e-10 | Selected-actor layer mutation: add or remove the current editor selection from one editor layer with deterministic readback | `UnrealEd`, `Layers`, `Engine/Selection`, `ULayersSubsystem` | ✅ DONE |
| 9e-11 | Layer lifecycle helpers: rename or delete one editor layer with deterministic readback | `UnrealEd`, `Layers`, `ULayer`, `ULayersSubsystem` | ✅ DONE |
| 9e-12 | Read-only world-partition inspection: report partitioned-world state and streaming readiness for the current editor world | `UnrealEd`, `WorldPartition`, `UWorldPartitionSubsystem` | ✅ DONE |

### Phase 9 Tranche Plan

| Tranche | Domain | Primary UE module families | First target MCP families | Status |
|---------|--------|----------------------------|---------------------------|--------|
| 9a | Blueprint parity | `Kismet`, `BlueprintGraph`, `KismetCompiler` | Variable metadata mutation, function signature mutation, rename/delete flows | ✅ DONE |
| 9b | Material graph | `MaterialEditor`, runtime material classes under `Engine` | Read-only graph inspection, graph validation, expression create/delete/connect/disconnect/replace, parameter and instance edits, recompile/layout helpers | ✅ DONE |
| 9c | UMG / Widget Blueprint | `UMGEditor`, runtime `UMG`, shared `Kismet` surfaces | Widget tree inspection, hierarchy mutation, layout mutation, event/data binding mutation, and widget animation mutation | ✅ DONE |
| 9d | Landscape advanced | `LandscapeEditor`, `LandscapeEditorUtilities`, runtime `Landscape` | Edit-layer inspection, layer info assets, weight/height queries, safe paint/flatten/sculpt tranche, rebuild helpers | ✅ DONE |
| 9e | Level / editor orchestration | `LevelEditor`, `UnrealEd`, `Layers`, `WorldPartition` | Viewport/editor subsystem helpers, selection inspection/mutation, layer membership, outliner/world-partition orchestration | ✅ DONE |
| 9f | Sequencer / MovieScene | `Sequencer`, `MovieScene`, `LevelSequence` | Sequence inspection, track/section/key mutation, binding helpers, playback helpers | ✅ DONE |
| 9g | Data assets / tables | `DataTableEditor`, runtime data-asset and table surfaces | Row CRUD, curve edits, asset creation, validation helpers | ✅ DONE |
| 9h | Animation / AnimBlueprint | `AnimGraph`, `Persona`, animation blueprint editor surfaces | Anim graph inspection, state-machine primitives, parameterized animation helpers | ✅ DONE |
| 9i | Niagara / AI assets | `NiagaraEditor`, runtime `Niagara`, `BehaviorTreeEditor`, `AIModule`, `GameplayTasks` | Niagara system/emitter inspection and validation, Blackboard assets, and constrained Behavior Tree authoring helpers | ✅ DONE |
| 9j | Procedural Content Generation (PCG) | `PCG`, `PCGEditor`, `PCGGeometryScriptInterop`, `WorldPartition`, `ComputeFramework` | PCG graph/component inspection, graph authoring, generation modes, world-partition integration, shape grammar, GPU/HLSL, editor-mode tooling, and runtime/debug helpers | 🔄 IN PROGRESS |

### 9f Wave Plan

| Wave | Scope | Primary UE module families | Status |
|------|-------|----------------------------|--------|
| 9f-1 | Level Sequence asset creation plus read-only MovieScene inspection for playback metadata, bindings, tracks, sections, spawnables, and possessables | `LevelSequence`, `MovieScene`, `LevelSequenceEditor`, `AssetTools`, `EditorScriptingUtilities` | ✅ DONE |
| 9f-2 | Camera-cut master-track mutation with idempotent readback | `LevelSequence`, `MovieScene`, `MovieSceneTracks`, `EditorScriptingUtilities` | ✅ DONE |
| 9f-3 | Generic unbound master-track mutation by track class with idempotent readback | `LevelSequence`, `MovieScene`, `MovieSceneTracks`, `EditorScriptingUtilities` | ✅ DONE |
| 9f-4 | Generic master-track section creation with explicit frame ranges | `LevelSequence`, `MovieScene`, `MovieSceneTracks`, `EditorScriptingUtilities` | ✅ DONE |
| 9f-5 | Generic master-track section range mutation with explicit frame ranges | `LevelSequence`, `MovieScene`, `MovieSceneTracks`, `EditorScriptingUtilities` | ✅ DONE |
| 9f-6 | Generic master-track section removal by section index | `LevelSequence`, `MovieScene`, `MovieSceneTracks`, `EditorScriptingUtilities` | ✅ DONE |
| 9f-7 | Actor possessable binding creation with deterministic idempotent readback | `LevelSequence`, `MovieScene`, `EditorScriptingUtilities`, `Engine` | ✅ DONE |
| 9f-8 | Bound track creation on existing bindings by track class with deterministic idempotent readback | `LevelSequence`, `MovieScene`, `MovieSceneTracks`, `EditorScriptingUtilities` | ✅ DONE |
| 9f-9 | Float property-key mutation on bound float tracks with section auto-create and serialized key readback | `LevelSequence`, `MovieScene`, `MovieSceneTracks`, `EditorScriptingUtilities` | ✅ DONE |
| 9f-10 | Sequence playback-range mutation with deterministic readback | `LevelSequence`, `MovieScene`, `EditorScriptingUtilities` | ✅ DONE |

### 9g Wave Plan

| Wave | Scope | Primary UE module families | Status |
|------|-------|----------------------------|--------|
| 9g-1 | Read-only DataTable inspection for row structs, columns, row names, and exported row payloads | `Engine`, `Json`, runtime `DataTable` surfaces | ✅ DONE |
| 9g-2 | Read-only DataTable row inspection by row name with resolved key-field handling | `Engine`, `Json`, runtime `DataTable` surfaces | ✅ DONE |
| 9g-3 | Empty DataTable asset creation for a supplied row struct with deterministic readback | `Engine`, `UnrealEd`, `AssetTools`, runtime `DataTable` surfaces | ✅ DONE |
| 9g-4 | DataTable row upsert by row name with JSON-backed row payload import and deterministic row readback | `Engine`, `Json`, `JsonUtilities`, runtime `DataTable` surfaces | ✅ DONE |
| 9g-5 | DataTable row delete by row name with deterministic post-delete table readback | `Engine`, runtime `DataTable` surfaces | ✅ DONE |
| 9g-6 | DataTable row rename by row name with deterministic renamed-row readback and key-field synchronization | `Engine`, `UnrealEd`, `DataTableEditor` surfaces | ✅ DONE |
| 9g-7 | DataTable row duplication by source row name with deterministic new-row readback and key-field synchronization | `Engine`, `UnrealEd`, `DataTableEditor` surfaces | ✅ DONE |
| 9g-8 | DataTable row reordering by row name with deterministic table-order readback | `Engine`, `UnrealEd`, `DataTableEditor` surfaces | ✅ DONE |
| 9g-9 | Read-only CurveTable inspection for curve mode, row names, and exported curve payloads | `Engine`, `Json`, runtime `CurveTable` surfaces | ✅ DONE |
| 9g-10 | Read-only CurveTable row inspection by row name with deterministic curve-key readback | `Engine`, `Json`, runtime `CurveTable` surfaces | ✅ DONE |
| 9g-11 | Empty CurveTable asset creation with explicit curve-table mode and deterministic readback | `Engine`, `UnrealEd`, `AssetRegistry`, runtime `CurveTable` surfaces | ✅ DONE |
| 9g-12 | CurveTable row upsert by row name with JSON-backed key import and deterministic row readback | `Engine`, `Json`, runtime `CurveTable` surfaces | ✅ DONE |
| 9g-13 | CurveTable row delete by row name with deterministic post-delete table readback | `Engine`, runtime `CurveTable` surfaces | ✅ DONE |
| 9g-14 | CurveTable row rename by row name with deterministic renamed-row readback | `Engine`, runtime `CurveTable` surfaces | ✅ DONE |
| 9g-15 | Data-table and curve-table validation helpers for row-struct, key-field, and import-shape sanity checks | `Engine`, `Json`, `JsonUtilities`, table/curve asset surfaces | ✅ DONE |

### 9h Wave Plan

| Wave | Scope | Primary UE module families | Status |
|------|-------|----------------------------|--------|
| 9h-1 | Read-only AnimBlueprint inspection for target skeleton, generated class, preview mesh, anim-layer graphs, state-machine summaries, and exposed variables | `Engine`, `AnimGraph`, `Persona`, `AnimationBlueprintEditor` | ✅ DONE |
| 9h-2 | Empty AnimBlueprint asset creation for a supplied skeleton and optional parent class with deterministic inspection readback | `Engine`, `UnrealEd`, `AssetTools`, `AnimationBlueprintEditor` | ✅ DONE |
| 9h-3 | Read-only state-machine inspection by AnimBlueprint and machine name with states, entry wiring, transitions, and per-state graph summaries | `AnimGraph`, `Persona`, `AnimationBlueprintEditor` | ✅ DONE |
| 9h-4 | State-machine creation inside an AnimBlueprint's AnimGraph with deterministic post-create readback | `AnimGraph`, `Persona`, `AnimationBlueprintEditor` | ✅ DONE |
| 9h-5 | State creation inside a named state machine with deterministic topology readback and per-state graph creation | `AnimGraph`, `Persona`, `AnimationBlueprintEditor` | ✅ DONE |
| 9h-6 | State rename and delete flows inside a named state machine with deterministic topology readback | `AnimGraph`, `Persona`, `AnimationBlueprintEditor` | ✅ DONE |
| 9h-7 | Transition creation and deletion between existing states with deterministic topology readback | `AnimGraph`, `Persona`, `AnimationBlueprintEditor` | ✅ DONE |
| 9h-8 | Transition-rule helpers for common guards (`always_true`, bool variable, enum/int equality) with deterministic rule-summary readback | `AnimGraph`, `BlueprintGraph`, `KismetCompiler` | ✅ DONE |
| 9h-9 | Sequence-player state binding helper that seeds or replaces one state's pose graph from a supplied `AnimSequence` asset | `Engine`, `AnimGraph`, `Persona` | ✅ DONE |
| 9h-10 | BlendSpace / AimOffset state binding helper that seeds or replaces one state's pose graph from a supplied `BlendSpace`-family asset | `Engine`, `AnimGraph`, `Persona` | ✅ DONE |
| 9h-11 | Asset-player parameter mutation for state-bound players: loop, play rate, start position, blend-space axes, and sync-group metadata | `Engine`, `AnimGraph`, `Persona` | ✅ DONE |
| 9h-12 | Read-only AnimBlueprint validation helpers for skeleton compatibility, asset-type sanity, unresolved player assets, and compile/readback health | `Engine`, `AnimGraph`, `KismetCompiler`, `Persona` | ✅ DONE |

Recommended batching for 9h:

- `9h-1` + `9h-2` landed together through the shared inspection serializer and `UAnimBlueprintFactory` asset-creation seam.
- `9h-3` + `9h-4` landed together through the shared state-machine graph discovery, graph filtering, and naming seam.
- `9h-5` + `9h-6` landed together through the shared state-machine lookup, state-node naming, and state readback seam.
- `9h-7` + `9h-8` landed together through the shared transition-node lookup, transition-rule graph reset, concrete Kismet equality-call authoring, and transition serializer seam.
- `9h-9` + `9h-10` landed together through the shared state pose-graph reset, concrete asset-player node insertion, and state serializer seam.
- `9h-11` landed separately through the shared state-bound asset-player mutation/readback seam.
- `9h-12` landed separately through a shared compile-plus-readback validation seam that layers structured issue reporting on top of the now-stable asset-player summaries.

**Wave 9i-1 + 9i-2 — Niagara System inspection + asset creation ✅ DONE (2026-05-13).** Added the first Niagara tranche through one shared system serializer and one asset-creation seam: `read_niagara_system_content` now exposes deterministic system spawn/update script metadata, compile-status booleans, exposed user-parameter summaries, emitter-handle metadata, and renderer summaries, while `create_niagara_system_asset` now creates empty or template-backed Niagara Systems and immediately reuses the same readback. Tool count **149 → 151**. Focused wrapper coverage in `Python/tests/test_phase9i_wave1_2_niagara_system_wrappers.py` passes **2/2**, and live-editor coverage in `Python/tests/test_phase9i_wave1_2_niagara_system.py` passes **2/2** after rebuilding `playtestingEditor`.

**Wave 9i-3 + 9i-4 — Niagara emitter inspection + emitter-handle lifecycle ✅ DONE (2026-05-13).** Extended the Niagara tranche through one shared per-emitter serializer and one emitter-handle lifecycle seam: `read_niagara_system_emitter` now exposes script-usage summaries, compile-status readback, renderer summaries, renderer-binding parameter summaries, event handlers, simulation stages, and emitter-instance metadata for one resolved handle, while `add_niagara_emitter_to_system`, `duplicate_niagara_system_emitter`, `rename_niagara_system_emitter`, and `remove_niagara_system_emitter` reuse the same readback after deterministic compile/save flows. Tool count **151 → 156**. Focused wrapper coverage in `Python/tests/test_phase9i_wave3_4_niagara_emitter_wrappers.py` passes **3/3**, and live-editor coverage in `Python/tests/test_phase9i_wave3_4_niagara_emitter.py` passes **1/1** after a clean `playtestingEditor` rebuild.

**Wave 9i-5 + 9i-6 — Niagara user-parameter mutation + validation ✅ DONE (2026-05-13).** Extended the Niagara tranche through one shared exposed-parameter serializer and one compile-plus-readback validation seam: `read_niagara_system_content` now includes typed exposed user-parameter default values plus structured system-script summaries, `set_niagara_system_user_parameters` now updates or seeds common float, bool, vector, color, and object-backed exposed defaults, and `validate_niagara_system` now compiles the system and reports structured issues for compile health, missing object-backed references, unresolved renderer user-parameter bindings, and disabled-emitter state sanity. Tool count **156 → 158**. Focused wrapper coverage in `Python/tests/test_phase9i_wave5_6_niagara_user_parameters_wrappers.py` passes **2/2**, and live-editor coverage in `Python/tests/test_phase9i_wave5_6_niagara_user_parameters.py` passes **2/2** after a clean `playtestingEditor` rebuild.

**Wave 9i-7 + 9i-8 — Blackboard inspection + asset/key lifecycle ✅ DONE (2026-05-13).** Extended the AI-asset tranche through one shared Blackboard serializer and one ordered entry-mutation seam: `read_blackboard_content` now exposes parent-chain metadata, local and inherited key summaries, key-type/default-value metadata, and object/class base-class filters, `create_blackboard_asset` now creates empty or parent-linked Blackboard assets and immediately reuses the same readback, and `update_blackboard_keys` now applies ordered add, update, rename, and delete flows for common bool, int, float, name, string, vector, object, and class keys with deterministic post-mutation inspection. Tool count **158 → 161**. Focused wrapper coverage in `Python/tests/test_phase9i_wave7_8_blackboard_wrappers.py` passes **3/3**, and live-editor coverage in `Python/tests/test_phase9i_wave7_8_blackboard.py` passes **2/2** after a clean `playtestingEditor` rebuild.

**Wave 9i-9 + 9i-10 — Behavior Tree inspection + asset creation ✅ DONE (2026-05-13).** Extended the AI-asset tranche through one shared Behavior Tree topology serializer and one asset-creation seam: `read_behavior_tree_content` now exposes linked Blackboard metadata, root composite shape, nested node topology, flat node summaries, and decorator/service/task readback, while `create_behavior_tree_asset` now creates a Behavior Tree asset, seeds a deterministic selector-root scaffold, optionally assigns a linked Blackboard, and immediately reuses the same readback. Tool count **161 → 163**. Focused wrapper coverage in `Python/tests/test_phase9i_wave9_10_behavior_tree_wrappers.py` passes **2/2**, and live-editor coverage in `Python/tests/test_phase9i_wave9_10_behavior_tree.py` passes **2/2** after a clean `playtestingEditor` rebuild.

**Wave 9i-11 + 9i-12 — Behavior Tree subtree authoring + property/validation helpers ✅ DONE (2026-05-13).** Finished the AI-asset tranche through one graph-backed mutation seam and one property/validation seam: `update_behavior_tree_subtree` now restores and mutates `UBehaviorTreeGraph` data for constrained composite/task/decorator/service add/remove flows inside an existing tree, `set_behavior_tree_node_properties` now updates Blackboard selectors, decorator abort modes, and graph-backed enabled state by topology path, `read_behavior_tree_content` now exposes selector metadata plus enabled-state summaries, and `validate_behavior_tree` reports structured issues for missing roots, unresolved Blackboard keys, disabled or development-only nodes, invalid decorator abort settings, and empty composite branches. Tool count **163 → 166**. Focused wrapper coverage in `Python/tests/test_phase9i_wave11_12_behavior_tree_wrappers.py` passes **3/3**, and live-editor coverage in `Python/tests/test_phase9i_wave11_12_behavior_tree.py` passes **2/2** after restarting the editor on the rebuilt `playtestingEditor` plugin.

### 9i Wave Plan

| Wave | Scope | Primary UE module families | Status |
|------|-------|----------------------------|--------|
| 9i-1 | Read-only Niagara System inspection for emitter handles, exposed user parameters, renderer summaries, and compile-status metadata | `NiagaraEditor`, runtime `Niagara`, `AssetRegistry` | ✅ DONE |
| 9i-2 | Niagara System asset creation from an empty baseline or template system with deterministic inspection readback | `NiagaraEditor`, runtime `Niagara`, `UnrealEd`, `AssetTools` | ✅ DONE |
| 9i-3 | Read-only Niagara Emitter inspection by system and emitter handle with script-usage, renderer, and parameter summaries | `NiagaraEditor`, runtime `Niagara` | ✅ DONE |
| 9i-4 | Niagara emitter-handle lifecycle helpers: add existing emitter assets, duplicate/remove handles, and rename emitter instances inside one system | `NiagaraEditor`, runtime `Niagara` | ✅ DONE |
| 9i-5 | Niagara user-parameter mutation helpers for common scalar, bool, vector, color, and object-backed defaults with deterministic readback | `NiagaraEditor`, runtime `Niagara`, `NiagaraCore` | ✅ DONE |
| 9i-6 | Read-only Niagara validation helpers for compile health, unresolved parameter bindings, missing referenced assets, and disabled-emitter state sanity | `NiagaraEditor`, runtime `Niagara` | ✅ DONE |
| 9i-7 | Read-only Blackboard inspection for parent chain, key entries, key types, object/base-class filters, and sync/default metadata | `BehaviorTreeEditor`, `AIModule`, runtime `BehaviorTree` / `Blackboard` surfaces | ✅ DONE |
| 9i-8 | Blackboard asset creation plus key lifecycle mutation for common key types with deterministic readback | `BehaviorTreeEditor`, `AIModule`, runtime `BehaviorTree` / `Blackboard` surfaces, `AssetTools` | ✅ DONE |
| 9i-9 | Read-only Behavior Tree inspection for linked Blackboard assets, root composite shape, node topology, and service/decorator/task summaries | `BehaviorTreeEditor`, `AIModule`, `GameplayTasks` | ✅ DONE |
| 9i-10 | Behavior Tree asset creation plus linked Blackboard assignment and deterministic scaffold readback | `BehaviorTreeEditor`, `AIModule`, `AssetTools` | ✅ DONE |
| 9i-11 | Constrained Behavior Tree node authoring helpers for composite, task, decorator, and service add/remove flows inside an existing subtree | `BehaviorTreeEditor`, `AIModule`, `GameplayTasks` | ✅ DONE |
| 9i-12 | Behavior Tree property and validation helpers for blackboard selectors, abort modes, node enablement, and graph-health reporting | `BehaviorTreeEditor`, `AIModule`, `GameplayTasks` | ✅ DONE |

Current 9i status: batch 6 complete — Waves 1 through 12 are done; the 9i Niagara / AI-asset tranche is complete.

Recommended batching for 9i:

- `9i-1` + `9i-2` landed together through one shared Niagara-system serializer and asset-creation seam so every later Niagara mutation can reuse the same readback.
- `9i-3` + `9i-4` landed together through shared emitter-handle lookup, deterministic per-emitter summaries, and one compile/save mutation seam.
- `9i-11` + `9i-12` landed together through shared `UBehaviorTreeGraph` bootstrap, topology-path lookup, graph-backed mutation, and runtime readback/validation seams.
- `9i-5` + `9i-6` landed together through one exposed-parameter serializer and one compile-plus-readback validation seam so user-parameter writes and issue reporting stay shape-consistent.
- `9i-7` + `9i-8` landed together through one Blackboard serializer plus ordered entry-mutation seam so parent-chain inspection, local/inherited key readback, and key lifecycle edits all share the same structure.
- `9i-9` + `9i-10` landed together through one Behavior Tree topology serializer and asset-creation seam so new trees immediately read back in the same structure later mutations will use.
- Run `9i-11` + `9i-12` together through shared node lookup, property mutation, and validation helpers; keep this as the final batch because it depends on the earlier tree and blackboard inspection surfaces being stable.
- Keep 9i ordered as two sub-tranches: Niagara first (`9i-1` through `9i-6`), then Blackboard/Behavior Tree (`9i-7` through `9i-12`), because the editor modules and validation harnesses do not overlap enough to justify interleaving them.

### 9j Wave Plan

Phase 9j covers the documented UE 5.7 PCG Framework surface: graph assets, PCG components and volumes, graph instances and parameters, node graph authoring, generation modes, world-partition/data-layer/HLOD integration, shape grammar, GPU execution, editor mode tools, and runtime/debug workflows. Optional sample-plugin pages such as PCG Biome are included only when the corresponding plugins are enabled in the target project.

| Wave | Scope | Primary UE module families | Status |
|------|-------|----------------------------|--------|
| 9j-1 | Read-only PCG graph and PCG component inspection for graph settings, template/tool metadata, parameters, assigned graph state, generation mode flags, and linked actor/component context | `PCG`, `PCGEditor`, `UnrealEd` | ✅ DONE |
| 9j-2 | PCG graph asset creation plus PCG component / PCG volume bootstrap, graph-instance creation, template flags, and deterministic post-create readback | `PCG`, `PCGEditor`, `AssetTools`, `UnrealEd` | ✅ DONE |
| 9j-3 | Read-only PCG node catalog and per-node inspection for documented categories, pins, node settings, overrideable properties, comments, reroutes, and subgraph references | `PCG`, `PCGEditor` | ✅ DONE |
| 9j-4 | Generic PCG graph editing primitives: add/delete/connect/disconnect/layout nodes, comment boxes, named reroutes, and subgraph nodes with stable node/pin identifiers | `PCG`, `PCGEditor`, `GraphEditor` | ✅ DONE |
| 9j-5 | Generic PCG node-settings mutation, graph-parameter CRUD, graph-instance parameter overrides, node enabled/debug/inspect flags, and C++/Blueprint override-pin support | `PCG`, `PCGEditor`, `StructUtils` | ✅ DONE |
| 9j-6 | Common world-building PCG node authoring for sampler, spatial, point-op, spawner, and subgraph families used in prompt-driven forests, roads, fences, fields, and assembly graphs | `PCG`, `PCGEditor`, `PCGGeometryScriptInterop`, runtime `Engine` | ⏳ PLANNED |
| 9j-7 | Partitioned, hierarchical, and runtime generation controls for PCG graphs, PCG components, and `PCGWorldActor`, including generation radii, scheduling, frustum culling, cleanup, and explicit generate/cleanup/regenerate helpers | `PCG`, `PCGEditor`, `WorldPartition`, `UnrealEd` | ⏳ PLANNED |
| 9j-8 | World-partition/data-layer/HLOD PCG integration for Spawn Actor / Create Target Actor flows, actor-data-layer queries, partition-by-data-layer helpers, and HLOD/data-layer assignment settings | `PCG`, `PCGEditor`, `WorldPartition`, `Engine` | ⏳ PLANNED |
| 9j-9 | Shape grammar, spline-focused tooling, and PCG Editor Mode surfaces: grammar-node settings, spline/surface/paint/volume tool metadata, tool-graph presets, raycast rules, and tool-instance settings | `PCG`, `PCGEditor`, `PCGEditorMode`, `PCGGeometryScriptInterop` | ⏳ PLANNED |
| 9j-10 | GPU-execution and Custom HLSL surfaces: GPU-enabled node flags, Custom HLSL node settings, compute-source assets, virtual-texture priming, landscape-height / grass-map GPU workflows, and supported GPU spawner settings | `PCG`, `PCGEditor`, `ComputeFramework`, `RenderCore` | ⏳ PLANNED |
| 9j-11 | PCG validation, debugging, and profiling helpers for graph health, node debug state, runtime-generation overlays/state, cache/runtime CVars, and CPU/GPU profiling readback | `PCG`, `PCGEditor`, `UnrealEd`, runtime `Engine` | ⏳ PLANNED |
| 9j-12 | Optional PCG ecosystem extensions gated by plugin detection: PCG Biome Core/sample-plugin inspection helpers and any sample-plugin-only assets/settings that are present in the target project | `PCG`, optional `PCGBiomeCore` / sample-plugin families | ⏳ PLANNED |

**Wave 9j-1 + 9j-2 — PCG graph/component inspection + bootstrap ✅ DONE (2026-05-14).** Started the PCG tranche through one shared graph/component serializer and one bootstrap seam: `read_pcg_graph_content` now exposes graph or graph-instance metadata including template/library flags, tool data, node count, title/color overrides, and user-parameter summaries, `read_pcg_component_content` inspects actor-owned PCG components with assigned-graph, generation-trigger, partitioning, scheduling-policy, and owner-transform context, and `create_pcg_graph_asset`, `create_pcg_graph_instance`, `add_pcg_component_to_actor`, and `create_pcg_volume` now cover the first deterministic PCG authoring/bootstrap path. Tool count **166 → 172**. Focused wrapper coverage now exists in `Python/tests/test_phase9j_batch1_pcg_wrappers.py`, and local validation passed through Python compile checks plus `get_errors`; full pytest execution was unavailable in this environment because `pytest` is not installed.

**Wave 9j-3 + 9j-4 — PCG node catalog + graph editing seam ✅ DONE (2026-05-14).** Finished the broader editor-facing graph surface on top of the earlier node/pin seam: `read_pcg_graph_content`, `read_pcg_graph_nodes`, and `read_pcg_graph_node` now serialize PCG comment boxes, named-reroute declaration/usage links, and resolved subgraph graph paths, while `set_pcg_graph_node_position`, `set_pcg_subgraph_node_asset`, `add_pcg_graph_comment`, `update_pcg_graph_comment`, `delete_pcg_graph_comment`, and `add_pcg_graph_reroute` now close the remaining layout, comment-box, named-reroute, and richer subgraph workflows on stable node identifiers. Tool count **185 → 192**. Focused wrapper coverage in `Python/tests/test_phase9j_batch4_pcg_wrappers.py` passes **7/7**, focused live-editor coverage in `Python/tests/test_phase9j_batch4_pcg.py` passes **2/2**, and the combined real PlayTesting regression across the adjacent batch-3 and batch-4 PCG suites now passes **17/17** after rebuild and editor restart.

**Wave 9j-5 — generic mutation tranche ✅ DONE (2026-05-14).** Completed the remaining mutation surface on the same public PCG seams: `delete_pcg_graph_parameter` closes graph-parameter CRUD on base graphs, the earlier `update_pcg_graph_node_settings`, `set_pcg_graph_node_state`, `create_pcg_graph_parameter`, `rename_pcg_graph_parameter`, `set_pcg_graph_parameter`, and `reset_pcg_graph_parameter_override` helpers remain live for node settings/state and graph-instance overrides, and override-pin authoring now rides on the shared overridable-parameter serializer plus the generic graph pin connect/disconnect seam. The validated mutation surface is covered by `Python/tests/test_phase9j_batch3_pcg_wrappers.py`, `Python/tests/test_phase9j_batch4_pcg_wrappers.py`, `Python/tests/test_phase9j_batch3_pcg.py`, and `Python/tests/test_phase9j_batch4_pcg.py` against the real PlayTesting project.

**Release prep — additive catalog, safety seam, and high-leverage editor helpers ✅ DONE (2026-05-14).** Added an additive discoverability layer on top of the existing MCP surface instead of replacing it: `get_unreal_tool_categories` and `list_unreal_tools` now infer category metadata from the live tool registry, while shared Python-side bridge validation now preflights common asset paths, actor names, enums, and GUID-like IDs and normalizes bridge failures into structured `error_code` plus recovery `hint` fields. Added a first release-prep editor helper tranche on the native editor command seam: `save_level`, `undo_last_action`, `redo_last_action`, `capture_viewport_screenshot`, `place_in_grid`, `place_in_circle`, `place_along_spline`, and `scatter_in_area`. Tool count **192 → 202**. Focused wrapper coverage in `Python/tests/test_release_registry_and_validation_wrappers.py` plus `Python/tests/test_release_editor_helpers_wrappers.py` passes **11/11**, the deployed PlayTesting `playtestingEditor` build succeeds after syncing only the touched plugin files, and focused live-editor coverage in `Python/tests/test_release_editor_helpers_live.py` passes **2/2** for placement, screenshot, and undo/redo. `save_level` is build-validated in this slice and intentionally not live-exercised because the deterministic live harness uses unsaved scratch maps.

Current 9j status: Waves 1 through 5 are done, and Waves 6 through 12 remain planned.

Recommended batching for 9j:

- Run `9j-1` + `9j-2` together through one shared PCG graph/component serializer and asset/bootstrap seam so every later mutation can reuse the same readback.
- Run `9j-3` + `9j-4` together through one node-registry and graph-editing seam so later prompt flows can resolve documented PCG node names to stable graph objects and pins.
- Continue `9j-5` on top of the stable graph-editing seam, because node-setting mutation, graph parameters, and override pins only become useful once nodes and pins can be addressed deterministically.
- Run `9j-6` as the first user-facing productivity tranche on top of the generic graph tools, covering the sampler/spatial/spawner nodes most likely to be used in level-building prompts.
- Run `9j-7` + `9j-8` together through shared PCG component, `PCGWorldActor`, World Partition, Data Layer, and HLOD setting surfaces.
- Run `9j-9` after the generic graph and component surfaces are stable, because PCG Editor Mode tool graphs and shape-grammar workflows depend on graph, parameter, and spline metadata being addressable.
- Run `9j-10` after CPU graph authoring is stable; GPU/HLSL support is broad but more fragile, and should be layered on top of the core graph/component tooling rather than used as the bootstrap tranche.
- Run `9j-11` near the end so validation and debug output can cover the real shapes exposed by the earlier waves instead of being written against placeholders.
- Keep `9j-12` optional and last: sample-plugin pages such as PCG Biome are real PCG documentation, but they are not universally enabled and should not block the core built-in PCG surface.

### Phase 9 Rules

- Every domain starts with read-only inspection tools before mutation tools.
- Editor-only and runtime-capable APIs must be recorded separately in the tranche inventory.
- Each tranche ships with focused validation: tool-count sync, bridge-vs-MCP parity check, and tranche-specific pytest coverage.
- Material graph, UMG, and landscape-advanced are the first post-Blueprint priority domains.

---

## Known Issues & Technical Debt

| # | Issue | Severity | Notes |
|---|-------|----------|-------|
| 1 | T3D paste: model can't generate valid T3D | ~~High~~ | 🔄 Re-active in Phase 5 plan with Claude Sonnet + validation harness |
| 2 | Single TCP connection (no multiplexing) | Low | Adequate for single-user editor use |
| 3 | `spawn_blueprint_actor` has 0.2s Sleep | Low | Workaround for compiled class latency |
| 4 | ~~pyproject.toml references "unreal-engine-mcp"~~ | Done | ✅ Renamed to `unreal-ai-mcp` in 1.0 release |
| 5 | ~~README.md is still flopperam branding~~ | Done | ✅ Rewritten for 1.0 release |
| 6 | DEBUGGING.md references Claude Desktop MCP | Low | Not relevant to UnrealAI |
| 7 | No session persistence | ~~Medium~~ | ✅ Fixed in Phase 6 — save/load endpoints + UI |
| 8 | 53KB system prompt too large for small models | Medium | Causes context confusion |
| 9 | No streaming (panel shows nothing until full response) | ~~Medium~~ | ✅ Fixed in Phase 6 — incremental streaming |
| 10 | `EditorCommands::spawn_blueprint_actor` creates temp `BlueprintCommands` instance | Low | Should use shared instance |
| 11 | TCP bridge port hard-coded to 55557 — multiple editors collide | Medium | Make port configurable via UDeveloperSettings; per-project `mcp.json` overrides via `env` block |
| 12 | `Open in VS Code` button hard-codes central venv + MCP script paths | Low | Move `PythonExe` / `McpScript` to UDeveloperSettings (Project Settings → UnrealAI) |
| 13 | `robocopy /MIR` keeps source timestamps → UBT thinks nothing changed | Low | Documented; deploy script touches `.cpp` mtime to force rebuild |

---

## Environment & Deployment

### Local Development
```
# Python agent
cd e:\unrealBP\UnrealAI\Python
e:\unrealBP\.venv\Scripts\python.exe unreal_ai_agent.py

# Plugin build (from any dir)
$proj = 'E:\Unreal5\PlayTesting\playtesting\playtesting.uproject'
$ubt = 'C:\Program Files\Epic Games\UE_5.7\Engine\Build\BatchFiles\Build.bat'
& $ubt playtestingEditor Win64 Development -Project="$proj" -WaitMutex -FromMsBuild

# Deploy plugin source to UE project (NOTE: touch .cpp after copy to force rebuild)
robocopy 'e:\unrealBP\UnrealAI\UnrealAIPlugin' 'E:\Unreal5\PlayTesting\playtesting\Plugins\UnrealAI' /MIR /XD Binaries Intermediate
(Get-Item 'E:\Unreal5\PlayTesting\playtesting\Plugins\UnrealAI\Source\UnrealAI\Private\UI\SUnrealAIChatPanel.cpp').LastWriteTime = Get-Date

# MCP server smoke test
e:\unrealBP\.venv\Scripts\python.exe e:\unrealBP\UnrealAI\scripts\test_mcp_smoke.py
```

### Ports
| Port | Service |
|------|---------|
| 8765 | Python FastAPI agent (HTTP) |
| 11434 | Ollama LLM server |
| 55557 | UE plugin TCP bridge |

### Models Tested
| Model | Size | Result |
|-------|------|--------|
| qwen2.5-coder:32b | 32B | Hung GPU (RTX 4090 24GB VRAM insufficient?) |
| qwen2.5-coder:14b | 14B | Works for basic tools, cannot drive BP graph authoring |
| qwen/qwen3-coder:free | Cloud (OpenRouter) | Rate-limited on free tier, untested for tools |
| Claude Sonnet 4.5 (via VS Code Copilot + MCP) | Cloud | ✅ Drives the MCP toolset cleanly. Target model for Phase 5 re-activation. |
| Claude Opus 4.7 (via VS Code Copilot + MCP) | Cloud | ✅ Same; reserved for harder T3D generation cases. |

### User Rules
- **DO NOT push to git** until manually confirmed by user
- **Support Unreal Engine 5.0 through 5.7**
- **Keep all files local**

---

## Decision Log

| Date | Decision | Rationale |
|------|----------|-----------|
| 2026-04-20 | Phase 5 Approach B (granular tools) → abandoned | 14B model hallucinated node/pin IDs across multi-step sequences |
| 2026-04-20 | Pivot to Approach A (T3D paste) | Single tool call, model generates everything at once, uses UE's built-in importer |
| 2026-04-20 | T3D paste approach → paused | Model emits T3D as chat text instead of tool call, or generates malformed T3D. C++ works, model bottleneck. |
| 2026-04-20 | Added OpenRouter BYOK | Enable cloud models as alternative to local Ollama; free tier rate-limited |
| 2026-04-20 | Added code-block UI | T3D and other code in responses was being truncated/unreadable in the flat text panel |
| 2026-04-20 | Phase 5 fully paused | Both BP graph authoring approaches blocked by model capability. Will revisit with stronger model. |
| 2026-04-20 | Phase 6 UX Polish completed | Streaming (poll-based with partial_text), markdown rendering, save/load, undo (FScopedTransaction), tool-call log |
| 2026-04-20 | Streaming via polling (not SSE) | UE's IHttpRequest doesn't support SSE natively; poll with partial_text is simpler and works across UE 5.0-5.7 |
| 2026-04-20 | Undo via FScopedTransaction | Wraps mutation commands only; read-only commands exempted via static TSet |
| 2026-04-20 | Added MCP server as second front-end (Phase 6.5) | User wants Claude Sonnet via VS Code Copilot subscription instead of being limited by local 14B model. FastMCP wraps the same 15 tools, reuses `ue_bridge.py` and `dispatch_tool` — no duplication. Slate panel + Ollama/OpenRouter path remain fully supported in parallel. |
| 2026-05-08 | Live ping verified MCP path end-to-end | `unrealai/spawn_actor` from VS Code Copilot successfully created `TestLight` PointLight at `[0,0,300]`, confirmed via `find_actors_by_name`. Closes the loop on Phase 6.5. |
| 2026-05-08 | Custom agent file location — `.claude/agents/*.md`, NOT `.github/chatmodes/` | Inspected bundled VS Code 1.119 Copilot extension. The `.chatmode.md` extension is from an older spec and is silently ignored by 1.119. Required frontmatter: `name`, `model`. Picker is the bottom-of-input Agent dropdown. |
| 2026-05-08 | "Open in VS Code" button → `--new-window` per UE project | Each Unreal project now provisions its own `<ProjectDir>/.vscode/mcp.json` and `<ProjectDir>/.claude/agents/unreal-mcp.md`, then `code <ProjectDir> --new-window`. Per-project chat history, sessions, and MCP scope. |
| 2026-05-08 | Phase 5 re-activated | With Claude Sonnet via MCP the model bottleneck is gone. Plan above outlines T3D hardening (validation pre-flight, post-paste verification, snippet library), granular fallback tools, inspection upgrades, and a 10-scenario validation harness for go/no-go. |
| 2026-05-08 | Phase 5 mid-phase progress | A1, A2, A4, A5, B (6 granular tools), C1 (Python+C++ code) all complete. MCP tool count 15 → 23 verified by smoke test. C1 awaiting editor close to rebuild. Outstanding: A3, C2, Workstream E. Phase 5 acceptance criterion #1 (10-scenario harness) and #4 (rebuild for pin types) are the gating items. |
| 2026-05-08 | Phase 5 late-phase progress | C1 C++ build succeeded after editor close (`UnrealAIBlueprintCommands.cpp` recompiled, link OK). Workstream E scaffolded: `Python/tests/scenarios.md` (10-scenario catalogue) + `Python/tests/test_blueprint_authoring.py` (16 tests). Pure-Python suite 6/6 green; 10 live-editor scenarios skip cleanly when bridge offline. Only gating item left for Phase 5: run the 10 live scenarios against an open editor. |
| 2026-05-08 | **Phase 5 complete** | Full live regression run: 16 passed in 17.59s. Round-2 fixes applied: graph shape (`graph_data.nodes` + `class` field), Branch pin names (`then`/`else`), `_create_variable_idempotent` helper to tolerate re-runs. All six Phase 5 acceptance criteria met. Backlog: A3 (`replace_node_by_name`), C2 (`list_node_classes`), C3 (`dry_run_compile`), Workstream D. |
| 2026-05-08 | **Phase 7 Wave 1 complete** | Added 8 MCP wrappers around existing C++ handlers: `spawn_blueprint_actor`, `set_physics_properties`, `set_static_mesh_properties`, `set_mesh_material_color`, `apply_material_to_actor`, `apply_material_to_blueprint`, `get_actor_material_info`, `get_blueprint_material_info`. Tool count 23 → 31. New `Python/tests/test_phase7_tools.py` with 6 end-to-end scenarios; combined suite 22 passed in 29.22s. No C++ changes required — all handlers were already routed in `UnrealAIBridge.cpp`. Wave 2 = asset search/import + material instance creation (needs new C++). |
| 2026-05-08 | **Phase 7 Wave 2 complete** | New C++ class `FUnrealAIAssetCommands` (header + ~290 LOC `.cpp`) implementing 5 handlers: `find_assets`, `get_asset_dependencies`, `get_referencers`, `create_material_instance`, `import_asset`. Wired through `UnrealAIBridge` (member, ctor `MakeShared`, dtor `Reset`, dispatch else-if block). `AssetTools` added to `UnrealAI.Build.cs` (alongside existing `AssetRegistry`). UE 5.7 asset registry uses `FARFilter::ClassPaths` + `FTopLevelAssetPath("/Script/Engine", ClassName)`. Plugin rebuilt clean after `Intermediate` nuke (32/32 actions, link OK, 11.83s). Five MCP tools added to `unreal_ai_mcp.py` — tool count 31 → **36**. New `Python/tests/test_phase7_wave2.py` with 8 scenarios covering find/deps/referencers/MI create+lookup/import-negative; full suite **30 passed in 24.76s**. |
| 2026-05-08 | **Phase 7 Wave 3a complete** | New C++ class `FUnrealAILevelCommands` implementing 4 level helpers: `snap_actors_to_grid`, `align_actors`, `duplicate_actor`, `focus_viewport`. Routed through `UnrealAIBridge` and exposed in `Python/unreal_ai_mcp.py`. New `Python/tests/test_phase7_wave3a.py` covers 7 live-editor scenarios; user-reported run: **7 passed in 4.94s**. Tool count 36 → **40**. Wave 3b = landscape creation and editing. |
| 2026-05-08 | **Phase 7 Wave 3b started** | Added the first landscape slice in source: new `FUnrealAILandscapeCommands` with `get_landscapes`, `create_landscape`, `set_landscape_flat_height`, and `import_landscape_heightmap`. Bridge wiring added in `UnrealAIBridge`, and `Landscape` + `LandscapeEditor` were added to `UnrealAI.Build.cs`. Deployed plugin rebuilt cleanly with `UnrealAILandscapeCommands.cpp` compiling and linking successfully (34/34 actions, 17.46s). Tool count 40 → **44**. New `Python/tests/test_phase7_wave3b_landscape.py` scaffolded for the next live run. |
| 2026-05-08 | **Phase 7 Wave 3b complete** | Live landscape validation passed: user-reported `Python/tests/test_phase7_wave3b_landscape.py` run = **4 passed in 1.67s**. `create_landscape` now returns the actor label consistently for MCP consumers (`name` and `label` fields), which fixed the only failing assertion in the wave. The first landscape slice is complete with `get_landscapes`, `create_landscape`, `set_landscape_flat_height`, and `import_landscape_heightmap` exposed and working. Tool count remains **44**. Landscape paint-layer tooling is deferred to a later wave. |
| 2026-05-08 | **Phase 9 Wave 1 complete** | Exposed the remaining routed Blueprint mutation handlers in `Python/unreal_ai_mcp.py`: `set_blueprint_variable_properties`, `add_blueprint_function_input`, `add_blueprint_function_output`, `delete_blueprint_function`, and `rename_blueprint_function`. Also extended `create_blueprint_function` to accept `return_type`. Added focused pure-Python wrapper coverage in `Python/tests/test_phase9_wave1_mcp_wrappers.py`; **5 passed in 1.04s**. Current MCP surface is now **50** tools. Next tranche: Material graph inventory + first read-only tooling. |
| 2026-05-11 | **Phase 9 Wave 2 first material slice complete** | Added new C++ handler family `FUnrealAIMaterialCommands` and wired it through `UnrealAIBridge` for three read-only material tools: `get_material_expressions`, `get_material_connections`, and `get_material_parameters`. The first slice resolves material instances back to their base material graph and uses runtime/public material APIs only, so no `MaterialEditor` dependency was required yet. Added `Python/tests/test_phase9_wave2_material_readonly.py` for focused live-editor smoke coverage; the narrow pytest target skipped cleanly while the bridge was offline. Editor-target build succeeded cleanly in **58.66s**. MCP tool count **50 → 53**. |
| 2026-05-11 | **Phase 9 Wave 3 core material mutation complete** | Added the first `MaterialEditor`-backed write tranche for 9b. `UnrealAI.Build.cs` now includes `MaterialEditor`, and `FUnrealAIMaterialCommands` exposes `create_material_asset`, `create_material_expression`, `delete_material_expression`, `connect_material_expressions`, `connect_material_property`, `recompile_material`, and `layout_material_expressions` in addition to the read-only tools. Added focused live-editor coverage in `Python/tests/test_phase9_wave3_material_mutation.py`; **2 passed in 11.03s**. Current MCP surface is now **61** tools. Remaining 9b scope is parameter/instance editing plus higher-level graph ergonomics. |
| 2026-05-11 | **Phase 9 Wave 4 material instance parameter edits complete** | Added `set_material_instance_parameters` to mutate scalar, vector, texture, and static-switch overrides on existing `UMaterialInstanceConstant` assets. Added focused live-editor coverage in `Python/tests/test_phase9_wave4_material_instance_parameters.py`; **2 passed in 5.39s**. Current MCP surface is now **62** tools. Remaining 9b scope is base-material parameter-expression mutation and higher-level graph ergonomics. |
| 2026-05-11 | **Phase 9 Wave 5 material disconnect helpers complete** | Added `disconnect_material_expressions` and `disconnect_material_property` as the first 9b-5 ergonomics tranche. This removes the need to delete nodes just to sever graph edges or material output bindings. Added focused live-editor coverage in `Python/tests/test_phase9_wave5_material_disconnect.py`; **2 passed in 7.39s**. Current MCP surface is now **64** tools. Remaining 9b scope is replace helpers, safer bulk edits, material-function coverage, and base-material parameter-expression mutation. |
| 2026-05-11 | **Phase 9 Wave 6 material replace helper complete** | Added `replace_material_expression` to swap one material node for another class while preserving compatible incoming edges, downstream consumers, and material-property bindings. Added focused live-editor coverage in `Python/tests/test_phase9_wave6_material_replace.py`; **1 passed in 4.50s**. Current MCP surface is now **65** tools. Remaining 9b scope is safer bulk edits, material-function coverage, and base-material parameter-expression mutation. |
| 2026-05-11 | **Phase 9 Wave 7 material-function coverage and bulk delete complete** | Added `create_material_function_asset`, generalized the material graph inspection/mutation handlers to work on both base materials and material functions, and added `delete_material_expressions` for preflighted bulk node deletes. Added focused live-editor coverage in `Python/tests/test_phase9_wave7_material_functions_bulk_delete.py` for material-function graph authoring plus bulk-delete behavior. Current MCP surface is now **67** tools. 9b-5 is now complete; remaining 9b scope is base-material parameter-expression mutation. |
| 2026-05-11 | **Phase 9 Wave 8 base material parameter-expression mutation complete** | Added `set_material_parameters` to update scalar, vector, texture, and static-switch parameter-expression defaults directly on base material assets. Added focused live-editor coverage in `Python/tests/test_phase9_wave8_material_parameters.py`; **1 passed in 5.59s**. Current MCP surface is now **68** tools. Phase 9b is now complete. |
| 2026-05-11 | **Phase 9c Wave 1 widget blueprint slice complete** | Added `FUnrealAIWidgetCommands` and exposed `create_widget_blueprint` plus `read_widget_blueprint_content` through `Python/unreal_ai_mcp.py`. The first widget tranche now creates deterministic root-panel widget assets and inspects editable widget trees, bindings, animations, named-slot content, and Canvas slot layout metadata. `UnrealAI.Build.cs` now includes `UMG` + `UMGEditor`. Focused coverage: `Python/tests/test_phase9c_wave1_widget_wrappers.py` **2 passed in 1.15s** and `Python/tests/test_phase9c_wave1_widget_blueprints.py` **1 passed in 1.04s**. Current MCP surface is now **70** tools. |
| 2026-05-11 | **Phase 9c Wave 2 broader slot inspection complete** | Expanded widget inspection to serialize common UMG slot subclasses beyond Canvas, covering representative box/content, grid, wrap, stack-box, and safe-zone layouts from source widget trees. `create_widget_blueprint` now supports an optional seeded default child to create deterministic slot instances for validation. Focused coverage: `Python/tests/test_phase9c_wave1_widget_wrappers.py` **3 passed in 0.69s** and `Python/tests/test_phase9c_wave2_widget_slots.py` **5 passed in 3.80s**. Current MCP surface remains **70** tools. |
| 2026-05-11 | **Phase 9c Wave 3 widget hierarchy mutation complete** | Added `add_widget_to_widget_blueprint`, `remove_widget_from_widget_blueprint`, and `reparent_widget_in_widget_blueprint` so source `WidgetTree` hierarchies can now be mutated safely through MCP. The handlers support panel parents, named-slot hosts, subtree-aware removal, and safe reparent flows with Blueprint dirtying/compile hooks. Focused coverage: `Python/tests/test_phase9c_wave1_widget_wrappers.py` **6 passed** and `Python/tests/test_phase9c_wave3_widget_hierarchy.py` **3 passed in 25.80s**. Current MCP surface is now **73** tools. |
| 2026-05-11 | **Phase 9c Wave 4 layout mutation complete** | Finished `set_widget_slot_layout_in_widget_blueprint` across every slot family currently serialized by `read_widget_blueprint_content`, covering Canvas, Grid, UniformGrid, HorizontalBox, VerticalBox, ScrollBox, StackBox, WrapBox, SafeZone, ScaleBox, BackgroundBlur, Border, Button, Overlay, SizeBox, WidgetSwitcher, and WindowTitleBarArea slot layouts through the same read/write field schema. Focused coverage: `Python/tests/test_phase9c_wave1_widget_wrappers.py` **9 passed in 0.92s** and `Python/tests/test_phase9c_wave4_widget_layout.py` **17 passed in 23.13s**. Current MCP surface remains **74** tools. |
| 2026-05-11 | **Phase 9c Wave 5 binding and animation mutation complete** | Expanded `set_widget_property_binding_in_widget_blueprint` so widget bindings can now target either Blueprint functions or Blueprint member-property paths, and added `create_widget_animation_in_widget_blueprint` plus `remove_widget_animation_from_widget_blueprint` for source `WidgetBlueprint` animation authoring/removal. Focused coverage: `Python/tests/test_phase9c_wave1_widget_wrappers.py` **14 passed in 0.96s** and `Python/tests/test_phase9c_wave5_widget_bindings.py` plus `Python/tests/test_phase9c_wave5_widget_animations.py` **3 passed in 6.81s**. Current MCP surface is now **78** tools. |
| 2026-05-12 | **Phase 9d Wave 1 landscape inspection complete** | Added `read_landscape_content` as the first read-only landscape-advanced MCP tool. The handler exposes selected edit-layer index, edit-layer metadata, used paint layers, deduplicated layer info assets, and target layer names derived from used paint layers. Focused coverage: `Python/tests/test_phase9d_wave1_landscape_wrappers.py` **1 passed in 1.17s** and `Python/tests/test_phase9d_wave1_landscape_inspection.py` **1 passed in 0.77s**. Current MCP surface is now **79** tools. Live validation also fixed a bridge race by setting accepted sockets to blocking mode in `MCPServerRunnable`. |
| 2026-05-12 | **Phase 9d Wave 2 landscape sampling complete** | Added `sample_landscape_point` as the next read-only landscape-advanced MCP tool. The handler samples one world location, returns world-space height, resolved component metadata, and per-layer paint weights for the named landscape. Focused coverage: `Python/tests/test_phase9d_wave2_landscape_sampling_wrappers.py` **1 passed in 1.08s** and `Python/tests/test_phase9d_wave2_landscape_sampling.py` **1 passed in 24.80s** after adding a short post-ping startup settle window for `create_landscape`. Current MCP surface is now **80** tools. Live validation showed collision-backed `GetHeightAtLocation` stayed stale after `set_landscape_flat_height`, so the final implementation reads editor heightmap data directly through `FLandscapeComponentDataInterface`. |
| 2026-05-12 | **Phase 9d Wave 3 landscape batch sampling complete** | Added `sample_landscape_points` as the next read-only landscape-advanced MCP tool. The handler reuses the direct editor heightmap sampling and per-layer weight logic from `sample_landscape_point`, but returns multiple sampled point results in one request. Focused coverage: `Python/tests/test_phase9d_wave3_landscape_batch_sampling_wrappers.py` **1 passed in 1.10s** and `Python/tests/test_phase9d_wave3_landscape_batch_sampling.py` **1 passed in 31.94s**. Current MCP surface is now **81** tools. |
| 2026-05-12 | **Phase 9d Wave 4 landscape grid sampling complete** | Added `sample_landscape_grid` as the next read-only landscape-advanced MCP tool. The handler expands one origin plus `step_x`, `step_y`, `count_x`, and `count_y` into a regular sample grid, then reuses the same direct editor heightmap sampling and per-layer weight logic as the single-point and batch samplers. Focused coverage: `Python/tests/test_phase9d_wave4_landscape_grid_sampling_wrappers.py` **1 passed in 1.04s** and `Python/tests/test_phase9d_wave4_landscape_grid_sampling.py` **1 passed in 21.32s**. Current MCP surface is now **82** tools. |
| 2026-05-12 | **Phase 9d Wave 5 landscape region sampling complete** | Added `sample_landscape_region` as the next read-only landscape-advanced MCP tool. The handler resolves inclusive region counts from `min_corner`, `max_corner`, `step_x`, and `step_y`, then reuses the same direct editor heightmap sampling and per-layer weight logic as the single-point, batch, and grid samplers. Focused coverage: `Python/tests/test_phase9d_wave5_landscape_region_sampling_wrappers.py` **1 passed in 0.73s** and `Python/tests/test_phase9d_wave5_landscape_region_sampling.py` **1 passed in 20.86s**. Current MCP surface is now **83** tools. |
| 2026-05-12 | **Phase 9d Wave 6 landscape height-region raster sampling complete** | Added `sample_landscape_height_region` as the next read-only landscape-advanced MCP tool. The handler resolves inclusive region counts from `min_corner`, `max_corner`, `step_x`, and `step_y`, but returns a dense `height_rows` matrix plus min/max summaries instead of per-point layer-weight objects, allowing higher sample density for height-only queries. Focused coverage: `Python/tests/test_phase9d_wave6_landscape_height_region_wrappers.py` **1 passed in 0.97s** and `Python/tests/test_phase9d_wave6_landscape_height_region.py` **1 passed in 22.08s**. Current MCP surface is now **84** tools. |
| 2026-05-12 | **Phase 9e Wave 3 actor layer membership inspection complete** | Added `get_actor_layers` plus `get_actors_in_layer` as the next read-only level/editor orchestration slice. The handlers expose per-actor editor layer membership from `AActor::Layers` and layer-to-actor membership through `ULayersSubsystem`, including deterministic count summaries plus missing-layer validation. Focused coverage: `Python/tests/test_phase9e_wave3_actor_layers_wrappers.py` **2 passed in 0.91s** and `Python/tests/test_phase9e_wave3_actor_layers.py` **2 passed in 11.50s**. Current MCP surface is now **94** tools. |
| 2026-05-12 | **Phase 9e Wave 4 actor layer membership mutation complete** | Added `add_actor_to_layer` plus `remove_actor_from_layer` as the next write slice for level/editor orchestration. The handlers reuse `ULayersSubsystem` mutation semantics so add requests auto-create missing layers, remove requests no-op cleanly when the actor is already absent, and both return deterministic post-mutation layer readback from `AActor::Layers`. Focused coverage: `Python/tests/test_phase9e_wave4_actor_layer_mutation_wrappers.py` **2 passed in 1.01s** and `Python/tests/test_phase9e_wave4_actor_layer_mutation.py` **2 passed in 11.12s**. Current MCP surface is now **96** tools. |
| 2026-05-12 | **Phase 9e Wave 5 layer-based selection mutation complete** | Added `select_actors_in_layer` plus `deselect_actors_in_layer` as the next write slice for level/editor orchestration. The handlers reuse `ULayersSubsystem::SelectActorsInLayer`, preserve replace-vs-append selection semantics, and compute deterministic `changed` state by diffing selected-actor name sets before and after each mutation. Focused coverage: `Python/tests/test_phase9e_wave5_layer_selection_wrappers.py` **2 passed in 0.71s** and `Python/tests/test_phase9e_wave5_layer_selection.py` **2 passed in 18.57s**. Current MCP surface is now **98** tools. |
| 2026-05-12 | **Phase 9e Wave 6 layer catalog inspection complete** | Added `get_all_layers` as the next read-only slice for level/editor orchestration. The handler enumerates all known editor layers through `ULayersSubsystem::AddAllLayersTo`, returns deterministic layer-name ordering, exposes `ULayer::IsVisible()` state, and includes per-layer actor counts for quick catalog inspection. Focused coverage: `Python/tests/test_phase9e_wave6_layer_catalog_wrappers.py` **1 passed in 1.32s** and `Python/tests/test_phase9e_wave6_layer_catalog.py` **1 passed in 11.64s**. Current MCP surface is now **99** tools. |
| 2026-05-12 | **Phase 9e Wave 7 layer visibility mutation complete** | Added `set_layer_visibility` as the next write slice for level/editor orchestration. The handler preflights existing layer names, applies the visibility change through `ULayersSubsystem::SetLayerVisibility`, and returns deterministic pre/post visibility readback plus the updated layer catalog entry. Focused coverage: `Python/tests/test_phase9e_wave7_layer_visibility_wrappers.py` **1 passed in 0.78s** and `Python/tests/test_phase9e_wave7_layer_visibility.py` **1 passed in 16.27s**. Current MCP surface is now **100** tools. |
| 2026-05-12 | **Phase 9e Wave 8 layer visibility convenience helpers complete** | Added `toggle_layer_visibility` plus `make_all_layers_visible` as the next write slice for level/editor orchestration. The handlers preflight existing layer names for single-layer toggles, reuse `ULayersSubsystem::ToggleLayerVisibility` and `ULayersSubsystem::MakeAllLayersVisible`, and return deterministic post-mutation layer catalog summaries for both targeted toggles and bulk visibility restore. Focused coverage: `Python/tests/test_phase9e_wave8_layer_visibility_convenience_wrappers.py` **2 passed in 0.84s** and `Python/tests/test_phase9e_wave8_layer_visibility_convenience.py` **1 passed in 16.15s**. Current MCP surface is now **102** tools. |
| 2026-05-12 | **Phase 9e Wave 9 explicit layer creation complete** | Added `create_layer` as the next write slice for level/editor orchestration. The handler preflights duplicate layer names, applies explicit empty-layer creation through `ULayersSubsystem::CreateLayer`, and returns deterministic readback for the new visible empty layer through the existing layer-catalog serializer. Focused coverage: `Python/tests/test_phase9e_wave9_layer_create_wrappers.py` **1 passed in 1.11s** and `Python/tests/test_phase9e_wave9_layer_create.py` **1 passed in 13.19s**. Current MCP surface is now **103** tools. |
| 2026-05-12 | **Phase 9e Wave 10 selected-actor layer mutation complete** | Added `add_selected_actors_to_layer` plus `remove_selected_actors_from_layer` as the next write slice for level/editor orchestration. The handlers reuse `ULayersSubsystem::AddSelectedActorsToLayer` and `ULayersSubsystem::RemoveSelectedActorsFromLayer`, preserve the active editor selection, and return deterministic selected-count plus layer-membership summaries with clear empty-selection and missing-layer validation. Focused coverage: `Python/tests/test_phase9e_wave10_selected_actor_layers_wrappers.py` **2 passed in 0.99s** and `Python/tests/test_phase9e_wave10_selected_actor_layers.py` **1 passed in 16.34s**. Current MCP surface is now **105** tools. |
| 2026-05-12 | **Phase 9e Wave 11 layer lifecycle helpers complete** | Added `rename_layer` plus `delete_layer` as the final layer-mutation slice for level/editor orchestration. The handlers preflight duplicate rename targets, keep same-name renames deterministic no-ops, reuse `ULayersSubsystem::RenameLayer` and `ULayersSubsystem::DeleteLayer`, and return stable post-mutation snapshots including removed-actor summaries on delete. Focused coverage: `Python/tests/test_phase9e_wave11_layer_lifecycle_wrappers.py` **2 passed in 1.56s** and `Python/tests/test_phase9e_wave11_layer_lifecycle.py` **1 passed in 29.70s**. Current MCP surface is now **107** tools. |
| 2026-05-12 | **Phase 9e Wave 12 world-partition inspection complete** | Added `get_world_partition_info` as the last read-only editor-orchestration helper needed to close the tranche. The handler reports partitioned-world state, `UWorldPartitionSubsystem` availability, streaming-complete state, and current world-partition object identity when present. Focused coverage: `Python/tests/test_phase9e_wave12_world_partition_wrappers.py` **1 passed in 0.84s** and `Python/tests/test_phase9e_wave12_world_partition.py` **1 passed in 12.48s**. Current MCP surface is now **108** tools. |
| 2026-05-12 | **Phase 9f Wave 1 level sequence creation + inspection complete** | Added `FUnrealAISequencerCommands` and wired it through `UnrealAIBridge` for the first Sequencer tranche. The new MCP tools `create_level_sequence` and `read_level_sequence_content` create `ULevelSequence` assets under `/Game`, load the editor-side `LevelSequenceFactoryNew` reflectively, and serialize MovieScene playback metadata, spawnables, possessables, object bindings, master tracks, and nested sections. `UnrealAI.Build.cs` now includes `LevelSequence` + `LevelSequenceEditor`, and `UnrealAI.uplugin` now declares the `LevelSequenceEditor` plugin dependency required by UBT. Focused coverage: `Python/tests/test_phase9f_wave1_level_sequence_wrappers.py` **2 passed in 0.76s** and `Python/tests/test_phase9f_wave1_level_sequence.py` **1 passed in 30.61s** after a clean `playtestingEditor` rebuild. Current MCP surface is now **110** tools. |
| 2026-05-12 | **Phase 9f Wave 2 camera-cut track mutation complete** | Added `add_camera_cut_track_to_level_sequence` as the first write helper in the Sequencer tranche. The handler creates the camera-cut master track through `UMovieScene::AddCameraCutTrack`, treats repeat calls as deterministic no-op readbacks, and the sequence serializer now includes the separate camera-cut track in `master_tracks` so track counts match editor behavior. `UnrealAI.Build.cs` now also includes `MovieSceneTracks` for this runtime track-class slice. Focused coverage: `Python/tests/test_phase9f_wave2_camera_cut_track_wrappers.py` **1 passed in 2.63s** and `Python/tests/test_phase9f_wave2_camera_cut_track.py` **1 passed in 37.60s** after a clean `playtestingEditor` rebuild. Current MCP surface is now **111** tools. |
| 2026-05-12 | **Phase 9f Wave 3 generic master-track mutation complete** | Added `add_master_track_to_level_sequence` as the next write helper in the Sequencer tranche. The handler uses `UMovieScene::FindTrack` plus `UMovieScene::AddTrack` to create one unbound master track per requested class, returns deterministic no-op readback when the same class already exists, and reuses the shared sequence serializer for validation. Focused coverage: `Python/tests/test_phase9f_wave3_master_track_wrappers.py` **1 passed in 7.12s** and `Python/tests/test_phase9f_wave3_master_track.py` **1 passed in 14.81s** after a clean `playtestingEditor` rebuild. Current MCP surface is now **112** tools. |
| 2026-05-12 | **Phase 9f Wave 4 master-track section mutation complete** | Added `add_section_to_master_track_in_level_sequence` as the first generic section-write helper in the Sequencer tranche. The handler finds an existing unbound master track by class, creates a new section through `UMovieSceneTrack::CreateNewSection`, applies an explicit inclusive-start/exclusive-end frame range, adds the section through `UMovieSceneTrack::AddSection`, and reuses shared sequence readback to verify the serialized section metadata immediately. Focused coverage: `Python/tests/test_phase9f_wave4_master_track_sections_wrappers.py` **1 passed in 1.50s** and `Python/tests/test_phase9f_wave4_master_track_sections.py` **1 passed in 24.99s** after a clean `playtestingEditor` rebuild. Current MCP surface is now **113** tools. |
| 2026-05-12 | **Phase 9f Wave 5 master-track section range mutation complete** | Added `set_section_range_in_master_track_in_level_sequence` as the next generic section-write helper in the Sequencer tranche. The handler finds an existing unbound master track by class, resolves one section by index, updates its inclusive-start/exclusive-end frame range through `UMovieSceneSection::SetRange`, and reuses shared sequence readback to verify the updated serialized section metadata immediately. Focused coverage: `Python/tests/test_phase9f_wave5_section_range_wrappers.py` **1 passed in 1.18s** and `Python/tests/test_phase9f_wave5_section_range.py` **1 passed in 30.04s** after a clean `playtestingEditor` rebuild. Current MCP surface is now **114** tools. |
| 2026-05-12 | **Phase 9f Wave 6 master-track section removal complete** | Added `remove_section_from_master_track_in_level_sequence` as the next generic section-lifecycle helper in the Sequencer tranche. The handler finds an existing unbound master track by class, resolves one section by index, removes it through `UMovieSceneTrack::RemoveSectionAt`, and reuses shared sequence readback to verify the remaining serialized section metadata immediately. Focused coverage: `Python/tests/test_phase9f_wave6_remove_section_wrappers.py` **1 passed in 1.04s** and `Python/tests/test_phase9f_wave6_remove_section.py` **1 passed in 19.13s** after a clean `playtestingEditor` rebuild. Current MCP surface is now **115** tools. |
| 2026-05-12 | **Phase 9f Wave 7 actor possessable binding complete** | Added `add_actor_possessable_to_level_sequence` as the first binding helper in the Sequencer tranche. The handler resolves actors by object name or label, uses `ULevelSequence::FindPossessableObjectId` for idempotence, creates missing possessables through `UMovieScene::AddPossessable`, binds them through `ULevelSequence::BindPossessableObject`, and returns deterministic binding readback including the input actor identifier and resolved actor label. Focused coverage: `Python/tests/test_phase9f_wave7_actor_possessable_wrappers.py` **1 passed in 1.19s** and `Python/tests/test_phase9f_wave7_actor_possessable.py` **1 passed in 11.85s** after a clean `playtestingEditor` rebuild. Current MCP surface is now **116** tools. |
| 2026-05-12 | **Phase 9f Wave 8 bound-track creation complete** | Added `add_track_to_binding_in_level_sequence` as the next binding helper in the Sequencer tranche. The handler resolves one existing binding by GUID, creates one bound track per requested class through `UMovieScene::AddTrack(TrackClass, BindingGuid)`, treats repeat calls as deterministic no-op readback, and returns the shared sequence snapshot plus binding-track identity. Focused coverage: `Python/tests/test_phase9f_wave8_binding_track_wrappers.py` **1 passed in 0.81s** and `Python/tests/test_phase9f_wave8_binding_track.py` **1 passed in 27.65s** after a clean `playtestingEditor` rebuild. Current MCP surface is now **117** tools. |
| 2026-05-12 | **Phase 9f Wave 9 bound float-key mutation complete** | Added `add_float_key_to_binding_track_in_level_sequence` as the first bound-property key helper in the Sequencer tranche. The handler targets an existing bound `UMovieSceneFloatTrack`, sets its property name and path through `UMovieScenePropertyTrack::SetPropertyNameAndPath`, finds or creates a float section through `FindOrAddSection`, mutates the float channel through `AddKeyToChannel`, and extends shared sequence serialization with property-track metadata plus `float_keys` / `float_key_count` for deterministic validation. Focused coverage: `Python/tests/test_phase9f_wave9_float_keys_wrappers.py` **1 passed in 1.00s** and `Python/tests/test_phase9f_wave9_float_keys.py` **1 passed in 43.63s** after a clean `playtestingEditor` rebuild. Current MCP surface is now **118** tools. |
| 2026-05-12 | **Phase 9f Wave 10 playback-range mutation complete** | Added `set_level_sequence_playback_range` as the final Sequencer tranche helper needed to close Phase 9f. The handler updates `UMovieScene::SetPlaybackRange` with explicit inclusive-start / exclusive-end bounds, saves the sequence asset, and returns deterministic playback-range readback through the existing `read_level_sequence_content` serializer. Focused coverage: `Python/tests/test_phase9f_wave10_playback_range_wrappers.py` **1 passed in 0.68s** and `Python/tests/test_phase9f_wave10_playback_range.py` **1 passed in 28.48s** after a clean `playtestingEditor` rebuild. Current MCP surface is now **119** tools. |
| 2026-05-12 | **Phase 9g Wave 1 DataTable inspection complete** | Added `read_data_table_content` as the first read-only helper in the data-assets / tables tranche. The handler lives on the asset-command surface, loads one `UDataTable`, reports its row-struct identity, serializes deterministic column metadata and row-name summaries, and exposes the table body both as exported `rows_json` and parsed `rows` when `UDataTable::GetTableAsJSON()` yields valid row arrays. Focused coverage: `Python/tests/test_phase9g_wave1_data_table_wrappers.py` **1 passed in 0.81s** and `Python/tests/test_phase9g_wave1_data_table.py` **1 passed in 27.96s** after a clean `playtestingEditor` rebuild. Current MCP surface is now **120** tools. |
| 2026-05-12 | **Phase 9g Wave 2 DataTable row inspection complete** | Added `read_data_table_row` as the next read-only helper in the data-assets / tables tranche. The handler resolves one `UDataTable`, validates the requested row through `FindRowUnchecked`, honors the table's configured JSON key field via `ImportKeyField` with a default of `Name`, and returns one deterministic row payload plus `row_json` filtered from the same `GetTableAsJSON()` export seam used in Wave 1. Focused coverage: `Python/tests/test_phase9g_wave2_data_table_row_wrappers.py` **1 passed in 1.46s** and `Python/tests/test_phase9g_wave2_data_table_row.py` **1 passed in 40.98s** after a clean `playtestingEditor` rebuild. Current MCP surface is now **121** tools. |
| 2026-05-13 | **Phase 9g Wave 3 DataTable asset creation complete** | Added `create_data_table_asset` as the first mutation helper in the data-assets / tables tranche. The handler loads the requested row struct by path, creates a new empty `UDataTable` through `UDataTableFactory` plus `AssetTools`, saves the asset, and reuses `read_data_table_content` for deterministic post-create readback including row-struct identity and empty row state. Focused coverage: `Python/tests/test_phase9g_wave3_create_data_table_wrappers.py` **1 passed in 1.51s** and `Python/tests/test_phase9g_wave3_create_data_table.py` **1 passed in 89.49s** after a clean `playtestingEditor` rebuild. Current MCP surface is now **122** tools. |
| 2026-05-13 | **Phase 9g Wave 4 DataTable row upsert complete** | Added `upsert_data_table_row` as the first row-authoring helper in the data-assets / tables tranche. The handler loads one `UDataTable`, deserializes a JSON-compatible row payload into the table's row struct with `FJsonObjectConverter`, injects the explicit key-field value when the table uses `ImportKeyField`, writes the row through `UDataTable::AddRow`, saves the asset, and reuses `read_data_table_row` for deterministic post-upsert readback. Focused coverage: `Python/tests/test_phase9g_wave4_upsert_data_table_row_wrappers.py` **1 passed in 1.01s** and `Python/tests/test_phase9g_wave4_upsert_data_table_row.py` **1 passed in 12.67s** after a clean `playtestingEditor` rebuild. Current MCP surface is now **123** tools. |
| 2026-05-13 | **Phase 9g Wave 5 DataTable row delete complete** | Added `delete_data_table_row` as the paired destructive helper in the data-assets / tables tranche. The handler resolves one `UDataTable`, validates row existence through `FindRowUnchecked`, removes the row with `UDataTable::RemoveRow`, saves the asset, and reuses `read_data_table_content` for deterministic post-delete table readback. Focused coverage: `Python/tests/test_phase9g_wave5_delete_data_table_row_wrappers.py` **1 passed in 0.67s** and `Python/tests/test_phase9g_wave5_delete_data_table_row.py` **1 passed in 13.14s** after a clean `playtestingEditor` rebuild. Current MCP surface is now **124** tools. |
| 2026-05-13 | **Phase 9g Wave 6 DataTable row rename complete** | Added `rename_data_table_row` as the next row lifecycle helper in the data-assets / tables tranche. The handler resolves one `UDataTable`, reuses `FDataTableEditorUtils::RenameRow` for the actual rename, explicitly synchronizes the table's configured key field when `ImportKeyField` is in play, saves the asset, and reuses `read_data_table_row` for deterministic post-rename readback. Focused coverage: `Python/tests/test_phase9g_wave6_rename_data_table_row_wrappers.py` **1 passed in 1.02s** and `Python/tests/test_phase9g_wave6_rename_data_table_row.py` **1 passed in 25.61s** after a clean `playtestingEditor` rebuild. Current MCP surface is now **125** tools. |
| 2026-05-13 | **Phase 9g Wave 7 DataTable row duplicate complete** | Added `duplicate_data_table_row` as the paired clone helper in the data-assets / tables tranche. The handler resolves one `UDataTable`, reuses `FDataTableEditorUtils::DuplicateRow` for the actual row copy, explicitly synchronizes the table's configured key field on the duplicated row when `ImportKeyField` is in play, saves the asset, and reuses `read_data_table_row` for deterministic new-row readback. Focused coverage: `Python/tests/test_phase9g_wave7_duplicate_data_table_row_wrappers.py` **1 passed in 0.65s** and `Python/tests/test_phase9g_wave7_duplicate_data_table_row.py` **1 passed in 15.03s** after a clean `playtestingEditor` rebuild. Current MCP surface is now **126** tools. |
| 2026-05-13 | **Phase 9g Wave 8 DataTable row reordering complete** | Added `move_data_table_row` as the next row-order helper in the data-assets / tables tranche. The handler resolves one `UDataTable`, validates the requested row plus move direction and distance, reuses `FDataTableEditorUtils::MoveRow` for the actual row-order mutation, saves the asset, and reuses `read_data_table_content` for deterministic post-move table-order readback including `row_index_before` and `row_index_after`. Focused coverage: `Python/tests/test_phase9g_wave8_move_data_table_row_wrappers.py` **1 passed in 1.12s** and live bridge validation confirmed the expected row order change after a clean `playtestingEditor` rebuild. Current MCP surface is now **127** tools. |
| 2026-05-13 | **Phase 9g Wave 9 CurveTable inspection complete** | Added `read_curve_table_content` as the first read-only CurveTable helper in the data-assets / tables tranche. The handler resolves one `UCurveTable`, reports its `curve_table_mode`, serializes deterministic row-name summaries from `GetRowMap()`, and exposes the table body both as exported `rows_json` and parsed `rows` through `UCurveTable::GetTableAsJSON()`. Focused coverage: `Python/tests/test_phase9g_wave9_curve_table_wrappers.py` passed and `Python/tests/test_phase9g_wave9_curve_table.py` was exercised after a clean `playtestingEditor` rebuild but skipped because no populated CurveTable assets were available under `/Game` or `/Engine`. Current MCP surface is now **128** tools. |
| 2026-05-13 | **Phase 9g Wave 10 CurveTable row inspection complete** | Added `read_curve_table_row` as the paired row-level CurveTable helper in the data-assets / tables tranche. The handler resolves one `UCurveTable`, validates row existence through `FindCurveUnchecked`, filters one deterministic row payload from the same `GetTableAsJSON()` export seam used in Wave 9, and returns `row_json` plus the resolved row object keyed by `Name`. Focused coverage: `Python/tests/test_phase9g_wave10_curve_table_row_wrappers.py` passed and `Python/tests/test_phase9g_wave10_curve_table_row.py` was exercised after a clean `playtestingEditor` rebuild but skipped because no populated CurveTable assets were available under `/Game` or `/Engine`. Current MCP surface is now **129** tools. |
| 2026-05-13 | **Phase 9g Wave 11 CurveTable asset creation complete** | Added `create_curve_table_asset` as the first CurveTable mutation helper in the data-assets / tables tranche. The handler directly creates a `UCurveTable` asset, establishes the requested `SimpleCurves` or `RichCurves` mode by adding and then deleting a seed row so the table remains empty without resetting the mode, saves the asset through the asset registry path, and reuses `read_curve_table_content` for deterministic post-create readback. Empty CurveTable inspection now normalizes empty-table `rows_json` to `[]` so empty asset readback stays parseable. Focused coverage: `Python/tests/test_phase9g_wave11_create_curve_table_wrappers.py` **1 passed in 1.04s** and `Python/tests/test_phase9g_wave11_create_curve_table.py` **1 passed in 11.54s** after a clean `playtestingEditor` rebuild (with an editor restart to clear the Live Coding build lock). Current MCP surface is now **130** tools. |
| 2026-05-13 | **Phase 9g Wave 12 CurveTable row upsert complete** | Added `upsert_curve_table_row` as the next CurveTable mutation helper in the data-assets / tables tranche. The handler resolves one `UCurveTable`, validates that the table already has an explicit simple or rich mode, parses the incoming `row_data` object using the same JSON row shape returned by CurveTable inspection (`Name` plus numeric time fields), replaces exactly one row through `AddSimpleCurve` or `AddRichCurve`, invalidates cached curve pointers, and reuses `read_curve_table_row` for deterministic post-upsert readback. Focused coverage: `Python/tests/test_phase9g_wave12_upsert_curve_table_row_wrappers.py` **1 passed in 1.10s** and `Python/tests/test_phase9g_wave12_upsert_curve_table_row.py` **1 passed in 12.50s** after a clean `playtestingEditor` rebuild (with an editor restart to clear the Live Coding build lock). Current MCP surface is now **131** tools. |
| 2026-05-13 | **Phase 9g Wave 13 CurveTable row delete complete** | Added `delete_curve_table_row` as the destructive CurveTable row lifecycle helper in the data-assets / tables tranche. The handler resolves one `UCurveTable`, validates row existence through `FindCurveUnchecked`, removes the row through `DeleteRow`, invalidates cached curve pointers, saves the asset, and reuses `read_curve_table_content` for deterministic post-delete table readback while preserving the table's current simple or rich mode. Focused coverage: `Python/tests/test_phase9g_wave13_delete_curve_table_row_wrappers.py` passed and `Python/tests/test_phase9g_wave13_delete_curve_table_row.py` passed after a clean `playtestingEditor` rebuild. Current MCP surface is now **132** tools. |
| 2026-05-13 | **Phase 9g Wave 14 CurveTable row rename complete** | Added `rename_curve_table_row` as the paired CurveTable row rename helper in the data-assets / tables tranche. The handler resolves one `UCurveTable`, preflights missing and duplicate target rows, renames the row through `RenameRow`, invalidates cached curve pointers, saves the asset, and reuses `read_curve_table_row` for deterministic post-rename readback keyed by the new row name. Focused coverage: `Python/tests/test_phase9g_wave14_rename_curve_table_row_wrappers.py` passed and `Python/tests/test_phase9g_wave14_rename_curve_table_row.py` passed after a clean `playtestingEditor` rebuild. Current MCP surface is now **133** tools. |
| 2026-05-13 | **Phase 9g Wave 15 table validation helpers complete** | Added `validate_data_table_row_import` and `validate_curve_table_row_import` as the finishing read-only validation helpers in the data-assets / tables tranche. The DataTable validator preflights row-struct presence, key-field alignment, unknown row fields, and JSON-to-struct import shape without mutating the asset, while the CurveTable validator preflights simple-vs-rich mode availability and the normalized numeric time/value row shape returned by CurveTable inspection. Both helpers return structured `issues`, `warning_count`, `error_count`, and normalized payload readback instead of mutating assets. Focused coverage: `Python/tests/test_phase9g_wave15_table_validation_wrappers.py` passed and `Python/tests/test_phase9g_wave15_table_validation.py` passed after a clean `playtestingEditor` rebuild. Current MCP surface is now **135** tools. |
| 2026-05-13 | **Phase 9g overall tranche complete** | The data-assets / tables tranche now covers DataTable and CurveTable inspection, row lifecycle mutation, empty-asset creation, row import validation, and DataTable row duplication/reordering end to end. Focused validation for the finishing slice passed across wrapper and live-editor coverage after the final `playtestingEditor` rebuild. |
| 2026-05-13 | **Phase 9h Wave 1 AnimBlueprint inspection complete** | Added `read_anim_blueprint_content` as the first read-only helper in the animation / AnimBlueprint tranche. The handler resolves one `UAnimBlueprint`, reports target skeleton, generated class, preview mesh, exposed variable summaries, top-level anim-layer/function graph summaries, and deterministic state-machine topology by scanning `UAnimGraphNode_StateMachineBase` nodes and walking each `UAnimationStateMachineGraph` for entry, state, and transition summaries. Focused coverage: `Python/tests/test_phase9h_wave1_anim_blueprint_wrappers.py` **1 passed in 1.05s** and `Python/tests/test_phase9h_wave1_anim_blueprint.py` **1 passed in 23.50s** after a clean `playtestingEditor` rebuild. Current MCP surface is now **136** tools. |
| 2026-05-13 | **Phase 9h Wave 2 AnimBlueprint asset creation complete** | Added `create_anim_blueprint_asset` as the first write helper in the animation / AnimBlueprint tranche. The handler uses `UAnimBlueprintFactory` plus `AssetTools::CreateAsset` to create empty AnimBlueprint assets for a supplied skeleton, optional parent class, optional preview mesh, and explicit template mode, then reuses `read_anim_blueprint_content` for deterministic post-create readback. Focused coverage: `Python/tests/test_phase9h_wave2_create_anim_blueprint_wrappers.py` **1 passed in 1.05s** and `Python/tests/test_phase9h_wave2_create_anim_blueprint.py` **1 passed in 23.50s** after a clean `playtestingEditor` rebuild. Current MCP surface is now **137** tools. |
| 2026-05-13 | **Phase 9h Wave 3 named AnimBlueprint state-machine inspection complete** | Added `read_anim_state_machine` as the machine-scoped inspection helper in the animation / AnimBlueprint tranche. The handler reuses shared AnimBlueprint inspection readback, filters one named state machine case-insensitively, and returns deterministic state, entry, transition, and owning-graph summaries without rewalking separate editor-private seams. Focused coverage landed together with the Wave 4 companion: `Python/tests/test_phase9h_wave3_anim_state_machine_wrappers.py` and `Python/tests/test_phase9h_wave4_create_anim_state_machine_wrappers.py` **2 passed in 1.14s**, and `Python/tests/test_phase9h_wave3_anim_state_machine.py` plus `Python/tests/test_phase9h_wave4_create_anim_state_machine.py` **2 passed in 76.09s** after a clean `playtestingEditor` rebuild. Current MCP surface is now **138** tools. |
| 2026-05-13 | **Phase 9h Wave 4 AnimBlueprint state-machine creation complete** | Added `create_anim_state_machine` as the first state-machine mutation helper in the animation / AnimBlueprint tranche. The handler resolves a target `UAnimationGraph`, creates one `UAnimGraphNode_StateMachine` through `FGraphNodeCreator`, renames the owned `UAnimationStateMachineGraph`, marks the blueprint structurally modified, saves the asset, and reuses `read_anim_state_machine` for deterministic post-create readback. Focused coverage landed together with the Wave 3 companion: `Python/tests/test_phase9h_wave3_anim_state_machine_wrappers.py` and `Python/tests/test_phase9h_wave4_create_anim_state_machine_wrappers.py` **2 passed in 1.14s**, and `Python/tests/test_phase9h_wave3_anim_state_machine.py` plus `Python/tests/test_phase9h_wave4_create_anim_state_machine.py` **2 passed in 76.09s** after a clean `playtestingEditor` rebuild. Current MCP surface is now **139** tools. |
| 2026-05-13 | **Phase 9h Wave 5 AnimBlueprint state creation complete** | Added `create_anim_state` as the first state-level mutation helper in the animation / AnimBlueprint tranche. The handler resolves one named `UAnimationStateMachineGraph`, creates one `UAnimStateNode` through `FGraphNodeCreator`, renames the owned `UAnimationStateGraph`, auto-connects the entry node when the machine has no current entry target, saves the asset, and reuses `read_anim_state_machine` for deterministic post-create readback. Focused coverage landed together with the Wave 6 companion: `Python/tests/test_phase9h_wave5_create_anim_state_wrappers.py` plus `Python/tests/test_phase9h_wave6_anim_state_rename_delete_wrappers.py` **3 passed in 0.87s**, and `Python/tests/test_phase9h_wave5_create_anim_state.py` plus `Python/tests/test_phase9h_wave6_anim_state_rename_delete.py` **2 passed in 25.13s** after a clean `playtestingEditor` rebuild. Current MCP surface is now **140** tools. |
| 2026-05-13 | **Phase 9h Wave 6 AnimBlueprint state rename/delete complete** | Added `rename_anim_state` and `delete_anim_state` as the first state lifecycle mutation helpers in the animation / AnimBlueprint tranche. Rename uses the state node's bound-graph rename seam for deterministic name updates, while delete explicitly removes transitions touching the target state before destroying the state node so the state machine does not retain dangling topology. Focused coverage landed together with the Wave 5 companion: `Python/tests/test_phase9h_wave5_create_anim_state_wrappers.py` plus `Python/tests/test_phase9h_wave6_anim_state_rename_delete_wrappers.py` **3 passed in 0.87s**, and `Python/tests/test_phase9h_wave5_create_anim_state.py` plus `Python/tests/test_phase9h_wave6_anim_state_rename_delete.py` **2 passed in 25.13s** after a clean `playtestingEditor` rebuild. Current MCP surface is now **142** tools. |
| 2026-05-13 | **Phase 9h Wave 7 AnimBlueprint transition lifecycle complete** | Added `create_anim_transition` and `delete_anim_transition` as the first directed transition mutation helpers in the animation / AnimBlueprint tranche. Transition creation now reuses `UAnimationStateMachineSchema::TryCreateConnection` for deterministic edge authoring between existing state pins, while delete resolves matching transition nodes between one source-target pair and destroys them before reusing `read_anim_state_machine` for post-mutation topology readback. Focused coverage landed together with the Wave 8 companion: `Python/tests/test_phase9h_wave7_anim_transition_wrappers.py` plus `Python/tests/test_phase9h_wave8_anim_transition_rule_wrappers.py` **3 passed in 1.18s**, and `Python/tests/test_phase9h_wave7_anim_transition.py` plus `Python/tests/test_phase9h_wave8_anim_transition_rule.py` **2 passed in 29.22s** after a clean `playtestingEditor` rebuild. Current MCP surface is now **144** tools. |
| 2026-05-13 | **Phase 9h Wave 8 AnimBlueprint transition rule helpers complete** | Added `set_anim_transition_rule` as the first common-guard authoring helper in the animation / AnimBlueprint tranche. The handler resets one transition's `UAnimationTransitionGraph`, supports deterministic `always_true`, bool-variable, int-equality, and enum-equality rule shapes, serializes transition `rule_summary` metadata through shared state-machine readback, and hardens the supporting Blueprint-variable path by accepting enum type paths plus non-`/Game/Blueprints` asset lookup during live setup. The implementation uses concrete `KismetMathLibrary` equality call nodes for stable int/enum rule authoring and re-reads pins after node reconstruction so expected-value constants survive deterministic readback. Focused coverage landed together with the Wave 7 companion: `Python/tests/test_phase9h_wave7_anim_transition_wrappers.py` plus `Python/tests/test_phase9h_wave8_anim_transition_rule_wrappers.py` **3 passed in 1.18s**, and `Python/tests/test_phase9h_wave7_anim_transition.py` plus `Python/tests/test_phase9h_wave8_anim_transition_rule.py` **2 passed in 29.22s** after a clean `playtestingEditor` rebuild. Current MCP surface is now **145** tools. |
| 2026-05-13 | **Phase 9h Wave 9 AnimBlueprint sequence-player state binding complete** | Added `set_anim_state_sequence_player` as the first state pose-graph asset-player helper in the animation / AnimBlueprint tranche. The handler resolves one target state's `UAnimationStateGraph`, clears every non-result node, inserts one concrete `UAnimGraphNode_SequencePlayer`, wires its pose output into the state's `UAnimGraphNode_StateResult`, saves the AnimBlueprint, and reuses shared state-machine readback to serialize deterministic `asset_player_summary`, `asset_player_binding_type`, and animation-asset metadata per state. Focused coverage landed together with the Wave 10 companion: `Python/tests/test_phase9h_wave9_anim_state_sequence_player_wrappers.py` plus `Python/tests/test_phase9h_wave10_anim_state_blend_space_player_wrappers.py` **2 passed in 1.37s**, and `Python/tests/test_phase9h_wave9_anim_state_sequence_player.py` plus `Python/tests/test_phase9h_wave10_anim_state_blend_space_player.py` **2 passed in 24.94s** after a clean `playtestingEditor` rebuild. Current MCP surface is now **146** tools. |
| 2026-05-13 | **Phase 9h Wave 10 AnimBlueprint blend-space / aim-offset state binding complete** | Added `set_anim_state_blend_space_player` as the paired state pose-graph asset-player helper in the animation / AnimBlueprint tranche. The handler reuses the same state-graph reset and serializer seam as Wave 9, choosing `UAnimGraphNode_BlendSpacePlayer` for standard `UBlendSpace` assets and `UAnimGraphNode_RotationOffsetBlendSpace` for `UAimOffsetBlendSpace` and `UAimOffsetBlendSpace1D`, then returns deterministic post-mutation readback including the updated state's bound asset-player summary. Focused coverage landed together with the Wave 9 companion: `Python/tests/test_phase9h_wave9_anim_state_sequence_player_wrappers.py` plus `Python/tests/test_phase9h_wave10_anim_state_blend_space_player_wrappers.py` **2 passed in 1.37s**, and `Python/tests/test_phase9h_wave9_anim_state_sequence_player.py` plus `Python/tests/test_phase9h_wave10_anim_state_blend_space_player.py` **2 passed in 24.94s** after a clean `playtestingEditor` rebuild. Current MCP surface is now **147** tools. |