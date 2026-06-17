"""Sequencer/Cinematics Pydantic models."""

from __future__ import annotations

from pydantic import BaseModel, Field


class KeyframeInfo(BaseModel):
    """A keyframe on a sequencer track."""

    time: float = Field(0.0, description="Time in seconds")
    value: str = Field("", description="String repr of keyframe value")
    interpolation: str = Field("Linear", description="Linear, Cubic, Constant, etc.")


class TrackInfo(BaseModel):
    """A track in a sequencer section."""

    track_name: str = ""
    track_type: str = Field("", description="Transform, Float, Bool, Event, etc.")
    object_binding: str = Field("", description="Name of the bound actor/component")
    keyframe_count: int = 0
    keyframes: list[KeyframeInfo] = Field(default_factory=list)


class SectionInfo(BaseModel):
    """A section (clip) on a sequencer track."""

    section_name: str = ""
    start_time: float = 0.0
    end_time: float = 0.0
    tracks: list[TrackInfo] = Field(default_factory=list)


class SequenceInfo(BaseModel):
    """Detailed information about a Level Sequence."""

    path: str = Field(description="Asset path of the LevelSequence")
    name: str = ""
    duration: float = Field(0.0, description="Total duration in seconds")
    frame_rate: float = Field(30.0, description="Sequence frame rate")
    tracks: list[TrackInfo] = Field(default_factory=list)
    track_count: int = 0
    sub_sequences: list[str] = Field(default_factory=list)
