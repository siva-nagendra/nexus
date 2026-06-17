// Copyright Nexus Team. All Rights Reserved.

#pragma once

#include "NexusCommandHandler.h"

/**
 * Handler for Niagara VFX subsystem commands: system/emitter creation,
 * parameter and variable setting, activation, spawning, querying,
 * and module stack CRUD (add/get/update/remove/reorder).
 * Namespace: "niagara", 13 commands.
 */
class FNexusNiagaraHandler : public INexusCommandHandler
{
public:
    virtual FString GetNamespace() const override { return TEXT("niagara"); }

    virtual TArray<FString> GetSupportedCommands() const override
    {
        return {
            TEXT("create_niagara_system"),
            TEXT("create_niagara_emitter"),
            TEXT("set_niagara_parameter"),
            TEXT("set_niagara_variable"),
            TEXT("activate_niagara_system"),
            TEXT("spawn_niagara_at_location"),
            TEXT("get_niagara_info"),
            TEXT("list_niagara_modules"),
            // Module stack CRUD (Node CRUD Phase 2)
            TEXT("add_module"),
            TEXT("get_emitter_stack"),
            TEXT("update_module"),
            TEXT("remove_module"),
            TEXT("reorder_modules")
        };
    }

    virtual TSharedPtr<FJsonObject> Handle(
        const FString& CommandType,
        const TSharedPtr<FJsonObject>& Params) override;

private:
    // Actor resolution helpers
    AActor* FindActorByPath(const FString& Path);
    AActor* FindActorByLabel(const FString& Label);
    AActor* ResolveActor(const TSharedPtr<FJsonObject>& Params,
        const FString& ParamName, FString& OutError);

    // Command handlers
    TSharedPtr<FJsonObject> HandleCreateNiagaraSystem(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleCreateNiagaraEmitter(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleSetNiagaraParameter(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleSetNiagaraVariable(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleActivateNiagaraSystem(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleSpawnNiagaraAtLocation(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleGetNiagaraInfo(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleListNiagaraModules(const TSharedPtr<FJsonObject>& Params);

    // Module stack CRUD (Node CRUD Phase 2)
    TSharedPtr<FJsonObject> HandleAddModule(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleGetEmitterStack(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleUpdateModule(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleRemoveModule(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleReorderModules(const TSharedPtr<FJsonObject>& Params);
};
