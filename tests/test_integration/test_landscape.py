"""Integration tests for the landscape subsystem (namespace: landscape)."""

from __future__ import annotations

import pytest

pytestmark = pytest.mark.integration


class TestLandscapeTerrain:
    """Create landscapes, sculpt, paint layers, manage foliage."""

    async def test_create_landscape(self, transport):
        """Create a landscape actor with default grid dimensions."""

    async def test_sculpt_landscape(self, transport):
        """Apply a sculpt brush stroke to raise terrain."""

    async def test_add_and_paint_foliage(self, transport):
        """Register a foliage type and paint instances."""

    async def test_get_landscape_info(self, transport):
        """Read landscape grid dimensions, layers, and foliage types."""
