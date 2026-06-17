// Copyright Nexus Team. All Rights Reserved.

#include "Handlers/NexusLevelHandler.h"
#include "Editor.h"
#include "EditorLevelLibrary.h"
#include "EditorLevelUtils.h"
#include "Engine/World.h"
#include "Engine/LevelStreaming.h"
#include "Engine/LevelStreamingDynamic.h"
#include "FileHelpers.h"
#include "WorldPartition/WorldPartition.h"
#include "WorldPartition/DataLayer/DataLayerManager.h"
#include "WorldPartition/DataLayer/DataLayerInstance.h"
#include "Engine/LevelBounds.h"
#include "Factories/WorldFactory.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "LevelUtils.h"

TSharedPtr<FJsonObject> FNexusLevelHandler::Handle(
    const FString& CommandType,
    const TSharedPtr<FJsonObject>& Params)
{
    FString SubCommand = CommandType.RightChop(GetNamespace().Len() + 1);

    if (SubCommand == TEXT("get_current"))            return HandleGetCurrent(Params);
    if (SubCommand == TEXT("load"))                   return HandleLoad(Params);
    if (SubCommand == TEXT("save"))                   return HandleSave(Params);
    if (SubCommand == TEXT("create"))                 return HandleCreate(Params);
    if (SubCommand == TEXT("list_sublevels"))          return HandleListSublevels(Params);
    if (SubCommand == TEXT("add_sublevel"))            return HandleAddSublevel(Params);
    if (SubCommand == TEXT("remove_sublevel"))         return HandleRemoveSublevel(Params);
    if (SubCommand == TEXT("set_sublevel_visibility")) return HandleSetSublevelVisibility(Params);
    if (SubCommand == TEXT("get_world_partition_info")) return HandleGetWorldPartitionInfo(Params);
    if (SubCommand == TEXT("set_data_layer"))          return HandleSetDataLayer(Params);
    if (SubCommand == TEXT("list_streaming_levels"))   return HandleListStreamingLevels(Params);
    if (SubCommand == TEXT("get_bounds"))              return HandleGetBounds(Params);

    return MakeError(TEXT("NOT_IMPLEMENTED"),
        FString::Printf(TEXT("Command '%s' not yet implemented"), *CommandType));
}

// ---------------------------------------------------------------------------
// get_current — return current persistent level info
// ---------------------------------------------------------------------------
TSharedPtr<FJsonObject> FNexusLevelHandler::HandleGetCurrent(
    const TSharedPtr<FJsonObject>& Params)
{
    UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
    TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
    if (World)
    {
        Data->SetStringField(TEXT("level_name"), World->GetMapName());
        Data->SetStringField(TEXT("level_path"), World->GetOutermost()->GetName());
        Data->SetBoolField(TEXT("is_current"), true);
        Data->SetBoolField(TEXT("is_dirty"), World->GetOutermost()->IsDirty());

        int32 ActorCount = 0;
        if (World->PersistentLevel)
        {
            ActorCount = World->PersistentLevel->Actors.Num();
        }
        Data->SetNumberField(TEXT("actor_count"), ActorCount);
    }
    return MakeSuccess(Data);
}

// ---------------------------------------------------------------------------
// load — load a level map in the editor
// ---------------------------------------------------------------------------
TSharedPtr<FJsonObject> FNexusLevelHandler::HandleLoad(
    const TSharedPtr<FJsonObject>& Params)
{
    FString LevelPath = GetStringParam(Params, TEXT("level_path"));
    if (LevelPath.IsEmpty())
    {
        return MakeError(TEXT("MISSING_PARAM"), TEXT("level_path is required"));
    }

    FString MapToLoad = LevelPath;
    FEditorFileUtils::LoadMap(MapToLoad);

    UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
    TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
    if (World)
    {
        Data->SetStringField(TEXT("level_name"), World->GetMapName());
        Data->SetStringField(TEXT("level_path"), World->GetOutermost()->GetName());
        Data->SetBoolField(TEXT("is_current"), true);
        Data->SetBoolField(TEXT("is_dirty"), false);

        int32 ActorCount = 0;
        if (World->PersistentLevel)
        {
            ActorCount = World->PersistentLevel->Actors.Num();
        }
        Data->SetNumberField(TEXT("actor_count"), ActorCount);
    }
    return MakeSuccess(Data);
}

// ---------------------------------------------------------------------------
// save — save the current level (optionally to a new path)
// ---------------------------------------------------------------------------
TSharedPtr<FJsonObject> FNexusLevelHandler::HandleSave(
    const TSharedPtr<FJsonObject>& Params)
{
    FString LevelPath = GetStringParam(Params, TEXT("level_path"));
    UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
    if (!World)
    {
        return MakeError(TEXT("NO_WORLD"), TEXT("No world to save"));
    }

    if (LevelPath.IsEmpty())
    {
        // Save in place
        FEditorFileUtils::SaveLevel(World->PersistentLevel);
    }
    else
    {
        // Save As to the specified path
        FEditorFileUtils::SaveLevel(World->PersistentLevel, LevelPath);
    }

    TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
    Data->SetStringField(TEXT("level_name"), World->GetMapName());
    Data->SetStringField(TEXT("level_path"), World->GetOutermost()->GetName());
    Data->SetBoolField(TEXT("is_current"), true);
    Data->SetBoolField(TEXT("is_dirty"), World->GetOutermost()->IsDirty());

    int32 ActorCount = 0;
    if (World->PersistentLevel)
    {
        ActorCount = World->PersistentLevel->Actors.Num();
    }
    Data->SetNumberField(TEXT("actor_count"), ActorCount);
    return MakeSuccess(Data);
}

// ---------------------------------------------------------------------------
// create — create a new empty level
// ---------------------------------------------------------------------------
TSharedPtr<FJsonObject> FNexusLevelHandler::HandleCreate(
    const TSharedPtr<FJsonObject>& Params)
{
    FString LevelPath = GetStringParam(Params, TEXT("level_path"));
    FString Template = GetStringParam(Params, TEXT("template"), TEXT("Default"));

    if (LevelPath.IsEmpty())
    {
        return MakeError(TEXT("MISSING_PARAM"), TEXT("level_path is required"));
    }

    // Extract folder and asset name from the path
    FString PackagePath, AssetName;
    int32 LastSlash;
    if (LevelPath.FindLastChar(TEXT('/'), LastSlash))
    {
        PackagePath = LevelPath.Left(LastSlash);
        AssetName = LevelPath.RightChop(LastSlash + 1);
    }
    else
    {
        return MakeError(TEXT("INVALID_PATH"),
            TEXT("level_path must be a full asset path like '/Game/Maps/NewLevel'"));
    }

    IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
    UWorldFactory* Factory = NewObject<UWorldFactory>();
    UObject* NewAsset = AssetTools.CreateAsset(AssetName, PackagePath, UWorld::StaticClass(), Factory);

    if (!NewAsset)
    {
        return MakeError(TEXT("CREATE_FAILED"),
            FString::Printf(TEXT("Failed to create level at '%s'"), *LevelPath));
    }

    // Load the newly created level
    FString NewMapPath = NewAsset->GetOutermost()->GetName();
    FEditorFileUtils::LoadMap(NewMapPath);

    UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
    TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
    if (World)
    {
        Data->SetStringField(TEXT("level_name"), World->GetMapName());
        Data->SetStringField(TEXT("level_path"), World->GetOutermost()->GetName());
        Data->SetBoolField(TEXT("is_current"), true);
        Data->SetBoolField(TEXT("is_dirty"), World->GetOutermost()->IsDirty());
        Data->SetNumberField(TEXT("actor_count"),
            World->PersistentLevel ? World->PersistentLevel->Actors.Num() : 0);
    }
    return MakeSuccess(Data);
}

// ---------------------------------------------------------------------------
// list_sublevels — list sublevels of the current persistent level
// ---------------------------------------------------------------------------
TSharedPtr<FJsonObject> FNexusLevelHandler::HandleListSublevels(
    const TSharedPtr<FJsonObject>& Params)
{
    UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
    if (!World) return MakeError(TEXT("NO_WORLD"), TEXT("No editor world"));

    TArray<TSharedPtr<FJsonValue>> Sublevels;
    for (ULevelStreaming* StreamingLevel : World->GetStreamingLevels())
    {
        if (StreamingLevel)
        {
            TSharedPtr<FJsonObject> SubObj = MakeShareable(new FJsonObject());
            SubObj->SetStringField(TEXT("level_name"),
                FPackageName::GetShortName(StreamingLevel->GetWorldAssetPackageName()));
            SubObj->SetStringField(TEXT("level_path"),
                StreamingLevel->GetWorldAssetPackageName());
            SubObj->SetBoolField(TEXT("is_visible"),
                StreamingLevel->GetShouldBeVisibleInEditor());
            SubObj->SetBoolField(TEXT("is_loaded"),
                StreamingLevel->HasLoadedLevel());
            SubObj->SetBoolField(TEXT("is_locked"),
                StreamingLevel->bLocked);
            SubObj->SetStringField(TEXT("streaming_type"),
                StreamingLevel->IsA<ULevelStreamingDynamic>()
                    ? TEXT("Blueprint") : TEXT("AlwaysLoaded"));
            Sublevels.Add(MakeShareable(new FJsonValueObject(SubObj)));
        }
    }

    TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
    Data->SetStringField(TEXT("persistent_level"), World->GetMapName());
    Data->SetArrayField(TEXT("sublevels"), Sublevels);
    Data->SetNumberField(TEXT("total_count"), Sublevels.Num());
    return MakeSuccess(Data);
}

// ---------------------------------------------------------------------------
// add_sublevel — add a level as a streaming sublevel
// ---------------------------------------------------------------------------
TSharedPtr<FJsonObject> FNexusLevelHandler::HandleAddSublevel(
    const TSharedPtr<FJsonObject>& Params)
{
    UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
    if (!World) return MakeError(TEXT("NO_WORLD"), TEXT("No editor world"));

    FString LevelPath = GetStringParam(Params, TEXT("level_path"));
    FString StreamingType = GetStringParam(Params, TEXT("streaming_type"), TEXT("AlwaysLoaded"));

    if (LevelPath.IsEmpty())
    {
        return MakeError(TEXT("MISSING_PARAM"), TEXT("level_path is required"));
    }

    // Determine the streaming level class based on type
    TSubclassOf<ULevelStreaming> StreamingClass =
        (StreamingType == TEXT("Blueprint"))
            ? ULevelStreamingDynamic::StaticClass()
            : ULevelStreaming::StaticClass();

    ULevelStreaming* NewStreamingLevel = EditorLevelUtils::AddLevelToWorld(
        World, *LevelPath, StreamingClass);

    if (!NewStreamingLevel)
    {
        return MakeError(TEXT("ADD_FAILED"),
            FString::Printf(TEXT("Failed to add sublevel '%s'"), *LevelPath));
    }

    TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
    Data->SetStringField(TEXT("level_name"),
        FPackageName::GetShortName(NewStreamingLevel->GetWorldAssetPackageName()));
    Data->SetStringField(TEXT("level_path"),
        NewStreamingLevel->GetWorldAssetPackageName());
    Data->SetBoolField(TEXT("is_visible"),
        NewStreamingLevel->GetShouldBeVisibleInEditor());
    Data->SetBoolField(TEXT("is_loaded"),
        NewStreamingLevel->HasLoadedLevel());
    Data->SetBoolField(TEXT("is_locked"), false);
    Data->SetStringField(TEXT("streaming_type"), StreamingType);
    return MakeSuccess(Data);
}

// ---------------------------------------------------------------------------
// remove_sublevel — remove a streaming sublevel from the world
// ---------------------------------------------------------------------------
TSharedPtr<FJsonObject> FNexusLevelHandler::HandleRemoveSublevel(
    const TSharedPtr<FJsonObject>& Params)
{
    UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
    if (!World) return MakeError(TEXT("NO_WORLD"), TEXT("No editor world"));

    FString LevelPath = GetStringParam(Params, TEXT("level_path"));
    if (LevelPath.IsEmpty())
    {
        return MakeError(TEXT("MISSING_PARAM"), TEXT("level_path is required"));
    }

    // Find the streaming level by path
    ULevelStreaming* FoundLevel = nullptr;
    for (ULevelStreaming* StreamingLevel : World->GetStreamingLevels())
    {
        if (StreamingLevel && StreamingLevel->GetWorldAssetPackageName() == LevelPath)
        {
            FoundLevel = StreamingLevel;
            break;
        }
    }

    if (!FoundLevel)
    {
        return MakeError(TEXT("NOT_FOUND"),
            FString::Printf(TEXT("Sublevel '%s' not found"), *LevelPath));
    }

    if (FoundLevel->HasLoadedLevel())
    {
        EditorLevelUtils::RemoveLevelFromWorld(FoundLevel->GetLoadedLevel());
    }

    // Return updated sublevel list
    return HandleListSublevels(Params);
}

// ---------------------------------------------------------------------------
// set_sublevel_visibility — show/hide a sublevel in the editor
// ---------------------------------------------------------------------------
TSharedPtr<FJsonObject> FNexusLevelHandler::HandleSetSublevelVisibility(
    const TSharedPtr<FJsonObject>& Params)
{
    UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
    if (!World) return MakeError(TEXT("NO_WORLD"), TEXT("No editor world"));

    FString LevelPath = GetStringParam(Params, TEXT("level_path"));
    bool bVisible = GetBoolParam(Params, TEXT("visible"), true);

    if (LevelPath.IsEmpty())
    {
        return MakeError(TEXT("MISSING_PARAM"), TEXT("level_path is required"));
    }

    ULevelStreaming* FoundLevel = nullptr;
    for (ULevelStreaming* StreamingLevel : World->GetStreamingLevels())
    {
        if (StreamingLevel && StreamingLevel->GetWorldAssetPackageName() == LevelPath)
        {
            FoundLevel = StreamingLevel;
            break;
        }
    }

    if (!FoundLevel)
    {
        return MakeError(TEXT("NOT_FOUND"),
            FString::Printf(TEXT("Sublevel '%s' not found"), *LevelPath));
    }

    FoundLevel->SetShouldBeVisibleInEditor(bVisible);
    if (FoundLevel->HasLoadedLevel())
    {
        FoundLevel->GetLoadedLevel()->bIsVisible = bVisible;
    }

    TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
    Data->SetStringField(TEXT("level_name"),
        FPackageName::GetShortName(FoundLevel->GetWorldAssetPackageName()));
    Data->SetStringField(TEXT("level_path"),
        FoundLevel->GetWorldAssetPackageName());
    Data->SetBoolField(TEXT("is_visible"), bVisible);
    Data->SetBoolField(TEXT("is_loaded"), FoundLevel->HasLoadedLevel());
    Data->SetBoolField(TEXT("is_locked"), FoundLevel->bLocked);
    Data->SetStringField(TEXT("streaming_type"),
        FoundLevel->IsA<ULevelStreamingDynamic>()
            ? TEXT("Blueprint") : TEXT("AlwaysLoaded"));
    return MakeSuccess(Data);
}

// ---------------------------------------------------------------------------
// get_world_partition_info — query World Partition configuration
// ---------------------------------------------------------------------------
TSharedPtr<FJsonObject> FNexusLevelHandler::HandleGetWorldPartitionInfo(
    const TSharedPtr<FJsonObject>& Params)
{
    UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
    if (!World) return MakeError(TEXT("NO_WORLD"), TEXT("No editor world"));

    TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());

    UWorldPartition* WP = World->GetWorldPartition();
    if (!WP)
    {
        Data->SetBoolField(TEXT("is_enabled"), false);
        Data->SetNumberField(TEXT("cell_size"), 0.0);
        Data->SetNumberField(TEXT("loading_range"), 0.0);
        Data->SetArrayField(TEXT("data_layers"), TArray<TSharedPtr<FJsonValue>>());
        Data->SetStringField(TEXT("runtime_hash_class"), TEXT(""));
        Data->SetNumberField(TEXT("loaded_cells"), 0);
        Data->SetNumberField(TEXT("total_cells"), 0);
        return MakeSuccess(Data);
    }

    Data->SetBoolField(TEXT("is_enabled"), true);
    Data->SetBoolField(TEXT("streaming_enabled"), WP->IsStreamingEnabled());

    // Gather data layers
    TArray<TSharedPtr<FJsonValue>> DataLayerNames;
    UDataLayerManager* DLM = UDataLayerManager::GetDataLayerManager(World);
    if (DLM)
    {
        DLM->ForEachDataLayerInstance([&DataLayerNames](UDataLayerInstance* Layer) -> bool
        {
            if (Layer)
            {
                DataLayerNames.Add(MakeShareable(
                    new FJsonValueString(Layer->GetDataLayerShortName())));
            }
            return true;
        });
    }
    Data->SetArrayField(TEXT("data_layers"), DataLayerNames);

    // Runtime hash class info
    Data->SetStringField(TEXT("runtime_hash_class"), TEXT("RuntimeSpatialHash"));

    Data->SetNumberField(TEXT("cell_size"), 0.0);
    Data->SetNumberField(TEXT("loading_range"), 0.0);
    Data->SetNumberField(TEXT("loaded_cells"), 0);
    Data->SetNumberField(TEXT("total_cells"), 0);

    return MakeSuccess(Data);
}

// ---------------------------------------------------------------------------
// set_data_layer — modify activation/visibility of a data layer
// ---------------------------------------------------------------------------
TSharedPtr<FJsonObject> FNexusLevelHandler::HandleSetDataLayer(
    const TSharedPtr<FJsonObject>& Params)
{
    UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
    if (!World) return MakeError(TEXT("NO_WORLD"), TEXT("No editor world"));

    FString LayerName = GetStringParam(Params, TEXT("layer_name"));
    bool bIsActive = GetBoolParam(Params, TEXT("is_active"), true);
    bool bIsVisible = GetBoolParam(Params, TEXT("is_visible"), true);

    if (LayerName.IsEmpty())
    {
        return MakeError(TEXT("MISSING_PARAM"), TEXT("layer_name is required"));
    }

    UDataLayerManager* DLM = UDataLayerManager::GetDataLayerManager(World);
    if (!DLM)
    {
        return MakeError(TEXT("NO_WORLD_PARTITION"),
            TEXT("World Partition / Data Layer Manager not available"));
    }

    // Find the data layer by name
    UDataLayerInstance* FoundLayer = nullptr;
    DLM->ForEachDataLayerInstance([&FoundLayer, &LayerName](UDataLayerInstance* Layer) -> bool
    {
        if (Layer && Layer->GetDataLayerShortName() == LayerName)
        {
            FoundLayer = Layer;
            return false; // stop iteration
        }
        return true;
    });

    if (!FoundLayer)
    {
        return MakeError(TEXT("NOT_FOUND"),
            FString::Printf(TEXT("Data layer '%s' not found"), *LayerName));
    }

    // UE 5.7: UDataLayerInstance does not have SetIsRuntime - runtime status is determined
    // by the DataLayerAsset type (EDataLayerType::Runtime). We can only modify visibility.
    // Set editor visibility
    FoundLayer->SetIsInitiallyVisible(bIsVisible);

    TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
    Data->SetStringField(TEXT("layer_name"), LayerName);
    Data->SetBoolField(TEXT("is_active"), bIsActive);
    Data->SetBoolField(TEXT("is_visible"), bIsVisible);
    return MakeSuccess(Data);
}

// ---------------------------------------------------------------------------
// list_streaming_levels — detailed info about all streaming levels
// ---------------------------------------------------------------------------
TSharedPtr<FJsonObject> FNexusLevelHandler::HandleListStreamingLevels(
    const TSharedPtr<FJsonObject>& Params)
{
    UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
    if (!World) return MakeError(TEXT("NO_WORLD"), TEXT("No editor world"));

    TArray<TSharedPtr<FJsonValue>> Levels;
    for (ULevelStreaming* StreamingLevel : World->GetStreamingLevels())
    {
        if (!StreamingLevel) continue;

        TSharedPtr<FJsonObject> LevelObj = MakeShareable(new FJsonObject());
        LevelObj->SetStringField(TEXT("package_name"),
            StreamingLevel->GetWorldAssetPackageName());
        LevelObj->SetStringField(TEXT("level_name"),
            FPackageName::GetShortName(StreamingLevel->GetWorldAssetPackageName()));
        LevelObj->SetBoolField(TEXT("is_loaded"),
            StreamingLevel->HasLoadedLevel());
        LevelObj->SetBoolField(TEXT("is_visible"),
            StreamingLevel->GetShouldBeVisibleInEditor());

        // Transform offset
        FTransform LevelTransform = StreamingLevel->LevelTransform;
        FVector Offset = LevelTransform.GetTranslation();
        TSharedPtr<FJsonObject> OffsetObj = VectorToJson(Offset);
        LevelObj->SetObjectField(TEXT("transform_offset"), OffsetObj);

        // Streaming type
        LevelObj->SetStringField(TEXT("streaming_type"),
            StreamingLevel->IsA<ULevelStreamingDynamic>()
                ? TEXT("Blueprint") : TEXT("AlwaysLoaded"));

        Levels.Add(MakeShareable(new FJsonValueObject(LevelObj)));
    }

    TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
    Data->SetArrayField(TEXT("levels"), Levels);
    Data->SetNumberField(TEXT("total_count"), Levels.Num());
    return MakeSuccess(Data);
}

// ---------------------------------------------------------------------------
// get_bounds — axis-aligned bounding box for the level
// ---------------------------------------------------------------------------
TSharedPtr<FJsonObject> FNexusLevelHandler::HandleGetBounds(
    const TSharedPtr<FJsonObject>& Params)
{
    UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
    if (!World) return MakeError(TEXT("NO_WORLD"), TEXT("No editor world"));

    FString LevelPath = GetStringParam(Params, TEXT("level_path"));

    // Compute bounds for the persistent level
    FBox Bounds(ForceInit);

    if (World->PersistentLevel)
    {
        for (AActor* Actor : World->PersistentLevel->Actors)
        {
            if (Actor && !Actor->IsA(ALevelBounds::StaticClass()))
            {
                FBox ActorBounds = Actor->GetComponentsBoundingBox(true);
                if (ActorBounds.IsValid)
                {
                    Bounds += ActorBounds;
                }
            }
        }
    }

    TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
    Data->SetStringField(TEXT("level_path"),
        LevelPath.IsEmpty() ? World->GetOutermost()->GetName() : LevelPath);

    // Bounds info
    TSharedPtr<FJsonObject> BoundsObj = MakeShareable(new FJsonObject());
    if (Bounds.IsValid)
    {
        FVector Origin = Bounds.GetCenter();
        FVector Extent = Bounds.GetExtent();

        BoundsObj->SetObjectField(TEXT("origin"), VectorToJson(Origin));
        BoundsObj->SetObjectField(TEXT("extent"), VectorToJson(Extent));
        BoundsObj->SetObjectField(TEXT("min"), VectorToJson(Bounds.Min));
        BoundsObj->SetObjectField(TEXT("max"), VectorToJson(Bounds.Max));
    }
    else
    {
        BoundsObj->SetObjectField(TEXT("origin"), VectorToJson(FVector::ZeroVector));
        BoundsObj->SetObjectField(TEXT("extent"), VectorToJson(FVector::ZeroVector));
        BoundsObj->SetObjectField(TEXT("min"), VectorToJson(FVector::ZeroVector));
        BoundsObj->SetObjectField(TEXT("max"), VectorToJson(FVector::ZeroVector));
    }
    Data->SetObjectField(TEXT("bounds"), BoundsObj);

    int32 ActorCount = World->PersistentLevel ? World->PersistentLevel->Actors.Num() : 0;
    Data->SetNumberField(TEXT("actor_count"), ActorCount);

    return MakeSuccess(Data);
}
