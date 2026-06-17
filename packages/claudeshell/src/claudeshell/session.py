"""Session management — PTY lifecycle, scrollback, and client routing."""

from __future__ import annotations

import asyncio
import base64
import json
import logging
import uuid
from pathlib import Path
from typing import Any

from claudeshell.platform import find_claude_cli
from claudeshell.pty_manager import PtyLike, spawn_pty

logger = logging.getLogger(__name__)


class Session:
    """A single Claude CLI terminal session.

    Manages:
    - A PTY process running Claude CLI
    - Scrollback buffer (last 50 KB of output)
    - Connected WebSocket clients (1:N — multiple browser tabs can view the same session)
    """

    SCROLLBACK_MAX = 50 * 1024  # 50 KB

    def __init__(
        self,
        session_id: str,
        cwd: str,
        project_name: str = "",
        mcp_config: dict[str, Any] | None = None,
        docs_dir: Path | None = None,
    ) -> None:
        self.id = session_id
        self.cwd = cwd
        self.project_name = project_name
        self.mcp_config = mcp_config
        self.docs_dir = docs_dir

        self._pty: PtyLike | None = None
        self._clients: set[Any] = set()
        self._scrollback = bytearray()
        self._alive = False
        self._reader_task: asyncio.Task[None] | None = None

    # ------------------------------------------------------------------
    # Lifecycle
    # ------------------------------------------------------------------

    async def start(self) -> None:
        """Provision environment and start Claude CLI in a PTY."""
        # Lazy-import provisioner to avoid circular deps during early loading
        from claudeshell.provisioner import McpProvisioner

        provisioner = McpProvisioner()

        if self.mcp_config:
            provisioner.ensure_mcp_json(
                self.cwd,
                self.mcp_config["server_name"],
                self.mcp_config["server_config"],
            )

        if self.docs_dir:
            provisioner.ensure_claude_md(self.cwd, self.docs_dir)

        claude_path = find_claude_cli()

        self._pty = spawn_pty(
            command=claude_path,
            cwd=self.cwd,
            rows=30,
            cols=120,
            env={"CLAUDESHELL": "1"},
        )
        self._alive = True

        logger.info(
            "Session %s started (PID %s, cwd=%s)",
            self.id,
            getattr(self._pty, "pid", "?"),
            self.cwd,
        )

        self._reader_task = asyncio.create_task(self._read_output_loop())

    async def restart(self) -> None:
        """Kill current PTY and start a fresh Claude session."""
        self.shutdown()
        self._scrollback.clear()
        await self.start()

    def shutdown(self) -> None:
        """Kill PTY and clean up resources."""
        self._alive = False
        if self._reader_task and not self._reader_task.done():
            self._reader_task.cancel()
            self._reader_task = None
        if self._pty:
            try:
                if self._pty.isalive():
                    self._pty.terminate(force=True)
            except Exception:
                pass
            self._pty = None

    # ------------------------------------------------------------------
    # PTY I/O
    # ------------------------------------------------------------------

    async def _read_output_loop(self) -> None:
        """Read PTY output in a thread executor and broadcast to clients."""
        loop = asyncio.get_running_loop()

        while self._alive and self._pty is not None:
            try:
                # PTY read is blocking — run in executor to avoid starving the event loop
                data = await loop.run_in_executor(None, self._blocking_read)
                if data is None:
                    # No data available (timeout in blocking_read)
                    continue

                raw = data.encode("utf-8") if isinstance(data, str) else data

                # Append to scrollback, trimming if necessary
                self._scrollback.extend(raw)
                if len(self._scrollback) > self.SCROLLBACK_MAX:
                    self._scrollback[:] = self._scrollback[-self.SCROLLBACK_MAX :]

                # Broadcast to all connected WebSocket clients
                encoded = base64.b64encode(raw).decode("ascii")
                msg = json.dumps({"type": "output", "data": encoded})
                await self._broadcast(msg)

            except EOFError:
                # PTY process exited
                self._alive = False
                exit_code = -1
                if self._pty:
                    try:
                        exit_code = getattr(self._pty, "exitstatus", None) or 0
                    except Exception:
                        exit_code = -1
                logger.info("Session %s PTY exited with code %s", self.id, exit_code)
                msg = json.dumps({"type": "exit", "code": exit_code})
                await self._broadcast(msg)

            except asyncio.CancelledError:
                break

            except Exception as exc:
                logger.error("Session %s read error: %s", self.id, exc)
                await asyncio.sleep(0.05)

    def _blocking_read(self) -> str | bytes | None:
        """Read from PTY with a short timeout. Runs in a thread executor."""
        if self._pty is None or not self._pty.isalive():
            raise EOFError("PTY process is no longer alive")
        try:
            data = self._pty.read(4096)
            return data if data else None
        except EOFError:
            raise
        except Exception:
            return None

    def write_input(self, data_b64: str) -> None:
        """Write base64-encoded input to the PTY."""
        if not self._pty or not self._alive:
            return
        raw = base64.b64decode(data_b64)
        text = raw.decode("utf-8", errors="replace")
        self._pty.write(text)

    def resize(self, cols: int, rows: int) -> None:
        """Resize the PTY dimensions."""
        if self._pty and self._alive:
            self._pty.setwinsize(rows, cols)

    # ------------------------------------------------------------------
    # Client management
    # ------------------------------------------------------------------

    def get_scrollback(self) -> str:
        """Return the scrollback buffer as a base64-encoded string."""
        return base64.b64encode(bytes(self._scrollback)).decode("ascii")

    def add_client(self, ws: Any) -> None:
        """Register a WebSocket client for output broadcasts."""
        self._clients.add(ws)

    def remove_client(self, ws: Any) -> None:
        """Unregister a WebSocket client."""
        self._clients.discard(ws)

    @property
    def client_count(self) -> int:
        return len(self._clients)

    @property
    def alive(self) -> bool:
        return self._alive

    async def _broadcast(self, msg: str) -> None:
        """Send a message to all connected WebSocket clients, pruning dead ones."""
        dead: set[Any] = set()
        for ws in list(self._clients):
            try:
                await ws.send(msg)
            except Exception:
                dead.add(ws)
        self._clients -= dead


class SessionRegistry:
    """Manages all active sessions with create / get / list / shutdown semantics."""

    def __init__(self) -> None:
        self._sessions: dict[str, Session] = {}

    def create(
        self,
        cwd: str,
        project_name: str = "",
        mcp_config: dict[str, Any] | None = None,
        docs_dir: Path | None = None,
    ) -> Session:
        """Create a new session (does not start it — call ``await session.start()``)."""
        session_id = uuid.uuid4().hex[:8]
        session = Session(
            session_id=session_id,
            cwd=cwd,
            project_name=project_name,
            mcp_config=mcp_config,
            docs_dir=docs_dir,
        )
        self._sessions[session_id] = session
        logger.info("Created session %s (project=%s, cwd=%s)", session_id, project_name, cwd)
        return session

    def get(self, session_id: str) -> Session | None:
        """Return a session by ID, or ``None`` if not found."""
        return self._sessions.get(session_id)

    def list_sessions(self) -> list[dict[str, Any]]:
        """Return a summary list of all sessions."""
        return [
            {
                "id": s.id,
                "project_name": s.project_name,
                "alive": s.alive,
                "clients": s.client_count,
            }
            for s in self._sessions.values()
        ]

    def shutdown(self, session_id: str) -> bool:
        """Shut down and remove a session. Returns True if found."""
        session = self._sessions.pop(session_id, None)
        if session is None:
            return False
        session.shutdown()
        logger.info("Shut down session %s", session_id)
        return True

    def shutdown_all(self) -> None:
        """Shut down every session and clear the registry."""
        for session in self._sessions.values():
            session.shutdown()
        count = len(self._sessions)
        self._sessions.clear()
        logger.info("Shut down all %d sessions", count)
