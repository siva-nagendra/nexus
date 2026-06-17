"""Scene state observation via MCP tool calls."""

from __future__ import annotations

import asyncio
import logging
import time
from dataclasses import dataclass, field
from typing import Any

from nexus.connection.manager import ConnectionManager

logger = logging.getLogger("nexus.agent.observer")


@dataclass
class SceneState:
    """Snapshot of the current SceneForge state.

    Captures enough information for the agent to make decisions
    about what action to take next.
    """

    pipeline_state: str = "Idle"
    mode: str = "Edit"
    has_image: bool = False
    segment_count: int = 0
    segments: list[dict[str, Any]] = field(default_factory=list)
    actors: list[dict[str, Any]] = field(default_factory=list)
    camera: dict[str, Any] = field(default_factory=dict)
    settings: dict[str, Any] = field(default_factory=dict)
    screenshot_path: str = ""

    @property
    def summary(self) -> str:
        """Short text summary for memory context field."""
        return (
            f"state={self.pipeline_state} mode={self.mode} "
            f"image={self.has_image} segments={self.segment_count} "
            f"actors={len(self.actors)}"
        )


class SceneObserver:
    """Captures scene state by calling SceneForge MCP commands.

    Runs multiple queries in sequence to build a complete SceneState
    snapshot for the agent's decision loop.
    """

    def __init__(self, conn: ConnectionManager) -> None:
        self.conn = conn

    async def capture_state(self, include_screenshot: bool = False) -> SceneState:
        """Take a full snapshot of the current scene state."""
        status_result = await self.conn.execute("sceneforge.get_status")
        status = status_result.data if status_result.success else {}

        segments_result = await self.conn.execute("sceneforge.get_segments")
        segments = segments_result.data.get("segments", []) if segments_result.success else []

        camera_result = await self.conn.execute("sceneforge.get_camera")
        camera = camera_result.data if camera_result.success else {}

        settings_result = await self.conn.execute("sceneforge.get_settings")
        settings = settings_result.data if settings_result.success else {}

        screenshot_path = ""
        if include_screenshot:
            ss_result = await self.conn.execute(
                "sceneforge.take_screenshot",
                {"width": 512, "height": 288},
            )
            if ss_result.success:
                screenshot_path = ss_result.data.get("file_path", "")

        return SceneState(
            pipeline_state=status.get("state", "Unknown"),
            mode=status.get("mode", "Unknown"),
            has_image=status.get("has_image", False),
            segment_count=status.get("segment_count", 0),
            segments=segments,
            camera=camera,
            settings=settings,
            screenshot_path=screenshot_path,
        )

    async def wait_for_idle(self, timeout_seconds: float = 300.0) -> bool:
        """Poll until the pipeline returns to Idle state.

        Returns True if idle was reached, False on timeout. Uses a 2-second
        poll interval to avoid hammering the connection.
        """
        start = time.monotonic()
        while time.monotonic() - start < timeout_seconds:
            result = await self.conn.execute("sceneforge.get_status")
            if result.success and result.data.get("state") == "Idle":
                return True
            await asyncio.sleep(2.0)
        return False
