// Copyright Nexus Team. All Rights Reserved.

#pragma once

#include "NexusCommandHandler.h"

class ULevelSequence;
class UMovieScene;

/**
 * Handler for Level Sequencer / Cinematics subsystem commands:
 * create/open sequences, manage tracks, keyframe transforms and floats,
 * camera cuts, sub-sequences, range management, and export.
 * Namespace: "sequencer", 12 commands.
 */
class FNexusSequencerHandler : public INexusCommandHandler
{
public:
    virtual FString GetNamespace() const override { return TEXT("sequencer"); }

    virtual TArray<FString> GetSupportedCommands() const override
    {
        return {
            TEXT("create_sequence"),
            TEXT("open_sequence"),
            TEXT("get_sequence_info"),
            TEXT("list_sequence_tracks"),
            TEXT("add_actor_track"),
            TEXT("add_transform_keyframe"),
            TEXT("add_float_keyframe"),
            TEXT("add_camera_cut"),
            TEXT("add_subsequence"),
            TEXT("set_sequence_range"),
            TEXT("remove_track"),
            TEXT("export_sequence")
        };
    }

    virtual TSharedPtr<FJsonObject> Handle(
        const FString& CommandType,
        const TSharedPtr<FJsonObject>& Params) override;

private:
    TSharedPtr<FJsonObject> HandleCreateSequence(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleOpenSequence(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleGetSequenceInfo(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleListSequenceTracks(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleAddActorTrack(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleAddTransformKeyframe(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleAddFloatKeyframe(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleAddCameraCut(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleAddSubsequence(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleSetSequenceRange(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleRemoveTrack(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleExportSequence(const TSharedPtr<FJsonObject>& Params);

    /** Helper: Load a ULevelSequence from an asset path, returning error JSON on failure. */
    ULevelSequence* LoadSequenceOrError(
        const FString& SequencePath,
        TSharedPtr<FJsonObject>& OutError);

    /** Helper: Find an actor by path or label in the editor world. */
    AActor* FindActorByPathOrLabel(const FString& ActorPath, UWorld* World);

    /** Helper: Convert interpolation string to ERichCurveInterpMode. */
    static ERichCurveInterpMode ParseInterpolation(const FString& InterpStr);

    /** Helper: Convert seconds to FFrameNumber using a MovieScene's tick resolution. */
    static FFrameNumber SecondsToFrameNumber(double Seconds, UMovieScene* MovieScene);

    /** Helper: Get binding display name via possessable/spawnable lookup (UE 5.7 deprecation of FMovieSceneBinding::GetName). */
    static FString GetBindingDisplayName(const struct FMovieSceneBinding& Binding, const UMovieScene* Scene);
};
