// Copyright Nexus Team. All Rights Reserved.

#include "Handlers/NexusGameFeaturesHandler.h"
#include "GameFeaturesSubsystem.h"
#include "GameFeatureData.h"
#include "GameFeatureTypes.h"
#include "GameFeatureAction.h"
// GameFeaturePluginStateMachine.h is a private header; types come from GameFeatureTypes.h
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "UObject/SavePackage.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"

// ─────────────────────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────────────────────

FString FNexusGameFeaturesHandler::GameFeatureStateToString(EGameFeaturePluginState State)
{
    switch (State)
    {
    case EGameFeaturePluginState::Uninitialized:       return TEXT("Uninitialized");
    case EGameFeaturePluginState::Terminal:             return TEXT("Terminal");
    case EGameFeaturePluginState::Installed:            return TEXT("Installed");
    case EGameFeaturePluginState::Registered:           return TEXT("Registered");
    case EGameFeaturePluginState::Loaded:               return TEXT("Loaded");
    case EGameFeaturePluginState::Active:               return TEXT("Active");
    default:                                           return TEXT("Unknown");
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Dispatch
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FNexusGameFeaturesHandler::Handle(
    const FString& CommandType,
    const TSharedPtr<FJsonObject>& Params)
{
    FString SubCommand = CommandType.RightChop(GetNamespace().Len() + 1);

    if (SubCommand == TEXT("list_game_features"))       return HandleListGameFeatures(Params);
    if (SubCommand == TEXT("activate_game_feature"))     return HandleActivateGameFeature(Params);
    if (SubCommand == TEXT("deactivate_game_feature"))   return HandleDeactivateGameFeature(Params);
    if (SubCommand == TEXT("create_game_feature"))       return HandleCreateGameFeature(Params);
    if (SubCommand == TEXT("get_game_feature_info"))     return HandleGetGameFeatureInfo(Params);

    return MakeError(TEXT("NOT_IMPLEMENTED"),
        FString::Printf(TEXT("Command '%s' not yet implemented"), *CommandType));
}

// ─────────────────────────────────────────────────────────────────────────────
// gamefeatures.list_game_features
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FNexusGameFeaturesHandler::HandleListGameFeatures(
    const TSharedPtr<FJsonObject>& Params)
{
    UGameFeaturesSubsystem& GFS = UGameFeaturesSubsystem::Get();
    FString FilterState = GetStringParam(Params, TEXT("filter_state"));

    TArray<TSharedPtr<FJsonValue>> FeaturesArr;

    // Use ForEachGameFeature to iterate over all known Game Feature plugins
    GFS.ForEachGameFeature([&](FGameFeatureInfo&& Info)
    {
        FString StateStr = GameFeatureStateToString(Info.CurrentState);

        // Apply filter if specified
        if (!FilterState.IsEmpty() && StateStr != FilterState)
        {
            return;
        }

        TSharedPtr<FJsonObject> FeatureObj = MakeShareable(new FJsonObject());
        FeatureObj->SetStringField(TEXT("name"), Info.Name);
        FeatureObj->SetStringField(TEXT("state"), StateStr);
        FeatureObj->SetStringField(TEXT("plugin_url"), Info.URL);
        FeatureObj->SetBoolField(TEXT("loaded_as_built_in"), Info.bLoadedAsBuiltIn);

        // Try to get the GameFeatureData for additional info
        const UGameFeatureData* FeatureData = GFS.GetGameFeatureDataForActivePluginByURL(Info.URL);

        if (FeatureData)
        {
            // List actions
            TArray<TSharedPtr<FJsonValue>> ActionsArr;
            for (const UGameFeatureAction* Action : FeatureData->GetActions())
            {
                if (Action)
                {
                    TSharedPtr<FJsonValue> ActionVal = MakeShareable(
                        new FJsonValueString(Action->GetClass()->GetName()));
                    ActionsArr.Add(ActionVal);
                }
            }
            FeatureObj->SetArrayField(TEXT("actions"), ActionsArr);
        }

        FeaturesArr.Add(MakeShareable(new FJsonValueObject(FeatureObj)));
    });

    TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
    Data->SetArrayField(TEXT("features"), FeaturesArr);
    Data->SetNumberField(TEXT("count"), FeaturesArr.Num());
    if (!FilterState.IsEmpty())
    {
        Data->SetStringField(TEXT("filter_state"), FilterState);
    }
    return MakeSuccess(Data);
}

// ─────────────────────────────────────────────────────────────────────────────
// gamefeatures.activate_game_feature
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FNexusGameFeaturesHandler::HandleActivateGameFeature(
    const TSharedPtr<FJsonObject>& Params)
{
    FString FeatureName = GetStringParam(Params, TEXT("feature_name"));
    if (FeatureName.IsEmpty())
    {
        return MakeError(TEXT("MISSING_PARAM"), TEXT("feature_name is required"));
    }

    UGameFeaturesSubsystem& GFS = UGameFeaturesSubsystem::Get();

    // Resolve plugin name to URL
    FString PluginURL;
    if (!GFS.GetPluginURLByName(FeatureName, PluginURL))
    {
        return MakeError(TEXT("FEATURE_NOT_FOUND"),
            FString::Printf(TEXT("Game Feature '%s' not found"), *FeatureName));
    }

    // Request activation — this is asynchronous in UE, but we issue the request
    // and report it was initiated. The actual state transition happens over frames.
    FGameFeaturePluginLoadComplete OnComplete;
    GFS.LoadAndActivateGameFeaturePlugin(
        PluginURL,
        OnComplete);

    TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
    Data->SetStringField(TEXT("feature_name"), FeatureName);
    Data->SetStringField(TEXT("plugin_url"), PluginURL);
    Data->SetStringField(TEXT("status"), TEXT("activation_requested"));
    return MakeSuccess(Data);
}

// ─────────────────────────────────────────────────────────────────────────────
// gamefeatures.deactivate_game_feature
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FNexusGameFeaturesHandler::HandleDeactivateGameFeature(
    const TSharedPtr<FJsonObject>& Params)
{
    FString FeatureName = GetStringParam(Params, TEXT("feature_name"));
    if (FeatureName.IsEmpty())
    {
        return MakeError(TEXT("MISSING_PARAM"), TEXT("feature_name is required"));
    }

    UGameFeaturesSubsystem& GFS = UGameFeaturesSubsystem::Get();

    // Resolve plugin name to URL
    FString PluginURL;
    if (!GFS.GetPluginURLByName(FeatureName, PluginURL))
    {
        return MakeError(TEXT("FEATURE_NOT_FOUND"),
            FString::Printf(TEXT("Game Feature '%s' not found"), *FeatureName));
    }

    // Request deactivation — transitions Active -> Loaded -> Registered
    FGameFeaturePluginDeactivateComplete OnComplete;
    GFS.DeactivateGameFeaturePlugin(
        PluginURL,
        OnComplete);

    TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
    Data->SetStringField(TEXT("feature_name"), FeatureName);
    Data->SetStringField(TEXT("plugin_url"), PluginURL);
    Data->SetStringField(TEXT("status"), TEXT("deactivation_requested"));
    return MakeSuccess(Data);
}

// ─────────────────────────────────────────────────────────────────────────────
// gamefeatures.create_game_feature
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FNexusGameFeaturesHandler::HandleCreateGameFeature(
    const TSharedPtr<FJsonObject>& Params)
{
    FString FeatureName = GetStringParam(Params, TEXT("feature_name"));
    FString InitialState = GetStringParam(Params, TEXT("initial_state"), TEXT("Registered"));

    if (FeatureName.IsEmpty())
    {
        return MakeError(TEXT("MISSING_PARAM"), TEXT("feature_name is required"));
    }

    // Validate feature name — alphanumeric + underscores only
    for (TCHAR Ch : FeatureName)
    {
        if (!FChar::IsAlnum(Ch) && Ch != TEXT('_'))
        {
            return MakeError(TEXT("INVALID_NAME"),
                FString::Printf(TEXT("Feature name '%s' contains invalid characters. "
                    "Use alphanumeric and underscores only."), *FeatureName));
        }
    }

    // Build the plugin directory path under the project's Plugins/GameFeatures directory
    FString ProjectDir = FPaths::ProjectDir();
    FString PluginDir = ProjectDir / TEXT("Plugins") / TEXT("GameFeatures") / FeatureName;

    // Check if already exists
    if (IFileManager::Get().DirectoryExists(*PluginDir))
    {
        return MakeError(TEXT("ALREADY_EXISTS"),
            FString::Printf(TEXT("Game Feature '%s' already exists at: %s"),
                *FeatureName, *PluginDir));
    }

    // Create directory structure
    IFileManager::Get().MakeDirectory(*(PluginDir / TEXT("Content")), true);
    IFileManager::Get().MakeDirectory(*(PluginDir / TEXT("Source") / FeatureName / TEXT("Private")), true);
    IFileManager::Get().MakeDirectory(*(PluginDir / TEXT("Source") / FeatureName / TEXT("Public")), true);

    // Create .uplugin file
    FString UPluginPath = PluginDir / FeatureName + TEXT(".uplugin");

    // Build dependencies JSON array
    TArray<TSharedPtr<FJsonValue>> DepsArr;
    const TArray<TSharedPtr<FJsonValue>>* DepsParam;
    if (Params.IsValid() && Params->TryGetArrayField(TEXT("dependencies"), DepsParam))
    {
        for (const auto& DepVal : *DepsParam)
        {
            FString DepName;
            if (DepVal->TryGetString(DepName) && !DepName.IsEmpty())
            {
                DepsArr.Add(MakeShareable(new FJsonValueString(DepName)));
            }
        }
    }

    FString PluginContent = FString::Printf(TEXT(
        "{\n"
        "    \"FileVersion\": 3,\n"
        "    \"Version\": 1,\n"
        "    \"VersionName\": \"1.0\",\n"
        "    \"FriendlyName\": \"%s\",\n"
        "    \"Description\": \"Game Feature: %s\",\n"
        "    \"Category\": \"Game Features\",\n"
        "    \"CreatedBy\": \"Nexus\",\n"
        "    \"BuiltInInitialFeatureState\": \"%s\",\n"
        "    \"ExplicitlyLoaded\": true,\n"
        "    \"Plugins\": [\n"
        "        {\n"
        "            \"Name\": \"GameFeatures\",\n"
        "            \"Enabled\": true\n"
        "        }\n"
        "    ]\n"
        "}\n"),
        *FeatureName, *FeatureName, *InitialState);

    if (!FFileHelper::SaveStringToFile(PluginContent, *UPluginPath))
    {
        return MakeError(TEXT("WRITE_FAILED"),
            FString::Printf(TEXT("Failed to write .uplugin file at: %s"), *UPluginPath));
    }

    // Create a GameFeatureData asset
    FString AssetPath = FString::Printf(TEXT("/Plugins/GameFeatures/%s/%s"), *FeatureName, *FeatureName);
    FString PackagePath = FString::Printf(TEXT("/Game/Plugins/GameFeatures/%s"), *FeatureName);

    // Create the package and data asset
    FString PackageName = FString::Printf(TEXT("/Game/Plugins/GameFeatures/%s/%s"),
        *FeatureName, *FeatureName);
    UPackage* Package = CreatePackage(*PackageName);
    if (!Package)
    {
        return MakeError(TEXT("PACKAGE_FAILED"),
            FString::Printf(TEXT("Failed to create package for '%s'"), *FeatureName));
    }

    UGameFeatureData* FeatureData = NewObject<UGameFeatureData>(
        Package, *FeatureName, RF_Public | RF_Standalone);

    if (!FeatureData)
    {
        return MakeError(TEXT("ASSET_FAILED"),
            FString::Printf(TEXT("Failed to create GameFeatureData for '%s'"), *FeatureName));
    }

    // Add requested actions
    TArray<TSharedPtr<FJsonValue>> ActionsCreated;
    const TArray<TSharedPtr<FJsonValue>>* ActionsParam;
    if (Params.IsValid() && Params->TryGetArrayField(TEXT("actions"), ActionsParam))
    {
        for (const auto& ActionVal : *ActionsParam)
        {
            FString ActionClassName;
            if (ActionVal->TryGetString(ActionClassName) && !ActionClassName.IsEmpty())
            {
                // Try to find the action class
                FString FullClassName = FString::Printf(TEXT("GameFeatureAction_%s"),
                    *ActionClassName);
                UClass* ActionClass = FindObject<UClass>(
                    nullptr, *FullClassName);

                if (!ActionClass)
                {
                    // Try with UGameFeatureAction_ prefix
                    FullClassName = FString::Printf(TEXT("UGameFeatureAction_%s"),
                        *ActionClassName);
                    ActionClass = FindObject<UClass>(nullptr, *FullClassName);
                }

                if (ActionClass && ActionClass->IsChildOf(UGameFeatureAction::StaticClass()))
                {
                    UGameFeatureAction* NewAction = NewObject<UGameFeatureAction>(
                        FeatureData, ActionClass);
                    if (NewAction)
                    {
#if WITH_EDITOR
                        FeatureData->GetMutableActionsInEditor().Add(NewAction);
#endif
                        ActionsCreated.Add(MakeShareable(
                            new FJsonValueString(ActionClassName)));
                    }
                }
            }
        }
    }

    // Mark the package dirty for save
    FeatureData->MarkPackageDirty();
    Package->MarkPackageDirty();

    // Save the asset
    FString FilePath = FPackageName::LongPackageNameToFilename(
        PackageName, FPackageName::GetAssetPackageExtension());
    FSavePackageArgs SaveArgs;
    SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
    UPackage::SavePackage(Package, FeatureData, *FilePath, SaveArgs);

    TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
    Data->SetStringField(TEXT("feature_name"), FeatureName);
    Data->SetStringField(TEXT("plugin_dir"), PluginDir);
    Data->SetStringField(TEXT("uplugin_path"), UPluginPath);
    Data->SetStringField(TEXT("asset_path"), PackageName);
    Data->SetStringField(TEXT("initial_state"), InitialState);
    Data->SetArrayField(TEXT("actions_created"), ActionsCreated);
    Data->SetArrayField(TEXT("dependencies"), DepsArr);
    return MakeSuccess(Data);
}

// ─────────────────────────────────────────────────────────────────────────────
// gamefeatures.get_game_feature_info
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FNexusGameFeaturesHandler::HandleGetGameFeatureInfo(
    const TSharedPtr<FJsonObject>& Params)
{
    FString FeatureName = GetStringParam(Params, TEXT("feature_name"));
    if (FeatureName.IsEmpty())
    {
        return MakeError(TEXT("MISSING_PARAM"), TEXT("feature_name is required"));
    }

    UGameFeaturesSubsystem& GFS = UGameFeaturesSubsystem::Get();

    // Resolve plugin name to URL
    FString PluginURL;
    if (!GFS.GetPluginURLByName(FeatureName, PluginURL))
    {
        return MakeError(TEXT("FEATURE_NOT_FOUND"),
            FString::Printf(TEXT("Game Feature '%s' not found"), *FeatureName));
    }

    TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
    Data->SetStringField(TEXT("feature_name"), FeatureName);
    Data->SetStringField(TEXT("plugin_url"), PluginURL);

    // Get current state using GetPluginState API
    EGameFeaturePluginState CurrentState = GFS.GetPluginState(PluginURL);
    Data->SetStringField(TEXT("state"), GameFeatureStateToString(CurrentState));

    // Try to get the GameFeatureData for detailed info
    const UGameFeatureData* FeatureData = GFS.GetGameFeatureDataForActivePluginByURL(PluginURL);

    if (FeatureData)
    {
        Data->SetBoolField(TEXT("has_data_asset"), true);
        Data->SetStringField(TEXT("data_asset_path"), FeatureData->GetPathName());

        // List actions
        TArray<TSharedPtr<FJsonValue>> ActionsArr;
        for (const UGameFeatureAction* Action : FeatureData->GetActions())
        {
            if (Action)
            {
                TSharedPtr<FJsonObject> ActionObj = MakeShareable(new FJsonObject());
                ActionObj->SetStringField(TEXT("class"), Action->GetClass()->GetName());
                ActionObj->SetStringField(TEXT("path"), Action->GetPathName());
                ActionsArr.Add(MakeShareable(new FJsonValueObject(ActionObj)));
            }
        }
        Data->SetArrayField(TEXT("actions"), ActionsArr);
        Data->SetNumberField(TEXT("action_count"), ActionsArr.Num());
    }
    else
    {
        Data->SetBoolField(TEXT("has_data_asset"), false);
    }

    // Plugin directory info
    FString PluginPath = FPaths::GetPath(PluginURL);
    if (!PluginPath.IsEmpty())
    {
        Data->SetStringField(TEXT("plugin_dir"), PluginPath);

        FString ContentDir = PluginPath / TEXT("Content");
        Data->SetBoolField(TEXT("has_content_dir"),
            IFileManager::Get().DirectoryExists(*ContentDir));
    }

    // Check if this is a project-level plugin (user-created) vs engine
    bool bIsProjectPlugin = PluginURL.Contains(FPaths::ProjectDir())
        || PluginURL.Contains(TEXT("/GameFeatures/"));
    Data->SetBoolField(TEXT("is_project_plugin"), bIsProjectPlugin);

    return MakeSuccess(Data);
}
