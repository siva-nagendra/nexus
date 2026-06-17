"""MLServer direct HTTP tools: call the FastAPI sidecar without C++ round-trip.

These tools are useful when the Python agent needs ML results directly
(e.g., for evaluation, comparison, or autonomous decision-making).
"""

from __future__ import annotations

import time
from typing import Any, Literal

import httpx
from fastmcp import Context
from mcp.types import ToolAnnotations

from nexus.memory.call_logger import get_call_logger
from nexus.server import mcp

MLSERVER_BASE = "http://127.0.0.1:8000"
# ML inference can be slow, especially for depth estimation and 3D conversion
MLSERVER_TIMEOUT = 120.0


async def _mlserver_get(endpoint: str, params: dict[str, Any] | None = None) -> dict:
    """Send a GET request to the MLServer sidecar and return the JSON response."""
    call_start = time.monotonic()
    async with httpx.AsyncClient(base_url=MLSERVER_BASE, timeout=MLSERVER_TIMEOUT) as client:
        response = await client.get(endpoint, params=params)
        response.raise_for_status()
        result = response.json()
        duration_ms = round((time.monotonic() - call_start) * 1000, 1)
        get_call_logger().log_call(
            command=f"mlserver.GET:{endpoint}",
            params=params,
            response=result,
            success=True,
            duration_ms=duration_ms,
            transport="http_mlserver",
        )
        return result


async def _mlserver_post(
    endpoint: str,
    json_body: dict[str, Any] | None = None,
) -> dict:
    """Send a POST request to the MLServer sidecar and return the JSON response."""
    call_start = time.monotonic()
    async with httpx.AsyncClient(base_url=MLSERVER_BASE, timeout=MLSERVER_TIMEOUT) as client:
        response = await client.post(endpoint, json=json_body)
        response.raise_for_status()
        result = response.json()
        duration_ms = round((time.monotonic() - call_start) * 1000, 1)
        get_call_logger().log_call(
            command=f"mlserver.POST:{endpoint}",
            params=json_body,
            response=result,
            success=True,
            duration_ms=duration_ms,
            transport="http_mlserver",
        )
        return result


# ---------------------------------------------------------------------------
# ml_health
# ---------------------------------------------------------------------------


@mcp.tool(
    title="MLServer Health",
    tags={"mlserver"},
    annotations=ToolAnnotations(
        readOnlyHint=True,
        destructiveHint=False,
        idempotentHint=True,
        openWorldHint=False,
    ),
)
async def ml_health(ctx: Context = None) -> dict:
    """Check if the MLServer sidecar is running and responsive."""
    try:
        return await _mlserver_get("/health")
    except httpx.ConnectError:
        raise ValueError(
            "MLServer is not reachable at http://127.0.0.1:8000. "
            "Launch SceneForge or start the sidecar manually."
        )
    except httpx.HTTPStatusError as status_error:
        raise ValueError(f"MLServer health check returned: {status_error.response.status_code}")


# ---------------------------------------------------------------------------
# ml_detect_objects
# ---------------------------------------------------------------------------


@mcp.tool(
    title="Detect Objects",
    tags={"mlserver"},
    annotations=ToolAnnotations(
        readOnlyHint=True,
        destructiveHint=False,
        idempotentHint=True,
        openWorldHint=False,
    ),
)
async def ml_detect_objects(
    image_base64: str,
    model: str = "gemini-2.5-flash",
    ctx: Context = None,
) -> dict:
    """Detect objects in an image using the configured vision model.

    Returns bounding boxes and labels for each detected object.

    Args:
        image_base64: Base64-encoded PNG or JPEG image.
        model: Vision model to use for detection.
    """
    try:
        return await _mlserver_post(
            "/detect-objects",
            {"image_base64": image_base64, "model": model},
        )
    except httpx.HTTPStatusError as status_error:
        raise ValueError(f"Object detection failed ({status_error.response.status_code})")
    except httpx.ConnectError:
        raise ValueError("MLServer is not reachable at http://127.0.0.1:8000.")


# ---------------------------------------------------------------------------
# ml_segment_batch
# ---------------------------------------------------------------------------


@mcp.tool(
    title="Batch Segment",
    tags={"mlserver"},
    annotations=ToolAnnotations(
        readOnlyHint=True,
        destructiveHint=False,
        idempotentHint=True,
        openWorldHint=False,
    ),
)
async def ml_segment_batch(
    boxes: list[list[float]],
    labels: list[str],
    ctx: Context = None,
) -> dict:
    """Segment multiple objects using SAM 2.1 with box prompts.

    Each box is [x_min, y_min, x_max, y_max] in normalized (0-1) coordinates.

    Args:
        boxes: List of bounding boxes, each as [x_min, y_min, x_max, y_max].
        labels: List of labels corresponding to each box.
    """
    try:
        return await _mlserver_post(
            "/segment-batch",
            {"boxes": boxes, "labels": labels},
        )
    except httpx.HTTPStatusError as status_error:
        raise ValueError(f"Batch segmentation failed ({status_error.response.status_code})")
    except httpx.ConnectError:
        raise ValueError("MLServer is not reachable at http://127.0.0.1:8000.")


# ---------------------------------------------------------------------------
# ml_depth
# ---------------------------------------------------------------------------


@mcp.tool(
    title="Estimate Depth",
    tags={"mlserver"},
    annotations=ToolAnnotations(
        readOnlyHint=True,
        destructiveHint=False,
        idempotentHint=True,
        openWorldHint=False,
    ),
)
async def ml_depth(
    image_base64: str,
    ctx: Context = None,
) -> dict:
    """Estimate metric depth from an image using Depth Anything V2.

    Returns a base64-encoded depth map and camera intrinsics.

    Args:
        image_base64: Base64-encoded PNG or JPEG image.
    """
    try:
        return await _mlserver_post(
            "/depth",
            {"image_base64": image_base64},
        )
    except httpx.HTTPStatusError as status_error:
        raise ValueError(f"Depth estimation failed ({status_error.response.status_code})")
    except httpx.ConnectError:
        raise ValueError("MLServer is not reachable at http://127.0.0.1:8000.")


# ---------------------------------------------------------------------------
# ml_depth_to_mesh
# ---------------------------------------------------------------------------


@mcp.tool(
    title="Depth to Mesh",
    tags={"mlserver"},
    annotations=ToolAnnotations(
        readOnlyHint=True,
        destructiveHint=False,
        idempotentHint=False,
        openWorldHint=False,
    ),
)
async def ml_depth_to_mesh(
    depth_base64: str,
    image_base64: str,
    focal_length_px: float,
    width: int,
    height: int,
    min_depth: float = 0.1,
    max_depth: float = 20.0,
    ctx: Context = None,
) -> dict:
    """Convert a depth map to a textured 3D mesh using Open3D.

    Args:
        depth_base64: Base64-encoded depth map (16-bit PNG or float array).
        image_base64: Base64-encoded color image for texturing.
        focal_length_px: Camera focal length in pixels.
        width: Image width in pixels.
        height: Image height in pixels.
        min_depth: Minimum depth threshold in meters.
        max_depth: Maximum depth threshold in meters.
    """
    try:
        return await _mlserver_post(
            "/depth-to-mesh",
            {
                "depth_base64": depth_base64,
                "image_base64": image_base64,
                "focal_length_px": focal_length_px,
                "width": width,
                "height": height,
                "min_depth": min_depth,
                "max_depth": max_depth,
            },
        )
    except httpx.HTTPStatusError as status_error:
        raise ValueError(f"Depth-to-mesh failed ({status_error.response.status_code})")
    except httpx.ConnectError:
        raise ValueError("MLServer is not reachable at http://127.0.0.1:8000.")


# ---------------------------------------------------------------------------
# ml_generate_image
# ---------------------------------------------------------------------------


@mcp.tool(
    title="ML Generate Image",
    tags={"mlserver"},
    annotations=ToolAnnotations(
        readOnlyHint=True,
        destructiveHint=False,
        idempotentHint=False,
        openWorldHint=False,
    ),
)
async def ml_generate_image(
    prompt: str,
    model: str = "gpt-image-1",
    aspect_ratio: str = "16:9",
    ctx: Context = None,
) -> dict:
    """Generate an image from a text prompt via the MLServer.

    Unlike sf_generate_image, this returns the base64 result directly
    without loading it into SceneForge's viewport.

    Args:
        prompt: Text description of the image to generate.
        model: Image generation model name.
        aspect_ratio: Aspect ratio (e.g., "16:9", "1:1", "4:3").
    """
    try:
        return await _mlserver_post(
            "/generate-image",
            {"prompt": prompt, "model": model, "aspect_ratio": aspect_ratio},
        )
    except httpx.HTTPStatusError as status_error:
        raise ValueError(f"Image generation failed ({status_error.response.status_code})")
    except httpx.ConnectError:
        raise ValueError("MLServer is not reachable at http://127.0.0.1:8000.")


# ---------------------------------------------------------------------------
# ml_convert_to_3d
# ---------------------------------------------------------------------------


@mcp.tool(
    title="ML Convert to 3D",
    tags={"mlserver"},
    annotations=ToolAnnotations(
        readOnlyHint=True,
        destructiveHint=False,
        idempotentHint=False,
        openWorldHint=False,
    ),
)
async def ml_convert_to_3d(
    cropped_image_base64: str,
    provider: Literal["meshy", "tripo3d", "rodin"] = "meshy",
    quality: Literal["draft", "standard", "high"] = "standard",
    ctx: Context = None,
) -> dict:
    """Submit a cropped segment image for 3D mesh conversion.

    Returns a task/job ID for polling status. Use ml_check_conversion to track progress.

    Args:
        cropped_image_base64: Base64-encoded cropped segment image.
        provider: Mesh generation provider.
        quality: Mesh quality tier.
    """
    try:
        return await _mlserver_post(
            "/convert-to-3d",
            {
                "cropped_image_base64": cropped_image_base64,
                "provider": provider,
                "quality": quality,
            },
        )
    except httpx.HTTPStatusError as status_error:
        raise ValueError(f"3D conversion submission failed ({status_error.response.status_code})")
    except httpx.ConnectError:
        raise ValueError("MLServer is not reachable at http://127.0.0.1:8000.")


# ---------------------------------------------------------------------------
# ml_check_conversion
# ---------------------------------------------------------------------------


@mcp.tool(
    title="Check 3D Conversion",
    tags={"mlserver"},
    annotations=ToolAnnotations(
        readOnlyHint=True,
        destructiveHint=False,
        idempotentHint=True,
        openWorldHint=False,
    ),
)
async def ml_check_conversion(
    task_id: str,
    provider: Literal["meshy", "tripo3d", "rodin"] = "meshy",
    ctx: Context = None,
) -> dict:
    """Check the status of a 3D conversion job.

    Args:
        task_id: Task/job ID returned by ml_convert_to_3d.
        provider: The provider that is handling the conversion.
    """
    # Each provider has its own status endpoint
    endpoint_map = {
        "meshy": f"/meshy-status/{task_id}",
        "tripo3d": f"/tripo3d-status/{task_id}",
        "rodin": f"/rodin-status/{task_id}",
    }
    endpoint = endpoint_map[provider]

    try:
        return await _mlserver_get(endpoint)
    except httpx.HTTPStatusError as status_error:
        raise ValueError(
            f"Conversion status check failed ({status_error.response.status_code}). "
            f"Verify task_id '{task_id}' is valid for provider '{provider}'."
        )
    except httpx.ConnectError:
        raise ValueError("MLServer is not reachable at http://127.0.0.1:8000.")


# ---------------------------------------------------------------------------
# ml_refine_placement
# ---------------------------------------------------------------------------


@mcp.tool(
    title="Refine Placement",
    tags={"mlserver"},
    annotations=ToolAnnotations(
        readOnlyHint=True,
        destructiveHint=False,
        idempotentHint=False,
        openWorldHint=False,
    ),
)
async def ml_refine_placement(
    target_image_base64: str,
    scene_screenshot_base64: str,
    actors: list[dict[str, Any]],
    ctx: Context = None,
) -> dict:
    """Use vision AI to suggest placement refinements for scene actors.

    Compares the target 2D image against the current 3D scene screenshot
    and suggests position/rotation/scale adjustments for each actor.

    Args:
        target_image_base64: Base64-encoded original reference image.
        scene_screenshot_base64: Base64-encoded screenshot of the current 3D scene.
        actors: List of actor dicts with keys: name, actor_path, position, rotation, scale.
    """
    try:
        return await _mlserver_post(
            "/refine-placement",
            {
                "target_image_base64": target_image_base64,
                "scene_screenshot_base64": scene_screenshot_base64,
                "actors": actors,
            },
        )
    except httpx.HTTPStatusError as status_error:
        raise ValueError(f"Placement refinement failed ({status_error.response.status_code})")
    except httpx.ConnectError:
        raise ValueError("MLServer is not reachable at http://127.0.0.1:8000.")


# ---------------------------------------------------------------------------
# ml_extract_labels
# ---------------------------------------------------------------------------


@mcp.tool(
    title="Extract Labels",
    tags={"mlserver"},
    annotations=ToolAnnotations(
        readOnlyHint=True,
        destructiveHint=False,
        idempotentHint=True,
        openWorldHint=False,
    ),
)
async def ml_extract_labels(
    image_base64: str,
    ctx: Context = None,
) -> dict:
    """Extract descriptive labels/captions for objects visible in an image.

    Useful for auto-naming segments or generating scene descriptions.

    Args:
        image_base64: Base64-encoded PNG or JPEG image.
    """
    try:
        return await _mlserver_post(
            "/extract-labels",
            {"image_base64": image_base64},
        )
    except httpx.HTTPStatusError as status_error:
        raise ValueError(f"Label extraction failed ({status_error.response.status_code})")
    except httpx.ConnectError:
        raise ValueError("MLServer is not reachable at http://127.0.0.1:8000.")
