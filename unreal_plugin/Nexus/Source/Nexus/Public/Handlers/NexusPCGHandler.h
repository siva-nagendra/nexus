// Copyright Nexus Team. All Rights Reserved.

#pragma once

#include "NexusCommandHandler.h"

/**
 * Handler for Procedural Content Generation (PCG) subsystem commands:
 * creating graphs, adding/connecting nodes, configuring settings, and executing.
 * Namespace: "pcg", 6 commands.
 *
 * Key UE APIs: UPCGGraph, UPCGComponent, UPCGNode, UPCGSettings, UPCGSubsystem.
 *
 * execute_pcg_graph is long-running (120s timeout) — critical for ML data pipelines.
 */
class FNexusPCGHandler : public INexusCommandHandler
{
public:
    virtual FString GetNamespace() const override { return TEXT("pcg"); }

    virtual TArray<FString> GetSupportedCommands() const override
    {
        return {
            TEXT("create_pcg_graph"),
            TEXT("add_pcg_node"),
            TEXT("connect_pcg_nodes"),
            TEXT("set_pcg_settings"),
            TEXT("execute_pcg_graph"),
            TEXT("get_pcg_info")
        };
    }

    virtual TSharedPtr<FJsonObject> Handle(
        const FString& CommandType,
        const TSharedPtr<FJsonObject>& Params) override;

private:
    // Command handlers
    TSharedPtr<FJsonObject> HandleCreatePCGGraph(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleAddPCGNode(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleConnectPCGNodes(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleSetPCGSettings(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleExecutePCGGraph(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleGetPCGInfo(const TSharedPtr<FJsonObject>& Params);

    // Helpers
    class UPCGComponent* FindPCGComponentByLabels(
        class UWorld* World,
        const FString& GraphName,
        const FString& OwnerActorLabel);

    class UPCGGraph* FindPCGGraphByName(
        class UWorld* World,
        const FString& GraphName);

    class UPCGNode* FindNodeByLabel(
        class UPCGGraph* Graph,
        const FString& NodeLabel);
};
