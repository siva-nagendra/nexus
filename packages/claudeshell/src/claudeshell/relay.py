"""ClaudeShell Relay — multi-session HTTP + WebSocket server.

The relay is the core of ClaudeShell.  It:
1. Manages Claude CLI sessions via PTY (pywinpty on Windows, pexpect on Unix)
2. Serves a web terminal frontend (xterm.js)
3. Proxies terminal I/O over WebSocket
4. Provides REST API for session lifecycle
5. Monitors parent process and self-terminates on crash

The relay knows NOTHING about Unreal, Maya, Blender, etc.
Any DCC adapter can start this relay.
"""

from __future__ import annotations

import asyncio
import json
import logging
import os
import time
from pathlib import Path
from typing import Any
from urllib.parse import parse_qs, urlparse

import websockets
from websockets.http11 import Response

from claudeshell.auth import validate_token
from claudeshell.platform import get_state_dir
from claudeshell.session import SessionRegistry

logger = logging.getLogger("claudeshell.relay")

# Standard MIME types for static file serving
_MIME_MAP: dict[str, str] = {
    ".html": "text/html; charset=utf-8",
    ".js": "application/javascript; charset=utf-8",
    ".css": "text/css; charset=utf-8",
    ".json": "application/json; charset=utf-8",
    ".png": "image/png",
    ".ico": "image/x-icon",
    ".svg": "image/svg+xml",
    ".woff": "font/woff",
    ".woff2": "font/woff2",
    ".ttf": "font/ttf",
    ".map": "application/json",
}


def _content_type(path: Path) -> str:
    """Return the Content-Type for a file path."""
    return _MIME_MAP.get(path.suffix.lower(), "application/octet-stream")


def _get_process_creation_time(pid: int) -> float | None:
    """Return the creation timestamp of a process (Windows only).

    Returns seconds since epoch, or None if the process doesn't exist.
    Used to detect PID reuse: Windows aggressively recycles PIDs, so
    checking existence alone isn't sufficient.
    """
    import ctypes
    import ctypes.wintypes

    PROCESS_QUERY_LIMITED_INFORMATION = 0x1000
    handle = ctypes.windll.kernel32.OpenProcess(
        PROCESS_QUERY_LIMITED_INFORMATION, False, pid
    )
    if not handle:
        return None

    try:
        creation = ctypes.wintypes.FILETIME()
        exit_time = ctypes.wintypes.FILETIME()
        kernel_time = ctypes.wintypes.FILETIME()
        user_time = ctypes.wintypes.FILETIME()

        ok = ctypes.windll.kernel32.GetProcessTimes(
            handle,
            ctypes.byref(creation),
            ctypes.byref(exit_time),
            ctypes.byref(kernel_time),
            ctypes.byref(user_time),
        )
        if not ok:
            return None

        # FILETIME is 100-nanosecond intervals since 1601-01-01
        ft = (creation.dwHighDateTime << 32) | creation.dwLowDateTime
        # Convert to Unix epoch (difference is 116444736000000000 * 100ns)
        return (ft - 116444736000000000) / 10_000_000
    finally:
        ctypes.windll.kernel32.CloseHandle(handle)


def _pid_exists(pid: int) -> bool:
    """Check whether a process ID is still alive (cross-platform)."""
    import sys

    if sys.platform == "win32":
        return _get_process_creation_time(pid) is not None

    # Unix: signal 0 checks existence without sending a signal
    try:
        os.kill(pid, 0)
    except OSError:
        return False
    except SystemError:
        return False
    return True


def _is_same_process(pid: int, expected_creation_time: float | None) -> bool:
    """Check if *pid* is still the same process we originally recorded.

    On Windows, PIDs are recycled. We compare the process creation timestamp
    to detect when a PID has been reassigned to a different process.
    On Unix, just checks existence (PID reuse is rare with 32-bit PIDs).
    """
    import sys

    if sys.platform == "win32" and expected_creation_time is not None:
        ct = _get_process_creation_time(pid)
        if ct is None:
            return False  # Process doesn't exist
        # Allow 2-second tolerance for clock skew
        return abs(ct - expected_creation_time) < 2.0

    return _pid_exists(pid)


class ClaudeShellRelay:
    """DCC-agnostic multi-session Claude terminal relay.

    Multiplexes HTTP REST API, static file serving, and WebSocket
    connections on a single port via the websockets library.
    """

    def __init__(
        self,
        port: int,
        token: str,
        web_dir: str | Path,
        docs_dir: str | Path | None = None,
        mcp_config: dict[str, Any] | str | None = None,
        parent_pid: int | None = None,
        version: str = "3.0.0",
        log_dir: Path | None = None,
    ) -> None:
        self.port = port
        self.token = token
        self.web_dir = Path(web_dir) if web_dir else Path()
        self.docs_dir = Path(docs_dir) if docs_dir else None
        self.version = version
        self.log_dir = log_dir or get_state_dir() / "logs"
        self.parent_pid = parent_pid if parent_pid and parent_pid > 0 else None

        # Parse mcp_config from JSON string if needed
        if isinstance(mcp_config, str) and mcp_config:
            try:
                self.mcp_config: dict[str, Any] | None = json.loads(mcp_config)
            except json.JSONDecodeError:
                logger.warning("Invalid mcp_config JSON, ignoring: %s", mcp_config)
                self.mcp_config = None
        else:
            self.mcp_config = mcp_config  # type: ignore[assignment]

        self.registry = SessionRegistry()
        self._server: Any = None
        self._started_at = time.monotonic()
        self._state_file: Path | None = None

    # ------------------------------------------------------------------
    # Public API
    # ------------------------------------------------------------------

    async def run(self) -> None:
        """Start HTTP + WebSocket servers and run until shutdown."""
        self._started_at = time.monotonic()

        self._server = await websockets.serve(
            self._ws_handler,
            "127.0.0.1",
            self.port,
            process_request=self._process_request,
        )

        # Start parent PID monitor
        if self.parent_pid:
            asyncio.create_task(self._monitor_parent())

        self._write_state_file()

        logger.info(
            "ClaudeShell relay v%s listening on 127.0.0.1:%d",
            self.version,
            self.port,
        )

        # Run forever (or until parent dies / KeyboardInterrupt)
        try:
            await asyncio.Future()
        finally:
            await self.shutdown()

    async def shutdown(self) -> None:
        """Shut down all sessions and the server."""
        self.registry.shutdown_all()
        if self._server:
            self._server.close()
            await self._server.wait_closed()
            self._server = None
        self._cleanup_state_file()
        logger.info("Relay shut down")

    # ------------------------------------------------------------------
    # HTTP request handler (process_request hook)
    # ------------------------------------------------------------------

    async def _process_request(
        self,
        connection: Any,
        request: Any,
    ) -> Response | None:
        """Intercept HTTP requests before WebSocket upgrade.

        Returns a Response to short-circuit (serve HTTP).
        Returns None to proceed with WebSocket upgrade.
        """
        path = request.path
        parsed = urlparse(path)
        clean_path = parsed.path

        # ── WebSocket upgrade: let it pass through ──
        if clean_path == "/ws":
            return None

        # ── REST API: /api/* ──
        if clean_path.startswith("/api/"):
            return await self._handle_api(path, request)

        # ── Static files ──
        return self._handle_static(clean_path)

    # ------------------------------------------------------------------
    # REST API handlers
    # ------------------------------------------------------------------

    async def _handle_api(self, path: str, request: Any) -> Response:
        """Route API requests.

        All endpoints are GET-only because the websockets library rejects
        non-GET methods at the HTTP parser level.  Parameters are passed
        via query string instead of request body.
        """
        # Parse query string from the full path
        parsed = urlparse(path)
        clean = parsed.path
        query = parse_qs(parsed.query)

        def _qfirst(key: str, default: str = "") -> str:
            """Get first value for a query parameter."""
            vals = query.get(key)
            return vals[0] if vals else default

        # /api/status — no auth required
        if clean == "/api/status":
            return self._json_response(
                200,
                {
                    "version": self.version,
                    "sessions": self.registry.list_sessions(),
                    "uptime": round(time.monotonic() - self._started_at, 1),
                },
            )

        # All other /api/ routes require auth (via query param or header)
        auth_token = _qfirst("token")
        if not auth_token:
            for name, value in request.headers.raw_items():
                if name.lower() == "authorization":
                    auth_token = value.replace("Bearer ", "") if value else ""
                    break

        if not validate_token(auth_token, self.token):
            return self._json_response(401, {"error": "Unauthorized"})

        # ── GET /api/session/create?cwd=...&project_name=... ──
        if clean == "/api/session/create":
            cwd = _qfirst("cwd", ".")
            project_name = _qfirst("project_name", "")

            session = self.registry.create(
                cwd=cwd,
                project_name=project_name,
                mcp_config=self.mcp_config,
                docs_dir=self.docs_dir,
            )
            try:
                await session.start()
            except Exception as exc:
                self.registry.shutdown(session.id)
                logger.error("Failed to start session: %s", exc)
                return self._json_response(500, {"error": f"Failed to start session: {exc}"})

            return self._json_response(200, {"session_id": session.id})

        # ── GET /api/session/{id}/shutdown ──
        if clean.startswith("/api/session/") and clean.endswith("/shutdown"):
            session_id = clean.split("/")[3]
            found = self.registry.shutdown(session_id)
            if found:
                return self._json_response(200, {"status": "shutdown"})
            return self._json_response(404, {"error": "Session not found"})

        # ── GET /api/session/{id}/status ──
        if clean.startswith("/api/session/") and clean.endswith("/status"):
            session_id = clean.split("/")[3]
            session = self.registry.get(session_id)
            if not session:
                return self._json_response(404, {"error": "Session not found"})
            return self._json_response(
                200,
                {
                    "id": session.id,
                    "alive": session.alive,
                    "project_name": session.project_name,
                    "clients": session.client_count,
                },
            )

        return self._json_response(404, {"error": "Not found"})

    # ------------------------------------------------------------------
    # WebSocket handler
    # ------------------------------------------------------------------

    async def _ws_handler(self, websocket: Any) -> None:
        """Handle WebSocket connection for a terminal session."""
        # Parse session_id and token from query params: /ws?session=X&token=Y
        raw_path = websocket.request.path if hasattr(websocket, "request") else ""
        query_string = raw_path.split("?", 1)[-1] if "?" in raw_path else ""
        query = parse_qs(query_string)

        session_id = query.get("session", [None])[0]
        ws_token = query.get("token", [None])[0]

        if not validate_token(ws_token, self.token):
            await websocket.close(4001, "Unauthorized")
            return

        session = self.registry.get(session_id)
        if not session:
            await websocket.close(4004, "Session not found")
            return

        # Register WebSocket client
        session.add_client(websocket)
        logger.info(
            "WS client connected to session %s (total: %d)",
            session_id,
            session.client_count,
        )

        try:
            # Send scrollback buffer
            scrollback = session.get_scrollback()
            if scrollback:
                await websocket.send(
                    json.dumps(
                        {
                            "type": "output",
                            "data": scrollback,
                        }
                    )
                )

            # Proxy I/O
            async for message in websocket:
                try:
                    msg = json.loads(message)
                except json.JSONDecodeError:
                    continue

                msg_type = msg.get("type")

                if msg_type == "input":
                    session.write_input(msg.get("data", ""))
                elif msg_type == "resize":
                    cols = int(msg.get("cols", 120))
                    rows = int(msg.get("rows", 30))
                    session.resize(cols, rows)
                elif msg_type == "restart":
                    await session.restart()
                elif msg_type == "ping":
                    await websocket.send(json.dumps({"type": "pong"}))

        except websockets.exceptions.ConnectionClosed:
            pass

        finally:
            session.remove_client(websocket)
            logger.info(
                "WS client disconnected from session %s (remaining: %d)",
                session_id,
                session.client_count,
            )

    # ------------------------------------------------------------------
    # Parent PID monitoring
    # ------------------------------------------------------------------

    async def _monitor_parent(self) -> None:
        """Watch parent PID, self-terminate if it dies or is recycled.

        Snapshots the parent's process creation time at startup, then polls
        every 3 seconds.  If the PID no longer exists, *or* the PID now
        belongs to a different process (Windows recycles PIDs aggressively),
        the relay shuts itself down.
        """
        pid = self.parent_pid
        if pid is None:
            return

        # Snapshot the parent's creation time so we can detect PID reuse
        parent_creation_time = _get_process_creation_time(pid) if os.name == "nt" else None
        logger.info(
            "Monitoring parent PID %d (creation_time=%.3f)",
            pid,
            parent_creation_time or 0.0,
        )

        while True:
            await asyncio.sleep(3)
            if not _is_same_process(pid, parent_creation_time):
                logger.warning("Parent PID %d died or was recycled — shutting down relay", pid)
                self.registry.shutdown_all()
                self._cleanup_state_file()
                # Force the event loop to stop
                loop = asyncio.get_running_loop()
                loop.call_soon(loop.stop)
                return

    # ------------------------------------------------------------------
    # State file management
    # ------------------------------------------------------------------

    def _write_state_file(self) -> None:
        """Write relay state to platform-appropriate location."""
        from datetime import UTC, datetime

        state_dir = get_state_dir() / "relay"
        state_dir.mkdir(parents=True, exist_ok=True)
        self._state_file = state_dir / f"relay-{self.port}.json"
        self._state_file.write_text(
            json.dumps(
                {
                    "port": self.port,
                    "pid": os.getpid(),
                    "version": self.version,
                    "parent_pid": self.parent_pid,
                    "started_at": datetime.now(tz=UTC).isoformat(),
                }
            )
        )
        logger.debug("State file written: %s", self._state_file)

    def _cleanup_state_file(self) -> None:
        """Remove the state file on shutdown."""
        if self._state_file and self._state_file.exists():
            try:
                self._state_file.unlink()
                logger.debug("State file removed: %s", self._state_file)
            except OSError:
                pass

    # ------------------------------------------------------------------
    # Static file serving
    # ------------------------------------------------------------------

    def _handle_static(self, path: str) -> Response:
        """Serve static frontend files from web_dir."""
        if not self.web_dir or not self.web_dir.is_dir():
            return self._json_response(404, {"error": "No web directory configured"})

        # Default document
        if path in ("/", "/index.html"):
            path = "/terminal.html"

        # Security: prevent directory traversal
        try:
            file_path = (self.web_dir / path.lstrip("/")).resolve()
            if not str(file_path).startswith(str(self.web_dir.resolve())):
                return self._json_response(403, {"error": "Forbidden"})
        except (ValueError, OSError):
            return self._json_response(400, {"error": "Bad request"})

        if file_path.exists() and file_path.is_file():
            content = file_path.read_bytes()
            ct = _content_type(file_path)
            return Response(
                200,
                "OK",
                websockets.datastructures.Headers(
                    [("Content-Type", ct), ("Content-Length", str(len(content)))]
                ),
                content,
            )

        return self._json_response(404, {"error": f"File not found: {path}"})

    # ------------------------------------------------------------------
    # Response helpers
    # ------------------------------------------------------------------

    @staticmethod
    def _json_response(status: int, data: dict[str, Any]) -> Response:
        """Build an HTTP JSON response."""
        body = json.dumps(data).encode("utf-8")
        reason = "OK" if status < 400 else "Error"
        return Response(
            status,
            reason,
            websockets.datastructures.Headers(
                [
                    ("Content-Type", "application/json; charset=utf-8"),
                    ("Content-Length", str(len(body))),
                ]
            ),
            body,
        )

    @staticmethod
    def _parse_request_body(request: Any) -> dict[str, Any]:
        """Parse JSON body from an HTTP request.

        The ``request`` object from websockets' ``process_request`` hook
        exposes ``.body`` as bytes in v14+.  We handle missing/empty
        bodies gracefully.
        """
        body_bytes = getattr(request, "body", None) or b""
        if isinstance(body_bytes, memoryview):
            body_bytes = bytes(body_bytes)
        if not body_bytes:
            return {}
        try:
            return json.loads(body_bytes)
        except (json.JSONDecodeError, UnicodeDecodeError):
            return {}
