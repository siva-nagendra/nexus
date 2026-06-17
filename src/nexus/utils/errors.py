"""Tool-level error classification with recovery hints.

Maps Python exceptions to structured responses that include error type,
recoverability, and domain-specific suggestions so the LLM can self-correct.

This complements connection/errors.py which handles transport-level
classification (TransientError/PermanentError for retry logic). This module
operates one layer up: once a tool catches an exception, it classifies it
into a structured dict the LLM can reason about.
"""

from __future__ import annotations


def classify_tool_error(exc: Exception) -> dict[str, object]:
    """Map a tool exception to a structured error response.

    Returns a dict with: error, error_type, recoverable, suggestion, category.
    """
    message = str(exc)

    if isinstance(exc, ConnectionError):
        return _error_dict(
            message,
            error_type="ConnectionError",
            recoverable=True,
            suggestion=(
                "Cannot reach the Nexus C++ plugin. "
                "Is Unreal Engine running with the Nexus plugin on port 13377?"
            ),
            category="connection",
        )

    if isinstance(exc, TimeoutError):
        return _error_dict(
            message,
            error_type="Timeout",
            recoverable=True,
            suggestion=(
                "Operation timed out. The editor may be busy. Retry or simplify the request."
            ),
            category="timeout",
        )

    if isinstance(exc, ValueError):
        return _error_dict(
            message,
            error_type="ValueError",
            recoverable=True,
            suggestion=(
                "A parameter has an invalid value. Common issues: "
                "wrong asset path format, invalid actor class, "
                "out-of-range coordinates."
            ),
            category="validation",
        )

    if isinstance(exc, TypeError):
        return _error_dict(
            message,
            error_type="TypeMismatch",
            recoverable=True,
            suggestion=(
                "A parameter has the wrong type. Common issues: "
                "location expects float not str, tags expects list[str] not str, "
                "properties expects dict not list."
            ),
            category="type_mismatch",
        )

    if isinstance(exc, NameError):
        return _error_dict(
            message,
            error_type="NotFound",
            recoverable=True,
            suggestion=(
                "The requested resource was not found. Use find_actors or "
                "search_assets to discover valid paths."
            ),
            category="not_found",
        )

    if isinstance(exc, PermissionError):
        return _error_dict(
            message,
            error_type="PermissionDenied",
            recoverable=False,
            suggestion="The operation was denied. The asset or actor may be locked or read-only.",
            category="permission",
        )

    if isinstance(exc, RuntimeError):
        return _error_dict(
            message,
            error_type="RuntimeError",
            recoverable=False,
            suggestion="An unexpected error occurred in the Unreal Engine plugin.",
            category="runtime",
        )

    # Fallback for unknown exception types
    return _error_dict(
        message,
        error_type=type(exc).__name__,
        recoverable=False,
        suggestion="An unexpected error occurred.",
        category="unknown",
    )


def _error_dict(
    message: str,
    error_type: str,
    recoverable: bool,
    suggestion: str,
    category: str,
) -> dict[str, object]:
    """Build the canonical error dict shape returned by classify_tool_error."""
    return {
        "error": message,
        "error_type": error_type,
        "recoverable": recoverable,
        "suggestion": suggestion,
        "category": category,
    }
