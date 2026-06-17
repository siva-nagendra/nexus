"""Platform-specific utilities for ClaudeShell."""

from __future__ import annotations

import os
import shutil
import sys
from pathlib import Path


def get_state_dir() -> Path:
    """Return the platform-appropriate state directory for ClaudeShell.

    - Windows: %LOCALAPPDATA%/claudeshell
    - macOS:   ~/Library/Application Support/claudeshell
    - Linux:   ~/.local/share/claudeshell
    """
    if sys.platform == "win32":
        base = os.environ.get("LOCALAPPDATA", "")
        if not base:
            base = os.path.expanduser("~/AppData/Local")
        return Path(base) / "claudeshell"
    elif sys.platform == "darwin":
        return Path.home() / "Library" / "Application Support" / "claudeshell"
    else:
        xdg = os.environ.get("XDG_DATA_HOME", "")
        if not xdg:
            xdg = str(Path.home() / ".local" / "share")
        return Path(xdg) / "claudeshell"


def find_claude_cli() -> str:
    """Find the Claude CLI executable. Zero hardcoded paths.

    Search order:
    1. CLAUDE_PATH environment variable
    2. shutil.which() (PATH lookup)
    3. Platform-specific common install locations
    """
    # 1. Explicit env var
    env_path = os.environ.get("CLAUDE_PATH", "")
    if env_path and os.path.isfile(env_path):
        return env_path

    # 2. PATH lookup
    which = shutil.which("claude")
    if which:
        return which

    # 3. Platform-specific common locations
    if sys.platform == "win32":
        candidates = [
            os.path.expanduser(r"~\AppData\Local\Programs\claude\claude.exe"),
            os.path.expanduser(r"~\AppData\Roaming\npm\claude.cmd"),
            os.path.expanduser(r"~\.claude\local\claude.exe"),
            r"C:\ProgramData\chocolatey\bin\claude.exe",
        ]
    elif sys.platform == "darwin":
        candidates = [
            "/usr/local/bin/claude",
            os.path.expanduser("~/.claude/local/claude"),
            os.path.expanduser("~/.nvm/versions/node/*/bin/claude"),
        ]
    else:
        candidates = [
            "/usr/local/bin/claude",
            os.path.expanduser("~/.local/bin/claude"),
            os.path.expanduser("~/.claude/local/claude"),
        ]

    for c in candidates:
        if os.path.isfile(c):
            return c

    # Last resort: assume it's on PATH
    return "claude"
