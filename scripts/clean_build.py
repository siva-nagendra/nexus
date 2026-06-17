#!/usr/bin/env python3
"""Unified build script for Nexus and ClaudeShell UE 5.7 engine plugins.

Handles the complete build lifecycle:
  1. Pre-flight checks (UE not running, UAT exists, VS installed)
  2. Kill stale relay/nexus processes
  3. Remove engine symlinks (prevents UBT scanning sibling plugins)
  4. Clean old build artifacts
  5. Compile via RunUAT BuildPlugin
  6. Deploy binaries to plugin source directories
  7. Restore engine symlinks
  8. Verify deployed DLLs

Usage:
    python scripts/clean_build.py                  # Build all plugins
    python scripts/clean_build.py nexus            # Build only Nexus
    python scripts/clean_build.py claudeshell      # Build only ClaudeShell
    python scripts/clean_build.py --links          # Only create engine symlinks
    python scripts/clean_build.py --unlinks        # Only remove engine symlinks
    python scripts/clean_build.py --clean          # Remove all build artifacts
    python scripts/clean_build.py --verify         # Check deployed binaries
"""

from __future__ import annotations

import argparse
import ctypes
import glob
import os
import shutil
import subprocess
import sys
import time

# ─── Configuration ────────────────────────────────────────────────────────────

UE_ROOT = os.environ.get("UE_ROOT", r"C:\Program Files\UE_5.7")
UAT = os.path.join(UE_ROOT, "Engine", "Build", "BatchFiles", "RunUAT.bat")
ENGINE_PLUGINS = os.path.join(UE_ROOT, "Engine", "Plugins")

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PLUGIN_ROOT = os.path.join(REPO_ROOT, "unreal_plugin")

# Build output goes OUTSIDE unreal_plugin/ to prevent UBT from scanning
# sibling plugin folders during compilation.
BUILD_ROOT = r"D:\devmac\_plugin_builds"

PLUGINS = {
    "nexus": {
        "display_name": "Nexus",
        "uplugin": os.path.join(PLUGIN_ROOT, "Nexus", "Nexus.uplugin"),
        "build_dir": os.path.join(BUILD_ROOT, "NexusBuild"),
        "bin_src": os.path.join(BUILD_ROOT, "NexusBuild", "Binaries", "Win64"),
        "bin_dst": os.path.join(PLUGIN_ROOT, "Nexus", "Binaries", "Win64"),
        "dll_name": "UnrealEditor-Nexus.dll",
        "symlink": os.path.join(ENGINE_PLUGINS, "Nexus"),
        "symlink_target": os.path.join(PLUGIN_ROOT, "Nexus"),
    },
    "claudeshell": {
        "display_name": "ClaudeShell",
        "uplugin": os.path.join(PLUGIN_ROOT, "ClaudeShell", "ClaudeShell.uplugin"),
        "build_dir": os.path.join(BUILD_ROOT, "ClaudeShellBuild"),
        "bin_src": os.path.join(BUILD_ROOT, "ClaudeShellBuild", "Binaries", "Win64"),
        "bin_dst": os.path.join(PLUGIN_ROOT, "ClaudeShell", "Binaries", "Win64"),
        "dll_name": "UnrealEditor-ClaudeShell.dll",
        "symlink": os.path.join(ENGINE_PLUGINS, "ClaudeShell"),
        "symlink_target": os.path.join(PLUGIN_ROOT, "ClaudeShell"),
    },
}

# Build order matters: ClaudeShell may depend on Nexus headers
BUILD_ORDER = ["nexus", "claudeshell"]

LOG_DIR = os.path.join(REPO_ROOT, "scripts", "logs")


# ─── Helpers ──────────────────────────────────────────────────────────────────


def _log(msg: str, level: str = "INFO") -> None:
    prefix = {"INFO": "  ", "OK": "  [OK]", "WARN": "  [!!]", "ERR": "  [ERR]", "STEP": "\n>>>"}
    print(f"{prefix.get(level, '  ')} {msg}", flush=True)


def _header(title: str) -> None:
    width = 60
    print(f"\n{'=' * width}")
    print(f"  {title}")
    print(f"{'=' * width}\n", flush=True)


def _is_admin() -> bool:
    """Check if running with admin privileges (needed for mklink /J in Program Files)."""
    try:
        return ctypes.windll.shell32.IsUserAnAdmin() != 0
    except Exception:
        return False


def _ue_is_running() -> bool:
    """Check if UnrealEditor.exe is running."""
    result = subprocess.run(
        ["cmd.exe", "/c", "tasklist", "/FI", "IMAGENAME eq UnrealEditor.exe"],
        capture_output=True, text=True,
    )
    return "UnrealEditor.exe" in result.stdout


def _kill_stale_processes() -> None:
    """Kill stale nexus/claudeshell relay processes that might lock files."""
    for proc_name in ["python.exe", "pythonw.exe"]:
        result = subprocess.run(
            ["cmd.exe", "/c", "wmic", "process", "where",
             f"name='{proc_name}'", "get", "commandline,processid"],
            capture_output=True, text=True,
        )
        for line in result.stdout.splitlines():
            if "claudeshell" in line.lower() or "nexus" in line.lower():
                # Extract PID (last token on the line)
                parts = line.strip().split()
                if parts:
                    try:
                        pid = int(parts[-1])
                        subprocess.run(
                            ["cmd.exe", "/c", "taskkill", "/PID", str(pid), "/F"],
                            capture_output=True, text=True,
                        )
                        _log(f"Killed stale process PID {pid}: {line.strip()[:80]}", "WARN")
                    except (ValueError, IndexError):
                        pass


# ─── Symlink Management ──────────────────────────────────────────────────────


def remove_symlink(path: str) -> None:
    """Remove a junction or directory. Handles both junctions and real dirs."""
    if not os.path.exists(path) and not os.path.isdir(path):
        return
    try:
        # os.rmdir works for empty dirs and junctions (doesn't follow junction)
        os.rmdir(path)
        _log(f"Removed junction: {path}")
    except OSError:
        # Real directory with contents — use rmtree
        try:
            shutil.rmtree(path)
            _log(f"Removed directory: {path}")
        except Exception as e:
            _log(f"Failed to remove {path}: {e}", "ERR")


def create_symlink(path: str, target: str) -> bool:
    """Create a junction. Uses list-form args to avoid shell escaping issues."""
    if os.path.exists(path):
        _log(f"Junction already exists: {path}", "WARN")
        return True
    result = subprocess.run(
        ["cmd.exe", "/c", "mklink", "/J", path, target],
        capture_output=True, text=True,
    )
    if result.returncode == 0:
        _log(f"Created junction: {os.path.basename(path)} -> {target}", "OK")
        return True
    else:
        _log(f"Failed to create junction: {path}", "ERR")
        _log(f"  stdout: {result.stdout.strip()}")
        _log(f"  stderr: {result.stderr.strip()}")
        return False


def remove_all_symlinks() -> None:
    """Remove all engine plugin symlinks."""
    for cfg in PLUGINS.values():
        remove_symlink(cfg["symlink"])


def create_all_symlinks() -> None:
    """Create all engine plugin symlinks."""
    for cfg in PLUGINS.values():
        create_symlink(cfg["symlink"], cfg["symlink_target"])


# ─── Build ────────────────────────────────────────────────────────────────────


def deploy_binaries(cfg: dict) -> bool:
    """Copy compiled binaries from build output to plugin source directory.

    Handles file-lock edge cases (WinError 1224) by retrying after a short
    delay, and falling back to remove-then-copy.
    """
    if not os.path.isdir(cfg["bin_src"]):
        _log(f"No binaries found at {cfg['bin_src']}", "ERR")
        return False

    os.makedirs(cfg["bin_dst"], exist_ok=True)
    files = [f for f in os.listdir(cfg["bin_src"])
             if os.path.isfile(os.path.join(cfg["bin_src"], f))]

    for fname in files:
        src = os.path.join(cfg["bin_src"], fname)
        dst = os.path.join(cfg["bin_dst"], fname)

        for attempt in range(3):
            try:
                shutil.copy2(src, dst)
                _log(f"Deployed: {fname}")
                break
            except OSError as e:
                if attempt < 2:
                    _log(f"Copy failed ({e}), retrying...", "WARN")
                    # Try removing the locked file first
                    try:
                        os.remove(dst)
                    except OSError:
                        pass
                    time.sleep(1)
                else:
                    _log(f"Failed to deploy {fname} after 3 attempts: {e}", "ERR")
                    return False

    return True


def build_plugin(name: str, cfg: dict, *, log_to_file: bool = False) -> bool:
    """Build a single plugin via RunUAT BuildPlugin."""
    _header(f"Building {cfg['display_name']}")

    # Remove ALL engine symlinks to prevent UBT from scanning sibling plugins
    _log("Removing engine symlinks...")
    remove_all_symlinks()

    # Clean old build artifacts
    if os.path.isdir(cfg["build_dir"]):
        shutil.rmtree(cfg["build_dir"], ignore_errors=True)
        _log(f"Cleaned: {cfg['build_dir']}")

    os.makedirs(BUILD_ROOT, exist_ok=True)

    # Compose UAT command
    cmd = [
        UAT, "BuildPlugin",
        f"-Plugin={cfg['uplugin']}",
        f"-Package={cfg['build_dir']}",
        "-TargetPlatforms=Win64",
        "-Rocket",
    ]

    _log(f"RunUAT BuildPlugin -Plugin={os.path.basename(cfg['uplugin'])} ...")

    # Set up optional log file
    log_file = None
    if log_to_file:
        os.makedirs(LOG_DIR, exist_ok=True)
        log_path = os.path.join(LOG_DIR, f"build_{name}.log")
        log_file = open(log_path, "w", encoding="utf-8")  # noqa: SIM115
        _log(f"Logging to: {log_path}")

    # Run build
    proc = subprocess.Popen(
        cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
        text=True, shell=True,
    )
    for line in proc.stdout:
        print(line, end="", flush=True)
        if log_file:
            log_file.write(line)
    proc.wait()

    if log_file:
        log_file.close()

    if proc.returncode != 0:
        _log(f"BUILD FAILED (exit code {proc.returncode})", "ERR")
        # Restore symlinks even on failure
        create_all_symlinks()
        return False

    # Deploy binaries
    _log("Deploying binaries...")
    if not deploy_binaries(cfg):
        _log("Binary deployment failed", "ERR")
        create_all_symlinks()
        return False

    _log(f"{cfg['display_name']} build SUCCESS", "OK")
    return True


# ─── Verify ───────────────────────────────────────────────────────────────────


def verify_binaries() -> bool:
    """Check that all plugin DLLs exist and report sizes."""
    _header("Binary Verification")
    all_ok = True
    for name, cfg in PLUGINS.items():
        dll = os.path.join(cfg["bin_dst"], cfg["dll_name"])
        if os.path.isfile(dll):
            size_mb = os.path.getsize(dll) / (1024 * 1024)
            mtime = time.strftime("%Y-%m-%d %H:%M:%S", time.localtime(os.path.getmtime(dll)))
            _log(f"{cfg['dll_name']}  ({size_mb:.1f} MB, {mtime})", "OK")
        else:
            _log(f"{cfg['dll_name']}  MISSING", "ERR")
            all_ok = False

    # Check symlinks
    print()
    for name, cfg in PLUGINS.items():
        link = cfg["symlink"]
        if os.path.isdir(link):
            _log(f"Engine junction: {os.path.basename(link)}", "OK")
        else:
            _log(f"Engine junction: {os.path.basename(link)}  MISSING", "ERR")
            all_ok = False

    return all_ok


# ─── Clean ────────────────────────────────────────────────────────────────────


def clean_all() -> None:
    """Remove all build artifacts."""
    _header("Cleaning Build Artifacts")
    for name, cfg in PLUGINS.items():
        if os.path.isdir(cfg["build_dir"]):
            shutil.rmtree(cfg["build_dir"], ignore_errors=True)
            _log(f"Removed: {cfg['build_dir']}")
    # Also clean log directory
    if os.path.isdir(LOG_DIR):
        shutil.rmtree(LOG_DIR, ignore_errors=True)
        _log(f"Removed: {LOG_DIR}")
    _log("Clean complete", "OK")


# ─── Pre-flight ───────────────────────────────────────────────────────────────


def preflight() -> bool:
    """Run pre-flight checks before building."""
    ok = True

    # Check UAT exists
    if not os.path.isfile(UAT):
        _log(f"RunUAT.bat not found at: {UAT}", "ERR")
        _log("Is UE 5.7 installed?")
        ok = False

    # Check uplugin files exist
    for name, cfg in PLUGINS.items():
        if not os.path.isfile(cfg["uplugin"]):
            _log(f"{cfg['display_name']} .uplugin not found: {cfg['uplugin']}", "ERR")
            ok = False

    # Check UE not running
    if _ue_is_running():
        _log("Unreal Editor is running! Close it before building.", "ERR")
        _log("  Run: taskkill /IM UnrealEditor.exe /F")
        ok = False

    # Check admin (needed for junctions in Program Files)
    if not _is_admin():
        _log("Not running as admin. Junction creation in Program Files may fail.", "WARN")
        _log("  Run from an elevated terminal if symlink operations fail.")

    return ok


# ─── CLI ──────────────────────────────────────────────────────────────────────


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Build Nexus and ClaudeShell UE 5.7 engine plugins.",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  python scripts/clean_build.py              Build all plugins
  python scripts/clean_build.py nexus        Build only Nexus
  python scripts/clean_build.py claudeshell  Build only ClaudeShell
  python scripts/clean_build.py --links      Create engine symlinks only
  python scripts/clean_build.py --unlinks    Remove engine symlinks only
  python scripts/clean_build.py --clean      Remove all build artifacts
  python scripts/clean_build.py --verify     Check deployed binaries
  python scripts/clean_build.py --log        Build all, log output to file
        """,
    )
    parser.add_argument(
        "target", nargs="?", default="all",
        choices=["nexus", "claudeshell", "all"],
        help="Which plugin to build (default: all)",
    )
    parser.add_argument("--links", action="store_true", help="Create engine symlinks only")
    parser.add_argument("--unlinks", action="store_true", help="Remove engine symlinks only")
    parser.add_argument("--clean", action="store_true", help="Remove all build artifacts")
    parser.add_argument("--verify", action="store_true", help="Verify deployed binaries")
    parser.add_argument("--log", action="store_true", help="Log build output to scripts/logs/")
    parser.add_argument("--kill-stale", action="store_true",
                        help="Kill stale nexus/claudeshell Python processes before building")

    args = parser.parse_args()

    # Handle utility commands first
    if args.links:
        _header("Creating Engine Symlinks")
        create_all_symlinks()
        return 0

    if args.unlinks:
        _header("Removing Engine Symlinks")
        remove_all_symlinks()
        return 0

    if args.clean:
        clean_all()
        return 0

    if args.verify:
        return 0 if verify_binaries() else 1

    # Full build flow
    _header("Pre-flight Checks")
    if not preflight():
        return 1
    _log("All pre-flight checks passed", "OK")

    if args.kill_stale:
        _log("Killing stale processes...")
        _kill_stale_processes()

    # Determine build targets (respect BUILD_ORDER)
    targets = BUILD_ORDER if args.target == "all" else [args.target]

    for name in targets:
        cfg = PLUGINS[name]
        if not build_plugin(name, cfg, log_to_file=args.log):
            # Restore symlinks on failure
            create_all_symlinks()
            return 1

    # Restore all symlinks after all builds complete
    _header("Restoring Engine Symlinks")
    create_all_symlinks()

    # Verify
    if not verify_binaries():
        return 1

    print(f"\nDone! Built: {', '.join(PLUGINS[t]['display_name'] for t in targets)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
