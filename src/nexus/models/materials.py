"""Material-related Pydantic models."""

from __future__ import annotations

from typing import Any

from pydantic import BaseModel, Field

from nexus.models.common import Color


class ParameterInfo(BaseModel):
    """A material parameter (scalar, vector, texture)."""

    name: str = ""
    param_type: str = Field("", description="Scalar, Vector, Texture, StaticSwitch")
    group: str = ""
    value: Any = Field(
        None,
        description="Parameter value (float for scalar, dict for vector, str for texture)",
    )
    default_value: str = ""


class ParameterSetResult(BaseModel):
    """Result of setting a parameter on a material instance."""

    material_path: str = ""
    parameter_name: str = ""
    value: Any = Field(None, description="The value that was set")
    texture_path: str = Field("", description="Texture path (for texture parameters)")


class ExpressionInfo(BaseModel):
    """A material graph expression node."""

    expression_id: str = ""
    expression_class: str = Field("", description="e.g. MaterialExpressionTextureSample")
    name: str = ""
    position_x: float = 0.0
    position_y: float = 0.0


class ExpressionPinInfo(BaseModel, extra="allow"):
    """A single input or output pin on a material expression node."""

    index: int = 0
    name: str = ""
    direction: str = Field("", description="'input' or 'output'")
    is_connected: bool = False
    connected_expression_id: str = Field("", description="ID of connected expression (inputs only)")
    connected_output_index: int = Field(0, description="Output index on the connected expression")


class ExpressionPinsResult(BaseModel, extra="allow"):
    """Result of querying pins on a material expression."""

    material_path: str = ""
    expression_id: str = ""
    expression_class: str = ""
    inputs: list[ExpressionPinInfo] = Field(default_factory=list)
    outputs: list[ExpressionPinInfo] = Field(default_factory=list)
    input_count: int = 0
    output_count: int = 0


class ExpressionNodeResult(BaseModel, extra="allow"):
    """Result of a material expression CRUD operation (add/update/remove/connect/disconnect)."""

    material_path: str = ""
    expression_id: str = ""
    expression_class: str = ""
    expression_type: str = ""
    position_x: float = 0.0
    position_y: float = 0.0


class MaterialInfo(BaseModel, extra="allow"):
    """Detailed information about a Material or Material Instance.

    Uses extra='allow' to accept additional fields from C++ responses
    (e.g. 'success', 'created') without validation errors.
    """

    path: str = Field("", description="Asset path of the material")
    name: str = ""
    is_instance: bool = False
    parent_path: str = Field("", description="Parent material path (for instances)")
    shading_model: str = Field("", description="DefaultLit, Unlit, Subsurface, etc.")
    blend_mode: str = Field("", description="Opaque, Translucent, Masked, Additive")
    two_sided: bool = False
    parameters: list[ParameterInfo] = Field(default_factory=list)
    expressions: list[ExpressionInfo] = Field(default_factory=list)
    base_color: Color = Field(default_factory=Color)
