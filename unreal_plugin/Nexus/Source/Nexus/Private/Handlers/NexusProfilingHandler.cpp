// Copyright Nexus Team. All Rights Reserved.

#include "Handlers/NexusProfilingHandler.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "HAL/PlatformMemory.h"
#include "HAL/PlatformTime.h"
#include "GenericPlatform/GenericPlatformMemory.h"
#include "RHI.h"
#include "RHIResources.h"
#include "ProfilingDebugging/TraceAuxiliary.h"
#include "Misc/App.h"
#include "Misc/Paths.h"
#include "Stats/Stats.h"
#include "UObject/UObjectIterator.h"
#include "Engine/Texture.h"
#include "Engine/StaticMesh.h"

// ─────────────────────────────────────────────────────────────────────────────
// Dispatch
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FNexusProfilingHandler::Handle(
    const FString& CommandType,
    const TSharedPtr<FJsonObject>& Params)
{
    FString SubCommand = CommandType.RightChop(GetNamespace().Len() + 1);

    if (SubCommand == TEXT("get_frame_stats"))       return HandleGetFrameStats(Params);
    if (SubCommand == TEXT("get_gpu_stats"))          return HandleGetGpuStats(Params);
    if (SubCommand == TEXT("get_memory_stats"))       return HandleGetMemoryStats(Params);
    if (SubCommand == TEXT("start_trace"))            return HandleStartTrace(Params);
    if (SubCommand == TEXT("stop_trace"))             return HandleStopTrace(Params);
    if (SubCommand == TEXT("execute_stat_command"))   return HandleExecuteStatCommand(Params);

    return MakeError(TEXT("NOT_IMPLEMENTED"),
        FString::Printf(TEXT("Command '%s' not yet implemented"), *CommandType));
}

// ─────────────────────────────────────────────────────────────────────────────
// profiling.get_frame_stats
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FNexusProfilingHandler::HandleGetFrameStats(
    const TSharedPtr<FJsonObject>& Params)
{
    int32 NumFrames = static_cast<int32>(GetNumberParam(Params, TEXT("num_frames"), 1.0));
    NumFrames = FMath::Clamp(NumFrames, 1, 120);

    TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());

    // Current frame timing from engine globals
    double DeltaTime = FApp::GetDeltaTime();
    float GameThreadTimeMs = FPlatformTime::ToMilliseconds(GGameThreadTime);
    float RenderThreadTimeMs = FPlatformTime::ToMilliseconds(GRenderThreadTime);

    // GPU time from RHI
    float GpuTimeMs = 0.0f;
    uint32 GPUCycles = 0;
    if (GDynamicRHI)
    {
        // RHIGetGPUFrameCycles returns GPU cycles for last completed frame
        GPUCycles = RHIGetGPUFrameCycles(0);
        GpuTimeMs = FPlatformTime::ToMilliseconds(GPUCycles);
    }

    // FPS
    double Fps = (DeltaTime > 0.0) ? (1.0 / DeltaTime) : 0.0;

    Data->SetNumberField(TEXT("delta_time_ms"), DeltaTime * 1000.0);
    Data->SetNumberField(TEXT("fps"), Fps);
    Data->SetNumberField(TEXT("game_thread_ms"), GameThreadTimeMs);
    Data->SetNumberField(TEXT("render_thread_ms"), RenderThreadTimeMs);
    Data->SetNumberField(TEXT("gpu_time_ms"), GpuTimeMs);
    Data->SetNumberField(TEXT("gpu_cycles"), static_cast<double>(GPUCycles));
    Data->SetNumberField(TEXT("num_frames_sampled"), NumFrames);

    // Draw call and triangle stats from viewport
    if (GEngine && GEngine->GetWorld())
    {
        UWorld* World = GEngine->GetWorld();
        if (World && World->Scene)
        {
            // These are available via stat counters
            Data->SetStringField(TEXT("note"),
                TEXT("For detailed per-frame history, use start_trace + Unreal Insights"));
        }
    }

    // Engine frame number
    Data->SetNumberField(TEXT("frame_number"), static_cast<double>(GFrameNumber));

    return MakeSuccess(Data);
}

// ─────────────────────────────────────────────────────────────────────────────
// profiling.get_gpu_stats
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FNexusProfilingHandler::HandleGetGpuStats(
    const TSharedPtr<FJsonObject>& Params)
{
    TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());

    // GPU adapter info from RHI
    if (GDynamicRHI)
    {
        FString AdapterName = GRHIAdapterName;
        FString DriverInfo = GRHIAdapterInternalDriverVersion;
        FString RHIName = GDynamicRHI->GetName();

        Data->SetStringField(TEXT("adapter_name"), AdapterName);
        Data->SetStringField(TEXT("driver_version"), DriverInfo);
        Data->SetStringField(TEXT("rhi_name"), RHIName);

        // GPU frame cycles
        uint32 GPUCycles = RHIGetGPUFrameCycles(0);
        float GpuTimeMs = FPlatformTime::ToMilliseconds(GPUCycles);
        Data->SetNumberField(TEXT("gpu_time_ms"), GpuTimeMs);
    }
    else
    {
        Data->SetStringField(TEXT("adapter_name"), TEXT("Unknown"));
        Data->SetStringField(TEXT("rhi_name"), TEXT("None"));
    }

    // VRAM stats via texture memory
    FTextureMemoryStats TextureMemStats;
    RHIGetTextureMemoryStats(TextureMemStats);

    TSharedPtr<FJsonObject> VramObj = MakeShareable(new FJsonObject());
    if (TextureMemStats.DedicatedVideoMemory > 0)
    {
        VramObj->SetNumberField(TEXT("dedicated_video_memory_mb"),
            TextureMemStats.DedicatedVideoMemory / (1024.0 * 1024.0));
        VramObj->SetNumberField(TEXT("dedicated_system_memory_mb"),
            TextureMemStats.DedicatedSystemMemory / (1024.0 * 1024.0));
        VramObj->SetNumberField(TEXT("shared_system_memory_mb"),
            TextureMemStats.SharedSystemMemory / (1024.0 * 1024.0));
        VramObj->SetNumberField(TEXT("total_graphics_memory_mb"),
            TextureMemStats.TotalGraphicsMemory / (1024.0 * 1024.0));

        // In UE 5.7, AllocatedMemorySize was replaced with StreamingMemorySize + NonStreamingMemorySize
        uint64 TotalAllocatedMemory = TextureMemStats.StreamingMemorySize + TextureMemStats.NonStreamingMemorySize;
        VramObj->SetNumberField(TEXT("allocated_memory_mb"),
            TotalAllocatedMemory / (1024.0 * 1024.0));
        VramObj->SetNumberField(TEXT("streaming_memory_mb"),
            TextureMemStats.StreamingMemorySize / (1024.0 * 1024.0));
        VramObj->SetNumberField(TEXT("non_streaming_memory_mb"),
            TextureMemStats.NonStreamingMemorySize / (1024.0 * 1024.0));
        if (TextureMemStats.TexturePoolSize > 0)
        {
            VramObj->SetNumberField(TEXT("texture_pool_size_mb"),
                TextureMemStats.TexturePoolSize / (1024.0 * 1024.0));
        }
    }
    else
    {
        VramObj->SetStringField(TEXT("note"), TEXT("VRAM stats not available on this platform"));
    }
    Data->SetObjectField(TEXT("vram"), VramObj);

    // Shader model / feature level
    ERHIFeatureLevel::Type FeatureLevel = GMaxRHIFeatureLevel;
    FString FeatureLevelStr;
    switch (FeatureLevel)
    {
    case ERHIFeatureLevel::ES3_1: FeatureLevelStr = TEXT("ES3.1"); break;
    case ERHIFeatureLevel::SM5:   FeatureLevelStr = TEXT("SM5"); break;
    case ERHIFeatureLevel::SM6:   FeatureLevelStr = TEXT("SM6"); break;
    default:                      FeatureLevelStr = TEXT("Unknown"); break;
    }
    Data->SetStringField(TEXT("feature_level"), FeatureLevelStr);

    return MakeSuccess(Data);
}

// ─────────────────────────────────────────────────────────────────────────────
// profiling.get_memory_stats
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FNexusProfilingHandler::HandleGetMemoryStats(
    const TSharedPtr<FJsonObject>& Params)
{
    bool bIncludeTextureStats = GetBoolParam(Params, TEXT("include_texture_stats"), false);
    bool bIncludeMeshStats = GetBoolParam(Params, TEXT("include_mesh_stats"), false);

    TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());

    // Platform memory stats
    FPlatformMemoryStats MemStats = FPlatformMemory::GetStats();

    TSharedPtr<FJsonObject> PhysicalObj = MakeShareable(new FJsonObject());
    PhysicalObj->SetNumberField(TEXT("total_physical_mb"),
        MemStats.TotalPhysical / (1024.0 * 1024.0));
    PhysicalObj->SetNumberField(TEXT("available_physical_mb"),
        MemStats.AvailablePhysical / (1024.0 * 1024.0));
    PhysicalObj->SetNumberField(TEXT("used_physical_mb"),
        (MemStats.TotalPhysical - MemStats.AvailablePhysical) / (1024.0 * 1024.0));
    PhysicalObj->SetNumberField(TEXT("total_virtual_mb"),
        MemStats.TotalVirtual / (1024.0 * 1024.0));
    PhysicalObj->SetNumberField(TEXT("available_virtual_mb"),
        MemStats.AvailableVirtual / (1024.0 * 1024.0));
    Data->SetObjectField(TEXT("physical"), PhysicalObj);

    // Process memory
    TSharedPtr<FJsonObject> ProcessObj = MakeShareable(new FJsonObject());
    ProcessObj->SetNumberField(TEXT("used_physical_mb"),
        MemStats.UsedPhysical / (1024.0 * 1024.0));
    ProcessObj->SetNumberField(TEXT("used_virtual_mb"),
        MemStats.UsedVirtual / (1024.0 * 1024.0));
    ProcessObj->SetNumberField(TEXT("peak_used_physical_mb"),
        MemStats.PeakUsedPhysical / (1024.0 * 1024.0));
    ProcessObj->SetNumberField(TEXT("peak_used_virtual_mb"),
        MemStats.PeakUsedVirtual / (1024.0 * 1024.0));
    Data->SetObjectField(TEXT("process"), ProcessObj);

    // Per-texture memory breakdown (optional — can be slow)
    if (bIncludeTextureStats)
    {
        TArray<TSharedPtr<FJsonValue>> TexturesArr;
        int64 TotalTextureBytes = 0;
        int32 TextureCount = 0;

        for (TObjectIterator<UTexture> It; It; ++It)
        {
            UTexture* Texture = *It;
            if (!Texture) continue;

            int64 ResourceSize = Texture->CalcTextureMemorySizeEnum(TMC_ResidentMips);
            TotalTextureBytes += ResourceSize;
            TextureCount++;

            // Only include the top textures by size (limit to 50 to avoid huge responses)
            if (TexturesArr.Num() < 50 && ResourceSize > 0)
            {
                TSharedPtr<FJsonObject> TexObj = MakeShareable(new FJsonObject());
                TexObj->SetStringField(TEXT("name"), Texture->GetName());
                TexObj->SetStringField(TEXT("path"), Texture->GetPathName());
                TexObj->SetNumberField(TEXT("size_mb"), ResourceSize / (1024.0 * 1024.0));

                // Texture dimensions
                if (UTexture2D* Tex2D = Cast<UTexture2D>(Texture))
                {
                    TexObj->SetNumberField(TEXT("width"), Tex2D->GetSizeX());
                    TexObj->SetNumberField(TEXT("height"), Tex2D->GetSizeY());
                    TexObj->SetNumberField(TEXT("num_mips"), Tex2D->GetNumMips());
                }

                TexturesArr.Add(MakeShareable(new FJsonValueObject(TexObj)));
            }
        }

        TSharedPtr<FJsonObject> TexStatsObj = MakeShareable(new FJsonObject());
        TexStatsObj->SetNumberField(TEXT("total_texture_memory_mb"),
            TotalTextureBytes / (1024.0 * 1024.0));
        TexStatsObj->SetNumberField(TEXT("texture_count"), TextureCount);
        TexStatsObj->SetArrayField(TEXT("top_textures"), TexturesArr);
        Data->SetObjectField(TEXT("texture_stats"), TexStatsObj);
    }

    // Per-mesh memory breakdown (optional — can be slow)
    if (bIncludeMeshStats)
    {
        TArray<TSharedPtr<FJsonValue>> MeshesArr;
        int64 TotalMeshBytes = 0;
        int32 MeshCount = 0;

        for (TObjectIterator<UStaticMesh> It; It; ++It)
        {
            UStaticMesh* Mesh = *It;
            if (!Mesh) continue;

            FResourceSizeEx ResSize;
            Mesh->GetResourceSizeEx(ResSize);
            int64 ResourceSize = ResSize.GetTotalMemoryBytes();
            TotalMeshBytes += ResourceSize;
            MeshCount++;

            // Only include the top meshes by size (limit to 50)
            if (MeshesArr.Num() < 50 && ResourceSize > 0)
            {
                TSharedPtr<FJsonObject> MeshObj = MakeShareable(new FJsonObject());
                MeshObj->SetStringField(TEXT("name"), Mesh->GetName());
                MeshObj->SetStringField(TEXT("path"), Mesh->GetPathName());
                MeshObj->SetNumberField(TEXT("size_mb"), ResourceSize / (1024.0 * 1024.0));
                MeshObj->SetNumberField(TEXT("num_lods"), Mesh->GetNumLODs());

                if (Mesh->GetRenderData() && Mesh->GetRenderData()->LODResources.Num() > 0)
                {
                    const FStaticMeshLODResources& LOD0 = Mesh->GetRenderData()->LODResources[0];
                    MeshObj->SetNumberField(TEXT("vertex_count"), LOD0.GetNumVertices());
                    MeshObj->SetNumberField(TEXT("triangle_count"), LOD0.GetNumTriangles());
                }

                MeshesArr.Add(MakeShareable(new FJsonValueObject(MeshObj)));
            }
        }

        TSharedPtr<FJsonObject> MeshStatsObj = MakeShareable(new FJsonObject());
        MeshStatsObj->SetNumberField(TEXT("total_mesh_memory_mb"),
            TotalMeshBytes / (1024.0 * 1024.0));
        MeshStatsObj->SetNumberField(TEXT("mesh_count"), MeshCount);
        MeshStatsObj->SetArrayField(TEXT("top_meshes"), MeshesArr);
        Data->SetObjectField(TEXT("mesh_stats"), MeshStatsObj);
    }

    return MakeSuccess(Data);
}

// ─────────────────────────────────────────────────────────────────────────────
// profiling.start_trace
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FNexusProfilingHandler::HandleStartTrace(
    const TSharedPtr<FJsonObject>& Params)
{
    FString OutputFile = GetStringParam(Params, TEXT("output_file"));
    double DurationSeconds = GetNumberParam(Params, TEXT("duration_seconds"), 0.0);

    // Build channel list from params
    TArray<FString> Channels;
    const TArray<TSharedPtr<FJsonValue>>* ChannelsArray = nullptr;
    if (Params.IsValid() && Params->TryGetArrayField(TEXT("channels"), ChannelsArray))
    {
        for (const TSharedPtr<FJsonValue>& Val : *ChannelsArray)
        {
            FString Channel = Val->AsString();
            if (!Channel.IsEmpty())
            {
                Channels.Add(Channel);
            }
        }
    }

    // Build the trace command arguments
    // FTraceAuxiliary expects channels as comma-separated
    FString ChannelsStr;
    if (Channels.Num() > 0)
    {
        ChannelsStr = FString::Join(Channels, TEXT(","));
    }
    else
    {
        ChannelsStr = TEXT("default");
    }

    // Determine output path
    if (OutputFile.IsEmpty())
    {
        OutputFile = FPaths::ProfilingDir() / FString::Printf(TEXT("NexusTrace_%s.utrace"),
            *FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S")));
    }

    // Start the trace using FTraceAuxiliary
    bool bStarted = FTraceAuxiliary::Start(
        FTraceAuxiliary::EConnectionType::Network,
        *ChannelsStr,
        nullptr, // No specific tail
        nullptr  // No specific connection string
    );

    TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
    Data->SetBoolField(TEXT("started"), bStarted);
    Data->SetStringField(TEXT("channels"), ChannelsStr);
    Data->SetStringField(TEXT("output_file"), OutputFile);

    if (DurationSeconds > 0.0)
    {
        Data->SetNumberField(TEXT("duration_seconds"), DurationSeconds);
        Data->SetStringField(TEXT("note"),
            TEXT("Duration-based auto-stop is requested. Use stop_trace to manually stop if needed."));
    }

    if (!bStarted)
    {
        return MakeError(TEXT("TRACE_START_FAILED"),
            TEXT("Failed to start trace. A trace may already be active, or Trace support is not enabled."));
    }

    return MakeSuccess(Data);
}

// ─────────────────────────────────────────────────────────────────────────────
// profiling.stop_trace
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FNexusProfilingHandler::HandleStopTrace(
    const TSharedPtr<FJsonObject>& Params)
{
    // Stop the active trace
    FTraceAuxiliary::Stop();

    TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
    Data->SetBoolField(TEXT("stopped"), true);
    Data->SetStringField(TEXT("note"),
        TEXT("Trace stopped. Check the Saved/Profiling/ directory for the .utrace file. Open with Unreal Insights for analysis."));

    return MakeSuccess(Data);
}

// ─────────────────────────────────────────────────────────────────────────────
// profiling.execute_stat_command
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FNexusProfilingHandler::HandleExecuteStatCommand(
    const TSharedPtr<FJsonObject>& Params)
{
    FString Command = GetStringParam(Params, TEXT("command"));
    if (Command.IsEmpty())
    {
        return MakeError(TEXT("MISSING_PARAM"), TEXT("command is required"));
    }

    bool bEnabled = GetBoolParam(Params, TEXT("enabled"), true);

    // Build the full stat command string
    // UE stat commands are: "stat <name>" to toggle, or we can use explicit enable/disable
    FString FullCommand;
    if (bEnabled)
    {
        FullCommand = FString::Printf(TEXT("stat %s"), *Command);
    }
    else
    {
        // Stat commands are toggles, but "stat none" disables all.
        // For specific disable, we just toggle it again (same command).
        FullCommand = FString::Printf(TEXT("stat %s"), *Command);
    }

    // Execute via GEngine
    bool bExecuted = false;
    if (GEngine)
    {
        UWorld* World = GEngine->GetWorld();
        if (World)
        {
            GEngine->Exec(World, *FullCommand);
            bExecuted = true;
        }
        else
        {
            // Try without a world context
            GEngine->Exec(nullptr, *FullCommand);
            bExecuted = true;
        }
    }

    TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
    Data->SetBoolField(TEXT("executed"), bExecuted);
    Data->SetStringField(TEXT("command"), FullCommand);
    Data->SetBoolField(TEXT("enabled"), bEnabled);
    Data->SetStringField(TEXT("note"),
        FString::Printf(TEXT("Stat command '%s' %s. Stat overlays are visible in the editor viewport."),
            *Command, bEnabled ? TEXT("enabled") : TEXT("toggled")));

    if (!bExecuted)
    {
        return MakeError(TEXT("ENGINE_NOT_AVAILABLE"),
            TEXT("GEngine is not available. Cannot execute stat commands."));
    }

    return MakeSuccess(Data);
}
