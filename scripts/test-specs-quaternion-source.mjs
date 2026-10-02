#!/usr/bin/env node
import { readFile } from "node:fs/promises";
import path from "node:path";
import { fileURLToPath } from "node:url";

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");
const source = await readFile(path.join(root, "ViroRenderer", "VROQuaternion.cpp"), "utf8");

const start = source.indexOf("VROQuaternion &VROQuaternion::set(float x, float y, float z)");
if (start < 0) {
  throw new Error("Could not find VROQuaternion::set(float,float,float).");
}

const body = source.slice(start, source.indexOf("}", start) + 1);
const required = [
  "angle = x * 0.5;",
  "const double sr = sin(angle);",
  "const double cr = cos(angle);",
  "angle = y * 0.5;",
  "const double sp = sin(angle);",
  "const double cp = cos(angle);",
  "angle = z * 0.5;",
  "const double sy = sin(angle);",
  "const double cy = cos(angle);",
  "const double cpcy = cp * cy;",
  "const double spcy = sp * cy;",
  "const double cpsy = cp * sy;",
  "const double spsy = sp * sy;",
  "X = (float)(sr * cpcy - cr * spsy);",
  "Y = (float)(cr * spcy + sr * cpsy);",
  "Z = (float)(cr * cpsy - sr * spcy);",
  "W = (float)(cr * cpcy + sr * spsy);",
  "return normalize();",
];

for (const line of required) {
  if (!body.includes(line)) {
    throw new Error(
      "VROQuaternion Euler composition changed; update the backend semantic contract intentionally. Missing: " +
        line
    );
  }
}

console.log("VROQuaternion source composition matches portable backend contract.");
