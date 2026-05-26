---
name: verify-on-device
description: Build the camera-fast Android app, install it on a connected device via adb, launch CameraActivity, capture a screenshot, and read the PNG so the result can be analyzed visually. Use when verifying UI changes (edge-to-edge, layout, theming) end-to-end on real hardware.
---

# verify-on-device

End-to-end "does this change actually look right?" loop for this repo. Each step has a known gotcha worth following exactly — they cost real time the first time around.

## Preconditions

- A device is connected via USB and `adb devices` shows it as `device` (not `unauthorized`).
- The `:app` module has been edited (Kotlin / themes / manifest). Native C++ does not need to have been touched — incremental NDK build is fine.

## Step 1 — Identify the device

```bash
adb devices
adb shell getprop ro.product.cpu.abi   # e.g. arm64-v8a
adb shell getprop ro.build.version.sdk
adb shell getprop ro.product.model
```

Note the ABI. You will pass it to gradle in Step 2.

## Step 2 — Build & install for the device's ABI ONLY

**Do not run the full `./gradlew assembleDebug`.** The repo ships `libs/shaderc/c++_static/arm64-v8a/libshaderc.a` and `…/x86_64/libshaderc.a`, but **not** an `armeabi-v7a` copy. A no-filter build fails with:

> ninja: error: '../../../../../libs/shaderc/c++_static/armeabi-v7a/libshaderc.a', missing and no known rule to make it

Limit the native build via the standard "inject build ABI" property:

```bash
./gradlew :app:installDebug -Pandroid.injected.build.abi=arm64-v8a --console=plain
```

Replace `arm64-v8a` with whatever Step 1 printed.

If the device is `x86_64` (emulator), use that. If it's `armeabi-v7a`, you must first commit/restore the missing prebuilt — that's a separate concern outside this skill.

## Step 3 — Grant camera permission & launch CameraActivity

The activity calls `finish()` if the permission prompt is declined, and the prompt only appears on first launch. Grant it explicitly so the activity opens straight into the preview:

```bash
adb shell pm grant com.dz.camerafast android.permission.CAMERA
adb shell am force-stop com.dz.camerafast
adb shell am start -n com.dz.camerafast/.CameraActivity
```

`force-stop` is important when iterating — without it, configuration may stick from the previous run (camera mode, lens facing).

## Step 4 — Capture a screenshot

Give the app ~2-3 seconds to bind cameras and lay out, then capture.

### Single-display devices (most phones)

```bash
sleep 3
adb exec-out screencap -p > /tmp/dz_e2e.png
```

### Multi-display devices (foldables like Samsung Z Fold/Flip)

`adb exec-out screencap -p` on a multi-display device prints a **warning to stdout before the PNG bytes**, corrupting the file. The file ends up looking like text starting with:

> [Warning] Multiple displays were found, but no display id was specified! …

Always pass `-d <display-id>` on foldables. Discover ids via:

```bash
adb shell dumpsys SurfaceFlinger --display-id
# Display 4630946474867211650 (HWC display 0): port=130 …   ← inner / main screen
# Display 4630946213010294403 (HWC display 3): port=131 …   ← outer / cover screen
```

Pick the active one (where the user is looking — for Z Fold4 cover screen is HWC 3) and capture:

```bash
adb exec-out screencap -d <display-id> -p > /tmp/dz_e2e.png
file /tmp/dz_e2e.png   # MUST report "PNG image data, … 8-bit/color RGBA"
```

If both displays are uncertain, capture both: `…_inner.png` and `…_outer.png`.

## Step 5 — Read the image and analyze

Use the `Read` tool on the PNG path. Claude will see the image directly.

Things worth checking visually for **UI-targeted changes** in this app:
- Top "CAMERA_X" / "CAMERA_2" header — does it sit at the correct inset? Does its dark-gray background extend behind a transparent status bar (edge-to-edge) or stop below it?
- Camera preview boxes (OpenGL on top, Vulkan below) — drawing? clipped? aspect right?
- Bottom button Row — tappable and clear of the gesture nav?
- Dark vs light system bar icons — legible against the background under them?

For **non-UI changes** (renderer perf, hardware-buffer plumbing), a screenshot is weak evidence — prefer a Perfetto trace instead and ignore this skill.

## Quick one-shot

For an arm64 phone with one display, the whole loop:

```bash
ABI=$(adb shell getprop ro.product.cpu.abi | tr -d '\r')
./gradlew :app:installDebug -Pandroid.injected.build.abi="$ABI" --console=plain
adb shell pm grant com.dz.camerafast android.permission.CAMERA
adb shell am force-stop com.dz.camerafast
adb shell am start -n com.dz.camerafast/.CameraActivity
sleep 3
adb exec-out screencap -p > /tmp/dz_e2e.png
file /tmp/dz_e2e.png
```

Then `Read /tmp/dz_e2e.png`.

## Gotchas recap

1. **`armeabi-v7a` build fails** — always pass `-Pandroid.injected.build.abi=<device-abi>`.
2. **Multi-display devices corrupt `screencap` output** — pass `-d <display-id>`.
3. **`finish()` on permission denial** — pre-grant `android.permission.CAMERA` via `pm grant`.
4. **State carries over between launches** — `force-stop` before `am start` when iterating.
5. **CMake re-runs on Kotlin-only changes** are cheap; don't avoid `installDebug` thinking it'll rebuild C++ — it won't unless C++ sources changed.
