"""AI subsystem Pydantic models — Behavior Tree + EQS node CRUD."""

from __future__ import annotations

from pydantic import BaseModel, Field

# ─────────────────────────────────────────────────────────────────────
# Behavior Tree node models (Phase 4A)
# ─────────────────────────────────────────────────────────────────────


class BTNodePinInfo(BaseModel, extra="allow"):
    """A single pin on a Behavior Tree graph node."""

    pin_id: str = ""
    pin_name: str = ""
    direction: str = Field("", description="'input' or 'output'")
    num_connections: int = 0
    connected_to: list[str] = Field(default_factory=list)


class BTSubNodeInfo(BaseModel, extra="allow"):
    """A decorator or service sub-node attached to a BT graph node."""

    node_id: str = ""
    title: str = ""
    node_category: str = Field("", description="'Decorator' or 'Service'")
    node_class: str = ""


class BTNodeInfo(BaseModel, extra="allow"):
    """A single node within a Behavior Tree graph."""

    node_id: str = ""
    title: str = ""
    node_category: str = Field(
        "",
        description="'Root', 'Composite', 'Task', 'Decorator', or 'Service'",
    )
    node_class: str = ""
    node_type: str = ""
    position_x: float = 0.0
    position_y: float = 0.0
    pins: list[BTNodePinInfo] = Field(default_factory=list)
    sub_nodes: list[BTSubNodeInfo] = Field(default_factory=list)
    parent_node_id: str = ""


class BTNodeListResult(BaseModel, extra="allow"):
    """Result of listing all nodes in a Behavior Tree."""

    tree_path: str = ""
    nodes: list[BTNodeInfo] = Field(default_factory=list)
    count: int = 0


class BTNodeResult(BaseModel, extra="allow"):
    """Result of a BT node CRUD operation (add/update)."""

    node_id: str = ""
    node_type: str = ""
    node_class: str = ""
    node_category: str = ""
    title: str = ""
    position_x: float = 0.0
    position_y: float = 0.0
    parent_node_id: str = ""
    pins: list[BTNodePinInfo] = Field(default_factory=list)


class BTNodeRemoveResult(BaseModel, extra="allow"):
    """Result of removing a BT node."""

    removed_node_id: str = ""
    removed_title: str = ""
    was_sub_node: bool = False
    remaining_nodes: int = 0


class BTConnectResult(BaseModel, extra="allow"):
    """Result of connecting two BT nodes."""

    parent_node_id: str = ""
    child_node_id: str = ""
    parent_title: str = ""
    child_title: str = ""
    connected: bool = False


class BTDisconnectResult(BaseModel, extra="allow"):
    """Result of disconnecting a BT node from its parent."""

    node_id: str = ""
    title: str = ""
    connections_broken: int = 0


# ─────────────────────────────────────────────────────────────────────
# EQS node models (Phase 4B)
# ─────────────────────────────────────────────────────────────────────


class EQSTestInfo(BaseModel, extra="allow"):
    """A single test within an EQS query option."""

    test_id: str = ""
    test_class: str = ""
    test_type: str = ""
    test_index: int = 0


class EQSGeneratorInfo(BaseModel, extra="allow"):
    """A generator within an EQS query option."""

    generator_id: str = ""
    generator_class: str = ""
    generator_type: str = ""


class EQSOptionInfo(BaseModel, extra="allow"):
    """An option (generator + tests) within an EQS query."""

    option_index: int = 0
    generator: EQSGeneratorInfo | None = None
    tests: list[EQSTestInfo] = Field(default_factory=list)
    test_count: int = 0


class EQSNodeListResult(BaseModel, extra="allow"):
    """Result of listing all generators and tests in an EQS query."""

    query_path: str = ""
    options: list[EQSOptionInfo] = Field(default_factory=list)
    option_count: int = 0


class EQSGeneratorResult(BaseModel, extra="allow"):
    """Result of adding a generator to an EQS query."""

    generator_id: str = ""
    generator_type: str = ""
    generator_class: str = ""
    option_index: int = 0
    query_path: str = ""


class EQSTestResult(BaseModel, extra="allow"):
    """Result of adding a test to an EQS query option."""

    test_id: str = ""
    test_type: str = ""
    test_class: str = ""
    option_index: int = 0
    generator_id: str = ""
    test_index: int = 0
    query_path: str = ""


class EQSUpdateResult(BaseModel, extra="allow"):
    """Result of updating an EQS node (generator or test)."""

    node_id: str = ""
    node_type: str = ""
    node_class: str = ""
    query_path: str = ""


class EQSRemoveResult(BaseModel, extra="allow"):
    """Result of removing an EQS node."""

    removed_node_id: str = ""
    removed_type: str = ""
    removed_class: str = ""
    remaining_options: int = 0
    query_path: str = ""
