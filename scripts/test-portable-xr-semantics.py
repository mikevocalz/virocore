#!/usr/bin/env python3
import json
import math
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
FIXTURE = ROOT / "tests" / "portable_xr" / "semantic_contract.json"

def close(a, b, eps=1e-9):
    return abs(a - b) <= eps

def euler_xyz_degrees_to_quaternion(rotation):
    x, y, z = [value * math.pi / 180.0 for value in rotation]
    cx, sx = math.cos(x / 2.0), math.sin(x / 2.0)
    cy, sy = math.cos(y / 2.0), math.sin(y / 2.0)
    cz, sz = math.cos(z / 2.0), math.sin(z / 2.0)
    return [
        sx * cy * cz - cx * sy * sz,
        cx * sy * cz + sx * cy * sz,
        cx * cy * sz - sx * sy * cz,
        cx * cy * cz + sx * sy * sz,
    ]

data = json.loads(FIXTURE.read_text())

assert data["version"] == 1
assert data["coordinateSystem"]["publicPositionUnit"] == "meters"
assert data["coordinateSystem"]["metersToCentimeters"] == 100

position = data["cases"]["position"]
assert [value * 100 for value in position["public"]] == position["lens"]

rotation = data["cases"]["rotationY90"]
actual_quat = euler_xyz_degrees_to_quaternion(rotation["eulerDegrees"])
for actual, expected in zip(actual_quat, rotation["quaternion"]):
    assert close(actual, expected), (actual, expected)

hit = data["cases"]["worldQuery"]
converted = [value / 100 for value in hit["lensCentimeters"]]
for actual, expected in zip(converted, hit["portableMeters"]):
    assert close(actual, expected), (actual, expected)

polyline = data["cases"]["polyline"]
assert close(polyline["thicknessMeters"] / 2, polyline["radiusMeters"])
assert close(polyline["radiusMeters"] * 100, polyline["radiusCentimeters"])

expected_phases = {
    "hover-enter",
    "hover-move",
    "hover-exit",
    "select-start",
    "select-end",
    "drag",
    "cancel",
}
assert set(data["interactionPhases"]) == expected_phases

assert len(data["requiredCapabilities"]) == len(set(data["requiredCapabilities"]))

print("portable XR semantic contract: PASS")
