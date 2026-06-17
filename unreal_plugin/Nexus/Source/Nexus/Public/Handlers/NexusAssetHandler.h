// Copyright Nexus Team. All Rights Reserved.

#pragma once

#include "NexusCommandHandler.h"

class FNexusAssetHandler : public INexusCommandHandler
{
public:
    virtual FString GetNamespace() const override { return TEXT("asset"); }

    virtual TArray<FString> GetSupportedCommands() const override
    {
        return {
            TEXT("search"), TEXT("get_info"), TEXT("exists"),
            TEXT("import"), TEXT("delete"), TEXT("rename"),
            TEXT("duplicate"), TEXT("save"), TEXT("save_all"),
            TEXT("get_references"), TEXT("get_dependents"),
            TEXT("create_folder"), TEXT("list_folder"),
            TEXT("set_metadata"), TEXT("validate"),
            // Batch operations (A3)
            TEXT("import_batch")
        };
    }

    virtual TSharedPtr<FJsonObject> Handle(
        const FString& CommandType,
        const TSharedPtr<FJsonObject>& Params) override;

private:
    TSharedPtr<FJsonObject> HandleSearch(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleGetInfo(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleExists(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleImport(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleDelete(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleRename(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleDuplicate(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleSave(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleSaveAll(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleGetReferences(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleGetDependents(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleCreateFolder(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleListFolder(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleSetMetadata(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleValidate(const TSharedPtr<FJsonObject>& Params);

    // --- Batch operations (A3) ---
    TSharedPtr<FJsonObject> HandleImportBatch(const TSharedPtr<FJsonObject>& Params);
};
