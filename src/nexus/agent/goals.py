"""Goal type definitions for the autonomous agent."""

from __future__ import annotations

from dataclasses import dataclass, field
from typing import Any, Literal

GoalType = Literal[
    "recreate_2d_as_3d",
    "improve_lighting",
    "refine_placement",
    "add_environment",
    "compose_scene",
    "batch_convert",
]


@dataclass
class SceneGoal:
    """A goal for the autonomous agent to achieve.

    Goals are the top-level unit of work. Each goal type maps to
    a different planning strategy in the agent loop.
    """

    goal_type: GoalType
    description: str
    reference_image_path: str = ""
    constraints: dict[str, Any] = field(default_factory=dict)
    tags: list[str] = field(default_factory=list)
    max_iterations: int = 50


@dataclass
class GoalResult:
    """Result of executing a goal, including iteration count and learned patterns."""

    success: bool
    iterations: int
    final_state: dict[str, Any] = field(default_factory=dict)
    error: str = ""
    learned_patterns: int = 0
