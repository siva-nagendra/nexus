"""Integration tests for the asset subsystem (namespace: asset)."""

from __future__ import annotations

import pytest

pytestmark = pytest.mark.integration


class TestAssetManagement:
    """Search, create, rename, duplicate, and validate assets."""

    async def test_search_assets(self, transport):
        """Search for assets by name and verify results are returned."""
        result = await transport.execute("asset.search", {"query": "SM_", "limit": 5})
        assert result.success

    async def test_create_and_list_folder(self, transport):
        """Create a content folder and list its contents."""

    async def test_get_asset_info(self, transport):
        """Get metadata for a known engine asset."""

    async def test_asset_references_and_dependents(self, transport):
        """Query outgoing references and incoming dependents of an asset."""

    async def test_save_all_assets(self, transport):
        """Save all dirty assets without error."""
