// Copyright Nexus Team. All Rights Reserved.

#include "ClaudeShellRelay.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/SecureHash.h"
#include "Serialization/JsonSerializer.h"
#include "SocketSubsystem.h"
#include "Sockets.h"
#include "TimerManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogClaudeShellRelay, Log, All);

// --------------------------------------------------------------------
// Construction / Destruction
// --------------------------------------------------------------------

FClaudeShellRelay::FClaudeShellRelay() = default;

FClaudeShellRelay::~FClaudeShellRelay()
{
	Stop();
}

// --------------------------------------------------------------------
// Stale relay cleanup
// --------------------------------------------------------------------

void FClaudeShellRelay::KillStaleRelay(int32 Port)
{
	// The relay writes a state file: <LocalAppData>/claudeshell/relay/relay-<port>.json
	// containing {"pid": <int>, "parent_pid": <int>, ...}.
	// Kill the relay if:
	//   1. Its parent_pid is no longer running (orphaned relay)
	//   2. Its parent_pid is not our PID (belongs to a different UE instance that died)
	//   3. We can't determine ownership — kill it to be safe
	FString LocalAppData = FPlatformMisc::GetEnvironmentVariable(TEXT("LOCALAPPDATA"));
	if (LocalAppData.IsEmpty())
	{
		LocalAppData = FPaths::Combine(FPlatformProcess::UserHomeDir(), TEXT("AppData/Local"));
	}

	FString StateFile = FPaths::Combine(LocalAppData, FString::Printf(TEXT("claudeshell/relay/relay-%d.json"), Port));
	FString Contents;
	if (!FFileHelper::LoadFileToString(Contents, *StateFile))
	{
		UE_LOG(LogClaudeShellRelay, Log, TEXT("No state file for port %d — nothing to clean up"), Port);
		return;
	}

	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Contents);
	TSharedPtr<FJsonObject> JsonObj;
	if (!FJsonSerializer::Deserialize(Reader, JsonObj) || !JsonObj.IsValid())
	{
		IFileManager::Get().Delete(*StateFile);
		return;
	}

	int32 StalePid = JsonObj->HasField(TEXT("pid"))
		? static_cast<int32>(JsonObj->GetNumberField(TEXT("pid")))
		: 0;
	int32 StaleParentPid = JsonObj->HasField(TEXT("parent_pid"))
		? static_cast<int32>(JsonObj->GetNumberField(TEXT("parent_pid")))
		: 0;

	if (StalePid <= 0)
	{
		IFileManager::Get().Delete(*StateFile);
		return;
	}

	int32 MyPid = static_cast<int32>(FPlatformProcess::GetCurrentProcessId());

	// Don't kill our own relay
	if (StaleParentPid == MyPid)
	{
		UE_LOG(LogClaudeShellRelay, Log, TEXT("Relay on port %d belongs to us (parent=%d) — keeping"), Port, MyPid);
		return;
	}

	// Check if the relay's parent is still alive
	bool bParentAlive = false;
	if (StaleParentPid > 0)
	{
		FProcHandle ParentHandle = FPlatformProcess::OpenProcess(StaleParentPid);
		if (ParentHandle.IsValid())
		{
			bParentAlive = FPlatformProcess::IsProcRunning(ParentHandle);
			FPlatformProcess::CloseProc(ParentHandle);
		}
	}

	if (bParentAlive)
	{
		// Another UE instance owns this relay — don't kill it
		UE_LOG(LogClaudeShellRelay, Warning,
			TEXT("Relay on port %d owned by another UE (PID %d, parent %d still alive) — skipping"),
			Port, StalePid, StaleParentPid);
		return;
	}

	// Parent is dead or unknown → this relay is orphaned, kill it
	FProcHandle StaleHandle = FPlatformProcess::OpenProcess(StalePid);
	if (StaleHandle.IsValid())
	{
		if (FPlatformProcess::IsProcRunning(StaleHandle))
		{
			UE_LOG(LogClaudeShellRelay, Log,
				TEXT("Killing orphaned relay (PID: %d, dead parent: %d) on port %d"),
				StalePid, StaleParentPid, Port);
			FPlatformProcess::TerminateProc(StaleHandle, true);
			FPlatformProcess::Sleep(0.5f);  // Give it time to release the port
		}
		FPlatformProcess::CloseProc(StaleHandle);
	}

	// Clean up the state file
	IFileManager::Get().Delete(*StateFile);
}

// --------------------------------------------------------------------
// Start / Stop
// --------------------------------------------------------------------

bool FClaudeShellRelay::Start(
	const FString& UvPath,
	const FString& PkgDir,
	int32 Port,
	const FString& ProjectDir,
	const FString& WebDir,
	const FString& DocsDir,
	const FString& McpConfig,
	int32 ParentPid)
{
	// Cache params for potential restart
	CachedUvPath = UvPath;
	CachedPkgDir = PkgDir;
	RelayPort = Port;
	CachedProjectDir = ProjectDir;
	CachedWebDir = WebDir;
	CachedDocsDir = DocsDir;
	CachedMcpConfig = McpConfig;
	CachedParentPid = ParentPid;

	// Kill any stale relay process on this port (from a previous UE session).
	// The relay writes a state file with its PID; read it and terminate if still alive.
	KillStaleRelay(Port);

	// Build command line:
	//   uv run --project <pkg-dir> -m claudeshell --port <port> --cwd <dir> ...
	FString Args = FString::Printf(
		TEXT("run --project \"%s\" -m claudeshell --port %d --cwd \"%s\""),
		*PkgDir, Port, *ProjectDir);

	if (!WebDir.IsEmpty())
	{
		Args += FString::Printf(TEXT(" --web-dir \"%s\""), *WebDir);
	}
	if (!DocsDir.IsEmpty())
	{
		Args += FString::Printf(TEXT(" --docs-dir \"%s\""), *DocsDir);
	}
	if (!McpConfig.IsEmpty())
	{
		// Escape the JSON string for command line (single quotes won't work on Windows)
		FString EscapedConfig = McpConfig.Replace(TEXT("\""), TEXT("\\\""));
		Args += FString::Printf(TEXT(" --mcp-config \"%s\""), *EscapedConfig);
	}
	if (ParentPid > 0)
	{
		Args += FString::Printf(TEXT(" --parent-pid %d"), ParentPid);
	}

	// Create subprocess with stdout pipe to capture the token
	void* ReadPipe = nullptr;
	void* WritePipe = nullptr;
	FPlatformProcess::CreatePipe(ReadPipe, WritePipe);

	uint32 ProcessId = 0;
	ProcessHandle = FPlatformProcess::CreateProc(
		*UvPath,
		*Args,
		false,   // bLaunchDetached
		true,    // bLaunchHidden
		true,    // bLaunchReallyHidden
		&ProcessId,
		0,       // PriorityModifier
		nullptr, // WorkingDirectory
		WritePipe,
		nullptr  // PipeReadChild
	);

	if (!ProcessHandle.IsValid())
	{
		UE_LOG(LogClaudeShellRelay, Error,
			TEXT("Failed to launch relay process (uv: %s)"), *UvPath);
		FPlatformProcess::ClosePipe(ReadPipe, WritePipe);
		return false;
	}

	UE_LOG(LogClaudeShellRelay, Log,
		TEXT("Relay process launched (PID: %u, Port: %d)"), ProcessId, Port);

	// Read stdout for up to 10 seconds to capture the token line
	// The relay prints: [claudeshell] Token: <uuid>
	FString CapturedOutput;
	double StartTime = FPlatformTime::Seconds();
	const double TimeoutSec = 10.0;

	while (FPlatformTime::Seconds() - StartTime < TimeoutSec)
	{
		FString PipeOutput = FPlatformProcess::ReadPipe(ReadPipe);
		if (!PipeOutput.IsEmpty())
		{
			CapturedOutput += PipeOutput;

			// Check if we got the token line
			Token = ParseTokenFromOutput(CapturedOutput);
			if (!Token.IsEmpty())
			{
				break;
			}
		}

		if (!FPlatformProcess::IsProcRunning(ProcessHandle))
		{
			UE_LOG(LogClaudeShellRelay, Error, TEXT("Relay process exited during startup"));
			break;
		}

		FPlatformProcess::Sleep(0.1f);
	}

	// Close only the read end — we're done reading.
	// The write pipe is the child's stdout handle; closing it kills the child process.
	// Store WritePipe so it stays alive until Stop().
	FPlatformProcess::ClosePipe(ReadPipe, nullptr);
	StdoutWritePipe = WritePipe;

	if (Token.IsEmpty())
	{
		UE_LOG(LogClaudeShellRelay, Warning,
			TEXT("Could not capture relay token from stdout. Output: %s"), *CapturedOutput);
	}
	else
	{
		UE_LOG(LogClaudeShellRelay, Log, TEXT("Relay token captured: %s..."),
			*Token.Left(8));
	}

	// Wait for the relay HTTP server to be ready (token is printed before
	// asyncio.run starts the server, so we must poll until it accepts connections)
	if (ProcessHandle.IsValid() && !Token.IsEmpty())
	{
		UE_LOG(LogClaudeShellRelay, Log, TEXT("Waiting for relay to accept connections..."));
		double ReadyStart = FPlatformTime::Seconds();
		const double ReadyTimeout = 15.0;  // up to 15s for first-run uv install
		bool bReady = false;

		while (FPlatformTime::Seconds() - ReadyStart < ReadyTimeout)
		{
			if (!FPlatformProcess::IsProcRunning(ProcessHandle))
			{
				UE_LOG(LogClaudeShellRelay, Error, TEXT("Relay process died while waiting for ready"));
				break;
			}

			if (HealthCheck())
			{
				bReady = true;
				break;
			}

			FPlatformProcess::Sleep(0.5f);
		}

		if (bReady)
		{
			UE_LOG(LogClaudeShellRelay, Log, TEXT("Relay ready (took %.1fs)"),
				FPlatformTime::Seconds() - ReadyStart);
		}
		else
		{
			UE_LOG(LogClaudeShellRelay, Warning,
				TEXT("Relay not responding after %.1fs — proceeding anyway"),
				FPlatformTime::Seconds() - ReadyStart);
		}
	}

	return ProcessHandle.IsValid();
}

void FClaudeShellRelay::Stop()
{
	StopHealthWatchdog();

	if (ProcessHandle.IsValid() && FPlatformProcess::IsProcRunning(ProcessHandle))
	{
		UE_LOG(LogClaudeShellRelay, Log, TEXT("Stopping relay process..."));
		FPlatformProcess::TerminateProc(ProcessHandle, true);
	}
	ProcessHandle.Reset();
	Token.Empty();
	ConsecutiveFailures = 0;

	// Now safe to close the child's stdout pipe
	if (StdoutWritePipe)
	{
		FPlatformProcess::ClosePipe(nullptr, StdoutWritePipe);
		StdoutWritePipe = nullptr;
	}
}

bool FClaudeShellRelay::IsAlive() const
{
	return ProcessHandle.IsValid()
		&& FPlatformProcess::IsProcRunning(ProcessHandle)
		&& HealthCheck();
}

// --------------------------------------------------------------------
// URL encoding helper
// --------------------------------------------------------------------

static FString UrlEncode(const FString& Value)
{
	FString Encoded;
	auto Utf8 = FTCHARToUTF8(*Value);
	const char* Ptr = Utf8.Get();
	int32 Len = Utf8.Length();

	for (int32 i = 0; i < Len; ++i)
	{
		uint8 Ch = static_cast<uint8>(Ptr[i]);
		if ((Ch >= 'A' && Ch <= 'Z') || (Ch >= 'a' && Ch <= 'z') ||
			(Ch >= '0' && Ch <= '9') || Ch == '-' || Ch == '_' || Ch == '.' || Ch == '~')
		{
			Encoded.AppendChar(static_cast<TCHAR>(Ch));
		}
		else
		{
			Encoded += FString::Printf(TEXT("%%%02X"), Ch);
		}
	}
	return Encoded;
}

// --------------------------------------------------------------------
// Session management via REST API
// --------------------------------------------------------------------

FString FClaudeShellRelay::CreateSession(const FString& Cwd, const FString& ProjectName)
{
	// All API endpoints use GET with query params (websockets library rejects POST)
	FString Path = FString::Printf(
		TEXT("/api/session/create?token=%s&cwd=%s&project_name=%s"),
		*UrlEncode(Token),
		*UrlEncode(Cwd),
		*UrlEncode(ProjectName));

	FString ResponseBody;
	int32 StatusCode = HttpRequest(TEXT("GET"), Path, FString(), ResponseBody);

	if (StatusCode != 200)
	{
		UE_LOG(LogClaudeShellRelay, Error,
			TEXT("CreateSession failed (HTTP %d): %s"), StatusCode, *ResponseBody);
		return FString();
	}

	// Parse session_id from JSON response
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ResponseBody);
	TSharedPtr<FJsonObject> JsonObj;
	if (FJsonSerializer::Deserialize(Reader, JsonObj) && JsonObj.IsValid())
	{
		return JsonObj->GetStringField(TEXT("session_id"));
	}

	UE_LOG(LogClaudeShellRelay, Error, TEXT("CreateSession: failed to parse response"));
	return FString();
}

bool FClaudeShellRelay::DestroySession(const FString& SessionId)
{
	FString Path = FString::Printf(TEXT("/api/session/%s/shutdown?token=%s"), *SessionId, *UrlEncode(Token));
	FString ResponseBody;
	int32 StatusCode = HttpRequest(TEXT("GET"), Path, FString(), ResponseBody);

	if (StatusCode == 200)
	{
		UE_LOG(LogClaudeShellRelay, Log, TEXT("Session %s destroyed"), *SessionId);
		return true;
	}

	UE_LOG(LogClaudeShellRelay, Warning,
		TEXT("DestroySession %s failed (HTTP %d): %s"), *SessionId, StatusCode, *ResponseBody);
	return false;
}

// --------------------------------------------------------------------
// Port computation
// --------------------------------------------------------------------

int32 FClaudeShellRelay::ComputeProjectPort(const FString& ProjectDir)
{
	// Same algorithm as Python: MD5(normalized lowercase path) → port in range
	FString Normalized = ProjectDir.ToLower().Replace(TEXT("\\"), TEXT("/"));
	FMD5 Md5;
	auto Utf8 = FTCHARToUTF8(*Normalized);
	Md5.Update(reinterpret_cast<const uint8*>(Utf8.Get()), Utf8.Length());

	uint8 Digest[16];
	Md5.Final(Digest);

	uint16 BaseOffset = *reinterpret_cast<uint16*>(Digest) % PORT_RANGE;
	return PORT_BASE + BaseOffset;
}

// --------------------------------------------------------------------
// Health watchdog
// --------------------------------------------------------------------

void FClaudeShellRelay::StartHealthWatchdog()
{
	// Use a game thread timer if GEngine is available, otherwise skip
	if (!GEngine)
	{
		return;
	}

	UWorld* World = GEngine->GetCurrentPlayWorld();
	if (!World)
	{
		// In editor, try to get any world
		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			if (Context.World())
			{
				World = Context.World();
				break;
			}
		}
	}
	if (!World)
	{
		UE_LOG(LogClaudeShellRelay, Warning,
			TEXT("No world available for health watchdog timer — using manual checks only"));
		return;
	}

	ConsecutiveFailures = 0;
	World->GetTimerManager().SetTimer(
		WatchdogTimerHandle,
		FTimerDelegate::CreateRaw(this, &FClaudeShellRelay::OnHealthTick),
		5.0f,  // 5 second interval
		true   // bLoop
	);

	UE_LOG(LogClaudeShellRelay, Log, TEXT("Health watchdog started (5s interval)"));
}

void FClaudeShellRelay::StopHealthWatchdog()
{
	if (WatchdogTimerHandle.IsValid() && GEngine)
	{
		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			if (Context.World())
			{
				Context.World()->GetTimerManager().ClearTimer(WatchdogTimerHandle);
				break;
			}
		}
		WatchdogTimerHandle.Invalidate();
	}
}

void FClaudeShellRelay::OnHealthTick()
{
	if (HealthCheck())
	{
		ConsecutiveFailures = 0;
		return;
	}

	ConsecutiveFailures++;
	UE_LOG(LogClaudeShellRelay, Warning,
		TEXT("Relay health check failed (%d/%d)"), ConsecutiveFailures, MaxConsecutiveFailures);

	if (ConsecutiveFailures >= MaxConsecutiveFailures)
	{
		UE_LOG(LogClaudeShellRelay, Error,
			TEXT("Relay unresponsive after %d failures — restarting"), MaxConsecutiveFailures);

		// Stop old process
		if (ProcessHandle.IsValid() && FPlatformProcess::IsProcRunning(ProcessHandle))
		{
			FPlatformProcess::TerminateProc(ProcessHandle, true);
		}
		ProcessHandle.Reset();
		Token.Empty();
		ConsecutiveFailures = 0;
		if (StdoutWritePipe)
		{
			FPlatformProcess::ClosePipe(nullptr, StdoutWritePipe);
			StdoutWritePipe = nullptr;
		}

		// Restart with cached params
		if (!CachedUvPath.IsEmpty())
		{
			Start(CachedUvPath, CachedPkgDir, RelayPort, CachedProjectDir,
				CachedWebDir, CachedDocsDir, CachedMcpConfig, CachedParentPid);
		}
	}
}

// --------------------------------------------------------------------
// HTTP helpers
// --------------------------------------------------------------------

bool FClaudeShellRelay::HealthCheck() const
{
	FString ResponseBody;
	int32 Status = HttpRequest(TEXT("GET"), TEXT("/api/status"), FString(), ResponseBody);
	return Status == 200;
}

FString FClaudeShellRelay::ParseTokenFromOutput(const FString& Output)
{
	// Look for: [claudeshell] Token: <uuid>
	const FString TokenPrefix = TEXT("[claudeshell] Token: ");
	int32 Idx = Output.Find(TokenPrefix);
	if (Idx == INDEX_NONE)
	{
		return FString();
	}

	// Extract everything after the prefix until newline or end
	int32 Start = Idx + TokenPrefix.Len();
	int32 End = Output.Find(TEXT("\n"), ESearchCase::IgnoreCase, ESearchDir::FromStart, Start);
	if (End == INDEX_NONE)
	{
		End = Output.Len();
	}

	FString TokenStr = Output.Mid(Start, End - Start);
	TokenStr.TrimStartAndEndInline();
	return TokenStr;
}

int32 FClaudeShellRelay::HttpRequest(
	const FString& Method,
	const FString& Path,
	const FString& Body,
	FString& OutBody) const
{
	// Raw TCP HTTP request to relay (avoids engine HTTP module dependency).
	// The relay is on localhost, so this is fast and reliable.

	ISocketSubsystem* SocketSub = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
	if (!SocketSub) return -1;

	TSharedRef<FInternetAddr> Addr = SocketSub->CreateInternetAddr();
	Addr->SetIp(0x7F000001); // 127.0.0.1
	Addr->SetPort(RelayPort);

	FSocket* Socket = SocketSub->CreateSocket(NAME_Stream, TEXT("RelayAPI"), false);
	if (!Socket) return -1;

	// Use non-blocking connect with poll, then switch to blocking for I/O.
	// On Windows, FSocket::Connect in blocking mode can hang for 30s on failure,
	// and GetConnectionState() is unreliable after non-blocking connect.
	Socket->SetNonBlocking(true);
	Socket->Connect(*Addr);

	// Poll for connection completion
	if (!Socket->Wait(ESocketWaitConditions::WaitForWrite, FTimespan::FromMilliseconds(3000)))
	{
		UE_LOG(LogClaudeShellRelay, Verbose,
			TEXT("HttpRequest: connect timeout to 127.0.0.1:%d"), RelayPort);
		Socket->Close();
		SocketSub->DestroySocket(Socket);
		return -1;
	}

	// Switch to blocking mode for reliable send/recv
	Socket->SetNonBlocking(false);

	// Build HTTP request
	FString Request = FString::Printf(TEXT("%s %s HTTP/1.1\r\nHost: 127.0.0.1:%d\r\nConnection: close\r\n"), *Method, *Path, RelayPort);

	if (!Token.IsEmpty())
	{
		Request += FString::Printf(TEXT("Authorization: Bearer %s\r\n"), *Token);
	}

	// Convert body to UTF-8 early so Content-Length is accurate
	TArray<uint8> BodyBytes;
	if (!Body.IsEmpty())
	{
		auto BodyUtf8 = FTCHARToUTF8(*Body);
		BodyBytes.Append(reinterpret_cast<const uint8*>(BodyUtf8.Get()), BodyUtf8.Length());
		Request += FString::Printf(TEXT("Content-Type: application/json\r\nContent-Length: %d\r\n"), BodyBytes.Num());
	}

	Request += TEXT("\r\n");

	// Send header
	auto HeaderUtf8 = FTCHARToUTF8(*Request);
	int32 BytesSent = 0;
	Socket->Send(reinterpret_cast<const uint8*>(HeaderUtf8.Get()), HeaderUtf8.Length(), BytesSent);

	// Send body separately (already UTF-8)
	if (BodyBytes.Num() > 0)
	{
		int32 BodySent = 0;
		Socket->Send(BodyBytes.GetData(), BodyBytes.Num(), BodySent);
	}

	// Wait for response, then read (up to 8KB)
	Socket->SetNonBlocking(true);
	Socket->Wait(ESocketWaitConditions::WaitForRead, FTimespan::FromMilliseconds(10000));
	Socket->SetNonBlocking(false);

	TArray<uint8> ResponseBuffer;
	ResponseBuffer.SetNumZeroed(8192);
	int32 BytesRead = 0;
	Socket->Recv(ResponseBuffer.GetData(), ResponseBuffer.Num() - 1, BytesRead);

	Socket->Close();
	SocketSub->DestroySocket(Socket);

	if (BytesRead <= 0)
	{
		UE_LOG(LogClaudeShellRelay, Verbose,
			TEXT("HttpRequest: no response from 127.0.0.1:%d for %s %s (BytesRead=%d)"),
			RelayPort, *Method, *Path, BytesRead);
		return -1;
	}

	ResponseBuffer[BytesRead] = 0;
	FString FullResponse = UTF8_TO_TCHAR(ResponseBuffer.GetData());

	// Parse HTTP status code from first line: "HTTP/1.x NNN ..."
	int32 StatusCode = -1;
	int32 SpaceIdx;
	if (FullResponse.FindChar(TEXT(' '), SpaceIdx))
	{
		FString StatusStr = FullResponse.Mid(SpaceIdx + 1, 3);
		StatusCode = FCString::Atoi(*StatusStr);
	}

	// Extract body (after \r\n\r\n)
	int32 BodyStart = FullResponse.Find(TEXT("\r\n\r\n"));
	if (BodyStart != INDEX_NONE)
	{
		OutBody = FullResponse.Mid(BodyStart + 4);
	}
	else
	{
		OutBody = FString();
	}

	return StatusCode;
}
