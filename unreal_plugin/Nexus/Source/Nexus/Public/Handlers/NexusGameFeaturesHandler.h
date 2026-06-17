// Copyright Nexus Team. All Rights Reserved.

#pragma once

#include "NexusCommandHandler.h"
#include "GameFeatureTypes.h"

/**
 * Handler for Game Features subsystem commands:
 * listing, activating, deactivating, creating, and inspecting
 * Game Feature plugins at editor time.
 * Namespace: "gamefeatures", 5 commands.
 *
 * Key UE APIs: UGameFeaturesSubsystem, UGameFeatureData,
 *              IGameFeatureStateChangeObserver, FGameFeaturePluginStateMachine.
 */
class FNexusGameFeaturesHandler : public INexusCommandHandler
{
public:
    virtual FString GetNamespace() const override { return TEXT("gamefeatures"); }

    virtual TArray<FString> GetSupportedCommands() const override
    {
        return {
            TEXT("list_game_features"),
            TEXT("activate_game_feature"),
            TEXT("deactivate_game_feature"),
            TEXT("create_game_feature"),
            TEXT("get_game_feature_info")
        };
    }

    virtual TSharedPtr<FJsonObject> Handle(
        const FString& CommandType,
        const TSharedPtr<FJsonObject>& Params) override;

private:
    // Command handlers
    TSharedPtr<FJsonObject> HandleListGameFeatures(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleActivateGameFeature(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleDeactivateGameFeature(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleCreateGameFeature(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleGetGameFeatureInfo(const TSharedPtr<FJsonObject>& Params);

    // Helpers
    FString GameFeatureStateToString(EGameFeaturePluginState State);
};
