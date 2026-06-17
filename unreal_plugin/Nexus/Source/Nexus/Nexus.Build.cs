// Copyright Nexus Team. All Rights Reserved.

using UnrealBuildTool;

public class Nexus : ModuleRules
{
    public Nexus(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(new string[]
        {
            "Core",
            "CoreUObject",
            "Engine",
            "Sockets",
            "Networking",
            "Json",
            "JsonUtilities"
        });

        PrivateDependencyModuleNames.AddRange(new string[]
        {
            "UnrealEd",
            "LevelEditor",
            "EditorSubsystem",
            "Slate",
            "SlateCore",
            "InputCore",
            "LevelSequence",
            "MovieScene",
            "MovieRenderPipelineCore",
            "MovieRenderPipelineEditor",
            "Landscape",
            "Foliage",
            "NavigationSystem",
            "AIModule",
            "Niagara",
            "NiagaraEditor",
            "UMG",
            "PhysicsCore",
            "SourceControl",
            // New deps for Phase A handlers
            "EnhancedInput",
            "GameFeatures",
            "PCG",
            "AudioMixer",
            "AnimGraphRuntime",
            "TraceLog",
            "PythonScriptPlugin",
            "MovieSceneTools",
            "AssetTools",
            "EditorScriptingUtilities",
            "UMGEditor",
            "LandscapeEditor",
            // Linker deps for Blueprint K2 nodes and schema
            "BlueprintGraph",
            "KismetCompiler",
            // Audio editor (USoundCueFactoryNew)
            "AudioEditor",
            // MRQ render passes (UMoviePipelineImagePassBase, DeferredPassBase)
            "MovieRenderPipelineRenderPasses",
            // Sequencer track types (3DTransform, Float, CameraCut, Bool, Visibility)
            "MovieSceneTracks",
            // RHI/RenderCore for profiling (GDynamicRHI, RHIGetGPUFrameCycles, etc.)
            "RHI",
            "RenderCore",
            // MetaSound node CRUD (Phase 3B)
            "MetasoundEngine",
            "MetasoundEditor",
            "MetasoundFrontend",
            // Behavior Tree graph manipulation (Phase 4A)
            "AIGraph",
            "BehaviorTreeEditor"
        });
    }
}
