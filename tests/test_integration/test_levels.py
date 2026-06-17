"""Integration tests for the level subsystem (namespace: level)."""

from __future__ import annotations

import pytest

pytestmark = pytest.mark.integration


class TestLevelOperations:
    """Load, save, create, and manage sub-levels."""

    async def test_get_current_level(self, transport):
        """Get information about the currently loaded persistent level."""
        result = await transport.execute("level.get_current_level", {})
        assert result.success

    async def test_create_and_save_level(self, transport):
        """Create a new empty level and save it to disk."""

    async def test_sublevel_management(self, transport):
        """Add and remove a streaming sub-level."""

    async def test_get_level_bounds(self, transport):
        """Get the axis-aligned bounding box of the current level."""
