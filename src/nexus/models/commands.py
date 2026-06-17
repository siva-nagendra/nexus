"""Wire protocol command and response envelopes."""

from __future__ import annotations

import uuid
from typing import Any

from pydantic import BaseModel, ConfigDict, Field


class CommandEnvelope(BaseModel):
    """JSON command sent from Python to the C++ plugin over TCP."""

    model_config = ConfigDict(extra="ignore")

    id: str = Field(default_factory=lambda: str(uuid.uuid4()))
    type: str = Field(description="Dot-separated command type, e.g. 'actor.spawn'")
    params: dict[str, Any] = Field(default_factory=dict)


class ErrorDetail(BaseModel):
    """Structured error from UE."""

    model_config = ConfigDict(extra="ignore")

    code: str = Field(description="Machine-readable error code, e.g. 'ACTOR_NOT_FOUND'")
    message: str = Field(description="Human-readable error description")


class ResponseEnvelope(BaseModel):
    """JSON response received from the C++ plugin over TCP."""

    model_config = ConfigDict(extra="ignore")

    id: str = ""
    success: bool = True
    data: dict[str, Any] | None = None
    error: ErrorDetail | None = None

    @classmethod
    def from_wire(cls, raw: bytes) -> ResponseEnvelope:
        """Parse from raw bytes (length prefix already stripped)."""
        return cls.model_validate_json(raw)


class CommandResult(BaseModel):
    """Internal result type used by the transport layer.

    Normalizes responses from the native TCP transport into a consistent
    shape for tool code to consume.
    """

    model_config = ConfigDict(extra="ignore")

    success: bool
    data: dict[str, Any] = Field(default_factory=dict)
    error: str = ""
    error_code: str = Field("", description="Machine-readable error code from C++ plugin")
    transport_used: str = Field("native", description="Which transport produced this result")
    duration_ms: float = Field(0.0, description="Round-trip latency in milliseconds")
