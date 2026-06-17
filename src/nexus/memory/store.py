"""JSONL-based persistent memory store with per-command TTL recall."""

from __future__ import annotations

import logging
import time
from pathlib import Path

from nexus.memory.models import MemoryEntry, PatternEntry

logger = logging.getLogger("nexus.memory")

MEMORY_DIR = Path.home() / ".nexus" / "memory"

# Rotation thresholds
MAX_FILE_SIZE = 50 * 1024 * 1024  # 50 MB per JSONL file before rotation
MAX_AGE_DAYS = 30  # Delete files older than this on startup
_MAX_ROTATIONS = 5  # Keep at most 5 rotated copies


class MemoryStore:
    """Persistent memory for the autonomous agent.

    Stores entries as JSON lines in category-specific files so that
    recall can be scoped to a goal_type without scanning everything.
    Automatically rotates large files and purges stale data on startup.
    """

    def __init__(self, store_path: Path | None = None) -> None:
        self.store_path = store_path or MEMORY_DIR
        self.store_path.mkdir(parents=True, exist_ok=True)
        self._cleanup_old_files()

    def _cleanup_old_files(self) -> None:
        """Delete JSONL files older than MAX_AGE_DAYS."""
        cutoff = time.time() - (MAX_AGE_DAYS * 86400)
        for jsonl_file in self.store_path.glob("*.jsonl"):
            if jsonl_file.stat().st_mtime < cutoff:
                jsonl_file.unlink()
                logger.info("Cleaned up old memory file: %s", jsonl_file.name)

    def _rotate_if_needed(self, filepath: Path) -> None:
        """Rotate file if it exceeds MAX_FILE_SIZE.

        Shifts existing rotations (.1 -> .2, .2 -> .3, etc.) so the
        newest overflow is always in .1 and the oldest is discarded.
        """
        if not filepath.exists():
            return
        if filepath.stat().st_size < MAX_FILE_SIZE:
            return
        # Shift existing rotations: .5 -> .6, .4 -> .5, ... .1 -> .2
        for rotation_index in range(_MAX_ROTATIONS, 0, -1):
            old_path = filepath.parent / (f"{filepath.stem}.{rotation_index}{filepath.suffix}")
            new_path = filepath.parent / (f"{filepath.stem}.{rotation_index + 1}{filepath.suffix}")
            if old_path.exists():
                old_path.rename(new_path)
        rotated_path = filepath.parent / f"{filepath.stem}.1{filepath.suffix}"
        filepath.rename(rotated_path)
        logger.info("Rotated %s (exceeded %d bytes)", filepath.name, MAX_FILE_SIZE)

    async def store(self, entry: MemoryEntry) -> None:
        """Append a memory entry to the goal-type category file."""
        category_file = self.store_path / f"{entry.goal_type}.jsonl"
        self._rotate_if_needed(category_file)
        with open(category_file, "a", encoding="utf-8") as file_handle:
            file_handle.write(entry.model_dump_json() + "\n")
        logger.debug("Stored memory entry %s for goal %s", entry.id, entry.goal_type)

    async def recall(
        self,
        goal_type: str,
        tags: list[str] | None = None,
        limit: int = 20,
    ) -> list[MemoryEntry]:
        """Recall entries for a goal type, newest first.

        Optionally filters by tag intersection so callers can scope
        recall to specific scene contexts or object types.
        """
        category_file = self.store_path / f"{goal_type}.jsonl"
        if not category_file.exists():
            return []
        entries = self._load_entries(category_file)
        if tags:
            tag_set = set(tags)
            entries = [entry for entry in entries if tag_set.intersection(entry.tags)]
        # Newest first for recency-biased decision making
        entries.sort(key=lambda entry: entry.timestamp, reverse=True)
        return entries[:limit]

    async def recall_failures(
        self,
        action_type: str = "",
        error_pattern: str = "",
        limit: int = 10,
    ) -> list[MemoryEntry]:
        """Find past failures matching an action type or error substring.

        Scans all category files because failures for one goal type
        may be relevant when the same action is attempted elsewhere.
        """
        all_failures: list[MemoryEntry] = []
        for category_file in self.store_path.glob("*.jsonl"):
            # Skip pattern files since they have a different schema
            if category_file.name.startswith("patterns_"):
                continue
            entries = self._load_entries(category_file)
            for entry in entries:
                if not entry.outcome.is_failure:
                    continue
                if action_type and entry.action.action_type != action_type:
                    continue
                if error_pattern and error_pattern.lower() not in entry.outcome.error.lower():
                    continue
                all_failures.append(entry)
        all_failures.sort(key=lambda entry: entry.timestamp, reverse=True)
        return all_failures[:limit]

    async def store_pattern(self, pattern: PatternEntry) -> None:
        """Append a learned pattern to the type-specific patterns file."""
        pattern_file = self.store_path / f"patterns_{pattern.pattern_type}.jsonl"
        with open(pattern_file, "a", encoding="utf-8") as file_handle:
            file_handle.write(pattern.model_dump_json() + "\n")

    async def get_patterns(self, pattern_type: str) -> list[PatternEntry]:
        """Load all learned patterns of a given type."""
        pattern_file = self.store_path / f"patterns_{pattern_type}.jsonl"
        if not pattern_file.exists():
            return []
        patterns: list[PatternEntry] = []
        for line in pattern_file.read_text(encoding="utf-8").strip().split("\n"):
            if line.strip():
                patterns.append(PatternEntry.model_validate_json(line))
        return patterns

    def _load_entries(self, filepath: Path) -> list[MemoryEntry]:
        """Load all entries from a JSONL file, skipping corrupt lines."""
        entries: list[MemoryEntry] = []
        raw_text = filepath.read_text(encoding="utf-8").strip()
        if not raw_text:
            return entries
        for line in raw_text.split("\n"):
            if not line.strip():
                continue
            try:
                entries.append(MemoryEntry.model_validate_json(line))
            except Exception:
                logger.warning("Skipping corrupt memory entry in %s", filepath.name)
        return entries
