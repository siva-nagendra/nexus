"""Native TCP transport to the Nexus C++ plugin (port 13377).

Uses persistent connection with TCP_NODELAY, SO_KEEPALIVE, and 64KB buffers.
Protocol: length-prefixed JSON — 4-byte big-endian uint32 length followed by JSON payload.
Request ID correlation ensures responses match their requests.
"""

from __future__ import annotations

import asyncio
import logging
from typing import Any

import orjson

from nexus.connection.base import TransportType, UnrealConnection
from nexus.connection.errors import TransientError
from nexus.connection.protocol import (
    DEFAULT_NATIVE_PORT,
    LENGTH_PREFIX_SIZE,
    TCP_BUFFER_SIZE,
    decode_length_prefix,
    encode_command,
)
from nexus.models.commands import CommandResult

logger = logging.getLogger("nexus.native")


class NativeTransport(UnrealConnection):
    """TCP client connecting to the Nexus C++ plugin in Unreal Editor.

    The C++ plugin listens on a TCP port and accepts length-prefixed JSON
    commands, dispatching them to registered handler subsystems.

    Features:
    - Persistent TCP connection (no reconnect per command)
    - asyncio.Lock serializes requests (game thread is single-threaded)
    - Request ID correlation verifies responses match requests
    - Length-prefixed framing for efficient single-pass reads
    - health_check() for liveness probing
    """

    transport_type = TransportType.NATIVE

    def __init__(self, host: str = "127.0.0.1", port: int = DEFAULT_NATIVE_PORT) -> None:
        self.host = host
        self.port = port
        self._reader: asyncio.StreamReader | None = None
        self._writer: asyncio.StreamWriter | None = None
        self._lock = asyncio.Lock()
        self._request_id = 0

    async def connect(self) -> None:
        """Open TCP connection with optimized socket options."""
        try:
            self._reader, self._writer = await asyncio.wait_for(
                asyncio.open_connection(self.host, self.port),
                timeout=10.0,
            )
            # Apply socket options
            sock = self._writer.transport.get_extra_info("socket")
            if sock is not None:
                import socket

                sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
                sock.setsockopt(socket.SOL_SOCKET, socket.SO_KEEPALIVE, 1)
                try:
                    sock.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, TCP_BUFFER_SIZE)
                    sock.setsockopt(socket.SOL_SOCKET, socket.SO_SNDBUF, TCP_BUFFER_SIZE)
                except OSError:
                    pass
            self._request_id = 0
            logger.info("Connected to Nexus C++ plugin at %s:%d", self.host, self.port)
        except (TimeoutError, OSError) as exc:
            self._reader = None
            self._writer = None
            raise ConnectionError(
                f"Cannot connect to Nexus C++ plugin at {self.host}:{self.port}: {exc}"
            ) from exc

    async def disconnect(self) -> None:
        """Close the TCP connection."""
        if self._writer is not None:
            try:
                self._writer.close()
                await self._writer.wait_closed()
            except OSError:
                pass
            finally:
                self._writer = None
                self._reader = None
            logger.debug("Disconnected from Nexus C++ plugin")

    async def close(self) -> None:
        """Alias for disconnect() — close the persistent connection."""
        await self.disconnect()

    async def is_connected(self) -> bool:
        """Check if the writer is open and usable."""
        if self._writer is None or self._writer.is_closing():
            return False
        try:
            self._writer.write(b"")
            await self._writer.drain()
            return True
        except OSError:
            return False

    async def health_check(self) -> bool:
        """Ping the C++ plugin with system.echo to verify liveness."""
        try:
            result = await self.execute("system.echo", {"ping": True}, timeout=5.0)
            return result.success
        except Exception:
            return False

    async def execute(
        self,
        command_type: str,
        params: dict[str, Any] | None = None,
        timeout: float | None = None,
    ) -> CommandResult:
        """Send a command over TCP and return the parsed response.

        Uses an asyncio.Lock to serialize requests (the UE game thread is
        single-threaded, so concurrent sends would corrupt the stream).
        Verifies request ID correlation on every response.

        Protocol:
        - Send: [4-byte length][JSON payload]
        - Recv: [4-byte length][JSON payload]
        """
        async with self._lock:
            if self._writer is None or self._reader is None:
                raise ConnectionError("Not connected to Nexus C++ plugin")

            resolved_timeout = self._resolve_timeout(command_type, timeout)
            self._request_id += 1
            cmd_id, frame = encode_command(command_type, params)

            try:
                # Send length-prefixed frame
                self._writer.write(frame)
                await self._writer.drain()
                logger.debug("Sent command %s (id=%s, %d bytes)", command_type, cmd_id, len(frame))

                # Receive: read 4-byte length prefix, then exact payload
                header = await asyncio.wait_for(
                    self._reader.readexactly(LENGTH_PREFIX_SIZE),
                    timeout=resolved_timeout,
                )
                payload_len = decode_length_prefix(header)

                if payload_len > 100 * 1024 * 1024:  # 100MB sanity limit
                    raise TransientError(
                        f"Response payload too large: {payload_len} bytes for {command_type}"
                    )

                payload = await asyncio.wait_for(
                    self._reader.readexactly(payload_len),
                    timeout=resolved_timeout,
                )

                # Parse response with orjson (faster than json.loads)
                resp = orjson.loads(payload)
                logger.debug("Received response for %s (id=%s)", command_type, cmd_id)

                # Verify request ID correlation
                resp_id = resp.get("id", "")
                if resp_id and resp_id != cmd_id:
                    raise TransientError(f"Response ID mismatch: expected {cmd_id}, got {resp_id}")

                return CommandResult(
                    success=resp.get("success", True),
                    data=resp.get("data") or {},
                    error=resp.get("error", {}).get("message", "") if resp.get("error") else "",
                    transport_used="native",
                )

            except asyncio.IncompleteReadError as exc:
                await self.disconnect()
                raise ConnectionError(
                    f"Connection closed mid-read for {command_type}: {exc}"
                ) from exc

            except TimeoutError:
                # After a timeout, the TCP stream is in an unknown state
                # (a stale response may arrive later). Disconnect to force
                # a clean reconnection on the next command.
                await self.disconnect()
                return CommandResult(
                    success=False,
                    error=f"Timeout ({resolved_timeout}s) waiting for response to {command_type}",
                    transport_used="native",
                )
            except (OSError, orjson.JSONDecodeError) as exc:
                # Connection likely broken — mark for reconnect
                await self.disconnect()
                raise ConnectionError(f"Native transport error for {command_type}: {exc}") from exc
