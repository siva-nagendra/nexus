"""Tier 1 — Always-visible core tools (~15 tools).

These handle 80% of interactions with Unreal Engine.
Each tool represents a complete user intent, consolidating multiple
atomic operations into a single call.
"""

from __future__ import annotations

from typing import Any, Literal

from fastmcp import Context
from mcp.types import ToolAnnotations

from nexus.server import get_conn, mcp

# ---------------------------------------------------------------------------
# spawn_actor
# ---------------------------------------------------------------------------


@mcp.tool(
    title="Spawn Actor",
    tags={"core"},
    annotations=ToolAnnotations(
        readOnlyHint=False,
        destructiveHint=False,
        idempotentHint=False,
        openWorldHint=False,
    ),
)
async def spawn_actor(
    actor_class: str,
    label: str = "",
    location_x: float = 0.0,
    location_y: float = 0.0,
    location_z: float = 0.0,
    rotation_pitch: float = 0.0,
    rotation_yaw: float = 0.0,
    rotation_roll: float = 0.0,
    scale_x: float = 1.0,
    scale_y: float = 1.0,
    scale_z: float = 1.0,
    material_path: str = "",
    mesh_path: str = "",
    tags: list[str] | None = None,
    mobility: Literal["Static", "Stationary", "Movable"] = "Static",
    ctx: Context = None,
) -> dict:
    """Spawn a fully configured actor in the current level.

    Creates an actor and optionally sets its mesh, material, tags, and mobility
    in a single operation. Use find_actors to verify placement afterward.

    Limitations: Blueprint actors require full path (e.g., '/Game/BP/BP_Enemy.BP_Enemy_C').
    Static mesh actors need a mesh_path to be visible.

    Args:
        actor_class: UE class name or Blueprint path. Common types: StaticMeshActor,
            PointLight, SpotLight, DirectionalLight, CameraActor, PlayerStart,
            or Blueprint path like '/Game/BP/BP_Enemy.BP_Enemy_C'.
        label: Display name in the editor Outliner.
        location_x: World-space X position.
        location_y: World-space Y position.
        location_z: World-space Z position.
        rotation_pitch: Pitch in degrees.
        rotation_yaw: Yaw in degrees.
        rotation_roll: Roll in degrees.
        scale_x: X scale factor.
        scale_y: Y scale factor.
        scale_z: Z scale factor.
        material_path: Optional asset path to apply as material (e.g., '/Game/Materials/M_Base').
        mesh_path: Optional static mesh path (e.g., '/Engine/BasicShapes/Cube.Cube').
        tags: Optional list of tags for grouping/querying.
        mobility: Actor mobility — Static, Stationary, or Movable.
    """
    conn = get_conn(ctx)

    result = await conn.execute(
        "actor.spawn",
        {
            "actor_class": actor_class,
            "label": label,
            "location": {"x": location_x, "y": location_y, "z": location_z},
            "rotation": {"pitch": rotation_pitch, "yaw": rotation_yaw, "roll": rotation_roll},
            "scale": {"x": scale_x, "y": scale_y, "z": scale_z},
        },
    )
    if not result.success:
        raise ValueError(
            f"{result.error}. Check actor_class is valid — common types: "
            "StaticMeshActor, PointLight, CameraActor."
        )

    actor_path = result.data.get("actor_path", "")

    # Server-side orchestration: apply mesh, material, tags, mobility without LLM round-trips
    if mesh_path and actor_path:
        await conn.execute(
            "actor.set_property",
            {"actor_path": actor_path, "property_name": "StaticMesh", "property_value": mesh_path},
        )

    if material_path and actor_path:
        await conn.execute(
            "actor.set_property",
            {
                "actor_path": actor_path,
                "property_name": "Material",
                "property_value": material_path,
            },
        )

    if tags and actor_path:
        for tag in tags:
            await conn.execute("actor.add_tag", {"actor_path": actor_path, "tag": tag})

    if mobility != "Static" and actor_path:
        await conn.execute("actor.set_mobility", {"actor_path": actor_path, "mobility": mobility})

    return result.data


# ---------------------------------------------------------------------------
# find_actors
# ---------------------------------------------------------------------------


@mcp.tool(
    title="Find Actors",
    tags={"core"},
    annotations=ToolAnnotations(
        readOnlyHint=True,
        destructiveHint=False,
        idempotentHint=True,
        openWorldHint=False,
    ),
)
async def find_actors(
    query: str = "",
    actor_class: str = "",
    tag: str = "",
    limit: int = 50,
    offset: int = 0,
    response_format: Literal["concise", "detailed"] = "concise",
    ctx: Context = None,
) -> dict:
    """Search for actors in the current level with filtering and pagination.

    Combines search-by-label, class filter, and tag filter in one call.
    Use response_format='detailed' for full properties including transforms.

    Args:
        query: Search string matched against actor labels, paths, and tags.
            Empty string returns all actors.
        actor_class: Filter by UE class name (e.g., 'StaticMeshActor', 'PointLight').
        tag: Filter by actor tag.
        limit: Maximum results (default 50).
        offset: Pagination offset.
        response_format: 'concise' returns names/paths only; 'detailed' returns full properties.
    """
    conn = get_conn(ctx)

    params: dict[str, Any] = {"limit": limit, "offset": offset}
    if query:
        params["query"] = query
    if actor_class:
        params["actor_class"] = actor_class
    if tag:
        params["tag"] = tag
    params["detailed"] = response_format == "detailed"

    # Use the most specific command available
    if actor_class and not query:
        result = await conn.execute("actor.find_by_class", params)
    elif query:
        result = await conn.execute("actor.find", params)
    else:
        result = await conn.execute("actor.list_all", params)

    if not result.success:
        raise ValueError(f"{result.error}. Use find_actors with different query parameters.")

    return result.data


# ---------------------------------------------------------------------------
# modify_actor
# ---------------------------------------------------------------------------


@mcp.tool(
    title="Modify Actor",
    tags={"core"},
    annotations=ToolAnnotations(
        readOnlyHint=False,
        destructiveHint=False,
        idempotentHint=True,
        openWorldHint=False,
    ),
)
async def modify_actor(
    actor_path: str,
    location_x: float | None = None,
    location_y: float | None = None,
    location_z: float | None = None,
    rotation_pitch: float | None = None,
    rotation_yaw: float | None = None,
    rotation_roll: float | None = None,
    scale_x: float | None = None,
    scale_y: float | None = None,
    scale_z: float | None = None,
    visible: bool | None = None,
    mobility: Literal["Static", "Stationary", "Movable"] | None = None,
    add_tags: list[str] | None = None,
    remove_tags: list[str] | None = None,
    property_name: str = "",
    property_value: str = "",
    new_label: str = "",
    ctx: Context = None,
) -> dict:
    """Modify any actor property in a single call.

    Set transform, visibility, mobility, tags, properties, or label — only
    non-None parameters are applied. Use find_actors to discover actor_path.

    Args:
        actor_path: Full object path of the actor.
        location_x: New X position (None = keep current).
        location_y: New Y position.
        location_z: New Z position.
        rotation_pitch: New pitch in degrees.
        rotation_yaw: New yaw in degrees.
        rotation_roll: New roll in degrees.
        scale_x: New X scale.
        scale_y: New Y scale.
        scale_z: New Z scale.
        visible: Set visibility (True/False).
        mobility: Set mobility type.
        add_tags: Tags to add.
        remove_tags: Tags to remove.
        property_name: UPROPERTY name to set (requires property_value).
        property_value: New value for the property.
        new_label: Rename the actor's display label.
    """
    conn = get_conn(ctx)
    last_result: dict = {}

    # Transform — only send if any component is specified
    has_transform = any(
        v is not None
        for v in [
            location_x,
            location_y,
            location_z,
            rotation_pitch,
            rotation_yaw,
            rotation_roll,
            scale_x,
            scale_y,
            scale_z,
        ]
    )
    if has_transform:
        transform_params: dict[str, Any] = {"actor_path": actor_path}
        if any(v is not None for v in [location_x, location_y, location_z]):
            transform_params["location"] = {
                "x": location_x if location_x is not None else 0.0,
                "y": location_y if location_y is not None else 0.0,
                "z": location_z if location_z is not None else 0.0,
            }
        if any(v is not None for v in [rotation_pitch, rotation_yaw, rotation_roll]):
            transform_params["rotation"] = {
                "pitch": rotation_pitch if rotation_pitch is not None else 0.0,
                "yaw": rotation_yaw if rotation_yaw is not None else 0.0,
                "roll": rotation_roll if rotation_roll is not None else 0.0,
            }
        if any(v is not None for v in [scale_x, scale_y, scale_z]):
            transform_params["scale"] = {
                "x": scale_x if scale_x is not None else 1.0,
                "y": scale_y if scale_y is not None else 1.0,
                "z": scale_z if scale_z is not None else 1.0,
            }
        r = await conn.execute("actor.set_transform", transform_params)
        if not r.success:
            raise ValueError(f"{r.error}. Use find_actors to verify the actor_path is valid.")
        last_result = r.data

    if visible is not None:
        r = await conn.execute(
            "actor.set_visibility",
            {"actor_path": actor_path, "visible": visible, "propagate_to_children": True},
        )
        if r.success:
            last_result = r.data

    if mobility is not None:
        r = await conn.execute(
            "actor.set_mobility", {"actor_path": actor_path, "mobility": mobility}
        )
        if r.success:
            last_result = r.data

    if add_tags:
        for tag in add_tags:
            await conn.execute("actor.add_tag", {"actor_path": actor_path, "tag": tag})

    if remove_tags:
        for tag in remove_tags:
            await conn.execute("actor.remove_tag", {"actor_path": actor_path, "tag": tag})

    if property_name and property_value:
        r = await conn.execute(
            "actor.set_property",
            {
                "actor_path": actor_path,
                "property_name": property_name,
                "property_value": property_value,
            },
        )
        if not r.success:
            raise ValueError(f"{r.error}. Check property_name is a valid UPROPERTY.")
        last_result = r.data

    if new_label:
        r = await conn.execute("actor.rename", {"actor_path": actor_path, "new_label": new_label})
        if r.success:
            last_result = r.data

    return last_result or {"actor_path": actor_path, "modified": True}


# ---------------------------------------------------------------------------
# delete_actors
# ---------------------------------------------------------------------------


@mcp.tool(
    title="Delete Actors",
    tags={"core"},
    annotations=ToolAnnotations(
        readOnlyHint=False,
        destructiveHint=True,
        idempotentHint=True,
        openWorldHint=False,
    ),
)
async def delete_actors(
    actor_paths: list[str] | None = None,
    label_pattern: str = "",
    ctx: Context = None,
) -> dict:
    """Delete one or many actors from the current level.

    Provide specific paths for targeted deletion, or a label pattern for bulk.
    Use find_actors first to preview what will be deleted.

    Args:
        actor_paths: List of full object paths to delete.
        label_pattern: Glob pattern to match actor labels (e.g., 'Tree_*', 'Light_*').
    """
    conn = get_conn(ctx)

    if actor_paths and len(actor_paths) == 1:
        result = await conn.execute("actor.delete", {"actor_path": actor_paths[0]})
    else:
        params: dict[str, Any] = {}
        if actor_paths:
            params["actor_paths"] = actor_paths
        if label_pattern:
            params["actor_label_pattern"] = label_pattern
        result = await conn.execute("actor.delete_batch", params)

    if not result.success:
        raise ValueError(f"{result.error}. Use find_actors to discover valid actor paths.")
    return result.data


# ---------------------------------------------------------------------------
# search_assets
# ---------------------------------------------------------------------------


@mcp.tool(
    title="Search Assets",
    tags={"core"},
    annotations=ToolAnnotations(
        readOnlyHint=True,
        destructiveHint=False,
        idempotentHint=True,
        openWorldHint=False,
    ),
)
async def search_assets(
    query: str,
    asset_class: str = "",
    limit: int = 50,
    offset: int = 0,
    ctx: Context = None,
) -> dict:
    """Search the Content Browser for assets by name, path, or class.

    Args:
        query: Search string matched against asset names and paths.
        asset_class: Filter by asset class (e.g., 'StaticMesh', 'Material', 'Texture2D').
        limit: Maximum results (default 50).
        offset: Pagination offset.
    """
    conn = get_conn(ctx)
    params: dict[str, Any] = {"query": query, "limit": limit, "offset": offset}
    if asset_class:
        params["asset_class"] = asset_class

    result = await conn.execute("asset.search", params)
    if not result.success:
        raise ValueError(f"{result.error}. Try a broader search query.")
    return result.data


# ---------------------------------------------------------------------------
# import_asset
# ---------------------------------------------------------------------------


@mcp.tool(
    title="Import Asset",
    tags={"core"},
    annotations=ToolAnnotations(
        readOnlyHint=False,
        destructiveHint=False,
        idempotentHint=False,
        openWorldHint=False,
    ),
)
async def import_asset(
    source_path: str,
    destination_path: str,
    asset_name: str = "",
    ctx: Context = None,
) -> dict:
    """Import an external file (FBX, PNG, WAV, etc.) into the UE Content Browser.

    Args:
        source_path: Absolute file path on disk (e.g., 'C:/Assets/model.fbx').
        destination_path: UE content path (e.g., '/Game/Meshes').
        asset_name: Override the asset name. If empty, uses the filename.
    """
    conn = get_conn(ctx)
    result = await conn.execute(
        "asset.import",
        {
            "source_path": source_path,
            "destination_path": destination_path,
            "asset_name": asset_name,
        },
        timeout=120.0,
    )
    if not result.success:
        raise ValueError(f"{result.error}. Verify the source file exists and format is supported.")
    return result.data


# ---------------------------------------------------------------------------
# manage_blueprint
# ---------------------------------------------------------------------------


@mcp.tool(
    title="Manage Blueprint",
    tags={"core"},
    annotations=ToolAnnotations(
        readOnlyHint=False,
        destructiveHint=False,
        idempotentHint=False,
        openWorldHint=False,
    ),
)
async def manage_blueprint(
    action: Literal["create", "compile", "add_variable", "add_component", "get_info"],
    blueprint_path: str = "",
    blueprint_name: str = "",
    parent_class: str = "Actor",
    variable_name: str = "",
    variable_type: str = "",
    variable_default: str = "",
    component_class: str = "",
    component_name: str = "",
    ctx: Context = None,
) -> dict:
    """Create, modify, or compile Blueprints in a single tool.

    Actions:
    - create: Create a new Blueprint with the given parent class.
    - compile: Compile an existing Blueprint to validate.
    - add_variable: Add a variable to a Blueprint.
    - add_component: Add a component to a Blueprint.
    - get_info: Get Blueprint variables, functions, and components.

    Args:
        action: Operation to perform.
        blueprint_path: Full asset path for existing BPs (e.g., '/Game/BP/BP_Player').
        blueprint_name: Name for new BPs (action='create').
        parent_class: Parent class for new BPs (default 'Actor').
        variable_name: Variable name (action='add_variable').
        variable_type: Variable type like 'float', 'bool', 'FVector' (action='add_variable').
        variable_default: Default value as string (action='add_variable').
        component_class: Component class like 'StaticMeshComponent' (action='add_component').
        component_name: Component display name (action='add_component').
    """
    conn = get_conn(ctx)

    if action == "create":
        result = await conn.execute(
            "blueprint.create",
            {"name": blueprint_name, "parent_class": parent_class},
        )
    elif action == "compile":
        result = await conn.execute("blueprint.compile", {"path": blueprint_path}, timeout=120.0)
    elif action == "add_variable":
        result = await conn.execute(
            "blueprint.add_variable",
            {
                "path": blueprint_path,
                "variable_name": variable_name,
                "variable_type": variable_type,
                "default_value": variable_default,
            },
        )
    elif action == "add_component":
        result = await conn.execute(
            "blueprint.add_component",
            {
                "path": blueprint_path,
                "component_class": component_class,
                "component_name": component_name,
            },
        )
    elif action == "get_info":
        result = await conn.execute("blueprint.get_info", {"path": blueprint_path})
    else:
        raise ValueError(f"Unknown action: {action}")

    if not result.success:
        raise ValueError(f"{result.error}. Check the blueprint_path or parameters.")
    return result.data


# ---------------------------------------------------------------------------
# manage_material
# ---------------------------------------------------------------------------


@mcp.tool(
    title="Manage Material",
    tags={"core"},
    annotations=ToolAnnotations(
        readOnlyHint=False,
        destructiveHint=False,
        idempotentHint=False,
        openWorldHint=False,
    ),
)
async def manage_material(
    action: Literal[
        "create", "create_instance", "set_scalar", "set_vector", "set_texture", "apply", "get_info"
    ],
    material_path: str = "",
    material_name: str = "",
    parent_path: str = "",
    instance_name: str = "",
    parameter_name: str = "",
    scalar_value: float = 0.0,
    vector_r: float = 0.0,
    vector_g: float = 0.0,
    vector_b: float = 0.0,
    vector_a: float = 1.0,
    texture_path: str = "",
    actor_path: str = "",
    slot_index: int = 0,
    shading_model: Literal[
        "DefaultLit", "Unlit", "Subsurface", "ClearCoat", "Cloth", "Eye", "Hair"
    ] = "DefaultLit",
    blend_mode: Literal["Opaque", "Translucent", "Masked", "Additive"] = "Opaque",
    ctx: Context = None,
) -> dict:
    """Create and configure materials, instances, and parameters.

    Actions:
    - create: Create a new material.
    - create_instance: Create a material instance from a parent material.
    - set_scalar: Set a scalar parameter on a material instance.
    - set_vector: Set a vector/color parameter on a material instance.
    - set_texture: Set a texture parameter on a material instance.
    - apply: Apply a material to an actor.
    - get_info: Get material parameters and settings.

    Args:
        action: Operation to perform.
        material_path: Path to existing material or instance.
        material_name: Name for new materials (action='create').
        parent_path: Parent material for instances (action='create_instance').
        instance_name: Name for new instance (action='create_instance').
        parameter_name: Parameter name for set_scalar/set_vector/set_texture.
        scalar_value: Float value for set_scalar.
        vector_r: Red channel 0-1 for set_vector.
        vector_g: Green channel 0-1 for set_vector.
        vector_b: Blue channel 0-1 for set_vector.
        vector_a: Alpha channel 0-1 for set_vector.
        texture_path: Asset path for set_texture.
        actor_path: Actor to apply material to (action='apply').
        slot_index: Material slot index (action='apply').
        shading_model: Shading model for new materials.
        blend_mode: Blend mode for new materials.
    """
    conn = get_conn(ctx)

    if action == "create":
        result = await conn.execute(
            "material.create",
            {
                "name": material_name,
                "shading_model": shading_model,
                "blend_mode": blend_mode,
            },
        )
    elif action == "create_instance":
        result = await conn.execute(
            "material.create_instance",
            {
                "parent_material_path": parent_path,
                "instance_path": instance_name,
                "name": instance_name,
            },
        )
    elif action == "set_scalar":
        result = await conn.execute(
            "material.set_scalar_parameter",
            {"material_path": material_path, "name": parameter_name, "value": scalar_value},
        )
    elif action == "set_vector":
        result = await conn.execute(
            "material.set_vector_parameter",
            {
                "material_path": material_path,
                "name": parameter_name,
                "value": {"r": vector_r, "g": vector_g, "b": vector_b, "a": vector_a},
            },
        )
    elif action == "set_texture":
        result = await conn.execute(
            "material.set_texture_parameter",
            {
                "material_path": material_path,
                "name": parameter_name,
                "texture_path": texture_path,
            },
        )
    elif action == "apply":
        result = await conn.execute(
            "material.apply_to_actor",
            {
                "material_path": material_path,
                "actor_path": actor_path,
                "slot_index": slot_index,
            },
        )
    elif action == "get_info":
        result = await conn.execute("material.get_parameters", {"material_path": material_path})
    else:
        raise ValueError(f"Unknown action: {action}")

    if not result.success:
        raise ValueError(
            f"{result.error}. Use search_assets with asset_class='Material' to find valid paths."
        )
    return result.data


# ---------------------------------------------------------------------------
# manage_level
# ---------------------------------------------------------------------------


@mcp.tool(
    title="Manage Level",
    tags={"core"},
    annotations=ToolAnnotations(
        readOnlyHint=False,
        destructiveHint=False,
        idempotentHint=False,
        openWorldHint=False,
    ),
)
async def manage_level(
    action: Literal["get_current", "load", "save", "list_sublevels", "add_sublevel"],
    level_path: str = "",
    save_all: bool = False,
    ctx: Context = None,
) -> dict:
    """Level operations: load, save, and manage sublevels.

    Actions:
    - get_current: Get the currently loaded level info.
    - load: Load a level by path.
    - save: Save the current level (or all if save_all=True).
    - list_sublevels: List streaming sublevels.
    - add_sublevel: Add a streaming sublevel.

    Args:
        action: Operation to perform.
        level_path: Path for load/add_sublevel operations.
        save_all: Save all dirty packages (action='save').
    """
    conn = get_conn(ctx)

    if action == "get_current":
        result = await conn.execute("level.get_current")
    elif action == "load":
        result = await conn.execute("level.load", {"path": level_path})
    elif action == "save":
        result = await conn.execute("level.save", {"save_all": save_all})
    elif action == "list_sublevels":
        result = await conn.execute("level.list_sublevels")
    elif action == "add_sublevel":
        result = await conn.execute("level.add_sublevel", {"path": level_path})
    else:
        raise ValueError(f"Unknown action: {action}")

    if not result.success:
        raise ValueError(f"{result.error}.")
    return result.data


# ---------------------------------------------------------------------------
# setup_lighting
# ---------------------------------------------------------------------------


@mcp.tool(
    title="Setup Lighting",
    tags={"core"},
    annotations=ToolAnnotations(
        readOnlyHint=False,
        destructiveHint=False,
        idempotentHint=False,
        openWorldHint=False,
    ),
)
async def setup_lighting(
    action: Literal[
        "add_light", "configure_atmosphere", "configure_fog", "configure_gi", "get_info"
    ],
    light_type: Literal[
        "PointLight", "SpotLight", "DirectionalLight", "RectLight", "SkyLight"
    ] = "PointLight",
    location_x: float = 0.0,
    location_y: float = 0.0,
    location_z: float = 300.0,
    rotation_pitch: float = -45.0,
    rotation_yaw: float = 0.0,
    intensity: float = 1.0,
    color_r: float = 1.0,
    color_g: float = 1.0,
    color_b: float = 1.0,
    temperature: float = 6500.0,
    actor_path: str = "",
    gi_method: Literal["Lumen", "ScreenSpace", "None"] = "Lumen",
    ctx: Context = None,
) -> dict:
    """Configure scene lighting: lights, atmosphere, fog, and global illumination.

    Actions:
    - add_light: Spawn a light of the specified type.
    - configure_atmosphere: Set up sky atmosphere and volumetric clouds.
    - configure_fog: Set up exponential height fog.
    - configure_gi: Configure global illumination method.
    - get_info: Get current lighting info for an existing light actor.

    Args:
        action: Operation to perform.
        light_type: Type of light to create.
        location_x: Light position X.
        location_y: Light position Y.
        location_z: Light position Z.
        rotation_pitch: Light rotation pitch.
        rotation_yaw: Light rotation yaw.
        intensity: Light intensity multiplier.
        color_r: Light color red (0-1).
        color_g: Light color green (0-1).
        color_b: Light color blue (0-1).
        temperature: Color temperature in Kelvin.
        actor_path: Existing light actor path (action='get_info').
        gi_method: Global illumination method (action='configure_gi').
    """
    conn = get_conn(ctx)

    if action == "add_light":
        result = await conn.execute(
            "lighting.spawn_light",
            {
                "light_type": light_type,
                "location": {"x": location_x, "y": location_y, "z": location_z},
                "rotation": {"pitch": rotation_pitch, "yaw": rotation_yaw, "roll": 0.0},
                "intensity": intensity,
                "color": {"r": color_r, "g": color_g, "b": color_b},
                "temperature": temperature,
            },
        )
    elif action == "configure_atmosphere":
        result = await conn.execute("lighting.set_sky_atmosphere")
    elif action == "configure_fog":
        result = await conn.execute("lighting.set_exponential_fog")
    elif action == "configure_gi":
        result = await conn.execute("lighting.configure_global_illumination", {"method": gi_method})
    elif action == "get_info":
        result = await conn.execute("lighting.get_lighting_info", {"actor_path": actor_path})
    else:
        raise ValueError(f"Unknown action: {action}")

    if not result.success:
        raise ValueError(f"{result.error}.")
    return result.data


# ---------------------------------------------------------------------------
# editor_control
# ---------------------------------------------------------------------------


@mcp.tool(
    title="Editor Control",
    tags={"core"},
    annotations=ToolAnnotations(
        readOnlyHint=False,
        destructiveHint=False,
        idempotentHint=False,
        openWorldHint=False,
    ),
)
async def editor_control(
    action: Literal[
        "get_viewport",
        "set_camera",
        "screenshot",
        "start_pie",
        "stop_pie",
        "select",
        "deselect",
        "focus",
        "console_command",
        "get_world_info",
    ],
    location_x: float = 0.0,
    location_y: float = 0.0,
    location_z: float = 0.0,
    rotation_pitch: float = 0.0,
    rotation_yaw: float = 0.0,
    rotation_roll: float = 0.0,
    actor_paths: list[str] | None = None,
    command: str = "",
    filename: str = "",
    width: int = 1920,
    height: int = 1080,
    ctx: Context = None,
) -> dict:
    """Control the editor viewport, PIE sessions, selection, and console.

    Actions:
    - get_viewport: Get viewport camera info.
    - set_camera: Move the viewport camera.
    - screenshot: Capture a viewport screenshot.
    - start_pie: Start Play-In-Editor.
    - stop_pie: Stop Play-In-Editor.
    - select: Select actors by path.
    - deselect: Clear selection.
    - focus: Focus viewport on an actor.
    - console_command: Execute an Unreal console command.
    - get_world_info: Get world/map information.

    Args:
        action: Operation to perform.
        location_x: Camera X position (set_camera).
        location_y: Camera Y position (set_camera).
        location_z: Camera Z position (set_camera).
        rotation_pitch: Camera pitch (set_camera).
        rotation_yaw: Camera yaw (set_camera).
        rotation_roll: Camera roll (set_camera).
        actor_paths: Actor paths for select/focus (first path used for focus).
        command: Console command string.
        filename: Screenshot filename.
        width: Screenshot width.
        height: Screenshot height.
    """
    conn = get_conn(ctx)

    if action == "get_viewport":
        result = await conn.execute("editor.get_viewport_info")
    elif action == "set_camera":
        result = await conn.execute(
            "editor.set_viewport_camera",
            {
                "location": {"x": location_x, "y": location_y, "z": location_z},
                "rotation": {
                    "pitch": rotation_pitch,
                    "yaw": rotation_yaw,
                    "roll": rotation_roll,
                },
            },
        )
    elif action == "screenshot":
        result = await conn.execute(
            "editor.take_screenshot",
            {"filename": filename, "width": width, "height": height, "show_ui": False},
        )
    elif action == "start_pie":
        result = await conn.execute("editor.start_pie")
    elif action == "stop_pie":
        result = await conn.execute("editor.stop_pie")
    elif action == "select":
        result = await conn.execute("editor.set_selection", {"actor_paths": actor_paths or []})
    elif action == "deselect":
        result = await conn.execute("editor.clear_selection")
    elif action == "focus":
        path = (actor_paths or [""])[0]
        result = await conn.execute("editor.focus_actor", {"actor_path": path})
    elif action == "console_command":
        result = await conn.execute("editor.execute_console_command", {"command": command})
    elif action == "get_world_info":
        result = await conn.execute("editor.get_world_info")
    else:
        raise ValueError(f"Unknown action: {action}")

    if not result.success:
        raise ValueError(f"{result.error}.")
    return result.data


# ---------------------------------------------------------------------------
# execute_python
# ---------------------------------------------------------------------------


@mcp.tool(
    title="Execute Python",
    tags={"core"},
    annotations=ToolAnnotations(
        readOnlyHint=False,
        destructiveHint=False,
        idempotentHint=False,
        openWorldHint=True,
    ),
)
async def execute_python(
    code: str,
    ctx: Context = None,
) -> dict:
    """Execute Python code inside the Unreal Engine process.

    The code runs in UE's Python environment with full access to the 'unreal' module.
    Use for operations not covered by dedicated tools, complex batch operations,
    or debugging. The 'unreal' module is pre-imported.

    Args:
        code: Python source code to execute. Can be a single expression or script.
    """
    conn = get_conn(ctx)
    result = await conn.execute("python.execute", {"code": code})
    if not result.success:
        raise ValueError(f"{result.error}.")
    return result.data


# ---------------------------------------------------------------------------
# get_scene_info
# ---------------------------------------------------------------------------


@mcp.tool(
    title="Get Scene Info",
    tags={"core"},
    annotations=ToolAnnotations(
        readOnlyHint=True,
        destructiveHint=False,
        idempotentHint=True,
        openWorldHint=False,
    ),
)
async def get_scene_info(
    ctx: Context = None,
) -> dict:
    """Get a scene overview: actor count, loaded levels, viewport state, and world info.

    Combines world info and viewport info in a single call for quick orientation.
    """
    conn = get_conn(ctx)
    world = await conn.execute("editor.get_world_info")
    viewport = await conn.execute("editor.get_viewport_info")

    return {
        "world": world.data if world.success else {"error": world.error},
        "viewport": viewport.data if viewport.success else {"error": viewport.error},
    }


# ---------------------------------------------------------------------------
# undo / redo
# ---------------------------------------------------------------------------


@mcp.tool(
    title="Undo",
    tags={"core"},
    annotations=ToolAnnotations(
        readOnlyHint=False,
        destructiveHint=False,
        idempotentHint=False,
        openWorldHint=False,
    ),
)
async def undo(ctx: Context = None) -> dict:
    """Undo the last editor action (equivalent to Ctrl+Z)."""
    conn = get_conn(ctx)
    result = await conn.execute("editor.undo")
    if not result.success:
        raise ValueError(f"{result.error}.")
    return result.data


@mcp.tool(
    title="Redo",
    tags={"core"},
    annotations=ToolAnnotations(
        readOnlyHint=False,
        destructiveHint=False,
        idempotentHint=False,
        openWorldHint=False,
    ),
)
async def redo(ctx: Context = None) -> dict:
    """Redo the last undone editor action (equivalent to Ctrl+Y)."""
    conn = get_conn(ctx)
    result = await conn.execute("editor.redo")
    if not result.success:
        raise ValueError(f"{result.error}.")
    return result.data
