// Copyright Nexus Team. All Rights Reserved.

#pragma once

#include "NexusCommandHandler.h"
#include "MaterialShared.h"

class UMaterial;
class UMaterialExpression;

class FNexusMaterialHandler : public INexusCommandHandler
{
public:
    virtual FString GetNamespace() const override { return TEXT("material"); }

    virtual TArray<FString> GetSupportedCommands() const override
    {
        return {
            TEXT("create"), TEXT("create_instance"), TEXT("info"),
            TEXT("set_scalar_parameter"), TEXT("set_vector_parameter"),
            TEXT("set_texture_parameter"), TEXT("get_parameters"),
            TEXT("set_shading_model"), TEXT("set_blend_mode"),
            TEXT("apply_to_actor"), TEXT("set_two_sided"),
            TEXT("get_expressions"),
            // Batch operations (A3)
            TEXT("apply_batch"),
            // Expression CRUD (Node CRUD Phase 1)
            TEXT("add_expression"),
            TEXT("get_expression_pins"),
            TEXT("update_expression"),
            TEXT("remove_expression"),
            TEXT("connect_expressions"),
            TEXT("disconnect_expression")
        };
    }

    virtual TSharedPtr<FJsonObject> Handle(
        const FString& CommandType,
        const TSharedPtr<FJsonObject>& Params) override;

private:
    TSharedPtr<FJsonObject> HandleCreate(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleCreateInstance(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleInfo(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleSetScalarParameter(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleSetVectorParameter(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleSetTextureParameter(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleGetParameters(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleSetShadingModel(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleSetBlendMode(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleApplyToActor(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleSetTwoSided(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleGetExpressions(const TSharedPtr<FJsonObject>& Params);

    // --- Batch operations (A3) ---
    TSharedPtr<FJsonObject> HandleApplyBatch(const TSharedPtr<FJsonObject>& Params);

    // --- Expression CRUD (Node CRUD Phase 1) ---
    TSharedPtr<FJsonObject> HandleAddExpression(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleGetExpressionPins(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleUpdateExpression(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleRemoveExpression(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleConnectExpressions(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleDisconnectExpression(const TSharedPtr<FJsonObject>& Params);

    // Helpers
    static EMaterialShadingModel ParseShadingModel(const FString& Model);
    static EBlendMode ParseBlendMode(const FString& Mode);

    /** Find a material expression by name within a material. */
    static UMaterialExpression* FindExpressionById(UMaterial* Material, const FString& ExpressionId);
};
