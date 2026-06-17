// Copyright Nexus Team. All Rights Reserved.

#include "ClaudeShellModule.h"
#include "ClaudeShellPython.h"
#include "ClaudeShellRelay.h"
#include "SClaudeShellTab.h"
#include "Framework/Docking/TabManager.h"
#include "ToolMenus.h"
#include "Widgets/Docking/SDockTab.h"
#include "LevelEditor.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "Interfaces/IPluginManager.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

#define LOCTEXT_NAMESPACE "ClaudeShell"

DEFINE_LOG_CATEGORY_STATIC(LogClaudeShell, Log, All);

const FName FClaudeShellModule::TabId(TEXT("ClaudeShellTab"));

// Version must match pyproject.toml
static const TCHAR* CLAUDESHELL_VERSION = TEXT("3.0.0");

// --------------------------------------------------------------------
// StartupModule — FAST: tab spawner + menu only
// --------------------------------------------------------------------

void FClaudeShellModule::StartupModule()
{
	ProjectDir = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir());
	FPaths::NormalizeDirectoryName(ProjectDir);

	// Register nomad tab spawner — the actual work happens in SpawnTab → EnsureReady
	FGlobalTabmanager::Get()->RegisterNomadTabSpawner(
		TabId,
		FOnSpawnTab::CreateRaw(this, &FClaudeShellModule::SpawnTab))
		.SetDisplayName(LOCTEXT("TabTitle", "Claude Shell"))
		.SetTooltipText(LOCTEXT("TabTooltip", "Embedded Claude Code terminal"))
		.SetMenuType(ETabSpawnerMenuType::Enabled)
		.SetIcon(FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Console"))
		.SetReuseTabMethod(FOnFindTabToReuse::CreateRaw(this, &FClaudeShellModule::OnFindTabToReuse));

	// Register menus
	UToolMenus::RegisterStartupCallback(
		FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FClaudeShellModule::RegisterMenus));

	UE_LOG(LogClaudeShell, Log, TEXT("Module started (Project=%s)"), *ProjectDir);
}

// --------------------------------------------------------------------
// ShutdownModule — Stop relay + cleanup
// --------------------------------------------------------------------

void FClaudeShellModule::ShutdownModule()
{
	UToolMenus::UnRegisterStartupCallback(this);
	UToolMenus::UnregisterOwner(this);
	FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(TabId);

	if (Relay.IsValid())
	{
		Relay->Stop();
		Relay.Reset();
	}

	bReady = false;
	UE_LOG(LogClaudeShell, Log, TEXT("Module shut down"));
}

// --------------------------------------------------------------------
// EnsureReady — Lazy init: Python → Bootstrap → Relay
// --------------------------------------------------------------------

bool FClaudeShellModule::EnsureReady()
{
	FScopeLock Lock(&ReadyLock);

	// Fast path: already initialized
	if (bReady && Relay.IsValid() && Relay->IsAlive())
	{
		return true;
	}

	// ── Step 1: Find uv ──
	if (UvPath.IsEmpty())
	{
		UvPath = FClaudeShellPython::FindUv();
		if (UvPath.IsEmpty())
		{
			UE_LOG(LogClaudeShell, Error,
				TEXT("uv not found. Install uv (https://docs.astral.sh/uv/) or set CLAUDESHELL_UV env var."));
			return false;
		}
		UE_LOG(LogClaudeShell, Log, TEXT("uv found: %s"), *UvPath);
	}

	// ── Step 2: Locate paths ──
	TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("ClaudeShell"));
	FString PluginDir = Plugin.IsValid() ? Plugin->GetBaseDir() : FString();

	// Resolve symlinks: when installed as an engine plugin via symlink,
	// GetBaseDir() returns the symlink path (e.g. C:/Program Files/.../Plugins/ClaudeShell)
	// but we need the real target (e.g. D:/devmac/nexus/unreal_plugin/ClaudeShell)
	// so that ../.. resolves to the actual Nexus root, not the engine directory.
	FString ResolvedPluginDir = IFileManager::Get().GetFilenameOnDisk(*FPaths::Combine(PluginDir, TEXT("ClaudeShell.uplugin")));
	if (!ResolvedPluginDir.IsEmpty())
	{
		PluginDir = FPaths::GetPath(ResolvedPluginDir);
	}

	// ClaudeShell package is at: <NexusRoot>/packages/claudeshell
	// Plugin is at:              <NexusRoot>/unreal_plugin/ClaudeShell
	FString NexusRoot = FPaths::ConvertRelativePathToFull(FPaths::Combine(PluginDir, TEXT("../..")));
	FPaths::NormalizeDirectoryName(NexusRoot);
	UE_LOG(LogClaudeShell, Log, TEXT("NexusRoot: %s (PluginDir: %s)"), *NexusRoot, *PluginDir);

	FString ClaudeShellPkgDir = FPaths::Combine(NexusRoot, TEXT("packages/claudeshell"));

	// ── Step 3: Launch relay via uv run ──
	// uv run handles Python discovery, venv creation, and dependency installation automatically.
	int32 Port = FClaudeShellRelay::ComputeProjectPort(ProjectDir);
	FString WebDir = FPaths::Combine(ClaudeShellPkgDir, TEXT("frontend"));
	FString DocsDir = FPaths::Combine(NexusRoot, TEXT("docs/nexus-unreal"));
	FString McpConfig = BuildMcpConfigJson();

	if (!Relay.IsValid())
	{
		Relay = MakeUnique<FClaudeShellRelay>();
	}

	// If relay is already alive (from a previous editor session), reuse it
	if (!Relay->IsAlive())
	{
		int32 ParentPid = static_cast<int32>(FPlatformProcess::GetCurrentProcessId());

		bool bStarted = Relay->Start(
			UvPath,
			ClaudeShellPkgDir,
			Port,
			ProjectDir,
			WebDir,
			DocsDir,
			McpConfig,
			ParentPid);

		if (!bStarted)
		{
			UE_LOG(LogClaudeShell, Error, TEXT("Failed to start relay on port %d"), Port);
			return false;
		}

		Relay->StartHealthWatchdog();
	}

	bReady = true;
	UE_LOG(LogClaudeShell, Log, TEXT("Relay ready on port %d (token: %s...)"),
		Relay->GetPort(), *Relay->GetToken().Left(8));
	return true;
}

// --------------------------------------------------------------------
// SpawnTab — EnsureReady → CreateSession → SClaudeShellTab
// --------------------------------------------------------------------

TSharedRef<SDockTab> FClaudeShellModule::SpawnTab(const FSpawnTabArgs& Args)
{
	if (!EnsureReady())
	{
		// Return a tab with an error message
		return SNew(SDockTab)
			.TabRole(ETabRole::NomadTab)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("ErrorText",
					"Claude Shell failed to start.\n\n"
					"Check the Output Log for details.\n"
					"Ensure uv is installed (https://docs.astral.sh/uv/)."))
				.Justification(ETextJustify::Center)
			];
	}

	// Create a new session for this tab
	FString SessionId = Relay->CreateSession(ProjectDir, FPaths::GetBaseFilename(ProjectDir));

	if (SessionId.IsEmpty())
	{
		UE_LOG(LogClaudeShell, Error, TEXT("Failed to create Claude session"));
		return SNew(SDockTab)
			.TabRole(ETabRole::NomadTab)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("SessionError", "Failed to create Claude session.\nCheck the Output Log."))
				.Justification(ETextJustify::Center)
			];
	}

	UE_LOG(LogClaudeShell, Log, TEXT("Session created: %s"), *SessionId);

	return SNew(SDockTab)
		.TabRole(ETabRole::NomadTab)
		[
			SNew(SClaudeShellTab)
			.RelayPort(Relay->GetPort())
			.SessionId(SessionId)
			.Token(Relay->GetToken())
		];
}

TSharedPtr<SDockTab> FClaudeShellModule::OnFindTabToReuse(const FTabId& InTabId)
{
	// Always return nullptr — allow multiple Claude Shell tabs (one session per tab)
	return nullptr;
}

// --------------------------------------------------------------------
// Menu registration
// --------------------------------------------------------------------

void FClaudeShellModule::RegisterMenus()
{
	FToolMenuOwnerScoped OwnerScoped(this);

	UToolMenu* Menu = UToolMenus::Get()->ExtendMenu("LevelEditor.MainMenu.Window");
	FToolMenuSection& Section = Menu->FindOrAddSection("ClaudeShell");
	Section.AddEntry(FToolMenuEntry::InitMenuEntry(
		"ClaudeShellTab",
		LOCTEXT("MenuLabel", "Claude Shell"),
		LOCTEXT("MenuTooltip", "Open the embedded Claude Code terminal"),
		FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Console"),
		FUIAction(FExecuteAction::CreateLambda([]()
		{
			FGlobalTabmanager::Get()->TryInvokeTab(FClaudeShellModule::TabId);
		}))
	));
}

// --------------------------------------------------------------------
// MCP config builder
// --------------------------------------------------------------------

FString FClaudeShellModule::BuildMcpConfigJson() const
{
	// Build the MCP config that the relay provisioner writes to .mcp.json
	// This tells Claude CLI where the Nexus MCP server is.
	// Uses uv to run the Nexus Python MCP server.
	if (UvPath.IsEmpty())
	{
		UE_LOG(LogClaudeShell, Warning, TEXT("uv not found — MCP config will be empty"));
		return FString();
	}

	TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("ClaudeShell"));
	FString PluginDir = Plugin.IsValid() ? Plugin->GetBaseDir() : FString();

	// Resolve symlinks (same as in StartupModule) so we get the real NexusRoot
	FString ResolvedPath = IFileManager::Get().GetFilenameOnDisk(*FPaths::Combine(PluginDir, TEXT("ClaudeShell.uplugin")));
	if (!ResolvedPath.IsEmpty())
	{
		PluginDir = FPaths::GetPath(ResolvedPath);
	}

	FString NexusRoot = FPaths::ConvertRelativePathToFull(FPaths::Combine(PluginDir, TEXT("../..")));
	FPaths::NormalizeDirectoryName(NexusRoot);

	FString NexusPkgDir = NexusRoot;

	// Build JSON: {"server_name": "nexus", "server_config": {"type": "stdio", "command": "uv", "args": ["run", "--project", "<dir>", "-m", "nexus"]}}
	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("server_name"), TEXT("nexus"));

	TSharedRef<FJsonObject> ServerConfig = MakeShared<FJsonObject>();
	ServerConfig->SetStringField(TEXT("type"), TEXT("stdio"));
	ServerConfig->SetStringField(TEXT("command"), UvPath);

	TArray<TSharedPtr<FJsonValue>> ServerArgs;
	ServerArgs.Add(MakeShared<FJsonValueString>(TEXT("run")));
	ServerArgs.Add(MakeShared<FJsonValueString>(TEXT("--project")));
	ServerArgs.Add(MakeShared<FJsonValueString>(NexusPkgDir));
	ServerArgs.Add(MakeShared<FJsonValueString>(TEXT("-m")));
	ServerArgs.Add(MakeShared<FJsonValueString>(TEXT("nexus")));
	ServerConfig->SetArrayField(TEXT("args"), ServerArgs);

	Root->SetObjectField(TEXT("server_config"), ServerConfig);

	FString OutputString;
	TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
		TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&OutputString);
	FJsonSerializer::Serialize(Root, Writer);

	return OutputString;
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FClaudeShellModule, ClaudeShell)
