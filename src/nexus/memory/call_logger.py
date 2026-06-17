"""Automatic call/response logging for all MCP tool invocations.

Every command sent through the Nexus MCP server is recorded as a JSONL
entry in ~/.nexus/memory/calls/. This creates a training dataset for
reinforcement learning to refine tool usage patterns over time.

Each entry captures: command, parameters, response, timing, success/failure,
and transport used (TCP, HTTP in-game, or HTTP proxy).
"""

from __future__ import annotations

import json
import logging
import time
from datetime import UTC, datetime
from pathlib import Path
from typing import Any

logger = logging.getLogger("nexus.memory.calls")

CALLS_DIR = Path.home() / ".nexus" / "memory" / "calls"

# Rotation thresholds (shared with memory/store.py constants)
MAX_FILE_SIZE = 50 * 1024 * 1024  # 50 MB
MAX_AGE_DAYS = 30
_MAX_ROTATIONS = 5


class CallLogger:
    """Records every MCP command invocation for reinforcement learning.

    Writes JSONL files partitioned by date (one file per day) to keep
    individual files manageable while preserving chronological order.
    Rotates large files and purges stale logs on startup.
    """

    def __init__(self, calls_dir: Path | None = None) -> None:
        self.calls_dir = calls_dir or CALLS_DIR
        self.calls_dir.mkdir(parents=True, exist_ok=True)
        self._session_id = datetime.now(UTC).strftime("%Y%m%dT%H%M%S")
        self._cleanup_old_files()

    def _cleanup_old_files(self) -> None:
        """Delete JSONL files older than MAX_AGE_DAYS."""
        cutoff = time.time() - (MAX_AGE_DAYS * 86400)
        for jsonl_file in self.calls_dir.glob("*.jsonl"):
            if jsonl_file.stat().st_mtime < cutoff:
                jsonl_file.unlink()
                logger.info("Cleaned up old call log: %s", jsonl_file.name)

    def _rotate_if_needed(self, filepath: Path) -> None:
        """Rotate file if it exceeds MAX_FILE_SIZE.

        Shifts existing rotations (.1 -> .2, .2 -> .3, etc.) so the
        newest overflow is always in .1 and the oldest is discarded.
        """
        if not filepath.exists():
            return
        if filepath.stat().st_size < MAX_FILE_SIZE:
            return
        for rotation_index in range(_MAX_ROTATIONS, 0, -1):
            old_path = filepath.parent / (f"{filepath.stem}.{rotation_index}{filepath.suffix}")
            new_path = filepath.parent / (f"{filepath.stem}.{rotation_index + 1}{filepath.suffix}")
            if old_path.exists():
                old_path.rename(new_path)
        rotated_path = filepath.parent / f"{filepath.stem}.1{filepath.suffix}"
        filepath.rename(rotated_path)
        logger.info("Rotated %s (exceeded %d bytes)", filepath.name, MAX_FILE_SIZE)

    def log_call(
        self,
        command: str,
        params: dict[str, Any] | None,
        response: dict[str, Any] | None,
        success: bool,
        error: str = "",
        duration_ms: float = 0.0,
        transport: str = "tcp",
    ) -> None:
        """Record a single command invocation."""
        entry = {
            "timestamp": datetime.now(UTC).isoformat(),
            "session": self._session_id,
            "command": command,
            "params": _sanitize_params(params),
            "success": success,
            "error": error,
            "duration_ms": round(duration_ms, 1),
            "transport": transport,
            "response_summary": _summarize_response(response),
        }

        date_str = datetime.now(UTC).strftime("%Y-%m-%d")
        log_file = self.calls_dir / f"{date_str}.jsonl"

        try:
            self._rotate_if_needed(log_file)
            with open(log_file, "a", encoding="utf-8") as file_handle:
                file_handle.write(json.dumps(entry, default=str) + "\n")
        except OSError as write_error:
            logger.warning("Failed to write call log: %s", write_error)

    def get_call_count(self) -> int:
        """Count total logged calls across all files."""
        total = 0
        for log_file in self.calls_dir.glob("*.jsonl"):
            total += sum(1 for _ in open(log_file, encoding="utf-8"))
        return total

    def get_recent_calls(self, limit: int = 50) -> list[dict[str, Any]]:
        """Read the most recent calls (newest first)."""
        all_entries: list[dict[str, Any]] = []
        # Read files in reverse date order
        for log_file in sorted(self.calls_dir.glob("*.jsonl"), reverse=True):
            for line in reversed(log_file.read_text(encoding="utf-8").strip().split("\n")):
                if line.strip():
                    try:
                        all_entries.append(json.loads(line))
                    except json.JSONDecodeError:
                        continue
                if len(all_entries) >= limit:
                    return all_entries
        return all_entries

    def get_failure_stats(self) -> dict[str, int]:
        """Count failures by command type for RL reward signal."""
        failures: dict[str, int] = {}
        for log_file in self.calls_dir.glob("*.jsonl"):
            for line in log_file.read_text(encoding="utf-8").strip().split("\n"):
                if not line.strip():
                    continue
                try:
                    entry = json.loads(line)
                    if not entry.get("success", True):
                        cmd = entry.get("command", "unknown")
                        failures[cmd] = failures.get(cmd, 0) + 1
                except json.JSONDecodeError:
                    continue
        return failures


def _sanitize_params(params: dict[str, Any] | None) -> dict[str, Any]:
    """Remove large binary data (base64 images) from params before logging."""
    if not params:
        return {}
    sanitized = {}
    for key, value in params.items():
        if isinstance(value, str) and len(value) > 1000:
            # Likely base64 image data; store length instead of content
            sanitized[key] = f"<{len(value)} chars>"
        else:
            sanitized[key] = value
    return sanitized


def _summarize_response(response: dict[str, Any] | None) -> dict[str, Any]:
    """Create a compact summary of the response for logging."""
    if not response:
        return {}
    summary: dict[str, Any] = {}
    for key, value in response.items():
        if isinstance(value, str) and len(value) > 500:
            summary[key] = f"<{len(value)} chars>"
        elif isinstance(value, list):
            summary[key] = f"<list of {len(value)}>"
        elif isinstance(value, dict) and len(str(value)) > 500:
            summary[key] = f"<dict with {len(value)} keys>"
        else:
            summary[key] = value
    return summary


# Singleton instance shared across the server
_global_logger: CallLogger | None = None


def get_call_logger() -> CallLogger:
    """Get or create the global CallLogger instance."""
    global _global_logger
    if _global_logger is None:
        _global_logger = CallLogger()
    return _global_logger
