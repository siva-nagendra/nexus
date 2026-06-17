// Copyright Nexus Team. All Rights Reserved.

#pragma once

#include "NexusCommandHandler.h"

/**
 * Handler for UMG (Unreal Motion Graphics) UI subsystem commands:
 * widget blueprint creation, widget animations, viewport management,
 * property setting, info queries, and binding inspection.
 * Namespace: "umg", 7 commands.
 */
class FNexusUIHandler : public INexusCommandHandler
{
public:
    virtual FString GetNamespace() const override { return TEXT("umg"); }

    virtual TArray<FString> GetSupportedCommands() const override
    {
        return {
            TEXT("create_widget_blueprint"),
            TEXT("create_widget_animation"),
            TEXT("add_widget_to_viewport"),
            TEXT("remove_widget_from_viewport"),
            TEXT("set_widget_property"),
            TEXT("get_widget_info"),
            TEXT("list_widget_bindings")
        };
    }

    virtual TSharedPtr<FJsonObject> Handle(
        const FString& CommandType,
        const TSharedPtr<FJsonObject>& Params) override;

private:
    TSharedPtr<FJsonObject> HandleCreateWidgetBlueprint(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleCreateWidgetAnimation(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleAddWidgetToViewport(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleRemoveWidgetFromViewport(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleSetWidgetProperty(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleGetWidgetInfo(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleListWidgetBindings(const TSharedPtr<FJsonObject>& Params);
};
