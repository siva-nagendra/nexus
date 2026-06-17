"""Data models for the agent memory system."""

from __future__ import annotations

import uuid
from datetime import UTC, datetime
from typing import Any

from pydantic import BaseModel, Field


class ActionRecord(BaseModel):
    """Captures what the agent did: command, parameters, and target."""

    action_type: str  # "move_actor", "generate_image", "adjust_lighting", etc.
    command: str  # Full sceneforge.* command that was executed
    parameters: dict[str, Any] = {}
    target_actor: str = ""  # Actor path if applicable


class EvaluationRecord(BaseModel):
    """Captures how an action turned out: success/failure and quality metrics."""

    is_failure: bool = False
    error: str = ""
    visual_score: float = 0.0  # 0-1 similarity to reference
    structural_ok: bool = True  # Whether the expected structural change occurred
    notes: str = ""


class MemoryEntry(BaseModel):
    """Single memory record linking an agent action to its outcome and context."""

    id: str = Field(default_factory=lambda: str(uuid.uuid4()))
    timestamp: datetime = Field(default_factory=lambda: datetime.now(UTC))
    goal_type: str  # "recreate_2d_as_3d", "improve_lighting", etc.
    action: ActionRecord
    outcome: EvaluationRecord
    context: str = ""  # Scene state summary at time of action
    tags: list[str] = []


class PatternEntry(BaseModel):
    """A learned pattern (placement rule, lighting setup, etc.) extracted from memory."""

    id: str = Field(default_factory=lambda: str(uuid.uuid4()))
    pattern_type: str  # "placement", "lighting", "material", "camera"
    description: str
    conditions: dict[str, Any] = {}  # When this pattern applies
    recipe: dict[str, Any] = {}  # What to do when conditions match
    success_count: int = 0
    failure_count: int = 0
    last_used: datetime = Field(default_factory=lambda: datetime.now(UTC))
