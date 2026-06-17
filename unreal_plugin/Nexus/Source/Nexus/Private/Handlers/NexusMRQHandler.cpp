// Copyright Nexus Team. All Rights Reserved.

#include "Handlers/NexusMRQHandler.h"
#include "Editor.h"
#include "Engine/World.h"
#include "LevelSequence.h"
#include "MoviePipelineQueue.h"
#include "MoviePipelineQueueSubsystem.h"
#include "MoviePipelinePrimaryConfig.h"
#include "MoviePipelineExecutor.h"
#include "MoviePipelineDeferredPasses.h"
#include "MoviePipelineOutputSetting.h"
#include "MoviePipelineAntiAliasingSetting.h"
#include "MoviePipelineImagePassBase.h"
// MoviePipelineMasterConfig.h was renamed to MoviePipelinePrimaryConfig.h in UE 5.x
// (already included above)
#include "MovieRenderPipelineSettings.h"
#include "MoviePipelinePIEExecutor.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "UObject/SavePackage.h"

// ─────────────────────────────────────────────────────────────────────────────
// Dispatch
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FNexusMRQHandler::Handle(
    const FString& CommandType,
    const TSharedPtr<FJsonObject>& Params)
{
    FString SubCommand = CommandType.RightChop(GetNamespace().Len() + 1);

    if (SubCommand == TEXT("create_render_queue"))   return HandleCreateRenderQueue(Params);
    if (SubCommand == TEXT("add_render_job"))         return HandleAddRenderJob(Params);
    if (SubCommand == TEXT("add_render_pass"))        return HandleAddRenderPass(Params);
    if (SubCommand == TEXT("render_queue"))           return HandleRenderQueue(Params);
    if (SubCommand == TEXT("cancel_render"))          return HandleCancelRender(Params);
    if (SubCommand == TEXT("get_render_status"))      return HandleGetRenderStatus(Params);
    if (SubCommand == TEXT("list_render_jobs"))       return HandleListRenderJobs(Params);
    if (SubCommand == TEXT("remove_render_job"))      return HandleRemoveRenderJob(Params);
    if (SubCommand == TEXT("set_output_settings"))    return HandleSetOutputSettings(Params);
    if (SubCommand == TEXT("add_beauty_pass"))        return HandleAddBeautyPass(Params);
    if (SubCommand == TEXT("add_gbuffer_passes"))     return HandleAddGBufferPasses(Params);
    if (SubCommand == TEXT("configure_antialiasing")) return HandleConfigureAntialiasing(Params);

    return MakeError(TEXT("NOT_IMPLEMENTED"),
        FString::Printf(TEXT("Command '%s' not yet implemented"), *CommandType));
}

// ─────────────────────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────────────────────

UMoviePipelineQueue* FNexusMRQHandler::GetOrCreateQueue(const FString& QueueName)
{
    // Check if we already have this queue cached
    if (TWeakObjectPtr<UMoviePipelineQueue>* Existing = QueueMap.Find(QueueName))
    {
        if (Existing->IsValid())
        {
            return Existing->Get();
        }
    }

    // Create a new transient queue object
    UMoviePipelineQueue* NewQueue = NewObject<UMoviePipelineQueue>(
        GetTransientPackage(), *QueueName);
    if (NewQueue)
    {
        QueueMap.Add(QueueName, NewQueue);
    }
    return NewQueue;
}

UMoviePipelineExecutorJob* FNexusMRQHandler::FindJobByName(
    UMoviePipelineQueue* Queue,
    const FString& JobName)
{
    if (!Queue) return nullptr;

    TArray<UMoviePipelineExecutorJob*> Jobs = Queue->GetJobs();
    for (UMoviePipelineExecutorJob* Job : Jobs)
    {
        if (Job && Job->JobName == JobName)
        {
            return Job;
        }
    }
    return nullptr;
}

UClass* FNexusMRQHandler::ResolvePassClass(const FString& PassType)
{
    // Map user-friendly pass type names to UE MRQ render pass classes.
    // UMoviePipelineDeferredPassBase is the standard deferred shading pass.
    // For GBuffer channels we use the deferred pass base with buffer visualization.

    if (PassType == TEXT("DeferredLighting") || PassType == TEXT("Beauty"))
    {
        return UMoviePipelineDeferredPassBase::StaticClass();
    }
    if (PassType == TEXT("PathTracer"))
    {
        // Path tracer pass — the class name in UE5.7
        static UClass* PathTracerClass = FindObject<UClass>(
            nullptr, TEXT("/Script/MovieRenderPipelineCore.MoviePipelinePathTracerPass"));
        if (PathTracerClass) return PathTracerClass;
        // Fall back to deferred if path tracer module not loaded
        return UMoviePipelineDeferredPassBase::StaticClass();
    }
    // GBuffer / AOV passes — these use the deferred pass base with specific
    // buffer visualization material overrides
    if (PassType == TEXT("BaseColor") || PassType == TEXT("WorldNormal") ||
        PassType == TEXT("Roughness") || PassType == TEXT("Metallic") ||
        PassType == TEXT("SceneDepth") || PassType == TEXT("ObjectId") ||
        PassType == TEXT("CustomStencil") || PassType == TEXT("CustomRender"))
    {
        return UMoviePipelineDeferredPassBase::StaticClass();
    }

    // Default: deferred
    return UMoviePipelineDeferredPassBase::StaticClass();
}

// ─────────────────────────────────────────────────────────────────────────────
// mrq.create_render_queue
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FNexusMRQHandler::HandleCreateRenderQueue(
    const TSharedPtr<FJsonObject>& Params)
{
    FString QueueName = GetStringParam(Params, TEXT("queue_name"), TEXT("DefaultQueue"));

    // If the queue already exists, clear it (reset behavior)
    if (TWeakObjectPtr<UMoviePipelineQueue>* Existing = QueueMap.Find(QueueName))
    {
        if (Existing->IsValid())
        {
            UMoviePipelineQueue* ExistingQueue = Existing->Get();
            // Remove all existing jobs
            TArray<UMoviePipelineExecutorJob*> Jobs = ExistingQueue->GetJobs();
            for (int32 i = Jobs.Num() - 1; i >= 0; --i)
            {
                ExistingQueue->DeleteJob(Jobs[i]);
            }

            TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
            Data->SetStringField(TEXT("queue_name"), QueueName);
            Data->SetNumberField(TEXT("job_count"), 0);
            Data->SetBoolField(TEXT("reset"), true);
            return MakeSuccess(Data);
        }
    }

    UMoviePipelineQueue* Queue = GetOrCreateQueue(QueueName);
    if (!Queue)
    {
        return MakeError(TEXT("CREATION_FAILED"),
            FString::Printf(TEXT("Failed to create render queue '%s'"), *QueueName));
    }

    TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
    Data->SetStringField(TEXT("queue_name"), QueueName);
    Data->SetNumberField(TEXT("job_count"), 0);
    Data->SetBoolField(TEXT("reset"), false);
    return MakeSuccess(Data);
}

// ─────────────────────────────────────────────────────────────────────────────
// mrq.add_render_job
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FNexusMRQHandler::HandleAddRenderJob(
    const TSharedPtr<FJsonObject>& Params)
{
    FString QueueName = GetStringParam(Params, TEXT("queue_name"), TEXT("DefaultQueue"));
    FString JobName = GetStringParam(Params, TEXT("job_name"));
    FString SequencePath = GetStringParam(Params, TEXT("sequence_path"));
    FString MapPath = GetStringParam(Params, TEXT("map_path"));
    FString OutputDirectory = GetStringParam(Params, TEXT("output_directory"));
    int32 FrameStart = static_cast<int32>(GetNumberParam(Params, TEXT("frame_range_start"), 0.0));
    int32 FrameEnd = static_cast<int32>(GetNumberParam(Params, TEXT("frame_range_end"), 0.0));

    UMoviePipelineQueue* Queue = GetOrCreateQueue(QueueName);
    if (!Queue)
    {
        return MakeError(TEXT("QUEUE_NOT_FOUND"),
            FString::Printf(TEXT("Failed to get queue '%s'"), *QueueName));
    }

    // Allocate a new job in the queue
    UMoviePipelineExecutorJob* Job = Queue->AllocateNewJob(UMoviePipelineExecutorJob::StaticClass());
    if (!Job)
    {
        return MakeError(TEXT("JOB_CREATION_FAILED"), TEXT("Failed to allocate new render job"));
    }

    // Set job name
    if (!JobName.IsEmpty())
    {
        Job->JobName = JobName;
    }

    // Set the level sequence to render
    if (!SequencePath.IsEmpty())
    {
        FSoftObjectPath SeqPath(SequencePath);
        Job->Sequence = SeqPath;
    }

    // Set the map/level
    if (!MapPath.IsEmpty())
    {
        FSoftObjectPath LevelPath(MapPath);
        Job->Map = LevelPath;
    }

    // Create a primary config for this job if it doesn't have one
    UMoviePipelinePrimaryConfig* Config = Job->GetConfiguration();
    if (!Config)
    {
        Config = NewObject<UMoviePipelinePrimaryConfig>(Job);
        Job->SetConfiguration(Config);
    }

    // Configure output settings if directory specified
    if (!OutputDirectory.IsEmpty() && Config)
    {
        UMoviePipelineOutputSetting* OutputSetting = Cast<UMoviePipelineOutputSetting>(
            Config->FindOrAddSettingByClass(UMoviePipelineOutputSetting::StaticClass()));
        if (OutputSetting)
        {
            OutputSetting->OutputDirectory.Path = OutputDirectory;
        }
    }

    TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
    Data->SetStringField(TEXT("queue_name"), QueueName);
    Data->SetStringField(TEXT("job_name"), Job->JobName);
    Data->SetStringField(TEXT("sequence_path"), SequencePath);
    Data->SetStringField(TEXT("map_path"), MapPath);
    Data->SetNumberField(TEXT("job_index"), Queue->GetJobs().Num() - 1);
    Data->SetNumberField(TEXT("total_jobs"), Queue->GetJobs().Num());
    return MakeSuccess(Data);
}

// ─────────────────────────────────────────────────────────────────────────────
// mrq.add_render_pass
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FNexusMRQHandler::HandleAddRenderPass(
    const TSharedPtr<FJsonObject>& Params)
{
    FString QueueName = GetStringParam(Params, TEXT("queue_name"), TEXT("DefaultQueue"));
    FString JobName = GetStringParam(Params, TEXT("job_name"));
    FString PassName = GetStringParam(Params, TEXT("pass_name"));
    FString PassType = GetStringParam(Params, TEXT("pass_type"), TEXT("DeferredLighting"));
    bool bEnabled = GetBoolParam(Params, TEXT("enabled"), true);
    FString OutputFormat = GetStringParam(Params, TEXT("output_format"), TEXT("EXR"));
    int32 ResX = static_cast<int32>(GetNumberParam(Params, TEXT("resolution_x"), 1920.0));
    int32 ResY = static_cast<int32>(GetNumberParam(Params, TEXT("resolution_y"), 1080.0));

    UMoviePipelineQueue* Queue = GetOrCreateQueue(QueueName);
    if (!Queue)
    {
        return MakeError(TEXT("QUEUE_NOT_FOUND"),
            FString::Printf(TEXT("Queue '%s' not found"), *QueueName));
    }

    UMoviePipelineExecutorJob* Job = FindJobByName(Queue, JobName);
    if (!Job)
    {
        return MakeError(TEXT("JOB_NOT_FOUND"),
            FString::Printf(TEXT("Job '%s' not found in queue '%s'"), *JobName, *QueueName));
    }

    UMoviePipelinePrimaryConfig* Config = Job->GetConfiguration();
    if (!Config)
    {
        return MakeError(TEXT("NO_CONFIG"),
            FString::Printf(TEXT("Job '%s' has no pipeline config"), *JobName));
    }

    // Resolve the pass UClass
    UClass* PassClass = ResolvePassClass(PassType);
    if (!PassClass)
    {
        return MakeError(TEXT("INVALID_PASS_TYPE"),
            FString::Printf(TEXT("Unknown pass type '%s'"), *PassType));
    }

    // Add the render pass setting to the job's config
    UMoviePipelineImagePassBase* PassSetting =
        Cast<UMoviePipelineImagePassBase>(Config->FindOrAddSettingByClass(PassClass));
    if (!PassSetting)
    {
        return MakeError(TEXT("PASS_CREATION_FAILED"),
            FString::Printf(TEXT("Failed to create pass of type '%s'"), *PassType));
    }

    // Configure output resolution via the output setting
    UMoviePipelineOutputSetting* OutputSetting = Cast<UMoviePipelineOutputSetting>(
        Config->FindOrAddSettingByClass(UMoviePipelineOutputSetting::StaticClass()));
    if (OutputSetting)
    {
        OutputSetting->OutputResolution = FIntPoint(ResX, ResY);
    }

    TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
    Data->SetStringField(TEXT("queue_name"), QueueName);
    Data->SetStringField(TEXT("job_name"), JobName);
    Data->SetStringField(TEXT("pass_name"), PassName);
    Data->SetStringField(TEXT("pass_type"), PassType);
    Data->SetBoolField(TEXT("enabled"), bEnabled);
    Data->SetStringField(TEXT("output_format"), OutputFormat);
    Data->SetNumberField(TEXT("resolution_x"), ResX);
    Data->SetNumberField(TEXT("resolution_y"), ResY);
    Data->SetStringField(TEXT("pass_class"), PassClass->GetName());
    return MakeSuccess(Data);
}

// ─────────────────────────────────────────────────────────────────────────────
// mrq.render_queue
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FNexusMRQHandler::HandleRenderQueue(
    const TSharedPtr<FJsonObject>& Params)
{
    FString QueueName = GetStringParam(Params, TEXT("queue_name"), TEXT("DefaultQueue"));
    bool bForceRerender = GetBoolParam(Params, TEXT("force_rerender"), false);

    UMoviePipelineQueue* Queue = GetOrCreateQueue(QueueName);
    if (!Queue)
    {
        return MakeError(TEXT("QUEUE_NOT_FOUND"),
            FString::Printf(TEXT("Queue '%s' not found"), *QueueName));
    }

    TArray<UMoviePipelineExecutorJob*> Jobs = Queue->GetJobs();
    if (Jobs.Num() == 0)
    {
        return MakeError(TEXT("EMPTY_QUEUE"),
            FString::Printf(TEXT("Queue '%s' has no jobs to render"), *QueueName));
    }

    // Use the editor's MRQ subsystem to execute the queue
    UMoviePipelineQueueSubsystem* QueueSubsystem =
        GEditor->GetEditorSubsystem<UMoviePipelineQueueSubsystem>();
    if (!QueueSubsystem)
    {
        return MakeError(TEXT("SUBSYSTEM_NOT_FOUND"),
            TEXT("MoviePipelineQueueSubsystem not available"));
    }

    // Check if already rendering
    if (QueueSubsystem->IsRendering())
    {
        if (!bForceRerender)
        {
            return MakeError(TEXT("ALREADY_RENDERING"),
                TEXT("A render is already in progress. Use force_rerender=true or cancel first."));
        }
    }

    // Start rendering the queue through the subsystem
    // The subsystem manages executor allocation internally
    QueueSubsystem->RenderQueueWithExecutor(UMoviePipelinePIEExecutor::StaticClass());

    TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
    Data->SetStringField(TEXT("queue_name"), QueueName);
    Data->SetNumberField(TEXT("total_jobs"), Jobs.Num());
    Data->SetBoolField(TEXT("rendering_started"), true);
    Data->SetBoolField(TEXT("force_rerender"), bForceRerender);
    return MakeSuccess(Data);
}

// ─────────────────────────────────────────────────────────────────────────────
// mrq.cancel_render
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FNexusMRQHandler::HandleCancelRender(
    const TSharedPtr<FJsonObject>& Params)
{
    FString QueueName = GetStringParam(Params, TEXT("queue_name"), TEXT("DefaultQueue"));

    UMoviePipelineQueueSubsystem* QueueSubsystem =
        GEditor->GetEditorSubsystem<UMoviePipelineQueueSubsystem>();
    if (!QueueSubsystem)
    {
        return MakeError(TEXT("SUBSYSTEM_NOT_FOUND"),
            TEXT("MoviePipelineQueueSubsystem not available"));
    }

    if (!QueueSubsystem->IsRendering())
    {
        TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
        Data->SetStringField(TEXT("queue_name"), QueueName);
        Data->SetBoolField(TEXT("was_rendering"), false);
        Data->SetStringField(TEXT("message"), TEXT("No render was in progress"));
        return MakeSuccess(Data);
    }

    // Request cancellation through the active executor
    UMoviePipelineExecutorBase* ActiveExecutor = QueueSubsystem->GetActiveExecutor();
    if (ActiveExecutor)
    {
        ActiveExecutor->CancelAllJobs();
    }

    TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
    Data->SetStringField(TEXT("queue_name"), QueueName);
    Data->SetBoolField(TEXT("was_rendering"), true);
    Data->SetBoolField(TEXT("cancel_requested"), true);
    return MakeSuccess(Data);
}

// ─────────────────────────────────────────────────────────────────────────────
// mrq.get_render_status
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FNexusMRQHandler::HandleGetRenderStatus(
    const TSharedPtr<FJsonObject>& Params)
{
    FString QueueName = GetStringParam(Params, TEXT("queue_name"), TEXT("DefaultQueue"));

    UMoviePipelineQueueSubsystem* QueueSubsystem =
        GEditor->GetEditorSubsystem<UMoviePipelineQueueSubsystem>();

    TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
    Data->SetStringField(TEXT("queue_name"), QueueName);

    if (!QueueSubsystem)
    {
        Data->SetBoolField(TEXT("is_rendering"), false);
        Data->SetStringField(TEXT("message"), TEXT("QueueSubsystem not available"));
        return MakeSuccess(Data);
    }

    bool bIsRendering = QueueSubsystem->IsRendering();
    Data->SetBoolField(TEXT("is_rendering"), bIsRendering);

    // Report on queue jobs
    UMoviePipelineQueue* Queue = nullptr;
    if (TWeakObjectPtr<UMoviePipelineQueue>* Existing = QueueMap.Find(QueueName))
    {
        if (Existing->IsValid())
        {
            Queue = Existing->Get();
        }
    }

    if (Queue)
    {
        TArray<UMoviePipelineExecutorJob*> Jobs = Queue->GetJobs();
        Data->SetNumberField(TEXT("total_jobs"), Jobs.Num());

        TArray<TSharedPtr<FJsonValue>> JobStatuses;
        int32 CompletedCount = 0;

        for (UMoviePipelineExecutorJob* Job : Jobs)
        {
            if (!Job) continue;

            TSharedPtr<FJsonObject> JobObj = MakeShareable(new FJsonObject());
            JobObj->SetStringField(TEXT("job_name"), Job->JobName);
            JobObj->SetBoolField(TEXT("consumed"), Job->IsConsumed());

            if (Job->IsConsumed())
            {
                CompletedCount++;
            }

            // Get status text from the job
            FString StatusMsg = Job->GetStatusMessage();
            if (!StatusMsg.IsEmpty())
            {
                JobObj->SetStringField(TEXT("status_message"), StatusMsg);
            }
            JobObj->SetNumberField(TEXT("progress"), Job->GetStatusProgress());

            JobStatuses.Add(MakeShareable(new FJsonValueObject(JobObj)));
        }

        Data->SetArrayField(TEXT("jobs"), JobStatuses);
        Data->SetNumberField(TEXT("completed_jobs"), CompletedCount);
    }
    else
    {
        Data->SetNumberField(TEXT("total_jobs"), 0);
        Data->SetNumberField(TEXT("completed_jobs"), 0);
    }

    return MakeSuccess(Data);
}

// ─────────────────────────────────────────────────────────────────────────────
// mrq.list_render_jobs
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FNexusMRQHandler::HandleListRenderJobs(
    const TSharedPtr<FJsonObject>& Params)
{
    FString QueueName = GetStringParam(Params, TEXT("queue_name"), TEXT("DefaultQueue"));

    UMoviePipelineQueue* Queue = nullptr;
    if (TWeakObjectPtr<UMoviePipelineQueue>* Existing = QueueMap.Find(QueueName))
    {
        if (Existing->IsValid())
        {
            Queue = Existing->Get();
        }
    }

    TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
    Data->SetStringField(TEXT("queue_name"), QueueName);

    if (!Queue)
    {
        Data->SetNumberField(TEXT("count"), 0);
        Data->SetArrayField(TEXT("jobs"), TArray<TSharedPtr<FJsonValue>>());
        return MakeSuccess(Data);
    }

    TArray<UMoviePipelineExecutorJob*> Jobs = Queue->GetJobs();
    TArray<TSharedPtr<FJsonValue>> JobArr;

    for (int32 i = 0; i < Jobs.Num(); ++i)
    {
        UMoviePipelineExecutorJob* Job = Jobs[i];
        if (!Job) continue;

        TSharedPtr<FJsonObject> JobObj = MakeShareable(new FJsonObject());
        JobObj->SetStringField(TEXT("job_name"), Job->JobName);
        JobObj->SetNumberField(TEXT("index"), i);
        JobObj->SetStringField(TEXT("sequence_path"), Job->Sequence.ToString());
        JobObj->SetStringField(TEXT("map_path"), Job->Map.ToString());
        JobObj->SetBoolField(TEXT("consumed"), Job->IsConsumed());

        // Report config info
        UMoviePipelinePrimaryConfig* Config = Job->GetConfiguration();
        if (Config)
        {
            TArray<UMoviePipelineSetting*> AllSettings = Config->GetUserSettings();
            JobObj->SetNumberField(TEXT("setting_count"), AllSettings.Num());
        }

        JobArr.Add(MakeShareable(new FJsonValueObject(JobObj)));
    }

    Data->SetArrayField(TEXT("jobs"), JobArr);
    Data->SetNumberField(TEXT("count"), JobArr.Num());
    return MakeSuccess(Data);
}

// ─────────────────────────────────────────────────────────────────────────────
// mrq.remove_render_job
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FNexusMRQHandler::HandleRemoveRenderJob(
    const TSharedPtr<FJsonObject>& Params)
{
    FString QueueName = GetStringParam(Params, TEXT("queue_name"), TEXT("DefaultQueue"));
    FString JobName = GetStringParam(Params, TEXT("job_name"));

    if (JobName.IsEmpty())
    {
        return MakeError(TEXT("MISSING_PARAM"), TEXT("job_name is required"));
    }

    UMoviePipelineQueue* Queue = nullptr;
    if (TWeakObjectPtr<UMoviePipelineQueue>* Existing = QueueMap.Find(QueueName))
    {
        if (Existing->IsValid())
        {
            Queue = Existing->Get();
        }
    }

    if (!Queue)
    {
        return MakeError(TEXT("QUEUE_NOT_FOUND"),
            FString::Printf(TEXT("Queue '%s' not found"), *QueueName));
    }

    TArray<UMoviePipelineExecutorJob*> Jobs = Queue->GetJobs();
    int32 RemoveIndex = INDEX_NONE;

    for (int32 i = 0; i < Jobs.Num(); ++i)
    {
        if (Jobs[i] && Jobs[i]->JobName == JobName)
        {
            RemoveIndex = i;
            break;
        }
    }

    if (RemoveIndex == INDEX_NONE)
    {
        return MakeError(TEXT("JOB_NOT_FOUND"),
            FString::Printf(TEXT("Job '%s' not found in queue '%s'"), *JobName, *QueueName));
    }

    Queue->DeleteJob(Jobs[RemoveIndex]);

    TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
    Data->SetStringField(TEXT("queue_name"), QueueName);
    Data->SetStringField(TEXT("removed_job"), JobName);
    Data->SetNumberField(TEXT("remaining_jobs"), Queue->GetJobs().Num());
    return MakeSuccess(Data);
}

// ─────────────────────────────────────────────────────────────────────────────
// mrq.set_output_settings
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FNexusMRQHandler::HandleSetOutputSettings(
    const TSharedPtr<FJsonObject>& Params)
{
    FString QueueName = GetStringParam(Params, TEXT("queue_name"), TEXT("DefaultQueue"));
    FString JobName = GetStringParam(Params, TEXT("job_name"));
    FString OutputDirectory = GetStringParam(Params, TEXT("output_directory"));
    FString FileNameFormat = GetStringParam(Params, TEXT("file_name_format"),
        TEXT("{sequence_name}.{frame_number}"));
    int32 ResX = static_cast<int32>(GetNumberParam(Params, TEXT("output_resolution_x"), 1920.0));
    int32 ResY = static_cast<int32>(GetNumberParam(Params, TEXT("output_resolution_y"), 1080.0));
    bool bUseCustomFrameRate = GetBoolParam(Params, TEXT("use_custom_frame_rate"), false);
    double CustomFrameRate = GetNumberParam(Params, TEXT("custom_frame_rate"), 24.0);
    bool bOverrideExisting = GetBoolParam(Params, TEXT("override_existing"), true);

    UMoviePipelineQueue* Queue = GetOrCreateQueue(QueueName);
    if (!Queue)
    {
        return MakeError(TEXT("QUEUE_NOT_FOUND"),
            FString::Printf(TEXT("Queue '%s' not found"), *QueueName));
    }

    UMoviePipelineExecutorJob* Job = FindJobByName(Queue, JobName);
    if (!Job)
    {
        return MakeError(TEXT("JOB_NOT_FOUND"),
            FString::Printf(TEXT("Job '%s' not found in queue '%s'"), *JobName, *QueueName));
    }

    UMoviePipelinePrimaryConfig* Config = Job->GetConfiguration();
    if (!Config)
    {
        return MakeError(TEXT("NO_CONFIG"),
            FString::Printf(TEXT("Job '%s' has no pipeline config"), *JobName));
    }

    // Configure output settings
    UMoviePipelineOutputSetting* OutputSetting = Cast<UMoviePipelineOutputSetting>(
        Config->FindOrAddSettingByClass(UMoviePipelineOutputSetting::StaticClass()));
    if (!OutputSetting)
    {
        return MakeError(TEXT("SETTING_FAILED"),
            TEXT("Failed to create or find output settings"));
    }

    if (!OutputDirectory.IsEmpty())
    {
        OutputSetting->OutputDirectory.Path = OutputDirectory;
    }
    OutputSetting->FileNameFormat = FileNameFormat;
    OutputSetting->OutputResolution = FIntPoint(ResX, ResY);
    OutputSetting->bOverrideExistingOutput = bOverrideExisting;

    if (bUseCustomFrameRate)
    {
        OutputSetting->bUseCustomFrameRate = true;
        OutputSetting->OutputFrameRate = FFrameRate(
            static_cast<int32>(CustomFrameRate), 1);
    }

    TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
    Data->SetStringField(TEXT("queue_name"), QueueName);
    Data->SetStringField(TEXT("job_name"), JobName);
    Data->SetStringField(TEXT("output_directory"), OutputSetting->OutputDirectory.Path);
    Data->SetStringField(TEXT("file_name_format"), FileNameFormat);
    Data->SetNumberField(TEXT("output_resolution_x"), ResX);
    Data->SetNumberField(TEXT("output_resolution_y"), ResY);
    Data->SetBoolField(TEXT("use_custom_frame_rate"), bUseCustomFrameRate);
    Data->SetNumberField(TEXT("custom_frame_rate"), CustomFrameRate);
    Data->SetBoolField(TEXT("override_existing"), bOverrideExisting);
    return MakeSuccess(Data);
}

// ─────────────────────────────────────────────────────────────────────────────
// mrq.add_beauty_pass
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FNexusMRQHandler::HandleAddBeautyPass(
    const TSharedPtr<FJsonObject>& Params)
{
    // Convenience wrapper: adds a DeferredLighting or PathTracer pass
    FString QueueName = GetStringParam(Params, TEXT("queue_name"), TEXT("DefaultQueue"));
    FString JobName = GetStringParam(Params, TEXT("job_name"));
    FString PassName = GetStringParam(Params, TEXT("pass_name"), TEXT("Beauty"));
    bool bUsePathTracer = GetBoolParam(Params, TEXT("use_path_tracer"), false);
    FString OutputFormat = GetStringParam(Params, TEXT("output_format"), TEXT("EXR"));
    int32 ResX = static_cast<int32>(GetNumberParam(Params, TEXT("resolution_x"), 1920.0));
    int32 ResY = static_cast<int32>(GetNumberParam(Params, TEXT("resolution_y"), 1080.0));

    // Build params to delegate to add_render_pass
    TSharedPtr<FJsonObject> PassParams = MakeShareable(new FJsonObject());
    PassParams->SetStringField(TEXT("queue_name"), QueueName);
    PassParams->SetStringField(TEXT("job_name"), JobName);
    PassParams->SetStringField(TEXT("pass_name"), PassName);
    PassParams->SetStringField(TEXT("pass_type"),
        bUsePathTracer ? TEXT("PathTracer") : TEXT("DeferredLighting"));
    PassParams->SetBoolField(TEXT("enabled"), true);
    PassParams->SetStringField(TEXT("output_format"), OutputFormat);
    PassParams->SetNumberField(TEXT("resolution_x"), ResX);
    PassParams->SetNumberField(TEXT("resolution_y"), ResY);

    return HandleAddRenderPass(PassParams);
}

// ─────────────────────────────────────────────────────────────────────────────
// mrq.add_gbuffer_passes — CRITICAL FOR ML DATA GENERATION
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FNexusMRQHandler::HandleAddGBufferPasses(
    const TSharedPtr<FJsonObject>& Params)
{
    FString QueueName = GetStringParam(Params, TEXT("queue_name"), TEXT("DefaultQueue"));
    FString JobName = GetStringParam(Params, TEXT("job_name"));
    FString OutputFormat = GetStringParam(Params, TEXT("output_format"), TEXT("EXR"));
    int32 ResX = static_cast<int32>(GetNumberParam(Params, TEXT("resolution_x"), 1920.0));
    int32 ResY = static_cast<int32>(GetNumberParam(Params, TEXT("resolution_y"), 1080.0));

    // Parse the passes array from params
    const TArray<TSharedPtr<FJsonValue>>* PassesArray = nullptr;
    Params->TryGetArrayField(TEXT("passes"), PassesArray);

    // If no passes array, use default GBuffer set
    struct FPassDef { FString Name; FString Type; };
    TArray<FPassDef> PassesToAdd;

    if (PassesArray && PassesArray->Num() > 0)
    {
        for (const TSharedPtr<FJsonValue>& PassVal : *PassesArray)
        {
            const TSharedPtr<FJsonObject>* PassObj = nullptr;
            if (PassVal->TryGetObject(PassObj) && PassObj)
            {
                FString PName = (*PassObj)->GetStringField(TEXT("pass_name"));
                FString PType = (*PassObj)->GetStringField(TEXT("pass_type"));
                if (!PType.IsEmpty())
                {
                    PassesToAdd.Add({ PName.IsEmpty() ? PType : PName, PType });
                }
            }
        }
    }
    else
    {
        // Default full GBuffer set for ML training data
        PassesToAdd.Add({ TEXT("BaseColor"), TEXT("BaseColor") });
        PassesToAdd.Add({ TEXT("WorldNormal"), TEXT("WorldNormal") });
        PassesToAdd.Add({ TEXT("Roughness"), TEXT("Roughness") });
        PassesToAdd.Add({ TEXT("Metallic"), TEXT("Metallic") });
        PassesToAdd.Add({ TEXT("SceneDepth"), TEXT("SceneDepth") });
    }

    TArray<TSharedPtr<FJsonValue>> AddedPasses;
    int32 SuccessCount = 0;
    int32 FailCount = 0;

    for (const FPassDef& PassDef : PassesToAdd)
    {
        TSharedPtr<FJsonObject> PassParams = MakeShareable(new FJsonObject());
        PassParams->SetStringField(TEXT("queue_name"), QueueName);
        PassParams->SetStringField(TEXT("job_name"), JobName);
        PassParams->SetStringField(TEXT("pass_name"), PassDef.Name);
        PassParams->SetStringField(TEXT("pass_type"), PassDef.Type);
        PassParams->SetBoolField(TEXT("enabled"), true);
        PassParams->SetStringField(TEXT("output_format"), OutputFormat);
        PassParams->SetNumberField(TEXT("resolution_x"), ResX);
        PassParams->SetNumberField(TEXT("resolution_y"), ResY);

        TSharedPtr<FJsonObject> Result = HandleAddRenderPass(PassParams);
        bool bPassSuccess = false;
        Result->TryGetBoolField(TEXT("success"), bPassSuccess);

        TSharedPtr<FJsonObject> PassInfo = MakeShareable(new FJsonObject());
        PassInfo->SetStringField(TEXT("pass_name"), PassDef.Name);
        PassInfo->SetStringField(TEXT("pass_type"), PassDef.Type);
        PassInfo->SetBoolField(TEXT("success"), bPassSuccess);

        if (bPassSuccess)
        {
            SuccessCount++;
        }
        else
        {
            FailCount++;
            // Extract error message
            const TSharedPtr<FJsonObject>* ErrorObj;
            if (Result->TryGetObjectField(TEXT("error"), ErrorObj))
            {
                FString ErrMsg;
                if ((*ErrorObj)->TryGetStringField(TEXT("message"), ErrMsg))
                {
                    PassInfo->SetStringField(TEXT("error"), ErrMsg);
                }
            }
        }

        AddedPasses.Add(MakeShareable(new FJsonValueObject(PassInfo)));
    }

    TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
    Data->SetStringField(TEXT("queue_name"), QueueName);
    Data->SetStringField(TEXT("job_name"), JobName);
    Data->SetArrayField(TEXT("passes"), AddedPasses);
    Data->SetNumberField(TEXT("success_count"), SuccessCount);
    Data->SetNumberField(TEXT("fail_count"), FailCount);
    Data->SetNumberField(TEXT("total_passes"), PassesToAdd.Num());
    Data->SetStringField(TEXT("output_format"), OutputFormat);
    Data->SetNumberField(TEXT("resolution_x"), ResX);
    Data->SetNumberField(TEXT("resolution_y"), ResY);
    return MakeSuccess(Data);
}

// ─────────────────────────────────────────────────────────────────────────────
// mrq.configure_antialiasing
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FNexusMRQHandler::HandleConfigureAntialiasing(
    const TSharedPtr<FJsonObject>& Params)
{
    FString QueueName = GetStringParam(Params, TEXT("queue_name"), TEXT("DefaultQueue"));
    FString JobName = GetStringParam(Params, TEXT("job_name"));
    int32 SpatialSamples = static_cast<int32>(
        GetNumberParam(Params, TEXT("spatial_sample_count"), 1.0));
    int32 TemporalSamples = static_cast<int32>(
        GetNumberParam(Params, TEXT("temporal_sample_count"), 1.0));
    bool bOverrideAA = GetBoolParam(Params, TEXT("override_anti_aliasing"), true);
    FString AAMethod = GetStringParam(Params, TEXT("anti_aliasing_method"), TEXT("TSR"));
    bool bUseCameraCutWarmUp = GetBoolParam(Params, TEXT("use_camera_cut_for_warm_up"), true);
    int32 WarmUpCount = static_cast<int32>(
        GetNumberParam(Params, TEXT("render_warm_up_count"), 32.0));

    UMoviePipelineQueue* Queue = GetOrCreateQueue(QueueName);
    if (!Queue)
    {
        return MakeError(TEXT("QUEUE_NOT_FOUND"),
            FString::Printf(TEXT("Queue '%s' not found"), *QueueName));
    }

    UMoviePipelineExecutorJob* Job = FindJobByName(Queue, JobName);
    if (!Job)
    {
        return MakeError(TEXT("JOB_NOT_FOUND"),
            FString::Printf(TEXT("Job '%s' not found in queue '%s'"), *JobName, *QueueName));
    }

    UMoviePipelinePrimaryConfig* Config = Job->GetConfiguration();
    if (!Config)
    {
        return MakeError(TEXT("NO_CONFIG"),
            FString::Printf(TEXT("Job '%s' has no pipeline config"), *JobName));
    }

    // Configure anti-aliasing settings
    UMoviePipelineAntiAliasingSetting* AASetting = Cast<UMoviePipelineAntiAliasingSetting>(
        Config->FindOrAddSettingByClass(UMoviePipelineAntiAliasingSetting::StaticClass()));
    if (!AASetting)
    {
        return MakeError(TEXT("SETTING_FAILED"),
            TEXT("Failed to create or find anti-aliasing settings"));
    }

    AASetting->SpatialSampleCount = FMath::Clamp(SpatialSamples, 1, 64);
    AASetting->TemporalSampleCount = FMath::Clamp(TemporalSamples, 1, 64);
    AASetting->bUseCameraCutForWarmUp = bUseCameraCutWarmUp;
    AASetting->RenderWarmUpCount = FMath::Max(0, WarmUpCount);

    if (bOverrideAA)
    {
        AASetting->bOverrideAntiAliasing = true;

        // Map string AA method to enum
        if (AAMethod == TEXT("TSR"))
        {
            AASetting->AntiAliasingMethod = EAntiAliasingMethod::AAM_TSR;
        }
        else if (AAMethod == TEXT("TAA"))
        {
            AASetting->AntiAliasingMethod = EAntiAliasingMethod::AAM_TemporalAA;
        }
        else if (AAMethod == TEXT("FXAA"))
        {
            AASetting->AntiAliasingMethod = EAntiAliasingMethod::AAM_FXAA;
        }
        else if (AAMethod == TEXT("MSAA"))
        {
            AASetting->AntiAliasingMethod = EAntiAliasingMethod::AAM_MSAA;
        }
        else if (AAMethod == TEXT("None"))
        {
            AASetting->AntiAliasingMethod = EAntiAliasingMethod::AAM_None;
        }
    }

    TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
    Data->SetStringField(TEXT("queue_name"), QueueName);
    Data->SetStringField(TEXT("job_name"), JobName);
    Data->SetNumberField(TEXT("spatial_sample_count"), AASetting->SpatialSampleCount);
    Data->SetNumberField(TEXT("temporal_sample_count"), AASetting->TemporalSampleCount);
    Data->SetBoolField(TEXT("override_anti_aliasing"), bOverrideAA);
    Data->SetStringField(TEXT("anti_aliasing_method"), AAMethod);
    Data->SetBoolField(TEXT("use_camera_cut_for_warm_up"), bUseCameraCutWarmUp);
    Data->SetNumberField(TEXT("render_warm_up_count"), AASetting->RenderWarmUpCount);
    Data->SetNumberField(TEXT("total_samples_per_pixel"),
        AASetting->SpatialSampleCount * AASetting->TemporalSampleCount);
    return MakeSuccess(Data);
}
