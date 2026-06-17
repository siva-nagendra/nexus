"""Response formatting helpers for MCP tool returns.

Compact JSON serialization reduces token overhead by 30-40% compared to
indented output, which matters when large actor lists or property dumps
flow through the LLM context window.
"""

from __future__ import annotations

import json
from typing import Any


def json_response(data: Any, compact: bool = True) -> str:
    """Serialize data to JSON, using compact separators by default."""
    if compact:
        return json.dumps(data, separators=(",", ":"), default=str)
    return json.dumps(data, indent=2, default=str)


def compact_response(data: dict[str, Any]) -> dict[str, Any]:
    """Strip None values, empty collections, and redundant success flags.

    Dropping these sentinels keeps the payload small without losing
    information the LLM actually needs to reason about.
    """
    return {
        key: value
        for key, value in data.items()
        if value is not None
        and value != []
        and value != ""
        and not (key == "success" and value is True)
    }


def error_response(
    message: str,
    error_type: str = "Error",
    recoverable: bool = False,
    suggestion: str = "",
    category: str = "unknown",
) -> str:
    """Build a structured error response as compact JSON."""
    payload: dict[str, object] = {
        "error": message,
        "error_type": error_type,
        "recoverable": recoverable,
    }
    if suggestion:
        payload["suggestion"] = suggestion
    if category != "unknown":
        payload["category"] = category
    return json_response(payload)
