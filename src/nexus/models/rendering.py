"""Rendering and MRQ-related Pydantic models."""

from __future__ import annotations

from pydantic import BaseModel, Field


class RenderPassConfig(BaseModel):
    """Configuration for a single MRQ render pass."""

    pass_name: str = ""
    pass_type: str = Field(
        "",
        description=(
            "Type of render pass: 'DeferredLighting', 'PathTracer', "
            "'ObjectId', 'CustomStencil', 'BaseColor', 'WorldNormal', "
            "'Roughness', 'Metallic', 'SceneDepth', 'CustomRender'"
        ),
    )
    enabled: bool = True
    output_format: str = Field("EXR", description="EXR, PNG, JPEG, BMP")
    resolution_x: int = 1920
    resolution_y: int = 1080


class MRQJobInfo(BaseModel):
    """Information about an MRQ render job."""

    job_name: str = ""
    sequence_path: str = Field("", description="LevelSequence asset path")
    map_path: str = Field("", description="Level/map asset path")
    output_directory: str = ""
    passes: list[RenderPassConfig] = Field(default_factory=list)
    frame_range_start: int = 0
    frame_range_end: int = 0
    status: str = Field("Pending", description="Pending, InProgress, Complete, Failed, Cancelled")


class MRQQueueInfo(BaseModel):
    """Information about the MRQ render queue."""

    queue_name: str = ""
    jobs: list[MRQJobInfo] = Field(default_factory=list)
    total_jobs: int = 0
    completed_jobs: int = 0
    is_rendering: bool = False


class RenderSettings(BaseModel):
    """Global rendering settings snapshot."""

    anti_aliasing_method: str = Field("TSR", description="TSR, TAA, FXAA, MSAA, None")
    global_illumination: str = Field("Lumen", description="Lumen, ScreenSpace, None")
    shadow_method: str = Field("VirtualShadowMaps", description="VirtualShadowMaps, ShadowMaps")
    nanite_enabled: bool = True
    lumen_enabled: bool = True
    vsm_enabled: bool = True
    ray_tracing_enabled: bool = False
    screen_percentage: float = 100.0
    post_process_quality: int = Field(4, ge=0, le=4)
