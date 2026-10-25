# OpenXR render quality

The OpenXR renderer (`ViroViewOpenXR`, Quest and PICO) renders each eye into a
swapchain larger than the runtime's recommended size, and applies fixed
foveation (`XR_FB_foveation`) once those swapchains exist. Both are configurable.
The defaults are 1.3x per axis and `LOW` dynamic foveation.

There is no JS runtime API. Configuration is native and build-time:

| Layer | Where | Sets |
| --- | --- | --- |
| Expo config plugin | `android.openxr` in the `@reactvision/react-viro` plugin options | manifest meta-data below |
| AndroidManifest | `<meta-data>` under `<application>` | startup defaults |
| Java | `ViroViewOpenXR.setRenderQuality(OpenXRRenderQuality)` | startup override |
| Java | `ViroViewOpenXR.setFoveationLevel(int, boolean)` | foveation at runtime |

## Shape

### Config plugin (viro)

```json
["@reactvision/react-viro", {
  "android": {
    "xRMode": ["QUEST"],
    "openxr": {
      "eyeBufferScale": 1.5,
      "foveation": { "level": "medium", "dynamic": false }
    }
  }
}]
```

```ts
openxr?: {
  /** Per-axis multiplier on the recommended eye-buffer size, 1.0 to 3.0. Default 1.3. */
  eyeBufferScale?: number;
  foveation?: {
    level: "off" | "low" | "medium" | "high";  // default "low"
    dynamic?: boolean;                          // default true
  };
};
```

Prebuild fails when a value is out of range or the wrong type, and when
`android.openxr` is set but `android.xRMode` contains neither `"QUEST"` nor
`"PICO"`. Omitted fields write no meta-data, so the renderer default applies.

### AndroidManifest

```xml
<meta-data android:name="com.viro.openxr.eyeBufferScale"   android:value="1.5"/>
<meta-data android:name="com.viro.openxr.foveationLevel"   android:value="medium"/>
<meta-data android:name="com.viro.openxr.foveationDynamic" android:value="false"/>
```

`OpenXRRenderQualityManifest.read(Context)` reads these. A missing key keeps
its default. An unreadable value (not a number, an unknown level name, not a
boolean) also keeps its default and logs a warning under the
`VRORendererOpenXR` tag; it does not stop the renderer starting. The plugin
validates values, so this only happens with a hand-edited manifest.

### Java

```java
public final class OpenXRRenderQuality {
    public static final OpenXRRenderQuality DEFAULT;  // 1.3f, LOW, dynamic
    public OpenXRRenderQuality(float eyeBufferScale,
                               OpenXRFoveationLevel foveationLevel,
                               boolean foveationDynamic);
    public float getEyeBufferScale();
    public OpenXRFoveationLevel getFoveationLevel();
    public boolean isFoveationDynamic();
}

public enum OpenXRFoveationLevel { OFF, LOW, MEDIUM, HIGH; public int getCode(); }

// ViroViewOpenXR
public void setRenderQuality(OpenXRRenderQuality renderQuality); // startup-only
public OpenXRRenderQuality getRenderQuality();
public void setFoveationLevel(int level, boolean dynamic);       // runtime
```

The constructor throws `IllegalArgumentException` for a NaN or infinite scale.
A finite scale below 1.0 is accepted and clamps to 1.0 in the renderer.

Precedence, highest first: `setRenderQuality()`, then the manifest, then
`OpenXRRenderQuality.DEFAULT`. A level passed to `setFoveationLevel()` before
startup is applied right after the startup level, so it wins.

## Call sites

Default. Nothing configured: each eye renders at 1.3x the recommended size,
clamped to the runtime maximum, with `LOW` dynamic foveation.

```java
ViroViewOpenXR view = new ViroViewOpenXR(activity, startupListener);
```

Override. Set before the view attaches to its window; the native renderer is
created on the first resume of the Activity that hosts the view.

```java
ViroViewOpenXR view = new ViroViewOpenXR(activity, startupListener);
view.setRenderQuality(new OpenXRRenderQuality(1.6f, OpenXRFoveationLevel.MEDIUM, false));
```

Clamped or unsupported. The request is honoured as far as the runtime allows
and the log shows what changed. On a runtime whose maximum width is 1.4x the
recommended width, a 2.0 request renders at 1.4x on that axis and 2.0x on any
axis with room. A runtime without `XR_FB_foveation` renders unfoveated and
logs nothing about foveation levels. Calling the setter too late throws:

```java
view.setRenderQuality(new OpenXRRenderQuality(2.0f, OpenXRFoveationLevel.HIGH, true));
// after startup:
// IllegalStateException: setRenderQuality() is startup-only: the OpenXR session
// already created its eye swapchains. ...
view.setFoveationLevel(OpenXRFoveationLevel.HIGH.getCode(), true);  // works at runtime
```

## Startup vs runtime

The eye-buffer scale is startup-only. `VROSceneRendererOpenXR` creates the
session in its constructor, and `createSwapchains()` creates both eye
swapchains once, inside that call. They are destroyed only when the session is
torn down, and a `ViroViewOpenXR` creates its native renderer once. Nothing in
between recreates them, so a scale change after startup would need a new
session. `setRenderQuality()` therefore throws `IllegalStateException` once the
renderer exists rather than storing a value that would never apply.

Foveation has two parts. The startup level is applied right after the
swapchains are created. `setFoveationLevel()` rebuilds the foveation profile on
the live swapchains through `xrUpdateSwapchainFB`, so it works at any time.

## Clamp semantics

`VROResolveEyeBufferSize()` in
`android/sharedCode/src/main/cpp/VROOpenXRRenderQuality.h`:

1. A NaN or infinite request is rejected and 1.3 is used instead.
2. A finite request below 1.0, including 0 and negatives, clamps to 1.0.
3. Each axis clamps independently to `maxImageRect / recommendedImageRect`.
4. The result never exceeds `maxImageRect`, even if a runtime reports a max
   smaller than its recommended size.

`scripts/test-eye-buffer-scale.sh` covers each rule on the host.

## Log line

One `ALOGI` line per eye, under the `VRORendererOpenXR` tag, before the
swapchain is created:

```
Eye 0: recommended 1680x1760, max 3360x3520, scale requested 1.300, applied 1.300x1.300, size requested 2184x2288, clamped 2184x2288, rendering 2184x2288
```

- `scale requested`: the resolved request. A rejected non-finite value shows
  the default followed by `(non-finite request rejected)`.
- `applied`: the scale actually used on X and Y after clamping.
- `size requested`: recommended size times the requested scale, before clamping.
- `clamped`: the size after clamping.
- `rendering`: the size the eye swapchain is created with.

`ViroViewOpenXR` also logs the startup `OpenXRRenderQuality` and where it came
from (`setRenderQuality` or `manifest/default`) just before creating the renderer.
