"""Integration tests for the animation subsystem (namespace: animation)."""

from __future__ import annotations

import pytest

pytestmark = pytest.mark.integration


class TestAnimationWorkflow:
    """Skeleton inspection, montage creation, blend space, IK setup."""

    async def test_list_anim_sequences(self, transport):
        """List animation sequences in the project."""
        result = await transport.execute("animation.list_anim_sequences", {})
        assert result.success

    async def test_get_skeleton_info(self, transport):
        """Get bone hierarchy and sockets from a skeleton asset."""

    async def test_create_anim_montage(self, transport):
        """Create a montage from an existing anim sequence."""

    async def test_create_blend_space(self, transport):
        """Create a 1D or 2D blend space for locomotion."""
