// Copyright Nexus Team. All Rights Reserved.

#include "Handlers/NexusEditorHandler.h"
#include "Editor.h"
#include "LevelEditorViewport.h"
#include "EditorLevelLibrary.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Misc/Paths.h"
#include "GenericPlatform/GenericPlatformMisc.h"
#include "UnrealClient.h"
#include "HighResScreenshot.h"
#include "Misc/FileHelper.h"
#include "HAL/PlatformFileManager.h"
#include "GameFramework/GameModeBase.h"
#include "Engine/LevelStreaming.h"
#include "Selection.h"

TSharedPtr<FJsonObject> FNexusEditorHandler::Handle(
    const FString& CommandType,
    const TSharedPtr<FJsonObject>& Params)
{
    FString SubCommand = CommandType.RightChop(GetNamespace().Len() + 1);

    if (SubCommand == TEXT("get_viewport_info")) return HandleGetViewportInfo(Params);
    if (SubCommand == TEXT("set_viewport_camera")) return HandleSetViewportCamera(Params);
    if (SubCommand == TEXT("take_screenshot")) return HandleTakeScreenshot(Params);
    if (SubCommand == TEXT("start_pie")) return HandleStartPIE(Params);
    if (SubCommand == TEXT("stop_pie")) return HandleStopPIE(Params);
    if (SubCommand == TEXT("is_pie_running")) return HandleIsPIERunning(Params);
    if (SubCommand == TEXT("get_selection")) return HandleGetSelection(Params);
    if (SubCommand == TEXT("set_selection")) return HandleSetSelection(Params);
    if (SubCommand == TEXT("clear_selection")) return HandleClearSelection(Params);
    if (SubCommand == TEXT("focus_actor")) return HandleFocusActor(Params);
    if (SubCommand == TEXT("get_project_info")) return HandleGetProjectInfo(Params);
    if (SubCommand == TEXT("get_world_info")) return HandleGetWorldInfo(Params);
    if (SubCommand == TEXT("execute_console_command")) return HandleExecuteConsoleCommand(Params);
    if (SubCommand == TEXT("undo")) return HandleUndo(Params);
    if (SubCommand == TEXT("redo")) return HandleRedo(Params);

    return MakeError(TEXT("NOT_IMPLEMENTED"),
        FString::Printf(TEXT("Command '%s' not yet implemented"), *CommandType));
}

// ---------------------------------------------------------------------------
// Helper: Build ViewportInfo JSON from the first level viewport client
// ---------------------------------------------------------------------------

static TSharedPtr<FJsonObject> BuildViewportInfoData(FLevelEditorViewportClient* ViewportClient)
{
    TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
    if (!ViewportClient)
    {
        return Data;
    }

    FVector Location = ViewportClient->GetViewLocation();
    FRotator Rotation = ViewportClient->GetViewRotation();

    TSharedPtr<FJsonObject> LocObj = MakeShareable(new FJsonObject());
    LocObj->SetNumberField(TEXT("x"), Location.X);
    LocObj->SetNumberField(TEXT("y"), Location.Y);
    LocObj->SetNumberField(TEXT("z"), Location.Z);
    Data->SetObjectField(TEXT("camera_location"), LocObj);

    TSharedPtr<FJsonObject> RotObj = MakeShareable(new FJsonObject());
    RotObj->SetNumberField(TEXT("pitch"), Rotation.Pitch);
    RotObj->SetNumberField(TEXT("yaw"), Rotation.Yaw);
    RotObj->SetNumberField(TEXT("roll"), Rotation.Roll);
    Data->SetObjectField(TEXT("camera_rotation"), RotObj);

    Data->SetNumberField(TEXT("fov"), ViewportClient->ViewFOV);

    FIntPoint ViewportSize = ViewportClient->Viewport
        ? ViewportClient->Viewport->GetSizeXY()
        : FIntPoint(1920, 1080);
    Data->SetNumberField(TEXT("viewport_size_x"), ViewportSize.X);
    Data->SetNumberField(TEXT("viewport_size_y"), ViewportSize.Y);

    FString ViewMode;
    switch (ViewportClient->GetViewMode())
    {
    case VMI_Lit:               ViewMode = TEXT("Lit"); break;
    case VMI_Unlit:             ViewMode = TEXT("Unlit"); break;
    case VMI_Wireframe:         ViewMode = TEXT("Wireframe"); break;
    case VMI_BrushWireframe:    ViewMode = TEXT("BrushWireframe"); break;
    case VMI_LightingOnly:      ViewMode = TEXT("LightingOnly"); break;
    default:                    ViewMode = TEXT("Other"); break;
    }
    Data->SetStringField(TEXT("view_mode"), ViewMode);

    return Data;
}

// ---------------------------------------------------------------------------
// Viewport commands
// ---------------------------------------------------------------------------

TSharedPtr<FJsonObject> FNexusEditorHandler::HandleGetViewportInfo(
    const TSharedPtr<FJsonObject>& Params)
{
    if (!GEditor || GEditor->GetLevelViewportClients().Num() == 0)
    {
        return MakeError(TEXT("NO_VIEWPORT"), TEXT("No active level editor viewport"));
    }

    FLevelEditorViewportClient* ViewportClient = GEditor->GetLevelViewportClients()[0];
    return MakeSuccess(BuildViewportInfoData(ViewportClient));
}

TSharedPtr<FJsonObject> FNexusEditorHandler::HandleSetViewportCamera(
    const TSharedPtr<FJsonObject>& Params)
{
    if (!GEditor || GEditor->GetLevelViewportClients().Num() == 0)
    {
        return MakeError(TEXT("NO_VIEWPORT"), TEXT("No active level editor viewport"));
    }

    FLevelEditorViewportClient* Client = GEditor->GetLevelViewportClients()[0];

    // Extract location from nested object: { "location": { "x": ..., "y": ..., "z": ... } }
    FVector Location = GetVectorParam(Params, TEXT("location"), Client->GetViewLocation());

    // Extract rotation from nested object: { "rotation": { "pitch": ..., "yaw": ..., "roll": ... } }
    FRotator Rotation = Client->GetViewRotation();
    const TSharedPtr<FJsonObject>* RotObj;
    if (Params.IsValid() && Params->TryGetObjectField(TEXT("rotation"), RotObj))
    {
        Rotation.Pitch = (*RotObj)->GetNumberField(TEXT("pitch"));
        Rotation.Yaw = (*RotObj)->GetNumberField(TEXT("yaw"));
        Rotation.Roll = (*RotObj)->GetNumberField(TEXT("roll"));
    }

    Client->SetViewLocation(Location);
    Client->SetViewRotation(Rotation);
    Client->Invalidate();

    return MakeSuccess(BuildViewportInfoData(Client));
}

TSharedPtr<FJsonObject> FNexusEditorHandler::HandleTakeScreenshot(
    const TSharedPtr<FJsonObject>& Params)
{
    FString Filename = GetStringParam(Params, TEXT("filename"));
    int32 Width = static_cast<int32>(GetNumberParam(Params, TEXT("width"), 1920));
    int32 Height = static_cast<int32>(GetNumberParam(Params, TEXT("height"), 1080));
    bool bShowUI = GetBoolParam(Params, TEXT("show_ui"), false);

    // Generate a default filename if none provided
    if (Filename.IsEmpty())
    {
        Filename = FString::Printf(TEXT("NexusScreenshot_%s"),
            *FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S")));
    }

    // Ensure it has a .png extension
    if (!Filename.EndsWith(TEXT(".png")))
    {
        Filename += TEXT(".png");
    }

    // Build the full path in project's Saved/Screenshots
    FString ScreenshotDir = FPaths::ProjectSavedDir() / TEXT("Screenshots");
    FString FullPath = ScreenshotDir / Filename;

    // Request screenshot
    FScreenshotRequest::RequestScreenshot(FullPath, bShowUI, false);

    TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
    Data->SetStringField(TEXT("file_path"), FullPath);
    Data->SetNumberField(TEXT("width"), Width);
    Data->SetNumberField(TEXT("height"), Height);
    Data->SetNumberField(TEXT("file_size_bytes"), 0); // Size not available until write completes
    return MakeSuccess(Data);
}

TSharedPtr<FJsonObject> FNexusEditorHandler::HandleFocusActor(
    const TSharedPtr<FJsonObject>& Params)
{
    FString ActorPath = GetStringParam(Params, TEXT("actor_path"));
    if (ActorPath.IsEmpty())
    {
        return MakeError(TEXT("MISSING_PARAM"), TEXT("'actor_path' parameter required"));
    }

    AActor* Actor = FindObject<AActor>(nullptr, *ActorPath);
    if (!Actor)
    {
        // Try by label
        UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
        if (World)
        {
            for (TActorIterator<AActor> It(World); It; ++It)
            {
                if (It->GetActorLabel() == ActorPath)
                {
                    Actor = *It;
                    break;
                }
            }
        }
    }

    if (!Actor)
    {
        return MakeError(TEXT("ACTOR_NOT_FOUND"),
            FString::Printf(TEXT("Actor '%s' not found"), *ActorPath));
    }

    GEditor->MoveViewportCamerasToActor(*Actor, false);

    // Return the updated viewport info after focusing
    if (GEditor->GetLevelViewportClients().Num() > 0)
    {
        return MakeSuccess(BuildViewportInfoData(GEditor->GetLevelViewportClients()[0]));
    }

    TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
    Data->SetStringField(TEXT("actor_path"), Actor->GetPathName());
    Data->SetBoolField(TEXT("focused"), true);
    return MakeSuccess(Data);
}

// ---------------------------------------------------------------------------
// PIE commands
// ---------------------------------------------------------------------------

TSharedPtr<FJsonObject> FNexusEditorHandler::HandleStartPIE(
    const TSharedPtr<FJsonObject>& Params)
{
    if (!GEditor)
    {
        return MakeError(TEXT("NO_EDITOR"), TEXT("GEditor not available"));
    }

    FRequestPlaySessionParams PIEParams;
    PIEParams.WorldType = EPlaySessionWorldType::PlayInEditor;
    GEditor->RequestPlaySession(PIEParams);

    TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
    Data->SetBoolField(TEXT("is_running"), true);
    Data->SetStringField(TEXT("session_id"), TEXT(""));
    Data->SetNumberField(TEXT("elapsed_seconds"), 0.0);
    return MakeSuccess(Data);
}

TSharedPtr<FJsonObject> FNexusEditorHandler::HandleStopPIE(
    const TSharedPtr<FJsonObject>& Params)
{
    if (!GEditor)
    {
        return MakeError(TEXT("NO_EDITOR"), TEXT("GEditor not available"));
    }

    GEditor->RequestEndPlayMap();

    TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
    Data->SetBoolField(TEXT("is_running"), false);
    Data->SetStringField(TEXT("session_id"), TEXT(""));
    Data->SetNumberField(TEXT("elapsed_seconds"), 0.0);
    return MakeSuccess(Data);
}

TSharedPtr<FJsonObject> FNexusEditorHandler::HandleIsPIERunning(
    const TSharedPtr<FJsonObject>& Params)
{
    if (!GEditor)
    {
        return MakeError(TEXT("NO_EDITOR"), TEXT("GEditor not available"));
    }

    bool bRunning = GEditor->IsPlaySessionInProgress();

    TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
    Data->SetBoolField(TEXT("is_running"), bRunning);
    Data->SetStringField(TEXT("session_id"), bRunning ? TEXT("default") : TEXT(""));
    Data->SetNumberField(TEXT("elapsed_seconds"), 0.0);
    return MakeSuccess(Data);
}

// ---------------------------------------------------------------------------
// Selection commands
// ---------------------------------------------------------------------------

TSharedPtr<FJsonObject> FNexusEditorHandler::HandleGetSelection(
    const TSharedPtr<FJsonObject>& Params)
{
    if (!GEditor)
    {
        return MakeError(TEXT("NO_EDITOR"), TEXT("GEditor not available"));
    }

    USelection* Selection = GEditor->GetSelectedActors();
    TArray<AActor*> SelectedActors;
    Selection->GetSelectedObjects<AActor>(SelectedActors);

    TArray<TSharedPtr<FJsonValue>> PathsArray;
    for (AActor* Actor : SelectedActors)
    {
        PathsArray.Add(MakeShareable(new FJsonValueString(Actor->GetPathName())));
    }

    TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
    Data->SetArrayField(TEXT("selected_paths"), PathsArray);
    Data->SetNumberField(TEXT("count"), SelectedActors.Num());
    return MakeSuccess(Data);
}

TSharedPtr<FJsonObject> FNexusEditorHandler::HandleSetSelection(
    const TSharedPtr<FJsonObject>& Params)
{
    if (!GEditor)
    {
        return MakeError(TEXT("NO_EDITOR"), TEXT("GEditor not available"));
    }

    const TArray<TSharedPtr<FJsonValue>>* ActorPathsArray;
    if (!Params.IsValid() || !Params->TryGetArrayField(TEXT("actor_paths"), ActorPathsArray))
    {
        return MakeError(TEXT("MISSING_PARAM"), TEXT("'actor_paths' array parameter required"));
    }

    // Clear current selection
    GEditor->SelectNone(false, true);

    int32 SelectedCount = 0;
    UWorld* World = GEditor->GetEditorWorldContext().World();

    for (const TSharedPtr<FJsonValue>& PathValue : *ActorPathsArray)
    {
        FString Path = PathValue->AsString();
        AActor* Actor = FindObject<AActor>(nullptr, *Path);

        // Fallback: try finding by label
        if (!Actor && World)
        {
            for (TActorIterator<AActor> It(World); It; ++It)
            {
                if (It->GetActorLabel() == Path)
                {
                    Actor = *It;
                    break;
                }
            }
        }

        if (Actor)
        {
            GEditor->SelectActor(Actor, true, true);
            SelectedCount++;
        }
    }

    TArray<TSharedPtr<FJsonValue>> SelectedPaths;
    USelection* Selection = GEditor->GetSelectedActors();
    TArray<AActor*> SelectedActors;
    Selection->GetSelectedObjects<AActor>(SelectedActors);
    for (AActor* Actor : SelectedActors)
    {
        SelectedPaths.Add(MakeShareable(new FJsonValueString(Actor->GetPathName())));
    }

    TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
    Data->SetArrayField(TEXT("selected_paths"), SelectedPaths);
    Data->SetNumberField(TEXT("count"), SelectedCount);
    return MakeSuccess(Data);
}

TSharedPtr<FJsonObject> FNexusEditorHandler::HandleClearSelection(
    const TSharedPtr<FJsonObject>& Params)
{
    if (!GEditor)
    {
        return MakeError(TEXT("NO_EDITOR"), TEXT("GEditor not available"));
    }

    GEditor->SelectNone(true, true);

    TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
    Data->SetArrayField(TEXT("selected_paths"), TArray<TSharedPtr<FJsonValue>>());
    Data->SetNumberField(TEXT("count"), 0);
    return MakeSuccess(Data);
}

// ---------------------------------------------------------------------------
// Existing commands refactored into Handle* methods
// ---------------------------------------------------------------------------

TSharedPtr<FJsonObject> FNexusEditorHandler::HandleGetProjectInfo(
    const TSharedPtr<FJsonObject>& Params)
{
    TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
    Data->SetStringField(TEXT("project_name"), FApp::GetProjectName());
    Data->SetStringField(TEXT("engine_version"), FEngineVersion::Current().ToString());
    Data->SetStringField(TEXT("project_dir"), FPaths::ProjectDir());
    Data->SetStringField(TEXT("content_dir"), FPaths::ProjectContentDir());
    return MakeSuccess(Data);
}

TSharedPtr<FJsonObject> FNexusEditorHandler::HandleGetWorldInfo(
    const TSharedPtr<FJsonObject>& Params)
{
    UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
    TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
    if (World)
    {
        Data->SetStringField(TEXT("map_name"), World->GetMapName());
        Data->SetStringField(TEXT("map_path"), World->GetOutermost()->GetName());

        bool bIsPIE = GEditor->IsPlaySessionInProgress();
        Data->SetStringField(TEXT("world_type"), bIsPIE ? TEXT("PIE") : TEXT("Editor"));
        Data->SetBoolField(TEXT("is_pie_world"), bIsPIE);

        int32 ActorCount = 0;
        for (TActorIterator<AActor> It(World); It; ++It) { ActorCount++; }
        Data->SetNumberField(TEXT("actor_count"), ActorCount);

        // Game mode
        AGameModeBase* GameMode = World->GetAuthGameMode();
        Data->SetStringField(TEXT("game_mode"),
            GameMode ? GameMode->GetClass()->GetName() : TEXT(""));

        // Streaming levels
        Data->SetNumberField(TEXT("streaming_levels_count"),
            World->GetStreamingLevels().Num());
    }
    return MakeSuccess(Data);
}

TSharedPtr<FJsonObject> FNexusEditorHandler::HandleExecuteConsoleCommand(
    const TSharedPtr<FJsonObject>& Params)
{
    FString Command = GetStringParam(Params, TEXT("command"));
    if (Command.IsEmpty())
    {
        return MakeError(TEXT("MISSING_PARAM"), TEXT("'command' parameter required"));
    }
    GEditor->Exec(GEditor->GetEditorWorldContext().World(), *Command);
    TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
    Data->SetStringField(TEXT("output"), Command);
    Data->SetBoolField(TEXT("success"), true);
    return MakeSuccess(Data);
}

TSharedPtr<FJsonObject> FNexusEditorHandler::HandleUndo(
    const TSharedPtr<FJsonObject>& Params)
{
    GEditor->UndoTransaction();
    TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
    Data->SetStringField(TEXT("output"), TEXT("Undo executed"));
    Data->SetBoolField(TEXT("success"), true);
    return MakeSuccess(Data);
}

TSharedPtr<FJsonObject> FNexusEditorHandler::HandleRedo(
    const TSharedPtr<FJsonObject>& Params)
{
    GEditor->RedoTransaction();
    TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
    Data->SetStringField(TEXT("output"), TEXT("Redo executed"));
    Data->SetBoolField(TEXT("success"), true);
    return MakeSuccess(Data);
}
