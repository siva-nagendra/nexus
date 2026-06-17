// Copyright Nexus Team. All Rights Reserved.

#include "Handlers/NexusPythonExecHandler.h"
#include "IPythonScriptPlugin.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/PlatformTime.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

// ─────────────────────────────────────────────────────────────────────────────
// Helper: Check if PythonScriptPlugin is available
// ─────────────────────────────────────────────────────────────────────────────

static IPythonScriptPlugin* GetPythonPlugin()
{
    static const FName PythonModuleName(TEXT("PythonScriptPlugin"));
    if (FModuleManager::Get().IsModuleLoaded(PythonModuleName))
    {
        return &FModuleManager::LoadModuleChecked<IPythonScriptPlugin>(PythonModuleName);
    }
    return nullptr;
}

// ─────────────────────────────────────────────────────────────────────────────
// Dispatch
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FNexusPythonExecHandler::Handle(
    const FString& CommandType,
    const TSharedPtr<FJsonObject>& Params)
{
    FString SubCommand = CommandType.RightChop(GetNamespace().Len() + 1);

    if (SubCommand == TEXT("execute"))      return HandleExecute(Params);
    if (SubCommand == TEXT("execute_file")) return HandleExecuteFile(Params);
    if (SubCommand == TEXT("get_paths"))    return HandleGetPaths(Params);

    return MakeError(TEXT("NOT_IMPLEMENTED"),
        FString::Printf(TEXT("Command '%s' not yet implemented"), *CommandType));
}

// ─────────────────────────────────────────────────────────────────────────────
// python.execute
// Execute inline Python code and capture output
// Params: code (string, required)
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FNexusPythonExecHandler::HandleExecute(
    const TSharedPtr<FJsonObject>& Params)
{
    FString Code = GetStringParam(Params, TEXT("code"));
    if (Code.IsEmpty())
    {
        return MakeError(TEXT("MISSING_PARAM"), TEXT("code is required"));
    }

    IPythonScriptPlugin* PythonPlugin = GetPythonPlugin();
    if (!PythonPlugin)
    {
        return MakeError(TEXT("PYTHON_NOT_AVAILABLE"),
            TEXT("PythonScriptPlugin is not loaded. Enable it in the project's .uplugin or Editor Preferences."));
    }

    // Wrap the user code to capture output and timing:
    // We use a wrapper script that:
    // 1. Redirects stdout/stderr to a StringIO buffer
    // 2. Executes the user code via exec()
    // 3. Captures the output and any return value
    // 4. Prints a NEXUS_RESULT marker with JSON output
    FString WrappedCode = FString::Printf(TEXT(
        "import sys, io, json, time, traceback\n"
        "_nexus_stdout = io.StringIO()\n"
        "_nexus_stderr = io.StringIO()\n"
        "_nexus_old_stdout = sys.stdout\n"
        "_nexus_old_stderr = sys.stderr\n"
        "sys.stdout = _nexus_stdout\n"
        "sys.stderr = _nexus_stderr\n"
        "_nexus_start = time.perf_counter()\n"
        "_nexus_return = ''\n"
        "_nexus_success = True\n"
        "try:\n"
        "    exec(%s)\n"
        "except Exception:\n"
        "    _nexus_success = False\n"
        "    traceback.print_exc()\n"
        "finally:\n"
        "    _nexus_elapsed = (time.perf_counter() - _nexus_start) * 1000.0\n"
        "    sys.stdout = _nexus_old_stdout\n"
        "    sys.stderr = _nexus_old_stderr\n"
        "    _nexus_out = _nexus_stdout.getvalue() + _nexus_stderr.getvalue()\n"
        "    _nexus_result = json.dumps({\n"
        "        'output': _nexus_out,\n"
        "        'return_value': _nexus_return,\n"
        "        'success': _nexus_success,\n"
        "        'execution_time_ms': round(_nexus_elapsed, 2)\n"
        "    })\n"
        "    print('NEXUS_RESULT:' + _nexus_result)\n"
    ), *QuoteCodeForPython(Code));

    // Execute through the Python plugin
    // ExecPythonCommand returns the captured log output
    FPythonCommandEx PythonCommand;
    PythonCommand.Command = WrappedCode;
    PythonCommand.ExecutionMode = EPythonCommandExecutionMode::ExecuteFile;
    PythonCommand.FileExecutionScope = EPythonFileExecutionScope::Private;

    bool bSuccess = PythonPlugin->ExecPythonCommandEx(PythonCommand);

    // Parse the NEXUS_RESULT from the log output
    TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());

    // Build log output string from FPythonLogOutputEntry array
    FString LogOutput;
    for (const FPythonLogOutputEntry& Entry : PythonCommand.LogOutput)
    {
        if (!LogOutput.IsEmpty()) LogOutput += TEXT("\n");
        LogOutput += Entry.Output;
    }

    FString ResultJson;
    bool bFoundResult = false;

    // Search for the NEXUS_RESULT marker in log output
    for (int32 i = PythonCommand.LogOutput.Num() - 1; i >= 0; --i)
    {
        const FString& Line = PythonCommand.LogOutput[i].Output;
        int32 MarkerIdx = Line.Find(TEXT("NEXUS_RESULT:"));
        if (MarkerIdx != INDEX_NONE)
        {
            ResultJson = Line.RightChop(MarkerIdx + 13); // len("NEXUS_RESULT:") = 13
            bFoundResult = true;
            break;
        }
    }

    if (bFoundResult)
    {
        // Parse the JSON result
        TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ResultJson);
        TSharedPtr<FJsonObject> ResultObj;
        if (FJsonSerializer::Deserialize(Reader, ResultObj) && ResultObj.IsValid())
        {
            return MakeSuccess(ResultObj);
        }
    }

    // Fallback: return raw output if we couldn't parse the structured result
    FString Output;
    for (const FPythonLogOutputEntry& Entry : PythonCommand.LogOutput)
    {
        if (!Entry.Output.Contains(TEXT("NEXUS_RESULT:")))
        {
            if (!Output.IsEmpty()) Output += TEXT("\n");
            Output += Entry.Output;
        }
    }

    Data->SetStringField(TEXT("output"), Output);
    Data->SetStringField(TEXT("return_value"), TEXT(""));
    Data->SetBoolField(TEXT("success"), bSuccess);
    Data->SetNumberField(TEXT("execution_time_ms"), 0.0);

    if (!bSuccess)
    {
        Data->SetBoolField(TEXT("success"), false);
    }

    return MakeSuccess(Data);
}

// ─────────────────────────────────────────────────────────────────────────────
// python.execute_file
// Execute a .py file inside UE's Python interpreter
// Params: file_path (string, required), args (array of strings, optional)
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FNexusPythonExecHandler::HandleExecuteFile(
    const TSharedPtr<FJsonObject>& Params)
{
    FString FilePath = GetStringParam(Params, TEXT("file_path"));
    if (FilePath.IsEmpty())
    {
        return MakeError(TEXT("MISSING_PARAM"), TEXT("file_path is required"));
    }

    // Resolve relative paths against the project directory
    if (FPaths::IsRelative(FilePath))
    {
        FilePath = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir(), FilePath);
    }

    // Verify the file exists
    if (!FPaths::FileExists(FilePath))
    {
        return MakeError(TEXT("FILE_NOT_FOUND"),
            FString::Printf(TEXT("Python file not found: %s"), *FilePath));
    }

    IPythonScriptPlugin* PythonPlugin = GetPythonPlugin();
    if (!PythonPlugin)
    {
        return MakeError(TEXT("PYTHON_NOT_AVAILABLE"),
            TEXT("PythonScriptPlugin is not loaded. Enable it in the project's .uplugin or Editor Preferences."));
    }

    // Extract optional args
    TArray<FString> Args;
    const TArray<TSharedPtr<FJsonValue>>* ArgsArray = nullptr;
    if (Params.IsValid() && Params->TryGetArrayField(TEXT("args"), ArgsArray))
    {
        for (const TSharedPtr<FJsonValue>& Val : *ArgsArray)
        {
            FString Arg = Val->AsString();
            if (!Arg.IsEmpty())
            {
                Args.Add(Arg);
            }
        }
    }

    // Build wrapper code that sets up sys.argv then execfile-style executes the script
    FString NormalizedPath = FilePath.Replace(TEXT("\\"), TEXT("/"));

    // Build the args list string for Python
    FString ArgsListStr = TEXT("[");
    ArgsListStr += FString::Printf(TEXT("r'%s'"), *NormalizedPath);
    for (const FString& Arg : Args)
    {
        ArgsListStr += FString::Printf(TEXT(", r'%s'"), *Arg);
    }
    ArgsListStr += TEXT("]");

    FString WrappedCode = FString::Printf(TEXT(
        "import sys, io, json, time, traceback\n"
        "sys.argv = %s\n"
        "_nexus_stdout = io.StringIO()\n"
        "_nexus_stderr = io.StringIO()\n"
        "_nexus_old_stdout = sys.stdout\n"
        "_nexus_old_stderr = sys.stderr\n"
        "sys.stdout = _nexus_stdout\n"
        "sys.stderr = _nexus_stderr\n"
        "_nexus_start = time.perf_counter()\n"
        "_nexus_success = True\n"
        "try:\n"
        "    with open(r'%s', 'r', encoding='utf-8') as _f:\n"
        "        exec(compile(_f.read(), r'%s', 'exec'))\n"
        "except Exception:\n"
        "    _nexus_success = False\n"
        "    traceback.print_exc()\n"
        "finally:\n"
        "    _nexus_elapsed = (time.perf_counter() - _nexus_start) * 1000.0\n"
        "    sys.stdout = _nexus_old_stdout\n"
        "    sys.stderr = _nexus_old_stderr\n"
        "    _nexus_out = _nexus_stdout.getvalue() + _nexus_stderr.getvalue()\n"
        "    _nexus_result = json.dumps({\n"
        "        'output': _nexus_out,\n"
        "        'return_value': '',\n"
        "        'success': _nexus_success,\n"
        "        'execution_time_ms': round(_nexus_elapsed, 2)\n"
        "    })\n"
        "    print('NEXUS_RESULT:' + _nexus_result)\n"
    ), *ArgsListStr, *NormalizedPath, *NormalizedPath);

    FPythonCommandEx PythonCommand;
    PythonCommand.Command = WrappedCode;
    PythonCommand.ExecutionMode = EPythonCommandExecutionMode::ExecuteStatement;
    PythonCommand.FileExecutionScope = EPythonFileExecutionScope::Private;

    bool bSuccess = PythonPlugin->ExecPythonCommandEx(PythonCommand);

    // Parse NEXUS_RESULT from log output
    TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());

    for (int32 i = PythonCommand.LogOutput.Num() - 1; i >= 0; --i)
    {
        const FString& Line = PythonCommand.LogOutput[i].Output;
        int32 MarkerIdx = Line.Find(TEXT("NEXUS_RESULT:"));
        if (MarkerIdx != INDEX_NONE)
        {
            FString ResultJson = Line.RightChop(MarkerIdx + 13);
            TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ResultJson);
            TSharedPtr<FJsonObject> ResultObj;
            if (FJsonSerializer::Deserialize(Reader, ResultObj) && ResultObj.IsValid())
            {
                // Add the file path to the result for reference
                ResultObj->SetStringField(TEXT("file_path"), FilePath);
                return MakeSuccess(ResultObj);
            }
            break;
        }
    }

    // Fallback
    FString Output;
    for (const FPythonLogOutputEntry& Entry : PythonCommand.LogOutput)
    {
        if (!Entry.Output.Contains(TEXT("NEXUS_RESULT:")))
        {
            if (!Output.IsEmpty()) Output += TEXT("\n");
            Output += Entry.Output;
        }
    }

    Data->SetStringField(TEXT("output"), Output);
    Data->SetStringField(TEXT("return_value"), TEXT(""));
    Data->SetBoolField(TEXT("success"), bSuccess);
    Data->SetNumberField(TEXT("execution_time_ms"), 0.0);
    Data->SetStringField(TEXT("file_path"), FilePath);

    return MakeSuccess(Data);
}

// ─────────────────────────────────────────────────────────────────────────────
// python.get_paths
// Query the embedded Python interpreter's path configuration
// Params: (none)
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FNexusPythonExecHandler::HandleGetPaths(
    const TSharedPtr<FJsonObject>& Params)
{
    IPythonScriptPlugin* PythonPlugin = GetPythonPlugin();
    if (!PythonPlugin)
    {
        return MakeError(TEXT("PYTHON_NOT_AVAILABLE"),
            TEXT("PythonScriptPlugin is not loaded. Enable it in the project's .uplugin or Editor Preferences."));
    }

    // Execute a Python script that gathers path info and prints JSON
    FString Code = TEXT(
        "import sys, json, os\n"
        "_paths = list(sys.path)\n"
        "_site_pkgs = [p for p in sys.path if 'site-packages' in p]\n"
        "_ver = f'{sys.version_info.major}.{sys.version_info.minor}.{sys.version_info.micro}'\n"
        "_unreal_path = ''\n"
        "try:\n"
        "    import unreal\n"
        "    _unreal_path = os.path.abspath(unreal.__file__) if hasattr(unreal, '__file__') else 'built-in'\n"
        "except ImportError:\n"
        "    _unreal_path = 'not available'\n"
        "_result = json.dumps({\n"
        "    'sys_path': _paths,\n"
        "    'site_packages': _site_pkgs,\n"
        "    'python_version': _ver,\n"
        "    'unreal_module_path': _unreal_path\n"
        "})\n"
        "print('NEXUS_RESULT:' + _result)\n"
    );

    FPythonCommandEx PythonCommand;
    PythonCommand.Command = Code;
    PythonCommand.ExecutionMode = EPythonCommandExecutionMode::ExecuteStatement;
    PythonCommand.FileExecutionScope = EPythonFileExecutionScope::Private;

    bool bSuccess = PythonPlugin->ExecPythonCommandEx(PythonCommand);

    // Parse NEXUS_RESULT from log output
    for (int32 i = PythonCommand.LogOutput.Num() - 1; i >= 0; --i)
    {
        const FString& Line = PythonCommand.LogOutput[i].Output;
        int32 MarkerIdx = Line.Find(TEXT("NEXUS_RESULT:"));
        if (MarkerIdx != INDEX_NONE)
        {
            FString ResultJson = Line.RightChop(MarkerIdx + 13);
            TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ResultJson);
            TSharedPtr<FJsonObject> ResultObj;
            if (FJsonSerializer::Deserialize(Reader, ResultObj) && ResultObj.IsValid())
            {
                return MakeSuccess(ResultObj);
            }
            break;
        }
    }

    // Fallback if parsing failed
    TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
    Data->SetStringField(TEXT("error_detail"),
        TEXT("Failed to parse Python path info. PythonScriptPlugin may be misconfigured."));

    // Still return basic info
    TArray<TSharedPtr<FJsonValue>> EmptyArr;
    Data->SetArrayField(TEXT("sys_path"), EmptyArr);
    Data->SetArrayField(TEXT("site_packages"), EmptyArr);
    Data->SetStringField(TEXT("python_version"), TEXT("unknown"));
    Data->SetStringField(TEXT("unreal_module_path"), TEXT("unknown"));

    if (!bSuccess)
    {
        return MakeError(TEXT("PYTHON_EXEC_FAILED"),
            TEXT("Failed to query Python paths. The Python environment may not be initialized."));
    }

    return MakeSuccess(Data);
}

// ─────────────────────────────────────────────────────────────────────────────
// Helper: Quote a string as a Python repr for embedding in exec()
// ─────────────────────────────────────────────────────────────────────────────

FString FNexusPythonExecHandler::QuoteCodeForPython(const FString& Code)
{
    // Use a triple-quoted raw-ish string to minimize escaping issues.
    // We need to handle the case where the code itself contains triple quotes.
    // Strategy: Use compile() with a base64-encoded version for safety.
    // Actually, simplest approach: escape backslashes and quotes, use triple quotes.

    FString Escaped = Code;
    Escaped = Escaped.Replace(TEXT("\\"), TEXT("\\\\"));
    Escaped = Escaped.Replace(TEXT("\"\"\""), TEXT("\\\"\\\"\\\""));

    return FString::Printf(TEXT("\"\"\"%s\"\"\""), *Escaped);
}
