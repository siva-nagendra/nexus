"""Tests for common Pydantic models."""

import pytest
from pydantic import ValidationError

from nexus.models.common import ActorRef, Color, Rotator, Transform, Vector3


class TestVector3:
    def test_defaults(self):
        v = Vector3()
        assert v.x == 0.0 and v.y == 0.0 and v.z == 0.0

    def test_to_list(self):
        v = Vector3(x=1, y=2, z=3)
        assert v.to_list() == [1.0, 2.0, 3.0]

    def test_from_list(self):
        v = Vector3.from_list([10, 20, 30])
        assert v.x == 10.0 and v.y == 20.0 and v.z == 30.0


class TestRotator:
    def test_defaults(self):
        r = Rotator()
        assert r.pitch == 0.0

    def test_to_list(self):
        r = Rotator(pitch=45, yaw=90, roll=0)
        assert r.to_list() == [45.0, 90.0, 0.0]


class TestTransform:
    def test_defaults(self):
        t = Transform()
        assert t.location.x == 0.0
        assert t.scale.x == 1.0


class TestColor:
    def test_defaults(self):
        c = Color()
        assert c.a == 1.0

    def test_clamped(self):
        with pytest.raises(ValidationError):
            Color(r=2.0)


class TestActorRef:
    def test_requires_at_least_one(self):
        with pytest.raises(ValidationError):
            ActorRef()

    def test_with_path(self):
        ref = ActorRef(actor_path="/Game/Maps/Main.Main:PersistentLevel.Cube_0")
        assert ref.actor_path.startswith("/Game")

    def test_with_label(self):
        ref = ActorRef(actor_label="MyCube")
        assert ref.actor_label == "MyCube"
