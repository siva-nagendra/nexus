"""Integration tests for the Niagara VFX subsystem (namespace: niagara)."""

from __future__ import annotations

import pytest

pytestmark = pytest.mark.integration


class TestNiagaraVFX:
    """Create systems, emitters, set parameters, spawn effects."""

    async def test_create_niagara_system(self, transport):
        """Create a Niagara system from a template."""
        result = await transport.execute(
            "niagara.create_niagara_system",
            {
                "system_name": "NS_IntegTest",
                "destination_folder": "/Game/IntegTest/VFX",
                "template": "Fountain",
            },
        )
        assert result.success

    async def test_create_emitter(self, transport):
        """Create a sprite emitter asset."""

    async def test_spawn_at_location(self, transport):
        """Spawn a Niagara system at a world location."""

    async def test_list_modules(self, transport):
        """List available Niagara modules by category."""
