"""Asset-related Pydantic models."""

from __future__ import annotations

from pydantic import BaseModel, Field


class AssetInfo(BaseModel):
    """Detailed information about a UE asset."""

    path: str = Field(description="Full asset path, e.g. '/Game/Meshes/SM_Cube.SM_Cube'")
    name: str = ""
    asset_class: str = Field("", description="Class name: StaticMesh, Material, Texture2D, etc.")
    package_path: str = ""
    disk_size_bytes: int = 0
    is_loaded: bool = False
    tags: dict[str, str] = Field(default_factory=dict)


class AssetSearchResult(BaseModel):
    """Results from an asset search query."""

    assets: list[AssetInfo] = Field(default_factory=list)
    total_count: int = 0
    truncated: bool = False


class AssetImportResult(BaseModel):
    """Result of importing an external file as a UE asset."""

    asset_path: str = ""
    asset_class: str = ""
    success: bool = True
    warnings: list[str] = Field(default_factory=list)


class AssetDependency(BaseModel):
    """A dependency relationship between two assets."""

    source_path: str = ""
    target_path: str = ""
    dependency_type: str = Field("", description="hard, soft, or searchable")
