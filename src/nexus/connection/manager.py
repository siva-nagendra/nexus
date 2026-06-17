"""ConnectionManager — native TCP transport with reconnect and exponential backoff.

Classifies errors as transient (retry up to 2x with 500ms backoff) or permanent
(fail immediately). Single transport: native TCP to the Nexus C++ plugin.
"""

from __future__ import annotations

import asyncio
import logging
import time
from typing import Any

from nexus.connection.base import TransportType, UnrealConnection
from nexus.connection.errors import (
    PermanentError,
    TransientError,
    classify_error_code,
)
from nexus.connection.native import NativeTransport
from nexus.connection.protocol import (
    BASE_RETRY_DELAY,
    DEFAULT_NATIVE_PORT,
    MAX_RETRIES,
    MAX_RETRY_DELAY,
)
from nexus.memory.call_logger import get_call_logger
from nexus.models.commands import CommandResult

logger = logging.getLogger("nexus.connection")


class ConnectionManager:
    """Manages native TCP transport lifecycle with reconnect and retry.

    Strategy:
    1. Connect to Nexus C++ plugin over TCP (native transport)
    2. Retry with exponential backoff on transient failures
    3. AsyncIO lock prevents concurrent command corruption
    """

    def __init__(
        self,
        native_host: str = "127.0.0.1",
        native_port: int = DEFAULT_NATIVE_PORT,
        max_retries: int = MAX_RETRIES,
    ) -> None:
        self.native_host = native_host
        self.native_port = native_port
        self.max_retries = max_retries
        self._native: NativeTransport | None = None
        self._lock = asyncio.Lock()
        self._active_transport: TransportType | None = None
        self._command_count: int = 0
        self._total_latency_ms: float = 0.0
        self._error_count: int = 0

    async def initialize(self) -> None:
        """Set up native transport. Called during server lifespan startup."""
        self._native = NativeTransport(self.native_host, self.native_port)
        try:
            await self._native.connect()
            self._active_transport = TransportType.NATIVE
            logger.info("Native transport connected on port %d", self.native_port)
        except ConnectionError:
            logger.warning(
                "Native transport unavailable — Nexus C++ plugin not running on port %d",
                self.native_port,
            )
            self._native = None

    async def shutdown(self) -> None:
        """Clean up transport. Called during server lifespan shutdown."""
        if self._native:
            await self._native.disconnect()
            self._native = None
        self._active_transport = None
        logger.info("Transport shut down")

    @property
    def active_transport_type(self) -> TransportType | None:
        """Currently active transport type."""
        return self._active_transport

    @property
    def stats(self) -> dict[str, Any]:
        """Connection statistics for health resource."""
        avg_latency = (
            round(self._total_latency_ms / self._command_count, 1)
            if self._command_count > 0
            else 0.0
        )
        return {
            "command_count": self._command_count,
            "error_count": self._error_count,
            "avg_latency_ms": avg_latency,
        }

    async def execute(
        self,
        command_type: str,
        params: dict[str, Any] | None = None,
        timeout: float | None = None,
    ) -> CommandResult:
        """Execute a command via native TCP transport with retry.

        Classifies errors:
        - TransientError / ConnectionError: retry up to max_retries with backoff
        - PermanentError: fail immediately with clear message
        """
        async with self._lock:
            last_error = ""
            t0 = time.monotonic()
            for attempt in range(self.max_retries + 1):
                try:
                    transport = await self._get_transport()
                    result = await transport.execute(command_type, params or {}, timeout)

                    duration_ms = round((time.monotonic() - t0) * 1000, 1)
                    self._command_count += 1
                    self._total_latency_ms += duration_ms

                    logger.info(
                        "Command %s completed (success=%s)",
                        command_type,
                        result.success,
                        extra={
                            "command": command_type,
                            "duration_ms": duration_ms,
                            "transport": "native",
                        },
                    )

                    # Log every call for RL training data
                    get_call_logger().log_call(
                        command=command_type,
                        params=params,
                        response=result.data,
                        success=result.success,
                        error=result.error,
                        duration_ms=duration_ms,
                        transport="tcp",
                    )

                    # Classify error responses from the C++ plugin
                    if not result.success:
                        self._error_count += 1
                        error_code = _extract_error_code(result.error)
                        if error_code:
                            err_cls = classify_error_code(error_code)
                            if err_cls is PermanentError:
                                return result
                            if err_cls is TransientError and attempt < self.max_retries:
                                raise TransientError(result.error)

                    return result

                except (ConnectionError, TransientError) as exc:
                    last_error = str(exc)
                    logger.warning(
                        "Command %s failed (attempt %d/%d): %s",
                        command_type,
                        attempt + 1,
                        self.max_retries + 1,
                        exc,
                    )
                    if isinstance(exc, ConnectionError):
                        await self._invalidate_transport()

                    if attempt < self.max_retries:
                        delay = min(
                            BASE_RETRY_DELAY * (2**attempt),
                            MAX_RETRY_DELAY,
                        )
                        logger.info("Retrying in %.1fs...", delay)
                        await asyncio.sleep(delay)

                except PermanentError as exc:
                    duration_ms = round((time.monotonic() - t0) * 1000, 1)
                    self._error_count += 1
                    logger.error(
                        "Command %s permanent failure: %s",
                        command_type,
                        exc,
                        extra={
                            "command": command_type,
                            "duration_ms": duration_ms,
                            "transport": "native",
                        },
                    )
                    return CommandResult(
                        success=False,
                        error=str(exc),
                        transport_used="native",
                    )

            self._error_count += 1
            return CommandResult(
                success=False,
                error=(
                    f"Command '{command_type}' failed after {self.max_retries + 1} "
                    f"attempts: {last_error}"
                ),
                transport_used="none",
            )

    async def _get_transport(self) -> UnrealConnection:
        """Get the native transport, reconnecting if needed."""
        if self._native and await self._native.is_connected():
            self._active_transport = TransportType.NATIVE
            return self._native

        # Try to reconnect
        try:
            native = NativeTransport(self.native_host, self.native_port)
            await native.connect()
            self._native = native
            self._active_transport = TransportType.NATIVE
            logger.info("Reconnected to native transport")
            return self._native
        except ConnectionError:
            pass

        raise ConnectionError(
            "Cannot connect to Nexus C++ plugin — is Unreal Engine running "
            f"with the Nexus plugin on port {self.native_port}?"
        )

    async def _invalidate_transport(self) -> None:
        """Mark transport as broken, forcing reconnect on next use."""
        if self._native:
            await self._native.disconnect()
            self._native = None
        self._active_transport = None


def _extract_error_code(error_message: str) -> str:
    """Extract an error code from an error message string.

    The C++ plugin returns error codes like 'ACTOR_NOT_FOUND', 'TIMEOUT', etc.
    These may appear as the full message or as a prefix before a colon.
    """
    if not error_message:
        return ""
    stripped = error_message.strip()
    first_word = stripped.split(":")[0].strip().split(" ")[0].strip()
    if first_word.replace("_", "").isalpha() and first_word.isupper():
        return first_word
    return ""
