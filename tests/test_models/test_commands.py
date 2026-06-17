"""Tests for command/response models."""

from nexus.models.commands import CommandEnvelope, CommandResult, ResponseEnvelope


class TestCommandEnvelope:
    def test_creates_with_uuid(self):
        cmd = CommandEnvelope(type="actor.spawn", params={"label": "Cube"})
        assert cmd.id  # non-empty
        assert cmd.type == "actor.spawn"

    def test_extra_fields_ignored(self):
        cmd = CommandEnvelope(type="test", params={}, unknown_field="ignored")
        assert cmd.type == "test"


class TestResponseEnvelope:
    def test_from_wire_bytes(self):
        raw = b'{"id": "abc", "success": true, "data": {"x": 1}, "error": null}'
        resp = ResponseEnvelope.from_wire(raw)
        assert resp.success is True
        assert resp.data == {"x": 1}

    def test_extra_fields_ignored(self):
        raw = b'{"id": "x", "success": true, "data": {}, "extra": "ignored"}'
        resp = ResponseEnvelope.from_wire(raw)
        assert resp.success is True


class TestCommandResult:
    def test_success(self):
        r = CommandResult(success=True, data={"actor": "Cube"}, transport_used="native")
        assert r.success
        assert r.data["actor"] == "Cube"

    def test_error(self):
        r = CommandResult(success=False, error="Not found", transport_used="mock")
        assert not r.success
        assert "Not found" in r.error

    def test_success_required(self):
        """success field is required — no default."""
        import pytest

        with pytest.raises(Exception):  # ValidationError
            CommandResult(data={})  # type: ignore[call-arg]

    def test_error_code_field(self):
        r = CommandResult(
            success=False,
            error="Not found",
            error_code="ACTOR_NOT_FOUND",
            transport_used="native",
        )
        assert r.error_code == "ACTOR_NOT_FOUND"

    def test_duration_ms_field(self):
        r = CommandResult(success=True, duration_ms=42.5)
        assert r.duration_ms == 42.5
