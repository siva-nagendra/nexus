<!-- nexus-docs-version: 3.0.0 -->
# Nexus — Unreal Engine 5 MCP Reference

> **Version 3.0.0** — 230+ tools across 25 subsystems for controlling UE5 Editor via Claude.

Nexus is a Model Context Protocol (MCP) server that gives Claude full programmatic access to Unreal Engine's editor and runtime systems. Tools are organized by namespace (e.g., `actors/spawn_actor`) and executed over a persistent TCP bridge to the C++ plugin inside the editor.

---

## Architecture Overview

```
Claude ──MCP/stdio──▶ Python MCP Server ──TCP──▶ C++ Plugin (UE Editor)
                       (src/nexus/)                (unreal_plugin/Nexus/)
```

- **Python layer**: FastMCP server with tool definitions, codegen, transport, and retry logic.
- **C++ layer**: 25 handler classes registered in a central dispatcher, executing commands on the game thread.
- **Transport**: Persistent TCP with newline-delimited JSON, request-ID correlation, and automatic retry on transient failures.

---

## Subsystem Reference

### 1. actors — Actor Management

Spawn, find, delete, transform, and batch-manipulate actors in the level.

| Tool | Description |
|------|-------------|
| `spawn_actor` | Spawn a new actor by class name (e.g., `StaticMeshActor`, `PointLight`, Blueprint paths) |
| `find_actors` | Search actors by label, path, or tag substring |
| `find_actors_by_class` | Find all actors of a specific UE class |
| `list_all_actors` | List all actors with pagination |
| `delete_actor` | Delete an actor by path |
| `get_actor_transform` | Get world-space location, rotation, scale |
| `set_actor_transform` | Set world-space location, rotation, scale |
| `get_actor_property` | Read any UPROPERTY value |
| `set_actor_property` | Write any UPROPERTY value |
| `get_actor_components` | List components on an actor |
| `set_actor_visibility` | Show/hide an actor |
| `duplicate_actor` | Clone an actor with optional offset |
| `rename_actor` | Change the display label |
| `add_actor_tag` / `remove_actor_tag` / `get_actor_tags` | Tag management |
| `attach_actor` / `detach_actor` | Parent/child relationships |
| `set_actor_mobility` | Set Static/Stationary/Movable |
| `get_actor_bounds` | Get axis-aligned bounding box |
| `spawn_actors_batch` | Spawn multiple actors in one call |
| `delete_actors_batch` | Delete multiple actors by path or label pattern |
| `set_transforms_batch` | Set transforms for multiple actors at once |

**Example — Spawn a cube at the origin:**
```
actors/spawn_actor  actor_class=StaticMeshActor  label=MyCube  location_z=100
```

---

### 2. assets — Content Browser Operations

Search, import, delete, rename, and manage assets in the Content Browser.

| Tool | Description |
|------|-------------|
| `search_assets` | Search by name/path/class with folder and recursion options |
| `get_asset_info` | Detailed asset metadata |
| `asset_exists` | Check if an asset path is valid |
| `import_asset` | Import FBX, OBJ, PNG, WAV, etc. |
| `delete_asset` | Delete with optional force (ignore references) |
| `rename_asset` | Rename and auto-update references |
| `duplicate_asset` | Copy to a new location |
| `save_asset` / `save_all_assets` | Persist changes to disk |
| `get_asset_references` | Outgoing dependencies |
| `get_asset_dependents` | Incoming references |
| `create_folder` / `list_folder` | Folder management |
| `set_asset_metadata` | Key-value metadata tags |
| `validate_asset` | Check for corruption or missing refs |
| `import_assets_batch` | Bulk import multiple files |

**Example — Find all static meshes named "chair":**
```
assets/search_assets  query=chair  asset_class=StaticMesh
```

---

### 3. blueprints — Blueprint Editing

Create, compile, and edit Blueprint assets including variables, functions, components, and the node graph.

| Tool | Description |
|------|-------------|
| `create_blueprint` | Create from any parent class (Actor, Pawn, Character, etc.) |
| `compile_blueprint` | Compile and report errors |
| `get_blueprint_info` | Variables, functions, graphs overview |
| `add_blueprint_variable` | Add typed variables with categories |
| `set_variable_default` | Set default values |
| `get_blueprint_variables` / `get_blueprint_functions` | List members |
| `add_blueprint_function` | Add function graphs |
| `add_blueprint_component` | Add scene/actor components |
| `get_blueprint_graphs` | List all graphs (event, function, macro) |
| `add_event_dispatcher` | Add multicast delegates |
| `set_parent_class` / `add_interface` | Inheritance management |
| `open_blueprint` | Open in Blueprint Editor |
| `add_blueprint_node` | Add nodes (function calls, events, branches, etc.) |
| `connect_pins` | Wire nodes together |
| `remove_node` / `get_node_pins` | Node manipulation |

**Example — Create a health variable on a Blueprint:**
```
blueprints/add_blueprint_variable  blueprint_path=/Game/BP/BP_Enemy  variable_name=Health  variable_type=Float  is_exposed=true
blueprints/set_variable_default    blueprint_path=/Game/BP/BP_Enemy  variable_name=Health  default_value=100.0
```

---

### 4. materials — Material System

Create materials and instances, configure parameters, and apply to actors.

| Tool | Description |
|------|-------------|
| `create_material` | New material with shading model and blend mode |
| `create_material_instance` | Instance from a parent material |
| `get_material_info` | Inspect material properties |
| `set_scalar_parameter` | Set float params (Roughness, Metallic, etc.) |
| `set_vector_parameter` | Set color/vector params (BaseColor, EmissiveColor) |
| `set_texture_parameter` | Assign textures |
| `get_material_parameters` | List all parameters |
| `set_shading_model` / `set_blend_mode` / `set_two_sided` | Material properties |
| `apply_material_to_actor` | Apply material to a mesh component |
| `get_material_expressions` | Inspect node graph |
| `apply_materials_batch` | Apply materials to multiple actors |

**Example — Create a red metallic material:**
```
materials/create_material_instance  instance_path=/Game/Materials/MI_Red  parent_material_path=/Game/Materials/M_Base
materials/set_vector_parameter      material_path=/Game/Materials/MI_Red  parameter_name=BaseColor  r=1.0  g=0.0  b=0.0
materials/set_scalar_parameter      material_path=/Game/Materials/MI_Red  parameter_name=Metallic  value=1.0
```

---

### 5. levels — Level Management

Load, save, create levels, manage sublevels and World Partition.

| Tool | Description |
|------|-------------|
| `get_current_level` | Info about the persistent level |
| `load_level` / `save_level` / `create_level` | Level lifecycle |
| `list_sublevels` / `add_sublevel` / `remove_sublevel` | Sublevel management |
| `set_sublevel_visibility` | Editor visibility toggle |
| `get_world_partition_info` / `set_data_layer` | World Partition config |
| `list_streaming_levels` | Streaming level states |
| `get_level_bounds` | Bounding box of all actors |

**Example — Create a new empty level:**
```
levels/create_level  level_path=/Game/Maps/NewLevel  template=EmptyLevel
```

---

### 6. editor — Editor Control

Viewport, PIE, screenshots, console commands, selection, and undo/redo.

| Tool | Description |
|------|-------------|
| `get_viewport_info` | Camera position and settings |
| `set_viewport_camera` | Move the editor camera |
| `take_screenshot` | Capture viewport image |
| `start_pie` / `stop_pie` / `is_pie_running` | Play-In-Editor control |
| `execute_console_command` | Run any console command |
| `get_selection` / `set_selection` / `clear_selection` | Actor selection |
| `undo` / `redo` | Editor history |
| `get_world_info` | Map name, actor count, world type |
| `focus_actor` | Zoom viewport to an actor |
| `set_editor_mode` | Switch to Select, Translate, Rotate, Scale, etc. |

**Example — Take a screenshot and focus on an actor:**
```
editor/focus_actor   actor_path=/Game/Maps/Main.Main:PersistentLevel.BP_Hero_0
editor/take_screenshot  filename=hero_shot  width=3840  height=2160
```

---

### 7. lighting — Lighting and Atmosphere

Spawn lights, configure atmosphere, fog, clouds, GI, and shadows.

| Tool | Description |
|------|-------------|
| `spawn_light` | Spawn Point, Spot, Directional, Rect, or Sky lights |
| `set_light_properties` | Intensity, color, temperature, radius, shadows |
| `create_light_scenario` | Lighting scenario sublevels |
| `set_sky_atmosphere` | Rayleigh/Mie scattering, ground albedo |
| `set_exponential_fog` | Fog density, color, volumetric fog |
| `set_volumetric_clouds` | Cloud layer altitude, thickness, tracing |
| `configure_global_illumination` | Lumen GI method and quality |
| `bake_lighting` | Build lightmaps (Preview through Production) |
| `set_shadow_settings` | Shadow resolution, cascades, ray-traced shadows |
| `get_lighting_info` | Level-wide lighting overview |

**Example — Set up a sunset directional light:**
```
lighting/spawn_light  light_type=DirectionalLight  label=SunLight  rotation_pitch=-45  intensity=10  use_temperature=true  temperature=3500
```

---

### 8. animation — Animation System

Animation blueprints, sequences, montages, blend spaces, IK, and retargeting.

| Tool | Description |
|------|-------------|
| `get_anim_blueprint_info` | Inspect AnimBP graph and state machines |
| `list_anim_sequences` | Find animations by skeleton or name |
| `get_skeleton_info` | Bone hierarchy and sockets |
| `list_anim_notifies` | Notify events on sequences/montages |
| `get_retarget_info` | Bone mapping between skeletons |
| `create_anim_montage` | Create montage from a sequence |
| `set_anim_blueprint` | Assign AnimBP to a skeletal mesh actor |
| `create_blend_space` | 1D or 2D blend space |
| `set_ik_settings` | CCDIK, FABRIK, or LimbIK configuration |
| `create_control_rig` | Generate a control rig for a skeletal mesh |
| `add_anim_notify` | Add notify events to sequences |
| `apply_retarget` | Retarget animations between skeletons |

**Example — Create a locomotion blend space:**
```
animation/create_blend_space  skeleton_path=/Game/Characters/SK_Hero  blend_space_name=BS_Locomotion  destination_folder=/Game/Characters/Animations  axis_x_name=Speed  axis_x_max=600
```

---

### 9. audio — Audio System

Spawn sounds, create Sound Cues and MetaSounds, configure attenuation and reverb.

| Tool | Description |
|------|-------------|
| `spawn_sound` | Place an AmbientSound actor |
| `create_sound_cue` | Combine SoundWaves with Random/Concat/Modulator |
| `create_metasound` | Create a MetaSound source (Synth, Granular, etc.) |
| `set_sound_properties` | Volume, pitch, auto-activate, sound class |
| `set_attenuation` | Inner radius, falloff, spatialization, occlusion |
| `set_reverb_settings` | Global or per-AudioVolume reverb |
| `list_sound_classes` | Enumerate Sound Classes |
| `get_audio_info` | Inspect sound actors or assets |

**Example — Spawn an ambient wind sound:**
```
audio/spawn_sound  sound_asset_path=/Game/Audio/SFX/S_Wind  label=AmbientWind  location_z=500  auto_activate=true
```

---

### 10. sequencer — Level Sequencer

Create sequences, tracks, keyframes, camera cuts, and export cinematics.

| Tool | Description |
|------|-------------|
| `create_sequence` / `open_sequence` | Create or open a Level Sequence |
| `get_sequence_info` / `list_sequence_tracks` | Inspect sequence data |
| `add_actor_track` | Bind an actor with Transform/Visibility/Float tracks |
| `add_transform_keyframe` | Key location, rotation, scale at a time |
| `add_float_keyframe` | Key scalar properties (intensity, FOV, etc.) |
| `add_camera_cut` | Define which camera is active during a time range |
| `add_subsequence` | Embed child sequences for hierarchical composition |
| `set_sequence_range` | Playback range and frame rate |
| `remove_track` | Delete a track and its keyframes |
| `export_sequence` | Export to FBX or JSON |

**Example — Animate a camera dolly:**
```
sequencer/create_sequence  sequence_name=LS_DollyShot  destination_folder=/Game/Cinematics  frame_rate=24  duration=5
sequencer/add_actor_track   sequence_path=/Game/Cinematics/LS_DollyShot  actor_path=CineCamera_0
sequencer/add_transform_keyframe  sequence_path=/Game/Cinematics/LS_DollyShot  actor_path=CineCamera_0  time=0  location_x=0  location_y=0  location_z=200
sequencer/add_transform_keyframe  sequence_path=/Game/Cinematics/LS_DollyShot  actor_path=CineCamera_0  time=5  location_x=500  location_y=0  location_z=200
```

---

### 11. physics — Physics and Collision

Collision profiles, simulation, constraints, forces, and physics assets.

| Tool | Description |
|------|-------------|
| `set_collision_profile` | Assign collision preset (BlockAll, OverlapAll, etc.) |
| `enable_physics_simulation` | Toggle dynamic rigid body simulation |
| `set_physics_properties` | Mass, damping, friction, restitution, CCD |
| `set_collision_response` | Per-channel Ignore/Overlap/Block |
| `add_physics_constraint` | Fixed, Hinge, Prismatic, BallSocket joints |
| `create_physics_asset` | Generate collision bodies for a skeletal mesh |
| `apply_force` | Apply forces or impulses to physics bodies |
| `get_physics_info` | Simulation state, velocity, constraints |

**Example — Make a crate physically simulated:**
```
physics/enable_physics_simulation  actor_path=SM_Crate_0  enable=true  gravity_enabled=true
physics/set_physics_properties     actor_path=SM_Crate_0  mass=50  restitution=0.3  friction=0.7
```

---

### 12. ai — AI Systems

Behavior trees, blackboards, EQS queries, State Trees, and AI perception.

| Tool | Description |
|------|-------------|
| `create_behavior_tree` | Create a new BT asset |
| `create_blackboard` | Create Blackboard Data with typed keys |
| `create_eqs_query` | Environment Query with generators and tests |
| `create_state_tree` | UE5 hierarchical state machine |
| `set_blackboard_key` | Set runtime blackboard values |
| `set_ai_perception` | Configure sight, hearing, damage, team senses |
| `assign_behavior_tree` | Bind a BT to an AI Controller |
| `get_ai_controller_info` | BT status, blackboard snapshot, perception data |
| `list_behavior_trees` | Find BT assets in the project |
| `run_eqs_query` | Execute an EQS query and return scored results |

**Example — Set up an AI enemy with patrol behavior:**
```
ai/create_blackboard   blackboard_name=BB_Enemy  destination_folder=/Game/AI  keys=TargetActor:Object,PatrolIndex:Int,IsAlert:Bool
ai/create_behavior_tree  tree_name=BT_EnemyPatrol  destination_folder=/Game/AI
ai/assign_behavior_tree  controller_path=AIController_0  behavior_tree_path=/Game/AI/BT_EnemyPatrol
```

---

### 13. niagara — Niagara VFX

Create Niagara systems and emitters, set parameters, spawn effects.

| Tool | Description |
|------|-------------|
| `create_niagara_system` | New system from template (Fountain, Sparks, Fire, etc.) |
| `create_niagara_emitter` | Sprite, Mesh, Ribbon, Light, or Audio emitters |
| `set_niagara_parameter` | Set user parameters on assets |
| `set_niagara_variable` | Override variables on live components |
| `activate_niagara_system` | Play/pause/reset effects |
| `spawn_niagara_at_location` | Place an effect in the world |
| `get_niagara_info` | Inspect system/emitter details |
| `list_niagara_modules` | Available modules by category (Spawn, Update, Render) |

**Example — Spawn a fire effect:**
```
niagara/create_niagara_system  system_name=NS_CampFire  destination_folder=/Game/VFX  template=Fire
niagara/spawn_niagara_at_location  system_path=/Game/VFX/NS_CampFire  location_z=50  label=CampfireVFX
```

---

### 14. ui — UMG Widget System

Create Widget Blueprints, animations, and manage viewport widgets.

| Tool | Description |
|------|-------------|
| `create_widget_blueprint` | New widget with optional HUD/Menu/Dialog template |
| `create_widget_animation` | Animate opacity, translation, scale with easing |
| `add_widget_to_viewport` | Show widget on screen with z-order |
| `remove_widget_from_viewport` | Remove by class or instance |
| `set_widget_property` | Set Text, Visibility, Color, FontSize, etc. |
| `get_widget_info` | Inspect widget hierarchy and state |
| `list_widget_bindings` | Show data bindings on a widget |

**Example — Create a HUD widget and show it:**
```
ui/create_widget_blueprint  widget_name=WBP_GameHUD  destination_folder=/Game/UI  template=HUD
ui/add_widget_to_viewport   widget_path=/Game/UI/WBP_GameHUD  z_order=10
```

---

### 15. mrq — Movie Render Queue

Set up render queues, jobs, passes, and export high-quality renders.

| Tool | Description |
|------|-------------|
| `create_render_queue` | Create or reset a render pipeline |
| `add_render_job` | Add a sequence to render with frame range |
| `add_render_pass` | DeferredLighting, PathTracer, ObjectId, GBuffer, etc. |
| `add_beauty_pass` | Convenience: deferred or path-traced beauty |
| `add_gbuffer_passes` | Add BaseColor, Normal, Roughness, Metallic, Depth |
| `configure_antialiasing` | Spatial/temporal samples, AA method, warm-up |
| `set_output_settings` | Resolution, format, naming template |
| `render_queue` | Start rendering (long-running) |
| `cancel_render` / `get_render_status` | Monitor and control |
| `list_render_jobs` / `remove_render_job` | Queue management |

**Example — Render a cinematic with GBuffer AOVs for ML data:**
```
mrq/create_render_queue
mrq/add_render_job       sequence_path=/Game/Cinematics/LS_Main  map_path=/Game/Maps/Main
mrq/add_beauty_pass      use_path_tracer=true  output_format=EXR  resolution_x=3840  resolution_y=2160
mrq/add_gbuffer_passes   output_format=EXR  resolution_x=3840  resolution_y=2160
mrq/render_queue
```

---

### 16. pcg — Procedural Content Generation

Build and execute PCG graphs for procedural level design.

| Tool | Description |
|------|-------------|
| `create_pcg_graph` | Create a graph on a new or existing actor |
| `add_pcg_node` | Add SurfaceSampler, MeshSampler, StaticMeshSpawner, etc. |
| `connect_pcg_nodes` | Wire nodes via pins |
| `set_pcg_settings` | Configure node properties |
| `execute_pcg_graph` | Run the graph to generate content |
| `get_pcg_info` | Inspect nodes, connections, execution stats |

**Example — Scatter trees on a landscape:**
```
pcg/create_pcg_graph      graph_name=PCG_ForestScatter
pcg/add_pcg_node          graph_name=PCG_ForestScatter  node_type=SurfaceSampler  node_label=Sampler
pcg/add_pcg_node          graph_name=PCG_ForestScatter  node_type=StaticMeshSpawner  node_label=TreeSpawner
pcg/connect_pcg_nodes     graph_name=PCG_ForestScatter  source_node_label=Sampler  target_node_label=TreeSpawner
pcg/execute_pcg_graph     graph_name=PCG_ForestScatter
```

---

### 17. landscape — Terrain and Foliage

Create landscapes, sculpt, paint layers, manage foliage, import/export heightmaps.

| Tool | Description |
|------|-------------|
| `create_landscape` | Create with grid dimensions, scale, and material |
| `sculpt_landscape` | Sculpt, Smooth, Flatten, Erosion, Noise tools |
| `paint_landscape_layer` | Paint material layers (Grass, Rock, Dirt) |
| `add_foliage_type` | Register a mesh for procedural foliage |
| `paint_foliage` | Paint or erase foliage instances |
| `import_heightmap` / `export_heightmap` | 16-bit heightmap I/O |
| `get_landscape_info` | Grid, components, layers, foliage types |

**Example — Create a landscape and sculpt a hill:**
```
landscape/create_landscape  components_x=8  components_y=8  scale_z=200  material_path=/Game/Materials/M_Landscape
landscape/sculpt_landscape  center_x=5000  center_y=5000  radius=2000  strength=0.5  tool_mode=Sculpt
```

---

### 18. input — Enhanced Input System

Create Input Actions, Mapping Contexts, triggers, and modifiers.

| Tool | Description |
|------|-------------|
| `create_input_action` | Define abstract inputs (Bool, Axis1D, Axis2D, Axis3D) |
| `create_mapping_context` | Group of key-to-action bindings |
| `add_action_mapping` | Bind a key to an action in a context |
| `set_trigger` | Down, Pressed, Released, Hold, Tap, Pulse, ChordAction |
| `set_modifier` | Negate, Scalar, DeadZone, Swizzle, FOVScaling, Smooth |
| `list_input_actions` | Enumerate all project input actions |

**Example — Set up WASD movement:**
```
input/create_input_action    action_name=IA_Move  value_type=Axis2D
input/create_mapping_context context_name=IMC_Default
input/add_action_mapping     context_name=IMC_Default  action_name=IA_Move  key=W
input/set_modifier           context_name=IMC_Default  action_name=IA_Move  key=W  modifier_type=Swizzle  swizzle_order=YXZ
input/add_action_mapping     context_name=IMC_Default  action_name=IA_Move  key=S
input/set_modifier           context_name=IMC_Default  action_name=IA_Move  key=S  modifier_type=Negate
input/set_modifier           context_name=IMC_Default  action_name=IA_Move  key=S  modifier_type=Swizzle  swizzle_order=YXZ
```

---

### 19. networking — Multiplayer and Replication

Configure actor replication, RPCs, and network relevancy.

| Tool | Description |
|------|-------------|
| `set_replication` | Enable replication, movement sync, update frequency |
| `set_net_role` | Authority, SimulatedProxy, AutonomousProxy |
| `add_rpc` | Mark functions as Server, Client, or NetMulticast RPCs |
| `set_net_relevancy` | Distance culling, owner-only, always relevant |
| `get_replication_info` | Net role, replicated properties, RPC list |
| `list_replicated_properties` | All replicated UPROPERTYs on a class |

**Example — Make a pickup replicated:**
```
networking/set_replication  actor_label=BP_HealthPickup  replicate=true  replicate_movement=true  net_update_frequency=10
```

---

### 20. rendering — Rendering Features

Configure Nanite, Lumen, VSM, TSR, post-processing, and console variables.

| Tool | Description |
|------|-------------|
| `get_rendering_settings` | Current AA, GI, shadow, Nanite/Lumen state |
| `set_nanite_enabled` | Toggle Nanite virtualized geometry |
| `set_lumen_settings` | GI/reflection quality, hardware RT, final gather |
| `set_vsm_settings` | Virtual Shadow Maps resolution and pool size |
| `set_tsr_settings` | TSR screen percentage and quality |
| `set_post_process_settings` | Bloom, exposure, AO, grain, motion blur, vignette |
| `set_console_variable` | Set any CVar directly |
| `get_scalability_settings` | Current quality levels for all scalability groups |

**Example — Enable hardware ray-traced Lumen at highest quality:**
```
rendering/set_lumen_settings  enabled=true  gi_quality=3  reflection_quality=3  use_hardware_ray_tracing=true
```

---

### 21. python — Python Execution

Execute arbitrary Python code inside the Unreal Engine process.

| Tool | Description |
|------|-------------|
| `execute_python` | Run inline Python (the `unreal` module is pre-imported) |
| `execute_python_file` | Run a .py file with optional arguments |
| `get_python_paths` | Inspect sys.path, site-packages, and unreal module location |

**Example — Count all actors in the level via Python:**
```
python/execute_python  code="actors = unreal.EditorLevelLibrary.get_all_level_actors()\nprint(len(actors))"
```

---

### 22. code — Code Analysis and Reflection

Inspect UE's class hierarchy, properties, functions, and modules at runtime.

| Tool | Description |
|------|-------------|
| `get_class_hierarchy` | Parent chain and child tree for any UE class |
| `list_classes` | Filter by parent class or module |
| `get_class_properties` | All UPROPERTY fields with types, flags, defaults |
| `get_class_functions` | All UFUNCTION methods with parameters and flags |
| `search_classes` | Fuzzy search across class/property/function names |
| `list_modules` | Loaded modules by type (Runtime, Editor, Developer) |

**Example — Find all BlueprintCallable functions on AActor:**
```
code/get_class_functions  class_name=Actor  include_inherited=false  filter_name=
```

---

### 23. gamefeatures — Game Feature Plugins

List, activate, deactivate, and create Game Feature plugins.

| Tool | Description |
|------|-------------|
| `list_game_features` | All registered plugins with states |
| `activate_game_feature` | Transition to Active state |
| `deactivate_game_feature` | Reverse all feature actions |
| `create_game_feature` | New plugin with directory structure |
| `get_game_feature_info` | Actions, dependencies, data asset path |

**Example — Activate a combat system feature:**
```
gamefeatures/activate_game_feature  feature_name=CombatSystem
```

---

### 24. sourcecontrol — Source Control

Perforce/SVN/Git operations via UE's source control abstraction.

| Tool | Description |
|------|-------------|
| `get_source_control_status` | File states (CheckedOut, Added, etc.) |
| `checkout_files` | Check out for editing |
| `add_files` | Mark new files for add |
| `revert_files` | Discard local changes |
| `submit_changelist` | Submit with description |
| `get_file_history` | Revision log for a file |
| `mark_for_delete` | Schedule files for deletion |

**Example — Check out and submit a material change:**
```
sourcecontrol/checkout_files   file_paths=["/Game/Materials/M_Hero"]
sourcecontrol/submit_changelist  description="Updated hero material roughness"  file_paths=["/Game/Materials/M_Hero"]
```

---

### 25. profiling — Performance Profiling

Frame stats, GPU profiling, memory analysis, Unreal Insights traces.

| Tool | Description |
|------|-------------|
| `get_frame_stats` | Frame time, game/render/GPU thread, draw calls, triangles |
| `get_gpu_stats` | VRAM usage, render pass timings, Nanite/Lumen GPU stats |
| `get_memory_stats` | Physical/process memory, optional per-texture/mesh breakdown |
| `start_trace` / `stop_trace` | Capture .utrace files for Unreal Insights |
| `execute_stat_command` | Toggle stat overlays (fps, unit, gpu, memory, etc.) |

**Example — Profile GPU performance:**
```
profiling/get_frame_stats  num_frames=60
profiling/get_gpu_stats
profiling/execute_stat_command  command=gpu  enabled=true
```

---

## Common Workflows

### Scene Setup
1. `levels/create_level` — Start with an empty level
2. `actors/spawn_actor` — Place meshes, cameras, lights
3. `materials/create_material_instance` + `apply_material_to_actor` — Apply materials
4. `lighting/spawn_light` + `configure_global_illumination` — Set up lighting

### Blueprint Prototyping
1. `blueprints/create_blueprint` — Create from a parent class
2. `blueprints/add_blueprint_variable` — Add state variables
3. `blueprints/add_blueprint_component` — Add mesh, collision, etc.
4. `blueprints/add_blueprint_function` + `add_blueprint_node` + `connect_pins` — Build logic
5. `blueprints/compile_blueprint` — Compile and check for errors

### Cinematic Rendering
1. `sequencer/create_sequence` — Set up the sequence
2. `sequencer/add_actor_track` + `add_transform_keyframe` — Animate actors
3. `sequencer/add_camera_cut` — Define camera angles
4. `mrq/create_render_queue` + `add_render_job` + `add_beauty_pass` — Configure render
5. `mrq/render_queue` — Execute the render

### ML Training Data Generation
1. Set up scene with actors, materials, and lighting
2. `actors/spawn_actors_batch` — Batch-spawn objects for variety
3. `mrq/add_gbuffer_passes` — GBuffer AOVs (BaseColor, Normal, Depth, etc.)
4. `mrq/render_queue` — Render all passes
5. Iterate with `actors/set_transforms_batch` for pose/position variation

### Performance Investigation
1. `profiling/get_frame_stats` — Identify bottleneck (CPU vs GPU)
2. `profiling/get_gpu_stats` — Drill into render pass timings
3. `profiling/get_memory_stats` — Check VRAM/RAM usage
4. `rendering/set_console_variable` — Experiment with quality settings
5. `profiling/start_trace` / `stop_trace` — Deep analysis with Unreal Insights

---

## Tips

- **Actor paths** look like `/Game/Maps/Main.Main:PersistentLevel.StaticMeshActor_0`. Use `actors/find_actors` to discover them.
- **Asset paths** look like `/Game/Materials/M_Base.M_Base`. Use `assets/search_assets` to find them.
- **Batch operations** (`spawn_actors_batch`, `set_transforms_batch`, `delete_actors_batch`, `import_assets_batch`, `apply_materials_batch`) use partial failure: some items may succeed while others fail. Always check the response.
- **Timeout-sensitive operations**: `bake_lighting` (300s), `render_queue` (300s), `execute_pcg_graph` (120s), `create_landscape`/`import_heightmap` (30-60s). These have elevated timeouts built in.
- **Python escape hatch**: If a specific UE API isn't covered by a dedicated tool, use `python/execute_python` with the `unreal` module for direct access.
