// Copyright Nexus Team. All Rights Reserved.

#include "Handlers/NexusLandscapeHandler.h"
#include "Editor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Landscape.h"
#include "LandscapeProxy.h"
#include "LandscapeInfo.h"
#include "LandscapeComponent.h"
#include "LandscapeLayerInfoObject.h"
// LandscapeEditorUtils.h is editor-only and in the LandscapeEditor module's Public folder
// We don't use any functions from it, so no include needed
#include "LandscapeEdit.h"
#include "LandscapeDataAccess.h"
#include "InstancedFoliageActor.h"
#include "InstancedFoliage.h"
#include "FoliageType_InstancedStaticMesh.h"
#include "FoliageType.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "Misc/FileHelper.h"
#include "AssetToolsModule.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "UObject/SavePackage.h"
#include "IImageWrapperModule.h"
#include "IImageWrapper.h"
#include "Modules/ModuleManager.h"

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

ALandscapeProxy* FNexusLandscapeHandler::FindLandscapeByLabel(
    UWorld* World, const FString& Label)
{
    if (!World) return nullptr;

    for (TActorIterator<ALandscapeProxy> It(World); It; ++It)
    {
        ALandscapeProxy* Landscape = *It;
        if (Label.IsEmpty())
        {
            // Return the first landscape if no label specified
            return Landscape;
        }
        if (Landscape->GetActorLabel() == Label || Landscape->GetPathName() == Label)
        {
            return Landscape;
        }
    }
    return nullptr;
}

// ─────────────────────────────────────────────────────────────────────────────
// Dispatch
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FNexusLandscapeHandler::Handle(
    const FString& CommandType,
    const TSharedPtr<FJsonObject>& Params)
{
    FString SubCommand = CommandType.RightChop(GetNamespace().Len() + 1);

    if (SubCommand == TEXT("create_landscape"))       return HandleCreateLandscape(Params);
    if (SubCommand == TEXT("sculpt_landscape"))        return HandleSculptLandscape(Params);
    if (SubCommand == TEXT("paint_landscape_layer"))   return HandlePaintLandscapeLayer(Params);
    if (SubCommand == TEXT("add_foliage_type"))        return HandleAddFoliageType(Params);
    if (SubCommand == TEXT("paint_foliage"))           return HandlePaintFoliage(Params);
    if (SubCommand == TEXT("remove_foliage"))          return HandleRemoveFoliage(Params);
    if (SubCommand == TEXT("import_heightmap"))        return HandleImportHeightmap(Params);
    if (SubCommand == TEXT("export_heightmap"))        return HandleExportHeightmap(Params);
    if (SubCommand == TEXT("get_landscape_info"))      return HandleGetLandscapeInfo(Params);

    return MakeError(TEXT("NOT_IMPLEMENTED"),
        FString::Printf(TEXT("Command '%s' not yet implemented"), *CommandType));
}

// ─────────────────────────────────────────────────────────────────────────────
// landscape.create_landscape
// Long-running: 30s timeout
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FNexusLandscapeHandler::HandleCreateLandscape(
    const TSharedPtr<FJsonObject>& Params)
{
    UWorld* World = GetEditorWorld();
    if (!World)
    {
        return MakeError(TEXT("NO_WORLD"), TEXT("No editor world available"));
    }

    // Grid configuration
    int32 NumQuadsX = static_cast<int32>(GetNumberParam(Params, TEXT("num_quads_x"), 63.0));
    int32 NumQuadsY = static_cast<int32>(GetNumberParam(Params, TEXT("num_quads_y"), 63.0));
    int32 SectionsPerComponent = static_cast<int32>(
        GetNumberParam(Params, TEXT("sections_per_component"), 1.0));
    int32 ComponentsX = static_cast<int32>(GetNumberParam(Params, TEXT("components_x"), 8.0));
    int32 ComponentsY = static_cast<int32>(GetNumberParam(Params, TEXT("components_y"), 8.0));

    FVector Scale = GetVectorParam(Params, TEXT("scale"), FVector(100.0, 100.0, 100.0));
    FVector Location = GetVectorParam(Params, TEXT("location"), FVector::ZeroVector);
    FString MaterialPath = GetStringParam(Params, TEXT("material_path"));

    // Validate inputs
    TArray<int32> ValidQuadSizes = {7, 15, 31, 63, 127, 255};
    if (!ValidQuadSizes.Contains(NumQuadsX) || !ValidQuadSizes.Contains(NumQuadsY))
    {
        return MakeError(TEXT("INVALID_PARAM"),
            TEXT("num_quads_x and num_quads_y must be one of: 7, 15, 31, 63, 127, 255"));
    }

    if (SectionsPerComponent != 1 && SectionsPerComponent != 2)
    {
        return MakeError(TEXT("INVALID_PARAM"),
            TEXT("sections_per_component must be 1 or 2"));
    }

    if (ComponentsX < 1 || ComponentsY < 1 || ComponentsX > 32 || ComponentsY > 32)
    {
        return MakeError(TEXT("INVALID_PARAM"),
            TEXT("components_x and components_y must be between 1 and 32"));
    }

    // Calculate total resolution
    int32 QuadsPerSection = NumQuadsX; // Assuming square sections
    int32 SizeX = QuadsPerSection * SectionsPerComponent * ComponentsX + 1;
    int32 SizeY = QuadsPerSection * SectionsPerComponent * ComponentsY + 1;

    // Create flat heightmap data (mid-height = 32768 for 16-bit)
    TArray<uint16> HeightData;
    HeightData.SetNumZeroed(SizeX * SizeY);
    for (int32 i = 0; i < HeightData.Num(); i++)
    {
        HeightData[i] = 32768; // Mid-height
    }

    // Prepare import layers (empty for now)
    TArray<FLandscapeImportLayerInfo> ImportLayers;

    // Create the landscape actor
    ALandscape* NewLandscape = World->SpawnActor<ALandscape>();
    if (!NewLandscape)
    {
        return MakeError(TEXT("SPAWN_FAILED"), TEXT("Failed to spawn landscape actor"));
    }

    // Set transform
    NewLandscape->SetActorLocation(Location);
    NewLandscape->SetActorScale3D(Scale);

    // Set material if specified
    if (!MaterialPath.IsEmpty())
    {
        UMaterialInterface* Material = Cast<UMaterialInterface>(
            StaticLoadObject(UMaterialInterface::StaticClass(), nullptr, *MaterialPath));
        if (Material)
        {
            NewLandscape->LandscapeMaterial = Material;
        }
    }

    // Import the landscape with the height data
    FGuid LandscapeGuid = FGuid::NewGuid();
    TMap<FGuid, TArray<uint16>> HeightDataPerLayers;
    HeightDataPerLayers.Add(LandscapeGuid, MoveTemp(HeightData));

    TMap<FGuid, TArray<FLandscapeImportLayerInfo>> MaterialLayerDataPerLayers;
    MaterialLayerDataPerLayers.Add(LandscapeGuid, ImportLayers);

    // UE 5.7: Import now requires an additional TArrayView<const FLandscapeLayer> parameter
    TArray<FLandscapeLayer> ImportLandscapeLayers;
    NewLandscape->Import(
        LandscapeGuid,
        0, 0,
        SizeX - 1, SizeY - 1,
        SectionsPerComponent, QuadsPerSection,
        HeightDataPerLayers,
        TEXT(""),
        MaterialLayerDataPerLayers,
        ELandscapeImportAlphamapType::Additive,
        ImportLandscapeLayers);

    // Register all components
    NewLandscape->RegisterAllComponents();
    NewLandscape->MarkPackageDirty();

    TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
    Data->SetStringField(TEXT("actor_path"), NewLandscape->GetPathName());
    Data->SetStringField(TEXT("actor_label"), NewLandscape->GetActorLabel());
    Data->SetNumberField(TEXT("size_x"), SizeX);
    Data->SetNumberField(TEXT("size_y"), SizeY);
    Data->SetNumberField(TEXT("components_x"), ComponentsX);
    Data->SetNumberField(TEXT("components_y"), ComponentsY);
    Data->SetNumberField(TEXT("quads_per_section"), QuadsPerSection);
    Data->SetNumberField(TEXT("sections_per_component"), SectionsPerComponent);
    Data->SetObjectField(TEXT("scale"), VectorToJson(Scale));
    Data->SetObjectField(TEXT("location"), VectorToJson(Location));
    return MakeSuccess(Data);
}

// ─────────────────────────────────────────────────────────────────────────────
// landscape.sculpt_landscape
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FNexusLandscapeHandler::HandleSculptLandscape(
    const TSharedPtr<FJsonObject>& Params)
{
    UWorld* World = GetEditorWorld();
    if (!World)
    {
        return MakeError(TEXT("NO_WORLD"), TEXT("No editor world available"));
    }

    FString LandscapeLabel = GetStringParam(Params, TEXT("landscape_label"));
    ALandscapeProxy* Landscape = FindLandscapeByLabel(World, LandscapeLabel);
    if (!Landscape)
    {
        return MakeError(TEXT("LANDSCAPE_NOT_FOUND"),
            FString::Printf(TEXT("Landscape '%s' not found"), *LandscapeLabel));
    }

    // Get brush parameters
    const TSharedPtr<FJsonObject>* CenterObj;
    double CenterX = 0.0, CenterY = 0.0;
    if (Params->TryGetObjectField(TEXT("center"), CenterObj))
    {
        CenterX = (*CenterObj)->GetNumberField(TEXT("x"));
        CenterY = (*CenterObj)->GetNumberField(TEXT("y"));
    }
    double Radius = GetNumberParam(Params, TEXT("radius"), 1000.0);
    double Falloff = GetNumberParam(Params, TEXT("falloff"), 0.5);
    double Strength = GetNumberParam(Params, TEXT("strength"), 0.3);
    FString ToolMode = GetStringParam(Params, TEXT("tool_mode"), TEXT("Sculpt"));

    // Get landscape info for editing
    ULandscapeInfo* LandscapeInfo = Landscape->GetLandscapeInfo();
    if (!LandscapeInfo)
    {
        return MakeError(TEXT("NO_LANDSCAPE_INFO"),
            TEXT("Failed to get landscape info for editing"));
    }

    // Use FLandscapeEditDataInterface for height modification
    FLandscapeEditDataInterface EditData(LandscapeInfo);

    // Calculate the affected area in landscape coordinates
    FVector LandscapeScale = Landscape->GetActorScale3D();
    FVector LandscapeLocation = Landscape->GetActorLocation();

    // Convert world coordinates to landscape-local coordinates
    double LocalX = (CenterX - LandscapeLocation.X) / LandscapeScale.X;
    double LocalY = (CenterY - LandscapeLocation.Y) / LandscapeScale.Y;
    double LocalRadius = Radius / FMath::Max(LandscapeScale.X, LandscapeScale.Y);

    int32 MinX = FMath::FloorToInt(LocalX - LocalRadius);
    int32 MinY = FMath::FloorToInt(LocalY - LocalRadius);
    int32 MaxX = FMath::CeilToInt(LocalX + LocalRadius);
    int32 MaxY = FMath::CeilToInt(LocalY + LocalRadius);

    // Clamp to valid range
    MinX = FMath::Max(0, MinX);
    MinY = FMath::Max(0, MinY);

    // Read existing height data
    // UE 5.7: GetHeightDataFast now takes uint16* Data instead of TArray<uint16>&
    int32 ReadMinX = MinX, ReadMinY = MinY, ReadMaxX = MaxX, ReadMaxY = MaxY;
    int32 DataSizeX = ReadMaxX - ReadMinX + 1;
    int32 DataSizeY = ReadMaxY - ReadMinY + 1;
    TArray<uint16> HeightData;
    HeightData.SetNumUninitialized(DataSizeX * DataSizeY);
    EditData.GetHeightDataFast(ReadMinX, ReadMinY, ReadMaxX, ReadMaxY, HeightData.GetData(), 0);

    if (HeightData.Num() == 0 || DataSizeX <= 0 || DataSizeY <= 0)
    {
        return MakeError(TEXT("NO_HEIGHT_DATA"),
            TEXT("Could not read height data from landscape at the specified location"));
    }

    // Apply sculpt operation
    int32 ModifiedVertices = 0;
    for (int32 Y = 0; Y < DataSizeY; Y++)
    {
        for (int32 X = 0; X < DataSizeX; X++)
        {
            double PosX = ReadMinX + X;
            double PosY = ReadMinY + Y;

            // Calculate distance from center
            double Dist = FMath::Sqrt(
                FMath::Square(PosX - LocalX) + FMath::Square(PosY - LocalY));

            if (Dist > LocalRadius) continue;

            // Calculate falloff-weighted strength
            double NormalizedDist = Dist / LocalRadius;
            double FalloffWeight = 1.0 - FMath::Pow(NormalizedDist, 1.0 / FMath::Max(Falloff, 0.01));
            FalloffWeight = FMath::Clamp(FalloffWeight, 0.0, 1.0);

            int32 Idx = Y * DataSizeX + X;
            uint16 CurrentHeight = HeightData[Idx];
            double HeightDelta = 0.0;

            if (ToolMode == TEXT("Sculpt"))
            {
                HeightDelta = Strength * FalloffWeight * 256.0; // Raise terrain
            }
            else if (ToolMode == TEXT("Smooth"))
            {
                // Average neighboring heights
                double AvgHeight = 0.0;
                int32 Count = 0;
                for (int32 DY = -1; DY <= 1; DY++)
                {
                    for (int32 DX = -1; DX <= 1; DX++)
                    {
                        int32 NX = X + DX, NY = Y + DY;
                        if (NX >= 0 && NX < DataSizeX && NY >= 0 && NY < DataSizeY)
                        {
                            AvgHeight += HeightData[NY * DataSizeX + NX];
                            Count++;
                        }
                    }
                }
                if (Count > 0)
                {
                    AvgHeight /= Count;
                    HeightDelta = (AvgHeight - CurrentHeight) * Strength * FalloffWeight;
                }
            }
            else if (ToolMode == TEXT("Flatten"))
            {
                // Flatten to center height
                static uint16 FlattenTarget = 32768;
                if (X == 0 && Y == 0)
                {
                    // Use the center-most height as flatten target
                    int32 CenterIdx = (DataSizeY / 2) * DataSizeX + (DataSizeX / 2);
                    if (CenterIdx >= 0 && CenterIdx < HeightData.Num())
                    {
                        FlattenTarget = HeightData[CenterIdx];
                    }
                }
                HeightDelta = (static_cast<double>(FlattenTarget) - CurrentHeight)
                    * Strength * FalloffWeight;
            }
            else if (ToolMode == TEXT("Noise"))
            {
                double NoiseVal = FMath::PerlinNoise2D(
                    FVector2D(PosX * 0.01, PosY * 0.01));
                HeightDelta = NoiseVal * Strength * FalloffWeight * 512.0;
            }
            else if (ToolMode == TEXT("Erosion"))
            {
                // Simple thermal erosion approximation
                double MaxSlope = 0.0;
                for (int32 DY = -1; DY <= 1; DY++)
                {
                    for (int32 DX = -1; DX <= 1; DX++)
                    {
                        if (DX == 0 && DY == 0) continue;
                        int32 NX = X + DX, NY = Y + DY;
                        if (NX >= 0 && NX < DataSizeX && NY >= 0 && NY < DataSizeY)
                        {
                            double Slope = FMath::Abs(
                                static_cast<double>(HeightData[NY * DataSizeX + NX])
                                - CurrentHeight);
                            MaxSlope = FMath::Max(MaxSlope, Slope);
                        }
                    }
                }
                HeightDelta = -MaxSlope * Strength * FalloffWeight * 0.1;
            }
            else
            {
                // Default: treat as sculpt (raise)
                HeightDelta = Strength * FalloffWeight * 256.0;
            }

            int32 NewHeight = FMath::Clamp(
                static_cast<int32>(CurrentHeight + HeightDelta), 0, 65535);
            HeightData[Idx] = static_cast<uint16>(NewHeight);
            ModifiedVertices++;
        }
    }

    // Write modified height data back
    EditData.SetHeightData(ReadMinX, ReadMinY, ReadMaxX, ReadMaxY,
        HeightData.GetData(), 0, true);

    Landscape->MarkPackageDirty();

    TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
    Data->SetStringField(TEXT("landscape_label"), Landscape->GetActorLabel());
    Data->SetStringField(TEXT("tool_mode"), ToolMode);
    Data->SetNumberField(TEXT("center_x"), CenterX);
    Data->SetNumberField(TEXT("center_y"), CenterY);
    Data->SetNumberField(TEXT("radius"), Radius);
    Data->SetNumberField(TEXT("strength"), Strength);
    Data->SetNumberField(TEXT("modified_vertices"), ModifiedVertices);
    return MakeSuccess(Data);
}

// ─────────────────────────────────────────────────────────────────────────────
// landscape.paint_landscape_layer
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FNexusLandscapeHandler::HandlePaintLandscapeLayer(
    const TSharedPtr<FJsonObject>& Params)
{
    UWorld* World = GetEditorWorld();
    if (!World)
    {
        return MakeError(TEXT("NO_WORLD"), TEXT("No editor world available"));
    }

    FString LandscapeLabel = GetStringParam(Params, TEXT("landscape_label"));
    ALandscapeProxy* Landscape = FindLandscapeByLabel(World, LandscapeLabel);
    if (!Landscape)
    {
        return MakeError(TEXT("LANDSCAPE_NOT_FOUND"),
            FString::Printf(TEXT("Landscape '%s' not found"), *LandscapeLabel));
    }

    FString LayerName = GetStringParam(Params, TEXT("layer_name"));
    if (LayerName.IsEmpty())
    {
        return MakeError(TEXT("MISSING_PARAM"), TEXT("layer_name is required"));
    }

    const TSharedPtr<FJsonObject>* CenterObj;
    double CenterX = 0.0, CenterY = 0.0;
    if (Params->TryGetObjectField(TEXT("center"), CenterObj))
    {
        CenterX = (*CenterObj)->GetNumberField(TEXT("x"));
        CenterY = (*CenterObj)->GetNumberField(TEXT("y"));
    }
    double Radius = GetNumberParam(Params, TEXT("radius"), 1000.0);
    double Falloff = GetNumberParam(Params, TEXT("falloff"), 0.5);
    double PaintStrength = GetNumberParam(Params, TEXT("strength"), 1.0);
    bool bErase = GetBoolParam(Params, TEXT("erase"), false);

    // Get landscape info
    ULandscapeInfo* LandscapeInfo = Landscape->GetLandscapeInfo();
    if (!LandscapeInfo)
    {
        return MakeError(TEXT("NO_LANDSCAPE_INFO"),
            TEXT("Failed to get landscape info for editing"));
    }

    // Find the landscape layer info for the specified layer name
    ULandscapeLayerInfoObject* TargetLayerInfo = nullptr;
    int32 TargetLayerIndex = -1;

    for (int32 i = 0; i < LandscapeInfo->Layers.Num(); i++)
    {
        if (LandscapeInfo->Layers[i].LayerInfoObj &&
            LandscapeInfo->Layers[i].LayerName.ToString() == LayerName)
        {
            TargetLayerInfo = LandscapeInfo->Layers[i].LayerInfoObj;
            TargetLayerIndex = i;
            break;
        }
    }

    if (!TargetLayerInfo)
    {
        return MakeError(TEXT("LAYER_NOT_FOUND"),
            FString::Printf(TEXT("Landscape layer '%s' not found. "
                "Ensure the layer is defined in the landscape material."), *LayerName));
    }

    // Use FLandscapeEditDataInterface for weight painting
    FLandscapeEditDataInterface EditData(LandscapeInfo);

    FVector LandscapeScale = Landscape->GetActorScale3D();
    FVector LandscapeLocation = Landscape->GetActorLocation();

    double LocalX = (CenterX - LandscapeLocation.X) / LandscapeScale.X;
    double LocalY = (CenterY - LandscapeLocation.Y) / LandscapeScale.Y;
    double LocalRadius = Radius / FMath::Max(LandscapeScale.X, LandscapeScale.Y);

    int32 MinX = FMath::FloorToInt(LocalX - LocalRadius);
    int32 MinY = FMath::FloorToInt(LocalY - LocalRadius);
    int32 MaxX = FMath::CeilToInt(LocalX + LocalRadius);
    int32 MaxY = FMath::CeilToInt(LocalY + LocalRadius);

    MinX = FMath::Max(0, MinX);
    MinY = FMath::Max(0, MinY);

    // Read existing weight data for this layer
    // UE 5.7: GetWeightDataFast now takes uint8* Data instead of TArray<uint8>&
    int32 ReadMinX = MinX, ReadMinY = MinY, ReadMaxX = MaxX, ReadMaxY = MaxY;
    int32 DataSizeX = ReadMaxX - ReadMinX + 1;
    int32 DataSizeY = ReadMaxY - ReadMinY + 1;
    TArray<uint8> WeightData;
    WeightData.SetNumUninitialized(DataSizeX * DataSizeY);
    EditData.GetWeightDataFast(TargetLayerInfo, ReadMinX, ReadMinY, ReadMaxX, ReadMaxY,
        WeightData.GetData(), 0);

    if (WeightData.Num() == 0 || DataSizeX <= 0 || DataSizeY <= 0)
    {
        return MakeError(TEXT("NO_WEIGHT_DATA"),
            TEXT("Could not read weight data from landscape at the specified location"));
    }

    // Apply paint operation
    int32 ModifiedVertices = 0;
    for (int32 Y = 0; Y < DataSizeY; Y++)
    {
        for (int32 X = 0; X < DataSizeX; X++)
        {
            double PosX = ReadMinX + X;
            double PosY = ReadMinY + Y;

            double Dist = FMath::Sqrt(
                FMath::Square(PosX - LocalX) + FMath::Square(PosY - LocalY));
            if (Dist > LocalRadius) continue;

            double NormalizedDist = Dist / LocalRadius;
            double FalloffWeight = 1.0 - FMath::Pow(NormalizedDist, 1.0 / FMath::Max(Falloff, 0.01));
            FalloffWeight = FMath::Clamp(FalloffWeight, 0.0, 1.0);

            int32 Idx = Y * DataSizeX + X;
            uint8 CurrentWeight = WeightData[Idx];

            if (bErase)
            {
                double NewWeight = CurrentWeight - PaintStrength * FalloffWeight * 255.0;
                WeightData[Idx] = static_cast<uint8>(FMath::Clamp(NewWeight, 0.0, 255.0));
            }
            else
            {
                double NewWeight = CurrentWeight + PaintStrength * FalloffWeight * 255.0;
                WeightData[Idx] = static_cast<uint8>(FMath::Clamp(NewWeight, 0.0, 255.0));
            }
            ModifiedVertices++;
        }
    }

    // Write modified weight data back
    EditData.SetAlphaData(TargetLayerInfo, ReadMinX, ReadMinY, ReadMaxX, ReadMaxY,
        WeightData.GetData(), 0, ELandscapeLayerPaintingRestriction::None);

    Landscape->MarkPackageDirty();

    TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
    Data->SetStringField(TEXT("landscape_label"), Landscape->GetActorLabel());
    Data->SetStringField(TEXT("layer_name"), LayerName);
    Data->SetBoolField(TEXT("erase"), bErase);
    Data->SetNumberField(TEXT("center_x"), CenterX);
    Data->SetNumberField(TEXT("center_y"), CenterY);
    Data->SetNumberField(TEXT("radius"), Radius);
    Data->SetNumberField(TEXT("strength"), PaintStrength);
    Data->SetNumberField(TEXT("modified_vertices"), ModifiedVertices);
    return MakeSuccess(Data);
}

// ─────────────────────────────────────────────────────────────────────────────
// landscape.add_foliage_type
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FNexusLandscapeHandler::HandleAddFoliageType(
    const TSharedPtr<FJsonObject>& Params)
{
    FString MeshPath = GetStringParam(Params, TEXT("mesh_path"));
    if (MeshPath.IsEmpty())
    {
        return MakeError(TEXT("MISSING_PARAM"), TEXT("mesh_path is required"));
    }

    double Density = GetNumberParam(Params, TEXT("density"), 100.0);
    bool bAlignToNormal = GetBoolParam(Params, TEXT("align_to_normal"), true);
    bool bRandomYaw = GetBoolParam(Params, TEXT("random_yaw"), true);
    double MinScale = GetNumberParam(Params, TEXT("min_scale"), 0.8);
    double MaxScale = GetNumberParam(Params, TEXT("max_scale"), 1.2);
    double GroundSlopeAngleMax = GetNumberParam(Params, TEXT("ground_slope_angle_max"), 45.0);
    int32 CullDistanceMax = static_cast<int32>(
        GetNumberParam(Params, TEXT("cull_distance_max"), 0.0));

    // Load the static mesh
    UStaticMesh* Mesh = Cast<UStaticMesh>(
        StaticLoadObject(UStaticMesh::StaticClass(), nullptr, *MeshPath));
    if (!Mesh)
    {
        return MakeError(TEXT("ASSET_NOT_FOUND"),
            FString::Printf(TEXT("Static mesh '%s' not found"), *MeshPath));
    }

    UWorld* World = GetEditorWorld();
    if (!World)
    {
        return MakeError(TEXT("NO_WORLD"), TEXT("No editor world available"));
    }

    // Create a saved FoliageType asset so it satisfies IsAsset() for World Partition worlds.
    // The UE assertion in FindOrAddMesh requires IsAsset()==true in partitioned worlds.
    FString FoliageTypeName = FString::Printf(TEXT("FoliageType_%s"),
        *Mesh->GetName());
    FString PackagePath = TEXT("/Game/Foliage");
    FString FullPath = PackagePath / FoliageTypeName;

    // Check if foliage type asset already exists
    UFoliageType_InstancedStaticMesh* FoliageType = LoadObject<UFoliageType_InstancedStaticMesh>(
        nullptr, *FullPath);
    if (!FoliageType)
    {
        IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
        UObject* NewAsset = AssetTools.CreateAsset(
            FoliageTypeName, PackagePath, UFoliageType_InstancedStaticMesh::StaticClass(), nullptr);
        FoliageType = Cast<UFoliageType_InstancedStaticMesh>(NewAsset);
    }
    if (!FoliageType)
    {
        return MakeError(TEXT("CREATE_FAILED"),
            TEXT("Failed to create foliage type asset"));
    }

    // Configure the foliage type
    FoliageType->SetSource(Mesh);
    FoliageType->Density = static_cast<float>(Density);
    FoliageType->AlignToNormal = bAlignToNormal;
    FoliageType->RandomYaw = bRandomYaw;
    FoliageType->ScaleX = FFloatInterval(static_cast<float>(MinScale), static_cast<float>(MaxScale));
    FoliageType->ScaleY = FFloatInterval(static_cast<float>(MinScale), static_cast<float>(MaxScale));
    FoliageType->ScaleZ = FFloatInterval(static_cast<float>(MinScale), static_cast<float>(MaxScale));
    FoliageType->GroundSlopeAngle = FFloatInterval(0.0f, static_cast<float>(GroundSlopeAngleMax));

    if (CullDistanceMax > 0)
    {
        FoliageType->CullDistance = FInt32Interval(0, CullDistanceMax);
    }

    // Mark the asset dirty so it gets saved
    FoliageType->PostEditChange();
    FoliageType->MarkPackageDirty();

    // Get or create the foliage actor for this level
    AInstancedFoliageActor* FoliageActor = AInstancedFoliageActor::GetInstancedFoliageActorForCurrentLevel(World, true);
    if (!FoliageActor)
    {
        return MakeError(TEXT("FOLIAGE_ACTOR_FAILED"),
            TEXT("Failed to get or create instanced foliage actor for current level"));
    }

    // Add the foliage type to the foliage actor
    FFoliageInfo* FoliageInfo = FoliageActor->FindOrAddMesh(FoliageType);
    if (!FoliageInfo)
    {
        return MakeError(TEXT("ADD_MESH_FAILED"),
            TEXT("Failed to add foliage type to foliage actor"));
    }

    FoliageActor->MarkPackageDirty();

    TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
    Data->SetStringField(TEXT("mesh_path"), MeshPath);
    Data->SetStringField(TEXT("foliage_type"), FoliageType->GetPathName());
    Data->SetNumberField(TEXT("density"), Density);
    Data->SetBoolField(TEXT("align_to_normal"), bAlignToNormal);
    Data->SetBoolField(TEXT("random_yaw"), bRandomYaw);
    Data->SetNumberField(TEXT("min_scale"), MinScale);
    Data->SetNumberField(TEXT("max_scale"), MaxScale);
    Data->SetNumberField(TEXT("ground_slope_angle_max"), GroundSlopeAngleMax);
    Data->SetNumberField(TEXT("cull_distance_max"), CullDistanceMax);
    return MakeSuccess(Data);
}

// ─────────────────────────────────────────────────────────────────────────────
// landscape.paint_foliage
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FNexusLandscapeHandler::HandlePaintFoliage(
    const TSharedPtr<FJsonObject>& Params)
{
    UWorld* World = GetEditorWorld();
    if (!World)
    {
        return MakeError(TEXT("NO_WORLD"), TEXT("No editor world available"));
    }

    FString MeshPath = GetStringParam(Params, TEXT("mesh_path"));
    if (MeshPath.IsEmpty())
    {
        return MakeError(TEXT("MISSING_PARAM"), TEXT("mesh_path is required"));
    }

    FVector Center = GetVectorParam(Params, TEXT("center"), FVector::ZeroVector);
    double Radius = GetNumberParam(Params, TEXT("radius"), 1000.0);
    double Density = GetNumberParam(Params, TEXT("density"), 1.0);
    bool bErase = GetBoolParam(Params, TEXT("erase"), false);
    bool bFilterLandscape = GetBoolParam(Params, TEXT("filter_landscape"), true);
    bool bFilterStaticMesh = GetBoolParam(Params, TEXT("filter_static_mesh"), false);

    // Load the mesh to find the matching foliage type
    UStaticMesh* Mesh = Cast<UStaticMesh>(
        StaticLoadObject(UStaticMesh::StaticClass(), nullptr, *MeshPath));
    if (!Mesh)
    {
        return MakeError(TEXT("ASSET_NOT_FOUND"),
            FString::Printf(TEXT("Static mesh '%s' not found"), *MeshPath));
    }

    // Get the foliage actor
    AInstancedFoliageActor* FoliageActor =
        AInstancedFoliageActor::GetInstancedFoliageActorForCurrentLevel(World, false);
    if (!FoliageActor)
    {
        return MakeError(TEXT("NO_FOLIAGE_ACTOR"),
            TEXT("No foliage actor found in current level. Use add_foliage_type first."));
    }

    // Find the foliage type matching this mesh
    // UE 5.7: GetFoliageInfos returns const map, so we first search for the type
    UFoliageType* MatchingType = nullptr;

    for (const auto& Pair : FoliageActor->GetFoliageInfos())
    {
        UFoliageType* FType = Pair.Key;
        if (UFoliageType_InstancedStaticMesh* ISMType =
            Cast<UFoliageType_InstancedStaticMesh>(FType))
        {
            if (ISMType->GetSource() == Mesh)
            {
                MatchingType = FType;
                break;
            }
        }
    }

    if (!MatchingType)
    {
        return MakeError(TEXT("FOLIAGE_TYPE_NOT_FOUND"),
            FString::Printf(TEXT("No foliage type found for mesh '%s'. "
                "Use add_foliage_type first."), *MeshPath));
    }

    // Get mutable FFoliageInfo via FindOrAddMesh
    FFoliageInfo* MatchingInfo = FoliageActor->FindOrAddMesh(MatchingType);
    if (!MatchingInfo)
    {
        return MakeError(TEXT("FOLIAGE_INFO_ERROR"),
            TEXT("Failed to get foliage info for modification"));
    }

    int32 InstancesAffected = 0;

    if (bErase)
    {
        // Remove foliage instances within the radius
        TArray<int32> InstancesToRemove;

        for (int32 i = 0; i < MatchingInfo->Instances.Num(); i++)
        {
            const FFoliageInstance& Instance = MatchingInfo->Instances[i];
            FVector InstanceLoc(Instance.Location);
            double Dist = FVector::Dist2D(InstanceLoc, Center);
            if (Dist <= Radius)
            {
                InstancesToRemove.Add(i);
            }
        }

        // Remove in reverse order to keep indices valid
        for (int32 i = InstancesToRemove.Num() - 1; i >= 0; i--)
        {
            MatchingInfo->RemoveInstances(TArrayView<const int32>(&InstancesToRemove[i], 1), true);
        }
        InstancesAffected = InstancesToRemove.Num();
    }
    else
    {
        // Place new foliage instances via raycasts
        double DensityFactor = FMath::Clamp(Density, 0.01, 10.0);
        double FoliageDensity = MatchingType->Density * DensityFactor;

        // Calculate number of instances to place based on area and density
        double Area = PI * Radius * Radius;
        // Density is per 1000x1000 units (1M sq units)
        int32 NumInstances = FMath::Max(1,
            FMath::RoundToInt(Area * FoliageDensity / 1000000.0));
        NumInstances = FMath::Min(NumInstances, 10000); // Cap at 10K per paint stroke

        TArray<FFoliageInstance> NewInstances;
        for (int32 i = 0; i < NumInstances; i++)
        {
            // Random position within the radius
            double Angle = FMath::FRandRange(0.0, 2.0 * PI);
            double Dist = FMath::Sqrt(FMath::FRandRange(0.0, 1.0)) * Radius;
            FVector TestPos = Center + FVector(
                FMath::Cos(Angle) * Dist,
                FMath::Sin(Angle) * Dist,
                10000.0); // Start high for raycast

            // Raycast down to find surface
            FHitResult HitResult;
            FCollisionQueryParams QueryParams;
            QueryParams.bTraceComplex = true;

            bool bHit = World->LineTraceSingleByChannel(
                HitResult,
                TestPos,
                TestPos - FVector(0, 0, 20000.0),
                ECC_WorldStatic,
                QueryParams);

            if (!bHit) continue;

            // Filter by surface type
            AActor* HitActor = HitResult.GetActor();
            if (!HitActor) continue;

            bool bIsLandscape = HitActor->IsA<ALandscapeProxy>();
            bool bIsStaticMesh = HitResult.Component.IsValid() &&
                HitResult.Component->IsA<UStaticMeshComponent>();

            if (bIsLandscape && !bFilterLandscape) continue;
            if (bIsStaticMesh && !bFilterStaticMesh) continue;
            if (!bIsLandscape && !bIsStaticMesh) continue;

            // Check slope angle
            double SlopeAngle = FMath::RadiansToDegrees(
                FMath::Acos(FVector::DotProduct(HitResult.Normal, FVector::UpVector)));
            if (SlopeAngle > MatchingType->GroundSlopeAngle.Max) continue;

            // Create the instance
            FFoliageInstance NewInstance;
            NewInstance.Location = HitResult.Location;

            if (MatchingType->AlignToNormal)
            {
                NewInstance.Rotation = HitResult.Normal.Rotation();
            }
            else
            {
                NewInstance.Rotation = FRotator::ZeroRotator;
            }

            if (MatchingType->RandomYaw)
            {
                NewInstance.Rotation.Yaw = FMath::FRandRange(0.0f, 360.0f);
            }

            // Random scale
            float Scale = FMath::FRandRange(
                MatchingType->ScaleX.Min, MatchingType->ScaleX.Max);
            NewInstance.DrawScale3D = FVector3f(Scale, Scale, Scale);

            NewInstances.Add(NewInstance);
        }

        // Add all instances at once
        // UE 5.7: AddInstances now takes TArray<const FFoliageInstance*>
        TArray<const FFoliageInstance*> InstancePtrs;
        InstancePtrs.Reserve(NewInstances.Num());
        for (const FFoliageInstance& Inst : NewInstances)
        {
            InstancePtrs.Add(&Inst);
        }
        MatchingInfo->AddInstances(MatchingType, InstancePtrs);
        InstancesAffected = NewInstances.Num();
    }

    FoliageActor->MarkPackageDirty();

    TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
    Data->SetStringField(TEXT("mesh_path"), MeshPath);
    Data->SetBoolField(TEXT("erase"), bErase);
    Data->SetNumberField(TEXT("instances_affected"), InstancesAffected);
    Data->SetObjectField(TEXT("center"), VectorToJson(Center));
    Data->SetNumberField(TEXT("radius"), Radius);
    return MakeSuccess(Data);
}

// ─────────────────────────────────────────────────────────────────────────────
// landscape.remove_foliage
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FNexusLandscapeHandler::HandleRemoveFoliage(
    const TSharedPtr<FJsonObject>& Params)
{
    UWorld* World = GetEditorWorld();
    if (!World)
    {
        return MakeError(TEXT("NO_WORLD"), TEXT("No editor world available"));
    }

    FString MeshPath = GetStringParam(Params, TEXT("mesh_path"));
    if (MeshPath.IsEmpty())
    {
        return MakeError(TEXT("MISSING_PARAM"), TEXT("mesh_path is required"));
    }

    bool bRemoveAll = GetBoolParam(Params, TEXT("remove_all"), false);
    FVector Center = GetVectorParam(Params, TEXT("center"), FVector::ZeroVector);
    double Radius = GetNumberParam(Params, TEXT("radius"), 1000.0);

    // Load the mesh
    UStaticMesh* Mesh = Cast<UStaticMesh>(
        StaticLoadObject(UStaticMesh::StaticClass(), nullptr, *MeshPath));
    if (!Mesh)
    {
        return MakeError(TEXT("ASSET_NOT_FOUND"),
            FString::Printf(TEXT("Static mesh '%s' not found"), *MeshPath));
    }

    AInstancedFoliageActor* FoliageActor =
        AInstancedFoliageActor::GetInstancedFoliageActorForCurrentLevel(World, false);
    if (!FoliageActor)
    {
        return MakeError(TEXT("NO_FOLIAGE_ACTOR"),
            TEXT("No foliage actor found in current level"));
    }

    // Find the matching foliage type
    // UE 5.7: GetFoliageInfos returns const map, so we first search for the type
    UFoliageType* MatchingType = nullptr;
    int32 InstanceCount = 0;

    for (const auto& Pair : FoliageActor->GetFoliageInfos())
    {
        UFoliageType* FType = Pair.Key;
        if (UFoliageType_InstancedStaticMesh* ISMType =
            Cast<UFoliageType_InstancedStaticMesh>(FType))
        {
            if (ISMType->GetSource() == Mesh)
            {
                MatchingType = FType;
                InstanceCount = Pair.Value.Get().Instances.Num();
                break;
            }
        }
    }

    if (!MatchingType)
    {
        return MakeError(TEXT("FOLIAGE_TYPE_NOT_FOUND"),
            FString::Printf(TEXT("No foliage type found for mesh '%s'"), *MeshPath));
    }

    int32 RemovedCount = 0;

    if (bRemoveAll)
    {
        // Remove all instances of this foliage type
        RemovedCount = InstanceCount;
        FoliageActor->RemoveFoliageType(&MatchingType, 1);
    }
    else
    {
        // Get mutable FFoliageInfo via FindOrAddMesh for modification
        FFoliageInfo* MatchingInfo = FoliageActor->FindOrAddMesh(MatchingType);
        if (!MatchingInfo)
        {
            return MakeError(TEXT("FOLIAGE_INFO_ERROR"),
                TEXT("Failed to get foliage info for modification"));
        }

        // Remove instances within radius
        TArray<int32> InstancesToRemove;

        for (int32 i = 0; i < MatchingInfo->Instances.Num(); i++)
        {
            const FFoliageInstance& Instance = MatchingInfo->Instances[i];
            double Dist = FVector::Dist(FVector(Instance.Location), Center);
            if (Dist <= Radius)
            {
                InstancesToRemove.Add(i);
            }
        }

        // Remove in reverse order
        for (int32 i = InstancesToRemove.Num() - 1; i >= 0; i--)
        {
            MatchingInfo->RemoveInstances(TArrayView<const int32>(&InstancesToRemove[i], 1), true);
        }
        RemovedCount = InstancesToRemove.Num();
    }

    FoliageActor->MarkPackageDirty();

    TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
    Data->SetStringField(TEXT("mesh_path"), MeshPath);
    Data->SetBoolField(TEXT("remove_all"), bRemoveAll);
    Data->SetNumberField(TEXT("instances_removed"), RemovedCount);
    if (!bRemoveAll)
    {
        Data->SetObjectField(TEXT("center"), VectorToJson(Center));
        Data->SetNumberField(TEXT("radius"), Radius);
    }
    return MakeSuccess(Data);
}

// ─────────────────────────────────────────────────────────────────────────────
// landscape.import_heightmap
// Long-running: 60s timeout
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FNexusLandscapeHandler::HandleImportHeightmap(
    const TSharedPtr<FJsonObject>& Params)
{
    UWorld* World = GetEditorWorld();
    if (!World)
    {
        return MakeError(TEXT("NO_WORLD"), TEXT("No editor world available"));
    }

    FString LandscapeLabel = GetStringParam(Params, TEXT("landscape_label"));
    ALandscapeProxy* Landscape = FindLandscapeByLabel(World, LandscapeLabel);
    if (!Landscape)
    {
        return MakeError(TEXT("LANDSCAPE_NOT_FOUND"),
            FString::Printf(TEXT("Landscape '%s' not found"), *LandscapeLabel));
    }

    FString FilePath = GetStringParam(Params, TEXT("file_path"));
    if (FilePath.IsEmpty())
    {
        return MakeError(TEXT("MISSING_PARAM"), TEXT("file_path is required"));
    }

    FString HeightmapFormat = GetStringParam(Params, TEXT("heightmap_format"), TEXT("PNG16"));
    bool bFlipY = GetBoolParam(Params, TEXT("flip_y"), false);

    // Verify file exists
    if (!FPaths::FileExists(FilePath))
    {
        return MakeError(TEXT("FILE_NOT_FOUND"),
            FString::Printf(TEXT("Heightmap file '%s' not found"), *FilePath));
    }

    // Get landscape info
    ULandscapeInfo* LandscapeInfo = Landscape->GetLandscapeInfo();
    if (!LandscapeInfo)
    {
        return MakeError(TEXT("NO_LANDSCAPE_INFO"),
            TEXT("Failed to get landscape info"));
    }

    // Load the heightmap file
    TArray<uint8> FileData;
    if (!FFileHelper::LoadFileToArray(FileData, *FilePath))
    {
        return MakeError(TEXT("FILE_READ_FAILED"),
            FString::Printf(TEXT("Failed to read heightmap file '%s'"), *FilePath));
    }

    TArray<uint16> HeightData;
    int32 DataSizeX = 0, DataSizeY = 0;

    if (HeightmapFormat == TEXT("RAW16") || HeightmapFormat == TEXT("R16"))
    {
        // Raw 16-bit unsigned data
        if (FileData.Num() % 2 != 0)
        {
            return MakeError(TEXT("INVALID_FORMAT"),
                TEXT("RAW16 file size must be even (16-bit per sample)"));
        }

        int32 NumSamples = FileData.Num() / 2;
        // Assume square
        DataSizeX = FMath::RoundToInt(FMath::Sqrt(static_cast<double>(NumSamples)));
        DataSizeY = DataSizeX;

        if (DataSizeX * DataSizeY != NumSamples)
        {
            return MakeError(TEXT("INVALID_FORMAT"),
                FString::Printf(TEXT("RAW16 data is not square. %d samples = %dx%d + remainder"),
                    NumSamples, DataSizeX, DataSizeY));
        }

        HeightData.SetNum(NumSamples);
        FMemory::Memcpy(HeightData.GetData(), FileData.GetData(), FileData.Num());
    }
    else if (HeightmapFormat == TEXT("PNG16"))
    {
        // Decode PNG via ImageWrapper
        IImageWrapperModule& ImageWrapperModule =
            FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
        TSharedPtr<IImageWrapper> ImageWrapper =
            ImageWrapperModule.CreateImageWrapper(EImageFormat::PNG);

        if (!ImageWrapper.IsValid() || !ImageWrapper->SetCompressed(
            FileData.GetData(), FileData.Num()))
        {
            return MakeError(TEXT("PNG_DECODE_FAILED"),
                TEXT("Failed to decode PNG heightmap"));
        }

        DataSizeX = ImageWrapper->GetWidth();
        DataSizeY = ImageWrapper->GetHeight();

        TArray<uint8> RawData;
        if (!ImageWrapper->GetRaw(ERGBFormat::Gray, 16, RawData))
        {
            return MakeError(TEXT("PNG_EXTRACT_FAILED"),
                TEXT("Failed to extract 16-bit grayscale data from PNG"));
        }

        HeightData.SetNum(DataSizeX * DataSizeY);
        FMemory::Memcpy(HeightData.GetData(), RawData.GetData(),
            FMath::Min(RawData.Num(), HeightData.Num() * 2));
    }
    else
    {
        return MakeError(TEXT("UNSUPPORTED_FORMAT"),
            FString::Printf(TEXT("Heightmap format '%s' is not supported. "
                "Use PNG16, RAW16, or R16."), *HeightmapFormat));
    }

    // Flip Y if requested
    if (bFlipY)
    {
        for (int32 Y = 0; Y < DataSizeY / 2; Y++)
        {
            for (int32 X = 0; X < DataSizeX; X++)
            {
                int32 TopIdx = Y * DataSizeX + X;
                int32 BottomIdx = (DataSizeY - 1 - Y) * DataSizeX + X;
                Swap(HeightData[TopIdx], HeightData[BottomIdx]);
            }
        }
    }

    // Apply heightmap to landscape
    FLandscapeEditDataInterface EditData(LandscapeInfo);

    // Get the existing landscape extents
    int32 MinX = 0, MinY = 0, MaxX = 0, MaxY = 0;
    if (!LandscapeInfo->GetLandscapeExtent(MinX, MinY, MaxX, MaxY))
    {
        return MakeError(TEXT("EXTENT_FAILED"),
            TEXT("Failed to get landscape extent"));
    }

    int32 LandscapeSizeX = MaxX - MinX + 1;
    int32 LandscapeSizeY = MaxY - MinY + 1;

    // Resize height data if it doesn't match (simple nearest-neighbor)
    if (DataSizeX != LandscapeSizeX || DataSizeY != LandscapeSizeY)
    {
        TArray<uint16> ResizedData;
        ResizedData.SetNum(LandscapeSizeX * LandscapeSizeY);

        for (int32 Y = 0; Y < LandscapeSizeY; Y++)
        {
            for (int32 X = 0; X < LandscapeSizeX; X++)
            {
                int32 SrcX = FMath::Clamp(
                    FMath::RoundToInt(static_cast<double>(X) * DataSizeX / LandscapeSizeX),
                    0, DataSizeX - 1);
                int32 SrcY = FMath::Clamp(
                    FMath::RoundToInt(static_cast<double>(Y) * DataSizeY / LandscapeSizeY),
                    0, DataSizeY - 1);
                ResizedData[Y * LandscapeSizeX + X] = HeightData[SrcY * DataSizeX + SrcX];
            }
        }

        HeightData = MoveTemp(ResizedData);
    }

    // Write height data
    EditData.SetHeightData(MinX, MinY, MaxX, MaxY, HeightData.GetData(), 0, true);

    Landscape->MarkPackageDirty();

    TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
    Data->SetStringField(TEXT("landscape_label"), Landscape->GetActorLabel());
    Data->SetStringField(TEXT("file_path"), FilePath);
    Data->SetStringField(TEXT("format"), HeightmapFormat);
    Data->SetNumberField(TEXT("source_width"), DataSizeX);
    Data->SetNumberField(TEXT("source_height"), DataSizeY);
    Data->SetNumberField(TEXT("landscape_width"), LandscapeSizeX);
    Data->SetNumberField(TEXT("landscape_height"), LandscapeSizeY);
    Data->SetBoolField(TEXT("flip_y"), bFlipY);
    Data->SetBoolField(TEXT("resized"), DataSizeX != LandscapeSizeX || DataSizeY != LandscapeSizeY);
    return MakeSuccess(Data);
}

// ─────────────────────────────────────────────────────────────────────────────
// landscape.export_heightmap
// Long-running: 60s timeout
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FNexusLandscapeHandler::HandleExportHeightmap(
    const TSharedPtr<FJsonObject>& Params)
{
    UWorld* World = GetEditorWorld();
    if (!World)
    {
        return MakeError(TEXT("NO_WORLD"), TEXT("No editor world available"));
    }

    FString LandscapeLabel = GetStringParam(Params, TEXT("landscape_label"));
    ALandscapeProxy* Landscape = FindLandscapeByLabel(World, LandscapeLabel);
    if (!Landscape)
    {
        return MakeError(TEXT("LANDSCAPE_NOT_FOUND"),
            FString::Printf(TEXT("Landscape '%s' not found"), *LandscapeLabel));
    }

    FString FilePath = GetStringParam(Params, TEXT("file_path"));
    if (FilePath.IsEmpty())
    {
        return MakeError(TEXT("MISSING_PARAM"), TEXT("file_path is required"));
    }

    FString HeightmapFormat = GetStringParam(Params, TEXT("heightmap_format"), TEXT("PNG16"));

    // Get landscape info
    ULandscapeInfo* LandscapeInfo = Landscape->GetLandscapeInfo();
    if (!LandscapeInfo)
    {
        return MakeError(TEXT("NO_LANDSCAPE_INFO"),
            TEXT("Failed to get landscape info"));
    }

    // Get landscape extents
    int32 MinX = 0, MinY = 0, MaxX = 0, MaxY = 0;
    if (!LandscapeInfo->GetLandscapeExtent(MinX, MinY, MaxX, MaxY))
    {
        return MakeError(TEXT("EXTENT_FAILED"),
            TEXT("Failed to get landscape extent"));
    }

    int32 SizeX = MaxX - MinX + 1;
    int32 SizeY = MaxY - MinY + 1;

    // Read height data
    // UE 5.7: GetHeightDataFast now takes uint16* Data instead of TArray<uint16>&
    FLandscapeEditDataInterface EditData(LandscapeInfo);
    int32 ReadMinX = MinX, ReadMinY = MinY, ReadMaxX = MaxX, ReadMaxY = MaxY;
    TArray<uint16> HeightData;
    HeightData.SetNumUninitialized(SizeX * SizeY);
    EditData.GetHeightDataFast(ReadMinX, ReadMinY, ReadMaxX, ReadMaxY, HeightData.GetData(), 0);

    if (HeightData.Num() == 0)
    {
        return MakeError(TEXT("NO_HEIGHT_DATA"),
            TEXT("Failed to read height data from landscape"));
    }

    // Ensure output directory exists
    FString OutputDir = FPaths::GetPath(FilePath);
    if (!FPaths::DirectoryExists(OutputDir))
    {
        IPlatformFile::GetPlatformPhysical().CreateDirectoryTree(*OutputDir);
    }

    bool bWriteSuccess = false;

    if (HeightmapFormat == TEXT("RAW16") || HeightmapFormat == TEXT("R16"))
    {
        // Write raw 16-bit data
        TArray<uint8> RawData;
        RawData.SetNum(HeightData.Num() * 2);
        FMemory::Memcpy(RawData.GetData(), HeightData.GetData(), RawData.Num());
        bWriteSuccess = FFileHelper::SaveArrayToFile(RawData, *FilePath);
    }
    else if (HeightmapFormat == TEXT("PNG16"))
    {
        // Encode as 16-bit grayscale PNG
        IImageWrapperModule& ImageWrapperModule =
            FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
        TSharedPtr<IImageWrapper> ImageWrapper =
            ImageWrapperModule.CreateImageWrapper(EImageFormat::PNG);

        if (ImageWrapper.IsValid())
        {
            // SetRaw expects raw bytes
            TArray<uint8> RawBytes;
            RawBytes.SetNum(HeightData.Num() * 2);
            FMemory::Memcpy(RawBytes.GetData(), HeightData.GetData(), RawBytes.Num());

            if (ImageWrapper->SetRaw(RawBytes.GetData(), RawBytes.Num(),
                SizeX, SizeY, ERGBFormat::Gray, 16))
            {
                // UE 5.7: GetCompressed returns TArray64<uint8>, convert to TArray<uint8>
                TArray64<uint8> CompressedData64 = ImageWrapper->GetCompressed();
                TArray<uint8> CompressedData;
                CompressedData.SetNumUninitialized(CompressedData64.Num());
                FMemory::Memcpy(CompressedData.GetData(), CompressedData64.GetData(), CompressedData64.Num());
                bWriteSuccess = FFileHelper::SaveArrayToFile(CompressedData, *FilePath);
            }
        }
    }
    else
    {
        return MakeError(TEXT("UNSUPPORTED_FORMAT"),
            FString::Printf(TEXT("Heightmap format '%s' is not supported"), *HeightmapFormat));
    }

    if (!bWriteSuccess)
    {
        return MakeError(TEXT("WRITE_FAILED"),
            FString::Printf(TEXT("Failed to write heightmap to '%s'"), *FilePath));
    }

    TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
    Data->SetStringField(TEXT("landscape_label"), Landscape->GetActorLabel());
    Data->SetStringField(TEXT("file_path"), FilePath);
    Data->SetStringField(TEXT("format"), HeightmapFormat);
    Data->SetNumberField(TEXT("width"), SizeX);
    Data->SetNumberField(TEXT("height"), SizeY);
    Data->SetNumberField(TEXT("file_size_bytes"),
        static_cast<double>(IFileManager::Get().FileSize(*FilePath)));
    return MakeSuccess(Data);
}

// ─────────────────────────────────────────────────────────────────────────────
// landscape.get_landscape_info
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FNexusLandscapeHandler::HandleGetLandscapeInfo(
    const TSharedPtr<FJsonObject>& Params)
{
    UWorld* World = GetEditorWorld();
    if (!World)
    {
        return MakeError(TEXT("NO_WORLD"), TEXT("No editor world available"));
    }

    FString LandscapeLabel = GetStringParam(Params, TEXT("landscape_label"));

    // If no label, list all landscapes
    if (LandscapeLabel.IsEmpty())
    {
        TArray<TSharedPtr<FJsonValue>> LandscapesArr;
        for (TActorIterator<ALandscapeProxy> It(World); It; ++It)
        {
            ALandscapeProxy* LP = *It;
            TSharedPtr<FJsonObject> LPObj = MakeShareable(new FJsonObject());
            LPObj->SetStringField(TEXT("actor_path"), LP->GetPathName());
            LPObj->SetStringField(TEXT("actor_label"), LP->GetActorLabel());
            LPObj->SetStringField(TEXT("class"), LP->GetClass()->GetName());
            LPObj->SetObjectField(TEXT("location"), VectorToJson(LP->GetActorLocation()));
            LPObj->SetObjectField(TEXT("scale"), VectorToJson(LP->GetActorScale3D()));
            LPObj->SetNumberField(TEXT("component_count"), LP->LandscapeComponents.Num());

            if (LP->LandscapeMaterial)
            {
                LPObj->SetStringField(TEXT("material_path"),
                    LP->LandscapeMaterial->GetPathName());
            }

            LandscapesArr.Add(MakeShareable(new FJsonValueObject(LPObj)));
        }

        TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
        Data->SetArrayField(TEXT("landscapes"), LandscapesArr);
        Data->SetNumberField(TEXT("count"), LandscapesArr.Num());
        return MakeSuccess(Data);
    }

    // Get specific landscape info
    ALandscapeProxy* Landscape = FindLandscapeByLabel(World, LandscapeLabel);
    if (!Landscape)
    {
        return MakeError(TEXT("LANDSCAPE_NOT_FOUND"),
            FString::Printf(TEXT("Landscape '%s' not found"), *LandscapeLabel));
    }

    ULandscapeInfo* LandscapeInfo = Landscape->GetLandscapeInfo();

    TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
    Data->SetStringField(TEXT("actor_path"), Landscape->GetPathName());
    Data->SetStringField(TEXT("actor_label"), Landscape->GetActorLabel());
    Data->SetStringField(TEXT("class"), Landscape->GetClass()->GetName());
    Data->SetObjectField(TEXT("location"), VectorToJson(Landscape->GetActorLocation()));
    Data->SetObjectField(TEXT("scale"), VectorToJson(Landscape->GetActorScale3D()));
    Data->SetNumberField(TEXT("component_count"), Landscape->LandscapeComponents.Num());

    // Grid dimensions
    if (LandscapeInfo)
    {
        int32 MinX = 0, MinY = 0, MaxX = 0, MaxY = 0;
        if (LandscapeInfo->GetLandscapeExtent(MinX, MinY, MaxX, MaxY))
        {
            TSharedPtr<FJsonObject> GridObj = MakeShareable(new FJsonObject());
            GridObj->SetNumberField(TEXT("min_x"), MinX);
            GridObj->SetNumberField(TEXT("min_y"), MinY);
            GridObj->SetNumberField(TEXT("max_x"), MaxX);
            GridObj->SetNumberField(TEXT("max_y"), MaxY);
            GridObj->SetNumberField(TEXT("size_x"), MaxX - MinX + 1);
            GridObj->SetNumberField(TEXT("size_y"), MaxY - MinY + 1);
            Data->SetObjectField(TEXT("grid"), GridObj);
        }

        // Layer info
        TArray<TSharedPtr<FJsonValue>> LayersArr;
        for (const FLandscapeInfoLayerSettings& LayerSettings : LandscapeInfo->Layers)
        {
            TSharedPtr<FJsonObject> LayerObj = MakeShareable(new FJsonObject());
            LayerObj->SetStringField(TEXT("name"), LayerSettings.LayerName.ToString());

            if (LayerSettings.LayerInfoObj)
            {
                LayerObj->SetStringField(TEXT("layer_info_path"),
                    LayerSettings.LayerInfoObj->GetPathName());
                // UE 5.7: bNoWeightBlend is deprecated, use GetBlendMethod() instead
                LayerObj->SetBoolField(TEXT("is_no_weight_blend"),
                    LayerSettings.LayerInfoObj->GetBlendMethod() == ELandscapeTargetLayerBlendMethod::None);
            }

            LayersArr.Add(MakeShareable(new FJsonValueObject(LayerObj)));
        }
        Data->SetArrayField(TEXT("layers"), LayersArr);
        Data->SetNumberField(TEXT("layer_count"), LayersArr.Num());
    }

    // Material
    if (Landscape->LandscapeMaterial)
    {
        Data->SetStringField(TEXT("material_path"),
            Landscape->LandscapeMaterial->GetPathName());
    }

    // Bounding box
    FBox BoundingBox = Landscape->GetComponentsBoundingBox(true);
    TSharedPtr<FJsonObject> BBoxObj = MakeShareable(new FJsonObject());
    BBoxObj->SetObjectField(TEXT("min"), VectorToJson(BoundingBox.Min));
    BBoxObj->SetObjectField(TEXT("max"), VectorToJson(BoundingBox.Max));
    FVector Extent = BoundingBox.GetExtent();
    BBoxObj->SetObjectField(TEXT("extent"), VectorToJson(Extent));
    Data->SetObjectField(TEXT("bounding_box"), BBoxObj);

    // Component details
    if (Landscape->LandscapeComponents.Num() > 0)
    {
        ULandscapeComponent* FirstComp = Landscape->LandscapeComponents[0];
        if (FirstComp)
        {
            TSharedPtr<FJsonObject> CompObj = MakeShareable(new FJsonObject());
            CompObj->SetNumberField(TEXT("subsection_size_quads"),
                FirstComp->SubsectionSizeQuads);
            CompObj->SetNumberField(TEXT("num_subsections"),
                FirstComp->NumSubsections);
            CompObj->SetNumberField(TEXT("component_size_quads"),
                FirstComp->ComponentSizeQuads);
            Data->SetObjectField(TEXT("component_info"), CompObj);
        }
    }

    // Foliage info
    AInstancedFoliageActor* FoliageActor =
        AInstancedFoliageActor::GetInstancedFoliageActorForCurrentLevel(World, false);
    if (FoliageActor)
    {
        TArray<TSharedPtr<FJsonValue>> FoliageArr;
        int32 TotalInstances = 0;

        for (const auto& Pair : FoliageActor->GetFoliageInfos())
        {
            UFoliageType* FType = Pair.Key;
            const FFoliageInfo& FInfo = Pair.Value.Get();

            TSharedPtr<FJsonObject> FoliageObj = MakeShareable(new FJsonObject());
            FoliageObj->SetStringField(TEXT("foliage_type"), FType->GetPathName());
            FoliageObj->SetNumberField(TEXT("instance_count"), FInfo.Instances.Num());
            FoliageObj->SetNumberField(TEXT("density"), FType->Density);

            if (UFoliageType_InstancedStaticMesh* ISMType =
                Cast<UFoliageType_InstancedStaticMesh>(FType))
            {
                UStaticMesh* Mesh = Cast<UStaticMesh>(ISMType->GetSource());
                if (Mesh)
                {
                    FoliageObj->SetStringField(TEXT("mesh_path"), Mesh->GetPathName());
                }
            }

            FoliageArr.Add(MakeShareable(new FJsonValueObject(FoliageObj)));
            TotalInstances += FInfo.Instances.Num();
        }

        Data->SetArrayField(TEXT("foliage_types"), FoliageArr);
        Data->SetNumberField(TEXT("foliage_type_count"), FoliageArr.Num());
        Data->SetNumberField(TEXT("total_foliage_instances"), TotalInstances);
    }

    return MakeSuccess(Data);
}
