// Copyright Nexus Team. All Rights Reserved.

#include "Handlers/NexusActorHandler.h"
#include "Engine/World.h"
#include "Engine/StaticMeshActor.h"
#include "EngineUtils.h"
#include "Editor.h"
#include "EditorLevelLibrary.h"
#include "Kismet/GameplayStatics.h"
#include "Components/SceneComponent.h"
#include "Selection.h"

// ─────────────────────────────────────────────────────────────────────────────
// Dispatch
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FNexusActorHandler::Handle(
    const FString& CommandType,
    const TSharedPtr<FJsonObject>& Params)
{
    FString SubCommand = CommandType.RightChop(GetNamespace().Len() + 1);

    if (SubCommand == TEXT("spawn"))          return HandleSpawn(Params);
    if (SubCommand == TEXT("find"))           return HandleFind(Params);
    if (SubCommand == TEXT("find_by_class"))  return HandleFindByClass(Params);
    if (SubCommand == TEXT("delete"))         return HandleDelete(Params);
    if (SubCommand == TEXT("get_transform"))  return HandleGetTransform(Params);
    if (SubCommand == TEXT("set_transform"))  return HandleSetTransform(Params);
    if (SubCommand == TEXT("get_property"))   return HandleGetProperty(Params);
    if (SubCommand == TEXT("set_property"))   return HandleSetProperty(Params);
    if (SubCommand == TEXT("list_all"))       return HandleListAll(Params);
    if (SubCommand == TEXT("get_components")) return HandleGetComponents(Params);
    if (SubCommand == TEXT("set_visibility")) return HandleSetVisibility(Params);
    if (SubCommand == TEXT("duplicate"))      return HandleDuplicate(Params);
    if (SubCommand == TEXT("rename"))         return HandleRename(Params);
    if (SubCommand == TEXT("add_tag"))        return HandleAddTag(Params);
    if (SubCommand == TEXT("remove_tag"))     return HandleRemoveTag(Params);
    if (SubCommand == TEXT("get_tags"))       return HandleGetTags(Params);
    if (SubCommand == TEXT("attach"))         return HandleAttach(Params);
    if (SubCommand == TEXT("detach"))         return HandleDetach(Params);
    if (SubCommand == TEXT("set_mobility"))         return HandleSetMobility(Params);
    if (SubCommand == TEXT("get_bounds"))           return HandleGetBounds(Params);
    // Batch operations (A3)
    if (SubCommand == TEXT("spawn_batch"))          return HandleSpawnBatch(Params);
    if (SubCommand == TEXT("set_properties_batch")) return HandleSetPropertiesBatch(Params);
    if (SubCommand == TEXT("delete_batch"))         return HandleDeleteBatch(Params);
    if (SubCommand == TEXT("set_transform_batch"))  return HandleSetTransformBatch(Params);

    return MakeError(TEXT("NOT_IMPLEMENTED"),
        FString::Printf(TEXT("Command '%s' not yet implemented"), *CommandType));
}

// ─────────────────────────────────────────────────────────────────────────────
// Actor resolution helpers
// ─────────────────────────────────────────────────────────────────────────────

AActor* FNexusActorHandler::FindActorByLabel(const FString& Label)
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

AActor* FNexusActorHandler::FindActorByPath(const FString& Path)
{
    if (Path.IsEmpty()) return nullptr;

    // StaticFindObject resolves full object paths like
    // /Game/Maps/Main.Main:PersistentLevel.StaticMeshActor_0
    UObject* Obj = StaticFindObject(AActor::StaticClass(), nullptr, *Path);
    return Cast<AActor>(Obj);
}

AActor* FNexusActorHandler::ResolveActor(const TSharedPtr<FJsonObject>& Params, FString& OutError)
{
    // Prefer actor_path if supplied
    FString ActorPath = GetStringParam(Params, TEXT("actor_path"));
    if (!ActorPath.IsEmpty())
    {
        AActor* Actor = FindActorByPath(ActorPath);
        if (!Actor)
        {
            OutError = FString::Printf(TEXT("No actor found at path '%s'"), *ActorPath);
        }
        return Actor;
    }

    // Fallback to actor_label
    FString ActorLabel = GetStringParam(Params, TEXT("actor_label"));
    if (!ActorLabel.IsEmpty())
    {
        AActor* Actor = FindActorByLabel(ActorLabel);
        if (!Actor)
        {
            OutError = FString::Printf(TEXT("No actor with label '%s'"), *ActorLabel);
        }
        return Actor;
    }

    OutError = TEXT("Must provide 'actor_path' or 'actor_label'");
    return nullptr;
}

// ─────────────────────────────────────────────────────────────────────────────
// spawn
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FNexusActorHandler::HandleSpawn(const TSharedPtr<FJsonObject>& Params)
{
    FString ClassPath = GetStringParam(Params, TEXT("class_path"));
    // Also accept "actor_class" for compatibility with Python tool
    if (ClassPath.IsEmpty())
    {
        ClassPath = GetStringParam(Params, TEXT("actor_class"), TEXT("/Script/Engine.StaticMeshActor"));
    }
    FString Label = GetStringParam(Params, TEXT("label"));
    FVector Location = GetVectorParam(Params, TEXT("location"));
    FVector RotVec = GetVectorParam(Params, TEXT("rotation"));
    FRotator Rotation(RotVec.X, RotVec.Y, RotVec.Z);

    // Handle rotation sent as {pitch, yaw, roll} (from Python tool)
    const TSharedPtr<FJsonObject>* RotObj;
    if (Params->TryGetObjectField(TEXT("rotation"), RotObj))
    {
        double Pitch = 0, Yaw = 0, Roll = 0;
        (*RotObj)->TryGetNumberField(TEXT("pitch"), Pitch);
        (*RotObj)->TryGetNumberField(TEXT("yaw"), Yaw);
        (*RotObj)->TryGetNumberField(TEXT("roll"), Roll);
        if (Pitch != 0 || Yaw != 0 || Roll != 0)
        {
            Rotation = FRotator(Pitch, Yaw, Roll);
        }
    }

    // Resolve short class name to full path if needed
    if (!ClassPath.Contains(TEXT("/")))
    {
        ClassPath = FString::Printf(TEXT("/Script/Engine.%s"), *ClassPath);
    }

    UClass* ActorClass = LoadClass<AActor>(nullptr, *ClassPath);
    if (!ActorClass)
    {
        return MakeError(TEXT("CLASS_NOT_FOUND"),
            FString::Printf(TEXT("Cannot find class '%s'"), *ClassPath));
    }

    UWorld* World = GEditor->GetEditorWorldContext().World();
    if (!World)
    {
        return MakeError(TEXT("NO_WORLD"), TEXT("No editor world available"));
    }

    FActorSpawnParameters SpawnParams;
    SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

    FTransform SpawnTransform(Rotation.Quaternion(), Location);
    AActor* NewActor = World->SpawnActor<AActor>(ActorClass, SpawnTransform, SpawnParams);
    if (!NewActor)
    {
        return MakeError(TEXT("SPAWN_FAILED"), TEXT("Failed to spawn actor"));
    }

    if (!Label.IsEmpty())
    {
        NewActor->SetActorLabel(Label);
    }

    // Apply scale if provided
    FVector Scale = GetVectorParam(Params, TEXT("scale"), FVector::OneVector);
    if (Scale != FVector::OneVector)
    {
        NewActor->SetActorScale3D(Scale);
    }

    TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
    Data->SetStringField(TEXT("actor_path"), NewActor->GetPathName());
    Data->SetStringField(TEXT("actor_label"), NewActor->GetActorLabel());
    Data->SetStringField(TEXT("actor_class"), NewActor->GetClass()->GetName());
    return MakeSuccess(Data);
}

// ─────────────────────────────────────────────────────────────────────────────
// find
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FNexusActorHandler::HandleFind(const TSharedPtr<FJsonObject>& Params)
{
    FString Pattern = GetStringParam(Params, TEXT("pattern"));
    FString Query = GetStringParam(Params, TEXT("query"));
    // Support both "pattern" (legacy) and "query" (Python tool) params
    if (Pattern.IsEmpty() && !Query.IsEmpty())
    {
        Pattern = Query;
    }
    if (Pattern.IsEmpty())
    {
        Pattern = TEXT("*");
    }
    int32 Limit = (int32)GetNumberParam(Params, TEXT("limit"), 100);

    UWorld* World = GEditor->GetEditorWorldContext().World();
    if (!World)
    {
        return MakeError(TEXT("NO_WORLD"), TEXT("No editor world available"));
    }

    TArray<TSharedPtr<FJsonValue>> ActorsArray;
    for (TActorIterator<AActor> It(World); It; ++It)
    {
        FString ActorLabel = It->GetActorLabel();
        FString ActorPath = It->GetPathName();
        if (ActorLabel.MatchesWildcard(Pattern) || ActorPath.Contains(Pattern))
        {
            TSharedPtr<FJsonObject> ActorObj = MakeShareable(new FJsonObject());
            ActorObj->SetStringField(TEXT("actor_path"), ActorPath);
            ActorObj->SetStringField(TEXT("actor_label"), ActorLabel);
            ActorObj->SetStringField(TEXT("actor_class"), It->GetClass()->GetName());
            ActorsArray.Add(MakeShareable(new FJsonValueObject(ActorObj)));

            if (ActorsArray.Num() >= Limit) break;
        }
    }

    TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
    Data->SetArrayField(TEXT("actors"), ActorsArray);
    Data->SetNumberField(TEXT("total_count"), ActorsArray.Num());
    return MakeSuccess(Data);
}

// ─────────────────────────────────────────────────────────────────────────────
// find_by_class
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FNexusActorHandler::HandleFindByClass(const TSharedPtr<FJsonObject>& Params)
{
    FString ClassName = GetStringParam(Params, TEXT("class_name"));
    if (ClassName.IsEmpty())
    {
        ClassName = GetStringParam(Params, TEXT("actor_class"), TEXT("StaticMeshActor"));
    }
    int32 Limit = (int32)GetNumberParam(Params, TEXT("limit"), 100);

    UWorld* World = GEditor->GetEditorWorldContext().World();
    if (!World) return MakeError(TEXT("NO_WORLD"), TEXT("No editor world"));

    TArray<TSharedPtr<FJsonValue>> ActorsArray;
    for (TActorIterator<AActor> It(World); It; ++It)
    {
        if (It->GetClass()->GetName() == ClassName)
        {
            TSharedPtr<FJsonObject> ActorObj = MakeShareable(new FJsonObject());
            ActorObj->SetStringField(TEXT("actor_path"), It->GetPathName());
            ActorObj->SetStringField(TEXT("actor_label"), It->GetActorLabel());
            ActorObj->SetStringField(TEXT("actor_class"), ClassName);
            ActorsArray.Add(MakeShareable(new FJsonValueObject(ActorObj)));

            if (ActorsArray.Num() >= Limit) break;
        }
    }

    TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
    Data->SetArrayField(TEXT("actors"), ActorsArray);
    Data->SetNumberField(TEXT("total_count"), ActorsArray.Num());
    return MakeSuccess(Data);
}

// ─────────────────────────────────────────────────────────────────────────────
// delete
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FNexusActorHandler::HandleDelete(const TSharedPtr<FJsonObject>& Params)
{
    FString ErrorMsg;
    AActor* Actor = ResolveActor(Params, ErrorMsg);
    if (!Actor)
    {
        return MakeError(TEXT("ACTOR_NOT_FOUND"), ErrorMsg);
    }

    TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
    Data->SetStringField(TEXT("actor_path"), Actor->GetPathName());
    Data->SetStringField(TEXT("actor_label"), Actor->GetActorLabel());
    Data->SetStringField(TEXT("deleted"), Actor->GetActorLabel());

    Actor->Destroy();

    return MakeSuccess(Data);
}

// ─────────────────────────────────────────────────────────────────────────────
// get_transform
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FNexusActorHandler::HandleGetTransform(const TSharedPtr<FJsonObject>& Params)
{
    FString ErrorMsg;
    AActor* Actor = ResolveActor(Params, ErrorMsg);
    if (!Actor) return MakeError(TEXT("ACTOR_NOT_FOUND"), ErrorMsg);

    FTransform Transform = Actor->GetActorTransform();

    TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
    Data->SetObjectField(TEXT("location"), VectorToJson(Transform.GetLocation()));
    Data->SetObjectField(TEXT("rotation"), RotatorToJson(Transform.Rotator()));
    Data->SetObjectField(TEXT("scale"), VectorToJson(Transform.GetScale3D()));
    return MakeSuccess(Data);
}

// ─────────────────────────────────────────────────────────────────────────────
// set_transform
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FNexusActorHandler::HandleSetTransform(const TSharedPtr<FJsonObject>& Params)
{
    FString ErrorMsg;
    AActor* Actor = ResolveActor(Params, ErrorMsg);
    if (!Actor) return MakeError(TEXT("ACTOR_NOT_FOUND"), ErrorMsg);

    const TSharedPtr<FJsonObject>* LocationObj;
    if (Params->TryGetObjectField(TEXT("location"), LocationObj))
    {
        FVector Loc(
            (*LocationObj)->GetNumberField(TEXT("x")),
            (*LocationObj)->GetNumberField(TEXT("y")),
            (*LocationObj)->GetNumberField(TEXT("z"))
        );
        Actor->SetActorLocation(Loc);
    }

    const TSharedPtr<FJsonObject>* RotationObj;
    if (Params->TryGetObjectField(TEXT("rotation"), RotationObj))
    {
        // Support both {x,y,z} and {pitch,yaw,roll} formats
        double Pitch = 0, Yaw = 0, Roll = 0;
        if ((*RotationObj)->TryGetNumberField(TEXT("pitch"), Pitch) ||
            (*RotationObj)->TryGetNumberField(TEXT("yaw"), Yaw) ||
            (*RotationObj)->TryGetNumberField(TEXT("roll"), Roll))
        {
            // Re-read all three in case only some were in the first conditional
            (*RotationObj)->TryGetNumberField(TEXT("pitch"), Pitch);
            (*RotationObj)->TryGetNumberField(TEXT("yaw"), Yaw);
            (*RotationObj)->TryGetNumberField(TEXT("roll"), Roll);
        }
        else
        {
            Pitch = (*RotationObj)->GetNumberField(TEXT("x"));
            Yaw = (*RotationObj)->GetNumberField(TEXT("y"));
            Roll = (*RotationObj)->GetNumberField(TEXT("z"));
        }
        Actor->SetActorRotation(FRotator(Pitch, Yaw, Roll));
    }

    const TSharedPtr<FJsonObject>* ScaleObj;
    if (Params->TryGetObjectField(TEXT("scale"), ScaleObj))
    {
        FVector Scale(
            (*ScaleObj)->GetNumberField(TEXT("x")),
            (*ScaleObj)->GetNumberField(TEXT("y")),
            (*ScaleObj)->GetNumberField(TEXT("z"))
        );
        Actor->SetActorScale3D(Scale);
    }

    // Return updated transform
    FTransform Transform = Actor->GetActorTransform();
    TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
    Data->SetStringField(TEXT("actor_path"), Actor->GetPathName());
    Data->SetObjectField(TEXT("location"), VectorToJson(Transform.GetLocation()));
    Data->SetObjectField(TEXT("rotation"), RotatorToJson(Transform.Rotator()));
    Data->SetObjectField(TEXT("scale"), VectorToJson(Transform.GetScale3D()));
    return MakeSuccess(Data);
}

// ─────────────────────────────────────────────────────────────────────────────
// get_property
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FNexusActorHandler::HandleGetProperty(const TSharedPtr<FJsonObject>& Params)
{
    FString ErrorMsg;
    AActor* Actor = ResolveActor(Params, ErrorMsg);
    if (!Actor) return MakeError(TEXT("ACTOR_NOT_FOUND"), ErrorMsg);

    FString PropertyName = GetStringParam(Params, TEXT("property_name"));
    if (PropertyName.IsEmpty())
    {
        return MakeError(TEXT("MISSING_PARAM"), TEXT("property_name is required"));
    }

    FProperty* Prop = Actor->GetClass()->FindPropertyByName(FName(*PropertyName));
    if (!Prop)
    {
        return MakeError(TEXT("PROPERTY_NOT_FOUND"),
            FString::Printf(TEXT("No property '%s' on actor class '%s'"),
                *PropertyName, *Actor->GetClass()->GetName()));
    }

    void* ValuePtr = Prop->ContainerPtrToValuePtr<void>(Actor);
    FString ValueStr;
    Prop->ExportTextItem_Direct(ValueStr, ValuePtr, nullptr, Actor, PPF_None);

    TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
    Data->SetStringField(TEXT("actor_path"), Actor->GetPathName());
    Data->SetStringField(TEXT("property_name"), PropertyName);
    Data->SetStringField(TEXT("property_value"), ValueStr);
    Data->SetStringField(TEXT("property_type"), Prop->GetCPPType());
    return MakeSuccess(Data);
}

// ─────────────────────────────────────────────────────────────────────────────
// set_property
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FNexusActorHandler::HandleSetProperty(const TSharedPtr<FJsonObject>& Params)
{
    FString ErrorMsg;
    AActor* Actor = ResolveActor(Params, ErrorMsg);
    if (!Actor) return MakeError(TEXT("ACTOR_NOT_FOUND"), ErrorMsg);

    FString PropertyName = GetStringParam(Params, TEXT("property_name"));
    FString PropertyValue = GetStringParam(Params, TEXT("property_value"));
    if (PropertyName.IsEmpty())
    {
        return MakeError(TEXT("MISSING_PARAM"), TEXT("property_name is required"));
    }

    FProperty* Prop = Actor->GetClass()->FindPropertyByName(FName(*PropertyName));
    if (!Prop)
    {
        return MakeError(TEXT("PROPERTY_NOT_FOUND"),
            FString::Printf(TEXT("No property '%s' on actor class '%s'"),
                *PropertyName, *Actor->GetClass()->GetName()));
    }

    void* ValuePtr = Prop->ContainerPtrToValuePtr<void>(Actor);
    const TCHAR* Result = Prop->ImportText_Direct(*PropertyValue, ValuePtr, Actor, PPF_None);
    if (!Result)
    {
        return MakeError(TEXT("INVALID_VALUE"),
            FString::Printf(TEXT("Failed to parse '%s' for property '%s' (type: %s)"),
                *PropertyValue, *PropertyName, *Prop->GetCPPType()));
    }

    // Notify editor of property change
    FPropertyChangedEvent ChangeEvent(Prop);
    Actor->PostEditChangeProperty(ChangeEvent);

    TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
    Data->SetStringField(TEXT("actor_path"), Actor->GetPathName());
    Data->SetStringField(TEXT("property_name"), PropertyName);
    Data->SetStringField(TEXT("property_value"), PropertyValue);
    Data->SetBoolField(TEXT("success"), true);
    return MakeSuccess(Data);
}

// ─────────────────────────────────────────────────────────────────────────────
// list_all
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FNexusActorHandler::HandleListAll(const TSharedPtr<FJsonObject>& Params)
{
    int32 Limit = (int32)GetNumberParam(Params, TEXT("limit"), 100);
    int32 Offset = (int32)GetNumberParam(Params, TEXT("offset"), 0);

    UWorld* World = GEditor->GetEditorWorldContext().World();
    if (!World) return MakeError(TEXT("NO_WORLD"), TEXT("No editor world"));

    TArray<TSharedPtr<FJsonValue>> ActorsArray;
    int32 TotalCount = 0;
    int32 Skipped = 0;

    for (TActorIterator<AActor> It(World); It; ++It)
    {
        TotalCount++;
        if (Skipped < Offset)
        {
            Skipped++;
            continue;
        }
        if (ActorsArray.Num() < Limit)
        {
            TSharedPtr<FJsonObject> ActorObj = MakeShareable(new FJsonObject());
            ActorObj->SetStringField(TEXT("actor_path"), It->GetPathName());
            ActorObj->SetStringField(TEXT("actor_label"), It->GetActorLabel());
            ActorObj->SetStringField(TEXT("actor_class"), It->GetClass()->GetName());
            ActorObj->SetObjectField(TEXT("location"), VectorToJson(It->GetActorLocation()));
            ActorsArray.Add(MakeShareable(new FJsonValueObject(ActorObj)));
        }
    }

    TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
    Data->SetArrayField(TEXT("actors"), ActorsArray);
    Data->SetNumberField(TEXT("total_count"), TotalCount);
    Data->SetBoolField(TEXT("truncated"), TotalCount > (Offset + Limit));
    return MakeSuccess(Data);
}

// ─────────────────────────────────────────────────────────────────────────────
// get_components
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FNexusActorHandler::HandleGetComponents(const TSharedPtr<FJsonObject>& Params)
{
    FString ErrorMsg;
    AActor* Actor = ResolveActor(Params, ErrorMsg);
    if (!Actor) return MakeError(TEXT("ACTOR_NOT_FOUND"), ErrorMsg);

    TArray<TSharedPtr<FJsonValue>> CompsArray;
    TArray<UActorComponent*> Components;
    Actor->GetComponents(Components);

    for (UActorComponent* Comp : Components)
    {
        TSharedPtr<FJsonObject> CompObj = MakeShareable(new FJsonObject());
        CompObj->SetStringField(TEXT("name"), Comp->GetName());
        CompObj->SetStringField(TEXT("class_name"), Comp->GetClass()->GetName());
        CompObj->SetBoolField(TEXT("is_root"), Comp == Actor->GetRootComponent());
        CompsArray.Add(MakeShareable(new FJsonValueObject(CompObj)));
    }

    TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
    Data->SetArrayField(TEXT("components"), CompsArray);
    Data->SetNumberField(TEXT("count"), CompsArray.Num());
    return MakeSuccess(Data);
}

// ─────────────────────────────────────────────────────────────────────────────
// get_bounds
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FNexusActorHandler::HandleGetBounds(const TSharedPtr<FJsonObject>& Params)
{
    FString ErrorMsg;
    AActor* Actor = ResolveActor(Params, ErrorMsg);
    if (!Actor) return MakeError(TEXT("ACTOR_NOT_FOUND"), ErrorMsg);

    FVector Origin, Extent;
    Actor->GetActorBounds(false, Origin, Extent);

    TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
    Data->SetStringField(TEXT("actor_path"), Actor->GetPathName());
    Data->SetObjectField(TEXT("origin"), VectorToJson(Origin));
    Data->SetObjectField(TEXT("extent"), VectorToJson(Extent));
    return MakeSuccess(Data);
}

// ─────────────────────────────────────────────────────────────────────────────
// set_visibility
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FNexusActorHandler::HandleSetVisibility(const TSharedPtr<FJsonObject>& Params)
{
    FString ErrorMsg;
    AActor* Actor = ResolveActor(Params, ErrorMsg);
    if (!Actor) return MakeError(TEXT("ACTOR_NOT_FOUND"), ErrorMsg);

    bool bVisible = GetBoolParam(Params, TEXT("visible"), true);
    bool bPropagate = GetBoolParam(Params, TEXT("propagate_to_children"), true);

    Actor->SetIsTemporarilyHiddenInEditor(!bVisible);

    if (bPropagate)
    {
        TArray<AActor*> AttachedActors;
        Actor->GetAttachedActors(AttachedActors);
        for (AActor* Child : AttachedActors)
        {
            Child->SetIsTemporarilyHiddenInEditor(!bVisible);
        }
    }

    TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
    Data->SetStringField(TEXT("actor_path"), Actor->GetPathName());
    Data->SetBoolField(TEXT("visible"), bVisible);
    return MakeSuccess(Data);
}

// ─────────────────────────────────────────────────────────────────────────────
// duplicate
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FNexusActorHandler::HandleDuplicate(const TSharedPtr<FJsonObject>& Params)
{
    FString ErrorMsg;
    AActor* Actor = ResolveActor(Params, ErrorMsg);
    if (!Actor) return MakeError(TEXT("ACTOR_NOT_FOUND"), ErrorMsg);

    FString NewLabel = GetStringParam(Params, TEXT("new_label"));
    FVector Offset = GetVectorParam(Params, TEXT("offset"), FVector::ZeroVector);

    // Use editor duplication: select the source actor, duplicate via editor command
    GEditor->SelectNone(false, true);
    GEditor->SelectActor(Actor, true, false, true);
    GEditor->edactDuplicateSelected(Actor->GetLevel(), false);

    // The duplicated actor is now the selected actor
    USelection* Selection = GEditor->GetSelectedActors();
    AActor* DuplicatedActor = nullptr;
    for (int32 i = 0; i < Selection->Num(); ++i)
    {
        AActor* Selected = Cast<AActor>(Selection->GetSelectedObject(i));
        if (Selected && Selected != Actor)
        {
            DuplicatedActor = Selected;
            break;
        }
    }

    if (!DuplicatedActor)
    {
        return MakeError(TEXT("DUPLICATE_FAILED"), TEXT("Failed to duplicate actor"));
    }

    // Apply offset
    if (!Offset.IsZero())
    {
        FVector NewLocation = DuplicatedActor->GetActorLocation() + Offset;
        DuplicatedActor->SetActorLocation(NewLocation);
    }

    // Set label if provided
    if (!NewLabel.IsEmpty())
    {
        DuplicatedActor->SetActorLabel(NewLabel);
    }

    TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
    Data->SetStringField(TEXT("actor_path"), DuplicatedActor->GetPathName());
    Data->SetStringField(TEXT("actor_label"), DuplicatedActor->GetActorLabel());
    Data->SetStringField(TEXT("actor_class"), DuplicatedActor->GetClass()->GetName());
    return MakeSuccess(Data);
}

// ─────────────────────────────────────────────────────────────────────────────
// rename
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FNexusActorHandler::HandleRename(const TSharedPtr<FJsonObject>& Params)
{
    FString ErrorMsg;
    AActor* Actor = ResolveActor(Params, ErrorMsg);
    if (!Actor) return MakeError(TEXT("ACTOR_NOT_FOUND"), ErrorMsg);

    FString NewLabel = GetStringParam(Params, TEXT("new_label"));
    if (NewLabel.IsEmpty())
    {
        return MakeError(TEXT("MISSING_PARAM"), TEXT("new_label is required"));
    }

    Actor->SetActorLabel(NewLabel);

    TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
    Data->SetStringField(TEXT("actor_path"), Actor->GetPathName());
    Data->SetStringField(TEXT("actor_label"), NewLabel);
    return MakeSuccess(Data);
}

// ─────────────────────────────────────────────────────────────────────────────
// add_tag
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FNexusActorHandler::HandleAddTag(const TSharedPtr<FJsonObject>& Params)
{
    FString ErrorMsg;
    AActor* Actor = ResolveActor(Params, ErrorMsg);
    if (!Actor) return MakeError(TEXT("ACTOR_NOT_FOUND"), ErrorMsg);

    FString Tag = GetStringParam(Params, TEXT("tag"));
    if (Tag.IsEmpty())
    {
        return MakeError(TEXT("MISSING_PARAM"), TEXT("tag is required"));
    }

    Actor->Tags.AddUnique(FName(*Tag));

    TArray<TSharedPtr<FJsonValue>> TagsArray;
    for (const FName& T : Actor->Tags)
    {
        TagsArray.Add(MakeShareable(new FJsonValueString(T.ToString())));
    }

    TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
    Data->SetStringField(TEXT("actor_path"), Actor->GetPathName());
    Data->SetArrayField(TEXT("tags"), TagsArray);
    return MakeSuccess(Data);
}

// ─────────────────────────────────────────────────────────────────────────────
// remove_tag
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FNexusActorHandler::HandleRemoveTag(const TSharedPtr<FJsonObject>& Params)
{
    FString ErrorMsg;
    AActor* Actor = ResolveActor(Params, ErrorMsg);
    if (!Actor) return MakeError(TEXT("ACTOR_NOT_FOUND"), ErrorMsg);

    FString Tag = GetStringParam(Params, TEXT("tag"));
    if (Tag.IsEmpty())
    {
        return MakeError(TEXT("MISSING_PARAM"), TEXT("tag is required"));
    }

    Actor->Tags.Remove(FName(*Tag));

    TArray<TSharedPtr<FJsonValue>> TagsArray;
    for (const FName& T : Actor->Tags)
    {
        TagsArray.Add(MakeShareable(new FJsonValueString(T.ToString())));
    }

    TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
    Data->SetStringField(TEXT("actor_path"), Actor->GetPathName());
    Data->SetArrayField(TEXT("tags"), TagsArray);
    return MakeSuccess(Data);
}

// ─────────────────────────────────────────────────────────────────────────────
// get_tags
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FNexusActorHandler::HandleGetTags(const TSharedPtr<FJsonObject>& Params)
{
    FString ErrorMsg;
    AActor* Actor = ResolveActor(Params, ErrorMsg);
    if (!Actor) return MakeError(TEXT("ACTOR_NOT_FOUND"), ErrorMsg);

    TArray<TSharedPtr<FJsonValue>> TagsArray;
    for (const FName& T : Actor->Tags)
    {
        TagsArray.Add(MakeShareable(new FJsonValueString(T.ToString())));
    }

    TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
    Data->SetStringField(TEXT("actor_path"), Actor->GetPathName());
    Data->SetArrayField(TEXT("tags"), TagsArray);
    return MakeSuccess(Data);
}

// ─────────────────────────────────────────────────────────────────────────────
// attach
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FNexusActorHandler::HandleAttach(const TSharedPtr<FJsonObject>& Params)
{
    FString ErrorMsg;
    AActor* Actor = ResolveActor(Params, ErrorMsg);
    if (!Actor) return MakeError(TEXT("ACTOR_NOT_FOUND"), ErrorMsg);

    FString ParentPath = GetStringParam(Params, TEXT("parent_path"));
    if (ParentPath.IsEmpty())
    {
        return MakeError(TEXT("MISSING_PARAM"), TEXT("parent_path is required"));
    }

    AActor* Parent = FindActorByPath(ParentPath);
    if (!Parent)
    {
        // Try finding parent by label as fallback
        Parent = FindActorByLabel(ParentPath);
    }
    if (!Parent)
    {
        return MakeError(TEXT("PARENT_NOT_FOUND"),
            FString::Printf(TEXT("No parent actor found at '%s'"), *ParentPath));
    }

    FString SocketName = GetStringParam(Params, TEXT("socket_name"));
    FAttachmentTransformRules Rules(EAttachmentRule::KeepWorld, true);

    bool bAttached = Actor->AttachToActor(Parent, Rules,
        SocketName.IsEmpty() ? NAME_None : FName(*SocketName));
    if (!bAttached)
    {
        return MakeError(TEXT("ATTACH_FAILED"), TEXT("Failed to attach actor to parent"));
    }

    TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
    Data->SetStringField(TEXT("actor_path"), Actor->GetPathName());
    Data->SetStringField(TEXT("parent_path"), Parent->GetPathName());
    Data->SetStringField(TEXT("socket_name"), SocketName);
    return MakeSuccess(Data);
}

// ─────────────────────────────────────────────────────────────────────────────
// detach
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FNexusActorHandler::HandleDetach(const TSharedPtr<FJsonObject>& Params)
{
    FString ErrorMsg;
    AActor* Actor = ResolveActor(Params, ErrorMsg);
    if (!Actor) return MakeError(TEXT("ACTOR_NOT_FOUND"), ErrorMsg);

    FDetachmentTransformRules Rules(EDetachmentRule::KeepWorld, true);
    Actor->DetachFromActor(Rules);

    TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
    Data->SetStringField(TEXT("actor_path"), Actor->GetPathName());
    Data->SetBoolField(TEXT("detached"), true);
    return MakeSuccess(Data);
}

// ─────────────────────────────────────────────────────────────────────────────
// set_mobility
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FNexusActorHandler::HandleSetMobility(const TSharedPtr<FJsonObject>& Params)
{
    FString ErrorMsg;
    AActor* Actor = ResolveActor(Params, ErrorMsg);
    if (!Actor) return MakeError(TEXT("ACTOR_NOT_FOUND"), ErrorMsg);

    USceneComponent* RootComp = Actor->GetRootComponent();
    if (!RootComp)
    {
        return MakeError(TEXT("NO_ROOT_COMPONENT"),
            TEXT("Actor has no root component to set mobility on"));
    }

    FString Mobility = GetStringParam(Params, TEXT("mobility"));
    EComponentMobility::Type MobilityType;

    if (Mobility == TEXT("Static"))
    {
        MobilityType = EComponentMobility::Static;
    }
    else if (Mobility == TEXT("Stationary"))
    {
        MobilityType = EComponentMobility::Stationary;
    }
    else if (Mobility == TEXT("Movable"))
    {
        MobilityType = EComponentMobility::Movable;
    }
    else
    {
        return MakeError(TEXT("INVALID_MOBILITY"),
            FString::Printf(TEXT("Invalid mobility '%s'. Must be 'Static', 'Stationary', or 'Movable'"),
                *Mobility));
    }

    RootComp->SetMobility(MobilityType);

    TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
    Data->SetStringField(TEXT("actor_path"), Actor->GetPathName());
    Data->SetStringField(TEXT("mobility"), Mobility);
    return MakeSuccess(Data);
}

// ─────────────────────────────────────────────────────────────────────────────
// spawn_batch (A3) — spawn up to 10K actors in one call
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FNexusActorHandler::HandleSpawnBatch(const TSharedPtr<FJsonObject>& Params)
{
    const TArray<TSharedPtr<FJsonValue>>* ActorsArray;
    if (!Params->TryGetArrayField(TEXT("actors"), ActorsArray))
    {
        return MakeError(TEXT("MISSING_PARAM"), TEXT("'actors' array is required"));
    }

    UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
    if (!World)
    {
        return MakeError(TEXT("NO_WORLD"), TEXT("No editor world available"));
    }

    TArray<TSharedPtr<FJsonValue>> ResultActors;
    TArray<TSharedPtr<FJsonValue>> Errors;
    int32 SuccessCount = 0;

    for (int32 i = 0; i < ActorsArray->Num(); i++)
    {
        const TSharedPtr<FJsonObject>* Spec;
        if (!(*ActorsArray)[i]->TryGetObject(Spec) || !Spec)
        {
            TSharedPtr<FJsonObject> Err = MakeShareable(new FJsonObject());
            Err->SetNumberField(TEXT("index"), i);
            Err->SetStringField(TEXT("error"), TEXT("Invalid actor spec (not an object)"));
            Errors.Add(MakeShareable(new FJsonValueObject(Err)));
            continue;
        }

        FString ClassPath = GetStringParam(*Spec, TEXT("class_path"),
            TEXT("/Script/Engine.StaticMeshActor"));
        FString Label = GetStringParam(*Spec, TEXT("label"));
        FVector Location = GetVectorParam(*Spec, TEXT("location"));
        FVector RotVec = GetVectorParam(*Spec, TEXT("rotation"));
        FRotator Rotation(RotVec.X, RotVec.Y, RotVec.Z);
        FVector Scale = GetVectorParam(*Spec, TEXT("scale"), FVector::OneVector);

        // Resolve short class names
        if (!ClassPath.Contains(TEXT("/")))
        {
            ClassPath = FString::Printf(TEXT("/Script/Engine.%s"), *ClassPath);
        }

        UClass* ActorClass = LoadClass<AActor>(nullptr, *ClassPath);
        if (!ActorClass)
        {
            TSharedPtr<FJsonObject> Err = MakeShareable(new FJsonObject());
            Err->SetNumberField(TEXT("index"), i);
            Err->SetStringField(TEXT("error"),
                FString::Printf(TEXT("Class not found: '%s'"), *ClassPath));
            Errors.Add(MakeShareable(new FJsonValueObject(Err)));
            continue;
        }

        FActorSpawnParameters SpawnParams;
        SpawnParams.SpawnCollisionHandlingOverride =
            ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

        FTransform SpawnTransform(Rotation.Quaternion(), Location);
        AActor* NewActor = World->SpawnActor<AActor>(
            ActorClass, SpawnTransform, SpawnParams);
        if (!NewActor)
        {
            TSharedPtr<FJsonObject> Err = MakeShareable(new FJsonObject());
            Err->SetNumberField(TEXT("index"), i);
            Err->SetStringField(TEXT("error"), TEXT("SpawnActor returned null"));
            Errors.Add(MakeShareable(new FJsonValueObject(Err)));
            continue;
        }

        if (!Label.IsEmpty())
        {
            NewActor->SetActorLabel(Label);
        }
        if (Scale != FVector::OneVector)
        {
            NewActor->SetActorScale3D(Scale);
        }

        TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject());
        Result->SetStringField(TEXT("actor_path"), NewActor->GetPathName());
        Result->SetStringField(TEXT("actor_label"), NewActor->GetActorLabel());
        ResultActors.Add(MakeShareable(new FJsonValueObject(Result)));
        SuccessCount++;
    }

    TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
    Data->SetNumberField(TEXT("spawned_count"), SuccessCount);
    Data->SetNumberField(TEXT("failed_count"), ActorsArray->Num() - SuccessCount);
    Data->SetArrayField(TEXT("actors"), ResultActors);
    Data->SetArrayField(TEXT("errors"), Errors);
    return MakeSuccess(Data);
}

// ─────────────────────────────────────────────────────────────────────────────
// set_properties_batch (A3) — set properties on multiple actors at once
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FNexusActorHandler::HandleSetPropertiesBatch(const TSharedPtr<FJsonObject>& Params)
{
    const TArray<TSharedPtr<FJsonValue>>* Operations;
    if (!Params->TryGetArrayField(TEXT("operations"), Operations))
    {
        return MakeError(TEXT("MISSING_PARAM"), TEXT("'operations' array is required"));
    }

    TArray<TSharedPtr<FJsonValue>> Errors;
    int32 SuccessCount = 0;

    for (int32 i = 0; i < Operations->Num(); i++)
    {
        const TSharedPtr<FJsonObject>* Op;
        if (!(*Operations)[i]->TryGetObject(Op) || !Op)
        {
            TSharedPtr<FJsonObject> Err = MakeShareable(new FJsonObject());
            Err->SetNumberField(TEXT("index"), i);
            Err->SetStringField(TEXT("error"), TEXT("Invalid operation (not an object)"));
            Errors.Add(MakeShareable(new FJsonValueObject(Err)));
            continue;
        }

        // Resolve actor
        FString ErrorMsg;
        AActor* Actor = ResolveActor(*Op, ErrorMsg);
        if (!Actor)
        {
            TSharedPtr<FJsonObject> Err = MakeShareable(new FJsonObject());
            Err->SetNumberField(TEXT("index"), i);
            Err->SetStringField(TEXT("error"), ErrorMsg);
            Errors.Add(MakeShareable(new FJsonValueObject(Err)));
            continue;
        }

        // Apply properties
        const TSharedPtr<FJsonObject>* PropsObj;
        if (!(*Op)->TryGetObjectField(TEXT("properties"), PropsObj))
        {
            TSharedPtr<FJsonObject> Err = MakeShareable(new FJsonObject());
            Err->SetNumberField(TEXT("index"), i);
            Err->SetStringField(TEXT("error"), TEXT("Missing 'properties' object"));
            Errors.Add(MakeShareable(new FJsonValueObject(Err)));
            continue;
        }

        bool bAllOk = true;
        for (const auto& Pair : (*PropsObj)->Values)
        {
            FProperty* Prop = Actor->GetClass()->FindPropertyByName(FName(*Pair.Key));
            if (!Prop)
            {
                bAllOk = false;
                continue;
            }

            FString ValueStr;
            if (Pair.Value->TryGetString(ValueStr))
            {
                void* ValuePtr = Prop->ContainerPtrToValuePtr<void>(Actor);
                Prop->ImportText_Direct(*ValueStr, ValuePtr, Actor, PPF_None);
            }
        }

        // Notify editor of changes
        Actor->PostEditChange();

        if (bAllOk)
        {
            SuccessCount++;
        }
        else
        {
            // Partial success — some properties may have been applied
            SuccessCount++;
            TSharedPtr<FJsonObject> Err = MakeShareable(new FJsonObject());
            Err->SetNumberField(TEXT("index"), i);
            Err->SetStringField(TEXT("error"), TEXT("Some properties not found on actor"));
            Errors.Add(MakeShareable(new FJsonValueObject(Err)));
        }
    }

    TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
    Data->SetNumberField(TEXT("updated_count"), SuccessCount);
    Data->SetNumberField(TEXT("failed_count"), Operations->Num() - SuccessCount);
    Data->SetArrayField(TEXT("errors"), Errors);
    return MakeSuccess(Data);
}

// ─────────────────────────────────────────────────────────────────────────────
// delete_batch (A3) — delete multiple actors by paths or wildcard pattern
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FNexusActorHandler::HandleDeleteBatch(const TSharedPtr<FJsonObject>& Params)
{
    UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
    if (!World)
    {
        return MakeError(TEXT("NO_WORLD"), TEXT("No editor world available"));
    }

    int32 DeletedCount = 0;
    int32 NotFoundCount = 0;

    // Mode 1: explicit actor_paths array
    const TArray<TSharedPtr<FJsonValue>>* PathsArray;
    if (Params->TryGetArrayField(TEXT("actor_paths"), PathsArray))
    {
        for (const TSharedPtr<FJsonValue>& PathVal : *PathsArray)
        {
            FString Path;
            if (!PathVal->TryGetString(Path)) continue;

            AActor* Actor = FindActorByPath(Path);
            if (!Actor)
            {
                // Try by label as fallback
                Actor = FindActorByLabel(Path);
            }
            if (Actor)
            {
                Actor->Destroy();
                DeletedCount++;
            }
            else
            {
                NotFoundCount++;
            }
        }
    }

    // Mode 2: wildcard pattern on labels
    FString Pattern = GetStringParam(Params, TEXT("actor_label_pattern"));
    if (!Pattern.IsEmpty())
    {
        TArray<AActor*> ToDelete;
        for (TActorIterator<AActor> It(World); It; ++It)
        {
            if (It->GetActorLabel().MatchesWildcard(Pattern))
            {
                ToDelete.Add(*It);
            }
        }
        for (AActor* Actor : ToDelete)
        {
            Actor->Destroy();
            DeletedCount++;
        }
    }

    TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
    Data->SetNumberField(TEXT("deleted_count"), DeletedCount);
    Data->SetNumberField(TEXT("not_found_count"), NotFoundCount);
    return MakeSuccess(Data);
}

// ─────────────────────────────────────────────────────────────────────────────
// set_transform_batch (A3) — set transforms on multiple actors at once
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FNexusActorHandler::HandleSetTransformBatch(const TSharedPtr<FJsonObject>& Params)
{
    const TArray<TSharedPtr<FJsonValue>>* Transforms;
    if (!Params->TryGetArrayField(TEXT("transforms"), Transforms))
    {
        return MakeError(TEXT("MISSING_PARAM"), TEXT("'transforms' array is required"));
    }

    TArray<TSharedPtr<FJsonValue>> Errors;
    int32 SuccessCount = 0;

    for (int32 i = 0; i < Transforms->Num(); i++)
    {
        const TSharedPtr<FJsonObject>* TransformSpec;
        if (!(*Transforms)[i]->TryGetObject(TransformSpec) || !TransformSpec)
        {
            TSharedPtr<FJsonObject> Err = MakeShareable(new FJsonObject());
            Err->SetNumberField(TEXT("index"), i);
            Err->SetStringField(TEXT("error"), TEXT("Invalid transform spec (not an object)"));
            Errors.Add(MakeShareable(new FJsonValueObject(Err)));
            continue;
        }

        // Resolve actor
        FString ErrorMsg;
        AActor* Actor = ResolveActor(*TransformSpec, ErrorMsg);
        if (!Actor)
        {
            TSharedPtr<FJsonObject> Err = MakeShareable(new FJsonObject());
            Err->SetNumberField(TEXT("index"), i);
            Err->SetStringField(TEXT("error"), ErrorMsg);
            Errors.Add(MakeShareable(new FJsonValueObject(Err)));
            continue;
        }

        // Apply location if present
        const TSharedPtr<FJsonObject>* LocationObj;
        if ((*TransformSpec)->TryGetObjectField(TEXT("location"), LocationObj))
        {
            FVector Loc(
                (*LocationObj)->GetNumberField(TEXT("x")),
                (*LocationObj)->GetNumberField(TEXT("y")),
                (*LocationObj)->GetNumberField(TEXT("z"))
            );
            Actor->SetActorLocation(Loc);
        }

        // Apply rotation if present
        const TSharedPtr<FJsonObject>* RotationObj;
        if ((*TransformSpec)->TryGetObjectField(TEXT("rotation"), RotationObj))
        {
            double Pitch = 0, Yaw = 0, Roll = 0;
            if ((*RotationObj)->TryGetNumberField(TEXT("pitch"), Pitch) ||
                (*RotationObj)->TryGetNumberField(TEXT("yaw"), Yaw) ||
                (*RotationObj)->TryGetNumberField(TEXT("roll"), Roll))
            {
                (*RotationObj)->TryGetNumberField(TEXT("pitch"), Pitch);
                (*RotationObj)->TryGetNumberField(TEXT("yaw"), Yaw);
                (*RotationObj)->TryGetNumberField(TEXT("roll"), Roll);
            }
            else
            {
                Pitch = (*RotationObj)->GetNumberField(TEXT("x"));
                Yaw = (*RotationObj)->GetNumberField(TEXT("y"));
                Roll = (*RotationObj)->GetNumberField(TEXT("z"));
            }
            Actor->SetActorRotation(FRotator(Pitch, Yaw, Roll));
        }

        // Apply scale if present
        const TSharedPtr<FJsonObject>* ScaleObj;
        if ((*TransformSpec)->TryGetObjectField(TEXT("scale"), ScaleObj))
        {
            FVector Scale(
                (*ScaleObj)->GetNumberField(TEXT("x")),
                (*ScaleObj)->GetNumberField(TEXT("y")),
                (*ScaleObj)->GetNumberField(TEXT("z"))
            );
            Actor->SetActorScale3D(Scale);
        }

        SuccessCount++;
    }

    TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
    Data->SetNumberField(TEXT("updated_count"), SuccessCount);
    Data->SetNumberField(TEXT("failed_count"), Transforms->Num() - SuccessCount);
    Data->SetArrayField(TEXT("errors"), Errors);
    return MakeSuccess(Data);
}
