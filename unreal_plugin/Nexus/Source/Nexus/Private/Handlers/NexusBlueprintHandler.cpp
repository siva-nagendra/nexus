// Copyright Nexus Team. All Rights Reserved.

#include "Handlers/NexusBlueprintHandler.h"
#include "Engine/Blueprint.h"
#include "Engine/SimpleConstructionScript.h"
#include "Engine/SCS_Node.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "AssetToolsModule.h"
#include "Factories/BlueprintFactory.h"
#include "EdGraphSchema_K2.h"
#include "K2Node.h"
#include "K2Node_CallFunction.h"
#include "K2Node_Event.h"
#include "K2Node_CustomEvent.h"
#include "K2Node_IfThenElse.h"
#include "K2Node_FunctionEntry.h"
#include "K2Node_FunctionResult.h"
#include "K2Node_VariableGet.h"
#include "K2Node_VariableSet.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "Editor.h"

// ---------------------------------------------------------------------------
// Handle dispatch
// ---------------------------------------------------------------------------

TSharedPtr<FJsonObject> FNexusBlueprintHandler::Handle(
    const FString& CommandType,
    const TSharedPtr<FJsonObject>& Params)
{
    FString SubCommand = CommandType.RightChop(GetNamespace().Len() + 1);

    if (SubCommand == TEXT("create"))             return HandleCreate(Params);
    if (SubCommand == TEXT("compile"))            return HandleCompile(Params);
    if (SubCommand == TEXT("get_info"))           return HandleInfo(Params);
    if (SubCommand == TEXT("add_variable"))       return HandleAddVariable(Params);
    if (SubCommand == TEXT("set_variable_default")) return HandleSetVariableDefault(Params);
    if (SubCommand == TEXT("get_variables"))      return HandleGetVariables(Params);
    if (SubCommand == TEXT("add_function"))       return HandleAddFunction(Params);
    if (SubCommand == TEXT("get_functions"))      return HandleGetFunctions(Params);
    if (SubCommand == TEXT("add_component"))      return HandleAddComponent(Params);
    if (SubCommand == TEXT("get_graphs"))         return HandleGetGraphs(Params);
    if (SubCommand == TEXT("add_event_dispatcher")) return HandleAddEventDispatcher(Params);
    if (SubCommand == TEXT("set_parent_class"))   return HandleSetParentClass(Params);
    if (SubCommand == TEXT("add_interface"))      return HandleAddInterface(Params);
    if (SubCommand == TEXT("open"))               return HandleOpen(Params);
    if (SubCommand == TEXT("add_node"))           return HandleAddNode(Params);
    if (SubCommand == TEXT("connect_pins"))       return HandleConnectPins(Params);
    if (SubCommand == TEXT("remove_node"))        return HandleRemoveNode(Params);
    if (SubCommand == TEXT("get_node_pins"))      return HandleGetNodePins(Params);

    return MakeError(TEXT("NOT_IMPLEMENTED"),
        FString::Printf(TEXT("Command '%s' not yet implemented"), *CommandType));
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

UBlueprint* FNexusBlueprintHandler::LoadBlueprint(
    const TSharedPtr<FJsonObject>& Params,
    const FString& ParamName)
{
    // Support both "blueprint_path" and "path" for backwards compat
    FString Path = GetStringParam(Params, ParamName);
    if (Path.IsEmpty())
    {
        Path = GetStringParam(Params, TEXT("path"));
    }
    if (Path.IsEmpty()) return nullptr;

    return LoadObject<UBlueprint>(nullptr, *Path);
}

UEdGraph* FNexusBlueprintHandler::FindGraphByName(UBlueprint* BP, const FString& GraphName)
{
    if (!BP) return nullptr;

    // Search UbergraphPages (event graphs)
    for (UEdGraph* Graph : BP->UbergraphPages)
    {
        if (Graph && Graph->GetName() == GraphName) return Graph;
    }
    // Search FunctionGraphs
    for (UEdGraph* Graph : BP->FunctionGraphs)
    {
        if (Graph && Graph->GetName() == GraphName) return Graph;
    }
    // Search MacroGraphs
    for (UEdGraph* Graph : BP->MacroGraphs)
    {
        if (Graph && Graph->GetName() == GraphName) return Graph;
    }
    return nullptr;
}

UEdGraphNode* FNexusBlueprintHandler::FindNodeById(UEdGraph* Graph, const FString& NodeId)
{
    if (!Graph) return nullptr;

    for (UEdGraphNode* Node : Graph->Nodes)
    {
        if (Node && Node->NodeGuid.ToString() == NodeId)
        {
            return Node;
        }
    }
    return nullptr;
}

TSharedPtr<FJsonObject> FNexusBlueprintHandler::PinToJson(const UEdGraphPin* Pin)
{
    TSharedPtr<FJsonObject> PinObj = MakeShareable(new FJsonObject());
    if (!Pin) return PinObj;

    PinObj->SetStringField(TEXT("pin_id"), Pin->PinId.ToString());
    PinObj->SetStringField(TEXT("name"), Pin->PinName.ToString());
    PinObj->SetStringField(TEXT("pin_type"), Pin->PinType.PinCategory.ToString());
    PinObj->SetStringField(TEXT("direction"),
        Pin->Direction == EGPD_Input ? TEXT("input") : TEXT("output"));
    PinObj->SetStringField(TEXT("default_value"), Pin->DefaultValue);
    PinObj->SetBoolField(TEXT("is_connected"), Pin->LinkedTo.Num() > 0);
    return PinObj;
}

FEdGraphPinType FNexusBlueprintHandler::ParsePinType(const FString& TypeName)
{
    FEdGraphPinType PinType;

    if (TypeName.Equals(TEXT("Boolean"), ESearchCase::IgnoreCase) || TypeName.Equals(TEXT("Bool"), ESearchCase::IgnoreCase))
    {
        PinType.PinCategory = UEdGraphSchema_K2::PC_Boolean;
    }
    else if (TypeName.Equals(TEXT("Integer"), ESearchCase::IgnoreCase) || TypeName.Equals(TEXT("Int"), ESearchCase::IgnoreCase))
    {
        PinType.PinCategory = UEdGraphSchema_K2::PC_Int;
    }
    else if (TypeName.Equals(TEXT("Int64"), ESearchCase::IgnoreCase))
    {
        PinType.PinCategory = UEdGraphSchema_K2::PC_Int64;
    }
    else if (TypeName.Equals(TEXT("Float"), ESearchCase::IgnoreCase) || TypeName.Equals(TEXT("Double"), ESearchCase::IgnoreCase))
    {
        PinType.PinCategory = UEdGraphSchema_K2::PC_Real;
        PinType.PinSubCategory = UEdGraphSchema_K2::PC_Double;
    }
    else if (TypeName.Equals(TEXT("String"), ESearchCase::IgnoreCase))
    {
        PinType.PinCategory = UEdGraphSchema_K2::PC_String;
    }
    else if (TypeName.Equals(TEXT("Name"), ESearchCase::IgnoreCase))
    {
        PinType.PinCategory = UEdGraphSchema_K2::PC_Name;
    }
    else if (TypeName.Equals(TEXT("Text"), ESearchCase::IgnoreCase))
    {
        PinType.PinCategory = UEdGraphSchema_K2::PC_Text;
    }
    else if (TypeName.Equals(TEXT("Vector"), ESearchCase::IgnoreCase))
    {
        PinType.PinCategory = UEdGraphSchema_K2::PC_Struct;
        PinType.PinSubCategoryObject = TBaseStructure<FVector>::Get();
    }
    else if (TypeName.Equals(TEXT("Rotator"), ESearchCase::IgnoreCase))
    {
        PinType.PinCategory = UEdGraphSchema_K2::PC_Struct;
        PinType.PinSubCategoryObject = TBaseStructure<FRotator>::Get();
    }
    else if (TypeName.Equals(TEXT("Transform"), ESearchCase::IgnoreCase))
    {
        PinType.PinCategory = UEdGraphSchema_K2::PC_Struct;
        PinType.PinSubCategoryObject = TBaseStructure<FTransform>::Get();
    }
    else if (TypeName.Equals(TEXT("Object"), ESearchCase::IgnoreCase))
    {
        PinType.PinCategory = UEdGraphSchema_K2::PC_Object;
        PinType.PinSubCategoryObject = UObject::StaticClass();
    }
    else if (TypeName.Equals(TEXT("Class"), ESearchCase::IgnoreCase))
    {
        PinType.PinCategory = UEdGraphSchema_K2::PC_Class;
        PinType.PinSubCategoryObject = UObject::StaticClass();
    }
    else if (TypeName.Equals(TEXT("Byte"), ESearchCase::IgnoreCase))
    {
        PinType.PinCategory = UEdGraphSchema_K2::PC_Byte;
    }
    else
    {
        // Try to load as a struct or class path
        UScriptStruct* Struct = FindObject<UScriptStruct>(nullptr, *TypeName);
        if (Struct)
        {
            PinType.PinCategory = UEdGraphSchema_K2::PC_Struct;
            PinType.PinSubCategoryObject = Struct;
        }
        else
        {
            UClass* Class = FindObject<UClass>(nullptr, *TypeName);
            if (Class)
            {
                PinType.PinCategory = UEdGraphSchema_K2::PC_Object;
                PinType.PinSubCategoryObject = Class;
            }
            else
            {
                // Default to wildcard
                PinType.PinCategory = UEdGraphSchema_K2::PC_Wildcard;
            }
        }
    }

    return PinType;
}

// ---------------------------------------------------------------------------
// Existing commands (refactored to use Handle* pattern)
// ---------------------------------------------------------------------------

TSharedPtr<FJsonObject> FNexusBlueprintHandler::HandleCreate(const TSharedPtr<FJsonObject>& Params)
{
    FString BlueprintPath = GetStringParam(Params, TEXT("blueprint_path"));
    FString ParentClassName = GetStringParam(Params, TEXT("parent_class"), TEXT("Actor"));

    // Extract name and folder from blueprint_path
    FString Name, Path;
    if (!BlueprintPath.IsEmpty())
    {
        int32 LastSlash;
        if (BlueprintPath.FindLastChar('/', LastSlash))
        {
            Path = BlueprintPath.Left(LastSlash);
            Name = BlueprintPath.RightChop(LastSlash + 1);
        }
        else
        {
            Name = BlueprintPath;
            Path = TEXT("/Game/Blueprints");
        }
    }
    else
    {
        // Fallback for legacy params
        Name = GetStringParam(Params, TEXT("name"), TEXT("NewBlueprint"));
        Path = GetStringParam(Params, TEXT("path"), TEXT("/Game/Blueprints"));
    }

    // Resolve parent class - try common engine classes first
    FString ClassPath = FString::Printf(TEXT("/Script/Engine.%s"), *ParentClassName);
    UClass* ParentClass = LoadClass<UObject>(nullptr, *ClassPath);
    if (!ParentClass)
    {
        // Try loading as a full path (e.g., /Game/BP/BP_Base.BP_Base_C)
        ParentClass = LoadClass<UObject>(nullptr, *ParentClassName);
    }
    if (!ParentClass) ParentClass = AActor::StaticClass();

    IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
    UBlueprintFactory* Factory = NewObject<UBlueprintFactory>();
    Factory->ParentClass = ParentClass;

    UObject* Asset = AssetTools.CreateAsset(Name, Path, UBlueprint::StaticClass(), Factory);
    if (!Asset)
    {
        return MakeError(TEXT("CREATE_FAILED"),
            FString::Printf(TEXT("Failed to create blueprint '%s' at '%s'"), *Name, *Path));
    }

    UBlueprint* BP = Cast<UBlueprint>(Asset);
    FKismetEditorUtilities::CompileBlueprint(BP);

    TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
    Data->SetStringField(TEXT("path"), BP->GetPathName());
    Data->SetStringField(TEXT("name"), Name);
    Data->SetStringField(TEXT("parent_class"), ParentClass->GetName());
    Data->SetBoolField(TEXT("is_compiled"), true);
    Data->SetBoolField(TEXT("has_errors"), BP->Status == BS_Error);
    return MakeSuccess(Data);
}

TSharedPtr<FJsonObject> FNexusBlueprintHandler::HandleCompile(const TSharedPtr<FJsonObject>& Params)
{
    UBlueprint* BP = LoadBlueprint(Params);
    if (!BP)
    {
        FString Path = GetStringParam(Params, TEXT("blueprint_path"));
        if (Path.IsEmpty()) Path = GetStringParam(Params, TEXT("path"));
        return MakeError(TEXT("NOT_FOUND"),
            FString::Printf(TEXT("Blueprint not found: '%s'"), *Path));
    }

    FKismetEditorUtilities::CompileBlueprint(BP);

    TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
    Data->SetStringField(TEXT("path"), BP->GetPathName());
    Data->SetStringField(TEXT("name"), BP->GetName());
    Data->SetStringField(TEXT("parent_class"), BP->ParentClass ? BP->ParentClass->GetName() : TEXT(""));
    Data->SetBoolField(TEXT("is_compiled"), true);
    Data->SetBoolField(TEXT("has_errors"), BP->Status == BS_Error);
    return MakeSuccess(Data);
}

TSharedPtr<FJsonObject> FNexusBlueprintHandler::HandleInfo(const TSharedPtr<FJsonObject>& Params)
{
    UBlueprint* BP = LoadBlueprint(Params);
    if (!BP)
    {
        FString Path = GetStringParam(Params, TEXT("blueprint_path"));
        if (Path.IsEmpty()) Path = GetStringParam(Params, TEXT("path"));
        return MakeError(TEXT("NOT_FOUND"),
            FString::Printf(TEXT("Blueprint not found: '%s'"), *Path));
    }

    TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
    Data->SetStringField(TEXT("path"), BP->GetPathName());
    Data->SetStringField(TEXT("name"), BP->GetName());
    Data->SetStringField(TEXT("parent_class"), BP->ParentClass ? BP->ParentClass->GetName() : TEXT(""));
    Data->SetBoolField(TEXT("is_compiled"), BP->Status != BS_Dirty);
    Data->SetBoolField(TEXT("has_errors"), BP->Status == BS_Error);

    // Variables
    TArray<TSharedPtr<FJsonValue>> VarsArray;
    for (const FBPVariableDescription& Var : BP->NewVariables)
    {
        TSharedPtr<FJsonObject> VarObj = MakeShareable(new FJsonObject());
        VarObj->SetStringField(TEXT("name"), Var.VarName.ToString());
        VarObj->SetStringField(TEXT("var_type"), Var.VarType.PinCategory.ToString());
        VarObj->SetStringField(TEXT("default_value"), Var.DefaultValue);
        VarObj->SetBoolField(TEXT("is_exposed"), (Var.PropertyFlags & CPF_ExposeOnSpawn) != 0);
        VarObj->SetStringField(TEXT("category"), Var.Category.ToString());
        VarsArray.Add(MakeShareable(new FJsonValueObject(VarObj)));
    }
    Data->SetArrayField(TEXT("variables"), VarsArray);

    // Functions
    TArray<TSharedPtr<FJsonValue>> FuncsArray;
    for (UEdGraph* Graph : BP->FunctionGraphs)
    {
        if (!Graph) continue;
        TSharedPtr<FJsonObject> FuncObj = MakeShareable(new FJsonObject());
        FuncObj->SetStringField(TEXT("name"), Graph->GetName());
        FuncsArray.Add(MakeShareable(new FJsonValueObject(FuncObj)));
    }
    Data->SetArrayField(TEXT("functions"), FuncsArray);

    // Graphs
    TArray<TSharedPtr<FJsonValue>> GraphsArray;
    for (UEdGraph* Graph : BP->UbergraphPages)
    {
        if (!Graph) continue;
        TSharedPtr<FJsonObject> GraphObj = MakeShareable(new FJsonObject());
        GraphObj->SetStringField(TEXT("graph_name"), Graph->GetName());
        GraphObj->SetStringField(TEXT("graph_type"), TEXT("EventGraph"));
        GraphObj->SetNumberField(TEXT("node_count"), Graph->Nodes.Num());
        GraphsArray.Add(MakeShareable(new FJsonValueObject(GraphObj)));
    }
    for (UEdGraph* Graph : BP->FunctionGraphs)
    {
        if (!Graph) continue;
        TSharedPtr<FJsonObject> GraphObj = MakeShareable(new FJsonObject());
        GraphObj->SetStringField(TEXT("graph_name"), Graph->GetName());
        GraphObj->SetStringField(TEXT("graph_type"), TEXT("FunctionGraph"));
        GraphObj->SetNumberField(TEXT("node_count"), Graph->Nodes.Num());
        GraphsArray.Add(MakeShareable(new FJsonValueObject(GraphObj)));
    }
    for (UEdGraph* Graph : BP->MacroGraphs)
    {
        if (!Graph) continue;
        TSharedPtr<FJsonObject> GraphObj = MakeShareable(new FJsonObject());
        GraphObj->SetStringField(TEXT("graph_name"), Graph->GetName());
        GraphObj->SetStringField(TEXT("graph_type"), TEXT("MacroGraph"));
        GraphObj->SetNumberField(TEXT("node_count"), Graph->Nodes.Num());
        GraphsArray.Add(MakeShareable(new FJsonValueObject(GraphObj)));
    }
    Data->SetArrayField(TEXT("graphs"), GraphsArray);

    // Component count
    int32 ComponentCount = 0;
    if (BP->SimpleConstructionScript)
    {
        ComponentCount = BP->SimpleConstructionScript->GetAllNodes().Num();
    }
    Data->SetNumberField(TEXT("component_count"), ComponentCount);

    return MakeSuccess(Data);
}

// ---------------------------------------------------------------------------
// Variable operations
// ---------------------------------------------------------------------------

TSharedPtr<FJsonObject> FNexusBlueprintHandler::HandleAddVariable(const TSharedPtr<FJsonObject>& Params)
{
    UBlueprint* BP = LoadBlueprint(Params);
    if (!BP)
    {
        return MakeError(TEXT("NOT_FOUND"),
            FString::Printf(TEXT("Blueprint not found: '%s'"), *GetStringParam(Params, TEXT("blueprint_path"))));
    }

    FString VariableName = GetStringParam(Params, TEXT("variable_name"));
    FString VariableType = GetStringParam(Params, TEXT("variable_type"), TEXT("Boolean"));
    bool bIsExposed = GetBoolParam(Params, TEXT("is_exposed"), false);
    FString Category = GetStringParam(Params, TEXT("category"));
    FString Tooltip = GetStringParam(Params, TEXT("tooltip"));

    if (VariableName.IsEmpty())
    {
        return MakeError(TEXT("INVALID_PARAMS"), TEXT("variable_name is required"));
    }

    FEdGraphPinType PinType = ParsePinType(VariableType);
    FName VarName = FName(*VariableName);

    // Add the variable
    bool bSuccess = FBlueprintEditorUtils::AddMemberVariable(BP, VarName, PinType);
    if (!bSuccess)
    {
        return MakeError(TEXT("ADD_FAILED"),
            FString::Printf(TEXT("Failed to add variable '%s' to blueprint"), *VariableName));
    }

    // Set expose on spawn flag
    if (bIsExposed)
    {
        FBlueprintEditorUtils::SetBlueprintVariableMetaData(
            BP, VarName, nullptr,
            FBlueprintMetadata::MD_ExposeOnSpawn, Category.IsEmpty() ? TEXT("Default") : Category);
        // Set the property flag
        int32 VarIdx = FBlueprintEditorUtils::FindNewVariableIndex(BP, VarName);
        if (VarIdx != INDEX_NONE)
        {
            BP->NewVariables[VarIdx].PropertyFlags |= CPF_ExposeOnSpawn;
        }
    }

    // Set category
    if (!Category.IsEmpty())
    {
        FBlueprintEditorUtils::SetBlueprintVariableCategory(BP, VarName, nullptr, FText::FromString(Category));
    }

    // Set tooltip
    if (!Tooltip.IsEmpty())
    {
        FBlueprintEditorUtils::SetBlueprintVariableMetaData(
            BP, VarName, nullptr,
            FBlueprintMetadata::MD_Tooltip, Tooltip);
    }

    FBlueprintEditorUtils::MarkBlueprintAsModified(BP);

    TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
    Data->SetStringField(TEXT("name"), VariableName);
    Data->SetStringField(TEXT("var_type"), VariableType);
    Data->SetBoolField(TEXT("is_exposed"), bIsExposed);
    Data->SetStringField(TEXT("category"), Category);
    Data->SetStringField(TEXT("tooltip"), Tooltip);
    return MakeSuccess(Data);
}

TSharedPtr<FJsonObject> FNexusBlueprintHandler::HandleSetVariableDefault(const TSharedPtr<FJsonObject>& Params)
{
    UBlueprint* BP = LoadBlueprint(Params);
    if (!BP)
    {
        return MakeError(TEXT("NOT_FOUND"),
            FString::Printf(TEXT("Blueprint not found: '%s'"), *GetStringParam(Params, TEXT("blueprint_path"))));
    }

    FString VariableName = GetStringParam(Params, TEXT("variable_name"));
    FString DefaultValue = GetStringParam(Params, TEXT("default_value"));

    if (VariableName.IsEmpty())
    {
        return MakeError(TEXT("INVALID_PARAMS"), TEXT("variable_name is required"));
    }

    FName VarName = FName(*VariableName);
    int32 VarIdx = FBlueprintEditorUtils::FindNewVariableIndex(BP, VarName);
    if (VarIdx == INDEX_NONE)
    {
        return MakeError(TEXT("NOT_FOUND"),
            FString::Printf(TEXT("Variable '%s' not found in blueprint"), *VariableName));
    }

    BP->NewVariables[VarIdx].DefaultValue = DefaultValue;
    FBlueprintEditorUtils::MarkBlueprintAsModified(BP);

    TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
    Data->SetStringField(TEXT("name"), VariableName);
    Data->SetStringField(TEXT("var_type"), BP->NewVariables[VarIdx].VarType.PinCategory.ToString());
    Data->SetStringField(TEXT("default_value"), DefaultValue);
    Data->SetBoolField(TEXT("is_exposed"), (BP->NewVariables[VarIdx].PropertyFlags & CPF_ExposeOnSpawn) != 0);
    Data->SetStringField(TEXT("category"), BP->NewVariables[VarIdx].Category.ToString());
    return MakeSuccess(Data);
}

TSharedPtr<FJsonObject> FNexusBlueprintHandler::HandleGetVariables(const TSharedPtr<FJsonObject>& Params)
{
    UBlueprint* BP = LoadBlueprint(Params);
    if (!BP)
    {
        return MakeError(TEXT("NOT_FOUND"),
            FString::Printf(TEXT("Blueprint not found: '%s'"), *GetStringParam(Params, TEXT("blueprint_path"))));
    }

    TArray<TSharedPtr<FJsonValue>> VarsArray;
    for (const FBPVariableDescription& Var : BP->NewVariables)
    {
        TSharedPtr<FJsonObject> VarObj = MakeShareable(new FJsonObject());
        VarObj->SetStringField(TEXT("name"), Var.VarName.ToString());
        VarObj->SetStringField(TEXT("var_type"), Var.VarType.PinCategory.ToString());
        VarObj->SetStringField(TEXT("default_value"), Var.DefaultValue);
        VarObj->SetBoolField(TEXT("is_exposed"), (Var.PropertyFlags & CPF_ExposeOnSpawn) != 0);
        VarObj->SetBoolField(TEXT("is_replicated"), (Var.PropertyFlags & CPF_Net) != 0);
        VarObj->SetStringField(TEXT("category"), Var.Category.ToString());
        VarObj->SetStringField(TEXT("tooltip"), Var.GetMetaData(FBlueprintMetadata::MD_Tooltip));
        VarsArray.Add(MakeShareable(new FJsonValueObject(VarObj)));
    }

    TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
    Data->SetStringField(TEXT("path"), BP->GetPathName());
    Data->SetStringField(TEXT("name"), BP->GetName());
    Data->SetArrayField(TEXT("variables"), VarsArray);
    return MakeSuccess(Data);
}

// ---------------------------------------------------------------------------
// Function operations
// ---------------------------------------------------------------------------

TSharedPtr<FJsonObject> FNexusBlueprintHandler::HandleAddFunction(const TSharedPtr<FJsonObject>& Params)
{
    UBlueprint* BP = LoadBlueprint(Params);
    if (!BP)
    {
        return MakeError(TEXT("NOT_FOUND"),
            FString::Printf(TEXT("Blueprint not found: '%s'"), *GetStringParam(Params, TEXT("blueprint_path"))));
    }

    FString FunctionName = GetStringParam(Params, TEXT("function_name"));
    bool bIsPure = GetBoolParam(Params, TEXT("is_pure"), false);
    FString AccessSpecifier = GetStringParam(Params, TEXT("access_specifier"), TEXT("Public"));
    FString Description = GetStringParam(Params, TEXT("description"));

    if (FunctionName.IsEmpty())
    {
        return MakeError(TEXT("INVALID_PARAMS"), TEXT("function_name is required"));
    }

    // Create new function graph
    UEdGraph* NewGraph = FBlueprintEditorUtils::CreateNewGraph(
        BP, FName(*FunctionName), UEdGraph::StaticClass(), UEdGraphSchema_K2::StaticClass());

    if (!NewGraph)
    {
        return MakeError(TEXT("CREATE_FAILED"),
            FString::Printf(TEXT("Failed to create function '%s'"), *FunctionName));
    }

    FBlueprintEditorUtils::AddFunctionGraph<UClass>(BP, NewGraph, /*bIsUserCreated=*/true, nullptr);

    // Set pure function flag
    if (bIsPure)
    {
        // Mark the function entry node as pure
        for (UEdGraphNode* Node : NewGraph->Nodes)
        {
            if (UK2Node_FunctionEntry* EntryNode = Cast<UK2Node_FunctionEntry>(Node))
            {
                EntryNode->AddExtraFlags(FUNC_BlueprintPure);
                break;
            }
        }
    }

    // Set access specifier
    if (!AccessSpecifier.IsEmpty())
    {
        EFunctionFlags FuncFlags = FUNC_None;
        if (AccessSpecifier.Equals(TEXT("Public"), ESearchCase::IgnoreCase))
        {
            FuncFlags |= FUNC_Public;
        }
        else if (AccessSpecifier.Equals(TEXT("Protected"), ESearchCase::IgnoreCase))
        {
            FuncFlags |= FUNC_Protected;
        }
        else if (AccessSpecifier.Equals(TEXT("Private"), ESearchCase::IgnoreCase))
        {
            FuncFlags |= FUNC_Private;
        }

        for (UEdGraphNode* Node : NewGraph->Nodes)
        {
            if (UK2Node_FunctionEntry* EntryNode = Cast<UK2Node_FunctionEntry>(Node))
            {
                EntryNode->AddExtraFlags(FuncFlags);
                break;
            }
        }
    }

    // Set description
    if (!Description.IsEmpty())
    {
        FBlueprintEditorUtils::SetBlueprintVariableMetaData(
            BP, FName(*FunctionName), nullptr,
            FBlueprintMetadata::MD_Tooltip, Description);
    }

    FBlueprintEditorUtils::MarkBlueprintAsModified(BP);

    TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
    Data->SetStringField(TEXT("name"), FunctionName);
    Data->SetBoolField(TEXT("is_pure"), bIsPure);
    Data->SetBoolField(TEXT("is_static"), false);
    Data->SetStringField(TEXT("access_specifier"), AccessSpecifier);
    Data->SetStringField(TEXT("description"), Description);
    return MakeSuccess(Data);
}

TSharedPtr<FJsonObject> FNexusBlueprintHandler::HandleGetFunctions(const TSharedPtr<FJsonObject>& Params)
{
    UBlueprint* BP = LoadBlueprint(Params);
    if (!BP)
    {
        return MakeError(TEXT("NOT_FOUND"),
            FString::Printf(TEXT("Blueprint not found: '%s'"), *GetStringParam(Params, TEXT("blueprint_path"))));
    }

    TArray<TSharedPtr<FJsonValue>> FuncsArray;
    for (UEdGraph* Graph : BP->FunctionGraphs)
    {
        if (!Graph) continue;

        TSharedPtr<FJsonObject> FuncObj = MakeShareable(new FJsonObject());
        FuncObj->SetStringField(TEXT("name"), Graph->GetName());

        // Determine if pure and get pins from entry node
        bool bIsPure = false;
        TArray<TSharedPtr<FJsonValue>> InputPins;
        TArray<TSharedPtr<FJsonValue>> OutputPins;

        for (UEdGraphNode* Node : Graph->Nodes)
        {
            if (UK2Node_FunctionEntry* EntryNode = Cast<UK2Node_FunctionEntry>(Node))
            {
                bIsPure = (EntryNode->GetExtraFlags() & FUNC_BlueprintPure) != 0;
                for (UEdGraphPin* Pin : EntryNode->Pins)
                {
                    if (Pin && Pin->Direction == EGPD_Output && Pin->PinType.PinCategory != UEdGraphSchema_K2::PC_Exec)
                    {
                        InputPins.Add(MakeShareable(new FJsonValueObject(PinToJson(Pin))));
                    }
                }
            }
            else if (UK2Node_FunctionResult* ResultNode = Cast<UK2Node_FunctionResult>(Node))
            {
                for (UEdGraphPin* Pin : ResultNode->Pins)
                {
                    if (Pin && Pin->Direction == EGPD_Input && Pin->PinType.PinCategory != UEdGraphSchema_K2::PC_Exec)
                    {
                        OutputPins.Add(MakeShareable(new FJsonValueObject(PinToJson(Pin))));
                    }
                }
            }
        }

        FuncObj->SetBoolField(TEXT("is_pure"), bIsPure);
        FuncObj->SetBoolField(TEXT("is_static"), false);
        FuncObj->SetStringField(TEXT("access_specifier"), TEXT("Public"));
        FuncObj->SetArrayField(TEXT("inputs"), InputPins);
        FuncObj->SetArrayField(TEXT("outputs"), OutputPins);
        FuncsArray.Add(MakeShareable(new FJsonValueObject(FuncObj)));
    }

    TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
    Data->SetStringField(TEXT("path"), BP->GetPathName());
    Data->SetStringField(TEXT("name"), BP->GetName());
    Data->SetArrayField(TEXT("functions"), FuncsArray);
    return MakeSuccess(Data);
}

// ---------------------------------------------------------------------------
// Component operations
// ---------------------------------------------------------------------------

TSharedPtr<FJsonObject> FNexusBlueprintHandler::HandleAddComponent(const TSharedPtr<FJsonObject>& Params)
{
    UBlueprint* BP = LoadBlueprint(Params);
    if (!BP)
    {
        return MakeError(TEXT("NOT_FOUND"),
            FString::Printf(TEXT("Blueprint not found: '%s'"), *GetStringParam(Params, TEXT("blueprint_path"))));
    }

    FString ComponentClassName = GetStringParam(Params, TEXT("component_class"));
    FString ComponentName = GetStringParam(Params, TEXT("component_name"));
    FString AttachTo = GetStringParam(Params, TEXT("attach_to"));

    if (ComponentClassName.IsEmpty())
    {
        return MakeError(TEXT("INVALID_PARAMS"), TEXT("component_class is required"));
    }

    // Resolve component class
    FString ClassPath = FString::Printf(TEXT("/Script/Engine.%s"), *ComponentClassName);
    UClass* ComponentClass = LoadClass<UActorComponent>(nullptr, *ClassPath);
    if (!ComponentClass)
    {
        // Try other common modules
        ClassPath = FString::Printf(TEXT("/Script/UMG.%s"), *ComponentClassName);
        ComponentClass = LoadClass<UActorComponent>(nullptr, *ClassPath);
    }
    if (!ComponentClass)
    {
        ClassPath = FString::Printf(TEXT("/Script/NavigationSystem.%s"), *ComponentClassName);
        ComponentClass = LoadClass<UActorComponent>(nullptr, *ClassPath);
    }
    if (!ComponentClass)
    {
        // Try loading as a full path
        ComponentClass = LoadClass<UActorComponent>(nullptr, *ComponentClassName);
    }
    if (!ComponentClass)
    {
        return MakeError(TEXT("CLASS_NOT_FOUND"),
            FString::Printf(TEXT("Component class '%s' not found"), *ComponentClassName));
    }

    if (!BP->SimpleConstructionScript)
    {
        return MakeError(TEXT("NO_SCS"),
            TEXT("Blueprint does not have a SimpleConstructionScript (may not be an Actor-based BP)"));
    }

    // Create the SCS node
    USCS_Node* NewNode = BP->SimpleConstructionScript->CreateNode(ComponentClass, FName(*ComponentName));
    if (!NewNode)
    {
        return MakeError(TEXT("CREATE_FAILED"),
            FString::Printf(TEXT("Failed to create component '%s'"), *ComponentClassName));
    }

    // Attach to parent or root
    if (!AttachTo.IsEmpty())
    {
        // Find the parent SCS node
        USCS_Node* ParentNode = nullptr;
        for (USCS_Node* Node : BP->SimpleConstructionScript->GetAllNodes())
        {
            if (Node && Node->GetVariableName().ToString() == AttachTo)
            {
                ParentNode = Node;
                break;
            }
        }
        if (ParentNode)
        {
            ParentNode->AddChildNode(NewNode);
        }
        else
        {
            BP->SimpleConstructionScript->AddNode(NewNode);
        }
    }
    else
    {
        BP->SimpleConstructionScript->AddNode(NewNode);
    }

    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP);

    // Build response
    TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
    Data->SetStringField(TEXT("path"), BP->GetPathName());
    Data->SetStringField(TEXT("name"), BP->GetName());
    Data->SetStringField(TEXT("parent_class"), BP->ParentClass ? BP->ParentClass->GetName() : TEXT(""));
    Data->SetNumberField(TEXT("component_count"), BP->SimpleConstructionScript->GetAllNodes().Num());
    return MakeSuccess(Data);
}

// ---------------------------------------------------------------------------
// Graph operations
// ---------------------------------------------------------------------------

TSharedPtr<FJsonObject> FNexusBlueprintHandler::HandleGetGraphs(const TSharedPtr<FJsonObject>& Params)
{
    UBlueprint* BP = LoadBlueprint(Params);
    if (!BP)
    {
        return MakeError(TEXT("NOT_FOUND"),
            FString::Printf(TEXT("Blueprint not found: '%s'"), *GetStringParam(Params, TEXT("blueprint_path"))));
    }

    TArray<TSharedPtr<FJsonValue>> GraphsArray;

    for (UEdGraph* Graph : BP->UbergraphPages)
    {
        if (!Graph) continue;
        TSharedPtr<FJsonObject> GraphObj = MakeShareable(new FJsonObject());
        GraphObj->SetStringField(TEXT("graph_name"), Graph->GetName());
        GraphObj->SetStringField(TEXT("graph_type"), TEXT("EventGraph"));
        GraphObj->SetNumberField(TEXT("node_count"), Graph->Nodes.Num());
        GraphsArray.Add(MakeShareable(new FJsonValueObject(GraphObj)));
    }

    for (UEdGraph* Graph : BP->FunctionGraphs)
    {
        if (!Graph) continue;
        TSharedPtr<FJsonObject> GraphObj = MakeShareable(new FJsonObject());
        GraphObj->SetStringField(TEXT("graph_name"), Graph->GetName());
        GraphObj->SetStringField(TEXT("graph_type"), TEXT("FunctionGraph"));
        GraphObj->SetNumberField(TEXT("node_count"), Graph->Nodes.Num());
        GraphsArray.Add(MakeShareable(new FJsonValueObject(GraphObj)));
    }

    for (UEdGraph* Graph : BP->MacroGraphs)
    {
        if (!Graph) continue;
        TSharedPtr<FJsonObject> GraphObj = MakeShareable(new FJsonObject());
        GraphObj->SetStringField(TEXT("graph_name"), Graph->GetName());
        GraphObj->SetStringField(TEXT("graph_type"), TEXT("MacroGraph"));
        GraphObj->SetNumberField(TEXT("node_count"), Graph->Nodes.Num());
        GraphsArray.Add(MakeShareable(new FJsonValueObject(GraphObj)));
    }

    TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
    Data->SetStringField(TEXT("path"), BP->GetPathName());
    Data->SetStringField(TEXT("name"), BP->GetName());
    Data->SetArrayField(TEXT("graphs"), GraphsArray);
    return MakeSuccess(Data);
}

// ---------------------------------------------------------------------------
// Event dispatchers
// ---------------------------------------------------------------------------

TSharedPtr<FJsonObject> FNexusBlueprintHandler::HandleAddEventDispatcher(const TSharedPtr<FJsonObject>& Params)
{
    UBlueprint* BP = LoadBlueprint(Params);
    if (!BP)
    {
        return MakeError(TEXT("NOT_FOUND"),
            FString::Printf(TEXT("Blueprint not found: '%s'"), *GetStringParam(Params, TEXT("blueprint_path"))));
    }

    FString DispatcherName = GetStringParam(Params, TEXT("dispatcher_name"));
    if (DispatcherName.IsEmpty())
    {
        return MakeError(TEXT("INVALID_PARAMS"), TEXT("dispatcher_name is required"));
    }

    FEdGraphPinType DelegateType;
    DelegateType.PinCategory = UEdGraphSchema_K2::PC_MCDelegate;

    FName DelegateName = FName(*DispatcherName);
    bool bSuccess = FBlueprintEditorUtils::AddMemberVariable(BP, DelegateName, DelegateType);
    if (!bSuccess)
    {
        return MakeError(TEXT("ADD_FAILED"),
            FString::Printf(TEXT("Failed to add event dispatcher '%s'"), *DispatcherName));
    }

    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP);

    TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
    Data->SetStringField(TEXT("path"), BP->GetPathName());
    Data->SetStringField(TEXT("name"), BP->GetName());
    Data->SetStringField(TEXT("dispatcher_name"), DispatcherName);
    return MakeSuccess(Data);
}

// ---------------------------------------------------------------------------
// Parent class / interfaces
// ---------------------------------------------------------------------------

TSharedPtr<FJsonObject> FNexusBlueprintHandler::HandleSetParentClass(const TSharedPtr<FJsonObject>& Params)
{
    UBlueprint* BP = LoadBlueprint(Params);
    if (!BP)
    {
        return MakeError(TEXT("NOT_FOUND"),
            FString::Printf(TEXT("Blueprint not found: '%s'"), *GetStringParam(Params, TEXT("blueprint_path"))));
    }

    FString ParentClassName = GetStringParam(Params, TEXT("parent_class"));
    if (ParentClassName.IsEmpty())
    {
        return MakeError(TEXT("INVALID_PARAMS"), TEXT("parent_class is required"));
    }

    // Resolve parent class
    UClass* NewParent = nullptr;

    // Try common engine class path
    FString ClassPath = FString::Printf(TEXT("/Script/Engine.%s"), *ParentClassName);
    NewParent = LoadClass<UObject>(nullptr, *ClassPath);

    if (!NewParent)
    {
        // Try as full path (e.g., /Game/BP/BP_Base.BP_Base_C)
        NewParent = LoadClass<UObject>(nullptr, *ParentClassName);
    }

    if (!NewParent)
    {
        return MakeError(TEXT("CLASS_NOT_FOUND"),
            FString::Printf(TEXT("Parent class '%s' not found"), *ParentClassName));
    }

    BP->ParentClass = NewParent;
    FBlueprintEditorUtils::RefreshAllNodes(BP);
    FKismetEditorUtilities::CompileBlueprint(BP);

    TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
    Data->SetStringField(TEXT("path"), BP->GetPathName());
    Data->SetStringField(TEXT("name"), BP->GetName());
    Data->SetStringField(TEXT("parent_class"), NewParent->GetName());
    Data->SetBoolField(TEXT("is_compiled"), true);
    Data->SetBoolField(TEXT("has_errors"), BP->Status == BS_Error);
    return MakeSuccess(Data);
}

TSharedPtr<FJsonObject> FNexusBlueprintHandler::HandleAddInterface(const TSharedPtr<FJsonObject>& Params)
{
    UBlueprint* BP = LoadBlueprint(Params);
    if (!BP)
    {
        return MakeError(TEXT("NOT_FOUND"),
            FString::Printf(TEXT("Blueprint not found: '%s'"), *GetStringParam(Params, TEXT("blueprint_path"))));
    }

    FString InterfacePath = GetStringParam(Params, TEXT("interface_path"));
    if (InterfacePath.IsEmpty())
    {
        return MakeError(TEXT("INVALID_PARAMS"), TEXT("interface_path is required"));
    }

    // Try to load the interface class
    UClass* InterfaceClass = LoadClass<UInterface>(nullptr, *InterfacePath);
    if (!InterfaceClass)
    {
        // Try as engine class
        FString EnginePath = FString::Printf(TEXT("/Script/Engine.%s"), *InterfacePath);
        InterfaceClass = LoadClass<UInterface>(nullptr, *EnginePath);
    }
    if (!InterfaceClass)
    {
        return MakeError(TEXT("CLASS_NOT_FOUND"),
            FString::Printf(TEXT("Interface '%s' not found"), *InterfacePath));
    }

    // Check if already implemented
    for (const FBPInterfaceDescription& Desc : BP->ImplementedInterfaces)
    {
        if (Desc.Interface == InterfaceClass)
        {
            return MakeError(TEXT("ALREADY_EXISTS"),
                FString::Printf(TEXT("Blueprint already implements '%s'"), *InterfacePath));
        }
    }

    FBPInterfaceDescription InterfaceDesc;
    InterfaceDesc.Interface = InterfaceClass;
    BP->ImplementedInterfaces.Add(InterfaceDesc);

    FBlueprintEditorUtils::RefreshAllNodes(BP);
    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP);

    TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
    Data->SetStringField(TEXT("path"), BP->GetPathName());
    Data->SetStringField(TEXT("name"), BP->GetName());
    Data->SetStringField(TEXT("interface"), InterfaceClass->GetName());
    return MakeSuccess(Data);
}

// ---------------------------------------------------------------------------
// Editor
// ---------------------------------------------------------------------------

TSharedPtr<FJsonObject> FNexusBlueprintHandler::HandleOpen(const TSharedPtr<FJsonObject>& Params)
{
    UBlueprint* BP = LoadBlueprint(Params);
    if (!BP)
    {
        return MakeError(TEXT("NOT_FOUND"),
            FString::Printf(TEXT("Blueprint not found: '%s'"), *GetStringParam(Params, TEXT("blueprint_path"))));
    }

    UAssetEditorSubsystem* AssetEditorSubsystem = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
    if (AssetEditorSubsystem)
    {
        AssetEditorSubsystem->OpenEditorForAsset(BP);
    }

    TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
    Data->SetStringField(TEXT("path"), BP->GetPathName());
    Data->SetStringField(TEXT("name"), BP->GetName());
    Data->SetStringField(TEXT("parent_class"), BP->ParentClass ? BP->ParentClass->GetName() : TEXT(""));
    Data->SetBoolField(TEXT("is_compiled"), BP->Status != BS_Dirty);
    Data->SetBoolField(TEXT("has_errors"), BP->Status == BS_Error);
    return MakeSuccess(Data);
}

// ---------------------------------------------------------------------------
// Graph node operations
// ---------------------------------------------------------------------------

TSharedPtr<FJsonObject> FNexusBlueprintHandler::HandleAddNode(const TSharedPtr<FJsonObject>& Params)
{
    UBlueprint* BP = LoadBlueprint(Params);
    if (!BP)
    {
        return MakeError(TEXT("NOT_FOUND"),
            FString::Printf(TEXT("Blueprint not found: '%s'"), *GetStringParam(Params, TEXT("blueprint_path"))));
    }

    FString GraphName = GetStringParam(Params, TEXT("graph_name"));
    FString NodeClassName = GetStringParam(Params, TEXT("node_class"));
    double PosX = GetNumberParam(Params, TEXT("position_x"), 0.0);
    double PosY = GetNumberParam(Params, TEXT("position_y"), 0.0);
    FString FunctionName = GetStringParam(Params, TEXT("function_name"));
    FString TargetClass = GetStringParam(Params, TEXT("target_class"));

    if (GraphName.IsEmpty() || NodeClassName.IsEmpty())
    {
        return MakeError(TEXT("INVALID_PARAMS"), TEXT("graph_name and node_class are required"));
    }

    UEdGraph* Graph = FindGraphByName(BP, GraphName);
    if (!Graph)
    {
        return MakeError(TEXT("GRAPH_NOT_FOUND"),
            FString::Printf(TEXT("Graph '%s' not found in blueprint"), *GraphName));
    }

    // Resolve node class
    UClass* NodeClass = nullptr;

    // Map common node class names
    static TMap<FString, UClass*> NodeClassMap;
    if (NodeClassMap.Num() == 0)
    {
        NodeClassMap.Add(TEXT("K2Node_CallFunction"), UK2Node_CallFunction::StaticClass());
        NodeClassMap.Add(TEXT("K2Node_Event"), UK2Node_Event::StaticClass());
        NodeClassMap.Add(TEXT("K2Node_CustomEvent"), UK2Node_CustomEvent::StaticClass());
        NodeClassMap.Add(TEXT("K2Node_IfThenElse"), UK2Node_IfThenElse::StaticClass());
        NodeClassMap.Add(TEXT("K2Node_VariableGet"), UK2Node_VariableGet::StaticClass());
        NodeClassMap.Add(TEXT("K2Node_VariableSet"), UK2Node_VariableSet::StaticClass());
    }

    UClass** FoundClass = NodeClassMap.Find(NodeClassName);
    if (FoundClass)
    {
        NodeClass = *FoundClass;
    }
    else
    {
        // Try to find the class dynamically
        NodeClass = FindObject<UClass>(nullptr, *FString::Printf(TEXT("/Script/BlueprintGraph.%s"), *NodeClassName));
    }

    if (!NodeClass)
    {
        return MakeError(TEXT("NODE_CLASS_NOT_FOUND"),
            FString::Printf(TEXT("Node class '%s' not found"), *NodeClassName));
    }

    // Create the node
    UK2Node* NewNode = NewObject<UK2Node>(Graph, NodeClass);
    if (!NewNode)
    {
        return MakeError(TEXT("CREATE_FAILED"),
            FString::Printf(TEXT("Failed to create node of class '%s'"), *NodeClassName));
    }

    NewNode->NodePosX = static_cast<int32>(PosX);
    NewNode->NodePosY = static_cast<int32>(PosY);

    // Configure function call nodes
    if (UK2Node_CallFunction* CallFuncNode = Cast<UK2Node_CallFunction>(NewNode))
    {
        if (!FunctionName.IsEmpty())
        {
            UClass* OwnerClass = nullptr;
            if (!TargetClass.IsEmpty())
            {
                FString TargetClassPath = FString::Printf(TEXT("/Script/Engine.%s"), *TargetClass);
                OwnerClass = LoadClass<UObject>(nullptr, *TargetClassPath);
            }
            if (!OwnerClass)
            {
                OwnerClass = UObject::StaticClass();
            }

            UFunction* Func = OwnerClass->FindFunctionByName(FName(*FunctionName));
            if (Func)
            {
                CallFuncNode->SetFromFunction(Func);
            }
            else
            {
                CallFuncNode->FunctionReference.SetExternalMember(FName(*FunctionName), OwnerClass);
            }
        }
    }

    Graph->AddNode(NewNode, /*bUserAction=*/true, /*bSelectNewNode=*/false);
    NewNode->AllocateDefaultPins();
    NewNode->ReconstructNode();

    FBlueprintEditorUtils::MarkBlueprintAsModified(BP);

    // Build response
    TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
    Data->SetStringField(TEXT("node_id"), NewNode->NodeGuid.ToString());
    Data->SetStringField(TEXT("node_class"), NodeClassName);
    Data->SetStringField(TEXT("title"), NewNode->GetNodeTitle(ENodeTitleType::FullTitle).ToString());
    Data->SetNumberField(TEXT("position_x"), PosX);
    Data->SetNumberField(TEXT("position_y"), PosY);

    TArray<TSharedPtr<FJsonValue>> PinsArray;
    for (UEdGraphPin* Pin : NewNode->Pins)
    {
        if (Pin)
        {
            PinsArray.Add(MakeShareable(new FJsonValueObject(PinToJson(Pin))));
        }
    }
    Data->SetArrayField(TEXT("pins"), PinsArray);

    return MakeSuccess(Data);
}

TSharedPtr<FJsonObject> FNexusBlueprintHandler::HandleConnectPins(const TSharedPtr<FJsonObject>& Params)
{
    UBlueprint* BP = LoadBlueprint(Params);
    if (!BP)
    {
        return MakeError(TEXT("NOT_FOUND"),
            FString::Printf(TEXT("Blueprint not found: '%s'"), *GetStringParam(Params, TEXT("blueprint_path"))));
    }

    FString GraphName = GetStringParam(Params, TEXT("graph_name"));
    FString SourceNodeId = GetStringParam(Params, TEXT("source_node_id"));
    FString SourcePinName = GetStringParam(Params, TEXT("source_pin_name"));
    FString TargetNodeId = GetStringParam(Params, TEXT("target_node_id"));
    FString TargetPinName = GetStringParam(Params, TEXT("target_pin_name"));

    if (GraphName.IsEmpty() || SourceNodeId.IsEmpty() || TargetNodeId.IsEmpty())
    {
        return MakeError(TEXT("INVALID_PARAMS"),
            TEXT("graph_name, source_node_id, source_pin_name, target_node_id, and target_pin_name are required"));
    }

    UEdGraph* Graph = FindGraphByName(BP, GraphName);
    if (!Graph)
    {
        return MakeError(TEXT("GRAPH_NOT_FOUND"),
            FString::Printf(TEXT("Graph '%s' not found"), *GraphName));
    }

    UEdGraphNode* SourceNode = FindNodeById(Graph, SourceNodeId);
    if (!SourceNode)
    {
        return MakeError(TEXT("NODE_NOT_FOUND"),
            FString::Printf(TEXT("Source node '%s' not found"), *SourceNodeId));
    }

    UEdGraphNode* TargetNode = FindNodeById(Graph, TargetNodeId);
    if (!TargetNode)
    {
        return MakeError(TEXT("NODE_NOT_FOUND"),
            FString::Printf(TEXT("Target node '%s' not found"), *TargetNodeId));
    }

    // Find pins
    UEdGraphPin* SourcePin = nullptr;
    for (UEdGraphPin* Pin : SourceNode->Pins)
    {
        if (Pin && Pin->PinName.ToString() == SourcePinName)
        {
            SourcePin = Pin;
            break;
        }
    }

    UEdGraphPin* TargetPin = nullptr;
    for (UEdGraphPin* Pin : TargetNode->Pins)
    {
        if (Pin && Pin->PinName.ToString() == TargetPinName)
        {
            TargetPin = Pin;
            break;
        }
    }

    if (!SourcePin)
    {
        return MakeError(TEXT("PIN_NOT_FOUND"),
            FString::Printf(TEXT("Source pin '%s' not found on node"), *SourcePinName));
    }
    if (!TargetPin)
    {
        return MakeError(TEXT("PIN_NOT_FOUND"),
            FString::Printf(TEXT("Target pin '%s' not found on node"), *TargetPinName));
    }

    // Connect
    const UEdGraphSchema* Schema = Graph->GetSchema();
    if (!Schema)
    {
        return MakeError(TEXT("SCHEMA_ERROR"), TEXT("Could not get graph schema"));
    }

    bool bConnected = Schema->TryCreateConnection(SourcePin, TargetPin);
    if (!bConnected)
    {
        return MakeError(TEXT("CONNECTION_FAILED"),
            FString::Printf(TEXT("Failed to connect '%s' to '%s'"), *SourcePinName, *TargetPinName));
    }

    FBlueprintEditorUtils::MarkBlueprintAsModified(BP);

    // Return info about the source node with updated pins
    TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
    Data->SetStringField(TEXT("node_id"), SourceNode->NodeGuid.ToString());
    Data->SetStringField(TEXT("node_class"), SourceNode->GetClass()->GetName());
    Data->SetStringField(TEXT("title"), SourceNode->GetNodeTitle(ENodeTitleType::FullTitle).ToString());

    TArray<TSharedPtr<FJsonValue>> PinsArray;
    for (UEdGraphPin* Pin : SourceNode->Pins)
    {
        if (Pin)
        {
            PinsArray.Add(MakeShareable(new FJsonValueObject(PinToJson(Pin))));
        }
    }
    Data->SetArrayField(TEXT("pins"), PinsArray);

    return MakeSuccess(Data);
}

TSharedPtr<FJsonObject> FNexusBlueprintHandler::HandleRemoveNode(const TSharedPtr<FJsonObject>& Params)
{
    UBlueprint* BP = LoadBlueprint(Params);
    if (!BP)
    {
        return MakeError(TEXT("NOT_FOUND"),
            FString::Printf(TEXT("Blueprint not found: '%s'"), *GetStringParam(Params, TEXT("blueprint_path"))));
    }

    FString GraphName = GetStringParam(Params, TEXT("graph_name"));
    FString NodeId = GetStringParam(Params, TEXT("node_id"));

    if (GraphName.IsEmpty() || NodeId.IsEmpty())
    {
        return MakeError(TEXT("INVALID_PARAMS"), TEXT("graph_name and node_id are required"));
    }

    UEdGraph* Graph = FindGraphByName(BP, GraphName);
    if (!Graph)
    {
        return MakeError(TEXT("GRAPH_NOT_FOUND"),
            FString::Printf(TEXT("Graph '%s' not found"), *GraphName));
    }

    UEdGraphNode* Node = FindNodeById(Graph, NodeId);
    if (!Node)
    {
        return MakeError(TEXT("NODE_NOT_FOUND"),
            FString::Printf(TEXT("Node '%s' not found in graph '%s'"), *NodeId, *GraphName));
    }

    FBlueprintEditorUtils::RemoveNode(BP, Node, /*bDontRecompile=*/true);
    FBlueprintEditorUtils::MarkBlueprintAsModified(BP);

    // Return the graph info
    TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
    Data->SetStringField(TEXT("graph_name"), GraphName);
    Data->SetStringField(TEXT("graph_type"),
        BP->UbergraphPages.Contains(Graph) ? TEXT("EventGraph") :
        BP->FunctionGraphs.Contains(Graph) ? TEXT("FunctionGraph") : TEXT("MacroGraph"));
    Data->SetNumberField(TEXT("node_count"), Graph->Nodes.Num());
    return MakeSuccess(Data);
}

TSharedPtr<FJsonObject> FNexusBlueprintHandler::HandleGetNodePins(const TSharedPtr<FJsonObject>& Params)
{
    UBlueprint* BP = LoadBlueprint(Params);
    if (!BP)
    {
        return MakeError(TEXT("NOT_FOUND"),
            FString::Printf(TEXT("Blueprint not found: '%s'"), *GetStringParam(Params, TEXT("blueprint_path"))));
    }

    FString GraphName = GetStringParam(Params, TEXT("graph_name"));
    FString NodeId = GetStringParam(Params, TEXT("node_id"));

    if (GraphName.IsEmpty() || NodeId.IsEmpty())
    {
        return MakeError(TEXT("INVALID_PARAMS"), TEXT("graph_name and node_id are required"));
    }

    UEdGraph* Graph = FindGraphByName(BP, GraphName);
    if (!Graph)
    {
        return MakeError(TEXT("GRAPH_NOT_FOUND"),
            FString::Printf(TEXT("Graph '%s' not found"), *GraphName));
    }

    UEdGraphNode* Node = FindNodeById(Graph, NodeId);
    if (!Node)
    {
        return MakeError(TEXT("NODE_NOT_FOUND"),
            FString::Printf(TEXT("Node '%s' not found in graph '%s'"), *NodeId, *GraphName));
    }

    TArray<TSharedPtr<FJsonValue>> PinsArray;
    for (UEdGraphPin* Pin : Node->Pins)
    {
        if (Pin)
        {
            PinsArray.Add(MakeShareable(new FJsonValueObject(PinToJson(Pin))));
        }
    }

    TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
    Data->SetStringField(TEXT("node_id"), Node->NodeGuid.ToString());
    Data->SetStringField(TEXT("node_class"), Node->GetClass()->GetName());
    Data->SetStringField(TEXT("title"), Node->GetNodeTitle(ENodeTitleType::FullTitle).ToString());
    Data->SetNumberField(TEXT("position_x"), Node->NodePosX);
    Data->SetNumberField(TEXT("position_y"), Node->NodePosY);
    Data->SetArrayField(TEXT("pins"), PinsArray);

    return MakeSuccess(Data);
}
