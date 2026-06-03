"""T3D pre-flight validator for Blueprint graph paste.

Phase 5 (Workstream A1) — catches the top model mistakes BEFORE the bytes hit
the editor, returning a structured error list the model can self-correct from.

The output of `parse_t3d` is also reused by the post-paste verifier
(Workstream A2) to detect unresolved LinkedTo references.
"""

from __future__ import annotations

import re
from dataclasses import dataclass, field
from typing import Dict, List, Set, Tuple

# --------------------------------------------------------------------------- #
# Regexes
# --------------------------------------------------------------------------- #

_BEGIN_OBJECT_RE = re.compile(
    r'^\s*Begin\s+Object\s+Class=([^\s]+)\s+Name="([^"]+)"',
    re.IGNORECASE,
)
_END_OBJECT_RE = re.compile(r'^\s*End\s+Object\s*$', re.IGNORECASE)
_NODE_GUID_RE = re.compile(r'^\s*NodeGuid=([0-9A-Fa-f]+)\s*$')
_PIN_RE = re.compile(r'^\s*CustomProperties\s+Pin\s+\((.*)\)\s*$', re.IGNORECASE)
_PIN_ID_RE = re.compile(r'PinId=([0-9A-Fa-f]+)')
_PIN_NAME_RE = re.compile(r'PinName="([^"]+)"')
# LinkedTo=(NodeName PinId,NodeName PinId,...)
_LINKED_TO_RE = re.compile(r'LinkedTo=\(([^)]*)\)')
_LINK_PAIR_RE = re.compile(r'([A-Za-z_][\w]*)\s+([0-9A-Fa-f]+)')

# --------------------------------------------------------------------------- #
# Data shapes
# --------------------------------------------------------------------------- #


@dataclass
class PinInfo:
    pin_id: str
    pin_name: str
    line_no: int


@dataclass
class NodeInfo:
    name: str                       # e.g. "K2Node_CallFunction_0"
    klass: str                      # e.g. "/Script/BlueprintGraph.K2Node_CallFunction"
    node_guid: str = ""
    pins: Dict[str, PinInfo] = field(default_factory=dict)   # pin_id -> PinInfo
    line_no: int = 0


@dataclass
class LinkRef:
    src_node: str
    src_pin: str
    target_node: str
    target_pin: str
    line_no: int


@dataclass
class ParsedT3D:
    nodes: Dict[str, NodeInfo] = field(default_factory=dict)   # node_name -> NodeInfo
    links: List[LinkRef] = field(default_factory=list)
    begin_count: int = 0
    end_count: int = 0


# --------------------------------------------------------------------------- #
# Parser (forgiving — never raises; collects what it can)
# --------------------------------------------------------------------------- #


def parse_t3d(text: str) -> ParsedT3D:
    """Tokenize T3D into nodes, pins, and link references. Never raises."""
    parsed = ParsedT3D()
    current: NodeInfo | None = None

    for idx, raw_line in enumerate(text.splitlines(), start=1):
        if m := _BEGIN_OBJECT_RE.match(raw_line):
            parsed.begin_count += 1
            klass, name = m.group(1), m.group(2)
            current = NodeInfo(name=name, klass=klass, line_no=idx)
            # Note: duplicate names get overwritten in the dict but counted via
            # _detect_duplicates below using begin_count vs len(nodes).
            parsed.nodes[name] = current
            continue

        if _END_OBJECT_RE.match(raw_line):
            parsed.end_count += 1
            current = None
            continue

        if current is None:
            continue

        if m := _NODE_GUID_RE.match(raw_line):
            current.node_guid = m.group(1)
            continue

        if m := _PIN_RE.match(raw_line):
            pin_body = m.group(1)
            pid_m = _PIN_ID_RE.search(pin_body)
            pname_m = _PIN_NAME_RE.search(pin_body)
            pin_id = pid_m.group(1) if pid_m else ""
            pin_name = pname_m.group(1) if pname_m else ""
            if pin_id:
                current.pins[pin_id] = PinInfo(pin_id, pin_name, idx)

            # Extract LinkedTo refs (zero or more allowed per pin block).
            for lt in _LINKED_TO_RE.findall(pin_body):
                for pair in _LINK_PAIR_RE.findall(lt):
                    parsed.links.append(LinkRef(
                        src_node=current.name,
                        src_pin=pin_id,
                        target_node=pair[0],
                        target_pin=pair[1],
                        line_no=idx,
                    ))
            continue

    return parsed


# --------------------------------------------------------------------------- #
# Validator
# --------------------------------------------------------------------------- #


def _err(code: str, message: str, **extra) -> Dict[str, object]:
    out: Dict[str, object] = {"code": code, "message": message}
    out.update(extra)
    return out


def validate_t3d(text: str) -> Tuple[ParsedT3D, List[Dict[str, object]]]:
    """Return (parsed_index, errors). Empty error list = OK to paste."""
    errors: List[Dict[str, object]] = []

    if not text or not text.strip():
        errors.append(_err("EMPTY", "t3d_text is empty"))
        return ParsedT3D(), errors

    if "Begin Object" not in text:
        errors.append(_err(
            "NO_BEGIN_OBJECT",
            "t3d_text contains no 'Begin Object' lines. Each node must be wrapped in 'Begin Object Class=... Name=\"...\"' / 'End Object'.",
        ))
        return ParsedT3D(), errors

    parsed = parse_t3d(text)

    # 1. Begin/End balance.
    if parsed.begin_count != parsed.end_count:
        errors.append(_err(
            "UNBALANCED_OBJECTS",
            f"'Begin Object' count ({parsed.begin_count}) does not match 'End Object' count ({parsed.end_count}).",
            begin_count=parsed.begin_count,
            end_count=parsed.end_count,
        ))

    # 2. Duplicate node Name (each Name="..." must be unique within a paste).
    if parsed.begin_count != len(parsed.nodes):
        # parse_t3d uses dict keyed by name, so a clash silently overwrites;
        # the begin_count mismatch is the only signal we have here.
        errors.append(_err(
            "DUPLICATE_NODE_NAME",
            f"Detected {parsed.begin_count} 'Begin Object' blocks but only {len(parsed.nodes)} unique Names. Each Name=\"...\" must be unique.",
        ))

    # 3. Duplicate NodeGuid.
    seen_guids: Set[str] = set()
    for node in parsed.nodes.values():
        if not node.node_guid:
            errors.append(_err(
                "MISSING_NODE_GUID",
                f"Node '{node.name}' is missing a 'NodeGuid=' line.",
                node=node.name,
            ))
            continue
        if node.node_guid in seen_guids:
            errors.append(_err(
                "DUPLICATE_NODE_GUID",
                f"Node '{node.name}' reuses NodeGuid '{node.node_guid}'. Every node needs a fresh 32-char hex GUID.",
                node=node.name,
                node_guid=node.node_guid,
            ))
        seen_guids.add(node.node_guid)

    # 4. Duplicate PinId (global; UE requires this for the importer to bind links).
    seen_pins: Dict[str, str] = {}   # pin_id -> "node.pin_name"
    for node in parsed.nodes.values():
        for pin in node.pins.values():
            key = f"{node.name}.{pin.pin_name}"
            if pin.pin_id in seen_pins:
                errors.append(_err(
                    "DUPLICATE_PIN_ID",
                    f"PinId '{pin.pin_id}' is used by both '{seen_pins[pin.pin_id]}' and '{key}'. Every pin needs a fresh 32-char hex PinId.",
                    pin_id=pin.pin_id,
                    locations=[seen_pins[pin.pin_id], key],
                ))
            else:
                seen_pins[pin.pin_id] = key

    # 5. Resolve LinkedTo references against the parsed index.
    for link in parsed.links:
        target = parsed.nodes.get(link.target_node)
        if target is None:
            errors.append(_err(
                "UNRESOLVED_LINK_NODE",
                f"LinkedTo on '{link.src_node}' (line {link.line_no}) targets unknown node '{link.target_node}'.",
                src_node=link.src_node,
                target_node=link.target_node,
                line=link.line_no,
            ))
            continue
        if link.target_pin not in target.pins:
            errors.append(_err(
                "UNRESOLVED_LINK_PIN",
                f"LinkedTo on '{link.src_node}' (line {link.line_no}) targets pin '{link.target_pin}' on '{link.target_node}', but that node has no such PinId.",
                src_node=link.src_node,
                target_node=link.target_node,
                target_pin=link.target_pin,
                line=link.line_no,
            ))

    return parsed, errors


# --------------------------------------------------------------------------- #
# Index helpers used by the post-paste verifier (A2)
# --------------------------------------------------------------------------- #


def expected_node_names(parsed: ParsedT3D) -> List[str]:
    return list(parsed.nodes.keys())


def link_summary(parsed: ParsedT3D) -> List[Dict[str, str]]:
    return [
        {
            "src_node": l.src_node,
            "src_pin": l.src_pin,
            "target_node": l.target_node,
            "target_pin": l.target_pin,
        }
        for l in parsed.links
    ]
