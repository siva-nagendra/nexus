"""CLI entry point for ClaudeShell relay.

Usage::

    claudeshell --port 19220 --token <uuid> --cwd /path/to/project
"""

from __future__ import annotations

import argparse
import asyncio
import os
import uuid


def _build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        prog="claudeshell",
        description="ClaudeShell — DCC-agnostic Claude terminal relay",
    )
    parser.add_argument(
        "--port",
        type=int,
        default=19220,
        help="Port to listen on (default: 19220)",
    )
    parser.add_argument(
        "--token",
        default="",
        help="Bearer token for authentication (auto-generated if empty)",
    )
    parser.add_argument(
        "--cwd",
        default=os.getcwd(),
        help="Working directory for Claude sessions",
    )
    parser.add_argument(
        "--web-dir",
        default="",
        help="Directory containing terminal frontend files",
    )
    parser.add_argument(
        "--docs-dir",
        default="",
        help="Directory containing documentation templates",
    )
    parser.add_argument(
        "--mcp-config",
        default="",
        help="JSON string of MCP server configuration to provision",
    )
    parser.add_argument(
        "--parent-pid",
        type=int,
        default=0,
        help="PID of parent process; relay self-terminates when parent dies",
    )
    parser.add_argument(
        "--version",
        action="version",
        version="%(prog)s 3.0.0",
    )
    return parser


def main() -> None:
    """Parse CLI args and launch the relay."""
    parser = _build_parser()
    args = parser.parse_args()

    token = args.token or str(uuid.uuid4())

    # Resolve web directory: explicit > adjacent frontend/ > bundled
    web_dir = args.web_dir
    if not web_dir:
        pkg_root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
        candidate = os.path.join(pkg_root, "..", "frontend")
        if os.path.isdir(candidate):
            web_dir = os.path.abspath(candidate)
        else:
            web_dir = ""

    # Import here to avoid circular imports at module level
    from claudeshell.relay import ClaudeShellRelay

    relay = ClaudeShellRelay(
        port=args.port,
        token=token,
        web_dir=web_dir,
        docs_dir=args.docs_dir,
        mcp_config=args.mcp_config,
        parent_pid=args.parent_pid,
        version="3.0.0",
    )

    # Print startup info for parent process to capture.
    # Flush immediately, then redirect stdout to devnull so a broken pipe
    # (from the parent closing its read end) can never crash the relay.
    import sys

    print(f"[claudeshell] Token: {token}")
    print(f"[claudeshell] Port:  {args.port}")
    print(f"[claudeshell] CWD:   {args.cwd}")
    sys.stdout.flush()

    try:
        sys.stdout = open(os.devnull, "w")  # noqa: SIM115
    except OSError:
        pass

    try:
        asyncio.run(relay.run())
    except KeyboardInterrupt:
        pass
