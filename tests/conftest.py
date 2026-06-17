"""Shared test fixtures for Nexus."""

from __future__ import annotations

from typing import Any

import pytest

from nexus.connection.manager import ConnectionManager
from nexus.models.commands import CommandResult


class MockConnectionManager(ConnectionManager):
    """Mock ConnectionManager that doesn't connect to UE."""

    def __init__(self) -> None:
        super().__init__()
        self._mock_responses: dict[str, CommandResult] = {}

    async def initialize(self) -> None:
        """No-op for tests."""

    async def shutdown(self) -> None:
        """No-op for tests."""

    def set_response(self, command_type: str, result: CommandResult) -> None:
        """Pre-configure a response for a command type."""
        self._mock_responses[command_type] = result

    async def execute(
        self,
        command_type: str,
        params: dict[str, Any] | None = None,
        timeout: float | None = None,
    ) -> CommandResult:
        """Return pre-configured response or a default success."""
        if command_type in self._mock_responses:
            return self._mock_responses[command_type]
        return CommandResult(
            success=True,
            data={"mock": True, "command": command_type, "params": params or {}},
            transport_used="mock",
        )


@pytest.fixture
def mock_conn() -> MockConnectionManager:
    """Provide a mock ConnectionManager for unit tests."""
    return MockConnectionManager()


@pytest.fixture
def success_result() -> CommandResult:
    """A generic success CommandResult."""
    return CommandResult(success=True, data={"ok": True}, transport_used="mock")


@pytest.fixture
def error_result() -> CommandResult:
    """A generic error CommandResult."""
    return CommandResult(success=False, error="Test error", transport_used="mock")
