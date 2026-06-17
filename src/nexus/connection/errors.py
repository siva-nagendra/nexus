"""Transport error classification for retry logic.

TransientError: retry-able (timeout, connection reset, handler crash).
PermanentError: not retry-able (invalid params, not found, parse error).
"""

from __future__ import annotations


class TransientError(Exception):
    """Retry-able transport or handler error.

    Examples: timeout, connection reset, TCP error, handler crash,
    missing world (editor not fully loaded yet).
    """


class PermanentError(Exception):
    """Not retry-able application error.

    Examples: missing parameter, actor/asset not found, invalid value,
    unknown command, parse error.
    """


# Error codes returned by the C++ plugin, classified by retry-ability.
TRANSIENT_CODES: frozenset[str] = frozenset(
    {
        "TIMEOUT",
        "HANDLER_CRASH",
        "NO_WORLD",
    }
)

PERMANENT_CODES: frozenset[str] = frozenset(
    {
        "MISSING_PARAM",
        "ACTOR_NOT_FOUND",
        "ASSET_NOT_FOUND",
        "CLASS_NOT_FOUND",
        "PROPERTY_NOT_FOUND",
        "INVALID_VALUE",
        "NOT_IMPLEMENTED",
        "PARSE_ERROR",
        "UNKNOWN_COMMAND",
    }
)


def classify_error_code(code: str) -> type[TransientError] | type[PermanentError] | None:
    """Classify an error code from the C++ plugin.

    Returns TransientError or PermanentError class, or None if unrecognized.
    """
    if code in TRANSIENT_CODES:
        return TransientError
    if code in PERMANENT_CODES:
        return PermanentError
    return None
