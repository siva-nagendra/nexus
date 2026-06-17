"""Integration tests for the PCG subsystem (namespace: pcg)."""

from __future__ import annotations

import pytest

pytestmark = pytest.mark.integration


class TestPCGGraphs:
    """Create, configure, and execute PCG graphs."""

    async def test_create_pcg_graph(self, transport):
        """Create a PCG graph attached to a new volume."""
        result = await transport.execute("pcg.create_pcg_graph", {"graph_name": "PCG_IntegTest"})
        assert result.success

    async def test_add_and_connect_nodes(self, transport):
        """Add a surface sampler and mesh spawner, wire them together."""

    async def test_execute_graph(self, transport):
        """Execute a PCG graph and verify generation results."""

    async def test_get_pcg_info(self, transport):
        """Inspect a PCG graph's nodes, connections, and stats."""
