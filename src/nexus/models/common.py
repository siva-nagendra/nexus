"""Common Pydantic v2 models shared across all Nexus subsystems."""

from __future__ import annotations

import re
from typing import Annotated, Any

from pydantic import BaseModel, ConfigDict, Field, field_validator


class Vector3(BaseModel):
    """3D vector for positions, scales, and directions."""

    model_config = ConfigDict(extra="ignore")

    x: float = 0.0
    y: float = 0.0
    z: float = 0.0

    def to_list(self) -> list[float]:
        return [self.x, self.y, self.z]

    @classmethod
    def from_list(cls, v: list[float]) -> Vector3:
        return cls(x=v[0], y=v[1], z=v[2]) if len(v) >= 3 else cls()


class Rotator(BaseModel):
    """Rotation in degrees (Unreal convention: pitch, yaw, roll)."""

    model_config = ConfigDict(extra="ignore")

    pitch: float = 0.0
    yaw: float = 0.0
    roll: float = 0.0

    def to_list(self) -> list[float]:
        return [self.pitch, self.yaw, self.roll]

    @classmethod
    def from_list(cls, v: list[float]) -> Rotator:
        return cls(pitch=v[0], yaw=v[1], roll=v[2]) if len(v) >= 3 else cls()


class Transform(BaseModel):
    """World-space transform combining location, rotation, and scale."""

    model_config = ConfigDict(extra="ignore")

    location: Vector3 = Field(default_factory=Vector3)
    rotation: Rotator = Field(default_factory=Rotator)
    scale: Vector3 = Field(default_factory=lambda: Vector3(x=1.0, y=1.0, z=1.0))


class Color(BaseModel):
    """Linear color with RGBA channels (0.0-1.0)."""

    model_config = ConfigDict(extra="ignore")

    r: float = Field(0.0, ge=0.0, le=1.0)
    g: float = Field(0.0, ge=0.0, le=1.0)
    b: float = Field(0.0, ge=0.0, le=1.0)
    a: float = Field(1.0, ge=0.0, le=1.0)


# Asset path pattern: /Game/..., /Engine/..., /Script/...
_ASSET_PATH_RE = re.compile(r"^/(Game|Engine|Script|Temp|Plugin)/")

AssetPath = Annotated[
    str,
    Field(
        description=(
            "Unreal asset path starting with /Game/, /Engine/, or /Script/. "
            "Example: '/Game/Materials/M_Base.M_Base'. "
            "Use search_assets to discover valid paths."
        ),
        pattern=r"^/(Game|Engine|Script|Temp|Plugin)/",
    ),
]


class ActorRef(BaseModel):
    """Reference to an actor in the level.

    Either actor_path (full path) or actor_label (display name) can identify an actor.
    actor_path is canonical; actor_label is for convenience.
    """

    model_config = ConfigDict(extra="ignore")

    actor_path: str = Field(
        "",
        description=(
            "Full object path in the level, e.g. "
            "'/Game/Maps/Main.Main:PersistentLevel.StaticMeshActor_0'. "
            "Use find_actors to discover paths."
        ),
    )
    actor_label: str = Field(
        "",
        description="Human-readable label shown in the editor Outliner.",
    )

    @field_validator("actor_path", "actor_label")
    @classmethod
    def at_least_one(cls, v: str, info) -> str:  # noqa: N805
        return v

    def model_post_init(self, __context: Any) -> None:
        if not self.actor_path and not self.actor_label:
            raise ValueError("At least one of actor_path or actor_label must be provided")


class BoundingBox(BaseModel):
    """Axis-aligned bounding box."""

    model_config = ConfigDict(extra="ignore")

    origin: Vector3 = Field(default_factory=Vector3)
    extent: Vector3 = Field(default_factory=Vector3)


class BatchResult(BaseModel):
    """Result of a batch operation with partial-failure semantics."""

    model_config = ConfigDict(extra="ignore")

    success_count: int = 0
    failed_count: int = 0
    results: list[dict[str, Any]] = Field(
        default_factory=list,
        description="Per-item results for successful operations",
    )
    errors: list[dict[str, Any]] = Field(
        default_factory=list,
        description="Per-item errors for failed operations, each with 'index' and 'error' keys",
    )


class PaginatedList(BaseModel):
    """Paginated list response for all query operations."""

    model_config = ConfigDict(extra="ignore")

    items: list[dict[str, Any]] = Field(default_factory=list)
    total: int = 0
    has_more: bool = False
    next_offset: int = 0


class AssetReference(BaseModel):
    """Lightweight reference to a UE asset."""

    model_config = ConfigDict(extra="ignore")

    path: AssetPath
    asset_class: str = Field("", description="UE class name, e.g. 'StaticMesh', 'Material'")
    name: str = Field("", description="Display name of the asset")
