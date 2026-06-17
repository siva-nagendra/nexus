"""Common UE workflow prompt templates."""

from __future__ import annotations

from fastmcp import FastMCP


def register_prompts(server: FastMCP) -> None:
    """Register workflow prompt templates."""

    @server.prompt()
    def scene_setup() -> str:
        """Guide for setting up a basic UE scene from scratch."""
        return (
            "To set up a new UE scene:\n"
            "1. Use get_scene_info to check the current level\n"
            "2. Use find_actors to see existing content\n"
            "3. Use setup_lighting with action='configure_atmosphere' for sky\n"
            "4. Use setup_lighting action='add_light', light_type='DirectionalLight'\n"
            "5. Use setup_lighting with action='add_light' and light_type='SkyLight' for ambient\n"
            "6. Use spawn_actor to add your scene content\n"
            "7. Use editor_control with action='screenshot' to verify the result"
        )

    @server.prompt()
    def render_setup() -> str:
        """Guide for configuring MRQ rendering."""
        return (
            "To render a cinematic:\n"
            "1. Use setup_cinematic to create sequence + camera\n"
            "2. Use manage_render_queue with action='create'\n"
            "3. Use manage_render_queue with action='add_job'\n"
            "4. Use manage_render_queue with action='add_pass'\n"
            "5. Use manage_render_queue with action='configure'\n"
            "6. Use manage_render_queue with action='submit'\n"
            "Or use render_cinematic to do all steps at once"
        )

    @server.prompt()
    def material_setup() -> str:
        """Guide for creating and applying materials."""
        return (
            "To create and apply a material:\n"
            "1. Use manage_material with action='create'\n"
            "2. Use manage_material with action='set_scalar' for numeric parameters\n"
            "3. Use manage_material with action='set_vector' for colors\n"
            "4. Use manage_material with action='set_texture' for textures\n"
            "5. Use manage_material with action='create_instance' for variations\n"
            "6. Use manage_material with action='apply' to assign to an actor\n"
            "Or use create_material_library to batch-create materials"
        )

    @server.prompt()
    def blueprint_workflow() -> str:
        """Guide for creating a Blueprint with components and logic."""
        return (
            "To create a Blueprint:\n"
            "1. Use manage_blueprint with action='create' and parent_class\n"
            "2. Use manage_blueprint with action='add_component' for mesh/collision\n"
            "3. Use manage_blueprint with action='add_variable' for state\n"
            "4. Use manage_blueprint with action='compile' to validate\n"
            "5. Use spawn_actor with the Blueprint path to place instances\n"
            "Or use create_interactive_object for a pre-configured interactive BP"
        )
