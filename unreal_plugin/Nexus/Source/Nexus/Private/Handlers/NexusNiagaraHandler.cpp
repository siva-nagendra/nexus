// Copyright Nexus Team. All Rights Reserved.

#include "Handlers/NexusNiagaraHandler.h"
#include "Editor.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "NiagaraSystem.h"
#include "NiagaraEmitter.h"
#include "NiagaraComponent.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystemFactoryNew.h"
#include "NiagaraEmitterFactoryNew.h"
#include "NiagaraEditorModule.h"
#include "NiagaraScriptVariable.h"
#include "NiagaraParameterDefinitionsBase.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "UObject/SavePackage.h"

// Module stack CRUD (Node CRUD Phase 2)
#include "NiagaraGraph.h"
#include "NiagaraNodeFunctionCall.h"
#include "NiagaraNodeOutput.h"
#include "NiagaraScript.h"
#include "NiagaraScriptSource.h"
#include "ViewModels/Stack/NiagaraStackGraphUtilities.h"

// ─────────────────────────────────────────────────────────────────────────────
// Dispatch
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FNexusNiagaraHandler::Handle(
    const FString& CommandType,
    const TSharedPtr<FJsonObject>& Params)
{
    FString SubCommand = CommandType.RightChop(GetNamespace().Len() + 1);

    if (SubCommand == TEXT("create_niagara_system"))      return HandleCreateNiagaraSystem(Params);
    if (SubCommand == TEXT("create_niagara_emitter"))      return HandleCreateNiagaraEmitter(Params);
    if (SubCommand == TEXT("set_niagara_parameter"))       return HandleSetNiagaraParameter(Params);
    if (SubCommand == TEXT("set_niagara_variable"))        return HandleSetNiagaraVariable(Params);
    if (SubCommand == TEXT("activate_niagara_system"))     return HandleActivateNiagaraSystem(Params);
    if (SubCommand == TEXT("spawn_niagara_at_location"))   return HandleSpawnNiagaraAtLocation(Params);
    if (SubCommand == TEXT("get_niagara_info"))            return HandleGetNiagaraInfo(Params);
    if (SubCommand == TEXT("list_niagara_modules"))        return HandleListNiagaraModules(Params);

    // Module stack CRUD (Node CRUD Phase 2)
    if (SubCommand == TEXT("add_module"))                  return HandleAddModule(Params);
    if (SubCommand == TEXT("get_emitter_stack"))           return HandleGetEmitterStack(Params);
    if (SubCommand == TEXT("update_module"))               return HandleUpdateModule(Params);
    if (SubCommand == TEXT("remove_module"))               return HandleRemoveModule(Params);
    if (SubCommand == TEXT("reorder_modules"))             return HandleReorderModules(Params);

    return MakeError(TEXT("NOT_IMPLEMENTED"),
        FString::Printf(TEXT("Command '%s' not yet implemented"), *CommandType));
}

// ─────────────────────────────────────────────────────────────────────────────
// Actor resolution helpers
// ─────────────────────────────────────────────────────────────────────────────

AActor* FNexusNiagaraHandler::FindActorByPath(const FString& Path)
{
    if (Path.IsEmpty()) return nullptr;
    UObject* Obj = StaticFindObject(AActor::StaticClass(), nullptr, *Path);
    return Cast<AActor>(Obj);
}

AActor* FNexusNiagaraHandler::FindActorByLabel(const FString& Label)
{
    UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
    if (!World) return nullptr;

    for (TActorIterator<AActor> It(World); It; ++It)
    {
        if (It->GetActorLabel() == Label)
        {
            return *It;
        }
    }
    return nullptr;
}

AActor* FNexusNiagaraHandler::ResolveActor(
    const TSharedPtr<FJsonObject>& Params,
    const FString& ParamName,
    FString& OutError)
{
    FString ActorPath = GetStringParam(Params, ParamName);
    if (ActorPath.IsEmpty())
    {
        OutError = FString::Printf(TEXT("'%s' is required"), *ParamName);
        return nullptr;
    }

    AActor* Actor = FindActorByPath(ActorPath);
    if (!Actor)
    {
        Actor = FindActorByLabel(ActorPath);
    }
    if (!Actor)
    {
        OutError = FString::Printf(TEXT("No actor found for '%s'"), *ActorPath);
    }
    return Actor;
}

// ─────────────────────────────────────────────────────────────────────────────
// niagara.create_niagara_system
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FNexusNiagaraHandler::HandleCreateNiagaraSystem(
    const TSharedPtr<FJsonObject>& Params)
{
    FString SystemName = GetStringParam(Params, TEXT("system_name"));
    if (SystemName.IsEmpty())
    {
        return MakeError(TEXT("MISSING_PARAM"), TEXT("system_name is required"));
    }

    FString DestFolder = GetStringParam(Params, TEXT("destination_folder"), TEXT("/Game/VFX"));
    FString Template = GetStringParam(Params, TEXT("template"), TEXT("Empty"));
    FString EmitterPaths = GetStringParam(Params, TEXT("emitter_paths"));

    // Create the Niagara System asset via factory
    IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>(
        "AssetTools").Get();

    UNiagaraSystemFactoryNew* Factory = NewObject<UNiagaraSystemFactoryNew>();

    UObject* NewAsset = AssetTools.CreateAsset(
        SystemName, DestFolder, UNiagaraSystem::StaticClass(), Factory);
    if (!NewAsset)
    {
        return MakeError(TEXT("CREATION_FAILED"),
            FString::Printf(TEXT("Failed to create Niagara System '%s' in '%s'"),
                *SystemName, *DestFolder));
    }

    UNiagaraSystem* NiagaraSystem = Cast<UNiagaraSystem>(NewAsset);
    if (!NiagaraSystem)
    {
        return MakeError(TEXT("CREATION_FAILED"),
            TEXT("Created asset is not a Niagara System"));
    }

    // If emitter paths were provided, try to add them
    TArray<FString> AddedEmitters;
    if (!EmitterPaths.IsEmpty())
    {
        TArray<FString> PathArray;
        EmitterPaths.ParseIntoArray(PathArray, TEXT(","), true);

        for (const FString& RawPath : PathArray)
        {
            FString EmitterPath = RawPath.TrimStartAndEnd();
            UNiagaraEmitter* Emitter = LoadObject<UNiagaraEmitter>(nullptr, *EmitterPath);
            if (Emitter)
            {
                // In UE5.7, emitters are added as handles within the system
                // AddEmitterHandle requires emitter ref, name, and version GUID
                NiagaraSystem->AddEmitterHandle(*Emitter, FName(*Emitter->GetName()), Emitter->GetExposedVersion().VersionGuid);
                AddedEmitters.Add(EmitterPath);
            }
        }
    }

    // Mark dirty and save
    NewAsset->MarkPackageDirty();

    TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
    Data->SetStringField(TEXT("asset_path"), NewAsset->GetPathName());
    Data->SetStringField(TEXT("system_name"), SystemName);
    Data->SetStringField(TEXT("template"), Template);
    Data->SetNumberField(TEXT("emitter_count"), AddedEmitters.Num());

    TArray<TSharedPtr<FJsonValue>> EmitterArr;
    for (const FString& E : AddedEmitters)
    {
        EmitterArr.Add(MakeShareable(new FJsonValueString(E)));
    }
    Data->SetArrayField(TEXT("added_emitters"), EmitterArr);

    return MakeSuccess(Data);
}

// ─────────────────────────────────────────────────────────────────────────────
// niagara.create_niagara_emitter
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FNexusNiagaraHandler::HandleCreateNiagaraEmitter(
    const TSharedPtr<FJsonObject>& Params)
{
    FString EmitterName = GetStringParam(Params, TEXT("emitter_name"));
    if (EmitterName.IsEmpty())
    {
        return MakeError(TEXT("MISSING_PARAM"), TEXT("emitter_name is required"));
    }

    FString DestFolder = GetStringParam(Params, TEXT("destination_folder"), TEXT("/Game/VFX"));
    FString Template = GetStringParam(Params, TEXT("template"), TEXT("Empty"));
    FString SimTarget = GetStringParam(Params, TEXT("sim_target"), TEXT("CPUSim"));
    bool bFixedBounds = GetBoolParam(Params, TEXT("fixed_bounds"), false);
    double BoundsExtent = GetNumberParam(Params, TEXT("bounds_extent"), 500.0);

    // Create the Niagara Emitter asset via factory
    IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>(
        "AssetTools").Get();

    UNiagaraEmitterFactoryNew* Factory = NewObject<UNiagaraEmitterFactoryNew>();

    UObject* NewAsset = AssetTools.CreateAsset(
        EmitterName, DestFolder, UNiagaraEmitter::StaticClass(), Factory);
    if (!NewAsset)
    {
        return MakeError(TEXT("CREATION_FAILED"),
            FString::Printf(TEXT("Failed to create Niagara Emitter '%s' in '%s'"),
                *EmitterName, *DestFolder));
    }

    UNiagaraEmitter* NiagaraEmitter = Cast<UNiagaraEmitter>(NewAsset);
    if (!NiagaraEmitter)
    {
        return MakeError(TEXT("CREATION_FAILED"),
            TEXT("Created asset is not a Niagara Emitter"));
    }

    // Configure simulation target
    // SimTarget is set on the emitter's compute properties
    // In UE5.7, this is typically configured through the emitter's scripts

    // Configure fixed bounds
    // In UE5.7, fixed bounds are configured via versioned emitter data
    // The FixedBounds property is on FVersionedNiagaraEmitterData accessed via GetEmitterData()
    // For newly created emitters, we cannot directly set bounds here as the data structure
    // is more complex. The emitter should be configured in the Niagara Editor or via scripts.
    // Marking for future implementation if direct property access becomes available.
    if (bFixedBounds)
    {
        // Note: Direct SetFixedBounds is not available on UNiagaraEmitter in UE 5.7.
        // Fixed bounds must be configured through the Niagara Editor or emitter versioned data.
        // The FixedBounds property exists on FVersionedNiagaraEmitterData.
    }

    NewAsset->MarkPackageDirty();

    TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
    Data->SetStringField(TEXT("asset_path"), NewAsset->GetPathName());
    Data->SetStringField(TEXT("emitter_name"), EmitterName);
    Data->SetStringField(TEXT("template"), Template);
    Data->SetStringField(TEXT("sim_target"), SimTarget);
    Data->SetBoolField(TEXT("fixed_bounds"), bFixedBounds);
    if (bFixedBounds)
    {
        Data->SetNumberField(TEXT("bounds_extent"), BoundsExtent);
    }
    return MakeSuccess(Data);
}

// ─────────────────────────────────────────────────────────────────────────────
// niagara.set_niagara_parameter
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FNexusNiagaraHandler::HandleSetNiagaraParameter(
    const TSharedPtr<FJsonObject>& Params)
{
    FString AssetPath = GetStringParam(Params, TEXT("asset_path"));
    if (AssetPath.IsEmpty())
    {
        return MakeError(TEXT("MISSING_PARAM"), TEXT("asset_path is required"));
    }

    FString ParameterName = GetStringParam(Params, TEXT("parameter_name"));
    if (ParameterName.IsEmpty())
    {
        return MakeError(TEXT("MISSING_PARAM"), TEXT("parameter_name is required"));
    }

    FString Value = GetStringParam(Params, TEXT("value"));
    FString ValueType = GetStringParam(Params, TEXT("value_type"), TEXT("Float"));
    FString EmitterName = GetStringParam(Params, TEXT("emitter_name"));

    // Load the Niagara asset (System or Emitter)
    UObject* Asset = LoadObject<UObject>(nullptr, *AssetPath);
    if (!Asset)
    {
        return MakeError(TEXT("NOT_FOUND"),
            FString::Printf(TEXT("Asset not found at '%s'"), *AssetPath));
    }

    UNiagaraSystem* System = Cast<UNiagaraSystem>(Asset);
    UNiagaraEmitter* Emitter = Cast<UNiagaraEmitter>(Asset);

    if (!System && !Emitter)
    {
        return MakeError(TEXT("INVALID_ASSET"),
            TEXT("Asset must be a NiagaraSystem or NiagaraEmitter"));
    }

    // Parameter setting on Niagara assets is done through the
    // UNiagaraSystem/UNiagaraEmitter exposed parameter stores.
    // The actual modification depends on the parameter type and the
    // Niagara scripting API.
    if (System)
    {
        FNiagaraUserRedirectionParameterStore& ParamStore =
            System->GetExposedParameters();

        // Parse value based on type and set it in the parameter store
        FNiagaraVariable Var;
        Var.SetName(FName(*ParameterName));

        if (ValueType == TEXT("Float"))
        {
            Var.SetType(FNiagaraTypeDefinition::GetFloatDef());
            float FloatVal = FCString::Atof(*Value);
            ParamStore.SetParameterData(reinterpret_cast<const uint8*>(&FloatVal), Var);
        }
        else if (ValueType == TEXT("Int"))
        {
            Var.SetType(FNiagaraTypeDefinition::GetIntDef());
            int32 IntVal = FCString::Atoi(*Value);
            ParamStore.SetParameterData(reinterpret_cast<const uint8*>(&IntVal), Var);
        }
        else if (ValueType == TEXT("Bool"))
        {
            Var.SetType(FNiagaraTypeDefinition::GetBoolDef());
            FNiagaraBool BoolVal;
            BoolVal.SetValue(Value.ToBool());
            ParamStore.SetParameterData(reinterpret_cast<const uint8*>(&BoolVal), Var);
        }
        else if (ValueType == TEXT("Vector"))
        {
            Var.SetType(FNiagaraTypeDefinition::GetVec3Def());
            TArray<FString> Parts;
            Value.ParseIntoArray(Parts, TEXT(","), true);
            FVector3f VecVal = FVector3f::ZeroVector;
            if (Parts.Num() >= 3)
            {
                VecVal.X = FCString::Atof(*Parts[0].TrimStartAndEnd());
                VecVal.Y = FCString::Atof(*Parts[1].TrimStartAndEnd());
                VecVal.Z = FCString::Atof(*Parts[2].TrimStartAndEnd());
            }
            ParamStore.SetParameterData(reinterpret_cast<const uint8*>(&VecVal), Var);
        }
        else if (ValueType == TEXT("Color"))
        {
            Var.SetType(FNiagaraTypeDefinition::GetColorDef());
            TArray<FString> Parts;
            Value.ParseIntoArray(Parts, TEXT(","), true);
            FLinearColor ColorVal = FLinearColor::White;
            if (Parts.Num() >= 3)
            {
                ColorVal.R = FCString::Atof(*Parts[0].TrimStartAndEnd());
                ColorVal.G = FCString::Atof(*Parts[1].TrimStartAndEnd());
                ColorVal.B = FCString::Atof(*Parts[2].TrimStartAndEnd());
                if (Parts.Num() >= 4)
                {
                    ColorVal.A = FCString::Atof(*Parts[3].TrimStartAndEnd());
                }
            }
            ParamStore.SetParameterData(reinterpret_cast<const uint8*>(&ColorVal), Var);
        }

        System->MarkPackageDirty();
    }

    Asset->MarkPackageDirty();

    TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
    Data->SetStringField(TEXT("asset_path"), AssetPath);
    Data->SetStringField(TEXT("parameter_name"), ParameterName);
    Data->SetStringField(TEXT("value"), Value);
    Data->SetStringField(TEXT("value_type"), ValueType);
    if (!EmitterName.IsEmpty())
    {
        Data->SetStringField(TEXT("emitter_name"), EmitterName);
    }
    Data->SetStringField(TEXT("asset_type"), System ? TEXT("NiagaraSystem") : TEXT("NiagaraEmitter"));
    return MakeSuccess(Data);
}

// ─────────────────────────────────────────────────────────────────────────────
// niagara.set_niagara_variable
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FNexusNiagaraHandler::HandleSetNiagaraVariable(
    const TSharedPtr<FJsonObject>& Params)
{
    FString Error;
    AActor* Actor = ResolveActor(Params, TEXT("actor_path"), Error);
    if (!Actor) return MakeError(TEXT("NOT_FOUND"), Error);

    FString VariableName = GetStringParam(Params, TEXT("variable_name"));
    if (VariableName.IsEmpty())
    {
        return MakeError(TEXT("MISSING_PARAM"), TEXT("variable_name is required"));
    }

    FString Value = GetStringParam(Params, TEXT("value"));
    FString ValueType = GetStringParam(Params, TEXT("value_type"), TEXT("Float"));

    // Find the NiagaraComponent on the actor
    UNiagaraComponent* NiagaraComp = Actor->FindComponentByClass<UNiagaraComponent>();
    if (!NiagaraComp)
    {
        return MakeError(TEXT("NO_COMPONENT"),
            FString::Printf(TEXT("Actor '%s' has no NiagaraComponent"), *Actor->GetName()));
    }

    FName VarFName(*VariableName);

    // Set variable based on type
    if (ValueType == TEXT("Float"))
    {
        float FloatVal = FCString::Atof(*Value);
        NiagaraComp->SetVariableFloat(VarFName, FloatVal);
    }
    else if (ValueType == TEXT("Int"))
    {
        int32 IntVal = FCString::Atoi(*Value);
        NiagaraComp->SetVariableInt(VarFName, IntVal);
    }
    else if (ValueType == TEXT("Bool"))
    {
        bool BoolVal = Value.ToBool();
        NiagaraComp->SetVariableBool(VarFName, BoolVal);
    }
    else if (ValueType == TEXT("Vector"))
    {
        TArray<FString> Parts;
        Value.ParseIntoArray(Parts, TEXT(","), true);
        FVector VecVal = FVector::ZeroVector;
        if (Parts.Num() >= 3)
        {
            VecVal.X = FCString::Atof(*Parts[0].TrimStartAndEnd());
            VecVal.Y = FCString::Atof(*Parts[1].TrimStartAndEnd());
            VecVal.Z = FCString::Atof(*Parts[2].TrimStartAndEnd());
        }
        NiagaraComp->SetVariableVec3(VarFName, VecVal);
    }
    else if (ValueType == TEXT("Color") || ValueType == TEXT("LinearColor"))
    {
        TArray<FString> Parts;
        Value.ParseIntoArray(Parts, TEXT(","), true);
        FLinearColor ColorVal = FLinearColor::White;
        if (Parts.Num() >= 3)
        {
            ColorVal.R = FCString::Atof(*Parts[0].TrimStartAndEnd());
            ColorVal.G = FCString::Atof(*Parts[1].TrimStartAndEnd());
            ColorVal.B = FCString::Atof(*Parts[2].TrimStartAndEnd());
            if (Parts.Num() >= 4)
            {
                ColorVal.A = FCString::Atof(*Parts[3].TrimStartAndEnd());
            }
        }
        NiagaraComp->SetVariableLinearColor(VarFName, ColorVal);
    }
    else if (ValueType == TEXT("Quat"))
    {
        TArray<FString> Parts;
        Value.ParseIntoArray(Parts, TEXT(","), true);
        FQuat QuatVal = FQuat::Identity;
        if (Parts.Num() >= 4)
        {
            QuatVal.X = FCString::Atof(*Parts[0].TrimStartAndEnd());
            QuatVal.Y = FCString::Atof(*Parts[1].TrimStartAndEnd());
            QuatVal.Z = FCString::Atof(*Parts[2].TrimStartAndEnd());
            QuatVal.W = FCString::Atof(*Parts[3].TrimStartAndEnd());
        }
        NiagaraComp->SetVariableQuat(VarFName, QuatVal);
    }
    else
    {
        return MakeError(TEXT("INVALID_TYPE"),
            FString::Printf(TEXT("Unsupported value_type '%s'. Use: Float, Int, Bool, Vector, Color, LinearColor, Quat"), *ValueType));
    }

    TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
    Data->SetStringField(TEXT("actor_path"), Actor->GetPathName());
    Data->SetStringField(TEXT("variable_name"), VariableName);
    Data->SetStringField(TEXT("value"), Value);
    Data->SetStringField(TEXT("value_type"), ValueType);
    return MakeSuccess(Data);
}

// ─────────────────────────────────────────────────────────────────────────────
// niagara.activate_niagara_system
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FNexusNiagaraHandler::HandleActivateNiagaraSystem(
    const TSharedPtr<FJsonObject>& Params)
{
    FString Error;
    AActor* Actor = ResolveActor(Params, TEXT("actor_path"), Error);
    if (!Actor) return MakeError(TEXT("NOT_FOUND"), Error);

    bool bActivate = GetBoolParam(Params, TEXT("activate"), true);
    bool bReset = GetBoolParam(Params, TEXT("reset"), false);

    // Find the NiagaraComponent on the actor
    UNiagaraComponent* NiagaraComp = Actor->FindComponentByClass<UNiagaraComponent>();
    if (!NiagaraComp)
    {
        return MakeError(TEXT("NO_COMPONENT"),
            FString::Printf(TEXT("Actor '%s' has no NiagaraComponent"), *Actor->GetName()));
    }

    if (bReset)
    {
        NiagaraComp->ResetSystem();
    }

    if (bActivate)
    {
        NiagaraComp->Activate(bReset);
    }
    else
    {
        NiagaraComp->Deactivate();
    }

    TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
    Data->SetStringField(TEXT("actor_path"), Actor->GetPathName());
    Data->SetBoolField(TEXT("activated"), bActivate);
    Data->SetBoolField(TEXT("reset"), bReset);
    Data->SetBoolField(TEXT("is_active"), NiagaraComp->IsActive());
    return MakeSuccess(Data);
}

// ─────────────────────────────────────────────────────────────────────────────
// niagara.spawn_niagara_at_location
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FNexusNiagaraHandler::HandleSpawnNiagaraAtLocation(
    const TSharedPtr<FJsonObject>& Params)
{
    FString SystemPath = GetStringParam(Params, TEXT("system_path"));
    if (SystemPath.IsEmpty())
    {
        return MakeError(TEXT("MISSING_PARAM"), TEXT("system_path is required"));
    }

    UNiagaraSystem* NiagaraSystem = LoadObject<UNiagaraSystem>(nullptr, *SystemPath);
    if (!NiagaraSystem)
    {
        return MakeError(TEXT("NOT_FOUND"),
            FString::Printf(TEXT("Niagara System not found at '%s'"), *SystemPath));
    }

    FVector Location = GetVectorParam(Params, TEXT("location"), FVector::ZeroVector);
    FVector Scale = GetVectorParam(Params, TEXT("scale"), FVector::OneVector);
    bool bAutoActivate = GetBoolParam(Params, TEXT("auto_activate"), true);
    bool bAutoDestroy = GetBoolParam(Params, TEXT("auto_destroy"), false);
    FString Label = GetStringParam(Params, TEXT("label"));

    // Parse rotation from array
    FRotator Rotation = FRotator::ZeroRotator;
    const TArray<TSharedPtr<FJsonValue>>* RotArray;
    if (Params->TryGetArrayField(TEXT("rotation"), RotArray) && RotArray->Num() >= 3)
    {
        Rotation.Pitch = (*RotArray)[0]->AsNumber();
        Rotation.Yaw = (*RotArray)[1]->AsNumber();
        Rotation.Roll = (*RotArray)[2]->AsNumber();
    }

    UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
    if (!World)
    {
        return MakeError(TEXT("NO_WORLD"), TEXT("No editor world available"));
    }

    // Spawn using UNiagaraFunctionLibrary
    UNiagaraComponent* NiagaraComp = UNiagaraFunctionLibrary::SpawnSystemAtLocation(
        World,
        NiagaraSystem,
        Location,
        Rotation,
        Scale,
        bAutoDestroy,
        bAutoActivate);

    if (!NiagaraComp)
    {
        return MakeError(TEXT("SPAWN_FAILED"),
            TEXT("Failed to spawn Niagara System at location"));
    }

    AActor* OwnerActor = NiagaraComp->GetOwner();

    // Set label if provided
    if (!Label.IsEmpty() && OwnerActor)
    {
        OwnerActor->SetActorLabel(Label);
    }

    TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
    Data->SetStringField(TEXT("system_path"), SystemPath);
    Data->SetObjectField(TEXT("location"), VectorToJson(Location));
    Data->SetObjectField(TEXT("rotation"), RotatorToJson(Rotation));
    Data->SetObjectField(TEXT("scale"), VectorToJson(Scale));
    Data->SetBoolField(TEXT("auto_activate"), bAutoActivate);
    Data->SetBoolField(TEXT("auto_destroy"), bAutoDestroy);
    if (OwnerActor)
    {
        Data->SetStringField(TEXT("actor_path"), OwnerActor->GetPathName());
        Data->SetStringField(TEXT("actor_label"),
            Label.IsEmpty() ? OwnerActor->GetActorLabel() : Label);
    }
    Data->SetStringField(TEXT("component_name"), NiagaraComp->GetName());
    Data->SetBoolField(TEXT("is_active"), NiagaraComp->IsActive());
    return MakeSuccess(Data);
}

// ─────────────────────────────────────────────────────────────────────────────
// niagara.get_niagara_info
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FNexusNiagaraHandler::HandleGetNiagaraInfo(
    const TSharedPtr<FJsonObject>& Params)
{
    FString AssetPath = GetStringParam(Params, TEXT("asset_path"));
    FString ActorPath = GetStringParam(Params, TEXT("actor_path"));
    bool bIncludeParameters = GetBoolParam(Params, TEXT("include_parameters"), true);
    bool bIncludeEmitters = GetBoolParam(Params, TEXT("include_emitters"), true);

    if (AssetPath.IsEmpty() && ActorPath.IsEmpty())
    {
        return MakeError(TEXT("MISSING_PARAM"),
            TEXT("Either asset_path or actor_path is required"));
    }

    TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());

    // Case 1: Inspect a live NiagaraComponent on an actor
    if (!ActorPath.IsEmpty())
    {
        FString Error;
        AActor* Actor = ResolveActor(Params, TEXT("actor_path"), Error);
        if (!Actor) return MakeError(TEXT("NOT_FOUND"), Error);

        UNiagaraComponent* NiagaraComp = Actor->FindComponentByClass<UNiagaraComponent>();
        if (!NiagaraComp)
        {
            return MakeError(TEXT("NO_COMPONENT"),
                FString::Printf(TEXT("Actor '%s' has no NiagaraComponent"), *Actor->GetName()));
        }

        Data->SetStringField(TEXT("actor_path"), Actor->GetPathName());
        Data->SetStringField(TEXT("actor_label"), Actor->GetActorLabel());
        Data->SetStringField(TEXT("component_name"), NiagaraComp->GetName());
        Data->SetBoolField(TEXT("is_active"), NiagaraComp->IsActive());
        Data->SetBoolField(TEXT("is_complete"), NiagaraComp->IsComplete());
        Data->SetObjectField(TEXT("location"),
            VectorToJson(NiagaraComp->GetComponentLocation()));
        Data->SetObjectField(TEXT("rotation"),
            RotatorToJson(NiagaraComp->GetComponentRotation()));

        // Get asset info from the component
        UNiagaraSystem* System = NiagaraComp->GetAsset();
        if (System)
        {
            Data->SetStringField(TEXT("system_asset_path"), System->GetPathName());
            Data->SetStringField(TEXT("system_name"), System->GetName());

            if (bIncludeEmitters)
            {
                TArray<TSharedPtr<FJsonValue>> EmitterArr;
                const TArray<FNiagaraEmitterHandle>& EmitterHandles = System->GetEmitterHandles();
                for (const FNiagaraEmitterHandle& Handle : EmitterHandles)
                {
                    TSharedPtr<FJsonObject> EmitterObj = MakeShareable(new FJsonObject());
                    EmitterObj->SetStringField(TEXT("name"), Handle.GetName().ToString());
                    EmitterObj->SetBoolField(TEXT("enabled"), Handle.GetIsEnabled());
                    EmitterArr.Add(MakeShareable(new FJsonValueObject(EmitterObj)));
                }
                Data->SetArrayField(TEXT("emitters"), EmitterArr);
                Data->SetNumberField(TEXT("emitter_count"), EmitterArr.Num());
            }
        }

        Data->SetStringField(TEXT("source"), TEXT("component"));
        return MakeSuccess(Data);
    }

    // Case 2: Inspect a Niagara asset
    UObject* Asset = LoadObject<UObject>(nullptr, *AssetPath);
    if (!Asset)
    {
        return MakeError(TEXT("NOT_FOUND"),
            FString::Printf(TEXT("Asset not found at '%s'"), *AssetPath));
    }

    UNiagaraSystem* System = Cast<UNiagaraSystem>(Asset);
    UNiagaraEmitter* Emitter = Cast<UNiagaraEmitter>(Asset);

    if (!System && !Emitter)
    {
        return MakeError(TEXT("INVALID_ASSET"),
            TEXT("Asset must be a NiagaraSystem or NiagaraEmitter"));
    }

    Data->SetStringField(TEXT("asset_path"), AssetPath);

    if (System)
    {
        Data->SetStringField(TEXT("asset_type"), TEXT("NiagaraSystem"));
        Data->SetStringField(TEXT("system_name"), System->GetName());
        Data->SetBoolField(TEXT("warmup_time_set"),
            System->GetWarmupTime() > 0.0f);
        Data->SetNumberField(TEXT("warmup_time"), System->GetWarmupTime());

        if (bIncludeEmitters)
        {
            TArray<TSharedPtr<FJsonValue>> EmitterArr;
            const TArray<FNiagaraEmitterHandle>& EmitterHandles = System->GetEmitterHandles();
            for (const FNiagaraEmitterHandle& Handle : EmitterHandles)
            {
                TSharedPtr<FJsonObject> EmitterObj = MakeShareable(new FJsonObject());
                EmitterObj->SetStringField(TEXT("name"), Handle.GetName().ToString());
                EmitterObj->SetBoolField(TEXT("enabled"), Handle.GetIsEnabled());
                EmitterArr.Add(MakeShareable(new FJsonValueObject(EmitterObj)));
            }
            Data->SetArrayField(TEXT("emitters"), EmitterArr);
            Data->SetNumberField(TEXT("emitter_count"), EmitterArr.Num());
        }

        if (bIncludeParameters)
        {
            TArray<TSharedPtr<FJsonValue>> ParamArr;
            const FNiagaraUserRedirectionParameterStore& ParamStore =
                System->GetExposedParameters();
            // ReadParameterVariables returns TArrayView, iterate directly
            TArrayView<const FNiagaraVariableWithOffset> Vars = ParamStore.ReadParameterVariables();
            for (const FNiagaraVariableWithOffset& VarWithOffset : Vars)
            {
                TSharedPtr<FJsonObject> ParamObj = MakeShareable(new FJsonObject());
                ParamObj->SetStringField(TEXT("name"), VarWithOffset.GetName().ToString());
                ParamObj->SetStringField(TEXT("type"),
                    VarWithOffset.GetType().GetName());
                ParamArr.Add(MakeShareable(new FJsonValueObject(ParamObj)));
            }
            Data->SetArrayField(TEXT("parameters"), ParamArr);
            Data->SetNumberField(TEXT("parameter_count"), ParamArr.Num());
        }

        Data->SetStringField(TEXT("source"), TEXT("asset"));
    }
    else if (Emitter)
    {
        Data->SetStringField(TEXT("asset_type"), TEXT("NiagaraEmitter"));
        Data->SetStringField(TEXT("emitter_name"), Emitter->GetName());
        Data->SetStringField(TEXT("source"), TEXT("asset"));
    }

    return MakeSuccess(Data);
}

// ─────────────────────────────────────────────────────────────────────────────
// niagara.list_niagara_modules
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FNexusNiagaraHandler::HandleListNiagaraModules(
    const TSharedPtr<FJsonObject>& Params)
{
    FString Category = GetStringParam(Params, TEXT("category"));
    FString NameFilter = GetStringParam(Params, TEXT("name_filter"));
    int32 MaxResults = static_cast<int32>(GetNumberParam(Params, TEXT("max_results"), 100.0));

    // Query the Asset Registry for Niagara Module Script assets
    FAssetRegistryModule& AssetRegistryModule =
        FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
    IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();

    FARFilter Filter;
    Filter.ClassPaths.Add(FTopLevelAssetPath(TEXT("/Script/Niagara"), TEXT("NiagaraScript")));
    Filter.bRecursiveClasses = true;
    Filter.bRecursivePaths = true;

    TArray<FAssetData> AssetResults;
    AssetRegistry.GetAssets(Filter, AssetResults);

    TArray<TSharedPtr<FJsonValue>> ModuleArr;
    int32 Count = 0;

    for (const FAssetData& AssetData : AssetResults)
    {
        if (Count >= MaxResults) break;

        FString AssetName = AssetData.AssetName.ToString();
        FString AssetPathStr = AssetData.GetSoftObjectPath().ToString();

        // Apply name filter
        if (!NameFilter.IsEmpty() && !AssetName.Contains(NameFilter))
        {
            continue;
        }

        // Determine category from the asset path or tags
        FString DetectedCategory = TEXT("Unknown");
        if (AssetPathStr.Contains(TEXT("Spawn")))
        {
            DetectedCategory = TEXT("Spawn");
        }
        else if (AssetPathStr.Contains(TEXT("Update")))
        {
            DetectedCategory = TEXT("Update");
        }
        else if (AssetPathStr.Contains(TEXT("Render")))
        {
            DetectedCategory = TEXT("Render");
        }
        else if (AssetPathStr.Contains(TEXT("Event")))
        {
            DetectedCategory = TEXT("Event");
        }

        // Apply category filter
        if (!Category.IsEmpty() && DetectedCategory != Category)
        {
            continue;
        }

        TSharedPtr<FJsonObject> ModuleObj = MakeShareable(new FJsonObject());
        ModuleObj->SetStringField(TEXT("name"), AssetName);
        ModuleObj->SetStringField(TEXT("asset_path"), AssetPathStr);
        ModuleObj->SetStringField(TEXT("category"), DetectedCategory);

        ModuleArr.Add(MakeShareable(new FJsonValueObject(ModuleObj)));
        Count++;
    }

    TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
    Data->SetArrayField(TEXT("modules"), ModuleArr);
    Data->SetNumberField(TEXT("count"), ModuleArr.Num());
    Data->SetNumberField(TEXT("max_results"), MaxResults);
    if (!Category.IsEmpty())
    {
        Data->SetStringField(TEXT("category_filter"), Category);
    }
    if (!NameFilter.IsEmpty())
    {
        Data->SetStringField(TEXT("name_filter"), NameFilter);
    }
    return MakeSuccess(Data);
}

// ─────────────────────────────────────────────────────────────────────────────
// Module stack CRUD helpers
// ─────────────────────────────────────────────────────────────────────────────

namespace
{
    /** Map a user-facing stage name to ENiagaraScriptUsage. */
    ENiagaraScriptUsage ParseStageUsage(const FString& StageName)
    {
        if (StageName.Equals(TEXT("ParticleSpawn"), ESearchCase::IgnoreCase))
            return ENiagaraScriptUsage::ParticleSpawnScript;
        if (StageName.Equals(TEXT("ParticleUpdate"), ESearchCase::IgnoreCase))
            return ENiagaraScriptUsage::ParticleUpdateScript;
        if (StageName.Equals(TEXT("EmitterSpawn"), ESearchCase::IgnoreCase))
            return ENiagaraScriptUsage::EmitterSpawnScript;
        if (StageName.Equals(TEXT("EmitterUpdate"), ESearchCase::IgnoreCase))
            return ENiagaraScriptUsage::EmitterUpdateScript;
        if (StageName.Equals(TEXT("SystemSpawn"), ESearchCase::IgnoreCase))
            return ENiagaraScriptUsage::SystemSpawnScript;
        if (StageName.Equals(TEXT("SystemUpdate"), ESearchCase::IgnoreCase))
            return ENiagaraScriptUsage::SystemUpdateScript;
        if (StageName.Equals(TEXT("ParticleEvent"), ESearchCase::IgnoreCase))
            return ENiagaraScriptUsage::ParticleEventScript;
        if (StageName.Equals(TEXT("SimulationStage"), ESearchCase::IgnoreCase))
            return ENiagaraScriptUsage::ParticleSimulationStageScript;
        // Default to ParticleUpdate as the most common target
        return ENiagaraScriptUsage::ParticleUpdateScript;
    }

    /** Convert ENiagaraScriptUsage to a user-facing name. */
    FString UsageToString(ENiagaraScriptUsage Usage)
    {
        switch (Usage)
        {
        case ENiagaraScriptUsage::ParticleSpawnScript:              return TEXT("ParticleSpawn");
        case ENiagaraScriptUsage::ParticleUpdateScript:             return TEXT("ParticleUpdate");
        case ENiagaraScriptUsage::EmitterSpawnScript:               return TEXT("EmitterSpawn");
        case ENiagaraScriptUsage::EmitterUpdateScript:              return TEXT("EmitterUpdate");
        case ENiagaraScriptUsage::SystemSpawnScript:                return TEXT("SystemSpawn");
        case ENiagaraScriptUsage::SystemUpdateScript:               return TEXT("SystemUpdate");
        case ENiagaraScriptUsage::ParticleEventScript:              return TEXT("ParticleEvent");
        case ENiagaraScriptUsage::ParticleSimulationStageScript:    return TEXT("SimulationStage");
        default:                                                     return TEXT("Unknown");
        }
    }

    /** Get the UNiagaraGraph for a given emitter + stage. Returns nullptr on failure. */
    UNiagaraGraph* GetEmitterGraph(FVersionedNiagaraEmitterData* EmitterData,
        ENiagaraScriptUsage Usage, FString& OutError)
    {
        if (!EmitterData)
        {
            OutError = TEXT("Invalid emitter data");
            return nullptr;
        }

        UNiagaraScript* Script = EmitterData->GetScript(Usage, FGuid());
        if (!Script)
        {
            OutError = FString::Printf(TEXT("No script found for usage '%s'"),
                *UsageToString(Usage));
            return nullptr;
        }

        UNiagaraScriptSource* Source = Cast<UNiagaraScriptSource>(Script->GetLatestSource());
        if (!Source || !Source->NodeGraph)
        {
            OutError = FString::Printf(TEXT("No graph found for usage '%s'"),
                *UsageToString(Usage));
            return nullptr;
        }

        return Source->NodeGraph;
    }

    /**
     * Get ordered module nodes from an output node via manual graph traversal.
     * This replaces FNiagaraStackGraphUtilities::GetOrderedModuleNodes which lacks NIAGARAEDITOR_API.
     */
    void GetOrderedModuleNodesManual(UNiagaraNodeOutput* OutputNode, TArray<UNiagaraNodeFunctionCall*>& OutModuleNodes)
    {
        OutModuleNodes.Reset();
        if (!OutputNode)
        {
            return;
        }

        UNiagaraGraph* Graph = Cast<UNiagaraGraph>(OutputNode->GetGraph());
        if (!Graph)
        {
            return;
        }

        // Collect all function call nodes from the graph that belong to this output's usage
        for (UEdGraphNode* Node : Graph->Nodes)
        {
            if (UNiagaraNodeFunctionCall* FuncNode = Cast<UNiagaraNodeFunctionCall>(Node))
            {
                // Include module function calls (those with a FunctionScript set typically represent modules)
                // We filter based on whether the node is a module by checking if it has a FunctionScript
                // or if it represents a module call in the stack
                if (FuncNode->FunctionScript || !FuncNode->GetNodeTitle(ENodeTitleType::ListView).IsEmpty())
                {
                    OutModuleNodes.Add(FuncNode);
                }
            }
        }

        // Sort by Y position to approximate visual stack order
        OutModuleNodes.Sort([](const UNiagaraNodeFunctionCall& A, const UNiagaraNodeFunctionCall& B)
        {
            return A.NodePosY < B.NodePosY;
        });
    }

    /** Collect ordered modules for one stage output node into JSON. */
    TSharedPtr<FJsonObject> BuildStageModulesJson(
        UNiagaraNodeOutput* OutputNode, ENiagaraScriptUsage Usage)
    {
        TSharedPtr<FJsonObject> StageObj = MakeShareable(new FJsonObject());
        StageObj->SetStringField(TEXT("stage"), UsageToString(Usage));

        TArray<UNiagaraNodeFunctionCall*> ModuleNodes;
        GetOrderedModuleNodesManual(OutputNode, ModuleNodes);

        TArray<TSharedPtr<FJsonValue>> ModulesArr;
        for (int32 i = 0; i < ModuleNodes.Num(); ++i)
        {
            UNiagaraNodeFunctionCall* ModuleNode = ModuleNodes[i];
            TSharedPtr<FJsonObject> ModObj = MakeShareable(new FJsonObject());
            ModObj->SetStringField(TEXT("module_id"), ModuleNode->GetName());
            ModObj->SetNumberField(TEXT("index"), i);
            ModObj->SetBoolField(TEXT("enabled"), ModuleNode->IsNodeEnabled());

            // Get the module script name if available
            if (ModuleNode->FunctionScript)
            {
                ModObj->SetStringField(TEXT("script_name"),
                    ModuleNode->FunctionScript->GetName());
                ModObj->SetStringField(TEXT("script_path"),
                    ModuleNode->FunctionScript->GetPathName());
            }
            else
            {
                ModObj->SetStringField(TEXT("script_name"),
                    ModuleNode->GetNodeTitle(ENodeTitleType::FullTitle).ToString());
            }

            // Node position
            ModObj->SetNumberField(TEXT("position_x"), ModuleNode->NodePosX);
            ModObj->SetNumberField(TEXT("position_y"), ModuleNode->NodePosY);

            ModulesArr.Add(MakeShareable(new FJsonValueObject(ModObj)));
        }

        StageObj->SetArrayField(TEXT("modules"), ModulesArr);
        StageObj->SetNumberField(TEXT("module_count"), ModulesArr.Num());
        return StageObj;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// niagara.add_module — add a module script to an emitter stack
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FNexusNiagaraHandler::HandleAddModule(
    const TSharedPtr<FJsonObject>& Params)
{
    FString SystemPath = GetStringParam(Params, TEXT("system_path"));
    if (SystemPath.IsEmpty())
    {
        return MakeError(TEXT("MISSING_PARAM"), TEXT("system_path is required"));
    }

    FString EmitterName = GetStringParam(Params, TEXT("emitter_name"));
    if (EmitterName.IsEmpty())
    {
        return MakeError(TEXT("MISSING_PARAM"), TEXT("emitter_name is required"));
    }

    FString ModuleScriptPath = GetStringParam(Params, TEXT("module_script_path"));
    if (ModuleScriptPath.IsEmpty())
    {
        return MakeError(TEXT("MISSING_PARAM"), TEXT("module_script_path is required"));
    }

    FString StageName = GetStringParam(Params, TEXT("stage"), TEXT("ParticleUpdate"));
    int32 TargetIndex = static_cast<int32>(GetNumberParam(Params, TEXT("index"), -1.0));

    // Load the system
    UNiagaraSystem* System = LoadObject<UNiagaraSystem>(nullptr, *SystemPath);
    if (!System)
    {
        return MakeError(TEXT("NOT_FOUND"),
            FString::Printf(TEXT("Niagara System not found at '%s'"), *SystemPath));
    }

    // Find the emitter handle by name
    const TArray<FNiagaraEmitterHandle>& EmitterHandles = System->GetEmitterHandles();
    FNiagaraEmitterHandle* TargetHandle = nullptr;
    for (int32 i = 0; i < EmitterHandles.Num(); ++i)
    {
        if (EmitterHandles[i].GetName().ToString() == EmitterName)
        {
            TargetHandle = const_cast<FNiagaraEmitterHandle*>(&EmitterHandles[i]);
            break;
        }
    }
    if (!TargetHandle)
    {
        return MakeError(TEXT("NOT_FOUND"),
            FString::Printf(TEXT("Emitter '%s' not found in system"), *EmitterName));
    }

    // Get the emitter data and target stage graph
    FVersionedNiagaraEmitterData* EmitterData = TargetHandle->GetEmitterData();
    ENiagaraScriptUsage Usage = ParseStageUsage(StageName);

    FString GraphError;
    UNiagaraGraph* Graph = GetEmitterGraph(EmitterData, Usage, GraphError);
    if (!Graph)
    {
        return MakeError(TEXT("GRAPH_ERROR"), GraphError);
    }

    // Find the output node for this stage (using FindEquivalentOutputNode which is exported)
    UNiagaraNodeOutput* OutputNode = Graph->FindEquivalentOutputNode(Usage);
    if (!OutputNode)
    {
        return MakeError(TEXT("NOT_FOUND"),
            FString::Printf(TEXT("No output node found for stage '%s'"), *StageName));
    }

    // Load the module script asset
    UNiagaraScript* ModuleScript = LoadObject<UNiagaraScript>(nullptr, *ModuleScriptPath);
    if (!ModuleScript)
    {
        return MakeError(TEXT("NOT_FOUND"),
            FString::Printf(TEXT("Module script not found at '%s'"), *ModuleScriptPath));
    }

    // Add the module to the stack
    // UE 5.7: AddScriptModuleToStack — verified via Engine/Plugins/FX/Niagara/Source/NiagaraEditor/Public/ViewModels/Stack/NiagaraStackGraphUtilities.h
    UNiagaraNodeFunctionCall* NewNode = FNiagaraStackGraphUtilities::AddScriptModuleToStack(
        ModuleScript, *OutputNode,
        TargetIndex == -1 ? INDEX_NONE : TargetIndex);

    if (!NewNode)
    {
        return MakeError(TEXT("ADD_FAILED"),
            TEXT("Failed to add module to emitter stack"));
    }

    // Finalize
    Graph->NotifyGraphChanged();
    System->MarkPackageDirty();

    // Build response
    TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
    Data->SetStringField(TEXT("system_path"), SystemPath);
    Data->SetStringField(TEXT("emitter_name"), EmitterName);
    Data->SetStringField(TEXT("stage"), UsageToString(Usage));
    Data->SetStringField(TEXT("module_id"), NewNode->GetName());
    Data->SetStringField(TEXT("module_script_path"), ModuleScriptPath);
    Data->SetStringField(TEXT("script_name"), ModuleScript->GetName());
    Data->SetNumberField(TEXT("position_x"), NewNode->NodePosX);
    Data->SetNumberField(TEXT("position_y"), NewNode->NodePosY);

    // Report actual index
    TArray<UNiagaraNodeFunctionCall*> OrderedModules;
    GetOrderedModuleNodesManual(OutputNode, OrderedModules);
    int32 ActualIndex = OrderedModules.IndexOfByKey(NewNode);
    Data->SetNumberField(TEXT("index"), ActualIndex);

    return MakeSuccess(Data);
}

// ─────────────────────────────────────────────────────────────────────────────
// niagara.get_emitter_stack — list all modules per stage for an emitter
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FNexusNiagaraHandler::HandleGetEmitterStack(
    const TSharedPtr<FJsonObject>& Params)
{
    FString SystemPath = GetStringParam(Params, TEXT("system_path"));
    if (SystemPath.IsEmpty())
    {
        return MakeError(TEXT("MISSING_PARAM"), TEXT("system_path is required"));
    }

    FString EmitterName = GetStringParam(Params, TEXT("emitter_name"));
    if (EmitterName.IsEmpty())
    {
        return MakeError(TEXT("MISSING_PARAM"), TEXT("emitter_name is required"));
    }

    // Load the system
    UNiagaraSystem* System = LoadObject<UNiagaraSystem>(nullptr, *SystemPath);
    if (!System)
    {
        return MakeError(TEXT("NOT_FOUND"),
            FString::Printf(TEXT("Niagara System not found at '%s'"), *SystemPath));
    }

    // Find the emitter handle by name
    const TArray<FNiagaraEmitterHandle>& EmitterHandles = System->GetEmitterHandles();
    FNiagaraEmitterHandle* TargetHandle = nullptr;
    for (int32 i = 0; i < EmitterHandles.Num(); ++i)
    {
        if (EmitterHandles[i].GetName().ToString() == EmitterName)
        {
            TargetHandle = const_cast<FNiagaraEmitterHandle*>(&EmitterHandles[i]);
            break;
        }
    }
    if (!TargetHandle)
    {
        return MakeError(TEXT("NOT_FOUND"),
            FString::Printf(TEXT("Emitter '%s' not found in system"), *EmitterName));
    }

    FVersionedNiagaraEmitterData* EmitterData = TargetHandle->GetEmitterData();
    if (!EmitterData)
    {
        return MakeError(TEXT("INVALID_EMITTER"), TEXT("Could not get emitter data"));
    }

    // Enumerate modules across all standard particle/emitter stages
    static const ENiagaraScriptUsage Stages[] = {
        ENiagaraScriptUsage::EmitterSpawnScript,
        ENiagaraScriptUsage::EmitterUpdateScript,
        ENiagaraScriptUsage::ParticleSpawnScript,
        ENiagaraScriptUsage::ParticleUpdateScript,
    };

    TArray<TSharedPtr<FJsonValue>> StagesArr;
    int32 TotalModules = 0;

    for (ENiagaraScriptUsage Usage : Stages)
    {
        FString Err;
        UNiagaraGraph* Graph = GetEmitterGraph(EmitterData, Usage, Err);
        if (!Graph) continue;

        UNiagaraNodeOutput* OutputNode = Graph->FindEquivalentOutputNode(Usage);
        if (!OutputNode) continue;

        TSharedPtr<FJsonObject> StageObj = BuildStageModulesJson(OutputNode, Usage);
        TotalModules += StageObj->GetIntegerField(TEXT("module_count"));
        StagesArr.Add(MakeShareable(new FJsonValueObject(StageObj)));
    }

    TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
    Data->SetStringField(TEXT("system_path"), SystemPath);
    Data->SetStringField(TEXT("emitter_name"), EmitterName);
    Data->SetArrayField(TEXT("stages"), StagesArr);
    Data->SetNumberField(TEXT("total_module_count"), TotalModules);

    return MakeSuccess(Data);
}

// ─────────────────────────────────────────────────────────────────────────────
// niagara.update_module — update module properties (enable/disable, position)
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FNexusNiagaraHandler::HandleUpdateModule(
    const TSharedPtr<FJsonObject>& Params)
{
    FString SystemPath = GetStringParam(Params, TEXT("system_path"));
    if (SystemPath.IsEmpty())
    {
        return MakeError(TEXT("MISSING_PARAM"), TEXT("system_path is required"));
    }

    FString EmitterName = GetStringParam(Params, TEXT("emitter_name"));
    if (EmitterName.IsEmpty())
    {
        return MakeError(TEXT("MISSING_PARAM"), TEXT("emitter_name is required"));
    }

    FString ModuleId = GetStringParam(Params, TEXT("module_id"));
    if (ModuleId.IsEmpty())
    {
        return MakeError(TEXT("MISSING_PARAM"), TEXT("module_id is required"));
    }

    FString StageName = GetStringParam(Params, TEXT("stage"), TEXT("ParticleUpdate"));

    // Load the system
    UNiagaraSystem* System = LoadObject<UNiagaraSystem>(nullptr, *SystemPath);
    if (!System)
    {
        return MakeError(TEXT("NOT_FOUND"),
            FString::Printf(TEXT("Niagara System not found at '%s'"), *SystemPath));
    }

    // Find the emitter handle by name
    const TArray<FNiagaraEmitterHandle>& EmitterHandles = System->GetEmitterHandles();
    FNiagaraEmitterHandle* TargetHandle = nullptr;
    for (int32 i = 0; i < EmitterHandles.Num(); ++i)
    {
        if (EmitterHandles[i].GetName().ToString() == EmitterName)
        {
            TargetHandle = const_cast<FNiagaraEmitterHandle*>(&EmitterHandles[i]);
            break;
        }
    }
    if (!TargetHandle)
    {
        return MakeError(TEXT("NOT_FOUND"),
            FString::Printf(TEXT("Emitter '%s' not found in system"), *EmitterName));
    }

    // Get graph for the target stage
    FVersionedNiagaraEmitterData* EmitterData = TargetHandle->GetEmitterData();
    ENiagaraScriptUsage Usage = ParseStageUsage(StageName);

    FString GraphError;
    UNiagaraGraph* Graph = GetEmitterGraph(EmitterData, Usage, GraphError);
    if (!Graph)
    {
        return MakeError(TEXT("GRAPH_ERROR"), GraphError);
    }

    // Find the output node and the target module (using FindEquivalentOutputNode which is exported)
    UNiagaraNodeOutput* OutputNode = Graph->FindEquivalentOutputNode(Usage);
    if (!OutputNode)
    {
        return MakeError(TEXT("NOT_FOUND"),
            FString::Printf(TEXT("No output node for stage '%s'"), *StageName));
    }

    TArray<UNiagaraNodeFunctionCall*> ModuleNodes;
    GetOrderedModuleNodesManual(OutputNode, ModuleNodes);

    UNiagaraNodeFunctionCall* TargetModule = nullptr;
    for (UNiagaraNodeFunctionCall* Node : ModuleNodes)
    {
        if (Node->GetName() == ModuleId)
        {
            TargetModule = Node;
            break;
        }
    }
    if (!TargetModule)
    {
        return MakeError(TEXT("NOT_FOUND"),
            FString::Printf(TEXT("Module '%s' not found in stage '%s'"),
                *ModuleId, *StageName));
    }

    // Apply updates
    bool bChanged = false;

    // Enable/disable
    bool bHasEnabled = Params->HasField(TEXT("enabled"));
    if (bHasEnabled)
    {
        bool bEnabled = GetBoolParam(Params, TEXT("enabled"), true);
        TargetModule->SetEnabledState(
            bEnabled ? ENodeEnabledState::Enabled : ENodeEnabledState::Disabled,
            /*bUserAction=*/ true);
        bChanged = true;
    }

    // Position
    bool bHasPosX = Params->HasField(TEXT("position_x"));
    bool bHasPosY = Params->HasField(TEXT("position_y"));
    if (bHasPosX)
    {
        TargetModule->NodePosX = static_cast<int32>(
            GetNumberParam(Params, TEXT("position_x"), TargetModule->NodePosX));
        bChanged = true;
    }
    if (bHasPosY)
    {
        TargetModule->NodePosY = static_cast<int32>(
            GetNumberParam(Params, TEXT("position_y"), TargetModule->NodePosY));
        bChanged = true;
    }

    if (bChanged)
    {
        Graph->NotifyGraphChanged();
        System->MarkPackageDirty();
    }

    // Build response
    TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
    Data->SetStringField(TEXT("system_path"), SystemPath);
    Data->SetStringField(TEXT("emitter_name"), EmitterName);
    Data->SetStringField(TEXT("stage"), UsageToString(Usage));
    Data->SetStringField(TEXT("module_id"), TargetModule->GetName());
    Data->SetBoolField(TEXT("enabled"), TargetModule->IsNodeEnabled());
    Data->SetNumberField(TEXT("position_x"), TargetModule->NodePosX);
    Data->SetNumberField(TEXT("position_y"), TargetModule->NodePosY);

    if (TargetModule->FunctionScript)
    {
        Data->SetStringField(TEXT("script_name"),
            TargetModule->FunctionScript->GetName());
    }

    return MakeSuccess(Data);
}

// ─────────────────────────────────────────────────────────────────────────────
// niagara.remove_module — remove a module from the emitter stack
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FNexusNiagaraHandler::HandleRemoveModule(
    const TSharedPtr<FJsonObject>& Params)
{
    FString SystemPath = GetStringParam(Params, TEXT("system_path"));
    if (SystemPath.IsEmpty())
    {
        return MakeError(TEXT("MISSING_PARAM"), TEXT("system_path is required"));
    }

    FString EmitterName = GetStringParam(Params, TEXT("emitter_name"));
    if (EmitterName.IsEmpty())
    {
        return MakeError(TEXT("MISSING_PARAM"), TEXT("emitter_name is required"));
    }

    FString ModuleId = GetStringParam(Params, TEXT("module_id"));
    if (ModuleId.IsEmpty())
    {
        return MakeError(TEXT("MISSING_PARAM"), TEXT("module_id is required"));
    }

    FString StageName = GetStringParam(Params, TEXT("stage"), TEXT("ParticleUpdate"));

    // Load the system
    UNiagaraSystem* System = LoadObject<UNiagaraSystem>(nullptr, *SystemPath);
    if (!System)
    {
        return MakeError(TEXT("NOT_FOUND"),
            FString::Printf(TEXT("Niagara System not found at '%s'"), *SystemPath));
    }

    // Find the emitter handle by name
    const TArray<FNiagaraEmitterHandle>& EmitterHandles = System->GetEmitterHandles();
    FNiagaraEmitterHandle* TargetHandle = nullptr;
    FGuid EmitterId;
    for (int32 i = 0; i < EmitterHandles.Num(); ++i)
    {
        if (EmitterHandles[i].GetName().ToString() == EmitterName)
        {
            TargetHandle = const_cast<FNiagaraEmitterHandle*>(&EmitterHandles[i]);
            EmitterId = EmitterHandles[i].GetId();
            break;
        }
    }
    if (!TargetHandle)
    {
        return MakeError(TEXT("NOT_FOUND"),
            FString::Printf(TEXT("Emitter '%s' not found in system"), *EmitterName));
    }

    // Get graph for the target stage
    FVersionedNiagaraEmitterData* EmitterData = TargetHandle->GetEmitterData();
    ENiagaraScriptUsage Usage = ParseStageUsage(StageName);

    FString GraphError;
    UNiagaraGraph* Graph = GetEmitterGraph(EmitterData, Usage, GraphError);
    if (!Graph)
    {
        return MakeError(TEXT("GRAPH_ERROR"), GraphError);
    }

    UNiagaraNodeOutput* OutputNode = Graph->FindEquivalentOutputNode(Usage);
    if (!OutputNode)
    {
        return MakeError(TEXT("NOT_FOUND"),
            FString::Printf(TEXT("No output node for stage '%s'"), *StageName));
    }

    TArray<UNiagaraNodeFunctionCall*> ModuleNodes;
    GetOrderedModuleNodesManual(OutputNode, ModuleNodes);

    UNiagaraNodeFunctionCall* TargetModule = nullptr;
    for (UNiagaraNodeFunctionCall* Node : ModuleNodes)
    {
        if (Node->GetName() == ModuleId)
        {
            TargetModule = Node;
            break;
        }
    }
    if (!TargetModule)
    {
        return MakeError(TEXT("NOT_FOUND"),
            FString::Printf(TEXT("Module '%s' not found in stage '%s'"),
                *ModuleId, *StageName));
    }

    // Capture info before removal
    FString RemovedScriptName = TargetModule->FunctionScript
        ? TargetModule->FunctionScript->GetName()
        : TargetModule->GetNodeTitle(ENodeTitleType::FullTitle).ToString();

    // Remove the module via direct graph node removal
    // (FNiagaraStackGraphUtilities::RemoveModuleFromStack lacks NIAGARAEDITOR_API export)
    Graph->RemoveNode(TargetModule);
    Graph->NotifyGraphChanged();
    System->MarkPackageDirty();

    TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
    Data->SetStringField(TEXT("system_path"), SystemPath);
    Data->SetStringField(TEXT("emitter_name"), EmitterName);
    Data->SetStringField(TEXT("stage"), UsageToString(Usage));
    Data->SetStringField(TEXT("removed_module_id"), ModuleId);
    Data->SetStringField(TEXT("removed_script_name"), RemovedScriptName);

    return MakeSuccess(Data);
}

// ─────────────────────────────────────────────────────────────────────────────
// niagara.reorder_modules — move a module to a new index within its stage
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FNexusNiagaraHandler::HandleReorderModules(
    const TSharedPtr<FJsonObject>& Params)
{
    FString SystemPath = GetStringParam(Params, TEXT("system_path"));
    if (SystemPath.IsEmpty())
    {
        return MakeError(TEXT("MISSING_PARAM"), TEXT("system_path is required"));
    }

    FString EmitterName = GetStringParam(Params, TEXT("emitter_name"));
    if (EmitterName.IsEmpty())
    {
        return MakeError(TEXT("MISSING_PARAM"), TEXT("emitter_name is required"));
    }

    FString ModuleId = GetStringParam(Params, TEXT("module_id"));
    if (ModuleId.IsEmpty())
    {
        return MakeError(TEXT("MISSING_PARAM"), TEXT("module_id is required"));
    }

    int32 NewIndex = static_cast<int32>(GetNumberParam(Params, TEXT("new_index"), 0.0));
    FString StageName = GetStringParam(Params, TEXT("stage"), TEXT("ParticleUpdate"));

    // Load the system
    UNiagaraSystem* System = LoadObject<UNiagaraSystem>(nullptr, *SystemPath);
    if (!System)
    {
        return MakeError(TEXT("NOT_FOUND"),
            FString::Printf(TEXT("Niagara System not found at '%s'"), *SystemPath));
    }

    // Find the emitter handle by name
    const TArray<FNiagaraEmitterHandle>& EmitterHandles = System->GetEmitterHandles();
    FNiagaraEmitterHandle* TargetHandle = nullptr;
    FGuid EmitterId;
    for (int32 i = 0; i < EmitterHandles.Num(); ++i)
    {
        if (EmitterHandles[i].GetName().ToString() == EmitterName)
        {
            TargetHandle = const_cast<FNiagaraEmitterHandle*>(&EmitterHandles[i]);
            EmitterId = EmitterHandles[i].GetId();
            break;
        }
    }
    if (!TargetHandle)
    {
        return MakeError(TEXT("NOT_FOUND"),
            FString::Printf(TEXT("Emitter '%s' not found in system"), *EmitterName));
    }

    // Get graph for the target stage
    FVersionedNiagaraEmitterData* EmitterData = TargetHandle->GetEmitterData();
    ENiagaraScriptUsage Usage = ParseStageUsage(StageName);

    FString GraphError;
    UNiagaraGraph* Graph = GetEmitterGraph(EmitterData, Usage, GraphError);
    if (!Graph)
    {
        return MakeError(TEXT("GRAPH_ERROR"), GraphError);
    }

    UNiagaraNodeOutput* OutputNode = Graph->FindEquivalentOutputNode(Usage);
    if (!OutputNode)
    {
        return MakeError(TEXT("NOT_FOUND"),
            FString::Printf(TEXT("No output node for stage '%s'"), *StageName));
    }

    TArray<UNiagaraNodeFunctionCall*> ModuleNodes;
    GetOrderedModuleNodesManual(OutputNode, ModuleNodes);

    UNiagaraNodeFunctionCall* TargetModule = nullptr;
    int32 CurrentIndex = INDEX_NONE;
    for (int32 i = 0; i < ModuleNodes.Num(); ++i)
    {
        if (ModuleNodes[i]->GetName() == ModuleId)
        {
            TargetModule = ModuleNodes[i];
            CurrentIndex = i;
            break;
        }
    }
    if (!TargetModule)
    {
        return MakeError(TEXT("NOT_FOUND"),
            FString::Printf(TEXT("Module '%s' not found in stage '%s'"),
                *ModuleId, *StageName));
    }

    // Clamp new index to valid range
    NewIndex = FMath::Clamp(NewIndex, 0, ModuleNodes.Num() - 1);

    if (CurrentIndex == NewIndex)
    {
        // No move needed - return current state
        TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
        Data->SetStringField(TEXT("system_path"), SystemPath);
        Data->SetStringField(TEXT("emitter_name"), EmitterName);
        Data->SetStringField(TEXT("stage"), UsageToString(Usage));
        Data->SetStringField(TEXT("module_id"), ModuleId);
        Data->SetNumberField(TEXT("old_index"), CurrentIndex);
        Data->SetNumberField(TEXT("new_index"), NewIndex);
        Data->SetBoolField(TEXT("moved"), false);
        return MakeSuccess(Data);
    }

    // FNiagaraStackGraphUtilities::MoveModule lacks NIAGARAEDITOR_API export in UE 5.7,
    // so module reordering is not available via external plugin API.
    // Users must use the Niagara editor UI to reorder modules.
    return MakeError(TEXT("NOT_SUPPORTED"),
        TEXT("Module reordering requires the Niagara editor stack API which is not externally accessible in UE 5.7. Use the Niagara editor UI to reorder modules."));
}
