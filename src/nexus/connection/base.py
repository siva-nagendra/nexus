"""Abstract base for Unreal Engine transports."""

from __future__ import annotations

import enum
from abc import ABC, abstractmethod
from typing import Any

from nexus.models.commands import CommandResult


class TransportType(enum.Enum):
    """Available transport mechanisms."""

    NATIVE = "native"  # TCP to C++ plugin


# Explicit timeout registry: command_type → timeout_seconds.
# Commands not listed fall through to _DEFAULT_TIMEOUT.
_DEFAULT_TIMEOUT = 30.0  # Mutations
_QUERY_TIMEOUT = 10.0
_LARGE_TIMEOUT = 120.0
_RENDER_TIMEOUT = 300.0

_TIMEOUT_MAP: dict[str, float] = {
    # Render / heavy operations (300s)
    "mrq.submit_job": _RENDER_TIMEOUT,
    "mrq.render_queue": _RENDER_TIMEOUT,
    "mrq.render": _RENDER_TIMEOUT,
    "lighting.bake_lighting": _RENDER_TIMEOUT,
    "landscape.create_landscape": _RENDER_TIMEOUT,
    "landscape.import_heightmap": _RENDER_TIMEOUT,
    "landscape.export_heightmap": _RENDER_TIMEOUT,
    "sequencer.export_sequence": _RENDER_TIMEOUT,
    "pcg.execute_pcg_graph": _RENDER_TIMEOUT,
    # Large / compile / import (120s)
    "blueprint.compile": _LARGE_TIMEOUT,
    "blueprint.compile_blueprint": _LARGE_TIMEOUT,
    "asset.import": _LARGE_TIMEOUT,
    "asset.import_asset": _LARGE_TIMEOUT,
    "asset.export": _LARGE_TIMEOUT,
    "niagara.compile": _LARGE_TIMEOUT,
    "animation.retarget": _LARGE_TIMEOUT,
    # Standard mutations (30s) — default, no need to list
    # Queries (10s)
    "system.echo": _QUERY_TIMEOUT,
    "editor.get_viewport_info": _QUERY_TIMEOUT,
    "editor.get_world_info": _QUERY_TIMEOUT,
    "editor.get_selection": _QUERY_TIMEOUT,
    "editor.is_pie_running": _QUERY_TIMEOUT,
    "editor.get_project_info": _QUERY_TIMEOUT,
    "actor.find": _QUERY_TIMEOUT,
    "actor.find_by_class": _QUERY_TIMEOUT,
    "actor.list_all": _QUERY_TIMEOUT,
    "actor.get_transform": _QUERY_TIMEOUT,
    "actor.get_property": _QUERY_TIMEOUT,
    "actor.get_components": _QUERY_TIMEOUT,
    "actor.get_tags": _QUERY_TIMEOUT,
    "actor.get_bounds": _QUERY_TIMEOUT,
    "asset.search": _QUERY_TIMEOUT,
    "asset.get_metadata": _QUERY_TIMEOUT,
    "asset.list_assets": _QUERY_TIMEOUT,
    "asset.exists": _QUERY_TIMEOUT,
    "blueprint.get_variables": _QUERY_TIMEOUT,
    "blueprint.get_functions": _QUERY_TIMEOUT,
    "blueprint.get_components": _QUERY_TIMEOUT,
    "material.get_parameters": _QUERY_TIMEOUT,
    "level.get_current": _QUERY_TIMEOUT,
    "level.list_sublevels": _QUERY_TIMEOUT,
    "python.get_paths": _QUERY_TIMEOUT,
    "profiling.get_stats": _QUERY_TIMEOUT,
    "code.get_class_info": _QUERY_TIMEOUT,
    "code.list_classes": _QUERY_TIMEOUT,
    "sequencer.get_tracks": _QUERY_TIMEOUT,
    "animation.get_skeleton": _QUERY_TIMEOUT,
    "audio.get_settings": _QUERY_TIMEOUT,
    "niagara.get_emitters": _QUERY_TIMEOUT,
    "physics.get_collision": _QUERY_TIMEOUT,
    "sourcecontrol.get_status": _QUERY_TIMEOUT,
    # SceneForge read-only queries: in-game HTTP server (port 13378) removes
    # game-thread cross-dispatch, so cached state returns are near-instant.
    "sceneforge.get_status": 5.0,
    "sceneforge.get_mode": 5.0,
    "sceneforge.get_settings": 5.0,
    "sceneforge.get_camera": 5.0,
    "sceneforge.get_session_dir": 5.0,
    "sceneforge.list_sessions": 5.0,
    "sceneforge.health_check": 5.0,
    # Reads that may serialize larger payloads (segments, images, depth maps)
    "sceneforge.get_segments": _QUERY_TIMEOUT,
    "sceneforge.get_scene_actors": _QUERY_TIMEOUT,
    "sceneforge.get_detected_objects": _QUERY_TIMEOUT,
    "sceneforge.get_generated_image": _QUERY_TIMEOUT,
    "sceneforge.get_depth_map": _QUERY_TIMEOUT,
    "sceneforge.get_current_mask": _QUERY_TIMEOUT,
    "sceneforge.get_depth_at_point": _QUERY_TIMEOUT,
    "sceneforge.screenshot": _QUERY_TIMEOUT,
    # Mutations: 15s is sufficient with direct in-game dispatch
    "sceneforge.segment": 15.0,
    "sceneforge.accept_segment": 15.0,
    "sceneforge.convert_to_3d": 15.0,
    "sceneforge.convert_all": 15.0,
    "sceneforge.toggle_mode": 15.0,
    "sceneforge.clear_selection": 15.0,
    "sceneforge.save_session": 15.0,
    "sceneforge.set_settings": 15.0,
    "sceneforge.exec_console": 15.0,
    # Async pipeline triggers that return quickly but start background work
    "sceneforge.generate_image": _LARGE_TIMEOUT,
    "sceneforge.generate_scene": _LARGE_TIMEOUT,
    "sceneforge.generate_whole_scene": _LARGE_TIMEOUT,
    "sceneforge.detect_objects": _LARGE_TIMEOUT,
    "sceneforge.estimate_depth": _LARGE_TIMEOUT,
}


def _resolve_timeout(command_type: str, timeout: float | None) -> float:
    """Resolve timeout: use explicit value, lookup in registry, or infer from command name."""
    if timeout is not None:
        return timeout

    # Exact match in registry
    if command_type in _TIMEOUT_MAP:
        return _TIMEOUT_MAP[command_type]

    # Heuristic fallback: commands starting with get/find/list/search/is/exists are queries
    action = command_type.split(".")[-1] if "." in command_type else command_type
    for prefix in ("get_", "find_", "list_", "search_", "is_", "exists_", "info"):
        if action.startswith(prefix):
            return _QUERY_TIMEOUT

    return _DEFAULT_TIMEOUT


class UnrealConnection(ABC):
    """Abstract base class for Unreal Engine transport implementations."""

    transport_type: TransportType

    @abstractmethod
    async def connect(self) -> None:
        """Establish connection to Unreal Engine. Raises ConnectionError on failure."""

    @abstractmethod
    async def disconnect(self) -> None:
        """Gracefully close the connection."""

    @abstractmethod
    async def is_connected(self) -> bool:
        """Check if the connection is alive."""

    @abstractmethod
    async def execute(
        self,
        command_type: str,
        params: dict[str, Any] | None = None,
        timeout: float | None = None,
    ) -> CommandResult:
        """Execute a command and return the result.

        Args:
            command_type: Dot-separated command, e.g. 'actor.spawn'.
            params: Command parameters.
            timeout: Override timeout in seconds. If None, uses timeout registry.

        Returns:
            CommandResult with success/data/error.
        """

    def _resolve_timeout(self, command_type: str, timeout: float | None) -> float:
        """Resolve timeout using the explicit registry."""
        return _resolve_timeout(command_type, timeout)
