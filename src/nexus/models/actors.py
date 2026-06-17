"""Actor-related Pydantic models."""

from __future__ import annotations

from pydantic import BaseModel, ConfigDict, Field

from nexus.models.common import BoundingBox, Transform, Vector3


class ComponentInfo(BaseModel):
    """Information about a component attached to an actor."""

    model_config = ConfigDict(extra="ignore")

    name: str = ""
    class_name: str = ""
    is_root: bool = False
    relative_location: Vector3 = Field(default_factory=Vector3)
    relative_rotation: Vector3 = Field(default_factory=Vector3)
    relative_scale: Vector3 = Field(default_factory=lambda: Vector3(x=1.0, y=1.0, z=1.0))


class ActorInfo(BaseModel):
    """Detailed information about a spawned or queried actor."""

    model_config = ConfigDict(extra="ignore")

    actor_path: str = Field(description="Full object path in the level")
    actor_label: str = Field("", description="Human-readable display name")
    actor_class: str = Field("", description="UE class name, e.g. 'StaticMeshActor'")
    transform: Transform = Field(default_factory=Transform)
    bounds: BoundingBox = Field(default_factory=BoundingBox)
    tags: list[str] = Field(default_factory=list)
    layer: str = ""
    is_hidden: bool = False
    components: list[ComponentInfo] = Field(default_factory=list)
    parent_path: str = Field("", description="Path of the parent actor if attached")
    children_count: int = 0


class ActorList(BaseModel):
    """List of actors returned by search/query operations."""

    model_config = ConfigDict(extra="ignore")

    actors: list[ActorInfo] = Field(default_factory=list)
    total_count: int = 0
    truncated: bool = Field(False, description="True if results were truncated due to limit")
