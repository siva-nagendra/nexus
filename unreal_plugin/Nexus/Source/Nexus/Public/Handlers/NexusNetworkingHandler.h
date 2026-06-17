// Copyright Nexus Team. All Rights Reserved.

#pragma once

#include "NexusCommandHandler.h"

/**
 * Handler for networking/replication subsystem commands:
 * configuring replication, net roles, RPCs, relevancy, and
 * querying replication info and replicated properties.
 * Namespace: "networking", 6 commands.
 *
 * Key UE APIs: AActor::bReplicates, bReplicateMovement, bAlwaysRelevant,
 *              NetUpdateFrequency, ENetRole, FProperty CPF_Net flags,
 *              FUNC_Net/FUNC_NetServer/FUNC_NetClient/FUNC_NetMulticast.
 */
class FNexusNetworkingHandler : public INexusCommandHandler
{
public:
    virtual FString GetNamespace() const override { return TEXT("networking"); }

    virtual TArray<FString> GetSupportedCommands() const override
    {
        return {
            TEXT("set_replication"),
            TEXT("set_net_role"),
            TEXT("add_rpc"),
            TEXT("set_net_relevancy"),
            TEXT("get_replication_info"),
            TEXT("list_replicated_properties")
        };
    }

    virtual TSharedPtr<FJsonObject> Handle(
        const FString& CommandType,
        const TSharedPtr<FJsonObject>& Params) override;

private:
    // Command handlers
    TSharedPtr<FJsonObject> HandleSetReplication(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleSetNetRole(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleAddRpc(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleSetNetRelevancy(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleGetReplicationInfo(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleListReplicatedProperties(const TSharedPtr<FJsonObject>& Params);

    // Helpers
    AActor* FindActorByLabel(const FString& Label);
};
