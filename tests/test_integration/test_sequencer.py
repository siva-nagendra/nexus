"""Integration tests for the sequencer subsystem (namespace: sequencer)."""

from __future__ import annotations

import pytest

pytestmark = pytest.mark.integration


class TestSequencerCinematics:
    """Create sequences, add tracks, keyframe, camera cuts."""

    async def test_create_and_open_sequence(self, transport):
        """Create a level sequence and open it in the Sequencer editor."""
        result = await transport.execute(
            "sequencer.create_sequence",
            {
                "sequence_name": "LS_IntegTest",
                "destination_folder": "/Game/IntegTest",
            },
        )
        assert result.success

    async def test_add_actor_track_and_keyframe(self, transport):
        """Add a transform track for an actor and set keyframes."""

    async def test_add_camera_cut(self, transport):
        """Add a camera cut section to a sequence."""

    async def test_set_sequence_range(self, transport):
        """Adjust playback range and frame rate."""
