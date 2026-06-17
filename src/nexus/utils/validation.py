"""Parameter validation helpers for multi-action tools.

Multi-action tools (manage_material, manage_anim_blueprint, etc.) accept
an `action` string that selects the sub-operation. These helpers validate
that value and return helpful error messages listing valid choices.
"""

from __future__ import annotations

from nexus.utils.responses import json_response


def validate_action(
    action: str,
    valid_actions: list[str],
    tool_name: str,
) -> str | None:
    """Validate an action parameter against a list of valid values.

    Returns None if the action is valid, or a JSON error string
    with the list of valid actions for the LLM to self-correct.
    """
    if not action or not action.strip():
        return json_response(
            {
                "error": f"The 'action' parameter is required for {tool_name}",
                "valid_actions": valid_actions,
            }
        )

    normalized = action.lower().strip()
    if normalized not in valid_actions:
        return json_response(
            {
                "error": f"Unknown action '{action}' for {tool_name}",
                "valid_actions": valid_actions,
            }
        )

    return None
