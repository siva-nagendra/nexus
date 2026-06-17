"""Audio node CRUD Pydantic models — Sound Cue + MetaSound."""

from __future__ import annotations

from pydantic import BaseModel, Field

# ─────────────────────────────────────────────────────────────────────
# Sound Cue node models (Phase 3A)
# ─────────────────────────────────────────────────────────────────────


class SoundCueNodeInfo(BaseModel, extra="allow"):
    """A single node within a Sound Cue graph."""

    node_id: str = ""
    node_class: str = ""
    max_child_nodes: int = 0
    child_count: int = 0
    position_x: float = 0.0
    position_y: float = 0.0
    children: list[str] = Field(default_factory=list)


class SoundCueNodeResult(BaseModel, extra="allow"):
    """Result of adding a node to a Sound Cue."""

    asset_path: str = ""
    node_id: str = ""
    node_type: str = ""
    node_class: str = ""
    position_x: float = 0.0
    position_y: float = 0.0
    max_child_nodes: int = 0


class SoundCueNodeListResult(BaseModel, extra="allow"):
    """Result of listing all nodes in a Sound Cue."""

    asset_path: str = ""
    nodes: list[SoundCueNodeInfo] = Field(default_factory=list)
    node_count: int = 0
    first_node: str = ""


class SoundCueNodeUpdateResult(BaseModel, extra="allow"):
    """Result of updating a Sound Cue node."""

    asset_path: str = ""
    node_id: str = ""
    node_class: str = ""
    modified_count: int = 0
    modified_properties: list[str] = Field(default_factory=list)


class SoundCueNodeRemoveResult(BaseModel, extra="allow"):
    """Result of removing a node from a Sound Cue."""

    asset_path: str = ""
    removed_node_id: str = ""
    removed_node_class: str = ""


class SoundCueConnectionResult(BaseModel, extra="allow"):
    """Result of connecting two Sound Cue nodes."""

    asset_path: str = ""
    parent_node_id: str = ""
    child_node_id: str = ""
    child_index: int = 0
    parent_child_count: int = 0


class SoundCueDisconnectResult(BaseModel, extra="allow"):
    """Result of disconnecting a Sound Cue node connection."""

    asset_path: str = ""
    parent_node_id: str = ""
    child_index: int = 0
    disconnected_child: str = ""


# ─────────────────────────────────────────────────────────────────────
# MetaSound node models (Phase 3B)
# ─────────────────────────────────────────────────────────────────────


class MetaSoundPinInfo(BaseModel, extra="allow"):
    """A single input or output pin on a MetaSound node."""

    name: str = ""
    type: str = ""
    vertex_id: str = ""


class MetaSoundNodeInfo(BaseModel, extra="allow"):
    """A single node within a MetaSound graph."""

    node_id: str = ""
    name: str = ""
    class_name: str = ""
    position_x: float = 0.0
    position_y: float = 0.0
    inputs: list[MetaSoundPinInfo] = Field(default_factory=list)
    outputs: list[MetaSoundPinInfo] = Field(default_factory=list)


class MetaSoundEdgeInfo(BaseModel, extra="allow"):
    """A connection (edge) between two MetaSound node pins."""

    from_node_id: str = ""
    from_vertex_id: str = ""
    to_node_id: str = ""
    to_vertex_id: str = ""


class MetaSoundNodeResult(BaseModel, extra="allow"):
    """Result of adding a node to a MetaSound."""

    asset_path: str = ""
    node_id: str = ""
    node_class_name: str = ""
    position_x: float = 0.0
    position_y: float = 0.0
    input_count: int = 0
    output_count: int = 0


class MetaSoundNodeListResult(BaseModel, extra="allow"):
    """Result of listing all nodes and edges in a MetaSound."""

    asset_path: str = ""
    nodes: list[MetaSoundNodeInfo] = Field(default_factory=list)
    node_count: int = 0
    edges: list[MetaSoundEdgeInfo] = Field(default_factory=list)
    edge_count: int = 0


class MetaSoundNodeUpdateResult(BaseModel, extra="allow"):
    """Result of updating a MetaSound node."""

    asset_path: str = ""
    node_id: str = ""
    modified_count: int = 0
    modified_properties: list[str] = Field(default_factory=list)


class MetaSoundNodeRemoveResult(BaseModel, extra="allow"):
    """Result of removing a node from a MetaSound."""

    asset_path: str = ""
    removed_node_id: str = ""
    removed_node_name: str = ""


class MetaSoundConnectionResult(BaseModel, extra="allow"):
    """Result of connecting or disconnecting MetaSound node pins."""

    asset_path: str = ""
    from_node_id: str = ""
    from_output_name: str = ""
    to_node_id: str = ""
    to_input_name: str = ""
