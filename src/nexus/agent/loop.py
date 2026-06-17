"""Autonomous agent loop: observe-decide-act-evaluate with memory."""

from __future__ import annotations

import asyncio
import logging

from nexus.agent.actions import ActionExecutor
from nexus.agent.config import AgentConfig
from nexus.agent.evaluator import Evaluation, SceneEvaluator
from nexus.agent.goals import GoalResult, SceneGoal
from nexus.agent.observer import SceneObserver, SceneState
from nexus.connection.manager import ConnectionManager
from nexus.memory.models import ActionRecord, EvaluationRecord, MemoryEntry
from nexus.memory.store import MemoryStore

logger = logging.getLogger("nexus.agent")


class SceneForgeAgent:
    """Autonomous scene composition agent driven by goals and memory.

    Runs an observe-decide-act-evaluate loop. Each iteration captures
    the scene state, selects an action based on the goal type, executes
    it, evaluates the outcome, and stores the experience in memory.
    """

    def __init__(
        self,
        conn: ConnectionManager,
        memory: MemoryStore,
        config: AgentConfig | None = None,
    ) -> None:
        self.conn = conn
        self.memory = memory
        self.config = config or AgentConfig()
        self.observer = SceneObserver(conn)
        self.evaluator = SceneEvaluator()
        self.executor = ActionExecutor(conn)
        # Prevent concurrent modifications to the same actor across iterations
        self._locked_actors: set[str] = set()
        # Track placements to periodically re-estimate depth (RANSAC ground plane refresh)
        self._objects_placed_since_ransac: int = 0

    async def execute_goal(self, goal: SceneGoal) -> GoalResult:
        """Run the agent loop until the goal is satisfied or limits reached."""
        logger.info("Starting goal: %s (%s)", goal.goal_type, goal.description)

        # Recall past experience to inform decision-making
        prior_knowledge = await self.memory.recall(
            goal_type=goal.goal_type,
            tags=goal.tags,
            limit=20,
        )
        if prior_knowledge:
            logger.info(
                "Loaded %d prior memory entries for %s",
                len(prior_knowledge),
                goal.goal_type,
            )

        iteration = 0
        consecutive_failures = 0
        learned_patterns = 0

        while iteration < goal.max_iterations:
            # OBSERVE: capture current scene state
            state = await self.observer.capture_state(include_screenshot=True)
            logger.info("Iteration %d: %s", iteration, state.summary)

            # DECIDE: select next action based on goal type and state
            action = await self._select_action(goal, state, prior_knowledge)
            if action is None:
                logger.info("No more actions needed; goal may be complete")
                break

            # Check resource lock before acting to avoid concurrent modifications
            target_actor = action.target_actor
            if target_actor and target_actor in self._locked_actors:
                logger.info("Actor %s is locked, skipping iteration", target_actor)
                iteration += 1
                continue

            # Lock the actor during action + evaluation
            if target_actor:
                self._locked_actors.add(target_actor)

            # ACT: execute the selected action
            pre_state = state
            result = await self.executor.execute(action)

            if not result.success:
                consecutive_failures += 1
                logger.warning(
                    "Action %s failed: %s (failure %d/%d)",
                    action.action_type,
                    result.error,
                    consecutive_failures,
                    self.config.max_consecutive_failures,
                )
            else:
                consecutive_failures = 0
                # Wait for pipeline to settle after async operations
                await asyncio.sleep(self.config.observation_interval)

                # Placement actions accumulate toward a periodic RANSAC ground plane refresh
                placement_action_types = (
                    "move_actor",
                    "scale_actor",
                    "convert_to_3d",
                    "convert_all",
                )
                if action.action_type in placement_action_types:
                    self._objects_placed_since_ransac += 1
                    if self._objects_placed_since_ransac >= 3:
                        logger.info(
                            "Refreshing ground plane after %d placements",
                            self._objects_placed_since_ransac,
                        )
                        await self.conn.execute("sceneforge.estimate_depth")
                        self._objects_placed_since_ransac = 0

            # EVALUATE: capture post-action state and compare
            post_state = await self.observer.capture_state()
            evaluation = Evaluation(
                structural_ok=result.success,
                is_failure=not result.success,
                error=result.error if not result.success else "",
                notes=f"pre: {pre_state.summary} -> post: {post_state.summary}",
            )

            # Release actor lock after evaluation completes
            if target_actor:
                self._locked_actors.discard(target_actor)

            # LEARN: persist this experience for future recall
            if self.config.enable_memory:
                memory_entry = MemoryEntry(
                    goal_type=goal.goal_type,
                    action=action,
                    outcome=EvaluationRecord(
                        is_failure=evaluation.is_failure,
                        error=evaluation.error,
                        visual_score=evaluation.visual_score,
                        structural_ok=evaluation.structural_ok,
                        notes=evaluation.notes,
                    ),
                    context=post_state.summary,
                    tags=goal.tags,
                )
                await self.memory.store(memory_entry)
                learned_patterns += 1

            # Abort if too many consecutive failures
            if consecutive_failures >= self.config.max_consecutive_failures:
                logger.error("Too many consecutive failures; aborting goal")
                return GoalResult(
                    success=False,
                    iterations=iteration + 1,
                    error="Max consecutive failures reached",
                    learned_patterns=learned_patterns,
                )

            iteration += 1

        final_state = await self.observer.capture_state()
        return GoalResult(
            success=True,
            iterations=iteration,
            final_state={
                "state": final_state.pipeline_state,
                "mode": final_state.mode,
                "segments": final_state.segment_count,
                "actors": len(final_state.actors),
            },
            learned_patterns=learned_patterns,
        )

    async def _select_action(
        self,
        goal: SceneGoal,
        state: SceneState,
        prior_knowledge: list[MemoryEntry],
    ) -> ActionRecord | None:
        """Select the next action based on goal type and current state.

        Each goal type has its own planning strategy that maps the
        current state to the next logical action in the sequence.
        """
        if goal.goal_type == "recreate_2d_as_3d":
            return await self._plan_recreate(goal, state)
        if goal.goal_type == "batch_convert":
            return await self._plan_batch_convert(state)
        if goal.goal_type == "improve_lighting":
            return await self._plan_lighting(state)
        if goal.goal_type == "refine_placement":
            return await self._plan_refinement(goal, state)
        if goal.goal_type == "add_environment":
            return await self._plan_environment(state)
        if goal.goal_type == "compose_scene":
            return await self._plan_compose(goal, state)
        return None

    async def _plan_recreate(self, goal: SceneGoal, state: SceneState) -> ActionRecord | None:
        """Plan actions for recreating a 2D image as a 3D scene.

        Follows the sequence: generate image -> run whole-scene generation -> explore.
        """
        # Generate image from the description if none exists yet
        if not state.has_image and state.pipeline_state == "Idle":
            return self.executor.build_action(
                "generate_image",
                "sceneforge.generate_image",
                {"prompt": goal.description},
            )
        # Kick off whole-scene generation once an image is loaded
        if state.has_image and state.segment_count == 0 and state.pipeline_state == "Idle":
            return self.executor.build_action(
                "generate_scene",
                "sceneforge.generate_scene",
            )
        # Pipeline is busy, wait for it
        if state.pipeline_state not in ("Idle", "Exploring"):
            return None
        # Switch to explore mode once segments are ready
        if state.segment_count > 0 and state.mode == "Edit":
            return self.executor.build_action(
                "toggle_mode",
                "sceneforge.toggle_mode",
            )
        # All steps complete
        return None

    async def _plan_batch_convert(self, state: SceneState) -> ActionRecord | None:
        """Convert all pending segments to 3D meshes."""
        if state.pipeline_state != "Idle":
            return None
        pending = [seg for seg in state.segments if seg.get("status") == "Pending"]
        if pending:
            return self.executor.build_action(
                "convert_all",
                "sceneforge.convert_all",
            )
        return None

    async def _plan_lighting(self, state: SceneState) -> ActionRecord | None:
        """Adjust scene lighting based on current actors and reference."""
        if state.pipeline_state not in ("Idle", "Exploring"):
            return None  # Pipeline busy, wait

        has_lights = any("Light" in actor.get("label", "") for actor in state.actors)

        if not has_lights:
            # No lights at all; trigger environment generation which includes lighting
            return self.executor.build_action(
                "spawn_lighting",
                "sceneforge.exec_console",
                {"command": "sf.generate"},
            )

        # Lights exist but we may want to evaluate quality via a screenshot
        if state.screenshot_path:
            return self.executor.build_action(
                "evaluate_lighting",
                "sceneforge.take_screenshot",
                {"width": 512, "height": 288},
            )

        # Lighting looks stable, goal may be complete
        return None

    async def _plan_refinement(self, goal: SceneGoal, state: SceneState) -> ActionRecord | None:
        """Refine object placement to match reference image using VLM comparison."""
        if state.pipeline_state not in ("Idle", "Exploring"):
            return None  # Pipeline busy

        if not state.actors:
            return None  # No actors to refine

        if not goal.reference_image_path:
            return None  # No reference to compare against

        # Capture current viewport for VLM-driven comparison
        return self.executor.build_action(
            "capture_for_refinement",
            "sceneforge.take_screenshot",
            {"width": 1024, "height": 576, "filename": "refinement_capture.png"},
        )

    async def _plan_environment(self, state: SceneState) -> ActionRecord | None:
        """Add sky, fog, and ground plane to the scene."""
        if state.pipeline_state not in ("Idle", "Exploring"):
            return None

        has_sky = any("Sky" in actor.get("label", "") for actor in state.actors)
        has_fog = any("Fog" in actor.get("label", "") for actor in state.actors)

        if not has_sky:
            return self.executor.build_action(
                "spawn_sky",
                "sceneforge.exec_console",
                {"command": "r.VolumetricCloud 1"},
            )

        if not has_fog:
            return self.executor.build_action(
                "spawn_fog",
                "sceneforge.exec_console",
                {"command": "r.Fog 1"},
            )

        # Sky and fog present; show scene shell if available
        return self.executor.build_action(
            "show_scene_shell",
            "sceneforge.toggle_scene_shell",
            {"visible": True},
        )

    async def _plan_compose(self, goal: SceneGoal, state: SceneState) -> ActionRecord | None:
        """Build a scene from a text description.

        Reuses the recreate pipeline with the description as prompt.
        """
        return await self._plan_recreate(goal, state)
