"""Integration tests for the source control subsystem (namespace: sourcecontrol)."""

from __future__ import annotations

import pytest

pytestmark = pytest.mark.integration


class TestSourceControl:
    """File status, checkout, add, revert, submit, history."""

    async def test_get_status(self, transport):
        """Get source control status for the project Content directory."""
        result = await transport.execute(
            "sourcecontrol.get_source_control_status",
            {
                "directory": "/Game",
            },
        )
        assert result.success

    async def test_checkout_and_revert(self, transport):
        """Check out a file then revert unchanged."""

    async def test_get_file_history(self, transport):
        """Get revision history for a known asset."""

    async def test_add_and_mark_for_delete(self, transport):
        """Mark a new file for add, then mark it for delete."""
