// Copyright Nexus Team. All Rights Reserved.

#include "Handlers/NexusAIHandler.h"
#include "Editor.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Pawn.h"
#include "AIController.h"
#include "BehaviorTree/BehaviorTree.h"
#include "BehaviorTree/BehaviorTreeComponent.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "BehaviorTree/BlackboardData.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Bool.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Int.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Float.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_String.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Name.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Vector.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Rotator.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Object.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Class.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Enum.h"
#include "EnvironmentQuery/EnvQuery.h"
#include "EnvironmentQuery/EnvQueryManager.h"
#include "Perception/AIPerceptionComponent.h"
#include "Perception/AISenseConfig_Sight.h"
#include "Perception/AISenseConfig_Hearing.h"
#include "Perception/AISenseConfig_Damage.h"
#include "Perception/AISenseConfig_Team.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Factories/DataAssetFactory.h"
#include "UObject/SavePackage.h"
// Behavior Tree node CRUD (Phase 4A)
#include "BehaviorTreeGraph.h"
#include "BehaviorTreeGraphNode.h"
#include "BehaviorTreeGraphNode_Composite.h"
#include "BehaviorTreeGraphNode_Task.h"
#include "BehaviorTreeGraphNode_Decorator.h"
#include "BehaviorTreeGraphNode_Service.h"
#include "BehaviorTree/BTCompositeNode.h"
#include "BehaviorTree/BTTaskNode.h"
#include "BehaviorTree/BTDecorator.h"
#include "BehaviorTree/BTService.h"
#include "BehaviorTree/Composites/BTComposite_Selector.h"
#include "BehaviorTree/Composites/BTComposite_Sequence.h"
#include "BehaviorTree/Composites/BTComposite_SimpleParallel.h"
#include "BehaviorTree/Tasks/BTTask_Wait.h"
#include "BehaviorTree/Tasks/BTTask_MoveTo.h"
#include "BehaviorTree/Decorators/BTDecorator_Blackboard.h"
#include "BehaviorTree/Decorators/BTDecorator_Cooldown.h"
#include "BehaviorTree/Decorators/BTDecorator_Loop.h"
#include "AIGraphNode.h"
// EQS node CRUD (Phase 4B)
#include "EnvironmentQuery/EnvQueryOption.h"
#include "EnvironmentQuery/EnvQueryGenerator.h"
#include "EnvironmentQuery/EnvQueryTest.h"
#include "EnvironmentQuery/Generators/EnvQueryGenerator_SimpleGrid.h"
#include "EnvironmentQuery/Generators/EnvQueryGenerator_OnCircle.h"
#include "EnvironmentQuery/Generators/EnvQueryGenerator_Donut.h"
#include "EnvironmentQuery/Generators/EnvQueryGenerator_ActorsOfClass.h"
#include "EnvironmentQuery/Tests/EnvQueryTest_Distance.h"
#include "EnvironmentQuery/Tests/EnvQueryTest_Trace.h"
#include "EnvironmentQuery/Tests/EnvQueryTest_Pathfinding.h"
#include "EnvironmentQuery/Tests/EnvQueryTest_Dot.h"

// ─────────────────────────────────────────────────────────────────────────────
// Dispatch
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FNexusAIHandler::Handle(
    const FString& CommandType,
    const TSharedPtr<FJsonObject>& Params)
{
    FString SubCommand = CommandType.RightChop(GetNamespace().Len() + 1);

    if (SubCommand == TEXT("create_behavior_tree"))   return HandleCreateBehaviorTree(Params);
    if (SubCommand == TEXT("create_blackboard"))       return HandleCreateBlackboard(Params);
    if (SubCommand == TEXT("create_eqs_query"))        return HandleCreateEQSQuery(Params);
    if (SubCommand == TEXT("create_state_tree"))       return HandleCreateStateTree(Params);
    if (SubCommand == TEXT("set_blackboard_key"))      return HandleSetBlackboardKey(Params);
    if (SubCommand == TEXT("set_ai_perception"))       return HandleSetAIPerception(Params);
    if (SubCommand == TEXT("assign_behavior_tree"))    return HandleAssignBehaviorTree(Params);
    if (SubCommand == TEXT("get_ai_controller_info"))  return HandleGetAIControllerInfo(Params);
    if (SubCommand == TEXT("list_behavior_trees"))     return HandleListBehaviorTrees(Params);
    if (SubCommand == TEXT("run_eqs_query"))           return HandleRunEQSQuery(Params);
    // Behavior Tree node CRUD (Phase 4A)
    if (SubCommand == TEXT("add_bt_node"))             return HandleAddBTNode(Params);
    if (SubCommand == TEXT("get_bt_nodes"))             return HandleGetBTNodes(Params);
    if (SubCommand == TEXT("update_bt_node"))           return HandleUpdateBTNode(Params);
    if (SubCommand == TEXT("remove_bt_node"))           return HandleRemoveBTNode(Params);
    if (SubCommand == TEXT("connect_bt_nodes"))         return HandleConnectBTNodes(Params);
    if (SubCommand == TEXT("disconnect_bt_node"))       return HandleDisconnectBTNode(Params);
    // EQS node CRUD (Phase 4B)
    if (SubCommand == TEXT("add_eqs_generator"))        return HandleAddEQSGenerator(Params);
    if (SubCommand == TEXT("add_eqs_test"))             return HandleAddEQSTest(Params);
    if (SubCommand == TEXT("get_eqs_nodes"))            return HandleGetEQSNodes(Params);
    if (SubCommand == TEXT("update_eqs_node"))          return HandleUpdateEQSNode(Params);
    if (SubCommand == TEXT("remove_eqs_node"))          return HandleRemoveEQSNode(Params);

    return MakeError(TEXT("NOT_IMPLEMENTED"),
        FString::Printf(TEXT("Command '%s' not yet implemented"), *CommandType));
}

// ─────────────────────────────────────────────────────────────────────────────
// ai.add_bt_node
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FNexusAIHandler::HandleAddBTNode(
    const TSharedPtr<FJsonObject>& Params)
{
    FString TreePath = GetStringParam(Params, TEXT("tree_path"));
    FString NodeType = GetStringParam(Params, TEXT("node_type"));
    FString ParentNodeId = GetStringParam(Params, TEXT("parent_node_id"));
    double PosX = GetNumberParam(Params, TEXT("position_x"), 0.0);
    double PosY = GetNumberParam(Params, TEXT("position_y"), 0.0);

    if (TreePath.IsEmpty() || NodeType.IsEmpty())
    {
        return MakeError(TEXT("MISSING_PARAM"),
            TEXT("tree_path and node_type are required"));
    }

    UBehaviorTree* BT = LoadObject<UBehaviorTree>(nullptr, *TreePath);
    if (!BT)
    {
        return MakeError(TEXT("NOT_FOUND"),
            FString::Printf(TEXT("Behavior Tree not found at '%s'"), *TreePath));
    }

    UBehaviorTreeGraph* BTGraph = Cast<UBehaviorTreeGraph>(BT->BTGraph);
    if (!BTGraph)
    {
        return MakeError(TEXT("NO_GRAPH"),
            TEXT("Behavior Tree has no graph. Open it in the editor first."));
    }

    // Map node_type string to graph node class + runtime BT node class
    // Composites
    static TMap<FString, TPair<UClass*, UClass*>> CompositeMap;
    if (CompositeMap.Num() == 0)
    {
        CompositeMap.Add(TEXT("Selector"),
            TPair<UClass*, UClass*>(UBehaviorTreeGraphNode_Composite::StaticClass(), UBTComposite_Selector::StaticClass()));
        CompositeMap.Add(TEXT("Sequence"),
            TPair<UClass*, UClass*>(UBehaviorTreeGraphNode_Composite::StaticClass(), UBTComposite_Sequence::StaticClass()));
        CompositeMap.Add(TEXT("SimpleParallel"),
            TPair<UClass*, UClass*>(UBehaviorTreeGraphNode_Composite::StaticClass(), UBTComposite_SimpleParallel::StaticClass()));
    }

    // Tasks
    static TMap<FString, TPair<UClass*, UClass*>> TaskMap;
    if (TaskMap.Num() == 0)
    {
        TaskMap.Add(TEXT("Wait"),
            TPair<UClass*, UClass*>(UBehaviorTreeGraphNode_Task::StaticClass(), UBTTask_Wait::StaticClass()));
        TaskMap.Add(TEXT("MoveTo"),
            TPair<UClass*, UClass*>(UBehaviorTreeGraphNode_Task::StaticClass(), UBTTask_MoveTo::StaticClass()));
    }

    // Decorators
    static TMap<FString, TPair<UClass*, UClass*>> DecoratorMap;
    if (DecoratorMap.Num() == 0)
    {
        DecoratorMap.Add(TEXT("BlackboardDecorator"),
            TPair<UClass*, UClass*>(UBehaviorTreeGraphNode_Decorator::StaticClass(), UBTDecorator_Blackboard::StaticClass()));
        DecoratorMap.Add(TEXT("Cooldown"),
            TPair<UClass*, UClass*>(UBehaviorTreeGraphNode_Decorator::StaticClass(), UBTDecorator_Cooldown::StaticClass()));
        DecoratorMap.Add(TEXT("Loop"),
            TPair<UClass*, UClass*>(UBehaviorTreeGraphNode_Decorator::StaticClass(), UBTDecorator_Loop::StaticClass()));
    }

    // Services
    static TMap<FString, TPair<UClass*, UClass*>> ServiceMap;
    if (ServiceMap.Num() == 0)
    {
        ServiceMap.Add(TEXT("DefaultFocus"),
            TPair<UClass*, UClass*>(UBehaviorTreeGraphNode_Service::StaticClass(), UBTService::StaticClass()));
    }

    UClass* GraphNodeClass = nullptr;
    UClass* RuntimeNodeClass = nullptr;
    FString NodeCategory;

    // Search all maps
    if (auto* FoundComposite = CompositeMap.Find(NodeType))
    {
        GraphNodeClass = FoundComposite->Key;
        RuntimeNodeClass = FoundComposite->Value;
        NodeCategory = TEXT("Composite");
    }
    else if (auto* FoundTask = TaskMap.Find(NodeType))
    {
        GraphNodeClass = FoundTask->Key;
        RuntimeNodeClass = FoundTask->Value;
        NodeCategory = TEXT("Task");
    }
    else if (auto* FoundDecorator = DecoratorMap.Find(NodeType))
    {
        GraphNodeClass = FoundDecorator->Key;
        RuntimeNodeClass = FoundDecorator->Value;
        NodeCategory = TEXT("Decorator");
    }
    else if (auto* FoundService = ServiceMap.Find(NodeType))
    {
        GraphNodeClass = FoundService->Key;
        RuntimeNodeClass = FoundService->Value;
        NodeCategory = TEXT("Service");
    }
    else
    {
        // Try dynamic class lookup for BT node types
        FString ClassPath = FString::Printf(TEXT("/Script/AIModule.BT%s"), *NodeType);
        RuntimeNodeClass = LoadClass<UBTNode>(nullptr, *ClassPath);
        if (!RuntimeNodeClass)
        {
            ClassPath = FString::Printf(TEXT("/Script/AIModule.BTTask_%s"), *NodeType);
            RuntimeNodeClass = LoadClass<UBTNode>(nullptr, *ClassPath);
        }
        if (!RuntimeNodeClass)
        {
            ClassPath = FString::Printf(TEXT("/Script/AIModule.BTComposite_%s"), *NodeType);
            RuntimeNodeClass = LoadClass<UBTNode>(nullptr, *ClassPath);
        }
        if (!RuntimeNodeClass)
        {
            return MakeError(TEXT("UNKNOWN_NODE_TYPE"),
                FString::Printf(TEXT("Unknown BT node type: '%s'. Supported: Selector, Sequence, SimpleParallel, Wait, MoveTo, BlackboardDecorator, Cooldown, Loop, DefaultFocus"), *NodeType));
        }

        // Determine graph node class from runtime class hierarchy
        if (RuntimeNodeClass->IsChildOf(UBTCompositeNode::StaticClass()))
        {
            GraphNodeClass = UBehaviorTreeGraphNode_Composite::StaticClass();
            NodeCategory = TEXT("Composite");
        }
        else if (RuntimeNodeClass->IsChildOf(UBTTaskNode::StaticClass()))
        {
            GraphNodeClass = UBehaviorTreeGraphNode_Task::StaticClass();
            NodeCategory = TEXT("Task");
        }
        else if (RuntimeNodeClass->IsChildOf(UBTDecorator::StaticClass()))
        {
            GraphNodeClass = UBehaviorTreeGraphNode_Decorator::StaticClass();
            NodeCategory = TEXT("Decorator");
        }
        else if (RuntimeNodeClass->IsChildOf(UBTService::StaticClass()))
        {
            GraphNodeClass = UBehaviorTreeGraphNode_Service::StaticClass();
            NodeCategory = TEXT("Service");
        }
        else
        {
            GraphNodeClass = UBehaviorTreeGraphNode_Task::StaticClass();
            NodeCategory = TEXT("Task");
        }
    }

    // Decorators and Services are subnodes — they attach to a parent graph node
    bool bIsSubNode = (NodeCategory == TEXT("Decorator") || NodeCategory == TEXT("Service"));

    if (bIsSubNode)
    {
        // Subnodes must have a parent
        if (ParentNodeId.IsEmpty())
        {
            return MakeError(TEXT("MISSING_PARAM"),
                TEXT("parent_node_id is required for Decorator and Service nodes"));
        }

        // Find parent graph node
        UBehaviorTreeGraphNode* ParentGraphNode = nullptr;
        for (UEdGraphNode* Node : BTGraph->Nodes)
        {
            if (Node && Node->NodeGuid.ToString() == ParentNodeId)
            {
                ParentGraphNode = Cast<UBehaviorTreeGraphNode>(Node);
                break;
            }
        }
        if (!ParentGraphNode)
        {
            return MakeError(TEXT("NOT_FOUND"),
                FString::Printf(TEXT("Parent node '%s' not found"), *ParentNodeId));
        }

        // Create the subnode graph node
        UBehaviorTreeGraphNode* SubGraphNode = NewObject<UBehaviorTreeGraphNode>(
            BTGraph, GraphNodeClass);
        if (!SubGraphNode)
        {
            return MakeError(TEXT("CREATE_FAILED"), TEXT("Failed to create BT sub-graph node"));
        }

        // Create and assign the runtime BT node
        UBTNode* RuntimeNode = NewObject<UBTNode>(SubGraphNode, RuntimeNodeClass);
        if (!RuntimeNode)
        {
            return MakeError(TEXT("CREATE_FAILED"), TEXT("Failed to create runtime BT node"));
        }
        SubGraphNode->NodeInstance = RuntimeNode;

        // Use UAIGraphNode::AddSubNode to properly attach
        // UE 5.7: AddSubNode is on UAIGraphNode base class
        ParentGraphNode->AddSubNode(SubGraphNode, BTGraph);
        SubGraphNode->AllocateDefaultPins();

        // Update the graph asset
        BTGraph->NotifyGraphChanged();
        BT->MarkPackageDirty();

        TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
        Data->SetStringField(TEXT("node_id"), SubGraphNode->NodeGuid.ToString());
        Data->SetStringField(TEXT("node_type"), NodeType);
        Data->SetStringField(TEXT("node_class"), RuntimeNodeClass->GetName());
        Data->SetStringField(TEXT("node_category"), NodeCategory);
        Data->SetStringField(TEXT("parent_node_id"), ParentNodeId);
        Data->SetStringField(TEXT("title"), SubGraphNode->GetNodeTitle(ENodeTitleType::FullTitle).ToString());
        return MakeSuccess(Data);
    }

    // Main node (Composite or Task) — added to graph directly
    UBehaviorTreeGraphNode* NewGraphNode = NewObject<UBehaviorTreeGraphNode>(
        BTGraph, GraphNodeClass);
    if (!NewGraphNode)
    {
        return MakeError(TEXT("CREATE_FAILED"), TEXT("Failed to create BT graph node"));
    }

    // Create and assign the runtime BT node
    UBTNode* RuntimeNode = NewObject<UBTNode>(NewGraphNode, RuntimeNodeClass);
    if (!RuntimeNode)
    {
        return MakeError(TEXT("CREATE_FAILED"), TEXT("Failed to create runtime BT node"));
    }
    NewGraphNode->NodeInstance = RuntimeNode;

    NewGraphNode->NodePosX = static_cast<int32>(PosX);
    NewGraphNode->NodePosY = static_cast<int32>(PosY);

    BTGraph->AddNode(NewGraphNode, /*bUserAction=*/true, /*bSelectNewNode=*/false);
    NewGraphNode->AllocateDefaultPins();

    // If parent specified, connect output of parent to input of new node
    if (!ParentNodeId.IsEmpty())
    {
        UEdGraphNode* ParentNode = nullptr;
        for (UEdGraphNode* Node : BTGraph->Nodes)
        {
            if (Node && Node->NodeGuid.ToString() == ParentNodeId)
            {
                ParentNode = Node;
                break;
            }
        }
        if (ParentNode)
        {
            // Find output pin on parent, input pin on child
            UEdGraphPin* ParentOutput = nullptr;
            for (UEdGraphPin* Pin : ParentNode->Pins)
            {
                if (Pin && Pin->Direction == EGPD_Output)
                {
                    ParentOutput = Pin;
                    break;
                }
            }

            UEdGraphPin* ChildInput = nullptr;
            for (UEdGraphPin* Pin : NewGraphNode->Pins)
            {
                if (Pin && Pin->Direction == EGPD_Input)
                {
                    ChildInput = Pin;
                    break;
                }
            }

            if (ParentOutput && ChildInput)
            {
                const UEdGraphSchema* Schema = BTGraph->GetSchema();
                if (Schema)
                {
                    Schema->TryCreateConnection(ParentOutput, ChildInput);
                }
            }
        }
    }

    // Update graph
    BTGraph->NotifyGraphChanged();
    BT->MarkPackageDirty();

    // Build response
    TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
    Data->SetStringField(TEXT("node_id"), NewGraphNode->NodeGuid.ToString());
    Data->SetStringField(TEXT("node_type"), NodeType);
    Data->SetStringField(TEXT("node_class"), RuntimeNodeClass->GetName());
    Data->SetStringField(TEXT("node_category"), NodeCategory);
    Data->SetNumberField(TEXT("position_x"), PosX);
    Data->SetNumberField(TEXT("position_y"), PosY);
    Data->SetStringField(TEXT("title"), NewGraphNode->GetNodeTitle(ENodeTitleType::FullTitle).ToString());

    // Pins
    TArray<TSharedPtr<FJsonValue>> PinsArr;
    for (UEdGraphPin* Pin : NewGraphNode->Pins)
    {
        if (!Pin) continue;
        TSharedPtr<FJsonObject> PinObj = MakeShareable(new FJsonObject());
        PinObj->SetStringField(TEXT("pin_id"), Pin->PinId.ToString());
        PinObj->SetStringField(TEXT("pin_name"), Pin->PinName.ToString());
        PinObj->SetStringField(TEXT("direction"),
            Pin->Direction == EGPD_Input ? TEXT("input") : TEXT("output"));
        PinsArr.Add(MakeShareable(new FJsonValueObject(PinObj)));
    }
    Data->SetArrayField(TEXT("pins"), PinsArr);

    if (!ParentNodeId.IsEmpty())
    {
        Data->SetStringField(TEXT("parent_node_id"), ParentNodeId);
    }

    return MakeSuccess(Data);
}

// ─────────────────────────────────────────────────────────────────────────────
// ai.get_bt_nodes
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FNexusAIHandler::HandleGetBTNodes(
    const TSharedPtr<FJsonObject>& Params)
{
    FString TreePath = GetStringParam(Params, TEXT("tree_path"));
    if (TreePath.IsEmpty())
    {
        return MakeError(TEXT("MISSING_PARAM"), TEXT("tree_path is required"));
    }

    UBehaviorTree* BT = LoadObject<UBehaviorTree>(nullptr, *TreePath);
    if (!BT)
    {
        return MakeError(TEXT("NOT_FOUND"),
            FString::Printf(TEXT("Behavior Tree not found at '%s'"), *TreePath));
    }

    UBehaviorTreeGraph* BTGraph = Cast<UBehaviorTreeGraph>(BT->BTGraph);
    if (!BTGraph)
    {
        return MakeError(TEXT("NO_GRAPH"),
            TEXT("Behavior Tree has no graph. Open it in the editor first."));
    }

    TArray<TSharedPtr<FJsonValue>> NodesArr;
    for (UEdGraphNode* Node : BTGraph->Nodes)
    {
        UBehaviorTreeGraphNode* BTNode = Cast<UBehaviorTreeGraphNode>(Node);
        if (!BTNode) continue;

        TSharedPtr<FJsonObject> NodeObj = MakeShareable(new FJsonObject());
        NodeObj->SetStringField(TEXT("node_id"), BTNode->NodeGuid.ToString());
        NodeObj->SetStringField(TEXT("title"),
            BTNode->GetNodeTitle(ENodeTitleType::FullTitle).ToString());
        NodeObj->SetNumberField(TEXT("position_x"), BTNode->NodePosX);
        NodeObj->SetNumberField(TEXT("position_y"), BTNode->NodePosY);

        // Determine category
        FString Category = TEXT("Unknown");
        FString NodeClassName;
        if (BTNode->NodeInstance)
        {
            NodeClassName = BTNode->NodeInstance->GetClass()->GetName();
            if (BTNode->NodeInstance->IsA<UBTCompositeNode>())
                Category = TEXT("Composite");
            else if (BTNode->NodeInstance->IsA<UBTTaskNode>())
                Category = TEXT("Task");
            else if (BTNode->NodeInstance->IsA<UBTDecorator>())
                Category = TEXT("Decorator");
            else if (BTNode->NodeInstance->IsA<UBTService>())
                Category = TEXT("Service");
        }
        else if (BTNode->IsA<UBehaviorTreeGraphNode_Composite>())
        {
            Category = TEXT("Composite");
        }
        else if (BTNode->IsA<UBehaviorTreeGraphNode_Task>())
        {
            Category = TEXT("Task");
        }

        // Check for Root node
        if (BTNode->GetClass()->GetName().Contains(TEXT("Root")))
        {
            Category = TEXT("Root");
        }

        NodeObj->SetStringField(TEXT("node_category"), Category);
        NodeObj->SetStringField(TEXT("node_class"), NodeClassName);

        // Pins
        TArray<TSharedPtr<FJsonValue>> PinsArr;
        for (UEdGraphPin* Pin : BTNode->Pins)
        {
            if (!Pin) continue;
            TSharedPtr<FJsonObject> PinObj = MakeShareable(new FJsonObject());
            PinObj->SetStringField(TEXT("pin_id"), Pin->PinId.ToString());
            PinObj->SetStringField(TEXT("pin_name"), Pin->PinName.ToString());
            PinObj->SetStringField(TEXT("direction"),
                Pin->Direction == EGPD_Input ? TEXT("input") : TEXT("output"));
            PinObj->SetNumberField(TEXT("num_connections"), Pin->LinkedTo.Num());

            // Connected node IDs
            TArray<TSharedPtr<FJsonValue>> ConnArr;
            for (UEdGraphPin* Linked : Pin->LinkedTo)
            {
                if (Linked && Linked->GetOwningNode())
                {
                    ConnArr.Add(MakeShareable(new FJsonValueString(
                        Linked->GetOwningNode()->NodeGuid.ToString())));
                }
            }
            PinObj->SetArrayField(TEXT("connected_to"), ConnArr);
            PinsArr.Add(MakeShareable(new FJsonValueObject(PinObj)));
        }
        NodeObj->SetArrayField(TEXT("pins"), PinsArr);

        // Sub-nodes (decorators + services) attached to this node
        UAIGraphNode* AINode = Cast<UAIGraphNode>(BTNode);
        if (AINode && AINode->SubNodes.Num() > 0)
        {
            TArray<TSharedPtr<FJsonValue>> SubArr;
            for (UAIGraphNode* SubNode : AINode->SubNodes)
            {
                if (!SubNode) continue;
                TSharedPtr<FJsonObject> SubObj = MakeShareable(new FJsonObject());
                SubObj->SetStringField(TEXT("node_id"), SubNode->NodeGuid.ToString());
                SubObj->SetStringField(TEXT("title"),
                    SubNode->GetNodeTitle(ENodeTitleType::FullTitle).ToString());

                FString SubCategory = TEXT("Unknown");
                FString SubClassName;
                if (SubNode->NodeInstance)
                {
                    SubClassName = SubNode->NodeInstance->GetClass()->GetName();
                    if (SubNode->NodeInstance->IsA<UBTDecorator>())
                        SubCategory = TEXT("Decorator");
                    else if (SubNode->NodeInstance->IsA<UBTService>())
                        SubCategory = TEXT("Service");
                }
                SubObj->SetStringField(TEXT("node_category"), SubCategory);
                SubObj->SetStringField(TEXT("node_class"), SubClassName);
                SubArr.Add(MakeShareable(new FJsonValueObject(SubObj)));
            }
            NodeObj->SetArrayField(TEXT("sub_nodes"), SubArr);
        }

        NodesArr.Add(MakeShareable(new FJsonValueObject(NodeObj)));
    }

    TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
    Data->SetArrayField(TEXT("nodes"), NodesArr);
    Data->SetNumberField(TEXT("count"), NodesArr.Num());
    Data->SetStringField(TEXT("tree_path"), BT->GetPathName());
    return MakeSuccess(Data);
}

// ─────────────────────────────────────────────────────────────────────────────
// ai.update_bt_node
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FNexusAIHandler::HandleUpdateBTNode(
    const TSharedPtr<FJsonObject>& Params)
{
    FString TreePath = GetStringParam(Params, TEXT("tree_path"));
    FString NodeId = GetStringParam(Params, TEXT("node_id"));

    if (TreePath.IsEmpty() || NodeId.IsEmpty())
    {
        return MakeError(TEXT("MISSING_PARAM"),
            TEXT("tree_path and node_id are required"));
    }

    UBehaviorTree* BT = LoadObject<UBehaviorTree>(nullptr, *TreePath);
    if (!BT)
    {
        return MakeError(TEXT("NOT_FOUND"),
            FString::Printf(TEXT("Behavior Tree not found at '%s'"), *TreePath));
    }

    UBehaviorTreeGraph* BTGraph = Cast<UBehaviorTreeGraph>(BT->BTGraph);
    if (!BTGraph)
    {
        return MakeError(TEXT("NO_GRAPH"),
            TEXT("Behavior Tree has no graph."));
    }

    // Find the node
    UBehaviorTreeGraphNode* TargetNode = nullptr;
    for (UEdGraphNode* Node : BTGraph->Nodes)
    {
        if (Node && Node->NodeGuid.ToString() == NodeId)
        {
            TargetNode = Cast<UBehaviorTreeGraphNode>(Node);
            break;
        }
    }

    // Also search subnodes
    if (!TargetNode)
    {
        for (UEdGraphNode* Node : BTGraph->Nodes)
        {
            UAIGraphNode* AINode = Cast<UAIGraphNode>(Node);
            if (!AINode) continue;
            for (UAIGraphNode* Sub : AINode->SubNodes)
            {
                if (Sub && Sub->NodeGuid.ToString() == NodeId)
                {
                    TargetNode = Cast<UBehaviorTreeGraphNode>(Sub);
                    break;
                }
            }
            if (TargetNode) break;
        }
    }

    if (!TargetNode)
    {
        return MakeError(TEXT("NOT_FOUND"),
            FString::Printf(TEXT("Node '%s' not found in behavior tree"), *NodeId));
    }

    // Update position if provided
    double PosX, PosY;
    if (Params->TryGetNumberField(TEXT("position_x"), PosX))
    {
        TargetNode->NodePosX = static_cast<int32>(PosX);
    }
    if (Params->TryGetNumberField(TEXT("position_y"), PosY))
    {
        TargetNode->NodePosY = static_cast<int32>(PosY);
    }

    // Update properties on the runtime node instance via reflection
    const TSharedPtr<FJsonObject>* PropsPtr = nullptr;
    if (Params->TryGetObjectField(TEXT("properties"), PropsPtr) && PropsPtr)
    {
        UObject* NodeInstance = TargetNode->NodeInstance;
        if (NodeInstance)
        {
            NodeInstance->PreEditChange(nullptr);

            for (auto& Pair : (*PropsPtr)->Values)
            {
                FProperty* Prop = NodeInstance->GetClass()->FindPropertyByName(FName(*Pair.Key));
                if (!Prop) continue;

                if (FBoolProperty* BoolProp = CastField<FBoolProperty>(Prop))
                {
                    bool bVal = false;
                    if (Pair.Value->TryGetBool(bVal))
                    {
                        BoolProp->SetPropertyValue_InContainer(NodeInstance, bVal);
                    }
                }
                else if (FFloatProperty* FloatProp = CastField<FFloatProperty>(Prop))
                {
                    double Val = 0.0;
                    if (Pair.Value->TryGetNumber(Val))
                    {
                        FloatProp->SetPropertyValue_InContainer(NodeInstance, static_cast<float>(Val));
                    }
                }
                else if (FDoubleProperty* DoubleProp = CastField<FDoubleProperty>(Prop))
                {
                    double Val = 0.0;
                    if (Pair.Value->TryGetNumber(Val))
                    {
                        DoubleProp->SetPropertyValue_InContainer(NodeInstance, Val);
                    }
                }
                else if (FIntProperty* IntProp = CastField<FIntProperty>(Prop))
                {
                    double Val = 0.0;
                    if (Pair.Value->TryGetNumber(Val))
                    {
                        IntProp->SetPropertyValue_InContainer(NodeInstance, static_cast<int32>(Val));
                    }
                }
                else if (FStrProperty* StrProp = CastField<FStrProperty>(Prop))
                {
                    FString Val;
                    if (Pair.Value->TryGetString(Val))
                    {
                        StrProp->SetPropertyValue_InContainer(NodeInstance, Val);
                    }
                }
                else if (FNameProperty* NameProp = CastField<FNameProperty>(Prop))
                {
                    FString Val;
                    if (Pair.Value->TryGetString(Val))
                    {
                        NameProp->SetPropertyValue_InContainer(NodeInstance, FName(*Val));
                    }
                }
            }

            NodeInstance->PostEditChange();
        }
    }

    BTGraph->NotifyGraphChanged();
    BT->MarkPackageDirty();

    TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
    Data->SetStringField(TEXT("node_id"), NodeId);
    Data->SetStringField(TEXT("title"),
        TargetNode->GetNodeTitle(ENodeTitleType::FullTitle).ToString());
    Data->SetNumberField(TEXT("position_x"), TargetNode->NodePosX);
    Data->SetNumberField(TEXT("position_y"), TargetNode->NodePosY);

    if (TargetNode->NodeInstance)
    {
        Data->SetStringField(TEXT("node_class"),
            TargetNode->NodeInstance->GetClass()->GetName());
    }
    return MakeSuccess(Data);
}

// ─────────────────────────────────────────────────────────────────────────────
// ai.remove_bt_node
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FNexusAIHandler::HandleRemoveBTNode(
    const TSharedPtr<FJsonObject>& Params)
{
    FString TreePath = GetStringParam(Params, TEXT("tree_path"));
    FString NodeId = GetStringParam(Params, TEXT("node_id"));

    if (TreePath.IsEmpty() || NodeId.IsEmpty())
    {
        return MakeError(TEXT("MISSING_PARAM"),
            TEXT("tree_path and node_id are required"));
    }

    UBehaviorTree* BT = LoadObject<UBehaviorTree>(nullptr, *TreePath);
    if (!BT)
    {
        return MakeError(TEXT("NOT_FOUND"),
            FString::Printf(TEXT("Behavior Tree not found at '%s'"), *TreePath));
    }

    UBehaviorTreeGraph* BTGraph = Cast<UBehaviorTreeGraph>(BT->BTGraph);
    if (!BTGraph)
    {
        return MakeError(TEXT("NO_GRAPH"),
            TEXT("Behavior Tree has no graph."));
    }

    // Find the node — check main nodes first
    UBehaviorTreeGraphNode* TargetNode = nullptr;
    bool bIsSubNode = false;
    UAIGraphNode* SubNodeParent = nullptr;

    for (UEdGraphNode* Node : BTGraph->Nodes)
    {
        if (Node && Node->NodeGuid.ToString() == NodeId)
        {
            TargetNode = Cast<UBehaviorTreeGraphNode>(Node);
            break;
        }

        // Also check subnodes
        UAIGraphNode* AINode = Cast<UAIGraphNode>(Node);
        if (AINode)
        {
            for (UAIGraphNode* Sub : AINode->SubNodes)
            {
                if (Sub && Sub->NodeGuid.ToString() == NodeId)
                {
                    TargetNode = Cast<UBehaviorTreeGraphNode>(Sub);
                    bIsSubNode = true;
                    SubNodeParent = AINode;
                    break;
                }
            }
        }
        if (TargetNode) break;
    }

    if (!TargetNode)
    {
        return MakeError(TEXT("NOT_FOUND"),
            FString::Printf(TEXT("Node '%s' not found in behavior tree"), *NodeId));
    }

    // Don't allow removing the root node
    if (TargetNode->GetClass()->GetName().Contains(TEXT("Root")))
    {
        return MakeError(TEXT("CANNOT_REMOVE"),
            TEXT("Cannot remove the root node of a Behavior Tree"));
    }

    FString RemovedTitle = TargetNode->GetNodeTitle(ENodeTitleType::FullTitle).ToString();

    if (bIsSubNode && SubNodeParent)
    {
        // Remove subnode (decorator/service) from parent
        SubNodeParent->RemoveSubNode(TargetNode);
    }
    else
    {
        // Break all pin connections
        for (UEdGraphPin* Pin : TargetNode->Pins)
        {
            if (Pin)
            {
                Pin->BreakAllPinLinks();
            }
        }
        BTGraph->RemoveNode(TargetNode);
    }

    BTGraph->NotifyGraphChanged();
    BT->MarkPackageDirty();

    TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
    Data->SetStringField(TEXT("removed_node_id"), NodeId);
    Data->SetStringField(TEXT("removed_title"), RemovedTitle);
    Data->SetBoolField(TEXT("was_sub_node"), bIsSubNode);
    Data->SetNumberField(TEXT("remaining_nodes"), BTGraph->Nodes.Num());
    return MakeSuccess(Data);
}

// ─────────────────────────────────────────────────────────────────────────────
// ai.connect_bt_nodes
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FNexusAIHandler::HandleConnectBTNodes(
    const TSharedPtr<FJsonObject>& Params)
{
    FString TreePath = GetStringParam(Params, TEXT("tree_path"));
    FString ParentNodeId = GetStringParam(Params, TEXT("parent_node_id"));
    FString ChildNodeId = GetStringParam(Params, TEXT("child_node_id"));

    if (TreePath.IsEmpty() || ParentNodeId.IsEmpty() || ChildNodeId.IsEmpty())
    {
        return MakeError(TEXT("MISSING_PARAM"),
            TEXT("tree_path, parent_node_id, and child_node_id are required"));
    }

    UBehaviorTree* BT = LoadObject<UBehaviorTree>(nullptr, *TreePath);
    if (!BT)
    {
        return MakeError(TEXT("NOT_FOUND"),
            FString::Printf(TEXT("Behavior Tree not found at '%s'"), *TreePath));
    }

    UBehaviorTreeGraph* BTGraph = Cast<UBehaviorTreeGraph>(BT->BTGraph);
    if (!BTGraph)
    {
        return MakeError(TEXT("NO_GRAPH"),
            TEXT("Behavior Tree has no graph."));
    }

    // Find parent and child nodes
    UEdGraphNode* ParentNode = nullptr;
    UEdGraphNode* ChildNode = nullptr;
    for (UEdGraphNode* Node : BTGraph->Nodes)
    {
        if (Node && Node->NodeGuid.ToString() == ParentNodeId)
        {
            ParentNode = Node;
        }
        if (Node && Node->NodeGuid.ToString() == ChildNodeId)
        {
            ChildNode = Node;
        }
        if (ParentNode && ChildNode) break;
    }

    if (!ParentNode)
    {
        return MakeError(TEXT("NOT_FOUND"),
            FString::Printf(TEXT("Parent node '%s' not found"), *ParentNodeId));
    }
    if (!ChildNode)
    {
        return MakeError(TEXT("NOT_FOUND"),
            FString::Printf(TEXT("Child node '%s' not found"), *ChildNodeId));
    }

    // Find output pin on parent
    UEdGraphPin* ParentOutput = nullptr;
    for (UEdGraphPin* Pin : ParentNode->Pins)
    {
        if (Pin && Pin->Direction == EGPD_Output)
        {
            ParentOutput = Pin;
            break;
        }
    }

    // Find input pin on child
    UEdGraphPin* ChildInput = nullptr;
    for (UEdGraphPin* Pin : ChildNode->Pins)
    {
        if (Pin && Pin->Direction == EGPD_Input)
        {
            ChildInput = Pin;
            break;
        }
    }

    if (!ParentOutput)
    {
        return MakeError(TEXT("NO_OUTPUT_PIN"),
            TEXT("Parent node has no output pin (not a composite or root node?)"));
    }
    if (!ChildInput)
    {
        return MakeError(TEXT("NO_INPUT_PIN"),
            TEXT("Child node has no input pin"));
    }

    // Use the graph schema to create the connection
    const UEdGraphSchema* Schema = BTGraph->GetSchema();
    if (!Schema)
    {
        return MakeError(TEXT("SCHEMA_ERROR"),
            TEXT("Could not get behavior tree graph schema"));
    }

    bool bConnected = Schema->TryCreateConnection(ParentOutput, ChildInput);
    if (!bConnected)
    {
        return MakeError(TEXT("CONNECTION_FAILED"),
            TEXT("Failed to connect nodes. Check that parent is a composite/root and child is a composite/task."));
    }

    BTGraph->NotifyGraphChanged();
    BT->MarkPackageDirty();

    TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
    Data->SetStringField(TEXT("parent_node_id"), ParentNodeId);
    Data->SetStringField(TEXT("child_node_id"), ChildNodeId);
    Data->SetStringField(TEXT("parent_title"),
        ParentNode->GetNodeTitle(ENodeTitleType::FullTitle).ToString());
    Data->SetStringField(TEXT("child_title"),
        ChildNode->GetNodeTitle(ENodeTitleType::FullTitle).ToString());
    Data->SetBoolField(TEXT("connected"), true);
    return MakeSuccess(Data);
}

// ─────────────────────────────────────────────────────────────────────────────
// ai.disconnect_bt_node
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FNexusAIHandler::HandleDisconnectBTNode(
    const TSharedPtr<FJsonObject>& Params)
{
    FString TreePath = GetStringParam(Params, TEXT("tree_path"));
    FString NodeId = GetStringParam(Params, TEXT("node_id"));

    if (TreePath.IsEmpty() || NodeId.IsEmpty())
    {
        return MakeError(TEXT("MISSING_PARAM"),
            TEXT("tree_path and node_id are required"));
    }

    UBehaviorTree* BT = LoadObject<UBehaviorTree>(nullptr, *TreePath);
    if (!BT)
    {
        return MakeError(TEXT("NOT_FOUND"),
            FString::Printf(TEXT("Behavior Tree not found at '%s'"), *TreePath));
    }

    UBehaviorTreeGraph* BTGraph = Cast<UBehaviorTreeGraph>(BT->BTGraph);
    if (!BTGraph)
    {
        return MakeError(TEXT("NO_GRAPH"),
            TEXT("Behavior Tree has no graph."));
    }

    // Find the node
    UEdGraphNode* TargetNode = nullptr;
    for (UEdGraphNode* Node : BTGraph->Nodes)
    {
        if (Node && Node->NodeGuid.ToString() == NodeId)
        {
            TargetNode = Node;
            break;
        }
    }

    if (!TargetNode)
    {
        return MakeError(TEXT("NOT_FOUND"),
            FString::Printf(TEXT("Node '%s' not found"), *NodeId));
    }

    // Break the input pin connection (disconnect from parent)
    int32 BrokenCount = 0;
    for (UEdGraphPin* Pin : TargetNode->Pins)
    {
        if (Pin && Pin->Direction == EGPD_Input && Pin->LinkedTo.Num() > 0)
        {
            BrokenCount += Pin->LinkedTo.Num();
            Pin->BreakAllPinLinks();
        }
    }

    BTGraph->NotifyGraphChanged();
    BT->MarkPackageDirty();

    TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
    Data->SetStringField(TEXT("node_id"), NodeId);
    Data->SetStringField(TEXT("title"),
        TargetNode->GetNodeTitle(ENodeTitleType::FullTitle).ToString());
    Data->SetNumberField(TEXT("connections_broken"), BrokenCount);
    return MakeSuccess(Data);
}

// ─────────────────────────────────────────────────────────────────────────────
// ai.add_eqs_generator
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FNexusAIHandler::HandleAddEQSGenerator(
    const TSharedPtr<FJsonObject>& Params)
{
    FString QueryPath = GetStringParam(Params, TEXT("query_path"));
    FString GeneratorType = GetStringParam(Params, TEXT("generator_type"));

    if (QueryPath.IsEmpty() || GeneratorType.IsEmpty())
    {
        return MakeError(TEXT("MISSING_PARAM"),
            TEXT("query_path and generator_type are required"));
    }

    UEnvQuery* Query = LoadObject<UEnvQuery>(nullptr, *QueryPath);
    if (!Query)
    {
        return MakeError(TEXT("NOT_FOUND"),
            FString::Printf(TEXT("EQS Query not found at '%s'"), *QueryPath));
    }

    // Map generator type to class
    static TMap<FString, UClass*> GeneratorClassMap;
    if (GeneratorClassMap.Num() == 0)
    {
        GeneratorClassMap.Add(TEXT("SimpleGrid"), UEnvQueryGenerator_SimpleGrid::StaticClass());
        GeneratorClassMap.Add(TEXT("OnCircle"), UEnvQueryGenerator_OnCircle::StaticClass());
        GeneratorClassMap.Add(TEXT("Donut"), UEnvQueryGenerator_Donut::StaticClass());
        GeneratorClassMap.Add(TEXT("ActorsOfClass"), UEnvQueryGenerator_ActorsOfClass::StaticClass());
    }

    UClass* GenClass = nullptr;
    if (UClass** Found = GeneratorClassMap.Find(GeneratorType))
    {
        GenClass = *Found;
    }
    else
    {
        // Try dynamic lookup
        FString ClassPath = FString::Printf(TEXT("/Script/AIModule.EnvQueryGenerator_%s"), *GeneratorType);
        GenClass = LoadClass<UEnvQueryGenerator>(nullptr, *ClassPath);
    }

    if (!GenClass)
    {
        return MakeError(TEXT("UNKNOWN_TYPE"),
            FString::Printf(TEXT("Unknown generator type: '%s'. Supported: SimpleGrid, OnCircle, Donut, ActorsOfClass"), *GeneratorType));
    }

    Query->PreEditChange(nullptr);

    // Create the option (generator wrapper) and generator
    UEnvQueryOption* Option = NewObject<UEnvQueryOption>(Query);
    if (!Option)
    {
        return MakeError(TEXT("CREATE_FAILED"), TEXT("Failed to create EQS Option"));
    }

    UEnvQueryGenerator* Generator = NewObject<UEnvQueryGenerator>(Option, GenClass);
    if (!Generator)
    {
        return MakeError(TEXT("CREATE_FAILED"), TEXT("Failed to create EQS Generator"));
    }
    Option->Generator = Generator;

    // Apply properties via reflection if provided
    const TSharedPtr<FJsonObject>* PropsPtr = nullptr;
    if (Params->TryGetObjectField(TEXT("properties"), PropsPtr) && PropsPtr)
    {
        for (auto& Pair : (*PropsPtr)->Values)
        {
            FProperty* Prop = Generator->GetClass()->FindPropertyByName(FName(*Pair.Key));
            if (!Prop) continue;

            if (FFloatProperty* FloatProp = CastField<FFloatProperty>(Prop))
            {
                double Val = 0.0;
                if (Pair.Value->TryGetNumber(Val))
                {
                    FloatProp->SetPropertyValue_InContainer(Generator, static_cast<float>(Val));
                }
            }
            else if (FDoubleProperty* DoubleProp = CastField<FDoubleProperty>(Prop))
            {
                double Val = 0.0;
                if (Pair.Value->TryGetNumber(Val))
                {
                    DoubleProp->SetPropertyValue_InContainer(Generator, Val);
                }
            }
            else if (FIntProperty* IntProp = CastField<FIntProperty>(Prop))
            {
                double Val = 0.0;
                if (Pair.Value->TryGetNumber(Val))
                {
                    IntProp->SetPropertyValue_InContainer(Generator, static_cast<int32>(Val));
                }
            }
            else if (FBoolProperty* BoolProp = CastField<FBoolProperty>(Prop))
            {
                bool bVal = false;
                if (Pair.Value->TryGetBool(bVal))
                {
                    BoolProp->SetPropertyValue_InContainer(Generator, bVal);
                }
            }
        }
    }

    // Add option to query
    Query->GetOptionsMutable().Add(Option);

    Query->PostEditChange();
    Query->MarkPackageDirty();

    TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
    Data->SetStringField(TEXT("generator_id"), Generator->GetName());
    Data->SetStringField(TEXT("generator_type"), GeneratorType);
    Data->SetStringField(TEXT("generator_class"), Generator->GetClass()->GetName());
    Data->SetNumberField(TEXT("option_index"), Query->GetOptions().Num() - 1);
    Data->SetStringField(TEXT("query_path"), Query->GetPathName());
    return MakeSuccess(Data);
}

// ─────────────────────────────────────────────────────────────────────────────
// ai.add_eqs_test
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FNexusAIHandler::HandleAddEQSTest(
    const TSharedPtr<FJsonObject>& Params)
{
    FString QueryPath = GetStringParam(Params, TEXT("query_path"));
    FString TestType = GetStringParam(Params, TEXT("test_type"));
    FString GeneratorId = GetStringParam(Params, TEXT("generator_id"));
    int32 OptionIndex = static_cast<int32>(GetNumberParam(Params, TEXT("option_index"), -1.0));

    if (QueryPath.IsEmpty() || TestType.IsEmpty())
    {
        return MakeError(TEXT("MISSING_PARAM"),
            TEXT("query_path and test_type are required"));
    }

    UEnvQuery* Query = LoadObject<UEnvQuery>(nullptr, *QueryPath);
    if (!Query)
    {
        return MakeError(TEXT("NOT_FOUND"),
            FString::Printf(TEXT("EQS Query not found at '%s'"), *QueryPath));
    }

    if (Query->GetOptions().Num() == 0)
    {
        return MakeError(TEXT("NO_GENERATORS"),
            TEXT("Query has no generator options. Add a generator first."));
    }

    // Map test type to class
    static TMap<FString, UClass*> TestClassMap;
    if (TestClassMap.Num() == 0)
    {
        TestClassMap.Add(TEXT("Distance"), UEnvQueryTest_Distance::StaticClass());
        TestClassMap.Add(TEXT("Trace"), UEnvQueryTest_Trace::StaticClass());
        TestClassMap.Add(TEXT("Pathfinding"), UEnvQueryTest_Pathfinding::StaticClass());
        TestClassMap.Add(TEXT("Dot"), UEnvQueryTest_Dot::StaticClass());
    }

    UClass* TestClass = nullptr;
    if (UClass** Found = TestClassMap.Find(TestType))
    {
        TestClass = *Found;
    }
    else
    {
        // Try dynamic lookup
        FString ClassPath = FString::Printf(TEXT("/Script/AIModule.EnvQueryTest_%s"), *TestType);
        TestClass = LoadClass<UEnvQueryTest>(nullptr, *ClassPath);
    }

    if (!TestClass)
    {
        return MakeError(TEXT("UNKNOWN_TYPE"),
            FString::Printf(TEXT("Unknown test type: '%s'. Supported: Distance, Trace, Pathfinding, Dot"), *TestType));
    }

    // Find the target option (generator) to attach the test to
    UEnvQueryOption* TargetOption = nullptr;
    int32 ResolvedIndex = -1;

    if (OptionIndex >= 0 && OptionIndex < Query->GetOptions().Num())
    {
        TargetOption = Query->GetOptions()[OptionIndex];
        ResolvedIndex = OptionIndex;
    }
    else if (!GeneratorId.IsEmpty())
    {
        // Search by generator name
        for (int32 i = 0; i < Query->GetOptions().Num(); ++i)
        {
            UEnvQueryOption* Opt = Query->GetOptions()[i];
            if (Opt && Opt->Generator && Opt->Generator->GetName() == GeneratorId)
            {
                TargetOption = Opt;
                ResolvedIndex = i;
                break;
            }
        }
    }
    else
    {
        // Default to last option
        TargetOption = Query->GetOptions().Last();
        ResolvedIndex = Query->GetOptions().Num() - 1;
    }

    if (!TargetOption)
    {
        return MakeError(TEXT("NOT_FOUND"),
            TEXT("Could not find target generator option to attach test to"));
    }

    Query->PreEditChange(nullptr);

    // Create the test
    UEnvQueryTest* NewTest = NewObject<UEnvQueryTest>(TargetOption, TestClass);
    if (!NewTest)
    {
        return MakeError(TEXT("CREATE_FAILED"), TEXT("Failed to create EQS Test"));
    }

    // Apply properties via reflection
    const TSharedPtr<FJsonObject>* PropsPtr = nullptr;
    if (Params->TryGetObjectField(TEXT("properties"), PropsPtr) && PropsPtr)
    {
        for (auto& Pair : (*PropsPtr)->Values)
        {
            FProperty* Prop = NewTest->GetClass()->FindPropertyByName(FName(*Pair.Key));
            if (!Prop) continue;

            if (FFloatProperty* FloatProp = CastField<FFloatProperty>(Prop))
            {
                double Val = 0.0;
                if (Pair.Value->TryGetNumber(Val))
                {
                    FloatProp->SetPropertyValue_InContainer(NewTest, static_cast<float>(Val));
                }
            }
            else if (FBoolProperty* BoolProp = CastField<FBoolProperty>(Prop))
            {
                bool bVal = false;
                if (Pair.Value->TryGetBool(bVal))
                {
                    BoolProp->SetPropertyValue_InContainer(NewTest, bVal);
                }
            }
        }
    }

    // Add test to the option
    TargetOption->Tests.Add(NewTest);

    Query->PostEditChange();
    Query->MarkPackageDirty();

    TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
    Data->SetStringField(TEXT("test_id"), NewTest->GetName());
    Data->SetStringField(TEXT("test_type"), TestType);
    Data->SetStringField(TEXT("test_class"), NewTest->GetClass()->GetName());
    Data->SetNumberField(TEXT("option_index"), ResolvedIndex);
    if (TargetOption->Generator)
    {
        Data->SetStringField(TEXT("generator_id"), TargetOption->Generator->GetName());
    }
    Data->SetNumberField(TEXT("test_index"), TargetOption->Tests.Num() - 1);
    Data->SetStringField(TEXT("query_path"), Query->GetPathName());
    return MakeSuccess(Data);
}

// ─────────────────────────────────────────────────────────────────────────────
// ai.get_eqs_nodes
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FNexusAIHandler::HandleGetEQSNodes(
    const TSharedPtr<FJsonObject>& Params)
{
    FString QueryPath = GetStringParam(Params, TEXT("query_path"));
    if (QueryPath.IsEmpty())
    {
        return MakeError(TEXT("MISSING_PARAM"), TEXT("query_path is required"));
    }

    UEnvQuery* Query = LoadObject<UEnvQuery>(nullptr, *QueryPath);
    if (!Query)
    {
        return MakeError(TEXT("NOT_FOUND"),
            FString::Printf(TEXT("EQS Query not found at '%s'"), *QueryPath));
    }

    TArray<TSharedPtr<FJsonValue>> OptionsArr;
    for (int32 i = 0; i < Query->GetOptions().Num(); ++i)
    {
        UEnvQueryOption* Option = Query->GetOptions()[i];
        if (!Option) continue;

        TSharedPtr<FJsonObject> OptObj = MakeShareable(new FJsonObject());
        OptObj->SetNumberField(TEXT("option_index"), i);

        // Generator info
        if (Option->Generator)
        {
            TSharedPtr<FJsonObject> GenObj = MakeShareable(new FJsonObject());
            GenObj->SetStringField(TEXT("generator_id"), Option->Generator->GetName());
            GenObj->SetStringField(TEXT("generator_class"), Option->Generator->GetClass()->GetName());

            // Extract key generator properties via display name
            FString GenType = Option->Generator->GetClass()->GetName();
            GenType.RemoveFromStart(TEXT("EnvQueryGenerator_"));
            GenObj->SetStringField(TEXT("generator_type"), GenType);

            OptObj->SetObjectField(TEXT("generator"), GenObj);
        }

        // Tests info
        TArray<TSharedPtr<FJsonValue>> TestsArr;
        for (int32 j = 0; j < Option->Tests.Num(); ++j)
        {
            UEnvQueryTest* Test = Option->Tests[j];
            if (!Test) continue;

            TSharedPtr<FJsonObject> TestObj = MakeShareable(new FJsonObject());
            TestObj->SetStringField(TEXT("test_id"), Test->GetName());
            TestObj->SetStringField(TEXT("test_class"), Test->GetClass()->GetName());

            FString TestType = Test->GetClass()->GetName();
            TestType.RemoveFromStart(TEXT("EnvQueryTest_"));
            TestObj->SetStringField(TEXT("test_type"), TestType);
            TestObj->SetNumberField(TEXT("test_index"), j);

            TestsArr.Add(MakeShareable(new FJsonValueObject(TestObj)));
        }
        OptObj->SetArrayField(TEXT("tests"), TestsArr);
        OptObj->SetNumberField(TEXT("test_count"), Option->Tests.Num());

        OptionsArr.Add(MakeShareable(new FJsonValueObject(OptObj)));
    }

    TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
    Data->SetArrayField(TEXT("options"), OptionsArr);
    Data->SetNumberField(TEXT("option_count"), Query->GetOptions().Num());
    Data->SetStringField(TEXT("query_path"), Query->GetPathName());
    return MakeSuccess(Data);
}

// ─────────────────────────────────────────────────────────────────────────────
// ai.update_eqs_node
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FNexusAIHandler::HandleUpdateEQSNode(
    const TSharedPtr<FJsonObject>& Params)
{
    FString QueryPath = GetStringParam(Params, TEXT("query_path"));
    FString NodeId = GetStringParam(Params, TEXT("node_id"));

    if (QueryPath.IsEmpty() || NodeId.IsEmpty())
    {
        return MakeError(TEXT("MISSING_PARAM"),
            TEXT("query_path and node_id are required"));
    }

    UEnvQuery* Query = LoadObject<UEnvQuery>(nullptr, *QueryPath);
    if (!Query)
    {
        return MakeError(TEXT("NOT_FOUND"),
            FString::Printf(TEXT("EQS Query not found at '%s'"), *QueryPath));
    }

    // Find the node by name — could be a generator or a test
    UObject* TargetNode = nullptr;
    FString NodeType;

    for (UEnvQueryOption* Option : Query->GetOptions())
    {
        if (!Option) continue;

        if (Option->Generator && Option->Generator->GetName() == NodeId)
        {
            TargetNode = Option->Generator;
            NodeType = TEXT("Generator");
            break;
        }

        for (UEnvQueryTest* Test : Option->Tests)
        {
            if (Test && Test->GetName() == NodeId)
            {
                TargetNode = Test;
                NodeType = TEXT("Test");
                break;
            }
        }
        if (TargetNode) break;
    }

    if (!TargetNode)
    {
        return MakeError(TEXT("NOT_FOUND"),
            FString::Printf(TEXT("EQS node '%s' not found in query"), *NodeId));
    }

    // Apply properties via reflection
    const TSharedPtr<FJsonObject>* PropsPtr = nullptr;
    if (Params->TryGetObjectField(TEXT("properties"), PropsPtr) && PropsPtr)
    {
        Query->PreEditChange(nullptr);
        TargetNode->PreEditChange(nullptr);

        for (auto& Pair : (*PropsPtr)->Values)
        {
            FProperty* Prop = TargetNode->GetClass()->FindPropertyByName(FName(*Pair.Key));
            if (!Prop) continue;

            if (FFloatProperty* FloatProp = CastField<FFloatProperty>(Prop))
            {
                double Val = 0.0;
                if (Pair.Value->TryGetNumber(Val))
                {
                    FloatProp->SetPropertyValue_InContainer(TargetNode, static_cast<float>(Val));
                }
            }
            else if (FDoubleProperty* DoubleProp = CastField<FDoubleProperty>(Prop))
            {
                double Val = 0.0;
                if (Pair.Value->TryGetNumber(Val))
                {
                    DoubleProp->SetPropertyValue_InContainer(TargetNode, Val);
                }
            }
            else if (FIntProperty* IntProp = CastField<FIntProperty>(Prop))
            {
                double Val = 0.0;
                if (Pair.Value->TryGetNumber(Val))
                {
                    IntProp->SetPropertyValue_InContainer(TargetNode, static_cast<int32>(Val));
                }
            }
            else if (FBoolProperty* BoolProp = CastField<FBoolProperty>(Prop))
            {
                bool bVal = false;
                if (Pair.Value->TryGetBool(bVal))
                {
                    BoolProp->SetPropertyValue_InContainer(TargetNode, bVal);
                }
            }
            else if (FStrProperty* StrProp = CastField<FStrProperty>(Prop))
            {
                FString Val;
                if (Pair.Value->TryGetString(Val))
                {
                    StrProp->SetPropertyValue_InContainer(TargetNode, Val);
                }
            }
            else if (FNameProperty* NameProp = CastField<FNameProperty>(Prop))
            {
                FString Val;
                if (Pair.Value->TryGetString(Val))
                {
                    NameProp->SetPropertyValue_InContainer(TargetNode, FName(*Val));
                }
            }
        }

        TargetNode->PostEditChange();
        Query->PostEditChange();
    }

    Query->MarkPackageDirty();

    TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
    Data->SetStringField(TEXT("node_id"), NodeId);
    Data->SetStringField(TEXT("node_type"), NodeType);
    Data->SetStringField(TEXT("node_class"), TargetNode->GetClass()->GetName());
    Data->SetStringField(TEXT("query_path"), Query->GetPathName());
    return MakeSuccess(Data);
}

// ─────────────────────────────────────────────────────────────────────────────
// ai.remove_eqs_node
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FNexusAIHandler::HandleRemoveEQSNode(
    const TSharedPtr<FJsonObject>& Params)
{
    FString QueryPath = GetStringParam(Params, TEXT("query_path"));
    FString NodeId = GetStringParam(Params, TEXT("node_id"));

    if (QueryPath.IsEmpty() || NodeId.IsEmpty())
    {
        return MakeError(TEXT("MISSING_PARAM"),
            TEXT("query_path and node_id are required"));
    }

    UEnvQuery* Query = LoadObject<UEnvQuery>(nullptr, *QueryPath);
    if (!Query)
    {
        return MakeError(TEXT("NOT_FOUND"),
            FString::Printf(TEXT("EQS Query not found at '%s'"), *QueryPath));
    }

    Query->PreEditChange(nullptr);

    FString RemovedType;
    FString RemovedClass;
    bool bRemoved = false;

    // Search generators — if a generator matches, remove the entire option
    for (int32 i = Query->GetOptions().Num() - 1; i >= 0; --i)
    {
        UEnvQueryOption* Option = Query->GetOptions()[i];
        if (!Option) continue;

        if (Option->Generator && Option->Generator->GetName() == NodeId)
        {
            RemovedType = TEXT("Generator");
            RemovedClass = Option->Generator->GetClass()->GetName();
            Query->GetOptionsMutable().RemoveAt(i);
            bRemoved = true;
            break;
        }

        // Search tests within this option
        for (int32 j = Option->Tests.Num() - 1; j >= 0; --j)
        {
            UEnvQueryTest* Test = Option->Tests[j];
            if (Test && Test->GetName() == NodeId)
            {
                RemovedType = TEXT("Test");
                RemovedClass = Test->GetClass()->GetName();
                Option->Tests.RemoveAt(j);
                bRemoved = true;
                break;
            }
        }
        if (bRemoved) break;
    }

    Query->PostEditChange();
    Query->MarkPackageDirty();

    if (!bRemoved)
    {
        return MakeError(TEXT("NOT_FOUND"),
            FString::Printf(TEXT("EQS node '%s' not found in query"), *NodeId));
    }

    TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
    Data->SetStringField(TEXT("removed_node_id"), NodeId);
    Data->SetStringField(TEXT("removed_type"), RemovedType);
    Data->SetStringField(TEXT("removed_class"), RemovedClass);
    Data->SetNumberField(TEXT("remaining_options"), Query->GetOptions().Num());
    Data->SetStringField(TEXT("query_path"), Query->GetPathName());
    return MakeSuccess(Data);
}

// ─────────────────────────────────────────────────────────────────────────────
// Actor / controller resolution
// ─────────────────────────────────────────────────────────────────────────────

AActor* FNexusAIHandler::FindActorByPath(const FString& Path)
{
    if (Path.IsEmpty()) return nullptr;
    UObject* Obj = StaticFindObject(AActor::StaticClass(), nullptr, *Path);
    return Cast<AActor>(Obj);
}

AActor* FNexusAIHandler::FindActorByLabel(const FString& Label)
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

AAIController* FNexusAIHandler::ResolveAIController(
    const TSharedPtr<FJsonObject>& Params,
    const FString& PathKey,
    FString& OutError)
{
    FString ControllerPath = GetStringParam(Params, PathKey);
    if (ControllerPath.IsEmpty())
    {
        OutError = FString::Printf(TEXT("'%s' parameter is required"), *PathKey);
        return nullptr;
    }

    // Try to find actor by path, then by label
    AActor* Actor = FindActorByPath(ControllerPath);
    if (!Actor) Actor = FindActorByLabel(ControllerPath);
    if (!Actor)
    {
        OutError = FString::Printf(TEXT("No actor found for '%s'"), *ControllerPath);
        return nullptr;
    }

    // If it's already an AI controller, return it
    AAIController* AIC = Cast<AAIController>(Actor);
    if (AIC) return AIC;

    // If it's a pawn, get its AI controller
    APawn* Pawn = Cast<APawn>(Actor);
    if (Pawn)
    {
        AIC = Cast<AAIController>(Pawn->GetController());
        if (AIC) return AIC;
        OutError = FString::Printf(TEXT("Pawn '%s' has no AI Controller"), *ControllerPath);
        return nullptr;
    }

    OutError = FString::Printf(TEXT("Actor '%s' is neither an AIController nor a Pawn"), *ControllerPath);
    return nullptr;
}

// ─────────────────────────────────────────────────────────────────────────────
// ai.create_behavior_tree
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FNexusAIHandler::HandleCreateBehaviorTree(
    const TSharedPtr<FJsonObject>& Params)
{
    FString TreeName = GetStringParam(Params, TEXT("tree_name"));
    FString DestFolder = GetStringParam(Params, TEXT("destination_folder"));
    if (TreeName.IsEmpty() || DestFolder.IsEmpty())
    {
        return MakeError(TEXT("MISSING_PARAM"),
            TEXT("tree_name and destination_folder are required"));
    }

    FString PackagePath = DestFolder / TreeName;
    UPackage* Package = CreatePackage(*PackagePath);
    if (!Package)
    {
        return MakeError(TEXT("CREATE_FAILED"),
            FString::Printf(TEXT("Failed to create package at '%s'"), *PackagePath));
    }

    UBehaviorTree* BT = NewObject<UBehaviorTree>(Package, FName(*TreeName),
        RF_Public | RF_Standalone | RF_Transactional);
    if (!BT)
    {
        return MakeError(TEXT("CREATE_FAILED"), TEXT("Failed to create BehaviorTree object"));
    }

    FString Description = GetStringParam(Params, TEXT("description"));
    // BehaviorTree doesn't have a built-in description field, but we store the metadata

    FAssetRegistryModule::AssetCreated(BT);
    BT->MarkPackageDirty();

    TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
    Data->SetStringField(TEXT("asset_path"), BT->GetPathName());
    Data->SetStringField(TEXT("tree_name"), TreeName);
    Data->SetStringField(TEXT("destination_folder"), DestFolder);
    return MakeSuccess(Data);
}

// ─────────────────────────────────────────────────────────────────────────────
// ai.create_blackboard
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FNexusAIHandler::HandleCreateBlackboard(
    const TSharedPtr<FJsonObject>& Params)
{
    FString BBName = GetStringParam(Params, TEXT("blackboard_name"));
    FString DestFolder = GetStringParam(Params, TEXT("destination_folder"));
    if (BBName.IsEmpty() || DestFolder.IsEmpty())
    {
        return MakeError(TEXT("MISSING_PARAM"),
            TEXT("blackboard_name and destination_folder are required"));
    }

    FString PackagePath = DestFolder / BBName;
    UPackage* Package = CreatePackage(*PackagePath);
    if (!Package)
    {
        return MakeError(TEXT("CREATE_FAILED"),
            FString::Printf(TEXT("Failed to create package at '%s'"), *PackagePath));
    }

    UBlackboardData* BB = NewObject<UBlackboardData>(Package, FName(*BBName),
        RF_Public | RF_Standalone | RF_Transactional);
    if (!BB)
    {
        return MakeError(TEXT("CREATE_FAILED"), TEXT("Failed to create BlackboardData object"));
    }

    // Set parent blackboard if specified
    FString ParentPath = GetStringParam(Params, TEXT("parent_blackboard_path"));
    if (!ParentPath.IsEmpty())
    {
        UBlackboardData* ParentBB = LoadObject<UBlackboardData>(nullptr, *ParentPath);
        if (ParentBB)
        {
            BB->Parent = ParentBB;
        }
    }

    // Parse keys in "name:type,name:type" format
    FString KeysStr = GetStringParam(Params, TEXT("keys"));
    TArray<FString> AddedKeys;
    if (!KeysStr.IsEmpty())
    {
        TArray<FString> KeyDefs;
        KeysStr.ParseIntoArray(KeyDefs, TEXT(","), true);

        for (const FString& KeyDef : KeyDefs)
        {
            FString KeyName, KeyType;
            if (!KeyDef.Split(TEXT(":"), &KeyName, &KeyType))
            {
                continue;
            }
            KeyName = KeyName.TrimStartAndEnd();
            KeyType = KeyType.TrimStartAndEnd();

            FBlackboardEntry NewEntry;
            NewEntry.EntryName = FName(*KeyName);

            if (KeyType == TEXT("Bool"))
                NewEntry.KeyType = NewObject<UBlackboardKeyType_Bool>(BB);
            else if (KeyType == TEXT("Int"))
                NewEntry.KeyType = NewObject<UBlackboardKeyType_Int>(BB);
            else if (KeyType == TEXT("Float"))
                NewEntry.KeyType = NewObject<UBlackboardKeyType_Float>(BB);
            else if (KeyType == TEXT("String"))
                NewEntry.KeyType = NewObject<UBlackboardKeyType_String>(BB);
            else if (KeyType == TEXT("Name"))
                NewEntry.KeyType = NewObject<UBlackboardKeyType_Name>(BB);
            else if (KeyType == TEXT("Vector"))
                NewEntry.KeyType = NewObject<UBlackboardKeyType_Vector>(BB);
            else if (KeyType == TEXT("Rotator"))
                NewEntry.KeyType = NewObject<UBlackboardKeyType_Rotator>(BB);
            else if (KeyType == TEXT("Object"))
                NewEntry.KeyType = NewObject<UBlackboardKeyType_Object>(BB);
            else if (KeyType == TEXT("Class"))
                NewEntry.KeyType = NewObject<UBlackboardKeyType_Class>(BB);
            else if (KeyType == TEXT("Enum"))
                NewEntry.KeyType = NewObject<UBlackboardKeyType_Enum>(BB);
            else
                NewEntry.KeyType = NewObject<UBlackboardKeyType_String>(BB);

            BB->Keys.Add(NewEntry);
            AddedKeys.Add(KeyName);
        }
    }

    FAssetRegistryModule::AssetCreated(BB);
    BB->MarkPackageDirty();

    TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
    Data->SetStringField(TEXT("asset_path"), BB->GetPathName());
    Data->SetStringField(TEXT("blackboard_name"), BBName);
    Data->SetNumberField(TEXT("key_count"), AddedKeys.Num());

    TArray<TSharedPtr<FJsonValue>> KeyArray;
    for (const FString& K : AddedKeys)
    {
        KeyArray.Add(MakeShareable(new FJsonValueString(K)));
    }
    Data->SetArrayField(TEXT("keys"), KeyArray);
    return MakeSuccess(Data);
}

// ─────────────────────────────────────────────────────────────────────────────
// ai.create_eqs_query
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FNexusAIHandler::HandleCreateEQSQuery(
    const TSharedPtr<FJsonObject>& Params)
{
    FString QueryName = GetStringParam(Params, TEXT("query_name"));
    FString DestFolder = GetStringParam(Params, TEXT("destination_folder"));
    if (QueryName.IsEmpty() || DestFolder.IsEmpty())
    {
        return MakeError(TEXT("MISSING_PARAM"),
            TEXT("query_name and destination_folder are required"));
    }

    FString PackagePath = DestFolder / QueryName;
    UPackage* Package = CreatePackage(*PackagePath);
    if (!Package)
    {
        return MakeError(TEXT("CREATE_FAILED"),
            FString::Printf(TEXT("Failed to create package at '%s'"), *PackagePath));
    }

    UEnvQuery* Query = NewObject<UEnvQuery>(Package, FName(*QueryName),
        RF_Public | RF_Standalone | RF_Transactional);
    if (!Query)
    {
        return MakeError(TEXT("CREATE_FAILED"), TEXT("Failed to create EnvQuery object"));
    }

    FString GeneratorType = GetStringParam(Params, TEXT("generator_type"), TEXT("SimpleGrid"));
    double GeneratorRadius = GetNumberParam(Params, TEXT("generator_radius"), 1000.0);
    double GeneratorSpacing = GetNumberParam(Params, TEXT("generator_spacing"), 100.0);

    // EQS Query creation stores configuration metadata — actual generator/test
    // setup requires UE editor or detailed codegen as generators are complex objects.

    FAssetRegistryModule::AssetCreated(Query);
    Query->MarkPackageDirty();

    TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
    Data->SetStringField(TEXT("asset_path"), Query->GetPathName());
    Data->SetStringField(TEXT("query_name"), QueryName);
    Data->SetStringField(TEXT("generator_type"), GeneratorType);
    Data->SetNumberField(TEXT("generator_radius"), GeneratorRadius);
    Data->SetNumberField(TEXT("generator_spacing"), GeneratorSpacing);
    Data->SetStringField(TEXT("note"),
        TEXT("EQS query asset created. Configure generators and tests in the editor or via codegen."));
    return MakeSuccess(Data);
}

// ─────────────────────────────────────────────────────────────────────────────
// ai.create_state_tree
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FNexusAIHandler::HandleCreateStateTree(
    const TSharedPtr<FJsonObject>& Params)
{
    FString TreeName = GetStringParam(Params, TEXT("state_tree_name"));
    FString DestFolder = GetStringParam(Params, TEXT("destination_folder"));
    if (TreeName.IsEmpty() || DestFolder.IsEmpty())
    {
        return MakeError(TEXT("MISSING_PARAM"),
            TEXT("state_tree_name and destination_folder are required"));
    }

    FString SchemaClass = GetStringParam(Params, TEXT("schema_class"), TEXT("StateTreeComponentSchema"));

    FString PackagePath = DestFolder / TreeName;
    UPackage* Package = CreatePackage(*PackagePath);
    if (!Package)
    {
        return MakeError(TEXT("CREATE_FAILED"),
            FString::Printf(TEXT("Failed to create package at '%s'"), *PackagePath));
    }

    // StateTree is a UStateTree asset — find the class dynamically
    UClass* StateTreeClass = FindObject<UClass>(nullptr, TEXT("/Script/StateTreeModule.StateTree"));
    if (!StateTreeClass)
    {
        // Try alternate path
        StateTreeClass = LoadClass<UObject>(nullptr, TEXT("/Script/StateTreeModule.StateTree"));
    }

    if (!StateTreeClass)
    {
        return MakeError(TEXT("MODULE_NOT_LOADED"),
            TEXT("StateTree module not available. Ensure StateTreeModule is loaded."));
    }

    UObject* StateTree = NewObject<UObject>(Package, StateTreeClass, FName(*TreeName),
        RF_Public | RF_Standalone | RF_Transactional);
    if (!StateTree)
    {
        return MakeError(TEXT("CREATE_FAILED"), TEXT("Failed to create StateTree object"));
    }

    FAssetRegistryModule::AssetCreated(StateTree);
    StateTree->MarkPackageDirty();

    TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
    Data->SetStringField(TEXT("asset_path"), StateTree->GetPathName());
    Data->SetStringField(TEXT("state_tree_name"), TreeName);
    Data->SetStringField(TEXT("schema_class"), SchemaClass);
    Data->SetStringField(TEXT("note"),
        TEXT("State Tree asset created. Configure states and transitions in the editor."));
    return MakeSuccess(Data);
}

// ─────────────────────────────────────────────────────────────────────────────
// ai.set_blackboard_key
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FNexusAIHandler::HandleSetBlackboardKey(
    const TSharedPtr<FJsonObject>& Params)
{
    FString Error;
    AAIController* AIC = ResolveAIController(Params, TEXT("controller_path"), Error);
    if (!AIC) return MakeError(TEXT("NOT_FOUND"), Error);

    FString KeyName = GetStringParam(Params, TEXT("key_name"));
    if (KeyName.IsEmpty())
    {
        return MakeError(TEXT("MISSING_PARAM"), TEXT("key_name is required"));
    }

    FString Value = GetStringParam(Params, TEXT("value"));
    FString ValueType = GetStringParam(Params, TEXT("value_type"), TEXT("String"));

    UBlackboardComponent* BBComp = AIC->GetBlackboardComponent();
    if (!BBComp)
    {
        return MakeError(TEXT("NO_BLACKBOARD"),
            TEXT("AI Controller has no BlackboardComponent"));
    }

    FName KeyFName(*KeyName);

    // Set the value based on type
    if (ValueType == TEXT("Bool"))
    {
        bool BoolVal = Value.Equals(TEXT("true"), ESearchCase::IgnoreCase) ||
                       Value.Equals(TEXT("1"));
        BBComp->SetValueAsBool(KeyFName, BoolVal);
    }
    else if (ValueType == TEXT("Int"))
    {
        BBComp->SetValueAsInt(KeyFName, FCString::Atoi(*Value));
    }
    else if (ValueType == TEXT("Float"))
    {
        BBComp->SetValueAsFloat(KeyFName, FCString::Atof(*Value));
    }
    else if (ValueType == TEXT("String"))
    {
        BBComp->SetValueAsString(KeyFName, Value);
    }
    else if (ValueType == TEXT("Name"))
    {
        BBComp->SetValueAsName(KeyFName, FName(*Value));
    }
    else if (ValueType == TEXT("Vector"))
    {
        // Parse "x,y,z" format
        TArray<FString> Parts;
        Value.ParseIntoArray(Parts, TEXT(","), true);
        if (Parts.Num() >= 3)
        {
            FVector Vec(FCString::Atof(*Parts[0]),
                        FCString::Atof(*Parts[1]),
                        FCString::Atof(*Parts[2]));
            BBComp->SetValueAsVector(KeyFName, Vec);
        }
        else
        {
            return MakeError(TEXT("INVALID_FORMAT"),
                TEXT("Vector value must be in 'x,y,z' format"));
        }
    }
    else if (ValueType == TEXT("Rotator"))
    {
        TArray<FString> Parts;
        Value.ParseIntoArray(Parts, TEXT(","), true);
        if (Parts.Num() >= 3)
        {
            FRotator Rot(FCString::Atof(*Parts[0]),
                         FCString::Atof(*Parts[1]),
                         FCString::Atof(*Parts[2]));
            BBComp->SetValueAsRotator(KeyFName, Rot);
        }
        else
        {
            return MakeError(TEXT("INVALID_FORMAT"),
                TEXT("Rotator value must be in 'pitch,yaw,roll' format"));
        }
    }
    else if (ValueType == TEXT("Object"))
    {
        UObject* Obj = StaticFindObject(UObject::StaticClass(), nullptr, *Value);
        if (!Obj) Obj = LoadObject<UObject>(nullptr, *Value);
        BBComp->SetValueAsObject(KeyFName, Obj);
    }
    else if (ValueType == TEXT("Class"))
    {
        UClass* ClassObj = LoadClass<UObject>(nullptr, *Value);
        BBComp->SetValueAsClass(KeyFName, ClassObj);
    }
    else
    {
        // Default to string
        BBComp->SetValueAsString(KeyFName, Value);
    }

    TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
    Data->SetStringField(TEXT("controller"), AIC->GetPathName());
    Data->SetStringField(TEXT("key_name"), KeyName);
    Data->SetStringField(TEXT("value"), Value);
    Data->SetStringField(TEXT("value_type"), ValueType);
    return MakeSuccess(Data);
}

// ─────────────────────────────────────────────────────────────────────────────
// ai.set_ai_perception
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FNexusAIHandler::HandleSetAIPerception(
    const TSharedPtr<FJsonObject>& Params)
{
    FString Error;
    AAIController* AIC = ResolveAIController(Params, TEXT("controller_path"), Error);
    if (!AIC) return MakeError(TEXT("NOT_FOUND"), Error);

    UAIPerceptionComponent* PerceptionComp = AIC->GetAIPerceptionComponent();
    if (!PerceptionComp)
    {
        // Try to add one if it doesn't exist
        PerceptionComp = NewObject<UAIPerceptionComponent>(AIC, TEXT("AIPerception"));
        if (PerceptionComp)
        {
            PerceptionComp->RegisterComponent();
            AIC->SetPerceptionComponent(*PerceptionComp);
        }
        else
        {
            return MakeError(TEXT("NO_PERCEPTION"),
                TEXT("AI Controller has no Perception component and could not create one"));
        }
    }

    TArray<FString> ConfiguredSenses;
    double Value;
    bool BoolValue;

    // Sight configuration
    bool bHasSightParams =
        Params->TryGetNumberField(TEXT("sight_radius"), Value) ||
        Params->TryGetNumberField(TEXT("sight_age"), Value) ||
        Params->TryGetNumberField(TEXT("lose_sight_radius"), Value) ||
        Params->TryGetNumberField(TEXT("peripheral_vision_angle"), Value) ||
        Params->TryGetNumberField(TEXT("auto_success_range"), Value);

    if (bHasSightParams)
    {
        UAISenseConfig_Sight* SightConfig = nullptr;
        for (auto ConfigIt = PerceptionComp->GetSensesConfigIterator(); ConfigIt; ++ConfigIt)
        {
            SightConfig = Cast<UAISenseConfig_Sight>(*ConfigIt);
            if (SightConfig) break;
        }
        if (!SightConfig)
        {
            SightConfig = NewObject<UAISenseConfig_Sight>(PerceptionComp);
            PerceptionComp->ConfigureSense(*SightConfig);
        }

        if (Params->TryGetNumberField(TEXT("sight_radius"), Value))
            SightConfig->SightRadius = Value;
        if (Params->TryGetNumberField(TEXT("lose_sight_radius"), Value))
            SightConfig->LoseSightRadius = Value;
        if (Params->TryGetNumberField(TEXT("peripheral_vision_angle"), Value))
            SightConfig->PeripheralVisionAngleDegrees = Value;
        if (Params->TryGetNumberField(TEXT("sight_age"), Value))
            SightConfig->SetMaxAge(Value);
        if (Params->TryGetNumberField(TEXT("auto_success_range"), Value))
            SightConfig->AutoSuccessRangeFromLastSeenLocation = Value;

        ConfiguredSenses.Add(TEXT("Sight"));
    }

    // Hearing configuration
    bool bHasHearingParams =
        Params->TryGetNumberField(TEXT("hearing_radius"), Value) ||
        Params->TryGetNumberField(TEXT("hearing_age"), Value);

    if (bHasHearingParams)
    {
        UAISenseConfig_Hearing* HearingConfig = nullptr;
        for (auto ConfigIt = PerceptionComp->GetSensesConfigIterator(); ConfigIt; ++ConfigIt)
        {
            HearingConfig = Cast<UAISenseConfig_Hearing>(*ConfigIt);
            if (HearingConfig) break;
        }
        if (!HearingConfig)
        {
            HearingConfig = NewObject<UAISenseConfig_Hearing>(PerceptionComp);
            PerceptionComp->ConfigureSense(*HearingConfig);
        }

        if (Params->TryGetNumberField(TEXT("hearing_radius"), Value))
            HearingConfig->HearingRange = Value;
        if (Params->TryGetNumberField(TEXT("hearing_age"), Value))
            HearingConfig->SetMaxAge(Value);

        ConfiguredSenses.Add(TEXT("Hearing"));
    }

    // Damage sense
    if (Params->TryGetBoolField(TEXT("damage_sense_enabled"), BoolValue) && BoolValue)
    {
        UAISenseConfig_Damage* DamageConfig = nullptr;
        for (auto ConfigIt = PerceptionComp->GetSensesConfigIterator(); ConfigIt; ++ConfigIt)
        {
            DamageConfig = Cast<UAISenseConfig_Damage>(*ConfigIt);
            if (DamageConfig) break;
        }
        if (!DamageConfig)
        {
            DamageConfig = NewObject<UAISenseConfig_Damage>(PerceptionComp);
            PerceptionComp->ConfigureSense(*DamageConfig);
        }
        ConfiguredSenses.Add(TEXT("Damage"));
    }

    // Team sense
    if (Params->TryGetBoolField(TEXT("team_sense_enabled"), BoolValue) && BoolValue)
    {
        UAISenseConfig_Team* TeamConfig = nullptr;
        for (auto ConfigIt = PerceptionComp->GetSensesConfigIterator(); ConfigIt; ++ConfigIt)
        {
            TeamConfig = Cast<UAISenseConfig_Team>(*ConfigIt);
            if (TeamConfig) break;
        }
        if (!TeamConfig)
        {
            TeamConfig = NewObject<UAISenseConfig_Team>(PerceptionComp);
            PerceptionComp->ConfigureSense(*TeamConfig);
        }
        ConfiguredSenses.Add(TEXT("Team"));
    }

    // Dominant sense
    FString DominantSense = GetStringParam(Params, TEXT("dominant_sense"));
    if (!DominantSense.IsEmpty())
    {
        if (DominantSense == TEXT("Sight"))
            PerceptionComp->SetDominantSense(UAISense_Sight::StaticClass());
        else if (DominantSense == TEXT("Hearing"))
            PerceptionComp->SetDominantSense(UAISense_Hearing::StaticClass());
        else if (DominantSense == TEXT("Damage"))
            PerceptionComp->SetDominantSense(UAISense_Damage::StaticClass());
        else if (DominantSense == TEXT("Team"))
            PerceptionComp->SetDominantSense(UAISense_Team::StaticClass());
    }

    PerceptionComp->RequestStimuliListenerUpdate();

    TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
    Data->SetStringField(TEXT("controller"), AIC->GetPathName());
    TArray<TSharedPtr<FJsonValue>> SenseArr;
    for (const FString& S : ConfiguredSenses)
    {
        SenseArr.Add(MakeShareable(new FJsonValueString(S)));
    }
    Data->SetArrayField(TEXT("configured_senses"), SenseArr);
    if (!DominantSense.IsEmpty())
    {
        Data->SetStringField(TEXT("dominant_sense"), DominantSense);
    }
    return MakeSuccess(Data);
}

// ─────────────────────────────────────────────────────────────────────────────
// ai.assign_behavior_tree
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FNexusAIHandler::HandleAssignBehaviorTree(
    const TSharedPtr<FJsonObject>& Params)
{
    FString Error;
    AAIController* AIC = ResolveAIController(Params, TEXT("controller_path"), Error);
    if (!AIC) return MakeError(TEXT("NOT_FOUND"), Error);

    FString BTPath = GetStringParam(Params, TEXT("behavior_tree_path"));
    if (BTPath.IsEmpty())
    {
        return MakeError(TEXT("MISSING_PARAM"), TEXT("behavior_tree_path is required"));
    }

    UBehaviorTree* BT = LoadObject<UBehaviorTree>(nullptr, *BTPath);
    if (!BT)
    {
        return MakeError(TEXT("NOT_FOUND"),
            FString::Printf(TEXT("Behavior Tree not found at '%s'"), *BTPath));
    }

    // Optionally set blackboard
    FString BBPath = GetStringParam(Params, TEXT("blackboard_path"));
    UBlackboardComponent* BBComp = nullptr;
    if (!BBPath.IsEmpty())
    {
        UBlackboardData* BBData = LoadObject<UBlackboardData>(nullptr, *BBPath);
        if (BBData)
        {
            AIC->UseBlackboard(BBData, BBComp);
        }
    }
    else if (BT->BlackboardAsset)
    {
        // Use the behavior tree's own blackboard
        AIC->UseBlackboard(BT->BlackboardAsset, BBComp);
    }

    bool bAutoRun = GetBoolParam(Params, TEXT("auto_run"), true);
    bool bRunning = false;

    if (bAutoRun)
    {
        bRunning = AIC->RunBehaviorTree(BT);
    }

    TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
    Data->SetStringField(TEXT("controller"), AIC->GetPathName());
    Data->SetStringField(TEXT("behavior_tree"), BT->GetPathName());
    Data->SetBoolField(TEXT("auto_run"), bAutoRun);
    Data->SetBoolField(TEXT("running"), bRunning);
    if (BT->BlackboardAsset)
    {
        Data->SetStringField(TEXT("blackboard"), BT->BlackboardAsset->GetPathName());
    }
    return MakeSuccess(Data);
}

// ─────────────────────────────────────────────────────────────────────────────
// ai.get_ai_controller_info
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FNexusAIHandler::HandleGetAIControllerInfo(
    const TSharedPtr<FJsonObject>& Params)
{
    FString Error;
    AAIController* AIC = ResolveAIController(Params, TEXT("controller_path"), Error);
    if (!AIC) return MakeError(TEXT("NOT_FOUND"), Error);

    bool bIncludeBlackboard = GetBoolParam(Params, TEXT("include_blackboard"), true);
    bool bIncludePerception = GetBoolParam(Params, TEXT("include_perception"), true);
    bool bIncludeTreeStatus = GetBoolParam(Params, TEXT("include_tree_status"), true);

    TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
    Data->SetStringField(TEXT("controller_path"), AIC->GetPathName());
    Data->SetStringField(TEXT("controller_class"), AIC->GetClass()->GetName());

    // Pawn info
    APawn* Pawn = AIC->GetPawn();
    if (Pawn)
    {
        Data->SetStringField(TEXT("pawn_path"), Pawn->GetPathName());
        Data->SetStringField(TEXT("pawn_class"), Pawn->GetClass()->GetName());
    }

    // Behavior tree status
    if (bIncludeTreeStatus)
    {
        UBehaviorTreeComponent* BTComp = Cast<UBehaviorTreeComponent>(
            AIC->GetBrainComponent());
        if (BTComp)
        {
            Data->SetBoolField(TEXT("tree_running"), BTComp->IsRunning());
            Data->SetBoolField(TEXT("tree_paused"), BTComp->IsPaused());

            UBehaviorTree* CurrentTree = BTComp->GetCurrentTree();
            if (CurrentTree)
            {
                Data->SetStringField(TEXT("current_tree"), CurrentTree->GetPathName());
            }
        }
        else
        {
            Data->SetBoolField(TEXT("tree_running"), false);
            Data->SetStringField(TEXT("tree_note"), TEXT("No BehaviorTreeComponent"));
        }
    }

    // Blackboard snapshot
    if (bIncludeBlackboard)
    {
        UBlackboardComponent* BBComp = AIC->GetBlackboardComponent();
        if (BBComp)
        {
            TSharedPtr<FJsonObject> BBData = MakeShareable(new FJsonObject());
            const UBlackboardData* BBAsset = BBComp->GetBlackboardAsset();
            if (BBAsset)
            {
                Data->SetStringField(TEXT("blackboard_asset"), BBAsset->GetPathName());

                for (const FBlackboardEntry& Entry : BBAsset->Keys)
                {
                    FString KName = Entry.EntryName.ToString();
                    FBlackboard::FKey KeyID = BBComp->GetKeyID(FName(*KName));
                    if (KeyID == FBlackboard::InvalidKey) continue;

                    FString ValueStr = BBComp->DescribeKeyValue(KeyID, EBlackboardDescription::Full);
                    BBData->SetStringField(KName, ValueStr);
                }
            }
            Data->SetObjectField(TEXT("blackboard"), BBData);
        }
    }

    // Perception info
    if (bIncludePerception)
    {
        UAIPerceptionComponent* PerceptionComp = AIC->GetAIPerceptionComponent();
        if (PerceptionComp)
        {
            TArray<AActor*> KnownActors;
            PerceptionComp->GetKnownPerceivedActors(nullptr, KnownActors);

            TArray<TSharedPtr<FJsonValue>> KnownArr;
            for (AActor* Known : KnownActors)
            {
                if (!Known) continue;
                TSharedPtr<FJsonObject> KnownObj = MakeShareable(new FJsonObject());
                KnownObj->SetStringField(TEXT("actor_path"), Known->GetPathName());
                KnownObj->SetStringField(TEXT("actor_label"), Known->GetActorLabel());
                KnownObj->SetStringField(TEXT("actor_class"), Known->GetClass()->GetName());
                KnownArr.Add(MakeShareable(new FJsonValueObject(KnownObj)));
            }
            Data->SetArrayField(TEXT("perceived_actors"), KnownArr);
            Data->SetNumberField(TEXT("perceived_actor_count"), KnownActors.Num());

            // List configured senses
            TArray<TSharedPtr<FJsonValue>> SenseArr;
            for (auto ConfigIt = PerceptionComp->GetSensesConfigIterator(); ConfigIt; ++ConfigIt)
            {
                UAISenseConfig* Config = *ConfigIt;
                if (!Config) continue;
                SenseArr.Add(MakeShareable(new FJsonValueString(
                    Config->GetSenseImplementation()->GetName())));
            }
            Data->SetArrayField(TEXT("configured_senses"), SenseArr);
        }
        else
        {
            Data->SetStringField(TEXT("perception_note"), TEXT("No AIPerceptionComponent"));
        }
    }

    return MakeSuccess(Data);
}

// ─────────────────────────────────────────────────────────────────────────────
// ai.list_behavior_trees
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FNexusAIHandler::HandleListBehaviorTrees(
    const TSharedPtr<FJsonObject>& Params)
{
    FString Folder = GetStringParam(Params, TEXT("folder"), TEXT("/Game"));
    FString NameFilter = GetStringParam(Params, TEXT("name_filter"));
    int32 MaxResults = static_cast<int32>(GetNumberParam(Params, TEXT("max_results"), 100.0));

    FAssetRegistryModule& AssetRegistryModule =
        FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
    IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();

    FARFilter Filter;
    Filter.ClassPaths.Add(UBehaviorTree::StaticClass()->GetClassPathName());
    Filter.PackagePaths.Add(FName(*Folder));
    Filter.bRecursivePaths = true;

    TArray<FAssetData> Assets;
    AssetRegistry.GetAssets(Filter, Assets);

    TArray<TSharedPtr<FJsonValue>> ResultArray;
    int32 Count = 0;
    for (const FAssetData& Asset : Assets)
    {
        if (Count >= MaxResults) break;

        FString AssetName = Asset.AssetName.ToString();
        if (!NameFilter.IsEmpty() && !AssetName.Contains(NameFilter))
        {
            continue;
        }

        TSharedPtr<FJsonObject> Entry = MakeShareable(new FJsonObject());
        Entry->SetStringField(TEXT("name"), AssetName);
        Entry->SetStringField(TEXT("asset_path"), Asset.GetObjectPathString());
        Entry->SetStringField(TEXT("package_path"), Asset.PackagePath.ToString());

        // Try to get associated blackboard
        UBehaviorTree* BT = Cast<UBehaviorTree>(Asset.GetAsset());
        if (BT && BT->BlackboardAsset)
        {
            Entry->SetStringField(TEXT("blackboard"), BT->BlackboardAsset->GetPathName());
        }

        ResultArray.Add(MakeShareable(new FJsonValueObject(Entry)));
        Count++;
    }

    TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
    Data->SetArrayField(TEXT("behavior_trees"), ResultArray);
    Data->SetNumberField(TEXT("count"), ResultArray.Num());
    Data->SetStringField(TEXT("folder"), Folder);
    return MakeSuccess(Data);
}

// ─────────────────────────────────────────────────────────────────────────────
// ai.run_eqs_query
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FNexusAIHandler::HandleRunEQSQuery(
    const TSharedPtr<FJsonObject>& Params)
{
    FString QueryPath = GetStringParam(Params, TEXT("query_path"));
    if (QueryPath.IsEmpty())
    {
        return MakeError(TEXT("MISSING_PARAM"), TEXT("query_path is required"));
    }

    FString QuerierPath = GetStringParam(Params, TEXT("querier_path"));
    if (QuerierPath.IsEmpty())
    {
        return MakeError(TEXT("MISSING_PARAM"), TEXT("querier_path is required"));
    }

    FString RunMode = GetStringParam(Params, TEXT("run_mode"), TEXT("SingleBestItem"));
    int32 MaxResults = static_cast<int32>(GetNumberParam(Params, TEXT("max_results"), 10.0));

    UEnvQuery* Query = LoadObject<UEnvQuery>(nullptr, *QueryPath);
    if (!Query)
    {
        return MakeError(TEXT("NOT_FOUND"),
            FString::Printf(TEXT("EQS Query not found at '%s'"), *QueryPath));
    }

    // Resolve querier actor
    AActor* Querier = FindActorByPath(QuerierPath);
    if (!Querier) Querier = FindActorByLabel(QuerierPath);
    if (!Querier)
    {
        return MakeError(TEXT("NOT_FOUND"),
            FString::Printf(TEXT("Querier actor not found: '%s'"), *QuerierPath));
    }

    UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
    if (!World)
    {
        return MakeError(TEXT("NO_WORLD"), TEXT("No editor world available"));
    }

    // Determine EQS run mode
    EEnvQueryRunMode::Type EQSRunMode = EEnvQueryRunMode::SingleResult;
    if (RunMode == TEXT("AllMatching"))
        EQSRunMode = EEnvQueryRunMode::AllMatching;
    else if (RunMode == TEXT("RandomBest25Pct"))
        EQSRunMode = EEnvQueryRunMode::RandomBest25Pct;

    // EQS queries are asynchronous by nature. For the synchronous MCP model,
    // we return the query setup info and note that results require a running game.
    TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
    Data->SetStringField(TEXT("query_path"), Query->GetPathName());
    Data->SetStringField(TEXT("querier"), Querier->GetPathName());
    Data->SetStringField(TEXT("run_mode"), RunMode);
    Data->SetNumberField(TEXT("max_results"), MaxResults);
    Data->SetStringField(TEXT("note"),
        TEXT("EQS query registered. Results are available during PIE (Play In Editor) sessions. ")
        TEXT("Use editor.start_pie first, then re-run this command for live results."));
    return MakeSuccess(Data);
}
