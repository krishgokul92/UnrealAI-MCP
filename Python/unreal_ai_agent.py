"""
UnrealAI Agent Server
=====================

HTTP bridge between the UnrealAI Slate panel (inside Unreal Editor) and Ollama.

Endpoints:
    POST /chat       — send a user message, get an assistant response
    GET  /models     — list available Ollama models
    GET  /health     — liveness check

The agent:
    1. Maintains per-session chat history
    2. Injects SKILL.md + blueprint_serialization.md as the system prompt
    3. Calls Ollama's /api/chat endpoint
    4. (Phase 3: text-only. Tool-calling loop comes in Phase 4+.)

Run:
    python unreal_ai_agent.py

Default ports:
    Agent HTTP server : 8765
    Ollama            : 11434 (local)
"""

from __future__ import annotations

import json
import logging
import os
import re
import sys
import threading
import time
import uuid
from pathlib import Path
from typing import Any, Dict, List, Optional

import requests
from fastapi import FastAPI, HTTPException
from fastapi.middleware.cors import CORSMiddleware
from pydantic import BaseModel, Field
import uvicorn

from ue_tools import TOOL_SCHEMAS, dispatch_tool


# --------------------------------------------------------------------------- #
# .env loader (no extra dependency)
# --------------------------------------------------------------------------- #

def _load_dotenv(path: Path) -> None:
    """Parse a .env file and inject into os.environ (does NOT override existing)."""
    if not path.is_file():
        return
    for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        if "=" not in line:
            continue
        key, _, value = line.partition("=")
        key = key.strip()
        value = value.strip().strip('"').strip("'")
        if key and key not in os.environ:
            os.environ[key] = value

_load_dotenv(Path(__file__).resolve().parent / ".env")


# --------------------------------------------------------------------------- #
# Configuration
# --------------------------------------------------------------------------- #

AGENT_HOST = os.environ.get("UNREALAI_HOST", "127.0.0.1")
AGENT_PORT = int(os.environ.get("UNREALAI_PORT", "8765"))
OLLAMA_URL = os.environ.get("OLLAMA_URL", "http://127.0.0.1:11434")
DEFAULT_MODEL = os.environ.get("UNREALAI_MODEL", "qwen2.5-coder:32b")

# OpenRouter (OpenAI-compatible)
OPENROUTER_URL = "https://openrouter.ai/api/v1"
OPENROUTER_API_KEY = os.environ.get("OPENROUTER_API_KEY", "")

# Curated list of recommended OpenRouter models (shown in the dropdown).
# The user can always type any model slug manually.
OPENROUTER_MODELS: List[str] = [
    "openrouter:qwen/qwen3-coder:free",
]

# Resolve skill file locations relative to this script.
# Project layout:
#   e:\unrealBP\claude-ue-skill\SKILL.md
#   e:\unrealBP\claude-ue-skill\references\blueprint_serialization.md
#   e:\unrealBP\UnrealAI\Python\unreal_ai_agent.py   <-- this file
HERE = Path(__file__).resolve().parent
SKILL_ROOT = (HERE.parent.parent / "claude-ue-skill").resolve()
SKILL_FILES = [
    SKILL_ROOT / "SKILL.md",
    SKILL_ROOT / "references" / "blueprint_serialization.md",
]


# --------------------------------------------------------------------------- #
# Logging
# --------------------------------------------------------------------------- #

logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s  %(levelname)-7s  %(name)s  %(message)s",
)
log = logging.getLogger("UnrealAI.Agent")


# --------------------------------------------------------------------------- #
# System prompt
# --------------------------------------------------------------------------- #

def _load_system_prompt() -> str:
    """Load and concatenate all skill files into one system prompt."""
    parts: List[str] = [
        "You are UnrealAI, an expert Unreal Engine 5 assistant integrated "
        "inside the UE Editor. You help with C++ gameplay programming, "
        "Blueprint editing, and project management. Always be concise, "
        "accurate, and reference the skill content below when relevant.\n\n"
        "You have access to tools that operate on the live Unreal project "
        "(list actors, spawn actors, create blueprints, etc.). Use them "
        "proactively when the user asks about or wants to change the scene. "
        "Never guess actor names or content paths — call a tool first. "
        "Each user turn begins with a fresh 'Current Unreal Editor Context' "
        "system block; trust that over the chat history.\n",
        # ---- Phase 5 (revised): T3D paste workflow ----
        "\n## Blueprint Authoring Workflow (T3D Paste)\n"
        "**HARD RULE:** When you need to add or modify nodes in a Blueprint graph, you MUST "
        "invoke the `paste_blueprint_graph` tool with the T3D text as the `t3d_text` argument. "
        "NEVER write the T3D text in the chat reply, in a markdown code block, or anywhere "
        "else visible to the user. Writing `Begin Object Class=...` outside a tool call has "
        "ZERO effect on the editor — the user only sees text on screen, no nodes appear. "
        "If you find yourself about to print T3D, stop and emit a tool call instead.\n\n"
        "When asked to create or modify a Blueprint graph, follow this protocol:\n"
        "1. **Create the asset** (if new) with `create_blueprint`.\n"
        "2. **Add components** (if needed) with `add_component_to_blueprint`.\n"
        "3. **Author the graph in ONE shot** by generating T3D clipboard text and calling the "
        "`paste_blueprint_graph` TOOL (not by printing the T3D in your reply). The T3D format "
        "is documented in the `blueprint_serialization.md` reference below — that is the same text "
        "the editor produces on Ctrl+C and accepts on Ctrl+V. The C++ side calls "
        "`FEdGraphUtilities::ImportNodesFromText` for you, so the editor builds "
        "real K2Nodes from your text.\n"
        "4. **Inspect** an existing BP first with `read_blueprint_content` or "
        "`analyze_blueprint_graph` so you know what's already there. Use "
        "`clear_graph=true` only if you intend to fully replace the graph.\n"
        "5. **Compile** is automatic when `auto_compile` is true (default). If "
        "you set it false, call `compile_blueprint` yourself.\n\n"
        "### Critical T3D rules (full spec in blueprint_serialization.md)\n"
        "- Wrap every node in `Begin Object Class=/Script/BlueprintGraph.<NodeClass> Name=\"<UniqueName>\"` … `End Object`.\n"
        "- Inside each node block, every NodeGuid and PinId MUST be a fresh GUID — never reuse them across the paste.\n"
        "- Variables are referenced by `VariableReference=(MemberName=\"<Name>\",MemberGuid=...,bSelfContext=True)`. The variable must already exist on the Blueprint (create it via the granular workflow if needed — but see note below).\n"
        "- Pin connections are written via `LinkedTo=(<TargetNodeName> <TargetPinId>,...)` inside the source pin's `CustomProperties Pin (... )` line. Both endpoints must reference each other.\n"
        "- Common nodes you'll generate: `K2Node_Event` (BeginPlay etc.), `K2Node_VariableGet`, `K2Node_VariableSet`, `K2Node_CallFunction` (PrintString lives in `/Script/Engine.KismetSystemLibrary`).\n\n"
        "### Failure modes to avoid\n"
        "- Do NOT invent variables in T3D — they must exist on the BP first, otherwise the paste succeeds but pins resolve to nothing.\n"
        "- Do NOT abbreviate or paraphrase the T3D — the importer is strict.\n"
        "- If `paste_blueprint_graph` returns `pasted_count: 0` or an error mentioning malformed text, re-read the spec and regenerate the WHOLE block; do not try to patch one line.\n"
        "- Note: this build only exposes `paste_blueprint_graph` for graph authoring. Variable creation is not yet exposed as a separate tool — for now, ask the user to add the variable manually in the editor before calling paste, OR include the variable's referenced metadata in your T3D and rely on the user to create the variable defaults afterward.\n",
    ]
    for f in SKILL_FILES:
        if f.is_file():
            try:
                parts.append(f"\n\n===== {f.name} =====\n\n")
                parts.append(f.read_text(encoding="utf-8", errors="replace"))
                log.info("Loaded skill file: %s (%d bytes)", f.name, f.stat().st_size)
            except Exception as exc:
                log.warning("Failed to read %s: %s", f, exc)
        else:
            log.warning("Skill file not found: %s", f)
    return "".join(parts)


SYSTEM_PROMPT = _load_system_prompt()
log.info("System prompt assembled (%d chars)", len(SYSTEM_PROMPT))


# --------------------------------------------------------------------------- #
# Session state (in-memory, per-process)
# --------------------------------------------------------------------------- #

class Session:
    """Holds the running chat history for one panel."""

    def __init__(self, session_id: str) -> None:
        self.id = session_id
        self.messages: List[Dict[str, Any]] = []

    def append(self, role: str, content: str) -> None:
        self.messages.append({"role": role, "content": content})

    def trimmed(self, max_messages: int = 40) -> List[Dict[str, Any]]:
        """Return the most recent `max_messages` messages."""
        return self.messages[-max_messages:]


SESSIONS: Dict[str, Session] = {}

# Models that returned 400 when `tools` was sent — skip sending tools for them.
MODELS_WITHOUT_TOOL_SUPPORT: set[str] = set()


# --------------------------------------------------------------------------- #
# Background jobs (so UE doesn't hold a long HTTP request open)
# --------------------------------------------------------------------------- #

class Job:
    """A single chat request running on a background thread."""

    __slots__ = ("id", "status", "response", "error", "model",
                 "started_at", "finished_at",
                 "partial_text", "tool_log", "_lock")

    def __init__(self, job_id: str, model: str) -> None:
        self.id = job_id
        self.status = "pending"          # pending | done | error
        self.response: str = ""
        self.error: str = ""
        self.model = model
        self.started_at = time.time()
        self.finished_at: Optional[float] = None
        self.partial_text: str = ""      # streamed text so far (while pending)
        self.tool_log: List[Dict[str, Any]] = []  # [{name, args, result}, ...]
        self._lock = threading.Lock()


JOBS: Dict[str, Job] = {}
JOBS_LOCK = threading.Lock()
JOB_TTL_SECONDS = 600  # keep finished jobs for 10 minutes


def _sweep_jobs() -> None:
    """Drop jobs older than TTL to keep memory bounded."""
    now = time.time()
    with JOBS_LOCK:
        stale = [
            jid for jid, j in JOBS.items()
            if j.finished_at is not None and (now - j.finished_at) > JOB_TTL_SECONDS
        ]
        for jid in stale:
            JOBS.pop(jid, None)


def _get_session(session_id: Optional[str]) -> Session:
    sid = session_id or "default"
    if sid not in SESSIONS:
        SESSIONS[sid] = Session(sid)
        log.info("Created new session: %s", sid)
    return SESSIONS[sid]


# --------------------------------------------------------------------------- #
# Ollama client
# --------------------------------------------------------------------------- #

def _ollama_chat(
    model: str,
    messages: List[Dict[str, Any]],
    tools: Optional[List[Dict[str, Any]]] = None,
    job: Optional["Job"] = None,
) -> Dict[str, Any]:
    """Call Ollama's /api/chat with streaming. Returns the `message` object.

    If `job` is provided, updates job.partial_text as tokens arrive so the
    UE panel can display incremental text while still thinking.
    """
    url = f"{OLLAMA_URL.rstrip('/')}/api/chat"

    def _build_payload(include_tools: bool) -> Dict[str, Any]:
        payload: Dict[str, Any] = {
            "model": model,
            "messages": messages,
            "stream": True,
            "options": {
                "temperature": 0.2,
                "num_ctx": 16384,
            },
        }
        if include_tools and tools:
            payload["tools"] = tools
        return payload

    log.info("Ollama call -> model=%s, messages=%d, tools=%d",
             model, len(messages), len(tools or []))

    # If we already know this model rejects tools, skip sending them.
    effective_tools = tools
    if tools and model in MODELS_WITHOUT_TOOL_SUPPORT:
        log.info("Skipping tools for %s (known unsupported)", model)
        effective_tools = None

    def _do_stream(payload: Dict[str, Any], is_tools_payload: bool = False) -> Dict[str, Any]:
        """Stream from Ollama NDJSON, collect full message, update job.partial_text."""
        try:
            resp = requests.post(url, json=payload, timeout=300, stream=True)
        except requests.Timeout:
            raise HTTPException(status_code=504, detail="Ollama request timed out (300s).")
        except requests.ConnectionError:
            raise HTTPException(
                status_code=502,
                detail=f"Cannot connect to Ollama at {OLLAMA_URL}. Is `ollama serve` running?",
            )

        if resp.status_code == 400 and is_tools_payload:
            resp.close()
            return {}  # signal to retry without tools

        if resp.status_code >= 400:
            body = resp.text[:500]
            resp.close()
            log.error("Ollama %d from %s: %s", resp.status_code, model, body)
            raise HTTPException(status_code=502, detail=f"Ollama {resp.status_code}: {body}")

        content_parts: List[str] = []
        final_message: Dict[str, Any] = {}

        for raw_line in resp.iter_lines(decode_unicode=True):
            if not raw_line:
                continue
            try:
                chunk = json.loads(raw_line)
            except json.JSONDecodeError:
                continue

            # Each chunk has {"message": {"role": "assistant", "content": "tok"}, "done": false}
            msg = chunk.get("message") or {}
            tok = msg.get("content") or ""
            if tok:
                content_parts.append(tok)
                if job:
                    with job._lock:
                        job.partial_text = "".join(content_parts)

            if chunk.get("done"):
                final_message = msg
                break

        resp.close()

        # Ensure content is the full accumulated text.
        if not final_message:
            final_message = {"role": "assistant", "content": "".join(content_parts)}
        elif not final_message.get("content"):
            final_message["content"] = "".join(content_parts)

        return final_message

    has_tools = effective_tools is not None
    result = _do_stream(_build_payload(include_tools=has_tools), is_tools_payload=has_tools)

    # If Ollama rejected the tools payload, remember and retry without tools.
    if not result and effective_tools:
        log.warning("Ollama 400 with tools (%s). Retrying without tools.", model)
        MODELS_WITHOUT_TOOL_SUPPORT.add(model)
        result = _do_stream(_build_payload(include_tools=False), is_tools_payload=False)

    if not result:
        log.warning("Empty Ollama response for model %s", model)
        return {"role": "assistant", "content": ""}

    return result


def _extract_text_tool_calls(content: str) -> List[Dict[str, Any]]:
    """Parse tool calls that the model emitted as plain-text JSON.

    Handles shapes like:
        {"name": "...", "arguments": {...}}
        {"name": "...", "parameters": {...}}
        {"tool": "...", "arguments": {...}}
        multiple such objects on separate lines or separated by whitespace

    Also strips ```json ... ``` fences. Returns a list of {name, arguments} dicts.
    """
    if not content or not content.strip():
        return []

    text = content.strip()

    # Remove markdown code fences.
    if text.startswith("```"):
        lines = text.splitlines()
        # drop first fence
        lines = lines[1:]
        # drop trailing fence if present
        while lines and lines[-1].strip().startswith("```"):
            lines.pop()
        text = "\n".join(lines).strip()

    # Fast path: whole content is a JSON object
    candidates: List[Any] = []

    def _try(s: str) -> bool:
        s = s.strip()
        if not s:
            return False
        try:
            obj = json.loads(s)
        except json.JSONDecodeError:
            return False
        if isinstance(obj, dict):
            candidates.append(obj)
            return True
        if isinstance(obj, list):
            for o in obj:
                if isinstance(o, dict):
                    candidates.append(o)
            return True
        return False

    if not _try(text):
        # Try line-by-line — model may have emitted one object per line.
        for line in text.splitlines():
            _try(line)

    # Still nothing? Try to find JSON objects via brace counting.
    if not candidates:
        buf: list[str] = []
        depth = 0
        for ch in text:
            if ch == "{":
                if depth == 0:
                    buf = []
                depth += 1
            if depth > 0:
                buf.append(ch)
            if ch == "}":
                depth -= 1
                if depth == 0 and buf:
                    _try("".join(buf))
                    buf = []

    # Normalize to {name, arguments}.
    calls: List[Dict[str, Any]] = []
    for obj in candidates:
        name = obj.get("name") or obj.get("tool") or obj.get("function")
        if not isinstance(name, str) or not name:
            continue
        args = obj.get("arguments")
        if args is None:
            args = obj.get("parameters")
        if args is None:
            args = obj.get("args") or {}
        if isinstance(args, str):
            # arguments sometimes arrive as a JSON string
            try:
                args = json.loads(args)
            except json.JSONDecodeError:
                args = {}
        if not isinstance(args, dict):
            args = {}
        calls.append({"function": {"name": name, "arguments": args}})
    return calls


# --------------------------------------------------------------------------- #
# OpenRouter (OpenAI-compatible) client
# --------------------------------------------------------------------------- #

_THINK_RE = re.compile(r"<think>.*?</think>\s*", re.DOTALL)

def _strip_think_blocks(text: str) -> str:
    """Remove <think>…</think> blocks that some models emit."""
    return _THINK_RE.sub("", text).strip()


# Heuristic: spot T3D blueprint clipboard text emitted as plain reply text.
# True T3D paste payloads always contain a "Begin Object Class=" line and a
# matching "End Object" line, with the K2Node namespace nearby.
_T3D_BEGIN_RE = re.compile(
    r"Begin\s+Object\s+Class\s*=\s*/Script/(?:BlueprintGraph|UnrealEd|Engine)\.",
    re.IGNORECASE,
)

def _looks_like_unwrapped_t3d(text: str) -> bool:
    if not text:
        return False
    if not _T3D_BEGIN_RE.search(text):
        return False
    # Require at least one matching End Object so we don't false-positive on
    # casual mentions like "use Begin Object Class=...".
    return "End Object" in text


def _openrouter_chat(
    model_slug: str,
    messages: List[Dict[str, Any]],
    tools: Optional[List[Dict[str, Any]]] = None,
    job: Optional["Job"] = None,
) -> Dict[str, Any]:
    """Call OpenRouter's /chat/completions (OpenAI format) with streaming.

    `model_slug` is the part after 'openrouter:' e.g. 'qwen/qwen3-coder:free'.
    Returns an Ollama-shaped message dict: {role, content, tool_calls?}.
    """
    if not OPENROUTER_API_KEY:
        raise HTTPException(status_code=500, detail="OPENROUTER_API_KEY not set. Add it to .env")

    url = f"{OPENROUTER_URL}/chat/completions"
    headers = {
        "Authorization": f"Bearer {OPENROUTER_API_KEY}",
        "Content-Type": "application/json",
        "HTTP-Referer": "https://unrealai.local",
        "X-Title": "UnrealAI",
    }

    # Build OpenAI-format payload.
    # Skip tools if we already know this model doesn't support them.
    full_model = f"openrouter:{model_slug}"
    effective_tools = tools
    if tools and full_model in MODELS_WITHOUT_TOOL_SUPPORT:
        log.info("Skipping tools for %s (known unsupported)", model_slug)
        effective_tools = None

    payload: Dict[str, Any] = {
        "model": model_slug,
        "messages": messages,
        "temperature": 0.2,
        "max_tokens": 8192,
        "stream": True,
    }
    if effective_tools:
        payload["tools"] = effective_tools

    log.info("OpenRouter call -> model=%s, messages=%d, tools=%d",
             model_slug, len(messages), len(effective_tools or []))

    # Retry with exponential backoff for 429 rate-limit errors.
    max_retries = 4
    resp = None
    for attempt in range(max_retries):
        try:
            resp = requests.post(url, json=payload, headers=headers, timeout=300, stream=True)
        except requests.Timeout:
            raise HTTPException(status_code=504, detail="OpenRouter request timed out (300s).")
        except requests.ConnectionError:
            raise HTTPException(status_code=502, detail="Cannot connect to OpenRouter.")

        if resp.status_code == 429 and attempt < max_retries - 1:
            resp.close()
            wait = 2 ** attempt * 5  # 5s, 10s, 20s
            log.warning("OpenRouter 429 (attempt %d/%d), retrying in %ds...",
                        attempt + 1, max_retries, wait)
            time.sleep(wait)
            continue
        break

    # If the model doesn't support tools (404), retry without them.
    if resp.status_code == 404 and effective_tools:
        resp.close()
        log.warning("OpenRouter 404 with tools (%s). Retrying without tools.", model_slug)
        MODELS_WITHOUT_TOOL_SUPPORT.add(full_model)
        payload.pop("tools", None)
        payload["stream"] = True
        try:
            resp = requests.post(url, json=payload, headers=headers, timeout=300, stream=True)
        except (requests.Timeout, requests.ConnectionError):
            pass  # fall through to error handler below

    if resp.status_code >= 400:
        body = resp.text[:500]
        resp.close()
        log.error("OpenRouter %d from %s: %s", resp.status_code, model_slug, body)
        raise HTTPException(status_code=502, detail=f"OpenRouter {resp.status_code}: {body}")

    # Parse SSE stream: lines like "data: {...}\n\n" or "data: [DONE]\n\n"
    content_parts: List[str] = []
    # Tool calls accumulate across chunks (OpenAI streaming sends them incrementally).
    tc_accum: Dict[int, Dict[str, Any]] = {}  # index -> {id, name, arguments_str}

    for raw_line in resp.iter_lines(decode_unicode=True):
        if not raw_line or not raw_line.startswith("data: "):
            continue
        data_str = raw_line[6:]
        if data_str.strip() == "[DONE]":
            break
        try:
            chunk = json.loads(data_str)
        except json.JSONDecodeError:
            continue

        delta = (chunk.get("choices") or [{}])[0].get("delta") or {}
        tok = delta.get("content") or ""
        if tok:
            content_parts.append(tok)
            if job:
                with job._lock:
                    job.partial_text = "".join(content_parts)

        # Accumulate tool_calls deltas.
        for tc_delta in (delta.get("tool_calls") or []):
            idx = tc_delta.get("index", 0)
            if idx not in tc_accum:
                tc_accum[idx] = {"id": "", "name": "", "arguments_str": ""}
            if "id" in tc_delta:
                tc_accum[idx]["id"] = tc_delta["id"]
            fn = tc_delta.get("function") or {}
            if "name" in fn:
                tc_accum[idx]["name"] = fn["name"]
            if "arguments" in fn:
                tc_accum[idx]["arguments_str"] += fn["arguments"]

    resp.close()

    content = _strip_think_blocks("".join(content_parts))

    # Normalize accumulated tool_calls.
    tool_calls: List[Dict[str, Any]] = []
    for idx in sorted(tc_accum.keys()):
        tc = tc_accum[idx]
        raw_args = tc["arguments_str"]
        try:
            parsed_args = json.loads(raw_args) if raw_args else {}
        except json.JSONDecodeError:
            parsed_args = {}
        entry: Dict[str, Any] = {
            "function": {"name": tc["name"], "arguments": parsed_args},
        }
        if tc["id"]:
            entry["id"] = tc["id"]
        tool_calls.append(entry)

    result: Dict[str, Any] = {"role": "assistant", "content": content}
    if tool_calls:
        result["tool_calls"] = tool_calls
    return result


def _is_openrouter_model(model: str) -> bool:
    return model.startswith("openrouter:")


def _llm_chat(
    model: str,
    messages: List[Dict[str, Any]],
    tools: Optional[List[Dict[str, Any]]] = None,
    job: Optional["Job"] = None,
) -> Dict[str, Any]:
    """Dispatch to the right backend based on model prefix."""
    if _is_openrouter_model(model):
        slug = model[len("openrouter:"):]
        return _openrouter_chat(slug, messages, tools, job=job)
    return _ollama_chat(model, messages, tools, job=job)


def _run_tool_loop(
    model: str,
    outbound: List[Dict[str, Any]],
    session: "Session",
    max_iterations: int = 5,
    job: Optional["Job"] = None,
) -> str:
    """Call LLM, execute any tool_calls, loop until the model returns text.

    Appends the final assistant content to the session and returns it.
    """
    for iteration in range(max_iterations):
        # Reset partial text for each LLM call so the panel shows fresh streaming.
        if job:
            with job._lock:
                job.partial_text = ""

        msg = _llm_chat(model, outbound, tools=TOOL_SCHEMAS, job=job)
        tool_calls = msg.get("tool_calls") or []
        content = msg.get("content") or ""

        # Fallback: some models emit tool calls as plain-text JSON instead of
        # populating message.tool_calls. Detect and normalize those.
        text_mode = False
        if not tool_calls and content:
            parsed = _extract_text_tool_calls(content)
            if parsed:
                log.info("Iteration %d: parsed %d tool call(s) from text content",
                         iteration + 1, len(parsed))
                tool_calls = parsed
                text_mode = True

        # Persist the assistant turn in history.
        assistant_entry: Dict[str, Any] = {"role": "assistant", "content": content}
        if tool_calls and not text_mode:
            assistant_entry["tool_calls"] = tool_calls
        session.messages.append(assistant_entry)
        outbound.append(assistant_entry)

        if not tool_calls:
            # Guardrail: small models often "show" T3D in chat instead of
            # invoking paste_blueprint_graph. Detect that and re-prompt rather
            # than returning the useless T3D blob to the user.
            if _looks_like_unwrapped_t3d(content):
                log.warning(
                    "Iteration %d: assistant emitted T3D text without calling "
                    "paste_blueprint_graph — nudging model to retry as tool call.",
                    iteration + 1,
                )
                nudge = {
                    "role": "system",
                    "content": (
                        "STOP. You wrote T3D text in your reply. That has NO effect — "
                        "no nodes were added to the Blueprint. You MUST invoke the "
                        "`paste_blueprint_graph` tool with the T3D as the `t3d_text` "
                        "argument. Do that now. Do not paraphrase, do not re-explain, "
                        "do not put the T3D in another code block — issue the tool call."
                    ),
                }
                session.messages.append(nudge)
                outbound.append(nudge)
                continue
            return content

        # Execute each tool and feed results back.
        log.info("Iteration %d: executing %d tool call(s)", iteration + 1, len(tool_calls))
        for tc in tool_calls:
            fn = (tc.get("function") or {})
            name = fn.get("name", "")
            args = fn.get("arguments", {})

            # Update job partial to show what tool is running.
            if job:
                with job._lock:
                    job.partial_text = f"[Calling {name}...]"

            result_text = dispatch_tool(name, args)

            # Log tool call for the panel's collapsible tool-call log.
            if job:
                with job._lock:
                    job.tool_log.append({
                        "name": name,
                        "args": args,
                        "result": result_text[:500],  # truncate for display
                    })

            tool_entry: Dict[str, Any] = {
                "role": "tool",
                "content": result_text,
                "name": name,
            }
            # Some Ollama builds echo a tool_call_id; include if present.
            if "id" in tc:
                tool_entry["tool_call_id"] = tc["id"]
            session.messages.append(tool_entry)
            outbound.append(tool_entry)

        # If we parsed from text, nudge the model with a system hint so the next
        # turn produces a natural-language answer instead of another JSON blob.
        if text_mode:
            nudge = {
                "role": "system",
                "content": (
                    "Tool results are above. Now reply to the user in plain natural "
                    "language, summarizing what you did and what was found. "
                    "Do not emit any more JSON tool calls unless you need more data."
                ),
            }
            session.messages.append(nudge)
            outbound.append(nudge)

    log.warning("Tool loop hit max_iterations=%d", max_iterations)
    return (
        "[UnrealAI] I reached the tool-call iteration limit. "
        "Try breaking the request into smaller steps."
    )


def _ollama_list_models() -> List[str]:
    """Return the list of locally available Ollama model names."""
    url = f"{OLLAMA_URL.rstrip('/')}/api/tags"
    try:
        resp = requests.get(url, timeout=10)
        resp.raise_for_status()
        data = resp.json()
        return [m["name"] for m in data.get("models", []) if "name" in m]
    except Exception as exc:
        log.warning("Failed to list Ollama models: %s", exc)
        return []


# --------------------------------------------------------------------------- #
# FastAPI app
# --------------------------------------------------------------------------- #

app = FastAPI(title="UnrealAI Agent", version="0.3.0")

# Allow requests from any local origin (UE is a desktop app, not a browser,
# but this makes debugging with curl / Postman painless).
app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"],
    allow_methods=["*"],
    allow_headers=["*"],
)


class ChatRequest(BaseModel):
    message: str = Field(..., description="User's message.")
    model: Optional[str] = Field(None, description="Ollama model name.")
    session_id: Optional[str] = Field(None, description="Chat session identifier.")
    reset: bool = Field(False, description="If true, clear history before this message.")
    context: Optional[Dict[str, Any]] = Field(
        None, description="Live Unreal Editor context (project name, level, selection, etc.)."
    )


class ChatResponse(BaseModel):
    session_id: str
    model: str
    response: str
    history_length: int
    job_id: Optional[str] = None
    status: Optional[str] = None  # "pending" | "done" | "error"
    partial_text: Optional[str] = None  # streaming text while still pending
    tool_log: Optional[List[Dict[str, Any]]] = None  # [{name, args, result}, ...]


@app.get("/health")
def health() -> Dict[str, Any]:
    return {
        "status": "ok",
        "ollama_url": OLLAMA_URL,
        "default_model": DEFAULT_MODEL,
        "skill_chars": len(SYSTEM_PROMPT),
        "sessions": len(SESSIONS),
    }


@app.get("/models")
def list_models() -> Dict[str, Any]:
    models = _ollama_list_models()
    # Append curated OpenRouter models if an API key is configured.
    if OPENROUTER_API_KEY:
        models.extend(OPENROUTER_MODELS)
    return {"models": models, "default": DEFAULT_MODEL}


def _format_context(ctx: Optional[Dict[str, Any]]) -> Optional[str]:
    """Render a project-context dict as a concise text block for the model."""
    if not ctx:
        return None
    lines: List[str] = ["# Current Unreal Editor Context"]
    scalar_keys = [
        ("project_name", "Project"),
        ("project_dir", "Project dir"),
        ("content_dir", "Content dir"),
        ("source_dir", "Source dir"),
        ("engine_version", "Engine"),
        ("current_level", "Current level"),
        ("actor_count", "Actors in level"),
    ]
    for key, label in scalar_keys:
        if key in ctx and ctx[key] not in (None, ""):
            lines.append(f"- {label}: {ctx[key]}")

    plugins = ctx.get("project_plugins") or []
    if plugins:
        lines.append(f"- Project plugins: {', '.join(plugins)}")

    content_folders = ctx.get("content_top_folders") or []
    if content_folders:
        lines.append(f"- Content top-level folders: {', '.join(content_folders)}")

    source_modules = ctx.get("source_modules") or []
    if source_modules:
        lines.append(f"- C++ source modules: {', '.join(source_modules)}")

    selection = ctx.get("selected_actors") or []
    if selection:
        lines.append(f"- Selected actors ({len(selection)}):")
        for s in selection[:10]:
            lines.append(f"    • {s.get('name')} ({s.get('class')}) @ {s.get('location')}")
        if len(selection) > 10:
            lines.append(f"    … and {len(selection) - 10} more")
    else:
        lines.append("- Selected actors: none")

    return "\n".join(lines)


@app.post("/chat", response_model=ChatResponse)
def chat(req: ChatRequest) -> ChatResponse:
    """Kick off a chat completion on a background thread and return a job id.

    The UE panel polls /chat/status/{job_id} to retrieve the final response.
    This avoids UE's HTTP-activity timeout on slow models like qwen:32b.
    """
    if not req.message.strip():
        raise HTTPException(status_code=400, detail="Message is empty.")

    session = _get_session(req.session_id)
    if req.reset:
        session.messages.clear()
        log.info("Session %s reset", session.id)

    session.append("user", req.message)

    # Build the message list sent to Ollama.
    # Order: skill system prompt → live project context (if any) → history.
    outbound: List[Dict[str, Any]] = [{"role": "system", "content": SYSTEM_PROMPT}]

    ctx_text = _format_context(req.context)
    if ctx_text:
        outbound.append({"role": "system", "content": ctx_text})
        log.info("Attached project context (%d chars)", len(ctx_text))

    outbound.extend(session.trimmed())

    model = req.model or DEFAULT_MODEL

    # Create a job and run the tool loop on a worker thread.
    job_id = uuid.uuid4().hex
    job = Job(job_id, model)
    with JOBS_LOCK:
        JOBS[job_id] = job

    def _worker() -> None:
        try:
            reply = _run_tool_loop(model, outbound, session, job=job)
            with JOBS_LOCK:
                job.response = reply
                job.status = "done"
                job.finished_at = time.time()
            log.info("Job %s done (%.1fs, %d chars)", job_id,
                     (job.finished_at or 0) - job.started_at, len(reply))
        except HTTPException as exc:
            with JOBS_LOCK:
                job.error = str(exc.detail)
                job.status = "error"
                job.finished_at = time.time()
            log.warning("Job %s failed: %s", job_id, exc.detail)
        except Exception as exc:  # noqa: BLE001
            with JOBS_LOCK:
                job.error = f"{type(exc).__name__}: {exc}"
                job.status = "error"
                job.finished_at = time.time()
            log.exception("Job %s crashed", job_id)
        finally:
            _sweep_jobs()

    threading.Thread(target=_worker, name=f"chat-{job_id[:8]}", daemon=True).start()
    log.info("Job %s started (model=%s)", job_id, model)

    # Return immediately with the job id. Response text is empty until polled.
    return ChatResponse(
        session_id=session.id,
        model=model,
        response="",
        history_length=len(session.messages),
        job_id=job_id,
        status="pending",
    )


@app.get("/chat/status/{job_id}", response_model=ChatResponse)
def chat_status(job_id: str) -> ChatResponse:
    """Poll a chat job's state. Returns response text once status == 'done'."""
    with JOBS_LOCK:
        job = JOBS.get(job_id)
        if job is None:
            raise HTTPException(status_code=404, detail=f"Unknown job id: {job_id}")
        # Snapshot under the lock.
        status = job.status
        response = job.response
        error = job.error
        model = job.model
        with job._lock:
            partial = job.partial_text
            tool_log_snapshot = list(job.tool_log)

    return ChatResponse(
        session_id="",
        model=model,
        response=response if status == "done" else ("" if status == "pending" else error),
        history_length=0,
        job_id=job_id,
        status=status,
        partial_text=partial if status == "pending" else None,
        tool_log=tool_log_snapshot if tool_log_snapshot else None,
    )


@app.post("/reset")
def reset(session_id: Optional[str] = None) -> Dict[str, Any]:
    session = _get_session(session_id)
    session.messages.clear()
    return {"status": "ok", "session_id": session.id}


# --------------------------------------------------------------------------- #
# Session save / load
# --------------------------------------------------------------------------- #

SESSIONS_DIR = Path(__file__).resolve().parent / "sessions"
SESSIONS_DIR.mkdir(exist_ok=True)


def _safe_filename(name: str) -> str:
    """Sanitise a session name for use as a file name."""
    import re
    cleaned = re.sub(r'[^\w\-. ]', '_', name).strip()
    return cleaned[:120] if cleaned else "unnamed"


@app.post("/session/save")
def session_save(session_id: str, name: Optional[str] = None) -> Dict[str, Any]:
    """Save a session to disk as JSON."""
    session = _get_session(session_id)
    if not session.messages:
        raise HTTPException(status_code=400, detail="Session is empty — nothing to save.")

    save_name = _safe_filename(name or session.id)
    ts = time.strftime("%Y%m%d_%H%M%S")
    filename = f"{save_name}_{ts}.json"
    filepath = SESSIONS_DIR / filename

    data = {
        "session_id": session.id,
        "name": save_name,
        "saved_at": ts,
        "messages": session.messages,
    }
    filepath.write_text(json.dumps(data, indent=2), encoding="utf-8")
    log.info("Saved session %s -> %s (%d messages)", session.id, filepath, len(session.messages))
    return {"status": "ok", "filename": filename, "messages": len(session.messages)}


@app.post("/session/load")
def session_load(filename: str, session_id: Optional[str] = None) -> Dict[str, Any]:
    """Load a previously saved session from disk."""
    filepath = SESSIONS_DIR / filename
    if not filepath.exists():
        raise HTTPException(status_code=404, detail=f"File not found: {filename}")

    # Guard against path traversal.
    try:
        filepath.resolve().relative_to(SESSIONS_DIR.resolve())
    except ValueError:
        raise HTTPException(status_code=400, detail="Invalid filename.")

    data = json.loads(filepath.read_text(encoding="utf-8"))
    messages = data.get("messages") or []

    session = _get_session(session_id)
    session.messages = messages
    log.info("Loaded session %s from %s (%d messages)", session.id, filename, len(messages))
    return {
        "status": "ok",
        "session_id": session.id,
        "filename": filename,
        "messages": len(messages),
        "history": messages,  # send full history so panel can rebuild UI
    }


@app.get("/sessions")
def list_sessions() -> Dict[str, Any]:
    """List all saved session files."""
    files = []
    for p in sorted(SESSIONS_DIR.glob("*.json"), key=lambda f: f.stat().st_mtime, reverse=True):
        try:
            data = json.loads(p.read_text(encoding="utf-8"))
            files.append({
                "filename": p.name,
                "name": data.get("name", p.stem),
                "saved_at": data.get("saved_at", ""),
                "message_count": len(data.get("messages", [])),
            })
        except Exception:
            continue
    return {"sessions": files}


# --------------------------------------------------------------------------- #
# Entry point
# --------------------------------------------------------------------------- #

def main() -> None:
    log.info("UnrealAI agent starting on http://%s:%d", AGENT_HOST, AGENT_PORT)
    log.info("Ollama URL     : %s", OLLAMA_URL)
    log.info("Default model  : %s", DEFAULT_MODEL)
    log.info("Skill root     : %s", SKILL_ROOT)
    if OPENROUTER_API_KEY:
        log.info("OpenRouter     : configured (%d models)", len(OPENROUTER_MODELS))
    else:
        log.info("OpenRouter     : not configured (no API key)")
    uvicorn.run(app, host=AGENT_HOST, port=AGENT_PORT, log_level="info")


if __name__ == "__main__":
    main()
