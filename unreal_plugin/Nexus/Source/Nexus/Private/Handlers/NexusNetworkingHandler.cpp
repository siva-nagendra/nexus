// Copyright Nexus Team. All Rights Reserved.

#include "Handlers/NexusNetworkingHandler.h"
#include "Editor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "K2Node_FunctionEntry.h"
#include "EdGraph/EdGraph.h"
#include "EdGraphSchema_K2.h"
#include "UObject/UnrealType.h"
#include "AssetRegistry/AssetRegistryModule.h"

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

AActor* FNexusNetworkingHandler::FindActorByLabel(const FString& Label)
{
    UWorld* World = GetEditorWorld();
    if (!World || Label.IsEmpty()) return nullptr;

    for (TActorIterator<AActor> It(World); It; ++It)
    {
        AActor* Actor = *It;
        if (Actor->GetActorLabel() == Label || Actor->GetPathName() == Label)
        {
            return Actor;
        }
    }
    return nullptr;
}

static FString NetRoleToString(ENetRole Role)
{
    switch (Role)
    {
    case ROLE_None:             return TEXT("ROLE_None");
    case ROLE_SimulatedProxy:   return TEXT("ROLE_SimulatedProxy");
    case ROLE_AutonomousProxy:  return TEXT("ROLE_AutonomousProxy");
    case ROLE_Authority:        return TEXT("ROLE_Authority");
    default:                    return TEXT("ROLE_Unknown");
    }
}

static FString ReplicationConditionToString(ELifetimeCondition Cond)
{
    switch (Cond)
    {
    case COND_None:             return TEXT("Always");
    case COND_InitialOnly:      return TEXT("InitialOnly");
    case COND_OwnerOnly:        return TEXT("OwnerOnly");
    case COND_SkipOwner:        return TEXT("SkipOwner");
    case COND_SimulatedOnly:    return TEXT("SimulatedOnly");
    case COND_AutonomousOnly:   return TEXT("AutonomousOnly");
    case COND_SimulatedOrPhysics: return TEXT("SimulatedOrPhysics");
    case COND_InitialOrOwner:   return TEXT("InitialOrOwner");
    case COND_Custom:           return TEXT("Custom");
    case COND_ReplayOrOwner:    return TEXT("ReplayOrOwner");
    case COND_ReplayOnly:       return TEXT("ReplayOnly");
    case COND_SimulatedOnlyNoReplay: return TEXT("SimulatedOnlyNoReplay");
    case COND_SimulatedOrPhysicsNoReplay: return TEXT("SimulatedOrPhysicsNoReplay");
    case COND_SkipReplay:       return TEXT("SkipReplay");
    case COND_Dynamic:          return TEXT("Dynamic");
    case COND_Never:            return TEXT("Never");
    default:                    return TEXT("Unknown");
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Dispatch
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FNexusNetworkingHandler::Handle(
    const FString& CommandType,
    const TSharedPtr<FJsonObject>& Params)
{
    FString SubCommand = CommandType.RightChop(GetNamespace().Len() + 1);

    if (SubCommand == TEXT("set_replication"))           return HandleSetReplication(Params);
    if (SubCommand == TEXT("set_net_role"))              return HandleSetNetRole(Params);
    if (SubCommand == TEXT("add_rpc"))                   return HandleAddRpc(Params);
    if (SubCommand == TEXT("set_net_relevancy"))         return HandleSetNetRelevancy(Params);
    if (SubCommand == TEXT("get_replication_info"))       return HandleGetReplicationInfo(Params);
    if (SubCommand == TEXT("list_replicated_properties")) return HandleListReplicatedProperties(Params);

    return MakeError(TEXT("NOT_IMPLEMENTED"),
        FString::Printf(TEXT("Command '%s' not yet implemented"), *CommandType));
}

// ─────────────────────────────────────────────────────────────────────────────
// networking.set_replication
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FNexusNetworkingHandler::HandleSetReplication(
    const TSharedPtr<FJsonObject>& Params)
{
    FString ActorLabel = GetStringParam(Params, TEXT("actor_label"));
    if (ActorLabel.IsEmpty())
    {
        return MakeError(TEXT("MISSING_PARAM"), TEXT("actor_label is required"));
    }

    AActor* Actor = FindActorByLabel(ActorLabel);
    if (!Actor)
    {
        return MakeError(TEXT("ACTOR_NOT_FOUND"),
            FString::Printf(TEXT("Actor '%s' not found"), *ActorLabel));
    }

    bool bReplicate = GetBoolParam(Params, TEXT("replicate"), true);
    bool bReplicateMovement = GetBoolParam(Params, TEXT("replicate_movement"), true);
    bool bAlwaysRelevant = GetBoolParam(Params, TEXT("always_relevant"), false);
    double NetUpdateFreq = GetNumberParam(Params, TEXT("net_update_frequency"), 100.0);
    double MinNetUpdateFreq = GetNumberParam(Params, TEXT("min_net_update_frequency"), 2.0);

    // Apply replication settings
    Actor->SetReplicates(bReplicate);
    Actor->SetReplicateMovement(bReplicateMovement);
    Actor->bAlwaysRelevant = bAlwaysRelevant;
    Actor->SetNetUpdateFrequency(static_cast<float>(NetUpdateFreq));
    Actor->SetMinNetUpdateFrequency(static_cast<float>(MinNetUpdateFreq));

    Actor->MarkPackageDirty();

    TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
    Data->SetStringField(TEXT("actor_label"), ActorLabel);
    Data->SetBoolField(TEXT("replicate"), Actor->GetIsReplicated());
    Data->SetBoolField(TEXT("replicate_movement"), Actor->IsReplicatingMovement());
    Data->SetBoolField(TEXT("always_relevant"), Actor->bAlwaysRelevant);
    Data->SetNumberField(TEXT("net_update_frequency"), Actor->GetNetUpdateFrequency());
    Data->SetNumberField(TEXT("min_net_update_frequency"), Actor->GetMinNetUpdateFrequency());
    return MakeSuccess(Data);
}

// ─────────────────────────────────────────────────────────────────────────────
// networking.set_net_role
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FNexusNetworkingHandler::HandleSetNetRole(
    const TSharedPtr<FJsonObject>& Params)
{
    FString ActorLabel = GetStringParam(Params, TEXT("actor_label"));
    if (ActorLabel.IsEmpty())
    {
        return MakeError(TEXT("MISSING_PARAM"), TEXT("actor_label is required"));
    }

    FString NetRoleStr = GetStringParam(Params, TEXT("net_role"), TEXT("ROLE_Authority"));

    AActor* Actor = FindActorByLabel(ActorLabel);
    if (!Actor)
    {
        return MakeError(TEXT("ACTOR_NOT_FOUND"),
            FString::Printf(TEXT("Actor '%s' not found"), *ActorLabel));
    }

    // Parse net role
    ENetRole NewRole = ROLE_Authority;
    if (NetRoleStr == TEXT("ROLE_None"))
    {
        NewRole = ROLE_None;
    }
    else if (NetRoleStr == TEXT("ROLE_SimulatedProxy"))
    {
        NewRole = ROLE_SimulatedProxy;
    }
    else if (NetRoleStr == TEXT("ROLE_AutonomousProxy"))
    {
        NewRole = ROLE_AutonomousProxy;
    }
    else if (NetRoleStr == TEXT("ROLE_Authority"))
    {
        NewRole = ROLE_Authority;
    }
    else
    {
        return MakeError(TEXT("INVALID_NET_ROLE"),
            FString::Printf(TEXT("Invalid net role '%s'. Use: ROLE_None, ROLE_SimulatedProxy, "
                "ROLE_AutonomousProxy, ROLE_Authority"), *NetRoleStr));
    }

    // Set the role — note: SetRole is the internal setter
    Actor->SetRole(NewRole);
    Actor->MarkPackageDirty();

    TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
    Data->SetStringField(TEXT("actor_label"), ActorLabel);
    Data->SetStringField(TEXT("net_role"), NetRoleToString(Actor->GetLocalRole()));
    Data->SetStringField(TEXT("remote_role"), NetRoleToString(Actor->GetRemoteRole()));
    return MakeSuccess(Data);
}

// ─────────────────────────────────────────────────────────────────────────────
// networking.add_rpc
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FNexusNetworkingHandler::HandleAddRpc(
    const TSharedPtr<FJsonObject>& Params)
{
    FString BlueprintPath = GetStringParam(Params, TEXT("blueprint_path"));
    FString FunctionName = GetStringParam(Params, TEXT("function_name"));
    FString RpcType = GetStringParam(Params, TEXT("rpc_type"), TEXT("Server"));
    bool bReliable = GetBoolParam(Params, TEXT("reliable"), true);
    bool bValidate = GetBoolParam(Params, TEXT("validate"), false);

    if (BlueprintPath.IsEmpty())
    {
        return MakeError(TEXT("MISSING_PARAM"), TEXT("blueprint_path is required"));
    }
    if (FunctionName.IsEmpty())
    {
        return MakeError(TEXT("MISSING_PARAM"), TEXT("function_name is required"));
    }

    // Load the Blueprint asset
    UBlueprint* Blueprint = LoadObject<UBlueprint>(nullptr, *BlueprintPath);
    if (!Blueprint)
    {
        return MakeError(TEXT("BLUEPRINT_NOT_FOUND"),
            FString::Printf(TEXT("Blueprint '%s' not found"), *BlueprintPath));
    }

    // Find the function graph by name
    UEdGraph* FunctionGraph = nullptr;
    for (UEdGraph* Graph : Blueprint->FunctionGraphs)
    {
        if (Graph && Graph->GetFName().ToString() == FunctionName)
        {
            FunctionGraph = Graph;
            break;
        }
    }

    if (!FunctionGraph)
    {
        return MakeError(TEXT("FUNCTION_NOT_FOUND"),
            FString::Printf(TEXT("Function '%s' not found in Blueprint '%s'"),
                *FunctionName, *BlueprintPath));
    }

    // Find the function entry node to modify flags
    UK2Node_FunctionEntry* EntryNode = nullptr;
    for (UEdGraphNode* Node : FunctionGraph->Nodes)
    {
        EntryNode = Cast<UK2Node_FunctionEntry>(Node);
        if (EntryNode) break;
    }

    if (!EntryNode)
    {
        return MakeError(TEXT("ENTRY_NODE_NOT_FOUND"),
            FString::Printf(TEXT("Function entry node not found for '%s'"), *FunctionName));
    }

    // Build the RPC flags
    int32 ExtraFlags = FUNC_Net;

    if (RpcType == TEXT("Server"))
    {
        ExtraFlags |= FUNC_NetServer;
    }
    else if (RpcType == TEXT("Client"))
    {
        ExtraFlags |= FUNC_NetClient;
    }
    else if (RpcType == TEXT("NetMulticast"))
    {
        ExtraFlags |= FUNC_NetMulticast;
    }
    else
    {
        return MakeError(TEXT("INVALID_RPC_TYPE"),
            FString::Printf(TEXT("Invalid RPC type '%s'. Use: Server, Client, NetMulticast"),
                *RpcType));
    }

    if (bReliable)
    {
        ExtraFlags |= FUNC_NetReliable;
    }

    if (bValidate && RpcType == TEXT("Server"))
    {
        ExtraFlags |= FUNC_NetValidate;
    }

    // Apply the flags to the entry node's extra flags
    EntryNode->AddExtraFlags(ExtraFlags);

    // Compile the Blueprint to apply changes
    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);

    TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
    Data->SetStringField(TEXT("blueprint_path"), BlueprintPath);
    Data->SetStringField(TEXT("function_name"), FunctionName);
    Data->SetStringField(TEXT("rpc_type"), RpcType);
    Data->SetBoolField(TEXT("reliable"), bReliable);
    Data->SetBoolField(TEXT("validate"), bValidate);
    return MakeSuccess(Data);
}

// ─────────────────────────────────────────────────────────────────────────────
// networking.set_net_relevancy
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FNexusNetworkingHandler::HandleSetNetRelevancy(
    const TSharedPtr<FJsonObject>& Params)
{
    FString ActorLabel = GetStringParam(Params, TEXT("actor_label"));
    if (ActorLabel.IsEmpty())
    {
        return MakeError(TEXT("MISSING_PARAM"), TEXT("actor_label is required"));
    }

    AActor* Actor = FindActorByLabel(ActorLabel);
    if (!Actor)
    {
        return MakeError(TEXT("ACTOR_NOT_FOUND"),
            FString::Printf(TEXT("Actor '%s' not found"), *ActorLabel));
    }

    bool bAlwaysRelevant = GetBoolParam(Params, TEXT("always_relevant"), false);
    bool bOnlyRelevantToOwner = GetBoolParam(Params, TEXT("only_relevant_to_owner"), false);
    double NetCullDistance = GetNumberParam(Params, TEXT("net_cull_distance"), 0.0);
    bool bUseOwnerRelevancy = GetBoolParam(Params, TEXT("use_owner_relevancy"), false);

    // Apply relevancy settings
    Actor->bAlwaysRelevant = bAlwaysRelevant;
    Actor->bOnlyRelevantToOwner = bOnlyRelevantToOwner;
    Actor->bNetUseOwnerRelevancy = bUseOwnerRelevancy;

    if (NetCullDistance > 0.0)
    {
        Actor->SetNetCullDistanceSquared(static_cast<float>(NetCullDistance * NetCullDistance));
    }

    Actor->MarkPackageDirty();

    TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
    Data->SetStringField(TEXT("actor_label"), ActorLabel);
    Data->SetBoolField(TEXT("always_relevant"), Actor->bAlwaysRelevant);
    Data->SetBoolField(TEXT("only_relevant_to_owner"), Actor->bOnlyRelevantToOwner);
    Data->SetNumberField(TEXT("net_cull_distance"),
        FMath::Sqrt(Actor->GetNetCullDistanceSquared()));
    Data->SetBoolField(TEXT("use_owner_relevancy"), Actor->bNetUseOwnerRelevancy);
    return MakeSuccess(Data);
}

// ─────────────────────────────────────────────────────────────────────────────
// networking.get_replication_info
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FNexusNetworkingHandler::HandleGetReplicationInfo(
    const TSharedPtr<FJsonObject>& Params)
{
    FString ActorLabel = GetStringParam(Params, TEXT("actor_label"));
    if (ActorLabel.IsEmpty())
    {
        return MakeError(TEXT("MISSING_PARAM"), TEXT("actor_label is required"));
    }

    AActor* Actor = FindActorByLabel(ActorLabel);
    if (!Actor)
    {
        return MakeError(TEXT("ACTOR_NOT_FOUND"),
            FString::Printf(TEXT("Actor '%s' not found"), *ActorLabel));
    }

    TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());

    // Basic replication state
    Data->SetBoolField(TEXT("replicates"), Actor->GetIsReplicated());
    Data->SetBoolField(TEXT("replicate_movement"), Actor->IsReplicatingMovement());
    Data->SetStringField(TEXT("net_role"), NetRoleToString(Actor->GetLocalRole()));
    Data->SetStringField(TEXT("remote_role"), NetRoleToString(Actor->GetRemoteRole()));

    // Update frequencies
    Data->SetNumberField(TEXT("net_update_frequency"), Actor->GetNetUpdateFrequency());
    Data->SetNumberField(TEXT("min_net_update_frequency"), Actor->GetMinNetUpdateFrequency());

    // Relevancy settings
    TSharedPtr<FJsonObject> RelevancyObj = MakeShareable(new FJsonObject());
    RelevancyObj->SetBoolField(TEXT("always_relevant"), Actor->bAlwaysRelevant);
    RelevancyObj->SetBoolField(TEXT("only_relevant_to_owner"), Actor->bOnlyRelevantToOwner);
    RelevancyObj->SetNumberField(TEXT("net_cull_distance"),
        FMath::Sqrt(Actor->GetNetCullDistanceSquared()));
    RelevancyObj->SetBoolField(TEXT("use_owner_relevancy"), Actor->bNetUseOwnerRelevancy);
    Data->SetObjectField(TEXT("relevancy"), RelevancyObj);

    // Owner info
    AActor* Owner = Actor->GetOwner();
    if (Owner)
    {
        Data->SetStringField(TEXT("owner"), Owner->GetActorLabel());
        Data->SetStringField(TEXT("owner_path"), Owner->GetPathName());
    }
    else
    {
        Data->SetStringField(TEXT("owner"), TEXT(""));
    }

    // Collect replicated properties from the actor's class
    TArray<TSharedPtr<FJsonValue>> PropsArr;
    UClass* ActorClass = Actor->GetClass();
    for (TFieldIterator<FProperty> PropIt(ActorClass); PropIt; ++PropIt)
    {
        FProperty* Prop = *PropIt;
        if (Prop->HasAnyPropertyFlags(CPF_Net))
        {
            TSharedPtr<FJsonObject> PropObj = MakeShareable(new FJsonObject());
            PropObj->SetStringField(TEXT("name"), Prop->GetName());
            PropObj->SetStringField(TEXT("type"), Prop->GetCPPType());

            // Check for RepNotify
            bool bHasRepNotify = Prop->HasAnyPropertyFlags(CPF_RepNotify);
            PropObj->SetBoolField(TEXT("rep_notify"), bHasRepNotify);

            if (bHasRepNotify)
            {
                PropObj->SetStringField(TEXT("rep_notify_func"), Prop->RepNotifyFunc.ToString());
            }

            PropsArr.Add(MakeShareable(new FJsonValueObject(PropObj)));
        }
    }
    Data->SetArrayField(TEXT("replicated_properties"), PropsArr);
    Data->SetNumberField(TEXT("replicated_property_count"), PropsArr.Num());

    // Collect RPC functions
    TArray<TSharedPtr<FJsonValue>> RpcsArr;
    for (TFieldIterator<UFunction> FuncIt(ActorClass); FuncIt; ++FuncIt)
    {
        UFunction* Func = *FuncIt;
        if (Func->HasAnyFunctionFlags(FUNC_Net))
        {
            TSharedPtr<FJsonObject> RpcObj = MakeShareable(new FJsonObject());
            RpcObj->SetStringField(TEXT("name"), Func->GetName());

            FString Direction;
            if (Func->HasAnyFunctionFlags(FUNC_NetServer))
            {
                Direction = TEXT("Server");
            }
            else if (Func->HasAnyFunctionFlags(FUNC_NetClient))
            {
                Direction = TEXT("Client");
            }
            else if (Func->HasAnyFunctionFlags(FUNC_NetMulticast))
            {
                Direction = TEXT("NetMulticast");
            }
            else
            {
                Direction = TEXT("Net");
            }
            RpcObj->SetStringField(TEXT("rpc_type"), Direction);
            RpcObj->SetBoolField(TEXT("reliable"),
                Func->HasAnyFunctionFlags(FUNC_NetReliable));
            RpcObj->SetBoolField(TEXT("validate"),
                Func->HasAnyFunctionFlags(FUNC_NetValidate));

            RpcsArr.Add(MakeShareable(new FJsonValueObject(RpcObj)));
        }
    }
    Data->SetArrayField(TEXT("rpcs"), RpcsArr);
    Data->SetNumberField(TEXT("rpc_count"), RpcsArr.Num());

    return MakeSuccess(Data);
}

// ─────────────────────────────────────────────────────────────────────────────
// networking.list_replicated_properties
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FNexusNetworkingHandler::HandleListReplicatedProperties(
    const TSharedPtr<FJsonObject>& Params)
{
    FString BlueprintPath = GetStringParam(Params, TEXT("blueprint_path"));
    bool bIncludeInherited = GetBoolParam(Params, TEXT("include_inherited"), true);

    if (BlueprintPath.IsEmpty())
    {
        return MakeError(TEXT("MISSING_PARAM"), TEXT("blueprint_path is required"));
    }

    // Load the Blueprint
    UBlueprint* Blueprint = LoadObject<UBlueprint>(nullptr, *BlueprintPath);
    if (!Blueprint)
    {
        return MakeError(TEXT("BLUEPRINT_NOT_FOUND"),
            FString::Printf(TEXT("Blueprint '%s' not found"), *BlueprintPath));
    }

    UClass* GeneratedClass = Blueprint->GeneratedClass;
    if (!GeneratedClass)
    {
        return MakeError(TEXT("NO_GENERATED_CLASS"),
            FString::Printf(TEXT("Blueprint '%s' has no generated class"), *BlueprintPath));
    }

    // Determine the iteration scope
    EFieldIterationFlags IterFlags = EFieldIterationFlags::Default;
    UClass* StopAtClass = bIncludeInherited ? UObject::StaticClass() : GeneratedClass->GetSuperClass();

    TArray<TSharedPtr<FJsonValue>> PropsArr;

    for (TFieldIterator<FProperty> PropIt(GeneratedClass, IterFlags); PropIt; ++PropIt)
    {
        FProperty* Prop = *PropIt;

        // If not including inherited, skip properties from parent classes
        if (!bIncludeInherited && PropIt->GetOwnerClass() != GeneratedClass)
        {
            continue;
        }

        if (!Prop->HasAnyPropertyFlags(CPF_Net))
        {
            continue;
        }

        TSharedPtr<FJsonObject> PropObj = MakeShareable(new FJsonObject());
        PropObj->SetStringField(TEXT("name"), Prop->GetName());
        PropObj->SetStringField(TEXT("type"), Prop->GetCPPType());
        PropObj->SetStringField(TEXT("owner_class"), Prop->GetOwnerClass()->GetName());

        // Determine replication condition from the property's lifetime conditions
        // We iterate the class's ClassReps to find the condition
        ELifetimeCondition Condition = COND_None;
        bool bFoundCondition = false;

        // Check the CDO for replication lifetime info
        // The condition is stored in the class's replicated property metadata
        // For Blueprint properties, check the ReplicationCondition metadata
        if (Prop->HasMetaData(TEXT("ReplicationCondition")))
        {
            FString CondStr = Prop->GetMetaData(TEXT("ReplicationCondition"));
            if (CondStr == TEXT("InitialOnly")) Condition = COND_InitialOnly;
            else if (CondStr == TEXT("OwnerOnly")) Condition = COND_OwnerOnly;
            else if (CondStr == TEXT("SkipOwner")) Condition = COND_SkipOwner;
            else if (CondStr == TEXT("SimulatedOnly")) Condition = COND_SimulatedOnly;
            else if (CondStr == TEXT("AutonomousOnly")) Condition = COND_AutonomousOnly;
            else if (CondStr == TEXT("Custom")) Condition = COND_Custom;
            bFoundCondition = true;
        }

        PropObj->SetStringField(TEXT("replication_condition"),
            ReplicationConditionToString(Condition));

        // RepNotify
        bool bHasRepNotify = Prop->HasAnyPropertyFlags(CPF_RepNotify);
        PropObj->SetBoolField(TEXT("rep_notify"), bHasRepNotify);
        if (bHasRepNotify)
        {
            PropObj->SetStringField(TEXT("rep_notify_func"),
                Prop->RepNotifyFunc.ToString());
        }

        // Whether this property is from the current class or inherited
        PropObj->SetBoolField(TEXT("inherited"),
            Prop->GetOwnerClass() != GeneratedClass);

        PropsArr.Add(MakeShareable(new FJsonValueObject(PropObj)));
    }

    TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
    Data->SetStringField(TEXT("blueprint_path"), BlueprintPath);
    Data->SetStringField(TEXT("class_name"), GeneratedClass->GetName());
    Data->SetArrayField(TEXT("properties"), PropsArr);
    Data->SetNumberField(TEXT("count"), PropsArr.Num());
    Data->SetBoolField(TEXT("include_inherited"), bIncludeInherited);
    return MakeSuccess(Data);
}
