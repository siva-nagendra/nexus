// Copyright Nexus Team. All Rights Reserved.

#pragma once

#include "NexusCommandHandler.h"

/**
 * Handler for Profiling / Performance commands:
 * Frame timing, GPU stats, memory stats, Unreal Insights trace capture,
 * and stat console commands. Most commands are read-only diagnostics.
 * Namespace: "profiling", 6 commands.
 *
 * Key UE APIs: FApp::GetDeltaTime(), FPlatformMemory::GetStats(),
 *              GDynamicRHI, FTraceAuxiliary::Start/Stop(),
 *              GEngine->Exec() for stat commands.
 */
class FNexusProfilingHandler : public INexusCommandHandler
{
public:
    virtual FString GetNamespace() const override { return TEXT("profiling"); }

    virtual TArray<FString> GetSupportedCommands() const override
    {
        return {
            TEXT("get_frame_stats"),
            TEXT("get_gpu_stats"),
            TEXT("get_memory_stats"),
            TEXT("start_trace"),
            TEXT("stop_trace"),
            TEXT("execute_stat_command")
        };
    }

    virtual TSharedPtr<FJsonObject> Handle(
        const FString& CommandType,
        const TSharedPtr<FJsonObject>& Params) override;

private:
    // Command handlers
    TSharedPtr<FJsonObject> HandleGetFrameStats(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleGetGpuStats(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleGetMemoryStats(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleStartTrace(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleStopTrace(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleExecuteStatCommand(const TSharedPtr<FJsonObject>& Params);
};
