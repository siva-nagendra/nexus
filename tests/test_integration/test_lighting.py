"""Integration tests for the lighting subsystem (namespace: lighting)."""

from __future__ import annotations

import pytest

pytestmark = pytest.mark.integration


class TestLightingSetup:
    """Spawn lights, configure atmosphere, fog, GI, shadows."""

    async def test_spawn_point_light(self, transport):
        """Spawn a point light and verify it exists in the level."""
        result = await transport.execute(
            "lighting.spawn_light",
            {
                "light_type": "PointLight",
                "label": "IntegTest_Light",
                "intensity": 5000,
            },
        )
        assert result.success

    async def test_set_light_properties(self, transport):
        """Modify intensity, color, and attenuation of a spawned light."""

    async def test_get_lighting_info(self, transport):
        """Get a level-wide lighting overview including GI settings."""

    async def test_configure_global_illumination(self, transport):
        """Switch between Lumen and Screen Space GI methods."""
