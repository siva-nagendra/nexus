"""Per-command TTL cache with automatic write-invalidation.

Read-heavy commands (actor.list_all, asset.search, system.get_plugins)
are cached with domain-appropriate TTLs. Any write command (actor.spawn,
actor.delete, actor.set_property) invalidates the entire read cache so
subsequent queries reflect the latest editor state.
"""

from __future__ import annotations

import hashlib
import json
import logging
import time
from typing import Any

logger = logging.getLogger("nexus.cache")

# TTL per command type (seconds). Unlisted read commands get DEFAULT_TTL.
COMMAND_TTL: dict[str, float] = {
    "actor.list_all": 5.0,
    "actor.find": 5.0,
    "actor.find_by_class": 5.0,
    "actor.get_property": 10.0,
    "asset.search": 30.0,
    "asset.get_info": 30.0,
    "system.get_plugins": 120.0,
    "system.get_project_info": 120.0,
    "level.get_info": 10.0,
}

DEFAULT_TTL: float = 10.0

# Commands that modify editor state; any hit invalidates the read cache
WRITE_COMMANDS: frozenset[str] = frozenset(
    {
        "actor.spawn",
        "actor.delete",
        "actor.set_property",
        "actor.set_mobility",
        "actor.add_tag",
        "actor.remove_tag",
        "actor.set_transform",
        "actor.rename",
        "blueprint.compile",
        "blueprint.add_component",
        "level.load",
        "level.new",
        "material.create_instance",
        "material.set_parameter",
        "lighting.spawn_light",
        "lighting.set_property",
        "editor.undo",
        "editor.redo",
        "pcg.execute",
        "landscape.sculpt",
        "landscape.paint",
    }
)


class CommandCache:
    """In-memory cache for read-only command results with TTL eviction."""

    def __init__(self) -> None:
        self._store: dict[str, tuple[float, Any]] = {}

    def get(self, command: str, params: dict[str, Any] | None = None) -> Any | None:
        """Return cached result if fresh, else None."""
        # Write commands are never cached
        if command in WRITE_COMMANDS:
            return None

        cache_key = self._make_key(command, params)
        entry = self._store.get(cache_key)
        if entry is None:
            return None

        cached_at, cached_result = entry
        ttl = COMMAND_TTL.get(command, DEFAULT_TTL)
        age = time.monotonic() - cached_at

        if age < ttl:
            logger.debug("Cache hit for %s (age=%.1fs, ttl=%.0fs)", command, age, ttl)
            return cached_result

        # Expired entry, evict it
        del self._store[cache_key]
        return None

    def store(self, command: str, params: dict[str, Any] | None, result: Any) -> None:
        """Store a successful read-command result."""
        if command in WRITE_COMMANDS:
            return
        cache_key = self._make_key(command, params)
        self._store[cache_key] = (time.monotonic(), result)

    def invalidate_reads(self) -> None:
        """Clear all cached read results after a write command."""
        if self._store:
            logger.debug("Invalidating %d cached entries after write", len(self._store))
            self._store.clear()

    def _make_key(self, command: str, params: dict[str, Any] | None) -> str:
        """Deterministic cache key from command + sorted params."""
        if not params:
            return command
        # MD5 is sufficient here; this is not security-sensitive, just deduplication
        param_hash = hashlib.md5(
            json.dumps(params, sort_keys=True, default=str).encode()
        ).hexdigest()[:12]
        return f"{command}:{param_hash}"
