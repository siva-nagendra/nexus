// Copyright Nexus Team. All Rights Reserved.

#pragma once

#include "NexusCommandHandler.h"

/**
 * Handler for audio subsystem commands: spawning sounds, creating Sound Cues
 * and MetaSounds, configuring attenuation, reverb, querying audio state,
 * and Sound Cue / MetaSound node CRUD.
 * Namespace: "audio", 20 commands.
 */
class FNexusAudioHandler : public INexusCommandHandler
{
public:
    virtual FString GetNamespace() const override { return TEXT("audio"); }

    virtual TArray<FString> GetSupportedCommands() const override
    {
        return {
            TEXT("spawn_sound"),
            TEXT("create_sound_cue"),
            TEXT("create_metasound"),
            TEXT("set_sound_properties"),
            TEXT("set_attenuation"),
            TEXT("set_reverb_settings"),
            TEXT("list_sound_classes"),
            TEXT("get_audio_info"),
            // Sound Cue node CRUD (Node CRUD Phase 3A)
            TEXT("add_sound_cue_node"),
            TEXT("get_sound_cue_nodes"),
            TEXT("update_sound_cue_node"),
            TEXT("remove_sound_cue_node"),
            TEXT("connect_sound_nodes"),
            TEXT("disconnect_sound_node"),
            // MetaSound node CRUD (Node CRUD Phase 3B)
            TEXT("add_metasound_node"),
            TEXT("get_metasound_nodes"),
            TEXT("update_metasound_node"),
            TEXT("remove_metasound_node"),
            TEXT("connect_metasound_nodes"),
            TEXT("disconnect_metasound_node")
        };
    }

    virtual TSharedPtr<FJsonObject> Handle(
        const FString& CommandType,
        const TSharedPtr<FJsonObject>& Params) override;

private:
    // Command handlers
    TSharedPtr<FJsonObject> HandleSpawnSound(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleCreateSoundCue(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleCreateMetaSound(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleSetSoundProperties(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleSetAttenuation(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleSetReverbSettings(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleListSoundClasses(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleGetAudioInfo(const TSharedPtr<FJsonObject>& Params);

    // Sound Cue node CRUD (Node CRUD Phase 3A)
    TSharedPtr<FJsonObject> HandleAddSoundCueNode(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleGetSoundCueNodes(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleUpdateSoundCueNode(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleRemoveSoundCueNode(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleConnectSoundNodes(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleDisconnectSoundNode(const TSharedPtr<FJsonObject>& Params);

    // MetaSound node CRUD (Node CRUD Phase 3B)
    TSharedPtr<FJsonObject> HandleAddMetaSoundNode(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleGetMetaSoundNodes(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleUpdateMetaSoundNode(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleRemoveMetaSoundNode(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleConnectMetaSoundNodes(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleDisconnectMetaSoundNode(const TSharedPtr<FJsonObject>& Params);

    // Helpers
    class UAudioComponent* FindAudioComponent(class AActor* Actor);
};
