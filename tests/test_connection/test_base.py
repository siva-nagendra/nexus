"""Tests for connection base classes — timeout registry."""

from nexus.connection.base import (
    _DEFAULT_TIMEOUT,
    _QUERY_TIMEOUT,
    _RENDER_TIMEOUT,
    _resolve_timeout,
)


class TestTimeoutRegistry:
    def test_query_commands_from_registry(self):
        assert _resolve_timeout("system.echo", None) == _QUERY_TIMEOUT
        assert _resolve_timeout("editor.get_viewport_info", None) == _QUERY_TIMEOUT
        assert _resolve_timeout("actor.find", None) == _QUERY_TIMEOUT

    def test_render_commands_from_registry(self):
        assert _resolve_timeout("mrq.submit_job", None) == _RENDER_TIMEOUT
        assert _resolve_timeout("lighting.bake_lighting", None) == _RENDER_TIMEOUT

    def test_mutation_commands_default(self):
        assert _resolve_timeout("actor.spawn", None) == _DEFAULT_TIMEOUT
        assert _resolve_timeout("actor.set_transform", None) == _DEFAULT_TIMEOUT

    def test_explicit_timeout_overrides(self):
        assert _resolve_timeout("actor.find", 42.0) == 42.0

    def test_heuristic_fallback_for_unknown_queries(self):
        # Commands with get_/find_/list_ prefix should be detected as queries
        assert _resolve_timeout("custom.get_data", None) == _QUERY_TIMEOUT
        assert _resolve_timeout("custom.find_items", None) == _QUERY_TIMEOUT
        assert _resolve_timeout("custom.list_entries", None) == _QUERY_TIMEOUT
