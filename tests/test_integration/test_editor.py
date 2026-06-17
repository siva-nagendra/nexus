"""Integration tests for the editor subsystem (namespace: editor)."""

from __future__ import annotations

import pytest

pytestmark = pytest.mark.integration


class TestEditorControls:
    """Viewport, selection, PIE, and console commands."""

    async def test_get_viewport_info(self, transport):
        """Read current viewport camera position and settings."""
        result = await transport.execute("editor.get_viewport_info", {})
        assert result.success

    async def test_set_viewport_camera(self, transport):
        """Move the viewport camera to a specific location."""

    async def test_selection_round_trip(self, transport):
        """Select actors, read selection back, then clear."""

    async def test_start_and_stop_pie(self, transport):
        """Start a PIE session, verify it's running, then stop it."""

    async def test_execute_console_command(self, transport):
        """Execute a benign console command like 'stat fps'."""
