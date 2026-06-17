"""Authentication utilities for ClaudeShell relay.

Provides constant-time token validation to prevent timing attacks.
"""

from __future__ import annotations

import hmac


def validate_token(provided: str | None, expected: str | None) -> bool:
    """Compare two tokens in constant time to prevent timing attacks.

    Args:
        provided: Token provided by the client (from header or query param).
        expected: Token the server expects.

    Returns:
        True if the tokens match, False otherwise (including None/empty).
    """
    if not provided or not expected:
        return False
    return hmac.compare_digest(provided, expected)
