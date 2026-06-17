"""Tests for native TCP transport reliability.

Tests mock TCP connections with length-prefixed framing, persistent connection
reuse, request ID correlation, health check probes, error classification, and
retry behavior.
"""

from __future__ import annotations

import asyncio
import struct
from unittest.mock import AsyncMock, MagicMock, patch

import orjson
import pytest

from nexus.connection.errors import (
    PERMANENT_CODES,
    TRANSIENT_CODES,
    PermanentError,
    TransientError,
    classify_error_code,
)
from nexus.connection.manager import ConnectionManager, _extract_error_code
from nexus.connection.native import NativeTransport
from nexus.connection.protocol import LENGTH_PREFIX_FORMAT, LENGTH_PREFIX_SIZE
from nexus.models.commands import CommandResult

# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------


class FakeTCPServer:
    """In-process TCP server using length-prefixed JSON framing."""

    def __init__(self, response_fn=None):
        self._server = None
        self.host = "127.0.0.1"
        self.port = 0  # OS-assigned
        self._response_fn = response_fn or self._default_response
        self.received_commands: list[dict] = []

    @staticmethod
    def _default_response(request: dict) -> dict:
        return {
            "id": request.get("id", ""),
            "success": True,
            "data": {"echo": True},
        }

    async def _handle_client(
        self, reader: asyncio.StreamReader, writer: asyncio.StreamWriter
    ) -> None:
        try:
            while True:
                # Read 4-byte length prefix
                header = await reader.readexactly(LENGTH_PREFIX_SIZE)
                payload_len = struct.unpack(LENGTH_PREFIX_FORMAT, header)[0]
                # Read exact payload
                payload = await reader.readexactly(payload_len)
                request = orjson.loads(payload)
                self.received_commands.append(request)

                # Generate response
                response = self._response_fn(request)
                resp_bytes = orjson.dumps(response)
                # Send length-prefixed response
                writer.write(struct.pack(LENGTH_PREFIX_FORMAT, len(resp_bytes)) + resp_bytes)
                await writer.drain()
        except (ConnectionError, asyncio.CancelledError, asyncio.IncompleteReadError):
            pass
        finally:
            writer.close()

    async def start(self) -> None:
        self._server = await asyncio.start_server(self._handle_client, self.host, 0)
        sock = self._server.sockets[0]
        self.port = sock.getsockname()[1]

    async def stop(self) -> None:
        if self._server:
            self._server.close()
            await self._server.wait_closed()


# ---------------------------------------------------------------------------
# NativeTransport tests
# ---------------------------------------------------------------------------


class TestNativeTransportPersistentConnection:
    """Verify persistent TCP connection reuse across multiple commands."""

    @pytest.mark.asyncio
    async def test_persistent_connection_reused(self):
        """Multiple execute() calls reuse the same TCP connection."""
        server = FakeTCPServer()
        await server.start()
        try:
            transport = NativeTransport(server.host, server.port)
            await transport.connect()

            r1 = await transport.execute("system.echo", {"n": 1}, timeout=5.0)
            r2 = await transport.execute("system.echo", {"n": 2}, timeout=5.0)

            assert r1.success
            assert r2.success
            assert len(server.received_commands) == 2
            assert server.received_commands[0]["params"]["n"] == 1
            assert server.received_commands[1]["params"]["n"] == 2

            await transport.disconnect()
        finally:
            await server.stop()

    @pytest.mark.asyncio
    async def test_connect_sets_state(self):
        """After connect(), is_connected() returns True."""
        server = FakeTCPServer()
        await server.start()
        try:
            transport = NativeTransport(server.host, server.port)
            await transport.connect()
            assert await transport.is_connected()
            await transport.disconnect()
            assert not await transport.is_connected()
        finally:
            await server.stop()

    @pytest.mark.asyncio
    async def test_close_alias(self):
        """close() is an alias for disconnect()."""
        server = FakeTCPServer()
        await server.start()
        try:
            transport = NativeTransport(server.host, server.port)
            await transport.connect()
            assert await transport.is_connected()
            await transport.close()
            assert not await transport.is_connected()
        finally:
            await server.stop()


class TestNativeTransportRequestIdCorrelation:
    """Verify request ID is sent and validated in responses."""

    @pytest.mark.asyncio
    async def test_request_id_sent_and_echoed(self):
        """Server receives request ID and transport accepts matching response."""
        server = FakeTCPServer()
        await server.start()
        try:
            transport = NativeTransport(server.host, server.port)
            await transport.connect()

            result = await transport.execute("actor.spawn", {"class": "Cube"}, timeout=5.0)
            assert result.success

            cmd = server.received_commands[0]
            assert "id" in cmd
            assert len(cmd["id"]) > 0

            await transport.disconnect()
        finally:
            await server.stop()

    @pytest.mark.asyncio
    async def test_request_id_mismatch_raises(self):
        """Mismatched response ID raises TransientError."""

        def bad_id_response(request: dict) -> dict:
            return {
                "id": "wrong-id-does-not-match",
                "success": True,
                "data": {},
            }

        server = FakeTCPServer(response_fn=bad_id_response)
        await server.start()
        try:
            transport = NativeTransport(server.host, server.port)
            await transport.connect()

            with pytest.raises(TransientError, match="mismatch"):
                await transport.execute("actor.spawn", {}, timeout=5.0)

            await transport.disconnect()
        finally:
            await server.stop()


class TestNativeTransportHealthCheck:
    """Verify health_check() probe behavior."""

    @pytest.mark.asyncio
    async def test_health_check_success(self):
        """health_check() returns True when server responds."""
        server = FakeTCPServer()
        await server.start()
        try:
            transport = NativeTransport(server.host, server.port)
            await transport.connect()

            healthy = await transport.health_check()
            assert healthy is True

            assert server.received_commands[-1]["type"] == "system.echo"

            await transport.disconnect()
        finally:
            await server.stop()

    @pytest.mark.asyncio
    async def test_health_check_not_connected(self):
        """health_check() returns False when not connected."""
        transport = NativeTransport("127.0.0.1", 1)
        healthy = await transport.health_check()
        assert healthy is False


class TestNativeTransportConnectionFailure:
    """Verify behavior when connection fails."""

    @pytest.mark.asyncio
    async def test_connect_failure_raises(self):
        """Connecting to a non-existent server raises ConnectionError."""
        transport = NativeTransport("127.0.0.1", 1)
        with pytest.raises(ConnectionError):
            await transport.connect()

    @pytest.mark.asyncio
    async def test_execute_without_connect_raises(self):
        """execute() without prior connect() raises ConnectionError."""
        transport = NativeTransport("127.0.0.1", 1)
        with pytest.raises(ConnectionError):
            await transport.execute("actor.spawn", {}, timeout=1.0)


# ---------------------------------------------------------------------------
# Error classification tests
# ---------------------------------------------------------------------------


class TestErrorClassification:
    """Verify error code classification logic."""

    def test_transient_codes_classified(self):
        for code in TRANSIENT_CODES:
            assert classify_error_code(code) is TransientError

    def test_permanent_codes_classified(self):
        for code in PERMANENT_CODES:
            assert classify_error_code(code) is PermanentError

    def test_unknown_code_returns_none(self):
        assert classify_error_code("SOME_UNKNOWN_CODE") is None
        assert classify_error_code("") is None

    def test_extract_error_code_from_message(self):
        assert _extract_error_code("ACTOR_NOT_FOUND: No actor at path /foo") == "ACTOR_NOT_FOUND"
        assert _extract_error_code("TIMEOUT") == "TIMEOUT"
        assert _extract_error_code("") == ""
        assert _extract_error_code("Some human readable error") == ""


# ---------------------------------------------------------------------------
# ConnectionManager retry tests
# ---------------------------------------------------------------------------


class TestConnectionManagerRetry:
    """Verify retry behavior in ConnectionManager."""

    @pytest.mark.asyncio
    async def test_retry_on_connection_error(self):
        """ConnectionManager retries on ConnectionError up to max_retries."""
        mgr = ConnectionManager(max_retries=2)

        call_count = 0

        async def mock_execute(cmd, params=None, timeout=None):
            nonlocal call_count
            call_count += 1
            if call_count < 3:
                raise ConnectionError("test connection lost")
            return CommandResult(success=True, data={"ok": True}, transport_used="native")

        mock_transport = AsyncMock()
        mock_transport.is_connected = AsyncMock(return_value=True)
        mock_transport.execute = mock_execute
        mock_transport.disconnect = AsyncMock()

        mgr._native = mock_transport
        mgr._active_transport = MagicMock()
        mgr._active_transport.value = "native"

        with patch.object(mgr, "_get_transport", return_value=mock_transport):
            with patch.object(mgr, "_invalidate_transport", new_callable=AsyncMock):
                result = await mgr.execute("actor.spawn", {"class": "Cube"})

        assert result.success
        assert call_count == 3  # 2 retries + 1 success

    @pytest.mark.asyncio
    async def test_no_retry_on_permanent_error(self):
        """ConnectionManager does not retry permanent errors."""
        mgr = ConnectionManager(max_retries=2)

        async def mock_execute(cmd, params=None, timeout=None):
            return CommandResult(
                success=False,
                error="ACTOR_NOT_FOUND: No actor at path /foo",
                transport_used="native",
            )

        mock_transport = AsyncMock()
        mock_transport.is_connected = AsyncMock(return_value=True)
        mock_transport.execute = mock_execute

        mgr._native = mock_transport
        mgr._active_transport = MagicMock()
        mgr._active_transport.value = "native"

        with patch.object(mgr, "_get_transport", return_value=mock_transport):
            result = await mgr.execute("actor.get_transform", {"actor_path": "/foo"})

        assert not result.success
        assert "ACTOR_NOT_FOUND" in result.error
