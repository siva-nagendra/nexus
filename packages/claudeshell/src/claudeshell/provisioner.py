"""MCP provisioner — manages .mcp.json and CLAUDE.md for any DCC adapter.

DCC-agnostic: takes ``server_name`` + ``server_config`` dict.
The UE adapter passes ``server_name="nexus"``, ``server_config={"type":"stdio",...}``.
A Maya adapter would pass ``server_name="maya-mcp"``, etc.

This module is lazy-imported by ``session.py`` to avoid circular deps
during early package loading.
"""

from __future__ import annotations

import json
import logging
from pathlib import Path

logger = logging.getLogger("claudeshell.provisioner")


class McpProvisioner:
    """Manages ``.mcp.json`` and ``.claude/CLAUDE.md`` for any DCC.

    Each method is idempotent — safe to call every session start.
    """

    def ensure_mcp_json(
        self,
        project_dir: str,
        server_name: str,
        server_config: dict,
    ) -> Path:
        """Merge server config into ``.mcp.json``, preserving existing entries.

        If the global ``~/.claude/.mcp.json`` already has this server configured,
        skip writing the project-level file to avoid conflicts.

        If ``.mcp.json`` already exists, reads it, adds/updates only the
        ``server_name`` key under ``mcpServers``, and writes back.
        Other servers in the file are left untouched.

        Args:
            project_dir: Path to the project root (where ``.mcp.json`` lives).
            server_name: Key under ``mcpServers`` (e.g. ``"nexus"``).
            server_config: Server configuration dict (e.g.
                ``{"type": "stdio", "command": "uv", "args": ["run", "nexus"]}``).

        Returns:
            Path to the written ``.mcp.json`` file, or global path if skipped.
        """
        # Check if user-level ~/.claude.json already has this server configured.
        # Claude Code stores user-scoped MCP servers in ~/.claude.json under "mcpServers".
        global_mcp = Path.home() / ".claude.json"
        if global_mcp.exists():
            try:
                global_data = json.loads(global_mcp.read_text(encoding="utf-8"))
                if server_name in global_data.get("mcpServers", {}):
                    logger.info(
                        "Server '%s' already in user config %s, skipping project-level write",
                        server_name, global_mcp,
                    )
                    return global_mcp
            except (json.JSONDecodeError, UnicodeDecodeError, OSError):
                pass  # Fall through to project-level write

        mcp_path = Path(project_dir) / ".mcp.json"

        existing: dict = {}
        if mcp_path.exists():
            try:
                existing = json.loads(mcp_path.read_text(encoding="utf-8"))
            except (json.JSONDecodeError, UnicodeDecodeError):
                logger.warning("Corrupt .mcp.json at %s, will overwrite", mcp_path)
                existing = {}

        servers = existing.setdefault("mcpServers", {})
        servers[server_name] = server_config

        mcp_path.write_text(json.dumps(existing, indent=2) + "\n", encoding="utf-8")
        logger.info("Updated .mcp.json at %s with server '%s'", mcp_path, server_name)
        return mcp_path

    def ensure_claude_md(
        self,
        project_dir: str,
        template_dir: Path,
        version: str = "3.0.0",
    ) -> Path:
        """Copy or update ``CLAUDE.md`` from template if the version changed.

        Uses a version marker comment (``<!-- nexus-docs-version: X.Y.Z -->``)
        at the top of the file. If the marker already matches the requested
        version, the file is left alone.

        Args:
            project_dir: Path to the project root.
            template_dir: Directory containing the ``NEXUS_CLAUDE.md`` template.
            version: Version string to stamp into the marker.

        Returns:
            Path to the target ``CLAUDE.md`` (may or may not have been updated).
        """
        target = Path(project_dir) / ".claude" / "CLAUDE.md"
        version_marker = f"<!-- nexus-docs-version: {version} -->"

        # Skip if already up to date
        if target.exists():
            try:
                content = target.read_text(encoding="utf-8")
                if version_marker in content:
                    logger.debug(
                        "CLAUDE.md at %s already at version %s, skipping",
                        target,
                        version,
                    )
                    return target
            except (OSError, UnicodeDecodeError):
                pass  # Proceed to overwrite

        # Locate template
        template = template_dir / "NEXUS_CLAUDE.md"
        if not template.exists():
            logger.warning("Template not found: %s", template)
            return target

        # Write with version marker prepended
        target.parent.mkdir(parents=True, exist_ok=True)
        template_content = template.read_text(encoding="utf-8")
        target.write_text(
            version_marker + "\n" + template_content,
            encoding="utf-8",
        )
        logger.info("Updated CLAUDE.md at %s to version %s", target, version)
        return target
