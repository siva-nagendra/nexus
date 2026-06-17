// Copyright Nexus Team. All Rights Reserved.

#pragma once

#include "NexusCommandHandler.h"

/**
 * Handler for Code Analysis / Reflection commands:
 * Inspects UE's UClass hierarchy, UPROPERTY fields, UFUNCTION methods,
 * and loaded modules via the reflection system. All commands are read-only.
 * Namespace: "code", 6 commands.
 *
 * Key UE APIs: TObjectIterator<UClass>, FProperty / TFieldIterator<FProperty>,
 *              UFunction, FModuleManager.
 */
class FNexusCodeAnalysisHandler : public INexusCommandHandler
{
public:
    virtual FString GetNamespace() const override { return TEXT("code"); }

    virtual TArray<FString> GetSupportedCommands() const override
    {
        return {
            TEXT("get_class_hierarchy"),
            TEXT("list_classes"),
            TEXT("get_class_properties"),
            TEXT("get_class_functions"),
            TEXT("search_classes"),
            TEXT("list_modules")
        };
    }

    virtual TSharedPtr<FJsonObject> Handle(
        const FString& CommandType,
        const TSharedPtr<FJsonObject>& Params) override;

private:
    // Command handlers
    TSharedPtr<FJsonObject> HandleGetClassHierarchy(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleListClasses(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleGetClassProperties(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleGetClassFunctions(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleSearchClasses(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleListModules(const TSharedPtr<FJsonObject>& Params);

    // Helpers
    UClass* FindClassByName(const FString& ClassName);
    TSharedPtr<FJsonObject> ClassToJson(const UClass* Class);
    TSharedPtr<FJsonObject> BuildChildTree(const UClass* ParentClass, int32 Depth, int32 MaxDepth, bool bIncludeAbstract);
    FString PropertyTypeToString(const FProperty* Property);
    FString PropertyFlagsToString(const FProperty* Property);
    FString FunctionFlagsToString(const UFunction* Function);
};
