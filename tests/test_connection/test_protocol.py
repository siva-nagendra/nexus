"""Tests for wire protocol helpers (length-prefixed framing)."""

import struct

import orjson

from nexus.connection.protocol import (
    LENGTH_PREFIX_FORMAT,
    LENGTH_PREFIX_SIZE,
    decode_length_prefix,
    decode_response,
    encode_command,
    encode_length_prefix,
)


class TestEncodeCommand:
    def test_returns_id_and_frame(self):
        cmd_id, frame = encode_command("actor.spawn", {"label": "Cube"})
        assert cmd_id  # non-empty UUID
        assert isinstance(frame, bytes)
        # First 4 bytes are length prefix
        payload_len = struct.unpack(LENGTH_PREFIX_FORMAT, frame[:LENGTH_PREFIX_SIZE])[0]
        payload = frame[LENGTH_PREFIX_SIZE:]
        assert len(payload) == payload_len
        parsed = orjson.loads(payload)
        assert parsed["type"] == "actor.spawn"
        assert parsed["id"] == cmd_id

    def test_empty_params(self):
        _, frame = encode_command("editor.undo")
        payload = frame[LENGTH_PREFIX_SIZE:]
        parsed = orjson.loads(payload)
        assert parsed["params"] == {}


class TestDecodeResponse:
    def test_success(self):
        raw = b'{"id": "x", "success": true, "data": {"ok": true}}'
        resp = decode_response(raw)
        assert resp["success"] is True

    def test_error(self):
        raw = b'{"id": "x", "success": false, "error": {"code": "E", "message": "fail"}}'
        resp = decode_response(raw)
        assert resp["success"] is False


class TestLengthPrefix:
    def test_encode_decode_roundtrip(self):
        payload = b'{"test": true}'
        frame = encode_length_prefix(payload)
        assert len(frame) == LENGTH_PREFIX_SIZE + len(payload)
        decoded_len = decode_length_prefix(frame[:LENGTH_PREFIX_SIZE])
        assert decoded_len == len(payload)

    def test_size_constant(self):
        assert LENGTH_PREFIX_SIZE == 4
