# Building ViroKit for iOS

CI builds the shared `ViroKit` scheme with Xcode 16.4 on macOS 15.
Install Xcode command-line tools and CocoaPods, then from the repository root:

```sh
cd ios
pod install
cd ..
bash scripts/build_ios_framework.sh
```

The script builds Release for `iphoneos`, checks that the binary and headers
exist, and stages `ios/dist/ViroKit.framework` plus `ios/dist/ViroKit.podspec`.
CI uploads that directory as `virokit-iphoneos` and fails if the artifact is
missing. The artifact is an iPhone/iPad device framework, not a simulator or
multi-platform XCFramework. The existing Xcode copy phase also copies to the
sibling `viro/ios/dist/ViroRenderer` directory; that is not the CI artifact path.

The Fastlane `virorender_viroreact_virokit` lane installs pods and invokes the
same script. Its framework-only lane requires pods to be installed first.
The old standalone static-library scheme no longer exists; its legacy lane
now fails explicitly rather than swallowing a failed build. Simulator slices
require compatible simulator builds of every vendored dependency and are not
produced by this device build.

`ReactVisionCCA` is an optional proprietary dependency on iOS. References in
the cloud-anchor provider to `scripts/build_ios.sh` refer to the sibling
`reactvisioncca` repository's deployment script. They are not instructions to
build ViroKit. Without that dependency, the iOS cloud-anchor provider reports
that it is unavailable. See `ios/build_visionos.sh` for the distinct visionOS
build and its dependency requirements.
