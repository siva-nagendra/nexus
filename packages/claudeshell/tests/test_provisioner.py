"""Tests for claudeshell.provisioner — McpProvisioner."""

from __future__ import annotations

import json
from pathlib import Path

from claudeshell.provisioner import McpProvisioner


class TestEnsureMcpJson:
    """Tests for McpProvisioner.ensure_mcp_json."""

    def test_creates_fresh_mcp_json(self, tmp_path: Path):
        """Should create .mcp.json with the server entry when no file exists."""
        provisioner = McpProvisioner()
        config = {"type": "stdio", "command": "python", "args": ["-m", "nexus"]}

        result = provisioner.ensure_mcp_json(str(tmp_path), "nexus", config)

        assert result.exists()
        data = json.loads(result.read_text(encoding="utf-8"))
        assert "mcpServers" in data
        assert "nexus" in data["mcpServers"]
        assert data["mcpServers"]["nexus"]["command"] == "python"

    def test_merges_without_clobbering(self, tmp_path: Path):
        """Should preserve existing server entries when adding a new one."""
        mcp_path = tmp_path / ".mcp.json"
        existing = {"mcpServers": {"other-server": {"type": "stdio", "command": "other-cmd"}}}
        mcp_path.write_text(json.dumps(existing), encoding="utf-8")

        provisioner = McpProvisioner()
        config = {"type": "stdio", "command": "python", "args": ["-m", "nexus"]}
        provisioner.ensure_mcp_json(str(tmp_path), "nexus", config)

        data = json.loads(mcp_path.read_text(encoding="utf-8"))
        # Both servers should exist
        assert "other-server" in data["mcpServers"]
        assert "nexus" in data["mcpServers"]

    def test_overwrites_corrupt_json(self, tmp_path: Path):
        """Should handle corrupt .mcp.json gracefully by overwriting."""
        mcp_path = tmp_path / ".mcp.json"
        mcp_path.write_text("{invalid json!!!", encoding="utf-8")

        provisioner = McpProvisioner()
        config = {"type": "stdio", "command": "python"}
        provisioner.ensure_mcp_json(str(tmp_path), "nexus", config)

        data = json.loads(mcp_path.read_text(encoding="utf-8"))
        assert "nexus" in data["mcpServers"]

    def test_updates_existing_server_config(self, tmp_path: Path):
        """Should update the server config if the server name already exists."""
        mcp_path = tmp_path / ".mcp.json"
        existing = {"mcpServers": {"nexus": {"type": "stdio", "command": "old-python"}}}
        mcp_path.write_text(json.dumps(existing), encoding="utf-8")

        provisioner = McpProvisioner()
        new_config = {"type": "stdio", "command": "new-python", "args": ["-m", "nexus"]}
        provisioner.ensure_mcp_json(str(tmp_path), "nexus", new_config)

        data = json.loads(mcp_path.read_text(encoding="utf-8"))
        assert data["mcpServers"]["nexus"]["command"] == "new-python"


class TestEnsureClaudeMd:
    """Tests for McpProvisioner.ensure_claude_md."""

    def test_creates_claude_md_from_template(self, tmp_path: Path):
        """Should create .claude/CLAUDE.md from template with version marker."""
        template_dir = tmp_path / "templates"
        template_dir.mkdir()
        (template_dir / "NEXUS_CLAUDE.md").write_text(
            "# Nexus Docs\nContent here.", encoding="utf-8"
        )

        project_dir = tmp_path / "project"
        project_dir.mkdir()

        provisioner = McpProvisioner()
        result = provisioner.ensure_claude_md(str(project_dir), template_dir, version="3.0.0")

        assert result.exists()
        content = result.read_text(encoding="utf-8")
        assert "<!-- nexus-docs-version: 3.0.0 -->" in content
        assert "# Nexus Docs" in content

    def test_skips_if_version_matches(self, tmp_path: Path):
        """Should not overwrite if CLAUDE.md already has the matching version marker."""
        template_dir = tmp_path / "templates"
        template_dir.mkdir()
        (template_dir / "NEXUS_CLAUDE.md").write_text("# New Content", encoding="utf-8")

        project_dir = tmp_path / "project"
        claude_dir = project_dir / ".claude"
        claude_dir.mkdir(parents=True)
        target = claude_dir / "CLAUDE.md"
        target.write_text("<!-- nexus-docs-version: 3.0.0 -->\n# Old Content", encoding="utf-8")

        provisioner = McpProvisioner()
        provisioner.ensure_claude_md(str(project_dir), template_dir, version="3.0.0")

        # Content should be unchanged (old content preserved)
        content = target.read_text(encoding="utf-8")
        assert "# Old Content" in content
        assert "# New Content" not in content

    def test_updates_when_version_differs(self, tmp_path: Path):
        """Should overwrite when version marker doesn't match."""
        template_dir = tmp_path / "templates"
        template_dir.mkdir()
        (template_dir / "NEXUS_CLAUDE.md").write_text("# Updated Docs", encoding="utf-8")

        project_dir = tmp_path / "project"
        claude_dir = project_dir / ".claude"
        claude_dir.mkdir(parents=True)
        target = claude_dir / "CLAUDE.md"
        target.write_text("<!-- nexus-docs-version: 2.0.0 -->\n# Old Docs", encoding="utf-8")

        provisioner = McpProvisioner()
        provisioner.ensure_claude_md(str(project_dir), template_dir, version="3.0.0")

        content = target.read_text(encoding="utf-8")
        assert "<!-- nexus-docs-version: 3.0.0 -->" in content
        assert "# Updated Docs" in content
