"""Asset MCP resources with URI templates."""

from __future__ import annotations

import json

from fastmcp import Context, FastMCP

from nexus.connection.manager import ConnectionManager


def _conn(ctx: Context) -> ConnectionManager:
    return ctx.request_context.lifespan_context["connection_manager"]


def register_asset_resources(server: FastMCP) -> None:
    """Register asset resources on the server."""

    @server.resource("nexus://assets/{asset_path*}")
    async def get_asset(asset_path: str, ctx: Context) -> str:
        """Get detailed information about a UE asset by path.

        Args:
            asset_path: Asset path without leading slash, e.g. 'Game/Materials/M_Base'.
        """
        full_path = f"/{asset_path}"
        conn = _conn(ctx)
        result = await conn.execute("asset.info", {"path": full_path})
        if result.success:
            return json.dumps(result.data)
        return json.dumps({"error": result.error, "path": full_path})
