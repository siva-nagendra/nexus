// Copyright Nexus Team. All Rights Reserved.

#pragma once

#include "NexusCommandHandler.h"

class FNexusBlueprintHandler : public INexusCommandHandler
{
public:
    virtual FString GetNamespace() const override { return TEXT("blueprint"); }

    virtual TArray<FString> GetSupportedCommands() const override
    {
        return {
            TEXT("create"), TEXT("compile"), TEXT("get_info"),
            TEXT("add_variable"), TEXT("set_variable_default"),
            TEXT("add_function"), TEXT("add_component"),
            TEXT("get_variables"), TEXT("get_functions"), TEXT("get_graphs"),
            TEXT("add_event_dispatcher"), TEXT("set_parent_class"),
            TEXT("add_interface"), TEXT("open"),
            TEXT("add_node"), TEXT("connect_pins"),
            TEXT("remove_node"), TEXT("get_node_pins")
        };
    }

    virtual TSharedPtr<FJsonObject> Handle(
        const FString& CommandType,
        const TSharedPtr<FJsonObject>& Params) override;

private:
    // Existing commands
    TSharedPtr<FJsonObject> HandleCreate(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleCompile(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleInfo(const TSharedPtr<FJsonObject>& Params);

    // Variable operations
    TSharedPtr<FJsonObject> HandleAddVariable(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleSetVariableDefault(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleGetVariables(const TSharedPtr<FJsonObject>& Params);

    // Function operations
    TSharedPtr<FJsonObject> HandleAddFunction(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleGetFunctions(const TSharedPtr<FJsonObject>& Params);

    // Component operations
    TSharedPtr<FJsonObject> HandleAddComponent(const TSharedPtr<FJsonObject>& Params);

    // Graph operations
    TSharedPtr<FJsonObject> HandleGetGraphs(const TSharedPtr<FJsonObject>& Params);

    // Event dispatchers
    TSharedPtr<FJsonObject> HandleAddEventDispatcher(const TSharedPtr<FJsonObject>& Params);

    // Parent class / interfaces
    TSharedPtr<FJsonObject> HandleSetParentClass(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleAddInterface(const TSharedPtr<FJsonObject>& Params);

    // Editor
    TSharedPtr<FJsonObject> HandleOpen(const TSharedPtr<FJsonObject>& Params);

    // Graph node operations
    TSharedPtr<FJsonObject> HandleAddNode(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleConnectPins(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleRemoveNode(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleGetNodePins(const TSharedPtr<FJsonObject>& Params);

    // Helpers
    UBlueprint* LoadBlueprint(const TSharedPtr<FJsonObject>& Params, const FString& ParamName = TEXT("blueprint_path"));
    UEdGraph* FindGraphByName(UBlueprint* BP, const FString& GraphName);
    UEdGraphNode* FindNodeById(UEdGraph* Graph, const FString& NodeId);
    TSharedPtr<FJsonObject> PinToJson(const UEdGraphPin* Pin);
    FEdGraphPinType ParsePinType(const FString& TypeName);
};
