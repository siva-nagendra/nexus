// Copyright Nexus Team. All Rights Reserved.

#pragma once

#include "NexusCommandHandler.h"

/**
 * Handler for executing Python code inside Unreal Engine's Python subsystem.
 * Namespace: "python", 3 commands.
 *
 * Provides the escape hatch for operations not covered by dedicated C++ handlers.
 * The codegen system generates UE Python code that ultimately runs through this
 * handler's execute command. Also supports running .py files and querying
 * the embedded Python interpreter's path configuration.
 *
 * Key UE APIs: IPythonScriptPlugin::ExecPythonCommand(),
 *              IPythonScriptPlugin::ExecPythonCommandEx()
 *
 * NOTE: PythonScriptPlugin must be enabled in the project. If not available,
 * commands return an appropriate error.
 */
class FNexusPythonExecHandler : public INexusCommandHandler
{
public:
    virtual FString GetNamespace() const override { return TEXT("python"); }

    virtual TArray<FString> GetSupportedCommands() const override
    {
        return {
            TEXT("execute"),
            TEXT("execute_file"),
            TEXT("get_paths")
        };
    }

    virtual TSharedPtr<FJsonObject> Handle(
        const FString& CommandType,
        const TSharedPtr<FJsonObject>& Params) override;

private:
    // Command handlers
    TSharedPtr<FJsonObject> HandleExecute(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleExecuteFile(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleGetPaths(const TSharedPtr<FJsonObject>& Params);

    /** Escape and triple-quote a code string for embedding in Python exec(). */
    static FString QuoteCodeForPython(const FString& Code);
};
