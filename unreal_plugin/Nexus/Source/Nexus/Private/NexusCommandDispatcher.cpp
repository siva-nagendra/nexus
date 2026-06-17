// Copyright Nexus Team. All Rights Reserved.

#include "NexusCommandDispatcher.h"
#include "NexusCommandHandler.h"

// Include all 25 handlers
#include "Handlers/NexusActorHandler.h"
#include "Handlers/NexusAssetHandler.h"
#include "Handlers/NexusBlueprintHandler.h"
#include "Handlers/NexusEditorHandler.h"
#include "Handlers/NexusMaterialHandler.h"
#include "Handlers/NexusLevelHandler.h"
#include "Handlers/NexusAnimationHandler.h"
#include "Handlers/NexusSequencerHandler.h"
#include "Handlers/NexusPhysicsHandler.h"
#include "Handlers/NexusAIHandler.h"
#include "Handlers/NexusNiagaraHandler.h"
#include "Handlers/NexusUIHandler.h"
#include "Handlers/NexusMRQHandler.h"
#include "Handlers/NexusRenderingHandler.h"
#include "Handlers/NexusLightingHandler.h"
#include "Handlers/NexusAudioHandler.h"
#include "Handlers/NexusLandscapeHandler.h"
#include "Handlers/NexusPCGHandler.h"
#include "Handlers/NexusEnhancedInputHandler.h"
#include "Handlers/NexusNetworkingHandler.h"
#include "Handlers/NexusGameFeaturesHandler.h"
#include "Handlers/NexusSourceControlHandler.h"
#include "Handlers/NexusCodeAnalysisHandler.h"
#include "Handlers/NexusProfilingHandler.h"
#include "Handlers/NexusPythonExecHandler.h"

FNexusCommandDispatcher::FNexusCommandDispatcher()
{
    RegisterDefaultHandlers();
}

FNexusCommandDispatcher::~FNexusCommandDispatcher()
{
    HandlerMap.Empty();
    Handlers.Empty();
}

void FNexusCommandDispatcher::RegisterDefaultHandlers()
{
    // Original 6 handlers
    RegisterHandler(MakeShareable(new FNexusActorHandler()));
    RegisterHandler(MakeShareable(new FNexusAssetHandler()));
    RegisterHandler(MakeShareable(new FNexusBlueprintHandler()));
    RegisterHandler(MakeShareable(new FNexusEditorHandler()));
    RegisterHandler(MakeShareable(new FNexusMaterialHandler()));
    RegisterHandler(MakeShareable(new FNexusLevelHandler()));

    // 19 new handlers
    RegisterHandler(MakeShareable(new FNexusAnimationHandler()));
    RegisterHandler(MakeShareable(new FNexusSequencerHandler()));
    RegisterHandler(MakeShareable(new FNexusPhysicsHandler()));
    RegisterHandler(MakeShareable(new FNexusAIHandler()));
    RegisterHandler(MakeShareable(new FNexusNiagaraHandler()));
    RegisterHandler(MakeShareable(new FNexusUIHandler()));
    RegisterHandler(MakeShareable(new FNexusMRQHandler()));
    RegisterHandler(MakeShareable(new FNexusRenderingHandler()));
    RegisterHandler(MakeShareable(new FNexusLightingHandler()));
    RegisterHandler(MakeShareable(new FNexusAudioHandler()));
    RegisterHandler(MakeShareable(new FNexusLandscapeHandler()));
    RegisterHandler(MakeShareable(new FNexusPCGHandler()));
    RegisterHandler(MakeShareable(new FNexusEnhancedInputHandler()));
    RegisterHandler(MakeShareable(new FNexusNetworkingHandler()));
    RegisterHandler(MakeShareable(new FNexusGameFeaturesHandler()));
    RegisterHandler(MakeShareable(new FNexusSourceControlHandler()));
    RegisterHandler(MakeShareable(new FNexusCodeAnalysisHandler()));
    RegisterHandler(MakeShareable(new FNexusProfilingHandler()));
    RegisterHandler(MakeShareable(new FNexusPythonExecHandler()));
}

void FNexusCommandDispatcher::RegisterHandler(TSharedPtr<INexusCommandHandler> Handler)
{
    if (!Handler.IsValid()) return;

    Handlers.Add(Handler);

    FString Namespace = Handler->GetNamespace();
    for (const FString& Cmd : Handler->GetSupportedCommands())
    {
        FString FullCommand = Namespace + TEXT(".") + Cmd;
        HandlerMap.Add(FullCommand, Handler);
        UE_LOG(LogTemp, Verbose, TEXT("Nexus: Registered command '%s'"), *FullCommand);
    }
}

TSharedPtr<FJsonObject> FNexusCommandDispatcher::Dispatch(
    const FString& CommandType,
    const TSharedPtr<FJsonObject>& Params)
{
    // Built-in system commands (not routed through handlers)
    if (CommandType == TEXT("system.list_commands"))
    {
        TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
        TArray<TSharedPtr<FJsonValue>> HandlerList;

        // Iterate registered handlers to build namespace -> commands listing
        for (const TSharedPtr<INexusCommandHandler>& Handler : Handlers)
        {
            TSharedPtr<FJsonObject> HandlerInfo = MakeShareable(new FJsonObject());
            HandlerInfo->SetStringField(TEXT("namespace"), Handler->GetNamespace());

            TArray<TSharedPtr<FJsonValue>> Commands;
            for (const FString& Cmd : Handler->GetSupportedCommands())
            {
                Commands.Add(MakeShareable(new FJsonValueString(Cmd)));
            }
            HandlerInfo->SetArrayField(TEXT("commands"), Commands);
            HandlerList.Add(MakeShareable(new FJsonValueObject(HandlerInfo)));
        }

        Data->SetArrayField(TEXT("handlers"), HandlerList);
        Data->SetNumberField(TEXT("total_commands"), HandlerMap.Num());

        TSharedPtr<FJsonObject> Response = MakeShareable(new FJsonObject());
        Response->SetBoolField(TEXT("success"), true);
        Response->SetObjectField(TEXT("data"), Data);
        return Response;
    }

    if (CommandType == TEXT("system.echo"))
    {
        TSharedPtr<FJsonObject> Response = MakeShareable(new FJsonObject());
        Response->SetBoolField(TEXT("success"), true);
        Response->SetObjectField(TEXT("data"), Params.IsValid() ? Params : MakeShareable(new FJsonObject()));
        return Response;
    }

    if (CommandType == TEXT("system.version"))
    {
        TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
        Data->SetStringField(TEXT("plugin_version"), TEXT("3.0.0"));
        Data->SetStringField(TEXT("engine"), TEXT("UnrealEngine"));
        Data->SetNumberField(TEXT("handler_count"), Handlers.Num());

        TSharedPtr<FJsonObject> Response = MakeShareable(new FJsonObject());
        Response->SetBoolField(TEXT("success"), true);
        Response->SetObjectField(TEXT("data"), Data);
        return Response;
    }

    TSharedPtr<INexusCommandHandler>* FoundHandler = HandlerMap.Find(CommandType);
    if (FoundHandler && FoundHandler->IsValid())
    {
        try
        {
            TSharedPtr<FJsonObject> Result = (*FoundHandler)->Handle(CommandType, Params);
            if (!Result.IsValid())
            {
                UE_LOG(LogTemp, Error, TEXT("Nexus: Handler returned null for '%s'"), *CommandType);
                return MakeError(TEXT("INTERNAL_ERROR"),
                    FString::Printf(TEXT("Handler returned null for command '%s'"), *CommandType));
            }
            return Result;
        }
        catch (const std::exception& Ex)
        {
            FString What = FString(ANSI_TO_TCHAR(Ex.what()));
            UE_LOG(LogTemp, Error, TEXT("Nexus: Handler crashed for '%s': %s"), *CommandType, *What);
            return MakeError(TEXT("HANDLER_CRASH"),
                FString::Printf(TEXT("Handler exception for '%s': %s"), *CommandType, *What));
        }
        catch (...)
        {
            UE_LOG(LogTemp, Error, TEXT("Nexus: Handler crashed for '%s' (unknown exception)"), *CommandType);
            return MakeError(TEXT("HANDLER_CRASH"),
                FString::Printf(TEXT("Unknown exception in handler for '%s'"), *CommandType));
        }
    }

    return MakeError(TEXT("UNKNOWN_COMMAND"),
        FString::Printf(TEXT("No handler registered for '%s'. Use 'system.list_commands' to see available commands."), *CommandType));
}

TArray<FString> FNexusCommandDispatcher::GetRegisteredCommands() const
{
    TArray<FString> Commands;
    HandlerMap.GetKeys(Commands);
    Commands.Sort();
    return Commands;
}

TSharedPtr<FJsonObject> FNexusCommandDispatcher::MakeError(const FString& Code, const FString& Message)
{
    TSharedPtr<FJsonObject> ErrorObj = MakeShareable(new FJsonObject());
    ErrorObj->SetStringField(TEXT("code"), Code);
    ErrorObj->SetStringField(TEXT("message"), Message);

    TSharedPtr<FJsonObject> Response = MakeShareable(new FJsonObject());
    Response->SetBoolField(TEXT("success"), false);
    Response->SetObjectField(TEXT("error"), ErrorObj);
    return Response;
}
