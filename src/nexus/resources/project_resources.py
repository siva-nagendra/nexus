"""Project-level MCP resources: health check, project info, transport status."""

from __future__ import annotations

import json

from fastmcp import Context, FastMCP

from nexus.connection.manager import ConnectionManager


def _conn(ctx: Context) -> ConnectionManager:
    return ctx.request_context.lifespan_context["connection_manager"]


def register_project_resources(server: FastMCP) -> None:
    """Register project resources on the server."""

    @server.resource("nexus://health")
    async def health_check(ctx: Context) -> str:
        """Check if Nexus can communicate with Unreal Engine.

        Returns connection status, transport type, command count, avg latency, and error rate.
        """
        conn = _conn(ctx)
        transport = conn.active_transport_type
        stats = conn.stats
        return json.dumps(
            {
                "status": "connected" if transport else "disconnected",
                "transport": transport.value if transport else None,
                **stats,
            }
        )

    @server.resource("nexus://project/info")
    async def project_info(ctx: Context) -> str:
        """Get current UE project information."""
        conn = _conn(ctx)
        result = await conn.execute("editor.get_project_info")
        if result.success:
            return json.dumps(result.data)
        return json.dumps({"error": result.error})

    @server.resource("nexus://transport/status")
    async def transport_status(ctx: Context) -> str:
        """Get detailed transport connection status."""
        conn = _conn(ctx)
        stats = conn.stats
        return json.dumps(
            {
                "active_transport": (
                    conn.active_transport_type.value if conn.active_transport_type else None
                ),
                "native_host": conn.native_host,
                "native_port": conn.native_port,
                **stats,
            }
        )
