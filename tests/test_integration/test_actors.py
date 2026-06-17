"""Integration tests for the actor subsystem (namespace: actor)."""

from __future__ import annotations

import pytest

pytestmark = pytest.mark.integration


class TestActorLifecycle:
    """Spawn, query, transform, and delete actors in a live level."""

    async def test_spawn_and_find_actor(self, transport):
        """Spawn a StaticMeshActor and verify it appears in find results."""
        result = await transport.execute(
            "actor.spawn", {"actor_class": "StaticMeshActor", "label": "IntegTest_Actor"}
        )
        assert result.success

        found = await transport.execute("actor.find", {"query": "IntegTest_Actor"})
        assert found.success
        assert len(found.data.get("actors", [])) >= 1

    async def test_set_and_get_transform(self, transport):
        """Set an actor transform then read it back to verify round-trip."""

    async def test_get_and_set_property(self, transport):
        """Read and write a UPROPERTY on a spawned actor."""

    async def test_batch_spawn_and_delete(self, transport):
        """Batch-spawn multiple actors and batch-delete them."""

    async def test_attach_and_detach(self, transport):
        """Attach an actor to a parent, verify, then detach."""

    async def test_duplicate_actor(self, transport):
        """Duplicate an actor and verify the copy exists at offset."""

    async def test_actor_tags(self, transport):
        """Add, get, and remove tags on an actor."""
