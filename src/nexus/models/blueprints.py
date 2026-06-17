"""Blueprint-related Pydantic models."""

from __future__ import annotations

from pydantic import BaseModel, Field


class PinInfo(BaseModel):
    """Information about a blueprint node pin."""

    pin_id: str = ""
    name: str = ""
    pin_type: str = Field("", description="exec, bool, float, int, string, object, struct, etc.")
    direction: str = Field("", description="input or output")
    default_value: str = ""
    is_connected: bool = False


class NodeInfo(BaseModel):
    """Information about a blueprint graph node."""

    node_id: str = ""
    node_class: str = Field("", description="K2Node_CallFunction, K2Node_Event, etc.")
    title: str = ""
    position_x: float = 0.0
    position_y: float = 0.0
    comment: str = ""
    pins: list[PinInfo] = Field(default_factory=list)


class GraphInfo(BaseModel):
    """Information about a blueprint event graph or function graph."""

    graph_name: str = ""
    graph_type: str = Field("", description="EventGraph, FunctionGraph, MacroGraph")
    nodes: list[NodeInfo] = Field(default_factory=list)
    node_count: int = 0


class VariableInfo(BaseModel):
    """Blueprint variable definition."""

    name: str = ""
    var_type: str = ""
    default_value: str = ""
    is_exposed: bool = False
    is_replicated: bool = False
    category: str = ""
    tooltip: str = ""


class FunctionInfo(BaseModel):
    """Blueprint function definition."""

    name: str = ""
    is_pure: bool = False
    is_static: bool = False
    access_specifier: str = Field("", description="Public, Protected, Private")
    inputs: list[PinInfo] = Field(default_factory=list)
    outputs: list[PinInfo] = Field(default_factory=list)
    description: str = ""


class BlueprintInfo(BaseModel):
    """Detailed information about a Blueprint asset."""

    path: str = Field(description="Asset path of the Blueprint")
    name: str = ""
    parent_class: str = ""
    is_compiled: bool = False
    has_errors: bool = False
    variables: list[VariableInfo] = Field(default_factory=list)
    functions: list[FunctionInfo] = Field(default_factory=list)
    graphs: list[GraphInfo] = Field(default_factory=list)
    component_count: int = 0
