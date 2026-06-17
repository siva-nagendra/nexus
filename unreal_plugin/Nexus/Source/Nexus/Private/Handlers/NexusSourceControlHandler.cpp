// Copyright Nexus Team. All Rights Reserved.

#include "Handlers/NexusSourceControlHandler.h"
#include "ISourceControlModule.h"
#include "ISourceControlProvider.h"
#include "ISourceControlState.h"
#include "ISourceControlRevision.h"
#include "SourceControlOperations.h"
#include "SourceControlHelpers.h"
#include "Misc/Paths.h"
#include "Misc/PackageName.h"
#include "HAL/FileManager.h"

// ─────────────────────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────────────────────

TArray<FString> FNexusSourceControlHandler::GetFilePathsFromParams(
    const TSharedPtr<FJsonObject>& Params, const FString& Key)
{
    TArray<FString> Paths;
    if (!Params.IsValid()) return Paths;

    const TArray<TSharedPtr<FJsonValue>>* PathsArray;
    if (Params->TryGetArrayField(Key, PathsArray))
    {
        for (const auto& PathVal : *PathsArray)
        {
            FString Path;
            if (PathVal->TryGetString(Path) && !Path.IsEmpty())
            {
                // Convert asset paths to filesystem paths if needed
                if (Path.StartsWith(TEXT("/Game/")) || Path.StartsWith(TEXT("/Engine/")))
                {
                    FString FilePath;
                    if (FPackageName::TryConvertLongPackageNameToFilename(
                        Path, FilePath, FPackageName::GetAssetPackageExtension()))
                    {
                        Paths.Add(FPaths::ConvertRelativePathToFull(FilePath));
                    }
                    else
                    {
                        Paths.Add(Path);
                    }
                }
                else
                {
                    Paths.Add(FPaths::ConvertRelativePathToFull(Path));
                }
            }
        }
    }
    return Paths;
}

FString FNexusSourceControlHandler::SourceControlStateToString(
    const ISourceControlState& State)
{
    if (State.IsCheckedOut())          return TEXT("CheckedOut");
    if (State.IsAdded())               return TEXT("Added");
    if (State.IsDeleted())             return TEXT("Deleted");
    if (State.IsModified())            return TEXT("Modified");
    if (State.IsConflicted())          return TEXT("Conflicted");
    if (!State.IsSourceControlled())   return TEXT("NotControlled");
    if (State.IsCheckedOutOther())     return TEXT("CheckedOutOther");
    if (State.CanCheckout())           return TEXT("Unchanged");
    return TEXT("Unknown");
}

// ─────────────────────────────────────────────────────────────────────────────
// Dispatch
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FNexusSourceControlHandler::Handle(
    const FString& CommandType,
    const TSharedPtr<FJsonObject>& Params)
{
    FString SubCommand = CommandType.RightChop(GetNamespace().Len() + 1);

    if (SubCommand == TEXT("get_source_control_status"))  return HandleGetSourceControlStatus(Params);
    if (SubCommand == TEXT("checkout_files"))              return HandleCheckoutFiles(Params);
    if (SubCommand == TEXT("add_files"))                   return HandleAddFiles(Params);
    if (SubCommand == TEXT("revert_files"))                return HandleRevertFiles(Params);
    if (SubCommand == TEXT("submit_changelist"))           return HandleSubmitChangelist(Params);
    if (SubCommand == TEXT("get_file_history"))            return HandleGetFileHistory(Params);
    if (SubCommand == TEXT("mark_for_delete"))             return HandleMarkForDelete(Params);

    return MakeError(TEXT("NOT_IMPLEMENTED"),
        FString::Printf(TEXT("Command '%s' not yet implemented"), *CommandType));
}

// ─────────────────────────────────────────────────────────────────────────────
// sourcecontrol.get_source_control_status
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FNexusSourceControlHandler::HandleGetSourceControlStatus(
    const TSharedPtr<FJsonObject>& Params)
{
    ISourceControlProvider& Provider = ISourceControlModule::Get().GetProvider();
    if (!Provider.IsEnabled())
    {
        return MakeError(TEXT("SC_DISABLED"),
            TEXT("Source control is not enabled. Configure it in Editor Preferences."));
    }

    TArray<FString> FilePaths = GetFilePathsFromParams(Params, TEXT("file_paths"));
    FString Directory = GetStringParam(Params, TEXT("directory"));
    bool bIncludeUncontrolled = GetBoolParam(Params, TEXT("include_uncontrolled"));

    // If a directory is specified instead of specific files, gather files from it
    if (FilePaths.Num() == 0 && !Directory.IsEmpty())
    {
        FString FullDir = FPaths::ConvertRelativePathToFull(Directory);
        if (!FPaths::DirectoryExists(FullDir))
        {
            // Try as project-relative
            FullDir = FPaths::ConvertRelativePathToFull(
                FPaths::ProjectDir() / Directory);
        }
        if (FPaths::DirectoryExists(FullDir))
        {
            IFileManager::Get().FindFilesRecursive(
                FilePaths, *FullDir, TEXT("*.*"), true, false);
        }
        else
        {
            return MakeError(TEXT("DIR_NOT_FOUND"),
                FString::Printf(TEXT("Directory '%s' not found"), *Directory));
        }
    }

    if (FilePaths.Num() == 0)
    {
        return MakeError(TEXT("MISSING_PARAM"),
            TEXT("Provide file_paths or directory to check status"));
    }

    // Update status from the provider
    Provider.Execute(
        ISourceControlOperation::Create<FUpdateStatus>(),
        FilePaths);

    TArray<TSharedPtr<FJsonValue>> FilesArr;

    for (const FString& FilePath : FilePaths)
    {
        FSourceControlStatePtr FileState = Provider.GetState(FilePath, EStateCacheUsage::Use);
        if (!FileState.IsValid()) continue;

        FString StateStr = SourceControlStateToString(*FileState);

        // Skip uncontrolled files unless requested
        if (!bIncludeUncontrolled && StateStr == TEXT("NotControlled"))
        {
            continue;
        }

        TSharedPtr<FJsonObject> FileObj = MakeShareable(new FJsonObject());
        FileObj->SetStringField(TEXT("path"), FilePath);
        FileObj->SetStringField(TEXT("state"), StateStr);
        FileObj->SetBoolField(TEXT("is_checked_out"), FileState->IsCheckedOut());
        FileObj->SetBoolField(TEXT("is_current"), FileState->IsCurrent());
        FileObj->SetBoolField(TEXT("is_source_controlled"), FileState->IsSourceControlled());
        FileObj->SetBoolField(TEXT("can_checkout"), FileState->CanCheckout());
        FileObj->SetBoolField(TEXT("can_edit"), FileState->CanEdit());

        // Check if checked out by someone else
        if (FileState->IsCheckedOutOther())
        {
            FString OtherUser;
            FileState->IsCheckedOutOther(&OtherUser);
            FileObj->SetStringField(TEXT("checked_out_by"), OtherUser);
        }

        FilesArr.Add(MakeShareable(new FJsonValueObject(FileObj)));
    }

    TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
    Data->SetStringField(TEXT("provider"), Provider.GetName().ToString());
    Data->SetArrayField(TEXT("files"), FilesArr);
    Data->SetNumberField(TEXT("count"), FilesArr.Num());
    return MakeSuccess(Data);
}

// ─────────────────────────────────────────────────────────────────────────────
// sourcecontrol.checkout_files
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FNexusSourceControlHandler::HandleCheckoutFiles(
    const TSharedPtr<FJsonObject>& Params)
{
    ISourceControlProvider& Provider = ISourceControlModule::Get().GetProvider();
    if (!Provider.IsEnabled())
    {
        return MakeError(TEXT("SC_DISABLED"),
            TEXT("Source control is not enabled"));
    }

    TArray<FString> FilePaths = GetFilePathsFromParams(Params);
    if (FilePaths.Num() == 0)
    {
        return MakeError(TEXT("MISSING_PARAM"), TEXT("file_paths is required"));
    }

    ECommandResult::Type Result = Provider.Execute(
        ISourceControlOperation::Create<FCheckOut>(),
        FilePaths);

    TArray<TSharedPtr<FJsonValue>> ResultsArr;
    int32 SuccessCount = 0;
    int32 FailCount = 0;

    for (const FString& FilePath : FilePaths)
    {
        FSourceControlStatePtr FileState = Provider.GetState(FilePath, EStateCacheUsage::Use);
        TSharedPtr<FJsonObject> FileObj = MakeShareable(new FJsonObject());
        FileObj->SetStringField(TEXT("path"), FilePath);

        if (FileState.IsValid() && FileState->IsCheckedOut())
        {
            FileObj->SetBoolField(TEXT("checked_out"), true);
            SuccessCount++;
        }
        else
        {
            FileObj->SetBoolField(TEXT("checked_out"), false);
            FileObj->SetStringField(TEXT("error"), TEXT("Failed to check out"));
            FailCount++;
        }

        ResultsArr.Add(MakeShareable(new FJsonValueObject(FileObj)));
    }

    TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
    Data->SetArrayField(TEXT("files"), ResultsArr);
    Data->SetNumberField(TEXT("success_count"), SuccessCount);
    Data->SetNumberField(TEXT("failed_count"), FailCount);
    Data->SetBoolField(TEXT("all_succeeded"), Result == ECommandResult::Succeeded);
    return MakeSuccess(Data);
}

// ─────────────────────────────────────────────────────────────────────────────
// sourcecontrol.add_files
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FNexusSourceControlHandler::HandleAddFiles(
    const TSharedPtr<FJsonObject>& Params)
{
    ISourceControlProvider& Provider = ISourceControlModule::Get().GetProvider();
    if (!Provider.IsEnabled())
    {
        return MakeError(TEXT("SC_DISABLED"),
            TEXT("Source control is not enabled"));
    }

    TArray<FString> FilePaths = GetFilePathsFromParams(Params);
    if (FilePaths.Num() == 0)
    {
        return MakeError(TEXT("MISSING_PARAM"), TEXT("file_paths is required"));
    }

    ECommandResult::Type Result = Provider.Execute(
        ISourceControlOperation::Create<FMarkForAdd>(),
        FilePaths);

    TArray<TSharedPtr<FJsonValue>> ResultsArr;
    int32 SuccessCount = 0;
    int32 FailCount = 0;

    for (const FString& FilePath : FilePaths)
    {
        FSourceControlStatePtr FileState = Provider.GetState(FilePath, EStateCacheUsage::Use);
        TSharedPtr<FJsonObject> FileObj = MakeShareable(new FJsonObject());
        FileObj->SetStringField(TEXT("path"), FilePath);

        if (FileState.IsValid() && FileState->IsAdded())
        {
            FileObj->SetBoolField(TEXT("added"), true);
            SuccessCount++;
        }
        else
        {
            FileObj->SetBoolField(TEXT("added"), false);
            FileObj->SetStringField(TEXT("error"), TEXT("Failed to mark for add"));
            FailCount++;
        }

        ResultsArr.Add(MakeShareable(new FJsonValueObject(FileObj)));
    }

    TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
    Data->SetArrayField(TEXT("files"), ResultsArr);
    Data->SetNumberField(TEXT("success_count"), SuccessCount);
    Data->SetNumberField(TEXT("failed_count"), FailCount);
    Data->SetBoolField(TEXT("all_succeeded"), Result == ECommandResult::Succeeded);
    return MakeSuccess(Data);
}

// ─────────────────────────────────────────────────────────────────────────────
// sourcecontrol.revert_files
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FNexusSourceControlHandler::HandleRevertFiles(
    const TSharedPtr<FJsonObject>& Params)
{
    ISourceControlProvider& Provider = ISourceControlModule::Get().GetProvider();
    if (!Provider.IsEnabled())
    {
        return MakeError(TEXT("SC_DISABLED"),
            TEXT("Source control is not enabled"));
    }

    TArray<FString> FilePaths = GetFilePathsFromParams(Params);
    if (FilePaths.Num() == 0)
    {
        return MakeError(TEXT("MISSING_PARAM"), TEXT("file_paths is required"));
    }

    bool bRevertUnchangedOnly = GetBoolParam(Params, TEXT("revert_unchanged_only"));

    TSharedRef<FRevert> RevertOp = ISourceControlOperation::Create<FRevert>();

    // If revert_unchanged_only, filter to only unchanged files first
    TArray<FString> FilesToRevert;
    if (bRevertUnchangedOnly)
    {
        // Update status first
        Provider.Execute(
            ISourceControlOperation::Create<FUpdateStatus>(),
            FilePaths);

        for (const FString& FilePath : FilePaths)
        {
            FSourceControlStatePtr FileState = Provider.GetState(FilePath, EStateCacheUsage::Use);
            if (FileState.IsValid() && FileState->IsCheckedOut() && !FileState->IsModified())
            {
                FilesToRevert.Add(FilePath);
            }
        }
    }
    else
    {
        FilesToRevert = FilePaths;
    }

    ECommandResult::Type Result = ECommandResult::Succeeded;
    if (FilesToRevert.Num() > 0)
    {
        Result = Provider.Execute(RevertOp, FilesToRevert);
    }

    TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
    Data->SetNumberField(TEXT("reverted_count"), FilesToRevert.Num());
    Data->SetNumberField(TEXT("requested_count"), FilePaths.Num());
    Data->SetBoolField(TEXT("revert_unchanged_only"), bRevertUnchangedOnly);
    Data->SetBoolField(TEXT("all_succeeded"), Result == ECommandResult::Succeeded);

    // List what was reverted
    TArray<TSharedPtr<FJsonValue>> RevertedArr;
    for (const FString& FilePath : FilesToRevert)
    {
        RevertedArr.Add(MakeShareable(new FJsonValueString(FilePath)));
    }
    Data->SetArrayField(TEXT("reverted_files"), RevertedArr);

    return MakeSuccess(Data);
}

// ─────────────────────────────────────────────────────────────────────────────
// sourcecontrol.submit_changelist
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FNexusSourceControlHandler::HandleSubmitChangelist(
    const TSharedPtr<FJsonObject>& Params)
{
    ISourceControlProvider& Provider = ISourceControlModule::Get().GetProvider();
    if (!Provider.IsEnabled())
    {
        return MakeError(TEXT("SC_DISABLED"),
            TEXT("Source control is not enabled"));
    }

    FString Description = GetStringParam(Params, TEXT("description"));
    if (Description.IsEmpty())
    {
        return MakeError(TEXT("MISSING_PARAM"),
            TEXT("description is required for changelist submission"));
    }

    TArray<FString> FilePaths = GetFilePathsFromParams(Params);
    bool bKeepCheckedOut = GetBoolParam(Params, TEXT("keep_checked_out"));

    // If no specific files, return an error - we can't enumerate all checked out files
    // in UE 5.7 without knowing the files in advance
    if (FilePaths.Num() == 0)
    {
        return MakeError(TEXT("NO_FILES"),
            TEXT("No files specified. In UE 5.7, you must explicitly specify files to submit."));
    }

    if (FilePaths.Num() == 0)
    {
        return MakeError(TEXT("NO_FILES"),
            TEXT("No files to submit. Check out files first."));
    }

    // Create and configure the check-in operation
    TSharedRef<FCheckIn> CheckInOp = ISourceControlOperation::Create<FCheckIn>();
    CheckInOp->SetDescription(FText::FromString(Description));

    ECommandResult::Type Result = Provider.Execute(CheckInOp, FilePaths);

    TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
    Data->SetBoolField(TEXT("submitted"), Result == ECommandResult::Succeeded);
    Data->SetStringField(TEXT("description"), Description);
    Data->SetNumberField(TEXT("file_count"), FilePaths.Num());
    Data->SetBoolField(TEXT("keep_checked_out"), bKeepCheckedOut);

    if (Result != ECommandResult::Succeeded)
    {
        Data->SetStringField(TEXT("error_detail"),
            TEXT("Check-in operation failed. Check source control logs for details."));
    }

    // List submitted files
    TArray<TSharedPtr<FJsonValue>> FilesArr;
    for (const FString& FilePath : FilePaths)
    {
        FilesArr.Add(MakeShareable(new FJsonValueString(FilePath)));
    }
    Data->SetArrayField(TEXT("files"), FilesArr);

    return MakeSuccess(Data);
}

// ─────────────────────────────────────────────────────────────────────────────
// sourcecontrol.get_file_history
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FNexusSourceControlHandler::HandleGetFileHistory(
    const TSharedPtr<FJsonObject>& Params)
{
    ISourceControlProvider& Provider = ISourceControlModule::Get().GetProvider();
    if (!Provider.IsEnabled())
    {
        return MakeError(TEXT("SC_DISABLED"),
            TEXT("Source control is not enabled"));
    }

    FString FilePath = GetStringParam(Params, TEXT("file_path"));
    if (FilePath.IsEmpty())
    {
        return MakeError(TEXT("MISSING_PARAM"), TEXT("file_path is required"));
    }

    int32 MaxRevisions = static_cast<int32>(GetNumberParam(Params, TEXT("max_revisions"), 20.0));

    // Convert asset path to filesystem path if needed
    if (FilePath.StartsWith(TEXT("/Game/")) || FilePath.StartsWith(TEXT("/Engine/")))
    {
        FString ConvertedPath;
        if (FPackageName::TryConvertLongPackageNameToFilename(
            FilePath, ConvertedPath, FPackageName::GetAssetPackageExtension()))
        {
            FilePath = FPaths::ConvertRelativePathToFull(ConvertedPath);
        }
    }
    else
    {
        FilePath = FPaths::ConvertRelativePathToFull(FilePath);
    }

    // Request history update
    TSharedRef<FUpdateStatus> UpdateOp = ISourceControlOperation::Create<FUpdateStatus>();
    UpdateOp->SetUpdateHistory(true);
    Provider.Execute(UpdateOp, TArray<FString>{FilePath});

    // Get the state with history
    FSourceControlStatePtr FileState = Provider.GetState(FilePath, EStateCacheUsage::Use);
    if (!FileState.IsValid())
    {
        return MakeError(TEXT("FILE_NOT_FOUND"),
            FString::Printf(TEXT("Cannot get source control state for '%s'"), *FilePath));
    }

    TArray<TSharedPtr<FJsonValue>> RevisionsArr;
    int32 HistoryCount = FileState->GetHistorySize();
    int32 Count = FMath::Min(HistoryCount, MaxRevisions);

    for (int32 i = 0; i < Count; ++i)
    {
        TSharedPtr<ISourceControlRevision, ESPMode::ThreadSafe> Revision = FileState->GetHistoryItem(i);
        if (!Revision.IsValid()) continue;

        TSharedPtr<FJsonObject> RevObj = MakeShareable(new FJsonObject());
        RevObj->SetNumberField(TEXT("revision_number"), Revision->GetRevisionNumber());
        RevObj->SetStringField(TEXT("revision"), Revision->GetRevision());
        RevObj->SetNumberField(TEXT("changelist"), Revision->GetCheckInIdentifier());
        RevObj->SetStringField(TEXT("date"), Revision->GetDate().ToString());
        RevObj->SetStringField(TEXT("user_name"), Revision->GetUserName());
        RevObj->SetStringField(TEXT("description"), Revision->GetDescription());
        RevObj->SetStringField(TEXT("action"), Revision->GetAction());

        RevisionsArr.Add(MakeShareable(new FJsonValueObject(RevObj)));
    }

    TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
    Data->SetStringField(TEXT("file_path"), FilePath);
    Data->SetArrayField(TEXT("revisions"), RevisionsArr);
    Data->SetNumberField(TEXT("count"), RevisionsArr.Num());
    Data->SetNumberField(TEXT("total_revisions"), HistoryCount);
    return MakeSuccess(Data);
}

// ─────────────────────────────────────────────────────────────────────────────
// sourcecontrol.mark_for_delete
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FNexusSourceControlHandler::HandleMarkForDelete(
    const TSharedPtr<FJsonObject>& Params)
{
    ISourceControlProvider& Provider = ISourceControlModule::Get().GetProvider();
    if (!Provider.IsEnabled())
    {
        return MakeError(TEXT("SC_DISABLED"),
            TEXT("Source control is not enabled"));
    }

    TArray<FString> FilePaths = GetFilePathsFromParams(Params);
    if (FilePaths.Num() == 0)
    {
        return MakeError(TEXT("MISSING_PARAM"), TEXT("file_paths is required"));
    }

    ECommandResult::Type Result = Provider.Execute(
        ISourceControlOperation::Create<FDelete>(),
        FilePaths);

    TArray<TSharedPtr<FJsonValue>> ResultsArr;
    int32 SuccessCount = 0;
    int32 FailCount = 0;

    for (const FString& FilePath : FilePaths)
    {
        FSourceControlStatePtr FileState = Provider.GetState(FilePath, EStateCacheUsage::Use);
        TSharedPtr<FJsonObject> FileObj = MakeShareable(new FJsonObject());
        FileObj->SetStringField(TEXT("path"), FilePath);

        if (FileState.IsValid() && FileState->IsDeleted())
        {
            FileObj->SetBoolField(TEXT("marked_for_delete"), true);
            SuccessCount++;
        }
        else
        {
            FileObj->SetBoolField(TEXT("marked_for_delete"), false);
            FileObj->SetStringField(TEXT("error"), TEXT("Failed to mark for delete"));
            FailCount++;
        }

        ResultsArr.Add(MakeShareable(new FJsonValueObject(FileObj)));
    }

    TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
    Data->SetArrayField(TEXT("files"), ResultsArr);
    Data->SetNumberField(TEXT("success_count"), SuccessCount);
    Data->SetNumberField(TEXT("failed_count"), FailCount);
    Data->SetBoolField(TEXT("all_succeeded"), Result == ECommandResult::Succeeded);
    return MakeSuccess(Data);
}
