"""Integration tests for the networking subsystem (namespace: networking)."""

from __future__ import annotations

import pytest

pytestmark = pytest.mark.integration


class TestNetworkReplication:
    """Replication, RPCs, relevancy, net roles."""

    async def test_set_replication(self, transport):
        """Enable replication and movement replication on an actor."""

    async def test_set_net_role(self, transport):
        """Set the network role of an actor."""

    async def test_get_replication_info(self, transport):
        """Read replication state and net role of an actor."""

    async def test_set_net_relevancy(self, transport):
        """Configure net cull distance and relevancy settings."""
