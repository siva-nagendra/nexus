"""Entry point: python -m nexus or `uv run nexus`."""

from __future__ import annotations

import logging

from nexus.server import mcp


def main() -> None:
    """Launch Nexus over MCP stdio transport.

    MCP uses stdin/stdout for JSON-RPC, so all StreamHandlers (which
    target stdout/stderr) must be removed before the server starts.
    File-based logging configured in the lifespan is unaffected.
    """
    root_logger = logging.getLogger()
    root_logger.handlers = [
        handler
        for handler in root_logger.handlers
        if not isinstance(handler, logging.StreamHandler)
    ]
    mcp.run(transport="stdio")


if __name__ == "__main__":
    main()
