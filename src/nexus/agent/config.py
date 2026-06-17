"""Agent configuration."""

from __future__ import annotations

from dataclasses import dataclass


@dataclass
class AgentConfig:
    """Configuration for the autonomous agent loop.

    These thresholds control when the agent stops iterating
    and how aggressively it retries after failures.
    """

    max_iterations: int = 50
    screenshot_width: int = 512
    screenshot_height: int = 288
    # Minimum visual similarity score (0-1) to consider a goal satisfied
    visual_threshold: float = 0.7
    # Seconds to wait between observation cycles
    observation_interval: float = 1.0
    # Whether to store memory entries for every action
    enable_memory: bool = True
    # Maximum consecutive failures before aborting
    max_consecutive_failures: int = 5
