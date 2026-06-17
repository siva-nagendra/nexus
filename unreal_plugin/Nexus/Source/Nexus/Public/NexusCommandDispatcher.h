// Copyright Nexus Team. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

class INexusCommandHandler;

/**
 * Routes incoming JSON commands to registered handler subsystems.
 * Uses a TMap<FString, Handler*> for O(1) command dispatch.
 * Pattern from kvick-games/UnrealMCP with added namespace grouping.
 */
class FNexusCommandDispatcher
{
public:
    FNexusCommandDispatcher();
    ~FNexusCommandDispatcher();

    /** Register a handler. All its supported commands are added to the routing map. */
    void RegisterHandler(TSharedPtr<INexusCommandHandler> Handler);

    /** Dispatch a command to the appropriate handler. Returns JSON response. */
    TSharedPtr<FJsonObject> Dispatch(
        const FString& CommandType,
        const TSharedPtr<FJsonObject>& Params
    );

    /** Get list of all registered command types. */
    TArray<FString> GetRegisteredCommands() const;

private:
    /** Register all built-in handlers. */
    void RegisterDefaultHandlers();

    /** Create a standard error response. */
    TSharedPtr<FJsonObject> MakeError(const FString& Code, const FString& Message);

    /** Command type -> handler routing map. */
    TMap<FString, TSharedPtr<INexusCommandHandler>> HandlerMap;

    /** All registered handlers (for lifecycle management). */
    TArray<TSharedPtr<INexusCommandHandler>> Handlers;
};
