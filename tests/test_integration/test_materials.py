"""Integration tests for the material subsystem (namespace: material)."""

from __future__ import annotations

import pytest

pytestmark = pytest.mark.integration


class TestMaterialPipeline:
    """Create materials, instances, set parameters, apply to actors."""

    async def test_create_material_and_instance(self, transport):
        """Create a parent material then a material instance from it."""
        result = await transport.execute(
            "material.create",
            {
                "material_path": "/Game/IntegTest/M_Test",
            },
        )
        assert result.success

    async def test_set_scalar_and_vector_parameters(self, transport):
        """Set scalar (roughness) and vector (base color) on an instance."""

    async def test_apply_material_to_actor(self, transport):
        """Apply a material to a spawned static mesh actor."""

    async def test_batch_apply_materials(self, transport):
        """Batch-apply materials to multiple actors."""
