"""Tier 2 — Deferred subsystem tools (~30 tools).

Loaded on-demand via Tool Search when the LLM needs them.
Each subsystem gets 1-4 consolidated tools with descriptive docstrings.
All command names match the C++ NexusCommandHandler dispatch table exactly.
"""

from __future__ import annotations

from typing import Any, Literal

from fastmcp import Context
from mcp.types import ToolAnnotations

from nexus.server import get_conn, mcp

# ===================================================================
# Animation subsystem
# C++ commands: get_anim_blueprint_info, list_anim_sequences,
#   get_skeleton_info, list_anim_notifies, get_retarget_info,
#   create_anim_montage, set_anim_blueprint, create_blend_space,
#   set_ik_settings, create_control_rig, add_anim_notify, apply_retarget,
#   add_blend_space_sample, get_blend_space_samples,
#   update_blend_space_sample, remove_blend_space_sample
# ===================================================================


@mcp.tool(
    title="Manage Animation Blueprint",
    tags={"animation"},
    annotations=ToolAnnotations(
        readOnlyHint=False,
        destructiveHint=False,
        idempotentHint=False,
        openWorldHint=False,
    ),
)
async def manage_anim_blueprint(
    action: Literal["get_info", "set_blueprint", "create_montage", "list_sequences"],
    path: str = "",
    name: str = "",
    skeleton_path: str = "",
    anim_blueprint_path: str = "",
    ctx: Context = None,
) -> dict:
    """Query and configure Animation Blueprints, montages, and sequences.

    Use search_assets with asset_class='AnimBlueprint' to find valid paths.

    Args:
        action: get_info, set_blueprint, create_montage, or list_sequences.
        path: Asset path for the target (AnimBP, skeleton, etc.).
        name: Name for new montage (action='create_montage').
        skeleton_path: Skeleton asset path (action='create_montage').
        anim_blueprint_path: AnimBP to assign (action='set_blueprint').
    """
    conn = get_conn(ctx)
    if action == "get_info":
        result = await conn.execute("animation.get_anim_blueprint_info", {"path": path})
    elif action == "set_blueprint":
        result = await conn.execute(
            "animation.set_anim_blueprint",
            {"path": path, "anim_blueprint_path": anim_blueprint_path},
        )
    elif action == "create_montage":
        result = await conn.execute(
            "animation.create_anim_montage",
            {"name": name, "skeleton_path": skeleton_path},
        )
    elif action == "list_sequences":
        result = await conn.execute("animation.list_anim_sequences", {"path": path})
    else:
        raise ValueError(f"Unknown action: {action}")
    if not result.success:
        raise ValueError(f"{result.error}.")
    return result.data


@mcp.tool(
    title="Manage Blend Space",
    tags={"animation"},
    annotations=ToolAnnotations(
        readOnlyHint=False,
        destructiveHint=False,
        idempotentHint=False,
        openWorldHint=False,
    ),
)
async def manage_blend_space(
    action: Literal["create", "add_sample", "get_samples", "update_sample", "remove_sample"],
    path: str = "",
    name: str = "",
    skeleton_path: str = "",
    sample_animation: str = "",
    sample_x: float = 0.0,
    sample_y: float = 0.0,
    sample_index: int = 0,
    ctx: Context = None,
) -> dict:
    """Create and configure Blend Spaces for animation blending.

    Args:
        action: create, add_sample, get_samples, update_sample, or remove_sample.
        path: Existing BlendSpace path.
        name: Name for new BlendSpace (action='create').
        skeleton_path: Skeleton asset (action='create').
        sample_animation: Animation asset path for sample.
        sample_x: X-axis value for sample.
        sample_y: Y-axis value for sample.
        sample_index: Index of sample to update/remove.
    """
    conn = get_conn(ctx)
    if action == "create":
        result = await conn.execute(
            "animation.create_blend_space",
            {"name": name, "skeleton_path": skeleton_path},
        )
    elif action == "add_sample":
        result = await conn.execute(
            "animation.add_blend_space_sample",
            {"path": path, "animation": sample_animation, "x": sample_x, "y": sample_y},
        )
    elif action == "get_samples":
        result = await conn.execute("animation.get_blend_space_samples", {"path": path})
    elif action == "update_sample":
        result = await conn.execute(
            "animation.update_blend_space_sample",
            {"path": path, "index": sample_index, "x": sample_x, "y": sample_y},
        )
    elif action == "remove_sample":
        result = await conn.execute(
            "animation.remove_blend_space_sample", {"path": path, "index": sample_index}
        )
    else:
        raise ValueError(f"Unknown action: {action}")
    if not result.success:
        raise ValueError(f"{result.error}.")
    return result.data


# ===================================================================
# Audio subsystem
# C++ commands: spawn_sound, create_sound_cue, create_metasound,
#   set_sound_properties, set_attenuation, set_reverb_settings,
#   list_sound_classes, get_audio_info, add_sound_cue_node, ...
# ===================================================================


@mcp.tool(
    title="Manage Audio",
    tags={"audio"},
    annotations=ToolAnnotations(
        readOnlyHint=False,
        destructiveHint=False,
        idempotentHint=False,
        openWorldHint=False,
    ),
)
async def manage_sound(
    action: Literal["spawn", "set_properties", "set_attenuation", "get_info"],
    actor_path: str = "",
    sound_path: str = "",
    location_x: float = 0.0,
    location_y: float = 0.0,
    location_z: float = 0.0,
    volume: float = 1.0,
    pitch: float = 1.0,
    attenuation_radius: float = 1000.0,
    auto_activate: bool = True,
    ctx: Context = None,
) -> dict:
    """Spawn and configure sound actors and attenuation.

    Args:
        action: spawn, set_properties, set_attenuation, or get_info.
        actor_path: Existing sound actor path.
        sound_path: Sound Wave or Cue asset path.
        location_x: Spawn position X.
        location_y: Spawn position Y.
        location_z: Spawn position Z.
        volume: Volume multiplier (0-1).
        pitch: Pitch multiplier.
        attenuation_radius: Sound falloff radius in units.
        auto_activate: Play sound automatically.
    """
    conn = get_conn(ctx)
    if action == "spawn":
        result = await conn.execute(
            "audio.spawn_sound",
            {
                "sound_path": sound_path,
                "location": {"x": location_x, "y": location_y, "z": location_z},
                "volume": volume,
                "auto_activate": auto_activate,
            },
        )
    elif action == "set_properties":
        result = await conn.execute(
            "audio.set_sound_properties",
            {"actor_path": actor_path, "volume": volume, "pitch": pitch},
        )
    elif action == "set_attenuation":
        result = await conn.execute(
            "audio.set_attenuation",
            {"actor_path": actor_path, "radius": attenuation_radius},
        )
    elif action == "get_info":
        result = await conn.execute("audio.get_audio_info", {"actor_path": actor_path})
    else:
        raise ValueError(f"Unknown action: {action}")
    if not result.success:
        raise ValueError(f"{result.error}.")
    return result.data


# ===================================================================
# Sequencer (Cinematics) subsystem
# C++ commands: create_sequence, open_sequence, get_sequence_info,
#   list_sequence_tracks, add_actor_track, add_transform_keyframe,
#   add_float_keyframe, add_camera_cut, add_subsequence,
#   set_sequence_range, remove_track, export_sequence
# ===================================================================


@mcp.tool(
    title="Manage Sequencer",
    tags={"sequencer"},
    annotations=ToolAnnotations(
        readOnlyHint=False,
        destructiveHint=False,
        idempotentHint=False,
        openWorldHint=False,
    ),
)
async def manage_level_sequence(
    action: Literal["create", "add_track", "add_camera_cut", "set_range", "get_info"],
    path: str = "",
    name: str = "",
    actor_path: str = "",
    track_type: str = "",
    start_frame: int = 0,
    end_frame: int = 300,
    ctx: Context = None,
) -> dict:
    """Create and configure Level Sequences for cinematics.

    Args:
        action: create, add_track, add_camera_cut, set_range, or get_info.
        path: Existing sequence path.
        name: Name for new sequence.
        actor_path: Actor to bind track to.
        track_type: Track type (Transform, Float, Bool, Event, CameraCut).
        start_frame: Playback start frame.
        end_frame: Playback end frame.
    """
    conn = get_conn(ctx)
    if action == "create":
        result = await conn.execute(
            "sequencer.create_sequence",
            {"sequence_name": name, "destination_folder": "/Game/Cinematics"},
        )
    elif action == "add_track":
        result = await conn.execute(
            "sequencer.add_actor_track",
            {"path": path, "actor_path": actor_path, "track_type": track_type},
        )
    elif action == "add_camera_cut":
        result = await conn.execute(
            "sequencer.add_camera_cut", {"path": path, "camera_path": actor_path}
        )
    elif action == "set_range":
        result = await conn.execute(
            "sequencer.set_sequence_range",
            {"path": path, "start_frame": start_frame, "end_frame": end_frame},
        )
    elif action == "get_info":
        result = await conn.execute("sequencer.get_sequence_info", {"path": path})
    else:
        raise ValueError(f"Unknown action: {action}")
    if not result.success:
        raise ValueError(f"{result.error}.")
    return result.data


# ===================================================================
# Niagara VFX subsystem
# C++ commands: create_niagara_system, create_niagara_emitter,
#   set_niagara_parameter, set_niagara_variable,
#   activate_niagara_system, spawn_niagara_at_location,
#   get_niagara_info, list_niagara_modules, add_module, ...
# ===================================================================


@mcp.tool(
    title="Manage Niagara VFX",
    tags={"niagara"},
    annotations=ToolAnnotations(
        readOnlyHint=False,
        destructiveHint=False,
        idempotentHint=False,
        openWorldHint=False,
    ),
)
async def manage_niagara_system(
    action: Literal["create", "add_emitter", "set_parameter", "spawn", "get_info"],
    path: str = "",
    name: str = "",
    emitter_name: str = "",
    parameter_name: str = "",
    parameter_value: str = "",
    location_x: float = 0.0,
    location_y: float = 0.0,
    location_z: float = 0.0,
    ctx: Context = None,
) -> dict:
    """Create and configure Niagara particle systems.

    Args:
        action: create, add_emitter, set_parameter, spawn, or get_info.
        path: Existing Niagara system path.
        name: Name for new system.
        emitter_name: Emitter template or name.
        parameter_name: User parameter name (action='set_parameter').
        parameter_value: Parameter value as string.
        location_x: Spawn position X.
        location_y: Spawn position Y.
        location_z: Spawn position Z.
    """
    conn = get_conn(ctx)
    if action == "create":
        result = await conn.execute(
            "niagara.create_niagara_system",
            {"system_name": name, "destination_folder": "/Game/VFX"},
        )
    elif action == "add_emitter":
        result = await conn.execute(
            "niagara.create_niagara_emitter",
            {"path": path, "emitter_name": emitter_name},
        )
    elif action == "set_parameter":
        result = await conn.execute(
            "niagara.set_niagara_parameter",
            {"path": path, "name": parameter_name, "value": parameter_value},
        )
    elif action == "spawn":
        result = await conn.execute(
            "niagara.spawn_niagara_at_location",
            {
                "path": path,
                "location": {"x": location_x, "y": location_y, "z": location_z},
            },
        )
    elif action == "get_info":
        result = await conn.execute("niagara.get_niagara_info", {"path": path})
    else:
        raise ValueError(f"Unknown action: {action}")
    if not result.success:
        raise ValueError(f"{result.error}.")
    return result.data


# ===================================================================
# AI subsystem
# C++ commands: create_behavior_tree, create_blackboard,
#   create_eqs_query, create_state_tree, set_blackboard_key,
#   set_ai_perception, assign_behavior_tree, get_ai_controller_info,
#   list_behavior_trees, run_eqs_query, add_bt_node, get_bt_nodes, ...
# ===================================================================


@mcp.tool(
    title="Manage Behavior Tree",
    tags={"ai"},
    annotations=ToolAnnotations(
        readOnlyHint=False,
        destructiveHint=False,
        idempotentHint=False,
        openWorldHint=False,
    ),
)
async def manage_behavior_tree(
    action: Literal["create", "add_node", "get_nodes", "assign", "list"],
    path: str = "",
    name: str = "",
    node_type: str = "",
    node_name: str = "",
    parent_node: str = "",
    actor_path: str = "",
    ctx: Context = None,
) -> dict:
    """Create and configure Behavior Trees for AI.

    Args:
        action: create, add_node, get_nodes, assign, or list.
        path: Existing BT path.
        name: Name for new BT.
        node_type: Node class (Selector, Sequence, BTTask, BTDecorator, BTService).
        node_name: Node display name.
        parent_node: Parent node to attach to.
        actor_path: AI controller actor (action='assign').
    """
    conn = get_conn(ctx)
    if action == "create":
        result = await conn.execute(
            "ai.create_behavior_tree",
            {"tree_name": name, "destination_folder": "/Game/AI"},
        )
    elif action == "add_node":
        result = await conn.execute(
            "ai.add_bt_node",
            {
                "path": path,
                "node_type": node_type,
                "node_name": node_name,
                "parent_node": parent_node,
            },
        )
    elif action == "get_nodes":
        result = await conn.execute("ai.get_bt_nodes", {"path": path})
    elif action == "assign":
        result = await conn.execute(
            "ai.assign_behavior_tree",
            {"path": path, "actor_path": actor_path},
        )
    elif action == "list":
        result = await conn.execute("ai.list_behavior_trees")
    else:
        raise ValueError(f"Unknown action: {action}")
    if not result.success:
        raise ValueError(f"{result.error}.")
    return result.data


@mcp.tool(
    title="Manage Environment Query",
    tags={"ai"},
    annotations=ToolAnnotations(
        readOnlyHint=False,
        destructiveHint=False,
        idempotentHint=False,
        openWorldHint=False,
    ),
)
async def manage_eqs(
    action: Literal["create", "add_generator", "add_test", "get_nodes", "run"],
    path: str = "",
    name: str = "",
    generator_type: str = "",
    test_type: str = "",
    test_params: dict[str, Any] | None = None,
    ctx: Context = None,
) -> dict:
    """Create and configure Environment Query System (EQS) queries.

    Args:
        action: create, add_generator, add_test, get_nodes, or run.
        path: Existing EQS query path.
        name: Name for new EQS query.
        generator_type: Generator class name.
        test_type: Test class name.
        test_params: Test configuration parameters.
    """
    conn = get_conn(ctx)
    if action == "create":
        result = await conn.execute("ai.create_eqs_query", {"name": name})
    elif action == "add_generator":
        result = await conn.execute(
            "ai.add_eqs_generator", {"path": path, "generator_type": generator_type}
        )
    elif action == "add_test":
        result = await conn.execute(
            "ai.add_eqs_test",
            {"path": path, "test_type": test_type, "params": test_params or {}},
        )
    elif action == "get_nodes":
        result = await conn.execute("ai.get_eqs_nodes", {"path": path})
    elif action == "run":
        result = await conn.execute("ai.run_eqs_query", {"path": path})
    else:
        raise ValueError(f"Unknown action: {action}")
    if not result.success:
        raise ValueError(f"{result.error}.")
    return result.data


# ===================================================================
# Physics subsystem
# C++ commands: set_collision_profile, enable_physics_simulation,
#   set_physics_properties, set_collision_response,
#   add_physics_constraint, create_physics_asset,
#   apply_force, get_physics_info
# ===================================================================


@mcp.tool(
    title="Manage Physics",
    tags={"physics"},
    annotations=ToolAnnotations(
        readOnlyHint=False,
        destructiveHint=False,
        idempotentHint=False,
        openWorldHint=False,
    ),
)
async def manage_collision(
    action: Literal["set_profile", "set_response", "enable_physics", "get_info"],
    actor_path: str = "",
    collision_profile: str = "",
    channel: str = "",
    response: Literal["Block", "Overlap", "Ignore"] = "Block",
    simulate_physics: bool = False,
    ctx: Context = None,
) -> dict:
    """Configure collision profiles, channel responses, and physics simulation.

    Args:
        action: set_profile, set_response, enable_physics, or get_info.
        actor_path: Target actor path.
        collision_profile: Preset collision profile name.
        channel: Collision channel name (action='set_response').
        response: Collision response type.
        simulate_physics: Enable/disable physics simulation.
    """
    conn = get_conn(ctx)
    if action == "set_profile":
        result = await conn.execute(
            "physics.set_collision_profile",
            {"actor_path": actor_path, "profile": collision_profile},
        )
    elif action == "set_response":
        result = await conn.execute(
            "physics.set_collision_response",
            {"actor_path": actor_path, "channel": channel, "response": response},
        )
    elif action == "enable_physics":
        result = await conn.execute(
            "physics.enable_physics_simulation",
            {"actor_path": actor_path, "simulate": simulate_physics},
        )
    elif action == "get_info":
        result = await conn.execute("physics.get_physics_info", {"actor_path": actor_path})
    else:
        raise ValueError(f"Unknown action: {action}")
    if not result.success:
        raise ValueError(f"{result.error}.")
    return result.data


# ===================================================================
# UI / UMG subsystem
# C++ commands: create_widget_blueprint, create_widget_animation,
#   add_widget_to_viewport, remove_widget_from_viewport,
#   set_widget_property, get_widget_info, list_widget_bindings
# ===================================================================


@mcp.tool(
    title="Manage UMG Widget",
    tags={"ui"},
    annotations=ToolAnnotations(
        readOnlyHint=False,
        destructiveHint=False,
        idempotentHint=False,
        openWorldHint=False,
    ),
)
async def manage_widget(
    action: Literal[
        "create", "set_property", "get_info", "add_to_viewport", "remove_from_viewport"
    ],
    path: str = "",
    name: str = "",
    property_name: str = "",
    property_value: str = "",
    ctx: Context = None,
) -> dict:
    """Create and configure UMG Widget Blueprints.

    Args:
        action: create, set_property, get_info, add_to_viewport, or remove_from_viewport.
        path: Existing widget path.
        name: Name for new widget (action='create').
        property_name: Property to set.
        property_value: Property value as string.
    """
    conn = get_conn(ctx)
    if action == "create":
        result = await conn.execute(
            "ui.create_widget_blueprint",
            {"widget_name": name, "destination_folder": "/Game/UI"},
        )
    elif action == "set_property":
        result = await conn.execute(
            "ui.set_widget_property",
            {"path": path, "property_name": property_name, "property_value": property_value},
        )
    elif action == "get_info":
        result = await conn.execute("ui.get_widget_info", {"path": path})
    elif action == "add_to_viewport":
        result = await conn.execute("ui.add_widget_to_viewport", {"path": path})
    elif action == "remove_from_viewport":
        result = await conn.execute("ui.remove_widget_from_viewport", {"path": path})
    else:
        raise ValueError(f"Unknown action: {action}")
    if not result.success:
        raise ValueError(f"{result.error}.")
    return result.data


# ===================================================================
# Movie Render Queue subsystem
# C++ commands: create_render_queue, add_render_job, add_render_pass,
#   render_queue, cancel_render, get_render_status, list_render_jobs,
#   remove_render_job, set_output_settings, add_beauty_pass,
#   add_gbuffer_passes, configure_antialiasing
# ===================================================================


@mcp.tool(
    title="Manage Movie Render Queue",
    tags={"mrq"},
    annotations=ToolAnnotations(
        readOnlyHint=False,
        destructiveHint=False,
        idempotentHint=False,
        openWorldHint=False,
    ),
)
async def manage_render_queue(
    action: Literal[
        "create", "add_job", "add_pass", "set_output", "render", "get_status", "cancel"
    ],
    queue_path: str = "",
    sequence_path: str = "",
    level_path: str = "",
    pass_type: Literal["beauty", "gbuffer"] = "beauty",
    output_dir: str = "",
    output_format: Literal["EXR", "PNG", "JPEG"] = "EXR",
    resolution_x: int = 1920,
    resolution_y: int = 1080,
    ctx: Context = None,
) -> dict:
    """Configure and execute Movie Render Queue renders.

    Args:
        action: create, add_job, add_pass, set_output, render, get_status, or cancel.
        queue_path: Existing MRQ path.
        sequence_path: Level Sequence to render (action='add_job').
        level_path: Level to render in (action='add_job').
        pass_type: beauty or gbuffer (action='add_pass').
        output_dir: Output directory path.
        output_format: Output image format.
        resolution_x: Output width.
        resolution_y: Output height.
    """
    conn = get_conn(ctx)
    if action == "create":
        result = await conn.execute("mrq.create_render_queue")
    elif action == "add_job":
        result = await conn.execute(
            "mrq.add_render_job",
            {
                "queue_path": queue_path,
                "sequence_path": sequence_path,
                "level_path": level_path,
            },
        )
    elif action == "add_pass":
        if pass_type == "gbuffer":
            result = await conn.execute("mrq.add_gbuffer_passes", {"queue_path": queue_path})
        else:
            result = await conn.execute("mrq.add_beauty_pass", {"queue_path": queue_path})
    elif action == "set_output":
        result = await conn.execute(
            "mrq.set_output_settings",
            {
                "queue_path": queue_path,
                "output_dir": output_dir,
                "output_format": output_format,
                "resolution_x": resolution_x,
                "resolution_y": resolution_y,
            },
        )
    elif action == "render":
        result = await conn.execute("mrq.render_queue", {"queue_path": queue_path}, timeout=300.0)
    elif action == "get_status":
        result = await conn.execute("mrq.get_render_status", {"queue_path": queue_path})
    elif action == "cancel":
        result = await conn.execute("mrq.cancel_render", {"queue_path": queue_path})
    else:
        raise ValueError(f"Unknown action: {action}")
    if not result.success:
        raise ValueError(f"{result.error}.")
    return result.data


# ===================================================================
# PCG subsystem
# C++ commands: create_pcg_graph, add_pcg_node, connect_pcg_nodes,
#   set_pcg_settings, execute_pcg_graph, get_pcg_info
# ===================================================================


@mcp.tool(
    title="Manage PCG",
    tags={"pcg"},
    annotations=ToolAnnotations(
        readOnlyHint=False,
        destructiveHint=False,
        idempotentHint=False,
        openWorldHint=False,
    ),
)
async def manage_pcg_graph(
    action: Literal["create", "execute", "set_settings", "get_info"],
    path: str = "",
    name: str = "",
    settings: dict[str, Any] | None = None,
    actor_path: str = "",
    ctx: Context = None,
) -> dict:
    """Create and execute Procedural Content Generation (PCG) graphs.

    Args:
        action: create, execute, set_settings, or get_info.
        path: Existing PCG graph path.
        name: Name for new PCG graph.
        settings: PCG settings dict (action='set_settings').
        actor_path: PCG actor in level (action='execute').
    """
    conn = get_conn(ctx)
    if action == "create":
        result = await conn.execute("pcg.create_pcg_graph", {"name": name})
    elif action == "execute":
        result = await conn.execute(
            "pcg.execute_pcg_graph",
            {"path": path, "actor_path": actor_path},
            timeout=300.0,
        )
    elif action == "set_settings":
        result = await conn.execute(
            "pcg.set_pcg_settings", {"path": path, "settings": settings or {}}
        )
    elif action == "get_info":
        result = await conn.execute("pcg.get_pcg_info", {"path": path})
    else:
        raise ValueError(f"Unknown action: {action}")
    if not result.success:
        raise ValueError(f"{result.error}.")
    return result.data


# ===================================================================
# Landscape subsystem
# C++ commands: create_landscape, sculpt_landscape,
#   paint_landscape_layer, add_foliage_type, paint_foliage,
#   remove_foliage, import_heightmap, export_heightmap,
#   get_landscape_info
# ===================================================================


@mcp.tool(
    title="Manage Landscape",
    tags={"landscape"},
    annotations=ToolAnnotations(
        readOnlyHint=False,
        destructiveHint=False,
        idempotentHint=False,
        openWorldHint=False,
    ),
)
async def manage_terrain(
    action: Literal["create", "sculpt", "paint_layer", "import_heightmap", "get_info"],
    size_x: int = 505,
    size_y: int = 505,
    sections_per_component: int = 1,
    heightmap_path: str = "",
    layer_name: str = "",
    material_path: str = "",
    actor_path: str = "",
    ctx: Context = None,
) -> dict:
    """Create and modify landscape terrain.

    Args:
        action: create, sculpt, paint_layer, import_heightmap, or get_info.
        size_x: Landscape resolution X.
        size_y: Landscape resolution Y.
        sections_per_component: Sections per component (1 or 2).
        heightmap_path: External heightmap file path.
        layer_name: Landscape layer name (action='paint_layer').
        material_path: Material to assign to landscape/layer.
        actor_path: Existing landscape actor path.
    """
    conn = get_conn(ctx)
    if action == "create":
        result = await conn.execute(
            "landscape.create_landscape",
            {
                "size_x": size_x,
                "size_y": size_y,
                "sections_per_component": sections_per_component,
                "material_path": material_path,
            },
            timeout=300.0,
        )
    elif action == "sculpt":
        result = await conn.execute("landscape.sculpt_landscape", {"actor_path": actor_path})
    elif action == "paint_layer":
        result = await conn.execute(
            "landscape.paint_landscape_layer",
            {"actor_path": actor_path, "layer_name": layer_name, "material_path": material_path},
        )
    elif action == "import_heightmap":
        result = await conn.execute(
            "landscape.import_heightmap",
            {"actor_path": actor_path, "heightmap_path": heightmap_path},
            timeout=300.0,
        )
    elif action == "get_info":
        result = await conn.execute("landscape.get_landscape_info", {"actor_path": actor_path})
    else:
        raise ValueError(f"Unknown action: {action}")
    if not result.success:
        raise ValueError(f"{result.error}.")
    return result.data


@mcp.tool(
    title="Manage Landscape Layer",
    tags={"landscape"},
    annotations=ToolAnnotations(
        readOnlyHint=False,
        destructiveHint=False,
        idempotentHint=False,
        openWorldHint=False,
    ),
)
async def manage_foliage(
    action: Literal["add_type", "paint", "remove", "get_info"],
    foliage_type_path: str = "",
    mesh_path: str = "",
    density: float = 100.0,
    scale_min: float = 0.8,
    scale_max: float = 1.2,
    actor_path: str = "",
    ctx: Context = None,
) -> dict:
    """Manage foliage painting and instanced foliage types.

    Args:
        action: add_type, paint, remove, or get_info.
        foliage_type_path: Foliage type asset path.
        mesh_path: Static mesh for new foliage type.
        density: Painting density (instances per 1000 units).
        scale_min: Minimum random scale.
        scale_max: Maximum random scale.
        actor_path: Landscape actor path (action='get_info').
    """
    conn = get_conn(ctx)
    if action == "add_type":
        result = await conn.execute("landscape.add_foliage_type", {"mesh_path": mesh_path})
    elif action == "paint":
        result = await conn.execute(
            "landscape.paint_foliage",
            {
                "type_path": foliage_type_path,
                "density": density,
                "scale_min": scale_min,
                "scale_max": scale_max,
            },
        )
    elif action == "remove":
        result = await conn.execute("landscape.remove_foliage", {"type_path": foliage_type_path})
    elif action == "get_info":
        result = await conn.execute("landscape.get_landscape_info", {"actor_path": actor_path})
    else:
        raise ValueError(f"Unknown action: {action}")
    if not result.success:
        raise ValueError(f"{result.error}.")
    return result.data


# ===================================================================
# Enhanced Input subsystem
# C++ commands: create_input_action, create_mapping_context,
#   add_action_mapping, set_trigger, set_modifier, list_input_actions
# ===================================================================


@mcp.tool(
    title="Manage Input",
    tags={"input"},
    annotations=ToolAnnotations(
        readOnlyHint=False,
        destructiveHint=False,
        idempotentHint=False,
        openWorldHint=False,
    ),
)
async def manage_input_actions(
    action: Literal["create_action", "create_mapping", "add_mapping", "list"],
    action_name: str = "",
    mapping_name: str = "",
    key: str = "",
    modifiers: list[str] | None = None,
    triggers: list[str] | None = None,
    ctx: Context = None,
) -> dict:
    """Configure Enhanced Input actions and mappings.

    Args:
        action: create_action, create_mapping, add_mapping, or list.
        action_name: Input Action asset name.
        mapping_name: Input Mapping Context name.
        key: Key binding (e.g., 'W', 'SpaceBar', 'LeftMouseButton').
        modifiers: Input modifiers (e.g., ['Negate', 'Swizzle']).
        triggers: Input triggers (e.g., ['Pressed', 'Released']).
    """
    conn = get_conn(ctx)
    if action == "create_action":
        result = await conn.execute("input.create_input_action", {"action_name": action_name})
    elif action == "create_mapping":
        result = await conn.execute("input.create_mapping_context", {"name": mapping_name})
    elif action == "add_mapping":
        result = await conn.execute(
            "input.add_action_mapping",
            {
                "action_name": action_name,
                "mapping_name": mapping_name,
                "key": key,
                "modifiers": modifiers or [],
                "triggers": triggers or [],
            },
        )
    elif action == "list":
        result = await conn.execute("input.list_input_actions")
    else:
        raise ValueError(f"Unknown action: {action}")
    if not result.success:
        raise ValueError(f"{result.error}.")
    return result.data


# ===================================================================
# Networking subsystem
# C++ commands: set_replication, set_net_role, add_rpc,
#   set_net_relevancy, get_replication_info, list_replicated_properties
# ===================================================================


@mcp.tool(
    title="Manage Networking",
    tags={"networking"},
    annotations=ToolAnnotations(
        readOnlyHint=False,
        destructiveHint=False,
        idempotentHint=False,
        openWorldHint=False,
    ),
)
async def manage_replication(
    action: Literal["set_replication", "add_rpc", "set_relevancy", "get_info"],
    actor_path: str = "",
    blueprint_path: str = "",
    property_name: str = "",
    replicated: bool = True,
    rpc_name: str = "",
    rpc_type: Literal["Server", "Client", "NetMulticast"] = "Server",
    ctx: Context = None,
) -> dict:
    """Configure actor replication and RPCs for multiplayer.

    Args:
        action: set_replication, add_rpc, set_relevancy, or get_info.
        actor_path: Target actor path.
        blueprint_path: Blueprint to modify.
        property_name: Property to mark as replicated.
        replicated: Enable/disable replication.
        rpc_name: RPC function name.
        rpc_type: RPC execution context.
    """
    conn = get_conn(ctx)
    if action == "set_replication":
        result = await conn.execute(
            "networking.set_replication",
            {
                "path": blueprint_path,
                "property_name": property_name,
                "replicated": replicated,
            },
        )
    elif action == "add_rpc":
        result = await conn.execute(
            "networking.add_rpc",
            {"path": blueprint_path, "rpc_name": rpc_name, "rpc_type": rpc_type},
        )
    elif action == "set_relevancy":
        result = await conn.execute("networking.set_net_relevancy", {"actor_path": actor_path})
    elif action == "get_info":
        result = await conn.execute(
            "networking.get_replication_info",
            {"actor_path": actor_path, "path": blueprint_path},
        )
    else:
        raise ValueError(f"Unknown action: {action}")
    if not result.success:
        raise ValueError(f"{result.error}.")
    return result.data


# ===================================================================
# Rendering Features subsystem
# C++ commands: get_rendering_settings, set_nanite_enabled,
#   set_lumen_settings, set_vsm_settings, set_tsr_settings,
#   set_post_process_settings, set_console_variable,
#   get_scalability_settings
# ===================================================================


@mcp.tool(
    title="Manage Rendering",
    tags={"rendering"},
    annotations=ToolAnnotations(
        readOnlyHint=False,
        destructiveHint=False,
        idempotentHint=False,
        openWorldHint=False,
    ),
)
async def configure_rendering(
    feature: Literal["nanite", "lumen", "vsm", "tsr", "post_process"],
    enabled: bool = True,
    actor_path: str = "",
    settings: dict[str, Any] | None = None,
    ctx: Context = None,
) -> dict:
    """Configure rendering features: Nanite, Lumen, VSM, TSR, post-process.

    Args:
        feature: Rendering feature to configure.
        enabled: Enable or disable the feature.
        actor_path: Post-process volume actor path (feature='post_process').
        settings: Additional settings dict for the feature.
    """
    conn = get_conn(ctx)
    cmd_map = {
        "nanite": "rendering.set_nanite_enabled",
        "lumen": "rendering.set_lumen_settings",
        "vsm": "rendering.set_vsm_settings",
        "tsr": "rendering.set_tsr_settings",
        "post_process": "rendering.set_post_process_settings",
    }
    params: dict[str, Any] = {"enabled": enabled}
    if actor_path:
        params["actor_path"] = actor_path
    if settings:
        params.update(settings)
    result = await conn.execute(cmd_map[feature], params)
    if not result.success:
        raise ValueError(f"{result.error}.")
    return result.data


# ===================================================================
# Profiling subsystem
# C++ commands: get_frame_stats, get_gpu_stats, get_memory_stats,
#   start_trace, stop_trace, execute_stat_command
# ===================================================================


@mcp.tool(
    title="Manage Profiling",
    tags={"profiling"},
    annotations=ToolAnnotations(
        readOnlyHint=True,
        destructiveHint=False,
        idempotentHint=True,
        openWorldHint=False,
    ),
)
async def profile_performance(
    action: Literal["frame_stats", "gpu_stats", "memory_stats", "start_trace", "stop_trace"],
    duration_seconds: float = 5.0,
    trace_channels: list[str] | None = None,
    ctx: Context = None,
) -> dict:
    """Profile and analyze performance: frame time, GPU, memory, and Unreal Insights traces.

    Args:
        action: frame_stats, gpu_stats, memory_stats, start_trace, or stop_trace.
        duration_seconds: Sampling duration for stats collection.
        trace_channels: Trace channels to enable for start_trace.
    """
    conn = get_conn(ctx)
    if action == "frame_stats":
        result = await conn.execute("profiling.get_frame_stats", {"duration": duration_seconds})
    elif action == "gpu_stats":
        result = await conn.execute("profiling.get_gpu_stats")
    elif action == "memory_stats":
        result = await conn.execute("profiling.get_memory_stats")
    elif action == "start_trace":
        result = await conn.execute(
            "profiling.start_trace",
            {"channels": trace_channels or ["cpu", "gpu", "frame"]},
        )
    elif action == "stop_trace":
        result = await conn.execute("profiling.stop_trace")
    else:
        raise ValueError(f"Unknown action: {action}")
    if not result.success:
        raise ValueError(f"{result.error}.")
    return result.data


# ===================================================================
# Source Control subsystem
# C++ commands: get_source_control_status, checkout_files, add_files,
#   revert_files, submit_changelist, get_file_history, mark_for_delete
# ===================================================================


@mcp.tool(
    title="Manage Source Control",
    tags={"sourcecontrol"},
    annotations=ToolAnnotations(
        readOnlyHint=False,
        destructiveHint=False,
        idempotentHint=False,
        openWorldHint=False,
    ),
)
async def manage_source_control(
    action: Literal["status", "checkout", "submit", "revert", "add", "history"],
    file_paths: list[str] | None = None,
    description: str = "",
    ctx: Context = None,
) -> dict:
    """Interact with UE source control (Perforce, Git, etc.).

    Args:
        action: status, checkout, submit, revert, add, or history.
        file_paths: Files to operate on. Empty = all changed files.
        description: Changelist description (action='submit').
    """
    conn = get_conn(ctx)
    if action == "status":
        result = await conn.execute(
            "sourcecontrol.get_source_control_status", {"paths": file_paths or []}
        )
    elif action == "checkout":
        result = await conn.execute("sourcecontrol.checkout_files", {"paths": file_paths or []})
    elif action == "submit":
        result = await conn.execute(
            "sourcecontrol.submit_changelist",
            {"paths": file_paths or [], "description": description},
        )
    elif action == "revert":
        result = await conn.execute("sourcecontrol.revert_files", {"paths": file_paths or []})
    elif action == "add":
        result = await conn.execute("sourcecontrol.add_files", {"paths": file_paths or []})
    elif action == "history":
        result = await conn.execute("sourcecontrol.get_file_history", {"paths": file_paths or []})
    else:
        raise ValueError(f"Unknown action: {action}")
    if not result.success:
        raise ValueError(f"{result.error}.")
    return result.data


# ===================================================================
# Code Analysis subsystem
# C++ commands: get_class_hierarchy, list_classes,
#   get_class_properties, get_class_functions,
#   search_classes, list_modules
# ===================================================================


@mcp.tool(
    title="Manage Code Analysis",
    tags={"code"},
    annotations=ToolAnnotations(
        readOnlyHint=True,
        destructiveHint=False,
        idempotentHint=True,
        openWorldHint=False,
    ),
)
async def analyze_classes(
    action: Literal[
        "get_hierarchy", "list_classes", "get_properties", "get_functions", "search_classes"
    ],
    class_name: str = "",
    query: str = "",
    include_inherited: bool = False,
    limit: int = 50,
    ctx: Context = None,
) -> dict:
    """Inspect UE class hierarchy, properties, and reflection metadata.

    Args:
        action: get_hierarchy, list_classes, get_properties, get_functions, or search_classes.
        class_name: Target class (e.g., 'AActor', 'UStaticMeshComponent').
        query: Search string for search_classes.
        include_inherited: Include inherited properties/functions.
        limit: Maximum results for list/search operations.
    """
    conn = get_conn(ctx)
    if action == "get_hierarchy":
        result = await conn.execute("code.get_class_hierarchy", {"class_name": class_name})
    elif action == "list_classes":
        result = await conn.execute("code.list_classes", {"class_name": class_name, "limit": limit})
    elif action == "get_properties":
        result = await conn.execute(
            "code.get_class_properties",
            {"class_name": class_name, "include_inherited": include_inherited},
        )
    elif action == "get_functions":
        result = await conn.execute(
            "code.get_class_functions",
            {"class_name": class_name, "include_inherited": include_inherited},
        )
    elif action == "search_classes":
        result = await conn.execute("code.search_classes", {"query": query, "limit": limit})
    else:
        raise ValueError(f"Unknown action: {action}")
    if not result.success:
        raise ValueError(f"{result.error}.")
    return result.data


# ===================================================================
# Game Features subsystem
# C++ commands: list_game_features, activate_game_feature,
#   deactivate_game_feature, create_game_feature,
#   get_game_feature_info
# ===================================================================


@mcp.tool(
    title="Manage Game Features",
    tags={"gamefeatures"},
    annotations=ToolAnnotations(
        readOnlyHint=False,
        destructiveHint=False,
        idempotentHint=False,
        openWorldHint=False,
    ),
)
async def manage_game_features(
    action: Literal["list", "activate", "deactivate", "get_info"],
    plugin_name: str = "",
    ctx: Context = None,
) -> dict:
    """Manage Game Feature plugins.

    Args:
        action: list, activate, deactivate, or get_info.
        plugin_name: Game Feature plugin name.
    """
    conn = get_conn(ctx)
    if action == "list":
        result = await conn.execute("gamefeatures.list_game_features")
    elif action == "activate":
        result = await conn.execute("gamefeatures.activate_game_feature", {"name": plugin_name})
    elif action == "deactivate":
        result = await conn.execute("gamefeatures.deactivate_game_feature", {"name": plugin_name})
    elif action == "get_info":
        result = await conn.execute("gamefeatures.get_game_feature_info", {"name": plugin_name})
    else:
        raise ValueError(f"Unknown action: {action}")
    if not result.success:
        raise ValueError(f"{result.error}.")
    return result.data
