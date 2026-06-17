"""MCP Resources for Nexus."""

from __future__ import annotations

from fastmcp import FastMCP


def register_all_resources(server: FastMCP) -> None:
    """Register all resource handlers on the main server."""
    from nexus.resources.asset_resources import register_asset_resources
    from nexus.resources.class_resources import register_class_resources
    from nexus.resources.project_resources import register_project_resources

    register_project_resources(server)
    register_asset_resources(server)
    register_class_resources(server)
