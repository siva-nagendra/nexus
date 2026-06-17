// Copyright Nexus Team. All Rights Reserved.

#include "ClaudeShellPython.h"
#include "HAL/PlatformProcess.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"

DEFINE_LOG_CATEGORY_STATIC(LogClaudeShellPython, Log, All);

// --------------------------------------------------------------------
// FindOnPath
// --------------------------------------------------------------------

FString FClaudeShellPython::FindOnPath(const FString& ExeName)
{
#if PLATFORM_WINDOWS
	FString PathEnv = FPlatformMisc::GetEnvironmentVariable(TEXT("PATH"));
	TArray<FString> Dirs;
	PathEnv.ParseIntoArray(Dirs, TEXT(";"), true);

	for (const FString& Dir : Dirs)
	{
		FString Candidate = FPaths::Combine(Dir, ExeName);
		if (!Candidate.EndsWith(TEXT(".exe")))
		{
			Candidate += TEXT(".exe");
		}
		if (FPaths::FileExists(Candidate))
		{
			return Candidate;
		}
	}
#else
	FString PathEnv = FPlatformMisc::GetEnvironmentVariable(TEXT("PATH"));
	TArray<FString> Dirs;
	PathEnv.ParseIntoArray(Dirs, TEXT(":"), true);

	for (const FString& Dir : Dirs)
	{
		FString Candidate = FPaths::Combine(Dir, ExeName);
		if (FPaths::FileExists(Candidate))
		{
			return Candidate;
		}
	}
#endif

	return FString();
}

// --------------------------------------------------------------------
// FindUv
// --------------------------------------------------------------------

FString FClaudeShellPython::FindUv()
{
	// 1. Explicit override via environment variable
	FString EnvOverride = FPlatformMisc::GetEnvironmentVariable(TEXT("CLAUDESHELL_UV"));
	if (!EnvOverride.IsEmpty() && FPaths::FileExists(EnvOverride))
	{
		UE_LOG(LogClaudeShellPython, Log, TEXT("Found uv via CLAUDESHELL_UV: %s"), *EnvOverride);
		return EnvOverride;
	}

	// 2. "uv" on PATH
	FString UvOnPath = FindOnPath(TEXT("uv"));
	if (!UvOnPath.IsEmpty())
	{
		UE_LOG(LogClaudeShellPython, Log, TEXT("Found uv on PATH: %s"), *UvOnPath);
		return UvOnPath;
	}

	// 3. Well-known install locations (UE launched from a desktop launcher often has a stripped PATH
	//    and env vars like USERPROFILE may be missing or corrupted)
	UE_LOG(LogClaudeShellPython, Log, TEXT("uv not on PATH — probing well-known install locations"));

	TArray<FString> WellKnownPaths;

	// Use UE's UserHomeDir() which calls the Windows API directly (not env vars)
	FString HomeDir = FPlatformProcess::UserHomeDir();
	FPaths::NormalizeDirectoryName(HomeDir);
	UE_LOG(LogClaudeShellPython, Log, TEXT("User home dir: %s"), *HomeDir);

#if PLATFORM_WINDOWS
	if (!HomeDir.IsEmpty())
	{
		// Default uv installer location: ~\.local\bin\uv.exe
		WellKnownPaths.Add(FPaths::Combine(HomeDir, TEXT(".local/bin/uv.exe")));
		// Cargo install location: ~\.cargo\bin\uv.exe
		WellKnownPaths.Add(FPaths::Combine(HomeDir, TEXT(".cargo/bin/uv.exe")));
	}

	// Also try env vars as fallback in case home dir differs from profile
	FString UserProfile = FPlatformMisc::GetEnvironmentVariable(TEXT("USERPROFILE"));
	if (!UserProfile.IsEmpty() && UserProfile != HomeDir)
	{
		WellKnownPaths.Add(FPaths::Combine(UserProfile, TEXT(".local/bin/uv.exe")));
	}
	FString LocalAppData = FPlatformMisc::GetEnvironmentVariable(TEXT("LOCALAPPDATA"));
	if (!LocalAppData.IsEmpty())
	{
		WellKnownPaths.Add(FPaths::Combine(LocalAppData, TEXT("uv/uv.exe")));
	}
	FString CargoHome = FPlatformMisc::GetEnvironmentVariable(TEXT("CARGO_HOME"));
	if (!CargoHome.IsEmpty())
	{
		WellKnownPaths.Add(FPaths::Combine(CargoHome, TEXT("bin/uv.exe")));
	}

	// Also try USERNAME-based profile paths (UserHomeDir may differ from profile dir,
	// e.g. UserHomeDir = C:/Users/siva but uv installed under C:/Users/siva.domain)
	FString UserName = FPlatformMisc::GetEnvironmentVariable(TEXT("USERNAME"));
	if (!UserName.IsEmpty())
	{
		FString UserNamePath = FString::Printf(TEXT("C:/Users/%s/.local/bin/uv.exe"), *UserName);
		WellKnownPaths.AddUnique(UserNamePath);
	}

	// Try domain-qualified username (e.g. user.domain or domain.user)
	FString UserDomain = FPlatformMisc::GetEnvironmentVariable(TEXT("USERDOMAIN"));
	if (!UserName.IsEmpty() && !UserDomain.IsEmpty())
	{
		// Common pattern: user profile is username.domain
		FString DomainQualified = FString::Printf(TEXT("C:/Users/%s.%s/.local/bin/uv.exe"), *UserName, *UserDomain);
		WellKnownPaths.AddUnique(DomainQualified);
	}

	// Try UE's UserName (may include domain suffix)
	FString UeUserName = FPlatformProcess::UserName(false);
	if (!UeUserName.IsEmpty())
	{
		FString UeUserPath = FString::Printf(TEXT("C:/Users/%s/.local/bin/uv.exe"), *UeUserName);
		WellKnownPaths.AddUnique(UeUserPath);
	}

	// Also try the login profile from %USERPROFILE% env var directly
	FString UserProfileEnv = FPlatformMisc::GetEnvironmentVariable(TEXT("USERPROFILE"));
	if (!UserProfileEnv.IsEmpty())
	{
		WellKnownPaths.AddUnique(FPaths::Combine(UserProfileEnv, TEXT(".local/bin/uv.exe")));
	}
#else
	if (!HomeDir.IsEmpty())
	{
		WellKnownPaths.Add(FPaths::Combine(HomeDir, TEXT(".local/bin/uv")));
		WellKnownPaths.Add(FPaths::Combine(HomeDir, TEXT(".cargo/bin/uv")));
	}
	FString CargoHome = FPlatformMisc::GetEnvironmentVariable(TEXT("CARGO_HOME"));
	if (!CargoHome.IsEmpty())
	{
		WellKnownPaths.Add(FPaths::Combine(CargoHome, TEXT("bin/uv")));
	}
	// Homebrew locations
	WellKnownPaths.Add(TEXT("/opt/homebrew/bin/uv"));
	WellKnownPaths.Add(TEXT("/usr/local/bin/uv"));
#endif

	for (const FString& Candidate : WellKnownPaths)
	{
		if (FPaths::FileExists(Candidate))
		{
			UE_LOG(LogClaudeShellPython, Log, TEXT("Found uv at well-known location: %s"), *Candidate);
			return Candidate;
		}
	}

	UE_LOG(LogClaudeShellPython, Error,
		TEXT("uv not found. Searched PATH and well-known locations. Install uv (https://docs.astral.sh/uv/) or set CLAUDESHELL_UV env var."));
	return FString();
}
