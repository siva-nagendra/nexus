"""UE enum and type mappings for tool parameters."""

from __future__ import annotations

from enum import StrEnum


class EBlendMode(StrEnum):
    OPAQUE = "Opaque"
    TRANSLUCENT = "Translucent"
    MASKED = "Masked"
    ADDITIVE = "Additive"
    MODULATE = "Modulate"
    ALPHA_COMPOSITE = "AlphaComposite"
    ALPHA_HOLDOUT = "AlphaHoldout"


class EShadingModel(StrEnum):
    DEFAULT_LIT = "DefaultLit"
    UNLIT = "Unlit"
    SUBSURFACE = "Subsurface"
    SUBSURFACE_PROFILE = "SubsurfaceProfile"
    CLEAR_COAT = "ClearCoat"
    CLOTH = "Cloth"
    EYE = "Eye"
    HAIR = "Hair"
    THIN_TRANSLUCENT = "ThinTranslucent"
    SINGLE_LAYER_WATER = "SingleLayerWater"
    STRATA = "Strata"


class ECollisionChannel(StrEnum):
    WORLD_STATIC = "WorldStatic"
    WORLD_DYNAMIC = "WorldDynamic"
    PAWN = "Pawn"
    VISIBILITY = "Visibility"
    CAMERA = "Camera"
    PHYSICS_BODY = "PhysicsBody"
    VEHICLE = "Vehicle"
    DESTRUCTIBLE = "Destructible"


class ELightType(StrEnum):
    POINT = "PointLight"
    SPOT = "SpotLight"
    DIRECTIONAL = "DirectionalLight"
    RECT = "RectLight"
    SKY = "SkyLight"


class EMobility(StrEnum):
    STATIC = "Static"
    STATIONARY = "Stationary"
    MOVABLE = "Movable"


class EAntiAliasingMethod(StrEnum):
    TSR = "TSR"
    TAA = "TAA"
    FXAA = "FXAA"
    MSAA = "MSAA"
    NONE = "None"


class EGlobalIllumination(StrEnum):
    LUMEN = "Lumen"
    SCREEN_SPACE = "ScreenSpace"
    NONE = "None"


class EOutputFormat(StrEnum):
    EXR = "EXR"
    PNG = "PNG"
    JPEG = "JPEG"
    BMP = "BMP"


# Common actor class paths
COMMON_ACTOR_CLASSES = {
    "static_mesh": "/Script/Engine.StaticMeshActor",
    "skeletal_mesh": "/Script/Engine.SkeletalMeshActor",
    "point_light": "/Script/Engine.PointLight",
    "spot_light": "/Script/Engine.SpotLight",
    "directional_light": "/Script/Engine.DirectionalLight",
    "rect_light": "/Script/Engine.RectLight",
    "sky_light": "/Script/Engine.SkyLight",
    "camera": "/Script/Engine.CameraActor",
    "player_start": "/Script/Engine.PlayerStart",
    "empty": "/Script/Engine.Actor",
    "cube": "/Script/Engine.StaticMeshActor",
    "sphere": "/Script/Engine.StaticMeshActor",
    "plane": "/Script/Engine.StaticMeshActor",
    "cylinder": "/Script/Engine.StaticMeshActor",
    "cone": "/Script/Engine.StaticMeshActor",
    "exponential_height_fog": "/Script/Engine.ExponentialHeightFog",
    "sky_atmosphere": "/Script/Engine.SkyAtmosphere",
    "volumetric_cloud": "/Script/Engine.VolumetricCloud",
    "post_process_volume": "/Script/Engine.PostProcessVolume",
    "audio": "/Script/Engine.AmbientSound",
    "trigger_box": "/Script/Engine.TriggerBox",
    "trigger_sphere": "/Script/Engine.TriggerSphere",
    "blocking_volume": "/Script/Engine.BlockingVolume",
    "nav_mesh": "/Script/NavigationSystem.NavMeshBoundsVolume",
    "niagara": "/Script/Niagara.NiagaraActor",
    "decal": "/Script/Engine.DecalActor",
    "text_render": "/Script/Engine.TextRenderActor",
}

# Common basic shape meshes
BASIC_SHAPES = {
    "cube": "/Engine/BasicShapes/Cube.Cube",
    "sphere": "/Engine/BasicShapes/Sphere.Sphere",
    "cylinder": "/Engine/BasicShapes/Cylinder.Cylinder",
    "cone": "/Engine/BasicShapes/Cone.Cone",
    "plane": "/Engine/BasicShapes/Plane.Plane",
}
