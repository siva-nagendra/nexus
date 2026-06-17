// Copyright Nexus Team. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "HAL/PlatformProcess.h"

/**
 * Manages the ClaudeShell Python relay process lifecycle.
 *
 * Responsibilities:
 * - Launch the relay subprocess (python -m claudeshell)
 * - Capture the auth token emitted by the relay on stdout
 * - Health-check the relay via HTTP /api/status (5s interval, 3 failures → restart)
 * - Create and destroy sessions via REST API
 * - Compute a deterministic per-project port from the project path
 * - Shut down the relay cleanly
 */
class FClaudeShellRelay
{
public:
	FClaudeShellRelay();
	~FClaudeShellRelay();

	/**
	 * Start the relay process via uv run.
	 *
	 * @param UvPath      Path to the uv executable.
	 * @param PkgDir      Path to the claudeshell Python package (contains pyproject.toml).
	 * @param Port        Port for the relay to listen on.
	 * @param ProjectDir  Project working directory (passed as --cwd to relay).
	 * @param WebDir      Directory containing terminal frontend files.
	 * @param DocsDir     Directory containing CLAUDE.md templates (optional).
	 * @param McpConfig   JSON string of MCP server config for provisioning (optional).
	 * @param ParentPid   PID of the parent UE process (relay self-terminates on parent death).
	 * @return true if the relay process was launched successfully.
	 */
	bool Start(
		const FString& UvPath,
		const FString& PkgDir,
		int32 Port,
		const FString& ProjectDir,
		const FString& WebDir,
		const FString& DocsDir = FString(),
		const FString& McpConfig = FString(),
		int32 ParentPid = 0);

	/** Stop the relay process and clean up. */
	void Stop();

	/** @return true if the relay process is running AND responding to health checks. */
	bool IsAlive() const;

	/** @return The auth token captured from relay stdout on startup. */
	FString GetToken() const { return Token; }

	/** @return The port the relay is listening on. */
	int32 GetPort() const { return RelayPort; }

	/**
	 * Create a new Claude session via the relay REST API.
	 *
	 * @param Cwd          Working directory for the session.
	 * @param ProjectName  Display name for the session.
	 * @return Session ID string, or empty on failure.
	 */
	FString CreateSession(const FString& Cwd, const FString& ProjectName = FString());

	/**
	 * Destroy an existing session via the relay REST API.
	 *
	 * @param SessionId  ID of the session to destroy.
	 * @return true if the session was shut down successfully.
	 */
	bool DestroySession(const FString& SessionId);

	/**
	 * Compute a deterministic port from a project directory path.
	 *
	 * Uses MD5 hash of the normalized lowercase path to pick a port in the range 19220-19319.
	 *
	 * @param ProjectDir  Path to the project directory.
	 * @return Port number in range [19220, 19319].
	 */
	static int32 ComputeProjectPort(const FString& ProjectDir);

	/**
	 * Start the health watchdog timer.
	 *
	 * Polls the relay every 5 seconds via HTTP /api/status.
	 * After 3 consecutive failures, attempts to restart the relay.
	 */
	void StartHealthWatchdog();

	/** Stop the health watchdog timer. */
	void StopHealthWatchdog();

private:
	/** Perform an HTTP health check against /api/status. */
	bool HealthCheck() const;

	/** Parse the relay token from captured stdout output. */
	static FString ParseTokenFromOutput(const FString& Output);

	/**
	 * Make an HTTP request to the relay REST API.
	 *
	 * @param Method    HTTP method (GET, POST).
	 * @param Path      API path (e.g. "/api/session/create").
	 * @param Body      JSON body for POST requests (optional).
	 * @param OutBody   Response body (output).
	 * @return HTTP status code, or -1 on connection failure.
	 */
	int32 HttpRequest(const FString& Method, const FString& Path,
					  const FString& Body, FString& OutBody) const;

	/** Watchdog tick — called by timer delegate. */
	void OnHealthTick();

	/** Kill any stale relay process from a previous session on the given port. */
	static void KillStaleRelay(int32 Port);

	mutable FProcHandle ProcessHandle;
	void* StdoutWritePipe = nullptr;  // Kept alive so child process stdout doesn't break
	FString Token;
	int32 RelayPort = 0;
	FString CachedUvPath;
	FString CachedPkgDir;
	FString CachedProjectDir;
	FString CachedWebDir;
	FString CachedDocsDir;
	FString CachedMcpConfig;
	int32 CachedParentPid = 0;

	// Health watchdog state
	FTimerHandle WatchdogTimerHandle;
	int32 ConsecutiveFailures = 0;
	static const int32 MaxConsecutiveFailures = 3;
	static const int32 PORT_BASE = 19220;
	static const int32 PORT_RANGE = 100;
};
