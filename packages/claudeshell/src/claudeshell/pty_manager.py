"""PTY process management — wraps pywinpty (Windows) or pexpect (Unix)."""

from __future__ import annotations

import os
import sys
from typing import Protocol


class PtyLike(Protocol):
    """Minimal interface for a pseudo-terminal process."""

    pid: int

    def read(self, size: int = 4096) -> str | bytes: ...
    def write(self, data: str) -> None: ...
    def setwinsize(self, rows: int, cols: int) -> None: ...
    def isalive(self) -> bool: ...
    def terminate(self, force: bool = False) -> None: ...


def spawn_pty(
    command: str,
    cwd: str,
    rows: int = 24,
    cols: int = 80,
    env: dict[str, str] | None = None,
) -> PtyLike:
    """Spawn a PTY process running *command* in *cwd*.

    Returns a platform-appropriate PTY wrapper that satisfies :class:`PtyLike`.
    """
    proc_env = os.environ.copy()
    proc_env["FORCE_COLOR"] = "1"
    proc_env["TERM"] = "xterm-256color"
    if env:
        proc_env.update(env)

    if sys.platform == "win32":
        from winpty import PtyProcess  # type: ignore[import-untyped]

        return PtyProcess.spawn(  # type: ignore[return-value]
            command,
            cwd=cwd,
            dimensions=(rows, cols),
        )
    else:
        import pexpect  # type: ignore[import-untyped]

        child = pexpect.spawn(
            command,
            cwd=cwd,
            dimensions=(rows, cols),
            env=proc_env,
            encoding=None,  # raw bytes
        )
        return child  # type: ignore[return-value]
