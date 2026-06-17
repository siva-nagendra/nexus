// Copyright Nexus Team. All Rights Reserved.

#include "Handlers/NexusLightingHandler.h"
#include "Editor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "Components/LightComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/SpotLightComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/RectLightComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/SkyAtmosphereComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/VolumetricCloudComponent.h"
#include "Engine/PointLight.h"
#include "Engine/SpotLight.h"
#include "Engine/DirectionalLight.h"
#include "Engine/RectLight.h"
#include "Engine/SkyLight.h"
#include "Atmosphere/AtmosphericFog.h"
#include "Engine/ExponentialHeightFog.h"
#include "LevelEditorSubsystem.h"
#include "Subsystems/UnrealEditorSubsystem.h"
#include "LevelEditor.h"
#include "EditorLevelUtils.h"
#include "LevelUtils.h"
#include "LightingBuildOptions.h"
#include "Engine/LevelStreaming.h"
#include "Engine/Level.h"
#include "Engine/LevelStreamingDynamic.h"

// ─────────────────────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────────────────────

ULightComponent* FNexusLightingHandler::FindLightComponent(AActor* Actor)
{
    if (!Actor) return nullptr;
    return Actor->FindComponentByClass<ULightComponent>();
}

static UWorld* GetEditorWorld()
{
    if (GEditor)
    {
        return GEditor->GetEditorWorldContext().World();
    }
    return nullptr;
}

static AActor* FindActorByPathOrLabel(UWorld* World, const FString& ActorPath)
{
    if (!World || ActorPath.IsEmpty()) return nullptr;

    for (TActorIterator<AActor> It(World); It; ++It)
    {
        AActor* Actor = *It;
        if (Actor->GetPathName() == ActorPath || Actor->GetActorLabel() == ActorPath)
        {
            return Actor;
        }
    }
    return nullptr;
}

// ─────────────────────────────────────────────────────────────────────────────
// Dispatch
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FNexusLightingHandler::Handle(
    const FString& CommandType,
    const TSharedPtr<FJsonObject>& Params)
{
    FString SubCommand = CommandType.RightChop(GetNamespace().Len() + 1);

    if (SubCommand == TEXT("spawn_light"))                    return HandleSpawnLight(Params);
    if (SubCommand == TEXT("create_light_scenario"))          return HandleCreateLightScenario(Params);
    if (SubCommand == TEXT("set_light_properties"))           return HandleSetLightProperties(Params);
    if (SubCommand == TEXT("set_sky_atmosphere"))             return HandleSetSkyAtmosphere(Params);
    if (SubCommand == TEXT("set_exponential_fog"))            return HandleSetExponentialFog(Params);
    if (SubCommand == TEXT("set_volumetric_clouds"))          return HandleSetVolumetricClouds(Params);
    if (SubCommand == TEXT("configure_global_illumination"))  return HandleConfigureGlobalIllumination(Params);
    if (SubCommand == TEXT("bake_lighting"))                  return HandleBakeLighting(Params);
    if (SubCommand == TEXT("set_shadow_settings"))            return HandleSetShadowSettings(Params);
    if (SubCommand == TEXT("get_lighting_info"))              return HandleGetLightingInfo(Params);

    return MakeError(TEXT("NOT_IMPLEMENTED"),
        FString::Printf(TEXT("Command '%s' not yet implemented"), *CommandType));
}

// ─────────────────────────────────────────────────────────────────────────────
// lighting.spawn_light
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FNexusLightingHandler::HandleSpawnLight(
    const TSharedPtr<FJsonObject>& Params)
{
    UWorld* World = GetEditorWorld();
    if (!World)
    {
        return MakeError(TEXT("NO_WORLD"), TEXT("No editor world available"));
    }

    FString LightType = GetStringParam(Params, TEXT("light_type"), TEXT("PointLight"));
    FString Label = GetStringParam(Params, TEXT("label"));
    FVector Location = GetVectorParam(Params, TEXT("location"));
    FVector RotVec = GetVectorParam(Params, TEXT("rotation"));
    FRotator Rotation(RotVec.X, RotVec.Y, RotVec.Z);
    double Intensity = GetNumberParam(Params, TEXT("intensity"), 5000.0);
    FVector Color = GetVectorParam(Params, TEXT("color"), FVector(1.0, 1.0, 1.0));
    double AttenuationRadius = GetNumberParam(Params, TEXT("attenuation_radius"), 1000.0);
    bool bCastShadows = GetBoolParam(Params, TEXT("cast_shadows"), true);

    // Determine the actor class to spawn
    UClass* LightClass = nullptr;
    if (LightType == TEXT("PointLight"))
    {
        LightClass = APointLight::StaticClass();
    }
    else if (LightType == TEXT("SpotLight"))
    {
        LightClass = ASpotLight::StaticClass();
    }
    else if (LightType == TEXT("DirectionalLight"))
    {
        LightClass = ADirectionalLight::StaticClass();
    }
    else if (LightType == TEXT("RectLight"))
    {
        LightClass = ARectLight::StaticClass();
    }
    else if (LightType == TEXT("SkyLight"))
    {
        LightClass = ASkyLight::StaticClass();
    }
    else
    {
        return MakeError(TEXT("INVALID_LIGHT_TYPE"),
            FString::Printf(TEXT("Unknown light type '%s'. Supported: PointLight, SpotLight, DirectionalLight, RectLight, SkyLight"), *LightType));
    }

    FActorSpawnParameters SpawnParams;
    SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

    FTransform SpawnTransform(Rotation.Quaternion(), Location);
    AActor* NewActor = World->SpawnActor<AActor>(LightClass, SpawnTransform, SpawnParams);
    if (!NewActor)
    {
        return MakeError(TEXT("SPAWN_FAILED"),
            FString::Printf(TEXT("Failed to spawn %s actor"), *LightType));
    }

    if (!Label.IsEmpty())
    {
        NewActor->SetActorLabel(Label);
    }

    // Configure light component properties
    ULightComponent* LightComp = FindLightComponent(NewActor);
    if (LightComp)
    {
        LightComp->SetIntensity(static_cast<float>(Intensity));
        LightComp->SetLightColor(FLinearColor(
            static_cast<float>(Color.X),
            static_cast<float>(Color.Y),
            static_cast<float>(Color.Z)));
        LightComp->SetCastShadows(bCastShadows);

        // Attenuation radius applies to point/spot/rect lights
        if (UPointLightComponent* PointComp = Cast<UPointLightComponent>(LightComp))
        {
            PointComp->SetAttenuationRadius(static_cast<float>(AttenuationRadius));
        }
    }

    NewActor->MarkPackageDirty();

    TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
    Data->SetStringField(TEXT("actor_path"), NewActor->GetPathName());
    Data->SetStringField(TEXT("actor_label"), NewActor->GetActorLabel());
    Data->SetStringField(TEXT("light_type"), LightType);
    Data->SetObjectField(TEXT("location"), VectorToJson(NewActor->GetActorLocation()));
    Data->SetObjectField(TEXT("rotation"), RotatorToJson(NewActor->GetActorRotation()));
    Data->SetNumberField(TEXT("intensity"), Intensity);
    Data->SetBoolField(TEXT("cast_shadows"), bCastShadows);
    return MakeSuccess(Data);
}

// ─────────────────────────────────────────────────────────────────────────────
// lighting.create_light_scenario
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FNexusLightingHandler::HandleCreateLightScenario(
    const TSharedPtr<FJsonObject>& Params)
{
    UWorld* World = GetEditorWorld();
    if (!World)
    {
        return MakeError(TEXT("NO_WORLD"), TEXT("No editor world available"));
    }

    FString ScenarioName = GetStringParam(Params, TEXT("scenario_name"));
    if (ScenarioName.IsEmpty())
    {
        return MakeError(TEXT("MISSING_PARAM"), TEXT("scenario_name is required"));
    }

    FString Description = GetStringParam(Params, TEXT("description"));
    FString LightsToInclude = GetStringParam(Params, TEXT("lights_to_include"));

    // Create a new streaming level for the lighting scenario using UEditorLevelUtils
    // In UE 5.7, use CreateNewStreamingLevel instead of CreateNewLevel
    ULevelStreaming* StreamingLevel = UEditorLevelUtils::CreateNewStreamingLevel(
        ULevelStreamingDynamic::StaticClass(),
        FString::Printf(TEXT("/Game/LightingScenarios/%s"), *ScenarioName),
        /*bMoveSelectedActorsIntoNewLevel=*/false);

    if (!StreamingLevel)
    {
        return MakeError(TEXT("LEVEL_CREATE_FAILED"),
            FString::Printf(TEXT("Failed to create lighting scenario sub-level '%s'"), *ScenarioName));
    }

    ULevel* NewLevel = StreamingLevel->GetLoadedLevel();

    // Set the level's lighting scenario flag
    // In UE 5.7, the lighting scenario flag is on ULevel, not ULevelStreaming
    if (NewLevel)
    {
        NewLevel->SetLightingScenario(true);
    }

    // Move specified lights into the new level if provided
    TArray<FString> MovedLights;
    if (!LightsToInclude.IsEmpty() && NewLevel)
    {
        TArray<FString> LightNames;
        LightsToInclude.ParseIntoArray(LightNames, TEXT(","), true);

        for (const FString& LightName : LightNames)
        {
            FString TrimmedName = LightName.TrimStartAndEnd();
            AActor* LightActor = FindActorByPathOrLabel(World, TrimmedName);
            if (LightActor && FindLightComponent(LightActor))
            {
                // Move the actor to the new level
                int32 NumMoved = UEditorLevelUtils::MoveActorsToLevel({LightActor}, StreamingLevel);
                if (NumMoved > 0)
                {
                    MovedLights.Add(TrimmedName);
                }
            }
        }
    }

    TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
    Data->SetStringField(TEXT("scenario_name"), ScenarioName);
    Data->SetStringField(TEXT("description"), Description);
    Data->SetBoolField(TEXT("is_lighting_scenario"), true);
    Data->SetNumberField(TEXT("lights_moved"), MovedLights.Num());

    TArray<TSharedPtr<FJsonValue>> MovedArr;
    for (const FString& Name : MovedLights)
    {
        MovedArr.Add(MakeShareable(new FJsonValueString(Name)));
    }
    Data->SetArrayField(TEXT("moved_lights"), MovedArr);
    return MakeSuccess(Data);
}

// ─────────────────────────────────────────────────────────────────────────────
// lighting.set_light_properties
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FNexusLightingHandler::HandleSetLightProperties(
    const TSharedPtr<FJsonObject>& Params)
{
    UWorld* World = GetEditorWorld();
    if (!World)
    {
        return MakeError(TEXT("NO_WORLD"), TEXT("No editor world available"));
    }

    FString ActorPath = GetStringParam(Params, TEXT("actor_path"));
    if (ActorPath.IsEmpty())
    {
        return MakeError(TEXT("MISSING_PARAM"), TEXT("actor_path is required"));
    }

    AActor* Actor = FindActorByPathOrLabel(World, ActorPath);
    if (!Actor)
    {
        return MakeError(TEXT("ACTOR_NOT_FOUND"),
            FString::Printf(TEXT("Light actor '%s' not found"), *ActorPath));
    }

    ULightComponent* LightComp = FindLightComponent(Actor);
    if (!LightComp)
    {
        return MakeError(TEXT("NOT_A_LIGHT"),
            FString::Printf(TEXT("Actor '%s' does not have a light component"), *ActorPath));
    }

    TArray<TSharedPtr<FJsonValue>> ModifiedArr;

    // Intensity
    double IntensityVal;
    if (Params->TryGetNumberField(TEXT("intensity"), IntensityVal))
    {
        LightComp->SetIntensity(static_cast<float>(IntensityVal));
        ModifiedArr.Add(MakeShareable(new FJsonValueString(TEXT("intensity"))));
    }

    // Color (from array [r, g, b])
    const TArray<TSharedPtr<FJsonValue>>* ColorArr;
    if (Params->TryGetArrayField(TEXT("color"), ColorArr) && ColorArr->Num() >= 3)
    {
        FLinearColor LightColor(
            static_cast<float>((*ColorArr)[0]->AsNumber()),
            static_cast<float>((*ColorArr)[1]->AsNumber()),
            static_cast<float>((*ColorArr)[2]->AsNumber()));
        LightComp->SetLightColor(LightColor);
        ModifiedArr.Add(MakeShareable(new FJsonValueString(TEXT("color"))));
    }

    // Attenuation radius
    double AttRadius;
    if (Params->TryGetNumberField(TEXT("attenuation_radius"), AttRadius))
    {
        if (UPointLightComponent* PointComp = Cast<UPointLightComponent>(LightComp))
        {
            PointComp->SetAttenuationRadius(static_cast<float>(AttRadius));
            ModifiedArr.Add(MakeShareable(new FJsonValueString(TEXT("attenuation_radius"))));
        }
    }

    // Source radius
    double SourceRadius;
    if (Params->TryGetNumberField(TEXT("source_radius"), SourceRadius))
    {
        if (UPointLightComponent* PointComp = Cast<UPointLightComponent>(LightComp))
        {
            PointComp->SourceRadius = static_cast<float>(SourceRadius);
            ModifiedArr.Add(MakeShareable(new FJsonValueString(TEXT("source_radius"))));
        }
    }

    // Inner/outer cone angle (spot lights)
    double InnerCone;
    if (Params->TryGetNumberField(TEXT("inner_cone_angle"), InnerCone))
    {
        if (USpotLightComponent* SpotComp = Cast<USpotLightComponent>(LightComp))
        {
            SpotComp->SetInnerConeAngle(static_cast<float>(InnerCone));
            ModifiedArr.Add(MakeShareable(new FJsonValueString(TEXT("inner_cone_angle"))));
        }
    }

    double OuterCone;
    if (Params->TryGetNumberField(TEXT("outer_cone_angle"), OuterCone))
    {
        if (USpotLightComponent* SpotComp = Cast<USpotLightComponent>(LightComp))
        {
            SpotComp->SetOuterConeAngle(static_cast<float>(OuterCone));
            ModifiedArr.Add(MakeShareable(new FJsonValueString(TEXT("outer_cone_angle"))));
        }
    }

    // Temperature
    double Temperature;
    if (Params->TryGetNumberField(TEXT("temperature"), Temperature))
    {
        LightComp->Temperature = static_cast<float>(Temperature);
        ModifiedArr.Add(MakeShareable(new FJsonValueString(TEXT("temperature"))));
    }

    // Use temperature
    bool bUseTemp;
    if (Params->TryGetBoolField(TEXT("use_temperature"), bUseTemp))
    {
        LightComp->bUseTemperature = bUseTemp;
        ModifiedArr.Add(MakeShareable(new FJsonValueString(TEXT("use_temperature"))));
    }

    // Cast shadows
    bool bCastShadows;
    if (Params->TryGetBoolField(TEXT("cast_shadows"), bCastShadows))
    {
        LightComp->SetCastShadows(bCastShadows);
        ModifiedArr.Add(MakeShareable(new FJsonValueString(TEXT("cast_shadows"))));
    }

    // Indirect lighting intensity
    double IndirectIntensity;
    if (Params->TryGetNumberField(TEXT("indirect_lighting_intensity"), IndirectIntensity))
    {
        LightComp->IndirectLightingIntensity = static_cast<float>(IndirectIntensity);
        ModifiedArr.Add(MakeShareable(new FJsonValueString(TEXT("indirect_lighting_intensity"))));
    }

    // Volumetric scattering intensity
    double VolScatter;
    if (Params->TryGetNumberField(TEXT("volumetric_scattering_intensity"), VolScatter))
    {
        LightComp->VolumetricScatteringIntensity = static_cast<float>(VolScatter);
        ModifiedArr.Add(MakeShareable(new FJsonValueString(TEXT("volumetric_scattering_intensity"))));
    }

    Actor->MarkPackageDirty();

    TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
    Data->SetBoolField(TEXT("success"), true);
    Data->SetStringField(TEXT("actor_path"), Actor->GetPathName());
    Data->SetStringField(TEXT("actor_label"), Actor->GetActorLabel());
    Data->SetNumberField(TEXT("modified_count"), ModifiedArr.Num());
    Data->SetArrayField(TEXT("modified_properties"), ModifiedArr);
    return MakeSuccess(Data);
}

// ─────────────────────────────────────────────────────────────────────────────
// lighting.set_sky_atmosphere
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FNexusLightingHandler::HandleSetSkyAtmosphere(
    const TSharedPtr<FJsonObject>& Params)
{
    UWorld* World = GetEditorWorld();
    if (!World)
    {
        return MakeError(TEXT("NO_WORLD"), TEXT("No editor world available"));
    }

    // Find the SkyAtmosphere actor
    FString ActorPath = GetStringParam(Params, TEXT("actor_path"));
    USkyAtmosphereComponent* AtmComp = nullptr;

    if (!ActorPath.IsEmpty())
    {
        AActor* Actor = FindActorByPathOrLabel(World, ActorPath);
        if (Actor)
        {
            AtmComp = Actor->FindComponentByClass<USkyAtmosphereComponent>();
        }
    }
    else
    {
        // Find the first SkyAtmosphere component in the world
        for (TActorIterator<AActor> It(World); It; ++It)
        {
            AtmComp = It->FindComponentByClass<USkyAtmosphereComponent>();
            if (AtmComp) break;
        }
    }

    if (!AtmComp)
    {
        return MakeError(TEXT("NO_SKY_ATMOSPHERE"),
            TEXT("No SkyAtmosphere component found in the level"));
    }

    TArray<TSharedPtr<FJsonValue>> ModifiedArr;

    // Rayleigh scattering
    FVector RayleighScattering = GetVectorParam(Params, TEXT("rayleigh_scattering"), FVector(-1, -1, -1));
    if (RayleighScattering.X >= 0.0)
    {
        AtmComp->RayleighScattering = FLinearColor(
            static_cast<float>(RayleighScattering.X),
            static_cast<float>(RayleighScattering.Y),
            static_cast<float>(RayleighScattering.Z));
        ModifiedArr.Add(MakeShareable(new FJsonValueString(TEXT("rayleigh_scattering"))));
    }

    // Rayleigh exponential distribution
    double RayleighExpDist;
    if (Params->TryGetNumberField(TEXT("rayleigh_exponential_distribution"), RayleighExpDist))
    {
        AtmComp->RayleighExponentialDistribution = static_cast<float>(RayleighExpDist);
        ModifiedArr.Add(MakeShareable(new FJsonValueString(TEXT("rayleigh_exponential_distribution"))));
    }

    // Mie scattering scale
    double MieScale;
    if (Params->TryGetNumberField(TEXT("mie_scattering_scale"), MieScale))
    {
        AtmComp->MieScatteringScale = static_cast<float>(MieScale);
        ModifiedArr.Add(MakeShareable(new FJsonValueString(TEXT("mie_scattering_scale"))));
    }

    // Mie absorption scale
    double MieAbsScale;
    if (Params->TryGetNumberField(TEXT("mie_absorption_scale"), MieAbsScale))
    {
        AtmComp->MieAbsorptionScale = static_cast<float>(MieAbsScale);
        ModifiedArr.Add(MakeShareable(new FJsonValueString(TEXT("mie_absorption_scale"))));
    }

    // Mie anisotropy
    double MieAniso;
    if (Params->TryGetNumberField(TEXT("mie_anisotropy"), MieAniso))
    {
        AtmComp->MieAnisotropy = FMath::Clamp(static_cast<float>(MieAniso), -1.0f, 1.0f);
        ModifiedArr.Add(MakeShareable(new FJsonValueString(TEXT("mie_anisotropy"))));
    }

    // Ground albedo
    FVector GroundAlbedo = GetVectorParam(Params, TEXT("ground_albedo"), FVector(-1, -1, -1));
    if (GroundAlbedo.X >= 0.0)
    {
        AtmComp->GroundAlbedo = FColor(
            static_cast<uint8>(FMath::Clamp(GroundAlbedo.X, 0.0, 1.0) * 255.0),
            static_cast<uint8>(FMath::Clamp(GroundAlbedo.Y, 0.0, 1.0) * 255.0),
            static_cast<uint8>(FMath::Clamp(GroundAlbedo.Z, 0.0, 1.0) * 255.0));
        ModifiedArr.Add(MakeShareable(new FJsonValueString(TEXT("ground_albedo"))));
    }

    // Atmosphere height
    double AtmHeight;
    if (Params->TryGetNumberField(TEXT("atmosphere_height"), AtmHeight))
    {
        AtmComp->AtmosphereHeight = static_cast<float>(AtmHeight);
        ModifiedArr.Add(MakeShareable(new FJsonValueString(TEXT("atmosphere_height"))));
    }

    // Multi scattering factor
    double MultiScatter;
    if (Params->TryGetNumberField(TEXT("multi_scattering_factor"), MultiScatter))
    {
        AtmComp->MultiScatteringFactor = FMath::Clamp(static_cast<float>(MultiScatter), 0.0f, 1.0f);
        ModifiedArr.Add(MakeShareable(new FJsonValueString(TEXT("multi_scattering_factor"))));
    }

    AtmComp->MarkRenderStateDirty();
    AtmComp->GetOwner()->MarkPackageDirty();

    TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
    Data->SetBoolField(TEXT("success"), true);
    Data->SetStringField(TEXT("message"),
        FString::Printf(TEXT("Updated %d sky atmosphere properties"), ModifiedArr.Num()));
    Data->SetArrayField(TEXT("modified_properties"), ModifiedArr);
    return MakeSuccess(Data);
}

// ─────────────────────────────────────────────────────────────────────────────
// lighting.set_exponential_fog
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FNexusLightingHandler::HandleSetExponentialFog(
    const TSharedPtr<FJsonObject>& Params)
{
    UWorld* World = GetEditorWorld();
    if (!World)
    {
        return MakeError(TEXT("NO_WORLD"), TEXT("No editor world available"));
    }

    FString ActorPath = GetStringParam(Params, TEXT("actor_path"));
    UExponentialHeightFogComponent* FogComp = nullptr;
    AActor* FogOwner = nullptr;

    if (!ActorPath.IsEmpty())
    {
        AActor* Actor = FindActorByPathOrLabel(World, ActorPath);
        if (Actor)
        {
            FogComp = Actor->FindComponentByClass<UExponentialHeightFogComponent>();
            FogOwner = Actor;
        }
    }
    else
    {
        for (TActorIterator<AExponentialHeightFog> It(World); It; ++It)
        {
            FogComp = It->GetComponent();
            FogOwner = *It;
            if (FogComp) break;
        }
    }

    if (!FogComp)
    {
        return MakeError(TEXT("NO_FOG"),
            TEXT("No ExponentialHeightFog found in the level"));
    }

    TArray<TSharedPtr<FJsonValue>> ModifiedArr;

    double FogDensity;
    if (Params->TryGetNumberField(TEXT("fog_density"), FogDensity))
    {
        FogComp->FogDensity = FMath::Clamp(static_cast<float>(FogDensity), 0.0f, 1.0f);
        ModifiedArr.Add(MakeShareable(new FJsonValueString(TEXT("fog_density"))));
    }

    double FogHeightFalloff;
    if (Params->TryGetNumberField(TEXT("fog_height_falloff"), FogHeightFalloff))
    {
        FogComp->FogHeightFalloff = static_cast<float>(FogHeightFalloff);
        ModifiedArr.Add(MakeShareable(new FJsonValueString(TEXT("fog_height_falloff"))));
    }

    double StartDist;
    if (Params->TryGetNumberField(TEXT("start_distance"), StartDist))
    {
        FogComp->StartDistance = static_cast<float>(StartDist);
        ModifiedArr.Add(MakeShareable(new FJsonValueString(TEXT("start_distance"))));
    }

    double CutoffDist;
    if (Params->TryGetNumberField(TEXT("fog_cutoff_distance"), CutoffDist))
    {
        FogComp->FogCutoffDistance = static_cast<float>(CutoffDist);
        ModifiedArr.Add(MakeShareable(new FJsonValueString(TEXT("fog_cutoff_distance"))));
    }

    double FogMaxOpacity;
    if (Params->TryGetNumberField(TEXT("fog_max_opacity"), FogMaxOpacity))
    {
        FogComp->FogMaxOpacity = FMath::Clamp(static_cast<float>(FogMaxOpacity), 0.0f, 1.0f);
        ModifiedArr.Add(MakeShareable(new FJsonValueString(TEXT("fog_max_opacity"))));
    }

    // In-scattering color
    FVector InscatterColor = GetVectorParam(Params, TEXT("inscattering_color"), FVector(-1, -1, -1));
    if (InscatterColor.X >= 0.0)
    {
        FogComp->FogInscatteringLuminance = FLinearColor(
            static_cast<float>(InscatterColor.X),
            static_cast<float>(InscatterColor.Y),
            static_cast<float>(InscatterColor.Z));
        ModifiedArr.Add(MakeShareable(new FJsonValueString(TEXT("inscattering_color"))));
    }

    double DirInscatterExp;
    if (Params->TryGetNumberField(TEXT("directional_inscattering_exponent"), DirInscatterExp))
    {
        FogComp->DirectionalInscatteringExponent = static_cast<float>(DirInscatterExp);
        ModifiedArr.Add(MakeShareable(new FJsonValueString(TEXT("directional_inscattering_exponent"))));
    }

    // Volumetric fog
    bool bVolFog;
    if (Params->TryGetBoolField(TEXT("volumetric_fog"), bVolFog))
    {
        FogComp->bEnableVolumetricFog = bVolFog;
        ModifiedArr.Add(MakeShareable(new FJsonValueString(TEXT("volumetric_fog"))));
    }

    double VolFogScatterDist;
    if (Params->TryGetNumberField(TEXT("volumetric_fog_scattering_distribution"), VolFogScatterDist))
    {
        FogComp->VolumetricFogScatteringDistribution = FMath::Clamp(static_cast<float>(VolFogScatterDist), -1.0f, 1.0f);
        ModifiedArr.Add(MakeShareable(new FJsonValueString(TEXT("volumetric_fog_scattering_distribution"))));
    }

    // Volumetric fog albedo
    FVector VolFogAlbedo = GetVectorParam(Params, TEXT("volumetric_fog_albedo"), FVector(-1, -1, -1));
    if (VolFogAlbedo.X >= 0.0)
    {
        FogComp->VolumetricFogAlbedo = FColor(
            static_cast<uint8>(FMath::Clamp(VolFogAlbedo.X, 0.0, 1.0) * 255.0),
            static_cast<uint8>(FMath::Clamp(VolFogAlbedo.Y, 0.0, 1.0) * 255.0),
            static_cast<uint8>(FMath::Clamp(VolFogAlbedo.Z, 0.0, 1.0) * 255.0));
        ModifiedArr.Add(MakeShareable(new FJsonValueString(TEXT("volumetric_fog_albedo"))));
    }

    FogComp->MarkRenderStateDirty();
    FogOwner->MarkPackageDirty();

    TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
    Data->SetBoolField(TEXT("success"), true);
    Data->SetStringField(TEXT("message"),
        FString::Printf(TEXT("Updated %d fog properties"), ModifiedArr.Num()));
    Data->SetArrayField(TEXT("modified_properties"), ModifiedArr);
    return MakeSuccess(Data);
}

// ─────────────────────────────────────────────────────────────────────────────
// lighting.set_volumetric_clouds
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FNexusLightingHandler::HandleSetVolumetricClouds(
    const TSharedPtr<FJsonObject>& Params)
{
    UWorld* World = GetEditorWorld();
    if (!World)
    {
        return MakeError(TEXT("NO_WORLD"), TEXT("No editor world available"));
    }

    FString ActorPath = GetStringParam(Params, TEXT("actor_path"));
    UVolumetricCloudComponent* CloudComp = nullptr;
    AActor* CloudOwner = nullptr;

    if (!ActorPath.IsEmpty())
    {
        AActor* Actor = FindActorByPathOrLabel(World, ActorPath);
        if (Actor)
        {
            CloudComp = Actor->FindComponentByClass<UVolumetricCloudComponent>();
            CloudOwner = Actor;
        }
    }
    else
    {
        for (TActorIterator<AActor> It(World); It; ++It)
        {
            CloudComp = It->FindComponentByClass<UVolumetricCloudComponent>();
            if (CloudComp)
            {
                CloudOwner = *It;
                break;
            }
        }
    }

    if (!CloudComp)
    {
        return MakeError(TEXT("NO_VOLUMETRIC_CLOUDS"),
            TEXT("No VolumetricCloud component found in the level"));
    }

    TArray<TSharedPtr<FJsonValue>> ModifiedArr;

    double LayerBottom;
    if (Params->TryGetNumberField(TEXT("layer_bottom_altitude"), LayerBottom))
    {
        CloudComp->LayerBottomAltitude = static_cast<float>(LayerBottom);
        ModifiedArr.Add(MakeShareable(new FJsonValueString(TEXT("layer_bottom_altitude"))));
    }

    double LayerHeight;
    if (Params->TryGetNumberField(TEXT("layer_height"), LayerHeight))
    {
        CloudComp->LayerHeight = static_cast<float>(LayerHeight);
        ModifiedArr.Add(MakeShareable(new FJsonValueString(TEXT("layer_height"))));
    }

    double TracingStartMax;
    if (Params->TryGetNumberField(TEXT("tracing_start_max_distance"), TracingStartMax))
    {
        CloudComp->TracingStartMaxDistance = static_cast<float>(TracingStartMax);
        ModifiedArr.Add(MakeShareable(new FJsonValueString(TEXT("tracing_start_max_distance"))));
    }

    double TracingMax;
    if (Params->TryGetNumberField(TEXT("tracing_max_distance"), TracingMax))
    {
        CloudComp->TracingMaxDistance = static_cast<float>(TracingMax);
        ModifiedArr.Add(MakeShareable(new FJsonValueString(TEXT("tracing_max_distance"))));
    }

    // Ground albedo
    FVector GroundAlbedo = GetVectorParam(Params, TEXT("ground_albedo"), FVector(-1, -1, -1));
    if (GroundAlbedo.X >= 0.0)
    {
        CloudComp->GroundAlbedo = FColor(
            static_cast<uint8>(FMath::Clamp(GroundAlbedo.X, 0.0, 1.0) * 255.0),
            static_cast<uint8>(FMath::Clamp(GroundAlbedo.Y, 0.0, 1.0) * 255.0),
            static_cast<uint8>(FMath::Clamp(GroundAlbedo.Z, 0.0, 1.0) * 255.0));
        ModifiedArr.Add(MakeShareable(new FJsonValueString(TEXT("ground_albedo"))));
    }

    // Note: BeerPowderIntensity, BeerPowderDepth, and ShadowReflectionSampleCountScale
    // were deprecated in UE 5.0+ and removed in UE 5.7.
    // These properties are no longer available on UVolumetricCloudComponent.

    bool bPerSampleTransmittance;
    if (Params->TryGetBoolField(TEXT("use_per_sample_atmospheric_light_transmittance"), bPerSampleTransmittance))
    {
        CloudComp->bUsePerSampleAtmosphericLightTransmittance = bPerSampleTransmittance;
        ModifiedArr.Add(MakeShareable(new FJsonValueString(TEXT("use_per_sample_atmospheric_light_transmittance"))));
    }

    CloudComp->MarkRenderStateDirty();
    CloudOwner->MarkPackageDirty();

    TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
    Data->SetBoolField(TEXT("success"), true);
    Data->SetStringField(TEXT("message"),
        FString::Printf(TEXT("Updated %d volumetric cloud properties"), ModifiedArr.Num()));
    Data->SetArrayField(TEXT("modified_properties"), ModifiedArr);
    return MakeSuccess(Data);
}

// ─────────────────────────────────────────────────────────────────────────────
// lighting.configure_global_illumination
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FNexusLightingHandler::HandleConfigureGlobalIllumination(
    const TSharedPtr<FJsonObject>& Params)
{
    FString GIMethod = GetStringParam(Params, TEXT("gi_method"), TEXT("Lumen"));
    int32 SceneLightingQuality = static_cast<int32>(GetNumberParam(Params, TEXT("lumen_scene_lighting_quality"), 1.0));
    int32 SceneDetail = static_cast<int32>(GetNumberParam(Params, TEXT("lumen_scene_detail"), 1.0));
    double SceneViewDistance = GetNumberParam(Params, TEXT("lumen_scene_view_distance"), 20000.0);
    bool bUseHWRT = GetBoolParam(Params, TEXT("use_hardware_raytracing"), false);
    int32 FinalGatherQuality = static_cast<int32>(GetNumberParam(Params, TEXT("final_gather_quality"), 1.0));
    bool bDynamicGI = GetBoolParam(Params, TEXT("dynamic_gi"), true);

    // Map GI method string to CVar value
    // r.DynamicGlobalIlluminationMethod: 0=None, 1=Lumen, 2=SSGI
    int32 GIMethodValue = 1; // Default Lumen
    if (GIMethod == TEXT("None") || GIMethod == TEXT("none"))
    {
        GIMethodValue = 0;
    }
    else if (GIMethod == TEXT("ScreenSpace") || GIMethod == TEXT("SSGI"))
    {
        GIMethodValue = 2;
    }
    else if (GIMethod == TEXT("Lumen") || GIMethod == TEXT("lumen"))
    {
        GIMethodValue = 1;
    }

    // Capture previous settings
    IConsoleVariable* GIMethodCVar = IConsoleManager::Get().FindConsoleVariable(TEXT("r.DynamicGlobalIlluminationMethod"));
    int32 PrevGIMethod = GIMethodCVar ? GIMethodCVar->GetInt() : -1;

    // Apply GI method
    if (GIMethodCVar)
    {
        GIMethodCVar->Set(GIMethodValue, ECVF_SetByCode);
    }

    // Lumen-specific settings
    FString Prev;
    if (GIMethodValue == 1)
    {
        // Lumen scene lighting quality mapped to final gather quality
        float QualityMap[] = { 0.25f, 0.5f, 1.0f, 2.0f };
        int32 ClampedQLQ = FMath::Clamp(SceneLightingQuality, 0, 3);

        IConsoleVariable* FGQCVar = IConsoleManager::Get().FindConsoleVariable(TEXT("r.Lumen.FinalGather.Quality"));
        if (FGQCVar) FGQCVar->Set(QualityMap[ClampedQLQ], ECVF_SetByCode);

        // Scene detail
        float DetailMap[] = { 0.25f, 0.5f, 1.0f, 2.0f };
        int32 ClampedDetail = FMath::Clamp(SceneDetail, 0, 3);
        IConsoleVariable* DetailCVar = IConsoleManager::Get().FindConsoleVariable(TEXT("r.Lumen.Scene.Detail"));
        if (DetailCVar) DetailCVar->Set(DetailMap[ClampedDetail], ECVF_SetByCode);

        // Scene view distance
        IConsoleVariable* ViewDistCVar = IConsoleManager::Get().FindConsoleVariable(TEXT("r.Lumen.Scene.ViewDistance"));
        if (ViewDistCVar) ViewDistCVar->Set(static_cast<float>(SceneViewDistance), ECVF_SetByCode);

        // Hardware ray tracing
        IConsoleVariable* HWRTCVar = IConsoleManager::Get().FindConsoleVariable(TEXT("r.Lumen.HardwareRayTracing"));
        if (HWRTCVar) HWRTCVar->Set(bUseHWRT ? 1 : 0, ECVF_SetByCode);

        // Final gather quality
        float FGMap[] = { 0.25f, 0.5f, 1.0f, 2.0f };
        int32 ClampedFGQ = FMath::Clamp(FinalGatherQuality, 0, 3);
        if (FGQCVar) FGQCVar->Set(FGMap[ClampedFGQ], ECVF_SetByCode);

        // Dynamic GI — controls Lumen scene lighting update speed
        IConsoleVariable* UpdateSpeedCVar = IConsoleManager::Get().FindConsoleVariable(TEXT("r.Lumen.Scene.LightingUpdateSpeed"));
        if (UpdateSpeedCVar)
        {
            UpdateSpeedCVar->Set(bDynamicGI ? 1.0f : 0.0f, ECVF_SetByCode);
        }
    }

    TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
    Data->SetBoolField(TEXT("success"), true);
    Data->SetStringField(TEXT("gi_method"), GIMethod);
    Data->SetNumberField(TEXT("gi_method_value"), GIMethodValue);
    Data->SetNumberField(TEXT("previous_gi_method"), PrevGIMethod);
    Data->SetBoolField(TEXT("use_hardware_raytracing"), bUseHWRT);
    Data->SetBoolField(TEXT("dynamic_gi"), bDynamicGI);
    Data->SetStringField(TEXT("message"),
        FString::Printf(TEXT("Global illumination configured to '%s'"), *GIMethod));
    return MakeSuccess(Data);
}

// ─────────────────────────────────────────────────────────────────────────────
// lighting.bake_lighting
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FNexusLightingHandler::HandleBakeLighting(
    const TSharedPtr<FJsonObject>& Params)
{
    if (!GEditor)
    {
        return MakeError(TEXT("NO_EDITOR"), TEXT("GEditor not available"));
    }

    UWorld* World = GetEditorWorld();
    if (!World)
    {
        return MakeError(TEXT("NO_WORLD"), TEXT("No editor world available"));
    }

    FString Quality = GetStringParam(Params, TEXT("quality"), TEXT("Production"));
    bool bOnlySelected = GetBoolParam(Params, TEXT("only_selected"), false);

    // Map quality string to ELightingBuildQuality
    ELightingBuildQuality BuildQuality = ELightingBuildQuality::Quality_Production;
    if (Quality == TEXT("Preview"))
    {
        BuildQuality = ELightingBuildQuality::Quality_Preview;
    }
    else if (Quality == TEXT("Medium"))
    {
        BuildQuality = ELightingBuildQuality::Quality_Medium;
    }
    else if (Quality == TEXT("High"))
    {
        BuildQuality = ELightingBuildQuality::Quality_High;
    }
    else if (Quality == TEXT("Production"))
    {
        BuildQuality = ELightingBuildQuality::Quality_Production;
    }

    // Set quality level and trigger the build
    FLightingBuildOptions BuildOptions;
    BuildOptions.QualityLevel = BuildQuality;
    BuildOptions.bOnlyBuildSelectedLevels = false;
    // In UE 5.7, the property is bOnlyBuildSelected, not bOnlyBuildSelectedActors
    BuildOptions.bOnlyBuildSelected = bOnlySelected;

    GEditor->BuildLighting(BuildOptions);

    TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
    Data->SetBoolField(TEXT("success"), true);
    Data->SetStringField(TEXT("quality"), Quality);
    Data->SetBoolField(TEXT("only_selected"), bOnlySelected);
    Data->SetStringField(TEXT("message"),
        FString::Printf(TEXT("Lighting build started with '%s' quality"), *Quality));
    return MakeSuccess(Data);
}

// ─────────────────────────────────────────────────────────────────────────────
// lighting.set_shadow_settings
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FNexusLightingHandler::HandleSetShadowSettings(
    const TSharedPtr<FJsonObject>& Params)
{
    FString ActorPath = GetStringParam(Params, TEXT("actor_path"));
    TArray<TSharedPtr<FJsonValue>> ModifiedArr;

    if (!ActorPath.IsEmpty())
    {
        // Per-light shadow settings
        UWorld* World = GetEditorWorld();
        if (!World)
        {
            return MakeError(TEXT("NO_WORLD"), TEXT("No editor world available"));
        }

        AActor* Actor = FindActorByPathOrLabel(World, ActorPath);
        if (!Actor)
        {
            return MakeError(TEXT("ACTOR_NOT_FOUND"),
                FString::Printf(TEXT("Actor '%s' not found"), *ActorPath));
        }

        ULightComponent* LightComp = FindLightComponent(Actor);
        if (!LightComp)
        {
            return MakeError(TEXT("NOT_A_LIGHT"),
                FString::Printf(TEXT("Actor '%s' does not have a light component"), *ActorPath));
        }

        double ShadowResScale;
        if (Params->TryGetNumberField(TEXT("shadow_resolution_scale"), ShadowResScale))
        {
            LightComp->ShadowResolutionScale = static_cast<float>(ShadowResScale);
            ModifiedArr.Add(MakeShareable(new FJsonValueString(TEXT("shadow_resolution_scale"))));
        }

        double ContactShadowLen;
        if (Params->TryGetNumberField(TEXT("contact_shadow_length"), ContactShadowLen))
        {
            LightComp->ContactShadowLength = static_cast<float>(ContactShadowLen);
            ModifiedArr.Add(MakeShareable(new FJsonValueString(TEXT("contact_shadow_length"))));
        }

        // Cascade shadow map count (directional lights only)
        double CascadeCount;
        if (Params->TryGetNumberField(TEXT("cascade_shadow_map_count"), CascadeCount))
        {
            if (UDirectionalLightComponent* DirComp = Cast<UDirectionalLightComponent>(LightComp))
            {
                DirComp->DynamicShadowCascades = FMath::Clamp(static_cast<int32>(CascadeCount), 0, 10);
                ModifiedArr.Add(MakeShareable(new FJsonValueString(TEXT("cascade_shadow_map_count"))));
            }
        }

        // Dynamic shadow distance
        double DynShadowDist;
        if (Params->TryGetNumberField(TEXT("dynamic_shadow_distance"), DynShadowDist))
        {
            if (UDirectionalLightComponent* DirComp = Cast<UDirectionalLightComponent>(LightComp))
            {
                DirComp->DynamicShadowDistanceMovableLight = static_cast<float>(DynShadowDist);
                DirComp->DynamicShadowDistanceStationaryLight = static_cast<float>(DynShadowDist);
                ModifiedArr.Add(MakeShareable(new FJsonValueString(TEXT("dynamic_shadow_distance"))));
            }
        }

        // Shadow bias
        double ShadowBias;
        if (Params->TryGetNumberField(TEXT("shadow_bias"), ShadowBias))
        {
            LightComp->ShadowBias = static_cast<float>(ShadowBias);
            ModifiedArr.Add(MakeShareable(new FJsonValueString(TEXT("shadow_bias"))));
        }

        // Shadow slope bias
        double ShadowSlopeBias;
        if (Params->TryGetNumberField(TEXT("shadow_slope_bias"), ShadowSlopeBias))
        {
            LightComp->ShadowSlopeBias = static_cast<float>(ShadowSlopeBias);
            ModifiedArr.Add(MakeShareable(new FJsonValueString(TEXT("shadow_slope_bias"))));
        }

        // Ray traced shadows
        // In UE 5.7, the property is CastRaytracedShadow (TEnumAsByte), not bCastRaytracedShadow
        bool bRTShadows;
        if (Params->TryGetBoolField(TEXT("enable_ray_traced_shadows"), bRTShadows))
        {
            LightComp->CastRaytracedShadow = bRTShadows ? ECastRayTracedShadow::Enabled : ECastRayTracedShadow::Disabled;
            ModifiedArr.Add(MakeShareable(new FJsonValueString(TEXT("enable_ray_traced_shadows"))));
        }

        // Shadow type (mobility)
        // In UE 5.7, SetMobility is on USceneComponent, not AActor
        FString ShadowType = GetStringParam(Params, TEXT("shadow_type"));
        if (!ShadowType.IsEmpty())
        {
            USceneComponent* RootComp = Actor->GetRootComponent();
            if (RootComp)
            {
                if (ShadowType == TEXT("Dynamic") || ShadowType == TEXT("Movable"))
                {
                    RootComp->SetMobility(EComponentMobility::Movable);
                }
                else if (ShadowType == TEXT("Static"))
                {
                    RootComp->SetMobility(EComponentMobility::Static);
                }
                else if (ShadowType == TEXT("Stationary"))
                {
                    RootComp->SetMobility(EComponentMobility::Stationary);
                }
            }
            ModifiedArr.Add(MakeShareable(new FJsonValueString(TEXT("shadow_type"))));
        }

        Actor->MarkPackageDirty();

        TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
        Data->SetBoolField(TEXT("success"), true);
        Data->SetStringField(TEXT("actor_path"), Actor->GetPathName());
        Data->SetNumberField(TEXT("modified_count"), ModifiedArr.Num());
        Data->SetArrayField(TEXT("modified_properties"), ModifiedArr);
        return MakeSuccess(Data);
    }
    else
    {
        // Global shadow settings via CVars
        FString Prev;

        double ShadowResScale;
        if (Params->TryGetNumberField(TEXT("shadow_resolution_scale"), ShadowResScale))
        {
            IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(TEXT("r.Shadow.RadiusThreshold"));
            if (CVar)
            {
                CVar->Set(static_cast<float>(ShadowResScale), ECVF_SetByCode);
                ModifiedArr.Add(MakeShareable(new FJsonValueString(TEXT("shadow_resolution_scale"))));
            }
        }

        double ContactShadowLen;
        if (Params->TryGetNumberField(TEXT("contact_shadow_length"), ContactShadowLen))
        {
            IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(TEXT("r.ContactShadows"));
            if (CVar)
            {
                CVar->Set(ContactShadowLen > 0.0 ? 1 : 0, ECVF_SetByCode);
                ModifiedArr.Add(MakeShareable(new FJsonValueString(TEXT("contact_shadow_length"))));
            }
        }

        bool bRTShadows;
        if (Params->TryGetBoolField(TEXT("enable_ray_traced_shadows"), bRTShadows))
        {
            IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(TEXT("r.RayTracing.Shadows"));
            if (CVar)
            {
                CVar->Set(bRTShadows ? 1 : 0, ECVF_SetByCode);
                ModifiedArr.Add(MakeShareable(new FJsonValueString(TEXT("enable_ray_traced_shadows"))));
            }
        }

        TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
        Data->SetBoolField(TEXT("success"), true);
        Data->SetStringField(TEXT("scope"), TEXT("global"));
        Data->SetNumberField(TEXT("modified_count"), ModifiedArr.Num());
        Data->SetArrayField(TEXT("modified_properties"), ModifiedArr);
        return MakeSuccess(Data);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// lighting.get_lighting_info
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FNexusLightingHandler::HandleGetLightingInfo(
    const TSharedPtr<FJsonObject>& Params)
{
    UWorld* World = GetEditorWorld();
    if (!World)
    {
        return MakeError(TEXT("NO_WORLD"), TEXT("No editor world available"));
    }

    FString ActorPath = GetStringParam(Params, TEXT("actor_path"));
    bool bIncludeGI = GetBoolParam(Params, TEXT("include_gi_settings"), true);
    bool bIncludeAtmosphere = GetBoolParam(Params, TEXT("include_atmosphere"), true);

    TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());

    if (!ActorPath.IsEmpty())
    {
        // Get info for a specific light actor
        AActor* Actor = FindActorByPathOrLabel(World, ActorPath);
        if (!Actor)
        {
            return MakeError(TEXT("ACTOR_NOT_FOUND"),
                FString::Printf(TEXT("Actor '%s' not found"), *ActorPath));
        }

        ULightComponent* LightComp = FindLightComponent(Actor);
        if (!LightComp)
        {
            return MakeError(TEXT("NOT_A_LIGHT"),
                FString::Printf(TEXT("Actor '%s' does not have a light component"), *ActorPath));
        }

        Data->SetStringField(TEXT("actor_path"), Actor->GetPathName());
        Data->SetStringField(TEXT("actor_label"), Actor->GetActorLabel());
        Data->SetStringField(TEXT("light_class"), LightComp->GetClass()->GetName());
        Data->SetNumberField(TEXT("intensity"), LightComp->Intensity);
        Data->SetBoolField(TEXT("cast_shadows"), LightComp->CastShadows);

        FLinearColor Color = LightComp->GetLightColor();
        TSharedPtr<FJsonObject> ColorObj = MakeShareable(new FJsonObject());
        ColorObj->SetNumberField(TEXT("r"), Color.R);
        ColorObj->SetNumberField(TEXT("g"), Color.G);
        ColorObj->SetNumberField(TEXT("b"), Color.B);
        Data->SetObjectField(TEXT("color"), ColorObj);

        Data->SetNumberField(TEXT("temperature"), LightComp->Temperature);
        Data->SetBoolField(TEXT("use_temperature"), LightComp->bUseTemperature);
        Data->SetNumberField(TEXT("indirect_lighting_intensity"), LightComp->IndirectLightingIntensity);
        Data->SetNumberField(TEXT("volumetric_scattering_intensity"), LightComp->VolumetricScatteringIntensity);

        // Mobility
        FString Mobility;
        switch (Actor->GetRootComponent()->Mobility)
        {
        case EComponentMobility::Static: Mobility = TEXT("Static"); break;
        case EComponentMobility::Stationary: Mobility = TEXT("Stationary"); break;
        case EComponentMobility::Movable: Mobility = TEXT("Movable"); break;
        }
        Data->SetStringField(TEXT("mobility"), Mobility);

        Data->SetObjectField(TEXT("location"), VectorToJson(Actor->GetActorLocation()));
        Data->SetObjectField(TEXT("rotation"), RotatorToJson(Actor->GetActorRotation()));

        // Type-specific properties
        if (UPointLightComponent* PointComp = Cast<UPointLightComponent>(LightComp))
        {
            Data->SetNumberField(TEXT("attenuation_radius"), PointComp->AttenuationRadius);
            Data->SetNumberField(TEXT("source_radius"), PointComp->SourceRadius);
        }
        if (USpotLightComponent* SpotComp = Cast<USpotLightComponent>(LightComp))
        {
            Data->SetNumberField(TEXT("inner_cone_angle"), SpotComp->InnerConeAngle);
            Data->SetNumberField(TEXT("outer_cone_angle"), SpotComp->OuterConeAngle);
        }

        // Shadow settings
        Data->SetNumberField(TEXT("shadow_resolution_scale"), LightComp->ShadowResolutionScale);
        Data->SetNumberField(TEXT("shadow_bias"), LightComp->ShadowBias);
        Data->SetNumberField(TEXT("shadow_slope_bias"), LightComp->ShadowSlopeBias);
        Data->SetNumberField(TEXT("contact_shadow_length"), LightComp->ContactShadowLength);

        return MakeSuccess(Data);
    }

    // Level-wide lighting overview
    TArray<TSharedPtr<FJsonValue>> LightsArr;
    int32 LightCount = 0;
    for (TActorIterator<AActor> It(World); It; ++It)
    {
        ULightComponent* LightComp = It->FindComponentByClass<ULightComponent>();
        if (LightComp)
        {
            TSharedPtr<FJsonObject> LightObj = MakeShareable(new FJsonObject());
            LightObj->SetStringField(TEXT("actor_path"), It->GetPathName());
            LightObj->SetStringField(TEXT("actor_label"), It->GetActorLabel());
            LightObj->SetStringField(TEXT("light_class"), LightComp->GetClass()->GetName());
            LightObj->SetNumberField(TEXT("intensity"), LightComp->Intensity);
            LightObj->SetBoolField(TEXT("cast_shadows"), LightComp->CastShadows);
            LightObj->SetBoolField(TEXT("visible"), LightComp->IsVisible());

            FString Mobility;
            switch (It->GetRootComponent() ? It->GetRootComponent()->Mobility.GetValue() : EComponentMobility::Static)
            {
            case EComponentMobility::Static: Mobility = TEXT("Static"); break;
            case EComponentMobility::Stationary: Mobility = TEXT("Stationary"); break;
            case EComponentMobility::Movable: Mobility = TEXT("Movable"); break;
            }
            LightObj->SetStringField(TEXT("mobility"), Mobility);

            LightsArr.Add(MakeShareable(new FJsonValueObject(LightObj)));
            LightCount++;
        }
    }
    Data->SetArrayField(TEXT("lights"), LightsArr);
    Data->SetNumberField(TEXT("light_count"), LightCount);

    // GI settings
    if (bIncludeGI)
    {
        TSharedPtr<FJsonObject> GIObj = MakeShareable(new FJsonObject());

        IConsoleVariable* GIMethodCVar = IConsoleManager::Get().FindConsoleVariable(TEXT("r.DynamicGlobalIlluminationMethod"));
        int32 GIMethodVal = GIMethodCVar ? GIMethodCVar->GetInt() : -1;
        FString GIMethodName;
        switch (GIMethodVal)
        {
        case 0: GIMethodName = TEXT("None"); break;
        case 1: GIMethodName = TEXT("Lumen"); break;
        case 2: GIMethodName = TEXT("SSGI"); break;
        default: GIMethodName = FString::Printf(TEXT("Unknown(%d)"), GIMethodVal); break;
        }
        GIObj->SetStringField(TEXT("method"), GIMethodName);
        GIObj->SetNumberField(TEXT("method_value"), GIMethodVal);

        IConsoleVariable* HWRTCVar = IConsoleManager::Get().FindConsoleVariable(TEXT("r.Lumen.HardwareRayTracing"));
        if (HWRTCVar)
        {
            GIObj->SetBoolField(TEXT("hardware_ray_tracing"), HWRTCVar->GetInt() > 0);
        }

        IConsoleVariable* FGQCVar = IConsoleManager::Get().FindConsoleVariable(TEXT("r.Lumen.FinalGather.Quality"));
        if (FGQCVar)
        {
            GIObj->SetNumberField(TEXT("final_gather_quality"), FGQCVar->GetFloat());
        }

        IConsoleVariable* ViewDistCVar = IConsoleManager::Get().FindConsoleVariable(TEXT("r.Lumen.Scene.ViewDistance"));
        if (ViewDistCVar)
        {
            GIObj->SetNumberField(TEXT("scene_view_distance"), ViewDistCVar->GetFloat());
        }

        Data->SetObjectField(TEXT("global_illumination"), GIObj);
    }

    // Atmosphere and fog
    if (bIncludeAtmosphere)
    {
        // Sky Atmosphere
        for (TActorIterator<AActor> It(World); It; ++It)
        {
            USkyAtmosphereComponent* AtmComp = It->FindComponentByClass<USkyAtmosphereComponent>();
            if (AtmComp)
            {
                TSharedPtr<FJsonObject> AtmObj = MakeShareable(new FJsonObject());
                AtmObj->SetStringField(TEXT("actor_path"), It->GetPathName());
                AtmObj->SetNumberField(TEXT("atmosphere_height"), AtmComp->AtmosphereHeight);
                AtmObj->SetNumberField(TEXT("mie_anisotropy"), AtmComp->MieAnisotropy);
                AtmObj->SetNumberField(TEXT("multi_scattering_factor"), AtmComp->MultiScatteringFactor);
                Data->SetObjectField(TEXT("sky_atmosphere"), AtmObj);
                break;
            }
        }

        // Exponential Height Fog
        for (TActorIterator<AExponentialHeightFog> It(World); It; ++It)
        {
            UExponentialHeightFogComponent* FogComp = It->GetComponent();
            if (FogComp)
            {
                TSharedPtr<FJsonObject> FogObj = MakeShareable(new FJsonObject());
                FogObj->SetStringField(TEXT("actor_path"), It->GetPathName());
                FogObj->SetNumberField(TEXT("fog_density"), FogComp->FogDensity);
                FogObj->SetNumberField(TEXT("fog_height_falloff"), FogComp->FogHeightFalloff);
                FogObj->SetBoolField(TEXT("volumetric_fog"), FogComp->bEnableVolumetricFog);
                FogObj->SetNumberField(TEXT("start_distance"), FogComp->StartDistance);
                Data->SetObjectField(TEXT("exponential_fog"), FogObj);
                break;
            }
        }

        // Volumetric Clouds
        for (TActorIterator<AActor> It(World); It; ++It)
        {
            UVolumetricCloudComponent* CloudComp = It->FindComponentByClass<UVolumetricCloudComponent>();
            if (CloudComp)
            {
                TSharedPtr<FJsonObject> CloudObj = MakeShareable(new FJsonObject());
                CloudObj->SetStringField(TEXT("actor_path"), It->GetPathName());
                CloudObj->SetNumberField(TEXT("layer_bottom_altitude"), CloudComp->LayerBottomAltitude);
                CloudObj->SetNumberField(TEXT("layer_height"), CloudComp->LayerHeight);
                Data->SetObjectField(TEXT("volumetric_clouds"), CloudObj);
                break;
            }
        }
    }

    return MakeSuccess(Data);
}
