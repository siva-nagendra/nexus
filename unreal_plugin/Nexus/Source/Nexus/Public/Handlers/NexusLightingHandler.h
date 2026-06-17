// Copyright Nexus Team. All Rights Reserved.

#pragma once

#include "NexusCommandHandler.h"

/**
 * Handler for lighting subsystem commands: light spawning, sky atmosphere,
 * exponential fog, volumetric clouds, GI configuration, light baking,
 * shadow settings, and lighting scenario management.
 * Namespace: "lighting", 10 commands.
 */
class FNexusLightingHandler : public INexusCommandHandler
{
public:
    virtual FString GetNamespace() const override { return TEXT("lighting"); }

    virtual TArray<FString> GetSupportedCommands() const override
    {
        return {
            TEXT("spawn_light"),
            TEXT("create_light_scenario"),
            TEXT("set_light_properties"),
            TEXT("set_sky_atmosphere"),
            TEXT("set_exponential_fog"),
            TEXT("set_volumetric_clouds"),
            TEXT("configure_global_illumination"),
            TEXT("bake_lighting"),
            TEXT("set_shadow_settings"),
            TEXT("get_lighting_info")
        };
    }

    virtual TSharedPtr<FJsonObject> Handle(
        const FString& CommandType,
        const TSharedPtr<FJsonObject>& Params) override;

private:
    // Command handlers
    TSharedPtr<FJsonObject> HandleSpawnLight(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleCreateLightScenario(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleSetLightProperties(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleSetSkyAtmosphere(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleSetExponentialFog(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleSetVolumetricClouds(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleConfigureGlobalIllumination(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleBakeLighting(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleSetShadowSettings(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleGetLightingInfo(const TSharedPtr<FJsonObject>& Params);

    // Helpers
    class ULightComponent* FindLightComponent(class AActor* Actor);
};
