// Copyright Nexus Team. All Rights Reserved.

#include "Handlers/NexusCodeAnalysisHandler.h"
#include "UObject/UObjectIterator.h"
#include "UObject/Class.h"
#include "UObject/UnrealType.h"
#include "UObject/Package.h"
#include "Modules/ModuleManager.h"

// ─────────────────────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────────────────────

UClass* FNexusCodeAnalysisHandler::FindClassByName(const FString& ClassName)
{
    if (ClassName.IsEmpty()) return nullptr;

    // Try direct lookup first (e.g., "AActor", "UStaticMeshComponent")
    // In UE 5.x, use FindFirstObject instead of ANY_PACKAGE
    UClass* Found = FindFirstObject<UClass>(*ClassName, EFindFirstObjectOptions::ExactClass);
    if (Found) return Found;

    // Try with common prefixes
    static const TCHAR* Prefixes[] = { TEXT("A"), TEXT("U"), TEXT("F") };
    for (const TCHAR* Prefix : Prefixes)
    {
        FString PrefixedName = FString(Prefix) + ClassName;
        Found = FindFirstObject<UClass>(*PrefixedName, EFindFirstObjectOptions::ExactClass);
        if (Found) return Found;
    }

    // Try case-insensitive scan
    FString LowerName = ClassName.ToLower();
    for (TObjectIterator<UClass> It; It; ++It)
    {
        if (It->GetName().ToLower() == LowerName ||
            It->GetFName().ToString().ToLower() == LowerName)
        {
            return *It;
        }
    }

    return nullptr;
}

TSharedPtr<FJsonObject> FNexusCodeAnalysisHandler::ClassToJson(const UClass* Class)
{
    TSharedPtr<FJsonObject> Obj = MakeShareable(new FJsonObject());
    Obj->SetStringField(TEXT("class_name"), Class->GetName());
    Obj->SetStringField(TEXT("full_path"), Class->GetPathName());

    if (const UClass* SuperClass = Class->GetSuperClass())
    {
        Obj->SetStringField(TEXT("parent_class"), SuperClass->GetName());
    }
    else
    {
        Obj->SetStringField(TEXT("parent_class"), TEXT(""));
    }

    // Module
    UPackage* Package = Class->GetOuterUPackage();
    if (Package)
    {
        Obj->SetStringField(TEXT("module"), Package->GetName());
    }

    Obj->SetBoolField(TEXT("is_abstract"), Class->HasAnyClassFlags(CLASS_Abstract));
    Obj->SetBoolField(TEXT("is_deprecated"), Class->HasAnyClassFlags(CLASS_Deprecated));
    Obj->SetStringField(TEXT("cdo_path"), Class->GetDefaultObject() ? Class->GetDefaultObject()->GetPathName() : TEXT(""));

    return Obj;
}

TSharedPtr<FJsonObject> FNexusCodeAnalysisHandler::BuildChildTree(
    const UClass* ParentClass, int32 Depth, int32 MaxDepth, bool bIncludeAbstract)
{
    TSharedPtr<FJsonObject> Node = MakeShareable(new FJsonObject());
    Node->SetStringField(TEXT("class_name"), ParentClass->GetName());
    Node->SetBoolField(TEXT("is_abstract"), ParentClass->HasAnyClassFlags(CLASS_Abstract));

    // Stop recursing if we hit max depth
    if (MaxDepth >= 0 && Depth >= MaxDepth)
    {
        Node->SetArrayField(TEXT("children"), TArray<TSharedPtr<FJsonValue>>());
        return Node;
    }

    TArray<TSharedPtr<FJsonValue>> ChildrenArr;
    for (TObjectIterator<UClass> It; It; ++It)
    {
        if (It->GetSuperClass() == ParentClass)
        {
            if (!bIncludeAbstract && It->HasAnyClassFlags(CLASS_Abstract))
            {
                continue;
            }
            TSharedPtr<FJsonObject> ChildNode = BuildChildTree(*It, Depth + 1, MaxDepth, bIncludeAbstract);
            ChildrenArr.Add(MakeShareable(new FJsonValueObject(ChildNode)));
        }
    }

    Node->SetArrayField(TEXT("children"), ChildrenArr);
    Node->SetNumberField(TEXT("child_count"), ChildrenArr.Num());

    return Node;
}

FString FNexusCodeAnalysisHandler::PropertyTypeToString(const FProperty* Property)
{
    if (!Property) return TEXT("Unknown");

    // Extended type string gives us the full type info (e.g., "TArray<FString>")
    return Property->GetCPPType();
}

FString FNexusCodeAnalysisHandler::PropertyFlagsToString(const FProperty* Property)
{
    if (!Property) return TEXT("");

    TArray<FString> Flags;

    if (Property->HasAnyPropertyFlags(CPF_Edit))
        Flags.Add(TEXT("EditAnywhere"));
    if (Property->HasAnyPropertyFlags(CPF_BlueprintVisible))
        Flags.Add(TEXT("BlueprintVisible"));
    if (Property->HasAnyPropertyFlags(CPF_BlueprintReadOnly))
        Flags.Add(TEXT("BlueprintReadOnly"));
    if (Property->HasAnyPropertyFlags(CPF_Net))
        Flags.Add(TEXT("Replicated"));
    if (Property->HasAnyPropertyFlags(CPF_Config))
        Flags.Add(TEXT("Config"));
    if (Property->HasAnyPropertyFlags(CPF_Transient))
        Flags.Add(TEXT("Transient"));
    if (Property->HasAnyPropertyFlags(CPF_SaveGame))
        Flags.Add(TEXT("SaveGame"));
    if (Property->HasAnyPropertyFlags(CPF_ExposeOnSpawn))
        Flags.Add(TEXT("ExposeOnSpawn"));
    if (Property->HasAnyPropertyFlags(CPF_Interp))
        Flags.Add(TEXT("Interp"));
    if (Property->HasAnyPropertyFlags(CPF_EditConst))
        Flags.Add(TEXT("EditConst"));

    return FString::Join(Flags, TEXT(", "));
}

FString FNexusCodeAnalysisHandler::FunctionFlagsToString(const UFunction* Function)
{
    if (!Function) return TEXT("");

    TArray<FString> Flags;

    if (Function->HasAnyFunctionFlags(FUNC_BlueprintCallable))
        Flags.Add(TEXT("BlueprintCallable"));
    if (Function->HasAnyFunctionFlags(FUNC_BlueprintPure))
        Flags.Add(TEXT("BlueprintPure"));
    if (Function->HasAnyFunctionFlags(FUNC_BlueprintEvent))
        Flags.Add(TEXT("BlueprintImplementableEvent"));
    if (Function->HasAnyFunctionFlags(FUNC_Net))
        Flags.Add(TEXT("Net"));
    if (Function->HasAnyFunctionFlags(FUNC_NetServer))
        Flags.Add(TEXT("Server"));
    if (Function->HasAnyFunctionFlags(FUNC_NetClient))
        Flags.Add(TEXT("Client"));
    if (Function->HasAnyFunctionFlags(FUNC_NetMulticast))
        Flags.Add(TEXT("NetMulticast"));
    if (Function->HasAnyFunctionFlags(FUNC_Exec))
        Flags.Add(TEXT("Exec"));
    if (Function->HasAnyFunctionFlags(FUNC_Static))
        Flags.Add(TEXT("Static"));
    if (Function->HasAnyFunctionFlags(FUNC_Const))
        Flags.Add(TEXT("Const"));
    if (Function->HasAnyFunctionFlags(FUNC_Native))
        Flags.Add(TEXT("Native"));

    return FString::Join(Flags, TEXT(", "));
}

// ─────────────────────────────────────────────────────────────────────────────
// Dispatch
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FNexusCodeAnalysisHandler::Handle(
    const FString& CommandType,
    const TSharedPtr<FJsonObject>& Params)
{
    FString SubCommand = CommandType.RightChop(GetNamespace().Len() + 1);

    if (SubCommand == TEXT("get_class_hierarchy"))  return HandleGetClassHierarchy(Params);
    if (SubCommand == TEXT("list_classes"))          return HandleListClasses(Params);
    if (SubCommand == TEXT("get_class_properties"))  return HandleGetClassProperties(Params);
    if (SubCommand == TEXT("get_class_functions"))   return HandleGetClassFunctions(Params);
    if (SubCommand == TEXT("search_classes"))        return HandleSearchClasses(Params);
    if (SubCommand == TEXT("list_modules"))          return HandleListModules(Params);

    return MakeError(TEXT("NOT_IMPLEMENTED"),
        FString::Printf(TEXT("Command '%s' not yet implemented"), *CommandType));
}

// ─────────────────────────────────────────────────────────────────────────────
// code.get_class_hierarchy
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FNexusCodeAnalysisHandler::HandleGetClassHierarchy(
    const TSharedPtr<FJsonObject>& Params)
{
    FString ClassName = GetStringParam(Params, TEXT("class_name"));
    if (ClassName.IsEmpty())
    {
        return MakeError(TEXT("MISSING_PARAM"), TEXT("class_name is required"));
    }

    int32 MaxDepth = static_cast<int32>(GetNumberParam(Params, TEXT("depth"), -1.0));
    bool bIncludeAbstract = GetBoolParam(Params, TEXT("include_abstract"), true);

    UClass* TargetClass = FindClassByName(ClassName);
    if (!TargetClass)
    {
        return MakeError(TEXT("CLASS_NOT_FOUND"),
            FString::Printf(TEXT("Class '%s' not found"), *ClassName));
    }

    // Build parent chain (from target up to UObject)
    TArray<TSharedPtr<FJsonValue>> ParentChain;
    const UClass* Current = TargetClass->GetSuperClass();
    while (Current)
    {
        TSharedPtr<FJsonObject> ParentObj = MakeShareable(new FJsonObject());
        ParentObj->SetStringField(TEXT("class_name"), Current->GetName());
        ParentObj->SetBoolField(TEXT("is_abstract"), Current->HasAnyClassFlags(CLASS_Abstract));
        ParentChain.Add(MakeShareable(new FJsonValueObject(ParentObj)));
        Current = Current->GetSuperClass();
    }

    // Build child tree
    TSharedPtr<FJsonObject> ChildTree = BuildChildTree(TargetClass, 0, MaxDepth, bIncludeAbstract);

    TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
    Data->SetStringField(TEXT("class_name"), TargetClass->GetName());
    Data->SetStringField(TEXT("full_path"), TargetClass->GetPathName());
    Data->SetBoolField(TEXT("is_abstract"), TargetClass->HasAnyClassFlags(CLASS_Abstract));
    Data->SetArrayField(TEXT("parent_chain"), ParentChain);
    Data->SetObjectField(TEXT("child_tree"), ChildTree);

    return MakeSuccess(Data);
}

// ─────────────────────────────────────────────────────────────────────────────
// code.list_classes
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FNexusCodeAnalysisHandler::HandleListClasses(
    const TSharedPtr<FJsonObject>& Params)
{
    FString ParentClassName = GetStringParam(Params, TEXT("parent_class"), TEXT("Object"));
    FString ModuleName = GetStringParam(Params, TEXT("module_name"));
    bool bIncludeAbstract = GetBoolParam(Params, TEXT("include_abstract"), false);
    bool bIncludeDeprecated = GetBoolParam(Params, TEXT("include_deprecated"), false);
    int32 Limit = static_cast<int32>(GetNumberParam(Params, TEXT("limit"), 100.0));
    int32 Offset = static_cast<int32>(GetNumberParam(Params, TEXT("offset"), 0.0));

    UClass* ParentClass = FindClassByName(ParentClassName);
    if (!ParentClass)
    {
        return MakeError(TEXT("CLASS_NOT_FOUND"),
            FString::Printf(TEXT("Parent class '%s' not found"), *ParentClassName));
    }

    FString LowerModuleName = ModuleName.ToLower();

    TArray<TSharedPtr<FJsonValue>> ClassesArr;
    int32 TotalCount = 0;
    int32 Skipped = 0;

    for (TObjectIterator<UClass> It; It; ++It)
    {
        UClass* Class = *It;

        // Filter: must be subclass of parent
        if (!Class->IsChildOf(ParentClass)) continue;

        // Filter: abstract
        if (!bIncludeAbstract && Class->HasAnyClassFlags(CLASS_Abstract)) continue;

        // Filter: deprecated
        if (!bIncludeDeprecated && Class->HasAnyClassFlags(CLASS_Deprecated)) continue;

        // Filter: module
        if (!ModuleName.IsEmpty())
        {
            UPackage* Package = Class->GetOuterUPackage();
            if (!Package || !Package->GetName().ToLower().Contains(LowerModuleName))
            {
                continue;
            }
        }

        TotalCount++;

        // Pagination
        if (Skipped < Offset)
        {
            Skipped++;
            continue;
        }

        if (ClassesArr.Num() >= Limit) continue; // Keep counting total

        ClassesArr.Add(MakeShareable(new FJsonValueObject(ClassToJson(Class))));
    }

    TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
    Data->SetArrayField(TEXT("classes"), ClassesArr);
    Data->SetNumberField(TEXT("count"), ClassesArr.Num());
    Data->SetNumberField(TEXT("total_count"), TotalCount);
    Data->SetNumberField(TEXT("offset"), Offset);
    Data->SetNumberField(TEXT("limit"), Limit);
    Data->SetStringField(TEXT("parent_class"), ParentClass->GetName());

    return MakeSuccess(Data);
}

// ─────────────────────────────────────────────────────────────────────────────
// code.get_class_properties
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FNexusCodeAnalysisHandler::HandleGetClassProperties(
    const TSharedPtr<FJsonObject>& Params)
{
    FString ClassName = GetStringParam(Params, TEXT("class_name"));
    if (ClassName.IsEmpty())
    {
        return MakeError(TEXT("MISSING_PARAM"), TEXT("class_name is required"));
    }

    bool bIncludeInherited = GetBoolParam(Params, TEXT("include_inherited"), false);
    FString FilterCategory = GetStringParam(Params, TEXT("filter_category"));
    FString FilterName = GetStringParam(Params, TEXT("filter_name"));

    UClass* TargetClass = FindClassByName(ClassName);
    if (!TargetClass)
    {
        return MakeError(TEXT("CLASS_NOT_FOUND"),
            FString::Printf(TEXT("Class '%s' not found"), *ClassName));
    }

    FString LowerCategory = FilterCategory.ToLower();
    FString LowerFilterName = FilterName.ToLower();

    TArray<TSharedPtr<FJsonValue>> PropsArr;

    // Use the appropriate iterator flags
    EFieldIterationFlags IterFlags = EFieldIterationFlags::None;
    if (bIncludeInherited)
    {
        IterFlags = EFieldIterationFlags::IncludeSuper;
    }

    for (TFieldIterator<FProperty> It(TargetClass, IterFlags); It; ++It)
    {
        FProperty* Property = *It;
        if (!Property) continue;

        FString PropName = Property->GetName();

        // Filter by name
        if (!FilterName.IsEmpty() && !PropName.ToLower().Contains(LowerFilterName))
        {
            continue;
        }

        // Filter by category
        if (!FilterCategory.IsEmpty())
        {
            FString Category = Property->GetMetaData(TEXT("Category"));
            if (!Category.ToLower().Contains(LowerCategory))
            {
                continue;
            }
        }

        TSharedPtr<FJsonObject> PropObj = MakeShareable(new FJsonObject());
        PropObj->SetStringField(TEXT("name"), PropName);
        PropObj->SetStringField(TEXT("type"), PropertyTypeToString(Property));
        PropObj->SetStringField(TEXT("flags"), PropertyFlagsToString(Property));
        PropObj->SetStringField(TEXT("category"), Property->GetMetaData(TEXT("Category")));
        PropObj->SetStringField(TEXT("tooltip"), Property->GetMetaData(TEXT("ToolTip")));
        PropObj->SetNumberField(TEXT("offset"), Property->GetOffset_ForInternal());
        PropObj->SetNumberField(TEXT("size"), Property->GetSize());

        // Owner class (useful when include_inherited is true)
        if (Property->GetOwnerClass())
        {
            PropObj->SetStringField(TEXT("owner_class"), Property->GetOwnerClass()->GetName());
        }

        // Check for edit-in-editor visibility
        PropObj->SetBoolField(TEXT("is_edit_anywhere"),
            Property->HasAnyPropertyFlags(CPF_Edit));
        PropObj->SetBoolField(TEXT("is_blueprint_visible"),
            Property->HasAnyPropertyFlags(CPF_BlueprintVisible));
        PropObj->SetBoolField(TEXT("is_replicated"),
            Property->HasAnyPropertyFlags(CPF_Net));

        PropsArr.Add(MakeShareable(new FJsonValueObject(PropObj)));
    }

    TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
    Data->SetStringField(TEXT("class_name"), TargetClass->GetName());
    Data->SetArrayField(TEXT("properties"), PropsArr);
    Data->SetNumberField(TEXT("count"), PropsArr.Num());
    Data->SetBoolField(TEXT("include_inherited"), bIncludeInherited);

    return MakeSuccess(Data);
}

// ─────────────────────────────────────────────────────────────────────────────
// code.get_class_functions
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FNexusCodeAnalysisHandler::HandleGetClassFunctions(
    const TSharedPtr<FJsonObject>& Params)
{
    FString ClassName = GetStringParam(Params, TEXT("class_name"));
    if (ClassName.IsEmpty())
    {
        return MakeError(TEXT("MISSING_PARAM"), TEXT("class_name is required"));
    }

    bool bIncludeInherited = GetBoolParam(Params, TEXT("include_inherited"), false);
    bool bIncludeEvents = GetBoolParam(Params, TEXT("include_events"), true);
    FString FilterName = GetStringParam(Params, TEXT("filter_name"));

    UClass* TargetClass = FindClassByName(ClassName);
    if (!TargetClass)
    {
        return MakeError(TEXT("CLASS_NOT_FOUND"),
            FString::Printf(TEXT("Class '%s' not found"), *ClassName));
    }

    FString LowerFilterName = FilterName.ToLower();

    TArray<TSharedPtr<FJsonValue>> FuncsArr;

    EFieldIterationFlags IterFlags = EFieldIterationFlags::None;
    if (bIncludeInherited)
    {
        IterFlags = EFieldIterationFlags::IncludeSuper;
    }

    for (TFieldIterator<UFunction> It(TargetClass, IterFlags); It; ++It)
    {
        UFunction* Function = *It;
        if (!Function) continue;

        // Filter out events if not requested
        if (!bIncludeEvents && Function->HasAnyFunctionFlags(FUNC_BlueprintEvent))
        {
            continue;
        }

        FString FuncName = Function->GetName();

        // Filter by name
        if (!FilterName.IsEmpty() && !FuncName.ToLower().Contains(LowerFilterName))
        {
            continue;
        }

        TSharedPtr<FJsonObject> FuncObj = MakeShareable(new FJsonObject());
        FuncObj->SetStringField(TEXT("name"), FuncName);
        FuncObj->SetStringField(TEXT("flags"), FunctionFlagsToString(Function));
        FuncObj->SetStringField(TEXT("category"), Function->GetMetaData(TEXT("Category")));

        // Owner class
        if (Function->GetOwnerClass())
        {
            FuncObj->SetStringField(TEXT("owner_class"), Function->GetOwnerClass()->GetName());
        }

        // Return type
        FString ReturnType = TEXT("void");
        if (FProperty* ReturnProp = Function->GetReturnProperty())
        {
            ReturnType = PropertyTypeToString(ReturnProp);
        }
        FuncObj->SetStringField(TEXT("return_type"), ReturnType);

        // Parameters
        TArray<TSharedPtr<FJsonValue>> ParamsArr;
        for (TFieldIterator<FProperty> ParamIt(Function); ParamIt; ++ParamIt)
        {
            FProperty* ParamProp = *ParamIt;
            if (!ParamProp) continue;

            // Skip the return value property
            if (ParamProp->HasAnyPropertyFlags(CPF_ReturnParm)) continue;

            TSharedPtr<FJsonObject> ParamObj = MakeShareable(new FJsonObject());
            ParamObj->SetStringField(TEXT("name"), ParamProp->GetName());
            ParamObj->SetStringField(TEXT("type"), PropertyTypeToString(ParamProp));
            ParamObj->SetBoolField(TEXT("is_out"), ParamProp->HasAnyPropertyFlags(CPF_OutParm));
            ParamObj->SetBoolField(TEXT("is_reference"), ParamProp->HasAnyPropertyFlags(CPF_ReferenceParm));

            // Default value from metadata
            FString DefaultValue = Function->GetMetaData(*FString::Printf(TEXT("CPP_Default_%s"), *ParamProp->GetName()));
            if (!DefaultValue.IsEmpty())
            {
                ParamObj->SetStringField(TEXT("default_value"), DefaultValue);
            }

            ParamsArr.Add(MakeShareable(new FJsonValueObject(ParamObj)));
        }
        FuncObj->SetArrayField(TEXT("parameters"), ParamsArr);
        FuncObj->SetNumberField(TEXT("param_count"), ParamsArr.Num());

        // Additional flags as booleans for convenience
        FuncObj->SetBoolField(TEXT("is_blueprint_callable"),
            Function->HasAnyFunctionFlags(FUNC_BlueprintCallable));
        FuncObj->SetBoolField(TEXT("is_pure"),
            Function->HasAnyFunctionFlags(FUNC_BlueprintPure));
        FuncObj->SetBoolField(TEXT("is_event"),
            Function->HasAnyFunctionFlags(FUNC_BlueprintEvent));
        FuncObj->SetBoolField(TEXT("is_native"),
            Function->HasAnyFunctionFlags(FUNC_Native));

        FuncsArr.Add(MakeShareable(new FJsonValueObject(FuncObj)));
    }

    TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
    Data->SetStringField(TEXT("class_name"), TargetClass->GetName());
    Data->SetArrayField(TEXT("functions"), FuncsArr);
    Data->SetNumberField(TEXT("count"), FuncsArr.Num());
    Data->SetBoolField(TEXT("include_inherited"), bIncludeInherited);
    Data->SetBoolField(TEXT("include_events"), bIncludeEvents);

    return MakeSuccess(Data);
}

// ─────────────────────────────────────────────────────────────────────────────
// code.search_classes
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FNexusCodeAnalysisHandler::HandleSearchClasses(
    const TSharedPtr<FJsonObject>& Params)
{
    FString Query = GetStringParam(Params, TEXT("query"));
    if (Query.IsEmpty())
    {
        return MakeError(TEXT("MISSING_PARAM"), TEXT("query is required"));
    }

    FString ParentClassName = GetStringParam(Params, TEXT("parent_class"));
    bool bSearchProperties = GetBoolParam(Params, TEXT("search_properties"), true);
    bool bSearchFunctions = GetBoolParam(Params, TEXT("search_functions"), true);
    int32 Limit = static_cast<int32>(GetNumberParam(Params, TEXT("limit"), 50.0));

    UClass* ParentFilter = nullptr;
    if (!ParentClassName.IsEmpty())
    {
        ParentFilter = FindClassByName(ParentClassName);
        if (!ParentFilter)
        {
            return MakeError(TEXT("CLASS_NOT_FOUND"),
                FString::Printf(TEXT("Parent class '%s' not found"), *ParentClassName));
        }
    }

    FString LowerQuery = Query.ToLower();
    TArray<TSharedPtr<FJsonValue>> ResultsArr;

    for (TObjectIterator<UClass> It; It; ++It)
    {
        if (ResultsArr.Num() >= Limit) break;

        UClass* Class = *It;

        // Parent filter
        if (ParentFilter && !Class->IsChildOf(ParentFilter)) continue;

        TArray<FString> MatchReasons;

        // Check class name
        if (Class->GetName().ToLower().Contains(LowerQuery))
        {
            MatchReasons.Add(TEXT("class_name"));
        }

        // Check property names
        if (bSearchProperties)
        {
            for (TFieldIterator<FProperty> PropIt(Class, EFieldIterationFlags::None); PropIt; ++PropIt)
            {
                if (PropIt->GetName().ToLower().Contains(LowerQuery))
                {
                    MatchReasons.Add(FString::Printf(TEXT("property:%s"), *PropIt->GetName()));
                    break; // One match per class is enough
                }
            }
        }

        // Check function names
        if (bSearchFunctions)
        {
            for (TFieldIterator<UFunction> FuncIt(Class, EFieldIterationFlags::None); FuncIt; ++FuncIt)
            {
                if (FuncIt->GetName().ToLower().Contains(LowerQuery))
                {
                    MatchReasons.Add(FString::Printf(TEXT("function:%s"), *FuncIt->GetName()));
                    break; // One match per class is enough
                }
            }
        }

        if (MatchReasons.Num() > 0)
        {
            TSharedPtr<FJsonObject> ResultObj = ClassToJson(Class);

            TArray<TSharedPtr<FJsonValue>> ReasonsArr;
            for (const FString& Reason : MatchReasons)
            {
                ReasonsArr.Add(MakeShareable(new FJsonValueString(Reason)));
            }
            ResultObj->SetArrayField(TEXT("match_reasons"), ReasonsArr);

            ResultsArr.Add(MakeShareable(new FJsonValueObject(ResultObj)));
        }
    }

    TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
    Data->SetStringField(TEXT("query"), Query);
    Data->SetArrayField(TEXT("results"), ResultsArr);
    Data->SetNumberField(TEXT("count"), ResultsArr.Num());
    Data->SetNumberField(TEXT("limit"), Limit);

    return MakeSuccess(Data);
}

// ─────────────────────────────────────────────────────────────────────────────
// code.list_modules
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FNexusCodeAnalysisHandler::HandleListModules(
    const TSharedPtr<FJsonObject>& Params)
{
    FString FilterType = GetStringParam(Params, TEXT("filter_type"));
    FString FilterName = GetStringParam(Params, TEXT("filter_name"));

    FString LowerFilterName = FilterName.ToLower();
    FString LowerFilterType = FilterType.ToLower();

    // Get all loaded module names
    TArray<FModuleStatus> ModuleStatuses;
    FModuleManager::Get().QueryModules(ModuleStatuses);

    // Build a map of module -> class count
    TMap<FString, int32> ModuleClassCounts;
    for (TObjectIterator<UClass> It; It; ++It)
    {
        UPackage* Package = It->GetOuterUPackage();
        if (Package)
        {
            FString PackageName = Package->GetName();
            // Extract module name from package path (e.g., "/Script/Engine" -> "Engine")
            FString ModuleName;
            if (PackageName.StartsWith(TEXT("/Script/")))
            {
                ModuleName = PackageName.RightChop(8); // len("/Script/") == 8
            }
            else
            {
                ModuleName = PackageName;
            }
            ModuleClassCounts.FindOrAdd(ModuleName)++;
        }
    }

    TArray<TSharedPtr<FJsonValue>> ModulesArr;

    for (const FModuleStatus& Status : ModuleStatuses)
    {
        FString ModuleName = Status.Name;

        // Filter by name
        if (!FilterName.IsEmpty() && !ModuleName.ToLower().Contains(LowerFilterName))
        {
            continue;
        }

        // Determine module type from filepath heuristics
        FString ModuleType = TEXT("Runtime");
        FString FilePath = Status.FilePath;
        if (FilePath.Contains(TEXT("Editor")))
        {
            ModuleType = TEXT("Editor");
        }
        else if (FilePath.Contains(TEXT("Developer")))
        {
            ModuleType = TEXT("Developer");
        }
        else if (FilePath.Contains(TEXT("Program")))
        {
            ModuleType = TEXT("Program");
        }

        // Filter by type
        if (!FilterType.IsEmpty() && ModuleType.ToLower() != LowerFilterType)
        {
            continue;
        }

        TSharedPtr<FJsonObject> ModObj = MakeShareable(new FJsonObject());
        ModObj->SetStringField(TEXT("name"), ModuleName);
        ModObj->SetStringField(TEXT("type"), ModuleType);
        ModObj->SetStringField(TEXT("file_path"), FilePath);
        ModObj->SetBoolField(TEXT("is_loaded"), Status.bIsLoaded);

        // Class count from our map
        int32* ClassCount = ModuleClassCounts.Find(ModuleName);
        ModObj->SetNumberField(TEXT("class_count"), ClassCount ? *ClassCount : 0);

        ModulesArr.Add(MakeShareable(new FJsonValueObject(ModObj)));
    }

    TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
    Data->SetArrayField(TEXT("modules"), ModulesArr);
    Data->SetNumberField(TEXT("count"), ModulesArr.Num());
    Data->SetNumberField(TEXT("total_loaded"), ModuleStatuses.Num());

    return MakeSuccess(Data);
}
