"""Integration tests for the code analysis subsystem (namespace: code)."""

from __future__ import annotations

import pytest

pytestmark = pytest.mark.integration


class TestCodeAnalysis:
    """UE class introspection: hierarchy, properties, functions, search."""

    async def test_get_class_hierarchy(self, transport):
        """Get the inheritance hierarchy for the Actor class."""
        result = await transport.execute("code.get_class_hierarchy", {"class_name": "Actor"})
        assert result.success

    async def test_get_class_properties(self, transport):
        """List UPROPERTY fields for the Actor class."""

    async def test_get_class_functions(self, transport):
        """List UFUNCTION methods for the Actor class."""

    async def test_search_classes(self, transport):
        """Search UE classes by name substring."""

    async def test_list_modules(self, transport):
        """List loaded UE modules filtered by type."""
