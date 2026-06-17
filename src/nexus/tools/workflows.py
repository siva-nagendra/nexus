"""Tier 3 — Workflow composite tools (~15 tools).

Multi-step orchestrated operations that execute entirely server-side.
Each tool performs a sequence of sub-operations to achieve a high-level goal,
eliminating multiple LLM round-trips.
"""

from __future__ import annotations

from typing import Any, Literal

from fastmcp import Context
from mcp.types import ToolAnnotations

from nexus.server import get_conn, mcp

# ---------------------------------------------------------------------------
# create_scene_from_description
# ---------------------------------------------------------------------------


@mcp.tool(
    title="Create Scene From Description",
    tags={"workflow"},
    annotations=ToolAnnotations(
        readOnlyHint=False,
        destructiveHint=False,
        idempotentHint=False,
        openWorldHint=False,
    ),
)
async def create_scene_from_description(
    actors: list[dict[str, Any]],
    scene_name: str = "",
    setup_default_lighting: bool = True,
    ctx: Context = None,
) -> dict:
    """Build a complete scene by spawning multiple actors with materials and lighting.

    Orchestrates actor spawning, material assignment, and lighting setup server-side
    in a single tool call. Each entry in the actors list should specify:
    - actor_class: UE class name (e.g., 'StaticMeshActor')
    - label: Display name
    - location: {x, y, z}
    - rotation: {pitch, yaw, roll} (optional)
    - scale: {x, y, z} (optional)
    - mesh_path: Static mesh path (optional)
    - material_path: Material path (optional)

    Args:
        actors: List of actor specifications to spawn.
        scene_name: Optional name prefix for all spawned actors.
        setup_default_lighting: Add DirectionalLight + SkyLight + SkyAtmosphere if true.
    """
    conn = get_conn(ctx)
    results: list[dict] = []
    errors: list[dict] = []

    # Step 1: Default lighting setup
    if setup_default_lighting:
        for light_class in ["DirectionalLight", "SkyLight", "SkyAtmosphere"]:
            label = f"{scene_name}_{light_class}" if scene_name else light_class
            r = await conn.execute(
                "actor.spawn",
                {
                    "actor_class": light_class,
                    "label": label,
                    "location": {"x": 0, "y": 0, "z": 0},
                    "rotation": {
                        "pitch": -50 if light_class == "DirectionalLight" else 0,
                        "yaw": 0,
                        "roll": 0,
                    },
                    "scale": {"x": 1, "y": 1, "z": 1},
                },
            )
            if r.success:
                results.append({"type": "lighting", "class": light_class, **r.data})

    # Step 2: Spawn actors
    for i, spec in enumerate(actors):
        label = spec.get("label", f"{scene_name}_actor_{i}" if scene_name else f"actor_{i}")
        loc = spec.get("location", {})
        rot = spec.get("rotation", {})
        scale = spec.get("scale", {})

        r = await conn.execute(
            "actor.spawn",
            {
                "actor_class": spec.get("actor_class", "StaticMeshActor"),
                "label": label,
                "location": {"x": loc.get("x", 0), "y": loc.get("y", 0), "z": loc.get("z", 0)},
                "rotation": {
                    "pitch": rot.get("pitch", 0),
                    "yaw": rot.get("yaw", 0),
                    "roll": rot.get("roll", 0),
                },
                "scale": {
                    "x": scale.get("x", 1),
                    "y": scale.get("y", 1),
                    "z": scale.get("z", 1),
                },
            },
        )
        if not r.success:
            errors.append({"index": i, "error": r.error})
            continue

        actor_path = r.data.get("actor_path", "")

        # Apply mesh if specified
        if spec.get("mesh_path") and actor_path:
            await conn.execute(
                "actor.set_property",
                {
                    "actor_path": actor_path,
                    "property_name": "StaticMesh",
                    "property_value": spec["mesh_path"],
                },
            )

        # Apply material if specified
        if spec.get("material_path") and actor_path:
            await conn.execute(
                "actor.set_property",
                {
                    "actor_path": actor_path,
                    "property_name": "Material",
                    "property_value": spec["material_path"],
                },
            )

        results.append({"index": i, "label": label, **r.data})

    return {
        "actors_created": len(results),
        "errors": len(errors),
        "results": results,
        "error_details": errors,
    }


# ---------------------------------------------------------------------------
# setup_cinematic
# ---------------------------------------------------------------------------


@mcp.tool(
    title="Setup Cinematic",
    tags={"workflow"},
    annotations=ToolAnnotations(
        readOnlyHint=False,
        destructiveHint=False,
        idempotentHint=False,
        openWorldHint=False,
    ),
)
async def setup_cinematic(
    sequence_name: str,
    camera_label: str = "CineCamera",
    camera_location_x: float = 0.0,
    camera_location_y: float = -500.0,
    camera_location_z: float = 200.0,
    camera_rotation_pitch: float = -10.0,
    camera_rotation_yaw: float = 0.0,
    start_frame: int = 0,
    end_frame: int = 300,
    ctx: Context = None,
) -> dict:
    """Create a complete cinematic setup: Level Sequence + Camera + Tracks.

    Spawns a camera actor, creates a Level Sequence, adds a camera cut track,
    and configures the playback range — all in one call.

    Args:
        sequence_name: Name for the new Level Sequence asset.
        camera_label: Display name for the spawned camera.
        camera_location_x: Camera position X.
        camera_location_y: Camera position Y.
        camera_location_z: Camera position Z.
        camera_rotation_pitch: Camera pitch in degrees.
        camera_rotation_yaw: Camera yaw in degrees.
        start_frame: Sequence start frame.
        end_frame: Sequence end frame.
    """
    conn = get_conn(ctx)
    steps: list[dict] = []

    # Step 1: Spawn camera
    cam = await conn.execute(
        "actor.spawn",
        {
            "actor_class": "/Script/CinematicCamera.CineCameraActor",
            "label": camera_label,
            "location": {
                "x": camera_location_x,
                "y": camera_location_y,
                "z": camera_location_z,
            },
            "rotation": {"pitch": camera_rotation_pitch, "yaw": camera_rotation_yaw, "roll": 0},
            "scale": {"x": 1, "y": 1, "z": 1},
        },
    )
    if not cam.success:
        raise ValueError(f"Failed to spawn camera: {cam.error}")
    steps.append({"step": "spawn_camera", **cam.data})
    camera_path = cam.data.get("actor_path", "")

    # Step 2: Create Level Sequence
    seq = await conn.execute(
        "sequencer.create_sequence",
        {"sequence_name": sequence_name, "destination_folder": "/Game/Cinematics"},
    )
    if not seq.success:
        raise ValueError(f"Failed to create sequence: {seq.error}")
    steps.append({"step": "create_sequence", **seq.data})
    sequence_path = seq.data.get("path", "")

    # Step 3: Add camera cut track
    if sequence_path and camera_path:
        cut = await conn.execute(
            "sequencer.add_camera_cut",
            {"path": sequence_path, "camera_path": camera_path},
        )
        if cut.success:
            steps.append({"step": "add_camera_cut", **cut.data})

    # Step 4: Set playback range
    if sequence_path:
        rng = await conn.execute(
            "sequencer.set_range",
            {"path": sequence_path, "start_frame": start_frame, "end_frame": end_frame},
        )
        if rng.success:
            steps.append({"step": "set_range", **rng.data})

    return {"steps_completed": len(steps), "details": steps}


# ---------------------------------------------------------------------------
# create_material_library
# ---------------------------------------------------------------------------


@mcp.tool(
    title="Create Material Library",
    tags={"workflow"},
    annotations=ToolAnnotations(
        readOnlyHint=False,
        destructiveHint=False,
        idempotentHint=False,
        openWorldHint=False,
    ),
)
async def create_material_library(
    materials: list[dict[str, Any]],
    base_path: str = "/Game/Materials",
    ctx: Context = None,
) -> dict:
    """Batch-create multiple materials and material instances.

    Each entry in materials list should specify:
    - name: Material name
    - type: 'material' or 'instance'
    - parent_path: Parent material path (for instances)
    - shading_model: DefaultLit, Unlit, etc. (for base materials)
    - blend_mode: Opaque, Translucent, etc. (for base materials)
    - parameters: dict of parameter_name → value (scalars, vectors, textures)

    Args:
        materials: List of material specifications.
        base_path: Base content path for created materials.
    """
    conn = get_conn(ctx)
    results: list[dict] = []
    errors: list[dict] = []

    for i, spec in enumerate(materials):
        mat_type = spec.get("type", "material")
        name = spec.get("name", f"M_Generated_{i}")

        dest_path = spec.get("path", base_path)
        if mat_type == "instance":
            r = await conn.execute(
                "material.create_instance",
                {
                    "parent_path": spec.get("parent_path", ""),
                    "instance_name": name,
                    "dest_path": dest_path,
                },
            )
        else:
            r = await conn.execute(
                "material.create",
                {
                    "name": name,
                    "dest_path": dest_path,
                    "shading_model": spec.get("shading_model", "DefaultLit"),
                    "blend_mode": spec.get("blend_mode", "Opaque"),
                },
            )

        if not r.success:
            errors.append({"index": i, "name": name, "error": r.error})
            continue

        mat_path = r.data.get("path", "")

        # Apply parameters if specified
        for param_name, param_value in spec.get("parameters", {}).items():
            if isinstance(param_value, int | float):
                await conn.execute(
                    "material.set_scalar_parameter",
                    {"path": mat_path, "name": param_name, "value": param_value},
                )
            elif isinstance(param_value, dict):
                await conn.execute(
                    "material.set_vector_parameter",
                    {"path": mat_path, "name": param_name, "value": param_value},
                )
            elif isinstance(param_value, str) and param_value.startswith("/"):
                await conn.execute(
                    "material.set_texture_parameter",
                    {"path": mat_path, "name": param_name, "texture_path": param_value},
                )

        results.append({"index": i, "name": name, **r.data})

    return {
        "materials_created": len(results),
        "errors": len(errors),
        "results": results,
        "error_details": errors,
    }


# ---------------------------------------------------------------------------
# setup_ai_agent
# ---------------------------------------------------------------------------


@mcp.tool(
    title="Setup AI Agent",
    tags={"workflow"},
    annotations=ToolAnnotations(
        readOnlyHint=False,
        destructiveHint=False,
        idempotentHint=False,
        openWorldHint=False,
    ),
)
async def setup_ai_agent(
    agent_name: str,
    behavior_tree_name: str = "",
    blackboard_keys: list[dict[str, str]] | None = None,
    perception_senses: list[str] | None = None,
    ctx: Context = None,
) -> dict:
    """Set up a complete AI agent: Behavior Tree + Blackboard + Perception.

    Creates the BT, configures blackboard keys, and optionally sets up
    AI perception senses — all in one call.

    Args:
        agent_name: Base name for the AI agent assets.
        behavior_tree_name: Override BT name (default: BT_{agent_name}).
        blackboard_keys: List of {name, type} dicts for blackboard keys.
        perception_senses: List of senses to enable (e.g., ['Sight', 'Hearing']).
    """
    conn = get_conn(ctx)
    steps: list[dict] = []

    bt_name = behavior_tree_name or f"BT_{agent_name}"

    # Step 1: Create Behavior Tree
    bt = await conn.execute(
        "ai.create_behavior_tree",
        {"tree_name": bt_name, "destination_folder": "/Game/AI"},
    )
    if not bt.success:
        raise ValueError(f"Failed to create behavior tree: {bt.error}")
    steps.append({"step": "create_bt", **bt.data})
    bt_path = bt.data.get("path", "")

    # Step 2: Create and configure Blackboard
    bb = await conn.execute("ai.create_blackboard", {"name": f"BB_{agent_name}"})
    if bb.success:
        steps.append({"step": "create_blackboard", **bb.data})
        bb_path = bb.data.get("path", "")

        # Add blackboard keys
        for key_spec in blackboard_keys or []:
            await conn.execute(
                "ai.add_blackboard_key",
                {
                    "path": bb_path,
                    "key_name": key_spec.get("name", ""),
                    "key_type": key_spec.get("type", "Object"),
                },
            )

        # Link BB to BT
        if bt_path:
            await conn.execute("ai.set_blackboard", {"path": bt_path, "blackboard_path": bb_path})

    # Step 3: Configure perception
    if perception_senses:
        for sense in perception_senses:
            r = await conn.execute(
                "ai.configure_perception",
                {"agent_name": agent_name, "sense": sense},
            )
            if r.success:
                steps.append({"step": f"add_sense_{sense}", **r.data})

    return {"steps_completed": len(steps), "details": steps}


# ---------------------------------------------------------------------------
# render_cinematic
# ---------------------------------------------------------------------------


@mcp.tool(
    title="Render Cinematic",
    tags={"workflow"},
    annotations=ToolAnnotations(
        readOnlyHint=False,
        destructiveHint=False,
        idempotentHint=False,
        openWorldHint=False,
    ),
)
async def render_cinematic(
    sequence_path: str,
    output_dir: str,
    output_format: Literal["EXR", "PNG", "JPEG"] = "EXR",
    resolution_x: int = 1920,
    resolution_y: int = 1080,
    passes: list[str] | None = None,
    ctx: Context = None,
) -> dict:
    """Full render pipeline: create MRQ config, add passes, and submit render.

    Args:
        sequence_path: Level Sequence to render.
        output_dir: Output directory for rendered frames.
        output_format: Image format.
        resolution_x: Output width.
        resolution_y: Output height.
        passes: Render passes to add (default: ['beauty']).
    """
    conn = get_conn(ctx)
    steps: list[dict] = []
    render_passes = passes or ["beauty"]

    # Step 1: Create render queue
    queue = await conn.execute("mrq.create_queue")
    if not queue.success:
        raise ValueError(f"Failed to create render queue: {queue.error}")
    steps.append({"step": "create_queue", **queue.data})
    queue_path = queue.data.get("path", "")

    # Step 2: Add job
    job = await conn.execute(
        "mrq.add_job",
        {"queue_path": queue_path, "sequence_path": sequence_path, "level_path": ""},
    )
    if job.success:
        steps.append({"step": "add_job", **job.data})

    # Step 3: Add render passes
    for pass_type in render_passes:
        r = await conn.execute("mrq.add_pass", {"queue_path": queue_path, "pass_type": pass_type})
        if r.success:
            steps.append({"step": f"add_pass_{pass_type}", **r.data})

    # Step 4: Configure output
    cfg = await conn.execute(
        "mrq.configure",
        {
            "queue_path": queue_path,
            "output_dir": output_dir,
            "output_format": output_format,
            "resolution": {"x": resolution_x, "y": resolution_y},
        },
    )
    if cfg.success:
        steps.append({"step": "configure_output", **cfg.data})

    # Step 5: Submit render
    submit = await conn.execute("mrq.submit_job", {"queue_path": queue_path}, timeout=300.0)
    if submit.success:
        steps.append({"step": "submit_render", **submit.data})

    return {"steps_completed": len(steps), "details": steps}


# ---------------------------------------------------------------------------
# performance_audit
# ---------------------------------------------------------------------------


@mcp.tool(
    title="Performance Audit",
    tags={"workflow"},
    annotations=ToolAnnotations(
        readOnlyHint=True,
        destructiveHint=False,
        idempotentHint=True,
        openWorldHint=False,
    ),
)
async def performance_audit(
    duration_seconds: float = 5.0,
    ctx: Context = None,
) -> dict:
    """Run a comprehensive performance audit: frame time + GPU + memory + bottleneck analysis.

    Collects frame stats, GPU stats, and memory stats in parallel, then
    provides a summary with identified bottlenecks.

    Args:
        duration_seconds: Duration to sample frame stats.
    """
    conn = get_conn(ctx)

    frame = await conn.execute("profiling.get_frame_stats", {"duration": duration_seconds})
    gpu = await conn.execute("profiling.get_gpu_stats")
    memory = await conn.execute("profiling.get_memory_stats")

    return {
        "frame_stats": frame.data if frame.success else {"error": frame.error},
        "gpu_stats": gpu.data if gpu.success else {"error": gpu.error},
        "memory_stats": memory.data if memory.success else {"error": memory.error},
    }


# ---------------------------------------------------------------------------
# create_interactive_object
# ---------------------------------------------------------------------------


@mcp.tool(
    title="Create Interactive Object",
    tags={"workflow"},
    annotations=ToolAnnotations(
        readOnlyHint=False,
        destructiveHint=False,
        idempotentHint=False,
        openWorldHint=False,
    ),
)
async def create_interactive_object(
    name: str,
    mesh_path: str = "",
    collision_profile: str = "OverlapAll",
    has_physics: bool = False,
    parent_class: str = "Actor",
    ctx: Context = None,
) -> dict:
    """Create a Blueprint with interaction: mesh + collision + optional physics.

    Builds a complete interactive object Blueprint with components pre-configured.

    Args:
        name: Blueprint name.
        mesh_path: Static mesh to use.
        collision_profile: Collision preset name.
        has_physics: Enable physics simulation on the mesh.
        parent_class: Blueprint parent class.
    """
    conn = get_conn(ctx)
    steps: list[dict] = []

    # Step 1: Create Blueprint
    bp = await conn.execute("blueprint.create", {"name": name, "parent_class": parent_class})
    if not bp.success:
        raise ValueError(f"Failed to create blueprint: {bp.error}")
    steps.append({"step": "create_bp", **bp.data})
    bp_path = bp.data.get("path", "")

    # Step 2: Add mesh component
    if mesh_path and bp_path:
        mesh = await conn.execute(
            "blueprint.add_component",
            {
                "path": bp_path,
                "component_class": "StaticMeshComponent",
                "component_name": "Mesh",
            },
        )
        if mesh.success:
            steps.append({"step": "add_mesh", **mesh.data})

    # Step 3: Add collision component
    if bp_path:
        col = await conn.execute(
            "blueprint.add_component",
            {
                "path": bp_path,
                "component_class": "BoxComponent",
                "component_name": "CollisionBox",
                "collision_profile": collision_profile,
            },
        )
        if col.success:
            steps.append({"step": "add_collision", **col.data})

    # Step 4: Enable physics if requested
    if has_physics and bp_path:
        phys = await conn.execute(
            "blueprint.add_component",
            {
                "path": bp_path,
                "component_class": "PhysicsComponent",
                "component_name": "Physics",
                "simulate_physics": True,
            },
        )
        if phys.success:
            steps.append({"step": "add_physics", **phys.data})

    # Step 5: Compile
    if bp_path:
        comp = await conn.execute("blueprint.compile", {"path": bp_path}, timeout=120.0)
        if comp.success:
            steps.append({"step": "compile", **comp.data})

    return {"steps_completed": len(steps), "blueprint_path": bp_path, "details": steps}


# ---------------------------------------------------------------------------
# batch_scene_operation
# ---------------------------------------------------------------------------


@mcp.tool(
    title="Batch Scene Operation",
    tags={"workflow"},
    annotations=ToolAnnotations(
        readOnlyHint=False,
        destructiveHint=False,
        idempotentHint=False,
        openWorldHint=False,
    ),
)
async def batch_scene_operation(
    operations: list[dict[str, Any]],
    ctx: Context = None,
) -> dict:
    """Execute an arbitrary batch of sub-operations in one tool call.

    Each operation dict should have:
    - command: The command type (e.g., 'actor.spawn', 'actor.set_transform')
    - params: Command parameters dict

    Executes operations sequentially; partial failure is possible.

    Args:
        operations: List of {command, params} dicts to execute.
    """
    conn = get_conn(ctx)
    results: list[dict] = []
    errors: list[dict] = []

    for i, op in enumerate(operations):
        command = op.get("command", "")
        params = op.get("params", {})
        if not command:
            errors.append({"index": i, "error": "Missing 'command' key"})
            continue

        r = await conn.execute(command, params)
        if r.success:
            results.append({"index": i, "command": command, **r.data})
        else:
            errors.append({"index": i, "command": command, "error": r.error})

    return {
        "success_count": len(results),
        "error_count": len(errors),
        "results": results,
        "errors": errors,
    }
