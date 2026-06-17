// Copyright Nexus Team. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

class FClaudeShellRelay;

/**
 * ClaudeShell Editor Module — Embedded Claude Code terminal for Unreal.
 *
 * Lifecycle:
 *   StartupModule  → Registers tab spawner + menu entry only (fast, no subprocess work)
 *   SpawnTab       → Calls EnsureReady() → bootstrap + relay + CreateSession → SClaudeShellTab
 *   ShutdownModule → Stops relay + unregisters tab
 *
 * EnsureReady() is thread-safe via FCriticalSection and idempotent after first run.
 */
class FClaudeShellModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	static const FName TabId;

private:
	/**
	 * Ensure the relay is ready: find uv, launch relay via uv run.
	 *
	 * Guarded by ReadyLock. After first successful run, subsequent calls are no-ops.
	 * uv handles Python discovery, venv creation, and dependency installation automatically.
	 *
	 * @return true if the relay is alive and ready for session creation.
	 */
	bool EnsureReady();

	/** Spawn the Claude Shell tab.  Called by FGlobalTabmanager. */
	TSharedRef<SDockTab> SpawnTab(const FSpawnTabArgs& Args);

	/** Called by FGlobalTabmanager — return nullptr to allow multiple tabs. */
	TSharedPtr<SDockTab> OnFindTabToReuse(const FTabId& InTabId);

	/** Register the "Claude Shell" entry in the Window menu. */
	void RegisterMenus();

	/** Build MCP server config JSON for the relay provisioner. */
	FString BuildMcpConfigJson() const;

	// ── State ──
	FCriticalSection ReadyLock;
	bool bReady = false;
	FString ProjectDir;
	FString UvPath;
	TUniquePtr<FClaudeShellRelay> Relay;
};
