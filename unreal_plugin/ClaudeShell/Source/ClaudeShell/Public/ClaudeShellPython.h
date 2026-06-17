// Copyright Nexus Team. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/**
 * Utilities for finding uv (the Python package manager) on the system.
 *
 * ClaudeShell uses `uv run` to handle Python discovery, venv creation,
 * dependency installation, and module execution in a single step.
 */
class FClaudeShellPython
{
public:
	/**
	 * Find the uv executable on the system PATH.
	 *
	 * Search order:
	 * 1. CLAUDESHELL_UV env var (explicit override)
	 * 2. "uv" on PATH
	 *
	 * @return Full path to the uv executable, or empty string if not found.
	 */
	static FString FindUv();

private:
	/**
	 * Search the system PATH for an executable by name.
	 *
	 * @param ExeName Executable name (e.g. "uv").
	 * @return Full path to the executable if found, empty string otherwise.
	 */
	static FString FindOnPath(const FString& ExeName);
};
