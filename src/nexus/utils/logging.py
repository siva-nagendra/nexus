"""Structured JSON logging with rotation and command latency tracking."""

from __future__ import annotations

import json
import logging
from logging.handlers import RotatingFileHandler
from pathlib import Path


class JsonFormatter(logging.Formatter):
    """Structured JSON log lines for machine parsing.

    Emits one JSON object per line. Includes standard fields (timestamp,
    level, logger, message) plus optional extras: command, duration_ms,
    transport — set via ``extra={}`` on log calls from ConnectionManager.
    """

    def format(self, record: logging.LogRecord) -> str:
        log_entry: dict[str, object] = {
            "timestamp": self.formatTime(record),
            "level": record.levelname,
            "logger": record.name,
            "message": record.getMessage(),
        }
        # Structured extras from ConnectionManager.execute()
        for key in ("command", "duration_ms", "transport"):
            value = getattr(record, key, None)
            if value is not None:
                log_entry[key] = value
        # Include exception info if present
        if record.exc_info:
            log_entry["exception"] = self.formatException(record.exc_info)
        return json.dumps(log_entry)


_LOG_DIR_DEFAULT = Path.home() / ".nexus" / "logs"


def setup_logging(
    debug: bool = False,
    log_dir: Path | None = None,
    level: str | None = None,
) -> None:
    """Configure structured logging to a rotating JSON file only.

    Args:
        debug: Enable DEBUG level (overridden by *level* if set).
        log_dir: Directory for log files. Defaults to ``~/.nexus/logs``.
        level: Explicit level name (e.g. ``"INFO"``). Takes precedence over *debug*.

    Log layout:
    - **File** (``nexus.log``): JSON lines via :class:`RotatingFileHandler`
      (10 MB per file, 5 backups). Contains all levels (DEBUG when *debug* is set).

    No StreamHandlers are attached because MCP uses stdin/stdout for
    JSON-RPC; any console output would corrupt the transport.
    """
    effective_level = logging.DEBUG if debug else logging.INFO
    if level is not None:
        effective_level = getattr(logging, level.upper(), logging.INFO)

    root = logging.getLogger("nexus")
    root.setLevel(effective_level)

    # Avoid duplicate handlers on repeated calls (e.g. tests)
    if root.handlers:
        return

    # --- Rotating JSON file handler ---
    target_dir = log_dir or _LOG_DIR_DEFAULT
    target_dir.mkdir(parents=True, exist_ok=True)

    file_handler = RotatingFileHandler(
        target_dir / "nexus.log",
        maxBytes=10 * 1024 * 1024,  # 10 MB
        backupCount=5,
        encoding="utf-8",
    )
    file_handler.setLevel(logging.DEBUG)
    file_handler.setFormatter(JsonFormatter())
    root.addHandler(file_handler)
