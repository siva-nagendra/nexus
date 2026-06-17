"""Result evaluation: structural checks and visual comparison."""

from __future__ import annotations

import logging
from dataclasses import dataclass

import httpx

from nexus.agent.observer import SceneState

logger = logging.getLogger("nexus.agent.evaluator")

MLSERVER_BASE = "http://127.0.0.1:8000"


@dataclass
class Evaluation:
    """Evaluation of an action's outcome against expectations."""

    structural_ok: bool = True
    visual_score: float = 0.0
    is_failure: bool = False
    error: str = ""
    notes: str = ""


class SceneEvaluator:
    """Evaluates whether actions achieved their intended effect.

    Supports two evaluation modes: structural (did counts/state change?)
    and visual (does the scene match a reference image?).
    """

    async def evaluate_structural(
        self,
        expected_change: str,
        pre_state: SceneState,
        post_state: SceneState,
    ) -> Evaluation:
        """Check if the expected structural change occurred between two states."""
        if expected_change == "segment_added":
            ok = post_state.segment_count > pre_state.segment_count
            return Evaluation(structural_ok=ok, is_failure=not ok)

        if expected_change == "actor_added":
            ok = len(post_state.actors) > len(pre_state.actors)
            return Evaluation(structural_ok=ok, is_failure=not ok)

        if expected_change == "state_changed":
            ok = post_state.pipeline_state != pre_state.pipeline_state
            return Evaluation(structural_ok=ok, is_failure=not ok)

        if expected_change == "mode_changed":
            ok = post_state.mode != pre_state.mode
            return Evaluation(structural_ok=ok, is_failure=not ok)

        # Unknown change type: assume success to avoid false negatives
        return Evaluation(structural_ok=True)

    async def compare_to_reference(
        self,
        screenshot_base64: str,
        reference_base64: str,
        actors: list[dict],
    ) -> Evaluation:
        """Use the MLServer VLM refinement endpoint to compare scene vs reference.

        Delegates to the /refine-placement endpoint which returns a confidence
        score and adjustment suggestions.
        """
        try:
            async with httpx.AsyncClient(base_url=MLSERVER_BASE, timeout=60.0) as client:
                response = await client.post(
                    "/refine-placement",
                    json={
                        "target_image_base64": reference_base64,
                        "scene_screenshot_base64": screenshot_base64,
                        "actors": actors,
                    },
                )
                response.raise_for_status()
                data = response.json()
                confidence = data.get("confidence", 0.0)
                return Evaluation(
                    structural_ok=True,
                    visual_score=confidence,
                    notes=data.get("notes", ""),
                )
        except httpx.HTTPError as http_err:
            logger.warning("VLM comparison failed: %s", http_err)
            return Evaluation(is_failure=True, error=str(http_err))
