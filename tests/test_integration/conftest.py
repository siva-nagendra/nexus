"""Shared fixtures for integration tests.

All tests in this directory require a running Unreal Engine instance
with the Nexus plugin active. They are excluded from CI by default
via the ``integration`` marker.
"""

from __future__ import annotations

import asyncio

import pytest

from nexus.connection.native import NativeTransport


@pytest.fixture(scope="session")
def event_loop():
    """Session-scoped event loop for integration tests."""
    loop = asyncio.new_event_loop()
    yield loop
    loop.close()


@pytest.fixture(scope="session")
async def transport(event_loop):
    """Persistent NativeTransport connected to a live UE instance."""
    conn = NativeTransport()
    await conn.connect()
    yield conn
    await conn.disconnect()
