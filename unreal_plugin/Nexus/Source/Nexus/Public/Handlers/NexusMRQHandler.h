// Copyright Nexus Team. All Rights Reserved.

#pragma once

#include "NexusCommandHandler.h"

/**
 * Handler for Movie Render Queue (MRQ) subsystem commands: queue creation,
 * job/pass management, render execution, output configuration, and GBuffer
 * AOV passes for ML training data generation.
 * Namespace: "mrq", 12 commands.
 */
class FNexusMRQHandler : public INexusCommandHandler
{
public:
    virtual FString GetNamespace() const override { return TEXT("mrq"); }

    virtual TArray<FString> GetSupportedCommands() const override
    {
        return {
            TEXT("create_render_queue"),
            TEXT("add_render_job"),
            TEXT("add_render_pass"),
            TEXT("render_queue"),
            TEXT("cancel_render"),
            TEXT("get_render_status"),
            TEXT("list_render_jobs"),
            TEXT("remove_render_job"),
            TEXT("set_output_settings"),
            TEXT("add_beauty_pass"),
            TEXT("add_gbuffer_passes"),
            TEXT("configure_antialiasing")
        };
    }

    virtual TSharedPtr<FJsonObject> Handle(
        const FString& CommandType,
        const TSharedPtr<FJsonObject>& Params) override;

private:
    // Command handlers
    TSharedPtr<FJsonObject> HandleCreateRenderQueue(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleAddRenderJob(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleAddRenderPass(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleRenderQueue(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleCancelRender(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleGetRenderStatus(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleListRenderJobs(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleRemoveRenderJob(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleSetOutputSettings(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleAddBeautyPass(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleAddGBufferPasses(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleConfigureAntialiasing(const TSharedPtr<FJsonObject>& Params);

    // Helper: find or create a queue subsystem instance
    class UMoviePipelineQueue* GetOrCreateQueue(const FString& QueueName);

    // Helper: find a job by name within a queue
    class UMoviePipelineExecutorJob* FindJobByName(class UMoviePipelineQueue* Queue, const FString& JobName);

    // Helper: map pass type string to the appropriate UE render pass class
    UClass* ResolvePassClass(const FString& PassType);

    // In-memory queue storage (keyed by queue name)
    TMap<FString, TWeakObjectPtr<class UMoviePipelineQueue>> QueueMap;
};
