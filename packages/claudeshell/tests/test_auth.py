"""Tests for claudeshell.auth — token validation."""

from __future__ import annotations

from claudeshell.auth import validate_token


class TestValidateToken:
    """Unit tests for validate_token — constant-time comparison."""

    def test_matching_tokens(self):
        """Should return True when tokens match exactly."""
        assert validate_token("my-secret-token-123", "my-secret-token-123") is True

    def test_mismatched_tokens(self):
        """Should return False when tokens differ."""
        assert validate_token("wrong-token", "correct-token") is False

    def test_none_provided(self):
        """Should return False when provided is None."""
        assert validate_token(None, "expected-token") is False

    def test_none_expected(self):
        """Should return False when expected is None."""
        assert validate_token("provided-token", None) is False

    def test_both_none(self):
        """Should return False when both are None."""
        assert validate_token(None, None) is False

    def test_empty_provided(self):
        """Should return False when provided is empty string."""
        assert validate_token("", "expected-token") is False

    def test_empty_expected(self):
        """Should return False when expected is empty string."""
        assert validate_token("provided-token", "") is False

    def test_both_empty(self):
        """Should return False when both are empty strings."""
        assert validate_token("", "") is False
