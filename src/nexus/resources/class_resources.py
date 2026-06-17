"""UE class hierarchy MCP resources."""

from __future__ import annotations

import json

from fastmcp import Context, FastMCP

from nexus.connection.manager import ConnectionManager


def _conn(ctx: Context) -> ConnectionManager:
    return ctx.request_context.lifespan_context["connection_manager"]


def register_class_resources(server: FastMCP) -> None:
    """Register class resources on the server."""

    @server.resource("nexus://classes/{class_name}")
    async def get_class_info(class_name: str, ctx: Context) -> str:
        """Get information about a UE class (properties, functions, hierarchy).

        Args:
            class_name: UE class name, e.g. 'StaticMeshActor', 'PointLight'.
        """
        conn = _conn(ctx)
        result = await conn.execute("code.class_info", {"class_name": class_name})
        if result.success:
            return json.dumps(result.data)
        return json.dumps({"error": result.error, "class_name": class_name})
