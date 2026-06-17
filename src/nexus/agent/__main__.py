"""Standalone headless agent runner.

Usage: python -m nexus.agent goal.json
"""

from __future__ import annotations

import asyncio
import json
import sys
from dataclasses import asdict
from pathlib import Path

from nexus.agent.config import AgentConfig
from nexus.agent.goals import SceneGoal
from nexus.agent.loop import SceneForgeAgent
from nexus.connection.manager import ConnectionManager
from nexus.memory.store import MemoryStore


async def run_agent(goal_path: str) -> None:
    """Load a goal from JSON and run the agent loop to completion."""
    goal_data = json.loads(Path(goal_path).read_text(encoding="utf-8"))
    goal = SceneGoal(**goal_data)

    conn = ConnectionManager()
    await conn.initialize()

    memory = MemoryStore()
    agent = SceneForgeAgent(conn, memory, AgentConfig(max_iterations=goal.max_iterations))

    try:
        result = await agent.execute_goal(goal)
        print(json.dumps(asdict(result), indent=2, default=str))  # noqa: T201
    finally:
        await conn.shutdown()


def main() -> None:
    """Entry point for `python -m nexus.agent <goal.json>`."""
    if len(sys.argv) < 2:
        print("Usage: python -m nexus.agent <goal.json>")  # noqa: T201
        sys.exit(1)
    asyncio.run(run_agent(sys.argv[1]))


if __name__ == "__main__":
    main()
