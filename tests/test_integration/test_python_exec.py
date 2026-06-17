"""Integration tests for the Python execution subsystem (namespace: python)."""

from __future__ import annotations

import pytest

pytestmark = pytest.mark.integration


class TestPythonExecution:
    """Execute Python code and files inside the UE process."""

    async def test_execute_python_snippet(self, transport):
        """Execute a simple Python expression and get the result."""
        result = await transport.execute(
            "python.execute",
            {
                "code": "import unreal; print(unreal.SystemLibrary.get_engine_version())",
            },
        )
        assert result.success

    async def test_get_python_paths(self, transport):
        """Get sys.path and unreal module location from the UE process."""
        result = await transport.execute("python.get_paths", {})
        assert result.success

    async def test_execute_python_file(self, transport):
        """Execute a .py file inside the UE Python environment."""
