"""Validation helpers for UE-specific values."""

from __future__ import annotations

import re

# Asset path: /Game/..., /Engine/..., /Script/..., etc.
_ASSET_PATH_RE = re.compile(r"^/(Game|Engine|Script|Temp|Plugin)/\S+")

# Class path: /Script/Engine.StaticMeshActor
_CLASS_PATH_RE = re.compile(r"^/Script/\w+\.\w+")

# Actor path in level
_ACTOR_PATH_RE = re.compile(r"^/Game/.+:PersistentLevel\.")

# Blueprint node ID
_NODE_ID_RE = re.compile(r"^K2Node_\w+_\d+|EdGraphNode_\w+_\d+")


def is_valid_asset_path(path: str) -> bool:
    """Check if a string looks like a valid UE asset path."""
    return bool(_ASSET_PATH_RE.match(path))


def is_valid_class_path(path: str) -> bool:
    """Check if a string looks like a valid UE class path."""
    return bool(_CLASS_PATH_RE.match(path))


def is_valid_actor_path(path: str) -> bool:
    """Check if a string looks like a valid actor object path."""
    return bool(_ACTOR_PATH_RE.match(path))


def sanitize_label(label: str) -> str:
    """Sanitize a string for use as a UE actor/asset label.

    Removes characters that UE doesn't allow in labels.
    """
    # Keep alphanumeric, underscores, hyphens, spaces
    sanitized = re.sub(r"[^\w\s\-]", "", label)
    return sanitized.strip()[:256]  # UE label length limit
