"""Animation blend space sample Pydantic models."""

from __future__ import annotations

from pydantic import BaseModel, Field


class BlendSpaceSampleInfo(BaseModel, extra="allow"):
    """A single sample point in a Blend Space."""

    index: int = 0
    animation: str = ""
    animation_name: str = ""
    x: float = 0.0
    y: float = 0.0
    rate_scale: float = 1.0


class BlendSpaceSamplesResult(BaseModel, extra="allow"):
    """Result of querying all samples in a Blend Space."""

    asset_path: str = ""
    samples: list[BlendSpaceSampleInfo] = Field(default_factory=list)
    count: int = 0


class BlendSpaceSampleAddResult(BaseModel, extra="allow"):
    """Result of adding a sample to a Blend Space."""

    asset_path: str = ""
    sample_index: int = 0
    animation: str = ""
    x: float = 0.0
    y: float = 0.0
    total_samples: int = 0


class BlendSpaceSampleUpdateResult(BaseModel, extra="allow"):
    """Result of updating a sample in a Blend Space."""

    asset_path: str = ""
    sample_index: int = 0
    animation: str = ""
    x: float = 0.0
    y: float = 0.0
    rate_scale: float = 1.0


class BlendSpaceSampleRemoveResult(BaseModel, extra="allow"):
    """Result of removing a sample from a Blend Space."""

    asset_path: str = ""
    deleted: bool = False
    deleted_index: int = 0
    remaining_samples: int = 0
