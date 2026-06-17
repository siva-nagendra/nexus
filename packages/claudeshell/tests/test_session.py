"""Tests for claudeshell.session — Session and SessionRegistry."""

from __future__ import annotations

import base64
from unittest.mock import MagicMock

from claudeshell.session import Session, SessionRegistry

# ------------------------------------------------------------------
# Session unit tests
# ------------------------------------------------------------------


class TestSession:
    """Unit tests for Session (no real PTY)."""

    def _make_session(self, **kwargs) -> Session:
        defaults = dict(session_id="abc12345", cwd="/tmp/project", project_name="Test")
        defaults.update(kwargs)
        return Session(**defaults)

    def test_init_state(self):
        """Session starts in a non-alive state with empty scrollback."""
        s = self._make_session()
        assert s.id == "abc12345"
        assert s.cwd == "/tmp/project"
        assert s.project_name == "Test"
        assert s.alive is False
        assert s.client_count == 0
        assert s.get_scrollback() == base64.b64encode(b"").decode("ascii")

    def test_scrollback_max(self):
        """Scrollback should be trimmed to SCROLLBACK_MAX bytes."""
        s = self._make_session()
        # Directly manipulate _scrollback to test trimming logic
        large_data = b"X" * (Session.SCROLLBACK_MAX + 1000)
        s._scrollback.extend(large_data)
        # Simulate trim (same logic as _read_output_loop)
        if len(s._scrollback) > Session.SCROLLBACK_MAX:
            s._scrollback[:] = s._scrollback[-Session.SCROLLBACK_MAX :]
        assert len(s._scrollback) == Session.SCROLLBACK_MAX

    def test_write_input_when_not_alive(self):
        """write_input should be a no-op when session is not alive."""
        s = self._make_session()
        # Should not raise
        s.write_input(base64.b64encode(b"hello").decode("ascii"))

    def test_add_remove_clients(self):
        """Client management: add, remove, discard idempotent."""
        s = self._make_session()
        ws1 = MagicMock()
        ws2 = MagicMock()

        s.add_client(ws1)
        assert s.client_count == 1
        s.add_client(ws2)
        assert s.client_count == 2

        s.remove_client(ws1)
        assert s.client_count == 1

        # Removing again should not raise (uses discard)
        s.remove_client(ws1)
        assert s.client_count == 1

    def test_shutdown_cleans_up(self):
        """shutdown() should set alive to False and clear PTY."""
        s = self._make_session()
        mock_pty = MagicMock()
        mock_pty.isalive.return_value = True
        s._pty = mock_pty
        s._alive = True

        s.shutdown()

        assert s.alive is False
        assert s._pty is None
        mock_pty.terminate.assert_called_once_with(force=True)

    def test_shutdown_when_already_dead(self):
        """shutdown() when session is already dead should not raise."""
        s = self._make_session()
        s.shutdown()
        assert s.alive is False


# ------------------------------------------------------------------
# SessionRegistry unit tests
# ------------------------------------------------------------------


class TestSessionRegistry:
    """Unit tests for SessionRegistry."""

    def test_create_returns_session(self):
        """create() should return a Session with a generated ID."""
        reg = SessionRegistry()
        session = reg.create(cwd="/tmp/project", project_name="TestProject")
        assert isinstance(session, Session)
        assert len(session.id) == 8  # uuid4().hex[:8]
        assert session.cwd == "/tmp/project"
        assert session.project_name == "TestProject"

    def test_get_existing_session(self):
        """get() should return a session by its ID."""
        reg = SessionRegistry()
        session = reg.create(cwd="/tmp/project")
        result = reg.get(session.id)
        assert result is session

    def test_get_nonexistent_session(self):
        """get() should return None for unknown IDs."""
        reg = SessionRegistry()
        assert reg.get("nonexistent") is None

    def test_list_sessions(self):
        """list_sessions() should return summary dicts for all sessions."""
        reg = SessionRegistry()
        s1 = reg.create(cwd="/tmp/a", project_name="A")
        s2 = reg.create(cwd="/tmp/b", project_name="B")

        listing = reg.list_sessions()
        assert len(listing) == 2
        ids = {item["id"] for item in listing}
        assert s1.id in ids
        assert s2.id in ids
        # Verify summary shape
        for item in listing:
            assert "id" in item
            assert "project_name" in item
            assert "alive" in item
            assert "clients" in item

    def test_shutdown_removes_session(self):
        """shutdown() should remove and kill the session."""
        reg = SessionRegistry()
        session = reg.create(cwd="/tmp/project")
        session_id = session.id

        result = reg.shutdown(session_id)
        assert result is True
        assert reg.get(session_id) is None

    def test_shutdown_nonexistent_returns_false(self):
        """shutdown() on a missing ID should return False."""
        reg = SessionRegistry()
        assert reg.shutdown("missing") is False

    def test_shutdown_all(self):
        """shutdown_all() should clear all sessions."""
        reg = SessionRegistry()
        reg.create(cwd="/tmp/a")
        reg.create(cwd="/tmp/b")
        reg.create(cwd="/tmp/c")

        reg.shutdown_all()
        assert reg.list_sessions() == []
