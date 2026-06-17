"""Integration tests for the AI subsystem (namespace: ai)."""

from __future__ import annotations

import pytest

pytestmark = pytest.mark.integration


class TestAIBehavior:
    """Behavior trees, blackboards, EQS, perception."""

    async def test_create_behavior_tree(self, transport):
        """Create a behavior tree asset."""
        result = await transport.execute(
            "ai.create_behavior_tree",
            {
                "tree_name": "BT_IntegTest",
                "destination_folder": "/Game/IntegTest/AI",
            },
        )
        assert result.success

    async def test_create_blackboard(self, transport):
        """Create a blackboard with typed keys."""

    async def test_create_eqs_query(self, transport):
        """Create an EQS query with a grid generator."""

    async def test_list_behavior_trees(self, transport):
        """List all behavior tree assets in the project."""
