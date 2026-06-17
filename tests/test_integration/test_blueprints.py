"""Integration tests for the blueprint subsystem (namespace: blueprint)."""

from __future__ import annotations

import pytest

pytestmark = pytest.mark.integration


class TestBlueprintWorkflow:
    """Create, modify, compile, and inspect Blueprints."""

    async def test_create_and_compile_blueprint(self, transport):
        """Create a new Blueprint, add a variable, compile it."""
        result = await transport.execute(
            "blueprint.create",
            {
                "blueprint_path": "/Game/IntegTest/BP_Test",
                "parent_class": "Actor",
            },
        )
        assert result.success

    async def test_add_variable_and_function(self, transport):
        """Add a variable and a function to a Blueprint."""

    async def test_add_component(self, transport):
        """Add a StaticMeshComponent to a Blueprint."""

    async def test_add_node_and_connect_pins(self, transport):
        """Add a PrintString node to the EventGraph and wire it."""

    async def test_get_blueprint_info(self, transport):
        """Inspect a Blueprint's variables, functions, and graphs."""
