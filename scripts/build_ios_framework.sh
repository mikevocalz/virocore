#!/bin/bash
# Build the supported device framework and stage the exact CI artifact.
set -euo pipefail
REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$REPO_ROOT/ios"
xcodebuild \
  -workspace ViroRenderer.xcworkspace \
  -scheme ViroKit \
  -sdk iphoneos \
  -configuration Release \
  -derivedDataPath "$REPO_ROOT/ios/build/derived-data" \
  IPHONEOS_DEPLOYMENT_TARGET=13.0
FRAMEWORK="$REPO_ROOT/ios/build/derived-data/Build/Products/Release-iphoneos/ViroKit.framework"
test -s "$FRAMEWORK/ViroKit"
test -d "$FRAMEWORK/Headers"
mkdir -p "$REPO_ROOT/ios/dist"
# Replace the staged framework so removed headers cannot survive a rebuild.
rm -rf "$REPO_ROOT/ios/dist/ViroKit.framework"
ditto "$FRAMEWORK" "$REPO_ROOT/ios/dist/ViroKit.framework"
cp ViroKit.podspec "$REPO_ROOT/ios/dist/ViroKit.podspec"
test -s "$REPO_ROOT/ios/dist/ViroKit.framework/ViroKit"
