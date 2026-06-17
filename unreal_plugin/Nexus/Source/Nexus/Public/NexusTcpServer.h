// Copyright Nexus Team. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "HAL/Runnable.h"
#include "IPAddress.h"

class FNexusCommandDispatcher;
class FSocket;

DECLARE_LOG_CATEGORY_EXTERN(LogNexus, Log, All);

/**
 * TCP server listening on port 13377 for incoming MCP commands.
 * Supports persistent connections with length-prefixed JSON framing.
 * Protocol: [4-byte big-endian uint32 length][JSON payload]
 * Commands are dispatched on the game thread with per-command timeout tiers.
 */
class FNexusTcpServer : public FRunnable
{
public:
	FNexusTcpServer(int32 InPort = 13377);
	virtual ~FNexusTcpServer();

	bool Start();
	void Stop();

	// FRunnable interface
	virtual bool Init() override;
	virtual uint32 Run() override;
	virtual void Exit() override;

private:
	/** Persistent connection loop: reads length-prefixed JSON, 30s idle timeout */
	void HandleClientConnection(FSocket* ClientSocket);

	/** Read exactly N bytes from socket, returns false on failure */
	bool RecvExact(FSocket* Socket, uint8* Buffer, int32 NumBytes, double TimeoutSeconds);

	/** Send a length-prefixed JSON response */
	bool SendLengthPrefixed(FSocket* Socket, const FString& JsonResponse);

	/** Dispatches a single command on the game thread with timeout */
	FString ProcessCommandWithTimeout(const FString& JsonCommand);

	/** Returns timeout in seconds based on command type tier */
	double GetCommandTimeout(const FString& CommandType) const;

	/** Build a JSON error response string */
	FString MakeErrorResponse(const FString& CommandId, const FString& Code, const FString& Message);

	/** Build a JSON success response string wrapping handler result */
	FString MakeSuccessResponse(const FString& CommandId, TSharedPtr<FJsonObject> ResultData);

	int32 Port;
	/** Atomic flag for thread-safe stop signaling */
	TAtomic<bool> bRunning;
	FRunnableThread* Thread;
	/** Raw listen socket - we manage accept ourselves (FTcpListener has internal thread conflicts) */
	FSocket* ListenSocket;
	/** Thread-safe because Dispatcher is created on game thread but accessed from TCP thread */
	TSharedPtr<FNexusCommandDispatcher, ESPMode::ThreadSafe> Dispatcher;
};
