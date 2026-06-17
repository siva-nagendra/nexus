# Nexus

In-game MCP server for Unreal Engine 5.7 with autonomous AI agents, native scene state awareness, and persistent learning.

## Architecture

```
Claude Code (MCP Client)
    |
    v
Python MCP Server (FastMCP 3.1.1, stdio)
    |
    v  HTTP (port 13378)
In-Game HTTP Server (FHttpServerModule, game-thread native)
    |
    v  synchronous dispatch (zero latency)
NexusCommandDispatcher (26 handler subsystems, 323+ commands)
    |
    +-> NexusSceneForgeHandler (45 SceneForge-specific commands)
    +-> NexusAgentPool (parallel autonomous workers)
    +-> FNexusCallLog (JSONL logging for RL training)
    +-> FNexusSceneState (per-frame cached state)
```

**Dual transport**: HTTP on port 13378 (game-thread, under 5ms) + legacy TCP on port 13377 (background thread, 60ms). Python tools prefer HTTP with automatic fallback to TCP via MLServer proxy.

## Tool Tiers (71 MCP tools)

| Tier | Count | Purpose |
|------|-------|---------|
| Core | 15 | Actors, assets, blueprints, materials, levels, lighting, editor, python |
| Deferred | 20 | Animation, audio, sequencer, Niagara, AI, physics, UI, MRQ, PCG, landscape, input, networking, rendering, profiling, source control, code analysis |
| Workflow | 8 | Scene creation, cinematics, material libraries, AI agents, rendering, performance audit |
| SceneForge | 19 | Pipeline control, segmentation, scene management, observation, settings, sessions, overlap check |
| MLServer | 10 | Direct HTTP to SAM 2.1, Depth Pro, Gemini, Meshy/Tripo3D/Rodin |

## SceneForge Integration

Full control of the whole-scene generation pipeline:

- **Generate image** from text prompt (Gemini 2.5 Flash / GPT Image)
- **Segment objects** via SAM 2.1 click-to-segment or batch box-prompt
- **Convert to 3D** via Meshy, Tripo3D, or Rodin (up to 5 concurrent)
- **Place meshes** using depth-based 3D bounding box lifting
- **Refine placement** via VLM comparison loop (Gemini Vision)
- **Overlap deduplication** prevents duplicate segments
- **Ground plane refresh** re-runs RANSAC every 3 placements

## Autonomous Agent

Parallel agent pool with OODA loop (observe-decide-act-evaluate):

```
sf_agent_run(goal_type="recreate_2d_as_3d", description="a cozy kitchen")
```

Goal types: `recreate_2d_as_3d`, `batch_convert`, `improve_lighting`, `refine_placement`, `add_environment`, `compose_scene`

Features:
- Up to 5 concurrent agents via `FNexusAgentPool`
- Resource locking prevents parallel actor conflicts
- Memory persistence for reinforcement learning (`~/.nexus/memory/`)
- Call logging with 50MB rotation and 30-day cleanup
- In-app activity panel (glassmorphism UI, toggle with N key)

## Quick Start

```bash
# Install Python dependencies
cd nexus && uv sync

# Run MCP server (stdio transport for Claude Code)
uv run nexus

# Or use direct Python
.venv/Scripts/python.exe -m nexus

# Run autonomous agent headlessly
.venv/Scripts/python.exe -m nexus.agent goal.json

# Lint
uv run ruff check src/
```

### Claude Code Configuration

`.mcp.json`:
```json
{
  "mcpServers": {
    "nexus": {
      "command": "D:\\research\\nexus\\.venv\\Scripts\\python.exe",
      "args": ["-m", "nexus"]
    }
  }
}
```

`settings.local.json`:
```json
{
  "permissions": { "allow": ["mcp__nexus__*"] },
  "enabledMcpjsonServers": ["nexus"]
}
```

## Project Structure

```
src/nexus/
  __main__.py              # Entry: strip StreamHandlers, mcp.run(transport="stdio")
  server.py                # FastMCP 3.1.1 instance + ResponseLimitingMiddleware
  connection/              # Native TCP transport, retry, error classification
  tools/
    core.py                # 15 UE core tools (actors, assets, blueprints...)
    deferred.py            # 20 subsystem tools (animation, physics, AI...)
    workflows.py           # 8 composite workflow tools
    sceneforge.py          # 19 SceneForge pipeline tools (HTTP to port 13378)
    mlserver.py            # 10 MLServer direct tools (HTTP to port 8000)
  agent/
    loop.py                # OODA agent loop with memory integration
    observer.py            # Scene state capture via MCP commands
    evaluator.py           # Structural + visual evaluation
    actions.py             # Action primitives with resource locking
    goals.py               # Goal type definitions
    config.py              # Agent configuration
    __main__.py            # Standalone headless runner
  memory/
    store.py               # JSONL persistent store with rotation
    models.py              # Pydantic models (MemoryEntry, PatternEntry)
    call_logger.py         # Per-call logging for RL training data
  utils/
    logging.py             # File-only JSON logging (no stderr for MCP)
    errors.py              # Tool-level error classification
    responses.py           # Compact JSON response helpers
    validation.py          # Action parameter validation
    cache.py               # Per-command TTL cache with write invalidation
  models/                  # Pydantic v2 response schemas
  resources/               # MCP resources (health, project info)

docs/
  architecture.html        # Interactive architecture visualization
```

## C++ Plugin

Located at `SceneForge/Plugins/Nexus/`:

- **NexusModule**: Dual HTTP (13378) + TCP (13377) server, per-frame state ticker
- **NexusCommandDispatcher**: Routes 323+ commands to 26 handler subsystems
- **NexusSceneForgeHandler**: 45 commands for SceneForge pipeline control
- **NexusAgentPool**: Parallel agent workers with OODA state machine
- **NexusCallLog**: JSONL call logging with rotation
- **NexusSceneState**: Per-frame cached scene snapshot
- **25 UE handlers**: Actor, Asset, Blueprint, Material, Level, Animation, Sequencer, Physics, AI, Niagara, UI, MRQ, Rendering, Lighting, Audio, Landscape, PCG, Input, Networking, GameFeatures, SourceControl, CodeAnalysis, Profiling, PythonExec, Editor

## Wire Protocol

```
HTTP (port 13378):  POST /nexus/{command}  {"params": {...}}
TCP  (port 13377):  [4-byte big-endian uint32 length][JSON payload]
Response:           {"success": true, "data": {...}, "transport": "http_ingame"}
```

## Prerequisites

- Unreal Engine 5.7
- Python 3.12+ with `uv`
- SceneForge running in `-game` mode (for SceneForge-specific tools)
- MLServer sidecar at localhost:8000 (for ML inference tools)

## License

MIT
