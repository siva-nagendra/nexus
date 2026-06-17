// Copyright Nexus Team. All Rights Reserved.

#pragma once

#include "NexusCommandHandler.h"

/**
 * Handler for animation subsystem commands: anim blueprints, montages,
 * blend spaces, skeletons, notifies, IK, retargeting, and control rigs.
 * Namespace: "animation", 16 commands.
 */
class FNexusAnimationHandler : public INexusCommandHandler
{
public:
    virtual FString GetNamespace() const override { return TEXT("animation"); }

    virtual TArray<FString> GetSupportedCommands() const override
    {
        return {
            TEXT("get_anim_blueprint_info"),
            TEXT("list_anim_sequences"),
            TEXT("get_skeleton_info"),
            TEXT("list_anim_notifies"),
            TEXT("get_retarget_info"),
            TEXT("create_anim_montage"),
            TEXT("set_anim_blueprint"),
            TEXT("create_blend_space"),
            TEXT("set_ik_settings"),
            TEXT("create_control_rig"),
            TEXT("add_anim_notify"),
            TEXT("apply_retarget"),
            TEXT("add_blend_space_sample"),
            TEXT("get_blend_space_samples"),
            TEXT("update_blend_space_sample"),
            TEXT("remove_blend_space_sample")
        };
    }

    virtual TSharedPtr<FJsonObject> Handle(
        const FString& CommandType,
        const TSharedPtr<FJsonObject>& Params) override;

private:
    TSharedPtr<FJsonObject> HandleGetAnimBlueprintInfo(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleListAnimSequences(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleGetSkeletonInfo(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleListAnimNotifies(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleGetRetargetInfo(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleCreateAnimMontage(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleSetAnimBlueprint(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleCreateBlendSpace(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleSetIkSettings(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleCreateControlRig(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleAddAnimNotify(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleApplyRetarget(const TSharedPtr<FJsonObject>& Params);

    // Blend Space sample CRUD
    TSharedPtr<FJsonObject> HandleAddBlendSpaceSample(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleGetBlendSpaceSamples(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleUpdateBlendSpaceSample(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleRemoveBlendSpaceSample(const TSharedPtr<FJsonObject>& Params);
};
