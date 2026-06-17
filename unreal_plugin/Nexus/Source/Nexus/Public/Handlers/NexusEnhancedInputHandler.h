// Copyright Nexus Team. All Rights Reserved.

#pragma once

#include "NexusCommandHandler.h"

/**
 * Handler for Enhanced Input subsystem commands:
 * creating input actions, mapping contexts, adding key bindings,
 * configuring triggers and modifiers, and querying existing actions.
 * Namespace: "input", 6 commands.
 *
 * Key UE APIs: UInputAction, UInputMappingContext, FEnhancedActionKeyMapping,
 *              UInputTrigger*, UInputModifier*, EInputActionValueType.
 */
class FNexusEnhancedInputHandler : public INexusCommandHandler
{
public:
    virtual FString GetNamespace() const override { return TEXT("input"); }

    virtual TArray<FString> GetSupportedCommands() const override
    {
        return {
            TEXT("create_input_action"),
            TEXT("create_mapping_context"),
            TEXT("add_action_mapping"),
            TEXT("set_trigger"),
            TEXT("set_modifier"),
            TEXT("list_input_actions")
        };
    }

    virtual TSharedPtr<FJsonObject> Handle(
        const FString& CommandType,
        const TSharedPtr<FJsonObject>& Params) override;

private:
    // Command handlers
    TSharedPtr<FJsonObject> HandleCreateInputAction(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleCreateMappingContext(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleAddActionMapping(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleSetTrigger(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleSetModifier(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleListInputActions(const TSharedPtr<FJsonObject>& Params);

    // Helpers
    class UInputAction* FindInputActionByName(const FString& ActionName);
    class UInputMappingContext* FindMappingContextByName(const FString& ContextName);
    struct FEnhancedActionKeyMapping* FindMappingEntry(
        class UInputMappingContext* Context,
        class UInputAction* Action,
        const FString& KeyName);
};
