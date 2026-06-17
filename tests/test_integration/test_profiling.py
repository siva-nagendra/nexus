"""Integration tests for the profiling subsystem (namespace: profiling)."""

from __future__ import annotations

import pytest

pytestmark = pytest.mark.integration


class TestProfiling:
    """Frame stats, GPU stats, memory, Insights traces, stat commands."""

    async def test_get_frame_stats(self, transport):
        """Get CPU and GPU frame timing for the last frame."""
        result = await transport.execute("profiling.get_frame_stats", {"num_frames": 1})
        assert result.success

    async def test_get_gpu_stats(self, transport):
        """Get GPU adapter info and VRAM usage."""

    async def test_get_memory_stats(self, transport):
        """Get process memory usage breakdown."""

    async def test_start_and_stop_trace(self, transport):
        """Start an Unreal Insights trace, then stop it."""

    async def test_execute_stat_command(self, transport):
        """Toggle the 'fps' stat display on and off."""
