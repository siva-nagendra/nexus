"""Wire protocol definitions for the native TCP transport.

Length-prefixed JSON over TCP (port 13377).
Frame: [4-byte big-endian uint32 length][JSON payload]
Request:  {"id": "uuid", "type": "actor.spawn", "params": {...}}
Response: {"id": "uuid", "success": true, "data": {...}, "error": null}
"""

from __future__ import annotations

import struct
import uuid
from typing import Any

import orjson

# Default native transport port (matches kvick-games convention)
DEFAULT_NATIVE_PORT = 13377

# TCP socket configuration
TCP_BUFFER_SIZE = 65536  # 64KB send/receive buffers
TCP_RECV_CHUNK = 65536  # 64KB per recv() call (up from 8KB)
TCP_NODELAY = True
TCP_KEEPALIVE = True

# Length prefix: 4-byte big-endian unsigned int
LENGTH_PREFIX_FORMAT = "!I"
LENGTH_PREFIX_SIZE = struct.calcsize(LENGTH_PREFIX_FORMAT)

# Retry configuration
MAX_RETRIES = 3
BASE_RETRY_DELAY = 0.5  # seconds
MAX_RETRY_DELAY = 5.0  # seconds


def encode_command(command_type: str, params: dict[str, Any] | None = None) -> tuple[str, bytes]:
    """Encode a command into length-prefixed wire format.

    Returns:
        Tuple of (command_id, length_prefixed_bytes).
    """
    cmd_id = str(uuid.uuid4())
    envelope = {
        "id": cmd_id,
        "type": command_type,
        "params": params or {},
    }
    payload = orjson.dumps(envelope)
    frame = struct.pack(LENGTH_PREFIX_FORMAT, len(payload)) + payload
    return cmd_id, frame


def decode_response(raw: bytes) -> dict[str, Any]:
    """Decode a JSON payload (already stripped of length prefix).

    Returns:
        Dict with keys: id, success, data, error.
    """
    return orjson.loads(raw)


def encode_length_prefix(payload: bytes) -> bytes:
    """Prepend a 4-byte big-endian length prefix to a payload."""
    return struct.pack(LENGTH_PREFIX_FORMAT, len(payload)) + payload


def decode_length_prefix(header: bytes) -> int:
    """Decode a 4-byte big-endian length prefix."""
    return struct.unpack(LENGTH_PREFIX_FORMAT, header)[0]
