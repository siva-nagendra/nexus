// Copyright Nexus Team. All Rights Reserved.

#include "NexusTcpServer.h"
#include "NexusCommandDispatcher.h"

#include "Common/TcpSocketBuilder.h"
#include "Sockets.h"
#include "SocketSubsystem.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

DEFINE_LOG_CATEGORY(LogNexus);

FNexusTcpServer::FNexusTcpServer(int32 InPort)
    : Port(InPort)
    , bRunning(false)
    , Thread(nullptr)
    , ListenSocket(nullptr)
{
    UE_LOG(LogNexus, Log, TEXT("Creating command dispatcher..."));
    Dispatcher = TSharedPtr<FNexusCommandDispatcher, ESPMode::ThreadSafe>(new FNexusCommandDispatcher());
    UE_LOG(LogNexus, Log, TEXT("Dispatcher created with %d commands"),
        Dispatcher.IsValid() ? Dispatcher->GetRegisteredCommands().Num() : -1);
}

FNexusTcpServer::~FNexusTcpServer()
{
    Stop();
}

bool FNexusTcpServer::Start()
{
    bRunning = true;
    Thread = FRunnableThread::Create(this, TEXT("NexusTcpServer"), 0, TPri_Normal);
    return Thread != nullptr;
}

void FNexusTcpServer::Stop()
{
    bRunning = false;
    if (ListenSocket)
    {
        ListenSocket->Close();
        ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->DestroySocket(ListenSocket);
        ListenSocket = nullptr;
    }
    if (Thread)
    {
        Thread->WaitForCompletion();
        delete Thread;
        Thread = nullptr;
    }
}

bool FNexusTcpServer::Init()
{
    return true;
}

uint32 FNexusTcpServer::Run()
{
    // Create raw listen socket (NOT FTcpListener which has its own accept thread)
    FIPv4Endpoint Endpoint(FIPv4Address(127, 0, 0, 1), Port);
    ListenSocket = FTcpSocketBuilder(TEXT("NexusTcpServer"))
        .AsReusable()
        .BoundToEndpoint(Endpoint)
        .Listening(8)
        .WithSendBufferSize(2 * 1024 * 1024);

    if (!ListenSocket)
    {
        UE_LOG(LogNexus, Error, TEXT("Failed to create TCP listen socket on port %d"), Port);
        return 1;
    }

    // Enable TCP_NODELAY on the listener
    ListenSocket->SetNoDelay(true);

    UE_LOG(LogNexus, Log, TEXT("Listening on 127.0.0.1:%d (length-prefixed protocol)"), Port);

    while (bRunning)
    {
        bool bHasPendingConnection = false;
        if (ListenSocket->WaitForPendingConnection(bHasPendingConnection, FTimespan::FromMilliseconds(100)))
        {
            if (bHasPendingConnection)
            {
                FSocket* ClientSocket = ListenSocket->Accept(TEXT("NexusClient"));
                if (ClientSocket)
                {
                    ClientSocket->SetNoDelay(true);
                    int32 BufferSize = 1048576;
                    ClientSocket->SetSendBufferSize(BufferSize, BufferSize);
                    ClientSocket->SetReceiveBufferSize(BufferSize, BufferSize);

                    HandleClientConnection(ClientSocket);

                    ClientSocket->Close();
                    ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->DestroySocket(ClientSocket);
                }
            }
        }
    }

    return 0;
}

void FNexusTcpServer::Exit()
{
    bRunning = false;
}

// ---------------------------------------------------------------------------
// Read exactly N bytes from socket
// ---------------------------------------------------------------------------

bool FNexusTcpServer::RecvExact(FSocket* Socket, uint8* Buffer, int32 NumBytes, double TimeoutSeconds)
{
    int32 TotalRead = 0;
    double StartTime = FPlatformTime::Seconds();

    while (TotalRead < NumBytes && bRunning)
    {
        if (FPlatformTime::Seconds() - StartTime > TimeoutSeconds)
        {
            UE_LOG(LogNexus, Warning, TEXT("RecvExact timeout after %.1fs (read %d/%d bytes)"),
                TimeoutSeconds, TotalRead, NumBytes);
            return false;
        }

        uint32 PendingDataSize = 0;
        if (!Socket->HasPendingData(PendingDataSize))
        {
            FPlatformProcess::Sleep(0.001f);
            continue;
        }

        int32 BytesRead = 0;
        if (!Socket->Recv(Buffer + TotalRead, NumBytes - TotalRead, BytesRead) || BytesRead <= 0)
        {
            UE_LOG(LogNexus, Log, TEXT("Client disconnected during RecvExact (read %d/%d)"),
                TotalRead, NumBytes);
            return false;
        }

        TotalRead += BytesRead;
    }

    return TotalRead == NumBytes;
}

// ---------------------------------------------------------------------------
// Send a length-prefixed JSON response
// ---------------------------------------------------------------------------

bool FNexusTcpServer::SendLengthPrefixed(FSocket* Socket, const FString& JsonResponse)
{
    // Convert to UTF-8
    FTCHARToUTF8 ResponseUtf8(*JsonResponse);
    int32 PayloadLen = ResponseUtf8.Length();

    // Send 4-byte big-endian length prefix
    uint8 LengthHeader[4];
    LengthHeader[0] = (PayloadLen >> 24) & 0xFF;
    LengthHeader[1] = (PayloadLen >> 16) & 0xFF;
    LengthHeader[2] = (PayloadLen >> 8) & 0xFF;
    LengthHeader[3] = PayloadLen & 0xFF;

    int32 BytesSent = 0;
    if (!Socket->Send(LengthHeader, 4, BytesSent) || BytesSent != 4)
    {
        UE_LOG(LogNexus, Warning, TEXT("Failed to send length prefix"));
        return false;
    }

    // Send JSON payload
    BytesSent = 0;
    if (!Socket->Send(reinterpret_cast<const uint8*>(ResponseUtf8.Get()), PayloadLen, BytesSent) || BytesSent != PayloadLen)
    {
        UE_LOG(LogNexus, Warning, TEXT("Failed to send response payload (%d/%d bytes)"), BytesSent, PayloadLen);
        return false;
    }

    return true;
}

// ---------------------------------------------------------------------------
// Persistent connection handler (length-prefixed protocol)
// ---------------------------------------------------------------------------

void FNexusTcpServer::HandleClientConnection(FSocket* ClientSocket)
{
    if (!ClientSocket) return;

    const double IdleTimeoutSeconds = 30.0;
    double LastActivityTime = FPlatformTime::Seconds();

    UE_LOG(LogNexus, Log, TEXT("Client connected, entering persistent loop (length-prefixed)"));

    while (bRunning)
    {
        // Check idle timeout
        if (FPlatformTime::Seconds() - LastActivityTime > IdleTimeoutSeconds)
        {
            UE_LOG(LogNexus, Log, TEXT("Client idle timeout after %.0fs"), IdleTimeoutSeconds);
            break;
        }

        // Check if any data is available
        uint32 PendingDataSize = 0;
        if (!ClientSocket->HasPendingData(PendingDataSize))
        {
            FPlatformProcess::Sleep(0.001f);
            continue;
        }

        // Read 4-byte length prefix
        uint8 LengthHeader[4];
        if (!RecvExact(ClientSocket, LengthHeader, 4, IdleTimeoutSeconds))
        {
            UE_LOG(LogNexus, Log, TEXT("Failed to read length prefix, closing connection"));
            break;
        }

        // Decode big-endian uint32 length
        uint32 PayloadLen = (static_cast<uint32>(LengthHeader[0]) << 24)
                          | (static_cast<uint32>(LengthHeader[1]) << 16)
                          | (static_cast<uint32>(LengthHeader[2]) << 8)
                          | static_cast<uint32>(LengthHeader[3]);

        // Sanity check: reject payloads > 100MB
        if (PayloadLen > 100 * 1024 * 1024)
        {
            UE_LOG(LogNexus, Error, TEXT("Payload too large: %u bytes, closing connection"), PayloadLen);
            break;
        }

        LastActivityTime = FPlatformTime::Seconds();

        // Read exact payload
        TArray<uint8> PayloadBuffer;
        PayloadBuffer.SetNumZeroed(PayloadLen);
        if (!RecvExact(ClientSocket, PayloadBuffer.GetData(), PayloadLen, 60.0))
        {
            UE_LOG(LogNexus, Log, TEXT("Failed to read payload (%u bytes), closing connection"), PayloadLen);
            break;
        }

        // Convert to FString
        FUTF8ToTCHAR Converter(reinterpret_cast<const ANSICHAR*>(PayloadBuffer.GetData()), PayloadLen);
        FString SingleCommand(Converter.Length(), Converter.Get());

        if (SingleCommand.TrimStartAndEnd().IsEmpty())
            continue;

        UE_LOG(LogNexus, Log, TEXT("Processing command: %.120s%s"),
            *SingleCommand, SingleCommand.Len() > 120 ? TEXT("...") : TEXT(""));

        // Process command on game thread with timeout
        FString Response;
        try
        {
            Response = ProcessCommandWithTimeout(SingleCommand);
        }
        catch (...)
        {
            UE_LOG(LogNexus, Error, TEXT("ProcessCommandWithTimeout crashed for: %.80s"), *SingleCommand);
            Response = MakeErrorResponse(TEXT(""), TEXT("CRASH"), TEXT("Server crash processing command"));
        }

        UE_LOG(LogNexus, Verbose, TEXT("Sending response (%d chars)"), Response.Len());

        // Send length-prefixed response
        if (!SendLengthPrefixed(ClientSocket, Response))
        {
            UE_LOG(LogNexus, Warning, TEXT("Failed to send response, closing connection"));
            return;
        }
    }

    UE_LOG(LogNexus, Log, TEXT("Client connection closed"));
}

// ---------------------------------------------------------------------------
// Command processing with timeout
// ---------------------------------------------------------------------------

FString FNexusTcpServer::ProcessCommandWithTimeout(const FString& CommandJson)
{
    UE_LOG(LogNexus, Log, TEXT("ProcessCommand: parsing JSON..."));

    // Parse JSON
    TSharedPtr<FJsonObject> Request;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(CommandJson);
    if (!FJsonSerializer::Deserialize(Reader, Request) || !Request.IsValid())
    {
        UE_LOG(LogNexus, Warning, TEXT("ProcessCommand: JSON parse failed"));
        return MakeErrorResponse(TEXT(""), TEXT("PARSE_ERROR"), TEXT("Invalid JSON command"));
    }

    FString CommandId;
    Request->TryGetStringField(TEXT("id"), CommandId);
    FString CommandType;
    Request->TryGetStringField(TEXT("type"), CommandType);

    UE_LOG(LogNexus, Log, TEXT("ProcessCommand: id=%s type=%s"), *CommandId, *CommandType);

    const TSharedPtr<FJsonObject>* ParamsPtr = nullptr;
    Request->TryGetObjectField(TEXT("params"), ParamsPtr);
    TSharedPtr<FJsonObject> Params = ParamsPtr ? *ParamsPtr : MakeShareable(new FJsonObject());

    // Determine timeout tier based on command type
    double TimeoutSeconds = GetCommandTimeout(CommandType);

    // Dispatch on game thread
    TSharedPtr<FJsonObject> ResultData;
    FString ErrorMessage;

    // Copy values for safe capture (avoid dangling references)
    FString CapturedCommandType = CommandType;
    TSharedPtr<FJsonObject> CapturedParams = Params;
    TSharedPtr<FNexusCommandDispatcher, ESPMode::ThreadSafe> CapturedDispatcher = Dispatcher;

    UE_LOG(LogNexus, Log, TEXT("ProcessCommand: dispatching to game thread..."));

    FGraphEventRef Task = FFunctionGraphTask::CreateAndDispatchWhenReady(
        [CapturedDispatcher, CapturedCommandType, CapturedParams, &ResultData, &ErrorMessage]()
        {
            UE_LOG(LogNexus, Log, TEXT("ProcessCommand: [GameThread] executing %s"), *CapturedCommandType);
            try
            {
                if (!CapturedDispatcher.IsValid())
                {
                    ErrorMessage = TEXT("Dispatcher is null");
                    UE_LOG(LogNexus, Error, TEXT("ProcessCommand: Dispatcher is null!"));
                    return;
                }
                ResultData = CapturedDispatcher->Dispatch(CapturedCommandType, CapturedParams);
                UE_LOG(LogNexus, Log, TEXT("ProcessCommand: [GameThread] dispatch completed"));
            }
            catch (...)
            {
                ErrorMessage = FString::Printf(
                    TEXT("Handler crashed for command '%s'"), *CapturedCommandType);
                UE_LOG(LogNexus, Error, TEXT("ProcessCommand: [GameThread] %s"), *ErrorMessage);
            }
        },
        TStatId(),
        nullptr,
        ENamedThreads::GameThread
    );

    UE_LOG(LogNexus, Log, TEXT("ProcessCommand: waiting for game thread (timeout=%.0fs)..."), TimeoutSeconds);

    // Wait with timeout
    double StartTime = FPlatformTime::Seconds();
    bool bTimedOut = false;
    while (!Task->IsComplete())
    {
        if (FPlatformTime::Seconds() - StartTime > TimeoutSeconds)
        {
            bTimedOut = true;
            break;
        }
        FPlatformProcess::Sleep(0.01f); // 10ms poll
    }

    if (bTimedOut)
    {
        UE_LOG(LogNexus, Warning, TEXT("ProcessCommand: TIMEOUT for %s"), *CommandType);
        return MakeErrorResponse(CommandId, TEXT("TIMEOUT"),
            FString::Printf(TEXT("Command '%s' timed out after %.0fs"),
                *CommandType, TimeoutSeconds));
    }

    if (!ErrorMessage.IsEmpty())
    {
        UE_LOG(LogNexus, Error, TEXT("ProcessCommand: error: %s"), *ErrorMessage);
        return MakeErrorResponse(CommandId, TEXT("HANDLER_CRASH"), ErrorMessage);
    }

    UE_LOG(LogNexus, Log, TEXT("ProcessCommand: building response for %s"), *CommandType);
    return MakeSuccessResponse(CommandId, ResultData);
}

// ---------------------------------------------------------------------------
// Timeout tier classification
// ---------------------------------------------------------------------------

double FNexusTcpServer::GetCommandTimeout(const FString& CommandType) const
{
    // Long-running operations: 300s (5 min)
    static const TSet<FString> LongRunning = {
        TEXT("mrq.render_queue"),
        TEXT("lighting.bake_lighting"),
        TEXT("landscape.create_landscape"),
        TEXT("landscape.import_heightmap"),
        TEXT("landscape.export_heightmap"),
        TEXT("sequencer.export_sequence"),
        TEXT("pcg.execute_pcg_graph")
    };

    if (LongRunning.Contains(CommandType))
    {
        return 300.0;
    }

    // Read-only query prefixes: 10s
    static const TArray<FString> ReadOnlyPrefixes = {
        TEXT("get_"), TEXT("list_"), TEXT("find_"), TEXT("info"),
        TEXT("exists"), TEXT("search"), TEXT("is_")
    };

    // Extract the action part after the namespace dot
    FString Action = CommandType;
    int32 DotIndex;
    if (Action.FindChar(TEXT('.'), DotIndex))
    {
        Action = Action.Mid(DotIndex + 1);
    }

    for (const FString& Prefix : ReadOnlyPrefixes)
    {
        if (Action.StartsWith(Prefix))
        {
            return 10.0;
        }
    }

    // Default: mutations get 30s
    return 30.0;
}

// ---------------------------------------------------------------------------
// Response helpers
// ---------------------------------------------------------------------------

FString FNexusTcpServer::MakeErrorResponse(const FString& CommandId, const FString& Code, const FString& Message)
{
    TSharedPtr<FJsonObject> Response = MakeShareable(new FJsonObject());
    Response->SetStringField(TEXT("id"), CommandId);
    Response->SetBoolField(TEXT("success"), false);

    TSharedPtr<FJsonObject> ErrorDetail = MakeShareable(new FJsonObject());
    ErrorDetail->SetStringField(TEXT("code"), Code);
    ErrorDetail->SetStringField(TEXT("message"), Message);
    Response->SetObjectField(TEXT("error"), ErrorDetail);

    FString Output;
    TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
        TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Output);
    FJsonSerializer::Serialize(Response.ToSharedRef(), Writer);
    return Output;
}

FString FNexusTcpServer::MakeSuccessResponse(const FString& CommandId, TSharedPtr<FJsonObject> ResultData)
{
    TSharedPtr<FJsonObject> Response = MakeShareable(new FJsonObject());
    Response->SetStringField(TEXT("id"), CommandId);

    if (ResultData.IsValid())
    {
        bool bSuccess = true;
        ResultData->TryGetBoolField(TEXT("success"), bSuccess);
        Response->SetBoolField(TEXT("success"), bSuccess);

        if (bSuccess)
        {
            const TSharedPtr<FJsonObject>* Data;
            if (ResultData->TryGetObjectField(TEXT("data"), Data))
            {
                Response->SetObjectField(TEXT("data"), *Data);
            }
            else
            {
                Response->SetObjectField(TEXT("data"), ResultData);
            }
        }
        else
        {
            const TSharedPtr<FJsonObject>* Error;
            if (ResultData->TryGetObjectField(TEXT("error"), Error))
            {
                Response->SetObjectField(TEXT("error"), *Error);
            }
        }
    }
    else
    {
        Response->SetBoolField(TEXT("success"), false);
        TSharedPtr<FJsonObject> ErrorDetail = MakeShareable(new FJsonObject());
        ErrorDetail->SetStringField(TEXT("code"), TEXT("INTERNAL_ERROR"));
        ErrorDetail->SetStringField(TEXT("message"), TEXT("Handler returned null"));
        Response->SetObjectField(TEXT("error"), ErrorDetail);
    }

    FString Output;
    TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
        TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Output);
    FJsonSerializer::Serialize(Response.ToSharedRef(), Writer);
    return Output;
}
