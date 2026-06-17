// Copyright Nexus Team. All Rights Reserved.

#pragma once

#include "NexusCommandHandler.h"

class FNexusEditorHandler : public INexusCommandHandler
{
public:
    virtual FString GetNamespace() const override { return TEXT("editor"); }

    virtual TArray<FString> GetSupportedCommands() const override
    {
        return {
            TEXT("get_viewport_info"), TEXT("set_viewport_camera"),
            TEXT("take_screenshot"), TEXT("start_pie"), TEXT("stop_pie"),
            TEXT("is_pie_running"), TEXT("execute_console_command"),
            TEXT("get_selection"), TEXT("set_selection"), TEXT("clear_selection"),
            TEXT("undo"), TEXT("redo"), TEXT("get_world_info"),
            TEXT("focus_actor"), TEXT("get_project_info")
        };
    }

    virtual TSharedPtr<FJsonObject> Handle(
        const FString& CommandType,
        const TSharedPtr<FJsonObject>& Params) override;

private:
    // Viewport
    TSharedPtr<FJsonObject> HandleGetViewportInfo(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleSetViewportCamera(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleTakeScreenshot(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleFocusActor(const TSharedPtr<FJsonObject>& Params);

    // PIE
    TSharedPtr<FJsonObject> HandleStartPIE(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleStopPIE(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleIsPIERunning(const TSharedPtr<FJsonObject>& Params);

    // Selection
    TSharedPtr<FJsonObject> HandleGetSelection(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleSetSelection(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleClearSelection(const TSharedPtr<FJsonObject>& Params);

    // Existing commands refactored into Handle* methods
    TSharedPtr<FJsonObject> HandleGetProjectInfo(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleGetWorldInfo(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleExecuteConsoleCommand(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleUndo(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleRedo(const TSharedPtr<FJsonObject>& Params);
};
