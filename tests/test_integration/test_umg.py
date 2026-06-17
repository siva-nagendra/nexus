"""Integration tests for the UMG UI subsystem (namespace: umg)."""

from __future__ import annotations

import pytest

pytestmark = pytest.mark.integration


class TestUMGWidgets:
    """Create widgets, set properties, manage viewport display."""

    async def test_create_widget_blueprint(self, transport):
        """Create a widget blueprint from a template."""
        result = await transport.execute(
            "umg.create_widget_blueprint",
            {
                "widget_name": "WBP_IntegTest",
                "destination_folder": "/Game/IntegTest/UI",
            },
        )
        assert result.success

    async def test_add_and_remove_from_viewport(self, transport):
        """Add a widget to the viewport then remove it."""

    async def test_set_widget_property(self, transport):
        """Set text and visibility on a child widget."""

    async def test_create_widget_animation(self, transport):
        """Create a fade-in animation on a widget element."""
