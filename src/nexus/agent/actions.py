"""Action primitives that map to SceneForge MCP commands."""

from __future__ import annotations

import logging
from dataclasses import dataclass, field
from typing import Any

from nexus.connection.manager import ConnectionManager
from nexus.memory.models import ActionRecord

logger = logging.getLogger("nexus.agent.actions")


@dataclass
class ActionResult:
    """Result of executing a single action via the connection manager."""

    success: bool
    data: dict[str, Any] = field(default_factory=dict)
    error: str = ""


class ActionExecutor:
    """Executes ActionRecords via the ConnectionManager and returns results.

    Thin wrapper that bridges the memory model (ActionRecord) to the
    transport layer (ConnectionManager.execute).
    """

    def __init__(self, conn: ConnectionManager) -> None:
        self.conn = conn
        # Per-actor lock set to prevent concurrent modifications
        self._locked: set[str] = set()

    async def execute(self, record: ActionRecord) -> ActionResult:
        """Execute an action and return the result."""
        result = await self.conn.execute(record.command, record.parameters)
        if result.success:
            return ActionResult(success=True, data=result.data or {})
        return ActionResult(success=False, error=result.error or "Unknown error")

    def build_action(
        self,
        action_type: str,
        command: str,
        parameters: dict[str, Any] | None = None,
        target_actor: str = "",
    ) -> ActionRecord:
        """Build an ActionRecord for execution and memory storage."""
        return ActionRecord(
            action_type=action_type,
            command=command,
            parameters=parameters or {},
            target_actor=target_actor,
        )

    def lock_actor(self, actor_path: str) -> bool:
        """Attempt to lock an actor. Returns True if the lock was acquired."""
        if actor_path in self._locked:
            return False
        self._locked.add(actor_path)
        return True

    def unlock_actor(self, actor_path: str) -> None:
        """Release a lock on an actor."""
        self._locked.discard(actor_path)
