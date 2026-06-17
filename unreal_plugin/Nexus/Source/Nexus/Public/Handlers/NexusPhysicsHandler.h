// Copyright Nexus Team. All Rights Reserved.

#pragma once

#include "NexusCommandHandler.h"

/**
 * Handler for physics subsystem commands: collision profiles, simulation,
 * physics properties, constraints, forces, and physics asset creation.
 * Namespace: "physics", 8 commands.
 */
class FNexusPhysicsHandler : public INexusCommandHandler
{
public:
    virtual FString GetNamespace() const override { return TEXT("physics"); }

    virtual TArray<FString> GetSupportedCommands() const override
    {
        return {
            TEXT("set_collision_profile"),
            TEXT("enable_physics_simulation"),
            TEXT("set_physics_properties"),
            TEXT("set_collision_response"),
            TEXT("add_physics_constraint"),
            TEXT("create_physics_asset"),
            TEXT("apply_force"),
            TEXT("get_physics_info")
        };
    }

    virtual TSharedPtr<FJsonObject> Handle(
        const FString& CommandType,
        const TSharedPtr<FJsonObject>& Params) override;

private:
    // Actor / component resolution helpers
    AActor* FindActorByPath(const FString& Path);
    AActor* FindActorByLabel(const FString& Label);
    AActor* ResolveActor(const TSharedPtr<FJsonObject>& Params, FString& OutError);
    UPrimitiveComponent* GetPrimitiveComponent(
        AActor* Actor, const FString& ComponentName, FString& OutError);

    // Command handlers
    TSharedPtr<FJsonObject> HandleSetCollisionProfile(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleEnablePhysicsSimulation(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleSetPhysicsProperties(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleSetCollisionResponse(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleAddPhysicsConstraint(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleCreatePhysicsAsset(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleApplyForce(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleGetPhysicsInfo(const TSharedPtr<FJsonObject>& Params);
};
