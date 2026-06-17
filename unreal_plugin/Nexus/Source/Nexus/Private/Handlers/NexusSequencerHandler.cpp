// Copyright Nexus Team. All Rights Reserved.

#include "Handlers/NexusSequencerHandler.h"
#include "Editor.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "LevelSequence.h"
#include "MovieScene.h"
#include "MovieSceneSection.h"
#include "MovieSceneTrack.h"
#include "MovieSceneBinding.h"
#include "MovieScenePossessable.h"
#include "MovieSceneSpawnable.h"
#include "Tracks/MovieScene3DTransformTrack.h"
#include "Sections/MovieScene3DTransformSection.h"
#include "Tracks/MovieSceneFloatTrack.h"
#include "Sections/MovieSceneFloatSection.h"
#include "Tracks/MovieSceneCameraCutTrack.h"
#include "Sections/MovieSceneCameraCutSection.h"
#include "Tracks/MovieSceneSubTrack.h"
#include "Sections/MovieSceneSubSection.h"
#include "Tracks/MovieSceneBoolTrack.h"
#include "Tracks/MovieSceneVisibilityTrack.h"
#include "Channels/MovieSceneFloatChannel.h"
#include "Channels/MovieSceneChannelProxy.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "AssetRegistry/AssetRegistryModule.h"
// LevelSequenceFactoryNew: use AssetTools.CreateAsset with explicit class instead of factory
// The factory is in LevelSequenceEditor Private dir, not accessible as public include
#include "Camera/CameraActor.h"
#include "CineCameraActor.h"
#include "MovieSceneToolHelpers.h"
#include "Misc/FrameRate.h"

// ─────────────────────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────────────────────

ULevelSequence* FNexusSequencerHandler::LoadSequenceOrError(
    const FString& SequencePath,
    TSharedPtr<FJsonObject>& OutError)
{
    if (SequencePath.IsEmpty())
    {
        OutError = MakeError(TEXT("MISSING_PARAM"), TEXT("sequence_path is required"));
        return nullptr;
    }

    ULevelSequence* Seq = LoadObject<ULevelSequence>(nullptr, *SequencePath);
    if (!Seq)
    {
        OutError = MakeError(TEXT("NOT_FOUND"),
            FString::Printf(TEXT("Level Sequence not found at '%s'"), *SequencePath));
        return nullptr;
    }

    return Seq;
}

AActor* FNexusSequencerHandler::FindActorByPathOrLabel(
    const FString& ActorPath, UWorld* World)
{
    if (!World || ActorPath.IsEmpty()) return nullptr;

    // Try full object path first
    UObject* Obj = StaticFindObject(AActor::StaticClass(), nullptr, *ActorPath);
    AActor* Actor = Cast<AActor>(Obj);

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

    return Actor;
}

ERichCurveInterpMode FNexusSequencerHandler::ParseInterpolation(const FString& InterpStr)
{
    if (InterpStr.Equals(TEXT("Linear"), ESearchCase::IgnoreCase))
        return RCIM_Linear;
    if (InterpStr.Equals(TEXT("Constant"), ESearchCase::IgnoreCase))
        return RCIM_Constant;
    // Default to cubic
    return RCIM_Cubic;
}

FFrameNumber FNexusSequencerHandler::SecondsToFrameNumber(
    double Seconds, UMovieScene* MovieScene)
{
    FFrameRate TickResolution = MovieScene->GetTickResolution();
    return FFrameNumber(static_cast<int32>(FMath::RoundToInt(Seconds * TickResolution.AsDecimal())));
}

FString FNexusSequencerHandler::GetBindingDisplayName(
    const FMovieSceneBinding& Binding, const UMovieScene* Scene)
{
    if (!Scene)
    {
        return Binding.GetObjectGuid().ToString();
    }

    const FGuid& BindingGuid = Binding.GetObjectGuid();

    // UE 5.7: Use FindPossessable/FindSpawnable by GUID (iteration removed)
    if (FMovieScenePossessable* Possessable = const_cast<UMovieScene*>(Scene)->FindPossessable(BindingGuid))
    {
        return Possessable->GetName();
    }

    if (FMovieSceneSpawnable* Spawnable = const_cast<UMovieScene*>(Scene)->FindSpawnable(BindingGuid))
    {
        return Spawnable->GetName();
    }

    // Fallback to GUID string
    return BindingGuid.ToString();
}

// ─────────────────────────────────────────────────────────────────────────────
// Dispatch
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FNexusSequencerHandler::Handle(
    const FString& CommandType,
    const TSharedPtr<FJsonObject>& Params)
{
    FString SubCommand = CommandType.RightChop(GetNamespace().Len() + 1);

    if (SubCommand == TEXT("create_sequence"))       return HandleCreateSequence(Params);
    if (SubCommand == TEXT("open_sequence"))          return HandleOpenSequence(Params);
    if (SubCommand == TEXT("get_sequence_info"))      return HandleGetSequenceInfo(Params);
    if (SubCommand == TEXT("list_sequence_tracks"))   return HandleListSequenceTracks(Params);
    if (SubCommand == TEXT("add_actor_track"))        return HandleAddActorTrack(Params);
    if (SubCommand == TEXT("add_transform_keyframe")) return HandleAddTransformKeyframe(Params);
    if (SubCommand == TEXT("add_float_keyframe"))     return HandleAddFloatKeyframe(Params);
    if (SubCommand == TEXT("add_camera_cut"))         return HandleAddCameraCut(Params);
    if (SubCommand == TEXT("add_subsequence"))        return HandleAddSubsequence(Params);
    if (SubCommand == TEXT("set_sequence_range"))     return HandleSetSequenceRange(Params);
    if (SubCommand == TEXT("remove_track"))           return HandleRemoveTrack(Params);
    if (SubCommand == TEXT("export_sequence"))        return HandleExportSequence(Params);

    return MakeError(TEXT("NOT_IMPLEMENTED"),
        FString::Printf(TEXT("Command '%s' not yet implemented"), *CommandType));
}

// ─────────────────────────────────────────────────────────────────────────────
// 1. create_sequence
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FNexusSequencerHandler::HandleCreateSequence(
    const TSharedPtr<FJsonObject>& Params)
{
    FString SequenceName = GetStringParam(Params, TEXT("sequence_name"));
    if (SequenceName.IsEmpty())
    {
        return MakeError(TEXT("MISSING_PARAM"), TEXT("sequence_name is required"));
    }

    FString DestFolder = GetStringParam(Params, TEXT("destination_folder"));
    if (DestFolder.IsEmpty())
    {
        return MakeError(TEXT("MISSING_PARAM"), TEXT("destination_folder is required"));
    }

    double FrameRate = GetNumberParam(Params, TEXT("frame_rate"), 30.0);
    double Duration = GetNumberParam(Params, TEXT("duration"), 10.0);
    bool bOpenAfter = GetBoolParam(Params, TEXT("open_after_create"), true);

    // Create via AssetTools without explicit factory (engine resolves factory from class)
    IAssetTools& AssetTools =
        FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
    UObject* NewAsset = AssetTools.CreateAsset(
        SequenceName, DestFolder, ULevelSequence::StaticClass(), nullptr);

    if (!NewAsset)
    {
        return MakeError(TEXT("CREATE_FAILED"), TEXT("Failed to create Level Sequence asset"));
    }

    ULevelSequence* Seq = Cast<ULevelSequence>(NewAsset);
    UMovieScene* MovieScene = Seq->GetMovieScene();

    // Set display rate
    int32 FrameRateInt = static_cast<int32>(FMath::RoundToInt(FrameRate));
    MovieScene->SetDisplayRate(FFrameRate(FrameRateInt, 1));

    // Set playback range based on duration
    FFrameRate TickResolution = MovieScene->GetTickResolution();
    FFrameNumber StartFrame(0);
    FFrameNumber EndFrame = FFrameNumber(
        static_cast<int32>(FMath::RoundToInt(Duration * TickResolution.AsDecimal())));
    MovieScene->SetPlaybackRange(
        TRange<FFrameNumber>(StartFrame, EndFrame));

    Seq->MarkPackageDirty();

    // Optionally open in editor
    if (bOpenAfter && GEditor)
    {
        GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->OpenEditorForAsset(Seq);
    }

    TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
    Data->SetStringField(TEXT("path"), Seq->GetPathName());
    Data->SetStringField(TEXT("name"), Seq->GetName());
    Data->SetNumberField(TEXT("duration"), Duration);
    Data->SetNumberField(TEXT("frame_rate"), FrameRate);
    return MakeSuccess(Data);
}

// ─────────────────────────────────────────────────────────────────────────────
// 2. open_sequence
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FNexusSequencerHandler::HandleOpenSequence(
    const TSharedPtr<FJsonObject>& Params)
{
    TSharedPtr<FJsonObject> Error;
    ULevelSequence* Seq = LoadSequenceOrError(
        GetStringParam(Params, TEXT("sequence_path")), Error);
    if (!Seq) return Error;

    if (GEditor)
    {
        GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->OpenEditorForAsset(Seq);
    }

    TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
    Data->SetStringField(TEXT("path"), Seq->GetPathName());
    Data->SetBoolField(TEXT("opened"), true);
    return MakeSuccess(Data);
}

// ─────────────────────────────────────────────────────────────────────────────
// 3. get_sequence_info
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FNexusSequencerHandler::HandleGetSequenceInfo(
    const TSharedPtr<FJsonObject>& Params)
{
    TSharedPtr<FJsonObject> Error;
    ULevelSequence* Seq = LoadSequenceOrError(
        GetStringParam(Params, TEXT("sequence_path")), Error);
    if (!Seq) return Error;

    UMovieScene* Scene = Seq->GetMovieScene();
    FFrameRate TickResolution = Scene->GetTickResolution();
    FFrameRate DisplayRate = Scene->GetDisplayRate();

    // Calculate duration from playback range
    TRange<FFrameNumber> PlaybackRange = Scene->GetPlaybackRange();
    double Duration = 0.0;
    if (PlaybackRange.HasLowerBound() && PlaybackRange.HasUpperBound())
    {
        int32 TotalFrames = PlaybackRange.GetUpperBoundValue().Value -
                            PlaybackRange.GetLowerBoundValue().Value;
        Duration = static_cast<double>(TotalFrames) / TickResolution.AsDecimal();
    }

    TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
    Data->SetStringField(TEXT("path"), Seq->GetPathName());
    Data->SetNumberField(TEXT("duration"), Duration);
    Data->SetNumberField(TEXT("frame_rate"), DisplayRate.AsDecimal());
    Data->SetNumberField(TEXT("tick_resolution"), TickResolution.AsDecimal());

    // Collect tracks from bindings
    TArray<TSharedPtr<FJsonValue>> TrackArray;
    int32 TrackCount = 0;

    for (const FMovieSceneBinding& Binding : static_cast<const UMovieScene*>(Scene)->GetBindings())
    {
        FString BindingName = GetBindingDisplayName(Binding, Scene);
        for (UMovieSceneTrack* Track : Binding.GetTracks())
        {
            if (!Track) continue;

            TSharedPtr<FJsonObject> TrackObj = MakeShareable(new FJsonObject());
            TrackObj->SetStringField(TEXT("track_name"), Track->GetTrackName().ToString());
            TrackObj->SetStringField(TEXT("track_type"), Track->GetClass()->GetName());
            TrackObj->SetStringField(TEXT("object_binding"), BindingName);

            // Count sections / keyframes
            int32 KeyCount = 0;
            for (UMovieSceneSection* Section : Track->GetAllSections())
            {
                if (!Section) continue;
                FMovieSceneChannelProxy& ChannelProxy = Section->GetChannelProxy();
                for (const FMovieSceneChannelEntry& Entry : ChannelProxy.GetAllEntries())
                {
                    for (FMovieSceneChannel* Channel : Entry.GetChannels())
                    {
                        if (Channel)
                        {
                            KeyCount += Channel->GetNumKeys();
                        }
                    }
                }
            }
            TrackObj->SetNumberField(TEXT("keyframe_count"), KeyCount);
            TrackObj->SetNumberField(TEXT("section_count"), Track->GetAllSections().Num());

            TrackArray.Add(MakeShareable(new FJsonValueObject(TrackObj)));
            TrackCount++;
        }
    }

    // Also collect tracks (camera cuts, sub tracks, etc.)
    for (UMovieSceneTrack* Track : Scene->GetTracks())
    {
        if (!Track) continue;

        TSharedPtr<FJsonObject> TrackObj = MakeShareable(new FJsonObject());
        TrackObj->SetStringField(TEXT("track_name"), Track->GetTrackName().ToString());
        TrackObj->SetStringField(TEXT("track_type"), Track->GetClass()->GetName());
        TrackObj->SetStringField(TEXT("object_binding"), TEXT("(master)"));
        TrackObj->SetNumberField(TEXT("section_count"), Track->GetAllSections().Num());
        TrackObj->SetNumberField(TEXT("keyframe_count"), 0);

        TrackArray.Add(MakeShareable(new FJsonValueObject(TrackObj)));
        TrackCount++;
    }

    // Camera cut track (special singleton)
    UMovieSceneTrack* CameraCutTrack = Scene->GetCameraCutTrack();
    if (CameraCutTrack)
    {
        TSharedPtr<FJsonObject> CamTrackObj = MakeShareable(new FJsonObject());
        CamTrackObj->SetStringField(TEXT("track_name"), TEXT("CameraCuts"));
        CamTrackObj->SetStringField(TEXT("track_type"), TEXT("MovieSceneCameraCutTrack"));
        CamTrackObj->SetStringField(TEXT("object_binding"), TEXT("(camera_cuts)"));
        CamTrackObj->SetNumberField(TEXT("section_count"),
            CameraCutTrack->GetAllSections().Num());
        CamTrackObj->SetNumberField(TEXT("keyframe_count"), 0);
        TrackArray.Add(MakeShareable(new FJsonValueObject(CamTrackObj)));
        TrackCount++;
    }

    Data->SetArrayField(TEXT("tracks"), TrackArray);
    Data->SetNumberField(TEXT("track_count"), TrackCount);

    // Sub-sequences
    TArray<TSharedPtr<FJsonValue>> SubSeqArray;
    for (UMovieSceneTrack* Track : Scene->GetTracks())
    {
        UMovieSceneSubTrack* SubTrack = Cast<UMovieSceneSubTrack>(Track);
        if (!SubTrack) continue;

        for (UMovieSceneSection* Section : SubTrack->GetAllSections())
        {
            UMovieSceneSubSection* SubSection = Cast<UMovieSceneSubSection>(Section);
            if (!SubSection) continue;

            ULevelSequence* SubSeq = Cast<ULevelSequence>(
                SubSection->GetSequence());
            if (!SubSeq) continue;

            TSharedPtr<FJsonObject> SubObj = MakeShareable(new FJsonObject());
            SubObj->SetStringField(TEXT("path"), SubSeq->GetPathName());
            SubObj->SetStringField(TEXT("name"), SubSeq->GetName());
            SubSeqArray.Add(MakeShareable(new FJsonValueObject(SubObj)));
        }
    }
    Data->SetArrayField(TEXT("sub_sequences"), SubSeqArray);

    return MakeSuccess(Data);
}

// ─────────────────────────────────────────────────────────────────────────────
// 4. list_sequence_tracks
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FNexusSequencerHandler::HandleListSequenceTracks(
    const TSharedPtr<FJsonObject>& Params)
{
    TSharedPtr<FJsonObject> Error;
    ULevelSequence* Seq = LoadSequenceOrError(
        GetStringParam(Params, TEXT("sequence_path")), Error);
    if (!Seq) return Error;

    FString TrackTypeFilter = GetStringParam(Params, TEXT("track_type_filter"));
    UMovieScene* Scene = Seq->GetMovieScene();

    TArray<TSharedPtr<FJsonValue>> TrackArray;

    for (const FMovieSceneBinding& Binding : static_cast<const UMovieScene*>(Scene)->GetBindings())
    {
        for (UMovieSceneTrack* Track : Binding.GetTracks())
        {
            if (!Track) continue;

            FString TrackType = Track->GetClass()->GetName();
            if (!TrackTypeFilter.IsEmpty() && !TrackType.Contains(TrackTypeFilter))
            {
                continue;
            }

            TSharedPtr<FJsonObject> TrackObj = MakeShareable(new FJsonObject());
            TrackObj->SetStringField(TEXT("track_name"), Track->GetTrackName().ToString());
            TrackObj->SetStringField(TEXT("track_type"), TrackType);
            TrackObj->SetStringField(TEXT("object_binding"), GetBindingDisplayName(Binding, Scene));

            int32 KeyCount = 0;
            for (UMovieSceneSection* Section : Track->GetAllSections())
            {
                if (!Section) continue;
                FMovieSceneChannelProxy& ChannelProxy = Section->GetChannelProxy();
                for (const FMovieSceneChannelEntry& Entry : ChannelProxy.GetAllEntries())
                {
                    for (FMovieSceneChannel* Channel : Entry.GetChannels())
                    {
                        if (Channel) KeyCount += Channel->GetNumKeys();
                    }
                }
            }
            TrackObj->SetNumberField(TEXT("keyframe_count"), KeyCount);

            TrackArray.Add(MakeShareable(new FJsonValueObject(TrackObj)));
        }
    }

    TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
    Data->SetArrayField(TEXT("tracks"), TrackArray);
    Data->SetNumberField(TEXT("count"), TrackArray.Num());
    return MakeSuccess(Data);
}

// ─────────────────────────────────────────────────────────────────────────────
// 5. add_actor_track
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FNexusSequencerHandler::HandleAddActorTrack(
    const TSharedPtr<FJsonObject>& Params)
{
    TSharedPtr<FJsonObject> Error;
    ULevelSequence* Seq = LoadSequenceOrError(
        GetStringParam(Params, TEXT("sequence_path")), Error);
    if (!Seq) return Error;

    FString ActorPath = GetStringParam(Params, TEXT("actor_path"));
    if (ActorPath.IsEmpty())
    {
        return MakeError(TEXT("MISSING_PARAM"), TEXT("actor_path is required"));
    }

    FString TrackType = GetStringParam(Params, TEXT("track_type"), TEXT("Transform"));
    FString TrackName = GetStringParam(Params, TEXT("track_name"));

    UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
    if (!World)
    {
        return MakeError(TEXT("NO_WORLD"), TEXT("No editor world available"));
    }

    AActor* Actor = FindActorByPathOrLabel(ActorPath, World);
    if (!Actor)
    {
        return MakeError(TEXT("NOT_FOUND"),
            FString::Printf(TEXT("Actor not found: '%s'"), *ActorPath));
    }

    UMovieScene* Scene = Seq->GetMovieScene();

    // Find or create binding for this actor
    FGuid BindingGuid;
    for (const FMovieSceneBinding& Binding : static_cast<const UMovieScene*>(Scene)->GetBindings())
    {
        // Check if this binding already has this actor
        for (UMovieSceneTrack* Track : Binding.GetTracks())
        {
            // We just need to find a binding matching this actor name
        }
        if (GetBindingDisplayName(Binding, Scene) == Actor->GetActorLabel())
        {
            BindingGuid = Binding.GetObjectGuid();
            break;
        }
    }

    if (!BindingGuid.IsValid())
    {
        // Create a new possessable binding
        BindingGuid = Scene->AddPossessable(
            Actor->GetActorLabel(), Actor->GetClass());

        // Bind the actor to the sequence
        Seq->BindPossessableObject(BindingGuid,
            *Actor, Actor->GetWorld());
    }

    // Determine track class from type string
    UClass* TrackClass = nullptr;
    if (TrackType.Equals(TEXT("Transform"), ESearchCase::IgnoreCase))
    {
        TrackClass = UMovieScene3DTransformTrack::StaticClass();
    }
    else if (TrackType.Equals(TEXT("Float"), ESearchCase::IgnoreCase))
    {
        TrackClass = UMovieSceneFloatTrack::StaticClass();
    }
    else if (TrackType.Equals(TEXT("Bool"), ESearchCase::IgnoreCase))
    {
        TrackClass = UMovieSceneBoolTrack::StaticClass();
    }
    else if (TrackType.Equals(TEXT("Visibility"), ESearchCase::IgnoreCase))
    {
        TrackClass = UMovieSceneVisibilityTrack::StaticClass();
    }
    else
    {
        // Default to float track for property-based tracks
        TrackClass = UMovieSceneFloatTrack::StaticClass();
    }

    UMovieSceneTrack* NewTrack = Scene->AddTrack(TrackClass, BindingGuid);
    if (!NewTrack)
    {
        return MakeError(TEXT("ADD_TRACK_FAILED"),
            FString::Printf(TEXT("Failed to add %s track for actor '%s'"),
                *TrackType, *ActorPath));
    }

    // Note: Track display name is read-only in UE 5.7+.
    // The track name is determined by the track type and bound object.

    // Add a default section spanning the playback range
    UMovieSceneSection* Section = NewTrack->CreateNewSection();
    if (Section)
    {
        TRange<FFrameNumber> PlaybackRange = Scene->GetPlaybackRange();
        Section->SetRange(PlaybackRange);
        NewTrack->AddSection(*Section);
    }

    Seq->MarkPackageDirty();

    TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
    Data->SetStringField(TEXT("track_name"),
        TrackName.IsEmpty() ? NewTrack->GetTrackName().ToString() : TrackName);
    Data->SetStringField(TEXT("track_type"), TrackType);
    Data->SetStringField(TEXT("binding_id"), BindingGuid.ToString());
    Data->SetStringField(TEXT("actor_path"), Actor->GetPathName());
    return MakeSuccess(Data);
}

// ─────────────────────────────────────────────────────────────────────────────
// 6. add_transform_keyframe
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FNexusSequencerHandler::HandleAddTransformKeyframe(
    const TSharedPtr<FJsonObject>& Params)
{
    TSharedPtr<FJsonObject> Error;
    ULevelSequence* Seq = LoadSequenceOrError(
        GetStringParam(Params, TEXT("sequence_path")), Error);
    if (!Seq) return Error;

    FString ActorPath = GetStringParam(Params, TEXT("actor_path"));
    if (ActorPath.IsEmpty())
    {
        return MakeError(TEXT("MISSING_PARAM"), TEXT("actor_path is required"));
    }

    double Time = GetNumberParam(Params, TEXT("time"), 0.0);
    FString InterpStr = GetStringParam(Params, TEXT("interpolation"), TEXT("Cubic"));
    ERichCurveInterpMode InterpMode = ParseInterpolation(InterpStr);

    UMovieScene* Scene = Seq->GetMovieScene();
    FFrameNumber FrameNum = SecondsToFrameNumber(Time, Scene);

    // Find the binding for this actor
    FGuid BindingGuid;
    for (const FMovieSceneBinding& Binding : static_cast<const UMovieScene*>(Scene)->GetBindings())
    {
        if (GetBindingDisplayName(Binding, Scene) == ActorPath)
        {
            BindingGuid = Binding.GetObjectGuid();
            break;
        }
    }

    // Also try by matching the actor in the world
    if (!BindingGuid.IsValid())
    {
        UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
        if (World)
        {
            AActor* Actor = FindActorByPathOrLabel(ActorPath, World);
            if (Actor)
            {
                for (const FMovieSceneBinding& Binding : static_cast<const UMovieScene*>(Scene)->GetBindings())
                {
                    if (GetBindingDisplayName(Binding, Scene) == Actor->GetActorLabel())
                    {
                        BindingGuid = Binding.GetObjectGuid();
                        break;
                    }
                }
            }
        }
    }

    if (!BindingGuid.IsValid())
    {
        return MakeError(TEXT("NO_BINDING"),
            FString::Printf(TEXT("No binding found for actor '%s'. "
                "Use add_actor_track first."), *ActorPath));
    }

    // Find the transform track
    UMovieScene3DTransformTrack* TransformTrack = nullptr;
    for (UMovieSceneTrack* Track : Scene->FindTracks(
            UMovieScene3DTransformTrack::StaticClass(), BindingGuid))
    {
        TransformTrack = Cast<UMovieScene3DTransformTrack>(Track);
        if (TransformTrack) break;
    }

    if (!TransformTrack)
    {
        return MakeError(TEXT("NO_TRANSFORM_TRACK"),
            FString::Printf(TEXT("No transform track for actor '%s'. "
                "Use add_actor_track with track_type='Transform' first."), *ActorPath));
    }

    // Get the first section
    UMovieScene3DTransformSection* Section = nullptr;
    for (UMovieSceneSection* Sec : TransformTrack->GetAllSections())
    {
        Section = Cast<UMovieScene3DTransformSection>(Sec);
        if (Section) break;
    }

    if (!Section)
    {
        return MakeError(TEXT("NO_SECTION"),
            TEXT("Transform track has no sections"));
    }

    // Access channels via proxy
    // Transform section has 9 channels: LocX, LocY, LocZ, RotX, RotY, RotZ, ScaleX, ScaleY, ScaleZ
    TArrayView<FMovieSceneFloatChannel*> FloatChannels =
        Section->GetChannelProxy().GetChannels<FMovieSceneFloatChannel>();

    if (FloatChannels.Num() < 9)
    {
        return MakeError(TEXT("CHANNEL_ERROR"),
            TEXT("Transform section does not have expected 9 float channels"));
    }

    // Helper lambda to add key based on interpolation mode
    auto AddKeyWithInterp = [](FMovieSceneFloatChannel* Chan, FFrameNumber Frame, float Value, ERichCurveInterpMode Interp)
    {
        if (Interp == RCIM_Linear)
        {
            Chan->AddLinearKey(Frame, Value);
        }
        else if (Interp == RCIM_Constant)
        {
            Chan->AddConstantKey(Frame, Value);
        }
        else
        {
            // Default to cubic
            Chan->AddCubicKey(Frame, Value);
        }
    };

    // Check for location params
    const TArray<TSharedPtr<FJsonValue>>* LocationArray;
    if (Params->TryGetArrayField(TEXT("location"), LocationArray) &&
        LocationArray->Num() >= 3)
    {
        // Channels 0,1,2 = Location X,Y,Z
        for (int32 i = 0; i < 3; i++)
        {
            FMovieSceneFloatChannel* Chan = FloatChannels[i];
            float Val = static_cast<float>((*LocationArray)[i]->AsNumber());
            AddKeyWithInterp(Chan, FrameNum, Val, InterpMode);
        }
    }

    // Check for rotation params
    const TArray<TSharedPtr<FJsonValue>>* RotationArray;
    if (Params->TryGetArrayField(TEXT("rotation"), RotationArray) &&
        RotationArray->Num() >= 3)
    {
        // Channels 3,4,5 = Rotation X(Roll), Y(Pitch), Z(Yaw)
        for (int32 i = 0; i < 3; i++)
        {
            FMovieSceneFloatChannel* Chan = FloatChannels[3 + i];
            float Val = static_cast<float>((*RotationArray)[i]->AsNumber());
            AddKeyWithInterp(Chan, FrameNum, Val, InterpMode);
        }
    }

    // Check for scale params
    const TArray<TSharedPtr<FJsonValue>>* ScaleArray;
    if (Params->TryGetArrayField(TEXT("scale"), ScaleArray) &&
        ScaleArray->Num() >= 3)
    {
        // Channels 6,7,8 = Scale X,Y,Z
        for (int32 i = 0; i < 3; i++)
        {
            FMovieSceneFloatChannel* Chan = FloatChannels[6 + i];
            float Val = static_cast<float>((*ScaleArray)[i]->AsNumber());
            AddKeyWithInterp(Chan, FrameNum, Val, InterpMode);
        }
    }

    Seq->MarkPackageDirty();

    TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
    Data->SetNumberField(TEXT("time"), Time);
    Data->SetStringField(TEXT("interpolation"), InterpStr);
    Data->SetBoolField(TEXT("success"), true);
    return MakeSuccess(Data);
}

// ─────────────────────────────────────────────────────────────────────────────
// 7. add_float_keyframe
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FNexusSequencerHandler::HandleAddFloatKeyframe(
    const TSharedPtr<FJsonObject>& Params)
{
    TSharedPtr<FJsonObject> Error;
    ULevelSequence* Seq = LoadSequenceOrError(
        GetStringParam(Params, TEXT("sequence_path")), Error);
    if (!Seq) return Error;

    FString ActorPath = GetStringParam(Params, TEXT("actor_path"));
    if (ActorPath.IsEmpty())
    {
        return MakeError(TEXT("MISSING_PARAM"), TEXT("actor_path is required"));
    }

    FString TrackName = GetStringParam(Params, TEXT("track_name"));
    if (TrackName.IsEmpty())
    {
        return MakeError(TEXT("MISSING_PARAM"), TEXT("track_name is required"));
    }

    double Time = GetNumberParam(Params, TEXT("time"), 0.0);
    double Value = GetNumberParam(Params, TEXT("value"), 0.0);
    FString InterpStr = GetStringParam(Params, TEXT("interpolation"), TEXT("Cubic"));
    ERichCurveInterpMode InterpMode = ParseInterpolation(InterpStr);

    UMovieScene* Scene = Seq->GetMovieScene();
    FFrameNumber FrameNum = SecondsToFrameNumber(Time, Scene);

    // Find the binding for this actor
    FGuid BindingGuid;
    for (const FMovieSceneBinding& Binding : static_cast<const UMovieScene*>(Scene)->GetBindings())
    {
        if (GetBindingDisplayName(Binding, Scene) == ActorPath)
        {
            BindingGuid = Binding.GetObjectGuid();
            break;
        }
    }

    if (!BindingGuid.IsValid())
    {
        UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
        if (World)
        {
            AActor* Actor = FindActorByPathOrLabel(ActorPath, World);
            if (Actor)
            {
                for (const FMovieSceneBinding& Binding : static_cast<const UMovieScene*>(Scene)->GetBindings())
                {
                    if (GetBindingDisplayName(Binding, Scene) == Actor->GetActorLabel())
                    {
                        BindingGuid = Binding.GetObjectGuid();
                        break;
                    }
                }
            }
        }
    }

    if (!BindingGuid.IsValid())
    {
        return MakeError(TEXT("NO_BINDING"),
            FString::Printf(TEXT("No binding found for actor '%s'"), *ActorPath));
    }

    // Find the float track by name
    UMovieSceneFloatTrack* FloatTrack = nullptr;
    for (UMovieSceneTrack* Track : Scene->FindTracks(
            UMovieSceneFloatTrack::StaticClass(), BindingGuid))
    {
        if (Track->GetTrackName().ToString() == TrackName ||
            Track->GetDisplayName().ToString() == TrackName)
        {
            FloatTrack = Cast<UMovieSceneFloatTrack>(Track);
            if (FloatTrack) break;
        }
    }

    if (!FloatTrack)
    {
        return MakeError(TEXT("NO_FLOAT_TRACK"),
            FString::Printf(TEXT("No float track named '%s' for actor '%s'"),
                *TrackName, *ActorPath));
    }

    // Get the first section
    UMovieSceneFloatSection* Section = nullptr;
    for (UMovieSceneSection* Sec : FloatTrack->GetAllSections())
    {
        Section = Cast<UMovieSceneFloatSection>(Sec);
        if (Section) break;
    }

    if (!Section)
    {
        return MakeError(TEXT("NO_SECTION"),
            TEXT("Float track has no sections"));
    }

    // Get the first float channel
    TArrayView<FMovieSceneFloatChannel*> FloatChannels =
        Section->GetChannelProxy().GetChannels<FMovieSceneFloatChannel>();

    if (FloatChannels.Num() < 1)
    {
        return MakeError(TEXT("CHANNEL_ERROR"),
            TEXT("Float section has no float channels"));
    }

    FMovieSceneFloatChannel* Channel = FloatChannels[0];
    float FloatValue = static_cast<float>(Value);
    if (InterpMode == RCIM_Linear)
    {
        Channel->AddLinearKey(FrameNum, FloatValue);
    }
    else if (InterpMode == RCIM_Constant)
    {
        Channel->AddConstantKey(FrameNum, FloatValue);
    }
    else
    {
        // Default to cubic
        Channel->AddCubicKey(FrameNum, FloatValue);
    }

    Seq->MarkPackageDirty();

    TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
    Data->SetNumberField(TEXT("time"), Time);
    Data->SetNumberField(TEXT("value"), Value);
    Data->SetStringField(TEXT("interpolation"), InterpStr);
    return MakeSuccess(Data);
}

// ─────────────────────────────────────────────────────────────────────────────
// 8. add_camera_cut
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FNexusSequencerHandler::HandleAddCameraCut(
    const TSharedPtr<FJsonObject>& Params)
{
    TSharedPtr<FJsonObject> Error;
    FString SeqPath = GetStringParam(Params, TEXT("sequence_path"));
    ULevelSequence* Seq = LoadSequenceOrError(SeqPath, Error);
    if (!Seq) return Error;

    FString CameraActorPath = GetStringParam(Params, TEXT("camera_actor_path"));
    if (CameraActorPath.IsEmpty())
    {
        return MakeError(TEXT("MISSING_PARAM"), TEXT("camera_actor_path is required"));
    }

    double StartTime = GetNumberParam(Params, TEXT("start_time"), 0.0);
    double BlendTime = GetNumberParam(Params, TEXT("blend_time"), 0.0);

    UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
    if (!World)
    {
        return MakeError(TEXT("NO_WORLD"), TEXT("No editor world available"));
    }

    AActor* CameraActor = FindActorByPathOrLabel(CameraActorPath, World);
    if (!CameraActor)
    {
        return MakeError(TEXT("NOT_FOUND"),
            FString::Printf(TEXT("Camera actor not found: '%s'"), *CameraActorPath));
    }

    UMovieScene* Scene = Seq->GetMovieScene();

    // Find or create binding for the camera
    FGuid CameraBindingGuid;
    for (const FMovieSceneBinding& Binding : static_cast<const UMovieScene*>(Scene)->GetBindings())
    {
        if (GetBindingDisplayName(Binding, Scene) == CameraActor->GetActorLabel())
        {
            CameraBindingGuid = Binding.GetObjectGuid();
            break;
        }
    }

    if (!CameraBindingGuid.IsValid())
    {
        CameraBindingGuid = Scene->AddPossessable(
            CameraActor->GetActorLabel(), CameraActor->GetClass());
        Seq->BindPossessableObject(CameraBindingGuid,
            *CameraActor, CameraActor->GetWorld());
    }

    // Get or create the camera cut track
    UMovieSceneCameraCutTrack* CameraCutTrack =
        Cast<UMovieSceneCameraCutTrack>(Scene->GetCameraCutTrack());
    if (!CameraCutTrack)
    {
        CameraCutTrack = Cast<UMovieSceneCameraCutTrack>(
            Scene->AddCameraCutTrack(UMovieSceneCameraCutTrack::StaticClass()));
    }

    if (!CameraCutTrack)
    {
        return MakeError(TEXT("TRACK_FAILED"),
            TEXT("Failed to create or find camera cut track"));
    }

    // Create camera cut section
    FFrameNumber StartFrame = SecondsToFrameNumber(StartTime, Scene);

    // Determine end frame
    FFrameNumber EndFrame;
    double EndTime;
    if (Params->HasField(TEXT("end_time")))
    {
        EndTime = GetNumberParam(Params, TEXT("end_time"), 0.0);
        EndFrame = SecondsToFrameNumber(EndTime, Scene);
    }
    else
    {
        // Use sequence end
        TRange<FFrameNumber> PlaybackRange = Scene->GetPlaybackRange();
        EndFrame = PlaybackRange.GetUpperBoundValue();
        EndTime = static_cast<double>(EndFrame.Value) /
                  Scene->GetTickResolution().AsDecimal();
    }

    UMovieSceneSection* NewSection = CameraCutTrack->CreateNewSection();
    UMovieSceneCameraCutSection* CutSection =
        Cast<UMovieSceneCameraCutSection>(NewSection);

    if (!CutSection)
    {
        return MakeError(TEXT("SECTION_FAILED"),
            TEXT("Failed to create camera cut section"));
    }

    CutSection->SetRange(TRange<FFrameNumber>(StartFrame, EndFrame));
    CutSection->SetCameraBindingID(
        FMovieSceneObjectBindingID(CameraBindingGuid));

    CameraCutTrack->AddSection(*CutSection);

    Seq->MarkPackageDirty();

    TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
    Data->SetStringField(TEXT("camera_path"), CameraActor->GetPathName());
    Data->SetNumberField(TEXT("start_time"), StartTime);
    Data->SetNumberField(TEXT("end_time"), EndTime);
    Data->SetNumberField(TEXT("blend_time"), BlendTime);
    return MakeSuccess(Data);
}

// ─────────────────────────────────────────────────────────────────────────────
// 9. add_subsequence
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FNexusSequencerHandler::HandleAddSubsequence(
    const TSharedPtr<FJsonObject>& Params)
{
    TSharedPtr<FJsonObject> Error;
    FString ParentPath = GetStringParam(Params, TEXT("parent_sequence_path"));
    ULevelSequence* ParentSeq = LoadSequenceOrError(ParentPath, Error);
    if (!ParentSeq) return Error;

    FString ChildPath = GetStringParam(Params, TEXT("child_sequence_path"));
    if (ChildPath.IsEmpty())
    {
        return MakeError(TEXT("MISSING_PARAM"), TEXT("child_sequence_path is required"));
    }

    ULevelSequence* ChildSeq = LoadObject<ULevelSequence>(nullptr, *ChildPath);
    if (!ChildSeq)
    {
        return MakeError(TEXT("NOT_FOUND"),
            FString::Printf(TEXT("Child sequence not found at '%s'"), *ChildPath));
    }

    double StartTime = GetNumberParam(Params, TEXT("start_time"), 0.0);
    double TimeScale = GetNumberParam(Params, TEXT("time_scale"), 1.0);
    int32 LoopCount = static_cast<int32>(GetNumberParam(Params, TEXT("loop_count"), 1));

    UMovieScene* ParentScene = ParentSeq->GetMovieScene();

    // Find or create sub track
    UMovieSceneSubTrack* SubTrack = nullptr;
    for (UMovieSceneTrack* Track : ParentScene->GetTracks())
    {
        SubTrack = Cast<UMovieSceneSubTrack>(Track);
        if (SubTrack) break;
    }

    if (!SubTrack)
    {
        SubTrack = Cast<UMovieSceneSubTrack>(
            ParentScene->AddTrack(UMovieSceneSubTrack::StaticClass()));
    }

    if (!SubTrack)
    {
        return MakeError(TEXT("TRACK_FAILED"),
            TEXT("Failed to create sub-sequence track"));
    }

    // Add sub section
    FFrameNumber StartFrame = SecondsToFrameNumber(StartTime, ParentScene);

    UMovieSceneSubSection* SubSection = SubTrack->AddSequence(
        ChildSeq, StartFrame, 0);

    if (!SubSection)
    {
        return MakeError(TEXT("SECTION_FAILED"),
            TEXT("Failed to add sub-sequence section"));
    }

    // Set time scale if not 1.0
    if (!FMath::IsNearlyEqual(TimeScale, 1.0))
    {
        SubSection->Parameters.TimeScale = TimeScale;
    }

    ParentSeq->MarkPackageDirty();

    TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
    Data->SetStringField(TEXT("child_path"), ChildSeq->GetPathName());
    Data->SetNumberField(TEXT("start_time"), StartTime);
    Data->SetNumberField(TEXT("time_scale"), TimeScale);
    Data->SetNumberField(TEXT("loop_count"), LoopCount);
    return MakeSuccess(Data);
}

// ─────────────────────────────────────────────────────────────────────────────
// 10. set_sequence_range
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FNexusSequencerHandler::HandleSetSequenceRange(
    const TSharedPtr<FJsonObject>& Params)
{
    TSharedPtr<FJsonObject> Error;
    ULevelSequence* Seq = LoadSequenceOrError(
        GetStringParam(Params, TEXT("sequence_path")), Error);
    if (!Seq) return Error;

    UMovieScene* Scene = Seq->GetMovieScene();
    FFrameRate TickResolution = Scene->GetTickResolution();

    // Update frame rate if specified
    if (Params->HasField(TEXT("frame_rate")))
    {
        double NewFrameRate = GetNumberParam(Params, TEXT("frame_rate"), 30.0);
        int32 FRInt = static_cast<int32>(FMath::RoundToInt(NewFrameRate));
        Scene->SetDisplayRate(FFrameRate(FRInt, 1));
    }

    // Update playback range if specified
    TRange<FFrameNumber> CurrentRange = Scene->GetPlaybackRange();
    FFrameNumber NewStart = CurrentRange.HasLowerBound()
        ? CurrentRange.GetLowerBoundValue() : FFrameNumber(0);
    FFrameNumber NewEnd = CurrentRange.HasUpperBound()
        ? CurrentRange.GetUpperBoundValue() : FFrameNumber(0);

    if (Params->HasField(TEXT("start_time")))
    {
        double StartTime = GetNumberParam(Params, TEXT("start_time"), 0.0);
        NewStart = FFrameNumber(
            static_cast<int32>(FMath::RoundToInt(StartTime * TickResolution.AsDecimal())));
    }

    if (Params->HasField(TEXT("end_time")))
    {
        double EndTime = GetNumberParam(Params, TEXT("end_time"), 10.0);
        NewEnd = FFrameNumber(
            static_cast<int32>(FMath::RoundToInt(EndTime * TickResolution.AsDecimal())));
    }

    Scene->SetPlaybackRange(TRange<FFrameNumber>(NewStart, NewEnd));

    // Update view/work range if specified
#if WITH_EDITORONLY_DATA
    if (Params->HasField(TEXT("work_range_start")) ||
        Params->HasField(TEXT("work_range_end")))
    {
        TRange<double> ViewRange = Scene->GetEditorData().GetViewRange();
        double WorkStart = Params->HasField(TEXT("work_range_start"))
            ? GetNumberParam(Params, TEXT("work_range_start"), 0.0)
            : ViewRange.GetLowerBoundValue();
        double WorkEnd = Params->HasField(TEXT("work_range_end"))
            ? GetNumberParam(Params, TEXT("work_range_end"), 10.0)
            : ViewRange.GetUpperBoundValue();

        Scene->GetEditorData().ViewStart = WorkStart;
        Scene->GetEditorData().ViewEnd = WorkEnd;
    }
#endif

    Seq->MarkPackageDirty();

    // Build response with current state
    FFrameRate DisplayRate = Scene->GetDisplayRate();
    TRange<FFrameNumber> FinalRange = Scene->GetPlaybackRange();
    double FinalStart = FinalRange.HasLowerBound()
        ? static_cast<double>(FinalRange.GetLowerBoundValue().Value) /
          TickResolution.AsDecimal()
        : 0.0;
    double FinalEnd = FinalRange.HasUpperBound()
        ? static_cast<double>(FinalRange.GetUpperBoundValue().Value) /
          TickResolution.AsDecimal()
        : 0.0;

    TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
    Data->SetNumberField(TEXT("start_time"), FinalStart);
    Data->SetNumberField(TEXT("end_time"), FinalEnd);
    Data->SetNumberField(TEXT("frame_rate"), DisplayRate.AsDecimal());
    return MakeSuccess(Data);
}

// ─────────────────────────────────────────────────────────────────────────────
// 11. remove_track
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FNexusSequencerHandler::HandleRemoveTrack(
    const TSharedPtr<FJsonObject>& Params)
{
    TSharedPtr<FJsonObject> Error;
    ULevelSequence* Seq = LoadSequenceOrError(
        GetStringParam(Params, TEXT("sequence_path")), Error);
    if (!Seq) return Error;

    FString TrackName = GetStringParam(Params, TEXT("track_name"));
    if (TrackName.IsEmpty())
    {
        return MakeError(TEXT("MISSING_PARAM"), TEXT("track_name is required"));
    }

    FString ActorPath = GetStringParam(Params, TEXT("actor_path"));
    UMovieScene* Scene = Seq->GetMovieScene();

    // Search through bindings for the track
    UMovieSceneTrack* FoundTrack = nullptr;

    for (const FMovieSceneBinding& Binding : static_cast<const UMovieScene*>(Scene)->GetBindings())
    {
        // If actor_path is specified, only look at matching bindings
        if (!ActorPath.IsEmpty() && GetBindingDisplayName(Binding, Scene) != ActorPath)
        {
            continue;
        }

        for (UMovieSceneTrack* Track : Binding.GetTracks())
        {
            if (!Track) continue;
            if (Track->GetTrackName().ToString() == TrackName ||
                Track->GetDisplayName().ToString() == TrackName)
            {
                FoundTrack = Track;
                break;
            }
        }
        if (FoundTrack) break;
    }

    // Also check non-bound tracks
    if (!FoundTrack)
    {
        for (UMovieSceneTrack* Track : Scene->GetTracks())
        {
            if (!Track) continue;
            if (Track->GetTrackName().ToString() == TrackName ||
                Track->GetDisplayName().ToString() == TrackName)
            {
                FoundTrack = Track;
                break;
            }
        }
    }

    if (!FoundTrack)
    {
        return MakeError(TEXT("NOT_FOUND"),
            FString::Printf(TEXT("Track '%s' not found in sequence"), *TrackName));
    }

    bool bRemoved = Scene->RemoveTrack(*FoundTrack);
    if (!bRemoved)
    {
        return MakeError(TEXT("REMOVE_FAILED"),
            FString::Printf(TEXT("Failed to remove track '%s'"), *TrackName));
    }

    Seq->MarkPackageDirty();

    TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
    Data->SetBoolField(TEXT("removed"), true);
    Data->SetStringField(TEXT("track_name"), TrackName);
    return MakeSuccess(Data);
}

// ─────────────────────────────────────────────────────────────────────────────
// 12. export_sequence — deferred to codegen for complex FBX/USD export
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FNexusSequencerHandler::HandleExportSequence(
    const TSharedPtr<FJsonObject>& Params)
{
    TSharedPtr<FJsonObject> Error;
    FString SeqPath = GetStringParam(Params, TEXT("sequence_path"));
    ULevelSequence* Seq = LoadSequenceOrError(SeqPath, Error);
    if (!Seq) return Error;

    FString OutputDir = GetStringParam(Params, TEXT("output_directory"));
    if (OutputDir.IsEmpty())
    {
        return MakeError(TEXT("MISSING_PARAM"), TEXT("output_directory is required"));
    }

    FString Format = GetStringParam(Params, TEXT("format"), TEXT("FBX"));
    bool bExportCameras = GetBoolParam(Params, TEXT("export_cameras"), true);
    bool bExportActors = GetBoolParam(Params, TEXT("export_actors"), true);

    // Validate output directory exists
    IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
    if (!PlatformFile.DirectoryExists(*OutputDir))
    {
        // Try to create it
        if (!PlatformFile.CreateDirectoryTree(*OutputDir))
        {
            return MakeError(TEXT("DIRECTORY_ERROR"),
                FString::Printf(TEXT("Cannot create output directory: '%s'"), *OutputDir));
        }
    }

    // Sequence export is complex and format-dependent.
    // For FBX we'd need FFbxExporter, for USD we'd need ULevelSequenceExporterUSD.
    // Both require significant setup that varies by project configuration.
    // Return validated parameters so the codegen layer can handle the export
    // via UE Python scripting which has better export pipeline support.
    UMovieScene* Scene = Seq->GetMovieScene();
    FFrameRate DisplayRate = Scene->GetDisplayRate();

    TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
    Data->SetStringField(TEXT("sequence_path"), Seq->GetPathName());
    Data->SetStringField(TEXT("output_directory"), OutputDir);
    Data->SetStringField(TEXT("format"), Format);
    Data->SetBoolField(TEXT("export_cameras"), bExportCameras);
    Data->SetBoolField(TEXT("export_actors"), bExportActors);
    Data->SetNumberField(TEXT("frame_rate"), DisplayRate.AsDecimal());

    // If frame_rate override was specified
    if (Params->HasField(TEXT("frame_rate")))
    {
        Data->SetNumberField(TEXT("export_frame_rate"),
            GetNumberParam(Params, TEXT("frame_rate"), DisplayRate.AsDecimal()));
    }

    Data->SetStringField(TEXT("note"),
        TEXT("Export parameters validated. Complex FBX/USD export delegated to codegen pipeline."));
    Data->SetBoolField(TEXT("success"), true);
    return MakeSuccess(Data);
}
