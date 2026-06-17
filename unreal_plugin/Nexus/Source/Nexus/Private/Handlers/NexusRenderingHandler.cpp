// Copyright Nexus Team. All Rights Reserved.

#include "Handlers/NexusRenderingHandler.h"
#include "Editor.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "GameFramework/WorldSettings.h"
#include "Engine/PostProcessVolume.h"
#include "Components/PostProcessComponent.h"
#include "Scalability.h"
#include "EngineUtils.h"

// ─────────────────────────────────────────────────────────────────────────────
// CVar Helpers
// ─────────────────────────────────────────────────────────────────────────────

FString FNexusRenderingHandler::GetCVarString(const TCHAR* CVarName)
{
    IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(CVarName);
    if (CVar)
    {
        return CVar->GetString();
    }
    return TEXT("");
}

int32 FNexusRenderingHandler::GetCVarInt(const TCHAR* CVarName)
{
    IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(CVarName);
    if (CVar)
    {
        return CVar->GetInt();
    }
    return 0;
}

float FNexusRenderingHandler::GetCVarFloat(const TCHAR* CVarName)
{
    IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(CVarName);
    if (CVar)
    {
        return CVar->GetFloat();
    }
    return 0.0f;
}

bool FNexusRenderingHandler::SetCVar(const FString& CVarName, const FString& Value, FString& OutPreviousValue)
{
    IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(*CVarName);
    if (!CVar)
    {
        return false;
    }
    OutPreviousValue = CVar->GetString();
    CVar->Set(*Value, ECVF_SetByCode);
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Dispatch
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FNexusRenderingHandler::Handle(
    const FString& CommandType,
    const TSharedPtr<FJsonObject>& Params)
{
    FString SubCommand = CommandType.RightChop(GetNamespace().Len() + 1);

    if (SubCommand == TEXT("get_rendering_settings"))     return HandleGetRenderingSettings(Params);
    if (SubCommand == TEXT("set_nanite_enabled"))          return HandleSetNaniteEnabled(Params);
    if (SubCommand == TEXT("set_lumen_settings"))          return HandleSetLumenSettings(Params);
    if (SubCommand == TEXT("set_vsm_settings"))            return HandleSetVsmSettings(Params);
    if (SubCommand == TEXT("set_tsr_settings"))            return HandleSetTsrSettings(Params);
    if (SubCommand == TEXT("set_post_process_settings"))   return HandleSetPostProcessSettings(Params);
    if (SubCommand == TEXT("set_console_variable"))        return HandleSetConsoleVariable(Params);
    if (SubCommand == TEXT("get_scalability_settings"))    return HandleGetScalabilitySettings(Params);

    return MakeError(TEXT("NOT_IMPLEMENTED"),
        FString::Printf(TEXT("Command '%s' not yet implemented"), *CommandType));
}

// ─────────────────────────────────────────────────────────────────────────────
// rendering.get_rendering_settings
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FNexusRenderingHandler::HandleGetRenderingSettings(
    const TSharedPtr<FJsonObject>& Params)
{
    TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());

    // Anti-aliasing method: 0=None, 1=FXAA, 2=TAA, 3=MSAA, 4=TSR
    int32 AAMethod = GetCVarInt(TEXT("r.AntiAliasingMethod"));
    FString AAName;
    switch (AAMethod)
    {
    case 0: AAName = TEXT("None"); break;
    case 1: AAName = TEXT("FXAA"); break;
    case 2: AAName = TEXT("TAA"); break;
    case 3: AAName = TEXT("MSAA"); break;
    case 4: AAName = TEXT("TSR"); break;
    default: AAName = FString::Printf(TEXT("Unknown(%d)"), AAMethod); break;
    }
    Data->SetStringField(TEXT("anti_aliasing_method"), AAName);

    // Global illumination method: 0=None, 1=Lumen, 2=SSGI, 3=ScreenSpace, 4=Plugin
    int32 GIMethod = GetCVarInt(TEXT("r.DynamicGlobalIlluminationMethod"));
    FString GIName;
    switch (GIMethod)
    {
    case 0: GIName = TEXT("None"); break;
    case 1: GIName = TEXT("Lumen"); break;
    case 2: GIName = TEXT("SSGI"); break;
    default: GIName = FString::Printf(TEXT("Unknown(%d)"), GIMethod); break;
    }
    Data->SetStringField(TEXT("global_illumination_method"), GIName);

    // Shadow method: 0=ShadowMaps, 1=VirtualShadowMaps
    int32 ShadowMethod = GetCVarInt(TEXT("r.Shadow.Virtual.Enable"));
    Data->SetStringField(TEXT("shadow_method"),
        ShadowMethod > 0 ? TEXT("VirtualShadowMaps") : TEXT("ShadowMaps"));

    // Nanite enabled
    int32 NaniteEnabled = GetCVarInt(TEXT("r.Nanite"));
    Data->SetBoolField(TEXT("nanite_enabled"), NaniteEnabled > 0);

    // Lumen enabled (GI method == 1)
    Data->SetBoolField(TEXT("lumen_enabled"), GIMethod == 1);

    // VSM enabled
    Data->SetBoolField(TEXT("vsm_enabled"), ShadowMethod > 0);

    // Ray tracing enabled
    int32 RTEnabled = GetCVarInt(TEXT("r.RayTracing"));
    Data->SetBoolField(TEXT("ray_tracing_enabled"), RTEnabled > 0);

    // Screen percentage
    float ScreenPct = GetCVarFloat(TEXT("r.ScreenPercentage"));
    Data->SetNumberField(TEXT("screen_percentage"), ScreenPct);

    // Post process quality level
    int32 PPQuality = GetCVarInt(TEXT("sg.PostProcessQuality"));
    Data->SetNumberField(TEXT("post_process_quality"), PPQuality);

    return MakeSuccess(Data);
}

// ─────────────────────────────────────────────────────────────────────────────
// rendering.set_nanite_enabled
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FNexusRenderingHandler::HandleSetNaniteEnabled(
    const TSharedPtr<FJsonObject>& Params)
{
    bool bEnabled = GetBoolParam(Params, TEXT("enabled"), true);

    FString PrevValue;
    bool bSet = SetCVar(TEXT("r.Nanite"), bEnabled ? TEXT("1") : TEXT("0"), PrevValue);
    if (!bSet)
    {
        return MakeError(TEXT("CVAR_NOT_FOUND"),
            TEXT("Console variable 'r.Nanite' not found"));
    }

    TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
    Data->SetBoolField(TEXT("success"), true);
    Data->SetStringField(TEXT("message"),
        FString::Printf(TEXT("Nanite %s"), bEnabled ? TEXT("enabled") : TEXT("disabled")));
    Data->SetBoolField(TEXT("nanite_enabled"), bEnabled);
    Data->SetStringField(TEXT("previous_value"), PrevValue);
    return MakeSuccess(Data);
}

// ─────────────────────────────────────────────────────────────────────────────
// rendering.set_lumen_settings
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FNexusRenderingHandler::HandleSetLumenSettings(
    const TSharedPtr<FJsonObject>& Params)
{
    bool bEnabled = GetBoolParam(Params, TEXT("enabled"), true);
    int32 GIQuality = static_cast<int32>(GetNumberParam(Params, TEXT("gi_quality"), 3.0));
    int32 ReflQuality = static_cast<int32>(GetNumberParam(Params, TEXT("reflection_quality"), 3.0));
    bool bHWRT = GetBoolParam(Params, TEXT("use_hardware_ray_tracing"), false);
    double FinalGatherQuality = GetNumberParam(Params, TEXT("final_gather_quality"), 1.0);
    double SceneLightingSpeed = GetNumberParam(Params, TEXT("scene_lighting_update_speed"), 1.0);

    // Capture previous settings
    TSharedPtr<FJsonObject> PrevSettings = MakeShareable(new FJsonObject());
    PrevSettings->SetNumberField(TEXT("gi_method"), GetCVarInt(TEXT("r.DynamicGlobalIlluminationMethod")));
    PrevSettings->SetNumberField(TEXT("reflection_method"), GetCVarInt(TEXT("r.ReflectionMethod")));
    PrevSettings->SetStringField(TEXT("lumen_hwrt"), GetCVarString(TEXT("r.Lumen.HardwareRayTracing")));
    PrevSettings->SetStringField(TEXT("final_gather_quality"), GetCVarString(TEXT("r.Lumen.FinalGather.Quality")));
    PrevSettings->SetStringField(TEXT("scene_lighting_update_speed"), GetCVarString(TEXT("r.Lumen.Scene.LightingUpdateSpeed")));

    // Apply settings
    FString Prev;

    // Enable/disable Lumen GI (method 1 = Lumen, 0 = None)
    SetCVar(TEXT("r.DynamicGlobalIlluminationMethod"), bEnabled ? TEXT("1") : TEXT("0"), Prev);

    // Enable/disable Lumen reflections (method 1 = Lumen, 0 = None)
    SetCVar(TEXT("r.ReflectionMethod"), bEnabled ? TEXT("1") : TEXT("0"), Prev);

    // Quality CVars (mapped from 0-4 integer levels)
    // Lumen scene detail — maps quality level to a representative float
    float GIQualityValues[] = { 0.25f, 0.5f, 1.0f, 2.0f, 4.0f };
    float ReflQualityValues[] = { 0.25f, 0.5f, 1.0f, 2.0f, 4.0f };

    int32 ClampedGI = FMath::Clamp(GIQuality, 0, 4);
    int32 ClampedRefl = FMath::Clamp(ReflQuality, 0, 4);

    SetCVar(TEXT("r.Lumen.FinalGather.Quality"),
        FString::SanitizeFloat(GIQualityValues[ClampedGI]), Prev);
    SetCVar(TEXT("r.Lumen.Reflections.Quality"),
        FString::SanitizeFloat(ReflQualityValues[ClampedRefl]), Prev);

    // Hardware ray tracing for Lumen
    SetCVar(TEXT("r.Lumen.HardwareRayTracing"), bHWRT ? TEXT("1") : TEXT("0"), Prev);

    // Final gather quality multiplier
    double ClampedFGQ = FMath::Clamp(FinalGatherQuality, 0.25, 4.0);
    SetCVar(TEXT("r.Lumen.FinalGather.Quality"),
        FString::SanitizeFloat(ClampedFGQ), Prev);

    // Scene lighting update speed
    double ClampedSpeed = FMath::Clamp(SceneLightingSpeed, 0.1, 10.0);
    SetCVar(TEXT("r.Lumen.Scene.LightingUpdateSpeed"),
        FString::SanitizeFloat(ClampedSpeed), Prev);

    TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
    Data->SetBoolField(TEXT("success"), true);
    Data->SetStringField(TEXT("message"),
        FString::Printf(TEXT("Lumen settings updated (enabled=%s, gi_quality=%d, reflection_quality=%d)"),
            bEnabled ? TEXT("true") : TEXT("false"), ClampedGI, ClampedRefl));
    Data->SetObjectField(TEXT("previous_settings"), PrevSettings);
    return MakeSuccess(Data);
}

// ─────────────────────────────────────────────────────────────────────────────
// rendering.set_vsm_settings
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FNexusRenderingHandler::HandleSetVsmSettings(
    const TSharedPtr<FJsonObject>& Params)
{
    bool bEnabled = GetBoolParam(Params, TEXT("enabled"), true);
    double ResScale = GetNumberParam(Params, TEXT("resolution_scale"), 1.0);
    int32 PagePoolMB = static_cast<int32>(GetNumberParam(Params, TEXT("page_pool_size_mb"), 512.0));
    bool bForceSinglePage = GetBoolParam(Params, TEXT("force_single_page"), false);
    int32 ClipmapLevels = static_cast<int32>(GetNumberParam(Params, TEXT("clipmap_levels"), 7.0));

    // Capture previous settings
    TSharedPtr<FJsonObject> PrevSettings = MakeShareable(new FJsonObject());
    PrevSettings->SetNumberField(TEXT("enabled"), GetCVarInt(TEXT("r.Shadow.Virtual.Enable")));
    PrevSettings->SetStringField(TEXT("resolution_scale"), GetCVarString(TEXT("r.Shadow.Virtual.ResolutionLodBiasDirectional")));
    PrevSettings->SetStringField(TEXT("page_pool_size_mb"), GetCVarString(TEXT("r.Shadow.Virtual.MaxPhysicalPages")));
    PrevSettings->SetStringField(TEXT("clipmap_levels"), GetCVarString(TEXT("r.Shadow.Virtual.Clipmap.LevelCount")));

    FString Prev;

    // Enable/disable VSM
    SetCVar(TEXT("r.Shadow.Virtual.Enable"), bEnabled ? TEXT("1") : TEXT("0"), Prev);

    // Resolution scale
    double ClampedRes = FMath::Clamp(ResScale, 0.25, 4.0);
    SetCVar(TEXT("r.Shadow.Virtual.ResolutionLodBiasDirectional"),
        FString::SanitizeFloat(ClampedRes), Prev);

    // Page pool size — convert MB to approximate page count (each page ~128KB, so MB * 8 ≈ pages)
    int32 ClampedPool = FMath::Clamp(PagePoolMB, 64, 4096);
    int32 PageCount = ClampedPool * 8;
    SetCVar(TEXT("r.Shadow.Virtual.MaxPhysicalPages"),
        FString::FromInt(PageCount), Prev);

    // Force single page
    SetCVar(TEXT("r.Shadow.Virtual.ForceSinglePage"),
        bForceSinglePage ? TEXT("1") : TEXT("0"), Prev);

    // Clipmap levels
    int32 ClampedLevels = FMath::Clamp(ClipmapLevels, 1, 16);
    SetCVar(TEXT("r.Shadow.Virtual.Clipmap.LevelCount"),
        FString::FromInt(ClampedLevels), Prev);

    TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
    Data->SetBoolField(TEXT("success"), true);
    Data->SetStringField(TEXT("message"),
        FString::Printf(TEXT("VSM settings updated (enabled=%s, resolution_scale=%.2f, clipmap_levels=%d)"),
            bEnabled ? TEXT("true") : TEXT("false"), ClampedRes, ClampedLevels));
    Data->SetObjectField(TEXT("previous_settings"), PrevSettings);
    return MakeSuccess(Data);
}

// ─────────────────────────────────────────────────────────────────────────────
// rendering.set_tsr_settings
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FNexusRenderingHandler::HandleSetTsrSettings(
    const TSharedPtr<FJsonObject>& Params)
{
    bool bEnabled = GetBoolParam(Params, TEXT("enabled"), true);
    double ScreenPct = GetNumberParam(Params, TEXT("screen_percentage"), 100.0);
    int32 Quality = static_cast<int32>(GetNumberParam(Params, TEXT("quality"), 3.0));
    bool bHistoryRejection = GetBoolParam(Params, TEXT("enable_history_rejection"), true);

    // Capture previous settings
    TSharedPtr<FJsonObject> PrevSettings = MakeShareable(new FJsonObject());
    PrevSettings->SetNumberField(TEXT("aa_method"), GetCVarInt(TEXT("r.AntiAliasingMethod")));
    PrevSettings->SetStringField(TEXT("screen_percentage"), GetCVarString(TEXT("r.ScreenPercentage")));
    PrevSettings->SetStringField(TEXT("tsr_quality"), GetCVarString(TEXT("r.TSR.Quality")));
    PrevSettings->SetStringField(TEXT("tsr_history_rejection"), GetCVarString(TEXT("r.TSR.History.ScreenPercentage")));

    FString Prev;

    // Enable TSR = set AA method to 4 (TSR); disable = revert to 2 (TAA)
    SetCVar(TEXT("r.AntiAliasingMethod"), bEnabled ? TEXT("4") : TEXT("2"), Prev);

    // Screen percentage
    double ClampedPct = FMath::Clamp(ScreenPct, 25.0, 100.0);
    SetCVar(TEXT("r.ScreenPercentage"),
        FString::SanitizeFloat(ClampedPct), Prev);

    // TSR quality
    int32 ClampedQuality = FMath::Clamp(Quality, 0, 4);
    SetCVar(TEXT("r.TSR.Quality"),
        FString::FromInt(ClampedQuality), Prev);

    // History rejection
    SetCVar(TEXT("r.TSR.History.ScreenPercentage"),
        bHistoryRejection ? TEXT("1") : TEXT("0"), Prev);

    TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
    Data->SetBoolField(TEXT("success"), true);
    Data->SetStringField(TEXT("message"),
        FString::Printf(TEXT("TSR settings updated (enabled=%s, screen_percentage=%.1f%%, quality=%d)"),
            bEnabled ? TEXT("true") : TEXT("false"), ClampedPct, ClampedQuality));
    Data->SetObjectField(TEXT("previous_settings"), PrevSettings);
    return MakeSuccess(Data);
}

// ─────────────────────────────────────────────────────────────────────────────
// rendering.set_post_process_settings
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FNexusRenderingHandler::HandleSetPostProcessSettings(
    const TSharedPtr<FJsonObject>& Params)
{
    // Find the first unbound post-process volume, or use the world's default
    UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
    if (!World)
    {
        return MakeError(TEXT("NO_WORLD"), TEXT("No editor world available"));
    }

    // Find an existing unbound post-process volume, or the first one
    APostProcessVolume* PPVolume = nullptr;
    for (TActorIterator<APostProcessVolume> It(World); It; ++It)
    {
        if (It->bUnbound)
        {
            PPVolume = *It;
            break;
        }
        if (!PPVolume)
        {
            PPVolume = *It;
        }
    }

    if (!PPVolume)
    {
        return MakeError(TEXT("NO_POST_PROCESS_VOLUME"),
            TEXT("No PostProcessVolume found in the level. Create an unbound PostProcessVolume first."));
    }

    FPostProcessSettings& Settings = PPVolume->Settings;
    TArray<TSharedPtr<FJsonValue>> ModifiedArr;

    // Bloom intensity
    double BloomIntensity = GetNumberParam(Params, TEXT("bloom_intensity"), -1.0);
    if (BloomIntensity >= 0.0)
    {
        Settings.bOverride_BloomIntensity = true;
        Settings.BloomIntensity = FMath::Clamp(static_cast<float>(BloomIntensity), 0.0f, 8.0f);
        ModifiedArr.Add(MakeShareable(new FJsonValueString(TEXT("bloom_intensity"))));
    }

    // Exposure compensation
    double ExposureComp = GetNumberParam(Params, TEXT("exposure_compensation"), -999.0);
    if (ExposureComp > -999.0)
    {
        Settings.bOverride_AutoExposureBias = true;
        Settings.AutoExposureBias = static_cast<float>(ExposureComp);
        ModifiedArr.Add(MakeShareable(new FJsonValueString(TEXT("exposure_compensation"))));
    }

    // Auto-exposure enabled
    bool bAutoExposure = GetBoolParam(Params, TEXT("auto_exposure_enabled"), true);
    Settings.bOverride_AutoExposureMethod = true;
    Settings.AutoExposureMethod = bAutoExposure
        ? EAutoExposureMethod::AEM_Histogram
        : EAutoExposureMethod::AEM_Manual;
    ModifiedArr.Add(MakeShareable(new FJsonValueString(TEXT("auto_exposure_enabled"))));

    // Auto-exposure min brightness
    double AEMinBright = GetNumberParam(Params, TEXT("auto_exposure_min_brightness"), -1.0);
    if (AEMinBright >= 0.0)
    {
        Settings.bOverride_AutoExposureMinBrightness = true;
        Settings.AutoExposureMinBrightness = static_cast<float>(AEMinBright);
        ModifiedArr.Add(MakeShareable(new FJsonValueString(TEXT("auto_exposure_min_brightness"))));
    }

    // Auto-exposure max brightness
    double AEMaxBright = GetNumberParam(Params, TEXT("auto_exposure_max_brightness"), -1.0);
    if (AEMaxBright >= 0.0)
    {
        Settings.bOverride_AutoExposureMaxBrightness = true;
        Settings.AutoExposureMaxBrightness = static_cast<float>(AEMaxBright);
        ModifiedArr.Add(MakeShareable(new FJsonValueString(TEXT("auto_exposure_max_brightness"))));
    }

    // Chromatic aberration
    double CAIntensity = GetNumberParam(Params, TEXT("chromatic_aberration_intensity"), -1.0);
    if (CAIntensity >= 0.0)
    {
        Settings.bOverride_SceneFringeIntensity = true;
        Settings.SceneFringeIntensity = FMath::Clamp(static_cast<float>(CAIntensity), 0.0f, 1.0f);
        ModifiedArr.Add(MakeShareable(new FJsonValueString(TEXT("chromatic_aberration_intensity"))));
    }

    // Vignette
    double VignetteIntensity = GetNumberParam(Params, TEXT("vignette_intensity"), -1.0);
    if (VignetteIntensity >= 0.0)
    {
        Settings.bOverride_VignetteIntensity = true;
        Settings.VignetteIntensity = FMath::Clamp(static_cast<float>(VignetteIntensity), 0.0f, 1.0f);
        ModifiedArr.Add(MakeShareable(new FJsonValueString(TEXT("vignette_intensity"))));
    }

    // Film grain
    double GrainIntensity = GetNumberParam(Params, TEXT("grain_intensity"), -1.0);
    if (GrainIntensity >= 0.0)
    {
        Settings.bOverride_FilmGrainIntensity = true;
        Settings.FilmGrainIntensity = FMath::Clamp(static_cast<float>(GrainIntensity), 0.0f, 1.0f);
        ModifiedArr.Add(MakeShareable(new FJsonValueString(TEXT("grain_intensity"))));
    }

    // Motion blur
    double MotionBlur = GetNumberParam(Params, TEXT("motion_blur_amount"), -1.0);
    if (MotionBlur >= 0.0)
    {
        Settings.bOverride_MotionBlurAmount = true;
        Settings.MotionBlurAmount = FMath::Clamp(static_cast<float>(MotionBlur), 0.0f, 1.0f);
        ModifiedArr.Add(MakeShareable(new FJsonValueString(TEXT("motion_blur_amount"))));
    }

    // Ambient occlusion
    double AOIntensity = GetNumberParam(Params, TEXT("ambient_occlusion_intensity"), -1.0);
    if (AOIntensity >= 0.0)
    {
        Settings.bOverride_AmbientOcclusionIntensity = true;
        Settings.AmbientOcclusionIntensity = FMath::Clamp(static_cast<float>(AOIntensity), 0.0f, 1.0f);
        ModifiedArr.Add(MakeShareable(new FJsonValueString(TEXT("ambient_occlusion_intensity"))));
    }

    PPVolume->MarkPackageDirty();

    TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
    Data->SetBoolField(TEXT("success"), true);
    Data->SetStringField(TEXT("message"),
        FString::Printf(TEXT("Updated %d post-process settings on volume '%s'"),
            ModifiedArr.Num(), *PPVolume->GetActorLabel()));
    Data->SetArrayField(TEXT("modified_settings"), ModifiedArr);
    Data->SetStringField(TEXT("volume_path"), PPVolume->GetPathName());
    Data->SetBoolField(TEXT("volume_unbound"), PPVolume->bUnbound);
    return MakeSuccess(Data);
}

// ─────────────────────────────────────────────────────────────────────────────
// rendering.set_console_variable
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FNexusRenderingHandler::HandleSetConsoleVariable(
    const TSharedPtr<FJsonObject>& Params)
{
    FString CVarName = GetStringParam(Params, TEXT("cvar_name"));
    if (CVarName.IsEmpty())
    {
        return MakeError(TEXT("MISSING_PARAM"), TEXT("cvar_name is required"));
    }

    FString Value = GetStringParam(Params, TEXT("value"));

    FString PrevValue;
    bool bSet = SetCVar(CVarName, Value, PrevValue);
    if (!bSet)
    {
        return MakeError(TEXT("CVAR_NOT_FOUND"),
            FString::Printf(TEXT("Console variable '%s' not found"), *CVarName));
    }

    // Verify the value was set by reading it back
    IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(*CVarName);
    FString NewValue = CVar ? CVar->GetString() : Value;

    TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
    Data->SetBoolField(TEXT("success"), true);
    Data->SetStringField(TEXT("message"),
        FString::Printf(TEXT("Set '%s' = '%s' (was '%s')"), *CVarName, *NewValue, *PrevValue));
    Data->SetStringField(TEXT("previous_value"), PrevValue);
    Data->SetStringField(TEXT("new_value"), NewValue);
    return MakeSuccess(Data);
}

// ─────────────────────────────────────────────────────────────────────────────
// rendering.get_scalability_settings
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FNexusRenderingHandler::HandleGetScalabilitySettings(
    const TSharedPtr<FJsonObject>& Params)
{
    Scalability::FQualityLevels QualityLevels = Scalability::GetQualityLevels();

    TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
    Data->SetNumberField(TEXT("anti_aliasing"), QualityLevels.AntiAliasingQuality);
    Data->SetNumberField(TEXT("effects"), QualityLevels.EffectsQuality);
    Data->SetNumberField(TEXT("foliage"), QualityLevels.FoliageQuality);
    Data->SetNumberField(TEXT("post_processing"), QualityLevels.PostProcessQuality);
    Data->SetNumberField(TEXT("resolution"), QualityLevels.ResolutionQuality);
    Data->SetNumberField(TEXT("shadow"), QualityLevels.ShadowQuality);
    Data->SetNumberField(TEXT("shading"), QualityLevels.ShadingQuality);
    Data->SetNumberField(TEXT("texture"), QualityLevels.TextureQuality);
    Data->SetNumberField(TEXT("view_distance"), QualityLevels.ViewDistanceQuality);
    Data->SetNumberField(TEXT("global_illumination"), QualityLevels.GlobalIlluminationQuality);
    return MakeSuccess(Data);
}
