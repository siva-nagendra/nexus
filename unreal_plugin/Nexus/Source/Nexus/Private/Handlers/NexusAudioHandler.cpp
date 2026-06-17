// Copyright Nexus Team. All Rights Reserved.

#include "Handlers/NexusAudioHandler.h"
#include "Editor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Sound/AmbientSound.h"
#include "Components/AudioComponent.h"
#include "Sound/SoundWave.h"
#include "Sound/SoundCue.h"
#include "Sound/SoundAttenuation.h"
#include "Sound/SoundClass.h"
#include "Sound/ReverbEffect.h"
#include "Sound/AudioVolume.h"
#include "Sound/SoundConcurrency.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "UObject/SavePackage.h"
#include "Factories/SoundCueFactoryNew.h"
#include "Sound/SoundNodeWavePlayer.h"
#include "Sound/SoundNodeRandom.h"
#include "Sound/SoundNodeConcatenator.h"
#include "Sound/SoundNodeModulator.h"
#include "SoundCueGraph/SoundCueGraph.h"
// Sound Cue node types for CRUD (Phase 3A)
#include "Sound/SoundNodeMixer.h"
#include "Sound/SoundNodeAttenuation.h"
#include "Sound/SoundNodeDelay.h"
#include "Sound/SoundNodeLooping.h"
#include "Sound/SoundNodeSwitch.h"
#include "Sound/SoundNodeEnveloper.h"
#include "Sound/SoundNodeDistanceCrossFade.h"
#include "Sound/SoundNodeDoppler.h"
#include "Sound/SoundNodeOscillator.h"
#include "Sound/SoundNodeBranch.h"
#include "Sound/SoundNodeDialoguePlayer.h"
// MetaSound node CRUD (Phase 3B)
#include "MetasoundSource.h"
#include "MetasoundFrontendDocument.h"
#include "MetasoundFrontendDocumentBuilder.h"

// ─────────────────────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────────────────────

UAudioComponent* FNexusAudioHandler::FindAudioComponent(AActor* Actor)
{
    if (!Actor) return nullptr;
    return Actor->FindComponentByClass<UAudioComponent>();
}

static UWorld* GetEditorWorld()
{
    if (GEditor)
    {
        return GEditor->GetEditorWorldContext().World();
    }
    return nullptr;
}

static AActor* FindActorByPathOrLabel(UWorld* World, const FString& ActorPath)
{
    if (!World || ActorPath.IsEmpty()) return nullptr;

    for (TActorIterator<AActor> It(World); It; ++It)
    {
        AActor* Actor = *It;
        if (Actor->GetPathName() == ActorPath || Actor->GetActorLabel() == ActorPath)
        {
            return Actor;
        }
    }
    return nullptr;
}

// ─────────────────────────────────────────────────────────────────────────────
// Dispatch
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FNexusAudioHandler::Handle(
    const FString& CommandType,
    const TSharedPtr<FJsonObject>& Params)
{
    FString SubCommand = CommandType.RightChop(GetNamespace().Len() + 1);

    if (SubCommand == TEXT("spawn_sound"))          return HandleSpawnSound(Params);
    if (SubCommand == TEXT("create_sound_cue"))      return HandleCreateSoundCue(Params);
    if (SubCommand == TEXT("create_metasound"))      return HandleCreateMetaSound(Params);
    if (SubCommand == TEXT("set_sound_properties"))  return HandleSetSoundProperties(Params);
    if (SubCommand == TEXT("set_attenuation"))       return HandleSetAttenuation(Params);
    if (SubCommand == TEXT("set_reverb_settings"))   return HandleSetReverbSettings(Params);
    if (SubCommand == TEXT("list_sound_classes"))     return HandleListSoundClasses(Params);
    if (SubCommand == TEXT("get_audio_info"))         return HandleGetAudioInfo(Params);
    // Sound Cue node CRUD (Phase 3A)
    if (SubCommand == TEXT("add_sound_cue_node"))      return HandleAddSoundCueNode(Params);
    if (SubCommand == TEXT("get_sound_cue_nodes"))      return HandleGetSoundCueNodes(Params);
    if (SubCommand == TEXT("update_sound_cue_node"))    return HandleUpdateSoundCueNode(Params);
    if (SubCommand == TEXT("remove_sound_cue_node"))    return HandleRemoveSoundCueNode(Params);
    if (SubCommand == TEXT("connect_sound_nodes"))      return HandleConnectSoundNodes(Params);
    if (SubCommand == TEXT("disconnect_sound_node"))    return HandleDisconnectSoundNode(Params);
    // MetaSound node CRUD (Phase 3B)
    if (SubCommand == TEXT("add_metasound_node"))       return HandleAddMetaSoundNode(Params);
    if (SubCommand == TEXT("get_metasound_nodes"))      return HandleGetMetaSoundNodes(Params);
    if (SubCommand == TEXT("update_metasound_node"))    return HandleUpdateMetaSoundNode(Params);
    if (SubCommand == TEXT("remove_metasound_node"))    return HandleRemoveMetaSoundNode(Params);
    if (SubCommand == TEXT("connect_metasound_nodes"))  return HandleConnectMetaSoundNodes(Params);
    if (SubCommand == TEXT("disconnect_metasound_node"))return HandleDisconnectMetaSoundNode(Params);

    return MakeError(TEXT("NOT_IMPLEMENTED"),
        FString::Printf(TEXT("Command '%s' not yet implemented"), *CommandType));
}

// ─────────────────────────────────────────────────────────────────────────────
// audio.spawn_sound
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FNexusAudioHandler::HandleSpawnSound(
    const TSharedPtr<FJsonObject>& Params)
{
    UWorld* World = GetEditorWorld();
    if (!World)
    {
        return MakeError(TEXT("NO_WORLD"), TEXT("No editor world available"));
    }

    FString SoundAssetPath = GetStringParam(Params, TEXT("sound_asset_path"));
    if (SoundAssetPath.IsEmpty())
    {
        return MakeError(TEXT("MISSING_PARAM"), TEXT("sound_asset_path is required"));
    }

    FString Label = GetStringParam(Params, TEXT("label"));
    FVector Location = GetVectorParam(Params, TEXT("location"));
    bool bAutoActivate = GetBoolParam(Params, TEXT("auto_activate"), true);
    bool bIsUISound = GetBoolParam(Params, TEXT("is_ui_sound"), false);
    double VolumeMultiplier = GetNumberParam(Params, TEXT("volume_multiplier"), 1.0);
    double PitchMultiplier = GetNumberParam(Params, TEXT("pitch_multiplier"), 1.0);
    FString AttenuationPath = GetStringParam(Params, TEXT("attenuation_settings_path"));

    // Load the sound asset
    USoundBase* SoundAsset = Cast<USoundBase>(
        StaticLoadObject(USoundBase::StaticClass(), nullptr, *SoundAssetPath));
    if (!SoundAsset)
    {
        return MakeError(TEXT("ASSET_NOT_FOUND"),
            FString::Printf(TEXT("Sound asset '%s' not found or is not a USoundBase"), *SoundAssetPath));
    }

    // Spawn AmbientSound actor
    FActorSpawnParameters SpawnParams;
    SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

    FTransform SpawnTransform(FRotator::ZeroRotator, Location);
    AAmbientSound* SoundActor = World->SpawnActor<AAmbientSound>(
        AAmbientSound::StaticClass(), SpawnTransform, SpawnParams);
    if (!SoundActor)
    {
        return MakeError(TEXT("SPAWN_FAILED"), TEXT("Failed to spawn AmbientSound actor"));
    }

    if (!Label.IsEmpty())
    {
        SoundActor->SetActorLabel(Label);
    }

    // Configure the audio component
    UAudioComponent* AudioComp = SoundActor->GetAudioComponent();
    if (AudioComp)
    {
        AudioComp->SetSound(SoundAsset);
        AudioComp->bAutoActivate = bAutoActivate;
        AudioComp->bIsUISound = bIsUISound;
        AudioComp->VolumeMultiplier = static_cast<float>(VolumeMultiplier);
        AudioComp->PitchMultiplier = static_cast<float>(PitchMultiplier);

        // Apply attenuation override if specified
        if (!AttenuationPath.IsEmpty())
        {
            USoundAttenuation* AttSettings = Cast<USoundAttenuation>(
                StaticLoadObject(USoundAttenuation::StaticClass(), nullptr, *AttenuationPath));
            if (AttSettings)
            {
                AudioComp->AttenuationSettings = AttSettings;
            }
        }
    }

    SoundActor->MarkPackageDirty();

    TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
    Data->SetStringField(TEXT("actor_path"), SoundActor->GetPathName());
    Data->SetStringField(TEXT("actor_label"), SoundActor->GetActorLabel());
    Data->SetStringField(TEXT("sound_asset"), SoundAssetPath);
    Data->SetObjectField(TEXT("location"), VectorToJson(SoundActor->GetActorLocation()));
    Data->SetBoolField(TEXT("auto_activate"), bAutoActivate);
    Data->SetBoolField(TEXT("is_ui_sound"), bIsUISound);
    Data->SetNumberField(TEXT("volume_multiplier"), VolumeMultiplier);
    Data->SetNumberField(TEXT("pitch_multiplier"), PitchMultiplier);
    return MakeSuccess(Data);
}

// ─────────────────────────────────────────────────────────────────────────────
// audio.create_sound_cue
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FNexusAudioHandler::HandleCreateSoundCue(
    const TSharedPtr<FJsonObject>& Params)
{
    FString CueName = GetStringParam(Params, TEXT("cue_name"));
    FString DestFolder = GetStringParam(Params, TEXT("destination_folder"));
    FString WavePaths = GetStringParam(Params, TEXT("sound_wave_paths"));
    FString MixerType = GetStringParam(Params, TEXT("mixer_type"), TEXT("Random"));
    double Volume = GetNumberParam(Params, TEXT("volume"), 1.0);
    double Pitch = GetNumberParam(Params, TEXT("pitch"), 1.0);

    if (CueName.IsEmpty())
    {
        return MakeError(TEXT("MISSING_PARAM"), TEXT("cue_name is required"));
    }
    if (DestFolder.IsEmpty())
    {
        return MakeError(TEXT("MISSING_PARAM"), TEXT("destination_folder is required"));
    }

    // Parse sound wave paths
    TArray<FString> WavePathArray;
    WavePaths.ParseIntoArray(WavePathArray, TEXT(","), true);
    if (WavePathArray.Num() == 0)
    {
        return MakeError(TEXT("MISSING_PARAM"),
            TEXT("sound_wave_paths is required (comma-separated asset paths)"));
    }

    // Load all sound waves
    TArray<USoundWave*> SoundWaves;
    for (const FString& WavePath : WavePathArray)
    {
        FString TrimmedPath = WavePath.TrimStartAndEnd();
        USoundWave* Wave = Cast<USoundWave>(
            StaticLoadObject(USoundWave::StaticClass(), nullptr, *TrimmedPath));
        if (Wave)
        {
            SoundWaves.Add(Wave);
        }
    }

    if (SoundWaves.Num() == 0)
    {
        return MakeError(TEXT("NO_VALID_WAVES"),
            TEXT("None of the specified sound wave paths could be loaded"));
    }

    // Create the package and sound cue
    FString PackagePath = DestFolder / CueName;
    if (!PackagePath.StartsWith(TEXT("/Game/")))
    {
        PackagePath = FString::Printf(TEXT("/Game/%s"), *PackagePath);
    }

    UPackage* Package = CreatePackage(*PackagePath);
    if (!Package)
    {
        return MakeError(TEXT("PACKAGE_FAILED"),
            FString::Printf(TEXT("Failed to create package for '%s'"), *PackagePath));
    }

    USoundCueFactoryNew* Factory = NewObject<USoundCueFactoryNew>();
    USoundCue* NewCue = Cast<USoundCue>(Factory->FactoryCreateNew(
        USoundCue::StaticClass(), Package, *CueName, RF_Public | RF_Standalone, nullptr, GWarn));
    if (!NewCue)
    {
        return MakeError(TEXT("CUE_CREATE_FAILED"),
            FString::Printf(TEXT("Failed to create Sound Cue '%s'"), *CueName));
    }

    // Set volume and pitch multipliers
    NewCue->VolumeMultiplier = static_cast<float>(Volume);
    NewCue->PitchMultiplier = static_cast<float>(Pitch);

    // Create wave player nodes for each sound wave
    TArray<USoundNode*> WavePlayerNodes;
    for (USoundWave* Wave : SoundWaves)
    {
        USoundNodeWavePlayer* WavePlayer = NewCue->ConstructSoundNode<USoundNodeWavePlayer>();
        WavePlayer->SetSoundWave(Wave);
        WavePlayerNodes.Add(WavePlayer);
        WavePlayer->GraphNode->NodePosX = -300;
        WavePlayer->GraphNode->NodePosY = WavePlayerNodes.Num() * 100;
    }

    // Create the mixer node if we have multiple waves
    if (WavePlayerNodes.Num() > 1)
    {
        USoundNode* MixerNode = nullptr;
        if (MixerType == TEXT("Random"))
        {
            MixerNode = NewCue->ConstructSoundNode<USoundNodeRandom>();
        }
        else if (MixerType == TEXT("Concatenate"))
        {
            MixerNode = NewCue->ConstructSoundNode<USoundNodeConcatenator>();
        }
        else if (MixerType == TEXT("Modulator"))
        {
            MixerNode = NewCue->ConstructSoundNode<USoundNodeModulator>();
        }
        else
        {
            MixerNode = NewCue->ConstructSoundNode<USoundNodeRandom>();
        }

        if (MixerNode)
        {
            MixerNode->GraphNode->NodePosX = -150;
            MixerNode->GraphNode->NodePosY = 0;

            // Connect wave players to mixer inputs
            MixerNode->InsertChildNode(WavePlayerNodes.Num() - 1);
            for (int32 i = 0; i < WavePlayerNodes.Num(); ++i)
            {
                MixerNode->ChildNodes[i] = WavePlayerNodes[i];
            }

            // Connect mixer to output
            NewCue->FirstNode = MixerNode;
        }
    }
    else if (WavePlayerNodes.Num() == 1)
    {
        // Single wave — connect directly to output
        NewCue->FirstNode = WavePlayerNodes[0];
    }

    NewCue->LinkGraphNodesFromSoundNodes();
    NewCue->MarkPackageDirty();

    // Save the package
    FString PackageFilename = FPackageName::LongPackageNameToFilename(
        PackagePath, FPackageName::GetAssetPackageExtension());
    FSavePackageArgs SaveArgs;
    SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
    UPackage::SavePackage(Package, NewCue, *PackageFilename, SaveArgs);

    // Notify asset registry
    FAssetRegistryModule::AssetCreated(NewCue);

    TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
    Data->SetStringField(TEXT("asset_path"), NewCue->GetPathName());
    Data->SetStringField(TEXT("cue_name"), CueName);
    Data->SetStringField(TEXT("mixer_type"), MixerType);
    Data->SetNumberField(TEXT("wave_count"), SoundWaves.Num());
    Data->SetNumberField(TEXT("volume"), Volume);
    Data->SetNumberField(TEXT("pitch"), Pitch);
    return MakeSuccess(Data);
}

// ─────────────────────────────────────────────────────────────────────────────
// audio.create_metasound
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FNexusAudioHandler::HandleCreateMetaSound(
    const TSharedPtr<FJsonObject>& Params)
{
    FString MetaSoundName = GetStringParam(Params, TEXT("metasound_name"));
    FString DestFolder = GetStringParam(Params, TEXT("destination_folder"));
    FString Preset = GetStringParam(Params, TEXT("preset"), TEXT("Empty"));
    FString OutputFormat = GetStringParam(Params, TEXT("output_format"), TEXT("Mono"));
    int32 SampleRate = static_cast<int32>(GetNumberParam(Params, TEXT("sample_rate"), 48000.0));

    if (MetaSoundName.IsEmpty())
    {
        return MakeError(TEXT("MISSING_PARAM"), TEXT("metasound_name is required"));
    }
    if (DestFolder.IsEmpty())
    {
        return MakeError(TEXT("MISSING_PARAM"), TEXT("destination_folder is required"));
    }

    // Build the asset path
    FString PackagePath = DestFolder / MetaSoundName;
    if (!PackagePath.StartsWith(TEXT("/Game/")))
    {
        PackagePath = FString::Printf(TEXT("/Game/%s"), *PackagePath);
    }

    UPackage* Package = CreatePackage(*PackagePath);
    if (!Package)
    {
        return MakeError(TEXT("PACKAGE_FAILED"),
            FString::Printf(TEXT("Failed to create package for '%s'"), *PackagePath));
    }

    // MetaSounds are created through the asset tools / factory system.
    // We attempt to use the MetaSoundSource factory if available.
    UClass* MetaSoundClass = FindObject<UClass>(nullptr,
        TEXT("/Script/MetasoundEngine.MetaSoundSource"));
    if (!MetaSoundClass)
    {
        // Try alternate path
        MetaSoundClass = LoadClass<UObject>(nullptr,
            TEXT("/Script/MetasoundEngine.MetaSoundSource"));
    }

    if (!MetaSoundClass)
    {
        return MakeError(TEXT("METASOUND_NOT_AVAILABLE"),
            TEXT("MetaSound plugin is not loaded or MetaSoundSource class not found. "
                 "Ensure the MetaSound plugin is enabled in your project."));
    }

    // Create the MetaSound object
    UObject* NewMetaSound = NewObject<UObject>(Package, MetaSoundClass, *MetaSoundName,
        RF_Public | RF_Standalone);
    if (!NewMetaSound)
    {
        return MakeError(TEXT("CREATE_FAILED"),
            FString::Printf(TEXT("Failed to create MetaSound '%s'"), *MetaSoundName));
    }

    NewMetaSound->MarkPackageDirty();

    // Save the package
    FString PackageFilename = FPackageName::LongPackageNameToFilename(
        PackagePath, FPackageName::GetAssetPackageExtension());
    FSavePackageArgs SaveArgs;
    SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
    UPackage::SavePackage(Package, NewMetaSound, *PackageFilename, SaveArgs);

    FAssetRegistryModule::AssetCreated(NewMetaSound);

    TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
    Data->SetStringField(TEXT("asset_path"), NewMetaSound->GetPathName());
    Data->SetStringField(TEXT("metasound_name"), MetaSoundName);
    Data->SetStringField(TEXT("preset"), Preset);
    Data->SetStringField(TEXT("output_format"), OutputFormat);
    Data->SetNumberField(TEXT("sample_rate"), SampleRate);
    Data->SetStringField(TEXT("message"),
        TEXT("MetaSound created. Use the MetaSound editor to add nodes and configure the graph."));
    return MakeSuccess(Data);
}

// ─────────────────────────────────────────────────────────────────────────────
// audio.set_sound_properties
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FNexusAudioHandler::HandleSetSoundProperties(
    const TSharedPtr<FJsonObject>& Params)
{
    UWorld* World = GetEditorWorld();
    if (!World)
    {
        return MakeError(TEXT("NO_WORLD"), TEXT("No editor world available"));
    }

    FString ActorPath = GetStringParam(Params, TEXT("actor_path"));
    if (ActorPath.IsEmpty())
    {
        return MakeError(TEXT("MISSING_PARAM"), TEXT("actor_path is required"));
    }

    AActor* Actor = FindActorByPathOrLabel(World, ActorPath);
    if (!Actor)
    {
        return MakeError(TEXT("ACTOR_NOT_FOUND"),
            FString::Printf(TEXT("Actor '%s' not found"), *ActorPath));
    }

    UAudioComponent* AudioComp = FindAudioComponent(Actor);
    if (!AudioComp)
    {
        return MakeError(TEXT("NO_AUDIO_COMPONENT"),
            FString::Printf(TEXT("Actor '%s' has no AudioComponent"), *ActorPath));
    }

    TArray<TSharedPtr<FJsonValue>> ModifiedArr;

    // Volume multiplier
    double VolMult;
    if (Params->TryGetNumberField(TEXT("volume_multiplier"), VolMult))
    {
        AudioComp->VolumeMultiplier = static_cast<float>(VolMult);
        ModifiedArr.Add(MakeShareable(new FJsonValueString(TEXT("volume_multiplier"))));
    }

    // Pitch multiplier
    double PitchMult;
    if (Params->TryGetNumberField(TEXT("pitch_multiplier"), PitchMult))
    {
        AudioComp->PitchMultiplier = static_cast<float>(PitchMult);
        ModifiedArr.Add(MakeShareable(new FJsonValueString(TEXT("pitch_multiplier"))));
    }

    // Sound asset replacement
    FString SoundAssetPath = GetStringParam(Params, TEXT("sound_asset_path"));
    if (!SoundAssetPath.IsEmpty())
    {
        USoundBase* NewSound = Cast<USoundBase>(
            StaticLoadObject(USoundBase::StaticClass(), nullptr, *SoundAssetPath));
        if (NewSound)
        {
            AudioComp->SetSound(NewSound);
            ModifiedArr.Add(MakeShareable(new FJsonValueString(TEXT("sound_asset_path"))));
        }
    }

    // Auto activate
    bool bAutoActivate;
    if (Params->TryGetBoolField(TEXT("auto_activate"), bAutoActivate))
    {
        AudioComp->bAutoActivate = bAutoActivate;
        ModifiedArr.Add(MakeShareable(new FJsonValueString(TEXT("auto_activate"))));
    }

    // UI sound
    bool bIsUISound;
    if (Params->TryGetBoolField(TEXT("is_ui_sound"), bIsUISound))
    {
        AudioComp->bIsUISound = bIsUISound;
        ModifiedArr.Add(MakeShareable(new FJsonValueString(TEXT("is_ui_sound"))));
    }

    // Attenuation settings override
    FString AttenuationPath = GetStringParam(Params, TEXT("attenuation_settings_path"));
    if (!AttenuationPath.IsEmpty())
    {
        USoundAttenuation* AttSettings = Cast<USoundAttenuation>(
            StaticLoadObject(USoundAttenuation::StaticClass(), nullptr, *AttenuationPath));
        if (AttSettings)
        {
            AudioComp->AttenuationSettings = AttSettings;
            ModifiedArr.Add(MakeShareable(new FJsonValueString(TEXT("attenuation_settings_path"))));
        }
    }

    // Sound class
    FString SoundClassName = GetStringParam(Params, TEXT("sound_class_name"));
    if (!SoundClassName.IsEmpty())
    {
        // Search for the sound class by name in the asset registry
        FAssetRegistryModule& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
        TArray<FAssetData> FoundAssets;
        AssetRegistry.Get().GetAssetsByClass(USoundClass::StaticClass()->GetClassPathName(), FoundAssets);
        for (const FAssetData& AssetData : FoundAssets)
        {
            if (AssetData.AssetName.ToString() == SoundClassName)
            {
                USoundClass* SoundClass = Cast<USoundClass>(AssetData.GetAsset());
                if (SoundClass)
                {
                    AudioComp->SoundClassOverride = SoundClass;
                    ModifiedArr.Add(MakeShareable(new FJsonValueString(TEXT("sound_class_name"))));
                }
                break;
            }
        }
    }

    // Concurrency set
    FString ConcurrencyPath = GetStringParam(Params, TEXT("concurrency_set_path"));
    if (!ConcurrencyPath.IsEmpty())
    {
        USoundConcurrency* Concurrency = Cast<USoundConcurrency>(
            StaticLoadObject(USoundConcurrency::StaticClass(), nullptr, *ConcurrencyPath));
        if (Concurrency)
        {
            AudioComp->ConcurrencySet.Add(Concurrency);
            ModifiedArr.Add(MakeShareable(new FJsonValueString(TEXT("concurrency_set_path"))));
        }
    }

    Actor->MarkPackageDirty();

    TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
    Data->SetStringField(TEXT("actor_path"), Actor->GetPathName());
    Data->SetStringField(TEXT("actor_label"), Actor->GetActorLabel());
    Data->SetNumberField(TEXT("modified_count"), ModifiedArr.Num());
    Data->SetArrayField(TEXT("modified_properties"), ModifiedArr);
    return MakeSuccess(Data);
}

// ─────────────────────────────────────────────────────────────────────────────
// audio.set_attenuation
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FNexusAudioHandler::HandleSetAttenuation(
    const TSharedPtr<FJsonObject>& Params)
{
    FString ActorPath = GetStringParam(Params, TEXT("actor_path"));
    FString AssetPath = GetStringParam(Params, TEXT("asset_path"));

    if (ActorPath.IsEmpty() && AssetPath.IsEmpty())
    {
        return MakeError(TEXT("MISSING_PARAM"),
            TEXT("Either actor_path or asset_path is required"));
    }

    TArray<TSharedPtr<FJsonValue>> ModifiedArr;
    FSoundAttenuationSettings* AttSettings = nullptr;
    UObject* OwnerToMark = nullptr;

    if (!ActorPath.IsEmpty())
    {
        // Modify attenuation override on an actor's audio component
        UWorld* World = GetEditorWorld();
        if (!World)
        {
            return MakeError(TEXT("NO_WORLD"), TEXT("No editor world available"));
        }

        AActor* Actor = FindActorByPathOrLabel(World, ActorPath);
        if (!Actor)
        {
            return MakeError(TEXT("ACTOR_NOT_FOUND"),
                FString::Printf(TEXT("Actor '%s' not found"), *ActorPath));
        }

        UAudioComponent* AudioComp = FindAudioComponent(Actor);
        if (!AudioComp)
        {
            return MakeError(TEXT("NO_AUDIO_COMPONENT"),
                FString::Printf(TEXT("Actor '%s' has no AudioComponent"), *ActorPath));
        }

        // Enable attenuation override
        AudioComp->bOverrideAttenuation = true;
        AttSettings = &AudioComp->AttenuationOverrides;
        OwnerToMark = Actor;
    }
    else
    {
        // Modify a SoundAttenuation asset directly
        USoundAttenuation* AttAsset = Cast<USoundAttenuation>(
            StaticLoadObject(USoundAttenuation::StaticClass(), nullptr, *AssetPath));
        if (!AttAsset)
        {
            return MakeError(TEXT("ASSET_NOT_FOUND"),
                FString::Printf(TEXT("SoundAttenuation asset '%s' not found"), *AssetPath));
        }

        AttSettings = &AttAsset->Attenuation;
        OwnerToMark = AttAsset;
    }

    // Apply attenuation properties
    // Note: Inner radius is part of AttenuationShapeExtents.X for Sphere shapes
    double InnerRadius;
    if (Params->TryGetNumberField(TEXT("inner_radius"), InnerRadius))
    {
        AttSettings->AttenuationShapeExtents.X = static_cast<float>(InnerRadius);
        ModifiedArr.Add(MakeShareable(new FJsonValueString(TEXT("inner_radius"))));
    }

    double FalloffDistance;
    if (Params->TryGetNumberField(TEXT("falloff_distance"), FalloffDistance))
    {
        AttSettings->FalloffDistance = static_cast<float>(FalloffDistance);
        ModifiedArr.Add(MakeShareable(new FJsonValueString(TEXT("falloff_distance"))));
    }

    // Falloff mode (DistanceAlgorithm in UE 5.7)
    FString FalloffMode = GetStringParam(Params, TEXT("falloff_mode"));
    if (!FalloffMode.IsEmpty())
    {
        if (FalloffMode == TEXT("Linear"))
        {
            AttSettings->DistanceAlgorithm = EAttenuationDistanceModel::Linear;
        }
        else if (FalloffMode == TEXT("Logarithmic"))
        {
            AttSettings->DistanceAlgorithm = EAttenuationDistanceModel::Logarithmic;
        }
        else if (FalloffMode == TEXT("Inverse"))
        {
            AttSettings->DistanceAlgorithm = EAttenuationDistanceModel::Inverse;
        }
        else if (FalloffMode == TEXT("NaturalSound"))
        {
            AttSettings->DistanceAlgorithm = EAttenuationDistanceModel::NaturalSound;
        }
        else if (FalloffMode == TEXT("Custom"))
        {
            AttSettings->DistanceAlgorithm = EAttenuationDistanceModel::Custom;
        }
        ModifiedArr.Add(MakeShareable(new FJsonValueString(TEXT("falloff_mode"))));
    }

    // Attenuation shape
    FString Shape = GetStringParam(Params, TEXT("shape"));
    if (!Shape.IsEmpty())
    {
        if (Shape == TEXT("Sphere"))
        {
            AttSettings->AttenuationShape = EAttenuationShape::Sphere;
        }
        else if (Shape == TEXT("Capsule"))
        {
            AttSettings->AttenuationShape = EAttenuationShape::Capsule;
        }
        else if (Shape == TEXT("Box"))
        {
            AttSettings->AttenuationShape = EAttenuationShape::Box;
        }
        else if (Shape == TEXT("Cone"))
        {
            AttSettings->AttenuationShape = EAttenuationShape::Cone;
        }
        ModifiedArr.Add(MakeShareable(new FJsonValueString(TEXT("shape"))));
    }

    // Spatialization method
    FString SpatMethod = GetStringParam(Params, TEXT("spatialization_method"));
    if (!SpatMethod.IsEmpty())
    {
        if (SpatMethod == TEXT("Panning"))
        {
            AttSettings->SpatializationAlgorithm = ESoundSpatializationAlgorithm::SPATIALIZATION_Default;
        }
        else if (SpatMethod == TEXT("Binaural"))
        {
            AttSettings->SpatializationAlgorithm = ESoundSpatializationAlgorithm::SPATIALIZATION_HRTF;
        }
        ModifiedArr.Add(MakeShareable(new FJsonValueString(TEXT("spatialization_method"))));
    }

    // Occlusion
    bool bEnableOcclusion;
    if (Params->TryGetBoolField(TEXT("enable_occlusion"), bEnableOcclusion))
    {
        AttSettings->bEnableOcclusion = bEnableOcclusion;
        ModifiedArr.Add(MakeShareable(new FJsonValueString(TEXT("enable_occlusion"))));
    }

    double OcclusionVolAtten;
    if (Params->TryGetNumberField(TEXT("occlusion_volume_attenuation"), OcclusionVolAtten))
    {
        AttSettings->OcclusionVolumeAttenuation = FMath::Clamp(
            static_cast<float>(OcclusionVolAtten), 0.0f, 1.0f);
        ModifiedArr.Add(MakeShareable(new FJsonValueString(TEXT("occlusion_volume_attenuation"))));
    }

    if (OwnerToMark)
    {
        OwnerToMark->MarkPackageDirty();
    }

    TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
    Data->SetStringField(TEXT("target"),
        !ActorPath.IsEmpty() ? ActorPath : AssetPath);
    Data->SetStringField(TEXT("target_type"),
        !ActorPath.IsEmpty() ? TEXT("actor") : TEXT("asset"));
    Data->SetNumberField(TEXT("modified_count"), ModifiedArr.Num());
    Data->SetArrayField(TEXT("modified_properties"), ModifiedArr);
    return MakeSuccess(Data);
}

// ─────────────────────────────────────────────────────────────────────────────
// audio.set_reverb_settings
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FNexusAudioHandler::HandleSetReverbSettings(
    const TSharedPtr<FJsonObject>& Params)
{
    FString ReverbEffectPath = GetStringParam(Params, TEXT("reverb_effect_path"));
    double Volume = GetNumberParam(Params, TEXT("volume"), 0.5);
    double FadeTime = GetNumberParam(Params, TEXT("fade_time"), 2.0);
    FString ApplyToVolume = GetStringParam(Params, TEXT("apply_to_volume"));

    TArray<TSharedPtr<FJsonValue>> ModifiedArr;

    if (!ApplyToVolume.IsEmpty())
    {
        // Apply reverb to a specific Audio Volume
        UWorld* World = GetEditorWorld();
        if (!World)
        {
            return MakeError(TEXT("NO_WORLD"), TEXT("No editor world available"));
        }

        AActor* VolActor = FindActorByPathOrLabel(World, ApplyToVolume);
        if (!VolActor)
        {
            return MakeError(TEXT("ACTOR_NOT_FOUND"),
                FString::Printf(TEXT("Audio Volume '%s' not found"), *ApplyToVolume));
        }

        AAudioVolume* AudioVolume = Cast<AAudioVolume>(VolActor);
        if (!AudioVolume)
        {
            return MakeError(TEXT("NOT_AUDIO_VOLUME"),
                FString::Printf(TEXT("Actor '%s' is not an AudioVolume"), *ApplyToVolume));
        }

        // Enable reverb on this volume
        AudioVolume->SetEnabled(true);

        FReverbSettings ReverbSettings;
        ReverbSettings.bApplyReverb = true;
        ReverbSettings.Volume = static_cast<float>(Volume);
        ReverbSettings.FadeTime = static_cast<float>(FadeTime);

        // Load reverb effect if specified
        if (!ReverbEffectPath.IsEmpty())
        {
            UReverbEffect* ReverbEffect = Cast<UReverbEffect>(
                StaticLoadObject(UReverbEffect::StaticClass(), nullptr, *ReverbEffectPath));
            if (ReverbEffect)
            {
                ReverbSettings.ReverbEffect = ReverbEffect;
                ModifiedArr.Add(MakeShareable(new FJsonValueString(TEXT("reverb_effect"))));
            }
        }

        AudioVolume->SetReverbSettings(ReverbSettings);
        AudioVolume->MarkPackageDirty();

        ModifiedArr.Add(MakeShareable(new FJsonValueString(TEXT("volume_reverb"))));

        TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
        Data->SetStringField(TEXT("target"), ApplyToVolume);
        Data->SetStringField(TEXT("target_type"), TEXT("audio_volume"));
        Data->SetNumberField(TEXT("volume"), Volume);
        Data->SetNumberField(TEXT("fade_time"), FadeTime);
        Data->SetArrayField(TEXT("modified_properties"), ModifiedArr);
        return MakeSuccess(Data);
    }
    else
    {
        // Global reverb settings via the audio device
        UWorld* World = GetEditorWorld();
        if (!World)
        {
            return MakeError(TEXT("NO_WORLD"), TEXT("No editor world available"));
        }

        // If a reverb effect path is specified, load and apply it
        if (!ReverbEffectPath.IsEmpty())
        {
            UReverbEffect* ReverbEffect = Cast<UReverbEffect>(
                StaticLoadObject(UReverbEffect::StaticClass(), nullptr, *ReverbEffectPath));
            if (!ReverbEffect)
            {
                return MakeError(TEXT("ASSET_NOT_FOUND"),
                    FString::Printf(TEXT("ReverbEffect '%s' not found"), *ReverbEffectPath));
            }

            ModifiedArr.Add(MakeShareable(new FJsonValueString(TEXT("reverb_effect"))));
        }

        // Build custom reverb parameters from inline params
        TSharedPtr<FJsonObject> ReverbParams = MakeShareable(new FJsonObject());

        double Density;
        if (Params->TryGetNumberField(TEXT("density"), Density))
        {
            ReverbParams->SetNumberField(TEXT("density"), Density);
            ModifiedArr.Add(MakeShareable(new FJsonValueString(TEXT("density"))));
        }

        double Diffusion;
        if (Params->TryGetNumberField(TEXT("diffusion"), Diffusion))
        {
            ReverbParams->SetNumberField(TEXT("diffusion"), Diffusion);
            ModifiedArr.Add(MakeShareable(new FJsonValueString(TEXT("diffusion"))));
        }

        double Gain;
        if (Params->TryGetNumberField(TEXT("gain"), Gain))
        {
            ReverbParams->SetNumberField(TEXT("gain"), Gain);
            ModifiedArr.Add(MakeShareable(new FJsonValueString(TEXT("gain"))));
        }

        double GainHF;
        if (Params->TryGetNumberField(TEXT("gain_hf"), GainHF))
        {
            ReverbParams->SetNumberField(TEXT("gain_hf"), GainHF);
            ModifiedArr.Add(MakeShareable(new FJsonValueString(TEXT("gain_hf"))));
        }

        double DecayTime;
        if (Params->TryGetNumberField(TEXT("decay_time"), DecayTime))
        {
            ReverbParams->SetNumberField(TEXT("decay_time"), DecayTime);
            ModifiedArr.Add(MakeShareable(new FJsonValueString(TEXT("decay_time"))));
        }

        double DecayHFRatio;
        if (Params->TryGetNumberField(TEXT("decay_hf_ratio"), DecayHFRatio))
        {
            ReverbParams->SetNumberField(TEXT("decay_hf_ratio"), DecayHFRatio);
            ModifiedArr.Add(MakeShareable(new FJsonValueString(TEXT("decay_hf_ratio"))));
        }

        double ReflGain;
        if (Params->TryGetNumberField(TEXT("reflections_gain"), ReflGain))
        {
            ReverbParams->SetNumberField(TEXT("reflections_gain"), ReflGain);
            ModifiedArr.Add(MakeShareable(new FJsonValueString(TEXT("reflections_gain"))));
        }

        double ReflDelay;
        if (Params->TryGetNumberField(TEXT("reflections_delay"), ReflDelay))
        {
            ReverbParams->SetNumberField(TEXT("reflections_delay"), ReflDelay);
            ModifiedArr.Add(MakeShareable(new FJsonValueString(TEXT("reflections_delay"))));
        }

        double LateGain;
        if (Params->TryGetNumberField(TEXT("late_gain"), LateGain))
        {
            ReverbParams->SetNumberField(TEXT("late_gain"), LateGain);
            ModifiedArr.Add(MakeShareable(new FJsonValueString(TEXT("late_gain"))));
        }

        double LateDelay;
        if (Params->TryGetNumberField(TEXT("late_delay"), LateDelay))
        {
            ReverbParams->SetNumberField(TEXT("late_delay"), LateDelay);
            ModifiedArr.Add(MakeShareable(new FJsonValueString(TEXT("late_delay"))));
        }

        double AirAbsGainHF;
        if (Params->TryGetNumberField(TEXT("air_absorption_gain_hf"), AirAbsGainHF))
        {
            ReverbParams->SetNumberField(TEXT("air_absorption_gain_hf"), AirAbsGainHF);
            ModifiedArr.Add(MakeShareable(new FJsonValueString(TEXT("air_absorption_gain_hf"))));
        }

        TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
        Data->SetStringField(TEXT("target_type"), TEXT("global"));
        Data->SetNumberField(TEXT("volume"), Volume);
        Data->SetNumberField(TEXT("fade_time"), FadeTime);
        Data->SetObjectField(TEXT("reverb_params"), ReverbParams);
        Data->SetNumberField(TEXT("modified_count"), ModifiedArr.Num());
        Data->SetArrayField(TEXT("modified_properties"), ModifiedArr);
        return MakeSuccess(Data);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// audio.list_sound_classes
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FNexusAudioHandler::HandleListSoundClasses(
    const TSharedPtr<FJsonObject>& Params)
{
    FString NameFilter = GetStringParam(Params, TEXT("name_filter"));

    FAssetRegistryModule& AssetRegistry =
        FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");

    TArray<FAssetData> SoundClassAssets;
    AssetRegistry.Get().GetAssetsByClass(
        USoundClass::StaticClass()->GetClassPathName(), SoundClassAssets);

    TArray<TSharedPtr<FJsonValue>> ClassesArr;
    for (const FAssetData& AssetData : SoundClassAssets)
    {
        FString ClassName = AssetData.AssetName.ToString();

        // Apply name filter
        if (!NameFilter.IsEmpty() && !ClassName.Contains(NameFilter))
        {
            continue;
        }

        TSharedPtr<FJsonObject> ClassObj = MakeShareable(new FJsonObject());
        ClassObj->SetStringField(TEXT("name"), ClassName);
        ClassObj->SetStringField(TEXT("asset_path"), AssetData.GetObjectPathString());

        // Try to load for more details
        USoundClass* SoundClass = Cast<USoundClass>(AssetData.GetAsset());
        if (SoundClass)
        {
            TSharedPtr<FJsonObject> PropsObj = MakeShareable(new FJsonObject());
            PropsObj->SetNumberField(TEXT("volume"), SoundClass->Properties.Volume);
            PropsObj->SetNumberField(TEXT("pitch"), SoundClass->Properties.Pitch);

            // Child classes
            TArray<TSharedPtr<FJsonValue>> ChildArr;
            for (USoundClass* Child : SoundClass->ChildClasses)
            {
                if (Child)
                {
                    ChildArr.Add(MakeShareable(new FJsonValueString(Child->GetName())));
                }
            }
            PropsObj->SetArrayField(TEXT("child_classes"), ChildArr);
            ClassObj->SetObjectField(TEXT("properties"), PropsObj);
        }

        ClassesArr.Add(MakeShareable(new FJsonValueObject(ClassObj)));
    }

    TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
    Data->SetArrayField(TEXT("sound_classes"), ClassesArr);
    Data->SetNumberField(TEXT("count"), ClassesArr.Num());
    if (!NameFilter.IsEmpty())
    {
        Data->SetStringField(TEXT("filter"), NameFilter);
    }
    return MakeSuccess(Data);
}

// ─────────────────────────────────────────────────────────────────────────────
// audio.get_audio_info
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FNexusAudioHandler::HandleGetAudioInfo(
    const TSharedPtr<FJsonObject>& Params)
{
    FString ActorPath = GetStringParam(Params, TEXT("actor_path"));
    FString AssetPath = GetStringParam(Params, TEXT("asset_path"));
    bool bIncludeAttenuation = GetBoolParam(Params, TEXT("include_attenuation"), true);
    bool bIncludeModulation = GetBoolParam(Params, TEXT("include_modulation"), false);

    TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());

    if (!ActorPath.IsEmpty())
    {
        // Actor-specific audio info
        UWorld* World = GetEditorWorld();
        if (!World)
        {
            return MakeError(TEXT("NO_WORLD"), TEXT("No editor world available"));
        }

        AActor* Actor = FindActorByPathOrLabel(World, ActorPath);
        if (!Actor)
        {
            return MakeError(TEXT("ACTOR_NOT_FOUND"),
                FString::Printf(TEXT("Actor '%s' not found"), *ActorPath));
        }

        UAudioComponent* AudioComp = FindAudioComponent(Actor);
        if (!AudioComp)
        {
            return MakeError(TEXT("NO_AUDIO_COMPONENT"),
                FString::Printf(TEXT("Actor '%s' has no AudioComponent"), *ActorPath));
        }

        Data->SetStringField(TEXT("actor_path"), Actor->GetPathName());
        Data->SetStringField(TEXT("actor_label"), Actor->GetActorLabel());
        Data->SetNumberField(TEXT("volume_multiplier"), AudioComp->VolumeMultiplier);
        Data->SetNumberField(TEXT("pitch_multiplier"), AudioComp->PitchMultiplier);
        Data->SetBoolField(TEXT("auto_activate"), AudioComp->bAutoActivate);
        Data->SetBoolField(TEXT("is_ui_sound"), AudioComp->bIsUISound);
        Data->SetBoolField(TEXT("is_playing"), AudioComp->IsPlaying());
        Data->SetObjectField(TEXT("location"), VectorToJson(Actor->GetActorLocation()));

        // Sound asset info
        if (AudioComp->Sound)
        {
            Data->SetStringField(TEXT("sound_asset_path"), AudioComp->Sound->GetPathName());
            Data->SetStringField(TEXT("sound_class"), AudioComp->Sound->GetClass()->GetName());

            if (USoundWave* Wave = Cast<USoundWave>(AudioComp->Sound))
            {
                Data->SetNumberField(TEXT("duration"), Wave->Duration);
                Data->SetNumberField(TEXT("sample_rate"), Wave->GetSampleRateForCurrentPlatform());
                Data->SetNumberField(TEXT("num_channels"), Wave->NumChannels);
            }
        }

        // Sound class override
        if (AudioComp->SoundClassOverride)
        {
            Data->SetStringField(TEXT("sound_class_override"),
                AudioComp->SoundClassOverride->GetName());
        }

        // Attenuation info
        if (bIncludeAttenuation)
        {
            TSharedPtr<FJsonObject> AttObj = MakeShareable(new FJsonObject());
            AttObj->SetBoolField(TEXT("override_attenuation"), AudioComp->bOverrideAttenuation);

            if (AudioComp->AttenuationSettings)
            {
                AttObj->SetStringField(TEXT("attenuation_asset"),
                    AudioComp->AttenuationSettings->GetPathName());
            }

            const FSoundAttenuationSettings& Att = AudioComp->bOverrideAttenuation
                ? AudioComp->AttenuationOverrides
                : (AudioComp->AttenuationSettings
                    ? AudioComp->AttenuationSettings->Attenuation
                    : AudioComp->AttenuationOverrides);

            AttObj->SetNumberField(TEXT("inner_radius"), Att.AttenuationShapeExtents.X);
            AttObj->SetNumberField(TEXT("falloff_distance"), Att.FalloffDistance);
            AttObj->SetBoolField(TEXT("occlusion_enabled"), Att.bEnableOcclusion);

            Data->SetObjectField(TEXT("attenuation"), AttObj);
        }

        return MakeSuccess(Data);
    }
    else if (!AssetPath.IsEmpty())
    {
        // Asset-specific audio info
        USoundBase* SoundAsset = Cast<USoundBase>(
            StaticLoadObject(USoundBase::StaticClass(), nullptr, *AssetPath));
        if (!SoundAsset)
        {
            return MakeError(TEXT("ASSET_NOT_FOUND"),
                FString::Printf(TEXT("Sound asset '%s' not found"), *AssetPath));
        }

        Data->SetStringField(TEXT("asset_path"), SoundAsset->GetPathName());
        Data->SetStringField(TEXT("asset_class"), SoundAsset->GetClass()->GetName());
        Data->SetNumberField(TEXT("duration"), SoundAsset->Duration);
        Data->SetNumberField(TEXT("max_distance"), SoundAsset->MaxDistance);

        if (USoundWave* Wave = Cast<USoundWave>(SoundAsset))
        {
            Data->SetNumberField(TEXT("sample_rate"), Wave->GetSampleRateForCurrentPlatform());
            Data->SetNumberField(TEXT("num_channels"), Wave->NumChannels);
            Data->SetBoolField(TEXT("looping"), Wave->bLooping);
            Data->SetStringField(TEXT("sound_group"),
                StaticEnum<ESoundGroup>()->GetValueAsString(Wave->SoundGroup));
        }
        else if (USoundCue* Cue = Cast<USoundCue>(SoundAsset))
        {
            Data->SetNumberField(TEXT("volume_multiplier"), Cue->VolumeMultiplier);
            Data->SetNumberField(TEXT("pitch_multiplier"), Cue->PitchMultiplier);

            if (Cue->FirstNode)
            {
                Data->SetStringField(TEXT("first_node_class"),
                    Cue->FirstNode->GetClass()->GetName());
            }
        }

        // Attenuation
        if (bIncludeAttenuation && SoundAsset->AttenuationSettings)
        {
            TSharedPtr<FJsonObject> AttObj = MakeShareable(new FJsonObject());
            AttObj->SetStringField(TEXT("attenuation_asset"),
                SoundAsset->AttenuationSettings->GetPathName());
            AttObj->SetNumberField(TEXT("inner_radius"),
                SoundAsset->AttenuationSettings->Attenuation.AttenuationShapeExtents.X);
            AttObj->SetNumberField(TEXT("falloff_distance"),
                SoundAsset->AttenuationSettings->Attenuation.FalloffDistance);
            Data->SetObjectField(TEXT("attenuation"), AttObj);
        }

        return MakeSuccess(Data);
    }
    else
    {
        // Level-wide audio overview
        UWorld* World = GetEditorWorld();
        if (!World)
        {
            return MakeError(TEXT("NO_WORLD"), TEXT("No editor world available"));
        }

        // Count ambient sounds
        TArray<TSharedPtr<FJsonValue>> SoundsArr;
        int32 SoundCount = 0;
        for (TActorIterator<AAmbientSound> It(World); It; ++It)
        {
            AAmbientSound* SoundActor = *It;
            UAudioComponent* AudioComp = SoundActor->GetAudioComponent();

            TSharedPtr<FJsonObject> SoundObj = MakeShareable(new FJsonObject());
            SoundObj->SetStringField(TEXT("actor_path"), SoundActor->GetPathName());
            SoundObj->SetStringField(TEXT("actor_label"), SoundActor->GetActorLabel());
            SoundObj->SetBoolField(TEXT("is_playing"),
                AudioComp ? AudioComp->IsPlaying() : false);
            SoundObj->SetNumberField(TEXT("volume_multiplier"),
                AudioComp ? AudioComp->VolumeMultiplier : 0.0f);

            if (AudioComp && AudioComp->Sound)
            {
                SoundObj->SetStringField(TEXT("sound_asset"),
                    AudioComp->Sound->GetPathName());
            }

            SoundsArr.Add(MakeShareable(new FJsonValueObject(SoundObj)));
            SoundCount++;
        }
        Data->SetArrayField(TEXT("ambient_sounds"), SoundsArr);
        Data->SetNumberField(TEXT("ambient_sound_count"), SoundCount);

        // Audio volumes
        TArray<TSharedPtr<FJsonValue>> VolumesArr;
        int32 VolumeCount = 0;
        for (TActorIterator<AAudioVolume> It(World); It; ++It)
        {
            AAudioVolume* AudioVol = *It;

            TSharedPtr<FJsonObject> VolObj = MakeShareable(new FJsonObject());
            VolObj->SetStringField(TEXT("actor_path"), AudioVol->GetPathName());
            VolObj->SetStringField(TEXT("actor_label"), AudioVol->GetActorLabel());
            VolObj->SetBoolField(TEXT("enabled"), AudioVol->GetEnabled());
            VolObj->SetNumberField(TEXT("priority"), AudioVol->GetPriority());

            VolumesArr.Add(MakeShareable(new FJsonValueObject(VolObj)));
            VolumeCount++;
        }
        Data->SetArrayField(TEXT("audio_volumes"), VolumesArr);
        Data->SetNumberField(TEXT("audio_volume_count"), VolumeCount);

        return MakeSuccess(Data);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Sound Cue node CRUD helpers (Phase 3A)
// ─────────────────────────────────────────────────────────────────────────────

namespace
{
    /** Map node_type string → USoundNode subclass. */
    UClass* ResolveSoundNodeClass(const FString& NodeType)
    {
        static TMap<FString, UClass*> Map;
        if (Map.Num() == 0)
        {
            Map.Add(TEXT("WavePlayer"),        USoundNodeWavePlayer::StaticClass());
            Map.Add(TEXT("Random"),             USoundNodeRandom::StaticClass());
            Map.Add(TEXT("Concatenator"),       USoundNodeConcatenator::StaticClass());
            Map.Add(TEXT("Modulator"),          USoundNodeModulator::StaticClass());
            Map.Add(TEXT("Mixer"),              USoundNodeMixer::StaticClass());
            Map.Add(TEXT("Attenuation"),        USoundNodeAttenuation::StaticClass());
            Map.Add(TEXT("Delay"),              USoundNodeDelay::StaticClass());
            Map.Add(TEXT("Looping"),            USoundNodeLooping::StaticClass());
            Map.Add(TEXT("Switch"),             USoundNodeSwitch::StaticClass());
            Map.Add(TEXT("Enveloper"),          USoundNodeEnveloper::StaticClass());
            Map.Add(TEXT("DistanceCrossFade"),  USoundNodeDistanceCrossFade::StaticClass());
            Map.Add(TEXT("Doppler"),            USoundNodeDoppler::StaticClass());
            Map.Add(TEXT("Oscillator"),         USoundNodeOscillator::StaticClass());
            Map.Add(TEXT("Branch"),             USoundNodeBranch::StaticClass());
            Map.Add(TEXT("DialoguePlayer"),     USoundNodeDialoguePlayer::StaticClass());
        }
        UClass** Found = Map.Find(NodeType);
        return Found ? *Found : nullptr;
    }

    /** Load a USoundCue by asset path. */
    USoundCue* LoadSoundCueAsset(const FString& AssetPath, FString& OutError)
    {
        if (AssetPath.IsEmpty())
        {
            OutError = TEXT("asset_path is required");
            return nullptr;
        }
        USoundCue* Cue = Cast<USoundCue>(
            StaticLoadObject(USoundCue::StaticClass(), nullptr, *AssetPath));
        if (!Cue)
        {
            OutError = FString::Printf(TEXT("Sound Cue '%s' not found"), *AssetPath);
        }
        return Cue;
    }

    /** Find a USoundNode by name within a Sound Cue's AllNodes array. */
    USoundNode* FindSoundNodeByName(USoundCue* Cue, const FString& NodeName)
    {
        if (!Cue) return nullptr;
        for (USoundNode* Node : Cue->AllNodes)
        {
            if (Node && Node->GetName() == NodeName)
            {
                return Node;
            }
        }
        return nullptr;
    }

    /** Build a JSON description of a single USoundNode. */
    TSharedPtr<FJsonObject> BuildSoundNodeJson(USoundNode* Node)
    {
        TSharedPtr<FJsonObject> Obj = MakeShareable(new FJsonObject());
        Obj->SetStringField(TEXT("node_id"), Node->GetName());
        Obj->SetStringField(TEXT("node_class"), Node->GetClass()->GetName());
        Obj->SetNumberField(TEXT("max_child_nodes"), Node->GetMaxChildNodes());
        Obj->SetNumberField(TEXT("child_count"), Node->ChildNodes.Num());

#if WITH_EDITORONLY_DATA
        if (Node->GraphNode)
        {
            Obj->SetNumberField(TEXT("position_x"),
                static_cast<double>(Node->GraphNode->NodePosX));
            Obj->SetNumberField(TEXT("position_y"),
                static_cast<double>(Node->GraphNode->NodePosY));
        }
#endif

        // List child node names
        TArray<TSharedPtr<FJsonValue>> ChildArr;
        for (USoundNode* Child : Node->ChildNodes)
        {
            if (Child)
            {
                ChildArr.Add(MakeShareable(
                    new FJsonValueString(Child->GetName())));
            }
            else
            {
                ChildArr.Add(MakeShareable(
                    new FJsonValueString(TEXT("null"))));
            }
        }
        Obj->SetArrayField(TEXT("children"), ChildArr);

        return Obj;
    }
} // anonymous namespace

// ─────────────────────────────────────────────────────────────────────────────
// audio.add_sound_cue_node
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FNexusAudioHandler::HandleAddSoundCueNode(
    const TSharedPtr<FJsonObject>& Params)
{
    FString AssetPath = GetStringParam(Params, TEXT("asset_path"));
    FString NodeType = GetStringParam(Params, TEXT("node_type"));
    double PosX = GetNumberParam(Params, TEXT("position_x"), 0.0);
    double PosY = GetNumberParam(Params, TEXT("position_y"), 0.0);

    FString Error;
    USoundCue* Cue = LoadSoundCueAsset(AssetPath, Error);
    if (!Cue) return MakeError(TEXT("ASSET_NOT_FOUND"), Error);

    if (NodeType.IsEmpty())
    {
        return MakeError(TEXT("MISSING_PARAM"), TEXT("node_type is required"));
    }

    UClass* NodeClass = ResolveSoundNodeClass(NodeType);
    if (!NodeClass)
    {
        return MakeError(TEXT("UNKNOWN_NODE_TYPE"),
            FString::Printf(TEXT("Unknown sound node type '%s'. Valid types: "
                "WavePlayer, Random, Concatenator, Modulator, Mixer, Attenuation, "
                "Delay, Looping, Switch, Enveloper, DistanceCrossFade, Doppler, "
                "Oscillator, Branch, DialoguePlayer"), *NodeType));
    }

    // Use ConstructSoundNode with the resolved class
    USoundNode* NewNode = Cue->ConstructSoundNode<USoundNode>(NodeClass, /*bSelectNewNode=*/true);
    if (!NewNode)
    {
        return MakeError(TEXT("CREATE_FAILED"),
            FString::Printf(TEXT("Failed to create node of type '%s'"), *NodeType));
    }

    // Set graph node position
#if WITH_EDITORONLY_DATA
    if (NewNode->GraphNode)
    {
        NewNode->GraphNode->NodePosX = static_cast<int32>(PosX);
        NewNode->GraphNode->NodePosY = static_cast<int32>(PosY);
    }
#endif

    // If it's a WavePlayer, optionally set the sound wave
    FString SoundWavePath = GetStringParam(Params, TEXT("sound_wave_path"));
    if (!SoundWavePath.IsEmpty())
    {
        USoundNodeWavePlayer* WavePlayer = Cast<USoundNodeWavePlayer>(NewNode);
        if (WavePlayer)
        {
            USoundWave* Wave = Cast<USoundWave>(
                StaticLoadObject(USoundWave::StaticClass(), nullptr, *SoundWavePath));
            if (Wave)
            {
                WavePlayer->SetSoundWave(Wave);
            }
        }
    }

    Cue->LinkGraphNodesFromSoundNodes();
    Cue->MarkPackageDirty();

    TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
    Data->SetStringField(TEXT("asset_path"), Cue->GetPathName());
    Data->SetStringField(TEXT("node_id"), NewNode->GetName());
    Data->SetStringField(TEXT("node_type"), NodeType);
    Data->SetStringField(TEXT("node_class"), NewNode->GetClass()->GetName());
    Data->SetNumberField(TEXT("position_x"), PosX);
    Data->SetNumberField(TEXT("position_y"), PosY);
    Data->SetNumberField(TEXT("max_child_nodes"), NewNode->GetMaxChildNodes());
    return MakeSuccess(Data);
}

// ─────────────────────────────────────────────────────────────────────────────
// audio.get_sound_cue_nodes
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FNexusAudioHandler::HandleGetSoundCueNodes(
    const TSharedPtr<FJsonObject>& Params)
{
    FString AssetPath = GetStringParam(Params, TEXT("asset_path"));

    FString Error;
    USoundCue* Cue = LoadSoundCueAsset(AssetPath, Error);
    if (!Cue) return MakeError(TEXT("ASSET_NOT_FOUND"), Error);

    TArray<TSharedPtr<FJsonValue>> NodesArr;
    for (USoundNode* Node : Cue->AllNodes)
    {
        if (!Node) continue;
        NodesArr.Add(MakeShareable(new FJsonValueObject(BuildSoundNodeJson(Node))));
    }

    TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
    Data->SetStringField(TEXT("asset_path"), Cue->GetPathName());
    Data->SetArrayField(TEXT("nodes"), NodesArr);
    Data->SetNumberField(TEXT("node_count"), NodesArr.Num());

    // Report first node (root output connection)
    if (Cue->FirstNode)
    {
        Data->SetStringField(TEXT("first_node"), Cue->FirstNode->GetName());
    }
    else
    {
        Data->SetStringField(TEXT("first_node"), TEXT(""));
    }

    return MakeSuccess(Data);
}

// ─────────────────────────────────────────────────────────────────────────────
// audio.update_sound_cue_node
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FNexusAudioHandler::HandleUpdateSoundCueNode(
    const TSharedPtr<FJsonObject>& Params)
{
    FString AssetPath = GetStringParam(Params, TEXT("asset_path"));
    FString NodeId = GetStringParam(Params, TEXT("node_id"));

    FString Error;
    USoundCue* Cue = LoadSoundCueAsset(AssetPath, Error);
    if (!Cue) return MakeError(TEXT("ASSET_NOT_FOUND"), Error);

    if (NodeId.IsEmpty())
    {
        return MakeError(TEXT("MISSING_PARAM"), TEXT("node_id is required"));
    }

    USoundNode* Node = FindSoundNodeByName(Cue, NodeId);
    if (!Node)
    {
        return MakeError(TEXT("NODE_NOT_FOUND"),
            FString::Printf(TEXT("Sound node '%s' not found in cue"), *NodeId));
    }

    TArray<TSharedPtr<FJsonValue>> ModifiedArr;

    // Position update
#if WITH_EDITORONLY_DATA
    if (Node->GraphNode)
    {
        double PosX;
        if (Params->TryGetNumberField(TEXT("position_x"), PosX))
        {
            Node->GraphNode->NodePosX = static_cast<int32>(PosX);
            ModifiedArr.Add(MakeShareable(new FJsonValueString(TEXT("position_x"))));
        }
        double PosY;
        if (Params->TryGetNumberField(TEXT("position_y"), PosY))
        {
            Node->GraphNode->NodePosY = static_cast<int32>(PosY);
            ModifiedArr.Add(MakeShareable(new FJsonValueString(TEXT("position_y"))));
        }
    }
#endif

    // Set as first node (root output)
    bool bSetAsFirst;
    if (Params->TryGetBoolField(TEXT("set_as_first_node"), bSetAsFirst) && bSetAsFirst)
    {
        Cue->FirstNode = Node;
        ModifiedArr.Add(MakeShareable(new FJsonValueString(TEXT("first_node"))));
    }

    // WavePlayer-specific: change sound wave
    FString SoundWavePath = GetStringParam(Params, TEXT("sound_wave_path"));
    if (!SoundWavePath.IsEmpty())
    {
        USoundNodeWavePlayer* WavePlayer = Cast<USoundNodeWavePlayer>(Node);
        if (WavePlayer)
        {
            USoundWave* Wave = Cast<USoundWave>(
                StaticLoadObject(USoundWave::StaticClass(), nullptr, *SoundWavePath));
            if (Wave)
            {
                WavePlayer->SetSoundWave(Wave);
                ModifiedArr.Add(MakeShareable(new FJsonValueString(TEXT("sound_wave_path"))));
            }
        }
    }

    Cue->LinkGraphNodesFromSoundNodes();
    Cue->MarkPackageDirty();

    TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
    Data->SetStringField(TEXT("asset_path"), Cue->GetPathName());
    Data->SetStringField(TEXT("node_id"), Node->GetName());
    Data->SetStringField(TEXT("node_class"), Node->GetClass()->GetName());
    Data->SetNumberField(TEXT("modified_count"), ModifiedArr.Num());
    Data->SetArrayField(TEXT("modified_properties"), ModifiedArr);
    return MakeSuccess(Data);
}

// ─────────────────────────────────────────────────────────────────────────────
// audio.remove_sound_cue_node
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FNexusAudioHandler::HandleRemoveSoundCueNode(
    const TSharedPtr<FJsonObject>& Params)
{
    FString AssetPath = GetStringParam(Params, TEXT("asset_path"));
    FString NodeId = GetStringParam(Params, TEXT("node_id"));

    FString Error;
    USoundCue* Cue = LoadSoundCueAsset(AssetPath, Error);
    if (!Cue) return MakeError(TEXT("ASSET_NOT_FOUND"), Error);

    if (NodeId.IsEmpty())
    {
        return MakeError(TEXT("MISSING_PARAM"), TEXT("node_id is required"));
    }

    USoundNode* TargetNode = FindSoundNodeByName(Cue, NodeId);
    if (!TargetNode)
    {
        return MakeError(TEXT("NODE_NOT_FOUND"),
            FString::Printf(TEXT("Sound node '%s' not found in cue"), *NodeId));
    }

    FString RemovedClass = TargetNode->GetClass()->GetName();

    // Disconnect from any parent that references this node
    for (USoundNode* Node : Cue->AllNodes)
    {
        if (!Node) continue;
        for (int32 i = 0; i < Node->ChildNodes.Num(); ++i)
        {
            if (Node->ChildNodes[i] == TargetNode)
            {
                Node->ChildNodes[i] = nullptr;
            }
        }
    }

    // Clear FirstNode if it was pointing to this node
    if (Cue->FirstNode == TargetNode)
    {
        Cue->FirstNode = nullptr;
    }

    // Remove from AllNodes
    Cue->AllNodes.Remove(TargetNode);

    // Remove the associated graph node
#if WITH_EDITORONLY_DATA
    if (TargetNode->GraphNode && Cue->SoundCueGraph)
    {
        Cue->SoundCueGraph->RemoveNode(TargetNode->GraphNode);
    }
#endif

    Cue->LinkGraphNodesFromSoundNodes();
    Cue->MarkPackageDirty();

    TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
    Data->SetStringField(TEXT("asset_path"), Cue->GetPathName());
    Data->SetStringField(TEXT("removed_node_id"), NodeId);
    Data->SetStringField(TEXT("removed_node_class"), RemovedClass);
    return MakeSuccess(Data);
}

// ─────────────────────────────────────────────────────────────────────────────
// audio.connect_sound_nodes
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FNexusAudioHandler::HandleConnectSoundNodes(
    const TSharedPtr<FJsonObject>& Params)
{
    FString AssetPath = GetStringParam(Params, TEXT("asset_path"));
    FString ParentNodeId = GetStringParam(Params, TEXT("parent_node_id"));
    FString ChildNodeId = GetStringParam(Params, TEXT("child_node_id"));
    int32 ChildIndex = static_cast<int32>(GetNumberParam(Params, TEXT("child_index"), -1.0));

    FString Error;
    USoundCue* Cue = LoadSoundCueAsset(AssetPath, Error);
    if (!Cue) return MakeError(TEXT("ASSET_NOT_FOUND"), Error);

    if (ParentNodeId.IsEmpty())
    {
        return MakeError(TEXT("MISSING_PARAM"), TEXT("parent_node_id is required"));
    }
    if (ChildNodeId.IsEmpty())
    {
        return MakeError(TEXT("MISSING_PARAM"), TEXT("child_node_id is required"));
    }

    USoundNode* ParentNode = FindSoundNodeByName(Cue, ParentNodeId);
    if (!ParentNode)
    {
        return MakeError(TEXT("NODE_NOT_FOUND"),
            FString::Printf(TEXT("Parent node '%s' not found"), *ParentNodeId));
    }

    USoundNode* ChildNode = FindSoundNodeByName(Cue, ChildNodeId);
    if (!ChildNode)
    {
        return MakeError(TEXT("NODE_NOT_FOUND"),
            FString::Printf(TEXT("Child node '%s' not found"), *ChildNodeId));
    }

    // Validate parent can accept children
    if (ParentNode->GetMaxChildNodes() == 0)
    {
        return MakeError(TEXT("NO_CHILD_SLOTS"),
            FString::Printf(TEXT("Node '%s' (%s) does not accept child nodes"),
                *ParentNodeId, *ParentNode->GetClass()->GetName()));
    }

    // Determine the child index
    if (ChildIndex < 0)
    {
        // Auto: use next available slot or grow
        ChildIndex = ParentNode->ChildNodes.Num();
    }

    // Expand ChildNodes array if needed via InsertChildNode
    while (ParentNode->ChildNodes.Num() <= ChildIndex)
    {
        ParentNode->InsertChildNode(ParentNode->ChildNodes.Num());
    }

    ParentNode->ChildNodes[ChildIndex] = ChildNode;

    Cue->LinkGraphNodesFromSoundNodes();
    Cue->MarkPackageDirty();

    TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
    Data->SetStringField(TEXT("asset_path"), Cue->GetPathName());
    Data->SetStringField(TEXT("parent_node_id"), ParentNodeId);
    Data->SetStringField(TEXT("child_node_id"), ChildNodeId);
    Data->SetNumberField(TEXT("child_index"), ChildIndex);
    Data->SetNumberField(TEXT("parent_child_count"), ParentNode->ChildNodes.Num());
    return MakeSuccess(Data);
}

// ─────────────────────────────────────────────────────────────────────────────
// audio.disconnect_sound_node
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FNexusAudioHandler::HandleDisconnectSoundNode(
    const TSharedPtr<FJsonObject>& Params)
{
    FString AssetPath = GetStringParam(Params, TEXT("asset_path"));
    FString ParentNodeId = GetStringParam(Params, TEXT("parent_node_id"));
    int32 ChildIndex = static_cast<int32>(GetNumberParam(Params, TEXT("child_index"), -1.0));
    FString ChildNodeId = GetStringParam(Params, TEXT("child_node_id"));

    FString Error;
    USoundCue* Cue = LoadSoundCueAsset(AssetPath, Error);
    if (!Cue) return MakeError(TEXT("ASSET_NOT_FOUND"), Error);

    if (ParentNodeId.IsEmpty())
    {
        return MakeError(TEXT("MISSING_PARAM"), TEXT("parent_node_id is required"));
    }

    USoundNode* ParentNode = FindSoundNodeByName(Cue, ParentNodeId);
    if (!ParentNode)
    {
        return MakeError(TEXT("NODE_NOT_FOUND"),
            FString::Printf(TEXT("Parent node '%s' not found"), *ParentNodeId));
    }

    FString DisconnectedChild;

    if (ChildIndex >= 0)
    {
        // Disconnect by index
        if (ChildIndex >= ParentNode->ChildNodes.Num())
        {
            return MakeError(TEXT("INDEX_OUT_OF_RANGE"),
                FString::Printf(TEXT("child_index %d is out of range (parent has %d children)"),
                    ChildIndex, ParentNode->ChildNodes.Num()));
        }
        if (ParentNode->ChildNodes[ChildIndex])
        {
            DisconnectedChild = ParentNode->ChildNodes[ChildIndex]->GetName();
        }
        ParentNode->ChildNodes[ChildIndex] = nullptr;
    }
    else if (!ChildNodeId.IsEmpty())
    {
        // Disconnect by child node name
        bool bFound = false;
        for (int32 i = 0; i < ParentNode->ChildNodes.Num(); ++i)
        {
            if (ParentNode->ChildNodes[i] &&
                ParentNode->ChildNodes[i]->GetName() == ChildNodeId)
            {
                DisconnectedChild = ChildNodeId;
                ParentNode->ChildNodes[i] = nullptr;
                ChildIndex = i;
                bFound = true;
                break;
            }
        }
        if (!bFound)
        {
            return MakeError(TEXT("CONNECTION_NOT_FOUND"),
                FString::Printf(TEXT("Child node '%s' is not connected to parent '%s'"),
                    *ChildNodeId, *ParentNodeId));
        }
    }
    else
    {
        return MakeError(TEXT("MISSING_PARAM"),
            TEXT("Either child_index or child_node_id is required"));
    }

    Cue->LinkGraphNodesFromSoundNodes();
    Cue->MarkPackageDirty();

    TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
    Data->SetStringField(TEXT("asset_path"), Cue->GetPathName());
    Data->SetStringField(TEXT("parent_node_id"), ParentNodeId);
    Data->SetNumberField(TEXT("child_index"), ChildIndex);
    Data->SetStringField(TEXT("disconnected_child"), DisconnectedChild);
    return MakeSuccess(Data);
}

// ─────────────────────────────────────────────────────────────────────────────
// MetaSound node CRUD helpers (Phase 3B)
// ─────────────────────────────────────────────────────────────────────────────

namespace
{
    /** Load a UMetaSoundSource by asset path. */
    UMetaSoundSource* LoadMetaSoundAsset(const FString& AssetPath, FString& OutError)
    {
        if (AssetPath.IsEmpty())
        {
            OutError = TEXT("asset_path is required");
            return nullptr;
        }
        UMetaSoundSource* MS = Cast<UMetaSoundSource>(
            StaticLoadObject(UMetaSoundSource::StaticClass(), nullptr, *AssetPath));
        if (!MS)
        {
            OutError = FString::Printf(TEXT("MetaSound '%s' not found"), *AssetPath);
        }
        return MS;
    }

    /** Parse a "Namespace.Name" or "Namespace.Name.Variant" string into FMetasoundFrontendClassName. */
    FMetasoundFrontendClassName ParseMetaSoundClassName(const FString& InClassString)
    {
        TArray<FString> Parts;
        InClassString.ParseIntoArray(Parts, TEXT("."), true);
        if (Parts.Num() >= 3)
        {
            return FMetasoundFrontendClassName(FName(*Parts[0]), FName(*Parts[1]), FName(*Parts[2]));
        }
        else if (Parts.Num() == 2)
        {
            return FMetasoundFrontendClassName(FName(*Parts[0]), FName(*Parts[1]));
        }
        else
        {
            // Single name — assume UE:: namespace
            return FMetasoundFrontendClassName(FName(TEXT("UE")), FName(*InClassString));
        }
    }

    /** Build a JSON description of a single FMetasoundFrontendNode. */
    TSharedPtr<FJsonObject> BuildMetaSoundNodeJson(const FMetasoundFrontendNode& Node,
        const FMetasoundFrontendClass* NodeClass)
    {
        TSharedPtr<FJsonObject> Obj = MakeShareable(new FJsonObject());
        Obj->SetStringField(TEXT("node_id"), Node.GetID().ToString());
        Obj->SetStringField(TEXT("name"), Node.Name.ToString());

        if (NodeClass)
        {
            const FMetasoundFrontendClassName& CN = NodeClass->Metadata.GetClassName();
            FString FullClassName = FString::Printf(TEXT("%s.%s"),
                *CN.Namespace.ToString(), *CN.Name.ToString());
            if (!CN.Variant.IsNone())
            {
                FullClassName += FString::Printf(TEXT(".%s"), *CN.Variant.ToString());
            }
            Obj->SetStringField(TEXT("class_name"), FullClassName);
        }

        // Position
        Obj->SetNumberField(TEXT("position_x"), Node.Style.Display.Locations.Num() > 0
            ? Node.Style.Display.Locations.begin()->Value.X : 0.0);
        Obj->SetNumberField(TEXT("position_y"), Node.Style.Display.Locations.Num() > 0
            ? Node.Style.Display.Locations.begin()->Value.Y : 0.0);

        // Input pins
        TArray<TSharedPtr<FJsonValue>> InputArr;
        for (const FMetasoundFrontendVertex& V : Node.Interface.Inputs)
        {
            TSharedPtr<FJsonObject> PinObj = MakeShareable(new FJsonObject());
            PinObj->SetStringField(TEXT("name"), V.Name.ToString());
            PinObj->SetStringField(TEXT("type"), V.TypeName.ToString());
            PinObj->SetStringField(TEXT("vertex_id"), V.VertexID.ToString());
            InputArr.Add(MakeShareable(new FJsonValueObject(PinObj)));
        }
        Obj->SetArrayField(TEXT("inputs"), InputArr);

        // Output pins
        TArray<TSharedPtr<FJsonValue>> OutputArr;
        for (const FMetasoundFrontendVertex& V : Node.Interface.Outputs)
        {
            TSharedPtr<FJsonObject> PinObj = MakeShareable(new FJsonObject());
            PinObj->SetStringField(TEXT("name"), V.Name.ToString());
            PinObj->SetStringField(TEXT("type"), V.TypeName.ToString());
            PinObj->SetStringField(TEXT("vertex_id"), V.VertexID.ToString());
            OutputArr.Add(MakeShareable(new FJsonValueObject(PinObj)));
        }
        Obj->SetArrayField(TEXT("outputs"), OutputArr);

        return Obj;
    }
} // anonymous namespace (MetaSound helpers)

// ─────────────────────────────────────────────────────────────────────────────
// audio.add_metasound_node
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FNexusAudioHandler::HandleAddMetaSoundNode(
    const TSharedPtr<FJsonObject>& Params)
{
    FString AssetPath = GetStringParam(Params, TEXT("asset_path"));
    FString NodeClassName = GetStringParam(Params, TEXT("node_class_name"));
    double PosX = GetNumberParam(Params, TEXT("position_x"), 0.0);
    double PosY = GetNumberParam(Params, TEXT("position_y"), 0.0);

    FString Error;
    UMetaSoundSource* MS = LoadMetaSoundAsset(AssetPath, Error);
    if (!MS) return MakeError(TEXT("ASSET_NOT_FOUND"), Error);

    if (NodeClassName.IsEmpty())
    {
        return MakeError(TEXT("MISSING_PARAM"), TEXT("node_class_name is required"));
    }

    // Build the builder
    TScriptInterface<IMetaSoundDocumentInterface> DocInterface;
    DocInterface.SetObject(MS);
    DocInterface.SetInterface(Cast<IMetaSoundDocumentInterface>(MS));
    FMetaSoundFrontendDocumentBuilder Builder(DocInterface);

    // Parse class name
    FMetasoundFrontendClassName ClassName = ParseMetaSoundClassName(NodeClassName);

    // Add node
    FGuid NewNodeID = FGuid::NewGuid();
    const FMetasoundFrontendNode* NewNode = Builder.AddNodeByClassName(
        ClassName, /*InMajorVersion=*/1, NewNodeID);
    if (!NewNode)
    {
        return MakeError(TEXT("ADD_FAILED"),
            FString::Printf(TEXT("Failed to add MetaSound node of class '%s'. "
                "Ensure the class is registered."), *NodeClassName));
    }

    // Set position
    FGuid LocationGuid = FGuid::NewGuid();
    Builder.SetNodeLocation(NewNodeID, FVector2D(PosX, PosY), &LocationGuid);

    MS->MarkPackageDirty();

    TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
    Data->SetStringField(TEXT("asset_path"), MS->GetPathName());
    Data->SetStringField(TEXT("node_id"), NewNodeID.ToString());
    Data->SetStringField(TEXT("node_class_name"), NodeClassName);
    Data->SetNumberField(TEXT("position_x"), PosX);
    Data->SetNumberField(TEXT("position_y"), PosY);
    Data->SetNumberField(TEXT("input_count"), NewNode->Interface.Inputs.Num());
    Data->SetNumberField(TEXT("output_count"), NewNode->Interface.Outputs.Num());
    return MakeSuccess(Data);
}

// ─────────────────────────────────────────────────────────────────────────────
// audio.get_metasound_nodes
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FNexusAudioHandler::HandleGetMetaSoundNodes(
    const TSharedPtr<FJsonObject>& Params)
{
    FString AssetPath = GetStringParam(Params, TEXT("asset_path"));

    FString Error;
    UMetaSoundSource* MS = LoadMetaSoundAsset(AssetPath, Error);
    if (!MS) return MakeError(TEXT("ASSET_NOT_FOUND"), Error);

    TScriptInterface<IMetaSoundDocumentInterface> DocInterface;
    DocInterface.SetObject(MS);
    DocInterface.SetInterface(Cast<IMetaSoundDocumentInterface>(MS));
    FMetaSoundFrontendDocumentBuilder Builder(DocInterface);

    TArray<TSharedPtr<FJsonValue>> NodesArr;

    Builder.IterateNodes(
        [&NodesArr](const FMetasoundFrontendClass& NodeClass, const FMetasoundFrontendNode& Node)
        {
            NodesArr.Add(MakeShareable(
                new FJsonValueObject(BuildMetaSoundNodeJson(Node, &NodeClass))));
        });

    // Edges
    const FMetasoundFrontendDocument& Doc = MS->GetConstDocument();
    TArray<TSharedPtr<FJsonValue>> EdgesArr;
    for (const FMetasoundFrontendGraphClass& SubGraphClass : Doc.Subgraphs)
    {
        for (const FMetasoundFrontendEdge& Edge : SubGraphClass.Graph.Edges)
        {
            TSharedPtr<FJsonObject> EdgeObj = MakeShareable(new FJsonObject());
            EdgeObj->SetStringField(TEXT("from_node_id"), Edge.FromNodeID.ToString());
            EdgeObj->SetStringField(TEXT("from_vertex_id"), Edge.FromVertexID.ToString());
            EdgeObj->SetStringField(TEXT("to_node_id"), Edge.ToNodeID.ToString());
            EdgeObj->SetStringField(TEXT("to_vertex_id"), Edge.ToVertexID.ToString());
            EdgesArr.Add(MakeShareable(new FJsonValueObject(EdgeObj)));
        }
    }
    // Also include root graph edges
    if (Doc.RootGraph.Graph.Edges.Num() > 0)
    {
        for (const FMetasoundFrontendEdge& Edge : Doc.RootGraph.Graph.Edges)
        {
            TSharedPtr<FJsonObject> EdgeObj = MakeShareable(new FJsonObject());
            EdgeObj->SetStringField(TEXT("from_node_id"), Edge.FromNodeID.ToString());
            EdgeObj->SetStringField(TEXT("from_vertex_id"), Edge.FromVertexID.ToString());
            EdgeObj->SetStringField(TEXT("to_node_id"), Edge.ToNodeID.ToString());
            EdgeObj->SetStringField(TEXT("to_vertex_id"), Edge.ToVertexID.ToString());
            EdgesArr.Add(MakeShareable(new FJsonValueObject(EdgeObj)));
        }
    }

    TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
    Data->SetStringField(TEXT("asset_path"), MS->GetPathName());
    Data->SetArrayField(TEXT("nodes"), NodesArr);
    Data->SetNumberField(TEXT("node_count"), NodesArr.Num());
    Data->SetArrayField(TEXT("edges"), EdgesArr);
    Data->SetNumberField(TEXT("edge_count"), EdgesArr.Num());
    return MakeSuccess(Data);
}

// ─────────────────────────────────────────────────────────────────────────────
// audio.update_metasound_node
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FNexusAudioHandler::HandleUpdateMetaSoundNode(
    const TSharedPtr<FJsonObject>& Params)
{
    FString AssetPath = GetStringParam(Params, TEXT("asset_path"));
    FString NodeIdStr = GetStringParam(Params, TEXT("node_id"));

    FString Error;
    UMetaSoundSource* MS = LoadMetaSoundAsset(AssetPath, Error);
    if (!MS) return MakeError(TEXT("ASSET_NOT_FOUND"), Error);

    if (NodeIdStr.IsEmpty())
    {
        return MakeError(TEXT("MISSING_PARAM"), TEXT("node_id is required"));
    }

    FGuid NodeID;
    if (!FGuid::Parse(NodeIdStr, NodeID))
    {
        return MakeError(TEXT("INVALID_PARAM"),
            FString::Printf(TEXT("'%s' is not a valid GUID"), *NodeIdStr));
    }

    TScriptInterface<IMetaSoundDocumentInterface> DocInterface;
    DocInterface.SetObject(MS);
    DocInterface.SetInterface(Cast<IMetaSoundDocumentInterface>(MS));
    FMetaSoundFrontendDocumentBuilder Builder(DocInterface);

    const FMetasoundFrontendNode* Node = Builder.FindNode(NodeID);
    if (!Node)
    {
        return MakeError(TEXT("NODE_NOT_FOUND"),
            FString::Printf(TEXT("MetaSound node '%s' not found"), *NodeIdStr));
    }

    TArray<TSharedPtr<FJsonValue>> ModifiedArr;

    // Position update
    double PosX, PosY;
    bool bHasPosX = Params->TryGetNumberField(TEXT("position_x"), PosX);
    bool bHasPosY = Params->TryGetNumberField(TEXT("position_y"), PosY);
    if (bHasPosX || bHasPosY)
    {
        // Get current position as base
        double CurX = 0.0, CurY = 0.0;
        if (Node->Style.Display.Locations.Num() > 0)
        {
            CurX = Node->Style.Display.Locations.begin()->Value.X;
            CurY = Node->Style.Display.Locations.begin()->Value.Y;
        }
        if (bHasPosX) CurX = PosX;
        if (bHasPosY) CurY = PosY;

        FGuid LocationGuid = FGuid::NewGuid();
        Builder.SetNodeLocation(NodeID, FVector2D(CurX, CurY), &LocationGuid);
        if (bHasPosX) ModifiedArr.Add(MakeShareable(new FJsonValueString(TEXT("position_x"))));
        if (bHasPosY) ModifiedArr.Add(MakeShareable(new FJsonValueString(TEXT("position_y"))));
    }

    MS->MarkPackageDirty();

    TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
    Data->SetStringField(TEXT("asset_path"), MS->GetPathName());
    Data->SetStringField(TEXT("node_id"), NodeIdStr);
    Data->SetNumberField(TEXT("modified_count"), ModifiedArr.Num());
    Data->SetArrayField(TEXT("modified_properties"), ModifiedArr);
    return MakeSuccess(Data);
}

// ─────────────────────────────────────────────────────────────────────────────
// audio.remove_metasound_node
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FNexusAudioHandler::HandleRemoveMetaSoundNode(
    const TSharedPtr<FJsonObject>& Params)
{
    FString AssetPath = GetStringParam(Params, TEXT("asset_path"));
    FString NodeIdStr = GetStringParam(Params, TEXT("node_id"));

    FString Error;
    UMetaSoundSource* MS = LoadMetaSoundAsset(AssetPath, Error);
    if (!MS) return MakeError(TEXT("ASSET_NOT_FOUND"), Error);

    if (NodeIdStr.IsEmpty())
    {
        return MakeError(TEXT("MISSING_PARAM"), TEXT("node_id is required"));
    }

    FGuid NodeID;
    if (!FGuid::Parse(NodeIdStr, NodeID))
    {
        return MakeError(TEXT("INVALID_PARAM"),
            FString::Printf(TEXT("'%s' is not a valid GUID"), *NodeIdStr));
    }

    TScriptInterface<IMetaSoundDocumentInterface> DocInterface;
    DocInterface.SetObject(MS);
    DocInterface.SetInterface(Cast<IMetaSoundDocumentInterface>(MS));
    FMetaSoundFrontendDocumentBuilder Builder(DocInterface);

    const FMetasoundFrontendNode* Node = Builder.FindNode(NodeID);
    if (!Node)
    {
        return MakeError(TEXT("NODE_NOT_FOUND"),
            FString::Printf(TEXT("MetaSound node '%s' not found"), *NodeIdStr));
    }

    FString NodeName = Node->Name.ToString();

    bool bRemoved = Builder.RemoveNode(NodeID);
    if (!bRemoved)
    {
        return MakeError(TEXT("REMOVE_FAILED"),
            FString::Printf(TEXT("Failed to remove MetaSound node '%s'"), *NodeIdStr));
    }

    MS->MarkPackageDirty();

    TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
    Data->SetStringField(TEXT("asset_path"), MS->GetPathName());
    Data->SetStringField(TEXT("removed_node_id"), NodeIdStr);
    Data->SetStringField(TEXT("removed_node_name"), NodeName);
    return MakeSuccess(Data);
}

// ─────────────────────────────────────────────────────────────────────────────
// audio.connect_metasound_nodes
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FNexusAudioHandler::HandleConnectMetaSoundNodes(
    const TSharedPtr<FJsonObject>& Params)
{
    FString AssetPath = GetStringParam(Params, TEXT("asset_path"));
    FString FromNodeIdStr = GetStringParam(Params, TEXT("from_node_id"));
    FString FromOutputName = GetStringParam(Params, TEXT("from_output_name"));
    FString ToNodeIdStr = GetStringParam(Params, TEXT("to_node_id"));
    FString ToInputName = GetStringParam(Params, TEXT("to_input_name"));

    FString Error;
    UMetaSoundSource* MS = LoadMetaSoundAsset(AssetPath, Error);
    if (!MS) return MakeError(TEXT("ASSET_NOT_FOUND"), Error);

    if (FromNodeIdStr.IsEmpty() || ToNodeIdStr.IsEmpty())
    {
        return MakeError(TEXT("MISSING_PARAM"),
            TEXT("from_node_id and to_node_id are required"));
    }
    if (FromOutputName.IsEmpty() || ToInputName.IsEmpty())
    {
        return MakeError(TEXT("MISSING_PARAM"),
            TEXT("from_output_name and to_input_name are required"));
    }

    FGuid FromNodeID, ToNodeID;
    if (!FGuid::Parse(FromNodeIdStr, FromNodeID))
    {
        return MakeError(TEXT("INVALID_PARAM"),
            FString::Printf(TEXT("'%s' is not a valid GUID"), *FromNodeIdStr));
    }
    if (!FGuid::Parse(ToNodeIdStr, ToNodeID))
    {
        return MakeError(TEXT("INVALID_PARAM"),
            FString::Printf(TEXT("'%s' is not a valid GUID"), *ToNodeIdStr));
    }

    TScriptInterface<IMetaSoundDocumentInterface> DocInterface;
    DocInterface.SetObject(MS);
    DocInterface.SetInterface(Cast<IMetaSoundDocumentInterface>(MS));
    FMetaSoundFrontendDocumentBuilder Builder(DocInterface);

    // Resolve output vertex on source node
    const FMetasoundFrontendVertex* OutputVertex = Builder.FindNodeOutput(
        FromNodeID, FName(*FromOutputName));
    if (!OutputVertex)
    {
        return MakeError(TEXT("PIN_NOT_FOUND"),
            FString::Printf(TEXT("Output pin '%s' not found on source node '%s'"),
                *FromOutputName, *FromNodeIdStr));
    }

    // Resolve input vertex on target node
    const FMetasoundFrontendVertex* InputVertex = Builder.FindNodeInput(
        ToNodeID, FName(*ToInputName));
    if (!InputVertex)
    {
        return MakeError(TEXT("PIN_NOT_FOUND"),
            FString::Printf(TEXT("Input pin '%s' not found on target node '%s'"),
                *ToInputName, *ToNodeIdStr));
    }

    // Build and add edge
    FMetasoundFrontendEdge Edge;
    Edge.FromNodeID = FromNodeID;
    Edge.FromVertexID = OutputVertex->VertexID;
    Edge.ToNodeID = ToNodeID;
    Edge.ToVertexID = InputVertex->VertexID;

    Builder.AddEdge(MoveTemp(Edge));

    MS->MarkPackageDirty();

    TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
    Data->SetStringField(TEXT("asset_path"), MS->GetPathName());
    Data->SetStringField(TEXT("from_node_id"), FromNodeIdStr);
    Data->SetStringField(TEXT("from_output_name"), FromOutputName);
    Data->SetStringField(TEXT("to_node_id"), ToNodeIdStr);
    Data->SetStringField(TEXT("to_input_name"), ToInputName);
    return MakeSuccess(Data);
}

// ─────────────────────────────────────────────────────────────────────────────
// audio.disconnect_metasound_node
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FNexusAudioHandler::HandleDisconnectMetaSoundNode(
    const TSharedPtr<FJsonObject>& Params)
{
    FString AssetPath = GetStringParam(Params, TEXT("asset_path"));
    FString FromNodeIdStr = GetStringParam(Params, TEXT("from_node_id"));
    FString FromOutputName = GetStringParam(Params, TEXT("from_output_name"));
    FString ToNodeIdStr = GetStringParam(Params, TEXT("to_node_id"));
    FString ToInputName = GetStringParam(Params, TEXT("to_input_name"));

    FString Error;
    UMetaSoundSource* MS = LoadMetaSoundAsset(AssetPath, Error);
    if (!MS) return MakeError(TEXT("ASSET_NOT_FOUND"), Error);

    if (FromNodeIdStr.IsEmpty() || ToNodeIdStr.IsEmpty())
    {
        return MakeError(TEXT("MISSING_PARAM"),
            TEXT("from_node_id and to_node_id are required"));
    }
    if (FromOutputName.IsEmpty() || ToInputName.IsEmpty())
    {
        return MakeError(TEXT("MISSING_PARAM"),
            TEXT("from_output_name and to_input_name are required"));
    }

    FGuid FromNodeID, ToNodeID;
    if (!FGuid::Parse(FromNodeIdStr, FromNodeID))
    {
        return MakeError(TEXT("INVALID_PARAM"),
            FString::Printf(TEXT("'%s' is not a valid GUID"), *FromNodeIdStr));
    }
    if (!FGuid::Parse(ToNodeIdStr, ToNodeID))
    {
        return MakeError(TEXT("INVALID_PARAM"),
            FString::Printf(TEXT("'%s' is not a valid GUID"), *ToNodeIdStr));
    }

    TScriptInterface<IMetaSoundDocumentInterface> DocInterface;
    DocInterface.SetObject(MS);
    DocInterface.SetInterface(Cast<IMetaSoundDocumentInterface>(MS));
    FMetaSoundFrontendDocumentBuilder Builder(DocInterface);

    // Resolve output vertex
    const FMetasoundFrontendVertex* OutputVertex = Builder.FindNodeOutput(
        FromNodeID, FName(*FromOutputName));
    if (!OutputVertex)
    {
        return MakeError(TEXT("PIN_NOT_FOUND"),
            FString::Printf(TEXT("Output pin '%s' not found on node '%s'"),
                *FromOutputName, *FromNodeIdStr));
    }

    // Resolve input vertex
    const FMetasoundFrontendVertex* InputVertex = Builder.FindNodeInput(
        ToNodeID, FName(*ToInputName));
    if (!InputVertex)
    {
        return MakeError(TEXT("PIN_NOT_FOUND"),
            FString::Printf(TEXT("Input pin '%s' not found on node '%s'"),
                *ToInputName, *ToNodeIdStr));
    }

    // Build edge to remove
    FMetasoundFrontendEdge Edge;
    Edge.FromNodeID = FromNodeID;
    Edge.FromVertexID = OutputVertex->VertexID;
    Edge.ToNodeID = ToNodeID;
    Edge.ToVertexID = InputVertex->VertexID;

    bool bRemoved = Builder.RemoveEdge(Edge);
    if (!bRemoved)
    {
        return MakeError(TEXT("DISCONNECT_FAILED"),
            FString::Printf(TEXT("Edge '%s.%s' → '%s.%s' not found or could not be removed"),
                *FromNodeIdStr, *FromOutputName, *ToNodeIdStr, *ToInputName));
    }

    MS->MarkPackageDirty();

    TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
    Data->SetStringField(TEXT("asset_path"), MS->GetPathName());
    Data->SetStringField(TEXT("from_node_id"), FromNodeIdStr);
    Data->SetStringField(TEXT("from_output_name"), FromOutputName);
    Data->SetStringField(TEXT("to_node_id"), ToNodeIdStr);
    Data->SetStringField(TEXT("to_input_name"), ToInputName);
    return MakeSuccess(Data);
}
