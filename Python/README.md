# UnrealAI Python MCP Server

This folder contains the standalone MCP server that bridges an MCP-aware AI
client (Claude, Cursor, Copilot Chat, Cline, ...) with the UnrealAI editor
plugin.

See the top-level [`../README.md`](../README.md) for full setup. Quick start:

```powershell
python -m venv .venv
.\.venv\Scripts\Activate.ps1
pip install -e .
python unreal_ai_mcp.py
```

## Files

| File | Purpose |
| ---- | ------- |
| `unreal_ai_mcp.py` | FastMCP server entrypoint (stdio). |
| `unreal_ai_agent.py` | Optional FastAPI HTTP agent on port 8765. |
| `unreal_ai_tooling.py` | Tool registry, categorization, wrappers. |
| `ue_bridge.py` | TCP client for the editor bridge (port 55557). |
| `ue_tools.py` | Schema + dispatch surface shared with the HTTP agent. |
| `t3d_validator.py` | Validator for Blueprint T3D snippets. |
| `t3d_snippet_loader.py` | Loader for the `t3d_snippets/` library. |
| `t3d_snippets/` | Reusable T3D node templates. |
| `tests/` | Pytest suite (wrapper tests run without the editor). |

## Tests

```powershell
pip install pytest
pytest tests/ -k "wrappers"
```

Live tests (no `_wrappers` suffix) require an Unreal Editor with the UnrealAI
plugin running and listening on `127.0.0.1:55557`.
