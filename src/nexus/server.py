"""Main Nexus FastMCP 3.0 server — workflow-oriented tools with progressive disclosure."""

from __future__ import annotations

from collections.abc import AsyncIterator
from contextlib import asynccontextmanager

from fastmcp import Context, FastMCP
from fastmcp.server.middleware.response_limiting import ResponseLimitingMiddleware

from nexus.connection.manager import ConnectionManager
from nexus.utils.logging import setup_logging


@asynccontextmanager
async def nexus_lifespan(server: FastMCP) -> AsyncIterator[dict]:
    """Server lifespan: initialize ConnectionManager, shut down on exit."""
    setup_logging()
    conn = ConnectionManager()
    await conn.initialize()
    try:
        yield {"connection_manager": conn}
    finally:
        await conn.shutdown()


mcp = FastMCP(
    "Nexus",
    instructions=(
        "Nexus is a workflow-oriented Unreal Engine 5.7 MCP server. "
        "Core tools handle 80% of tasks: spawn_actor, find_actors, modify_actor, "
        "delete_actors, search_assets, import_asset, manage_blueprint, manage_material, "
        "manage_level, setup_lighting, editor_control, execute_python, get_scene_info, "
        "undo, redo. Use Tool Search to discover specialized subsystem tools for "
        "animation, audio, sequencer, Niagara VFX, AI, physics, UI, Movie Render Queue, "
        "PCG, landscape, input, networking, rendering, profiling, source control, and "
        "code analysis. Workflow tools (create_scene_from_description, setup_cinematic, etc.) "
        "orchestrate multi-step operations server-side."
    ),
    lifespan=nexus_lifespan,
)

# UTF-8-safe truncation prevents oversized responses from blowing up context
mcp.add_middleware(ResponseLimitingMiddleware(max_size=500_000))


# ---------------------------------------------------------------------------
# Dependency injection helper
# ---------------------------------------------------------------------------


def get_conn(ctx: Context) -> ConnectionManager:
    """Extract ConnectionManager from request context. Used by all tools."""
    return ctx.request_context.lifespan_context["connection_manager"]


# ---------------------------------------------------------------------------
# Tool registration — import and register all tool modules
# ---------------------------------------------------------------------------


def _register_all_tools() -> None:
    """Import all tool modules. Each module registers tools on the mcp instance."""
    from nexus.tools import (
        core,  # noqa: F401
        deferred,  # noqa: F401
        mlserver,  # noqa: F401
        sceneforge,  # noqa: F401
        workflows,  # noqa: F401
    )


_register_all_tools()


# ---------------------------------------------------------------------------
# Resources
# ---------------------------------------------------------------------------

from nexus.resources import register_all_resources  # noqa: E402

register_all_resources(mcp)
