// Copyright Nexus Team. All Rights Reserved.

#pragma once

#include "NexusCommandHandler.h"

/**
 * Handler for Source Control subsystem commands:
 * querying file status, checkout, add, revert, submit, history,
 * and delete operations through UE's provider-agnostic source
 * control abstraction (Perforce, SVN, Git, Plastic SCM, etc.).
 * Namespace: "sourcecontrol", 7 commands.
 *
 * Key UE APIs: ISourceControlModule, ISourceControlProvider,
 *              FSourceControlOperationRef, FSourceControlStateRef.
 */
class FNexusSourceControlHandler : public INexusCommandHandler
{
public:
    virtual FString GetNamespace() const override { return TEXT("sourcecontrol"); }

    virtual TArray<FString> GetSupportedCommands() const override
    {
        return {
            TEXT("get_source_control_status"),
            TEXT("checkout_files"),
            TEXT("add_files"),
            TEXT("revert_files"),
            TEXT("submit_changelist"),
            TEXT("get_file_history"),
            TEXT("mark_for_delete")
        };
    }

    virtual TSharedPtr<FJsonObject> Handle(
        const FString& CommandType,
        const TSharedPtr<FJsonObject>& Params) override;

private:
    // Command handlers
    TSharedPtr<FJsonObject> HandleGetSourceControlStatus(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleCheckoutFiles(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleAddFiles(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleRevertFiles(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleSubmitChangelist(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleGetFileHistory(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleMarkForDelete(const TSharedPtr<FJsonObject>& Params);

    // Helpers
    TArray<FString> GetFilePathsFromParams(const TSharedPtr<FJsonObject>& Params, const FString& Key = TEXT("file_paths"));
    FString SourceControlStateToString(const class ISourceControlState& State);
};
