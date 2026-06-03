"""
UnrealAI TCP Bridge Client
==========================

Tiny socket client that talks to the UE plugin's C++ TCP bridge on port 55557.

Wire format:
    Client → server : one JSON message `{"type": "cmd_name", "params": {...}}`
    Server → client : one JSON response (no length prefix, no framing)

The server processes one command per connection and writes the reply back,
so we open a fresh socket per call.
"""

from __future__ import annotations

import json
import logging
import socket
from typing import Any, Dict

log = logging.getLogger("UnrealAI.Bridge")

DEFAULT_HOST = "127.0.0.1"
DEFAULT_PORT = 55557
DEFAULT_TIMEOUT = 30.0
MAX_RESPONSE_BYTES = 2 * 1024 * 1024  # 2 MB


class BridgeError(RuntimeError):
    """Raised when the UE TCP bridge cannot be reached or returns nothing."""


def send_command(
    command_type: str,
    params: Dict[str, Any] | None = None,
    *,
    host: str = DEFAULT_HOST,
    port: int = DEFAULT_PORT,
    timeout: float = DEFAULT_TIMEOUT,
) -> Dict[str, Any]:
    """Send one command to the UE plugin and return the parsed JSON response.

    Raises BridgeError on connection or decode failure.
    """
    payload = {"type": command_type, "params": params or {}}
    raw = json.dumps(payload).encode("utf-8")

    try:
        with socket.create_connection((host, port), timeout=timeout) as s:
            s.settimeout(timeout)
            s.sendall(raw)

            # Read until server closes or max size reached.
            chunks: list[bytes] = []
            received = 0
            while received < MAX_RESPONSE_BYTES:
                try:
                    chunk = s.recv(65536)
                except socket.timeout:
                    break
                if not chunk:
                    break
                chunks.append(chunk)
                received += len(chunk)
                # UE sends one JSON doc then keeps socket alive; try to parse
                # early so we don't wait for the full timeout on each call.
                try:
                    text = b"".join(chunks).decode("utf-8")
                    return json.loads(text)
                except (UnicodeDecodeError, json.JSONDecodeError):
                    continue  # need more bytes
    except (ConnectionRefusedError, OSError) as exc:
        raise BridgeError(
            f"Cannot reach UE bridge at {host}:{port}. "
            f"Open the UE project with the UnrealAI plugin loaded. ({exc})"
        ) from exc

    text = b"".join(chunks).decode("utf-8", errors="replace").strip()
    if not text:
        raise BridgeError("UE bridge returned an empty response.")
    try:
        return json.loads(text)
    except json.JSONDecodeError as exc:
        raise BridgeError(f"UE bridge returned invalid JSON: {text[:500]}") from exc


def ping(host: str = DEFAULT_HOST, port: int = DEFAULT_PORT) -> bool:
    """Return True if the UE bridge responds to a ping."""
    try:
        resp = send_command("ping", {}, host=host, port=port, timeout=3.0)
        return bool(resp)
    except BridgeError:
        return False
