// Copyright Nexus Team. All Rights Reserved.

#pragma once

#include "NexusCommandHandler.h"

/**
 * Handler for actor commands: spawn, find, delete, transform, properties, etc.
 * Supports 20 base commands (spawn_batch added separately in A3).
 */
class FNexusActorHandler : public INexusCommandHandler
{
public:
    virtual FString GetNamespace() const override { return TEXT("actor"); }

    virtual TArray<FString> GetSupportedCommands() const override
    {
        return {
            TEXT("spawn"), TEXT("find"), TEXT("find_by_class"), TEXT("delete"),
            TEXT("get_transform"), TEXT("set_transform"),
            TEXT("get_property"), TEXT("set_property"),
            TEXT("list_all"), TEXT("get_components"),
            TEXT("set_visibility"), TEXT("duplicate"), TEXT("rename"),
            TEXT("add_tag"), TEXT("remove_tag"), TEXT("get_tags"),
            TEXT("attach"), TEXT("detach"),
            TEXT("set_mobility"), TEXT("get_bounds"),
            // Batch operations (A3)
            TEXT("spawn_batch"), TEXT("set_properties_batch"),
            TEXT("delete_batch"), TEXT("set_transform_batch")
        };
    }

    virtual TSharedPtr<FJsonObject> Handle(
        const FString& CommandType,
        const TSharedPtr<FJsonObject>& Params) override;

private:
    // --- Existing commands ---
    TSharedPtr<FJsonObject> HandleSpawn(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleFind(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleFindByClass(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleDelete(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleGetTransform(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleSetTransform(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleListAll(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleGetComponents(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleGetBounds(const TSharedPtr<FJsonObject>& Params);

    // --- New commands ---
    TSharedPtr<FJsonObject> HandleGetProperty(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleSetProperty(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleSetVisibility(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleDuplicate(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleRename(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleAddTag(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleRemoveTag(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleGetTags(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleAttach(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleDetach(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleSetMobility(const TSharedPtr<FJsonObject>& Params);

    // --- Batch operations (A3) ---
    TSharedPtr<FJsonObject> HandleSpawnBatch(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleSetPropertiesBatch(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleDeleteBatch(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleSetTransformBatch(const TSharedPtr<FJsonObject>& Params);

    /** Find actor by label in current world. */
    AActor* FindActorByLabel(const FString& Label);

    /** Find actor by full object path in current world. */
    AActor* FindActorByPath(const FString& Path);

    /**
     * Resolve actor from params — supports both actor_path and actor_label.
     * Prefers actor_path if present; falls back to actor_label.
     * Returns nullptr and sets OutError if not found.
     */
    AActor* ResolveActor(const TSharedPtr<FJsonObject>& Params, FString& OutError);
};
