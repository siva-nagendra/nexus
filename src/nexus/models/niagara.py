"""Niagara module stack Pydantic models."""

from __future__ import annotations

from pydantic import BaseModel, Field


class NiagaraModuleInfo(BaseModel, extra="allow"):
    """A single module within a Niagara emitter stack stage."""

    module_id: str = ""
    index: int = 0
    enabled: bool = True
    script_name: str = ""
    script_path: str = ""
    position_x: float = 0.0
    position_y: float = 0.0


class NiagaraStageInfo(BaseModel, extra="allow"):
    """Modules within one stage (e.g. ParticleSpawn, ParticleUpdate)."""

    stage: str = ""
    modules: list[NiagaraModuleInfo] = Field(default_factory=list)
    module_count: int = 0


class NiagaraStackInfo(BaseModel, extra="allow"):
    """Full emitter stack: all stages with their modules."""

    system_path: str = ""
    emitter_name: str = ""
    stages: list[NiagaraStageInfo] = Field(default_factory=list)
    total_module_count: int = 0


class NiagaraModuleResult(BaseModel, extra="allow"):
    """Result of a module CRUD operation (add/update)."""

    system_path: str = ""
    emitter_name: str = ""
    stage: str = ""
    module_id: str = ""
    module_script_path: str = ""
    script_name: str = ""
    position_x: float = 0.0
    position_y: float = 0.0
    index: int = 0
    enabled: bool = True


class NiagaraModuleRemoveResult(BaseModel, extra="allow"):
    """Result of removing a module from the stack."""

    system_path: str = ""
    emitter_name: str = ""
    stage: str = ""
    removed_module_id: str = ""
    removed_script_name: str = ""


class NiagaraModuleReorderResult(BaseModel, extra="allow"):
    """Result of reordering a module within a stage."""

    system_path: str = ""
    emitter_name: str = ""
    stage: str = ""
    module_id: str = ""
    old_index: int = 0
    new_index: int = 0
    moved: bool = False
