"""Integration tests for the enhanced input subsystem (namespace: input)."""

from __future__ import annotations

import pytest

pytestmark = pytest.mark.integration


class TestEnhancedInput:
    """Create input actions, mapping contexts, bind keys."""

    async def test_create_input_action(self, transport):
        """Create a Bool input action for jumping."""
        result = await transport.execute(
            "input.create_input_action",
            {
                "action_name": "IA_IntegTest",
                "value_type": "Bool",
            },
        )
        assert result.success

    async def test_create_mapping_context(self, transport):
        """Create a mapping context and add an action mapping."""

    async def test_set_trigger_and_modifier(self, transport):
        """Configure a Pressed trigger and Negate modifier on a binding."""

    async def test_list_input_actions(self, transport):
        """List all input action assets in the project."""
