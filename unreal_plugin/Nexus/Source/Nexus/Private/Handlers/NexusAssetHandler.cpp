// Copyright Nexus Team. All Rights Reserved.

#include "Handlers/NexusAssetHandler.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "EditorAssetLibrary.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "AutomatedAssetImportData.h"
#include "FileHelpers.h"
#include "Editor.h"
#include "Subsystems/EditorAssetSubsystem.h"

TSharedPtr<FJsonObject> FNexusAssetHandler::Handle(
    const FString& CommandType,
    const TSharedPtr<FJsonObject>& Params)
{
    FString SubCommand = CommandType.RightChop(GetNamespace().Len() + 1);

    if (SubCommand == TEXT("search")) return HandleSearch(Params);
    if (SubCommand == TEXT("get_info")) return HandleGetInfo(Params);
    if (SubCommand == TEXT("exists")) return HandleExists(Params);
    if (SubCommand == TEXT("import")) return HandleImport(Params);
    if (SubCommand == TEXT("delete")) return HandleDelete(Params);
    if (SubCommand == TEXT("rename")) return HandleRename(Params);
    if (SubCommand == TEXT("duplicate")) return HandleDuplicate(Params);
    if (SubCommand == TEXT("save")) return HandleSave(Params);
    if (SubCommand == TEXT("save_all")) return HandleSaveAll(Params);
    if (SubCommand == TEXT("get_references")) return HandleGetReferences(Params);
    if (SubCommand == TEXT("get_dependents")) return HandleGetDependents(Params);
    if (SubCommand == TEXT("create_folder")) return HandleCreateFolder(Params);
    if (SubCommand == TEXT("list_folder")) return HandleListFolder(Params);
    if (SubCommand == TEXT("set_metadata")) return HandleSetMetadata(Params);
    if (SubCommand == TEXT("validate")) return HandleValidate(Params);
    // Batch operations (A3)
    if (SubCommand == TEXT("import_batch")) return HandleImportBatch(Params);

    return MakeError(TEXT("NOT_IMPLEMENTED"),
        FString::Printf(TEXT("Command '%s' not yet implemented"), *CommandType));
}

// ---------------------------------------------------------------------------
// search
// ---------------------------------------------------------------------------

TSharedPtr<FJsonObject> FNexusAssetHandler::HandleSearch(const TSharedPtr<FJsonObject>& Params)
{
    FString Query = GetStringParam(Params, TEXT("query"));
    FString AssetClass = GetStringParam(Params, TEXT("asset_class"));
    FString Folder = GetStringParam(Params, TEXT("folder"), TEXT("/Game"));
    bool bRecursive = GetBoolParam(Params, TEXT("recursive"), true);
    int32 Limit = static_cast<int32>(GetNumberParam(Params, TEXT("limit"), 100.0));

    IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();

    FARFilter Filter;
    Filter.PackagePaths.Add(FName(*Folder));
    Filter.bRecursivePaths = bRecursive;
    if (!AssetClass.IsEmpty())
    {
        Filter.ClassPaths.Add(FTopLevelAssetPath(FName(TEXT("/Script/Engine")), FName(*AssetClass)));
    }

    TArray<FAssetData> AllResults;
    Registry.GetAssets(Filter, AllResults);

    // Filter by query string (wildcard match on name)
    TArray<FAssetData> Filtered;
    for (const FAssetData& Asset : AllResults)
    {
        if (Query.IsEmpty() || Asset.AssetName.ToString().Contains(Query, ESearchCase::IgnoreCase)
            || Asset.PackageName.ToString().Contains(Query, ESearchCase::IgnoreCase))
        {
            Filtered.Add(Asset);
            if (Filtered.Num() >= Limit)
            {
                break;
            }
        }
    }

    TArray<TSharedPtr<FJsonValue>> AssetsArray;
    for (const FAssetData& Asset : Filtered)
    {
        TSharedPtr<FJsonObject> AssetObj = MakeShareable(new FJsonObject());
        AssetObj->SetStringField(TEXT("path"), Asset.GetSoftObjectPath().ToString());
        AssetObj->SetStringField(TEXT("name"), Asset.AssetName.ToString());
        AssetObj->SetStringField(TEXT("asset_class"), Asset.AssetClassPath.GetAssetName().ToString());
        AssetObj->SetStringField(TEXT("package_path"), Asset.PackageName.ToString());
        AssetsArray.Add(MakeShareable(new FJsonValueObject(AssetObj)));
    }

    TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
    Data->SetArrayField(TEXT("assets"), AssetsArray);
    Data->SetNumberField(TEXT("total_count"), Filtered.Num());
    Data->SetBoolField(TEXT("truncated"), AllResults.Num() > Limit);
    return MakeSuccess(Data);
}

// ---------------------------------------------------------------------------
// get_info
// ---------------------------------------------------------------------------

TSharedPtr<FJsonObject> FNexusAssetHandler::HandleGetInfo(const TSharedPtr<FJsonObject>& Params)
{
    FString Path = GetStringParam(Params, TEXT("asset_path"));
    if (Path.IsEmpty())
    {
        Path = GetStringParam(Params, TEXT("path"));
    }

    IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
    FAssetData AssetData = Registry.GetAssetByObjectPath(FSoftObjectPath(Path));

    if (!AssetData.IsValid())
    {
        return MakeError(TEXT("ASSET_NOT_FOUND"), FString::Printf(TEXT("Asset not found: '%s'"), *Path));
    }

    TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
    Data->SetStringField(TEXT("path"), AssetData.GetSoftObjectPath().ToString());
    Data->SetStringField(TEXT("name"), AssetData.AssetName.ToString());
    Data->SetStringField(TEXT("asset_class"), AssetData.AssetClassPath.GetAssetName().ToString());
    Data->SetStringField(TEXT("package_path"), AssetData.PackageName.ToString());
    Data->SetBoolField(TEXT("is_loaded"), AssetData.IsAssetLoaded());
    return MakeSuccess(Data);
}

// ---------------------------------------------------------------------------
// exists
// ---------------------------------------------------------------------------

TSharedPtr<FJsonObject> FNexusAssetHandler::HandleExists(const TSharedPtr<FJsonObject>& Params)
{
    FString Path = GetStringParam(Params, TEXT("asset_path"));
    if (Path.IsEmpty())
    {
        Path = GetStringParam(Params, TEXT("path"));
    }

    bool bExists = UEditorAssetLibrary::DoesAssetExist(Path);
    TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
    Data->SetStringField(TEXT("path"), Path);
    Data->SetBoolField(TEXT("exists"), bExists);
    return MakeSuccess(Data);
}

// ---------------------------------------------------------------------------
// import
// ---------------------------------------------------------------------------

TSharedPtr<FJsonObject> FNexusAssetHandler::HandleImport(const TSharedPtr<FJsonObject>& Params)
{
    FString SourcePath = GetStringParam(Params, TEXT("source_path"));
    FString DestinationPath = GetStringParam(Params, TEXT("destination_path"));
    FString AssetName = GetStringParam(Params, TEXT("asset_name"));

    if (SourcePath.IsEmpty())
    {
        return MakeError(TEXT("MISSING_PARAM"), TEXT("source_path is required"));
    }
    if (DestinationPath.IsEmpty())
    {
        return MakeError(TEXT("MISSING_PARAM"), TEXT("destination_path is required"));
    }

    // Verify source file exists
    if (!FPaths::FileExists(SourcePath))
    {
        return MakeError(TEXT("FILE_NOT_FOUND"), FString::Printf(TEXT("Source file not found: '%s'"), *SourcePath));
    }

    IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get();

    UAutomatedAssetImportData* ImportData = NewObject<UAutomatedAssetImportData>();
    ImportData->Filenames.Add(SourcePath);
    ImportData->DestinationPath = DestinationPath;
    ImportData->bReplaceExisting = true;

    TArray<UObject*> Imported = AssetTools.ImportAssetsAutomated(ImportData);

    if (Imported.Num() == 0)
    {
        return MakeError(TEXT("IMPORT_FAILED"), FString::Printf(TEXT("Failed to import '%s'"), *SourcePath));
    }

    UObject* ImportedAsset = Imported[0];
    TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
    Data->SetStringField(TEXT("asset_path"), ImportedAsset->GetPathName());
    Data->SetStringField(TEXT("asset_class"), ImportedAsset->GetClass()->GetName());
    Data->SetBoolField(TEXT("success"), true);

    TArray<TSharedPtr<FJsonValue>> Warnings;
    Data->SetArrayField(TEXT("warnings"), Warnings);
    return MakeSuccess(Data);
}

// ---------------------------------------------------------------------------
// delete
// ---------------------------------------------------------------------------

TSharedPtr<FJsonObject> FNexusAssetHandler::HandleDelete(const TSharedPtr<FJsonObject>& Params)
{
    FString Path = GetStringParam(Params, TEXT("asset_path"));
    if (Path.IsEmpty())
    {
        Path = GetStringParam(Params, TEXT("path"));
    }

    bool bDeleted = UEditorAssetLibrary::DeleteAsset(Path);
    TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
    Data->SetStringField(TEXT("path"), Path);
    Data->SetBoolField(TEXT("deleted"), bDeleted);
    return MakeSuccess(Data);
}

// ---------------------------------------------------------------------------
// rename
// ---------------------------------------------------------------------------

TSharedPtr<FJsonObject> FNexusAssetHandler::HandleRename(const TSharedPtr<FJsonObject>& Params)
{
    FString AssetPath = GetStringParam(Params, TEXT("asset_path"));
    FString NewName = GetStringParam(Params, TEXT("new_name"));

    if (AssetPath.IsEmpty())
    {
        return MakeError(TEXT("MISSING_PARAM"), TEXT("asset_path is required"));
    }
    if (NewName.IsEmpty())
    {
        return MakeError(TEXT("MISSING_PARAM"), TEXT("new_name is required"));
    }

    if (!UEditorAssetLibrary::DoesAssetExist(AssetPath))
    {
        return MakeError(TEXT("ASSET_NOT_FOUND"), FString::Printf(TEXT("Asset not found: '%s'"), *AssetPath));
    }

    // Build new path: same directory, new name
    FString PackagePath = FPackageName::GetLongPackagePath(AssetPath);
    FString NewPath = PackagePath / NewName;

    bool bRenamed = UEditorAssetLibrary::RenameAsset(AssetPath, NewPath);
    if (!bRenamed)
    {
        return MakeError(TEXT("RENAME_FAILED"), FString::Printf(TEXT("Failed to rename '%s' to '%s'"), *AssetPath, *NewPath));
    }

    TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
    Data->SetStringField(TEXT("old_path"), AssetPath);
    Data->SetStringField(TEXT("new_path"), NewPath);
    Data->SetStringField(TEXT("name"), NewName);
    Data->SetBoolField(TEXT("success"), true);
    return MakeSuccess(Data);
}

// ---------------------------------------------------------------------------
// duplicate
// ---------------------------------------------------------------------------

TSharedPtr<FJsonObject> FNexusAssetHandler::HandleDuplicate(const TSharedPtr<FJsonObject>& Params)
{
    FString AssetPath = GetStringParam(Params, TEXT("asset_path"));
    FString DestinationPath = GetStringParam(Params, TEXT("destination_path"));
    FString NewName = GetStringParam(Params, TEXT("new_name"));

    if (AssetPath.IsEmpty())
    {
        return MakeError(TEXT("MISSING_PARAM"), TEXT("asset_path is required"));
    }
    if (DestinationPath.IsEmpty())
    {
        return MakeError(TEXT("MISSING_PARAM"), TEXT("destination_path is required"));
    }

    if (!UEditorAssetLibrary::DoesAssetExist(AssetPath))
    {
        return MakeError(TEXT("ASSET_NOT_FOUND"), FString::Printf(TEXT("Asset not found: '%s'"), *AssetPath));
    }

    // If no new name provided, derive from original + _Copy
    if (NewName.IsEmpty())
    {
        NewName = FPackageName::GetShortName(AssetPath) + TEXT("_Copy");
    }

    FString FullDestPath = DestinationPath / NewName;
    UObject* Duplicated = UEditorAssetLibrary::DuplicateAsset(AssetPath, FullDestPath);

    if (!Duplicated)
    {
        return MakeError(TEXT("DUPLICATE_FAILED"), FString::Printf(TEXT("Failed to duplicate '%s' to '%s'"), *AssetPath, *FullDestPath));
    }

    TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
    Data->SetStringField(TEXT("source_path"), AssetPath);
    Data->SetStringField(TEXT("new_path"), Duplicated->GetPathName());
    Data->SetStringField(TEXT("name"), NewName);
    Data->SetBoolField(TEXT("success"), true);
    return MakeSuccess(Data);
}

// ---------------------------------------------------------------------------
// save
// ---------------------------------------------------------------------------

TSharedPtr<FJsonObject> FNexusAssetHandler::HandleSave(const TSharedPtr<FJsonObject>& Params)
{
    FString Path = GetStringParam(Params, TEXT("asset_path"));
    if (Path.IsEmpty())
    {
        Path = GetStringParam(Params, TEXT("path"));
    }

    bool bSaved = UEditorAssetLibrary::SaveAsset(Path);
    TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
    Data->SetStringField(TEXT("path"), Path);
    Data->SetBoolField(TEXT("saved"), bSaved);
    return MakeSuccess(Data);
}

// ---------------------------------------------------------------------------
// save_all
// ---------------------------------------------------------------------------

TSharedPtr<FJsonObject> FNexusAssetHandler::HandleSaveAll(const TSharedPtr<FJsonObject>& Params)
{
    bool bSaved = FEditorFileUtils::SaveDirtyPackages(
        /*bPromptUserToSave=*/ false,
        /*bSaveMapPackages=*/ true,
        /*bSaveContentPackages=*/ true);

    TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
    Data->SetBoolField(TEXT("success"), bSaved);
    return MakeSuccess(Data);
}

// ---------------------------------------------------------------------------
// get_references
// ---------------------------------------------------------------------------

TSharedPtr<FJsonObject> FNexusAssetHandler::HandleGetReferences(const TSharedPtr<FJsonObject>& Params)
{
    FString AssetPath = GetStringParam(Params, TEXT("asset_path"));
    if (AssetPath.IsEmpty())
    {
        return MakeError(TEXT("MISSING_PARAM"), TEXT("asset_path is required"));
    }

    IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();

    TArray<FAssetIdentifier> References;
    Registry.GetReferencers(FAssetIdentifier(FName(*AssetPath)), References);

    TArray<TSharedPtr<FJsonValue>> RefsArray;
    for (const FAssetIdentifier& Ref : References)
    {
        RefsArray.Add(MakeShareable(new FJsonValueString(Ref.PackageName.ToString())));
    }

    TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
    Data->SetStringField(TEXT("asset_path"), AssetPath);
    Data->SetArrayField(TEXT("references"), RefsArray);
    Data->SetNumberField(TEXT("count"), RefsArray.Num());
    return MakeSuccess(Data);
}

// ---------------------------------------------------------------------------
// get_dependents
// ---------------------------------------------------------------------------

TSharedPtr<FJsonObject> FNexusAssetHandler::HandleGetDependents(const TSharedPtr<FJsonObject>& Params)
{
    FString AssetPath = GetStringParam(Params, TEXT("asset_path"));
    if (AssetPath.IsEmpty())
    {
        return MakeError(TEXT("MISSING_PARAM"), TEXT("asset_path is required"));
    }

    IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();

    TArray<FAssetIdentifier> Dependencies;
    Registry.GetDependencies(FAssetIdentifier(FName(*AssetPath)), Dependencies);

    TArray<TSharedPtr<FJsonValue>> DepsArray;
    for (const FAssetIdentifier& Dep : Dependencies)
    {
        DepsArray.Add(MakeShareable(new FJsonValueString(Dep.PackageName.ToString())));
    }

    TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
    Data->SetStringField(TEXT("asset_path"), AssetPath);
    Data->SetArrayField(TEXT("dependents"), DepsArray);
    Data->SetNumberField(TEXT("count"), DepsArray.Num());
    return MakeSuccess(Data);
}

// ---------------------------------------------------------------------------
// create_folder
// ---------------------------------------------------------------------------

TSharedPtr<FJsonObject> FNexusAssetHandler::HandleCreateFolder(const TSharedPtr<FJsonObject>& Params)
{
    FString FolderPath = GetStringParam(Params, TEXT("folder_path"));
    if (FolderPath.IsEmpty())
    {
        return MakeError(TEXT("MISSING_PARAM"), TEXT("folder_path is required"));
    }

    bool bCreated = UEditorAssetLibrary::MakeDirectory(FolderPath);

    TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
    Data->SetStringField(TEXT("folder_path"), FolderPath);
    Data->SetBoolField(TEXT("created"), bCreated);
    return MakeSuccess(Data);
}

// ---------------------------------------------------------------------------
// list_folder (BUG FIX: was discarding ListAssets return value)
// ---------------------------------------------------------------------------

TSharedPtr<FJsonObject> FNexusAssetHandler::HandleListFolder(const TSharedPtr<FJsonObject>& Params)
{
    FString Path = GetStringParam(Params, TEXT("folder_path"));
    if (Path.IsEmpty())
    {
        Path = GetStringParam(Params, TEXT("path"), TEXT("/Game/"));
    }
    bool bRecursive = GetBoolParam(Params, TEXT("recursive"), false);

    // FIX: Capture the return value from ListAssets
    TArray<FString> Assets = UEditorAssetLibrary::ListAssets(Path, bRecursive, false);

    TArray<TSharedPtr<FJsonValue>> Items;
    for (const FString& Asset : Assets)
    {
        Items.Add(MakeShareable(new FJsonValueString(Asset)));
    }

    TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
    Data->SetStringField(TEXT("path"), Path);
    Data->SetArrayField(TEXT("items"), Items);
    Data->SetNumberField(TEXT("count"), Items.Num());
    return MakeSuccess(Data);
}

// ---------------------------------------------------------------------------
// set_metadata
// ---------------------------------------------------------------------------

TSharedPtr<FJsonObject> FNexusAssetHandler::HandleSetMetadata(const TSharedPtr<FJsonObject>& Params)
{
    FString AssetPath = GetStringParam(Params, TEXT("asset_path"));
    FString Key = GetStringParam(Params, TEXT("key"));
    FString Value = GetStringParam(Params, TEXT("value"));

    if (AssetPath.IsEmpty())
    {
        return MakeError(TEXT("MISSING_PARAM"), TEXT("asset_path is required"));
    }
    if (Key.IsEmpty())
    {
        return MakeError(TEXT("MISSING_PARAM"), TEXT("key is required"));
    }

    if (!UEditorAssetLibrary::DoesAssetExist(AssetPath))
    {
        return MakeError(TEXT("ASSET_NOT_FOUND"), FString::Printf(TEXT("Asset not found: '%s'"), *AssetPath));
    }

    // UE 5.7: SetMetadataTag now requires loading the asset first and passing UObject*
    UObject* LoadedAsset = UEditorAssetLibrary::LoadAsset(AssetPath);
    if (!LoadedAsset)
    {
        return MakeError(TEXT("LOAD_FAILED"), FString::Printf(TEXT("Failed to load asset: '%s'"), *AssetPath));
    }

    // Use the EditorAssetSubsystem to set metadata
    UEditorAssetSubsystem* EditorAssetSubsystem = GEditor->GetEditorSubsystem<UEditorAssetSubsystem>();
    if (EditorAssetSubsystem)
    {
        EditorAssetSubsystem->SetMetadataTag(LoadedAsset, FName(*Key), Value);
    }

    TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
    Data->SetStringField(TEXT("asset_path"), AssetPath);
    Data->SetStringField(TEXT("key"), Key);
    Data->SetStringField(TEXT("value"), Value);
    Data->SetBoolField(TEXT("success"), true);
    return MakeSuccess(Data);
}

// ---------------------------------------------------------------------------
// validate
// ---------------------------------------------------------------------------

TSharedPtr<FJsonObject> FNexusAssetHandler::HandleValidate(const TSharedPtr<FJsonObject>& Params)
{
    FString AssetPath = GetStringParam(Params, TEXT("asset_path"));
    if (AssetPath.IsEmpty())
    {
        return MakeError(TEXT("MISSING_PARAM"), TEXT("asset_path is required"));
    }

    IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
    FAssetData AssetData = Registry.GetAssetByObjectPath(FSoftObjectPath(AssetPath));

    TArray<TSharedPtr<FJsonValue>> Issues;
    bool bValid = true;

    // Check 1: Asset exists in registry
    if (!AssetData.IsValid())
    {
        bValid = false;
        Issues.Add(MakeShareable(new FJsonValueString(TEXT("Asset not found in registry"))));
    }
    else
    {
        // Check 2: Asset is loadable
        UObject* LoadedAsset = AssetData.GetAsset();
        if (!LoadedAsset)
        {
            bValid = false;
            Issues.Add(MakeShareable(new FJsonValueString(TEXT("Asset failed to load - possible corruption"))));
        }

        // Check 3: Check for circular references (basic check via dependencies)
        TArray<FAssetIdentifier> Dependencies;
        Registry.GetDependencies(FAssetIdentifier(FName(*AssetPath)), Dependencies);
        for (const FAssetIdentifier& Dep : Dependencies)
        {
            TArray<FAssetIdentifier> SubDeps;
            Registry.GetDependencies(Dep, SubDeps);
            for (const FAssetIdentifier& SubDep : SubDeps)
            {
                if (SubDep.PackageName == FName(*AssetPath))
                {
                    Issues.Add(MakeShareable(new FJsonValueString(
                        FString::Printf(TEXT("Circular reference detected with '%s'"), *Dep.PackageName.ToString()))));
                }
            }
        }
    }

    TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
    Data->SetStringField(TEXT("asset_path"), AssetPath);
    Data->SetBoolField(TEXT("valid"), bValid);
    Data->SetArrayField(TEXT("issues"), Issues);
    return MakeSuccess(Data);
}

// ---------------------------------------------------------------------------
// import_batch (A3) — import multiple assets in one call
// ---------------------------------------------------------------------------

TSharedPtr<FJsonObject> FNexusAssetHandler::HandleImportBatch(const TSharedPtr<FJsonObject>& Params)
{
    const TArray<TSharedPtr<FJsonValue>>* Imports;
    if (!Params->TryGetArrayField(TEXT("imports"), Imports))
    {
        return MakeError(TEXT("MISSING_PARAM"), TEXT("'imports' array is required"));
    }

    IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get();

    TArray<TSharedPtr<FJsonValue>> Results;
    TArray<TSharedPtr<FJsonValue>> Errors;
    int32 SuccessCount = 0;

    for (int32 i = 0; i < Imports->Num(); i++)
    {
        const TSharedPtr<FJsonObject>* ImportSpec;
        if (!(*Imports)[i]->TryGetObject(ImportSpec) || !ImportSpec)
        {
            TSharedPtr<FJsonObject> Err = MakeShareable(new FJsonObject());
            Err->SetNumberField(TEXT("index"), i);
            Err->SetStringField(TEXT("error"), TEXT("Invalid import spec (not an object)"));
            Errors.Add(MakeShareable(new FJsonValueObject(Err)));
            continue;
        }

        FString SourcePath = GetStringParam(*ImportSpec, TEXT("source_path"));
        FString DestinationPath = GetStringParam(*ImportSpec, TEXT("destination_path"));

        if (SourcePath.IsEmpty() || DestinationPath.IsEmpty())
        {
            TSharedPtr<FJsonObject> Err = MakeShareable(new FJsonObject());
            Err->SetNumberField(TEXT("index"), i);
            Err->SetStringField(TEXT("error"), TEXT("source_path and destination_path are required"));
            Errors.Add(MakeShareable(new FJsonValueObject(Err)));
            continue;
        }

        if (!FPaths::FileExists(SourcePath))
        {
            TSharedPtr<FJsonObject> Err = MakeShareable(new FJsonObject());
            Err->SetNumberField(TEXT("index"), i);
            Err->SetStringField(TEXT("error"),
                FString::Printf(TEXT("Source file not found: '%s'"), *SourcePath));
            Errors.Add(MakeShareable(new FJsonValueObject(Err)));
            continue;
        }

        UAutomatedAssetImportData* ImportData = NewObject<UAutomatedAssetImportData>();
        ImportData->Filenames.Add(SourcePath);
        ImportData->DestinationPath = DestinationPath;
        ImportData->bReplaceExisting = true;

        TArray<UObject*> Imported = AssetTools.ImportAssetsAutomated(ImportData);
        if (Imported.Num() == 0)
        {
            TSharedPtr<FJsonObject> Err = MakeShareable(new FJsonObject());
            Err->SetNumberField(TEXT("index"), i);
            Err->SetStringField(TEXT("error"),
                FString::Printf(TEXT("Failed to import '%s'"), *SourcePath));
            Errors.Add(MakeShareable(new FJsonValueObject(Err)));
            continue;
        }

        TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject());
        Result->SetStringField(TEXT("asset_path"), Imported[0]->GetPathName());
        Result->SetStringField(TEXT("asset_class"), Imported[0]->GetClass()->GetName());
        Result->SetStringField(TEXT("source_path"), SourcePath);
        Results.Add(MakeShareable(new FJsonValueObject(Result)));
        SuccessCount++;
    }

    TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
    Data->SetNumberField(TEXT("imported_count"), SuccessCount);
    Data->SetNumberField(TEXT("failed_count"), Imports->Num() - SuccessCount);
    Data->SetArrayField(TEXT("results"), Results);
    Data->SetArrayField(TEXT("errors"), Errors);
    return MakeSuccess(Data);
}
