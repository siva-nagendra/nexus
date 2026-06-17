// Copyright Nexus Team. All Rights Reserved.

#include "Handlers/NexusEnhancedInputHandler.h"
#include "Editor.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "InputTriggers.h"
#include "InputModifiers.h"
#include "EnhancedInputSubsystems.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "UObject/SavePackage.h"
#include "Factories/DataAssetFactory.h"
#include "InputActionValue.h"

// ─────────────────────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────────────────────

static UWorld* GetEditorWorld()
{
    if (GEditor)
    {
        return GEditor->GetEditorWorldContext().World();
    }
    return nullptr;
}

UInputAction* FNexusEnhancedInputHandler::FindInputActionByName(const FString& ActionName)
{
    if (ActionName.IsEmpty()) return nullptr;

    FAssetRegistryModule& AssetRegistryModule =
        FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
    IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();

    TArray<FAssetData> AssetDataList;
    AssetRegistry.GetAssetsByClass(UInputAction::StaticClass()->GetClassPathName(), AssetDataList);

    for (const FAssetData& AssetData : AssetDataList)
    {
        if (AssetData.AssetName.ToString() == ActionName)
        {
            return Cast<UInputAction>(AssetData.GetAsset());
        }
    }

    // Also try partial/path match
    for (const FAssetData& AssetData : AssetDataList)
    {
        if (AssetData.GetObjectPathString().Contains(ActionName))
        {
            return Cast<UInputAction>(AssetData.GetAsset());
        }
    }

    return nullptr;
}

UInputMappingContext* FNexusEnhancedInputHandler::FindMappingContextByName(
    const FString& ContextName)
{
    if (ContextName.IsEmpty()) return nullptr;

    FAssetRegistryModule& AssetRegistryModule =
        FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
    IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();

    TArray<FAssetData> AssetDataList;
    AssetRegistry.GetAssetsByClass(
        UInputMappingContext::StaticClass()->GetClassPathName(), AssetDataList);

    for (const FAssetData& AssetData : AssetDataList)
    {
        if (AssetData.AssetName.ToString() == ContextName)
        {
            return Cast<UInputMappingContext>(AssetData.GetAsset());
        }
    }

    for (const FAssetData& AssetData : AssetDataList)
    {
        if (AssetData.GetObjectPathString().Contains(ContextName))
        {
            return Cast<UInputMappingContext>(AssetData.GetAsset());
        }
    }

    return nullptr;
}

FEnhancedActionKeyMapping* FNexusEnhancedInputHandler::FindMappingEntry(
    UInputMappingContext* Context,
    UInputAction* Action,
    const FString& KeyName)
{
    if (!Context || !Action) return nullptr;

    TArray<FEnhancedActionKeyMapping>& Mappings = const_cast<TArray<FEnhancedActionKeyMapping>&>(
        Context->GetMappings());

    for (FEnhancedActionKeyMapping& Mapping : Mappings)
    {
        if (Mapping.Action == Action)
        {
            if (KeyName.IsEmpty() || Mapping.Key.GetFName().ToString() == KeyName)
            {
                return &Mapping;
            }
        }
    }

    return nullptr;
}

// ─────────────────────────────────────────────────────────────────────────────
// Dispatch
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FNexusEnhancedInputHandler::Handle(
    const FString& CommandType,
    const TSharedPtr<FJsonObject>& Params)
{
    FString SubCommand = CommandType.RightChop(GetNamespace().Len() + 1);

    if (SubCommand == TEXT("create_input_action"))   return HandleCreateInputAction(Params);
    if (SubCommand == TEXT("create_mapping_context")) return HandleCreateMappingContext(Params);
    if (SubCommand == TEXT("add_action_mapping"))     return HandleAddActionMapping(Params);
    if (SubCommand == TEXT("set_trigger"))            return HandleSetTrigger(Params);
    if (SubCommand == TEXT("set_modifier"))           return HandleSetModifier(Params);
    if (SubCommand == TEXT("list_input_actions"))     return HandleListInputActions(Params);

    return MakeError(TEXT("NOT_IMPLEMENTED"),
        FString::Printf(TEXT("Command '%s' not yet implemented"), *CommandType));
}

// ─────────────────────────────────────────────────────────────────────────────
// input.create_input_action
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FNexusEnhancedInputHandler::HandleCreateInputAction(
    const TSharedPtr<FJsonObject>& Params)
{
    FString ActionName = GetStringParam(Params, TEXT("action_name"));
    if (ActionName.IsEmpty())
    {
        return MakeError(TEXT("MISSING_PARAM"), TEXT("action_name is required"));
    }

    FString ValueTypeStr = GetStringParam(Params, TEXT("value_type"), TEXT("Bool"));
    FString Description = GetStringParam(Params, TEXT("description"));
    bool bConsumeInput = GetBoolParam(Params, TEXT("consume_input"), true);
    bool bSaveAsset = GetBoolParam(Params, TEXT("save_asset"), true);

    // Parse value type
    EInputActionValueType ValueType = EInputActionValueType::Boolean;
    if (ValueTypeStr == TEXT("Axis1D"))
    {
        ValueType = EInputActionValueType::Axis1D;
    }
    else if (ValueTypeStr == TEXT("Axis2D"))
    {
        ValueType = EInputActionValueType::Axis2D;
    }
    else if (ValueTypeStr == TEXT("Axis3D"))
    {
        ValueType = EInputActionValueType::Axis3D;
    }
    else if (ValueTypeStr != TEXT("Bool") && ValueTypeStr != TEXT("Boolean"))
    {
        return MakeError(TEXT("INVALID_VALUE_TYPE"),
            FString::Printf(TEXT("Invalid value type '%s'. Use: Bool, Axis1D, Axis2D, Axis3D"),
                *ValueTypeStr));
    }

    // Check if asset already exists
    UInputAction* Existing = FindInputActionByName(ActionName);
    if (Existing)
    {
        // Update existing action
        Existing->ValueType = ValueType;
        Existing->bConsumeInput = bConsumeInput;
        if (!Description.IsEmpty())
        {
            Existing->ActionDescription = FText::FromString(Description);
        }

        if (bSaveAsset)
        {
            Existing->MarkPackageDirty();
        }

        TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
        Data->SetStringField(TEXT("action_name"), ActionName);
        Data->SetStringField(TEXT("asset_path"), Existing->GetPathName());
        Data->SetStringField(TEXT("value_type"), ValueTypeStr);
        Data->SetBoolField(TEXT("consume_input"), bConsumeInput);
        Data->SetBoolField(TEXT("updated_existing"), true);
        return MakeSuccess(Data);
    }

    // Create new input action asset via AssetTools
    IAssetTools& AssetTools =
        FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();

    FString PackagePath = TEXT("/Game/Input");
    FString AssetName = ActionName;

    UDataAssetFactory* Factory = NewObject<UDataAssetFactory>();

    UObject* NewAsset = AssetTools.CreateAsset(
        AssetName, PackagePath,
        UInputAction::StaticClass(), Factory);

    UInputAction* NewAction = Cast<UInputAction>(NewAsset);
    if (!NewAction)
    {
        return MakeError(TEXT("CREATE_FAILED"),
            FString::Printf(TEXT("Failed to create input action asset '%s'"), *ActionName));
    }

    NewAction->ValueType = ValueType;
    NewAction->bConsumeInput = bConsumeInput;
    if (!Description.IsEmpty())
    {
        NewAction->ActionDescription = FText::FromString(Description);
    }

    if (bSaveAsset)
    {
        NewAction->MarkPackageDirty();
        UPackage* Package = NewAction->GetOutermost();
        if (Package)
        {
            FString PackageFileName = FPackageName::LongPackageNameToFilename(
                Package->GetName(), FPackageName::GetAssetPackageExtension());
            FSavePackageArgs SaveArgs;
            SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
            UPackage::SavePackage(Package, NewAction, *PackageFileName, SaveArgs);
        }
    }

    TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
    Data->SetStringField(TEXT("action_name"), ActionName);
    Data->SetStringField(TEXT("asset_path"), NewAction->GetPathName());
    Data->SetStringField(TEXT("value_type"), ValueTypeStr);
    Data->SetBoolField(TEXT("consume_input"), bConsumeInput);
    Data->SetBoolField(TEXT("updated_existing"), false);
    return MakeSuccess(Data);
}

// ─────────────────────────────────────────────────────────────────────────────
// input.create_mapping_context
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FNexusEnhancedInputHandler::HandleCreateMappingContext(
    const TSharedPtr<FJsonObject>& Params)
{
    FString ContextName = GetStringParam(Params, TEXT("context_name"));
    if (ContextName.IsEmpty())
    {
        return MakeError(TEXT("MISSING_PARAM"), TEXT("context_name is required"));
    }

    FString Description = GetStringParam(Params, TEXT("description"));
    bool bSaveAsset = GetBoolParam(Params, TEXT("save_asset"), true);

    // Check if asset already exists
    UInputMappingContext* Existing = FindMappingContextByName(ContextName);
    if (Existing)
    {
        if (!Description.IsEmpty())
        {
            Existing->ContextDescription = FText::FromString(Description);
        }

        if (bSaveAsset)
        {
            Existing->MarkPackageDirty();
        }

        TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
        Data->SetStringField(TEXT("context_name"), ContextName);
        Data->SetStringField(TEXT("asset_path"), Existing->GetPathName());
        Data->SetNumberField(TEXT("mapping_count"), Existing->GetMappings().Num());
        Data->SetBoolField(TEXT("updated_existing"), true);
        return MakeSuccess(Data);
    }

    // Create new mapping context asset
    IAssetTools& AssetTools =
        FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();

    FString PackagePath = TEXT("/Game/Input");
    UDataAssetFactory* Factory = NewObject<UDataAssetFactory>();

    UObject* NewAsset = AssetTools.CreateAsset(
        ContextName, PackagePath,
        UInputMappingContext::StaticClass(), Factory);

    UInputMappingContext* NewContext = Cast<UInputMappingContext>(NewAsset);
    if (!NewContext)
    {
        return MakeError(TEXT("CREATE_FAILED"),
            FString::Printf(TEXT("Failed to create mapping context asset '%s'"), *ContextName));
    }

    if (!Description.IsEmpty())
    {
        NewContext->ContextDescription = FText::FromString(Description);
    }

    if (bSaveAsset)
    {
        NewContext->MarkPackageDirty();
        UPackage* Package = NewContext->GetOutermost();
        if (Package)
        {
            FString PackageFileName = FPackageName::LongPackageNameToFilename(
                Package->GetName(), FPackageName::GetAssetPackageExtension());
            FSavePackageArgs SaveArgs;
            SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
            UPackage::SavePackage(Package, NewContext, *PackageFileName, SaveArgs);
        }
    }

    TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
    Data->SetStringField(TEXT("context_name"), ContextName);
    Data->SetStringField(TEXT("asset_path"), NewContext->GetPathName());
    Data->SetNumberField(TEXT("mapping_count"), 0);
    Data->SetBoolField(TEXT("updated_existing"), false);
    return MakeSuccess(Data);
}

// ─────────────────────────────────────────────────────────────────────────────
// input.add_action_mapping
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FNexusEnhancedInputHandler::HandleAddActionMapping(
    const TSharedPtr<FJsonObject>& Params)
{
    FString ContextName = GetStringParam(Params, TEXT("context_name"));
    FString ActionName = GetStringParam(Params, TEXT("action_name"));
    FString KeyStr = GetStringParam(Params, TEXT("key"));
    bool bIsGamepad = GetBoolParam(Params, TEXT("is_gamepad"), false);

    if (ContextName.IsEmpty())
    {
        return MakeError(TEXT("MISSING_PARAM"), TEXT("context_name is required"));
    }
    if (ActionName.IsEmpty())
    {
        return MakeError(TEXT("MISSING_PARAM"), TEXT("action_name is required"));
    }
    if (KeyStr.IsEmpty())
    {
        return MakeError(TEXT("MISSING_PARAM"), TEXT("key is required"));
    }

    // Find the mapping context
    UInputMappingContext* Context = FindMappingContextByName(ContextName);
    if (!Context)
    {
        return MakeError(TEXT("CONTEXT_NOT_FOUND"),
            FString::Printf(TEXT("Mapping context '%s' not found"), *ContextName));
    }

    // Find the input action
    UInputAction* Action = FindInputActionByName(ActionName);
    if (!Action)
    {
        return MakeError(TEXT("ACTION_NOT_FOUND"),
            FString::Printf(TEXT("Input action '%s' not found"), *ActionName));
    }

    // Resolve the key name to FKey
    FKey Key(*KeyStr);
    if (!Key.IsValid())
    {
        return MakeError(TEXT("INVALID_KEY"),
            FString::Printf(TEXT("Key '%s' is not a valid Unreal key name"), *KeyStr));
    }

    // Add the mapping entry
    FEnhancedActionKeyMapping& NewMapping = Context->MapKey(Action, Key);

    // Note: In UE 5.7, player mappability is controlled through PlayerMappableKeySettings
    // and SettingBehavior enum (which is protected). For gamepad mapping purposes,
    // no additional flags need to be set as the key itself determines gamepad usage.

    Context->MarkPackageDirty();

    TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
    Data->SetStringField(TEXT("context_name"), ContextName);
    Data->SetStringField(TEXT("action_name"), ActionName);
    Data->SetStringField(TEXT("key"), Key.GetFName().ToString());
    Data->SetBoolField(TEXT("is_gamepad"), bIsGamepad);
    Data->SetNumberField(TEXT("total_mappings"), Context->GetMappings().Num());
    return MakeSuccess(Data);
}

// ─────────────────────────────────────────────────────────────────────────────
// input.set_trigger
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FNexusEnhancedInputHandler::HandleSetTrigger(
    const TSharedPtr<FJsonObject>& Params)
{
    FString ContextName = GetStringParam(Params, TEXT("context_name"));
    FString ActionName = GetStringParam(Params, TEXT("action_name"));
    FString KeyStr = GetStringParam(Params, TEXT("key"));
    FString TriggerTypeStr = GetStringParam(Params, TEXT("trigger_type"), TEXT("Down"));
    double HoldTimeThreshold = GetNumberParam(Params, TEXT("hold_time_threshold"), 0.5);
    double ActuationThreshold = GetNumberParam(Params, TEXT("actuation_threshold"), 0.5);
    double TapReleaseTimeThreshold = GetNumberParam(Params, TEXT("tap_release_time_threshold"), 0.2);

    if (ContextName.IsEmpty() || ActionName.IsEmpty() || KeyStr.IsEmpty())
    {
        return MakeError(TEXT("MISSING_PARAM"),
            TEXT("context_name, action_name, and key are required"));
    }

    // Find mapping context
    UInputMappingContext* Context = FindMappingContextByName(ContextName);
    if (!Context)
    {
        return MakeError(TEXT("CONTEXT_NOT_FOUND"),
            FString::Printf(TEXT("Mapping context '%s' not found"), *ContextName));
    }

    // Find input action
    UInputAction* Action = FindInputActionByName(ActionName);
    if (!Action)
    {
        return MakeError(TEXT("ACTION_NOT_FOUND"),
            FString::Printf(TEXT("Input action '%s' not found"), *ActionName));
    }

    // Find the specific mapping entry
    FEnhancedActionKeyMapping* Mapping = FindMappingEntry(Context, Action, KeyStr);
    if (!Mapping)
    {
        return MakeError(TEXT("MAPPING_NOT_FOUND"),
            FString::Printf(TEXT("No mapping found for action '%s' with key '%s' in context '%s'"),
                *ActionName, *KeyStr, *ContextName));
    }

    // Create the trigger object based on type
    UInputTrigger* NewTrigger = nullptr;

    if (TriggerTypeStr == TEXT("Down"))
    {
        UInputTriggerDown* Trigger = NewObject<UInputTriggerDown>(Context);
        Trigger->ActuationThreshold = static_cast<float>(ActuationThreshold);
        NewTrigger = Trigger;
    }
    else if (TriggerTypeStr == TEXT("Pressed"))
    {
        UInputTriggerPressed* Trigger = NewObject<UInputTriggerPressed>(Context);
        Trigger->ActuationThreshold = static_cast<float>(ActuationThreshold);
        NewTrigger = Trigger;
    }
    else if (TriggerTypeStr == TEXT("Released"))
    {
        UInputTriggerReleased* Trigger = NewObject<UInputTriggerReleased>(Context);
        Trigger->ActuationThreshold = static_cast<float>(ActuationThreshold);
        NewTrigger = Trigger;
    }
    else if (TriggerTypeStr == TEXT("Hold"))
    {
        UInputTriggerHold* Trigger = NewObject<UInputTriggerHold>(Context);
        Trigger->HoldTimeThreshold = static_cast<float>(HoldTimeThreshold);
        Trigger->ActuationThreshold = static_cast<float>(ActuationThreshold);
        NewTrigger = Trigger;
    }
    else if (TriggerTypeStr == TEXT("HoldAndRelease"))
    {
        UInputTriggerHoldAndRelease* Trigger =
            NewObject<UInputTriggerHoldAndRelease>(Context);
        Trigger->HoldTimeThreshold = static_cast<float>(HoldTimeThreshold);
        Trigger->ActuationThreshold = static_cast<float>(ActuationThreshold);
        NewTrigger = Trigger;
    }
    else if (TriggerTypeStr == TEXT("Tap"))
    {
        UInputTriggerTap* Trigger = NewObject<UInputTriggerTap>(Context);
        Trigger->TapReleaseTimeThreshold = static_cast<float>(TapReleaseTimeThreshold);
        Trigger->ActuationThreshold = static_cast<float>(ActuationThreshold);
        NewTrigger = Trigger;
    }
    else if (TriggerTypeStr == TEXT("Pulse"))
    {
        UInputTriggerPulse* Trigger = NewObject<UInputTriggerPulse>(Context);
        Trigger->ActuationThreshold = static_cast<float>(ActuationThreshold);
        NewTrigger = Trigger;
    }
    else if (TriggerTypeStr == TEXT("ChordAction"))
    {
        UInputTriggerChordAction* Trigger =
            NewObject<UInputTriggerChordAction>(Context);
        NewTrigger = Trigger;
    }
    else
    {
        return MakeError(TEXT("INVALID_TRIGGER_TYPE"),
            FString::Printf(TEXT("Invalid trigger type '%s'. Use: Down, Pressed, Released, "
                "Hold, HoldAndRelease, Tap, Pulse, ChordAction"), *TriggerTypeStr));
    }

    // Replace existing triggers on this mapping — clear and add new
    Mapping->Triggers.Empty();
    Mapping->Triggers.Add(NewTrigger);

    Context->MarkPackageDirty();

    TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
    Data->SetStringField(TEXT("context_name"), ContextName);
    Data->SetStringField(TEXT("action_name"), ActionName);
    Data->SetStringField(TEXT("key"), KeyStr);
    Data->SetStringField(TEXT("trigger_type"), TriggerTypeStr);
    Data->SetNumberField(TEXT("hold_time_threshold"), HoldTimeThreshold);
    Data->SetNumberField(TEXT("actuation_threshold"), ActuationThreshold);
    Data->SetNumberField(TEXT("tap_release_time_threshold"), TapReleaseTimeThreshold);
    return MakeSuccess(Data);
}

// ─────────────────────────────────────────────────────────────────────────────
// input.set_modifier
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FNexusEnhancedInputHandler::HandleSetModifier(
    const TSharedPtr<FJsonObject>& Params)
{
    FString ContextName = GetStringParam(Params, TEXT("context_name"));
    FString ActionName = GetStringParam(Params, TEXT("action_name"));
    FString KeyStr = GetStringParam(Params, TEXT("key"));
    FString ModifierTypeStr = GetStringParam(Params, TEXT("modifier_type"), TEXT("Negate"));

    // Scalar params
    FVector ScalarVec = FVector(1.0, 1.0, 1.0);
    const TSharedPtr<FJsonObject>* ScalarObj;
    if (Params->TryGetObjectField(TEXT("scalar"), ScalarObj))
    {
        double SX = 1.0, SY = 1.0, SZ = 1.0;
        (*ScalarObj)->TryGetNumberField(TEXT("x"), SX);
        (*ScalarObj)->TryGetNumberField(TEXT("y"), SY);
        (*ScalarObj)->TryGetNumberField(TEXT("z"), SZ);
        ScalarVec = FVector(SX, SY, SZ);
    }

    double DeadZoneLower = GetNumberParam(Params, TEXT("dead_zone_lower"), 0.2);
    double DeadZoneUpper = GetNumberParam(Params, TEXT("dead_zone_upper"), 1.0);
    FString DeadZoneTypeStr = GetStringParam(Params, TEXT("dead_zone_type"), TEXT("Axial"));
    FString SwizzleOrderStr = GetStringParam(Params, TEXT("swizzle_order"), TEXT("YXZ"));

    if (ContextName.IsEmpty() || ActionName.IsEmpty() || KeyStr.IsEmpty())
    {
        return MakeError(TEXT("MISSING_PARAM"),
            TEXT("context_name, action_name, and key are required"));
    }

    // Find mapping context
    UInputMappingContext* Context = FindMappingContextByName(ContextName);
    if (!Context)
    {
        return MakeError(TEXT("CONTEXT_NOT_FOUND"),
            FString::Printf(TEXT("Mapping context '%s' not found"), *ContextName));
    }

    // Find input action
    UInputAction* Action = FindInputActionByName(ActionName);
    if (!Action)
    {
        return MakeError(TEXT("ACTION_NOT_FOUND"),
            FString::Printf(TEXT("Input action '%s' not found"), *ActionName));
    }

    // Find the specific mapping entry
    FEnhancedActionKeyMapping* Mapping = FindMappingEntry(Context, Action, KeyStr);
    if (!Mapping)
    {
        return MakeError(TEXT("MAPPING_NOT_FOUND"),
            FString::Printf(TEXT("No mapping found for action '%s' with key '%s' in context '%s'"),
                *ActionName, *KeyStr, *ContextName));
    }

    // Create the modifier object based on type
    UInputModifier* NewModifier = nullptr;

    if (ModifierTypeStr == TEXT("Negate"))
    {
        UInputModifierNegate* Modifier = NewObject<UInputModifierNegate>(Context);
        NewModifier = Modifier;
    }
    else if (ModifierTypeStr == TEXT("Scalar"))
    {
        UInputModifierScalar* Modifier = NewObject<UInputModifierScalar>(Context);
        Modifier->Scalar = ScalarVec;
        NewModifier = Modifier;
    }
    else if (ModifierTypeStr == TEXT("DeadZone"))
    {
        UInputModifierDeadZone* Modifier = NewObject<UInputModifierDeadZone>(Context);
        Modifier->LowerThreshold = static_cast<float>(DeadZoneLower);
        Modifier->UpperThreshold = static_cast<float>(DeadZoneUpper);
        if (DeadZoneTypeStr == TEXT("Radial"))
        {
            Modifier->Type = EDeadZoneType::Radial;
        }
        else
        {
            Modifier->Type = EDeadZoneType::Axial;
        }
        NewModifier = Modifier;
    }
    else if (ModifierTypeStr == TEXT("Swizzle"))
    {
        UInputModifierSwizzleAxis* Modifier =
            NewObject<UInputModifierSwizzleAxis>(Context);
        if (SwizzleOrderStr == TEXT("YXZ"))
        {
            Modifier->Order = EInputAxisSwizzle::YXZ;
        }
        else if (SwizzleOrderStr == TEXT("ZYX"))
        {
            Modifier->Order = EInputAxisSwizzle::ZYX;
        }
        else if (SwizzleOrderStr == TEXT("XZY"))
        {
            Modifier->Order = EInputAxisSwizzle::XZY;
        }
        else if (SwizzleOrderStr == TEXT("YZX"))
        {
            Modifier->Order = EInputAxisSwizzle::YZX;
        }
        else if (SwizzleOrderStr == TEXT("ZXY"))
        {
            Modifier->Order = EInputAxisSwizzle::ZXY;
        }
        NewModifier = Modifier;
    }
    else if (ModifierTypeStr == TEXT("FOVScaling"))
    {
        UInputModifierFOVScaling* Modifier =
            NewObject<UInputModifierFOVScaling>(Context);
        NewModifier = Modifier;
    }
    else if (ModifierTypeStr == TEXT("ResponseCurve"))
    {
        UInputModifierResponseCurveExponential* Modifier =
            NewObject<UInputModifierResponseCurveExponential>(Context);
        NewModifier = Modifier;
    }
    else if (ModifierTypeStr == TEXT("Smooth"))
    {
        UInputModifierSmooth* Modifier = NewObject<UInputModifierSmooth>(Context);
        NewModifier = Modifier;
    }
    else
    {
        return MakeError(TEXT("INVALID_MODIFIER_TYPE"),
            FString::Printf(TEXT("Invalid modifier type '%s'. Use: Negate, Scalar, DeadZone, "
                "Swizzle, FOVScaling, ResponseCurve, Smooth"), *ModifierTypeStr));
    }

    // Replace existing modifiers on this mapping — clear and add new
    Mapping->Modifiers.Empty();
    Mapping->Modifiers.Add(NewModifier);

    Context->MarkPackageDirty();

    TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
    Data->SetStringField(TEXT("context_name"), ContextName);
    Data->SetStringField(TEXT("action_name"), ActionName);
    Data->SetStringField(TEXT("key"), KeyStr);
    Data->SetStringField(TEXT("modifier_type"), ModifierTypeStr);
    return MakeSuccess(Data);
}

// ─────────────────────────────────────────────────────────────────────────────
// input.list_input_actions
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FNexusEnhancedInputHandler::HandleListInputActions(
    const TSharedPtr<FJsonObject>& Params)
{
    FString FilterName = GetStringParam(Params, TEXT("filter_name"));
    bool bIncludeEngine = GetBoolParam(Params, TEXT("include_engine"), false);

    FAssetRegistryModule& AssetRegistryModule =
        FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
    IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();

    // Get all input action assets
    TArray<FAssetData> ActionAssets;
    AssetRegistry.GetAssetsByClass(
        UInputAction::StaticClass()->GetClassPathName(), ActionAssets);

    // Get all mapping context assets for cross-referencing
    TArray<FAssetData> ContextAssets;
    AssetRegistry.GetAssetsByClass(
        UInputMappingContext::StaticClass()->GetClassPathName(), ContextAssets);

    // Load all contexts for referencing
    TArray<UInputMappingContext*> LoadedContexts;
    for (const FAssetData& CtxData : ContextAssets)
    {
        if (!bIncludeEngine && CtxData.GetObjectPathString().StartsWith(TEXT("/Engine/")))
        {
            continue;
        }
        UInputMappingContext* Ctx = Cast<UInputMappingContext>(CtxData.GetAsset());
        if (Ctx)
        {
            LoadedContexts.Add(Ctx);
        }
    }

    TArray<TSharedPtr<FJsonValue>> ActionsArr;

    for (const FAssetData& AssetData : ActionAssets)
    {
        FString AssetPath = AssetData.GetObjectPathString();

        // Filter engine assets if not requested
        if (!bIncludeEngine && AssetPath.StartsWith(TEXT("/Engine/")))
        {
            continue;
        }

        FString Name = AssetData.AssetName.ToString();

        // Apply name filter
        if (!FilterName.IsEmpty() && !Name.Contains(FilterName, ESearchCase::IgnoreCase))
        {
            continue;
        }

        UInputAction* Action = Cast<UInputAction>(AssetData.GetAsset());
        if (!Action) continue;

        TSharedPtr<FJsonObject> ActionObj = MakeShareable(new FJsonObject());
        ActionObj->SetStringField(TEXT("name"), Name);
        ActionObj->SetStringField(TEXT("asset_path"), AssetPath);

        // Value type
        FString ValueTypeStr;
        switch (Action->ValueType)
        {
        case EInputActionValueType::Boolean:
            ValueTypeStr = TEXT("Bool");
            break;
        case EInputActionValueType::Axis1D:
            ValueTypeStr = TEXT("Axis1D");
            break;
        case EInputActionValueType::Axis2D:
            ValueTypeStr = TEXT("Axis2D");
            break;
        case EInputActionValueType::Axis3D:
            ValueTypeStr = TEXT("Axis3D");
            break;
        default:
            ValueTypeStr = TEXT("Unknown");
            break;
        }
        ActionObj->SetStringField(TEXT("value_type"), ValueTypeStr);

        ActionObj->SetStringField(TEXT("description"),
            Action->ActionDescription.ToString());
        ActionObj->SetBoolField(TEXT("consume_input"), Action->bConsumeInput);

        // Find which mapping contexts reference this action
        TArray<TSharedPtr<FJsonValue>> ReferencingContexts;
        for (UInputMappingContext* Ctx : LoadedContexts)
        {
            if (!Ctx) continue;

            for (const FEnhancedActionKeyMapping& Mapping : Ctx->GetMappings())
            {
                if (Mapping.Action == Action)
                {
                    TSharedPtr<FJsonObject> CtxRef = MakeShareable(new FJsonObject());
                    CtxRef->SetStringField(TEXT("context_name"), Ctx->GetName());
                    CtxRef->SetStringField(TEXT("key"),
                        Mapping.Key.GetFName().ToString());
                    ReferencingContexts.Add(
                        MakeShareable(new FJsonValueObject(CtxRef)));
                    break; // One entry per context
                }
            }
        }
        ActionObj->SetArrayField(TEXT("mapping_contexts"), ReferencingContexts);

        ActionsArr.Add(MakeShareable(new FJsonValueObject(ActionObj)));
    }

    TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
    Data->SetArrayField(TEXT("actions"), ActionsArr);
    Data->SetNumberField(TEXT("count"), ActionsArr.Num());
    Data->SetBoolField(TEXT("include_engine"), bIncludeEngine);
    if (!FilterName.IsEmpty())
    {
        Data->SetStringField(TEXT("filter"), FilterName);
    }
    return MakeSuccess(Data);
}
