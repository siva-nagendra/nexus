"""Bootstrap venv for ClaudeShell.

Usage:
  python -m claudeshell.bootstrap --target-dir <venv> --requirements <reqs.txt> --version <ver>

Outputs JSON progress lines on stdout:
  {"step": "creating_venv", "progress": 0.2}
  {"step": "installing_deps", "progress": 0.5, "package": "websockets"}
  {"step": "complete", "progress": 1.0, "stamp": "3.0.0"}
"""

from __future__ import annotations

import argparse
import json
import subprocess
import sys
import venv
from pathlib import Path


def emit(data: dict) -> None:
    """Emit a JSON progress line to stdout for the C++ frontend to parse."""
    print(json.dumps(data), flush=True)


def bootstrap(target_dir: Path, requirements: Path, version: str) -> None:
    """Create or verify a venv for ClaudeShell with the correct dependencies.

    Args:
        target_dir: Directory to create the venv in.
        requirements: Path to requirements.txt with needed packages.
        version: Version string written to a stamp file for idempotent checks.
    """
    stamp_file = target_dir / ".claudeshell_version"

    # Check if already bootstrapped with correct version
    if stamp_file.exists() and stamp_file.read_text().strip() == version:
        emit({"step": "already_bootstrapped", "progress": 1.0, "version": version})
        return

    # Create venv
    emit({"step": "creating_venv", "progress": 0.1})
    builder = venv.EnvBuilder(with_pip=True, clear=True)
    builder.create(str(target_dir))
    emit({"step": "venv_created", "progress": 0.3})

    # Find pip in venv (platform-dependent path)
    if sys.platform == "win32":
        pip = target_dir / "Scripts" / "pip.exe"
    else:
        pip = target_dir / "bin" / "pip"

    if not pip.exists():
        emit({"step": "error", "progress": 0.3, "message": f"pip not found at {pip}"})
        sys.exit(1)

    # Validate requirements file exists
    if not requirements.exists():
        emit(
            {
                "step": "error",
                "progress": 0.3,
                "message": f"Requirements file not found: {requirements}",
            }
        )
        sys.exit(1)

    # Install requirements
    emit({"step": "installing_deps", "progress": 0.5})
    try:
        subprocess.check_call(
            [str(pip), "install", "-r", str(requirements), "--quiet"],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )
    except subprocess.CalledProcessError as exc:
        emit(
            {
                "step": "error",
                "progress": 0.5,
                "message": f"pip install failed with exit code {exc.returncode}",
            }
        )
        sys.exit(1)

    # Write stamp file to mark successful bootstrap
    stamp_file.write_text(version)
    emit({"step": "complete", "progress": 1.0, "stamp": version})


def main() -> None:
    """CLI entry point for bootstrap."""
    parser = argparse.ArgumentParser(description="Bootstrap a venv for ClaudeShell relay")
    parser.add_argument(
        "--target-dir",
        required=True,
        type=Path,
        help="Directory to create the venv in",
    )
    parser.add_argument(
        "--requirements",
        required=True,
        type=Path,
        help="Path to requirements.txt",
    )
    parser.add_argument(
        "--version",
        required=True,
        help="Version string for stamp file",
    )
    args = parser.parse_args()
    bootstrap(args.target_dir, args.requirements, args.version)


if __name__ == "__main__":
    main()
