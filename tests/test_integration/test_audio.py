"""Integration tests for the audio subsystem (namespace: audio)."""

from __future__ import annotations

import pytest

pytestmark = pytest.mark.integration


class TestAudioPipeline:
    """Spawn sounds, create cues, configure attenuation and reverb."""

    async def test_spawn_ambient_sound(self, transport):
        """Spawn an ambient sound actor at a location."""

    async def test_create_sound_cue(self, transport):
        """Create a Sound Cue from SoundWave assets."""

    async def test_set_attenuation(self, transport):
        """Configure attenuation radius and falloff on a sound actor."""

    async def test_get_audio_info(self, transport):
        """Get level-wide audio overview or specific actor audio info."""
        result = await transport.execute("audio.get_audio_info", {})
        assert result.success
