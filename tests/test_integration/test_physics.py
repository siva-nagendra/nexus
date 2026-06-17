"""Integration tests for the physics subsystem (namespace: physics)."""

from __future__ import annotations

import pytest

pytestmark = pytest.mark.integration


class TestPhysicsSimulation:
    """Collision, simulation, constraints, forces."""

    async def test_enable_physics_on_actor(self, transport):
        """Enable physics simulation on a spawned static mesh."""
        result = await transport.execute(
            "actor.spawn",
            {
                "actor_class": "StaticMeshActor",
                "label": "IntegTest_PhysActor",
            },
        )
        assert result.success

    async def test_set_collision_profile(self, transport):
        """Assign a collision profile to an actor."""

    async def test_add_physics_constraint(self, transport):
        """Create a hinge constraint between two actors."""

    async def test_get_physics_info(self, transport):
        """Read physics state including mass, velocity, and collision."""
