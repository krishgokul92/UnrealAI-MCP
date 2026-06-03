"""Loader for the T3D snippet library (Phase 5 / A4).

Responsibilities:
  * Enumerate snippets defined in `t3d_snippets/__init__.py`.
  * Read snippet T3D from disk on demand.
  * Rewrite every 32-char hex token (NodeGuid + PinId values, including the
    duplicates inside LinkedTo refs) with a fresh GUID, so consecutive calls
    return paste-safe non-colliding text. LinkedTo references stay consistent
    because they reuse the exact same hex strings.
"""

from __future__ import annotations

import re
import uuid
from pathlib import Path
from typing import Dict, List

from t3d_snippets import SNIPPETS

_SNIPPET_DIR = Path(__file__).parent / "t3d_snippets"
_HEX32_RE = re.compile(r"\b([0-9A-Fa-f]{32})\b")
# Treat the all-zeros GUID as a sentinel (PersistentGuid / unset references) —
# do NOT rewrite it.
_ALL_ZERO = "0" * 32


def list_snippets() -> List[Dict[str, object]]:
    """Return manifest entries (no T3D body) for the model to browse."""
    return [
        {
            "name": s["name"],
            "description": s["description"],
            "exec_in_pin": s["exec_in_pin"],
            "exec_out_pins": s["exec_out_pins"],
            "data_pins": s["data_pins"],
            "tags": s["tags"],
        }
        for s in SNIPPETS
    ]


def get_snippet(name: str, fresh_guids: bool = True) -> Dict[str, object]:
    """Return one snippet, optionally with fresh GUIDs.

    Args:
        name: snippet name from `list_snippets`.
        fresh_guids: if True (default) rewrite every 32-char hex GUID with a
            fresh value so the result is paste-safe. Set False to inspect the
            canonical template form.

    Returns: dict with keys `name`, `description`, `t3d_text`,
    `node_name` (the importer-visible name for LinkedTo refs),
    `pin_names` (sorted list).
    """
    entry = next((s for s in SNIPPETS if s["name"] == name), None)
    if entry is None:
        return {"error": f"Unknown snippet '{name}'. Call list_t3d_snippets to enumerate."}

    path = _SNIPPET_DIR / entry["file"]
    if not path.exists():
        return {"error": f"Snippet file missing on disk: {path.name}"}

    text = path.read_text(encoding="utf-8")

    if fresh_guids:
        text = _rewrite_guids(text)

    # Pull the importer-visible Name="..." for the model's reference.
    name_match = re.search(r'^\s*Begin\s+Object\s+Class=\S+\s+Name="([^"]+)"', text, re.MULTILINE)
    pin_names = re.findall(r'PinName="([^"]+)"', text)

    return {
        "name": entry["name"],
        "description": entry["description"],
        "exec_in_pin": entry["exec_in_pin"],
        "exec_out_pins": entry["exec_out_pins"],
        "data_pins": entry["data_pins"],
        "node_name": name_match.group(1) if name_match else None,
        "pin_names": pin_names,
        "t3d_text": text,
        "hint": (
            "Paste this T3D into a Blueprint via `paste_blueprint_graph`. "
            "If composing multiple snippets in one paste, request each with a "
            "separate `get_t3d_snippet` call (each call returns fresh GUIDs)."
        ),
    }


def _rewrite_guids(text: str) -> str:
    """Replace every distinct 32-char hex token with a fresh GUID.

    Distinct-by-source — meaning if the same token appears N times (NodeGuid +
    LinkedTo references), all N occurrences become the same fresh GUID. This
    preserves LinkedTo bindings.
    """
    seen: Dict[str, str] = {}

    def sub(m: re.Match) -> str:
        original = m.group(1)
        if original == _ALL_ZERO:
            return original
        if original not in seen:
            seen[original] = uuid.uuid4().hex.upper()
        return seen[original]

    return _HEX32_RE.sub(sub, text)
