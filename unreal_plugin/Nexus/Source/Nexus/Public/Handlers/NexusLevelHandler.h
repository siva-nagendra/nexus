// Copyright Nexus Team. All Rights Reserved.

#pragma once

#include "NexusCommandHandler.h"

class FNexusLevelHandler : public INexusCommandHandler
{
public:
    virtual FString GetNamespace() const override { return TEXT("level"); }

    virtual TArray<FString> GetSupportedCommands() const override
    {
        return {
            TEXT("get_current"), TEXT("load"), TEXT("save"),
            TEXT("create"), TEXT("list_sublevels"),
            TEXT("add_sublevel"), TEXT("remove_sublevel"),
            TEXT("set_sublevel_visibility"),
            TEXT("get_world_partition_info"), TEXT("set_data_layer"),
            TEXT("list_streaming_levels"), TEXT("get_bounds")
        };
    }

    virtual TSharedPtr<FJsonObject> Handle(
        const FString& CommandType,
        const TSharedPtr<FJsonObject>& Params) override;

private:
    TSharedPtr<FJsonObject> HandleGetCurrent(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleLoad(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleSave(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleCreate(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleListSublevels(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleAddSublevel(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleRemoveSublevel(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleSetSublevelVisibility(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleGetWorldPartitionInfo(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleSetDataLayer(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleListStreamingLevels(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleGetBounds(const TSharedPtr<FJsonObject>& Params);
};
