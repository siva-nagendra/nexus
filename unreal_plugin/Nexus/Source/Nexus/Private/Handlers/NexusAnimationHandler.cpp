// Copyright Nexus Team. All Rights Reserved.

#include "Handlers/NexusAnimationHandler.h"
#include "Editor.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "Animation/AnimBlueprint.h"
#include "Animation/AnimBlueprintGeneratedClass.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimMontage.h"
#include "Animation/BlendSpace.h"
#include "Animation/BlendSpace1D.h"
#include "Animation/Skeleton.h"
#include "Animation/AnimNotifies/AnimNotify.h"
#include "Animation/AnimNotifies/AnimNotifyState.h"
#include "Engine/SkeletalMeshSocket.h"
#include "Components/SkeletalMeshComponent.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "Factories/AnimMontageFactory.h"
#include "Factories/BlendSpaceFactoryNew.h"
#include "Factories/BlendSpaceFactory1D.h"

// ─────────────────────────────────────────────────────────────────────────────
// Dispatch
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FNexusAnimationHandler::Handle(
    const FString& CommandType,
    const TSharedPtr<FJsonObject>& Params)
{
    FString SubCommand = CommandType.RightChop(GetNamespace().Len() + 1);

    if (SubCommand == TEXT("get_anim_blueprint_info")) return HandleGetAnimBlueprintInfo(Params);
    if (SubCommand == TEXT("list_anim_sequences"))     return HandleListAnimSequences(Params);
    if (SubCommand == TEXT("get_skeleton_info"))        return HandleGetSkeletonInfo(Params);
    if (SubCommand == TEXT("list_anim_notifies"))       return HandleListAnimNotifies(Params);
    if (SubCommand == TEXT("get_retarget_info"))        return HandleGetRetargetInfo(Params);
    if (SubCommand == TEXT("create_anim_montage"))      return HandleCreateAnimMontage(Params);
    if (SubCommand == TEXT("set_anim_blueprint"))       return HandleSetAnimBlueprint(Params);
    if (SubCommand == TEXT("create_blend_space"))       return HandleCreateBlendSpace(Params);
    if (SubCommand == TEXT("set_ik_settings"))          return HandleSetIkSettings(Params);
    if (SubCommand == TEXT("create_control_rig"))       return HandleCreateControlRig(Params);
    if (SubCommand == TEXT("add_anim_notify"))          return HandleAddAnimNotify(Params);
    if (SubCommand == TEXT("apply_retarget"))           return HandleApplyRetarget(Params);
    if (SubCommand == TEXT("add_blend_space_sample"))   return HandleAddBlendSpaceSample(Params);
    if (SubCommand == TEXT("get_blend_space_samples"))  return HandleGetBlendSpaceSamples(Params);
    if (SubCommand == TEXT("update_blend_space_sample"))return HandleUpdateBlendSpaceSample(Params);
    if (SubCommand == TEXT("remove_blend_space_sample"))return HandleRemoveBlendSpaceSample(Params);

    return MakeError(TEXT("NOT_IMPLEMENTED"),
        FString::Printf(TEXT("Command '%s' not yet implemented"), *CommandType));
}

// ─────────────────────────────────────────────────────────────────────────────
// Read-only queries
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FNexusAnimationHandler::HandleGetAnimBlueprintInfo(
    const TSharedPtr<FJsonObject>& Params)
{
    FString AssetPath = GetStringParam(Params, TEXT("asset_path"));
    if (AssetPath.IsEmpty())
    {
        return MakeError(TEXT("MISSING_PARAM"), TEXT("asset_path is required"));
    }

    UAnimBlueprint* AnimBP = LoadObject<UAnimBlueprint>(nullptr, *AssetPath);
    if (!AnimBP)
    {
        return MakeError(TEXT("NOT_FOUND"),
            FString::Printf(TEXT("Animation Blueprint not found at '%s'"), *AssetPath));
    }

    TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
    Data->SetStringField(TEXT("path"), AnimBP->GetPathName());
    Data->SetStringField(TEXT("name"), AnimBP->GetName());

    // Skeleton info
    USkeleton* Skeleton = AnimBP->TargetSkeleton.Get();
    Data->SetStringField(TEXT("skeleton_path"), Skeleton ? Skeleton->GetPathName() : TEXT(""));

    // Compilation status
    UAnimBlueprintGeneratedClass* GenClass = AnimBP->GetAnimBlueprintGeneratedClass();
    Data->SetBoolField(TEXT("is_compiled"), GenClass != nullptr);
    Data->SetBoolField(TEXT("has_errors"), AnimBP->Status == BS_Error);

    // Parent class
    UClass* ParentClass = AnimBP->ParentClass;
    Data->SetStringField(TEXT("parent_class"),
        ParentClass ? ParentClass->GetName() : TEXT(""));

    return MakeSuccess(Data);
}

TSharedPtr<FJsonObject> FNexusAnimationHandler::HandleListAnimSequences(
    const TSharedPtr<FJsonObject>& Params)
{
    FString SkeletonPath = GetStringParam(Params, TEXT("skeleton_path"));
    FString Folder = GetStringParam(Params, TEXT("folder"), TEXT("/Game"));
    FString NameFilter = GetStringParam(Params, TEXT("name_filter"));
    int32 MaxResults = static_cast<int32>(GetNumberParam(Params, TEXT("max_results"), 100));

    FAssetRegistryModule& AssetRegistryModule =
        FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
    IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();

    FARFilter Filter;
    Filter.ClassPaths.Add(UAnimSequence::StaticClass()->GetClassPathName());
    Filter.PackagePaths.Add(FName(*Folder));
    Filter.bRecursivePaths = true;

    TArray<FAssetData> Assets;
    AssetRegistry.GetAssets(Filter, Assets);

    TArray<TSharedPtr<FJsonValue>> SequenceArray;
    int32 Count = 0;

    for (const FAssetData& AssetData : Assets)
    {
        if (Count >= MaxResults) break;

        FString AssetName = AssetData.AssetName.ToString();
        if (!NameFilter.IsEmpty() && !AssetName.Contains(NameFilter))
        {
            continue;
        }

        // Load the sequence to get detailed info
        UAnimSequence* Seq = Cast<UAnimSequence>(AssetData.GetAsset());
        if (!Seq) continue;

        // Filter by skeleton if specified
        if (!SkeletonPath.IsEmpty())
        {
            USkeleton* Skel = Seq->GetSkeleton();
            if (!Skel || Skel->GetPathName() != SkeletonPath)
            {
                continue;
            }
        }

        TSharedPtr<FJsonObject> SeqObj = MakeShareable(new FJsonObject());
        SeqObj->SetStringField(TEXT("path"), AssetData.GetObjectPathString());
        SeqObj->SetStringField(TEXT("name"), AssetName);
        SeqObj->SetNumberField(TEXT("duration"), Seq->GetPlayLength());
        SeqObj->SetNumberField(TEXT("num_frames"), Seq->GetNumberOfSampledKeys());

        USkeleton* SeqSkel = Seq->GetSkeleton();
        SeqObj->SetStringField(TEXT("skeleton_path"),
            SeqSkel ? SeqSkel->GetPathName() : TEXT(""));

        SequenceArray.Add(MakeShareable(new FJsonValueObject(SeqObj)));
        Count++;
    }

    TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
    Data->SetArrayField(TEXT("sequences"), SequenceArray);
    Data->SetNumberField(TEXT("count"), SequenceArray.Num());
    return MakeSuccess(Data);
}

TSharedPtr<FJsonObject> FNexusAnimationHandler::HandleGetSkeletonInfo(
    const TSharedPtr<FJsonObject>& Params)
{
    FString SkeletonPath = GetStringParam(Params, TEXT("skeleton_path"));
    if (SkeletonPath.IsEmpty())
    {
        return MakeError(TEXT("MISSING_PARAM"), TEXT("skeleton_path is required"));
    }

    bool bIncludeBones = GetBoolParam(Params, TEXT("include_bones"), true);
    bool bIncludeSockets = GetBoolParam(Params, TEXT("include_sockets"), true);

    USkeleton* Skeleton = LoadObject<USkeleton>(nullptr, *SkeletonPath);
    if (!Skeleton)
    {
        return MakeError(TEXT("NOT_FOUND"),
            FString::Printf(TEXT("Skeleton not found at '%s'"), *SkeletonPath));
    }

    const FReferenceSkeleton& RefSkel = Skeleton->GetReferenceSkeleton();

    TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
    Data->SetStringField(TEXT("path"), Skeleton->GetPathName());
    Data->SetStringField(TEXT("name"), Skeleton->GetName());
    Data->SetNumberField(TEXT("bone_count"), RefSkel.GetNum());

    if (bIncludeBones)
    {
        TArray<TSharedPtr<FJsonValue>> BoneArray;
        for (int32 i = 0; i < RefSkel.GetNum(); i++)
        {
            TSharedPtr<FJsonObject> BoneObj = MakeShareable(new FJsonObject());
            BoneObj->SetStringField(TEXT("name"), RefSkel.GetBoneName(i).ToString());
            BoneObj->SetNumberField(TEXT("index"), i);
            BoneObj->SetNumberField(TEXT("parent_index"), RefSkel.GetParentIndex(i));
            BoneArray.Add(MakeShareable(new FJsonValueObject(BoneObj)));
        }
        Data->SetArrayField(TEXT("bones"), BoneArray);
    }

    if (bIncludeSockets)
    {
        TArray<TSharedPtr<FJsonValue>> SocketArray;
        for (USkeletalMeshSocket* Socket : Skeleton->Sockets)
        {
            if (!Socket) continue;
            TSharedPtr<FJsonObject> SocketObj = MakeShareable(new FJsonObject());
            SocketObj->SetStringField(TEXT("name"), Socket->SocketName.ToString());
            SocketObj->SetStringField(TEXT("bone_name"), Socket->BoneName.ToString());
            SocketObj->SetObjectField(TEXT("relative_location"),
                VectorToJson(FVector(Socket->RelativeLocation)));
            SocketObj->SetObjectField(TEXT("relative_rotation"),
                RotatorToJson(FRotator(Socket->RelativeRotation)));
            SocketObj->SetObjectField(TEXT("relative_scale"),
                VectorToJson(Socket->RelativeScale));
            SocketArray.Add(MakeShareable(new FJsonValueObject(SocketObj)));
        }
        Data->SetArrayField(TEXT("sockets"), SocketArray);
    }

    return MakeSuccess(Data);
}

TSharedPtr<FJsonObject> FNexusAnimationHandler::HandleListAnimNotifies(
    const TSharedPtr<FJsonObject>& Params)
{
    FString AnimPath = GetStringParam(Params, TEXT("anim_sequence_path"));
    if (AnimPath.IsEmpty())
    {
        return MakeError(TEXT("MISSING_PARAM"), TEXT("anim_sequence_path is required"));
    }

    UAnimSequenceBase* Seq = LoadObject<UAnimSequenceBase>(nullptr, *AnimPath);
    if (!Seq)
    {
        return MakeError(TEXT("NOT_FOUND"),
            FString::Printf(TEXT("Animation asset not found at '%s'"), *AnimPath));
    }

    TArray<TSharedPtr<FJsonValue>> NotifyArray;
    for (const FAnimNotifyEvent& Notify : Seq->Notifies)
    {
        TSharedPtr<FJsonObject> NotifyObj = MakeShareable(new FJsonObject());
        NotifyObj->SetStringField(TEXT("name"), Notify.NotifyName.ToString());
        NotifyObj->SetNumberField(TEXT("trigger_time"), Notify.GetTriggerTime());
        NotifyObj->SetNumberField(TEXT("duration"), Notify.GetDuration());

        // Notify class info
        if (Notify.Notify)
        {
            NotifyObj->SetStringField(TEXT("notify_class"),
                Notify.Notify->GetClass()->GetName());
        }
        else if (Notify.NotifyStateClass)
        {
            NotifyObj->SetStringField(TEXT("notify_class"),
                Notify.NotifyStateClass->GetClass()->GetName());
        }
        else
        {
            NotifyObj->SetStringField(TEXT("notify_class"), TEXT(""));
        }

        NotifyArray.Add(MakeShareable(new FJsonValueObject(NotifyObj)));
    }

    TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
    Data->SetArrayField(TEXT("notifies"), NotifyArray);
    Data->SetNumberField(TEXT("count"), NotifyArray.Num());
    return MakeSuccess(Data);
}

TSharedPtr<FJsonObject> FNexusAnimationHandler::HandleGetRetargetInfo(
    const TSharedPtr<FJsonObject>& Params)
{
    FString SourcePath = GetStringParam(Params, TEXT("source_skeleton_path"));
    if (SourcePath.IsEmpty())
    {
        return MakeError(TEXT("MISSING_PARAM"), TEXT("source_skeleton_path is required"));
    }

    FString TargetPath = GetStringParam(Params, TEXT("target_skeleton_path"));

    USkeleton* SourceSkel = LoadObject<USkeleton>(nullptr, *SourcePath);
    if (!SourceSkel)
    {
        return MakeError(TEXT("NOT_FOUND"),
            FString::Printf(TEXT("Source skeleton not found at '%s'"), *SourcePath));
    }

    TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
    Data->SetStringField(TEXT("source_skeleton"), SourceSkel->GetPathName());
    Data->SetStringField(TEXT("source_name"), SourceSkel->GetName());

    const FReferenceSkeleton& SourceRef = SourceSkel->GetReferenceSkeleton();
    Data->SetNumberField(TEXT("source_bone_count"), SourceRef.GetNum());

    // List source bone names
    TArray<TSharedPtr<FJsonValue>> SourceBones;
    for (int32 i = 0; i < SourceRef.GetNum(); i++)
    {
        SourceBones.Add(MakeShareable(
            new FJsonValueString(SourceRef.GetBoneName(i).ToString())));
    }
    Data->SetArrayField(TEXT("source_bones"), SourceBones);

    if (!TargetPath.IsEmpty())
    {
        USkeleton* TargetSkel = LoadObject<USkeleton>(nullptr, *TargetPath);
        if (!TargetSkel)
        {
            return MakeError(TEXT("NOT_FOUND"),
                FString::Printf(TEXT("Target skeleton not found at '%s'"), *TargetPath));
        }

        Data->SetStringField(TEXT("target_skeleton"), TargetSkel->GetPathName());
        Data->SetStringField(TEXT("target_name"), TargetSkel->GetName());

        const FReferenceSkeleton& TargetRef = TargetSkel->GetReferenceSkeleton();
        Data->SetNumberField(TEXT("target_bone_count"), TargetRef.GetNum());

        // Build basic bone mapping by matching names
        TSharedPtr<FJsonObject> BoneMapping = MakeShareable(new FJsonObject());
        int32 MappedCount = 0;
        for (int32 i = 0; i < SourceRef.GetNum(); i++)
        {
            FName BoneName = SourceRef.GetBoneName(i);
            int32 TargetIdx = TargetRef.FindBoneIndex(BoneName);
            if (TargetIdx != INDEX_NONE)
            {
                BoneMapping->SetNumberField(BoneName.ToString(), TargetIdx);
                MappedCount++;
            }
        }
        Data->SetObjectField(TEXT("bone_mapping"), BoneMapping);
        Data->SetNumberField(TEXT("mapped_bone_count"), MappedCount);
    }

    return MakeSuccess(Data);
}

// ─────────────────────────────────────────────────────────────────────────────
// Mutation commands
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FNexusAnimationHandler::HandleCreateAnimMontage(
    const TSharedPtr<FJsonObject>& Params)
{
    FString SeqPath = GetStringParam(Params, TEXT("anim_sequence_path"));
    if (SeqPath.IsEmpty())
    {
        return MakeError(TEXT("MISSING_PARAM"), TEXT("anim_sequence_path is required"));
    }

    UAnimSequence* SourceSeq = LoadObject<UAnimSequence>(nullptr, *SeqPath);
    if (!SourceSeq)
    {
        return MakeError(TEXT("NOT_FOUND"),
            FString::Printf(TEXT("Animation Sequence not found at '%s'"), *SeqPath));
    }

    FString MontageName = GetStringParam(Params, TEXT("montage_name"));
    if (MontageName.IsEmpty())
    {
        MontageName = SourceSeq->GetName() + TEXT("_Montage");
    }

    FString DestFolder = GetStringParam(Params, TEXT("destination_folder"));
    if (DestFolder.IsEmpty())
    {
        DestFolder = FPackageName::GetLongPackagePath(SourceSeq->GetOutermost()->GetName());
    }

    FString SlotName = GetStringParam(Params, TEXT("slot_name"), TEXT("DefaultSlot"));

    // Create montage via factory
    UAnimMontageFactory* Factory = NewObject<UAnimMontageFactory>();
    Factory->SourceAnimation = SourceSeq;

    IAssetTools& AssetTools =
        FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
    UObject* NewAsset = AssetTools.CreateAsset(
        MontageName, DestFolder, UAnimMontage::StaticClass(), Factory);

    if (!NewAsset)
    {
        return MakeError(TEXT("CREATE_FAILED"), TEXT("Failed to create Animation Montage"));
    }

    UAnimMontage* Montage = Cast<UAnimMontage>(NewAsset);

    // Set slot name on first slot if available
    if (Montage && Montage->SlotAnimTracks.Num() > 0)
    {
        Montage->SlotAnimTracks[0].SlotName = FName(*SlotName);
        Montage->MarkPackageDirty();
    }

    TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
    Data->SetStringField(TEXT("path"), Montage->GetPathName());
    Data->SetStringField(TEXT("name"), Montage->GetName());
    Data->SetStringField(TEXT("slot_name"), SlotName);
    Data->SetNumberField(TEXT("duration"), Montage->GetPlayLength());
    return MakeSuccess(Data);
}

TSharedPtr<FJsonObject> FNexusAnimationHandler::HandleSetAnimBlueprint(
    const TSharedPtr<FJsonObject>& Params)
{
    FString ActorPath = GetStringParam(Params, TEXT("mesh_actor_path"));
    if (ActorPath.IsEmpty())
    {
        return MakeError(TEXT("MISSING_PARAM"), TEXT("mesh_actor_path is required"));
    }

    FString AnimBPPath = GetStringParam(Params, TEXT("anim_blueprint_path"));
    if (AnimBPPath.IsEmpty())
    {
        return MakeError(TEXT("MISSING_PARAM"), TEXT("anim_blueprint_path is required"));
    }

    // Find the actor — try by path first, then by label
    UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
    if (!World)
    {
        return MakeError(TEXT("NO_WORLD"), TEXT("No editor world available"));
    }

    AActor* Actor = nullptr;

    // Try full object path
    UObject* Obj = StaticFindObject(AActor::StaticClass(), nullptr, *ActorPath);
    Actor = Cast<AActor>(Obj);

    // Fallback: search by label
    if (!Actor)
    {
        for (TActorIterator<AActor> It(World); It; ++It)
        {
            if (It->GetActorLabel() == ActorPath)
            {
                Actor = *It;
                break;
            }
        }
    }

    if (!Actor)
    {
        return MakeError(TEXT("NOT_FOUND"),
            FString::Printf(TEXT("Actor not found: '%s'"), *ActorPath));
    }

    USkeletalMeshComponent* SkelComp =
        Actor->FindComponentByClass<USkeletalMeshComponent>();
    if (!SkelComp)
    {
        return MakeError(TEXT("NO_SKELETAL_MESH"),
            TEXT("Actor does not have a SkeletalMeshComponent"));
    }

    UAnimBlueprint* AnimBP = LoadObject<UAnimBlueprint>(nullptr, *AnimBPPath);
    if (!AnimBP)
    {
        return MakeError(TEXT("NOT_FOUND"),
            FString::Printf(TEXT("Animation Blueprint not found at '%s'"), *AnimBPPath));
    }

    UAnimBlueprintGeneratedClass* AnimClass = AnimBP->GetAnimBlueprintGeneratedClass();
    if (!AnimClass)
    {
        return MakeError(TEXT("NOT_COMPILED"),
            TEXT("Animation Blueprint is not compiled"));
    }

    SkelComp->SetAnimInstanceClass(AnimClass);
    Actor->MarkPackageDirty();

    TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
    Data->SetStringField(TEXT("actor_path"), Actor->GetPathName());
    Data->SetStringField(TEXT("anim_blueprint_path"), AnimBP->GetPathName());
    Data->SetBoolField(TEXT("success"), true);
    return MakeSuccess(Data);
}

TSharedPtr<FJsonObject> FNexusAnimationHandler::HandleCreateBlendSpace(
    const TSharedPtr<FJsonObject>& Params)
{
    FString SkeletonPath = GetStringParam(Params, TEXT("skeleton_path"));
    if (SkeletonPath.IsEmpty())
    {
        return MakeError(TEXT("MISSING_PARAM"), TEXT("skeleton_path is required"));
    }

    FString BSName = GetStringParam(Params, TEXT("blend_space_name"));
    if (BSName.IsEmpty())
    {
        return MakeError(TEXT("MISSING_PARAM"), TEXT("blend_space_name is required"));
    }

    FString DestFolder = GetStringParam(Params, TEXT("destination_folder"));
    if (DestFolder.IsEmpty())
    {
        return MakeError(TEXT("MISSING_PARAM"), TEXT("destination_folder is required"));
    }

    bool bIs1D = GetBoolParam(Params, TEXT("is_1d"), false);
    FString AxisXName = GetStringParam(Params, TEXT("axis_x_name"), TEXT("Speed"));
    double AxisXMin = GetNumberParam(Params, TEXT("axis_x_min"), 0.0);
    double AxisXMax = GetNumberParam(Params, TEXT("axis_x_max"), 600.0);
    FString AxisYName = GetStringParam(Params, TEXT("axis_y_name"), TEXT("Direction"));
    double AxisYMin = GetNumberParam(Params, TEXT("axis_y_min"), -180.0);
    double AxisYMax = GetNumberParam(Params, TEXT("axis_y_max"), 180.0);

    USkeleton* Skeleton = LoadObject<USkeleton>(nullptr, *SkeletonPath);
    if (!Skeleton)
    {
        return MakeError(TEXT("NOT_FOUND"),
            FString::Printf(TEXT("Skeleton not found at '%s'"), *SkeletonPath));
    }

    IAssetTools& AssetTools =
        FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();

    UObject* NewAsset = nullptr;

    if (bIs1D)
    {
        UBlendSpaceFactory1D* Factory = NewObject<UBlendSpaceFactory1D>();
        Factory->TargetSkeleton = Skeleton;
        NewAsset = AssetTools.CreateAsset(
            BSName, DestFolder, UBlendSpace1D::StaticClass(), Factory);
    }
    else
    {
        UBlendSpaceFactoryNew* Factory = NewObject<UBlendSpaceFactoryNew>();
        Factory->TargetSkeleton = Skeleton;
        NewAsset = AssetTools.CreateAsset(
            BSName, DestFolder, UBlendSpace::StaticClass(), Factory);
    }

    if (!NewAsset)
    {
        return MakeError(TEXT("CREATE_FAILED"), TEXT("Failed to create Blend Space"));
    }

    // Configure axes via property reflection (BlendParameters is protected)
    auto SetBlendParameter = [](UBlendSpace* BS, int32 Index, const FString& Name, float Min, float Max)
    {
        if (!BS) return;

        // Use property system to modify protected BlendParameters array
        FProperty* Prop = UBlendSpace::StaticClass()->FindPropertyByName(TEXT("BlendParameters"));
        if (Prop)
        {
            FBlendParameter* Params = Prop->ContainerPtrToValuePtr<FBlendParameter>(BS);
            if (Params && Index >= 0 && Index < 3)
            {
                Params[Index].DisplayName = Name;
                Params[Index].Min = Min;
                Params[Index].Max = Max;
            }
        }
    };

    UBlendSpace* BS2D = Cast<UBlendSpace>(NewAsset);
    UBlendSpace1D* BS1D = Cast<UBlendSpace1D>(NewAsset);

    if (BS2D)
    {
        SetBlendParameter(BS2D, 0, AxisXName, AxisXMin, AxisXMax);
        SetBlendParameter(BS2D, 1, AxisYName, AxisYMin, AxisYMax);
        BS2D->MarkPackageDirty();
    }
    else if (BS1D)
    {
        SetBlendParameter(BS1D, 0, AxisXName, AxisXMin, AxisXMax);
        BS1D->MarkPackageDirty();
    }

    TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
    Data->SetStringField(TEXT("path"), NewAsset->GetPathName());
    Data->SetStringField(TEXT("name"), NewAsset->GetName());
    Data->SetBoolField(TEXT("is_1d"), bIs1D);

    TSharedPtr<FJsonObject> AxisX = MakeShareable(new FJsonObject());
    AxisX->SetStringField(TEXT("name"), AxisXName);
    AxisX->SetNumberField(TEXT("min"), AxisXMin);
    AxisX->SetNumberField(TEXT("max"), AxisXMax);
    Data->SetObjectField(TEXT("axis_x"), AxisX);

    if (!bIs1D)
    {
        TSharedPtr<FJsonObject> AxisY = MakeShareable(new FJsonObject());
        AxisY->SetStringField(TEXT("name"), AxisYName);
        AxisY->SetNumberField(TEXT("min"), AxisYMin);
        AxisY->SetNumberField(TEXT("max"), AxisYMax);
        Data->SetObjectField(TEXT("axis_y"), AxisY);
    }

    return MakeSuccess(Data);
}

TSharedPtr<FJsonObject> FNexusAnimationHandler::HandleAddAnimNotify(
    const TSharedPtr<FJsonObject>& Params)
{
    FString AnimPath = GetStringParam(Params, TEXT("anim_sequence_path"));
    if (AnimPath.IsEmpty())
    {
        return MakeError(TEXT("MISSING_PARAM"), TEXT("anim_sequence_path is required"));
    }

    FString NotifyName = GetStringParam(Params, TEXT("notify_name"));
    if (NotifyName.IsEmpty())
    {
        return MakeError(TEXT("MISSING_PARAM"), TEXT("notify_name is required"));
    }

    double TriggerTime = GetNumberParam(Params, TEXT("trigger_time"), 0.0);
    FString NotifyClass = GetStringParam(Params, TEXT("notify_class"), TEXT("AnimNotify"));
    double Duration = GetNumberParam(Params, TEXT("duration"), 0.0);

    UAnimSequenceBase* Seq = LoadObject<UAnimSequenceBase>(nullptr, *AnimPath);
    if (!Seq)
    {
        return MakeError(TEXT("NOT_FOUND"),
            FString::Printf(TEXT("Animation asset not found at '%s'"), *AnimPath));
    }

    // Validate trigger time
    if (TriggerTime < 0.0 || TriggerTime > Seq->GetPlayLength())
    {
        return MakeError(TEXT("INVALID_TIME"),
            FString::Printf(TEXT("trigger_time %.3f is outside sequence duration %.3f"),
                TriggerTime, Seq->GetPlayLength()));
    }

    // Add a new notify event
    FAnimNotifyEvent& NewNotify = Seq->Notifies.AddDefaulted_GetRef();
    NewNotify.NotifyName = FName(*NotifyName);

    // Create the notify object
    if (Duration > 0.0)
    {
        // Notify state (spans a duration)
        UAnimNotifyState* NotifyState = NewObject<UAnimNotifyState>(
            Seq, UAnimNotifyState::StaticClass(), FName(*NotifyName));
        NewNotify.NotifyStateClass = NotifyState;
        NewNotify.SetDuration(Duration);
    }
    else
    {
        // Instant notify
        UAnimNotify* Notify = NewObject<UAnimNotify>(
            Seq, UAnimNotify::StaticClass(), FName(*NotifyName));
        NewNotify.Notify = Notify;
    }

    NewNotify.SetTime(TriggerTime);
    NewNotify.TriggerTimeOffset = GetTriggerTimeOffsetForType(
        Seq->CalculateOffsetForNotify(TriggerTime));

    Seq->MarkPackageDirty();
    Seq->RefreshCacheData();

    TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
    Data->SetStringField(TEXT("anim_path"), Seq->GetPathName());
    Data->SetStringField(TEXT("notify_name"), NotifyName);
    Data->SetNumberField(TEXT("trigger_time"), TriggerTime);
    Data->SetNumberField(TEXT("duration"), Duration);
    Data->SetNumberField(TEXT("total_notifies"), Seq->Notifies.Num());
    return MakeSuccess(Data);
}

// ─────────────────────────────────────────────────────────────────────────────
// Complex commands — deferred to codegen fallback
// These provide basic stub implementations or simple approaches.
// Full IK/ControlRig/Retarget logic can be handled via Python codegen.
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FNexusAnimationHandler::HandleSetIkSettings(
    const TSharedPtr<FJsonObject>& Params)
{
    FString AnimBPPath = GetStringParam(Params, TEXT("anim_blueprint_path"));
    if (AnimBPPath.IsEmpty())
    {
        return MakeError(TEXT("MISSING_PARAM"), TEXT("anim_blueprint_path is required"));
    }

    FString IkType = GetStringParam(Params, TEXT("ik_type"), TEXT("CCDIK"));
    FString EffectorBone = GetStringParam(Params, TEXT("effector_bone"));
    FString RootBone = GetStringParam(Params, TEXT("root_bone"));
    double Precision = GetNumberParam(Params, TEXT("precision"), 1.0);
    int32 MaxIterations = static_cast<int32>(GetNumberParam(Params, TEXT("max_iterations"), 10));
    bool bEnable = GetBoolParam(Params, TEXT("enable"), true);

    UAnimBlueprint* AnimBP = LoadObject<UAnimBlueprint>(nullptr, *AnimBPPath);
    if (!AnimBP)
    {
        return MakeError(TEXT("NOT_FOUND"),
            FString::Printf(TEXT("Animation Blueprint not found at '%s'"), *AnimBPPath));
    }

    // IK configuration requires modifying anim graph nodes which is complex.
    // Return the validated parameters so the Python codegen layer can handle
    // the actual node manipulation via UE Python scripting.
    TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
    Data->SetStringField(TEXT("anim_blueprint_path"), AnimBP->GetPathName());
    Data->SetStringField(TEXT("ik_type"), IkType);
    Data->SetStringField(TEXT("effector_bone"), EffectorBone);
    Data->SetStringField(TEXT("root_bone"), RootBone);
    Data->SetNumberField(TEXT("precision"), Precision);
    Data->SetNumberField(TEXT("max_iterations"), MaxIterations);
    Data->SetBoolField(TEXT("enable"), bEnable);
    Data->SetStringField(TEXT("note"),
        TEXT("IK settings validated. Use codegen for full anim graph node manipulation."));
    return MakeSuccess(Data);
}

TSharedPtr<FJsonObject> FNexusAnimationHandler::HandleCreateControlRig(
    const TSharedPtr<FJsonObject>& Params)
{
    FString MeshPath = GetStringParam(Params, TEXT("skeletal_mesh_path"));
    if (MeshPath.IsEmpty())
    {
        return MakeError(TEXT("MISSING_PARAM"), TEXT("skeletal_mesh_path is required"));
    }

    FString RigName = GetStringParam(Params, TEXT("control_rig_name"));
    FString DestFolder = GetStringParam(Params, TEXT("destination_folder"));

    USkeletalMesh* SkelMesh = LoadObject<USkeletalMesh>(nullptr, *MeshPath);
    if (!SkelMesh)
    {
        return MakeError(TEXT("NOT_FOUND"),
            FString::Printf(TEXT("Skeletal Mesh not found at '%s'"), *MeshPath));
    }

    if (RigName.IsEmpty())
    {
        RigName = SkelMesh->GetName() + TEXT("_CtrlRig");
    }

    if (DestFolder.IsEmpty())
    {
        DestFolder = FPackageName::GetLongPackagePath(SkelMesh->GetOutermost()->GetName());
    }

    // Control Rig creation is complex and requires the ControlRig plugin module.
    // Validate inputs and return info for codegen to handle the actual creation.
    TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
    Data->SetStringField(TEXT("skeletal_mesh_path"), SkelMesh->GetPathName());
    Data->SetStringField(TEXT("control_rig_name"), RigName);
    Data->SetStringField(TEXT("destination_folder"), DestFolder);

    USkeleton* Skel = SkelMesh->GetSkeleton();
    Data->SetStringField(TEXT("skeleton_path"), Skel ? Skel->GetPathName() : TEXT(""));
    Data->SetNumberField(TEXT("bone_count"),
        Skel ? Skel->GetReferenceSkeleton().GetNum() : 0);
    Data->SetStringField(TEXT("note"),
        TEXT("Control Rig parameters validated. Use codegen for full ControlRig Blueprint creation."));
    return MakeSuccess(Data);
}

TSharedPtr<FJsonObject> FNexusAnimationHandler::HandleApplyRetarget(
    const TSharedPtr<FJsonObject>& Params)
{
    FString SourceAnimPath = GetStringParam(Params, TEXT("source_anim_path"));
    if (SourceAnimPath.IsEmpty())
    {
        return MakeError(TEXT("MISSING_PARAM"), TEXT("source_anim_path is required"));
    }

    FString SourceSkelPath = GetStringParam(Params, TEXT("source_skeleton_path"));
    if (SourceSkelPath.IsEmpty())
    {
        return MakeError(TEXT("MISSING_PARAM"), TEXT("source_skeleton_path is required"));
    }

    FString TargetSkelPath = GetStringParam(Params, TEXT("target_skeleton_path"));
    if (TargetSkelPath.IsEmpty())
    {
        return MakeError(TEXT("MISSING_PARAM"), TEXT("target_skeleton_path is required"));
    }

    FString DestFolder = GetStringParam(Params, TEXT("destination_folder"));
    bool bDuplicate = GetBoolParam(Params, TEXT("duplicate_and_retarget"), true);

    // Validate assets exist
    UAnimSequence* SourceAnim = LoadObject<UAnimSequence>(nullptr, *SourceAnimPath);
    if (!SourceAnim)
    {
        return MakeError(TEXT("NOT_FOUND"),
            FString::Printf(TEXT("Source animation not found at '%s'"), *SourceAnimPath));
    }

    USkeleton* SourceSkel = LoadObject<USkeleton>(nullptr, *SourceSkelPath);
    if (!SourceSkel)
    {
        return MakeError(TEXT("NOT_FOUND"),
            FString::Printf(TEXT("Source skeleton not found at '%s'"), *SourceSkelPath));
    }

    USkeleton* TargetSkel = LoadObject<USkeleton>(nullptr, *TargetSkelPath);
    if (!TargetSkel)
    {
        return MakeError(TEXT("NOT_FOUND"),
            FString::Printf(TEXT("Target skeleton not found at '%s'"), *TargetSkelPath));
    }

    if (DestFolder.IsEmpty())
    {
        DestFolder = FPackageName::GetLongPackagePath(SourceAnim->GetOutermost()->GetName());
    }

    // Retargeting is complex and requires IKRetargeter assets.
    // Validate inputs and return info for codegen to handle.
    TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
    Data->SetStringField(TEXT("source_anim_path"), SourceAnim->GetPathName());
    Data->SetStringField(TEXT("source_skeleton_path"), SourceSkel->GetPathName());
    Data->SetStringField(TEXT("target_skeleton_path"), TargetSkel->GetPathName());
    Data->SetStringField(TEXT("destination_folder"), DestFolder);
    Data->SetBoolField(TEXT("duplicate_and_retarget"), bDuplicate);
    Data->SetStringField(TEXT("note"),
        TEXT("Retarget parameters validated. Use codegen with IKRetargeter for full retargeting."));
    return MakeSuccess(Data);
}

// ─────────────────────────────────────────────────────────────────────────────
// Blend Space sample CRUD
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FNexusAnimationHandler::HandleAddBlendSpaceSample(
    const TSharedPtr<FJsonObject>& Params)
{
    FString AssetPath = GetStringParam(Params, TEXT("asset_path"));
    if (AssetPath.IsEmpty())
    {
        return MakeError(TEXT("MISSING_PARAM"), TEXT("asset_path is required"));
    }

    FString AnimPath = GetStringParam(Params, TEXT("anim_sequence_path"));
    if (AnimPath.IsEmpty())
    {
        return MakeError(TEXT("MISSING_PARAM"), TEXT("anim_sequence_path is required"));
    }

    double X = GetNumberParam(Params, TEXT("x"), 0.0);
    double Y = GetNumberParam(Params, TEXT("y"), 0.0);

    // UE 5.7: UBlendSpace is the base for both 1D and 2D blend spaces
    UBlendSpace* BS = LoadObject<UBlendSpace>(nullptr, *AssetPath);
    if (!BS)
    {
        return MakeError(TEXT("NOT_FOUND"),
            FString::Printf(TEXT("Blend Space not found at '%s'"), *AssetPath));
    }

    UAnimSequence* Anim = LoadObject<UAnimSequence>(nullptr, *AnimPath);
    if (!Anim)
    {
        return MakeError(TEXT("NOT_FOUND"),
            FString::Printf(TEXT("Anim Sequence not found at '%s'"), *AnimPath));
    }

    BS->PreEditChange(nullptr);

    // UE 5.7: AddSample returns the index of the new sample
    int32 NewIndex = BS->AddSample(Anim, FVector(X, Y, 0.0));

    BS->ValidateSampleData();
    BS->PostEditChange();
    BS->MarkPackageDirty();

    TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
    Data->SetStringField(TEXT("asset_path"), BS->GetPathName());
    Data->SetNumberField(TEXT("sample_index"), NewIndex);
    Data->SetStringField(TEXT("animation"), Anim->GetPathName());
    Data->SetNumberField(TEXT("x"), X);
    Data->SetNumberField(TEXT("y"), Y);
    Data->SetNumberField(TEXT("total_samples"), BS->GetBlendSamples().Num());
    return MakeSuccess(Data);
}

TSharedPtr<FJsonObject> FNexusAnimationHandler::HandleGetBlendSpaceSamples(
    const TSharedPtr<FJsonObject>& Params)
{
    FString AssetPath = GetStringParam(Params, TEXT("asset_path"));
    if (AssetPath.IsEmpty())
    {
        return MakeError(TEXT("MISSING_PARAM"), TEXT("asset_path is required"));
    }

    UBlendSpace* BS = LoadObject<UBlendSpace>(nullptr, *AssetPath);
    if (!BS)
    {
        return MakeError(TEXT("NOT_FOUND"),
            FString::Printf(TEXT("Blend Space not found at '%s'"), *AssetPath));
    }

    const TArray<FBlendSample>& Samples = BS->GetBlendSamples();

    TArray<TSharedPtr<FJsonValue>> SampleArray;
    for (int32 i = 0; i < Samples.Num(); i++)
    {
        const FBlendSample& Sample = Samples[i];
        TSharedPtr<FJsonObject> SampleObj = MakeShareable(new FJsonObject());
        SampleObj->SetNumberField(TEXT("index"), i);
        SampleObj->SetStringField(TEXT("animation"),
            Sample.Animation ? Sample.Animation->GetPathName() : TEXT(""));
        SampleObj->SetStringField(TEXT("animation_name"),
            Sample.Animation ? Sample.Animation->GetName() : TEXT(""));
        SampleObj->SetNumberField(TEXT("x"), Sample.SampleValue.X);
        SampleObj->SetNumberField(TEXT("y"), Sample.SampleValue.Y);
        SampleObj->SetNumberField(TEXT("rate_scale"), Sample.RateScale);
        SampleArray.Add(MakeShareable(new FJsonValueObject(SampleObj)));
    }

    TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
    Data->SetStringField(TEXT("asset_path"), BS->GetPathName());
    Data->SetArrayField(TEXT("samples"), SampleArray);
    Data->SetNumberField(TEXT("count"), Samples.Num());
    return MakeSuccess(Data);
}

TSharedPtr<FJsonObject> FNexusAnimationHandler::HandleUpdateBlendSpaceSample(
    const TSharedPtr<FJsonObject>& Params)
{
    FString AssetPath = GetStringParam(Params, TEXT("asset_path"));
    if (AssetPath.IsEmpty())
    {
        return MakeError(TEXT("MISSING_PARAM"), TEXT("asset_path is required"));
    }

    int32 SampleIndex = static_cast<int32>(GetNumberParam(Params, TEXT("sample_index"), -1));
    if (SampleIndex < 0)
    {
        return MakeError(TEXT("MISSING_PARAM"), TEXT("sample_index is required and must be >= 0"));
    }

    UBlendSpace* BS = LoadObject<UBlendSpace>(nullptr, *AssetPath);
    if (!BS)
    {
        return MakeError(TEXT("NOT_FOUND"),
            FString::Printf(TEXT("Blend Space not found at '%s'"), *AssetPath));
    }

    if (SampleIndex >= BS->GetBlendSamples().Num())
    {
        return MakeError(TEXT("INDEX_OUT_OF_RANGE"),
            FString::Printf(TEXT("Sample index %d out of range (0..%d)"),
                SampleIndex, BS->GetBlendSamples().Num() - 1));
    }

    BS->PreEditChange(nullptr);

    // Update position if x/y provided
    bool bHasX = Params->HasField(TEXT("x"));
    bool bHasY = Params->HasField(TEXT("y"));
    if (bHasX || bHasY)
    {
        const FBlendSample& Current = BS->GetBlendSamples()[SampleIndex];
        double NewX = bHasX ? GetNumberParam(Params, TEXT("x"), 0.0) : Current.SampleValue.X;
        double NewY = bHasY ? GetNumberParam(Params, TEXT("y"), 0.0) : Current.SampleValue.Y;
        // UE 5.7: EditSampleValue updates the position of a sample
        BS->EditSampleValue(SampleIndex, FVector(NewX, NewY, 0.0));
    }

    // Update animation if provided
    FString NewAnimPath = GetStringParam(Params, TEXT("anim_sequence_path"));
    if (!NewAnimPath.IsEmpty())
    {
        UAnimSequence* NewAnim = LoadObject<UAnimSequence>(nullptr, *NewAnimPath);
        if (!NewAnim)
        {
            BS->PostEditChange();
            return MakeError(TEXT("NOT_FOUND"),
                FString::Printf(TEXT("Anim Sequence not found at '%s'"), *NewAnimPath));
        }
        // UE 5.7: ReplaceSampleAnimation updates the animation on a sample
        BS->ReplaceSampleAnimation(SampleIndex, NewAnim);
    }

    BS->ValidateSampleData();
    BS->PostEditChange();
    BS->MarkPackageDirty();

    // Read back the updated sample
    const FBlendSample& Updated = BS->GetBlendSamples()[SampleIndex];
    TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
    Data->SetStringField(TEXT("asset_path"), BS->GetPathName());
    Data->SetNumberField(TEXT("sample_index"), SampleIndex);
    Data->SetStringField(TEXT("animation"),
        Updated.Animation ? Updated.Animation->GetPathName() : TEXT(""));
    Data->SetNumberField(TEXT("x"), Updated.SampleValue.X);
    Data->SetNumberField(TEXT("y"), Updated.SampleValue.Y);
    Data->SetNumberField(TEXT("rate_scale"), Updated.RateScale);
    return MakeSuccess(Data);
}

TSharedPtr<FJsonObject> FNexusAnimationHandler::HandleRemoveBlendSpaceSample(
    const TSharedPtr<FJsonObject>& Params)
{
    FString AssetPath = GetStringParam(Params, TEXT("asset_path"));
    if (AssetPath.IsEmpty())
    {
        return MakeError(TEXT("MISSING_PARAM"), TEXT("asset_path is required"));
    }

    int32 SampleIndex = static_cast<int32>(GetNumberParam(Params, TEXT("sample_index"), -1));
    if (SampleIndex < 0)
    {
        return MakeError(TEXT("MISSING_PARAM"), TEXT("sample_index is required and must be >= 0"));
    }

    UBlendSpace* BS = LoadObject<UBlendSpace>(nullptr, *AssetPath);
    if (!BS)
    {
        return MakeError(TEXT("NOT_FOUND"),
            FString::Printf(TEXT("Blend Space not found at '%s'"), *AssetPath));
    }

    if (SampleIndex >= BS->GetBlendSamples().Num())
    {
        return MakeError(TEXT("INDEX_OUT_OF_RANGE"),
            FString::Printf(TEXT("Sample index %d out of range (0..%d)"),
                SampleIndex, BS->GetBlendSamples().Num() - 1));
    }

    BS->PreEditChange(nullptr);

    // UE 5.7: DeleteSample removes the sample at the given index
    bool bDeleted = BS->DeleteSample(SampleIndex);

    BS->ValidateSampleData();
    BS->PostEditChange();
    BS->MarkPackageDirty();

    TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
    Data->SetStringField(TEXT("asset_path"), BS->GetPathName());
    Data->SetBoolField(TEXT("deleted"), bDeleted);
    Data->SetNumberField(TEXT("deleted_index"), SampleIndex);
    Data->SetNumberField(TEXT("remaining_samples"), BS->GetBlendSamples().Num());
    return MakeSuccess(Data);
}
