// Copyright Nexus Team. All Rights Reserved.

#pragma once

#include "NexusCommandHandler.h"

class AAIController;

/**
 * Handler for AI subsystem commands: behavior trees, blackboards, EQS,
 * state trees, perception, AI controller inspection,
 * and BT/EQS node CRUD.
 * Namespace: "ai", 21 commands.
 */
class FNexusAIHandler : public INexusCommandHandler
{
public:
    virtual FString GetNamespace() const override { return TEXT("ai"); }

    virtual TArray<FString> GetSupportedCommands() const override
    {
        return {
            TEXT("create_behavior_tree"),
            TEXT("create_blackboard"),
            TEXT("create_eqs_query"),
            TEXT("create_state_tree"),
            TEXT("set_blackboard_key"),
            TEXT("set_ai_perception"),
            TEXT("assign_behavior_tree"),
            TEXT("get_ai_controller_info"),
            TEXT("list_behavior_trees"),
            TEXT("run_eqs_query"),
            // Behavior Tree node CRUD (Node CRUD Phase 4A)
            TEXT("add_bt_node"),
            TEXT("get_bt_nodes"),
            TEXT("update_bt_node"),
            TEXT("remove_bt_node"),
            TEXT("connect_bt_nodes"),
            TEXT("disconnect_bt_node"),
            // EQS node CRUD (Node CRUD Phase 4B)
            TEXT("add_eqs_generator"),
            TEXT("add_eqs_test"),
            TEXT("get_eqs_nodes"),
            TEXT("update_eqs_node"),
            TEXT("remove_eqs_node")
        };
    }

    virtual TSharedPtr<FJsonObject> Handle(
        const FString& CommandType,
        const TSharedPtr<FJsonObject>& Params) override;

private:
    // Actor / controller resolution
    AActor* FindActorByPath(const FString& Path);
    AActor* FindActorByLabel(const FString& Label);
    AAIController* ResolveAIController(const TSharedPtr<FJsonObject>& Params,
        const FString& PathKey, FString& OutError);

    // Command handlers
    TSharedPtr<FJsonObject> HandleCreateBehaviorTree(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleCreateBlackboard(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleCreateEQSQuery(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleCreateStateTree(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleSetBlackboardKey(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleSetAIPerception(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleAssignBehaviorTree(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleGetAIControllerInfo(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleListBehaviorTrees(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleRunEQSQuery(const TSharedPtr<FJsonObject>& Params);

    // Behavior Tree node CRUD (Node CRUD Phase 4A)
    TSharedPtr<FJsonObject> HandleAddBTNode(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleGetBTNodes(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleUpdateBTNode(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleRemoveBTNode(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleConnectBTNodes(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleDisconnectBTNode(const TSharedPtr<FJsonObject>& Params);

    // EQS node CRUD (Node CRUD Phase 4B)
    TSharedPtr<FJsonObject> HandleAddEQSGenerator(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleAddEQSTest(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleGetEQSNodes(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleUpdateEQSNode(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleRemoveEQSNode(const TSharedPtr<FJsonObject>& Params);
};
