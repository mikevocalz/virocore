#!/usr/bin/env python3
import json
import math
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
FIXTURE = ROOT / "tests" / "xr_backend" / "conformance.json"

def close(a, b, eps=1e-6):
    return abs(a - b) <= eps

def assert_vec(actual, expected, eps=1e-6):
    if len(actual) != len(expected):
        raise AssertionError(f"length mismatch: {actual} vs {expected}")
    for i, (a, b) in enumerate(zip(actual, expected)):
        if not close(float(a), float(b), eps):
            raise AssertionError(f"vector mismatch at {i}: {actual} vs {expected}")

def euler_xyz_to_quat(rotation):
    x, y, z = [math.radians(v) * 0.5 for v in rotation]
    sx, cx = math.sin(x), math.cos(x)
    sy, cy = math.sin(y), math.cos(y)
    sz, cz = math.sin(z), math.cos(z)
    return [
        sx * cy * cz - cx * sy * sz,
        cx * sy * cz + sx * cy * sz,
        cx * cy * sz - sx * sy * cz,
        cx * cy * cz + sx * sy * sz,
    ]

def main():
    data = json.loads(FIXTURE.read_text())

    assert data["schemaVersion"] == 1
    cs = data["coordinateSystem"]
    assert cs == {
        "handedness": "right-handed",
        "right": "+X",
        "up": "+Y",
        "forward": "-Z",
    }

    units = data["units"]
    assert units["position"] == "meters"
    assert units["distance"] == "meters"
    assert units["polylineWidth"] == "meters"
    assert units["portableRotation"] == "quaternion-xyzw"

    transforms = {entry["name"]: entry for entry in data["transforms"]}
    forward = transforms["forward-one-and-half-meters"]
    assert_vec(
        [value * 100 for value in forward["positionMeters"]],
        forward["lensCentimeters"],
    )

    for name in ("yaw-positive-90", "mixed-xyz-20-30-40"):
        rotation = transforms[name]
        assert len(rotation["eulerDegreesXYZ"]) == 3
        assert len(rotation["quaternionXYZW"]) == 4
        expected_quat = euler_xyz_to_quat(rotation["eulerDegreesXYZ"])
        assert_vec(expected_quat, rotation["quaternionXYZW"])
        norm = math.sqrt(sum(v * v for v in rotation["quaternionXYZW"]))
        assert close(norm, 1.0)

    interaction = data["interaction"]
    required_phases = {
        "hover-enter", "hover-move", "hover-exit",
        "select-start", "select-end", "drag", "cancel",
    }
    assert set(interaction["phases"]) == required_phases
    assert "gaze" in interaction["sources"]
    assert "pinch" in interaction["sources"]

    world = data["worldQuery"]
    native_m = [value / 100 for value in world["nativeExample"]["lensCentimeters"]]
    assert_vec(native_m, world["result"]["positionMeters"])
    ox, oy, oz = world["request"]["originMeters"]
    px, py, pz = world["result"]["positionMeters"]
    distance = math.sqrt((px-ox)**2 + (py-oy)**2 + (pz-oz)**2)
    assert close(distance, world["result"]["distanceMeters"])

    line = data["polyline"]
    assert len(line["pointsMeters"]) >= 2
    assert line["widthMeters"] > 0
    radius_cm = (line["widthMeters"] * 100) / 2
    assert close(radius_cm, line["lensRadiusCentimeters"])

    capabilities = data["capabilities"]
    assert len(capabilities) == len(set(capabilities))
    for required in (
        "world-query",
        "surface-placement",
        "gaze-targeting",
        "volumetric-line",
    ):
        assert required in capabilities

    print("XR backend conformance fixture passed")
    return 0

if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:
        print(f"XR backend conformance failed: {exc}", file=sys.stderr)
        raise
