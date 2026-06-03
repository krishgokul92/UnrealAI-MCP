# UnrealAI

UnrealAI is an Unreal Engine 5.5+ editor plugin plus a Python MCP (Model Context Protocol)
server that lets any MCP-aware AI client inspect, build, and modify content inside a
running Unreal Editor.

## 🚀 Key Features

- **✨ 202 MCP Tools** — Complete programmatic access to Unreal Editor (Blueprints, Materials, Landscapes, Widgets, Level Sequences, PCG, Niagara, Animations, and more)
- **🤖 AI-First Design** — Works with Claude, GitHub Copilot, Cursor, Cline, or any MCP-aware client
- **🎨 Blueprint Automation** — Generate complex Blueprint graphs through natural language
- **⚡ Real-Time Editing** — Inspect and modify UE5 content live in the editor
- **🔗 Multiple Frontends** — In-editor Slate panel, VS Code integration, or standalone HTTP agent

The package contains three things:

1. **`Plugin/UnrealAIPlugin/`** &mdash; the C++ editor plugin that opens a TCP bridge
   (default port `55557`) and exposes the in-editor command surface.
2. **`Python/`** &mdash; the MCP server (`unreal_ai_mcp.py`) and an optional HTTP agent
   (`unreal_ai_agent.py`).
3. **`claude-ue-skill/` + `prompts/`** &mdash; the agent skill files and the
   ready-to-drop prompt definitions for Claude Code and GitHub Copilot Chat.

---

## Layout

```
UnrealAI-Release/
├── README.md                     <- you are here
├── ROADMAP.md
├── Plugin/
│   └── UnrealAIPlugin/           <- drop into <YourProject>/Plugins/
│       ├── UnrealAI.uplugin
│       ├── Config/
│       └── Source/UnrealAI/
├── Python/
│   ├── pyproject.toml
│   ├── unreal_ai_mcp.py          <- MCP server entrypoint
│   ├── unreal_ai_agent.py        <- optional HTTP agent
│   ├── unreal_ai_tooling.py
│   ├── ue_bridge.py
│   ├── ue_tools.py
│   ├── t3d_validator.py
│   ├── t3d_snippet_loader.py
│   ├── t3d_snippets/
│   └── tests/
├── claude-ue-skill/              <- agent skill loaded by unreal_ai_agent.py
│   ├── SKILL.md
│   └── references/
├── prompts/
│   ├── claude-agents/unreal-mcp.md
│   └── github-chatmodes/unreal-mcp.chatmode.md
└── Guides/
    ├── blueprint-graph-guide.md
    ├── colored-shapes-tutorial.md
    ├── phase1-release-guide.md
    ├── prompt-examples.md
    └── tools-reference.md
```

---

## Requirements

- **Unreal Engine 5.5, 5.6, or 5.7** (Win64 / Mac / Linux)
- **Python 3.10 – 3.13**
- A C++ toolchain for your platform (Visual Studio 2022 on Windows)

---

## Install the editor plugin

1. Copy `Plugin/UnrealAIPlugin/` into your Unreal project's `Plugins/` folder, e.g.
   `<YourProject>/Plugins/UnrealAIPlugin/`.
2. Right-click your `.uproject` &rarr; **Generate Visual Studio project files**.
3. Open the solution and build the **Editor** target for your project
   (e.g. `YourProjectEditor | Development Editor | Win64`).
4. Launch the editor. The plugin auto-starts the TCP bridge on port `55557`
   and exposes the **UnrealAI Control Panel** under *Window &rarr; UnrealAI*.

---

## Install the Python MCP server

```powershell
cd Python
python -m venv .venv
.\.venv\Scripts\Activate.ps1
pip install -e .
```

Run the server:

```powershell
python unreal_ai_mcp.py
```

The server reads MCP requests on stdio and forwards them to the editor over the
TCP bridge.

---

## Wire it into your MCP client

### Claude Code / Claude Desktop

`claude_desktop_config.json`:

```json
{
  "mcpServers": {
    "unreal-ai": {
      "command": "python",
      "args": ["E:/path/to/UnrealAI-Release/Python/unreal_ai_mcp.py"]
    }
  }
}
```

Also copy `prompts/claude-agents/unreal-mcp.md` into your project's
`.claude/agents/` folder to get the matching agent definition.

### VS Code (GitHub Copilot Chat)

Copy `prompts/github-chatmodes/unreal-mcp.chatmode.md` into your workspace's
`.github/chatmodes/` folder, then register the MCP server with your client of choice.

### Cursor / Cline / any other MCP host

Point the host at the same `python unreal_ai_mcp.py` command. The server speaks
standard MCP over stdio.

---

## Common Workflows

Looking for a specific capability? Start here:

- **Create colored objects?** → [Colored Shapes Tutorial](Guides/colored-shapes-tutorial.md)
- **Build Blueprint logic with AI?** → [Blueprint Graph Guide](Guides/blueprint-graph-guide.md)
- **See what tools are available?** → [Tools Reference](Guides/tools-reference.md)
- **Look for prompt examples?** → [Prompt Examples](Guides/prompt-examples.md)
- **Need material editing, landscapes, or animations?** → [Tools Reference](Guides/tools-reference.md) (search by capability)

---

## Optional: HTTP agent

`Python/unreal_ai_agent.py` is a FastAPI service (default port `8765`) that wraps
the same tool surface for browser / WebSocket clients. It loads the skill files
from `claude-ue-skill/` at startup, so keep the two folders next to each other
or set `UNREAL_AI_SKILL_ROOT` to an explicit path.

Run it with:

```powershell
python unreal_ai_agent.py
```

---

## Tests

```powershell
cd Python
pip install pytest
pytest tests/ -k "wrappers"
```

The `wrappers` subset is environment-independent. Live tests (those without the
`_wrappers` suffix) require a running editor with the plugin loaded.

---

## Documentation

### 📚 Getting Started & References

| Guide | Description |
|-------|-------------|
| [**Tools Reference**](Guides/tools-reference.md) | Complete reference of all **202 MCP tools** organized by workflow (Blueprints, Materials, Landscapes, Widgets, Level Sequences, PCG, Niagara, etc.). Start here to understand what's available. |
| [**Blueprint Graph Guide**](Guides/blueprint-graph-guide.md) | Learn how to programmatically create and manipulate Blueprint graphs using MCP tools. Build complex Blueprint logic through natural language without opening the editor. |
| [**Colored Shapes Tutorial**](Guides/colored-shapes-tutorial.md) | 🎨 Step-by-step tutorial for creating colored geometric shapes in UE5. Perfect for learning the workflow: create Blueprint → add components → assign materials → spawn actors. |
| [**Prompt Examples**](Guides/prompt-examples.md) | 💡 Real-world examples showing how natural language requests translate into MCP tool calls. Includes architectural projects (houses, castles), game level design (mazes, physics playgrounds), and more. |

### 📖 Additional Documentation

- [**claude-ue-skill/README.md**](claude-ue-skill/README.md) — Claude Code skill installation and features
- [**claude-ue-skill/SKILL.md**](claude-ue-skill/SKILL.md) — Detailed skill reference for the Claude integration

---

## License

MIT License

Copyright (c) 2026 UnrealAI

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
