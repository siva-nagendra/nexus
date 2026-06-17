// Copyright Nexus Team. All Rights Reserved.

#pragma once

#include "NexusCommandHandler.h"

/**
 * Handler for rendering subsystem commands: CVar-based control of
 * Nanite, Lumen, VSM, TSR, post-process, console variables, and scalability.
 * Namespace: "rendering", 8 commands.
 */
class FNexusRenderingHandler : public INexusCommandHandler
{
public:
    virtual FString GetNamespace() const override { return TEXT("rendering"); }

    virtual TArray<FString> GetSupportedCommands() const override
    {
        return {
            TEXT("get_rendering_settings"),
            TEXT("set_nanite_enabled"),
            TEXT("set_lumen_settings"),
            TEXT("set_vsm_settings"),
            TEXT("set_tsr_settings"),
            TEXT("set_post_process_settings"),
            TEXT("set_console_variable"),
            TEXT("get_scalability_settings")
        };
    }

    virtual TSharedPtr<FJsonObject> Handle(
        const FString& CommandType,
        const TSharedPtr<FJsonObject>& Params) override;

private:
    // Command handlers
    TSharedPtr<FJsonObject> HandleGetRenderingSettings(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleSetNaniteEnabled(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleSetLumenSettings(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleSetVsmSettings(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleSetTsrSettings(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleSetPostProcessSettings(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleSetConsoleVariable(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleGetScalabilitySettings(const TSharedPtr<FJsonObject>& Params);

    // Helpers
    FString GetCVarString(const TCHAR* CVarName);
    int32 GetCVarInt(const TCHAR* CVarName);
    float GetCVarFloat(const TCHAR* CVarName);
    bool SetCVar(const FString& CVarName, const FString& Value, FString& OutPreviousValue);
};
