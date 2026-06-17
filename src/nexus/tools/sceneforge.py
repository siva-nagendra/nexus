"""SceneForge application tools: pipeline control, segmentation, scene management, and observation.

Routes commands to the in-game HTTP server (port 13378) first, falling back
to the MLServer HTTP proxy (port 8000) when the in-game server is unreachable.
Each HTTP request opens a fresh TCP connection to the Nexus C++ plugin.

Requires SceneForge to be running in game mode with the Nexus plugin active.
"""

from __future__ import annotations

import logging
import time
from typing import Any, Literal

import httpx
from fastmcp import Context
from mcp.types import ToolAnnotations

from nexus.memory.call_logger import get_call_logger
from nexus.server import get_conn, mcp

_logger = logging.getLogger("nexus.tools")

# Primary: in-game HTTP server bound by the Nexus C++ plugin
_PROXY_BASE = "http://127.0.0.1:13378"
# Fallback: MLServer sidecar proxy (slower, cross-thread dispatch)
_FALLBACK_BASE = "http://127.0.0.1:8000"
_PROXY_TIMEOUT = 60.0


def _parse_response(data: dict, command: str) -> tuple[bool, str]:
    """Extract success flag and error message from the proxy response payload."""
    success = data.get("success", True)
    error_msg = ""
    if not success:
        error_info = data.get("error", {})
        error_msg = (
            error_info.get("message", str(error_info))
            if isinstance(error_info, dict)
            else str(error_info)
        )
    return success, error_msg


async def _sf_execute(
    command: str, params: dict[str, Any] | None = None, timeout: float = 30.0
) -> dict:
    """Execute a SceneForge command, preferring the in-game HTTP server.

    Falls back to the MLServer proxy when the primary port is unreachable,
    so callers work regardless of which server is running.
    """
    call_start = time.monotonic()
    transport = "http_ingame"

    try:
        async with httpx.AsyncClient(base_url=_PROXY_BASE, timeout=_PROXY_TIMEOUT) as client:
            response = await client.post(
                f"/nexus/{command}",
                json={"params": params or {}, "timeout": timeout},
            )
            response.raise_for_status()
            data = response.json()
    except httpx.ConnectError:
        # In-game HTTP server unreachable; fall back to MLServer sidecar
        _logger.warning(
            "In-game HTTP server unreachable at %s, falling back to MLServer proxy",
            _PROXY_BASE,
        )
        transport = "http_proxy_fallback"
        async with httpx.AsyncClient(base_url=_FALLBACK_BASE, timeout=_PROXY_TIMEOUT) as client:
            response = await client.post(
                f"/nexus/{command}",
                json={"params": params or {}, "timeout": timeout},
            )
            response.raise_for_status()
            data = response.json()

    duration_ms = round((time.monotonic() - call_start) * 1000, 1)
    success, error_msg = _parse_response(data, command)

    # Log every call for RL training data
    get_call_logger().log_call(
        command=command,
        params=params,
        response=data.get("data"),
        success=success,
        error=error_msg,
        duration_ms=duration_ms,
        transport=transport,
    )

    if not success:
        raise ValueError(f"{command} failed: {error_msg}")
    return data.get("data", {})


# ---------------------------------------------------------------------------
# sf_status
# ---------------------------------------------------------------------------


@mcp.tool(
    title="SceneForge Status",
    tags={"sceneforge"},
    annotations=ToolAnnotations(
        readOnlyHint=True,
        destructiveHint=False,
        idempotentHint=True,
        openWorldHint=False,
    ),
)
async def sf_status(ctx: Context = None) -> dict:
    """Get the current SceneForge pipeline state, mode, image status, and segment count."""
    return await _sf_execute("sceneforge.get_status")


# ---------------------------------------------------------------------------
# sf_generate_image
# ---------------------------------------------------------------------------


@mcp.tool(
    title="Generate Image",
    tags={"sceneforge"},
    annotations=ToolAnnotations(
        readOnlyHint=False,
        destructiveHint=False,
        idempotentHint=False,
        openWorldHint=False,
    ),
)
async def sf_generate_image(prompt: str, ctx: Context = None) -> dict:
    """Generate a 2D image from a text prompt using the configured image model.

    The image appears in SceneForge's viewport and becomes the canvas for segmentation.
    This is step 1 of the pipeline: Prompt > Image > Segment > Convert > Explore.

    Args:
        prompt: Text description of the scene to generate
            (e.g., "a cozy living room with a fireplace").
    """
    return await _sf_execute("sceneforge.generate_image", {"prompt": prompt}, timeout=120.0)


# ---------------------------------------------------------------------------
# sf_segment
# ---------------------------------------------------------------------------


@mcp.tool(
    title="Segment Object",
    tags={"sceneforge"},
    annotations=ToolAnnotations(
        readOnlyHint=False,
        destructiveHint=False,
        idempotentHint=False,
        openWorldHint=False,
    ),
)
async def sf_segment(
    x: float,
    y: float,
    positive: bool = True,
    add_to_existing: bool = False,
    ctx: Context = None,
) -> dict:
    """Click on the generated image to segment an object using SAM 2.1.

    Coordinates are normalized (0-1). Multiple clicks refine the mask.
    Use positive=True for foreground, False for background exclusion.
    Set add_to_existing=True to accumulate points (like Shift-click).

    Args:
        x: Normalized X coordinate on the image (0=left, 1=right).
        y: Normalized Y coordinate on the image (0=top, 1=bottom).
        positive: True for foreground inclusion, False for background exclusion.
        add_to_existing: True to add this point to existing selections (multi-point refinement).
    """
    return await _sf_execute(
        "sceneforge.segment",
        {
            "x": x,
            "y": y,
            "positive": positive,
            "add_to_existing": add_to_existing,
        },
        timeout=30.0,
    )


# ---------------------------------------------------------------------------
# sf_accept_segment
# ---------------------------------------------------------------------------


@mcp.tool(
    title="Accept Segment",
    tags={"sceneforge"},
    annotations=ToolAnnotations(
        readOnlyHint=False,
        destructiveHint=False,
        idempotentHint=False,
        openWorldHint=False,
    ),
)
async def sf_accept_segment(name: str = "", ctx: Context = None) -> dict:
    """Accept the current segmentation mask as a named segment.

    Args:
        name: Display name for the segment (auto-generated if empty).
    """
    params: dict[str, Any] = {}
    if name:
        params["name"] = name

    return await _sf_execute("sceneforge.accept_segment", params)


# ---------------------------------------------------------------------------
# sf_convert
# ---------------------------------------------------------------------------


@mcp.tool(
    title="Convert to 3D",
    tags={"sceneforge"},
    annotations=ToolAnnotations(
        readOnlyHint=False,
        destructiveHint=False,
        idempotentHint=False,
        openWorldHint=False,
    ),
)
async def sf_convert(
    segment_index: int = -1,
    convert_all: bool = False,
    ctx: Context = None,
) -> dict:
    """Convert a segment (or all pending segments) to a 3D mesh.

    Uses the configured mesh provider (Meshy, Tripo3D, or Rodin).
    Conversion is async; use sf_status to monitor progress.

    Args:
        segment_index: Index of the segment to convert (-1 with convert_all=True converts all).
        convert_all: If True, converts all pending segments.
    """
    if convert_all:
        return await _sf_execute("sceneforge.convert_all", timeout=30.0)

    return await _sf_execute(
        "sceneforge.convert_to_3d",
        {"segment_index": segment_index},
        timeout=30.0,
    )


# ---------------------------------------------------------------------------
# sf_generate_scene
# ---------------------------------------------------------------------------


@mcp.tool(
    title="Generate Whole Scene",
    tags={"sceneforge"},
    annotations=ToolAnnotations(
        readOnlyHint=False,
        destructiveHint=False,
        idempotentHint=False,
        openWorldHint=False,
    ),
)
async def sf_generate_scene(ctx: Context = None) -> dict:
    """Run the full whole-scene generation pipeline.

    One-click pipeline: detect objects > segment all > build scene shell > convert all to 3D.
    Monitor progress with sf_status().
    """
    return await _sf_execute("sceneforge.generate_scene", timeout=30.0)


# ---------------------------------------------------------------------------
# sf_screenshot
# ---------------------------------------------------------------------------


@mcp.tool(
    title="SceneForge Screenshot",
    tags={"sceneforge"},
    annotations=ToolAnnotations(
        readOnlyHint=True,
        destructiveHint=False,
        idempotentHint=True,
        openWorldHint=False,
    ),
)
async def sf_screenshot(
    filename: str = "",
    width: int = 1920,
    height: int = 1080,
    ctx: Context = None,
) -> dict:
    """Capture the current viewport as a screenshot.

    Args:
        filename: Output filename (auto-generated if empty).
        width: Screenshot width in pixels.
        height: Screenshot height in pixels.
    """
    return await _sf_execute(
        "sceneforge.take_screenshot",
        {"filename": filename, "width": width, "height": height},
    )


# ---------------------------------------------------------------------------
# sf_get_image
# ---------------------------------------------------------------------------


@mcp.tool(
    title="Get Generated Image",
    tags={"sceneforge"},
    annotations=ToolAnnotations(
        readOnlyHint=True,
        destructiveHint=False,
        idempotentHint=True,
        openWorldHint=False,
    ),
)
async def sf_get_image(ctx: Context = None) -> dict:
    """Get the generated 2D image as base64-encoded PNG."""
    return await _sf_execute("sceneforge.get_generated_image")


# ---------------------------------------------------------------------------
# sf_get_depth
# ---------------------------------------------------------------------------


@mcp.tool(
    title="Get Depth Map",
    tags={"sceneforge"},
    annotations=ToolAnnotations(
        readOnlyHint=True,
        destructiveHint=False,
        idempotentHint=True,
        openWorldHint=False,
    ),
)
async def sf_get_depth(ctx: Context = None) -> dict:
    """Get the depth map with camera intrinsics (FOV, focal length, height)."""
    return await _sf_execute("sceneforge.get_depth_map")


# ---------------------------------------------------------------------------
# sf_get_segments
# ---------------------------------------------------------------------------


@mcp.tool(
    title="Get Segments",
    tags={"sceneforge"},
    annotations=ToolAnnotations(
        readOnlyHint=True,
        destructiveHint=False,
        idempotentHint=True,
        openWorldHint=False,
    ),
)
async def sf_get_segments(ctx: Context = None) -> dict:
    """Get all segments with their status, bounding boxes, depth, and 3D bbox data."""
    return await _sf_execute("sceneforge.get_segments")


# ---------------------------------------------------------------------------
# sf_mode
# ---------------------------------------------------------------------------


@mcp.tool(
    title="SceneForge Mode",
    tags={"sceneforge"},
    annotations=ToolAnnotations(
        readOnlyHint=False,
        destructiveHint=False,
        idempotentHint=True,
        openWorldHint=False,
    ),
)
async def sf_mode(
    action: Literal["get", "toggle", "edit", "explore"] = "toggle",
    ctx: Context = None,
) -> dict:
    """Get or change the SceneForge interaction mode.

    Args:
        action: "get" returns current mode, "toggle" switches, "edit"/"explore" set directly.
    """
    if action == "get":
        return await _sf_execute("sceneforge.get_mode")

    if action == "toggle":
        return await _sf_execute("sceneforge.toggle_mode")

    # For "edit" or "explore", get current mode first and toggle if needed
    current_data = await _sf_execute("sceneforge.get_mode")
    current_mode = current_data.get("mode", "").lower()
    if current_mode != action:
        return await _sf_execute("sceneforge.toggle_mode")
    return current_data


# ---------------------------------------------------------------------------
# sf_move_object
# ---------------------------------------------------------------------------


@mcp.tool(
    title="Move Scene Object",
    tags={"sceneforge"},
    annotations=ToolAnnotations(
        readOnlyHint=False,
        destructiveHint=False,
        idempotentHint=True,
        openWorldHint=False,
    ),
)
async def sf_move_object(
    actor_path: str,
    x: float,
    y: float,
    z: float,
    yaw: float | None = None,
    scale_factor: float | None = None,
    ctx: Context = None,
) -> dict:
    """Move and optionally scale a scene actor.

    Args:
        actor_path: Full actor path in the level.
        x: World X position.
        y: World Y position.
        z: World Z position.
        yaw: Optional yaw rotation in degrees.
        scale_factor: Optional uniform scale multiplier.
    """
    move_params: dict[str, Any] = {
        "actor_path": actor_path,
        "location": {"x": x, "y": y, "z": z},
    }
    if yaw is not None:
        move_params["rotation"] = {"yaw": yaw}

    move_data = await _sf_execute("sceneforge.move_actor", move_params)

    # Apply scale separately if requested
    if scale_factor is not None:
        return await _sf_execute(
            "sceneforge.scale_actor",
            {"actor_path": actor_path, "scale_factor": scale_factor},
        )

    return move_data


# ---------------------------------------------------------------------------
# sf_settings
# ---------------------------------------------------------------------------


@mcp.tool(
    title="SceneForge Settings",
    tags={"sceneforge"},
    annotations=ToolAnnotations(
        readOnlyHint=False,
        destructiveHint=False,
        idempotentHint=True,
        openWorldHint=False,
    ),
)
async def sf_settings(
    action: Literal["get", "set"] = "get",
    key: str = "",
    value: str = "",
    ctx: Context = None,
) -> dict:
    """Read or write SceneForge settings.

    Settings include: ImageModel, MeshProvider, MeshQuality, DepthModelMode,
    bAutoHideImageOnMeshComplete, MaxConcurrentConversions, bNRPActive, NRPPreset, etc.

    Args:
        action: "get" returns all settings, "set" changes a specific setting.
        key: Setting property name (for action="set").
        value: New value as string (for action="set").
    """
    if action == "get":
        return await _sf_execute("sceneforge.get_settings")

    if action == "set":
        if not key:
            raise ValueError("Setting 'key' is required for action='set'.")
        return await _sf_execute(
            "sceneforge.set_settings",
            {"key": key, "value": value},
        )

    raise ValueError(f"Unknown action: {action}")


# ---------------------------------------------------------------------------
# sf_camera
# ---------------------------------------------------------------------------


@mcp.tool(
    title="SceneForge Camera",
    tags={"sceneforge"},
    annotations=ToolAnnotations(
        readOnlyHint=False,
        destructiveHint=False,
        idempotentHint=True,
        openWorldHint=False,
    ),
)
async def sf_camera(
    action: Literal["get", "set"] = "get",
    x: float = 0.0,
    y: float = 0.0,
    z: float = 0.0,
    pitch: float = 0.0,
    yaw: float = 0.0,
    ctx: Context = None,
) -> dict:
    """Get or set the viewport camera position and rotation.

    Args:
        action: "get" returns current camera transform, "set" moves the camera.
        x: World X position (for action="set").
        y: World Y position (for action="set").
        z: World Z position (for action="set").
        pitch: Rotation pitch in degrees (for action="set").
        yaw: Rotation yaw in degrees (for action="set").
    """
    if action == "get":
        return await _sf_execute("sceneforge.get_camera")

    if action == "set":
        return await _sf_execute(
            "sceneforge.set_camera",
            {
                "location": {"x": x, "y": y, "z": z},
                "rotation": {"pitch": pitch, "yaw": yaw},
            },
        )

    raise ValueError(f"Unknown action: {action}")


# ---------------------------------------------------------------------------
# sf_session
# ---------------------------------------------------------------------------


@mcp.tool(
    title="SceneForge Session",
    tags={"sceneforge"},
    annotations=ToolAnnotations(
        readOnlyHint=False,
        destructiveHint=False,
        idempotentHint=True,
        openWorldHint=False,
    ),
)
async def sf_session(
    action: Literal["save", "load", "list", "get_dir"] = "list",
    name: str = "",
    path: str = "",
    ctx: Context = None,
) -> dict:
    """Manage SceneForge sessions (save/load/list).

    Args:
        action: Operation to perform.
        name: Session name (for save).
        path: Session file path (for load).
    """
    if action == "save":
        params: dict[str, Any] = {}
        if name:
            params["name"] = name
        return await _sf_execute("sceneforge.save_session", params)

    if action == "load":
        return await _sf_execute("sceneforge.load_session", {"path": path})

    if action == "list":
        return await _sf_execute("sceneforge.list_sessions")

    if action == "get_dir":
        return await _sf_execute("sceneforge.get_session_dir")

    raise ValueError(f"Unknown action: {action}")


# ---------------------------------------------------------------------------
# sf_toggle_visibility
# ---------------------------------------------------------------------------


@mcp.tool(
    title="Toggle Visibility",
    tags={"sceneforge"},
    annotations=ToolAnnotations(
        readOnlyHint=False,
        destructiveHint=False,
        idempotentHint=True,
        openWorldHint=False,
    ),
)
async def sf_toggle_visibility(
    target: Literal["image", "shell", "actor"] = "image",
    actor_path: str = "",
    visible: bool = True,
    ctx: Context = None,
) -> dict:
    """Toggle visibility of the reference image, scene shell, or a specific actor.

    Args:
        target: What to show/hide ("image", "shell", or "actor").
        actor_path: Actor path (required when target="actor").
        visible: True to show, False to hide.
    """
    if target == "image":
        return await _sf_execute(
            "sceneforge.toggle_image_plane",
            {"visible": visible},
        )

    if target == "shell":
        return await _sf_execute(
            "sceneforge.toggle_scene_shell",
            {"visible": visible},
        )

    if target == "actor":
        if not actor_path:
            raise ValueError("actor_path is required when target='actor'.")
        return await _sf_execute(
            "actor.set_visibility",
            {"actor_path": actor_path, "visible": visible, "propagate_to_children": True},
        )

    raise ValueError(f"Unknown target: {target}")


# ---------------------------------------------------------------------------
# sf_health
# ---------------------------------------------------------------------------


@mcp.tool(
    title="SceneForge Health",
    tags={"sceneforge"},
    annotations=ToolAnnotations(
        readOnlyHint=True,
        destructiveHint=False,
        idempotentHint=True,
        openWorldHint=False,
    ),
)
async def sf_health(ctx: Context = None) -> dict:
    """Check health of SceneForge, Nexus plugin, and ML sidecar."""
    return await _sf_execute("sceneforge.health_check")


# ---------------------------------------------------------------------------
# sf_console
# ---------------------------------------------------------------------------


@mcp.tool(
    title="SceneForge Console",
    tags={"sceneforge"},
    annotations=ToolAnnotations(
        readOnlyHint=False,
        destructiveHint=False,
        idempotentHint=False,
        openWorldHint=True,
    ),
)
async def sf_console(command: str, ctx: Context = None) -> dict:
    """Execute a SceneForge console command (sf.* or any UE console command).

    Common commands: sf.generate <prompt>, sf.accept [name], sf.convert [index],
    sf.convertall, sf.mode, sf.status, sf.scene, sf.clear, sf.settings

    Args:
        command: Console command string (e.g., "sf.generate a cozy living room").
    """
    return await _sf_execute("sceneforge.exec_console", {"command": command})


# ---------------------------------------------------------------------------
# sf_check_overlap
# ---------------------------------------------------------------------------


@mcp.tool(
    title="Check Segment Overlap",
    tags={"sceneforge"},
    annotations=ToolAnnotations(
        readOnlyHint=True,
        destructiveHint=False,
        idempotentHint=True,
        openWorldHint=False,
    ),
)
async def sf_check_overlap(
    new_mask_base64: str,
    existing_masks_base64: list[str],
    overlap_threshold: float = 0.7,
    ctx: Context = None,
) -> dict:
    """Check if a new segmentation mask overlaps existing masks.

    Uses pixel-level comparison via the MLServer /check-overlap endpoint.
    Returns is_duplicate (bool) and overlap_ratio (float).

    Args:
        new_mask_base64: Base64-encoded PNG of the new mask.
        existing_masks_base64: List of base64-encoded PNGs of existing masks.
        overlap_threshold: Minimum overlap ratio to consider a duplicate (0-1).
    """
    call_start = time.monotonic()
    # Always hits MLServer directly since overlap detection runs on CPU/GPU there
    async with httpx.AsyncClient(base_url=_FALLBACK_BASE, timeout=30.0) as client:
        response = await client.post(
            "/check-overlap",
            json={
                "new_mask_base64": new_mask_base64,
                "existing_masks_base64": existing_masks_base64,
                "overlap_threshold": overlap_threshold,
            },
        )
        response.raise_for_status()
        result = response.json()

    duration_ms = round((time.monotonic() - call_start) * 1000, 1)
    get_call_logger().log_call(
        command="mlserver.POST:/check-overlap",
        params={
            "threshold": overlap_threshold,
            "existing_count": len(existing_masks_base64),
        },
        response=result,
        success=True,
        duration_ms=duration_ms,
        transport="http_mlserver",
    )

    return result


# ---------------------------------------------------------------------------
# sf_agent_run
# ---------------------------------------------------------------------------


@mcp.tool(
    title="Run SceneForge Agent",
    tags={"sceneforge", "agent"},
    annotations=ToolAnnotations(
        readOnlyHint=False,
        destructiveHint=False,
        idempotentHint=False,
        openWorldHint=True,
    ),
)
async def sf_agent_run(
    goal_type: str,
    description: str,
    reference_image_path: str = "",
    max_iterations: int = 50,
    ctx: Context = None,
) -> dict:
    """Run the autonomous SceneForge agent to achieve a goal.

    The agent observes the scene, makes decisions, executes actions, and learns
    from outcomes. It runs autonomously without requiring human prompts.

    Goal types:
    - "recreate_2d_as_3d": Generate image from description, then build 3D scene
    - "batch_convert": Convert all pending segments to 3D meshes
    - "improve_lighting": Analyze and improve scene lighting
    - "refine_placement": Adjust object positions to match reference
    - "add_environment": Add sky, fog, ground plane
    - "compose_scene": Build scene from text description

    Args:
        goal_type: Type of goal (see above).
        description: Human-readable description of what to achieve.
        reference_image_path: Optional reference image for comparison-based goals.
        max_iterations: Maximum agent iterations before stopping.
    """
    from nexus.agent.config import AgentConfig
    from nexus.agent.goals import SceneGoal
    from nexus.agent.loop import SceneForgeAgent
    from nexus.memory.store import MemoryStore

    conn = get_conn(ctx)
    memory = MemoryStore()
    config = AgentConfig(max_iterations=max_iterations)
    agent = SceneForgeAgent(conn, memory, config)

    goal = SceneGoal(
        goal_type=goal_type,
        description=description,
        reference_image_path=reference_image_path,
        max_iterations=max_iterations,
    )

    result = await agent.execute_goal(goal)
    return {
        "success": result.success,
        "iterations": result.iterations,
        "final_state": result.final_state,
        "error": result.error,
        "learned_patterns": result.learned_patterns,
    }
