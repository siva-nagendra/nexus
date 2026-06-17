// Copyright Nexus Team. All Rights Reserved.

#pragma once

#include "NexusCommandHandler.h"

/**
 * Handler for landscape and foliage subsystem commands: creating landscapes,
 * sculpting terrain, painting layers, managing foliage types, and heightmap I/O.
 * Namespace: "landscape", 9 commands.
 *
 * Key UE APIs: ALandscapeProxy, ULandscapeInfo, FLandscapeEditDataInterface,
 * AInstancedFoliageActor, UFoliageType_InstancedStaticMesh.
 *
 * create_landscape and heightmap ops are long-running (30s/60s timeout).
 */
class FNexusLandscapeHandler : public INexusCommandHandler
{
public:
    virtual FString GetNamespace() const override { return TEXT("landscape"); }

    virtual TArray<FString> GetSupportedCommands() const override
    {
        return {
            TEXT("create_landscape"),
            TEXT("sculpt_landscape"),
            TEXT("paint_landscape_layer"),
            TEXT("add_foliage_type"),
            TEXT("paint_foliage"),
            TEXT("remove_foliage"),
            TEXT("import_heightmap"),
            TEXT("export_heightmap"),
            TEXT("get_landscape_info")
        };
    }

    virtual TSharedPtr<FJsonObject> Handle(
        const FString& CommandType,
        const TSharedPtr<FJsonObject>& Params) override;

private:
    // Command handlers
    TSharedPtr<FJsonObject> HandleCreateLandscape(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleSculptLandscape(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandlePaintLandscapeLayer(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleAddFoliageType(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandlePaintFoliage(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleRemoveFoliage(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleImportHeightmap(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleExportHeightmap(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleGetLandscapeInfo(const TSharedPtr<FJsonObject>& Params);

    // Helpers
    class ALandscapeProxy* FindLandscapeByLabel(class UWorld* World, const FString& Label);
};
