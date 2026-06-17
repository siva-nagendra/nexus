// Copyright Nexus Team. All Rights Reserved.

#include "Handlers/NexusPCGHandler.h"
#include "Editor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "PCGGraph.h"
#include "PCGComponent.h"
#include "PCGNode.h"
#include "PCGSettings.h"
#include "PCGSubsystem.h"
#include "PCGPin.h"
#include "PCGVolume.h"
#include "PCGData.h"
#include "PCGEdge.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "UObject/SavePackage.h"
#include "UObject/UObjectIterator.h"

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

UPCGComponent* FNexusPCGHandler::FindPCGComponentByLabels(
    UWorld* World,
    const FString& GraphName,
    const FString& OwnerActorLabel)
{
    if (!World) return nullptr;

    for (TActorIterator<AActor> It(World); It; ++It)
    {
        AActor* Actor = *It;

        // Filter by owner label if specified
        if (!OwnerActorLabel.IsEmpty())
        {
            if (Actor->GetActorLabel() != OwnerActorLabel &&
                Actor->GetPathName() != OwnerActorLabel)
            {
                continue;
            }
        }

        // Search for PCG component on this actor
        TArray<UPCGComponent*> PCGComps;
        Actor->GetComponents<UPCGComponent>(PCGComps);

        for (UPCGComponent* Comp : PCGComps)
        {
            if (!Comp) continue;

            UPCGGraph* Graph = Comp->GetGraph();
            if (!Graph) continue;

            // Match by graph name if specified
            if (!GraphName.IsEmpty())
            {
                if (Graph->GetName() == GraphName || Graph->GetFName().ToString() == GraphName)
                {
                    return Comp;
                }
            }
            else
            {
                // If no graph name and we matched actor, return first
                return Comp;
            }
        }
    }
    return nullptr;
}

UPCGGraph* FNexusPCGHandler::FindPCGGraphByName(
    UWorld* World,
    const FString& GraphName)
{
    if (GraphName.IsEmpty()) return nullptr;

    // First try finding via PCG component in the world
    UPCGComponent* Comp = FindPCGComponentByLabels(World, GraphName, TEXT(""));
    if (Comp && Comp->GetGraph())
    {
        return Comp->GetGraph();
    }

    // Search asset registry for PCG graph assets
    FAssetRegistryModule& AssetRegistryModule =
        FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
    IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();

    TArray<FAssetData> AssetDataList;
    AssetRegistry.GetAssetsByClass(UPCGGraph::StaticClass()->GetClassPathName(), AssetDataList);

    for (const FAssetData& AssetData : AssetDataList)
    {
        if (AssetData.AssetName.ToString() == GraphName)
        {
            return Cast<UPCGGraph>(AssetData.GetAsset());
        }
    }

    return nullptr;
}

UPCGNode* FNexusPCGHandler::FindNodeByLabel(
    UPCGGraph* Graph,
    const FString& NodeLabel)
{
    if (!Graph || NodeLabel.IsEmpty()) return nullptr;

    // Search all nodes in the graph
    for (UPCGNode* Node : Graph->GetNodes())
    {
        if (!Node) continue;

        if (Node->NodeTitle.ToString() == NodeLabel ||
            Node->GetName() == NodeLabel ||
            Node->GetFName().ToString() == NodeLabel)
        {
            return Node;
        }
    }

    return nullptr;
}

// ─────────────────────────────────────────────────────────────────────────────
// Dispatch
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FNexusPCGHandler::Handle(
    const FString& CommandType,
    const TSharedPtr<FJsonObject>& Params)
{
    FString SubCommand = CommandType.RightChop(GetNamespace().Len() + 1);

    if (SubCommand == TEXT("create_pcg_graph"))   return HandleCreatePCGGraph(Params);
    if (SubCommand == TEXT("add_pcg_node"))        return HandleAddPCGNode(Params);
    if (SubCommand == TEXT("connect_pcg_nodes"))   return HandleConnectPCGNodes(Params);
    if (SubCommand == TEXT("set_pcg_settings"))    return HandleSetPCGSettings(Params);
    if (SubCommand == TEXT("execute_pcg_graph"))   return HandleExecutePCGGraph(Params);
    if (SubCommand == TEXT("get_pcg_info"))         return HandleGetPCGInfo(Params);

    return MakeError(TEXT("NOT_IMPLEMENTED"),
        FString::Printf(TEXT("Command '%s' not yet implemented"), *CommandType));
}

// ─────────────────────────────────────────────────────────────────────────────
// pcg.create_pcg_graph
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FNexusPCGHandler::HandleCreatePCGGraph(
    const TSharedPtr<FJsonObject>& Params)
{
    UWorld* World = GetEditorWorld();
    if (!World)
    {
        return MakeError(TEXT("NO_WORLD"), TEXT("No editor world available"));
    }

    FString GraphName = GetStringParam(Params, TEXT("graph_name"), TEXT("PCGGraph"));
    FString OwnerActorLabel = GetStringParam(Params, TEXT("owner_actor_label"));
    bool bGenerateOnLoad = GetBoolParam(Params, TEXT("generate_on_load"), false);
    int32 Seed = static_cast<int32>(GetNumberParam(Params, TEXT("seed"), -1.0));

    // Create the PCG graph asset
    UPCGGraph* NewGraph = NewObject<UPCGGraph>(
        GetTransientPackage(), *GraphName, RF_Transactional);
    if (!NewGraph)
    {
        return MakeError(TEXT("CREATE_FAILED"), TEXT("Failed to create PCG graph object"));
    }

    AActor* OwnerActor = nullptr;

    if (!OwnerActorLabel.IsEmpty())
    {
        // Find the specified actor
        for (TActorIterator<AActor> It(World); It; ++It)
        {
            AActor* Actor = *It;
            if (Actor->GetActorLabel() == OwnerActorLabel ||
                Actor->GetPathName() == OwnerActorLabel)
            {
                OwnerActor = Actor;
                break;
            }
        }

        if (!OwnerActor)
        {
            return MakeError(TEXT("ACTOR_NOT_FOUND"),
                FString::Printf(TEXT("Actor '%s' not found"), *OwnerActorLabel));
        }
    }
    else
    {
        // Spawn a new PCGVolume actor to host the graph
        FActorSpawnParameters SpawnParams;
        SpawnParams.SpawnCollisionHandlingOverride =
            ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        OwnerActor = World->SpawnActor<APCGVolume>(SpawnParams);

        if (!OwnerActor)
        {
            return MakeError(TEXT("SPAWN_FAILED"),
                TEXT("Failed to spawn PCGVolume actor"));
        }
        OwnerActor->SetActorLabel(GraphName);
    }

    // Add or find PCG component on the actor
    UPCGComponent* PCGComp = OwnerActor->FindComponentByClass<UPCGComponent>();
    if (!PCGComp)
    {
        PCGComp = NewObject<UPCGComponent>(OwnerActor, TEXT("PCGComponent"));
        if (!PCGComp)
        {
            return MakeError(TEXT("COMPONENT_FAILED"),
                TEXT("Failed to create PCG component"));
        }
        PCGComp->RegisterComponent();
        OwnerActor->AddInstanceComponent(PCGComp);
    }

    // Assign the graph to the component
    PCGComp->SetGraph(NewGraph);
    PCGComp->bGenerated = false;

    // Configure generation settings
    if (bGenerateOnLoad)
    {
        PCGComp->GenerationTrigger = EPCGComponentGenerationTrigger::GenerateOnLoad;
    }
    else
    {
        PCGComp->GenerationTrigger = EPCGComponentGenerationTrigger::GenerateOnDemand;
    }

    // Set seed
    if (Seed >= 0)
    {
        PCGComp->Seed = Seed;
    }

    OwnerActor->MarkPackageDirty();

    TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
    Data->SetStringField(TEXT("graph_name"), NewGraph->GetName());
    Data->SetStringField(TEXT("owner_actor_path"), OwnerActor->GetPathName());
    Data->SetStringField(TEXT("owner_actor_label"), OwnerActor->GetActorLabel());
    Data->SetBoolField(TEXT("generate_on_load"), bGenerateOnLoad);
    Data->SetNumberField(TEXT("seed"), PCGComp->Seed);
    Data->SetNumberField(TEXT("node_count"), NewGraph->GetNodes().Num());
    return MakeSuccess(Data);
}

// ─────────────────────────────────────────────────────────────────────────────
// pcg.add_pcg_node
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FNexusPCGHandler::HandleAddPCGNode(
    const TSharedPtr<FJsonObject>& Params)
{
    UWorld* World = GetEditorWorld();
    if (!World)
    {
        return MakeError(TEXT("NO_WORLD"), TEXT("No editor world available"));
    }

    FString GraphName = GetStringParam(Params, TEXT("graph_name"));
    if (GraphName.IsEmpty())
    {
        return MakeError(TEXT("MISSING_PARAM"), TEXT("graph_name is required"));
    }

    FString NodeType = GetStringParam(Params, TEXT("node_type"));
    if (NodeType.IsEmpty())
    {
        return MakeError(TEXT("MISSING_PARAM"), TEXT("node_type is required"));
    }

    FString NodeLabel = GetStringParam(Params, TEXT("node_label"));

    // Get node position
    double PosX = 0.0, PosY = 0.0;
    const TSharedPtr<FJsonObject>* PositionObj;
    if (Params->TryGetObjectField(TEXT("position"), PositionObj))
    {
        PosX = (*PositionObj)->GetNumberField(TEXT("x"));
        PosY = (*PositionObj)->GetNumberField(TEXT("y"));
    }

    // Find the graph
    UPCGGraph* Graph = FindPCGGraphByName(World, GraphName);
    if (!Graph)
    {
        return MakeError(TEXT("GRAPH_NOT_FOUND"),
            FString::Printf(TEXT("PCG graph '%s' not found"), *GraphName));
    }

    // Find the settings class for the given node type
    // PCG node types are represented as UPCGSettings subclasses
    // Common pattern: UPCGSurfaceSamplerSettings, UPCGStaticMeshSpawnerSettings, etc.
    FString SettingsClassName = FString::Printf(TEXT("PCG%sSettings"), *NodeType);

    UClass* SettingsClass = nullptr;

    // Search for the class by name
    for (TObjectIterator<UClass> ClassIt; ClassIt; ++ClassIt)
    {
        UClass* TestClass = *ClassIt;
        if (TestClass->IsChildOf(UPCGSettings::StaticClass()))
        {
            FString ClassName = TestClass->GetName();
            // Try exact match or partial match
            if (ClassName == SettingsClassName ||
                ClassName == FString::Printf(TEXT("PCG%sSettings"), *NodeType) ||
                ClassName.Contains(NodeType))
            {
                SettingsClass = TestClass;
                break;
            }
        }
    }

    if (!SettingsClass)
    {
        return MakeError(TEXT("UNKNOWN_NODE_TYPE"),
            FString::Printf(TEXT("PCG node type '%s' not found. Could not locate settings class '%s'"),
                *NodeType, *SettingsClassName));
    }

    // Create the settings instance
    UPCGSettings* Settings = NewObject<UPCGSettings>(Graph, SettingsClass);
    if (!Settings)
    {
        return MakeError(TEXT("CREATE_FAILED"),
            FString::Printf(TEXT("Failed to create PCG settings for node type '%s'"),
                *NodeType));
    }

    // Add the node to the graph
    UPCGNode* NewNode = Graph->AddNode(Settings);
    if (!NewNode)
    {
        return MakeError(TEXT("ADD_NODE_FAILED"),
            TEXT("Failed to add node to graph"));
    }

    // Set node label/title
    if (!NodeLabel.IsEmpty())
    {
        NewNode->SetNodeTitle(FName(*NodeLabel));
    }
    else
    {
        NewNode->SetNodeTitle(FName(*FString::Printf(TEXT("%s_%d"), *NodeType, Graph->GetNodes().Num() - 1)));
    }

    // Set node position
    NewNode->PositionX = static_cast<int32>(PosX);
    NewNode->PositionY = static_cast<int32>(PosY);

    Graph->MarkPackageDirty();

    // Build response
    TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
    Data->SetStringField(TEXT("graph_name"), Graph->GetName());
    Data->SetStringField(TEXT("node_label"), NewNode->NodeTitle.ToString());
    Data->SetStringField(TEXT("node_type"), NodeType);
    Data->SetStringField(TEXT("settings_class"), SettingsClass->GetName());
    Data->SetNumberField(TEXT("position_x"), PosX);
    Data->SetNumberField(TEXT("position_y"), PosY);

    // List pins
    TArray<TSharedPtr<FJsonValue>> InputPinsArr;
    for (UPCGPin* Pin : NewNode->GetInputPins())
    {
        if (Pin)
        {
            TSharedPtr<FJsonObject> PinObj = MakeShareable(new FJsonObject());
            PinObj->SetStringField(TEXT("name"), Pin->Properties.Label.ToString());
            InputPinsArr.Add(MakeShareable(new FJsonValueObject(PinObj)));
        }
    }
    Data->SetArrayField(TEXT("input_pins"), InputPinsArr);

    TArray<TSharedPtr<FJsonValue>> OutputPinsArr;
    for (UPCGPin* Pin : NewNode->GetOutputPins())
    {
        if (Pin)
        {
            TSharedPtr<FJsonObject> PinObj = MakeShareable(new FJsonObject());
            PinObj->SetStringField(TEXT("name"), Pin->Properties.Label.ToString());
            OutputPinsArr.Add(MakeShareable(new FJsonValueObject(PinObj)));
        }
    }
    Data->SetArrayField(TEXT("output_pins"), OutputPinsArr);

    Data->SetNumberField(TEXT("total_nodes"), Graph->GetNodes().Num());
    return MakeSuccess(Data);
}

// ─────────────────────────────────────────────────────────────────────────────
// pcg.connect_pcg_nodes
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FNexusPCGHandler::HandleConnectPCGNodes(
    const TSharedPtr<FJsonObject>& Params)
{
    UWorld* World = GetEditorWorld();
    if (!World)
    {
        return MakeError(TEXT("NO_WORLD"), TEXT("No editor world available"));
    }

    FString GraphName = GetStringParam(Params, TEXT("graph_name"));
    if (GraphName.IsEmpty())
    {
        return MakeError(TEXT("MISSING_PARAM"), TEXT("graph_name is required"));
    }

    FString SourceNodeLabel = GetStringParam(Params, TEXT("source_node_label"));
    FString SourcePinName = GetStringParam(Params, TEXT("source_pin"), TEXT("Out"));
    FString TargetNodeLabel = GetStringParam(Params, TEXT("target_node_label"));
    FString TargetPinName = GetStringParam(Params, TEXT("target_pin"), TEXT("In"));

    if (SourceNodeLabel.IsEmpty() || TargetNodeLabel.IsEmpty())
    {
        return MakeError(TEXT("MISSING_PARAM"),
            TEXT("source_node_label and target_node_label are required"));
    }

    // Find graph
    UPCGGraph* Graph = FindPCGGraphByName(World, GraphName);
    if (!Graph)
    {
        return MakeError(TEXT("GRAPH_NOT_FOUND"),
            FString::Printf(TEXT("PCG graph '%s' not found"), *GraphName));
    }

    // Find source node
    UPCGNode* SourceNode = FindNodeByLabel(Graph, SourceNodeLabel);
    if (!SourceNode)
    {
        return MakeError(TEXT("NODE_NOT_FOUND"),
            FString::Printf(TEXT("Source node '%s' not found in graph '%s'"),
                *SourceNodeLabel, *GraphName));
    }

    // Find target node
    UPCGNode* TargetNode = FindNodeByLabel(Graph, TargetNodeLabel);
    if (!TargetNode)
    {
        return MakeError(TEXT("NODE_NOT_FOUND"),
            FString::Printf(TEXT("Target node '%s' not found in graph '%s'"),
                *TargetNodeLabel, *GraphName));
    }

    // Find the output pin on source node
    UPCGPin* SourcePin = nullptr;
    for (UPCGPin* Pin : SourceNode->GetOutputPins())
    {
        if (Pin && (Pin->Properties.Label.ToString() == SourcePinName ||
                    Pin->GetName() == SourcePinName))
        {
            SourcePin = Pin;
            break;
        }
    }

    if (!SourcePin)
    {
        // If no matching pin found by name, try the first output pin
        TArray<UPCGPin*> OutputPins = SourceNode->GetOutputPins();
        if (OutputPins.Num() > 0)
        {
            SourcePin = OutputPins[0];
        }
    }

    if (!SourcePin)
    {
        return MakeError(TEXT("PIN_NOT_FOUND"),
            FString::Printf(TEXT("Output pin '%s' not found on node '%s'"),
                *SourcePinName, *SourceNodeLabel));
    }

    // Find the input pin on target node
    UPCGPin* TargetPin = nullptr;
    for (UPCGPin* Pin : TargetNode->GetInputPins())
    {
        if (Pin && (Pin->Properties.Label.ToString() == TargetPinName ||
                    Pin->GetName() == TargetPinName))
        {
            TargetPin = Pin;
            break;
        }
    }

    if (!TargetPin)
    {
        // If no matching pin found by name, try the first input pin
        TArray<UPCGPin*> InputPins = TargetNode->GetInputPins();
        if (InputPins.Num() > 0)
        {
            TargetPin = InputPins[0];
        }
    }

    if (!TargetPin)
    {
        return MakeError(TEXT("PIN_NOT_FOUND"),
            FString::Printf(TEXT("Input pin '%s' not found on node '%s'"),
                *TargetPinName, *TargetNodeLabel));
    }

    // Create the connection
    bool bConnected = SourcePin->AddEdgeTo(TargetPin);
    if (!bConnected)
    {
        return MakeError(TEXT("CONNECT_FAILED"),
            FString::Printf(TEXT("Failed to connect '%s.%s' to '%s.%s'. "
                "Types may be incompatible."),
                *SourceNodeLabel, *SourcePinName,
                *TargetNodeLabel, *TargetPinName));
    }

    Graph->MarkPackageDirty();

    TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
    Data->SetStringField(TEXT("graph_name"), Graph->GetName());
    Data->SetStringField(TEXT("source_node"), SourceNodeLabel);
    Data->SetStringField(TEXT("source_pin"), SourcePin->Properties.Label.ToString());
    Data->SetStringField(TEXT("target_node"), TargetNodeLabel);
    Data->SetStringField(TEXT("target_pin"), TargetPin->Properties.Label.ToString());
    Data->SetBoolField(TEXT("connected"), true);
    return MakeSuccess(Data);
}

// ─────────────────────────────────────────────────────────────────────────────
// pcg.set_pcg_settings
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FNexusPCGHandler::HandleSetPCGSettings(
    const TSharedPtr<FJsonObject>& Params)
{
    UWorld* World = GetEditorWorld();
    if (!World)
    {
        return MakeError(TEXT("NO_WORLD"), TEXT("No editor world available"));
    }

    FString GraphName = GetStringParam(Params, TEXT("graph_name"));
    if (GraphName.IsEmpty())
    {
        return MakeError(TEXT("MISSING_PARAM"), TEXT("graph_name is required"));
    }

    FString NodeLabel = GetStringParam(Params, TEXT("node_label"));
    if (NodeLabel.IsEmpty())
    {
        return MakeError(TEXT("MISSING_PARAM"), TEXT("node_label is required"));
    }

    const TSharedPtr<FJsonObject>* SettingsObj;
    if (!Params->TryGetObjectField(TEXT("settings"), SettingsObj) || !SettingsObj)
    {
        return MakeError(TEXT("MISSING_PARAM"), TEXT("settings is required"));
    }

    // Find graph
    UPCGGraph* Graph = FindPCGGraphByName(World, GraphName);
    if (!Graph)
    {
        return MakeError(TEXT("GRAPH_NOT_FOUND"),
            FString::Printf(TEXT("PCG graph '%s' not found"), *GraphName));
    }

    // Find node
    UPCGNode* Node = FindNodeByLabel(Graph, NodeLabel);
    if (!Node)
    {
        return MakeError(TEXT("NODE_NOT_FOUND"),
            FString::Printf(TEXT("Node '%s' not found in graph '%s'"),
                *NodeLabel, *GraphName));
    }

    // Get the node's settings object
    UPCGSettings* NodeSettings = Node->GetSettings();
    if (!NodeSettings)
    {
        return MakeError(TEXT("NO_SETTINGS"),
            FString::Printf(TEXT("Node '%s' has no settings to modify"),
                *NodeLabel));
    }

    // Apply settings via property reflection
    UClass* SettingsClass = NodeSettings->GetClass();
    TArray<FString> AppliedKeys;
    TArray<FString> FailedKeys;

    for (const auto& Pair : (*SettingsObj)->Values)
    {
        const FString& PropName = Pair.Key;
        const TSharedPtr<FJsonValue>& PropValue = Pair.Value;

        FProperty* Property = SettingsClass->FindPropertyByName(*PropName);
        if (!Property)
        {
            FailedKeys.Add(FString::Printf(TEXT("%s (property not found)"), *PropName));
            continue;
        }

        void* PropertyAddr = Property->ContainerPtrToValuePtr<void>(NodeSettings);
        bool bSet = false;

        // Handle common property types
        if (FFloatProperty* FloatProp = CastField<FFloatProperty>(Property))
        {
            FloatProp->SetPropertyValue(PropertyAddr,
                static_cast<float>(PropValue->AsNumber()));
            bSet = true;
        }
        else if (FDoubleProperty* DoubleProp = CastField<FDoubleProperty>(Property))
        {
            DoubleProp->SetPropertyValue(PropertyAddr, PropValue->AsNumber());
            bSet = true;
        }
        else if (FIntProperty* IntProp = CastField<FIntProperty>(Property))
        {
            IntProp->SetPropertyValue(PropertyAddr,
                static_cast<int32>(PropValue->AsNumber()));
            bSet = true;
        }
        else if (FBoolProperty* BoolProp = CastField<FBoolProperty>(Property))
        {
            BoolProp->SetPropertyValue(PropertyAddr, PropValue->AsBool());
            bSet = true;
        }
        else if (FStrProperty* StrProp = CastField<FStrProperty>(Property))
        {
            StrProp->SetPropertyValue(PropertyAddr, PropValue->AsString());
            bSet = true;
        }
        else if (FNameProperty* NameProp = CastField<FNameProperty>(Property))
        {
            NameProp->SetPropertyValue(PropertyAddr, FName(*PropValue->AsString()));
            bSet = true;
        }
        else if (FEnumProperty* EnumProp = CastField<FEnumProperty>(Property))
        {
            // Try setting enum by name or value
            FNumericProperty* UnderlyingProp = EnumProp->GetUnderlyingProperty();
            if (UnderlyingProp)
            {
                // Try as number first
                double NumVal;
                if (PropValue->TryGetNumber(NumVal))
                {
                    UnderlyingProp->SetIntPropertyValue(PropertyAddr,
                        static_cast<int64>(NumVal));
                    bSet = true;
                }
                else
                {
                    // Try as enum name string
                    UEnum* Enum = EnumProp->GetEnum();
                    if (Enum)
                    {
                        int64 EnumVal = Enum->GetValueByNameString(PropValue->AsString());
                        if (EnumVal != INDEX_NONE)
                        {
                            UnderlyingProp->SetIntPropertyValue(PropertyAddr, EnumVal);
                            bSet = true;
                        }
                    }
                }
            }
        }

        if (bSet)
        {
            AppliedKeys.Add(PropName);
        }
        else
        {
            FailedKeys.Add(FString::Printf(TEXT("%s (unsupported type: %s)"),
                *PropName, *Property->GetClass()->GetName()));
        }
    }

    if (AppliedKeys.Num() > 0)
    {
        Graph->MarkPackageDirty();
    }

    TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
    Data->SetStringField(TEXT("graph_name"), Graph->GetName());
    Data->SetStringField(TEXT("node_label"), NodeLabel);
    Data->SetStringField(TEXT("settings_class"), SettingsClass->GetName());
    Data->SetNumberField(TEXT("applied_count"), AppliedKeys.Num());
    Data->SetNumberField(TEXT("failed_count"), FailedKeys.Num());

    TArray<TSharedPtr<FJsonValue>> AppliedArr;
    for (const FString& Key : AppliedKeys)
    {
        AppliedArr.Add(MakeShareable(new FJsonValueString(Key)));
    }
    Data->SetArrayField(TEXT("applied"), AppliedArr);

    if (FailedKeys.Num() > 0)
    {
        TArray<TSharedPtr<FJsonValue>> FailedArr;
        for (const FString& Key : FailedKeys)
        {
            FailedArr.Add(MakeShareable(new FJsonValueString(Key)));
        }
        Data->SetArrayField(TEXT("failed"), FailedArr);
    }

    return MakeSuccess(Data);
}

// ─────────────────────────────────────────────────────────────────────────────
// pcg.execute_pcg_graph
// Long-running: 120s timeout
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FNexusPCGHandler::HandleExecutePCGGraph(
    const TSharedPtr<FJsonObject>& Params)
{
    UWorld* World = GetEditorWorld();
    if (!World)
    {
        return MakeError(TEXT("NO_WORLD"), TEXT("No editor world available"));
    }

    FString GraphName = GetStringParam(Params, TEXT("graph_name"));
    FString OwnerActorLabel = GetStringParam(Params, TEXT("owner_actor_label"));
    int32 Seed = static_cast<int32>(GetNumberParam(Params, TEXT("seed"), -1.0));
    bool bForceRegenerate = GetBoolParam(Params, TEXT("force_regenerate"), true);

    // Find the PCG component
    UPCGComponent* PCGComp = FindPCGComponentByLabels(World, GraphName, OwnerActorLabel);
    if (!PCGComp)
    {
        return MakeError(TEXT("PCG_COMPONENT_NOT_FOUND"),
            FString::Printf(TEXT("PCG component not found for graph '%s' / actor '%s'"),
                *GraphName, *OwnerActorLabel));
    }

    UPCGGraph* Graph = PCGComp->GetGraph();
    if (!Graph)
    {
        return MakeError(TEXT("NO_GRAPH"),
            TEXT("PCG component has no graph assigned"));
    }

    // Override seed if specified
    if (Seed >= 0)
    {
        PCGComp->Seed = Seed;
    }

    // Clean up previously generated content if force regenerating
    if (bForceRegenerate)
    {
        PCGComp->CleanupLocalImmediate(true);
    }

    // Execute the graph via PCG subsystem
    UPCGSubsystem* PCGSubsystem = World->GetSubsystem<UPCGSubsystem>();
    if (!PCGSubsystem)
    {
        return MakeError(TEXT("NO_PCG_SUBSYSTEM"),
            TEXT("PCG subsystem not available"));
    }

    // Trigger generation
    PCGComp->Generate(bForceRegenerate);

    // Get generation results
    AActor* OwnerActor = PCGComp->GetOwner();

    TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
    Data->SetStringField(TEXT("graph_name"), Graph->GetName());
    Data->SetStringField(TEXT("owner_actor_label"),
        OwnerActor ? OwnerActor->GetActorLabel() : TEXT(""));
    Data->SetStringField(TEXT("owner_actor_path"),
        OwnerActor ? OwnerActor->GetPathName() : TEXT(""));
    Data->SetNumberField(TEXT("seed"), PCGComp->Seed);
    Data->SetBoolField(TEXT("force_regenerate"), bForceRegenerate);
    Data->SetBoolField(TEXT("generated"), PCGComp->bGenerated);
    Data->SetNumberField(TEXT("node_count"), Graph->GetNodes().Num());

    // Count generated managed resources
    int32 GeneratedActorCount = 0;
    if (OwnerActor)
    {
        // Count child actors that were generated
        TArray<AActor*> ChildActors;
        OwnerActor->GetAttachedActors(ChildActors);
        GeneratedActorCount = ChildActors.Num();
    }
    Data->SetNumberField(TEXT("generated_actor_count"), GeneratedActorCount);

    if (OwnerActor)
    {
        OwnerActor->MarkPackageDirty();
    }

    return MakeSuccess(Data);
}

// ─────────────────────────────────────────────────────────────────────────────
// pcg.get_pcg_info
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FNexusPCGHandler::HandleGetPCGInfo(
    const TSharedPtr<FJsonObject>& Params)
{
    UWorld* World = GetEditorWorld();
    if (!World)
    {
        return MakeError(TEXT("NO_WORLD"), TEXT("No editor world available"));
    }

    FString GraphName = GetStringParam(Params, TEXT("graph_name"));
    FString OwnerActorLabel = GetStringParam(Params, TEXT("owner_actor_label"));

    // If no graph name and no actor, list all PCG components in the world
    if (GraphName.IsEmpty() && OwnerActorLabel.IsEmpty())
    {
        TArray<TSharedPtr<FJsonValue>> PCGArr;

        for (TActorIterator<AActor> It(World); It; ++It)
        {
            AActor* Actor = *It;
            TArray<UPCGComponent*> PCGComps;
            Actor->GetComponents<UPCGComponent>(PCGComps);

            for (UPCGComponent* Comp : PCGComps)
            {
                if (!Comp) continue;

                TSharedPtr<FJsonObject> CompObj = MakeShareable(new FJsonObject());
                CompObj->SetStringField(TEXT("actor_label"), Actor->GetActorLabel());
                CompObj->SetStringField(TEXT("actor_path"), Actor->GetPathName());
                CompObj->SetBoolField(TEXT("generated"), Comp->bGenerated);
                CompObj->SetNumberField(TEXT("seed"), Comp->Seed);

                UPCGGraph* Graph = Comp->GetGraph();
                if (Graph)
                {
                    CompObj->SetStringField(TEXT("graph_name"), Graph->GetName());
                    CompObj->SetNumberField(TEXT("node_count"), Graph->GetNodes().Num());
                }

                FString TriggerStr;
                switch (Comp->GenerationTrigger)
                {
                case EPCGComponentGenerationTrigger::GenerateOnLoad:
                    TriggerStr = TEXT("OnLoad");
                    break;
                case EPCGComponentGenerationTrigger::GenerateOnDemand:
                    TriggerStr = TEXT("OnDemand");
                    break;
                default:
                    TriggerStr = TEXT("Unknown");
                    break;
                }
                CompObj->SetStringField(TEXT("generation_trigger"), TriggerStr);

                PCGArr.Add(MakeShareable(new FJsonValueObject(CompObj)));
            }
        }

        TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
        Data->SetArrayField(TEXT("pcg_components"), PCGArr);
        Data->SetNumberField(TEXT("count"), PCGArr.Num());
        return MakeSuccess(Data);
    }

    // Find specific PCG component
    UPCGComponent* PCGComp = FindPCGComponentByLabels(World, GraphName, OwnerActorLabel);
    if (!PCGComp)
    {
        return MakeError(TEXT("PCG_COMPONENT_NOT_FOUND"),
            FString::Printf(TEXT("PCG component not found for graph '%s' / actor '%s'"),
                *GraphName, *OwnerActorLabel));
    }

    UPCGGraph* Graph = PCGComp->GetGraph();
    AActor* OwnerActor = PCGComp->GetOwner();

    TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
    Data->SetStringField(TEXT("owner_actor_label"),
        OwnerActor ? OwnerActor->GetActorLabel() : TEXT(""));
    Data->SetStringField(TEXT("owner_actor_path"),
        OwnerActor ? OwnerActor->GetPathName() : TEXT(""));
    Data->SetBoolField(TEXT("generated"), PCGComp->bGenerated);
    Data->SetNumberField(TEXT("seed"), PCGComp->Seed);

    FString TriggerStr;
    switch (PCGComp->GenerationTrigger)
    {
    case EPCGComponentGenerationTrigger::GenerateOnLoad:
        TriggerStr = TEXT("OnLoad");
        break;
    case EPCGComponentGenerationTrigger::GenerateOnDemand:
        TriggerStr = TEXT("OnDemand");
        break;
    default:
        TriggerStr = TEXT("Unknown");
        break;
    }
    Data->SetStringField(TEXT("generation_trigger"), TriggerStr);

    if (Graph)
    {
        Data->SetStringField(TEXT("graph_name"), Graph->GetName());

        // Enumerate nodes
        TArray<TSharedPtr<FJsonValue>> NodesArr;
        for (UPCGNode* Node : Graph->GetNodes())
        {
            if (!Node) continue;

            TSharedPtr<FJsonObject> NodeObj = MakeShareable(new FJsonObject());
            NodeObj->SetStringField(TEXT("label"), Node->NodeTitle.ToString());
            NodeObj->SetStringField(TEXT("name"), Node->GetName());
            NodeObj->SetNumberField(TEXT("position_x"), Node->PositionX);
            NodeObj->SetNumberField(TEXT("position_y"), Node->PositionY);

            UPCGSettings* Settings = Node->GetSettings();
            if (Settings)
            {
                NodeObj->SetStringField(TEXT("settings_class"),
                    Settings->GetClass()->GetName());
            }

            // Input pins
            TArray<TSharedPtr<FJsonValue>> InputPinsArr;
            for (UPCGPin* Pin : Node->GetInputPins())
            {
                if (!Pin) continue;

                TSharedPtr<FJsonObject> PinObj = MakeShareable(new FJsonObject());
                PinObj->SetStringField(TEXT("name"), Pin->Properties.Label.ToString());
                PinObj->SetNumberField(TEXT("edge_count"), Pin->EdgeCount());
                InputPinsArr.Add(MakeShareable(new FJsonValueObject(PinObj)));
            }
            NodeObj->SetArrayField(TEXT("input_pins"), InputPinsArr);

            // Output pins
            TArray<TSharedPtr<FJsonValue>> OutputPinsArr;
            for (UPCGPin* Pin : Node->GetOutputPins())
            {
                if (!Pin) continue;

                TSharedPtr<FJsonObject> PinObj = MakeShareable(new FJsonObject());
                PinObj->SetStringField(TEXT("name"), Pin->Properties.Label.ToString());
                PinObj->SetNumberField(TEXT("edge_count"), Pin->EdgeCount());
                OutputPinsArr.Add(MakeShareable(new FJsonValueObject(PinObj)));
            }
            NodeObj->SetArrayField(TEXT("output_pins"), OutputPinsArr);

            NodesArr.Add(MakeShareable(new FJsonValueObject(NodeObj)));
        }
        Data->SetArrayField(TEXT("nodes"), NodesArr);
        Data->SetNumberField(TEXT("node_count"), NodesArr.Num());

        // Enumerate edges/connections
        TArray<TSharedPtr<FJsonValue>> EdgesArr;
        for (UPCGNode* Node : Graph->GetNodes())
        {
            if (!Node) continue;

            for (UPCGPin* OutputPin : Node->GetOutputPins())
            {
                if (!OutputPin) continue;

                for (UPCGEdge* Edge : OutputPin->Edges)
                {
                    if (!Edge) continue;
                    UPCGPin* ConnectedPin = Edge->GetOtherPin(OutputPin);
                    if (!ConnectedPin || !ConnectedPin->Node) continue;

                    TSharedPtr<FJsonObject> EdgeObj = MakeShareable(new FJsonObject());
                    EdgeObj->SetStringField(TEXT("source_node"),
                        Node->NodeTitle.ToString());
                    EdgeObj->SetStringField(TEXT("source_pin"),
                        OutputPin->Properties.Label.ToString());
                    EdgeObj->SetStringField(TEXT("target_node"),
                        ConnectedPin->Node->NodeTitle.ToString());
                    EdgeObj->SetStringField(TEXT("target_pin"),
                        ConnectedPin->Properties.Label.ToString());
                    EdgesArr.Add(MakeShareable(new FJsonValueObject(EdgeObj)));
                }
            }
        }
        Data->SetArrayField(TEXT("edges"), EdgesArr);
        Data->SetNumberField(TEXT("edge_count"), EdgesArr.Num());
    }

    // Count generated actors
    if (OwnerActor)
    {
        TArray<AActor*> ChildActors;
        OwnerActor->GetAttachedActors(ChildActors);
        Data->SetNumberField(TEXT("generated_actor_count"), ChildActors.Num());
    }

    return MakeSuccess(Data);
}
