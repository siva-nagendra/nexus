// Copyright Nexus Team. All Rights Reserved.

#include "Handlers/NexusMaterialHandler.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Materials/MaterialExpression.h"
#include "Materials/MaterialExpressionTextureSample.h"
#include "Materials/MaterialExpressionTextureCoordinate.h"
#include "Materials/MaterialExpressionConstant.h"
#include "Materials/MaterialExpressionConstant3Vector.h"
#include "Materials/MaterialExpressionConstant4Vector.h"
#include "Materials/MaterialExpressionAdd.h"
#include "Materials/MaterialExpressionMultiply.h"
#include "Materials/MaterialExpressionSubtract.h"
#include "Materials/MaterialExpressionDivide.h"
#include "Materials/MaterialExpressionLinearInterpolate.h"
#include "Materials/MaterialExpressionClamp.h"
#include "Materials/MaterialExpressionOneMinus.h"
#include "Materials/MaterialExpressionPower.h"
#include "Materials/MaterialExpressionAbs.h"
#include "Materials/MaterialExpressionComponentMask.h"
#include "Materials/MaterialExpressionAppendVector.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#include "Materials/MaterialExpressionTextureObjectParameter.h"
#include "Materials/MaterialExpressionWorldPosition.h"
#include "Materials/MaterialExpressionTime.h"
#include "AssetToolsModule.h"
#include "Factories/MaterialFactoryNew.h"
#include "Factories/MaterialInstanceConstantFactoryNew.h"
#include "Engine/Texture.h"
#include "Engine/Texture2D.h"
#include "Components/MeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Editor.h"
#include "EngineUtils.h"

// ─────────────────────────────────────────────────────────────────────────────
// Dispatch
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FNexusMaterialHandler::Handle(
    const FString& CommandType,
    const TSharedPtr<FJsonObject>& Params)
{
    FString SubCommand = CommandType.RightChop(GetNamespace().Len() + 1);

    if (SubCommand == TEXT("create"))               return HandleCreate(Params);
    if (SubCommand == TEXT("create_instance"))       return HandleCreateInstance(Params);
    if (SubCommand == TEXT("info") ||
        SubCommand == TEXT("get_info"))              return HandleInfo(Params);
    if (SubCommand == TEXT("set_scalar_parameter"))  return HandleSetScalarParameter(Params);
    if (SubCommand == TEXT("set_vector_parameter"))  return HandleSetVectorParameter(Params);
    if (SubCommand == TEXT("set_texture_parameter")) return HandleSetTextureParameter(Params);
    if (SubCommand == TEXT("get_parameters"))        return HandleGetParameters(Params);
    if (SubCommand == TEXT("set_shading_model"))     return HandleSetShadingModel(Params);
    if (SubCommand == TEXT("set_blend_mode"))        return HandleSetBlendMode(Params);
    if (SubCommand == TEXT("apply_to_actor"))        return HandleApplyToActor(Params);
    if (SubCommand == TEXT("set_two_sided"))         return HandleSetTwoSided(Params);
    if (SubCommand == TEXT("get_expressions"))       return HandleGetExpressions(Params);
    // Batch operations (A3)
    if (SubCommand == TEXT("apply_batch"))           return HandleApplyBatch(Params);
    // Expression CRUD (Node CRUD Phase 1)
    if (SubCommand == TEXT("add_expression"))        return HandleAddExpression(Params);
    if (SubCommand == TEXT("get_expression_pins"))   return HandleGetExpressionPins(Params);
    if (SubCommand == TEXT("update_expression"))     return HandleUpdateExpression(Params);
    if (SubCommand == TEXT("remove_expression"))     return HandleRemoveExpression(Params);
    if (SubCommand == TEXT("connect_expressions"))   return HandleConnectExpressions(Params);
    if (SubCommand == TEXT("disconnect_expression")) return HandleDisconnectExpression(Params);

    return MakeError(TEXT("NOT_IMPLEMENTED"),
        FString::Printf(TEXT("Command '%s' not yet implemented"), *CommandType));
}

// ─────────────────────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────────────────────

EMaterialShadingModel FNexusMaterialHandler::ParseShadingModel(const FString& Model)
{
    if (Model == TEXT("Unlit"))               return MSM_Unlit;
    if (Model == TEXT("Subsurface"))          return MSM_Subsurface;
    if (Model == TEXT("SubsurfaceProfile"))   return MSM_SubsurfaceProfile;
    if (Model == TEXT("ClearCoat"))           return MSM_ClearCoat;
    if (Model == TEXT("TwoSidedFoliage"))     return MSM_TwoSidedFoliage;
    if (Model == TEXT("Hair"))                return MSM_Hair;
    if (Model == TEXT("Cloth"))               return MSM_Cloth;
    if (Model == TEXT("Eye"))                 return MSM_Eye;
    if (Model == TEXT("SingleLayerWater"))    return MSM_SingleLayerWater;
    if (Model == TEXT("ThinTranslucent"))     return MSM_ThinTranslucent;
    // Default
    return MSM_DefaultLit;
}

EBlendMode FNexusMaterialHandler::ParseBlendMode(const FString& Mode)
{
    if (Mode == TEXT("Translucent"))      return BLEND_Translucent;
    if (Mode == TEXT("Masked"))           return BLEND_Masked;
    if (Mode == TEXT("Additive"))         return BLEND_Additive;
    if (Mode == TEXT("Modulate"))         return BLEND_Modulate;
    if (Mode == TEXT("AlphaComposite"))   return BLEND_AlphaComposite;
    if (Mode == TEXT("AlphaHoldout"))     return BLEND_AlphaHoldout;
    // Default
    return BLEND_Opaque;
}

// ─────────────────────────────────────────────────────────────────────────────
// create — supports material_path OR legacy name+path params
// Python sends: material_path, shading_model, blend_mode, two_sided
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FNexusMaterialHandler::HandleCreate(
    const TSharedPtr<FJsonObject>& Params)
{
    // Support both Python tool params (material_path) and legacy (name+path)
    FString MaterialPath = GetStringParam(Params, TEXT("material_path"));
    FString Name, Path;

    if (!MaterialPath.IsEmpty())
    {
        // Parse /Game/Materials/M_MyMaterial into path=/Game/Materials and name=M_MyMaterial
        int32 LastSlash;
        if (MaterialPath.FindLastChar('/', LastSlash))
        {
            Path = MaterialPath.Left(LastSlash);
            Name = MaterialPath.RightChop(LastSlash + 1);
        }
        else
        {
            Name = MaterialPath;
            Path = TEXT("/Game/Materials");
        }
    }
    else
    {
        Name = GetStringParam(Params, TEXT("name"), TEXT("M_New"));
        Path = GetStringParam(Params, TEXT("path"), TEXT("/Game/Materials"));
    }

    IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
    UMaterialFactoryNew* Factory = NewObject<UMaterialFactoryNew>();
    UObject* Asset = AssetTools.CreateAsset(Name, Path, UMaterial::StaticClass(), Factory);

    if (!Asset)
    {
        return MakeError(TEXT("CREATE_FAILED"), TEXT("Failed to create material"));
    }

    UMaterial* Mat = Cast<UMaterial>(Asset);
    if (Mat)
    {
        // Apply optional initial properties
        FString ShadingModel = GetStringParam(Params, TEXT("shading_model"));
        if (!ShadingModel.IsEmpty())
        {
            Mat->SetShadingModel(ParseShadingModel(ShadingModel));
        }

        FString BlendMode = GetStringParam(Params, TEXT("blend_mode"));
        if (!BlendMode.IsEmpty())
        {
            Mat->BlendMode = ParseBlendMode(BlendMode);
        }

        bool bTwoSided = GetBoolParam(Params, TEXT("two_sided"), false);
        if (bTwoSided)
        {
            Mat->TwoSided = true;
        }

        Mat->PostEditChange();
    }

    TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
    Data->SetStringField(TEXT("path"), Asset->GetPathName());
    Data->SetStringField(TEXT("name"), Name);
    return MakeSuccess(Data);
}

// ─────────────────────────────────────────────────────────────────────────────
// create_instance
// Python sends: instance_path, parent_material_path
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FNexusMaterialHandler::HandleCreateInstance(
    const TSharedPtr<FJsonObject>& Params)
{
    // Support both Python tool params (instance_path/parent_material_path) and legacy
    FString InstancePath = GetStringParam(Params, TEXT("instance_path"));
    FString ParentPath = GetStringParam(Params, TEXT("parent_material_path"));

    FString Name, Path;

    if (!InstancePath.IsEmpty())
    {
        int32 LastSlash;
        if (InstancePath.FindLastChar('/', LastSlash))
        {
            Path = InstancePath.Left(LastSlash);
            Name = InstancePath.RightChop(LastSlash + 1);
        }
        else
        {
            Name = InstancePath;
            Path = TEXT("/Game/Materials");
        }
    }
    else
    {
        Name = GetStringParam(Params, TEXT("name"), TEXT("MI_New"));
        Path = GetStringParam(Params, TEXT("path"), TEXT("/Game/Materials"));
    }

    if (ParentPath.IsEmpty())
    {
        ParentPath = GetStringParam(Params, TEXT("parent_path"));
    }

    UMaterialInterface* Parent = LoadObject<UMaterialInterface>(nullptr, *ParentPath);
    if (!Parent)
    {
        return MakeError(TEXT("PARENT_NOT_FOUND"),
            FString::Printf(TEXT("Parent material not found at '%s'"), *ParentPath));
    }

    IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
    UMaterialInstanceConstantFactoryNew* Factory = NewObject<UMaterialInstanceConstantFactoryNew>();
    Factory->InitialParent = Parent;
    UObject* Asset = AssetTools.CreateAsset(Name, Path, UMaterialInstanceConstant::StaticClass(), Factory);

    if (!Asset)
    {
        return MakeError(TEXT("CREATE_FAILED"), TEXT("Failed to create material instance"));
    }

    TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
    Data->SetStringField(TEXT("path"), Asset->GetPathName());
    Data->SetStringField(TEXT("name"), Name);
    Data->SetStringField(TEXT("parent_path"), ParentPath);
    Data->SetBoolField(TEXT("is_instance"), true);
    return MakeSuccess(Data);
}

// ─────────────────────────────────────────────────────────────────────────────
// info / get_info
// Python sends: material_path
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FNexusMaterialHandler::HandleInfo(
    const TSharedPtr<FJsonObject>& Params)
{
    FString MaterialPath = GetStringParam(Params, TEXT("material_path"));
    if (MaterialPath.IsEmpty())
    {
        MaterialPath = GetStringParam(Params, TEXT("path"));
    }

    UObject* Asset = LoadObject<UObject>(nullptr, *MaterialPath);
    if (!Asset)
    {
        return MakeError(TEXT("NOT_FOUND"),
            FString::Printf(TEXT("Material not found at '%s'"), *MaterialPath));
    }

    bool bIsInstance = Asset->IsA<UMaterialInstanceConstant>();

    TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
    Data->SetStringField(TEXT("path"), Asset->GetPathName());
    Data->SetStringField(TEXT("name"), Asset->GetName());
    Data->SetBoolField(TEXT("is_instance"), bIsInstance);

    if (UMaterial* Mat = Cast<UMaterial>(Asset))
    {
        Data->SetStringField(TEXT("shading_model"),
            *UEnum::GetValueAsString(Mat->GetShadingModels().GetFirstShadingModel()));
        Data->SetStringField(TEXT("blend_mode"),
            *UEnum::GetValueAsString(Mat->BlendMode.GetValue()));
        Data->SetBoolField(TEXT("two_sided"), Mat->IsTwoSided());
    }
    else if (UMaterialInstanceConstant* MIC = Cast<UMaterialInstanceConstant>(Asset))
    {
        if (MIC->Parent)
        {
            Data->SetStringField(TEXT("parent_path"), MIC->Parent->GetPathName());
        }
    }

    return MakeSuccess(Data);
}

// ─────────────────────────────────────────────────────────────────────────────
// set_scalar_parameter
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FNexusMaterialHandler::HandleSetScalarParameter(
    const TSharedPtr<FJsonObject>& Params)
{
    FString MaterialPath = GetStringParam(Params, TEXT("material_path"));
    FString ParamName = GetStringParam(Params, TEXT("parameter_name"));
    double Value = GetNumberParam(Params, TEXT("value"), 0.0);

    UMaterialInstanceConstant* MIC = LoadObject<UMaterialInstanceConstant>(nullptr, *MaterialPath);
    if (!MIC)
    {
        return MakeError(TEXT("NOT_FOUND"),
            FString::Printf(TEXT("Material instance not found at '%s'"), *MaterialPath));
    }

    FMaterialParameterInfo ParamInfo{FName(*ParamName)};
    MIC->SetScalarParameterValueEditorOnly(ParamInfo, static_cast<float>(Value));
    MIC->PostEditChange();
    MIC->MarkPackageDirty();

    TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
    Data->SetStringField(TEXT("material_path"), MaterialPath);
    Data->SetStringField(TEXT("parameter_name"), ParamName);
    Data->SetNumberField(TEXT("value"), Value);
    return MakeSuccess(Data);
}

// ─────────────────────────────────────────────────────────────────────────────
// set_vector_parameter
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FNexusMaterialHandler::HandleSetVectorParameter(
    const TSharedPtr<FJsonObject>& Params)
{
    FString MaterialPath = GetStringParam(Params, TEXT("material_path"));
    FString ParamName = GetStringParam(Params, TEXT("parameter_name"));

    // Value comes as a sub-object {r, g, b, a}
    float R = 0.f, G = 0.f, B = 0.f, A = 1.f;
    const TSharedPtr<FJsonObject>* ValueObj;
    if (Params->TryGetObjectField(TEXT("value"), ValueObj))
    {
        R = static_cast<float>((*ValueObj)->GetNumberField(TEXT("r")));
        G = static_cast<float>((*ValueObj)->GetNumberField(TEXT("g")));
        B = static_cast<float>((*ValueObj)->GetNumberField(TEXT("b")));
        A = static_cast<float>((*ValueObj)->GetNumberField(TEXT("a")));
    }

    UMaterialInstanceConstant* MIC = LoadObject<UMaterialInstanceConstant>(nullptr, *MaterialPath);
    if (!MIC)
    {
        return MakeError(TEXT("NOT_FOUND"),
            FString::Printf(TEXT("Material instance not found at '%s'"), *MaterialPath));
    }

    FMaterialParameterInfo ParamInfo{FName(*ParamName)};
    MIC->SetVectorParameterValueEditorOnly(ParamInfo, FLinearColor(R, G, B, A));
    MIC->PostEditChange();
    MIC->MarkPackageDirty();

    TSharedPtr<FJsonObject> ValueJson = MakeShareable(new FJsonObject());
    ValueJson->SetNumberField(TEXT("r"), R);
    ValueJson->SetNumberField(TEXT("g"), G);
    ValueJson->SetNumberField(TEXT("b"), B);
    ValueJson->SetNumberField(TEXT("a"), A);

    TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
    Data->SetStringField(TEXT("material_path"), MaterialPath);
    Data->SetStringField(TEXT("parameter_name"), ParamName);
    Data->SetObjectField(TEXT("value"), ValueJson);
    return MakeSuccess(Data);
}

// ─────────────────────────────────────────────────────────────────────────────
// set_texture_parameter
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FNexusMaterialHandler::HandleSetTextureParameter(
    const TSharedPtr<FJsonObject>& Params)
{
    FString MaterialPath = GetStringParam(Params, TEXT("material_path"));
    FString ParamName = GetStringParam(Params, TEXT("parameter_name"));
    FString TexturePath = GetStringParam(Params, TEXT("texture_path"));

    UMaterialInstanceConstant* MIC = LoadObject<UMaterialInstanceConstant>(nullptr, *MaterialPath);
    if (!MIC)
    {
        return MakeError(TEXT("NOT_FOUND"),
            FString::Printf(TEXT("Material instance not found at '%s'"), *MaterialPath));
    }

    UTexture* Texture = LoadObject<UTexture>(nullptr, *TexturePath);
    if (!Texture)
    {
        return MakeError(TEXT("TEXTURE_NOT_FOUND"),
            FString::Printf(TEXT("Texture not found at '%s'"), *TexturePath));
    }

    FMaterialParameterInfo ParamInfo{FName(*ParamName)};
    MIC->SetTextureParameterValueEditorOnly(ParamInfo, Texture);
    MIC->PostEditChange();
    MIC->MarkPackageDirty();

    TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
    Data->SetStringField(TEXT("material_path"), MaterialPath);
    Data->SetStringField(TEXT("parameter_name"), ParamName);
    Data->SetStringField(TEXT("texture_path"), Texture->GetPathName());
    return MakeSuccess(Data);
}

// ─────────────────────────────────────────────────────────────────────────────
// get_parameters
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FNexusMaterialHandler::HandleGetParameters(
    const TSharedPtr<FJsonObject>& Params)
{
    FString MaterialPath = GetStringParam(Params, TEXT("material_path"));
    if (MaterialPath.IsEmpty())
    {
        MaterialPath = GetStringParam(Params, TEXT("path"));
    }

    UMaterialInterface* Material = LoadObject<UMaterialInterface>(nullptr, *MaterialPath);
    if (!Material)
    {
        return MakeError(TEXT("NOT_FOUND"),
            FString::Printf(TEXT("Material not found at '%s'"), *MaterialPath));
    }

    // Scalar parameters
    TArray<FMaterialParameterInfo> ScalarInfos;
    TArray<FGuid> ScalarGuids;
    Material->GetAllScalarParameterInfo(ScalarInfos, ScalarGuids);

    TArray<TSharedPtr<FJsonValue>> ScalarArray;
    for (const FMaterialParameterInfo& Info : ScalarInfos)
    {
        float ScalarValue = 0.f;
        Material->GetScalarParameterValue(Info, ScalarValue);

        TSharedPtr<FJsonObject> Entry = MakeShareable(new FJsonObject());
        Entry->SetStringField(TEXT("name"), Info.Name.ToString());
        Entry->SetNumberField(TEXT("value"), ScalarValue);
        ScalarArray.Add(MakeShareable(new FJsonValueObject(Entry)));
    }

    // Vector parameters
    TArray<FMaterialParameterInfo> VectorInfos;
    TArray<FGuid> VectorGuids;
    Material->GetAllVectorParameterInfo(VectorInfos, VectorGuids);

    TArray<TSharedPtr<FJsonValue>> VectorArray;
    for (const FMaterialParameterInfo& Info : VectorInfos)
    {
        FLinearColor VecValue;
        Material->GetVectorParameterValue(Info, VecValue);

        TSharedPtr<FJsonObject> ValObj = MakeShareable(new FJsonObject());
        ValObj->SetNumberField(TEXT("r"), VecValue.R);
        ValObj->SetNumberField(TEXT("g"), VecValue.G);
        ValObj->SetNumberField(TEXT("b"), VecValue.B);
        ValObj->SetNumberField(TEXT("a"), VecValue.A);

        TSharedPtr<FJsonObject> Entry = MakeShareable(new FJsonObject());
        Entry->SetStringField(TEXT("name"), Info.Name.ToString());
        Entry->SetObjectField(TEXT("value"), ValObj);
        VectorArray.Add(MakeShareable(new FJsonValueObject(Entry)));
    }

    // Texture parameters
    TArray<FMaterialParameterInfo> TextureInfos;
    TArray<FGuid> TextureGuids;
    Material->GetAllTextureParameterInfo(TextureInfos, TextureGuids);

    TArray<TSharedPtr<FJsonValue>> TextureArray;
    for (const FMaterialParameterInfo& Info : TextureInfos)
    {
        UTexture* TexValue = nullptr;
        Material->GetTextureParameterValue(Info, TexValue);

        TSharedPtr<FJsonObject> Entry = MakeShareable(new FJsonObject());
        Entry->SetStringField(TEXT("name"), Info.Name.ToString());
        Entry->SetStringField(TEXT("texture_path"),
            TexValue ? TexValue->GetPathName() : TEXT(""));
        TextureArray.Add(MakeShareable(new FJsonValueObject(Entry)));
    }

    TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
    Data->SetStringField(TEXT("material_path"), MaterialPath);
    Data->SetArrayField(TEXT("scalars"), ScalarArray);
    Data->SetArrayField(TEXT("vectors"), VectorArray);
    Data->SetArrayField(TEXT("textures"), TextureArray);
    return MakeSuccess(Data);
}

// ─────────────────────────────────────────────────────────────────────────────
// set_shading_model
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FNexusMaterialHandler::HandleSetShadingModel(
    const TSharedPtr<FJsonObject>& Params)
{
    FString MaterialPath = GetStringParam(Params, TEXT("material_path"));
    FString ShadingModel = GetStringParam(Params, TEXT("shading_model"), TEXT("DefaultLit"));

    UMaterial* Mat = LoadObject<UMaterial>(nullptr, *MaterialPath);
    if (!Mat)
    {
        return MakeError(TEXT("NOT_FOUND"),
            FString::Printf(TEXT("Material not found at '%s' (must be a Material, not an instance)"), *MaterialPath));
    }

    Mat->SetShadingModel(ParseShadingModel(ShadingModel));
    Mat->PostEditChange();
    Mat->MarkPackageDirty();

    TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
    Data->SetStringField(TEXT("material_path"), MaterialPath);
    Data->SetStringField(TEXT("shading_model"), ShadingModel);
    return MakeSuccess(Data);
}

// ─────────────────────────────────────────────────────────────────────────────
// set_blend_mode
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FNexusMaterialHandler::HandleSetBlendMode(
    const TSharedPtr<FJsonObject>& Params)
{
    FString MaterialPath = GetStringParam(Params, TEXT("material_path"));
    FString BlendMode = GetStringParam(Params, TEXT("blend_mode"), TEXT("Opaque"));

    UMaterial* Mat = LoadObject<UMaterial>(nullptr, *MaterialPath);
    if (!Mat)
    {
        return MakeError(TEXT("NOT_FOUND"),
            FString::Printf(TEXT("Material not found at '%s' (must be a Material, not an instance)"), *MaterialPath));
    }

    Mat->BlendMode = ParseBlendMode(BlendMode);
    Mat->PostEditChange();
    Mat->MarkPackageDirty();

    TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
    Data->SetStringField(TEXT("material_path"), MaterialPath);
    Data->SetStringField(TEXT("blend_mode"), BlendMode);
    return MakeSuccess(Data);
}

// ─────────────────────────────────────────────────────────────────────────────
// apply_to_actor
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FNexusMaterialHandler::HandleApplyToActor(
    const TSharedPtr<FJsonObject>& Params)
{
    FString ActorPath = GetStringParam(Params, TEXT("actor_path"));
    FString MaterialPath = GetStringParam(Params, TEXT("material_path"));
    int32 SlotIndex = static_cast<int32>(GetNumberParam(Params, TEXT("slot_index"), 0.0));

    // Resolve actor by path
    UObject* ActorObj = StaticFindObject(AActor::StaticClass(), nullptr, *ActorPath);
    AActor* Actor = Cast<AActor>(ActorObj);
    if (!Actor)
    {
        // Try finding by label as fallback
        UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
        if (World)
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
    }

    if (!Actor)
    {
        return MakeError(TEXT("ACTOR_NOT_FOUND"),
            FString::Printf(TEXT("Actor not found: '%s'"), *ActorPath));
    }

    UMaterialInterface* Material = LoadObject<UMaterialInterface>(nullptr, *MaterialPath);
    if (!Material)
    {
        return MakeError(TEXT("MATERIAL_NOT_FOUND"),
            FString::Printf(TEXT("Material not found at '%s'"), *MaterialPath));
    }

    UMeshComponent* MeshComp = Actor->FindComponentByClass<UMeshComponent>();
    if (!MeshComp)
    {
        return MakeError(TEXT("NO_MESH"),
            FString::Printf(TEXT("Actor '%s' has no mesh component"), *Actor->GetActorLabel()));
    }

    if (SlotIndex < 0 || SlotIndex >= MeshComp->GetNumMaterials())
    {
        return MakeError(TEXT("INVALID_SLOT"),
            FString::Printf(TEXT("Slot index %d out of range (0-%d)"),
                SlotIndex, MeshComp->GetNumMaterials() - 1));
    }

    MeshComp->SetMaterial(SlotIndex, Material);

    TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
    Data->SetStringField(TEXT("actor_path"), Actor->GetPathName());
    Data->SetStringField(TEXT("material_path"), Material->GetPathName());
    Data->SetNumberField(TEXT("slot_index"), SlotIndex);
    return MakeSuccess(Data);
}

// ─────────────────────────────────────────────────────────────────────────────
// set_two_sided
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FNexusMaterialHandler::HandleSetTwoSided(
    const TSharedPtr<FJsonObject>& Params)
{
    FString MaterialPath = GetStringParam(Params, TEXT("material_path"));
    bool bTwoSided = GetBoolParam(Params, TEXT("two_sided"), false);

    UMaterial* Mat = LoadObject<UMaterial>(nullptr, *MaterialPath);
    if (!Mat)
    {
        return MakeError(TEXT("NOT_FOUND"),
            FString::Printf(TEXT("Material not found at '%s' (must be a Material, not an instance)"), *MaterialPath));
    }

    Mat->TwoSided = bTwoSided;
    Mat->PostEditChange();
    Mat->MarkPackageDirty();

    TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
    Data->SetStringField(TEXT("material_path"), MaterialPath);
    Data->SetBoolField(TEXT("two_sided"), bTwoSided);
    return MakeSuccess(Data);
}

// ─────────────────────────────────────────────────────────────────────────────
// get_expressions
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FNexusMaterialHandler::HandleGetExpressions(
    const TSharedPtr<FJsonObject>& Params)
{
    FString MaterialPath = GetStringParam(Params, TEXT("material_path"));

    UMaterial* Mat = LoadObject<UMaterial>(nullptr, *MaterialPath);
    if (!Mat)
    {
        return MakeError(TEXT("NOT_FOUND"),
            FString::Printf(TEXT("Material not found at '%s' (must be a Material, not an instance)"), *MaterialPath));
    }

    TArray<TSharedPtr<FJsonValue>> ExpressionsArray;

    // Guard against engine assertion in GetExpressions() — some materials
    // (e.g., material functions, transient materials) may crash.
    // Use GetExpressionCollection() which is safer in UE 5.7.
    const FMaterialExpressionCollection& ExprCollection = Mat->GetExpressionCollection();
    for (UMaterialExpression* Expr : ExprCollection.Expressions)
    {
        if (!Expr) continue;

        TSharedPtr<FJsonObject> Entry = MakeShareable(new FJsonObject());
        Entry->SetStringField(TEXT("id"), Expr->GetName());
        Entry->SetStringField(TEXT("class"), Expr->GetClass()->GetName());
        Entry->SetStringField(TEXT("name"), Expr->GetEditableName());
        Entry->SetNumberField(TEXT("position_x"), Expr->MaterialExpressionEditorX);
        Entry->SetNumberField(TEXT("position_y"), Expr->MaterialExpressionEditorY);
        ExpressionsArray.Add(MakeShareable(new FJsonValueObject(Entry)));
    }

    TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
    Data->SetStringField(TEXT("material_path"), MaterialPath);
    Data->SetArrayField(TEXT("expressions"), ExpressionsArray);
    Data->SetNumberField(TEXT("expression_count"), ExpressionsArray.Num());
    return MakeSuccess(Data);
}

// ─────────────────────────────────────────────────────────────────────────────
// apply_batch (A3) — apply materials to multiple actors in one call
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FNexusMaterialHandler::HandleApplyBatch(
    const TSharedPtr<FJsonObject>& Params)
{
    const TArray<TSharedPtr<FJsonValue>>* Operations;
    if (!Params->TryGetArrayField(TEXT("operations"), Operations))
    {
        return MakeError(TEXT("MISSING_PARAM"), TEXT("'operations' array is required"));
    }

    UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;

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

        FString ActorPath = GetStringParam(*Op, TEXT("actor_path"));
        FString MaterialPath = GetStringParam(*Op, TEXT("material_path"));
        int32 SlotIndex = static_cast<int32>(GetNumberParam(*Op, TEXT("slot_index"), 0.0));

        // Resolve actor by path, then by label
        UObject* ActorObj = StaticFindObject(AActor::StaticClass(), nullptr, *ActorPath);
        AActor* Actor = Cast<AActor>(ActorObj);
        if (!Actor && World)
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
            TSharedPtr<FJsonObject> Err = MakeShareable(new FJsonObject());
            Err->SetNumberField(TEXT("index"), i);
            Err->SetStringField(TEXT("error"),
                FString::Printf(TEXT("Actor not found: '%s'"), *ActorPath));
            Errors.Add(MakeShareable(new FJsonValueObject(Err)));
            continue;
        }

        UMaterialInterface* Material = LoadObject<UMaterialInterface>(nullptr, *MaterialPath);
        if (!Material)
        {
            TSharedPtr<FJsonObject> Err = MakeShareable(new FJsonObject());
            Err->SetNumberField(TEXT("index"), i);
            Err->SetStringField(TEXT("error"),
                FString::Printf(TEXT("Material not found: '%s'"), *MaterialPath));
            Errors.Add(MakeShareable(new FJsonValueObject(Err)));
            continue;
        }

        UMeshComponent* MeshComp = Actor->FindComponentByClass<UMeshComponent>();
        if (!MeshComp)
        {
            TSharedPtr<FJsonObject> Err = MakeShareable(new FJsonObject());
            Err->SetNumberField(TEXT("index"), i);
            Err->SetStringField(TEXT("error"),
                FString::Printf(TEXT("Actor '%s' has no mesh component"), *Actor->GetActorLabel()));
            Errors.Add(MakeShareable(new FJsonValueObject(Err)));
            continue;
        }

        if (SlotIndex < 0 || SlotIndex >= MeshComp->GetNumMaterials())
        {
            TSharedPtr<FJsonObject> Err = MakeShareable(new FJsonObject());
            Err->SetNumberField(TEXT("index"), i);
            Err->SetStringField(TEXT("error"),
                FString::Printf(TEXT("Slot index %d out of range (0-%d)"),
                    SlotIndex, MeshComp->GetNumMaterials() - 1));
            Errors.Add(MakeShareable(new FJsonValueObject(Err)));
            continue;
        }

        MeshComp->SetMaterial(SlotIndex, Material);
        SuccessCount++;
    }

    TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
    Data->SetNumberField(TEXT("applied_count"), SuccessCount);
    Data->SetNumberField(TEXT("failed_count"), Operations->Num() - SuccessCount);
    Data->SetArrayField(TEXT("errors"), Errors);
    return MakeSuccess(Data);
}

// ─────────────────────────────────────────────────────────────────────────────
// FindExpressionById — locate an expression in a material by its UObject name
// ─────────────────────────────────────────────────────────────────────────────

UMaterialExpression* FNexusMaterialHandler::FindExpressionById(
    UMaterial* Material, const FString& ExpressionId)
{
    if (!Material) return nullptr;

    for (UMaterialExpression* Expr : Material->GetExpressions())
    {
        if (Expr && Expr->GetName() == ExpressionId)
        {
            return Expr;
        }
    }
    return nullptr;
}

// ─────────────────────────────────────────────────────────────────────────────
// add_expression — create a new expression node in the material graph
// Python sends: material_path, expression_type, position_x, position_y
//   + type-specific: constant_value, constant_r/g/b/a, texture_path, param_name
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FNexusMaterialHandler::HandleAddExpression(
    const TSharedPtr<FJsonObject>& Params)
{
    FString MaterialPath = GetStringParam(Params, TEXT("material_path"));
    FString ExpressionType = GetStringParam(Params, TEXT("expression_type"));
    double PosX = GetNumberParam(Params, TEXT("position_x"), 0.0);
    double PosY = GetNumberParam(Params, TEXT("position_y"), 0.0);

    UMaterial* Mat = LoadObject<UMaterial>(nullptr, *MaterialPath);
    if (!Mat)
    {
        return MakeError(TEXT("NOT_FOUND"),
            FString::Printf(TEXT("Material not found at '%s'"), *MaterialPath));
    }

    if (ExpressionType.IsEmpty())
    {
        return MakeError(TEXT("MISSING_PARAM"), TEXT("'expression_type' is required"));
    }

    // Map expression_type string to UClass
    // UE 5.7: Using GetExpressionCollection().AddExpression() for proper registration
    static TMap<FString, UClass*> ExprTypeMap;
    if (ExprTypeMap.Num() == 0)
    {
        ExprTypeMap.Add(TEXT("TextureSample"),           UMaterialExpressionTextureSample::StaticClass());
        ExprTypeMap.Add(TEXT("TextureCoordinate"),       UMaterialExpressionTextureCoordinate::StaticClass());
        ExprTypeMap.Add(TEXT("Constant"),                UMaterialExpressionConstant::StaticClass());
        ExprTypeMap.Add(TEXT("Constant3Vector"),         UMaterialExpressionConstant3Vector::StaticClass());
        ExprTypeMap.Add(TEXT("Constant4Vector"),         UMaterialExpressionConstant4Vector::StaticClass());
        ExprTypeMap.Add(TEXT("Add"),                     UMaterialExpressionAdd::StaticClass());
        ExprTypeMap.Add(TEXT("Multiply"),                UMaterialExpressionMultiply::StaticClass());
        ExprTypeMap.Add(TEXT("Subtract"),                UMaterialExpressionSubtract::StaticClass());
        ExprTypeMap.Add(TEXT("Divide"),                  UMaterialExpressionDivide::StaticClass());
        ExprTypeMap.Add(TEXT("Lerp"),                    UMaterialExpressionLinearInterpolate::StaticClass());
        ExprTypeMap.Add(TEXT("LinearInterpolate"),       UMaterialExpressionLinearInterpolate::StaticClass());
        ExprTypeMap.Add(TEXT("Clamp"),                   UMaterialExpressionClamp::StaticClass());
        ExprTypeMap.Add(TEXT("OneMinus"),                UMaterialExpressionOneMinus::StaticClass());
        ExprTypeMap.Add(TEXT("Power"),                   UMaterialExpressionPower::StaticClass());
        ExprTypeMap.Add(TEXT("Abs"),                     UMaterialExpressionAbs::StaticClass());
        ExprTypeMap.Add(TEXT("ComponentMask"),           UMaterialExpressionComponentMask::StaticClass());
        ExprTypeMap.Add(TEXT("AppendVector"),            UMaterialExpressionAppendVector::StaticClass());
        ExprTypeMap.Add(TEXT("ScalarParameter"),         UMaterialExpressionScalarParameter::StaticClass());
        ExprTypeMap.Add(TEXT("VectorParameter"),         UMaterialExpressionVectorParameter::StaticClass());
        ExprTypeMap.Add(TEXT("TextureObjectParameter"),  UMaterialExpressionTextureObjectParameter::StaticClass());
        ExprTypeMap.Add(TEXT("WorldPosition"),           UMaterialExpressionWorldPosition::StaticClass());
        ExprTypeMap.Add(TEXT("Time"),                    UMaterialExpressionTime::StaticClass());
    }

    UClass** FoundClass = ExprTypeMap.Find(ExpressionType);
    if (!FoundClass)
    {
        return MakeError(TEXT("INVALID_TYPE"),
            FString::Printf(TEXT("Unknown expression type '%s'. Valid types: TextureSample, TextureCoordinate, "
                "Constant, Constant3Vector, Constant4Vector, Add, Multiply, Subtract, Divide, Lerp, "
                "LinearInterpolate, Clamp, OneMinus, Power, Abs, ComponentMask, AppendVector, "
                "ScalarParameter, VectorParameter, TextureObjectParameter, WorldPosition, Time"), *ExpressionType));
    }

    Mat->PreEditChange(nullptr);

    // Create the expression — UE 5.7: NewObject then AddExpression via GetExpressionCollection()
    UMaterialExpression* NewExpr = NewObject<UMaterialExpression>(Mat, *FoundClass);
    if (!NewExpr)
    {
        Mat->PostEditChange();
        return MakeError(TEXT("CREATE_FAILED"), TEXT("Failed to create material expression"));
    }

    Mat->GetExpressionCollection().AddExpression(NewExpr);

    // Set editor position
    NewExpr->MaterialExpressionEditorX = static_cast<int32>(PosX);
    NewExpr->MaterialExpressionEditorY = static_cast<int32>(PosY);

    // Type-specific configuration
    if (UMaterialExpressionConstant* ConstExpr = Cast<UMaterialExpressionConstant>(NewExpr))
    {
        ConstExpr->R = static_cast<float>(GetNumberParam(Params, TEXT("constant_value"), 0.0));
    }
    else if (UMaterialExpressionConstant3Vector* Const3Expr = Cast<UMaterialExpressionConstant3Vector>(NewExpr))
    {
        float R = static_cast<float>(GetNumberParam(Params, TEXT("constant_r"), 0.0));
        float G = static_cast<float>(GetNumberParam(Params, TEXT("constant_g"), 0.0));
        float B = static_cast<float>(GetNumberParam(Params, TEXT("constant_b"), 0.0));
        Const3Expr->Constant = FLinearColor(R, G, B, 1.0f);
    }
    else if (UMaterialExpressionConstant4Vector* Const4Expr = Cast<UMaterialExpressionConstant4Vector>(NewExpr))
    {
        float R = static_cast<float>(GetNumberParam(Params, TEXT("constant_r"), 0.0));
        float G = static_cast<float>(GetNumberParam(Params, TEXT("constant_g"), 0.0));
        float B = static_cast<float>(GetNumberParam(Params, TEXT("constant_b"), 0.0));
        float A = static_cast<float>(GetNumberParam(Params, TEXT("constant_a"), 1.0));
        Const4Expr->Constant = FLinearColor(R, G, B, A);
    }
    else if (UMaterialExpressionTextureSample* TexExpr = Cast<UMaterialExpressionTextureSample>(NewExpr))
    {
        FString TexturePath = GetStringParam(Params, TEXT("texture_path"));
        if (!TexturePath.IsEmpty())
        {
            UTexture* Texture = LoadObject<UTexture>(nullptr, *TexturePath);
            if (Texture)
            {
                TexExpr->Texture = Texture;
            }
        }
    }
    else if (UMaterialExpressionScalarParameter* ScalarParam = Cast<UMaterialExpressionScalarParameter>(NewExpr))
    {
        FString ParamName = GetStringParam(Params, TEXT("param_name"));
        if (!ParamName.IsEmpty())
        {
            ScalarParam->ParameterName = FName(*ParamName);
        }
        ScalarParam->DefaultValue = static_cast<float>(GetNumberParam(Params, TEXT("constant_value"), 0.0));
    }
    else if (UMaterialExpressionVectorParameter* VecParam = Cast<UMaterialExpressionVectorParameter>(NewExpr))
    {
        FString ParamName = GetStringParam(Params, TEXT("param_name"));
        if (!ParamName.IsEmpty())
        {
            VecParam->ParameterName = FName(*ParamName);
        }
        float R = static_cast<float>(GetNumberParam(Params, TEXT("constant_r"), 0.0));
        float G = static_cast<float>(GetNumberParam(Params, TEXT("constant_g"), 0.0));
        float B = static_cast<float>(GetNumberParam(Params, TEXT("constant_b"), 0.0));
        float A = static_cast<float>(GetNumberParam(Params, TEXT("constant_a"), 1.0));
        VecParam->DefaultValue = FLinearColor(R, G, B, A);
    }
    else if (UMaterialExpressionTextureObjectParameter* TexObjParam = Cast<UMaterialExpressionTextureObjectParameter>(NewExpr))
    {
        FString ParamName = GetStringParam(Params, TEXT("param_name"));
        if (!ParamName.IsEmpty())
        {
            TexObjParam->ParameterName = FName(*ParamName);
        }
        FString TexturePath = GetStringParam(Params, TEXT("texture_path"));
        if (!TexturePath.IsEmpty())
        {
            UTexture* Texture = LoadObject<UTexture>(nullptr, *TexturePath);
            if (Texture)
            {
                TexObjParam->Texture = Texture;
            }
        }
    }

    Mat->PostEditChange();
    Mat->MarkPackageDirty();

    // Build response
    TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
    Data->SetStringField(TEXT("material_path"), MaterialPath);
    Data->SetStringField(TEXT("expression_id"), NewExpr->GetName());
    Data->SetStringField(TEXT("expression_class"), NewExpr->GetClass()->GetName());
    Data->SetStringField(TEXT("expression_type"), ExpressionType);
    Data->SetNumberField(TEXT("position_x"), NewExpr->MaterialExpressionEditorX);
    Data->SetNumberField(TEXT("position_y"), NewExpr->MaterialExpressionEditorY);
    return MakeSuccess(Data);
}

// ─────────────────────────────────────────────────────────────────────────────
// get_expression_pins — enumerate inputs and outputs for an expression
// Python sends: material_path, expression_id
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FNexusMaterialHandler::HandleGetExpressionPins(
    const TSharedPtr<FJsonObject>& Params)
{
    FString MaterialPath = GetStringParam(Params, TEXT("material_path"));
    FString ExpressionId = GetStringParam(Params, TEXT("expression_id"));

    UMaterial* Mat = LoadObject<UMaterial>(nullptr, *MaterialPath);
    if (!Mat)
    {
        return MakeError(TEXT("NOT_FOUND"),
            FString::Printf(TEXT("Material not found at '%s'"), *MaterialPath));
    }

    UMaterialExpression* Expr = FindExpressionById(Mat, ExpressionId);
    if (!Expr)
    {
        return MakeError(TEXT("EXPRESSION_NOT_FOUND"),
            FString::Printf(TEXT("Expression '%s' not found in material '%s'"), *ExpressionId, *MaterialPath));
    }

    // Enumerate inputs using FExpressionInputIterator (UE 5.7 pattern)
    TArray<TSharedPtr<FJsonValue>> InputsArray;
    int32 InputIdx = 0;
    for (FExpressionInputIterator It(Expr); It; ++It, ++InputIdx)
    {
        FExpressionInput* Input = It.operator->();
        TSharedPtr<FJsonObject> PinObj = MakeShareable(new FJsonObject());
        PinObj->SetNumberField(TEXT("index"), InputIdx);
        PinObj->SetStringField(TEXT("name"), Expr->GetInputName(InputIdx).ToString());
        PinObj->SetStringField(TEXT("direction"), TEXT("input"));
        PinObj->SetBoolField(TEXT("is_connected"), Input->Expression != nullptr);
        if (Input->Expression)
        {
            PinObj->SetStringField(TEXT("connected_expression_id"), Input->Expression->GetName());
            PinObj->SetNumberField(TEXT("connected_output_index"), Input->OutputIndex);
        }
        InputsArray.Add(MakeShareable(new FJsonValueObject(PinObj)));
    }

    // Enumerate outputs
    TArray<TSharedPtr<FJsonValue>> OutputsArray;
    TArray<FExpressionOutput>& Outputs = Expr->GetOutputs();
    for (int32 OutIdx = 0; OutIdx < Outputs.Num(); ++OutIdx)
    {
        TSharedPtr<FJsonObject> PinObj = MakeShareable(new FJsonObject());
        PinObj->SetNumberField(TEXT("index"), OutIdx);
        PinObj->SetStringField(TEXT("name"), Outputs[OutIdx].OutputName.ToString());
        PinObj->SetStringField(TEXT("direction"), TEXT("output"));
        // Outputs don't track connections directly; connections are tracked by inputs
        PinObj->SetBoolField(TEXT("is_connected"), false);
        OutputsArray.Add(MakeShareable(new FJsonValueObject(PinObj)));
    }

    TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
    Data->SetStringField(TEXT("material_path"), MaterialPath);
    Data->SetStringField(TEXT("expression_id"), Expr->GetName());
    Data->SetStringField(TEXT("expression_class"), Expr->GetClass()->GetName());
    Data->SetArrayField(TEXT("inputs"), InputsArray);
    Data->SetArrayField(TEXT("outputs"), OutputsArray);
    Data->SetNumberField(TEXT("input_count"), InputsArray.Num());
    Data->SetNumberField(TEXT("output_count"), OutputsArray.Num());
    return MakeSuccess(Data);
}

// ─────────────────────────────────────────────────────────────────────────────
// update_expression — update position and/or properties of an expression
// Python sends: material_path, expression_id, position_x, position_y, properties (dict)
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FNexusMaterialHandler::HandleUpdateExpression(
    const TSharedPtr<FJsonObject>& Params)
{
    FString MaterialPath = GetStringParam(Params, TEXT("material_path"));
    FString ExpressionId = GetStringParam(Params, TEXT("expression_id"));

    UMaterial* Mat = LoadObject<UMaterial>(nullptr, *MaterialPath);
    if (!Mat)
    {
        return MakeError(TEXT("NOT_FOUND"),
            FString::Printf(TEXT("Material not found at '%s'"), *MaterialPath));
    }

    UMaterialExpression* Expr = FindExpressionById(Mat, ExpressionId);
    if (!Expr)
    {
        return MakeError(TEXT("EXPRESSION_NOT_FOUND"),
            FString::Printf(TEXT("Expression '%s' not found in material '%s'"), *ExpressionId, *MaterialPath));
    }

    Mat->PreEditChange(nullptr);

    // Update position if provided
    if (Params->HasField(TEXT("position_x")))
    {
        Expr->MaterialExpressionEditorX = static_cast<int32>(GetNumberParam(Params, TEXT("position_x"), 0.0));
    }
    if (Params->HasField(TEXT("position_y")))
    {
        Expr->MaterialExpressionEditorY = static_cast<int32>(GetNumberParam(Params, TEXT("position_y"), 0.0));
    }

    // Update properties via reflection
    TArray<FString> UpdatedProperties;
    const TSharedPtr<FJsonObject>* PropsObj;
    if (Params->TryGetObjectField(TEXT("properties"), PropsObj))
    {
        UClass* ExprClass = Expr->GetClass();
        for (const auto& Pair : (*PropsObj)->Values)
        {
            FName PropName(*Pair.Key);
            FProperty* Prop = ExprClass->FindPropertyByName(PropName);
            if (!Prop)
            {
                continue; // Skip unknown properties
            }

            void* ValuePtr = Prop->ContainerPtrToValuePtr<void>(Expr);

            // Handle common property types
            if (FFloatProperty* FloatProp = CastField<FFloatProperty>(Prop))
            {
                double Val = 0.0;
                if (Pair.Value->TryGetNumber(Val))
                {
                    FloatProp->SetPropertyValue(ValuePtr, static_cast<float>(Val));
                    UpdatedProperties.Add(Pair.Key);
                }
            }
            else if (FDoubleProperty* DoubleProp = CastField<FDoubleProperty>(Prop))
            {
                double Val = 0.0;
                if (Pair.Value->TryGetNumber(Val))
                {
                    DoubleProp->SetPropertyValue(ValuePtr, Val);
                    UpdatedProperties.Add(Pair.Key);
                }
            }
            else if (FIntProperty* IntProp = CastField<FIntProperty>(Prop))
            {
                double Val = 0.0;
                if (Pair.Value->TryGetNumber(Val))
                {
                    IntProp->SetPropertyValue(ValuePtr, static_cast<int32>(Val));
                    UpdatedProperties.Add(Pair.Key);
                }
            }
            else if (FBoolProperty* BoolProp = CastField<FBoolProperty>(Prop))
            {
                bool Val = false;
                if (Pair.Value->TryGetBool(Val))
                {
                    BoolProp->SetPropertyValue(ValuePtr, Val);
                    UpdatedProperties.Add(Pair.Key);
                }
            }
            else if (FStrProperty* StrProp = CastField<FStrProperty>(Prop))
            {
                FString Val;
                if (Pair.Value->TryGetString(Val))
                {
                    StrProp->SetPropertyValue(ValuePtr, Val);
                    UpdatedProperties.Add(Pair.Key);
                }
            }
            else if (FNameProperty* NameProp = CastField<FNameProperty>(Prop))
            {
                FString Val;
                if (Pair.Value->TryGetString(Val))
                {
                    NameProp->SetPropertyValue(ValuePtr, FName(*Val));
                    UpdatedProperties.Add(Pair.Key);
                }
            }
        }
    }

    Mat->PostEditChange();
    Mat->MarkPackageDirty();

    // Build response
    TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
    Data->SetStringField(TEXT("material_path"), MaterialPath);
    Data->SetStringField(TEXT("expression_id"), Expr->GetName());
    Data->SetStringField(TEXT("expression_class"), Expr->GetClass()->GetName());
    Data->SetNumberField(TEXT("position_x"), Expr->MaterialExpressionEditorX);
    Data->SetNumberField(TEXT("position_y"), Expr->MaterialExpressionEditorY);

    TArray<TSharedPtr<FJsonValue>> UpdatedArr;
    for (const FString& PropName : UpdatedProperties)
    {
        UpdatedArr.Add(MakeShareable(new FJsonValueString(PropName)));
    }
    Data->SetArrayField(TEXT("updated_properties"), UpdatedArr);
    return MakeSuccess(Data);
}

// ─────────────────────────────────────────────────────────────────────────────
// remove_expression — delete an expression from the material graph
// Python sends: material_path, expression_id
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FNexusMaterialHandler::HandleRemoveExpression(
    const TSharedPtr<FJsonObject>& Params)
{
    FString MaterialPath = GetStringParam(Params, TEXT("material_path"));
    FString ExpressionId = GetStringParam(Params, TEXT("expression_id"));

    UMaterial* Mat = LoadObject<UMaterial>(nullptr, *MaterialPath);
    if (!Mat)
    {
        return MakeError(TEXT("NOT_FOUND"),
            FString::Printf(TEXT("Material not found at '%s'"), *MaterialPath));
    }

    UMaterialExpression* Expr = FindExpressionById(Mat, ExpressionId);
    if (!Expr)
    {
        return MakeError(TEXT("EXPRESSION_NOT_FOUND"),
            FString::Printf(TEXT("Expression '%s' not found in material '%s'"), *ExpressionId, *MaterialPath));
    }

    FString RemovedClass = Expr->GetClass()->GetName();

    Mat->PreEditChange(nullptr);

    // Disconnect any inputs on other expressions that reference this expression
    for (UMaterialExpression* OtherExpr : Mat->GetExpressions())
    {
        if (!OtherExpr || OtherExpr == Expr) continue;

        for (FExpressionInputIterator It(OtherExpr); It; ++It)
        {
            FExpressionInput* Input = It.operator->();
            if (Input->Expression == Expr)
            {
                Input->Expression = nullptr;
                Input->OutputIndex = 0;
            }
        }
    }

    // Also disconnect material-level inputs (BaseColor, Metallic, etc.)
    // UE 5.7: Material inputs are on GetEditorOnlyData()
    if (UMaterialEditorOnlyData* EditorData = Mat->GetEditorOnlyData())
    {
        // Check all material input connections
        auto DisconnectIfMatches = [Expr](FExpressionInput& Input)
        {
            if (Input.Expression == Expr)
            {
                Input.Expression = nullptr;
                Input.OutputIndex = 0;
            }
        };

        DisconnectIfMatches(EditorData->BaseColor);
        DisconnectIfMatches(EditorData->Metallic);
        DisconnectIfMatches(EditorData->Specular);
        DisconnectIfMatches(EditorData->Roughness);
        DisconnectIfMatches(EditorData->Anisotropy);
        DisconnectIfMatches(EditorData->Normal);
        DisconnectIfMatches(EditorData->Tangent);
        DisconnectIfMatches(EditorData->EmissiveColor);
        DisconnectIfMatches(EditorData->Opacity);
        DisconnectIfMatches(EditorData->OpacityMask);
        DisconnectIfMatches(EditorData->WorldPositionOffset);
        DisconnectIfMatches(EditorData->Displacement);
        DisconnectIfMatches(EditorData->SubsurfaceColor);
        DisconnectIfMatches(EditorData->ClearCoat);
        DisconnectIfMatches(EditorData->ClearCoatRoughness);
        DisconnectIfMatches(EditorData->AmbientOcclusion);
        DisconnectIfMatches(EditorData->Refraction);
        DisconnectIfMatches(EditorData->PixelDepthOffset);
    }

    // UE 5.7: Remove via GetExpressionCollection()
    Mat->GetExpressionCollection().RemoveExpression(Expr);

    Mat->PostEditChange();
    Mat->MarkPackageDirty();

    TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
    Data->SetStringField(TEXT("material_path"), MaterialPath);
    Data->SetStringField(TEXT("removed_expression_id"), ExpressionId);
    Data->SetStringField(TEXT("removed_expression_class"), RemovedClass);
    Data->SetBoolField(TEXT("success"), true);
    return MakeSuccess(Data);
}

// ─────────────────────────────────────────────────────────────────────────────
// connect_expressions — wire a source expression output to a target input
// Python sends: material_path, source_expression_id, source_output_index,
//               target_expression_id ("material" = main material inputs),
//               target_input_name (pin name or material input name)
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FNexusMaterialHandler::HandleConnectExpressions(
    const TSharedPtr<FJsonObject>& Params)
{
    FString MaterialPath = GetStringParam(Params, TEXT("material_path"));
    FString SourceId = GetStringParam(Params, TEXT("source_expression_id"));
    int32 SourceOutputIndex = static_cast<int32>(GetNumberParam(Params, TEXT("source_output_index"), 0.0));
    FString TargetId = GetStringParam(Params, TEXT("target_expression_id"));
    FString TargetInputName = GetStringParam(Params, TEXT("target_input_name"));

    UMaterial* Mat = LoadObject<UMaterial>(nullptr, *MaterialPath);
    if (!Mat)
    {
        return MakeError(TEXT("NOT_FOUND"),
            FString::Printf(TEXT("Material not found at '%s'"), *MaterialPath));
    }

    // Resolve source expression
    UMaterialExpression* SourceExpr = FindExpressionById(Mat, SourceId);
    if (!SourceExpr)
    {
        return MakeError(TEXT("EXPRESSION_NOT_FOUND"),
            FString::Printf(TEXT("Source expression '%s' not found"), *SourceId));
    }

    // Validate source output index
    TArray<FExpressionOutput>& SourceOutputs = SourceExpr->GetOutputs();
    if (SourceOutputIndex < 0 || SourceOutputIndex >= SourceOutputs.Num())
    {
        return MakeError(TEXT("INVALID_OUTPUT"),
            FString::Printf(TEXT("Source output index %d out of range (0-%d)"),
                SourceOutputIndex, FMath::Max(0, SourceOutputs.Num() - 1)));
    }

    Mat->PreEditChange(nullptr);

    // Determine if connecting to a material-level input or to another expression
    if (TargetId.Equals(TEXT("material"), ESearchCase::IgnoreCase))
    {
        // Connect to material main input (BaseColor, Metallic, Roughness, etc.)
        // UE 5.7: Material inputs are on GetEditorOnlyData()
        UMaterialEditorOnlyData* EditorData = Mat->GetEditorOnlyData();
        if (!EditorData)
        {
            Mat->PostEditChange();
            return MakeError(TEXT("NO_EDITOR_DATA"), TEXT("Material has no editor-only data"));
        }

        FExpressionInput* TargetInput = nullptr;
        FString ResolvedInputName = TargetInputName;

        // Map input name to the correct FExpressionInput field
        if (TargetInputName.Equals(TEXT("BaseColor"), ESearchCase::IgnoreCase))
            TargetInput = &EditorData->BaseColor;
        else if (TargetInputName.Equals(TEXT("Metallic"), ESearchCase::IgnoreCase))
            TargetInput = &EditorData->Metallic;
        else if (TargetInputName.Equals(TEXT("Specular"), ESearchCase::IgnoreCase))
            TargetInput = &EditorData->Specular;
        else if (TargetInputName.Equals(TEXT("Roughness"), ESearchCase::IgnoreCase))
            TargetInput = &EditorData->Roughness;
        else if (TargetInputName.Equals(TEXT("Anisotropy"), ESearchCase::IgnoreCase))
            TargetInput = &EditorData->Anisotropy;
        else if (TargetInputName.Equals(TEXT("Normal"), ESearchCase::IgnoreCase))
            TargetInput = &EditorData->Normal;
        else if (TargetInputName.Equals(TEXT("Tangent"), ESearchCase::IgnoreCase))
            TargetInput = &EditorData->Tangent;
        else if (TargetInputName.Equals(TEXT("EmissiveColor"), ESearchCase::IgnoreCase))
            TargetInput = &EditorData->EmissiveColor;
        else if (TargetInputName.Equals(TEXT("Opacity"), ESearchCase::IgnoreCase))
            TargetInput = &EditorData->Opacity;
        else if (TargetInputName.Equals(TEXT("OpacityMask"), ESearchCase::IgnoreCase))
            TargetInput = &EditorData->OpacityMask;
        else if (TargetInputName.Equals(TEXT("WorldPositionOffset"), ESearchCase::IgnoreCase))
            TargetInput = &EditorData->WorldPositionOffset;
        else if (TargetInputName.Equals(TEXT("Displacement"), ESearchCase::IgnoreCase))
            TargetInput = &EditorData->Displacement;
        else if (TargetInputName.Equals(TEXT("SubsurfaceColor"), ESearchCase::IgnoreCase))
            TargetInput = &EditorData->SubsurfaceColor;
        else if (TargetInputName.Equals(TEXT("ClearCoat"), ESearchCase::IgnoreCase))
            TargetInput = &EditorData->ClearCoat;
        else if (TargetInputName.Equals(TEXT("ClearCoatRoughness"), ESearchCase::IgnoreCase))
            TargetInput = &EditorData->ClearCoatRoughness;
        else if (TargetInputName.Equals(TEXT("AmbientOcclusion"), ESearchCase::IgnoreCase))
            TargetInput = &EditorData->AmbientOcclusion;
        else if (TargetInputName.Equals(TEXT("Refraction"), ESearchCase::IgnoreCase))
            TargetInput = &EditorData->Refraction;
        else if (TargetInputName.Equals(TEXT("PixelDepthOffset"), ESearchCase::IgnoreCase))
            TargetInput = &EditorData->PixelDepthOffset;

        if (!TargetInput)
        {
            Mat->PostEditChange();
            return MakeError(TEXT("INVALID_INPUT"),
                FString::Printf(TEXT("Unknown material input '%s'. Valid inputs: BaseColor, Metallic, "
                    "Specular, Roughness, Anisotropy, Normal, Tangent, EmissiveColor, Opacity, "
                    "OpacityMask, WorldPositionOffset, Displacement, SubsurfaceColor, ClearCoat, "
                    "ClearCoatRoughness, AmbientOcclusion, Refraction, PixelDepthOffset"), *TargetInputName));
        }

        // UE 5.7: Connect the material input to the source expression
        TargetInput->Connect(SourceOutputIndex, SourceExpr);

        Mat->PostEditChange();
        Mat->MarkPackageDirty();

        TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
        Data->SetStringField(TEXT("material_path"), MaterialPath);
        Data->SetStringField(TEXT("source_expression_id"), SourceExpr->GetName());
        Data->SetNumberField(TEXT("source_output_index"), SourceOutputIndex);
        Data->SetStringField(TEXT("target"), TEXT("material"));
        Data->SetStringField(TEXT("target_input_name"), ResolvedInputName);
        Data->SetBoolField(TEXT("connected"), true);
        return MakeSuccess(Data);
    }
    else
    {
        // Connect expression-to-expression
        UMaterialExpression* TargetExpr = FindExpressionById(Mat, TargetId);
        if (!TargetExpr)
        {
            Mat->PostEditChange();
            return MakeError(TEXT("EXPRESSION_NOT_FOUND"),
                FString::Printf(TEXT("Target expression '%s' not found"), *TargetId));
        }

        // Find the target input by name or index
        FExpressionInput* TargetInput = nullptr;
        int32 FoundInputIndex = -1;

        int32 InputIdx = 0;
        for (FExpressionInputIterator It(TargetExpr); It; ++It, ++InputIdx)
        {
            FString InputName = TargetExpr->GetInputName(InputIdx).ToString();
            if (InputName.Equals(TargetInputName, ESearchCase::IgnoreCase)
                || FString::FromInt(InputIdx) == TargetInputName)
            {
                TargetInput = It.operator->();
                FoundInputIndex = InputIdx;
                break;
            }
        }

        if (!TargetInput)
        {
            Mat->PostEditChange();
            return MakeError(TEXT("INVALID_INPUT"),
                FString::Printf(TEXT("Input '%s' not found on expression '%s'"),
                    *TargetInputName, *TargetId));
        }

        // UE 5.7: Connect the expression input to the source expression output
        TargetInput->Connect(SourceOutputIndex, SourceExpr);

        Mat->PostEditChange();
        Mat->MarkPackageDirty();

        TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
        Data->SetStringField(TEXT("material_path"), MaterialPath);
        Data->SetStringField(TEXT("source_expression_id"), SourceExpr->GetName());
        Data->SetNumberField(TEXT("source_output_index"), SourceOutputIndex);
        Data->SetStringField(TEXT("target_expression_id"), TargetExpr->GetName());
        Data->SetStringField(TEXT("target_input_name"), TargetInputName);
        Data->SetNumberField(TEXT("target_input_index"), FoundInputIndex);
        Data->SetBoolField(TEXT("connected"), true);
        return MakeSuccess(Data);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// disconnect_expression — break a connection on a target input
// Python sends: material_path, target_expression_id ("material" = main inputs),
//               target_input_name
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FNexusMaterialHandler::HandleDisconnectExpression(
    const TSharedPtr<FJsonObject>& Params)
{
    FString MaterialPath = GetStringParam(Params, TEXT("material_path"));
    FString TargetId = GetStringParam(Params, TEXT("target_expression_id"));
    FString TargetInputName = GetStringParam(Params, TEXT("target_input_name"));

    UMaterial* Mat = LoadObject<UMaterial>(nullptr, *MaterialPath);
    if (!Mat)
    {
        return MakeError(TEXT("NOT_FOUND"),
            FString::Printf(TEXT("Material not found at '%s'"), *MaterialPath));
    }

    Mat->PreEditChange(nullptr);

    FString DisconnectedFrom;
    int32 DisconnectedOutputIndex = 0;

    if (TargetId.Equals(TEXT("material"), ESearchCase::IgnoreCase))
    {
        // Disconnect a material-level input
        UMaterialEditorOnlyData* EditorData = Mat->GetEditorOnlyData();
        if (!EditorData)
        {
            Mat->PostEditChange();
            return MakeError(TEXT("NO_EDITOR_DATA"), TEXT("Material has no editor-only data"));
        }

        FExpressionInput* TargetInput = nullptr;

        if (TargetInputName.Equals(TEXT("BaseColor"), ESearchCase::IgnoreCase))
            TargetInput = &EditorData->BaseColor;
        else if (TargetInputName.Equals(TEXT("Metallic"), ESearchCase::IgnoreCase))
            TargetInput = &EditorData->Metallic;
        else if (TargetInputName.Equals(TEXT("Specular"), ESearchCase::IgnoreCase))
            TargetInput = &EditorData->Specular;
        else if (TargetInputName.Equals(TEXT("Roughness"), ESearchCase::IgnoreCase))
            TargetInput = &EditorData->Roughness;
        else if (TargetInputName.Equals(TEXT("Anisotropy"), ESearchCase::IgnoreCase))
            TargetInput = &EditorData->Anisotropy;
        else if (TargetInputName.Equals(TEXT("Normal"), ESearchCase::IgnoreCase))
            TargetInput = &EditorData->Normal;
        else if (TargetInputName.Equals(TEXT("Tangent"), ESearchCase::IgnoreCase))
            TargetInput = &EditorData->Tangent;
        else if (TargetInputName.Equals(TEXT("EmissiveColor"), ESearchCase::IgnoreCase))
            TargetInput = &EditorData->EmissiveColor;
        else if (TargetInputName.Equals(TEXT("Opacity"), ESearchCase::IgnoreCase))
            TargetInput = &EditorData->Opacity;
        else if (TargetInputName.Equals(TEXT("OpacityMask"), ESearchCase::IgnoreCase))
            TargetInput = &EditorData->OpacityMask;
        else if (TargetInputName.Equals(TEXT("WorldPositionOffset"), ESearchCase::IgnoreCase))
            TargetInput = &EditorData->WorldPositionOffset;
        else if (TargetInputName.Equals(TEXT("Displacement"), ESearchCase::IgnoreCase))
            TargetInput = &EditorData->Displacement;
        else if (TargetInputName.Equals(TEXT("SubsurfaceColor"), ESearchCase::IgnoreCase))
            TargetInput = &EditorData->SubsurfaceColor;
        else if (TargetInputName.Equals(TEXT("ClearCoat"), ESearchCase::IgnoreCase))
            TargetInput = &EditorData->ClearCoat;
        else if (TargetInputName.Equals(TEXT("ClearCoatRoughness"), ESearchCase::IgnoreCase))
            TargetInput = &EditorData->ClearCoatRoughness;
        else if (TargetInputName.Equals(TEXT("AmbientOcclusion"), ESearchCase::IgnoreCase))
            TargetInput = &EditorData->AmbientOcclusion;
        else if (TargetInputName.Equals(TEXT("Refraction"), ESearchCase::IgnoreCase))
            TargetInput = &EditorData->Refraction;
        else if (TargetInputName.Equals(TEXT("PixelDepthOffset"), ESearchCase::IgnoreCase))
            TargetInput = &EditorData->PixelDepthOffset;

        if (!TargetInput)
        {
            Mat->PostEditChange();
            return MakeError(TEXT("INVALID_INPUT"),
                FString::Printf(TEXT("Unknown material input '%s'"), *TargetInputName));
        }

        // Record what was connected before clearing
        if (TargetInput->Expression)
        {
            DisconnectedFrom = TargetInput->Expression->GetName();
            DisconnectedOutputIndex = TargetInput->OutputIndex;
        }

        TargetInput->Expression = nullptr;
        TargetInput->OutputIndex = 0;
    }
    else
    {
        // Disconnect an expression-level input
        UMaterialExpression* TargetExpr = FindExpressionById(Mat, TargetId);
        if (!TargetExpr)
        {
            Mat->PostEditChange();
            return MakeError(TEXT("EXPRESSION_NOT_FOUND"),
                FString::Printf(TEXT("Target expression '%s' not found"), *TargetId));
        }

        FExpressionInput* TargetInput = nullptr;
        int32 InputIdx = 0;
        for (FExpressionInputIterator It(TargetExpr); It; ++It, ++InputIdx)
        {
            FString InputName = TargetExpr->GetInputName(InputIdx).ToString();
            if (InputName.Equals(TargetInputName, ESearchCase::IgnoreCase)
                || FString::FromInt(InputIdx) == TargetInputName)
            {
                TargetInput = It.operator->();
                break;
            }
        }

        if (!TargetInput)
        {
            Mat->PostEditChange();
            return MakeError(TEXT("INVALID_INPUT"),
                FString::Printf(TEXT("Input '%s' not found on expression '%s'"),
                    *TargetInputName, *TargetId));
        }

        if (TargetInput->Expression)
        {
            DisconnectedFrom = TargetInput->Expression->GetName();
            DisconnectedOutputIndex = TargetInput->OutputIndex;
        }

        TargetInput->Expression = nullptr;
        TargetInput->OutputIndex = 0;
    }

    Mat->PostEditChange();
    Mat->MarkPackageDirty();

    TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
    Data->SetStringField(TEXT("material_path"), MaterialPath);
    Data->SetStringField(TEXT("target_expression_id"), TargetId);
    Data->SetStringField(TEXT("target_input_name"), TargetInputName);
    Data->SetBoolField(TEXT("disconnected"), true);
    if (!DisconnectedFrom.IsEmpty())
    {
        Data->SetStringField(TEXT("previously_connected_expression"), DisconnectedFrom);
        Data->SetNumberField(TEXT("previously_connected_output_index"), DisconnectedOutputIndex);
    }
    return MakeSuccess(Data);
}
